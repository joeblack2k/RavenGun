#!/usr/bin/env bash
# De RIO-brug als USB-bord (tools/rio-bridge/rg_rio_bridge.c, de 32-bits build): het lightgun-bord 0c70:0780 beantwoord in het
# geheugen van een i386-game (Jurassic Park; Aliens Armageddon en Walking Dead met een oudere bibliotheek zonder versleuteling).
# Bouwt de brug zoals CMake hem 32-bits bouwt, een nep-game zonder PIE (tests/rio_bridge_usb_unit.c: vier pmrt_usb-functies en
# een xrtea op vaste adressen), zet hun adressen en eerste hele instructies in een tabel en draait de nep-game twee keer met de
# brug in LD_PRELOAD: met `versleuteling adres=... sleutel=...` en met `versleuteling geen`. Daarna:
#   - een tabel met een usb-bord zonder `versleuteling`-regel wordt afgekeurd (de brug doet niets);
#   - een tabel met een verkeerde handtekening haakt niet aan (het bord krijgt geen handvat);
#   - de usb-tabellen in patches/*/rio-invoer.ini worden door de brug aanvaard.
# Er is geen spel nodig. Skip (77) zonder gcc -m32.
# Omgeving: RG_RIO_BRIDGE_SRC = andere brugbron (bv. de versie van voor `versleuteling geen`, om te zien dat de test dan faalt).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-gcc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
command -v nm >/dev/null && command -v objdump >/dev/null || { echo "overgeslagen: geen nm/objdump"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*"; exit 1; }
mkdir -p "$T/inc32/gnu"
[ -f /usr/include/gnu/stubs-32.h ] || : >"$T/inc32/gnu/stubs-32.h"
printf 'int main(void){return 0;}\n' >"$T/probe.c"
"$CC" -m32 -I"$T/inc32" -o "$T/probe" "$T/probe.c" >/dev/null 2>&1 || { echo "overgeslagen: geen 32-bits toolchain (gcc -m32)"; exit 77; }

"$CC" -m32 -shared -fPIC -O2 -Wall -Wextra -fno-stack-protector -D_FILE_OFFSET_BITS=64 -I"$T/inc32" -o "$T/librg_rio_bridge.so" \
  "${RG_RIO_BRIDGE_SRC:-$ROOT/tools/rio-bridge/rg_rio_bridge.c}" -lpthread -lm -ldl 2>"$T/so.err" || { cat "$T/so.err"; fail "de 32-bits brug bouwt niet"; }
[[ ! -s "$T/so.err" ]] || { cat "$T/so.err"; fail "waarschuwingen bij het bouwen van de brug"; }
"$CC" -m32 -O0 -fno-pie -no-pie -fno-stack-protector -fcf-protection=none -Wall -I"$T/inc32" -o "$T/rio_bridge_usb_unit" \
  "$ROOT/tests/rio_bridge_usb_unit.c" || fail "de nep-game bouwt niet"

# adres en eerste hele instructies (samen minstens 5 bytes) van een functie in de nep-game
adres() { nm "$T/rio_bridge_usb_unit" | awk -v s="$1" '$3 == s { print $1 }'; }
sig() {
  objdump -d --disassemble="$1" "$T/rio_bridge_usb_unit" | awk -F'\t' '
    /^ *[0-9a-f]+:\t/ { gsub(/ /, "", $2); s = s $2; if (length(s) >= 10) { print s; exit } }'
}
for f in nep_usb_open nep_usb_lees nep_usb_schrijf nep_usb_sluit nep_xrtea nep_sleutel; do
  a=$(adres "$f"); [[ -n "$a" ]] || fail "symbool $f niet gevonden"
  printf -v "A_$f" '%s' "$a"
done
for f in nep_usb_open nep_usb_lees nep_usb_schrijf nep_usb_sluit nep_xrtea; do
  s=$(sig "$f"); [[ ${#s} -ge 10 ]] || fail "geen handtekening voor $f"
  printf -v "S_$f" '%s' "$s"
done

tabel() {   # $1 = versleutelingsregel, $2 = handtekening van open (standaard de echte)
  cat <<INI
proces      naam=rio_bridge_usb_unit
beeld       bits=8
usb         vid=0c70 pid=0780 interval=4 type=2 lengte=64 som=63
haak        naam=pmrt_usb_open            rol=open    adres=$A_nep_usb_open sig=${2:-$S_nep_usb_open}
haak        naam=pmrt_usb_interrupt_read  rol=lees    adres=$A_nep_usb_lees sig=$S_nep_usb_lees
haak        naam=pmrt_usb_interrupt_write rol=schrijf adres=$A_nep_usb_schrijf sig=$S_nep_usb_schrijf
haak        naam=pmrt_usb_close           rol=sluit   adres=$A_nep_usb_sluit sig=$S_nep_usb_sluit
$1
veld id=7 byte=8 deel=hoog
adcveld ch=19 hoog=14 deel=hoog laag=16
schakelaar bit=4 id=7
kanaal bron=12 ch=19 min=0 max=4095 inverteer=nee
INI
}
draai() {   # $1 = modus (geen|xor), $2 = tabel, $3 = logboek
  head -c 64 /dev/zero >"$T/beeld"
  timeout 20 env LD_PRELOAD="$T/librg_rio_bridge.so" RG_RIO_TABLE="$2" RG_RIO_SHM="$T/beeld" RG_RIO_LOG=1 \
    "$T/rio_bridge_usb_unit" "$1" "$T/beeld" 2>"$3"
}

tabel "versleuteling adres=$A_nep_xrtea sig=$S_nep_xrtea sleutel=$A_nep_sleutel" >"$T/xor.ini"
draai xor "$T/xor.ini" "$T/xor.log" || { cat "$T/xor.log"; fail "usb-bord met versleuteling"; }
grep -q 'heen en terug in orde' "$T/xor.log" || { cat "$T/xor.log"; fail "de versleutelingstoets staat niet in het logboek"; }

tabel "versleuteling geen" >"$T/geen.ini"
draai geen "$T/geen.ini" "$T/geen.log" || { cat "$T/geen.log"; fail "usb-bord zonder versleuteling"; }
grep -q 'zonder versleuteling' "$T/geen.log" || { cat "$T/geen.log"; fail "de tabel meldt niet dat er geen versleuteling is"; }
grep -q 'aangehaakt' "$T/geen.log" || { cat "$T/geen.log"; fail "geen 'aangehaakt' in het logboek"; }

# zonder versleutelingsregel: afgekeurd, de brug doet niets (geen handvat)
tabel "" >"$T/zonder.ini"
draai geen "$T/zonder.ini" "$T/zonder.log" && fail "een usb-tabel zonder versleutelingsregel werd toch aanvaard"
grep -q 'afgekeurd' "$T/zonder.log" || { cat "$T/zonder.log"; fail "een usb-tabel zonder versleutelingsregel is niet afgekeurd"; }

# een verkeerde handtekening: geen haak, het bord krijgt geen handvat
tabel "versleuteling geen" "90909090909090" >"$T/fout.ini"
draai geen "$T/fout.ini" "$T/fout.log" && fail "met een verkeerde handtekening haakte de brug toch aan"
grep -q 'geen handvat' "$T/fout.log" || { cat "$T/fout.log"; fail "verkeerde handtekening: onverwachte uitkomst"; }

# de usb-tabellen van de games: de brug moet ze aanvaarden
n=0
for t in "$ROOT"/patches/*/rio-invoer.ini; do
  [[ -f "$t" ]] && grep -q '^usb ' "$t" || continue
  sed 's/^proces .*/proces naam=rio_bridge_usb_unit/' "$t" >"$T/game.ini"
  : >"$T/game.log"
  LD_PRELOAD="$T/librg_rio_bridge.so" RG_RIO_TABLE="$T/game.ini" RG_RIO_SHM="$T/beeld" RG_RIO_LOG=1 "$T/probe" 2>"$T/game.log"
  grep -q 'afgekeurd' "$T/game.log" && { cat "$T/game.log"; fail "$t wordt afgekeurd"; }
  n=$((n + 1))
done
echo "usb-tabellen van games aanvaard: $n"
echo "OK"

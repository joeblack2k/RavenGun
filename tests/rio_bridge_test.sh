#!/usr/bin/env bash
# De RIO-brug (tools/rio-bridge/rg_rio_bridge.c): het lightgun-bord van een native Raw Thrills-Unity-game (NERF) uit het bordbeeld
# van de IO-brug. Bouwt de brug zoals CMake hem bouwt, een nep-librio.so in twee builds (tests/rio_bridge_fakelib.c: een met de
# handtekening uit de tabel en int3 erachter, een andere build die de brug met rust moet laten) en de eenheidstest
# (tests/rio_bridge_unit.c), en draait de test met de brug in LD_PRELOAD: echte dlopen, echt aanpassen, echt bordbeeld. Er is geen
# spel nodig. Ook: de tabellen van NERF (patches/NERF/rio-invoer*.ini) moeten geldige tabellen zijn (de brug keurt ze niet af).
# Skip (77) zonder C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
BRON="$ROOT/tools/rio-bridge/rg_rio_bridge.c"
"$CC" -shared -fPIC -O2 -Wall -Wextra -fno-stack-protector -o "$T/librg_rio_bridge.so" "$BRON" -lm -lpthread -ldl 2>"$T/so.err" \
  || { cat "$T/so.err"; echo "FAIL: de brug bouwt niet"; exit 1; }
[[ ! -s "$T/so.err" ]] || { cat "$T/so.err"; echo "FAIL: waarschuwingen bij het bouwen van de brug"; exit 1; }
mkdir -p "$T/goed" "$T/fout"
"$CC" -shared -fPIC -O1 -DRG_GOEDE_BUILD -o "$T/goed/librio.so" "$ROOT/tests/rio_bridge_fakelib.c" || { echo "FAIL: nep-librio (goed)"; exit 1; }
"$CC" -shared -fPIC -O1 -o "$T/fout/librio.so" "$ROOT/tests/rio_bridge_fakelib.c" || { echo "FAIL: nep-librio (fout)"; exit 1; }
"$CC" -O1 -Wall -o "$T/rio_bridge_unit" "$ROOT/tests/rio_bridge_unit.c" -lm -ldl || { echo "FAIL: de eenheidstest bouwt niet"; exit 1; }
cat >"$T/tabel.ini" <<'INI'
bibliotheek naam=librio.so
proces      naam=rio_bridge_unit
beeld       bits=8
functie naam=RIO_Connected  rol=verbonden  sig=488b0579fc210083
functie naam=RIO_SW_Count   rol=schakelaar sig=8d47dc83f8277738
functie naam=RIO_ADC_Val    rol=adc        sig=89ff488d04bf488d
functie naam=RIO_ADC_Val_Ex rol=adc_ex     sig=8d47ed83f8037740
schakelaar bit=7 id=15
schakelaar bit=6 id=7
schakelaar bit=9 id=11 min=300
kanaal bron=12 ch=19 min=0 max=4095 inverteer=nee
kanaal bron=13 ch=20 min=0 max=4095 inverteer=ja
INI
head -c 64 /dev/zero >"$T/beeld"; : >"$T/probe"
LD_PRELOAD="$T/librg_rio_bridge.so" RG_RIO_TABLE="$T/tabel.ini" RG_RIO_SHM="$T/beeld" RG_RIO_PROBE="$T/probe" RG_RIO_LOG=1 \
  "$T/rio_bridge_unit" "$T/goed/librio.so" "$T/fout/librio.so" "$T/beeld" "$T/probe" >"$T/uit" 2>"$T/log"
rc=$?
cat "$T/uit"
(( rc == 0 )) || { echo "--- log van de brug ---"; cat "$T/log"; echo "FAIL: eenheidstest (rc $rc)"; exit 1; }
grep -q 'NIET aangepast' "$T/log" || { cat "$T/log"; echo "FAIL: de brug meldt de andere build niet"; exit 1; }
# De tabellen van NERF: elk moet door de brug worden aanvaard (geen "afgekeurd").
for tabel in "$ROOT"/patches/NERF/rio-invoer*.ini; do
  [[ -f "$tabel" ]] || continue
  sed 's/^proces .*/proces naam=rio_bridge_unit/' "$tabel" >"$T/nerf.ini"
  : >"$T/nerf.log"
  LD_PRELOAD="$T/librg_rio_bridge.so" RG_RIO_TABLE="$T/nerf.ini" RG_RIO_SHM="$T/beeld" RG_RIO_LOG=1 \
    "$T/rio_bridge_unit" 2>"$T/nerf.log" >/dev/null
  grep -q 'afgekeurd' "$T/nerf.log" && { cat "$T/nerf.log"; echo "FAIL: $tabel wordt afgekeurd"; exit 1; }
  grep -q "tabel $T/nerf.ini" "$T/nerf.log" || { cat "$T/nerf.log"; echo "FAIL: $tabel niet gelezen"; exit 1; }
done
echo "OK"

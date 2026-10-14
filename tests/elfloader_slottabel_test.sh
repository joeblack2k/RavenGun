#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# rg-elfloader en de SLOTTABEL per game (RG_ELF_SLOT_TABLE), voor de gepakte klasse.
#
# Een beeld dat uit een draaiende kast is gedumpt (Jurassic Park: 577 stubs) heeft eerste-PLT-stubs die 0xdeadbeef
# pushen en GOT-slots met het adres dat de functie OP DE KAST had; de namen staan niet meer in het bestand. De tabel
# (patches/<Game>/dropin/eerste-plt-slots.tsv) geeft per slot de naam, en de lader schrijft dlsym(naam) in elk slot dat
# NOG de stale waarde uit de tabel bevat. Deze test bouwt een synthetische gast met drie zulke stubs en controleert:
#   1. tegenproef: ZONDER tabel blijven alle drie de slots stale (de gast roept dan geen stub aan);
#   2. MET tabel: het slot waarvan de stale waarde klopt krijgt de functie, en de aanroep via de stub komt aan
#      (de gast schrijft de teruggegeven waarde in zijn markerbestand);
#   3. een rij waarvan de stale waarde NIET klopt raakt het slot niet (een tabel van een andere game-versie doet niets);
#   4. een naam die geen bibliotheek kent wordt GEMELD en het slot blijft staan; de lader gaat door;
#   5. een rij met een slot buiten het beeld wordt geteld en overgeslagen;
#   6. een tabel die niet te openen is, is FATAAL (rc 127) en de gast start niet: stil doorgaan zonder namen is fout.
# Geen game, geen NAS. Skip (77) zonder gcc -m32.
#
# Omgeving: RG_ELFLOADER_SRC = andere lader-bron (bv. de versie zonder slottabel, om te zien dat de test dan faalt).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${RG_ELFLOADER_SRC:-$ROOT/tools/rg-elfloader/rg_elfloader.c}
CC=${CC:-gcc}
command -v "$CC" >/dev/null 2>&1 || { echo "geen $CC"; exit 77; }
command -v nm >/dev/null 2>&1 || { echo "geen nm"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT

printf 'int main(void){return 0;}\n' > "$T/probe.c"
INC=""
[ -d "$ROOT/tools/rg-elfloader/inc" ] && INC="-I$ROOT/tools/rg-elfloader/inc"
"$CC" -m32 $INC -o "$T/probe" "$T/probe.c" >/dev/null 2>&1 || { echo "geen 32-bits toolchain (gcc -m32)"; exit 77; }

fail() { echo "FAIL: $*"; exit 1; }

cat > "$T/fakelib.c" <<'C'
int fakelib_a(void) { return 41; }
int fakelib_b(void) { return 42; }
int fakelib_c(void) { return 3; }
C
"$CC" -m32 $INC -shared -fPIC -o "$T/libfakelib.so" "$T/fakelib.c" || fail "lib bouwen"

# De gast: drie met de hand gemaakte eerste-PLT-stubs zoals in een gedumpt beeld (jmp *<abs slot>; push $0xdeadbeef),
# met in elk slot een stale kast-adres. fakelib_c wordt gewoon geimporteerd, zodat de lib in DT_NEEDED staat.
cat > "$T/gast.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int fakelib_c(void);
extern uint32_t rg_slot_a, rg_slot_b, rg_slot_c;
int rg_stub_a(void);
__asm__(
    ".data\n"
    ".globl rg_slot_a\nrg_slot_a: .long 0xb7001000\n"
    ".globl rg_slot_b\nrg_slot_b: .long 0xb7002000\n"
    ".globl rg_slot_c\nrg_slot_c: .long 0xb7003000\n"
    ".text\n"
    ".globl rg_stub_a\nrg_stub_a: jmp *rg_slot_a\n pushl $0xdeadbeef\n hlt\n"
    ".globl rg_stub_b\nrg_stub_b: jmp *rg_slot_b\n pushl $0xdeadbeef\n hlt\n"
    ".globl rg_stub_c\nrg_stub_c: jmp *rg_slot_c\n pushl $0xdeadbeef\n hlt\n");
int main(void) {
    FILE *f = fopen(getenv("RG_TEST_MARKER") ? getenv("RG_TEST_MARKER") : "/nonexistent/marker", "w");
    if (!f) return 99;
    fprintf(f, "a=%08x b=%08x c=%08x\n", rg_slot_a, rg_slot_b, rg_slot_c);
    if (rg_slot_a != 0xb7001000u)                       /* alleen aanroepbaar als het slot hersteld is */
        fprintf(f, "stub_a=%d\n", rg_stub_a());
    fclose(f);
    return fakelib_c();
}
C
"$CC" -m32 $INC -no-pie -Wl,-Ttext-segment=0x08048000 -Wl,--no-as-needed -o "$T/gast" "$T/gast.c" -L"$T" -lfakelib \
    || fail "gast bouwen"

adr() { nm "$T/gast" | awk -v s="$1" '$3 == s { print "0x" $1 }'; }
STUB_A=$(adr rg_stub_a); STUB_B=$(adr rg_stub_b); STUB_C=$(adr rg_stub_c)
SLOT_A=$(adr rg_slot_a); SLOT_B=$(adr rg_slot_b); SLOT_C=$(adr rg_slot_c)
[ -n "$STUB_A" ] && [ -n "$SLOT_A" ] && [ -n "$SLOT_B" ] && [ -n "$SLOT_C" ] || fail "voorwaarde: stub/slot-adressen niet gevonden"
# Voorwaarde: de stub is echt `ff 25 <abs slot>` (anders test dit de klasse niet)
objdump -d --start-address="$STUB_A" --stop-address="$(printf '0x%x' $((STUB_A + 6)))" "$T/gast" | grep -q 'ff 25' \
    || fail "voorwaarde: rg_stub_a is geen 'jmp *abs32'"

printf '# testtabel\nplt_stub_vaddr\tgot_slot_vaddr\tstale_value_in_dump\tname\textra\n' > "$T/tabel.tsv"
printf '%s\t%s\t0xb7001000\tfakelib_a\tgoed\n'            "$STUB_A" "$SLOT_A" >> "$T/tabel.tsv"
printf '%s\t%s\t0xb7009999\tfakelib_b\tandere stale\n'    "$STUB_B" "$SLOT_B" >> "$T/tabel.tsv"
printf '%s\t%s\t0xb7003000\tbestaat_niet_xyz\tgeen naam\n' "$STUB_C" "$SLOT_C" >> "$T/tabel.tsv"
printf '0x00000010\t0x00000010\t0xb7004000\tfakelib_a\tbuiten beeld\n'        >> "$T/tabel.tsv"

"$CC" -m32 -O2 -Wall -Wextra $INC -Wl,-z,execstack -o "$T/rg-elfloader" "$SRC" -ldl || fail "lader bouwen"
export RG_TEST_MARKER="$T/marker"
export XDG_RUNTIME_DIR="$T"

# --- 1. tegenproef: zonder tabel blijft alles stale en roept de gast niets aan ---
OUT=$(LD_LIBRARY_PATH="$T" "$T/rg-elfloader" "$T/gast" 2>&1); RC=$?
[ "$RC" -eq 3 ] || { echo "$OUT"; fail "zonder tabel: rc=$RC, verwacht 3"; }
grep -q '^a=b7001000 b=b7002000 c=b7003000$' "$T/marker" || { cat "$T/marker"; fail "zonder tabel: de slots zijn niet meer stale"; }
grep -q 'stub_a=' "$T/marker" && fail "zonder tabel: de gast riep de stub toch aan"
echo "$OUT" | grep -q 'slottabel' && fail "zonder tabel: de lader meldt toch een slottabel"
rm -f "$T/marker"

# --- 2..5. met tabel ---
OUT=$(RG_ELF_SLOT_TABLE="$T/tabel.tsv" LD_LIBRARY_PATH="$T" "$T/rg-elfloader" "$T/gast" 2>&1); RC=$?
echo "$OUT"
[ "$RC" -eq 3 ] || fail "met tabel: rc=$RC, verwacht 3"
[ -e "$T/marker" ] || fail "met tabel: de gast draaide niet (geen markerbestand)"
cat "$T/marker"
grep -q '^stub_a=41$' "$T/marker"            || fail "met tabel: de aanroep via stub A kwam niet bij fakelib_a aan (verwacht stub_a=41)"
grep -q '^a=b7001000 ' "$T/marker"           && fail "met tabel: slot A is niet hersteld"
grep -q ' b=b7002000 ' "$T/marker"           || fail "een rij met een ANDERE stale waarde heeft slot B toch geschreven"
grep -q ' c=b7003000$' "$T/marker"           || fail "een onbekende naam heeft slot C toch geschreven"
echo "$OUT" | grep -q 'slottabel .*: 1 slots op naam gezet, 1 naam-niet-gevonden, 1 slot-had-andere-waarde, 0 zwak/0, 1 buiten beeld' \
    || fail "de samenvatting van de slottabel klopt niet (verwacht 1 gezet, 1 niet gevonden, 1 andere waarde, 1 buiten beeld)"
echo "$OUT" | grep -q 'slottabel: bestaat_niet_xyz niet gevonden' || fail "de onbekende naam is niet gemeld"
rm -f "$T/marker"

# --- 6. een tabel die er niet is: fataal, en de gast start niet ---
OUT=$(RG_ELF_SLOT_TABLE="$T/bestaat-niet.tsv" LD_LIBRARY_PATH="$T" "$T/rg-elfloader" "$T/gast" 2>&1); RC=$?
[ "$RC" -eq 127 ] || { echo "$OUT"; fail "ontbrekende tabel: rc=$RC, verwacht 127 (fataal)"; }
[ -e "$T/marker" ] && fail "ontbrekende tabel: de gast is toch gestart"
echo "$OUT" | grep -q 'kan slottabel' || fail "ontbrekende tabel: geen melding"

echo "OK: slottabel (zonder tabel onaangeroerd; met tabel 1 gezet, andere stale en onbekende naam onaangeroerd; ontbrekende tabel fataal)"

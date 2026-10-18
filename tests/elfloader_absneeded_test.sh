#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# rg-elfloader en een DT_NEEDED met een ABSOLUUT pad (Aliens Armageddon, Walking Dead, GhostBusters: "/usr/lib/libcsv.so").
#
# De gast wordt gelinkt tegen een bibliotheek zonder SONAME via haar volle pad, zodat dat pad letterlijk in DT_NEEDED staat.
# Daarna verhuist de bibliotheek (zoals op de kast /usr/lib/libcsv.so, in de sandbox /rg/lib/libcsv.so). Controleert:
#   1. tegenproef: de NEEDED is echt een absoluut pad, en de gewone dynamische linker vindt hem niet meer;
#   2. de lader zoekt de basisnaam in LD_LIBRARY_PATH, meldt dat, en de gast draait met de functie uit de bibliotheek;
#   3. bestaat het absolute pad wel, dan neemt de lader dat (en meldt niets);
#   4. --ontleed-alleen vindt de bibliotheek ook (lib-defs uit het bestand onder de basisnaam).
# Geen game, geen NAS. Skip (77) zonder gcc -m32.
#
# Omgeving: RG_ELFLOADER_SRC = andere lader-bron (bv. de versie zonder deze terugval, om te zien dat de test dan faalt).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${RG_ELFLOADER_SRC:-$ROOT/tools/rg-elfloader/rg_elfloader.c}
CC=${CC:-gcc}
command -v "$CC" >/dev/null 2>&1 || { echo "geen $CC"; exit 77; }
command -v readelf >/dev/null 2>&1 || { echo "geen readelf"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# De probe moet dezelfde kopteksten gebruiken als de synthetische gast en libs: zonder 32-bits
# libc-kopteksten (gnu/stubs-32.h) lukt 'gcc -m32' op een leeg bestand wel, maar het echte werk
# niet. Zo'n systeem slaat deze test dus over in plaats van te falen.
printf '#include <stdio.h>\n#include <unistd.h>\nint main(void){char b[8]; snprintf(b, sizeof b, "%%d", (int)getpid()); return b[0] == 0;}\n' > "$T/probe.c"
INC=""
[ -d "$ROOT/tools/rg-elfloader/inc" ] && INC="-I$ROOT/tools/rg-elfloader/inc"
"$CC" -m32 $INC -o "$T/probe" "$T/probe.c" >/dev/null 2>&1 || { echo "skip: geen volledige 32-bits toolchain (gcc -m32 met 32-bits libc-kopteksten, gnu/stubs-32.h)"; exit 77; }

mkdir -p "$T/kast/usr/lib" "$T/rglib"
printf 'int csv_waarde(void) { return 42; }\n' > "$T/csv.c"
"$CC" -m32 $INC -shared -fPIC -o "$T/kast/usr/lib/libcsv.so" "$T/csv.c" || fail "lib bouwen"
cat > "$T/gast.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
int csv_waarde(void);
int main(void) {
    FILE *f = fopen(getenv("RG_TEST_MARKER") ? getenv("RG_TEST_MARKER") : "/nonexistent/marker", "w");
    if (!f) return 99;
    fprintf(f, "csv=%d\n", csv_waarde());
    fclose(f);
    return 3;
}
C
"$CC" -m32 $INC -no-pie -Wl,-Ttext-segment=0x08048000 -Wl,--no-as-needed -o "$T/gast" "$T/gast.c" "$T/kast/usr/lib/libcsv.so" \
    || fail "gast bouwen"
ABS="$T/kast/usr/lib/libcsv.so"
readelf -dW "$T/gast" | grep -q "NEEDED.*\[$ABS\]" || { readelf -dW "$T/gast" | grep NEEDED; fail "voorwaarde: de NEEDED is geen absoluut pad"; }
"$CC" -m32 -O2 -Wall -Wextra $INC -Wl,-z,execstack -o "$T/rg-elfloader" "$SRC" -ldl || fail "lader bouwen"
export RG_TEST_MARKER="$T/marker" XDG_RUNTIME_DIR="$T"

# --- 3. het absolute pad bestaat: dat wordt genomen, zonder melding ---
OUT=$(LD_LIBRARY_PATH="$T/rglib" "$T/rg-elfloader" "$T/gast" 2>&1); RC=$?
[ "$RC" -eq 3 ] || { echo "$OUT"; fail "absoluut pad aanwezig: rc=$RC, verwacht 3"; }
grep -q '^csv=42$' "$T/marker" || fail "absoluut pad aanwezig: de gast kreeg de functie niet"
echo "$OUT" | grep -q 'bestaat niet' && fail "absoluut pad aanwezig: de lader meldt toch een terugval"
rm -f "$T/marker"

# verhuizen: zoals in de sandbox (de bibliotheek staat onder /rg/lib, niet op het pad van de kast)
mv "$ABS" "$T/rglib/libcsv.so"

# --- 1. tegenproef: de gewone dynamische linker vindt hem niet ---
LD_LIBRARY_PATH="$T/rglib" "$T/gast" >/dev/null 2>&1 && fail "tegenproef: de gast startte zonder lader toch (pad niet absoluut?)"
[ -e "$T/marker" ] && fail "tegenproef: markerbestand zonder lader"

# --- 2. de lader zoekt de basisnaam ---
OUT=$(LD_LIBRARY_PATH="$T/rglib" "$T/rg-elfloader" "$T/gast" 2>&1); RC=$?
echo "$OUT" | grep -v '^$' | head -5
[ "$RC" -eq 3 ] || fail "terugval: rc=$RC, verwacht 3"
grep -q '^csv=42$' "$T/marker" || fail "terugval: de gast kreeg de functie niet (verwacht csv=42)"
echo "$OUT" | grep -q "NEEDED $ABS bestaat niet: gezocht als libcsv.so" || fail "terugval: geen melding"
[ "$(echo "$OUT" | grep -c 'bestaat niet')" -eq 1 ] || fail "terugval: de melding staat er niet precies een keer"
rm -f "$T/marker"

# --- 4. analysemodus ---
OUT=$(LD_LIBRARY_PATH="$T/rglib" "$T/rg-elfloader" --ontleed-alleen "$T/gast" 2>&1); RC=$?
[ "$RC" -eq 0 ] || { echo "$OUT"; fail "--ontleed-alleen: rc=$RC"; }
[ -e "$T/marker" ] && fail "--ontleed-alleen startte de gast"
echo "$OUT" | grep -q "$T/rglib/libcsv.so" || { echo "$OUT"; fail "--ontleed-alleen: de bibliotheek is niet onder de basisnaam gevonden"; }

echo "OK: absolute NEEDED (bestaand pad genomen; anders de basisnaam via LD_LIBRARY_PATH, gemeld; ook in --ontleed-alleen)"

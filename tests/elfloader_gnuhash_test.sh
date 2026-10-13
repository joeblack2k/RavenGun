#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# rg-elfloader en gasten/libs met ALLEEN DT_GNU_HASH (geen DT_HASH), plus de analysemodus --ontleed-alleen.
#
# Echte i386-games hebben soms alleen DT_GNU_HASH; de lader leidde het aantal gast-symbolen toen uitsluitend
# uit DT_HASH af en brak af met "geen DT_HASH" voor er een relocatie was toegepast. Deze test bouwt een
# synthetische gast (ET_EXEC, 0x08048000, -no-pie) en een lib, beide met --hash-style=gnu, en controleert:
#   1. eenheidstest (tests/elfloader_gnuhash_unit.c): tel_dynsyms() + parse_lib_file() incl. de harde fouten;
#   2. de echte lader: `rg-elfloader --ontleed-alleen <gast>` telt de symbolen, vindt de lib, past relocaties
#      toe en STARTT DE GAST NIET (de gast schrijft bij start een markerbestand; dat mag er niet zijn).
# Geen game, geen NAS. Skip (77) zonder gcc -m32.
#
# Omgeving: RG_ELFLOADER_SRC = andere lader-bron (bv. de oude versie, om te zien dat de test dan faalt).
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${RG_ELFLOADER_SRC:-$ROOT/tools/rg-elfloader/rg_elfloader.c}
CC=${CC:-gcc}
command -v "$CC" >/dev/null 2>&1 || { echo "geen $CC"; exit 77; }
command -v readelf >/dev/null 2>&1 || { echo "geen readelf"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT

# 32-bits toolchain aanwezig? (anders skip, zoals de lader zelf niet te bouwen is)
printf 'int main(void){return 0;}\n' > "$T/probe.c"
INC=""
[ -d "$ROOT/tools/rg-elfloader/inc" ] && INC="-I$ROOT/tools/rg-elfloader/inc"
"$CC" -m32 $INC -o "$T/probe" "$T/probe.c" >/dev/null 2>&1 || { echo "geen 32-bits toolchain (gcc -m32)"; exit 77; }

fail() { echo "FAIL: $*"; exit 1; }

# --- synthetische lib: drie functies, ALLEEN DT_GNU_HASH; en dezelfde lib met ALLEEN DT_HASH ---
cat > "$T/fakelib.c" <<'C'
int fakelib_a(void) { return 1; }
int fakelib_b(void) { return 2; }
int fakelib_c(void) { return 3; }
C
"$CC" -m32 $INC -shared -fPIC -Wl,--hash-style=gnu  -o "$T/libfakelib.so"      "$T/fakelib.c" || fail "lib (gnu) bouwen"
"$CC" -m32 $INC -shared -fPIC -Wl,--hash-style=sysv -o "$T/libfakelib_sysv.so" "$T/fakelib.c" || fail "lib (sysv) bouwen"

# --- synthetische gast: ET_EXEC op 0x08048000, hangt aan libfakelib, schrijft bij start een markerbestand ---
cat > "$T/gast.c" <<'C'
#include <stdio.h>
int fakelib_a(void); int fakelib_b(void); int fakelib_c(void);
int main(void) {
    FILE *f = fopen(getenv("RG_TEST_MARKER") ? getenv("RG_TEST_MARKER") : "/nonexistent/marker", "w");
    if (f) { fputs("gast gestart\n", f); fclose(f); }
    return fakelib_a() + fakelib_b() + fakelib_c();
}
C
sed -i '1i #include <stdlib.h>' "$T/gast.c"
"$CC" -m32 $INC -no-pie -Wl,--hash-style=gnu -Wl,-Ttext-segment=0x08048000 -o "$T/gast" "$T/gast.c" \
    -L"$T" -lfakelib || fail "gast bouwen"

# Voorwaarde: de gast en de lib hebben GEEN DT_HASH (anders test dit niets)
readelf -d "$T/gast"         | grep -q '(HASH)'     && fail "voorwaarde: gast heeft toch DT_HASH"
readelf -d "$T/gast"         | grep -q '(GNU_HASH)' || fail "voorwaarde: gast heeft geen DT_GNU_HASH"
readelf -d "$T/libfakelib.so" | grep -q '(HASH)'    && fail "voorwaarde: lib heeft toch DT_HASH"
GAST_SYMS=$(readelf --dyn-syms -W "$T/gast" | awk '$1 ~ /^[0-9]+:$/' | wc -l)

# --- 1. eenheidstest (een fout hier stopt de run niet: de echte-lader-stap hieronder toont het gevolg ook) ---
FAILS=0
if "$CC" -m32 $INC -O1 -Wall -Wno-unused-function -DRG_ELFLOADER_ZONDER_MAIN "-DRG_LADER_BRON=\"$SRC\"" \
    -o "$T/unit" "$ROOT/tests/elfloader_gnuhash_unit.c" -ldl; then
    "$T/unit" "$T/libfakelib.so" "$T/libfakelib_sysv.so" || { echo "FAIL: eenheidstest"; FAILS=1; }
else
    echo "FAIL: eenheidstest bouwen (lader-bron zonder RG_ELFLOADER_ZONDER_MAIN?)"; FAILS=1
fi

# --- 2. de echte lader, analysemodus ---
"$CC" -m32 -O2 -Wall -Wextra $INC -Wl,-z,execstack -o "$T/rg-elfloader" "$SRC" -ldl || fail "lader bouwen"
export RG_TEST_MARKER="$T/marker"
export XDG_RUNTIME_DIR="$T"
OUT=$(LD_LIBRARY_PATH="$T" "$T/rg-elfloader" --ontleed-alleen "$T/gast" 2>&1)
RC=$?
echo "$OUT"
[ "$RC" -eq 0 ] || { [ "$FAILS" -eq 0 ] || echo "(eenheidstest faalde ook)"; fail "--ontleed-alleen eindigde met rc=$RC"; }
echo "$OUT" | grep -q "gast-dynsyms: $GAST_SYMS (bron: (DT_STRTAB-DT_SYMTAB)/16" || fail "verwachtte gast-dynsyms: $GAST_SYMS via STRTAB-SYMTAB"
echo "$OUT" | grep -q "libfakelib.so .*STRTAB-SYMTAB"   || fail "libfakelib.so niet via STRTAB-SYMTAB geteld"
echo "$OUT" | grep -q "spel NIET gestart"               || fail "geen 'spel NIET gestart'"
echo "$OUT" | grep -Eq "relocaties: [1-9][0-9]* toegepast" || fail "geen toegepaste relocaties"
echo "$OUT" | grep -q "R_386_JMP_SLOT.*fakelib_a"        || fail "relocatie voor fakelib_a niet gezien"
echo "$OUT" | grep -q "R_386_JMP_SLOT.*fakelib_c"        || fail "relocatie voor fakelib_c niet gezien"
[ -e "$T/marker" ] && fail "de gast is gestart (markerbestand bestaat)"

[ "$FAILS" -eq 0 ] || exit 1
echo "ok: GNU_HASH-only gast + lib, analysemodus start de gast niet"
exit 0

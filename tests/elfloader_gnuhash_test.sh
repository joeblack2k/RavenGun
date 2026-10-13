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
#      Vooraf de tegenproef: in de gewone modus start dezelfde gast WEL en staat het markerbestand er, dus
#      een ontbrekend markerbestand zegt iets. En de samenvatting zegt van elke toegepaste relocatie naar
#      welke module hij wees, en splitst onopgelost in zwak en sterk.
#   3. het plafond van de lib-defs-lijst, tegen de echte lader: een gast die hangt aan een lib met meer
#      symbolen dan de lijst kan houden. --ontleed-alleen moet dat op stderr en in de samenvatting melden;
#      de gewone modus moet weigeren (FATAAL, rc 127) en de gast niet starten. Stil afkappen is fout.
#   4. de analyse moet een gemis ook MELDEN, anders bewijst "0 sterk onopgelost" niets: een lib die niet te
#      vinden is, en een lib waarin een symbool ontbreekt.
#   5. de laadorde: een lib ZONDER eigen DT_NEEDED die leunt op een lib verderop in de lijst van de gast
#      (libGLU.so.1 bij Terminator Salvation: "undefined symbol: floor") moet in een latere ronde laden, in de
#      gewone modus net zo goed als in de analyse. En een lib die nooit laadt blijft in de gewone modus fataal.
#   6. de hulpprocessen van de shim-bouw (64-bits) erven de LD_PRELOAD van het spel niet: geen
#      "cannot be preloaded (wrong ELF class)" in de uitvoer, en de gast ziet de variabele nog wel.
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
# Tegenproef voor het markerbestand: de gewone modus start de gast (die 1+2+3 teruggeeft) en dan staat het er.
LD_LIBRARY_PATH="$T" "$T/rg-elfloader" "$T/gast" >"$T/gewoon.log" 2>&1
RC=$?
[ "$RC" -eq 6 ] || { cat "$T/gewoon.log"; fail "tegenproef: de gewone modus gaf rc=$RC, verwacht 6 (de gast draait dan niet)"; }
[ -e "$T/marker" ] || fail "tegenproef: de gast draaide maar schreef geen markerbestand; de controle hieronder bewijst dan niets"
rm -f "$T/marker"
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
# Wiens adres is er ingevuld: elke toegepaste relocatie hoort bij een module, en geen enkele bij de lader zelf.
TOEGEPAST=$(echo "$OUT" | sed -n 's/^relocaties: \([0-9][0-9]*\) toegepast.*/\1/p')
PERMODULE=$(echo "$OUT" | sed -n '/^relocatiedoelen per module:/,/^[^ ]/p' | awk '$1 ~ /^[0-9]+$/ { n += $1 } END { print n + 0 }')
[ -n "$TOEGEPAST" ] && [ "$PERMODULE" -eq "$TOEGEPAST" ] || fail "relocatiedoelen per module tellen op tot $PERMODULE, toegepast is $TOEGEPAST"
echo "$OUT" | grep -Eq "^ +3  .*/libfakelib\.so$" || fail "verwachtte 3 relocatiedoelen in libfakelib.so"
echo "$OUT" | grep -q "DE LADER ZELF" && fail "een relocatie wijst naar de lader zelf"
echo "$OUT" | grep -q "onopgelost ([0-9]* zwak, 0 sterk)" || fail "verwachtte 0 sterk onopgelost bij een complete gast"
# Tegenproef voor stap 3: een gast die ruim past mag de vol-melding niet geven.
echo "$OUT" | grep -q "lib-defs-lijst vol\|LIJST VOL" && fail "vol-melding bij een gast die ruim past"

# --- 3. het plafond van de lib-defs-lijst, tegen de echte lader ---
PLAFOND=$("$T/unit" --plafond 2>/dev/null)
case "$PLAFOND" in ''|*[!0-9]*) fail "plafond van de lib-defs-lijst niet af te lezen (eenheidstest --plafond)";; esac
OVER=1000
awk -v n=$((PLAFOND + OVER)) 'BEGIN { print ".text"; for (i = 0; i < n; i++) printf ".globl v%07d\nv%07d:\n", i, i; print "ret" }' > "$T/vol.s"
"$CC" -m32 -shared -nostdlib -Wl,--hash-style=gnu -o "$T/libvol.so" "$T/vol.s" || fail "volle lib bouwen"
"$CC" -m32 $INC -no-pie -Wl,--hash-style=gnu -Wl,-Ttext-segment=0x08048000 -o "$T/gast_vol" "$T/gast.c" \
    -L"$T" -lfakelib -Wl,--no-as-needed -lvol || fail "gast (volle lib) bouwen"
readelf -d "$T/gast_vol" | grep -q 'libvol.so' || fail "voorwaarde: gast_vol hangt niet aan libvol.so"
VOL_SYMS=$(readelf --dyn-syms -W "$T/libvol.so" | awk '$1 ~ /^[0-9]+:$/ && $7 != "UND"' | wc -l)
[ "$VOL_SYMS" -ge $((PLAFOND + OVER)) ] || fail "voorwaarde: libvol.so definieert $VOL_SYMS symbolen, minder dan $((PLAFOND + OVER))"

OUT=$(LD_LIBRARY_PATH="$T" "$T/rg-elfloader" --ontleed-alleen "$T/gast_vol" 2>&1)
RC=$?
echo "$OUT" | grep "lib-defs\|spel NIET\|FATAAL"
[ "$RC" -eq 0 ] || fail "--ontleed-alleen op de volle gast eindigde met rc=$RC"
WEG=$(echo "$OUT" | sed -n 's/.*WAARSCHUWING: lib-defs-lijst vol, \([0-9][0-9]*\) symbolen weggevallen.*/\1/p')
[ -n "$WEG" ] || fail "geen WAARSCHUWING bij een volle lib-defs-lijst (stil afgekapt)"
[ "$WEG" -ge "$OVER" ] || fail "weggevallen: $WEG, verwacht minstens $OVER"
echo "$OUT" | grep -q "lib-defs: $PLAFOND gedefinieerde symbolen" || fail "lijst niet precies vol ($PLAFOND)"
echo "$OUT" | grep -q "lib-defs: LIJST VOL, $WEG symbolen weggevallen" || fail "de samenvatting noemt de volle lijst niet"
echo "$OUT" | grep -q "spel NIET gestart" || fail "geen 'spel NIET gestart' (volle gast)"
[ -e "$T/marker" ] && fail "de volle gast is gestart in de analysemodus"

OUT=$(LD_LIBRARY_PATH="$T" "$T/rg-elfloader" "$T/gast_vol" 2>&1)
RC=$?
echo "$OUT" | grep "lib-defs\|FATAAL"
[ "$RC" -eq 127 ] || fail "de gewone modus moet weigeren bij een volle lib-defs-lijst (rc=$RC, verwacht 127)"
echo "$OUT" | grep -q "FATAAL: lib-defs-lijst vol, $WEG symbolen weggevallen" || fail "geen FATAAL met het aantal weggevallen symbolen"
[ -e "$T/marker" ] && fail "de gast is gestart ondanks een volle lib-defs-lijst"

# --- 4. een gemis moet gemeld worden ---
mkdir -p "$T/leeg" "$T/half"
OUT=$(LD_LIBRARY_PATH="$T/leeg" "$T/rg-elfloader" --ontleed-alleen "$T/gast" 2>&1)
RC=$?
echo "$OUT" | grep "NIET geladen\|^relocaties\|STERK\|fakelib"
[ "$RC" -eq 0 ] || fail "--ontleed-alleen zonder de lib eindigde met rc=$RC (de analyse hoort door te gaan)"
echo "$OUT" | grep -q "^NIET geladen (1):" || fail "een onvindbare lib is niet als NIET geladen gemeld"
echo "$OUT" | grep -q "onopgelost ([0-9]* zwak, 3 sterk)" || fail "verwachtte 3 sterk onopgelost zonder libfakelib.so"
for s in fakelib_a fakelib_b fakelib_c; do
    echo "$OUT" | sed -n '/onopgeloste symbolen, STERK/,/^[^ ]/p' | grep -q " $s\$" || fail "$s staat niet bij de sterke onopgeloste symbolen"
done
[ -e "$T/marker" ] && fail "de gast is gestart in de analysemodus (zonder lib)"

printf 'int fakelib_a(void) { return 1; }\nint fakelib_b(void) { return 2; }\n' > "$T/half/fakelib.c"
"$CC" -m32 $INC -shared -fPIC -Wl,--hash-style=gnu -o "$T/half/libfakelib.so" "$T/half/fakelib.c" || fail "halve lib bouwen"
OUT=$(LD_LIBRARY_PATH="$T/half" "$T/rg-elfloader" --ontleed-alleen "$T/gast" 2>&1)
RC=$?
echo "$OUT" | grep "NIET geladen\|^relocaties\|STERK\|fakelib_c"
[ "$RC" -eq 0 ] || fail "--ontleed-alleen met de halve lib eindigde met rc=$RC"
echo "$OUT" | grep -q "onopgelost ([0-9]* zwak, 1 sterk)" || fail "verwachtte 1 sterk onopgelost met de halve lib"
echo "$OUT" | sed -n '/onopgeloste symbolen, STERK/,/^[^ ]/p' | grep -q " fakelib_c\$" || fail "fakelib_c staat niet bij de sterke onopgeloste symbolen"
[ -e "$T/marker" ] && fail "de gast is gestart in de analysemodus (halve lib)"

# --- 5. de laadorde: een lib zonder DT_NEEDED die leunt op een latere lib ---
printf 'int fakelib_a(void);\nint leun(void) { return fakelib_a() + 10; }\n' > "$T/leun.c"
"$CC" -m32 $INC -shared -fPIC -nostdlib -Wl,--hash-style=gnu -o "$T/libleun.so" "$T/leun.c" || fail "leunende lib bouwen"
cat > "$T/gast_leun.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
int leun(void);
int main(void) {
    FILE *f = fopen(getenv("RG_TEST_MARKER") ? getenv("RG_TEST_MARKER") : "/nonexistent/marker", "w");
    if (f) { fputs(getenv("LD_PRELOAD") ? getenv("LD_PRELOAD") : "-", f); fclose(f); }
    return leun();
}
C
"$CC" -m32 $INC -no-pie -Wl,--hash-style=gnu -Wl,-Ttext-segment=0x08048000 -o "$T/gast_leun" "$T/gast_leun.c" \
    -L"$T" -Wl,--no-as-needed -lleun -lfakelib || fail "gast (leunende lib) bouwen"
readelf -d "$T/libleun.so" | grep -q '(NEEDED)' && fail "voorwaarde: libleun.so heeft toch een DT_NEEDED"
ORDE=$(readelf -d "$T/gast_leun" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')
case "$ORDE" in "libleun.so libfakelib.so "*) ;; *) fail "voorwaarde: DT_NEEDED-volgorde is '$ORDE', verwacht libleun.so voor libfakelib.so";; esac

rm -f "$T/marker"
OUT=$(LD_LIBRARY_PATH="$T" "$T/rg-elfloader" "$T/gast_leun" 2>&1)
RC=$?
echo "$OUT" | grep "latere ronde\|FATAAL"
[ "$RC" -eq 11 ] || fail "gewone modus met een leunende lib: rc=$RC, verwacht 11 (de gast draait dan niet)"
[ -e "$T/marker" ] || fail "gewone modus met een leunende lib: de gast is niet gestart"
echo "$OUT" | grep -q "geladen in een latere ronde: libleun.so (eerste poging: .*fakelib_a" || fail "geen melding dat libleun.so pas in een latere ronde laadde"
rm -f "$T/marker"
OUT=$(LD_LIBRARY_PATH="$T" "$T/rg-elfloader" --ontleed-alleen "$T/gast_leun" 2>&1)
echo "$OUT" | grep "^geladen:\|^  latere ronde"
echo "$OUT" | grep -q "^geladen: 3 van de 3 bibliotheken, waarvan 1 pas in een latere ronde" || fail "analyse: verwachtte 3 van 3 geladen, 1 in een latere ronde"
echo "$OUT" | grep -q "^  latere ronde: libleun.so " || fail "analyse noemt libleun.so niet als latere ronde"
OUT=$(LD_LIBRARY_PATH="$T" "$T/rg-elfloader" --ontleed-alleen "$T/gast" 2>&1)
echo "$OUT" | grep -q "^geladen: 2 van de 2 bibliotheken, waarvan 0 pas in een latere ronde" || fail "analyse: een gast zonder leunende lib hoort 0 latere rondes te hebben"
# een lib die nooit laadt blijft fataal in de gewone modus, en de gast start niet
OUT=$(LD_LIBRARY_PATH="$T/leeg" "$T/rg-elfloader" "$T/gast_leun" 2>&1)
RC=$?
echo "$OUT" | grep "FATAAL"
[ "$RC" -eq 127 ] || fail "gewone modus zonder de libs: rc=$RC, verwacht 127"
echo "$OUT" | grep -q "FATAAL: dlopen(lib[a-z]*\.so) mislukt, ook nadat de andere bibliotheken geladen zijn" || fail "geen FATAAL voor de lib die nooit laadt"
[ -e "$T/marker" ] && fail "de gast is gestart terwijl een lib ontbrak"

# --- 6. de preload van het spel hoort niet bij de hulpprocessen van de shim-bouw ---
cat > "$T/voor.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
__attribute__((constructor)) static void voor(void) {
    FILE *f = fopen(getenv("RG_TEST_VOOR") ? getenv("RG_TEST_VOOR") : "/nonexistent/voor", "w");
    if (f) { fputs("geladen\n", f); fclose(f); }
}
C
"$CC" -m32 $INC -shared -fPIC -o "$T/libvoor.so" "$T/voor.c" || fail "preload-lib bouwen"
# Tegenproef: een 64-bits hulpproces met deze preload geeft de melding echt (anders bewijst de afwezigheid niets).
LD_PRELOAD="$T/libvoor.so" /bin/sh -c : 2>&1 | grep -q "cannot be preloaded" || fail "tegenproef: een 64-bits proces meldt de 32-bits preload niet"
rm -f "$T/marker" "$T/voor"
OUT=$(RG_TEST_VOOR="$T/voor" LD_PRELOAD="$T/libvoor.so" LD_LIBRARY_PATH="$T" "$T/rg-elfloader" "$T/gast_leun" 2>&1)
RC=$?
[ "$RC" -eq 11 ] || { echo "$OUT"; fail "gewone modus met LD_PRELOAD: rc=$RC, verwacht 11"; }
[ -e "$T/voor" ] || fail "de preload is niet in het proces van de lader geladen"
echo "$OUT" | grep -q "cannot be preloaded" && { echo "$OUT" | grep "cannot be preloaded" | head -2; fail "de hulpprocessen van de shim-bouw erven de LD_PRELOAD van het spel"; }
[ "$(cat "$T/marker" 2>/dev/null)" = "$T/libvoor.so" ] || fail "de gast ziet LD_PRELOAD niet meer ('$(cat "$T/marker" 2>/dev/null)')"

[ "$FAILS" -eq 0 ] || exit 1
echo "ok: GNU_HASH-only gast + lib; volle lib-defs-lijst en gemis melden zich; leunende lib laadt in een latere ronde; preload blijft bij het spel"
exit 0

#!/bin/bash
# Final check: the PLACED drop-in binaries do the work, and nothing of anyone else was touched.
set -u
R=~/projects/ravengun
PR=~/projects/ravengun-patches
P="$PR/patches/Halo"
DATA=~/.local/share/ravengun
N=/home/deck/mnt/spidernas_emulation/Teknoparrot
TMP=$(mktemp -d)
INC=()
[ -e /usr/include/gnu/stubs-32.h ] || INC=(-I"$R/hasp/src/inc")

echo "############ 1. JurassicPark: de GEPLAATSTE 32-bits hook, naam-route, 11 doelen"
gcc -m32 "${INC[@]}" -Wall -O2 -o "$TMP/t32" "$P/probe/hook-name-route-test.c" -ldl || exit 1
JP_ADR="0x08529650 0x085282b0 0x085295f0 0x08528520 0x08528470 0x08529550 0x085294d0 0x085283d0 0x08528090 0x08528670 0x085282a0"
rm -f /tmp/jp-plaats.log
env "RAVENGUN_HASP_ELF_GAME=$N/Jurassic Park/game" \
    "RAVENGUN_HASP_ELF_LIB=$DATA/hasp/ravengun-hasp.so" \
    "RAVENGUN_HASP_GAME=JurassicPark" "RAVENGUN_HASP_DIR=$DATA/hasp" \
    "RAVENGUN_HASP_VERBOSE=1" "RAVENGUN_HASP_ELF_LOG=/tmp/jp-plaats.log" \
    "$TMP/t32" "$N/Jurassic Park/game" "$PR/patches/JurassicPark/dropin/libravengun-hasphook.so" \
    $JP_ADR 2>&1 | tail -14
echo "exitcode ${PIPESTATUS[0]}"
echo "--- en de hook zelf:"
grep -E 'dynsym:|hasp-symbolen|totaal aangehaakt' /tmp/jp-plaats.log | sed 's/^/    | /'
echo "--- sha256 van het geplaatste bestand: $(sha256sum "$PR/patches/JurassicPark/dropin/libravengun-hasphook.so" | cut -c1-16)"

echo
echo "############ 2. Halo: de GEPLAATSTE 64-bits hook, adres-route, 7 doelen (controle)"
gcc -Wall -O2 -o "$TMP/t64" "$P/probe/hook-address-route-test.c" -ldl || exit 1
rm -f /tmp/halo-plaats.log
env "RAVENGUN_HASP_ELF_GAME=$N/Halo Fireteam/g7/halo/game" \
    "RAVENGUN_HASP_ELF_LIB=$DATA/hasp/ravengun-hasp-x64.so" \
    "RAVENGUN_HASP_GAME=Halo Fireteam" "RAVENGUN_HASP_DIR=$DATA/hasp" "RAVENGUN_HASP_VERBOSE=1" \
    "RAVENGUN_HASP_ELF_ADRESLIJST=$P/hasp-haken.ini" \
    "RAVENGUN_HASP_ELF_LOG=/tmp/halo-plaats.log" \
    "$TMP/t64" "$N/Halo Fireteam/g7/halo/game" "$PR/patches/Halo/dropin/libravengun-hasphook-x64.so" \
    2>&1 | tail -4
echo "exitcode ${PIPESTATUS[0]}"

echo
echo "############ 3. is het recept nog schoon, en wat is er in de lader-repo veranderd?"
cd "$R"
echo "--- packaging/native-games.json:"
git status --porcelain packaging/native-games.json && echo "  (leeg = schoon)"
echo "--- alle wijzigingen in de lader-repo:"
git status --porcelain | sed 's/^/  /'
echo "--- en in de patches-repo:"
cd "$PR" && git status --porcelain | sed 's/^/  /'

echo
echo "############ 4. de documenten: geen mojibake"
for f in "$P/DROPIN-GELIJKSTELLING.md" "$P/NATIVE-RECIPE-PATCH.md" "$P/HASP-INJECTIE.md"; do
  moj=$(grep -c -P 'Ã.|Â.|â€' "$f" 2>/dev/null || true)
  printf '  %-32s %6s bytes  mojibake: %s\n' "$(basename "$f")" "$(stat -c%s "$f")" "${moj:-0}"
done

echo
echo "############ 5. Terminator en GalagaAssault: niets van mij aangeraakt"
echo "--- Terminator-bestanden (nieuwste eerst, om de andere schrijver te zien):"
ls -lt "$PR/patches/Terminator" | head -6 | sed 's/^/  /'
echo "--- GalagaAssault staat er nog, onveranderd:"
ls -l "$PR/patches/GalagaAssault/dropin/libravengun-hasphook.so" | sed 's/^/  /'
echo "== done"

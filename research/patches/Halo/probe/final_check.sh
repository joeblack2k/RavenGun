#!/bin/bash
# Final check: prove the PLACED drop-in binary (the one the recipe will preload) hooks the seven
# addresses, and that the documents are clean. Nothing is started.
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
DATA=~/.local/share/ravengun
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
TMP=$(mktemp -d)

echo "############ 1. het geplaatste artefact, niet een verse bouw"
ls -l "$P/dropin/libravengun-hasphook-x64.so"
sha256sum "$P/dropin/libravengun-hasphook-x64.so" | cut -c1-16

gcc -Wall -O2 -o "$TMP/test" "$P/probe/hook-address-route-test.c" -ldl || exit 1
rm -f /tmp/hook-dropin.log
env "RAVENGUN_HASP_ELF_GAME=$G/game" \
    "RAVENGUN_HASP_ELF_LIB=$DATA/hasp/ravengun-hasp-x64.so" \
    "RAVENGUN_HASP_GAME=Halo Fireteam" "RAVENGUN_HASP_DIR=$DATA/hasp" \
    "RAVENGUN_HASP_VERBOSE=1" \
    "RAVENGUN_HASP_ELF_ADRESLIJST=$P/hasp-haken.ini" \
    "RAVENGUN_HASP_ELF_LOG=/tmp/hook-dropin.log" \
    "$TMP/test" "$G/game" "$P/dropin/libravengun-hasphook-x64.so" 2>&1 | tail -11
echo "exitcode ${PIPESTATUS[0]}"
echo "--- het oordeel in het logboek:"
grep -E 'dynsym:|hasp-symbolen|adreslijst:|GELDIG|totaal aangehaakt' /tmp/hook-dropin.log | sed 's/^/    | /'

echo
echo "############ 2. de documenten: geen mojibake, en de verwijzingen kloppen"
for f in "$P/NATIVE-RECIPE-PATCH.md" "$P/HASP-INJECTIE.md" "$P/RUN-1-DRAAIBOEK.md" "$P/HALO-NATIVE-ROUTE.md" "$P/hasp-haken.ini"; do
  [ -f "$f" ] || { echo "  ONTBREEKT $f"; continue; }
  moj=$(grep -c -P 'Ã.|Â.|â€' "$f" 2>/dev/null || true)
  printf '  %-28s %6s bytes  mojibake: %s\n' "$(basename "$f")" "$(stat -c%s "$f")" "${moj:-0}"
done

echo
echo "############ 3. de receptkaart is nog steeds schoon (ik heb er niets in geschreven)"
cd "$R" && git status --porcelain packaging/native-games.json && echo "  (leeg = schoon)"
git diff --stat -- packaging/native-games.json && echo "  (leeg = schoon)"

echo
echo "############ 4. wat er in de lader-repo is gewijzigd (alleen de hook-bron)"
git status --porcelain tools/rt-hasphook/ scripts/ | sed 's/^/  /'
echo "--- en de drop-in-kopieën per game:"
bash "$R/scripts/build-hasphook.sh" --check 2>&1 | grep -E 'actueel|AFWIJKEND|ONTBREEKT' | sed 's/^/  /'
echo "== done"

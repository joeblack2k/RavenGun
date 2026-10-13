#!/bin/bash
# Final check after the reporting change. Nothing is started.
set -u
R=~/projects/ravengun
PR=~/projects/ravengun-patches
P="$PR/patches/Halo"

echo "############ 1. de wijziging in de hook is alleen rapportage"
cd "$R"
git diff --stat -- tools/rt-hasphook/hasp_inline_hook.c | sed 's/^/  /'
echo "  --- welke functies zijn geraakt:"
git diff -U0 -- tools/rt-hasphook/hasp_inline_hook.c \
  | grep -E '^[+-].*(hh_adres_zoek_ruw|hh_reden_kort|hh_naam_in_dynsym|hh_meld_ontbrekend|hh_adres_zoek\(|hh_haak_een|hh_haak_alles|adres = )' \
  | sed 's/^/    /' | head -20
echo "  --- de aanhaakroute zelf (hh_haak_alles / hh_haak_een) is niet veranderd:"
git diff -- tools/rt-hasphook/hasp_inline_hook.c | grep -cE '^[+-].*(hh_haak_een|adres = )' | sed 's/^/    gewijzigde regels daarin: /'

echo
echo "############ 2. het bewijs opnieuw, vanaf de gepushte plek"
bash "$P/probe/reporting-fix-proof.sh" > /tmp/rap-eind.out 2>&1
echo "  exitcode $?"
grep -E 'IDENTIEK|VERSCHIL|RESULTAAT' /tmp/rap-eind.out | sed 's/^/  /'
echo "  --- de samenvatting voor en na, Terminator:"
grep -A1 'Terminator / voor' /tmp/rap-eind.out | head -2 | sed 's/^/  /'
grep -E 'in de adreslijst maar afgekeurd' /tmp/rap-eind.out | head -1 | sed 's/^ */  /'

echo
echo "############ 3. de documenten en het recept"
for f in "$P/DROPIN-GELIJKSTELLING.md" "$P/NATIVE-RECIPE-PATCH.md" "$P/HASP-INJECTIE.md"; do
  moj=$(grep -c -P 'Ã.|Â.|â€' "$f" 2>/dev/null || true)
  printf '  %-32s %6s bytes  mojibake: %s\n' "$(basename "$f")" "$(stat -c%s "$f")" "${moj:-0}"
done
cd "$R" && git status --porcelain packaging/native-games.json && echo "  recept: schoon"
echo "  --- mijn wijzigingen in de patches-repo:"
cd "$PR" && git status --porcelain | grep -E 'Halo|JurassicPark' | sed 's/^/    /'

echo
echo "############ 4. de drop-in-kopieën staan gelijk aan de bron"
bash "$R/scripts/build-hasphook.sh" --check 2>&1 | grep -E 'actueel|AFWIJKEND|ONTBREEKT' | sed 's/^/  /'
echo "== done"

#!/bin/bash
# Recon voor de TEST-vraag: waar zit TEST in deze game, en wat had de eerdere GESLAAGDE meting
# (waarin TEST de kalibratie oversloeg) dat wij nu niet hebben?
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo

echo "############ 1. de FMOD-stub: bestaat hij, en is hij gebouwd?"
ls -l "$R/tools/fmod-stub/" 2>/dev/null | sed 's/^/  /'
find "$R/build" -iname '*fmod*' 2>/dev/null | sed 's/^/  gebouwd: /'
grep -rn 'fmod' "$R/packaging/native-games.json" 2>/dev/null | head -5 | sed 's/^/  recept: /'

echo
echo "############ 2. wat het project zelf zegt over TEST, de kalibratie en de stub"
grep -rniE 'fmod-stub|TEST slaat|kalibratie|kalibratiescherm|attract' "$R/docs/"*.md 2>/dev/null | head -14 | sed 's/^/  /'
echo "  --- en in de Halo-dossiers:"
grep -rniE 'TEST|kalibratie|fmod|schakelaar' "$P/"*.md 2>/dev/null | head -12 | sed 's/^/  /'

echo
echo "############ 3. het bewijs voor de schakelaarindeling (k = ... = test)"
grep -rniE 'schakelaar k|k 0 = start|diag|test.*schakelaar|schakelaar.*test|0x50 per schakelaar|13' \
  "$R/tools/rt-input-shim/rg_rt_input.c" 2>/dev/null | head -12 | sed 's/^/  /'
echo "  --- en in de JVS-documentatie:"
find "$R/docs" -iname '*jvs*' 2>/dev/null | sed 's/^/    /'
grep -rniE 'TEST|diag|service|schakelaar|switch' "$R/docs"/*jvs* 2>/dev/null | head -14 | sed 's/^/    /'

echo
echo "############ 4. hoe leest de game de tabel? (de getter en de tabel in het beeld)"
grep -nE 'GETTER_ADDR|getter_sig|TABLE_BASE|TABLE_OFF|BOARD_STRIDE|ENTRY_STRIDE|alltypes' "$R/tools/rt-input-shim/rg_rt_input.c" | sed 's/^/  /'
echo "  --- is er een eerdere meting die de tabel zelf dumpte?"
grep -rlniE '180ae80|61e890|alltypes|13 schakelaars' "$R" "$P" 2>/dev/null | head -8 | sed 's/^/    /'

echo
echo "############ 5. wat er in ONZE runs aan FMOD ontbrak"
grep -nE 'FMOD|fmod' "$P/gamelog-run5.txt" 2>/dev/null | head -5 | sed 's/^/  /'
echo "== done"

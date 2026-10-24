#!/bin/bash
# Derde pas: de echte startscripts lezen, en het manifest van de FileTest bekijken.
set -u
H="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"

echo "############ 1. de map utils volledig"
ls -la "$H/utils" | sed 's/^/  /'

echo
echo "############ 2. run_game.sh (de TeknoParrot-versie)"
cat "$H/utils/run_game.sh" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 3. run_game.sh_original (het origineel)"
cat "$H/utils/run_game.sh_original" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 4. het verschil tussen die twee, letterlijk"
diff "$H/utils/run_game.sh_original" "$H/utils/run_game.sh" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 5. go.sh (wat de kast zelf aanroept)"
cat "$H/utils/go.sh" 2>/dev/null | head -40 | sed 's/^/  /'

echo
echo "############ 6. het manifest van de FileTest: eFileMaps"
ls -l "$H/eFileMaps" | sed 's/^/  /'
file "$H/eFileMaps" 2>/dev/null | sed 's/^/  /'
echo "  --- de eerste 40 regels:"
head -40 "$H/eFileMaps" 2>/dev/null | sed 's/^/    /'
echo "  --- noemt het manifest 'game'?"
grep -c 'game' "$H/eFileMaps" 2>/dev/null | sed 's/^/    regels met \"game\": /'

echo
echo "############ 7. staat game2 er, en zo ja wat is het?"
ls -l "$H/game2" "$H/game" "$H/game_original" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 8. en het projectscript dat de OFFICIELE werkende route doet"
ls -l ~/projects/ravengun/scripts/play-halo.sh 2>/dev/null | sed 's/^/  /'
grep -nE 'LD_PRELOAD|game|args|-f|-n|export|cd |SCREEN|WINEPREFIX|halo.so' ~/projects/ravengun/scripts/play-halo.sh 2>/dev/null | head -25 | sed 's/^/  /'
echo "== done"

#!/bin/bash
# Recon voor de START-route (run 3), lezend en niets startend:
#  - hoe werkt de input-shim, en hoe druk je START?
#  - wat geeft de OFFICIELE start van Halo mee aan argumenten/variabelen, en wat geven wij mee?
set -u
R=~/projects/ravengun
N=~/.local/share/ravengun/native/Halo
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam Raven"

echo "############ 1. de input-shim: bestaat hij, en wat is zijn gebruiksaanwijzing?"
ls -l "$R/tools/rt-input-shim/" 2>/dev/null | sed 's/^/  /'
for f in "$R/tools/rt-input-shim/README.md" "$R/tools/rt-input-shim/"*.c "$R/tools/rt-input-shim/"*.py; do
  [ -f "$f" ] || continue
  echo "  --- $(basename "$f"):"
  grep -nE 'gebruik|usage|START|start|JvsState|shm|/dev/shm|schakelaar|switch|argument|argv' "$f" 2>/dev/null | head -20 | sed 's/^/    /'
done

echo
echo "############ 2. hoe wordt de shim gebruikt in de projectbestanden?"
grep -rn 'rt-input-shim' "$R/scripts/" "$R/docs/" "$R/packaging/" 2>/dev/null | head -12 | sed 's/^/  /'

echo
echo "############ 3. het gedeelde geheugen dat de game verwacht"
ls -l /dev/shm/ 2>/dev/null | grep -iE 'tekno|jvs|halo' | sed 's/^/  /'
echo "  --- wat play-native daarover zegt:"
grep -nE 'JvsState|shm|SHARE_IPC|gedeeld' "$R/scripts/play-native.sh" 2>/dev/null | head -10 | sed 's/^/  /'

echo
echo "############ 4. wat WIJ de game meegeven (uit de laatste run)"
grep -ho 'cmd: .*' ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | tail -3 | sed 's/^/  /'
grep -ho 'preload=.*' ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | tail -2 | sed 's/^/  /'

echo
echo "############ 5. wat de OFFICIELE start meegeeft"
echo "  --- startscripts in de NAS-map:"
ls -l "$NAS" 2>/dev/null | grep -iE '\.bat|\.sh|\.cmd|run|game' | sed 's/^/    /'
for f in "$NAS/game.bat" "$NAS/run_game.sh" "$NAS/game.sh"; do
  [ -f "$f" ] || continue
  echo "  --- inhoud van $(basename "$f"):"
  head -40 "$f" | sed 's/^/    /'
done
echo "  --- en de overlay/onze vervanging:"
find "$N" -maxdepth 3 -iname '*game.sh*' -o -maxdepth 3 -iname '*.bat' 2>/dev/null | head -8 | sed 's/^/    /'

echo
echo "############ 6. is er iets dat op een venster wacht (het scenario van de andere partij)?"
grep -rniE 'argument|dialog|venster|wait|wacht' "$R/packaging/native-games.json" 2>/dev/null | head -6 | sed 's/^/  /'
grep -nE '"args"|args:|argv' "$R/packaging/native-games.json" 2>/dev/null | head -6 | sed 's/^/  /'
echo "== done"

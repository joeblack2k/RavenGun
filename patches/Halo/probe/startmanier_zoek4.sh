#!/bin/bash
# Laatste twee vragen: (a) welke werkmap geeft onze native route de game, (b) noemt het manifest 'game'?
set -u
R=~/projects/ravengun
H="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"

echo "############ 1. de werkmap in de native route"
grep -nE 'chdir|--chdir|cwd|cd |werkmap' "$R/scripts/play-native.sh" 2>/dev/null | head -20 | sed 's/^/  /'
echo "  --- en wat de launcher zelf doet (play-native.py?):"
find "$R/scripts" -maxdepth 1 -name '*native*' | sed 's/^/    /'
for f in "$R/scripts/play-native.py" "$R/scripts/native_route.py"; do
  [ -f "$f" ] || continue
  echo "  --- $(basename "$f"):"
  grep -nE 'chdir|cwd|cd |bwrap|--chdir|werkmap' "$f" 2>/dev/null | head -15 | sed 's/^/    /'
done
echo "  --- de bwrap-opbouw in play-native.sh:"
grep -n -A22 'BW=(' "$R/scripts/play-native.sh" 2>/dev/null | head -30 | sed 's/^/    /'

echo
echo "############ 2. het manifest: noemt het de binary, en met welke gegevens?"
ls -l "$H/eFileMaps/" | sed 's/^/  /'
echo "  --- komt 'game' in de kaarten voor?"
grep -l 'game' "$H/eFileMaps/"fileMap* 2>/dev/null | sed 's/^/    in: /'
echo "  --- de eerste bytes van fileMap000 (om het formaat te zien):"
xxd -l 128 "$H/eFileMaps/fileMap000" 2>/dev/null | sed 's/^/    /'
echo "  --- en de tekstregels erin (voor zover leesbaar):"
strings -n 4 "$H/eFileMaps/fileMap000" 2>/dev/null | head -12 | sed 's/^/    /'
echo "  --- staat de grootte van de binary erin (33554432 = 0x2000000, of 31270111)?"
for f in "$H/eFileMaps/"fileMap*; do
  if strings -n 4 "$f" 2>/dev/null | grep -qiE 'game|\.so|data'; then
    printf '    %s: %s\n' "$(basename "$f")" "$(strings -n 4 "$f" 2>/dev/null | head -4 | tr '\n' ' ')"
  fi
done
echo "== done"

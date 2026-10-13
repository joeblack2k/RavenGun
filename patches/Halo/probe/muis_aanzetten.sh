#!/bin/bash
# ravengunctl vinden, de muis-id opzoeken, de muismodus aanzetten en dat verifieren.
set -u
CTL=""
for c in ravengunctl ~/projects/ravengun/build/ravengunctl ~/.local/bin/ravengunctl \
         ~/.local/share/ravengun/bin/ravengunctl; do
  [ -x "$c" ] && { CTL="$c"; break; }
done
[ -n "$CTL" ] || CTL=$(find ~ -maxdepth 4 -name 'ravengunctl' -type f -perm -u+x 2>/dev/null | head -1)
echo "############ 1. het commando"
echo "  gevonden: ${CTL:-GEEN}"
[ -n "$CTL" ] || { echo "  (ravengunctl niet gevonden; laten we kijken wat er wel is:)"; ls -l ~/projects/ravengun/build/ 2>/dev/null | head -20 | sed 's/^/    /'; exit 0; }

echo
echo "############ 2. wat kan het?"
"$CTL" --help 2>&1 | head -45 | sed 's/^/  /'

echo
echo "############ 3. de muizen"
"$CTL" mice 2>&1 | head -25 | sed 's/^/  /'

echo
echo "############ 4. de stand NU (mouse_guns en de lightgun)"
"$CTL" status 2>&1 | grep -iB2 -A8 'mouse_gun\|lightgun' | head -40 | sed 's/^/  /'

echo
echo "############ 5. de muismodus aanzetten voor speler 1"
MID=$("$CTL" mice 2>/dev/null | grep -oE '^[[:space:]]*[0-9]+' | head -1 | tr -d ' ')
echo "  gekozen muis-id: ${MID:-geen}"
if [ -n "${MID:-}" ]; then
  "$CTL" mouse-gun --player 1 --mouse "$MID" 2>&1 | head -10 | sed 's/^/  zetten: /'
  sleep 1
  echo "  --- en de stand NA:"
  "$CTL" status 2>&1 | grep -iA8 'mouse_gun' | head -20 | sed 's/^/    /'
else
  echo "  geen muis-id gevonden; de uitvoer van 'mice' hierboven zegt wat er wel is"
fi
echo "== done"

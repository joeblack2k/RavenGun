#!/bin/bash
# De muismodus van ravengun aanzetten: eerst kijken wat de commando's zijn, dan zetten, dan tonen.
set -u
echo "############ 1. wat kan ravengunctl?"
command -v ravengunctl | sed 's/^/  pad: /'
ravengunctl --help 2>&1 | head -40 | sed 's/^/  /'

echo
echo "############ 2. welke muizen zijn er?"
ravengunctl mice 2>&1 | head -25 | sed 's/^/  /'

echo
echo "############ 3. de stand nu (het blok mouse_guns en de lightgun)"
ravengunctl status 2>&1 | grep -iA6 'mouse_gun\|lightgun\|gun' | head -30 | sed 's/^/  /'

echo
echo "############ 4. is er een directe lightgun- of invoeropdracht?"
ravengunctl 2>&1 | grep -iE 'mouse|gun|light|inject|move|click' | head -15 | sed 's/^/  /'

echo
echo "############ 5. en welke hulpmiddelen om de muis te besturen zijn er?"
for t in xdotool ydotool evtest libinput; do printf '  %-10s %s\n' "$t" "$(command -v "$t" || echo nee)"; done
ls -l /dev/input/by-id/ 2>/dev/null | grep -iE 'mouse|lightgun' | sed 's/^/  /'
echo "  --- het virtuele lichtgeweer:"
ls -l /dev/input/event13 2>/dev/null | sed 's/^/  /'
cat /sys/class/input/event13/device/name 2>/dev/null | sed 's/^/  naam: /'
echo "== done"

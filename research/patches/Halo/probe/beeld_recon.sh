#!/bin/bash
# Hoe kiest play-native.sh het beeld? Wat verandert er als --headless wegvalt, en wat doet --gamescope?
set -u
R=~/projects/ravengun
S="$R/scripts/play-native.sh"

echo "############ 1. de opties en hun betekenis (kop van het script)"
sed -n '1,45p' "$S" | grep -nE 'headless|gamescope|res|display|DISPLAY|GAMESCOPE|HEADLESS|RES' | sed 's/^/  /'

echo
echo "############ 2. waar HEADLESS en GAMESCOPE worden gebruikt"
grep -nE 'HEADLESS|GAMESCOPE|gamescope|DISPLAY=' "$S" | sed 's/^/  /'

echo
echo "############ 3. het stuk waar het beeld wordt opgezet"
grep -n -B4 -A22 'GAMESCOPE' "$S" | sed -n '1,60p' | sed 's/^/  /'

echo
echo "############ 4. wat er in de laatste run gebeurde (headless)"
grep -ho 'display=[^ ]* res=[^ ]* headless=[^ ]*' ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | sort -u | sed 's/^/  /'
echo "  --- en de omgeving van deze sessie:"
printf '  DISPLAY=%s  WAYLAND_DISPLAY=%s  XDG_SESSION_TYPE=%s  XDG_RUNTIME_DIR=%s\n' \
  "${DISPLAY:-leeg}" "${WAYLAND_DISPLAY:-leeg}" "${XDG_SESSION_TYPE:-leeg}" "${XDG_RUNTIME_DIR:-leeg}"
echo "  --- draait er een compositor/scherm?"
pgrep -a -x 'ksmserver|plasmashell|kwin_wayland|kwin_x11|Xwayland|Xorg' 2>/dev/null | head -6 | sed 's/^/    /'
echo "  --- welke beeldschermen ziet het systeem?"
ls /sys/class/drm/ 2>/dev/null | grep -E '^card[0-9]-' | sed 's/^/    /'
for c in /sys/class/drm/card*-*/status; do [ -f "$c" ] && printf '    %s = %s\n' "$c" "$(cat "$c")"; done 2>/dev/null | head -6
echo "== done"

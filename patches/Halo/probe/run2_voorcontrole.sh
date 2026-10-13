#!/bin/bash
# Voorcontrole voor run 2. Kijkt alleen; stopt NIETS van iemand anders.
set -u
D=~/.local/share/ravengun

echo "############ 1. tijd"
date '+  %H:%M:%S'

echo
echo "############ 2. draait er iets dat de bench bezet houdt?"
for pat in '/pm/g7/halo/game' 'play-native' 'gamescope' 'OpenParrotLoader' 'wineserver' 'VACUUM'; do
  n=$(pgrep -f "$pat" 2>/dev/null | grep -v $$ | wc -l)
  printf '  %-18s %s\n' "$pat" "$n"
done
echo "  --- de volledige regels (zodat ik zie wat het is):"
pgrep -a -f 'gamescope|wineserver|play-native|/pm/g7/halo/game' 2>/dev/null | grep -v pgrep | head -10 | sed 's/^/    /'
echo "  (leeg = niets)"

echo
echo "############ 3. de kern, zoals afgesproken"
C="$D/openparrot/current/OpenParrotx64/OpenParrot64.dll"
printf '  current -> %s\n' "$(readlink "$D/openparrot/current" 2>/dev/null)"
printf '  sha256 van de kern die current levert: %s\n' "$(sha256sum "$C" 2>/dev/null | cut -c1-16)"
printf '  de gesloten kern (f5c7532d) staat op: %s\n' "$(sha256sum "$D/runtime/TeknoParrotUi-2.0.0.130-linux-x64/TeknoParrot/TeknoParrot64.dll" 2>/dev/null | cut -c1-16)"

echo
echo "############ 4. mijn instrumenten"
printf '  tap  : %s bytes  sha256 %s\n' "$(stat -c%s ~/projects/ravengun-patches/patches/Halo/probe/libusb-tap.so 2>/dev/null)" \
  "$(sha256sum ~/projects/ravengun-patches/patches/Halo/probe/libusb-tap.so 2>/dev/null | cut -c1-16)"
printf '  logmap: %s\n' "$(ls -ld ~/.local/share/ravengun/tap 2>/dev/null)"

echo
echo "############ 5. de game-map en het logboek van de game VOOR de run"
ls -la "$D/native/Halo/0/upper" | sed 's/^/  /'
G="$D/native/Halo/0/upper/pmuser/gamelog.txt"
printf '  gamelog: %s bytes, mtime %s\n' "$(stat -c%s "$G" 2>/dev/null)" "$(stat -c%y "$G" 2>/dev/null | cut -c1-19)"
echo "  --- de laatste 3 regels voor de run:"
tail -3 "$G" 2>/dev/null | sed 's/^/    /'
cp -a "$G" /tmp/gamelog-voor-run2.txt 2>/dev/null && echo "  (kopie bewaard als /tmp/gamelog-voor-run2.txt)"
echo "== klaar om te beslissen"

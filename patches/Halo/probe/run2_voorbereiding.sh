#!/bin/bash
# De aangescherpte tap installeren en de voorbereiding van run 2 controleren. Start niets.
set -u
P=~/projects/ravengun-patches/patches/Halo
TAPDIR=~/.local/share/ravengun/tap

echo "############ 1. de tap installeren (de echte, die in de drop-in-view komt)"
gcc -O2 -Wall -Wextra -fPIC -shared -o "$P/probe/libusb-tap.so" "$P/probe/libusb-tap.c" -ldl 2>&1 | head -6
echo "  gcc exitcode ${PIPESTATUS[0]}"
ls -l "$P/probe/libusb-tap.so" | sed 's/^/  /'
sha256sum "$P/probe/libusb-tap.so" | cut -c1-16 | sed 's/^/  sha256 /'
echo "  --- vraagt hij iets bijzonders van de libc?"
objdump -T "$P/probe/libusb-tap.so" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -u | sed 's/^/    /'
echo "  --- en de nieuwe functies zitten erin:"
nm -D "$P/probe/libusb-tap.so" 2>/dev/null | grep -cE ' open$| openat$| fopen$' | sed 's/^/    open-varianten: /'

echo
echo "############ 2. de zelftest opnieuw, kort"
bash "$P/probe/tap_zelftest.sh" 2>&1 | grep -E 'gcc exitcode|APPARAAT|DIT IS HET|regels in het logboek zonder' | sed 's/^/  /'

echo
echo "############ 3. de eigen logmap voor run 2 (buiten de game-map)"
mkdir -p "$TAPDIR"
ls -ld "$TAPDIR" | sed 's/^/  /'
echo "  valt dit binnen de game-map? $(case "$TAPDIR" in *native/Halo*) echo JA - FOUT;; *) echo nee - goed;; esac)"

echo
echo "############ 4. de game-map is schoon"
ls -la ~/.local/share/ravengun/native/Halo/0/upper | sed 's/^/  /'
printf '  usb-tap.log in de game-map: %s\n' "$(find ~/.local/share/ravengun/native/Halo -name 'usb-tap.log' 2>/dev/null | wc -l)"
printf '  bewaard logboek van run 1: %s\n' "$(ls -l ~/.local/share/ravengun/rg-run1-usb-tap.log 2>/dev/null | awk '{print $5" bytes"}')"

echo
echo "############ 5. draait er iets? (bench moet vrij zijn voor de run)"
pgrep -a -f 'VACUUM|play-native|/pm/g7/halo/game|gamescope|OpenParrotLoader' | grep -v pgrep | sed 's/^/  /'
echo "  (leeg = vrij)"

echo
echo "############ 6. het draaiboek staat klaar"
ls -l "$P/RUN-2-DRAAIBOEK.md" | sed 's/^/  /'
grep -cE '^\| \*\*i|^\| \*\*ii|^\| \*\*iii|^\| \*\*iv' "$P/RUN-2-DRAAIBOEK.md" | sed 's/^/  uitkomsten in de tabel: /'
echo "== done"

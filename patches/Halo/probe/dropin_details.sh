#!/bin/bash
# The remaining facts for the write-up. Read-only.
set -u
PR=~/projects/ravengun-patches
R=~/projects/ravengun

echo "############ 1. JurassicPark/dropin/dropin.ini — wat staat er, en is Audit=false gezet?"
cat "$PR/patches/JurassicPark/dropin/dropin.ini"

echo
echo "############ 2. Terminator: bestaat de map, en wat staat er?"
ls -la "$PR/patches/Terminator/" 2>/dev/null | sed 's/^/  /'
echo "--- heeft Terminator een dongle.json / memory?"
find "$PR/patches/Terminator" -maxdepth 3 -type f 2>/dev/null | sed 's/^/  /'
echo "--- en de game.ini: welke loader/dongle-bron?"
grep -iE '^(Type|EmulationProfile|ExecutableName|Is64Bit|Dongle|Source|Loader)' "$PR/patches/Terminator/game.ini" 2>/dev/null | sed 's/^/  /'

echo
echo "############ 3. Halo: dezelfde vraag, ter vergelijking"
grep -iE 'Preload|Audit|Env|^\[' "$PR/patches/Halo/dropin/dropin.ini" | sed 's/^/  /'

echo
echo "############ 4. is er voor Terminator een halo.so-equivalent in de gamemap?"
ls -1 "/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /" 2>/dev/null | sed 's/^/  /'
echo "--- .so-bestanden in de Terminator-map:"
find "/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation " -maxdepth 3 -name '*.so*' 2>/dev/null | sed 's/^/  /'
echo "--- en voor Jurassic Park:"
find "/home/deck/mnt/spidernas_emulation/Teknoparrot/Jurassic Park" -maxdepth 3 -name '*.so*' 2>/dev/null | head -12 | sed 's/^/  /'
echo "== done"

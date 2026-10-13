#!/bin/bash
# Waarom komt de kopie van de game niet door? Eenvoudige, luidruchtige test.
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam"
SRC="$NAS/g7/halo/game"
echo "1. bron":; timeout 20 stat -c '   %s %n' "$SRC"; echo "   stat-exit=$?"
echo "2. tijd van 4 MB lezen met dd:"
time timeout 60 dd if="$SRC" of=/dev/null bs=1M count=4 2>&1 | tail -3
echo "3. hele kopie met cp:"
rm -f /tmp/io-brug-game.bin.part
time timeout 180 cp -f "$SRC" /tmp/io-brug-game.bin.part
echo "   cp-exit=$?"
ls -la /tmp/io-brug-game.bin.part 2>&1
if [ -s /tmp/io-brug-game.bin.part ]; then mv -f /tmp/io-brug-game.bin.part /tmp/io-brug-game.bin; echo "   verplaatst"; fi
ls -la /tmp/io-brug-game.bin 2>&1

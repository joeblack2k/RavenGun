#!/bin/bash
# Diagnose: loopt de statische analyse nog, en is de NAS bereikbaar?
pgrep -af io_brug_static | grep -v pgrep
echo '--- processtaat ---'
ps -eo pid,stat,wchan:24,etime,cmd | grep -E 'io_brug|objdump|cp -f|find /home' | grep -v grep | head -10
echo '--- NAS bereikbaar? (10 s limiet) ---'
timeout 10 ls -la '/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/' | head -6
echo "exitcode ls = $?"
echo '--- staat /tmp/io-brug-game.bin er al? ---'
ls -la /tmp/io-brug-game.bin 2>&1
echo '--- uitvoerbestand ---'
wc -l /home/deck/io-brug-static.out

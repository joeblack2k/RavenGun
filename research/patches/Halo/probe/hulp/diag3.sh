#!/bin/bash
# Wat doet RUN 20 nu?
echo "=== $(date '+%T') ==="
echo "--- run20-script:"
pgrep -af run20_uitvoeren.sh | grep -v pgrep
echo "--- play-native / game / gamescope:"
pgrep -af 'play-native.sh|/pm/g7/halo/game|gamescope|bwrap' | grep -v pgrep | head -12
echo "--- python (memlees/pols):"
pgrep -af 'python3' | grep -v pgrep | head -6
echo "--- processtaat van de run20-shell:"
ps -eo pid,ppid,stat,wchan:20,etime,cmd | grep -E 'run20|play-native' | grep -v grep | head -6
echo "--- run20.out regels:"
wc -l /home/deck/run20.out
echo "--- spellogboek (laatste 12):"
L=$(ls -t /home/deck/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | head -1); echo "   ($L)"
tail -12 "$L" 2>/dev/null
echo "--- gamelog:"
tail -6 /home/deck/.local/share/ravengun/native/Halo/0/upper/pmuser/gamelog.txt 2>/dev/null

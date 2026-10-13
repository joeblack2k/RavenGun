#!/bin/bash
# Loopt de statische analyse nog? (de python-stappen hebben een andere cmdline dan het script)
echo "--- bash-script:"
pgrep -af io_brug_static.sh | grep -v pgrep
echo "--- python-stappen:"
ps -eo pid,etime,stat,pcpu,cmd | grep -E 'python3 -|objdump' | grep -v grep | head -5
echo "--- regels uitvoer:"
wc -l /home/deck/io-brug-static.out
tail -4 /home/deck/io-brug-static.out

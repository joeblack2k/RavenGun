#!/bin/bash
# usage: win-watch.sh <exact comm> <tag>   -- started by win-run.sh, runs on the host next to the run.
# Appends to the run's record the FULL path of every core DLL mapped in the live game process: 10 s after the process
# appears and then every 30 s until the run has ended.
COMM=$1; TAG=$2
O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/win-bench
REC=$O/$TAG.run
until pgrep -x "$COMM" >/dev/null || grep -q outer-rc $O/$TAG.txt 2>/dev/null; do sleep 1; done
echo "game process first seen $(date +%H:%M:%S)" >> $REC
grep -a -m1 'kern (' $O/$TAG.out 2>/dev/null | sed 's/^/launcher says: /' >> $REC
sleep 10
while ! grep -q outer-rc $O/$TAG.txt 2>/dev/null; do
  p=$(pgrep -x "$COMM" | head -1)
  if [ -n "$p" ]; then
    cores=$(awk '{ $1=$2=$3=$4=$5=""; sub(/^ +/,""); print }' /proc/$p/maps 2>/dev/null | grep -i -E '/(OpenParrot(64)?|TeknoParrot(64)?)\.dll$' | sort -u | tr '\n' ' ')
    echo "$(date +%H:%M:%S) alive: pid $p, $(ls /proc/$p/task 2>/dev/null | wc -l) threads; core DLL mapped: ${cores:-NONE}" >> $REC
  else
    echo "$(date +%H:%M:%S) NO game process" >> $REC
  fi
  n=0; until [ $n -ge 30 ] || grep -q outer-rc $O/$TAG.txt 2>/dev/null; do sleep 1; n=$((n+1)); done
done

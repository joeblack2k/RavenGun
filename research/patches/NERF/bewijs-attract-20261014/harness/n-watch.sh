#!/bin/bash
# usage: n-watch.sh <tag>   -- started by n-run.sh, runs on the host next to the run. Appends to the run's record, 10 s after the
# game process appears and then every 30 s: the executable of the process, which HASP library is mapped (full path), which RIO
# libraries, and whether ANYTHING of Wine / TeknoParrot / OpenParrot / BepInEx is mapped (the native route must show none).
TAG=$1
O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/nerf-bench
REC=$O/$TAG.run
until pgrep -x Nerf.x86_64 >/dev/null || grep -q outer-rc $O/$TAG.txt 2>/dev/null; do sleep 1; done
grep -q outer-rc $O/$TAG.txt 2>/dev/null && { echo "the game process never appeared" >> $REC; exit 0; }
echo "game process first seen $(date +%H:%M:%S)" >> $REC
sleep 10
while ! grep -q outer-rc $O/$TAG.txt 2>/dev/null; do
  p=$(pgrep -x Nerf.x86_64 | head -1)
  if [ -n "$p" ]; then
    M=$(awk '{ $1=$2=$3=$4=$5=""; sub(/^ +/,""); print }' /proc/$p/maps 2>/dev/null | sort -u)
    hasp=$(echo "$M" | grep -i -E 'hasp' | tr '\n' ' ')
    rio=$(echo "$M" | grep -i -E '/lib(rio|RIO2|UnityNatives)\.so' | sed 's#.*/##' | tr '\n' ' ')
    foreign=$(echo "$M" | grep -i -c -E '/wine|ntdll|TeknoParrot(64)?\.dll|OpenParrot|BepInEx|NERFTPPlugin|winhttp|doorstop|UnityPlayer\.dll')
    echo "$(date +%H:%M:%S) alive: pid $p, exe $(readlink /proc/$p/exe 2>/dev/null), $(ls /proc/$p/task 2>/dev/null | wc -l) threads; hasp mapped: ${hasp:-NONE}; ${rio:-no rio libs}; Wine/TeknoParrot/OpenParrot/BepInEx mappings: $foreign" >> $REC
  else
    echo "$(date +%H:%M:%S) NO game process" >> $REC
  fi
  n=0; until [ $n -ge 30 ] || grep -q outer-rc $O/$TAG.txt 2>/dev/null; do sleep 1; n=$((n+1)); done
done

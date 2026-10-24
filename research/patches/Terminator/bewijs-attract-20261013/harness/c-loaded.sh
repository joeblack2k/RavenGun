#!/bin/bash
# Which preloads are REALLY mapped in the game process, and who printed the ld.so refusals? usage: c-loaded.sh <tag>
TAG=$1; W=/tmp/ts-c; T=$W/log/$TAG
echo "now $(date +%H:%M:%S)"
for p in $(pgrep -f "rg-elfloader $W/view$TAG/game"); do
  cls=$(head -c 5 /proc/$p/exe | tail -c 1 | od -An -tu1 | tr -d ' ')
  echo "pid $p comm $(cat /proc/$p/comm) exe-class $([ "$cls" = 1 ] && echo ELF32 || echo ELF64) threads $(ls /proc/$p/task | wc -l)"
  for lib in ptracer-permit hasphook libtrace2 libexit-log libioctl-log net-log bestand-log fork-log ravengun-hasp.so; do
    n=$(grep -c "$lib" /proc/$p/maps); echo "    maps: $lib -> $n mappings"
  done
done
echo "--- ld.so refusals in out.txt: total $(grep -c 'cannot be preloaded' $T/out.txt), per library:"
grep 'cannot be preloaded' $T/out.txt | sed -E "s/.*object '([^']+)'.*\((.*)\).*/\2  \1/" | sed -E 's#/tmp/[^ ]*/##' | sort | uniq -c
echo "--- log files of this run (size, lines, lines from game pid)"
G=$(pgrep -f "rg-elfloader $W/view$TAG/game" | tail -1)
for f in exit.log ioctl.log net.log bestand.log fork.log trace.log permit.log; do [ -e $T/$f ] && echo "$f $(stat -c %s $T/$f) B, $(wc -l < $T/$f) lines, $(grep -c "pid $G" $T/$f) with game pid $G, first: $(head -1 $T/$f | cut -c1-110)"; done

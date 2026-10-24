#!/bin/bash
# one-look state of a run: usage c-state.sh <tag>
TAG=$1; W=/tmp/ts-c; T=$W/log/$TAG; O=$W/log/$TAG-tv
echo "now $(date +%H:%M:%S)"; cat $O/env.txt 2>/dev/null; cat $T/RESULT.txt 2>/dev/null
tail -3 $T/threads.log 2>/dev/null
echo "--- shots (hash, name)"; for f in $O/shots/*.png; do [ -e "$f" ] && echo "$(sha256sum "$f" | cut -c1-10) $(stat -c %s "$f") $(basename "$f")"; done | tail -${N:-12}
echo "--- game syslog (this run)"; awk '/Welcome to Terminator/{n=NR} {l[NR]=$0} END{for(i=(n>1?n-1:1);i<=NR;i++)print l[i]}' $W/upper$TAG/log/syslog_bb3.txt 2>/dev/null | grep -v 'Failed to load shader' | tail -${L:-15} | cut -c1-200
echo "--- trace: scr_start_cb"; grep -c scr_start_cb $T/trace.log 2>/dev/null; grep scr_start_cb $T/trace.log 2>/dev/null | tail -2 | cut -c1-160

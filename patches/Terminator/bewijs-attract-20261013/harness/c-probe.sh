#!/bin/bash
# usage: c-probe.sh <tag>  -- wait for the warning screen, press START, capture; if the lockout screen is back, open TEST > SYSTEM INFORMATION > VERSION LIST and capture
TAG=$1; SP=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/bench; W=/tmp/ts-c; S=$W/upper$TAG/log/syslog_bb3.txt; O=$W/log/$TAG-tv/mine; mkdir -p $O
shot(){ f=$O/$1-$(date +%H%M%S).png; ~/projects/ravengun/scripts/rg-tv-env.sh --no-hold -- spectacle -b -n -f -o $f 2>/dev/null; echo "$1 $(sha256sum $f | cut -c1-10) $f"; }
press(){ for s in "$@"; do python3 $SP/../input-lane/ts-jamma-inject.py $W/log/$TAG/pty/ttyS0-peer $s --hold 120 --state $SP/$TAG.jamma-state >/dev/null 2>&1; sleep 0.35; done; }
for i in $(seq 1 150); do grep -q 'cf file check produced errors' "$S" 2>/dev/null && break; [ -e $W/log/$TAG/RESULT.txt ] && { echo "RUN ENDED EARLY"; cat $W/log/$TAG/RESULT.txt; exit 1; }; sleep 1; done
echo "file-check line after ${i}s; shaders failed: $(grep -c "$(date '+%b %d').*Failed to load shader" "$S")"
sleep 22; shot warn | cut -d' ' -f1,2
press start1; echo "START at $(date +%H:%M:%S)"; sleep 10; A=$(shot afterstart); echo "$A"
if [ "$(echo "$A" | cut -d' ' -f2)" != "005f6c7cd8" ]; then echo "NOT the lockout screen -> leaving the game alone"; exit 0; fi
press test; sleep 4; press voldown voldown voldown voldown voldown voldown voldown voldown start1; sleep 1; press voldown start1; sleep 1.5; shot versions
echo "--- hasp calls"; grep -E 'hasp_(read|write)' $W/log/$TAG/out.txt | sort | uniq -c

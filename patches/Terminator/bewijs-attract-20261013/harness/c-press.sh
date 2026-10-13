#!/bin/bash
# usage: c-press.sh <tag> <switch> [<switch>...]   -- presses each switch (120 ms hold), 0.35 s apart; then takes a TV capture
TAG=$1; shift; SP=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad
for s in "$@"; do python3 $SP/input-lane/ts-jamma-inject.py /tmp/ts-c/log/$TAG/pty/ttyS0-peer $s --hold 120 --state $SP/bench/$TAG.jamma-state 2>&1 | grep -E '^press' | cut -c1-60; sleep 0.35; done
sleep ${SETTLE:-1.2}
mkdir -p /tmp/ts-c/log/$TAG-tv/mine; f=/tmp/ts-c/log/$TAG-tv/mine/m-$(date +%H%M%S).png
~/projects/ravengun/scripts/rg-tv-env.sh --no-hold -- spectacle -b -n -f -o $f 2>/dev/null; echo "$f $(sha256sum $f | cut -c1-10)"

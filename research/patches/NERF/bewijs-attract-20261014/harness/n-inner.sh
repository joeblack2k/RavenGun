#!/bin/bash
# runs INSIDE rg-tv-env: capture loop + the product launcher. usage: n-inner.sh <tag> <seconds> [extra play-native args]
TAG=$1; SECS=$2; shift 2
O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/nerf-bench
mkdir -p $O/$TAG-shots
( i=0; while [ ! -e $O/$TAG-shots/stop ]; do i=$((i+1)); spectacle -b -n -f -o $O/$TAG-shots/shot-$(printf %03d $i)-$(date +%H%M%S).png >/dev/null 2>&1; sleep ${SHOTSECS:-10}; done ) &
SH=$!
cd /home/deck/projects/ravengun
G=(); [ "${N_GAMESCOPE:-0}" = 1 ] && G=(--gamescope)
scripts/play-native.sh --game NERF "${G[@]}" --seconds $SECS "$@" > $O/$TAG.out 2>&1
echo "rc=$?" >> $O/$TAG.out
touch $O/$TAG-shots/stop; sleep 1; kill $SH 2>/dev/null; true

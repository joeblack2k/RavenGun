#!/bin/bash
# runs INSIDE rg-tv-env (desktop session display, status board held away). usage: c-outer.sh <tag>
TAG=$1; SP=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/bench
export W=/tmp/ts-c; OUT=$W/log/$TAG-tv; mkdir -p $OUT/shots
echo "tv-env: DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY WAYLAND_DISPLAY=$WAYLAND_DISPLAY start=$(date +%H:%M:%S)" > $OUT/env.txt
( i=0; while [ ! -e $OUT/stop ]; do i=$((i+1)); f=$OUT/shots/shot-$(printf %03d $i)-$(date +%H%M%S).png; spectacle -b -n -f -o $f >>$OUT/spectacle.log 2>&1; sleep ${SHOTSECS:-10}; done ) &
SH=$!
export TRACELIST=${TRACELIST:-$SP/merged5.lst} RUNSECS=${RUNSECS:-900}
unset XAUTHORITY
gamescope -f -W 1920 -H 1080 -w 1360 -h 768 --force-grab-cursor -- bash $SP/c-game.sh $TAG > $OUT/gamescope.log 2>&1
rc=$?
touch $OUT/stop; sleep 1; kill $SH 2>/dev/null
echo "gamescope rc=$rc end=$(date +%H:%M:%S)" >> $OUT/env.txt

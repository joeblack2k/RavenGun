#!/bin/bash
# usage: jp2-run.sh <tag> <seconds> <game-binary> <schedule-file|-> [extra play-native args...]
# One Jurassic Park bench run on the TV with the RIO meter preloaded. Captures every 10 s plus one 1.5 s after each injection.
TAG=$1; SECS=$2; GAMEBIN=$3; SCHED=$4; shift 4
D=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/jp; R=$D/runs/$TAG
mkdir -p $R/shots $R/meter; cp $D/jp_rio_meter.so $R/meter/; : > $R/meter/inject
[[ "$SCHED" != - ]] && cp "$SCHED" $R/schedule.txt
echo "$TAG start $(date +%H:%M:%S) game=$(sha256sum "$GAMEBIN" | cut -c1-16) meter=$(sha256sum $R/meter/jp_rio_meter.so | cut -c1-16)" > $R/run.txt
timeout $((SECS+120)) /home/deck/projects/ravengun/scripts/rg-tv-env.sh -- bash $D/jp2-inner.sh "$TAG" "$SECS" "$GAMEBIN" "$@" > $R/outer.log 2>&1
echo "$TAG outer-rc=$? end $(date +%H:%M:%S) log=$(ls -t ~/.local/state/ravengun/logs/native-JurassicPark-*.log | head -1)" >> $R/run.txt

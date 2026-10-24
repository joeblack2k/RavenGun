#!/bin/bash
# usage: jp-run.sh <tag> <seconds> [extra play-native args...]   -- one Jurassic Park bench run on the TV, with captures every 10 s
TAG=$1; SECS=$2; O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/jp-bench
echo "$TAG start $(date +%H:%M:%S)" > $O/$TAG.txt
timeout $((SECS+120)) /home/deck/projects/ravengun/scripts/rg-tv-env.sh -- bash $O/jp-inner.sh "$@" > $O/$TAG.outer 2>&1
echo "$TAG outer-rc=$? end $(date +%H:%M:%S) log=$(ls -t ~/.local/state/ravengun/logs/native-JurassicPark-*.log | head -1)" >> $O/$TAG.txt

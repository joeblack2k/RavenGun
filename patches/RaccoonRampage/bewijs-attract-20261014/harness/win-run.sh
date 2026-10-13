#!/bin/bash
# usage: win-run.sh <profile> <share-folder-name> <exe-comm> <tag> <seconds> [K=V ...]
TAG=$4; O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/win-bench
echo "$TAG start $(date +%H:%M:%S)" > $O/$TAG.txt
/home/deck/projects/ravengun/scripts/rg-tv-env.sh -- bash $O/win-inner.sh "$@" > $O/$TAG.outer 2>&1
echo "$TAG outer-rc=$? end $(date +%H:%M:%S)" >> $O/$TAG.txt

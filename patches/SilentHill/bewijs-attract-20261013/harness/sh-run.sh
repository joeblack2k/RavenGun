#!/bin/bash
# usage: sh-run.sh <tag> <seconds> [K=V ...]   -- one Silent Hill bench run on the TV on our own core, NAS read-only, captures every 10 s
TAG=$1; O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/sh-bench
echo "$TAG start $(date +%H:%M:%S)" > $O/$TAG.txt
/home/deck/projects/ravengun/scripts/rg-tv-env.sh -- bash $O/sh-inner.sh "$@" > $O/$TAG.outer 2>&1
echo "$TAG outer-rc=$? end $(date +%H:%M:%S)" >> $O/$TAG.txt

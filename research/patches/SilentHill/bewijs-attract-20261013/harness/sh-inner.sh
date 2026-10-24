#!/bin/bash
# runs INSIDE rg-tv-env. usage: sh-inner.sh <tag> <seconds> [extra env assignments K=V ...]
# The NAS game folder is made READ-ONLY for the whole process tree (bwrap ro-bind over itself): a from-zero title gets no
# chance to write or delete on the share. --unshare-pid: when the launcher ends, everything inside ends with it (no orphans).
TAG=$1; SECS=$2; shift 2; O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/sh-bench
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Silent Hill The Arcade"
mkdir -p $O/$TAG-shots
( i=0; while [ ! -e $O/$TAG-shots/stop ]; do i=$((i+1)); spectacle -b -n -f -o $O/$TAG-shots/shot-$(printf %03d $i)-$(date +%H%M%S).png >/dev/null 2>&1; sleep ${SHOTSECS:-10}; done ) &
SH=$!
cd /home/deck/projects/ravengun
env "$@" bwrap --die-with-parent --unshare-pid --dev-bind / / --proc /proc --tmpfs /tmp/.X11-unix --ro-bind "$NAS" "$NAS" -- \
    timeout -s TERM -k 10 $SECS python3 scripts/launch-direct.py SilentHill --run > $O/$TAG.out 2>&1
echo "rc=$?" >> $O/$TAG.out
touch $O/$TAG-shots/stop; sleep 1; kill $SH 2>/dev/null; true

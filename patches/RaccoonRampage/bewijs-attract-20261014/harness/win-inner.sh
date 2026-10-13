#!/bin/bash
# runs INSIDE rg-tv-env. usage: win-inner.sh <profile> <share-folder-name> <exe-comm> <tag> <seconds> [K=V ...]
# WIN_SHM_SOURCE=<file> (exported by the caller): the launcher's --shm-source, as scripts/play-halo.sh passes it for the
# tp-shm input route (the daemon's bridge writes /dev/shm/ravengun_JvsState, NOT the launcher's default file).
# WIN_SHARE=overlay (exported by the caller): the game's folder on the share gets a WRITABLE THROW-AWAY layer (overlayfs,
# writes go to an invisible tmpfs and are gone after the run; the share itself is never written). Default: read-only.
# The game's folder on the share is READ-ONLY for the whole process tree; --unshare-pid ends everything with the launcher.
PROF=$1; NASNAME=$2; COMM=$3; TAG=$4; SECS=$5; shift 5; O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/win-bench
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/$NASNAME"
mkdir -p $O/$TAG-shots
( i=0; while [ ! -e $O/$TAG-shots/stop ]; do i=$((i+1)); spectacle -b -n -f -o $O/$TAG-shots/shot-$(printf %03d $i)-$(date +%H%M%S).png >/dev/null 2>&1; sleep ${SHOTSECS:-10}; done ) &
SH=$!
cd /home/deck/projects/ravengun
if [ "${WIN_SHARE:-ro}" = overlay ]; then SHARE=(--overlay-src "$NAS" --tmp-overlay "$NAS"); else SHARE=(--ro-bind "$NAS" "$NAS"); fi
echo "share mode: ${WIN_SHARE:-ro}  shm source: ${WIN_SHM_SOURCE:-launcher default}" > $O/$TAG.mode
env "$@" bwrap --die-with-parent --unshare-pid --dev-bind / / --proc /proc --tmpfs /tmp/.X11-unix "${SHARE[@]}" -- \
    timeout -s TERM -k 10 $SECS python3 scripts/launch-direct.py $PROF --run ${WIN_SHM_SOURCE:+--shm-source "$WIN_SHM_SOURCE"} > $O/$TAG.out 2>&1
echo "rc=$?" >> $O/$TAG.out
touch $O/$TAG-shots/stop; sleep 1; kill $SH 2>/dev/null; true

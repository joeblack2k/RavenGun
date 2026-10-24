#!/bin/bash
# runs INSIDE rg-tv-env. usage: win-inner.sh <profile> <share-folder-name> <exe-comm> <tag> <seconds> [K=V ...]
# WIN_SHM_SOURCE=<file> (exported by the caller): the launcher's --shm-source, as scripts/play-halo.sh passes it for the
# tp-shm input route (the daemon's bridge writes /dev/shm/ravengun_JvsState, NOT the launcher's default file).
# WIN_EXE=<path> (exported by the caller): the launcher's --exe, to start the game from another path (e.g. C:\\Sega\\shell\\Shell.exe).
# WIN_SHARE=overlay (exported by the caller): the game's folder on the share gets a WRITABLE THROW-AWAY layer (overlayfs,
# writes go to an invisible tmpfs and are gone after the run; the share itself is never written). Default: read-only.
# The game's folder on the share is READ-ONLY for the whole process tree; --unshare-pid ends everything with the launcher.
PROF=$1; NASNAME=$2; COMM=$3; TAG=$4; SECS=$5; shift 5; O=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/win-bench
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/$NASNAME"
mkdir -p $O/$TAG-shots
( i=0; while [ ! -e $O/$TAG-shots/stop ]; do i=$((i+1)); spectacle -b -n -f -o $O/$TAG-shots/shot-$(printf %03d $i)-$(date +%H%M%S).png >/dev/null 2>&1; sleep ${SHOTSECS:-10}; done ) &
SH=$!
cd /home/deck/projects/ravengun
# WIN_SHARE=overlay-keep: the same writable layer, but KEPT on disk ($HOME/.local/state/ravengun/bench-upper/<tag>/upper), so what
# the game wrote during the run can be read afterwards. The share itself is still never written.
KEEP=$HOME/.local/state/ravengun/bench-upper/$TAG
if [ "${WIN_SHARE:-ro}" = overlay ]; then SHARE=(--overlay-src "$NAS" --tmp-overlay "$NAS")
elif [ "${WIN_SHARE:-ro}" = overlay-keep ]; then mkdir -p "$KEEP/upper" "$KEEP/work"; SHARE=(--overlay-src "$NAS" --overlay "$KEEP/upper" "$KEEP/work" "$NAS")
else SHARE=(--ro-bind "$NAS" "$NAS"); fi
echo "share mode: ${WIN_SHARE:-ro}  shm source: ${WIN_SHM_SOURCE:-launcher default}" > $O/$TAG.mode
# the first thing inside the sandbox writes into the run's record how the share's game folder is mounted THERE
# WIN_MASK_DEV="<dev> ..." (exported by the caller): device nodes the game must NOT reach; /dev/null is shown in their place.
# (Men in Black opens the first serial port that answers; on this machine that was the owner's Steam Controller Puck, /dev/ttyACM0.)
MASK=(); for d in ${WIN_MASK_DEV:-}; do [ -e "$d" ] && MASK+=(--bind /dev/null "$d"); done
echo "devices hidden from the game: ${WIN_MASK_DEV:-none}" >> $O/$TAG.mode
env "$@" bwrap --die-with-parent --unshare-pid --dev-bind / / --proc /proc --tmpfs /tmp/.X11-unix "${SHARE[@]}" "${MASK[@]}" -- \
    bash -c 'echo "inside the sandbox, the share folder is mounted as: $(findmnt -n -o FSTYPE,OPTIONS -T "$1" | cut -c1-150)" >> "$2"; shift 2; exec "$@"' _ "$NAS" "$O/$TAG.run" \
    timeout -s TERM -k 10 $SECS python3 scripts/launch-direct.py $PROF --run ${WIN_SHM_SOURCE:+--shm-source "$WIN_SHM_SOURCE"} ${WIN_EXE:+--exe "$WIN_EXE"} > $O/$TAG.out 2>&1
echo "rc=$?" >> $O/$TAG.out
touch $O/$TAG-shots/stop; sleep 1; kill $SH 2>/dev/null; true

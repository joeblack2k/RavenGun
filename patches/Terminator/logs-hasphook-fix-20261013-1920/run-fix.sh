#!/bin/bash
# usage: run-fix.sh <tag> [extra env assignments...]   (one game run, max 90 s; /tmp only; NAS read-only)
set -u
TAG="$1"; shift
R="$HOME/projects/ravengun"; D="$R/patches/Terminator"
S=/tmp/claude-1000/-home-deck-projects-ravengun/ad4281a2-6cd6-4f12-90bf-4bd9323231c3/scratchpad
LOADER="$R/tools/rg-elfloader/rg-elfloader"
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
EL="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
LB32="$HOME/.local/share/ravengun/runtime/lib32"
INC="$R/tools/rg-elfloader/inc"
HOOK="$S/build/libravengun-hasphook.so"
[ -n "${HOOKOVERRIDE:-}" ] && HOOK="$HOOKOVERRIDE"
W=/tmp/ts-fix/$TAG
for p in gamescope rg-elfloader wine wine64 game; do pgrep -x $p >/dev/null && { echo "BEZET door $p"; exit 1; }; done
rm -rf "$W"; mkdir -p "$W/log" "$W/upper" "$W/work" "$W/view" "$W/hasp-root"
cp -r "$D/dropin/ravengun-hasp/Terminator" "$W/hasp-root/Terminator"
if [ -n "${ADD_FFF2:-}" ]; then python3 - "$W/hasp-root/Terminator" <<'PY'
import json,sys,os
d=sys.argv[1]; j=json.load(open(d+"/dongle.json"))
for fid in (65522,65520):
    n="%032d"%int(bin(fid)[2:]); open(d+"/memory/"+n,"wb").write(bytes(128))
    j["memory"].append({"name":"memory","fileid":fid,"size":128,"file":"memory/"+n})
json.dump(j,open(d+"/dongle.json","w"),indent=1)
PY
fi
for f in ptracer-permit fork-log net-log bestand-log; do
  gcc -m32 -shared -fPIC -O1 -I"$INC" -o "$W/libravengun-$f.so" "$D/$f.c" -ldl 2>&1 | head -3
done
cp /tmp/ts-dongle/libtrace.so "$W/libtrace.so"
cp "$NASDIR/game" "$W/view/game"
for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" "$W/view/$d"; done
cp -r "$NASDIR/T4User" "$W/view/T4User"
cp "$NASDIR/T4User/log/syslog_bb3.txt" "$W/view/syslog_bb3.txt" 2>/dev/null
socat pty,raw,echo=0,link="$W/sierra2" pty,raw,echo=0,link="$W/sierra2-peer" >"$W/socat.log" 2>&1 &
SOCAT=$!; for i in $(seq 1 20); do [ -L "$W/sierra2" ] && break; sleep 0.2; done
BW=(bwrap --die-with-parent --ro-bind /usr /usr --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin
    --ro-bind /etc /etc --proc /proc --ro-bind /sys /sys --ro-bind /home /home --ro-bind /var /var --ro-bind /opt /opt --ro-bind /boot /boot --ro-bind /srv /srv)
[ -e /root ] && BW+=(--ro-bind /root /root)
BW+=(--dev /dev --dev-bind-try /dev/pts /dev/pts --dev-bind-try /dev/bus/usb /dev/bus/usb)
for dn in null zero full random urandom tty shm dri snd ttyS0 input; do [ -e "/dev/$dn" ] && BW+=(--dev-bind-try "/dev/$dn" "/dev/$dn"); done
[ -L "$W/sierra2" ] && BW+=(--dev-bind "$(readlink -f "$W/sierra2")" /dev/sierra2)
BW+=(--ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 --bind /tmp /tmp)
BW+=(--dir /g3 --ro-bind "$NASDIR" /g3)
BW+=(--overlay-src "$NASDIR/T4User" --overlay "$W/upper" "$W/work" /T4User)
cat >"$W/binnen.sh" <<EOF
cd "$W/view" || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB32" RG_ELFLOADER_VERBOSE=1
export RAVENGUN_NET_LOG=$W/log/net.log RAVENGUN_BESTAND_LOG=$W/log/bestand.log RAVENGUN_FORK_LOG=$W/log/fork.log RAVENGUN_PTRACER_LOG=$W/log/permit.log
export RAVENGUN_HASP_ELF_GAME=$W/view/game RAVENGUN_HASP_ELF_ADRESLIJST=/tmp/ts-dongle/hasp-haken-pc.ini RAVENGUN_HASP_ELF_LOG=$W/log/hasp.log RAVENGUN_HASP_VERBOSE=1
export RAVENGUN_HASP_GAME=Terminator RAVENGUN_HASP_DIR=$W/hasp-root
export RG_TRACE_LIST=/tmp/ts-dongle/trace2.lst RG_TRACE_WATCH=/tmp/ts-dongle/watch10.lst RG_TRACE_LOG=$W/log/trace.log
$*
${CLOBBER:+export CLOBBER_LOG=$W/log/clobber.log}
exec timeout -s TERM -k 5 90 env LD_PRELOAD="${CLOBBER:+/tmp/claude-1000/-home-deck-projects-ravengun/ad4281a2-6cd6-4f12-90bf-4bd9323231c3/scratchpad/build/libclobber.so:}$W/libravengun-ptracer-permit.so:$HOOK:$W/libtrace.so:$W/libravengun-net-log.so:$W/libravengun-bestand-log.so:$W/libravengun-fork-log.so" "$LOADER" "$W/view/game"
EOF
t0=$(date +%s%N)
"${BW[@]}" /bin/bash "$W/binnen.sh" >"$W/log/out.txt" 2>&1; rc=$?
t1=$(date +%s%N)
kill $SOCAT 2>/dev/null
cp "$W/upper/log/syslog_bb3.txt" "$W/log/syslog_bb3.txt" 2>/dev/null
{ echo "tag=$TAG rc=$rc life_ms=$(( (t1-t0)/1000000 ))"; echo "hook=$HOOK $(sha256sum $HOOK|cut -d' ' -f1)"; echo "extra env: $*"; } | tee "$W/log/RESULT.txt"

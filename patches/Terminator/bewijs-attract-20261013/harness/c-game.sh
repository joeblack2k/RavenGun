#!/bin/bash
# usage: [VAR=...] b6-run.sh <tag>   -- runs Terminator with the FULL drop-in (patches/Terminator/dropin), arm 2 devices + fake dsp
TAG=$1; VARIANT=${VAR:-}
W=${W:-/tmp/ts-c}; T=$W/log/$TAG; V=$W/view$TAG; DI=$HOME/projects/ravengun-patches/patches/Terminator/dropin
rm -rf $T $W/upper$TAG $W/work$TAG $V; mkdir -p $T $W/upper$TAG $W/work$TAG $V
NASDIR="/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
cp "$NASDIR/game" $V/game
for d in T4Data ServiceDongleApp hasp env irgun GeneralPurposeStateMachine.txt; do [ -e "$NASDIR/$d" ] && ln -sfn "$NASDIR/$d" $V/$d; done
[ -e "$NASDIR/T4User" ] && cp -r "$NASDIR/T4User" $V/T4User
cp "$NASDIR/T4User/log/syslog_bb3.txt" $V/syslog_bb3.txt 2>/dev/null
# --- de drop-in toepassen zoals dropin.ini hem beschrijft (To = . ; sectienaam = pad in de view)
eval "$(python3 - "$DI" "$V" <<'EOF'
import sys,configparser,shutil,os,shlex
di,v=sys.argv[1],sys.argv[2]
cp=configparser.ConfigParser(interpolation=None); cp.optionxform=str
cp.read(os.path.join(di,'dropin.ini'))
pre=[];env=[]
for sec in cp.sections():
    to=cp[sec].get('To','.')
    dst=os.path.normpath(os.path.join(v,to,sec)); os.makedirs(os.path.dirname(dst),exist_ok=True)
    shutil.copy2(os.path.join(di,sec),dst)
    if cp[sec].get('Preload','false').lower()=='true': pre.append(dst)
    if cp[sec].get('Env'): env+= [e for e in cp[sec]['Env'].split(';') if e]
print("DROPIN_PRELOAD=%s"%shlex.quote(":".join(pre)))
print("DROPIN_ENV=%s"%shlex.quote(" ".join(shlex.quote(e) for e in env)))
EOF
)"
case "$VARIANT" in
  nof0)  rm -f $V/ravengun-hasp/Terminator/memory/00000000000000001111111111110000;;
  zero2) head -c 128 /dev/zero > $V/ravengun-hasp/Terminator/memory/00000000000000001111111111110010;;
  zero0) head -c 128 /dev/zero > $V/ravengun-hasp/Terminator/memory/00000000000000001111111111110000;;
  zeroboth) head -c 128 /dev/zero > $V/ravengun-hasp/Terminator/memory/00000000000000001111111111110010; head -c 128 /dev/zero > $V/ravengun-hasp/Terminator/memory/00000000000000001111111111110000;;
esac
M=$V/ravengun-hasp/Terminator/memory
[ -n "${FFF2_FILE:-}" ] && cp "$FFF2_FILE" $M/00000000000000001111111111110010
[ -n "${FFF0_FILE:-}" ] && cp "$FFF0_FILE" $M/00000000000000001111111111110000
sha256sum $M/* > $T/dongle-memory.sha256
BW=(bwrap --die-with-parent
    --ro-bind /usr /usr
    --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/bin /bin --symlink usr/bin /sbin
    --ro-bind /etc /etc --proc /proc --ro-bind /sys /sys
    --ro-bind /home /home --ro-bind /var /var --ro-bind /opt /opt --ro-bind /boot /boot --ro-bind /srv /srv)
[ -e /root ] && BW+=(--ro-bind /root /root)
BW+=(--dev /dev --dev-bind-try /dev/pts /dev/pts --dev-bind-try /dev/bus/usb /dev/bus/usb)
for dn in null zero full random urandom tty shm dri snd input; do [ -e "/dev/$dn" ] && BW+=(--dev-bind-try "/dev/$dn" "/dev/$dn"); done
BW+=(--dev-bind /dev/null /dev/dsp --dev-bind "${JS0_SRC:-/dev/null}" /dev/js0)
[ -n "${EXTRA_BW:-}" ] && BW+=($EXTRA_BW)
mkdir -p $T/pty
socat -d -d pty,raw,echo=0,link=$T/pty/sierra2 pty,raw,echo=0,link=$T/pty/sierra2-peer >$T/socat1.log 2>&1 & S1=$!
socat -d -d pty,raw,echo=0,link=$T/pty/ttyS0 pty,raw,echo=0,link=$T/pty/ttyS0-peer >$T/socat2.log 2>&1 & S2=$!
for i in $(seq 1 25); do [ -L $T/pty/sierra2 ] && [ -L $T/pty/ttyS0 ] && break; sleep 0.2; done
# de verre kant van beide seriele lijnen LEEGLEZEN vanaf de eerste seconde: zonder lezer loopt de buffer na ~4,5 min vol en meldt de game "JAMMA: jamma_msg() -1" (run r3)
( exec cat $T/pty/ttyS0-peer > $T/ttyS0-from-game.bin ) & D1=$!
( exec cat $T/pty/sierra2-peer > $T/sierra2-from-game.bin ) & D2=$!
BW+=(--dev-bind "$(readlink -f $T/pty/sierra2)" /dev/sierra2 --dev-bind "$(readlink -f $T/pty/ttyS0)" /dev/ttyS0)
BW+=(--ro-bind /run /run --bind-try /run/user/1000 /run/user/1000 --bind /tmp /tmp)
BW+=(--dir /g3 --ro-bind "$NASDIR" /g3)
BW+=(--overlay-src "$NASDIR/T4User" --overlay "$W/upper$TAG" "$W/work$TAG" /T4User)
EL=$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2; LB=$HOME/.local/share/ravengun/runtime/lib32
LOADER=$HOME/projects/ravengun/tools/rg-elfloader/rg-elfloader
B4=/tmp/claude-1000/-home-deck/6c50f9d7-7b90-4218-b922-50975534fcdb/scratchpad/bench/instr
PRE="$B4/libravengun-ptracer-permit.so:$DROPIN_PRELOAD:$B4/libtrace2.so:$B4/libexit-log.so:$B4/libioctl-log.so:$B4/libravengun-net-log.so:$B4/libravengun-bestand-log.so:$B4/libravengun-fork-log.so"
cat > $W/binnen$TAG.sh <<EOF
cd $V || exit 1
export LD_LIBRARY_PATH="$EL/libs:$LB" RG_ELFLOADER_VERBOSE=1
export RAVENGUN_NET_LOG=$T/net.log RAVENGUN_BESTAND_LOG=$T/bestand.log RAVENGUN_FORK_LOG=$T/fork.log RAVENGUN_PTRACER_LOG=$T/permit.log
export RG_TRACE_LIST=${TRACELIST:-$B4/merged3.lst} RG_TRACE_LOG=$T/trace.log RG_IOCTL_LOG=$T/ioctl.log RG_EXIT_LOG=$T/exit.log RG_FAKE_DSP=1 RG_GLUT_DISPLAY_FROM_ENV=1
export $DROPIN_ENV ${GAME_ENV:-} DISPLAY=$DISPLAY WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=/run/user/1000
exec timeout -s TERM -k 5 ${RUNSECS:-130} env LD_PRELOAD="$PRE" "$LOADER" "$V/game"
EOF
t0=$(date +%s%N); "${BW[@]}" /bin/bash $W/binnen$TAG.sh >$T/out.txt 2>&1 &
BP=$!
[ -n "${HOOK:-}" ] && ( T=$T V=$V bash "$HOOK" >$T/hook.log 2>&1 & )
( while kill -0 $BP 2>/dev/null; do
    for p in $(pgrep -f "$V/game" ); do
      [ -r /proc/$p/task ] && echo "$(( ($(date +%s%N)-t0)/1000000 )) ms pid $p threads $(ls /proc/$p/task | wc -l) state $(awk '/^State/{print $2}' /proc/$p/status) comm $(cat /proc/$p/comm)" >> $T/threads.log
    done; sleep 2; done ) &
SP=$!
wait $BP; rc=$?; t1=$(date +%s%N); kill $SP 2>/dev/null
kill $S1 $S2 $D1 $D2 2>/dev/null
echo "tag=$TAG variant=${VARIANT:-none} rc=$rc life_ms=$(( (t1-t0)/1000000 ))" | tee $T/RESULT.txt

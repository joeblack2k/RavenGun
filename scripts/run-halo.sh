#!/usr/bin/env bash
# RavenGun: start Halo: Fireteam Raven (native Linux build) in de actieve grafische sessie.
#
# De game verwacht het absolute pad /pm/g7/halo. Dat maken we alleen binnen een
# bubblewrap-mountnamespace aan; het SteamOS-rootbestandssysteem blijft ongewijzigd.
# De lokale gamekopie wordt read-only gebruikt; schrijfacties van de game gaan naar
# een aparte overlay (upper), zodat de originele bestanden onaangetast blijven.
#
# Gebruik: run-halo.sh [--debug] [--res BREEDTExHOOGTE] [--no-preload] [-- extra game-args]
set -euo pipefail

DATA="${XDG_DATA_HOME:-$HOME/.local/share}/ravengun"
STATE="${XDG_STATE_HOME:-$HOME/.local/state}/ravengun"
G7="$DATA/runtime/halo-g7"            # bevat halo/ en dump/ (gestaged van de NAS)
LIBDIR="${RAVENGUN_HALO_LIBDIR:-$DATA/runtime/lib}"  # runtime-libraries (FMOD); lib-fmodstub = diagnose zonder geluid
OVL="$DATA/halo-overlay"               # schrijfbare laag van de game
LOGDIR="$STATE/logs"

DEBUG=0; RES=""; PRELOAD=1; EXTRA=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --debug) DEBUG=1 ;;
    --res) RES="$2"; shift ;;
    --no-preload) PRELOAD=0 ;;
    --) shift; EXTRA=("$@"); break ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "Onbekende optie: $1" >&2; exit 2 ;;
  esac
  shift
done

die() { echo "run-halo: $*" >&2; exit 1; }

# --- bron/runtime controleren -------------------------------------------------
[[ -x "$G7/halo/game" ]] || die "game ontbreekt in $G7/halo (staging niet gedaan?)"
[[ -d "$G7/dump" ]]      || die "dump-map ontbreekt in $G7/dump"
(( PRELOAD == 0 )) || [[ -f "$G7/halo/halo.so" ]] || die "halo.so ontbreekt"
command -v bwrap >/dev/null || die "bwrap niet gevonden"
mkdir -p "$OVL/upper" "$OVL/work" "$LIBDIR" "$LOGDIR"

# --- grafische sessie bepalen (niets hardcoden) -------------------------------
RUNDIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
[[ -d "$RUNDIR" ]] || die "XDG_RUNTIME_DIR $RUNDIR bestaat niet"
if [[ -z "${DISPLAY:-}" || -z "${XAUTHORITY:-}" ]]; then
  # Lees DISPLAY/XAUTHORITY uit de opdrachtregel van de kwin_wayland van deze gebruiker
  # (kwin start Xwayland met --xwayland-display en --xwayland-xauthority).
  KWPID=$(pgrep -u "$(id -u)" -x kwin_wayland | head -1 || true)
  [[ -n "$KWPID" ]] || die "geen kwin_wayland van deze gebruiker; is Desktop Mode actief?"
  mapfile -d '' -t KWARGS < "/proc/$KWPID/cmdline"
  for (( i = 0; i < ${#KWARGS[@]} - 1; i++ )); do
    case "${KWARGS[$i]}" in
      --xwayland-display)    DISPLAY="${DISPLAY:-${KWARGS[$((i+1))]}}" ;;
      --xwayland-xauthority) XAUTHORITY="${XAUTHORITY:-${KWARGS[$((i+1))]}}" ;;
    esac
  done
fi
[[ -n "${DISPLAY:-}" ]] || die "DISPLAY niet vast te stellen"
[[ -r "${XAUTHORITY:-/nonexistent}" ]] || die "XAUTHORITY niet leesbaar"
XNUM="${DISPLAY#:}"; XNUM="${XNUM%%.*}"

if [[ -z "$RES" ]] && command -v xrandr >/dev/null; then
  # Werkelijke schermgrootte van de X-root (Xwayland volgt de desktopmodus).
  RES=$(DISPLAY="$DISPLAY" XAUTHORITY="$XAUTHORITY" xrandr --current 2>/dev/null \
        | sed -n 's/.*current \([0-9]*\) x \([0-9]*\).*/\1x\2/p' | head -1 || true)
fi
RES="${RES:-1920x1080}"

TS=$(date +%Y%m%d-%H%M%S)
LOG="$LOGDIR/halo-$TS.log"
echo "run-halo: log $LOG"

# --- sandbox opbouwen ---------------------------------------------------------
BW=(bwrap
  --die-with-parent --unshare-pid --unshare-ipc --unshare-uts --unshare-net
  --ro-bind /usr /usr --symlink usr/lib /lib --symlink usr/lib64 /lib64
  --symlink usr/bin /bin --symlink usr/bin /sbin
  --ro-bind /etc /etc
  --proc /proc --dev /dev
  --dev-bind /dev/dri /dev/dri --dev-bind-try /dev/snd /dev/snd --dev-bind-try /dev/input /dev/input
  --ro-bind /sys /sys --ro-bind-try /run/udev /run/udev
  --tmpfs /tmp --dir /tmp/.X11-unix
  --ro-bind "/tmp/.X11-unix/X$XNUM" "/tmp/.X11-unix/X$XNUM"
  --ro-bind "$XAUTHORITY" /tmp/.Xauthority
  --dir "$RUNDIR" --chmod 0700 "$RUNDIR"
  --ro-bind-try "$RUNDIR/pulse" "$RUNDIR/pulse"
  --ro-bind-try "$RUNDIR/pipewire-0" "$RUNDIR/pipewire-0"
  --overlay-src "$G7/halo" --overlay "$OVL/upper" "$OVL/work" /pm/g7/halo
  --ro-bind "$G7/dump" /pm/g7/dump
  --ro-bind "$LIBDIR" /pm/lib
  --chdir /pm/g7/halo
  --clearenv
  --setenv HOME /pm/g7/halo --setenv PATH /usr/bin
  --setenv DISPLAY "$DISPLAY" --setenv XAUTHORITY /tmp/.Xauthority
  --setenv XDG_RUNTIME_DIR "$RUNDIR"
  --setenv PULSE_SERVER "unix:$RUNDIR/pulse/native"
  --setenv LD_LIBRARY_PATH /pm/lib
  --setenv LANG C.UTF-8
)
(( PRELOAD )) && BW+=(--setenv LD_PRELOAD /pm/g7/halo/halo.so)
(( DEBUG ))   && BW+=(--setenv LD_DEBUG files)
[[ -n "${RAVENGUN_LDD_ONLY:-}" ]] && BW+=(--setenv LD_TRACE_LOADED_OBJECTS 1 --setenv LD_WARN 1 --setenv LD_BIND_NOW 1)

GAMECMD=(/pm/g7/halo/game "-f$RES" -n "${EXTRA[@]}")

{
  echo "== run-halo $TS"
  echo "display=$DISPLAY res=$RES preload=$PRELOAD debug=$DEBUG"
  echo "cmd: ${GAMECMD[*]}"
} >>"$LOG"

# Alleen het eigen proces stoppen bij Ctrl-C/kill van deze launcher.
CHILD=""
cleanup() { [[ -n "$CHILD" ]] && kill -TERM "$CHILD" 2>/dev/null || true; }
trap cleanup INT TERM

set +e
"${BW[@]}" "${GAMECMD[@]}" >>"$LOG" 2>&1 &
CHILD=$!
wait "$CHILD"; RC=$?
set -e
echo "== exit $RC" >>"$LOG"
echo "run-halo: game beëindigd met exitcode $RC (log: $LOG)"
(( DEBUG )) && tail -40 "$LOG"
exit "$RC"

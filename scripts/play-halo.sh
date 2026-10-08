#!/usr/bin/env bash
# Start Halo: Fireteam Raven via TeknoParrot met de Move als lightgun, in de juiste volgorde:
# daemon actief -> uitvoer wapenen (gun + toetsenbord bestaan) -> optioneel opnemen -> game starten.
# TeknoParrot opent de invoerapparaten alleen bij de gamestart; daarom eerst wapenen.
#
# Gebruik: scripts/play-halo.sh [--profile NAAM] [--record TAG] [--no-wait-move]
#          scripts/play-halo.sh --stop        (game stoppen, opname stoppen, uitvoer ontwapenen)
# --profile: TeknoParrot-profiel uit UserProfiles/ (standaard Halo; bv. JurassicPark, zie tp-add-game.py).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CTL="$ROOT/build/ravengunctl"
TP="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64"
WINE="$HOME/.local/share/ravengun/wine/proton-11.0-2/bin"
PREFIX="$HOME/.local/share/TeknoParrotUI/prefixes/shared/wine"
LOGDIR="$HOME/.local/state/ravengun/logs"
UNIT=ravengun-tpui
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# BudgieLoader_x64.exe (64-bit games) of BudgieLoader.exe (32-bit); comm is afgekapt op 15 tekens.
game_running() { pgrep -x "BudgieLoader_x6|BudgieLoader.ex" >/dev/null; }

stop_all() {
  if game_running; then WINEPREFIX="$PREFIX" "$WINE/wineserver" -k || true; fi
  for _ in $(seq 1 20); do game_running || break; sleep 1; done
  systemctl --user stop "$UNIT.service" 2>/dev/null || true
  "$CTL" record --stop >/dev/null 2>&1 || true
  "$CTL" disarm >/dev/null 2>&1 || true
  game_running && { echo "De game draait nog." >&2; exit 1; }
  echo "Game gestopt, uitvoer ontwapend."
}

RECORD=""; WAIT_MOVE=1; PROFILE=Halo
while [[ $# -gt 0 ]]; do
  case "$1" in
    --stop) stop_all; exit 0 ;;
    --profile) PROFILE="${2:?--profile vereist een naam}"; shift 2 ;;
    --record) RECORD="${2:?--record vereist een tag}"; shift 2 ;;
    --no-wait-move) WAIT_MOVE=0; shift ;;
    *) echo "Gebruik: $0 [--profile NAAM] [--record TAG] [--no-wait-move] | --stop" >&2; exit 2 ;;
  esac
done

[[ -f "$TP/UserProfiles/$PROFILE.xml" ]] || { echo "Onbekend profiel: $PROFILE (zie scripts/tp-add-game.py)" >&2; exit 1; }
game_running && { echo "Er draait al een game; eerst '$0 --stop'." >&2; exit 1; }
systemctl --user is-active --quiet ravengund.service || systemctl --user start ravengund.service
for _ in $(seq 1 20); do "$CTL" status >/dev/null 2>&1 && break; sleep 0.5; done

if [[ $WAIT_MOVE -eq 1 ]]; then
  echo "Wachten op de Move (druk op de PS-knop)..."
  for _ in $(seq 1 120); do "$CTL" status 2>/dev/null | grep -q '^Move:.*verbonden (' && break; sleep 1; done
  "$CTL" status 2>/dev/null | grep -q '^Move:.*verbonden (' || { echo "Geen Move binnen 2 minuten." >&2; exit 1; }
fi

"$CTL" arm >/dev/null
for L in usb-RavenGun_Virtual_Lightgun-event-mouse usb-RavenGun_Virtual_Keyboard-event-kbd; do
  for _ in $(seq 1 20); do [[ -e /dev/input/by-id/$L ]] && break; sleep 0.25; done
  [[ -e /dev/input/by-id/$L ]] || { echo "Invoerapparaat ontbreekt: $L" >&2; exit 1; }
done
[[ -n "$RECORD" ]] && "$CTL" record --start --tag "$RECORD" --duration 3600 >/dev/null && echo "Opname gestart ($RECORD)."

# Schermomgeving van de ingelogde sessie (ook bij starten via SSH).
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" DISPLAY="${DISPLAY:-:0}"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}"
if [[ -z "${XAUTHORITY:-}" ]]; then
  KP=$(pgrep -x kwin_wayland | head -1 || true)
  if [[ -n "$KP" ]]; then
    mapfile -d '' ARGS < "/proc/$KP/cmdline"
    for i in "${!ARGS[@]}"; do [[ ${ARGS[$i]} == --xwayland-xauthority ]] && export XAUTHORITY="${ARGS[$((i+1))]}"; done
  fi
fi

mkdir -p "$LOGDIR"; LOG="$LOGDIR/tpui-$(date +%Y%m%d-%H%M%S).log"
systemctl --user stop "$UNIT.service" 2>/dev/null || true
systemd-run --user --unit="$UNIT" --collect --quiet --working-directory="$TP" \
  -E XDG_RUNTIME_DIR -E WAYLAND_DISPLAY -E DISPLAY -E DBUS_SESSION_BUS_ADDRESS ${XAUTHORITY:+-E XAUTHORITY} \
  /bin/sh -c "exec ./TeknoParrotUi --profile=$PROFILE.xml >>'$LOG' 2>&1"

for _ in $(seq 1 90); do game_running && break; sleep 1; done
game_running || { echo "De game is niet gestart; zie $LOG" >&2; exit 1; }
echo "$PROFILE gestart. Stoppen: $0 --stop"

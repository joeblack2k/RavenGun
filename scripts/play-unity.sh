#!/usr/bin/env bash
# Start een Unity-game (Tomb Raider Arcade, Rabbids Hollywood Arcade) ZONDER TeknoParrot en zonder DemulShooter.exe:
# de game draait direct onder Proton met de BepInEx-plugin van DemulShooter, en ravengund is de TCP-klant van die plugin
# (127.0.0.1:33610). Zie docs/decisions.md (route 1) en docs/procedures.md.
#
# Gebruik: scripts/play-unity.sh --game TombRaider|RabbidsHollywood [--no-wait-move] [--wine] [--windowed] [--env K=V]...
#          scripts/play-unity.sh --stop      (game stoppen, Unity-route in de daemon stoppen, uitvoer ontwapenen)
# Volgorde: view en prefix klaarzetten (aagames-setup.py) -> daemon actief -> Move -> uitvoer wapenen -> game starten ->
# daemon vertellen welke game actief is (aagames start). De game draait in de transient user-unit ravengun-tpui, net als
# de TeknoParrot-games, zodat de webapp (Games-lijst) en play-halo.sh --stop hem herkennen.
#   --wine      de plain-wine-runner (proton-11.0-2) in plaats van GE-Proton met DXVK
#   --windowed  zonder -popupwindow (standaard: randloos schermvullend venster)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CTL="${RAVENGUN_CTL:-$ROOT/build/ravengunctl}"
GE="$HOME/.local/share/TeknoParrotUI/proton/GE-Proton11-7-x86_64"
WINE="$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine"
PREFIXES="$HOME/.local/share/ravengun/prefixes"
LOGDIR="$HOME/.local/state/ravengun/logs"
UNIT=ravengun-tpui
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

say()  { echo "$*"; [[ -t 1 ]] || notify-send -a RavenGun -t 8000 -i input-gaming "RavenGun" "$*" 2>/dev/null || true; }
fail() { echo "$*" >&2; [[ -t 2 ]] || notify-send -a RavenGun -t 20000 -i dialog-error "RavenGun" "$*" 2>/dev/null || true; exit 1; }

unit_pids() {
  local cg; cg=$(systemctl --user show -p ControlGroup --value "$UNIT.service" 2>/dev/null) || return 0
  [[ -n "$cg" && -r "/sys/fs/cgroup$cg/cgroup.procs" ]] && cat "/sys/fs/cgroup$cg/cgroup.procs" || true
}
unit_alive() { [[ -n "$(unit_pids)" ]]; }
game_running() {
  local p c
  for p in $(unit_pids); do
    { read -r c < "/proc/$p/comm"; } 2>/dev/null || continue
    [[ "${c,,}" == "game.exe" ]] && return 0
  done
  return 1
}

stop_all() {
  "$CTL" aagames stop >/dev/null 2>&1 || true
  systemctl --user stop "$UNIT.service" 2>/dev/null || true
  for _ in $(seq 1 15); do unit_alive || break; sleep 1; done
  "$CTL" disarm >/dev/null 2>&1 || true
  if unit_alive; then echo "Er draaien nog processen van de game: $(unit_pids | tr '\n' ' ')" >&2; exit 1; fi
  echo "Game gestopt, Unity-route en uitvoer uit."
}

GAME=""; WAIT_MOVE=1; USE_WINE=0; POPUP=1; EXTRA_ENV=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --stop) stop_all; exit 0 ;;
    --game|--profile) GAME="${2:?$1 vereist een naam}"; shift 2 ;;
    --no-wait-move) WAIT_MOVE=0; shift ;;
    --wine) USE_WINE=1; shift ;;
    --windowed) POPUP=0; shift ;;
    --env) [[ "${2:-}" == *=* ]] || { echo "--env vereist K=V" >&2; exit 2; }; EXTRA_ENV+=("$2"); shift 2 ;;
    --record) shift 2 ;;   # alleen voor compatibiliteit met play-halo.sh (opnemen hoort daar)
    *) echo "Gebruik: $0 --game TombRaider|RabbidsHollywood [--no-wait-move] [--wine] [--windowed] [--env K=V]... | --stop" >&2; exit 2 ;;
  esac
done
[[ -n "$GAME" ]] || { echo "Gebruik: $0 --game TombRaider|RabbidsHollywood | --stop" >&2; exit 2; }
[[ "$GAME" =~ ^[A-Za-z0-9_-]{1,64}$ ]] || fail "Ongeldige gamenaam: $GAME"

unit_alive && fail "Er draait al een game; stop die eerst (Esc of '$0 --stop')."

# View en prefix (geen .exe van DemulShooter; alleen de plugin-bestanden uit de lokale map).
VIEW=$(python3 -I "$ROOT/scripts/aagames-setup.py" "$GAME" --no-prefix --print-view) || fail "View maken mislukt voor $GAME (zie scripts/aagames-setup.py)"
python3 -I "$ROOT/scripts/aagames-setup.py" "$GAME" >/dev/null || fail "Prefix maken mislukt"
PFX_ROOT="$PREFIXES/unity-aagames"
[[ -f "$VIEW/Game.exe" ]] || fail "Game.exe ontbreekt in $VIEW"

systemctl --user is-active --quiet ravengund.service || systemctl --user start ravengund.service
for _ in $(seq 1 20); do "$CTL" status >/dev/null 2>&1 && break; sleep 0.5; done

if [[ $WAIT_MOVE -eq 1 ]]; then
  say "Zet de Move aan: druk op de PS-knop."
  for _ in $(seq 1 120); do "$CTL" status 2>/dev/null | grep -q '^Move:.*verbonden (' && break; sleep 1; done
  "$CTL" status 2>/dev/null | grep -q '^Move:.*verbonden (' || fail "Geen Move gevonden binnen 2 minuten. Druk op PS en start opnieuw."
fi

"$CTL" arm >/dev/null

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

# De plugin laadt via doorstop (winhttp.dll in de gamemap): de override moet "native, dan builtin" zijn.
SDENV=(-E "WINEDLLOVERRIDES=winhttp=n,b" -E "WINEDEBUG=-all")
for E in "${EXTRA_ENV[@]}"; do SDENV+=(-E "$E"); done
GAMEARGS=(Game.exe)
[[ $POPUP -eq 1 ]] && GAMEARGS+=(-popupwindow)
if [[ $USE_WINE -eq 1 ]]; then
  PFX="$PFX_ROOT/pfx"
  CMD=("$WINE" "${GAMEARGS[@]}")
  SDENV+=(-E "WINEPREFIX=$PFX")
else
  CMD=("$GE/proton" run "${GAMEARGS[@]}")
  SDENV+=(-E "STEAM_COMPAT_DATA_PATH=$PFX_ROOT" -E "STEAM_COMPAT_CLIENT_INSTALL_PATH=$HOME/.local/share/Steam")
fi

mkdir -p "$LOGDIR"; LOG="$LOGDIR/unity-$GAME-$(date +%Y%m%d-%H%M%S).log"
systemctl --user stop "$UNIT.service" 2>/dev/null || true
systemd-run --user --unit="$UNIT" --collect --quiet --working-directory="$VIEW" -p TimeoutStopSec=10 \
  -E XDG_RUNTIME_DIR -E WAYLAND_DISPLAY -E DISPLAY -E DBUS_SESSION_BUS_ADDRESS ${XAUTHORITY:+-E XAUTHORITY} "${SDENV[@]}" \
  /bin/sh -c 'exec "$@" >>"$0" 2>&1' "$LOG" "${CMD[@]}"

for _ in $(seq 1 "${RAVENGUN_START_WAIT:-120}"); do game_running && break; sleep 1; done
game_running || { "$CTL" disarm >/dev/null 2>&1 || true; fail "$GAME is niet gestart; zie $LOG"; }
"$CTL" aagames start "$GAME" >/dev/null || fail "De daemon kon de Unity-route niet starten (ravengunctl aagames start $GAME)"
say "$GAME start. Move verbonden, lightgun klaar."

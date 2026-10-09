#!/usr/bin/env bash
# Start Halo: Fireteam Raven via TeknoParrot met de Move als lightgun, in de juiste volgorde:
# daemon actief -> uitvoer wapenen (gun + toetsenbord bestaan) -> optioneel opnemen -> game starten.
# TeknoParrot opent de invoerapparaten alleen bij de gamestart; daarom eerst wapenen.
#
# Gebruik: scripts/play-halo.sh [--profile NAAM] [--record TAG] [--no-wait-move]
#          scripts/play-halo.sh --stop        (game stoppen, opname stoppen, uitvoer ontwapenen)
# --profile: TeknoParrot-profiel uit UserProfiles/ (standaard Halo; bv. JurassicPark, zie tp-add-game.py).
# --env K=V: extra omgevingsvariabele voor TeknoParrot/Wine/de game (herhaalbaar). Daarnaast wordt, indien
#            aanwezig, UserProfiles/<PROFIEL>.env gelezen (regels K=V, # = commentaar) vanuit de TeknoParrot-map.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CTL="$ROOT/build/ravengunctl"
TP="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64"
WINE="$HOME/.local/share/ravengun/wine/proton-11.0-2/bin"
PREFIX="$HOME/.local/share/TeknoParrotUI/prefixes/shared/wine"
LOGDIR="$HOME/.local/state/ravengun/logs"
UNIT=ravengun-tpui
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# Meldingen: in een terminal op stdout/stderr; gestart vanaf het bureaublad of Steam (geen terminal) ook als
# bureaubladmelding, zodat de speler ziet wat er gebeurt (bv. "druk op PS").
say()  { echo "$*"; [[ -t 1 ]] || notify-send -a RavenGun -i input-gaming "RavenGun" "$*" 2>/dev/null || true; }
fail() { echo "$*" >&2; [[ -t 2 ]] || notify-send -a RavenGun -u critical -i dialog-error "RavenGun" "$*" 2>/dev/null || true; exit 1; }

# Alle processen van een test leven in de cgroup van de transient user-unit ravengun-tpui (TeknoParrotUi, de
# Proton-/Wine-processen, wineserver, de game). Daardoor kun je ze per profiel herkennen en allemaal in een keer
# stoppen, zonder een andere TeknoParrot-sessie of wineserver van de speler aan te raken.
unit_pids() {
  local cg; cg=$(systemctl --user show -p ControlGroup --value "$UNIT.service" 2>/dev/null) || return 0
  [[ -n "$cg" && -r "/sys/fs/cgroup$cg/cgroup.procs" ]] && cat "/sys/fs/cgroup$cg/cgroup.procs" || true
}

# Procesnamen (comm: kleine letters, afgekapt op 15 tekens) die "de game draait" betekenen voor dit profiel:
# de loaders (BudgieLoader*, OpenParrotLoade*) plus de bestandsnamen uit ExecutableName, GamePath en GamePath2.
GAME_COMMS=""
build_game_comms() {
  local f="$TP/UserProfiles/$PROFILE.xml" v n
  GAME_COMMS="|budgieloader_x6|budgieloader.ex|openparrotloade|"
  [[ -f "$f" ]] || return 0
  while IFS= read -r v; do
    IFS=';' read -ra parts <<< "$v"
    for n in "${parts[@]}"; do
      n="${n//\\//}"; n="${n##*/}"; n="${n,,}"; n="${n:0:15}"
      [[ -n "$n" ]] && GAME_COMMS+="$n|"
    done
  done < <(sed -n 's|.*<\(ExecutableName\|GamePath2\?\)>\(.*\)</\1>.*|\2|p' "$f")
}

game_running() {
  local p c
  [[ -n "$GAME_COMMS" ]] || build_game_comms
  for p in $(unit_pids); do
    { read -r c < "/proc/$p/comm"; } 2>/dev/null || continue
    c="${c,,}"
    [[ "$GAME_COMMS" == *"|$c|"* ]] && return 0
  done
  return 1
}

# Nog iets van de test over (welk proces dan ook in de unit-cgroup)?
unit_alive() { [[ -n "$(unit_pids)" ]]; }

stop_all() {
  # Oude route voor Halo: een BudgieLoader buiten de unit (bv. met de hand gestart) via de wineserver van de
  # plain-wine-prefix, die alleen Halo gebruikt.
  if pgrep -x "BudgieLoader_x6|BudgieLoader.ex" >/dev/null; then WINEPREFIX="$PREFIX" "$WINE/wineserver" -k 2>/dev/null || true; fi
  # De unit stoppen beeindigt TeknoParrotUi en alles wat het startte (wine, wineserver, game), met SIGKILL na 10 s.
  systemctl --user stop "$UNIT.service" 2>/dev/null || true
  for _ in $(seq 1 15); do unit_alive || break; sleep 1; done
  "$CTL" record --stop >/dev/null 2>&1 || true
  "$CTL" aagames stop >/dev/null 2>&1 || true   # Unity-route (play-unity.sh): de rumble gaat weer terug naar de trekker
  "$CTL" disarm >/dev/null 2>&1 || true
  if unit_alive; then echo "Er draaien nog processen van de test: $(unit_pids | tr '\n' ' ')" >&2; exit 1; fi
  echo "Game gestopt, uitvoer ontwapend."
}

RECORD=""; WAIT_MOVE=1; PROFILE=Halo; EXTRA_ENV=(); USE_TP=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --stop) stop_all; exit 0 ;;
    --profile) PROFILE="${2:?--profile vereist een naam}"; shift 2 ;;
    --env) [[ "${2:-}" == *=* ]] || { echo "--env vereist K=V" >&2; exit 2; }; EXTRA_ENV+=("$2"); shift 2 ;;
    --record) RECORD="${2:?--record vereist een tag}"; shift 2 ;;
    --no-wait-move) WAIT_MOVE=0; shift ;;
    --teknoparrot) USE_TP=1; shift ;;
    *) echo "Gebruik: $0 [--profile NAAM] [--record TAG] [--env K=V]... [--no-wait-move] [--teknoparrot] | --stop" >&2; exit 2 ;;
  esac
done

# Unity-route (zonder TeknoParrot): het profiel staat in packaging/aagames-games.json.
if [[ $USE_TP -eq 0 ]] && grep -qE "^[[:space:]]{4}\"$PROFILE\":[[:space:]]*\{" "$ROOT/packaging/aagames-games.json" 2>/dev/null; then
  UARGS=(--game "$PROFILE")
  [[ $WAIT_MOVE -eq 0 ]] && UARGS+=(--no-wait-move)
  for E in "${EXTRA_ENV[@]}"; do UARGS+=(--env "$E"); done
  # Geen exec: de webapp herkent een lopende start aan het proces van dit script (games.cpp, script_state).
  "$ROOT/scripts/play-unity.sh" "${UARGS[@]}"
  exit $?
fi
[[ -f "$TP/UserProfiles/$PROFILE.xml" ]] || fail "Onbekend profiel: $PROFILE (zie scripts/tp-add-game.py)"
game_running && fail "Er draait al een game; stop die eerst (Esc of '$0 --stop')."
systemctl --user is-active --quiet ravengund.service || systemctl --user start ravengund.service
for _ in $(seq 1 20); do "$CTL" status >/dev/null 2>&1 && break; sleep 0.5; done

if [[ $WAIT_MOVE -eq 1 ]]; then
  say "Zet de Move aan: druk op de PS-knop."
  for _ in $(seq 1 120); do "$CTL" status 2>/dev/null | grep -q '^Move:.*verbonden (' && break; sleep 1; done
  "$CTL" status 2>/dev/null | grep -q '^Move:.*verbonden (' || fail "Geen Move gevonden binnen 2 minuten. Druk op PS en start opnieuw."
fi

"$CTL" arm >/dev/null
for L in usb-RavenGun_Virtual_Lightgun-event-mouse usb-RavenGun_Virtual_Keyboard-event-kbd; do
  for _ in $(seq 1 20); do [[ -e /dev/input/by-id/$L ]] && break; sleep 0.25; done
  [[ -e /dev/input/by-id/$L ]] || fail "Invoerapparaat ontbreekt: $L"
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

# Per-profiel omgevingsvariabelen: UserProfiles/<PROFIEL>.env, daarna --env (laatste wint).
ENVFILE="$TP/UserProfiles/$PROFILE.env"
if [[ -f "$ENVFILE" ]]; then
  while IFS= read -r L || [[ -n "$L" ]]; do
    L="${L%%#*}"; L="${L#"${L%%[![:space:]]*}"}"; L="${L%"${L##*[![:space:]]}"}"
    [[ "$L" == *=* ]] && EXTRA_ENV=("$L" "${EXTRA_ENV[@]}")
  done < "$ENVFILE"
fi
SDENV=(); for E in "${EXTRA_ENV[@]}"; do SDENV+=(-E "$E"); done
[[ ${#EXTRA_ENV[@]} -gt 0 ]] && echo "Extra omgeving: ${EXTRA_ENV[*]}"

mkdir -p "$LOGDIR"; LOG="$LOGDIR/tpui-$(date +%Y%m%d-%H%M%S).log"
systemctl --user stop "$UNIT.service" 2>/dev/null || true
systemd-run --user --unit="$UNIT" --collect --quiet --working-directory="$TP" -p TimeoutStopSec=10 \
  -E XDG_RUNTIME_DIR -E WAYLAND_DISPLAY -E DISPLAY -E DBUS_SESSION_BUS_ADDRESS ${XAUTHORITY:+-E XAUTHORITY} "${SDENV[@]}" \
  /bin/sh -c "exec ./TeknoParrotUi --profile=$PROFILE.xml >>'$LOG' 2>&1"

for _ in $(seq 1 "${RAVENGUN_START_WAIT:-120}"); do game_running && break; sleep 1; done
game_running || fail "$PROFILE is niet gestart; zie $LOG"
say "$PROFILE start. Move verbonden, lightgun klaar."

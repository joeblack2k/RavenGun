#!/usr/bin/env bash
# OLED-bescherming en statusbord: staat er langer dan IDLE_S geen game op de tv, dan komt er een zwart scherm met een
# langzaam bewegend statusbord (zwarte pixels staan op een OLED uit). Zodra er een game MET BEELD start, verdwijnt het
# binnen enkele seconden. De tv kan niet uit vanwege de HDMI-handshake; daarom geen DPMS/blanking.
# Het zwarte scherm is `ravengun --saver` (eigen Qt-venster); wat erop staat komt uit saver-status.txt
# (scripts/rg-saver-status.sh) en uit saver-game.txt, dat dit script elke paar seconden schrijft.
#
# Draait er nu een spel? Drie antwoorden (`--game-state`):
#   zichtbaar    er draait een spel met beeld op de tv: het bord gaat weg, je ziet het spel zelf.
#   onzichtbaar  er draait een spel zonder beeld (een meting met een headless gamescope of zonder DISPLAY): het bord
#                komt meteen en zegt dat er een spel draait -- anders zie je een leeg bureaublad terwijl er gemeten wordt.
#   geen         er draait niets: het bord komt na IDLE_S seconden.
# "Er draait een spel" zegt niets over de uitslag; die staat in saver-status.txt.
# Gebruik: scripts/oled-saver.sh --start | --stop | --status | --stop-view | --game-state   (draait als user-unit ravengun-oled-saver)
set -euo pipefail
IDLE_S="${RG_SAVER_IDLE_S:-600}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/scripts/rg-paths.sh"
UNIT=ravengun-oled-saver; VIEW=ravengun-oled-view
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

STATE="${XDG_STATE_HOME:-$HOME/.local/state}/ravengun"
PROC="${RG_SAVER_PROC:-/proc}"          # voor de test: een namaak-/proc met <pid>/environ

# De processen van deze gebruiker als regels "pid opdrachtregel". RG_SAVER_PS_FILE vervangt de echte lijst (test).
proces_lijst() {
  if [[ -n "${RG_SAVER_PS_FILE:-}" ]]; then cat "$RG_SAVER_PS_FILE"; else ps -u "$(id -u)" -o pid=,args= 2>/dev/null || true; fi
}
# Heeft dit proces een scherm (DISPLAY of WAYLAND_DISPLAY in zijn omgeving)? Niet te lezen = geen scherm aannemen.
heeft_scherm() {
  [[ -r "$PROC/$1/environ" ]] || return 1
  tr '\0' '\n' < "$PROC/$1/environ" 2>/dev/null | grep -Eq '^(DISPLAY|WAYLAND_DISPLAY)=.' 
}
# Heeft een run het scherm genomen? scripts/rg-tv-env.sh schrijft voor een start met beeld een houder met het pid van die run
# (en de starttijd van het proces, zodat een later proces met hetzelfde pid niet telt). Leeft dat proces, dan staat er een spel
# op de tv, welk programma het ook is; een houder van een proces dat weg is wordt opgeruimd.
HOLD_DIR="${RG_SAVER_HOLD_DIR:-$XDG_RUNTIME_DIR/ravengun/tv-hold}"
scherm_genomen() {
  local f pid start nu genomen=1
  [[ -d "$HOLD_DIR" ]] || return 1
  for f in "$HOLD_DIR"/*; do
    [[ -f "$f" ]] || continue
    pid=${f##*/}; start=$(head -1 "$f" 2>/dev/null || true)
    nu=""; if [[ -r "$PROC/$pid/stat" ]]; then nu=$(awk '{print $22}' "$PROC/$pid/stat" 2>/dev/null || true); fi
    if [[ "$pid" =~ ^[0-9]+$ && -d "$PROC/$pid" && ( -z "$start" || "$start" == 0 || -z "$nu" || "$nu" == "$start" ) ]]; then genomen=0
    else rm -f "$f" 2>/dev/null || true; fi
  done
  return $genomen
}
# geen | zichtbaar | onzichtbaar.
#   0. Heeft een run het scherm genomen (een houder van rg-tv-env.sh met een levend proces), dan zichtbaar. Dit gaat voor alles:
#      het hangt niet af van welk programma er draait.
#   1. Een gamescope beslist als er een is: een gewone gamescope staat op de tv, een met `--backend headless` niet.
#      (Wat BINNEN een headless gamescope draait heeft wel een DISPLAY, maar dat is het onzichtbare scherm van die gamescope.)
#   2. Zonder gamescope: een start van ons (rg-direct --run, een lader of Game.exe) telt als
#      zichtbaar als dat proces een scherm in zijn omgeving heeft, anders als onzichtbaar.
#   3. De eenheid van de game (ravengun-tpui, ook de native route) telt als zichtbaar.
game_state() {
  local gs_tv=0 gs_headless=0 met_scherm=0 zonder_scherm=0 pid args
  if scherm_genomen; then echo zichtbaar; return; fi
  while read -r pid args; do
    [[ -n "$pid" && -n "${args:-}" ]] || continue
    case "$args" in
      *oled-saver.sh*|*rg-saver-status*) continue ;;                      # wijzelf
      gamescope\ *|*/gamescope\ *)
        if [[ "$args" == *"--backend headless"* ]]; then gs_headless=1; else gs_tv=1; fi ;;
      *rg-direct\ --run*|*BudgieLoader*|*Game.exe*)
        if heeft_scherm "$pid"; then met_scherm=1; else zonder_scherm=1; fi ;;
    esac
  done < <(proces_lijst)
  if (( gs_tv )); then echo zichtbaar; return; fi
  if (( gs_headless )); then echo onzichtbaar; return; fi
  if [[ -z "${RG_SAVER_PS_FILE:-}" ]] && systemctl --user is-active --quiet ravengun-tpui.service 2>/dev/null; then echo zichtbaar; return; fi
  if (( met_scherm )); then echo zichtbaar; elif (( zonder_scherm )); then echo onzichtbaar; else echo geen; fi
}
schrijf_game_state() {   # atomair, zodat het bord nooit een half bestand leest
  mkdir -p "$STATE"
  printf '%s\n' "$1" > "$STATE/saver-game.txt.tmp" && mv -f "$STATE/saver-game.txt.tmp" "$STATE/saver-game.txt"
}
view_on() { systemctl --user is-active --quiet "$VIEW.service"; }
view_start() {
  # Eigen Qt-venster (geen browser: die kan een balk tonen die inbrandt). Geïnstalleerd: <prefix>/bin/ravengun; in de repo
  # de app-build (scripts/build-app.sh).
  local bin="$RG_BIN/ravengun"
  [[ -x "$bin" ]] || bin="$ROOT/build-app/ravengun"
  [[ -x "$bin" ]] || { echo "ravengun-app niet gevonden (scripts/build-app.sh)" >&2; return 0; }
  systemd-run --user --unit="$VIEW" --collect --quiet \
    -E XDG_RUNTIME_DIR -E WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" -E DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}" \
    "$bin" --saver >/dev/null 2>&1 || true
}
view_stop() {
  systemctl --user stop "$VIEW.service" 2>/dev/null || true
  # Oude installaties: de Brave-kiosk (flatpak start Brave buiten de cgroup van de unit). Ruim de processen met het
  # oude saver-profiel op (alleen die: andere Brave-vensters van de gebruiker blijven staan).
  local prof="--user-data-dir=$HOME/.local/state/ravengun/saver-browser" pid
  for pid in $(pgrep -u "$(id -u)" -f -- "$prof" || true); do [[ $pid != $$ ]] && kill "$pid" 2>/dev/null || true; done
}

case "${1:-}" in
  --start)
    systemctl --user is-active --quiet "$UNIT.service" && { echo "draait al"; exit 0; }
    systemd-run --user --unit="$UNIT" --collect --quiet -E XDG_RUNTIME_DIR -E RG_SAVER_IDLE_S="$IDLE_S" "$0" --loop
    echo "OLED-bescherming aan (zwart scherm na $IDLE_S s zonder game)" ;;
  --stop) systemctl --user stop "$UNIT.service" 2>/dev/null || true; view_stop; echo "OLED-bescherming uit" ;;
  --game-state) game_state ;;
  --stop-view) view_stop ;;   # ExecStopPost van de unit: ook bij 'systemctl stop' het zwarte scherm dichtdoen
  --status) systemctl --user is-active "$UNIT.service" || true; view_on && echo "zwart scherm staat aan" || echo "zwart scherm staat uit" ;;
  --loop)
    trap 'view_stop' EXIT
    trap 'exit 0' TERM INT   # anders slaat bash bij SIGTERM de EXIT-trap over
    idle=0
    while :; do
      gs=$(game_state)
      schrijf_game_state "$gs"
      case "$gs" in
        zichtbaar)   idle=0; if view_on; then view_stop; fi ;;
        onzichtbaar) idle=$IDLE_S; if ! view_on; then view_start; fi ;;
        *)           idle=$((idle+3)); if (( idle >= IDLE_S )) && ! view_on; then view_start; fi ;;
      esac
      sleep 3
    done ;;
  *) echo "Gebruik: $0 --start|--stop|--status|--game-state" >&2; exit 2 ;;
esac

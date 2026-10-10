#!/usr/bin/env bash
# OLED-bescherming: staat er langer dan IDLE_S geen game op de tv, dan komt er een zwart scherm met een klein,
# langzaam bewegend klokje (zwarte pixels staan op een OLED uit). Zodra er een game start, verdwijnt het binnen
# enkele seconden. De tv kan niet uit vanwege de HDMI-handshake; daarom geen DPMS/blanking.
# Het zwarte scherm is `ravengun --saver` (eigen Qt-venster).
# Gebruik: scripts/oled-saver.sh --start | --stop | --status | --stop-view      (draait als user-unit ravengun-oled-saver)
set -euo pipefail
IDLE_S="${RG_SAVER_IDLE_S:-600}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/scripts/rg-paths.sh"
UNIT=ravengun-oled-saver; VIEW=ravengun-oled-view
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

game_running() {
  systemctl --user is-active --quiet ravengun-tpui.service && return 0
  pgrep -x "BudgieLoader_x6|BudgieLoader.ex|Game.exe|gamescope" >/dev/null && return 0
  return 1
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
  --stop-view) view_stop ;;   # ExecStopPost van de unit: ook bij 'systemctl stop' het zwarte scherm dichtdoen
  --status) systemctl --user is-active "$UNIT.service" || true; view_on && echo "zwart scherm staat aan" || echo "zwart scherm staat uit" ;;
  --loop)
    trap 'view_stop' EXIT
    trap 'exit 0' TERM INT   # anders slaat bash bij SIGTERM de EXIT-trap over
    idle=0
    while :; do
      if game_running; then idle=0; view_on && view_stop
      else idle=$((idle+3)); if (( idle >= IDLE_S )) && ! view_on; then view_start; fi
      fi
      sleep 3
    done ;;
  *) echo "Gebruik: $0 --start|--stop|--status" >&2; exit 2 ;;
esac

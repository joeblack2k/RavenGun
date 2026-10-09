#!/usr/bin/env bash
# OLED-bescherming: staat er langer dan IDLE_S geen game op de tv, dan komt er een zwart scherm met een klein,
# langzaam bewegend klokje (zwarte pixels staan op een OLED uit). Zodra er een game start, verdwijnt het binnen
# enkele seconden. De tv kan niet uit vanwege de HDMI-handshake; daarom geen DPMS/blanking.
# Gebruik: scripts/oled-saver.sh --start | --stop | --status      (draait als user-unit ravengun-oled-saver)
set -euo pipefail
IDLE_S="${RG_SAVER_IDLE_S:-600}"
UNIT=ravengun-oled-saver; VIEW=ravengun-oled-view
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

game_running() {
  systemctl --user is-active --quiet ravengun-tpui.service && return 0
  pgrep -x "BudgieLoader_x6|BudgieLoader.ex|Game.exe|gamescope" >/dev/null && return 0
  return 1
}
view_on() { systemctl --user is-active --quiet "$VIEW.service"; }
view_start() {
  local html='<body style="margin:0;background:#000;overflow:hidden;cursor:none"><div id=c style="position:absolute;color:#333;font:28px sans-serif"></div><script>function m(){var c=document.getElementById("c"),d=new Date();c.textContent=("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2);c.style.left=Math.random()*85+"%";c.style.top=Math.random()*90+"%"}m();setInterval(m,20000)</script>'
  systemd-run --user --unit="$VIEW" --collect --quiet \
    -E XDG_RUNTIME_DIR -E WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" -E DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-unix:path=$XDG_RUNTIME_DIR/bus}" \
    flatpak run com.brave.Browser --user-data-dir="$HOME/.local/state/ravengun/saver-browser" --kiosk \
    --app="data:text/html,$html" --ozone-platform-hint=auto --no-first-run --no-default-browser-check --noerrdialogs \
    --disable-infobars --disable-session-crashed-bubble >/dev/null 2>&1 || true
}
view_stop() { systemctl --user stop "$VIEW.service" 2>/dev/null || true; }

case "${1:-}" in
  --start)
    systemctl --user is-active --quiet "$UNIT.service" && { echo "draait al"; exit 0; }
    systemd-run --user --unit="$UNIT" --collect --quiet -E XDG_RUNTIME_DIR -E RG_SAVER_IDLE_S="$IDLE_S" "$0" --loop
    echo "OLED-bescherming aan (zwart scherm na $IDLE_S s zonder game)" ;;
  --stop) systemctl --user stop "$UNIT.service" 2>/dev/null || true; view_stop; echo "OLED-bescherming uit" ;;
  --status) systemctl --user is-active "$UNIT.service" || true; view_on && echo "zwart scherm staat aan" || echo "zwart scherm staat uit" ;;
  --loop)
    trap 'view_stop' EXIT
    idle=0
    while :; do
      if game_running; then idle=0; view_on && view_stop
      else idle=$((idle+3)); if (( idle >= IDLE_S )) && ! view_on; then view_start; fi
      fi
      sleep 3
    done ;;
  *) echo "Gebruik: $0 --start|--stop|--status" >&2; exit 2 ;;
esac

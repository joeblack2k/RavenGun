#!/usr/bin/env bash
# Opent het RavenGun-bedieningspaneel (web-UI van de daemon) in een eigen appvenster, zonder browserbalken.
# Start de daemon als die nog niet draait.
set -euo pipefail
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
systemctl --user is-active --quiet ravengund.service || systemctl --user start ravengund.service
PORT=8765
for _ in $(seq 1 20); do (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null && break; sleep 0.25; done
PROFILE_DIR="$HOME/.local/state/ravengun/app-browser"; mkdir -p "$PROFILE_DIR"
exec flatpak run com.brave.Browser --user-data-dir="$PROFILE_DIR" --app="http://127.0.0.1:$PORT/" \
  --class=RavenGun --ozone-platform-hint=auto --no-first-run --no-default-browser-check --window-size=1280,860 >/dev/null 2>&1

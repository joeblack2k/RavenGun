#!/usr/bin/env bash
# Opent het RavenGun-bedieningspaneel (web-UI van de daemon) in een eigen appvenster, zonder browserbalken.
# Start de daemon als die nog niet draait. Gebruikt een eigen browserprofiel (raakt je gewone Brave niet).
#
# Brave-meldingen: in dat eigen profiel worden vooraf de statistiekmeldingen ("privacy-preserving product analytics",
# P3A) uitgezet en als gelezen gemarkeerd; verder gaan de vlaggen hieronder over eerste-start-schermen, herstelbalken
# en de standaardbrowser-vraag. Dit is niet op de echte tv geprobeerd: ziet u toch een balk, meld het dan.
set -euo pipefail
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
systemctl --user is-active --quiet ravengund.service || systemctl --user start ravengund.service
PORT=8765
for _ in $(seq 1 20); do (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2>/dev/null && break; sleep 0.25; done
PROFILE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}/ravengun/app-browser"; mkdir -p "$PROFILE_DIR"

# Alleen als de browser met dit profiel niet al draait (hij schrijft "Local State" bij het afsluiten terug).
if [[ ! -e "$PROFILE_DIR/SingletonLock" ]] && command -v python3 >/dev/null 2>&1; then
  python3 - "$PROFILE_DIR/Local State" <<'PY' || true
import json, sys
p = sys.argv[1]
try:
    with open(p) as f: d = json.load(f)
except Exception:
    d = {}
b = d.setdefault("brave", {})
b.setdefault("p3a", {}).update({"enabled": False, "notice_acknowledged": True})
b.setdefault("stats", {})["reporting_enabled"] = False
b.setdefault("rewards", {})["notifications_disabled"] = True
with open(p, "w") as f: json.dump(d, f)
PY
fi

# Vaste venstergrootte en -positie (onder Wayland beslist de vensterbeheerder over de positie).
exec flatpak run com.brave.Browser --user-data-dir="$PROFILE_DIR" --app="http://127.0.0.1:$PORT/" \
  --class=RavenGun --ozone-platform-hint=auto --window-size=1280,860 --window-position=80,40 \
  --no-first-run --no-default-browser-check --disable-infobars --hide-crash-restore-bubble \
  --disable-session-crashed-bubble --disable-features=Translate,MediaRouter,BraveRewards,BraveVPN,BraveWallet,BraveNews \
  --disable-sync --disable-background-networking --disable-component-update \
  >/dev/null 2>&1

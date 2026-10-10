#!/usr/bin/env bash
# Herstart ravengund alleen als er geen game draait (een herstart tijdens een game laat de game zijn
# virtuele invoerapparaten verliezen). Met --wait: wacht tot het vrij is (max. 30 min). Exit 3 = game actief.
set -euo pipefail
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
busy() {
  systemctl --user is-active --quiet ravengun-tpui.service && return 0
  pgrep -x "BudgieLoader_x6|BudgieLoader.ex|Game.exe|Shell.exe|Transformers2.ex|wineserver|gamescope" >/dev/null && return 0
  return 1
}
if [[ "${1:-}" == --wait ]]; then
  for _ in $(seq 1 600); do busy || break; sleep 3; done
fi
if busy; then echo "Er draait een game; daemon NIET herstart." >&2; exit 3; fi
systemctl --user restart ravengund.service
for _ in $(seq 1 30); do "$(dirname "$0")/../build/ravengunctl" status >/dev/null 2>&1 && { echo "Daemon herstart."; exit 0; }; sleep 0.5; done
echo "Daemon herstart, maar antwoordt (nog) niet." >&2; exit 1

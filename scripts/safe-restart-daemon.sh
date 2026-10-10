#!/usr/bin/env bash
# Herstart ravengund alleen als er geen game draait (een herstart tijdens een game laat de game zijn
# virtuele invoerapparaten verliezen). Met --wait: wacht tot het vrij is (max. 30 min). Exit 3 = game actief.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/rg-paths.sh"   # RG_BIN: repo (build/) of installatie (<prefix>/bin)
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
busy() {
  systemctl --user is-active --quiet ravengun-tpui.service && return 0
  pgrep -x "BudgieLoader_x6|BudgieLoader.ex|Game.exe|Shell.exe|Transformers2.ex|wineserver|gamescope" >/dev/null && return 0
  return 1
}
if [[ "${1:-}" == --wait ]]; then
  for _ in $(seq 1 600); do busy || break; sleep 3; done
fi
if busy; then echo "A game is running; the engine was NOT restarted." >&2; exit 3; fi
"${RAVENGUN_SYSTEMCTL:-systemctl}" --user restart ravengund.service
for _ in $(seq 1 30); do "$RG_BIN/ravengunctl" status >/dev/null 2>&1 && { echo "Engine restarted."; exit 0; }; sleep 0.5; done
echo "Engine restarted, but it does not answer (yet)." >&2; exit 1

#!/usr/bin/env bash
# Stopt alleen de door run-halo.sh gestarte Halo-sessie (via het pidfile), niets anders.
set -euo pipefail
PIDF="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/ravengun/halo.pid"
[[ -f "$PIDF" ]] || { echo "stop-halo: geen actieve Halo-launcher"; exit 0; }
PID=$(cat "$PIDF")
if ! grep -q 'run-halo.sh' "/proc/$PID/cmdline" 2>/dev/null; then
  echo "stop-halo: pidfile verouderd (pid $PID is geen run-halo.sh); verwijderd"; rm -f "$PIDF"; exit 0
fi
kill -TERM "$PID"
for _ in $(seq 1 20); do kill -0 "$PID" 2>/dev/null || { echo "stop-halo: gestopt"; exit 0; }; sleep 0.5; done
echo "stop-halo: launcher $PID reageert niet op TERM binnen 10 s" >&2; exit 1

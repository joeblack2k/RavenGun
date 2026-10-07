#!/bin/sh
# Integratietest zonder hardware: ravengund --no-hid, socket- en maprechten, enkele instantie,
# ravengunctl status/recenter/watch en nette shutdown op SIGTERM.
# gebruik: ipc_smoke.sh <ravengund> <ravengunctl>
set -u
D="$1"; C="$2"
T=$(mktemp -d) || exit 1
export XDG_RUNTIME_DIR="$T"
fail() { echo "FAIL: $*"; kill "$PID" 2>/dev/null; rm -rf "$T"; exit 1; }

"$D" --no-hid 2>"$T/log" &
PID=$!
i=0
while [ ! -S "$T/ravengun/ravengund.sock" ]; do
  i=$((i+1)); [ $i -gt 50 ] && fail "socket verschijnt niet"; sleep 0.1
done

[ "$(stat -c %a "$T/ravengun")" = "700" ] || fail "map niet 0700"
[ "$(stat -c %a "$T/ravengun/ravengund.sock")" = "600" ] || fail "socket niet 0600"

"$D" --no-hid 2>/dev/null
[ $? -eq 69 ] || fail "tweede instantie niet geweigerd (verwacht exit 69)"

OUT=$("$C" status --json) || fail "status faalde"
echo "$OUT" | grep -q '"ok":true' || fail "status niet ok"
echo "$OUT" | grep -q '"hid_enabled":false' || fail "hid_enabled ontbreekt"
echo "$OUT" | grep -q '"schema_version":1' || fail "schema_version ontbreekt"
"$C" status >/dev/null || fail "status (mens) faalde"

"$C" recenter 2>/dev/null
[ $? -eq 75 ] || fail "recenter zonder Move moet exit 75 geven"

"$C" filter --off >/dev/null || fail "filter --off faalde"
"$C" status --json | grep -q '"filter":{"enabled":false' || fail "filter niet uit"

"$C" watch --duration 0.5 --json >/dev/null || fail "watch faalde"

kill -TERM "$PID"
wait "$PID"
RC=$?
[ $RC -eq 0 ] || fail "daemon exitcode $RC na SIGTERM"
[ ! -e "$T/ravengun/ravengund.sock" ] || fail "socket niet opgeruimd"

XDG_RUNTIME_DIR="$T" "$C" status 2>/dev/null
[ $? -eq 69 ] || fail "status zonder daemon moet exit 69 geven"

rm -rf "$T"
echo "ipc_smoke: alles in orde"

#!/bin/sh
# Integratietest zonder hardware: ravengund --no-hid, socket- en maprechten, enkele instantie,
# ravengunctl status/recenter/watch en nette shutdown op SIGTERM.
# gebruik: ipc_smoke.sh <ravengund> <ravengunctl>
set -u
D="$1"; C="$2"
T=$(mktemp -d) || exit 1
export XDG_RUNTIME_DIR="$T" XDG_STATE_HOME="$T/state" XDG_CONFIG_HOME="$T/config"
fail() { echo "FAIL: $*"; kill "$PID" 2>/dev/null; rm -rf "$T"; exit 1; }

"$D" --no-hid --no-camera 2>"$T/log" &
PID=$!
i=0
while [ ! -S "$T/ravengun/ravengund.sock" ]; do
  i=$((i+1)); [ $i -gt 50 ] && fail "socket verschijnt niet"; sleep 0.1
done

[ "$(stat -c %a "$T/ravengun")" = "700" ] || fail "map niet 0700"
[ "$(stat -c %a "$T/ravengun/ravengund.sock")" = "600" ] || fail "socket niet 0600"

"$D" --no-hid --no-camera 2>/dev/null
[ $? -eq 69 ] || fail "tweede instantie niet geweigerd (verwacht exit 69)"

OUT=$("$C" status --json) || fail "status faalde"
echo "$OUT" | grep -q '"ok":true' || fail "status niet ok"
echo "$OUT" | grep -q '"hid_enabled":false' || fail "hid_enabled ontbreekt"
echo "$OUT" | grep -q '"schema_version":2' || fail "schema_version ontbreekt"
"$C" status >/dev/null || fail "status (mens) faalde"

"$C" recenter 2>/dev/null
[ $? -eq 75 ] || fail "recenter zonder Move moet exit 75 geven"

"$C" filter --off >/dev/null || fail "filter --off faalde"
"$C" status --json | grep -q '"filter":{"enabled":false' || fail "filter niet uit"

"$C" watch --duration 0.5 --json >/dev/null || fail "watch faalde"

OUT=$("$C" camera --json) || fail "camera faalde"
echo "$OUT" | grep -q '"enabled":false' || fail "camera niet uit bij --no-camera"
"$C" status --json | grep -q '"position":{"kind":"image_2d","valid":false' || fail "position (image_2d) ontbreekt"

# Fase 0: opname zonder Move (alleen header/einde), rechten 0700/0600, geen paden van de client.
"$C" record --start --tag smoke --duration 30 >/dev/null || fail "record --start faalde"
"$C" record --start --tag smoke 2>/dev/null
[ $? -eq 75 ] || fail "tweede opname moet exit 75 geven"
"$C" record --status --json | grep -q '"active":true' || fail "opname niet actief"
"$C" record --mark "test label" >/dev/null || fail "record --mark faalde"
"$C" led rood >/dev/null || fail "led faalde"
"$C" record --stop --json | grep -q '"reason":"stop"' || fail "record --stop faalde"
"$C" record --stop 2>/dev/null
[ $? -eq 75 ] || fail "stop zonder opname moet exit 75 geven"
F=$(ls "$T/state/ravengun/recordings/"*smoke.rgrec.jsonl 2>/dev/null | head -1)
[ -n "$F" ] || fail "opnamebestand ontbreekt"
[ "$(stat -c %a "$F")" = "600" ] || fail "opnamebestand niet 0600"
[ "$(stat -c %a "$T/state/ravengun/recordings")" = "700" ] || fail "opnamemap niet 0700"
head -1 "$F" | grep -q '"format":"ravengun-recording"' || fail "header ontbreekt"
grep -q '"k":"mark","t":[0-9]*,"label":"test label"' "$F" || fail "label ontbreekt"
tail -1 "$F" | grep -q '"k":"end"' || fail "eindregel ontbreekt"
"$C" record --start --tag "../x" 2>/dev/null
[ $? -ne 0 ] || fail "ongeldige tag geaccepteerd"
# Fase 3: kalibratie zonder ZCM1 wordt geweigerd; status werkt.
"$C" calibrate mag --start 2>/dev/null
[ $? -eq 75 ] || fail "calibrate mag zonder Move moet exit 75 geven"
"$C" calibrate mag --status --json | grep -q '"calibrated":false' || fail "calibrate mag --status"
"$C" status --json | grep -q '"timebase":{' || fail "timebase ontbreekt in status"

kill -TERM "$PID"
wait "$PID"
RC=$?
[ $RC -eq 0 ] || fail "daemon exitcode $RC na SIGTERM"
[ ! -e "$T/ravengun/ravengund.sock" ] || fail "socket niet opgeruimd"

XDG_RUNTIME_DIR="$T" "$C" status 2>/dev/null
[ $? -eq 69 ] || fail "status zonder daemon moet exit 69 geven"

rm -rf "$T"
echo "ipc_smoke: alles in orde"

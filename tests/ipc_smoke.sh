#!/bin/sh
# Integratietest zonder hardware: ravengund --no-hid, socket- en maprechten, enkele instantie,
# ravengunctl status/recenter/watch en nette shutdown op SIGTERM.
# gebruik: ipc_smoke.sh <ravengund> <ravengunctl>
set -u
D="$1"; C="$2"
T=$(mktemp -d) || exit 1
export XDG_RUNTIME_DIR="$T" XDG_CONFIG_HOME="$T/cfg" HOME="$T"
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

# Uitvoer: standaard disarmed, geen apparaat; patronen en knoppen worden geweigerd zonder arm.
# (Arm zelf maakt een uinput-apparaat en hoort niet in deze test: de test raakt geen invoerapparaten.)
OUT=$("$C" output --json) || fail "output faalde"
echo "$OUT" | grep -q '"armed":false' || fail "uitvoer niet standaard disarmed"
echo "$OUT" | grep -q '"created":false' || fail "apparaat bestaat al zonder arm"
"$C" status --json | grep -q '"output":{"armed":false' || fail "status mist de uitvoer"
"$C" test-pattern center 2>/dev/null
[ $? -eq 75 ] || fail "test-pattern zonder arm moet exit 75 geven"
"$C" output-button trigger tap 2>/dev/null
[ $? -eq 75 ] || fail "output-button zonder arm moet exit 75 geven"
"$C" test-pattern bogus 2>/dev/null
[ $? -ne 0 ] || fail "onbekend patroon moet falen"
"$C" set-output --invert-y 1 --scale-x 1.1 >/dev/null || fail "set-output faalde"
"$C" output --json | grep -q '"invert_y":true' || fail "set-output niet toegepast"
"$C" disarm >/dev/null || fail "disarm faalde"
"$C" output --json | grep -q '"created":false' || fail "disarm maakte een apparaat aan"

kill -TERM "$PID"
wait "$PID"
RC=$?
[ $RC -eq 0 ] || fail "daemon exitcode $RC na SIGTERM"
[ ! -e "$T/ravengun/ravengund.sock" ] || fail "socket niet opgeruimd"

XDG_RUNTIME_DIR="$T" "$C" status 2>/dev/null
[ $? -eq 69 ] || fail "status zonder daemon moet exit 69 geven"

rm -rf "$T"
echo "ipc_smoke: alles in orde"

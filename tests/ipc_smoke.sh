#!/bin/sh
# Integratietest zonder hardware: ravengund --no-hid, socket- en maprechten, enkele instantie,
# ravengunctl status/recenter/watch en nette shutdown op SIGTERM.
# gebruik: ipc_smoke.sh <ravengund> <ravengunctl>
set -u
D="$1"; C="$2"
T=$(mktemp -d) || exit 1
export XDG_RUNTIME_DIR="$T" XDG_STATE_HOME="$T/state" XDG_CONFIG_HOME="$T/cfg" HOME="$T"
fail() { echo "FAIL: $*"; kill "$PID" 2>/dev/null; rm -rf "$T"; exit 1; }

"$D" --no-hid --no-bias-store --no-camera 2>"$T/log" &
PID=$!
i=0
while [ ! -S "$T/ravengun/ravengund.sock" ]; do
  i=$((i+1)); [ $i -gt 50 ] && fail "socket verschijnt niet"; sleep 0.1
done

[ "$(stat -c %a "$T/ravengun")" = "700" ] || fail "map niet 0700"
[ "$(stat -c %a "$T/ravengun/ravengund.sock")" = "600" ] || fail "socket niet 0600"

"$D" --no-hid --no-bias-store --no-camera 2>/dev/null
[ $? -eq 69 ] || fail "tweede instantie niet geweigerd (verwacht exit 69)"

OUT=$("$C" status --json) || fail "status faalde"
echo "$OUT" | grep -q '"ok":true' || fail "status niet ok"
echo "$OUT" | grep -q '"hid_enabled":false' || fail "hid_enabled ontbreekt"
echo "$OUT" | grep -q '"schema_version":2' || fail "schema_version ontbreekt"
"$C" status >/dev/null || fail "status (mens) faalde"

"$C" recenter 2>/dev/null
[ $? -eq 75 ] || fail "recenter zonder Move moet exit 75 geven"

"$C" status --json | grep -q '"yaw_trim":{"learning":true,"trim_deg_s":0' || fail "yaw_trim ontbreekt in de status"
"$C" trim >/dev/null || fail "trim faalde"
"$C" trim reset >/dev/null || fail "trim reset faalde"

"$C" filter --off >/dev/null || fail "filter --off faalde"
"$C" status --json | grep -q '"filter":{"enabled":false' || fail "filter niet uit"

# Voorspelling en uitvoerklok: live instelbaar, zichtbaar in de status.
"$C" predict 35 >/dev/null || fail "predict faalde"
"$C" predict | grep -q "35 ms" || fail "predict toont de horizon niet"
"$C" status --json | grep -q '"predict_ms":35' || fail "predict_ms niet in de status"
"$C" predict 300 2>/dev/null
[ $? -eq 64 ] || fail "predict 300 moet een gebruiksfout geven"
"$C" predict 0 >/dev/null || fail "predict 0 faalde"
"$C" clock 125 --extrapolate-ms 30 >/dev/null || fail "clock faalde"
"$C" status --json | grep -q '"clock":{"enabled":true,"hz":125' || fail "klok niet in de status"
"$C" clock 0 >/dev/null || fail "clock 0 faalde"
"$C" clock 10 2>/dev/null
[ $? -eq 64 ] || fail "clock 10 moet een gebruiksfout geven"
"$C" rumble | grep -q "aan, sterkte 180, puls 60 ms, 9 Hz bij vasthouden, pomp 90" || fail "rumble-standaard niet in de status"
"$C" rumble --strength 200 --pulse-ms 80 --repeat-hz 0 --pump-strength 0 >/dev/null || fail "rumble instellen faalde"
"$C" status --json | grep -q '"rumble":{"enabled":true,"strength":200,"pulse_ms":80,"repeat_hz":0,"pump_strength":0' || fail "rumble niet in de status-JSON"
"$C" rumble | grep -q "puls 80 ms, één puls per schot, pomp uit" || fail "rumble-tekst klopt niet"
"$C" rumble off >/dev/null || fail "rumble off faalde"
"$C" status --json | grep -q '"rumble":{"enabled":false,"strength":200' || fail "rumble niet uit"
rc=0; "$C" rumble test 2>/dev/null || rc=$?
[ "$rc" -ne 0 ] || fail "rumble test zonder Move moet falen"
"$C" rumble on >/dev/null || fail "rumble on faalde"
"$C" rumble --strength 300 2>/dev/null
[ $? -eq 64 ] || fail "rumble --strength 300 moet een gebruiksfout geven"
"$C" rumble bla 2>/dev/null
[ $? -eq 64 ] || fail "rumble bla moet een gebruiksfout geven"
"$C" filter --d-cutoff 5 >/dev/null || fail "filter --d-cutoff faalde"

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

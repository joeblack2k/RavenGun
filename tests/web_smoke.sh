#!/bin/sh
# Integratietest van de webinterface zonder hardware: ravengund --no-hid --no-camera met een eigen
# runtime-map en poort; alle eindpunten via curl; beveiligingsweigeringen; LAN zonder/met token.
# gebruik: web_smoke.sh <ravengund>
set -u
D="$1"
command -v curl >/dev/null 2>&1 || { echo "SKIP: curl ontbreekt"; exit 0; }
T=$(mktemp -d) || exit 1
export XDG_RUNTIME_DIR="$T/run" HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config"
mkdir -p "$HOME" "$T/run" && chmod 700 "$T/run"
PORT=$((20000 + $$ % 20000))
B="http://127.0.0.1:$PORT"
PID=""
fail() { echo "FAIL: $*"; [ -n "$PID" ] && kill "$PID" 2>/dev/null; cat "$T/log" 2>/dev/null | tail -5; rm -rf "$T"; exit 1; }
code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

"$D" --no-hid --no-camera --web-port "$PORT" 2>"$T/log" &
PID=$!
i=0
until curl -s -o /dev/null "$B/api/csrf"; do
  i=$((i+1)); [ $i -gt 50 ] && fail "webserver start niet"; sleep 0.1
done

CSRF=$(curl -s "$B/api/csrf" | sed 's/.*"csrf":"\([0-9a-f]*\)".*/\1/')
[ ${#CSRF} -eq 32 ] || fail "csrf-token onverwacht: $CSRF"
J='Content-Type: application/json'

curl -s "$B/api/status" | grep -q '"ok":true' || fail "status"
curl -s "$B/api/status" | grep -q '"connected":false' || fail "status moet 'geen Move' tonen"
curl -s "$B/api/camera" | grep -q '"enabled":false' || fail "camera"
curl -s "$B/api/sample" | grep -q '"schema_version"' || fail "sample"
curl -s "$B/" | grep -q "name=\"csrf-token\" content=\"$CSRF\"" || fail "index zonder csrf-token"
[ "$(code "$B/app.js")" = 200 ] || fail "app.js"
[ "$(code "$B/app.css")" = 200 ] || fail "app.css"
[ "$(code "$B/nope")" = 404 ] || fail "404"
curl -si "$B/api/status" | grep -qi '^access-control' && fail "CORS-header aanwezig"

# Muterende endpoints
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"min_cutoff_hz":2.0,"beta":0.3}' "$B/api/filter")" = 200 ] || fail "filter"
curl -s "$B/api/status" | grep -q '"min_cutoff_hz":2' || fail "filter niet doorgevoerd"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"h_edge_deg":25,"v_edge_deg":14}' "$B/api/aim")" = 200 ] || fail "aim"
curl -s "$B/api/status" | grep -q '"h_edge_deg":25' || fail "aim niet doorgevoerd"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"h_edge_deg":500}' "$B/api/aim")" = 400 ] || fail "aim-validatie"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{}' "$B/api/recenter")" = 503 ] || fail "recenter zonder Move moet 503 geven"

# Beveiliging
[ "$(code -X POST -H "$J" -d '{}' "$B/api/recenter")" = 403 ] || fail "POST zonder CSRF"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -H 'Origin: http://evil.example' -d '{}' "$B/api/recenter")" = 403 ] || fail "vreemde Origin"
[ "$(code -H 'Host: evil.example' "$B/api/status")" = 403 ] || fail "vreemde Host"
[ "$(code -X OPTIONS "$B/api/status")" = 405 ] || fail "OPTIONS"

# SSE: een paar frames, daarna sluiten.
SSE=$(curl -s -N --max-time 1 "$B/api/stream")
echo "$SSE" | grep -q '^event: sample' || fail "geen SSE-sample"

kill -TERM "$PID"; wait "$PID" 2>/dev/null; PID=""
grep -q 'web-interface op http://127.0.0.1' "$T/log" || fail "URL niet gelogd"

# LAN: token wordt gemaakt (0600), staat niet in de log, en zonder token is er 401.
"$D" --no-hid --no-camera --web-port "$PORT" --web-lan 2>"$T/log" &
PID=$!
i=0
until [ -f "$HOME/.config/ravengun/web-token" ] && curl -s -o /dev/null -H "Host: 127.0.0.1:$PORT" "$B/api/csrf"; do
  i=$((i+1)); [ $i -gt 50 ] && fail "LAN-webserver start niet"; sleep 0.1
done
TOKF="$HOME/.config/ravengun/web-token"
[ "$(stat -c %a "$TOKF")" = 600 ] || fail "tokenbestand niet 0600"
TOK=$(cat "$TOKF")
grep -q "$TOK" "$T/log" && fail "token staat in de log"
[ "$(code "$B/api/status")" = 401 ] || fail "LAN zonder token moet 401 geven"
[ "$(code -H "Authorization: Bearer $TOK" "$B/api/status")" = 200 ] || fail "LAN met token"
kill -TERM "$PID"; wait "$PID" 2>/dev/null; PID=""

echo "OK"
rm -rf "$T"

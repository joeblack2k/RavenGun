#!/bin/sh
# Integratietest van de webinterface zonder hardware: ravengund --no-hid --no-camera met een eigen
# runtime-map en poort; alle eindpunten via curl; beveiligingsweigeringen; LAN zonder/met token.
# gebruik: web_smoke.sh <ravengund>
set -u
D="$1"
command -v curl >/dev/null 2>&1 || { echo "SKIP: curl ontbreekt"; exit 0; }
T=$(mktemp -d) || exit 1
export XDG_RUNTIME_DIR="$T/run" HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" XDG_STATE_HOME="$T/home/.local/state"
mkdir -p "$HOME" "$T/run" && chmod 700 "$T/run"
PORT=$((20000 + $$ % 20000))
B="http://127.0.0.1:$PORT"
PID=""
fail() { echo "FAIL: $*"; [ -n "$PID" ] && kill "$PID" 2>/dev/null; cat "$T/log" 2>/dev/null | tail -5; rm -rf "$T"; exit 1; }
code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

# Nep-TeknoParrot in de eigen HOME (standaardlocatie) en een nep-startscript; --games-dry-run start nooit echt iets.
TP="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64"
mkdir -p "$TP/UserProfiles" "$TP/Icons"
printf '<GameProfile><GameNameInternal>HALO Test</GameNameInternal><IconName>Icons/Halo.png</IconName></GameProfile>' > "$TP/UserProfiles/Halo.xml"
printf '<GameProfile></GameProfile>' > "$TP/UserProfiles/JurassicPark.xml"
printf '<GameProfile></GameProfile>' > "$TP/UserProfiles/Slecht naam.xml"
printf 'PNGDATA' > "$TP/Icons/Halo.png"
printf 'GEHEIM' > "$TP/geheim.png"
printf '#!/bin/sh\nexit 0\n' > "$T/play.sh"; chmod +x "$T/play.sh"

"$D" --no-hid --no-bias-store --no-camera --web-port "$PORT" --play-script "$T/play.sh" --games-dry-run 2>"$T/log" &
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
curl -s "$B/api/record" | grep -q '"active":false' || fail "record-status"
curl -s "$B/api/magnetometer" | grep -q '"mag_calibration"' || fail "magnetometer-status"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{}' "$B/api/record")" = 405 ] || fail "record is alleen GET"
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
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"predict_ms":55}' "$B/api/aim")" = 200 ] || fail "predict_ms"
curl -s "$B/api/status" | grep -q '"predict_ms":55' || fail "predict_ms niet doorgevoerd"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"predict_ms":999}' "$B/api/aim")" = 400 ] || fail "predict_ms-validatie"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"clock_hz":125,"extrapolate_ms":30}' "$B/api/clock")" = 200 ] || fail "clock"
curl -s "$B/api/status" | grep -q '"clock":{"enabled":true,"hz":125' || fail "clock niet doorgevoerd"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"clock_hz":7}' "$B/api/clock")" = 400 ] || fail "clock-validatie"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"enabled":true,"strength":150,"pulse_ms":70,"repeat_hz":6,"pump_strength":50}' "$B/api/rumble")" = 200 ] || fail "rumble"
curl -s "$B/api/status" | grep -q '"rumble":{"enabled":true,"strength":150,"pulse_ms":70,"repeat_hz":6,"pump_strength":50' || fail "rumble niet doorgevoerd"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"strength":300}' "$B/api/rumble")" = 400 ] || fail "rumble-validatie"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{}' "$B/api/rumble-test")" = 503 ] || fail "rumble test zonder Move moet 503 geven"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{}' "$B/api/recenter")" = 503 ] || fail "recenter zonder Move moet 503 geven"

# Nieuwe endpoints: info, instellingen, diagnose, LED, camera zonder camera, uitvoer, web-poort
curl -s "$B/api/info" | grep -q '"psmoveapi_pin":"[0-9a-f]\{40\}"' || fail "info mist de PSMoveAPI-pin"
curl -s "$B/api/info" | grep -q '"git_commit"' || fail "info mist de commit"
curl -s "$B/api/settings" | grep -q '"presets"' || fail "settings mist presets"
curl -si "$B/api/diagnostics" | grep -qi '^content-disposition: attachment' || fail "diagnose zonder download-header"
curl -s "$B/api/diagnostics" | grep -q '"kind":"ravengun-diagnostics"' || fail "diagnose-inhoud"
curl -s "$B/api/diagnostics" | grep -q "$HOME" && fail "diagnose bevat de thuismap"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"preset":"magenta"}' "$B/api/led")" = 200 ] || fail "led preset"
curl -s "$B/api/status" | grep -q '"rgb":\[255,0,255\]' || fail "LED niet doorgevoerd"
curl -s -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"r":255,"g":128,"b":0}' "$B/api/led" | grep -q '"custom":true' || fail "led custom"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"preset":"paars"}' "$B/api/led")" = 400 ] || fail "led onbekende preset"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"r":999,"g":0,"b":0}' "$B/api/led")" = 400 ] || fail "led bereik"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"exposure":60}' "$B/api/camera")" = 503 ] || fail "set_camera zonder camera moet 503 geven"
curl -s -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"exposure":60}' "$B/api/camera" | grep -q 'no-camera' || fail "set_camera zonder camera: melding"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"exposure":999}' "$B/api/camera")" = 400 ] || fail "set_camera bereik"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"mode":"1x1@1"}' "$B/api/camera")" = 400 ] || fail "set_camera modus"
curl -s "$B/api/output" | grep -q '"armed":false' || fail "uitvoer niet disarmed"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"pattern":"center"}' "$B/api/test-pattern")" = 503 ] || fail "testpatroon zonder arm moet 503 geven"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"scale_x":1.05}' "$B/api/output")" = 200 ] || fail "set_output"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"port":18999}' "$B/api/web")" = 200 ] || fail "set_web"
[ "$(code -X POST -H "$J" -d '{"preset":"cyaan"}' "$B/api/led")" = 403 ] || fail "led zonder CSRF"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -H 'Origin: http://evil.example' -d '{"preset":"cyaan"}' "$B/api/led")" = 403 ] || fail "led vreemde Origin"
[ "$(code -X POST -H "$J" -d '{"exposure":60}' "$B/api/camera")" = 403 ] || fail "camera zonder CSRF"
CFG="$XDG_CONFIG_HOME/ravengun/config.json"
[ -f "$CFG" ] || fail "config.json is niet gemaakt"
[ "$(stat -c %a "$CFG")" = 600 ] || fail "config.json niet 0600"
[ "$(stat -c %a "$XDG_CONFIG_HOME/ravengun")" = 700 ] || fail "configmap niet 0700"
grep -q '"min_cutoff_hz":2' "$CFG" || fail "filter niet bewaard"
grep -q '"preset":"custom"' "$CFG" || fail "LED niet bewaard"
grep -q '"port":18999' "$CFG" || fail "web-poort niet bewaard"
ls "$XDG_CONFIG_HOME/ravengun" | grep -q '\.tmp\.' && fail "tijdelijk bestand achtergebleven"
# De tabbladen worden door de app-code gebouwd; de pagina laadt app-core.js voor app.js en bevat alle zeven vlakken.
for p in "Overzicht" "Richten" "Trillen" "Knoppen" "LED en camera" "Games" "Info (technisch)"; do
  curl -s "$B/app-core.js" | grep -q "label: '$p'" || fail "tab $p ontbreekt"
done
for p in overzicht richten trillen knoppen led games info; do
  curl -s "$B/" | grep -q "id=\"p-$p\"" || fail "pagina p-$p ontbreekt"
done
[ "$(code "$B/app-core.js")" = 200 ] || fail "app-core.js"
curl -s "$B/" | grep -qE 'https?://' && fail "index verwijst naar een externe bron"
curl -s "$B/app.js" | grep -qE "https?://" && fail "app.js verwijst naar een externe bron"
curl -s "$B/api/settings" | grep -q '"defaults":{"schema":2' || fail "settings mist de fabrieksinstellingen"

# Games: lijst (alleen geldige profielnamen), notities, iconen, start/stop met dry-run en weigeringen.
curl -s "$B/api/games" | grep -q '"profile":"Halo","name":"HALO Test","icon":"/api/game-icon/Halo.png"' || fail "games: Halo ontbreekt"
curl -s "$B/api/games" | grep -q '"profile":"JurassicPark","name":"Jurassic Park","icon":null,"note":"Werkt nog niet: zwart beeld."' || fail "games: notitie ontbreekt"
curl -s "$B/api/games" | grep -q 'Slecht' && fail "games: ongeldige profielnaam in de lijst"
[ "$(curl -s "$B/api/game-icon/Halo.png")" = PNGDATA ] || fail "icoon"
[ "$(code "$B/api/game-icon/Halo.png")" = 200 ] || fail "icoon status"
for bad in '../geheim.png' '..%2fgeheim.png' '%2e%2e%2fgeheim.png' 'Halo.xml' 'geheim' ''; do
  c=$(code --path-as-is "$B/api/game-icon/$bad"); [ "$c" = 400 ] || [ "$c" = 404 ] || fail "icoon-pad '$bad' gaf $c"
done
curl -s --path-as-is "$B/api/game-icon/../geheim.png" | grep -q GEHEIM && fail "icoon buiten de Icons-map geserveerd"
[ "$(code -X POST -H "$J" -d '{"profile":"Halo"}' "$B/api/games/start")" = 403 ] || fail "game start zonder CSRF"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -H 'Origin: http://evil.example' -d '{"profile":"Halo"}' "$B/api/games/start")" = 403 ] || fail "game start vreemde Origin"
for bad in 'Nope' '../UserProfiles/Halo' 'Halo;id' 'Halo.xml' 'Slecht naam' '$(id)' ''; do
  [ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d "{\"profile\":\"$bad\"}" "$B/api/games/start")" = 400 ] || fail "game start met '$bad' moet 400 geven"
done
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"profile":"Halo","cmd":"x"}' "$B/api/games/start")" = 400 ] || fail "game start met extra veld"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"profile":"Halo"}' "$B/api/games/start")" = 200 ] || fail "game start (dry-run)"
grep -q 'games (dry-run): systemd-run --user --unit=ravengun-play .* -- .*/play.sh --profile Halo$' "$T/log" || fail "games: opdrachtregel niet zoals verwacht"
curl -s "$B/api/games" | grep -q '"state":"waiting_move"' || fail "games: toestand na de start"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"profile":"JurassicPark"}' "$B/api/games/start")" = 503 ] || fail "tweede start tijdens de eerste"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{}' "$B/api/games/stop")" = 200 ] || fail "game stop"

# Knoppenindeling live aanpassen en bewaren
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"map":{"cross":"key:f3","circle":"aux1"}}' "$B/api/gun-buttons")" = 200 ] || fail "gun-buttons"
curl -s "$B/api/output" | grep -q '"cross":"key:f3"' || fail "gun-buttons niet live toegepast"
grep -q '"cross": *"key:f3"' "$XDG_CONFIG_HOME/ravengun/config.json" || fail "gun-buttons niet bewaard"
for bad in '{"map":{"cross":"vliegen"}}' '{"map":{"ps":"reload"}}' '{"map":{"t":"reload"}}' '{"map":{"cross":"key:f99"}}' '{"map":{"laser":"reload"}}' '{"cross":"reload"}' '{"map":{}}'; do
  [ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d "$bad" "$B/api/gun-buttons")" = 400 ] || fail "gun-buttons $bad moet 400 geven"
done
curl -s "$B/api/output" | grep -q '"cross":"key:f3"' || fail "ongeldige indeling heeft de oude indeling aangetast"
[ "$(code -X POST -H "$J" -d '{"map":{"cross":"reload"}}' "$B/api/gun-buttons")" = 403 ] || fail "gun-buttons zonder CSRF"

# Schatter en leren van centreren
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"mode":"vqf_mag","recenter_learning":false}' "$B/api/fusion")" = 200 ] || fail "fusion"
curl -s "$B/api/status" | grep -q '"learning":false' || fail "leren niet uitgezet"
curl -s "$B/api/status" | grep -q '"mode":"vqf_mag"' || fail "schatter niet gewijzigd"
curl -s "$B/api/status" | grep -q '"navigation":{"connected":false' || fail "status mist de Navigation Controller"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"mode":"kompas"}' "$B/api/fusion")" = 400 ] || fail "fusion-validatie"

# Kalibratie-UI op de tv
curl -s "$B/calibrate" | grep -q "name=\"csrf-token\" content=\"$CSRF\"" || fail "/calibrate zonder csrf-token"
curl -si "$B/calibrate" | grep -qi "^content-security-policy: default-src 'none'; script-src 'self'" || fail "/calibrate zonder CSP"
for f in calibrate.js calibrate-core.js calibrate.css; do [ "$(code "$B/$f")" = 200 ] || fail "$f"; done
curl -s "$B/calibrate" | grep -qE 'https?://' && fail "/calibrate verwijst naar een externe bron"
curl -s "$B/api/calibrate/ui" | grep -q '"running":false' || fail "kiosk-status"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"trekker":"t","cirkel":"circle"}' "$B/api/calibrate/buttons")" = 200 ] || fail "knoppenmapping"
curl -s "$B/api/settings" | grep -q '"sharp_shooter":{"buttons":{"cirkel":"circle","trekker":"t"},"extension":"auto"}' || fail "mapping niet in de instellingen"
grep -q '"cirkel": *"circle"' "$XDG_CONFIG_HOME/ravengun/config.json" || fail "mapping niet bewaard in config.json"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"trekker":"l2"}' "$B/api/calibrate/buttons")" = 400 ] || fail "mapping met onbekende knop"
[ "$(code -X POST -H "$J" -d '{"trekker":"t"}' "$B/api/calibrate/buttons")" = 403 ] || fail "mapping zonder CSRF"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -H 'Origin: http://evil.example' -d '{}' "$B/api/calibrate/mag/start")" = 403 ] || fail "mag/start vreemde Origin"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{}' "$B/api/calibrate/mag/start")" = 503 ] || fail "mag/start zonder Move moet 503 geven"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"tag":"stil-test"}' "$B/api/calibrate/rec/start")" = 200 ] || fail "rec/start"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"label":"stil_midden_1"}' "$B/api/calibrate/rec/mark")" = 200 ] || fail "rec/mark"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{}' "$B/api/calibrate/rec/stop")" = 200 ] || fail "rec/stop"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -d '{"action":"rm"}' "$B/api/calibrate/ui")" = 400 ] || fail "ui met onbekende actie"

# Beveiliging
[ "$(code -X POST -H "$J" -d '{}' "$B/api/recenter")" = 403 ] || fail "POST zonder CSRF"
[ "$(code -X POST -H "$J" -H "X-CSRF-Token: $CSRF" -H 'Origin: http://evil.example' -d '{}' "$B/api/recenter")" = 403 ] || fail "vreemde Origin"
[ "$(code -H 'Host: evil.example' "$B/api/status")" = 403 ] || fail "vreemde Host"
[ "$(code -X OPTIONS "$B/api/status")" = 405 ] || fail "OPTIONS"

# SSE: een paar frames, daarna sluiten.
SSE=$(curl -s -N --max-time 1 "$B/api/stream")
echo "$SSE" | grep -q '^event: sample' || fail "geen SSE-sample"
echo "$SSE" | grep -q '"edges":{"press":\[' || fail "SSE zonder cumulatieve knopflanken (edges)"

kill -TERM "$PID"; wait "$PID" 2>/dev/null; PID=""
grep -q 'web-interface op http://127.0.0.1' "$T/log" || fail "URL niet gelogd"

# Herstart: bewaarde instellingen worden toegepast; de opdrachtregel gaat voor; een corrupt bestand geeft een melding.
start_d() {
  "$D" --no-hid --no-bias-store --no-camera --web-port "$PORT" "$@" 2>"$T/log" &
  PID=$!
  i=0
  until curl -s -o /dev/null "$B/api/csrf"; do i=$((i+1)); [ $i -gt 50 ] && fail "daemon start niet ($*)"; sleep 0.1; done
}
stop_d() { kill -TERM "$PID"; wait "$PID" 2>/dev/null; PID=""; }
start_d
curl -s "$B/api/status" | grep -q '"min_cutoff_hz":2' || fail "bewaard filter niet toegepast bij de start"
curl -s "$B/api/status" | grep -q '"h_edge_deg":25' || fail "bewaarde aim niet toegepast bij de start"
curl -s "$B/api/status" | grep -q '"rgb":\[255,128,0\]' || fail "bewaarde LED niet toegepast bij de start"
curl -s "$B/api/settings" | grep -q '"state":"loaded"' || fail "settings-toestand"
stop_d
start_d --min-cutoff 3 --no-config
curl -s "$B/api/status" | grep -q '"min_cutoff_hz":3' || fail "opdrachtregel gaat niet voor (--no-config)"
curl -s "$B/api/settings" | grep -q '"values":null' && fail "--no-config: waarden verwacht (standaard)"
stop_d
start_d --min-cutoff 3
curl -s "$B/api/status" | grep -q '"min_cutoff_hz":3' || fail "opdrachtregel gaat niet voor op het bestand"
curl -s "$B/api/status" | grep -q '"h_edge_deg":25' || fail "overige bewaarde waarden verdwenen"
stop_d
echo '{ kapot' > "$CFG"
start_d
curl -s "$B/api/settings" | grep -q '"state":"recovered"' || fail "corrupt bestand: kopie niet gebruikt"
curl -s "$B/api/settings" | grep -q 'laatst-goede' || fail "corrupt bestand: geen melding"
grep -q 'waarschuwing.*instellingen' "$T/log" || fail "corrupt bestand: niet gelogd"
stop_d
rm -f "$CFG" "$CFG.bak" "$CFG.corrupt"

# LAN: token wordt gemaakt (0600), staat niet in de log, en zonder token is er 401.
"$D" --no-hid --no-bias-store --no-camera --web-port "$PORT" --web-lan 2>"$T/log" &
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

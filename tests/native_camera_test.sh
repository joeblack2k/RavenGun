#!/usr/bin/env bash
# De camera op de native route: scripts/play-native.sh geeft een camera alleen door aan een spel dat hem IN het spel gebruikt
# ([Camera] Use = game in game.ini), met --dev-bind-try. Met het echte script en zonder iets te starten (--dry-run):
#   1. een spel zonder [Camera] krijgt geen camera, en het commando is wat het zonder deze functie ook was (geen /dev/video);
#   2. Use = game: precies een --dev-bind-try van de gekozen camera, en verder is het commando byte voor byte hetzelfde;
#   3. Use = setup (alleen voor de inrichting): geen camera;
#   4. de keuze uit de instellingen van de app bepaalt WELKE camera, en 'geen' daar wint;
#   5. de gebruikerslaag kan hem per game aan- of uitzetten;
#   6. een instelling die niet klopt (een onbekende rol, een apparaat dat geen camera is) stopt de start, luid;
#   7. het logboek van de run zegt welke keuze er gemaakt is, ook als dat "geen" is.
# Hermetisch: een eigen namaak-datamap, -NAS en -bwrap in een tijdelijke map.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PLAY="${RG_PLAY_NATIVE:-$ROOT/scripts/play-native.sh}"
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
FAILS=0
fail() { echo "FAIL: $*"; FAILS=$((FAILS + 1)); }
tel() { grep -oF -- "$1" <<<"$2" | wc -l; }
export HOME="$T/home"; mkdir -p "$HOME"
N="$T/native"; mkdir -p "$N/scripts" "$N/packaging" "$N/build" "$N/patches/Spel" "$T/nas/Spel/pm" "$T/bin" "$T/run" "$T/tpui" "$T/cfg/ravengun"
cp "$PLAY" "$N/scripts/play-native.sh"
cp "$ROOT/scripts/rg-paths.sh" "$ROOT/scripts/rg-identiteit.sh" "$ROOT/scripts/camera.py" "$ROOT/scripts/gamesettings.py" "$N/scripts/"
printf 'x' > "$T/nas/Spel/pm/game"
cat > "$N/packaging/native-games.json" <<JSON
{"defaults": {"res": "1280x720"},
 "games": {"Spel": {"title": "Spel", "status": "werkt", "enabled": true, "arch": "x86_64", "exe": "/pm/game", "cwd": "/pm",
   "args": ["-n"], "mounts": [{"mode": "ro", "src": "@nas/Spel/pm", "dst": "/pm"}]}}}
JSON
printf '#!/bin/sh\nexit 3\n' > "$T/bin/bwrap"; chmod +x "$T/bin/bwrap"
native() { env -u RG_NATIVE_INNER -u RAVENGUN_PATCHES_DIR -u RAVENGUN_USER_PATCHES_DIR PATH="$T/bin:$PATH" DISPLAY=:99 XAUTHORITY= \
               XDG_DATA_HOME="$T/data" XDG_STATE_HOME="$T/state" XDG_RUNTIME_DIR="$T/run" XDG_CONFIG_HOME="$T/cfg" \
               RAVENGUN_NAS_TP="$T/nas" RAVENGUN_TPUI="$T/tpui" RAVENGUN_BIN_DIR="$N/build" RAVENGUN_LIB_DIR="$N/build" \
               "$N/scripts/play-native.sh" "$@"; }
INI="$N/patches/Spel/game.ini"; EIGEN="$T/data/ravengun/patches/Spel/game.ini"; CONF="$T/cfg/ravengun/ravengun-app.conf"
# De camera die "auto" oplevert hangt van deze machine af; daarom staat de keuze in deze test steeds vast (in de instellingen).
printf '[Games]\ncamera=/dev/video7\n' > "$CONF"

# 1. zonder [Camera]
KAAL=$(native --game Spel --dry-run 2>"$T/err"); RC=$?
[[ $RC -eq 0 && "$KAAL" == bwrap* ]] || fail "zonder [Camera]: geen commando (exit $RC): $(cat "$T/err")"
[[ "$KAAL" != *"/dev/video"* ]] || fail "zonder [Camera] hoort er geen camera in het commando te staan: $KAAL"
grep -q "^camera: geen (rol none" "$T/err" || fail "zonder [Camera] hoort de keuze 'geen' gemeld te worden: $(cat "$T/err")"
printf '[Launch]\nMode = direct\n' > "$INI"
[[ "$(native --game Spel --dry-run 2>/dev/null)" == "$KAAL" ]] || fail "een game.ini zonder [Camera] verandert het commando"

# 2. Use = game
printf '[Camera]\nUse = game\n' > "$INI"
MET=$(native --game Spel --dry-run 2>"$T/err"); RC=$?
[[ $RC -eq 0 ]] || fail "Use = game: exit $RC: $(cat "$T/err")"
[[ "$(tel "--dev-bind-try /dev/video7 /dev/video7 " "$MET ")" -eq 1 && "$(tel "/dev/video" "$MET")" -eq 2 ]] || fail "de camera wordt niet (precies een keer) met --dev-bind-try doorgegeven: $MET"
[[ "${MET/--dev-bind-try \/dev\/video7 \/dev\/video7 /}" == "$KAAL" ]] || fail "met een camera is er meer veranderd dan die ene koppeling:"$'\n'"  zonder: $KAAL"$'\n'"  met:    $MET"
grep -q "^camera: /dev/video7 doorgegeven (rol game" "$T/err" || fail "de doorgegeven camera wordt niet gemeld: $(cat "$T/err")"
grep -qF "  hulp: $N/scripts/camera.py sha256 $(sha256sum "$N/scripts/camera.py" | cut -d' ' -f1)" "$T/err" || fail "de identiteit noemt camera.py niet (een andere keuzeregel is een andere meting): $(cat "$T/err")"

# 3. Use = setup
printf '[Camera]\nUse = setup\nDevice = /dev/video7\n' > "$INI"
[[ "$(native --game Spel --dry-run 2>"$T/err")" == "$KAAL" ]] || fail "een spel dat de camera alleen voor de inrichting wil, krijgt hem toch"
grep -q "^camera: geen (rol setup" "$T/err" || fail "de rol setup wordt niet gemeld: $(cat "$T/err")"

# 3b. Use = presence (het spel wil alleen dat er een camera IS): de echte camera gaat er NOOIT in. Zolang de virtuele camera er
#     niet is (librg_camera.so naast onze bibliotheken) wordt er niets doorgegeven, en dat staat er in zoveel woorden.
printf '[Camera]\nUse = presence\nDevice = /dev/video7\n' > "$INI"
PRES=$(native --game Spel --dry-run 2>"$T/err"); RC=$?
[[ $RC -eq 0 && "$PRES" == "$KAAL" ]] || fail "presence zonder virtuele camera hoort het commando niet te veranderen (exit $RC): $PRES"
grep -q "^camera: geen (rol presence: .*virtuele camera is nog niet gebouwd" "$T/err" || fail "presence zonder virtuele camera wordt niet uitgelegd: $(cat "$T/err")"

# 4. welke camera: game.ini noemt er zelf een; en 'geen' in de instellingen wint
printf '[Camera]\nUse = game\nDevice = /dev/video3\n' > "$INI"
[[ "$(native --game Spel --dry-run 2>/dev/null)" == *"--dev-bind-try /dev/video3 /dev/video3"* ]] || fail "het apparaat uit game.ini wordt niet gebruikt"
printf '[Camera]\nUse = game\n' > "$INI"; printf '[Games]\ncamera=none\n' > "$CONF"
[[ "$(native --game Spel --dry-run 2>"$T/err")" == "$KAAL" ]] || fail "'geen camera' in de instellingen wint niet"
grep -q "^camera: geen (rol game" "$T/err" || fail "'geen camera' in de instellingen wordt niet gemeld: $(cat "$T/err")"
printf '[Games]\ncamera=/dev/video7\n' > "$CONF"

# 5. de gebruikerslaag
mkdir -p "$(dirname "$EIGEN")"; printf '[Camera]\nUse = setup\n' > "$EIGEN"
[[ "$(native --game Spel --dry-run 2>/dev/null)" == "$KAAL" ]] || fail "de gebruikerslaag kan de camera niet uitzetten"
printf '[Launch]\nMode = direct\n' > "$INI"; printf '[Camera]\nUse = game\n' > "$EIGEN"
[[ "$(native --game Spel --dry-run 2>/dev/null)" == *"--dev-bind-try /dev/video7 /dev/video7"* ]] || fail "de gebruikerslaag kan de camera niet aanzetten"
rm -f "$EIGEN"

# 6. een instelling die niet klopt stopt de start
for slecht in "Use = altijd" "Use = game"$'\n'"Device = /dev/sda"; do
  printf '[Camera]\n%s\n' "$slecht" > "$INI"
  OUT=$(native --game Spel --dry-run 2>"$T/err"); RC=$?
  [[ $RC -ne 0 && -z "$OUT" ]] || fail "een verkeerde instelling ($slecht) stopt de start niet (exit $RC): $OUT"
  grep -q "camera-instelling van Spel klopt niet" "$T/err" || fail "een verkeerde instelling ($slecht) wordt niet uitgelegd: $(cat "$T/err")"
done
[[ "$(cat "$T/err")" != *"/dev/sda /dev/sda"* ]] || fail "een apparaat dat geen camera is komt toch in het commando"

# 7. een echte doorloop (nep-bwrap): de keuze staat in het logboek, voor de start
printf '[Camera]\nUse = game\n' > "$INI"
native --game Spel >"$T/run.out" 2>&1; RC=$?
[[ $RC -eq 3 ]] || fail "de doorloop geeft de exitcode van het (nep-)spel niet terug ($RC): $(cat "$T/run.out")"
LOG=$(ls "$T/state/ravengun/logs"/native-Spel-*.log 2>/dev/null | head -1)
if [[ -n "$LOG" ]]; then grep -q "^camera: /dev/video7 doorgegeven (rol game" "$LOG" || fail "het logboek noemt de camera niet: $(head -10 "$LOG")"
else fail "de doorloop schreef geen logboek"; fi

[[ $FAILS -eq 0 ]] || { echo "$FAILS fout(en)"; exit 1; }
echo "ok: een camera alleen voor een spel dat hem in het spel gebruikt; zonder instelling verandert er niets"

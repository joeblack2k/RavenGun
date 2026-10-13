#!/usr/bin/env bash
# De invoertabel per game op de native route: scripts/play-native.sh geeft patches/<Game>/rt-invoer.ini aan de invoerhulp
# (librg_rt_input.so) mee. Wat hier wordt afgedwongen, met het echte script en zonder iets te starten (--dry-run):
#   1. zonder tabel verandert er NIETS: geen bind, geen RG_RT_TABLE (de hulp doet dan wat hij altijd deed);
#   2. met een tabel in de patchmap: alleen-lezen de sandbox in, RG_RT_TABLE wijst ernaar, en verder is het commando byte voor
#      byte hetzelfde als zonder tabel;
#   3. de gebruikerslaag gaat als heel bestand voor de meegeleverde;
#   4. een RG_RT_TABLE via --env gaat voor: dan komt er geen tweede bij;
#   5. een spel dat de hulp niet gebruikt krijgt geen tabel, ook niet als er een in zijn patchmap staat; komt de hulp er met
#      --env LD_PRELOAD bij, dan wel;
#   6. via gamescope (buitenste en binnenste aanroep) komt de tabel er precies een keer in;
#   7. de identiteit van de run noemt de tabel met zijn sha256: een andere tabel is een andere meting.
# Hermetisch: een eigen namaak-datamap, -NAS en -bwrap in een tijdelijke map. Skip (77) zonder sha256sum.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PLAY="${RG_PLAY_NATIVE:-$ROOT/scripts/play-native.sh}"     # RG_PLAY_NATIVE: een andere versie van het script toetsen
command -v sha256sum >/dev/null || { echo "overgeslagen: geen sha256sum"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
FAILS=0
fail() { echo "FAIL: $*"; FAILS=$((FAILS + 1)); }
sha() { sha256sum "$1" | cut -d' ' -f1; }
tel() { grep -oF -- "$1" <<<"$2" | wc -l; }
export HOME="$T/home"; mkdir -p "$HOME"

N="$T/native"; mkdir -p "$N/scripts" "$N/packaging" "$N/build" "$N/patches/Met" "$N/patches/Zonder" "$T/nas/Spel/pm" "$T/bin" "$T/run" "$T/tpui"
cp "$PLAY" "$N/scripts/play-native.sh"; cp "$ROOT/scripts/rg-paths.sh" "$ROOT/scripts/rg-identiteit.sh" "$ROOT/scripts/camera.py" "$ROOT/scripts/gamesettings.py" "$N/scripts/"
printf 'x' > "$T/nas/Spel/pm/game"; printf 'de hulp\n' > "$N/build/librg_rt_input.so"
cat > "$N/packaging/native-games.json" <<JSON
{"defaults": {"res": "1280x720"},
 "games": {
  "Met": {"title": "Met de hulp", "status": "werkt", "enabled": true, "arch": "x86_64", "exe": "/pm/game", "cwd": "/pm",
    "preload": "/rg/lib/librg_rt_input.so:/pm/eigen.so", "args": ["-n"], "mounts": [{"mode": "ro", "src": "@nas/Spel/pm", "dst": "/pm"}],
    "libs": ["@build/librg_rt_input.so"]},
  "Zonder": {"title": "Zonder de hulp", "status": "werkt", "enabled": true, "arch": "x86_64", "exe": "/pm/game", "cwd": "/pm",
    "args": ["-n"], "mounts": [{"mode": "ro", "src": "@nas/Spel/pm", "dst": "/pm"}], "libs": ["@build/librg_rt_input.so"]}}}
JSON
printf '#!/bin/sh\nexit 3\n' > "$T/bin/bwrap"; printf '#!/bin/sh\nexit 3\n' > "$T/bin/gamescope"; chmod +x "$T/bin/bwrap" "$T/bin/gamescope"
native() { env -u RG_NATIVE_INNER PATH="$T/bin:$PATH" DISPLAY=:99 XAUTHORITY= XDG_DATA_HOME="$T/data" XDG_STATE_HOME="$T/state" XDG_RUNTIME_DIR="$T/run" \
               RAVENGUN_NAS_TP="$T/nas" RAVENGUN_TPUI="$T/tpui" RAVENGUN_BIN_DIR="$N/build" RAVENGUN_LIB_DIR="$N/build" \
               "$N/scripts/play-native.sh" "$@"; }
DOEL=/rg/tabel/rt-invoer.ini
MEE="$N/patches/Met/rt-invoer.ini"; EIGEN="$T/data/ravengun/patches/Met/rt-invoer.ini"

# 1. zonder tabel: niets
KAAL=$(native --game Met --dry-run 2>"$T/err"); RC=$?
[[ $RC -eq 0 && "$KAAL" == bwrap* ]] || fail "zonder tabel: --dry-run geeft geen commando (exit $RC): $(cat "$T/err")"
[[ "$KAAL" != *"/rg/tabel"* && "$KAAL" != *RG_RT_TABLE* ]] || fail "zonder tabel hoort er niets bij te komen: $KAAL"
grep -q "invoertabel" "$T/err" && fail "zonder tabel hoort de identiteit er geen te noemen: $(cat "$T/err")"
[[ "$KAAL" == *"--setenv LD_PRELOAD /rg/lib/librg_rt_input.so:/pm/eigen.so"* ]] || fail "de preload van het profiel is veranderd: $KAAL"

# 2. de meegeleverde tabel
printf '# meegeleverd\nschakelaar bit=7 bord=0 k=9 puls=0\n' > "$MEE"
MET=$(native --game Met --dry-run 2>"$T/err"); RC=$?
[[ $RC -eq 0 ]] || fail "met tabel: exit $RC: $(cat "$T/err")"
[[ "$(tel "--ro-bind $MEE $DOEL " "$MET ")" -eq 1 ]] || fail "de tabel gaat niet (precies een keer) alleen-lezen de sandbox in: $MET"
[[ "$(tel "--setenv RG_RT_TABLE $DOEL " "$MET ")" -eq 1 && "$(tel "RG_RT_TABLE" "$MET")" -eq 1 ]] || fail "RG_RT_TABLE wijst niet (precies een keer) naar de tabel: $MET"
ZONDER=${MET/--ro-bind $MEE $DOEL /}; ZONDER=${ZONDER/--setenv RG_RT_TABLE $DOEL /}
[[ "$ZONDER" == "$KAAL" ]] || fail "met een tabel is er meer veranderd dan de bind en de variabele:"$'\n'"  zonder: $KAAL"$'\n'"  met:    $MET"
grep -qF "  invoertabel: $MEE sha256 $(sha "$MEE")" "$T/err" || fail "de identiteit noemt de tabel niet met zijn sha256: $(cat "$T/err")"
# een andere inhoud is een andere sha256 in de identiteit
printf 'as bron=12 adres=0x1000 marge=2\n' >> "$MEE"
native --game Met --dry-run >/dev/null 2>"$T/err"
grep -qF "  invoertabel: $MEE sha256 $(sha "$MEE")" "$T/err" || fail "na een wijziging van de tabel klopt de sha256 in de identiteit niet"

# 3. de gebruikerslaag gaat voor
mkdir -p "$(dirname "$EIGEN")"; printf '# van de gebruiker\n' > "$EIGEN"
GEBR=$(native --game Met --dry-run 2>"$T/err")
[[ "$(tel "--ro-bind $EIGEN $DOEL " "$GEBR ")" -eq 1 ]] || fail "de gebruikerslaag gaat niet voor: $GEBR"
[[ "$GEBR" != *"$MEE"* ]] || fail "naast de gebruikerslaag is ook de meegeleverde tabel gebonden: $GEBR"
[[ "$(tel "$DOEL" "$GEBR")" -eq 2 ]] || fail "het doel hoort precies twee keer voor te komen (bind en variabele): $GEBR"
grep -qF "  invoertabel: $EIGEN sha256 $(sha "$EIGEN")" "$T/err" || fail "de identiteit noemt de tabel van de gebruiker niet: $(cat "$T/err")"
rm -f "$EIGEN"

# 4. een RG_RT_TABLE via --env gaat voor
ENVV=$(native --game Met --env RG_RT_TABLE=/pm/elders.ini --dry-run 2>"$T/err")
[[ "$ENVV" == *"--setenv RG_RT_TABLE /pm/elders.ini"* && "$(tel "RG_RT_TABLE" "$ENVV")" -eq 1 && "$ENVV" != *"/rg/tabel"* ]] || fail "een RG_RT_TABLE via --env gaat niet voor: $ENVV"

# 5. een spel zonder de hulp
printf '# tabel bij een spel zonder de hulp\n' > "$N/patches/Zonder/rt-invoer.ini"
ZH=$(native --game Zonder --dry-run 2>"$T/err"); RC=$?
[[ $RC -eq 0 && "$ZH" != *"/rg/tabel"* && "$ZH" != *RG_RT_TABLE* ]] || fail "een spel zonder de hulp krijgt toch een tabel (exit $RC): $ZH"
ZP=$(native --game Zonder --env LD_PRELOAD=/pm/eigen.so:/rg/lib/librg_rt_input.so --dry-run 2>"$T/err")
[[ "$(tel "--ro-bind $N/patches/Zonder/rt-invoer.ini $DOEL " "$ZP ")" -eq 1 && "$(tel "RG_RT_TABLE" "$ZP")" -eq 1 ]] || fail "met de hulp via --env LD_PRELOAD komt de tabel er niet bij: $ZP"

# 6. via gamescope: de buitenste aanroep geeft de tabel door, de binnenste zet hem er precies een keer in
BUITEN=$(native --game Met --headless --dry-run 2>"$T/err"); RC=$?
[[ $RC -eq 0 && "$BUITEN" == RG_NATIVE_INNER=1* ]] || fail "de buitenste aanroep (--headless --dry-run) geeft geen commando (exit $RC): $BUITEN $(cat "$T/err")"
[[ "$(tel "--bind-ro $MEE $DOEL" "$BUITEN")" -eq 1 && "$(tel "--env RG_RT_TABLE=$DOEL" "$BUITEN")" -eq 1 ]] || fail "de buitenste aanroep geeft de tabel niet (precies een keer) door: $BUITEN"
BINNEN=$(env RG_NATIVE_INNER=1 PATH="$T/bin:$PATH" DISPLAY=:99 XAUTHORITY= XDG_DATA_HOME="$T/data" XDG_STATE_HOME="$T/state" XDG_RUNTIME_DIR="$T/run" \
             RAVENGUN_NAS_TP="$T/nas" RAVENGUN_TPUI="$T/tpui" RAVENGUN_BIN_DIR="$N/build" RAVENGUN_LIB_DIR="$N/build" \
             "$N/scripts/play-native.sh" --game Met --bind-ro "$MEE" "$DOEL" --env "RG_RT_TABLE=$DOEL" --dry-run 2>"$T/err")
[[ "$(tel "--ro-bind $MEE $DOEL " "$BINNEN ")" -eq 1 && "$(tel "RG_RT_TABLE" "$BINNEN")" -eq 1 ]] || fail "de binnenste aanroep zet de tabel er niet precies een keer in: $BINNEN"
grep -qF "  invoertabel: $MEE sha256 $(sha "$MEE")" "$T/err" || fail "de binnenste aanroep (die het logboek schrijft) noemt de tabel niet: $(cat "$T/err")"

# 7. een echte doorloop (nep-bwrap): de tabel staat in het logboek van de run, voor de start
native --game Met >"$T/run.out" 2>&1; RC=$?
[[ $RC -eq 3 ]] || fail "de doorloop geeft de exitcode van het (nep-)spel niet terug ($RC): $(cat "$T/run.out")"
LOG=$(ls "$T/state/ravengun/logs"/native-Met-*.log 2>/dev/null | head -1)
if [[ -n "$LOG" ]]; then grep -qF "  invoertabel: $MEE sha256 $(sha "$MEE")" "$LOG" || fail "het logboek van de run noemt de tabel niet: $(head -12 "$LOG")"
else fail "de doorloop schreef geen logboek"; fi

[[ $FAILS -eq 0 ]] || { echo "$FAILS fout(en)"; exit 1; }
echo "ok: de invoertabel per game gaat mee de sandbox in (gebruikerslaag voor meegeleverd), en zonder tabel verandert er niets"

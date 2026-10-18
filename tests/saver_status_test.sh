#!/usr/bin/env bash
# Het statusbord van de tv: tools/dev/rg-saver-status.sh (de stand van het werk) en scripts/oled-saver.sh --game-state
# (draait er nu een spel). Hermetisch: een eigen statusmap, een namaak-proceslijst en een namaak-/proc.
#
# Wat hier wordt afgedwongen, omdat een verkeerd gelezen status erger is dan geen status:
#   - ontbreekt het statusbestand of staat er onzin in, dan zegt het bord dat (exitcode 1), nooit "goed";
#   - een status die niet klopt wordt NIET geschreven en het oude bestand blijft staan;
#   - "draait er nu een spel" kent drie antwoorden, en een headless meting telt niet als een spel op de tv.
# Overgeslagen (77) zonder de app (scripts/build-app.sh): de ontleder van het bord zit in de app.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP="${RAVENGUN_APP_BIN:-}"
for c in "$APP" "$ROOT/build-app/ravengun" "$ROOT/build/ravengun"; do [[ -n "$c" && -x "$c" ]] && { APP="$c"; break; }; done
[[ -n "$APP" && -x "$APP" ]] || { echo "overgeslagen: geen app-build (scripts/build-app.sh)"; exit 77; }
"$APP" --help 2>/dev/null | grep -q -- "--saver-check" || QT_QPA_PLATFORM=offscreen "$APP" --help 2>/dev/null | grep -q -- "--saver-check" \
  || { echo "overgeslagen: deze app-build kent --saver-check nog niet (scripts/build-app.sh)"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
FAILS=0
fail() { echo "FAIL: $*"; FAILS=$((FAILS + 1)); }
export XDG_STATE_HOME="$T/state" RAVENGUN_APP_BIN="$APP"
F="$T/state/ravengun/saver-status.txt"
S="$ROOT/tools/dev/rg-saver-status.sh"

# 1. Geen bestand: het bord zegt dat er geen status is.
UIT=$("$S" --show 2>&1); RC=$?
[[ $RC -eq 1 ]] || fail "zonder bestand: exitcode $RC, verwacht 1"
grep -q "No status available" <<<"$UIT" && grep -q "the status file is missing" <<<"$UIT" || fail "zonder bestand: $UIT"
grep -q "Going well" <<<"$UIT" && fail "zonder bestand staat er 'Het gaat goed'"

# 2. Een goede status wordt geschreven en komt er in gewone taal uit.
UIT=$("$S" --spel Halo --bezig "Richten en schieten testen" --stand goed --stap "3 van 5" \
          --doel "Zien of de trekker aankomt" --klaar "Beeld op de tv; Kalibratie overgeslagen" --nog "Twee minuten spelen" \
          --gehaald "Rabbids" 2>&1); RC=$?
[[ $RC -eq 0 ]] || fail "goede status: exitcode $RC ($UIT)"
[[ -f "$F" ]] || fail "goede status: het bestand is niet geschreven"
for regel in "Halo: Richten en schieten testen" "Going well · step 3 of 5" "Purpose: Zien of de trekker aankomt" \
             "Done: Beeld op de tv · Kalibratie overgeslagen" "To do: Twee minuten spelen" \
             "Passed (5 minutes attract or play): Rabbids"; do
  grep -qF "$regel" <<<"$UIT" || fail "goede status: de regel '$regel' ontbreekt in: $UIT"
done
grep -q "^spel: Halo$" "$F" && grep -q "^stand: goed$" "$F" || fail "het bestand heeft niet het afgesproken formaat: $(cat "$F")"
"$S" --show >/dev/null 2>&1 || fail "--show keurt de zojuist geschreven status af"
GOED=$(cat "$F")

# 2b. De nieuwe sleutels: vraag en doelen, en de activiteitenlijst (--feed).
UIT=$("$S" --spel Halo --bezig Testen --stand bezig --vraag "Met of zonder kalibratie?" --doelen "Alles halen; Wiki" 2>&1); RC=$?
[[ $RC -eq 0 ]] || fail "vraag: exitcode $RC ($UIT)"
grep -qF "Question for you: Met of zonder kalibratie?" <<<"$UIT" || fail "vraag ontbreekt in: $UIT"
grep -q "^vraag: Met of zonder kalibratie?$" "$F" && grep -q "^doelen: Alles halen; Wiki$" "$F" || fail "vraag/doelen niet in het bestand: $(cat "$F")"
"$S" --spel Halo --bezig Testen --stand bezig >/dev/null 2>&1; grep -q "^vraag:" "$F" && fail "de vraag blijft staan na een status zonder --vraag"
GOED=$(cat "$F")
FD="$T/state/ravengun/saver-feed.txt"
"$S" --feed "Halo gestart" >/dev/null || fail "--feed alleen"
[[ "$(cat "$F")" == "$GOED" ]] || fail "--feed alleen heeft de status veranderd"
grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2} Halo gestart$' "$FD" || fail "feedregel klopt niet: $(cat "$FD")"
for i in $(seq 1 60); do "$S" --feed "regel $i" >/dev/null; done
[[ $(wc -l < "$FD") -eq 50 ]] || fail "de feed houdt $(wc -l < "$FD") regels, verwacht 50"
tail -n 1 "$FD" | grep -q "regel 60$" || fail "de nieuwste feedregel staat niet onderaan"
"$S" --spel Halo --bezig Testen --stand bezig --feed "samen met status" >/dev/null || fail "status met --feed"
tail -n 1 "$FD" | grep -q "samen met status$" || fail "feed bij status ontbreekt"
LEN=$(wc -l < "$FD")
"$S" --spel Halo --bezig Testen --stand prima --feed "niet schrijven" >/dev/null 2>&1
[[ $(wc -l < "$FD") -eq $LEN ]] || fail "een afgekeurde status schreef toch een feedregel"
"$S" --clear-feed >/dev/null; [[ ! -e "$FD" ]] || fail "--clear-feed"

# 3. Een status die niet klopt wordt niet geschreven; het oude bestand blijft staan.
nee() {   # $1 = wat er in de melding moet staan; de rest = de opties
  local wat="$1"; shift
  local uit rc; uit=$("$S" "$@" 2>&1); rc=$?
  [[ $rc -eq 1 ]] || fail "moet geweigerd worden (exitcode $rc): $*"
  grep -q "NIET geschreven" <<<"$uit" && grep -qF "$wat" <<<"$uit" || fail "melding klopt niet voor [$*]: $uit"
  [[ "$(cat "$F")" == "$GOED" ]] || fail "het oude bestand is veranderd door een afgekeurde status: $*"
}
nee "'stand' is missing" --spel Halo --bezig Testen
nee "'spel' is missing" --bezig Testen --stand goed
nee "goed, slecht or bezig" --spel Halo --bezig Testen --stand prima
nee "that cannot be" --spel Halo --bezig Testen --stand goed --stap "6 van 5"
nee "3 of 5" --spel Halo --bezig Testen --stand goed --stap "de derde"
nee "too long" --spel Halo --bezig "$(printf 'x%.0s' $(seq 1 200))" --stand goed

# 4. Onzin in het bestand zelf (met de hand geschreven): het bord zegt dat, met de reden.
onzin() {   # $1 = inhoud, $2 = wat er in de reden moet staan
  printf '%b' "$1" > "$T/onzin.txt"
  local uit rc; uit=$("$S" --check "$T/onzin.txt" 2>&1); rc=$?
  [[ $rc -eq 1 ]] || fail "onzin moet exitcode 1 geven ($rc): $1"
  grep -q "No status available" <<<"$uit" && grep -qF "$2" <<<"$uit" || fail "onzin [$1]: $uit"
  grep -q "Going well" <<<"$uit" && fail "onzin [$1] wordt als 'Het gaat goed' getoond"
}
onzin "" "empty"
onzin "alles goed\n" "not 'key: value'"
onzin '{"spel": "Halo", "stand": "goed"}\n' "unknown key"
onzin "spel: Halo\nbezig: Testen\nstand: goed\nkleur: groen\n" "unknown key 'kleur'"
onzin "spel: Halo\nspel: Rabbids\nbezig: Testen\nstand: goed\n" "appears twice"
onzin "spel: Halo\nbezig: Testen\nstand:\n" "'stand' is missing"
head -c 4096 /dev/urandom > "$T/ruis.txt"
"$S" --check "$T/ruis.txt" >/dev/null 2>&1 && fail "willekeurige bytes worden als geldige status gelezen"

# 5. --clear: daarna is er geen status.
"$S" --clear >/dev/null || fail "--clear"
"$S" --show >/dev/null 2>&1 && fail "na --clear is er nog een status"

# 6. Draait er nu een spel? (scripts/oled-saver.sh --game-state)
O="$ROOT/scripts/oled-saver.sh"
mkdir -p "$T/proc/100" "$T/proc/200"
printf 'HOME=/x\0DISPLAY=:0\0' > "$T/proc/100/environ"       # een proces met een scherm
printf 'HOME=/x\0PATH=/bin\0' > "$T/proc/200/environ"         # een proces zonder scherm
staat() {   # $1 = verwacht; stdin = de proceslijst
  cat > "$T/ps.txt"
  local uit; uit=$(RG_SAVER_PS_FILE="$T/ps.txt" RG_SAVER_PROC="$T/proc" RG_SAVER_HOLD_DIR="$T/hold" "$O" --game-state 2>&1)
  [[ "$uit" == "$1" ]] || fail "game-state: verwacht '$1', kreeg '$uit' bij: $(tr '\n' '|' < "$T/ps.txt")"
}
staat geen <<'EOF'
1 /usr/lib/systemd/systemd --user
50 /usr/bin/plasmashell --no-respawn
60 bash /x/scripts/oled-saver.sh --loop
EOF
staat zichtbaar <<'EOF'
300 gamescope -f -W 1920 -H 1080 -w 1920 -h 1080 --force-grab-cursor -- /x/bin/rg-exec --keep DISPLAY -- /usr/bin/bwrap --die-with-parent
100 /x/bin/rg-exec --keep DISPLAY -- /usr/bin/bwrap --die-with-parent
EOF
staat onzichtbaar <<'EOF'
300 gamescope --backend headless -W 1920 -H 1080 -w 1920 -h 1080 -- /x/bin/rg-exec --keep DISPLAY -- /usr/bin/bwrap --die-with-parent
100 /x/bin/rg-exec --keep DISPLAY -- /usr/bin/bwrap --die-with-parent
EOF
staat zichtbaar <<'EOF'
100 /x/bin/rg-direct --run /x/state/ravengun/direct/LuigisMansion.json
EOF
staat onzichtbaar <<'EOF'
200 /x/bin/rg-direct --run /x/state/ravengun/direct/LuigisMansion.json
EOF
staat geen <<'EOF'
200 /x/bin/rg-direct --plan LuigisMansion
EOF
staat zichtbaar <<'EOF'
300 /usr/bin/gamescope -W 1280 -H 720 -S fit -f --backend wayland -- /x/wine /x/BudgieLoader.exe Z:\game
200 /x/bin/rg-direct --run /x/state/ravengun/direct/Halo.json
EOF

# 7. Een run heeft het scherm genomen (de houder van tools/dev/rg-tv-env.sh): het bord wijkt, welk programma er ook draait --
#    ook een dat het bord niet kent en dat zonder houder "geen spel" zou opleveren. Een houder van een proces dat weg is telt
#    niet en wordt opgeruimd; een houder waarvan het pid nu van een ANDER proces is (andere starttijd) ook niet.
mkdir -p "$T/hold" "$T/proc/300"
printf 'x' > "$T/proc/300/environ"; printf '300 (bash) S 1 300 300 0 -1 4194304 0 0 0 0 0 0 0 0 20 0 1 0 5555 1 1\n' > "$T/proc/300/stat"
ONBEKEND=$'1 /usr/lib/systemd/systemd --user\n300 /bin/bash /tmp/x/binnen.sh\n301 /x/rg-elfloader /tmp/x/view/game'
staat geen <<<"$ONBEKEND"                                   # zonder houder: het bord kent dit programma niet
printf '5555\n13:57:00\n/bin/bash /tmp/x/binnen.sh \n' > "$T/hold/300"
staat zichtbaar <<<"$ONBEKEND"                              # met een levende houder: het bord wijkt
[[ -f "$T/hold/300" ]] || fail "een levende houder is weggehaald"
printf '4444\n13:57:00\niets anders \n' > "$T/hold/300"       # hetzelfde pid, een andere starttijd: een ander proces
staat geen <<<"$ONBEKEND"
[[ ! -e "$T/hold/300" ]] || fail "een houder van een ander proces met hetzelfde pid is niet opgeruimd"
printf '1\n13:57:00\nweg \n' > "$T/hold/999"                  # het proces bestaat niet meer
staat geen <<<"$ONBEKEND"
[[ ! -e "$T/hold/999" ]] || fail "een houder van een proces dat weg is, is niet opgeruimd"
printf '5555\n13:57:00\n/bin/bash /tmp/x/binnen.sh \n' > "$T/hold/300"
staat zichtbaar <<<$'1 /usr/lib/systemd/systemd --user\n300 /bin/bash /tmp/x/binnen.sh\n70 gamescope --backend headless -W 1920 -H 1080 -- x'
rm -f "$T/hold/300"

[[ $FAILS -eq 0 ]] || { echo "$FAILS fout(en)"; exit 1; }
echo "ok: het statusbord keurt onzin af, schrijft alleen wat klopt, en weet of er een spel draait"

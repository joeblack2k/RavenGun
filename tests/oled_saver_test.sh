#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# OLED-bescherming: een spel dat de daemon start moet het saver-venster wegzetten.
#
# De lus (ravengun --saver-loop, unit ravengun-oled-saver.service) kijkt elke seconde naar de
# processen en zet het venster ravengun-oled-view.service uit zodra er een spel zichtbaar is
# (gamescope zonder --backend headless, of ravengun-tpui, of een van onze laders met een scherm).
# Dat is dezelfde route voor een spel dat de app start, dat de web-UI start en dat ravengunctl
# start: ze komen alle drie bij dezelfde unit en hetzelfde gamescope-proces uit.
#
# Deze test draait de echte lus met een nagemaakte /proc, een nagemaakte systemctl en een
# nagemaakte systemd-run (alle drie hebben daarvoor een omgevingsvariabele). Zo is te zien dat
# het venster stopt bij een spel en weer start als het spel weg is.
set -u
# De binary: het eerste argument (CMake geeft $<TARGET_FILE:ravengun_app> mee, dus de GEBOUWDE app),
# anders RAVENGUN_APP, anders de geinstalleerde app. Zonder binary: skip (77).
BIN=${1:-}
[ -z "$BIN" ] && BIN=${RAVENGUN_APP:-}
for k in "$BIN" "$HOME/.local/opt/ravengun/bin/ravengun" /usr/local/bin/ravengun; do
    [ -n "$k" ] && [ -x "$k" ] && BIN=$k && break
done
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "skip: geen ravengun-app (Qt) om de saver-lus te draaien (zet RAVENGUN_APP)"
    exit 77
fi

T=$(mktemp -d) || exit 1
LUS=""
trap '[ -n "$LUS" ] && kill "$LUS" 2>/dev/null; rm -rf "$T"' EXIT
mkdir -p "$T/proc" "$T/hold" "$T/state"
CTL_LOG="$T/ctl.log"; RUN_LOG="$T/run.log"; VIEW_ON="$T/view-on"
: > "$CTL_LOG"; : > "$RUN_LOG"; : > "$VIEW_ON"

printf '#!/bin/sh\necho "$*" >> "%s"\ncase "$*" in\n  *is-active*ravengun-oled-view.service*) [ -f "%s" ] && exit 0 || exit 3 ;;\n  *is-active*) exit 3 ;;\nesac\nexit 0\n' "$CTL_LOG" "$VIEW_ON" > "$T/systemctl"
printf '#!/bin/sh\necho "$*" >> "%s"\nexit 0\n' "$RUN_LOG" > "$T/systemd-run"
chmod +x "$T/systemctl" "$T/systemd-run"

# Een spel dat zichtbaar is: gamescope (Wayland-backend, dus op de tv) in de nagemaakte /proc.
maak_spel() {
    rm -rf "$T/proc/1001"
    mkdir -p "$T/proc/1001"
    printf 'gamescope\0-W\01920\0-H\01080\0-S\0fit\0-f\0--backend\0wayland\0--\0wine\0' > "$T/proc/1001/cmdline"
    printf 'WAYLAND_DISPLAY=wayland-0\0DISPLAY=:1\0' > "$T/proc/1001/environ"
}
start_lus() {
    env RG_SAVER_IDLE_S=1 RG_SAVER_PROC="$T/proc" RG_SAVER_HOLD_DIR="$T/hold" \
        XDG_STATE_HOME="$T/state" RAVENGUN_SYSTEMCTL="$T/systemctl" \
        RAVENGUN_SYSTEMD_RUN="$T/systemd-run" RAVENGUN_APP="$BIN" \
        "$BIN" --saver-loop >/dev/null 2>&1 &
    LUS=$!
}
stop_lus() {
    [ -n "$LUS" ] && kill "$LUS" 2>/dev/null
    [ -n "$LUS" ] && wait "$LUS" 2>/dev/null
    LUS=""
}
# Wachten tot het logboek het verwachte teken bevat (de lus schrijft elke seconde); hooguit tien
# seconden. Geen vaste slaap: op een langzame machine is die te kort en op een snelle te lang.
wacht_op() {
    patroon=$1; bestand=$2; i=0
    while [ "$i" -lt 10 ]; do
        grep -q -- "$patroon" "$bestand" 2>/dev/null && return 0
        sleep 1
        i=$((i + 1))
    done
    return 1
}
fail() { echo "FAIL: $*"; exit 1; }

# 1. Venster staat aan, spel zichtbaar -> het venster moet uit.
maak_spel
: > "$VIEW_ON"
: > "$CTL_LOG"
start_lus
wacht_op "stop ravengun-oled-view.service" "$CTL_LOG" || true
grep -q "stop ravengun-oled-view.service" "$CTL_LOG" ||
    fail "de lus zette het saver-venster niet uit terwijl er een spel zichtbaar was (log: $(tr '\n' '|' < "$CTL_LOG"))"
grep -q '^zichtbaar' "$T/state/ravengun/saver-game.txt" 2>/dev/null ||
    fail "de spelstand op het bord is niet 'zichtbaar' (bestand: $(cat "$T/state/ravengun/saver-game.txt" 2>/dev/null))"
stop_lus

# 2. Geen spel en het venster staat aan -> er gebeurt niets (geen start- of stopopdracht).
rm -rf "$T/proc/1001"
: > "$VIEW_ON"
: > "$CTL_LOG"; : > "$RUN_LOG"
start_lus
wacht_op "is-active" "$CTL_LOG" || true
grep -q ' stop ' "$CTL_LOG" && fail "de lus zette het venster uit terwijl er geen spel was (log: $(tr '\n' '|' < "$CTL_LOG"))"
[ -s "$RUN_LOG" ] && fail "de lus startte het venster opnieuw terwijl het al aan stond"
stop_lus

# 3. Geen spel, venster uit -> na de wachttijd moet het venster weer starten.
rm -rf "$T/proc/1001"; rm -f "$VIEW_ON"
: > "$CTL_LOG"; : > "$RUN_LOG"
start_lus
wacht_op -- "--unit=ravengun-oled-view" "$RUN_LOG" || true
grep -q -- "--unit=ravengun-oled-view" "$RUN_LOG" ||
    fail "de lus zette het saver-venster niet terug toen het spel weg was (log: $(tr '\n' '|' < "$RUN_LOG"))"
grep -q '^geen' "$T/state/ravengun/saver-game.txt" 2>/dev/null ||
    fail "de spelstand op het bord is niet 'geen' (bestand: $(cat "$T/state/ravengun/saver-game.txt" 2>/dev/null))"
stop_lus

echo "oled-saver-test: alles in orde"

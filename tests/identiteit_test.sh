#!/usr/bin/env bash
# De startscripts zeggen zelf wie ze zijn: scripts/rg-identiteit.sh, en het gebruik ervan in play-native.sh en play-halo.sh.
# Drie metingen in een week gingen verloren aan een oude kopie waarvan niemand wist dat hij draaide. Wat hier wordt afgedwongen:
#   1. rg_identiteit noemt het pad, de ECHTE sha256 (nagerekend) en de soort: repo, geinstalleerd (met versienummer), losse kopie;
#   2. bestanden die bij de run horen staan erbij met hun sha256, en een bestand dat ontbreekt wordt genoemd, niet overgeslagen;
#   3. vanuit een niet-geinstalleerde kopie staat erbij of het geinstalleerde script hetzelfde is of een ander;
#   4. play-native.sh zet het bovenaan het logboek van een run (een echte doorloop van het script, met een nep-bwrap: er start
#      niets), en bij --dry-run op stderr zodat stdout het commando blijft;
#   5. play-halo.sh schrijft het voor elke start in het logboek.
# Hermetisch: een eigen namaak-repo, -installatie en -NAS in een tijdelijke map. Skip (77) zonder sha256sum.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
command -v sha256sum >/dev/null || { echo "overgeslagen: geen sha256sum"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
FAILS=0
fail() { echo "FAIL: $*"; FAILS=$((FAILS + 1)); }
sha() { sha256sum "$1" | cut -d' ' -f1; }
export HOME="$T/home"; mkdir -p "$HOME"
. "$ROOT/scripts/rg-identiteit.sh"

# --- 1-3. de functie zelf -------------------------------------------------------------------------------------------
# een losse kopie
mkdir -p "$T/los/scripts"; printf '#!/bin/sh\necho hoi\n' > "$T/los/scripts/start.sh"; printf 'data\n' > "$T/los/gegevens.json"
UIT=$(rg_identiteit "$T/los/scripts/start.sh" gegevens="$T/los/gegevens.json" lader="$T/los/bestaat-niet")
[[ "$(sed -n 1p <<<"$UIT")" == "script: $T/los/scripts/start.sh" ]] || fail "eerste regel: $UIT"
grep -qF "een losse kopie (geen repo, geen installatie); sha256 $(sha "$T/los/scripts/start.sh")" <<<"$UIT" || fail "losse kopie of sha256 klopt niet: $UIT"
grep -qF "  gegevens: $T/los/gegevens.json sha256 $(sha "$T/los/gegevens.json")" <<<"$UIT" || fail "het gegevensbestand staat er niet met zijn sha256: $UIT"
grep -qF "  lader: $T/los/bestaat-niet ONTBREEKT" <<<"$UIT" || fail "een ontbrekend bestand wordt niet genoemd: $UIT"
grep -q "geinstalleerde script" <<<"$UIT" && fail "zonder installatie hoort er geen vergelijking te staan: $UIT"
# een andere inhoud is een andere sha256
printf '# een regel erbij\n' >> "$T/los/scripts/start.sh"
grep -qF "sha256 $(sha "$T/los/scripts/start.sh")" <<<"$(rg_identiteit "$T/los/scripts/start.sh")" || fail "na een wijziging klopt de sha256 niet meer"

# een installatie: <prefix>/bin en <prefix>/share/ravengun/scripts, met VERSION
mkdir -p "$T/inst/bin" "$T/inst/share/ravengun/scripts"; cp "$T/los/scripts/start.sh" "$T/inst/share/ravengun/scripts/start.sh"
printf '0.1.0\nv9.9.9-proef\n' > "$T/inst/share/ravengun/VERSION"
UIT=$(rg_identiteit "$T/inst/share/ravengun/scripts/start.sh")
grep -qF "de GEINSTALLEERDE versie (0.1.0 v9.9.9-proef); sha256 $(sha "$T/inst/share/ravengun/scripts/start.sh")" <<<"$UIT" || fail "installatie: $UIT"
rm "$T/inst/share/ravengun/VERSION"
grep -qF "de GEINSTALLEERDE versie; sha256" <<<"$(rg_identiteit "$T/inst/share/ravengun/scripts/start.sh")" || fail "installatie zonder VERSION"

# een repo (een .git ernaast; zonder echte git-gegevens blijft de commit weg en dat mag)
mkdir -p "$T/repo/scripts" "$T/repo/.git"; cp "$T/los/scripts/start.sh" "$T/repo/scripts/start.sh"
UIT=$(rg_identiteit "$T/repo/scripts/start.sh")
grep -qF "de versie uit de REPO" <<<"$UIT" && grep -qF "sha256 $(sha "$T/repo/scripts/start.sh")" <<<"$UIT" || fail "repo: $UIT"
# de echte repo: met de commit
if [[ -e "$ROOT/.git" ]] && command -v git >/dev/null && git -C "$ROOT" rev-parse HEAD >/dev/null 2>&1; then
  UIT=$(rg_identiteit "$ROOT/scripts/rg-paths.sh")
  grep -qE "de versie uit de REPO \(commit [0-9a-f]{7,}; (met wijzigingen in scripts/|scripts/ gelijk aan)" <<<"$UIT" || fail "de echte repo noemt geen commit: $UIT"
fi

# vanuit de repo: is het geinstalleerde script hetzelfde?
mkdir -p "$HOME/.local/opt/ravengun/share/ravengun/scripts"; I="$HOME/.local/opt/ravengun/share/ravengun/scripts/start.sh"
cp "$T/repo/scripts/start.sh" "$I"
grep -qF "  het geinstalleerde script ($I) is hetzelfde" <<<"$(rg_identiteit "$T/repo/scripts/start.sh")" || fail "gelijke installatie niet herkend"
printf '# de installatie loopt achter\n' >> "$I"
grep -qF "  het geinstalleerde script ($I) is een ANDERE versie (sha256 $(sha "$I"))" <<<"$(rg_identiteit "$T/repo/scripts/start.sh")" || fail "andere installatie niet gemeld"
rm -rf "$HOME/.local/opt"

# --- 4. play-native.sh van begin tot eind, zonder iets te starten ---------------------------------------------------
# Een namaak-datamap met het echte script, een eigen gegevensbestand met een nep-spel, een nep-NAS en een nep-bwrap.
N="$T/native"; mkdir -p "$N/scripts" "$N/packaging" "$N/build" "$T/nas/Spel/pm" "$T/bin" "$T/extra"
cp "$ROOT/scripts/play-native.sh" "$ROOT/scripts/rg-paths.sh" "$ROOT/scripts/rg-identiteit.sh" "$N/scripts/"
printf 'x' > "$T/nas/Spel/pm/game"; printf 'onze bibliotheek\n' > "$N/build/librg_proef.so"; printf 'meegegeven\n' > "$T/extra/tap.so"
cat > "$N/packaging/native-games.json" <<JSON
{"defaults": {"res": "1280x720"},
 "games": {"Nep": {"title": "Nepspel", "status": "werkt", "enabled": true, "arch": "i386", "exe": "/pm/game", "cwd": "/pm",
   "args": ["-n", "-f{res}"], "mounts": [{"mode": "ro", "src": "@nas/Spel/pm", "dst": "/pm"}], "libs": ["@build/librg_proef.so"]}}}
JSON
printf '#!/bin/sh\necho "nep-bwrap: er wordt niets gestart"\nexit 3\n' > "$T/bin/bwrap"; chmod +x "$T/bin/bwrap"
native() { env PATH="$T/bin:$PATH" DISPLAY=:99 XAUTHORITY= XDG_DATA_HOME="$T/data" XDG_STATE_HOME="$T/state" XDG_RUNTIME_DIR="$T/run" \
               RAVENGUN_NAS_TP="$T/nas" RAVENGUN_TPUI="$T/tpui" RAVENGUN_BIN_DIR="$N/build" RAVENGUN_LIB_DIR="$N/build" \
               "$N/scripts/play-native.sh" "$@"; }
mkdir -p "$T/run" "$T/tpui"
if grep -q "rg_identiteit" "$N/scripts/play-native.sh"; then
  PN="$N/scripts/play-native.sh"
  # --dry-run: de identiteit op stderr, het commando alleen op stdout
  OUT=$(native --game Nep --bind-ro "$T/extra/tap.so" /rg/lib/tap.so --dry-run 2>"$T/dry.err"); RC=$?
  [[ $RC -eq 0 ]] || fail "play-native --dry-run: exitcode $RC ($(cat "$T/dry.err"))"
  [[ "$(wc -l <<<"$OUT")" -eq 1 && "$OUT" == bwrap* ]] || fail "--dry-run: stdout is niet alleen het commando: $OUT"
  grep -qF "script: $PN" "$T/dry.err" || fail "--dry-run: de identiteit staat niet op stderr: $(cat "$T/dry.err")"
  # een echte doorloop: het logboek begint met wie het script is
  native --game Nep --bind-ro "$T/extra/tap.so" /rg/lib/tap.so >"$T/run.out" 2>&1; RC=$?
  [[ $RC -eq 3 ]] || fail "play-native: de exitcode van het (nep-)spel komt niet terug ($RC): $(cat "$T/run.out")"
  LOG=$(ls "$T/state/ravengun/logs"/native-Nep-*.log 2>/dev/null | head -1)
  [[ -n "$LOG" ]] || fail "play-native: geen logboek geschreven"
  if [[ -n "$LOG" ]]; then
    [[ "$(sed -n 1p "$LOG")" == "== play-native Nep "* ]] || fail "logboek: eerste regel: $(sed -n 1p "$LOG")"
    [[ "$(sed -n 2p "$LOG")" == "script: $PN" ]] || fail "logboek: de tweede regel is niet het script: $(sed -n 2p "$LOG")"
    grep -qF "een losse kopie (geen repo, geen installatie); sha256 $(sha "$PN")" "$LOG" || fail "logboek: soort of sha256 van het script klopt niet"
    grep -qF "  gegevens: $N/packaging/native-games.json sha256 $(sha "$N/packaging/native-games.json")" "$LOG" || fail "logboek: het gegevensbestand ontbreekt"
    grep -qF "  bibliotheek: $N/build/librg_proef.so sha256 $(sha "$N/build/librg_proef.so")" "$LOG" || fail "logboek: onze eigen bibliotheek ontbreekt"
    grep -qF "  meegegeven: $T/extra/tap.so sha256 $(sha "$T/extra/tap.so")" "$LOG" || fail "logboek: het meegegeven bestand ontbreekt"
    grep -qF "  hulp: $N/scripts/rg-paths.sh sha256 $(sha "$N/scripts/rg-paths.sh")" "$LOG" || fail "logboek: het hulpscript ontbreekt"
    # de identiteit staat VOOR de start van het spel
    [[ "$(grep -n '^script: ' "$LOG" | cut -d: -f1)" -lt "$(grep -n 'nep-bwrap' "$LOG" | cut -d: -f1)" ]] || fail "logboek: de identiteit staat niet voor de start"
    grep -q "^cmd: /pm/game -n -f1280x720" "$LOG" || fail "logboek: de opdrachtregel van het spel is veranderd: $(grep '^cmd' "$LOG")"
  fi
else
  fail "scripts/play-native.sh noemt zichzelf niet (rg_identiteit ontbreekt)"
fi

# --- 5. play-halo.sh: voor elke start een regel in het logboek ------------------------------------------------------
# Het script zelf draaien kan hier niet (het wil de daemon, systemd en een spel). Daarom de bron: het leest de hulp in, en
# voor elke start van een eenheid staat de aanroep die de identiteit in het logboek schrijft.
PH="$ROOT/scripts/play-halo.sh"
grep -q '^\. "\$ROOT/scripts/rg-identiteit.sh"' "$PH" || fail "play-halo.sh leest rg-identiteit.sh niet in"
STARTS=$(grep -c 'systemd-run --user --unit="\$UNIT"' "$PH"); WIE=$(grep -c '^ *wie_ben_ik$' "$PH")
[[ "$STARTS" -ge 1 && "$STARTS" -eq "$WIE" ]] || fail "play-halo.sh: $STARTS starts maar $WIE keer wie_ben_ik"
awk '/^ *wie_ben_ik$/ { getline; if ($0 !~ /systemd-run --user --unit=/) bad = 1 } END { exit bad }' "$PH" || fail "play-halo.sh: wie_ben_ik staat niet direct voor een start"
if grep -q "^wie_ben_ik()" "$PH"; then
  # de functie zelf, los uitgevoerd: schrijft in $LOG en laat het script nooit omvallen
  ( set -euo pipefail; ROOT="$ROOT"; LOG="$T/halo.log"; eval "$(grep '^wie_ben_ik()' "$PH")"; set -- ; wie_ben_ik ) || fail "wie_ben_ik faalt"
  grep -qF "sha256 " "$T/halo.log" 2>/dev/null || fail "wie_ben_ik schrijft niets in het logboek"
  ( set -euo pipefail; ROOT="$ROOT"; LOG="/bestaat/niet/x.log"; eval "$(grep '^wie_ben_ik()' "$PH")"; wie_ben_ik ) || fail "wie_ben_ik laat het script omvallen als het logboek niet te schrijven is"
fi

[[ $FAILS -eq 0 ]] || { echo "$FAILS fout(en)"; exit 1; }
echo "ok: de startscripts zeggen zelf wie ze zijn (pad, sha256, repo of installatie)"

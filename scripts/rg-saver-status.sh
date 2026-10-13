#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Zet de stand van het werk op het statusbord van de tv (`ravengun --saver`): welk spel wordt getest, wat er nu
# gebeurt, of het goed of slecht gaat, de hoeveelste stap het is, waar de test voor dient, wat al gedaan is en wat
# nog niet. Wie de kamer binnenloopt leest het in een oogopslag, in gewone taal.
#
# Het bestand: ~/.local/state/ravengun/saver-status.txt (XDG_STATE_HOME), `sleutel: waarde` per regel:
#     spel: Halo
#     bezig: Richten en schieten testen met de Move
#     stand: goed                      (goed | slecht | bezig; bezig = nog geen uitslag)
#     stap: 3 van 5                    (optioneel)
#     doel: Zien of de trekker en het richtpunt in het spel aankomen        (optioneel)
#     klaar: Beeld op de tv; Kalibratie overgeslagen                        (optioneel, met ; ertussen)
#     nog: Twee minuten spelen                                              (optioneel, met ; ertussen)
#     gehaald: Rabbids                 (optioneel: spellen die 5 minuten attract of spel haalden)
# Je mag het bestand ook zelf schrijven. Het bord leest het elke 12 seconden opnieuw.
#
# Een verkeerd gelezen status is erger dan geen status. Dit script keurt de status daarom eerst met de ontleder van
# het bord zelf (`ravengun --saver-check`) en schrijft hem ALLEEN als hij klopt; klopt hij niet, dan blijft het oude
# bestand staan en stopt het script met exitcode 1. Het bord zelf doet hetzelfde: ontbreekt het bestand of klopt het
# niet, dan staat er "Geen status beschikbaar" met de reden, nooit iets dat op "goed" lijkt.
#
# Gebruik:
#   scripts/rg-saver-status.sh --spel Halo --bezig "Richten en schieten testen" --stand goed \
#        [--stap "3 van 5"] [--doel "..."] [--klaar "a; b"] [--nog "c; d"] [--gehaald "x; y"]
#   scripts/rg-saver-status.sh --show            wat het bord nu toont (exitcode 1 als de status ontbreekt of niet klopt)
#   scripts/rg-saver-status.sh --check BESTAND   een bestand keuren zonder iets te schrijven
#   scripts/rg-saver-status.sh --clear           het statusbestand weghalen (het bord zegt dan dat er geen status is)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/scripts/rg-paths.sh"
STATE="${XDG_STATE_HOME:-$HOME/.local/state}/ravengun"
FILE="$STATE/saver-status.txt"
die() { echo "rg-saver-status: $*" >&2; exit 2; }

# De ontleder van het bord zelf. Zonder die ontleder wordt er niets geschreven: ongekeurd schrijven is juist het risico.
app() {
  local c
  for c in "${RAVENGUN_APP_BIN:-}" "$RG_BIN/ravengun" "$ROOT/build-app/ravengun" "$HOME/.local/opt/ravengun/bin/ravengun"; do
    [[ -n "$c" && -x "$c" ]] && { echo "$c"; return 0; }
  done
  return 1
}
keur() {   # $1 = bestand; drukt af wat het bord zou tonen; exitcode 1 als het niet klopt
  local bin; bin=$(app) || die "de app (ravengun) is niet gevonden, dus de status kan niet gekeurd worden (scripts/build-app.sh)"
  QT_QPA_PLATFORM=offscreen "$bin" --saver-check --saver-file "$1"
}

declare -A V=(); ORDE=(spel bezig stand stap doel klaar nog gehaald)
[[ $# -gt 0 ]] || { sed -n '2,29p' "$0"; exit 2; }
while [[ $# -gt 0 ]]; do
  case "$1" in
    --show)  keur "$FILE"; exit $? ;;
    --check) [[ $# -ge 2 ]] || die "--check vereist een bestand"; keur "$2"; exit $? ;;
    --clear) rm -f "$FILE"; echo "rg-saver-status: het statusbestand is weg; het bord zegt nu dat er geen status is"; exit 0 ;;
    -h|--help) sed -n '2,29p' "$0"; exit 0 ;;
    --spel|--bezig|--stand|--stap|--doel|--klaar|--nog|--gehaald)
      [[ $# -ge 2 ]] || die "$1 vereist een waarde"
      [[ "$2" != *$'\n'* ]] || die "$1: een waarde is een regel"
      V[${1#--}]="$2"; shift ;;
    *) die "onbekende optie: $1 (zie --help)" ;;
  esac
  shift
done

mkdir -p "$STATE"
TMP=$(mktemp "$STATE/saver-status.XXXXXX")
trap 'rm -f "$TMP"' EXIT
for k in "${ORDE[@]}"; do
  if [[ -n "${V[$k]:-}" ]]; then printf '%s: %s\n' "$k" "${V[$k]}" >> "$TMP"; fi
done
if ! UIT=$(keur "$TMP"); then
  echo "rg-saver-status: NIET geschreven, de status klopt niet:" >&2
  printf '%s\n' "$UIT" | sed 's/^/  /' >&2
  exit 1
fi
chmod 644 "$TMP"
mv -f "$TMP" "$FILE"
trap - EXIT
printf '%s\n' "$UIT"

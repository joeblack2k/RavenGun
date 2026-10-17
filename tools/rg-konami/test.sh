#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Test van rg-konami zonder game en zonder Wine:
#   1. de eenheidstest van de logica (test_konami.c, gewone gcc): IOB_DIGITAL_IN_W, richtpunt + io.sav-kalibratie (de CRC nagerekend
#      tegen de io.sav die de kern van TeknoParrot schreef), knoppen/munten van Gashaaaan, balinslagen;
#   2. als de DLL's gebouwd zijn (build.sh): exporttabel tegen de .def, alleen systeem-DLL's als import, en -- als de NAS er is
#      (alleen lezen) -- tegen het origineel en tegen alles wat de game uit deze DLL importeert.
#
# Gebruik: tools/rg-konami/test.sh [--le3 DIR] [--gsh DIR] [--nas DIR]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
LE3="$ROOT/patches/LethalEnforcers3/dropin"
GSH="$ROOT/patches/Gashaaaan/dropin"
NAS="$HOME/mnt/spidernas_emulation/Teknoparrot"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --le3) LE3="$2"; shift 2 ;;
    --gsh) GSH="$2"; shift 2 ;;
    --nas) NAS="$2"; shift 2 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cc -std=c11 -O2 -Wall -Wextra -Werror -o "$TMP/test_konami" "$HERE/test_konami.c" -lm
"$TMP/test_konami"
"$TMP/test_konami" --schrijf-iosav "$TMP/io.sav" >/dev/null
if [[ -f $LE3/SAVE/io.sav ]]; then
  cmp "$TMP/io.sav" "$LE3/SAVE/io.sav" && echo "io.sav in $LE3/SAVE is actueel"
fi

fout=0
controleer() {   # controleer <eigen.dll> <def> <origineel> <map met importeurs>
  local eigen="$1" def="$2" orig="$3" map="$4"
  [[ -f $eigen ]] || { echo "niet gebouwd: $eigen (tools/rg-konami/build.sh)"; return 0; }
  python3 -I "$HERE/vergelijk_exports.py" --def "$def" "$eigen" || fout=1
  if [[ -f $orig ]]; then
    python3 -I "$HERE/vergelijk_exports.py" --importeurs "$map" "$eigen" "$orig" || fout=1
  else
    echo "origineel niet gevonden ($orig): alleen tegen de .def vergeleken"
  fi
}
controleer "$LE3/iob.dll" "$HERE/le3_iob.def" "$NAS/Lethal Enforcers 3/iob.dll" "$NAS/Lethal Enforcers 3"
controleer "$LE3/gunio.dll" "$HERE/le3_gunio.def" "$NAS/Lethal Enforcers 3/gunio.dll" "$NAS/Lethal Enforcers 3"
controleer "$GSH/iob.dll" "$HERE/gsh_iob.def" "$NAS/Gashaaaan!!!!/bin/iob.dll" "$NAS/Gashaaaan!!!!/bin"
controleer "$GSH/ballio.dll" "$HERE/gsh_ballio.def" "$NAS/Gashaaaan!!!!/bin/ballio.dll" "$NAS/Gashaaaan!!!!/bin"
exit $fout

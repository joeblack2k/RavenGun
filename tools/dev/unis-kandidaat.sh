#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# De UNIS-kandidaten (Elevator Action Invasion, Raccoon Rampage) zonder TeknoParrot: bouwt rg-unis (tools/dev/build-unis.sh), zet per
# game een wegwerp-patchmap neer (game.kandidaat.ini als game.ini, dropin/dropin.ini, dropin/rg-unis.ini, dropin/UsbPluginsDll.dll) en
# controleert met `rg-direct --plan` en `rg-direct --tp-refs --expect-free` dat het plan niets van TeknoParrot meer noemt.
# Er wordt niets gestart en niets in ravengun-patches geschreven.
#
#   tools/dev/unis-kandidaat.sh [--out MAP] [--tv] [GAME ...]      GAME: ElevatorActionInvasion RaccoonRampage (standaard beide)
#     --out  de patchwortel die gemaakt wordt (standaard build/unis-kandidaat); die map is een --patches voor rg-direct en voor de app
#            ("RAVENGUN_PATCHES"/gebruikerslaag) en bevat per game ook de DLL, dus ze is meteen bruikbaar voor de tv-proef
#     --tv   alleen het plan tonen (geen controle op TP-verwijzingen)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAGE="$ROOT/docs/research/wartran/patches"
OUT="$ROOT/build/unis-kandidaat"
CHECK=1
GAMES=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --tv) CHECK=0; shift ;;
    -h|--help) sed -n '3,12p' "$0"; exit 0 ;;
    -*) echo "onbekende optie: $1" >&2; exit 2 ;;
    *) GAMES+=("$1"); shift ;;
  esac
done
[[ ${#GAMES[@]} -gt 0 ]] || GAMES=(ElevatorActionInvasion RaccoonRampage)
RGD="$ROOT/build/rg-direct"
[[ -x $RGD ]] || { echo "build/rg-direct ontbreekt (eerst: cmake -S . -B build && cmake --build build -j)" >&2; exit 1; }
fout=0
for g in "${GAMES[@]}"; do
  src="$STAGE/$g"
  [[ -f $src/game.kandidaat.ini ]] || { echo "$g: geen kandidaat in $src" >&2; fout=1; continue; }
  d="$OUT/$g"
  rm -rf "$d"; mkdir -p "$d/dropin"
  cp "$src/game.kandidaat.ini" "$d/game.ini"
  cp "$src/dropin/dropin.ini" "$src/dropin/rg-unis.ini" "$d/dropin/"
  "$ROOT/tools/dev/build-unis.sh" --out "$d/dropin" >/dev/null
  echo "== $g: patchmap $d"
  RAVENGUN_TP_DIR=/nonexistent "$RGD" --plan "$g" --patches "$OUT" || fout=1
  if [[ $CHECK -eq 1 ]]; then
    echo "-- TP-verwijzingen in het plan:"
    RAVENGUN_TP_DIR=/nonexistent "$RGD" --tp-refs --patches "$OUT" --expect-free "$g" || fout=1
  fi
done
exit $fout

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Zet de vervangers van de gesloten TeknoParrot-kern voor de WartranTroopers-familie in de patchmappen (docs/research/wartran/):
# bouwt de BepInEx-plugins (scripts/build-bepinex-plugin.sh) en rg-kaboom (tools/dev/build-kaboom.sh) en kopieert de klaargezette
# game.ini, dropin.ini en tabellen uit docs/research/wartran/patches/<Game>/ naar <PATCHES>/<Game>/.
#
#   tools/dev/wartran-uitrol.sh [--patches DIR] [--nas DIR] [GAME ...]
#     --patches  de patchwortel (standaard: patches/ van de repo, de link naar ravengun-patches/patches)
#     --nas      de map met de spelmappen, voor Managed/ van de Unity-games (standaard ~/mnt/spidernas_emulation/Teknoparrot; alleen
#                gelezen)
#     GAME       MarsSortie WildWestShootout AfterDark2 Rampage (standaard alle vier)
#
# Veiligheid: een game.ini (en de dropin.ini van Rampage) wordt alleen vervangen als het bestand in de patchmap nog precies het
# bestand is waarop de klaargezette versie gebouwd is (<bestand>.basis.sha256). Is het intussen gewijzigd, dan stopt het script
# voor die game met een melding: dan eerst met de hand samenvoegen (diff de twee bestanden).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAGE="$ROOT/docs/research/wartran/patches"
PATCHES="$ROOT/patches"
NAS="${HOME}/mnt/spidernas_emulation/Teknoparrot"
GAMES=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --patches) PATCHES="$2"; shift 2 ;;
    --nas) NAS="$2"; shift 2 ;;
    -h|--help) sed -n '3,15p' "$0"; exit 0 ;;
    -*) echo "onbekende optie: $1" >&2; exit 2 ;;
    *) GAMES+=("$1"); shift ;;
  esac
done
[[ ${#GAMES[@]} -gt 0 ]] || GAMES=(MarsSortie WildWestShootout AfterDark2 Rampage)
[[ -d $PATCHES ]] || { echo "patchwortel $PATCHES bestaat niet" >&2; exit 1; }

declare -A MANAGED=(
  [MarsSortie]="Mars Sortie/Shooter_Data/Managed"
  [WildWestShootout]="Wild West Shootout/CowBoy_Data/Managed"
  [AfterDark2]="Night Hunter - After Dark, Chapter 2/qumo2_en_Data/Managed"
)

basis_ok() {   # basis_ok <doel> <klaargezet>: het doel is nog het bestand waarop de klaargezette versie gebouwd is
  local doel="$1" basis="$2.basis.sha256"
  [[ -f $basis ]] || return 0
  [[ -f $doel ]] || { echo "  $doel ontbreekt" >&2; return 1; }
  [[ "$(sha256sum "$doel" | cut -d' ' -f1)" == "$(tr -d ' \n' < "$basis")" ]] && return 0
  if cmp -s "$doel" "$2"; then return 0; fi
  echo "  $doel is gewijzigd sinds de klaargezette versie (diff \"$doel\" \"$2\"); eerst samenvoegen" >&2
  return 1
}

fout=0
for g in "${GAMES[@]}"; do
  src="$STAGE/$g"
  [[ -d $src ]] || { echo "$g: niets klaargezet in $src" >&2; fout=1; continue; }
  dst="$PATCHES/$g"
  echo "== $g -> $dst"
  if ! basis_ok "$dst/game.ini" "$src/game.ini"; then fout=1; continue; fi
  if [[ -f $src/dropin/dropin.ini.basis.sha256 ]] && ! basis_ok "$dst/dropin/dropin.ini" "$src/dropin/dropin.ini"; then fout=1; continue; fi
  if [[ $g == Rampage ]]; then
    "$ROOT/tools/dev/build-kaboom.sh" --out "$dst/dropin"
  else
    "$ROOT/scripts/build-bepinex-plugin.sh" "$g" "$NAS/${MANAGED[$g]}"
    mkdir -p "$dst/dropin"
    cp "$ROOT/build/bepinex-plugin/$g/RavenGun.$g.dll" "$dst/dropin/"
  fi
  cp "$src/game.ini" "$dst/game.ini"
  for f in "$src"/dropin/*; do
    case "$f" in *.basis.sha256) continue ;; esac
    cp "$f" "$dst/dropin/"
  done
  echo "  klaar: game.ini, dropin/ ($(cd "$dst/dropin" && ls | tr '\n' ' '))"
done
exit $fout

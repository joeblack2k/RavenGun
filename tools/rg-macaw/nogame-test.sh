#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Proef van AVS + ea3_boot zonder het spel: rg-macaw.exe met RG_MACAW_NOGAME=1 in een wegwerpmap met alleen de twee AVS-dll's en
# prop/ van de game, eigen WINEPREFIX en zonder scherm. arkndd.dll wordt NIET geladen en er verschijnt niets op de tv. De game-map
# (NAS) wordt alleen gelezen: conf/ wordt gekopieerd, AVS schrijft in de kopie.
#
# Gebruik: tools/rg-macaw/nogame-test.sh <spelmap> <rg-macaw.exe> [VAR=waarde ...]
#   <spelmap>      de map met modules/, prop/ en conf/ (bv. .../Teknoparrot/Silent Scope Bone Eater)
#   <rg-macaw.exe> de gebouwde lader (tools/rg-macaw/build.sh --out <map>)
#   VAR=waarde     extra omgeving voor de lader, bv. RG_MACAW_ACCOUNTID=none (reproduceert "bad account ID") of
#                  RG_MACAW_SERVICES=offline
# Omgeving: WINE (standaard ~/.local/share/ravengun/wine/proton-11.0-2/bin/wine), SECS (wachttijd na ea3_boot, standaard 8).
# Uitvoer: het logboek van de lader (rg-macaw.log) op stdout; exitcode 0 als "ea3_boot: complete" van AVS erin staat.
set -euo pipefail
GAME="${1:?spelmap}"; EXE="${2:?rg-macaw.exe}"; shift 2
WINE="${WINE:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine}"
WINESERVER="$(dirname "$WINE")/wineserver"
T="$(mktemp -d)"
trap '"$WINESERVER" -k >/dev/null 2>&1 || true; rm -rf "$T"' EXIT
mkdir -p "$T/game/modules" "$T/pfx"
cp "$GAME/modules/libavs-win64.dll" "$GAME/modules/libavs-win64-ea3.dll" "$T/game/modules/"
cp -r "$GAME/prop" "$GAME/conf" "$T/game/"
chmod -R u+w "$T/game/conf"
cp "$EXE" "$T/game/rg-macaw.exe"
unset DISPLAY WAYLAND_DISPLAY
export WINEPREFIX="$T/pfx" WINEDEBUG=-all WINEARCH=win64
export WINEDLLOVERRIDES="mscoree,mshtml=d;winex11.drv,winewayland.drv=d"
export RG_MACAW_NOGAME=1 RG_MACAW_NOGAME_SECS="${SECS:-8}"
for kv in "$@"; do export "$kv"; done
cd "$T/game"
# het pad van de nep-arkndd.dll bepaalt alleen de spelmap (rgm_module_root); het bestand wordt niet geladen
timeout 180 "$WINE" "$T/game/rg-macaw.exe" "Z:${T//\//\\}\\game\\modules\\arkndd.dll" >/dev/null 2>&1 || true
cat rg-macaw.log
grep -q "ea3_boot: complete" rg-macaw.log

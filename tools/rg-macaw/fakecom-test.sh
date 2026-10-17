#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Test van de virtuele COM2 en de libacio-controle van rg-macaw.exe, zonder het spel: een nep-arkndd.dll (fakecom_game.c) doet dezelfde
# seriele aanroepen als het echte spel; rg-macaw.exe draait de volledige route (AVS, ea3, dll_entry_init, ea3_boot, dll_entry_main) onder
# Wine in een wegwerpmap, zonder scherm. arkndd.dll wordt niet geladen; er verschijnt niets op de tv.
#
# Gebruik: tools/rg-macaw/fakecom-test.sh <spelmap> <rg-macaw.exe>      (zie nogame-test.sh voor <spelmap>)
# Omgeving: WINE (standaard ~/.local/share/ravengun/wine/proton-11.0-2/bin/wine), SECS (wachttijd, standaard 6).
# Exitcode 0 = de nep-game meldt OK en het log heeft "virtuele COM: CreateFileA(COM2) faalde".
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
GAME="${1:?spelmap}"; EXE="${2:?rg-macaw.exe}"
WINE="${WINE:-$HOME/.local/share/ravengun/wine/proton-11.0-2/bin/wine}"
WINESERVER="$(dirname "$WINE")/wineserver"
CONTAINERFILE="$ROOT/packaging/containers/openparrot/Containerfile"
IMAGE="localhost/ravengun-openparrot-build:$(sha256sum "$CONTAINERFILE" | cut -c1-12)"
T="$(mktemp -d)"
trap '"$WINESERVER" -k >/dev/null 2>&1 || true; rm -rf "$T"' EXIT
mkdir -p "$T/game/modules" "$T/pfx" "$T/out"
podman run --rm --network=none -v "$HERE:/src:ro" -v "$T/out:/out" "$IMAGE" \
  x86_64-w64-mingw32-gcc -shared -O2 -Wall -Wextra -o /out/arkndd.dll /src/fakecom_game.c -static-libgcc
cp "$T/out/arkndd.dll" "$T/game/modules/arkndd.dll"
cp "$GAME/modules/libavs-win64.dll" "$GAME/modules/libavs-win64-ea3.dll" "$T/game/modules/"
cp -r "$GAME/prop" "$GAME/conf" "$T/game/"
chmod -R u+w "$T/game/conf"
cp "$EXE" "$T/game/rg-macaw.exe"
unset DISPLAY WAYLAND_DISPLAY
export WINEPREFIX="$T/pfx" WINEDEBUG=-all WINEARCH=win64
export WINEDLLOVERRIDES="mscoree,mshtml=d;winex11.drv,winewayland.drv=d"
cd "$T/game"
timeout 180 "$WINE" "$T/game/rg-macaw.exe" "Z:${T//\//\\}\\game\\modules\\arkndd.dll" >/dev/null 2>&1 || true
grep -v "^AVS: " rg-macaw.log
echo "--- fakecom-result.txt:"; cat fakecom-result.txt
grep -q "virtuele COM: CreateFileA(COM2) faalde" rg-macaw.log
grep -q "^OK" fakecom-result.txt   # de CRT schrijft CRLF
grep -q "ark-config.xml: noProjCheck = 1" rg-macaw.log
grep -q "ark-config.xml: loglevel = information" rg-macaw.log

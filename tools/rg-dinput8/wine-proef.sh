#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# De proef van rg-dinput8 onder Wine (geen spel): bouwt dinput8.dll en tools/rg-dinput8/proef_harnas.c met de mingw-sysroot en draait
# het proefprogramma in een eigen wegwerpprefix, zonder scherm (DISPLAY en WAYLAND_DISPLAY leeg), met WINEDLLOVERRIDES dinput8=n zoals
# dropin.ini van Bug Busters. Exitcode 0 = "harnas: ok". De eerste keer maakt Wine de prefix (een halve minuut).
#
# Gebruik: tools/rg-dinput8/wine-proef.sh [--wine PAD]   (standaard de Proton-Wine van RavenGun)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WINE="${HOME}/.local/share/ravengun/wine/proton-11.0-2/bin/wine"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --wine) WINE="$2"; shift 2 ;;
    -h|--help) sed -n '3,7p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
[[ -x $WINE ]] || { echo "Wine ontbreekt: $WINE" >&2; exit 77; }
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
CC="$SR/usr/bin/i686-w64-mingw32-gcc"
[[ -x $CC ]] || { echo "mingw-sysroot ontbreekt: $CC" >&2; exit 77; }
T="$(mktemp -d)"
trap 'WINEPREFIX="$T/pfx" "$(dirname "$WINE")/wineserver" -k 2>/dev/null || true; rm -rf "$T"' EXIT
mkdir -p "$T/run"
bash "$HERE/build.sh" --out "$T/run" >/dev/null
GCCV="$(ls "$SR/usr/lib/gcc/i686-w64-mingw32/" | head -1)"
"$CC" -B"$SR/usr/lib/gcc/i686-w64-mingw32/$GCCV/" -B"$SR/usr/i686-w64-mingw32/" -B"$SR/usr/i686-w64-mingw32/lib/" \
  -isystem "$SR/usr/i686-w64-mingw32/include" -L"$SR/usr/i686-w64-mingw32/lib" -O1 -Wall -o "$T/run/harnas.exe" \
  "$HERE/proef_harnas.c" -ldinput8 -ldxguid
cd "$T/run"
rc=0
env -u DISPLAY -u WAYLAND_DISPLAY WINEPREFIX="$T/pfx" WINEDEBUG=-all \
    WINEDLLOVERRIDES="dinput8=n;winemenubuilder.exe=d;mscoree=d;mshtml=d" RG_DINPUT8_LOG=rg-dinput8.log \
    timeout 240 "$WINE" harnas.exe > uit.txt 2>&1 || rc=$?
grep -v "XDG_RUNTIME_DIR\|ntsync\|configuration directory" uit.txt || true
echo "--- rg-dinput8.log"
cat rg-dinput8.log 2>/dev/null || true
exit $rc

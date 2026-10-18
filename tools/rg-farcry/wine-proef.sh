#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# De proef van de toetsenbordfilter in onze Usbioextreme.dll onder Wine (geen spel): bouwt de DLL en tools/rg-farcry/proef_harnas.c met de
# mingw-sysroot en draait het proefprogramma in een eigen wegwerpprefix, zonder scherm (DISPLAY en WAYLAND_DISPLAY leeg). Twee runs:
# "doorlaten" (RG_USBIO_KEYS=1: de toets '5' bereikt DirectInput, de controle op de proef zelf) en "blokkeren" (standaard: '5' weg, 'W' blijft).
# Exitcode 0 = beide goed, 77 = ontbrekende keten (overgeslagen).
#
# Gebruik: tools/rg-farcry/wine-proef.sh [--wine PAD]   (standaard de Proton-Wine van RavenGun)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WINE="${HOME}/.local/share/ravengun/wine/proton-11.0-2/bin/wine"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --wine) WINE="$2"; shift 2 ;;
    -h|--help) sed -n '3,8p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
[[ -x $WINE ]] || { echo "Wine ontbreekt: $WINE" >&2; exit 77; }
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
CC="$SR/usr/bin/i686-w64-mingw32-gcc"
[[ -x $CC ]] || { echo "mingw-sysroot ontbreekt: $CC" >&2; exit 77; }
T="$(mktemp -d)"
trap 'WINEPREFIX="$T/pfx" "$(dirname "$WINE")/wineserver" -k 2>/dev/null || true; rm -rf "$T"' EXIT
mkdir -p "$T/run" "$T/nep"
bash "$HERE/build.sh" "$T/run/Usbioextreme.dll" >/dev/null
GCCV="$(ls "$SR/usr/lib/gcc/i686-w64-mingw32/" | head -1)"
"$CC" -B"$SR/usr/lib/gcc/i686-w64-mingw32/$GCCV/" -B"$SR/usr/i686-w64-mingw32/" -B"$SR/usr/i686-w64-mingw32/lib/" \
  -isystem "$SR/usr/i686-w64-mingw32/include" -L"$SR/usr/i686-w64-mingw32/lib" -O1 -Wall -Wextra -o "$T/run/harnas.exe" \
  "$HERE/proef_harnas.c" -ldinput8 -ldxguid -luser32 -lkernel32
"$CC" -B"$SR/usr/lib/gcc/i686-w64-mingw32/$GCCV/" -B"$SR/usr/i686-w64-mingw32/" -B"$SR/usr/i686-w64-mingw32/lib/" \
  -isystem "$SR/usr/i686-w64-mingw32/include" -L"$SR/usr/i686-w64-mingw32/lib" -O1 -Wall -Wextra -shared -nostartfiles \
  -o "$T/nep/dinput8.dll" "$HERE/proef_nepdinput8.c" -ldxguid -luser32 -lkernel32 -Wl,--kill-at -Wl,-e,_DllMain@12 \
  || { echo "de nep-dinput8 bouwt niet" >&2; exit 1; }
cd "$T/run"
rc=0
# 1. Wine's eigen dinput8: de haken geven door. 2. de nep-dinput8 (native, naast de exe): de toetsen '5' en '6' weg of niet.
for modus in doorgifte doorlaten blokkeren; do
  echo "=== $modus"
  keys=0; [[ $modus == doorlaten ]] && keys=1
  ov="winemenubuilder.exe=d;mscoree=d;mshtml=d"
  if [[ $modus == doorgifte ]]; then rm -f dinput8.dll; else cp "$T/nep/dinput8.dll" dinput8.dll; ov="dinput8=n;$ov"; fi
  r=0
  env -u DISPLAY -u WAYLAND_DISPLAY WINEPREFIX="$T/pfx" WINEDEBUG=-all RG_USBIO_KEYS=$keys WINEDLLOVERRIDES="$ov" \
      timeout 240 "$WINE" harnas.exe "$modus" > "uit-$modus.txt" 2>&1 || r=$?
  grep -v "XDG_RUNTIME_DIR\|ntsync\|configuration directory" "uit-$modus.txt" || true
  echo "--- exitcode $r; rg-usbio.log:"
  grep -a -E "dinput8|DirectInput|IDirectInput|toets" rg-usbio.log 2>/dev/null || true
  [[ $r -eq 0 ]] || rc=$r
done
exit $rc

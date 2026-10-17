#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt de wajvio.dll van RavenGun (tools/rg-lostland/wajvio_rg.c) met de eigen mingw-w64-sysroot, zoals
# patches/SilentHill/build-shaiolib.sh, en draait de natieve test van de afbeelding (tests/lostland_wajv_map_test.c).
#
# Gebruik: tools/rg-lostland/build.sh [--out DIR]   (standaard patches/LostLandAdventure/dropin/)
# Schrijft nergens anders: niet naar ~/.local/share/ravengun.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/patches/LostLandAdventure/dropin"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    -h|--help) sed -n '2,6p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
CC="$SR/usr/bin/x86_64-w64-mingw32-gcc"
[ -x "$CC" ] || { echo "ontbreekt: $CC" >&2; exit 1; }
cc -O1 -Wall -Wextra -I"$HERE" -o "${TMPDIR:-/tmp}/lostland_wajv_map_test" "$ROOT/tests/lostland_wajv_map_test.c" "$HERE/rg_wajv_map.c"
"${TMPDIR:-/tmp}/lostland_wajv_map_test"
mkdir -p "$OUT"
DEF="$(mktemp -d)/wajvio.def"
cat > "$DEF" <<'DEF_EOF'
LIBRARY wajvio.dll
EXPORTS
    WAJVClose @1
    WAJVEnableWatchDogTimer @2
    WAJVGetInput @3
    WAJVGetLibraryVersion @4
    WAJVGetNodeCount @5
    WAJVGetRetryCount @6
    WAJVGetStatus @7
    WAJVOpen @8
    WAJVRestartPL @9
    WAJVSetAsyncComm @10
    WAJVSetFunctionSettings @11
    WAJVSetOutput @12
    WAJVSetPLParm @13
    WAJVSioRead @14
    WAJVSioSetMode @15
    WAJVSioWrite @16
    WAJVUpdate @17
    WAJVUseSio @18
DEF_EOF
"$CC" \
  -B"$SR/usr/lib/gcc/x86_64-w64-mingw32/16.2.0/" \
  -B"$SR/usr/x86_64-w64-mingw32/" \
  -B"$SR/usr/x86_64-w64-mingw32/lib/" \
  -isystem "$SR/usr/x86_64-w64-mingw32/include" \
  -L"$SR/usr/x86_64-w64-mingw32/lib" \
  -I"$HERE" -shared -O1 -Wall -Wextra -ffreestanding -fno-builtin \
  -fno-stack-protector -fno-asynchronous-unwind-tables -fno-ident \
  -nostdlib -nostartfiles -Wl,-e,DllMain -Wl,--no-insert-timestamp \
  -Wl,--image-base,0x180000000 -Wl,-s \
  -o "$OUT/wajvio.dll" "$HERE/wajvio_rg.c" "$HERE/rg_wajv_map.c" "$DEF" -lkernel32
echo "gebouwd: $(sha256sum "$OUT/wajvio.dll")"

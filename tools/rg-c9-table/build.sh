#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds patches/Coopers9/dropin/DINPUT8.dll (tools/rg-c9-table: dinput8 proxy + the repair of the runtime call table at
# 0x512000, 32-bit) with the mingw-w64 cross compiler in ~/.cache/ravengun-mingw.
#
# Usage: tools/rg-c9-table/build.sh [--out DIR]   (default patches/Coopers9/dropin/)
# Writes nowhere else: not to ~/.local/share/ravengun.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/patches/Coopers9/dropin"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    -h|--help) sed -n '3,8p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
SR="${RAVENGUN_MINGW:-$HOME/.cache/ravengun-mingw}/sysroot"
CC="$SR/usr/bin/i686-w64-mingw32-gcc"
OD="$SR/usr/bin/i686-w64-mingw32-objdump"
[[ -x "$CC" ]] || { echo "missing: $CC" >&2; exit 1; }
GCCV="$(ls "$SR/usr/lib/gcc/i686-w64-mingw32/" | sort -V | tail -1)"
[[ -f "$HERE/rg_c9_map.h" ]] || { echo "missing rg_c9_map.h: generate it with tools/rg-c9-table/genmap.py" >&2; exit 1; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$CC" -B"$SR/usr/lib/gcc/i686-w64-mingw32/$GCCV/" -B"$SR/usr/i686-w64-mingw32/" \
  -O2 -s -shared -Wall -Wextra -Werror -static-libgcc -Wl,--no-insert-timestamp \
  -I"$HERE" "$HERE/dinput8_proxy.c" "$HERE/rg_c9table.c" "$HERE/dinput8.def" -o "$TMP/DINPUT8.dll"
# The game takes exactly this one function from the DLL; without an export of that name it does not load at all.
"$OD" -p "$TMP/DINPUT8.dll" | grep -qE "DirectInput8Create$" \
  || { echo "DINPUT8.dll does not export DirectInput8Create" >&2; exit 1; }
mkdir -p "$OUT"
cp "$TMP/DINPUT8.dll" "$OUT/DINPUT8.dll"
echo "built: $(sha256sum "$OUT/DINPUT8.dll")"

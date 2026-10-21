#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds rg-c9-table.dll (tools/rg-c9-table: the repair of Coopers's 9's runtime call table at 0x512000, 32-bit, loaded
# by the OpenParrot loader through TP_PRELOAD_DLL) with the mingw-w64 cross compiler in ~/.cache/ravengun-mingw.
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
    -h|--help) sed -n '3,7p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
SR="${RAVENGUN_MINGW:-$HOME/.cache/ravengun-mingw}/sysroot"
CC="$SR/usr/bin/i686-w64-mingw32-gcc"
[[ -x "$CC" ]] || { echo "missing: $CC" >&2; exit 1; }
GCCV="$(ls "$SR/usr/lib/gcc/i686-w64-mingw32/" | sort -V | tail -1)"
[[ -f "$HERE/rg_c9_map.h" ]] || { echo "missing rg_c9_map.h: generate it with tools/rg-c9-table/genmap.py" >&2; exit 1; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$CC" -B"$SR/usr/lib/gcc/i686-w64-mingw32/$GCCV/" -B"$SR/usr/i686-w64-mingw32/" \
  -O2 -s -shared -Wall -Wextra -Werror -static-libgcc -Wl,--no-insert-timestamp -Wl,--kill-at \
  -I"$HERE" "$HERE/dllmain.c" "$HERE/rg_c9table.c" "$HERE/c9_hasp.c" "$HERE/c9_lpt.c" -o "$TMP/rg-c9-table.dll"
mkdir -p "$OUT"
cp "$TMP/rg-c9-table.dll" "$OUT/rg-c9-table.dll"
echo "built: $(sha256sum "$OUT/rg-c9-table.dll")"

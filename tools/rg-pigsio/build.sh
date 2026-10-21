#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds rg-pigsio.dll (tools/rg-pigsio: the synthetic USB HID I/O board of Pigs Might Fly's Shell.exe, 32-bit) with the
# mingw-w64 cross compiler in ~/.cache/ravengun-mingw, after running the native test of the board logic.
#
# Usage: tools/rg-pigsio/build.sh [--out DIR]   (default patches/PigsMightFly/dropin/)
# Writes nowhere else: not to ~/.local/share/ravengun.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/patches/PigsMightFly/dropin"
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
bash "$ROOT/tests/pigsio_test.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$CC" -B"$SR/usr/lib/gcc/i686-w64-mingw32/$GCCV/" -B"$SR/usr/i686-w64-mingw32/" \
  -O2 -s -shared -Wall -Wextra -Werror -static-libgcc -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup \
  -I"$HERE" "$HERE/rg_pigsio.c" "$HERE/rg_pigs_board.c" -o "$TMP/rg-pigsio.dll"
mkdir -p "$OUT"
cp "$TMP/rg-pigsio.dll" "$OUT/rg-pigsio.dll"
echo "built: $(sha256sum "$OUT/rg-pigsio.dll")"

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds DK2WIN32.dll (tools/rg-dk2win: the synthetic Dinkey Pro key of Pigs Might Fly's Shell.exe, 32-bit) with the
# mingw-w64 cross compiler in ~/.cache/ravengun-mingw, then checks that the seven ordinals the shell imports are there.
#
# Usage: tools/rg-dk2win/build.sh [--out DIR]   (default patches/PigsMightFly/dropin/)
# Writes nowhere else: not to ~/.local/share/ravengun.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/patches/PigsMightFly/dropin"
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
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$CC" -B"$SR/usr/lib/gcc/i686-w64-mingw32/$GCCV/" -B"$SR/usr/i686-w64-mingw32/" \
  -O2 -s -shared -Wall -Wextra -Werror -static-libgcc -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup \
  "$HERE/rg_dk2win.c" "$HERE/rg_dk2win.def" -o "$TMP/DK2WIN32.dll"
# The import table of Shell.exe names no functions, only these ordinals: a DLL without one of them does not load.
ORD="$(mktemp)"
"$OD" -p "$TMP/DK2WIN32.dll" | sed -n 's/.*+base\[ *\([0-9]*\)\].*/\1/p' | sort -n > "$ORD"
for o in 4 10 13 17 20 24 25; do
  grep -qx "$o" "$ORD" || { echo "DK2WIN32.dll has no ordinal $o (has: $(tr '\n' ' ' < "$ORD"))" >&2; exit 1; }
done
rm -f "$ORD"
mkdir -p "$OUT"
cp "$TMP/DK2WIN32.dll" "$OUT/DK2WIN32.dll"
echo "built: $(sha256sum "$OUT/DK2WIN32.dll")"

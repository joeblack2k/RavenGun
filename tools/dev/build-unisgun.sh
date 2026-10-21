#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds rg-unisgun.dll (tools/rg-unisgun/: RavenGun's own serial gun board for After Dark, loaded by rg-compat with
# RG_COMPAT_LOAD) with the i686 mingw cross compiler in ~/.cache/ravengun-mingw (the same toolchain as tools/dev/build-compat.sh).
#   tools/dev/build-unisgun.sh --out MAP [--out MAP]...      MAP is usually patches/AfterDark/dropin
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/tools/rg-unisgun"
MINGW="${RAVENGUN_MINGW:-$HOME/.cache/ravengun-mingw}"
ARCH=i686
OUTS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUTS+=("$2"); shift 2 ;;
    -h|--help) sed -n '3,5p' "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[[ ${#OUTS[@]} -gt 0 ]] || { echo "give --out MAP (e.g. patches/AfterDark/dropin)" >&2; exit 2; }
SR="$MINGW/sysroot"
CC="$SR/usr/bin/$ARCH-w64-mingw32-gcc"
[[ -x "$CC" ]] || { echo "no $ARCH-mingw in $MINGW (see $MINGW/BUILD.md)" >&2; exit 1; }
GCCV="$(ls "$SR/usr/lib/gcc/$ARCH-w64-mingw32/" | sort -V | tail -1)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
"$CC" -B"$SR/usr/lib/gcc/$ARCH-w64-mingw32/$GCCV/" -B"$SR/usr/$ARCH-w64-mingw32/" \
  -O2 -s -shared -std=c11 -Wall -Wextra -Werror -static-libgcc -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup \
  "$SRC/rg_unisgun.c" "$SRC/rg_unisgun_kern.c" -o "$TMP/rg-unisgun.dll"
for o in "${OUTS[@]}"; do
  mkdir -p "$o"
  cp "$TMP/rg-unisgun.dll" "$o/rg-unisgun.dll"
  echo "done: $o/rg-unisgun.dll"
done
echo "source: $(cd "$ROOT" && cat tools/rg-unisgun/rg_unisgun.c tools/rg-unisgun/rg_unisgun_kern.c tools/rg-unisgun/rg_unisgun_kern.h | sha256sum | cut -c1-16); dll: $(sha256sum "$TMP/rg-unisgun.dll" | cut -c1-16)"

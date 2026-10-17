#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt rg-kaboom.dll (32-bit PE, alleen kernel32 en user32) met de eigen mingw-w64-sysroot en zet hem in de drop-in-map van
# Infinity Blade FX. Zelfde opzet als patches/SilentHill/build-shaiolib.sh.
#
# Gebruik: tools/rg-kaboom/build.sh [--out DIR]     (standaard patches/InfinityBladeFX/dropin)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/patches/InfinityBladeFX/dropin"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    *) echo "onbekende optie: $1" >&2; exit 2 ;;
  esac
done
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
CC="$SR/usr/bin/i686-w64-mingw32-gcc"
[ -x "$CC" ] || { echo "ontbreekt: $CC" >&2; exit 1; }
mkdir -p "$OUT"
"$CC" -shared -O2 -Wall -Wextra -nostdlib -ffreestanding -fno-builtin \
  -o "$OUT/rg-kaboom.dll" "$HERE/rg_kaboom.c" "$HERE/kaboom_proto.c" \
  -Wl,--no-insert-timestamp -Wl,-e,_DllMain@12 -lkernel32 -luser32
sha256sum "$OUT/rg-kaboom.dll"

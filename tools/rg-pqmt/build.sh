#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt PQMTClient.dll (32-bit PE, alleen kernel32 en user32) voor Infinity Blade FX met de eigen mingw-w64-sysroot en zet hem in
# patches/InfinityBladeFX/dropin/. Zelfde opzet als tools/rg-farcry/build.sh.
#
# Gebruik: tools/rg-pqmt/build.sh [--out DIR]
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
"$CC" \
  -B"$SR/usr/lib/gcc/i686-w64-mingw32/16.2.0/" -B"$SR/usr/i686-w64-mingw32/" -B"$SR/usr/i686-w64-mingw32/lib/" \
  -isystem "$SR/usr/i686-w64-mingw32/include" -L"$SR/usr/i686-w64-mingw32/lib" \
  -shared -O2 -Wall -Wextra -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-ident \
  -mno-stack-arg-probe -nostdlib -nostartfiles \
  -o "$OUT/PQMTClient.dll" "$HERE/rg_pqmt.c" "$HERE/pqmtclient.def" \
  -Wl,-e,_DllMain@12 -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup -Wl,-s \
  -lkernel32 -luser32
sha256sum "$OUT/PQMTClient.dll"

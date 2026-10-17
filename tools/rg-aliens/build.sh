#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bouwt rg-aliens-hook.dll (PE32, i386) met de zelfstandige mingw-w64-sysroot van het project, op dezelfde manier als
# patches/SilentHill/build-shaiolib.sh (de i686-compiler direct met -B). Geen C-runtime, alleen kernel32 en user32.
#
# Gebruik: tools/rg-aliens/build.sh [UITVOERBESTAND]    (standaard: build-aliens/rg-aliens-hook.dll in de werkmap van de repo)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
CC="$SR/usr/bin/i686-w64-mingw32-gcc"
[ -x "$CC" ] || { echo "ontbreekt: $CC (RG_MINGW_SYSROOT)" >&2; exit 1; }
OUT="${1:-$ROOT/build-aliens/rg-aliens-hook.dll}"
mkdir -p "$(dirname "$OUT")"
"$CC" \
  -B"$SR/usr/lib/gcc/i686-w64-mingw32/16.2.0/" \
  -B"$SR/usr/i686-w64-mingw32/" \
  -B"$SR/usr/i686-w64-mingw32/lib/" \
  -isystem "$SR/usr/i686-w64-mingw32/include" \
  -L"$SR/usr/i686-w64-mingw32/lib" \
  -shared -O1 -Wall -Wextra -ffreestanding -fno-builtin \
  -fno-stack-protector -fno-asynchronous-unwind-tables -fno-ident \
  -mno-stack-arg-probe -mstackrealign \
  -nostdlib -nostartfiles \
  -o "$OUT" "$HERE/rg_aliens_hook.c" \
  -Wl,-e,_DllMain@12 -Wl,--no-insert-timestamp \
  -Wl,--image-base,0x10000000 -Wl,-s \
  -luser32 -lkernel32
echo "gebouwd: $(sha256sum "$OUT")"

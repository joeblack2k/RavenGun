#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt onze Usbioextreme.dll (PE32 i386) voor Far Cry: Paradise Lost met de eigen mingw-w64-keten (zoals patches/SilentHill/build-shaiolib.sh).
#   tools/rg-farcry/build.sh [uitvoerbestand]     standaard patches/FarCryParadiseLost/dropin/Usbioextreme.dll
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
CC="$SR/usr/bin/i686-w64-mingw32-gcc"
[ -x "$CC" ] || { echo "ontbreekt: $CC" >&2; exit 1; }
OUT="${1:-$HERE/../../patches/FarCryParadiseLost/dropin/Usbioextreme.dll}"
mkdir -p "$(dirname "$OUT")"
"$CC" \
  -B"$SR/usr/lib/gcc/i686-w64-mingw32/16.2.0/" -B"$SR/usr/i686-w64-mingw32/" -B"$SR/usr/i686-w64-mingw32/lib/" \
  -isystem "$SR/usr/i686-w64-mingw32/include" -L"$SR/usr/i686-w64-mingw32/lib" \
  -shared -O1 -Wall -Wextra -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-ident \
  -mno-stack-arg-probe -nostdlib -nostartfiles \
  -o "$OUT" "$HERE/rg_usbio.c" "$HERE/usbioextreme.def" \
  -Wl,-e,_DllMain@12 -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup -Wl,--image-base,0x10000000 -Wl,-s \
  -lkernel32
echo "build ok: $(sha256sum "$OUT")"

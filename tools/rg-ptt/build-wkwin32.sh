#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt onze WkWin32.dll (PE32 i386) voor Pull The Trigger met de mingw-w64-toolchain van het project (zoals
# patches/SilentHill/build-shaiolib.sh). Uitvoer: patches/PullTheTrigger/dropin/WkWin32.dll (inert tot de sectie uit docs/research/losse/PullTheTrigger.dropin.ini in dropin.ini staat)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
CC="$SR/usr/bin/i686-w64-mingw32-gcc"
[ -x "$CC" ] || { echo "ontbreekt: $CC" >&2; exit 1; }
OUT="${RG_PTT_OUT:-$HERE/../../patches/PullTheTrigger/dropin/WkWin32.dll}"
mkdir -p "$(dirname "$OUT")"
DEF="$(mktemp -d)/WkWin32.def"
# Ordinalen en namen van de echte WkWin32.dll; de stub zoekt op ordinaal (GetProcAddress met een getal).
cat > "$DEF" <<'DEF_EOF'
LIBRARY WKWIN32.dll
EXPORTS
    WkbGetLastError = rg_WkbGetLastError@0 @20
    WkbGetVersion2 = rg_WkbGetVersion2@4 @33
    WkbOpen2 = rg_WkbOpen2@24 @34
    WkbClose2 = rg_WkbClose2@4 @35
    WkbCrypt2 = rg_WkbCrypt2@24 @37
    WkbAccess2 = rg_WkbAccess2@8 @45
    WkbRelease2 = rg_WkbRelease2@4 @46
    WkbQueryEntry2 = rg_WkbQueryEntry2@16 @47
    WkbGetVersion3 = rg_WkbGetVersion3@4 @72
DEF_EOF
"$CC" \
  -B"$SR/usr/lib/gcc/i686-w64-mingw32/16.2.0/" -B"$SR/usr/i686-w64-mingw32/" -B"$SR/usr/i686-w64-mingw32/lib/" \
  -isystem "$SR/usr/i686-w64-mingw32/include" -L"$SR/usr/i686-w64-mingw32/lib" \
  -shared -O1 -Wall -Wextra -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-ident \
  -mno-stack-arg-probe -nostdlib -nostartfiles -I"$HERE" \
  -o "$OUT" "$HERE/rg_ptt_wk.c" "$DEF" \
  -Wl,-e,_DllMain@12 -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup \
  -Wl,--image-base,0x20000000 -Wl,-s -lkernel32
echo "gebouwd: $(sha256sum "$OUT")"

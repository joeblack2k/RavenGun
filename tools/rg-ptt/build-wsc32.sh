#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Bouwt onze wsc32.dll (PE32 i386) voor Pull The Trigger met de mingw-w64-toolchain van het project (zoals build-wkwin32.sh).
# Uitvoer: patches/PullTheTrigger/dropin/wsc32.dll (of RG_PTT_WSC_OUT). Zie tools/rg-ptt/rg_ptt_wsc32.c voor wat hij doet.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
CC="$SR/usr/bin/i686-w64-mingw32-gcc"
[ -x "$CC" ] || { echo "ontbreekt: $CC" >&2; exit 1; }
OUT="${RG_PTT_WSC_OUT:-$HERE/../../patches/PullTheTrigger/dropin/wsc32.dll}"
mkdir -p "$(dirname "$OUT")"
DEF="$(mktemp -d)/wsc32.def"
# Ordinalen van de echte wsc32.dll (gemeten); het getal na @ in de naam is het aantal argumentbytes van de stdcall-aanroep.
cat > "$DEF" <<'DEF_EOF'
LIBRARY WSC32.dll
EXPORTS
    SioBaud = rg_SioBaud@8 @1
    SioBrkSig = rg_SioBrkSig@8 @2
    SioCTS = rg_SioCTS@4 @3
    SioDCD = rg_SioDCD@4 @4
    SioDebug = rg_SioDebug@4 @5
    SioDone = rg_SioDone@4 @6
    SioDSR = rg_SioDSR@4 @7
    SioDTR = rg_SioDTR@8 @8
    SioFlow = rg_SioFlow@8 @9
    SioGetc = rg_SioGetc@4 @10
    SioGets = rg_SioGets@12 @11
    SioInfo = rg_SioInfo@4 @12
    SioParms = rg_SioParms@16 @13
    SioPutc = rg_SioPutc@8 @14
    SioPuts = rg_SioPuts@12 @15
    SioRead = rg_SioRead@8 @16
    SioReset = rg_SioReset@12 @17
    SioRI = rg_SioRI@4 @18
    SioRTS = rg_SioRTS@8 @19
    SioRxClear = rg_SioRxClear@4 @20
    SioRxQue = rg_SioRxQue@4 @21
    SioStatus = rg_SioStatus@8 @22
    SioTimer = rg_SioTimer@4 @23
    SioTxClear = rg_SioTxClear@4 @24
    SioTxQue = rg_SioTxQue@4 @25
    SioUnGetc = rg_SioUnGetc@8 @26
    SioWinError = rg_SioWinError@8 @27
    SioEvent = rg_SioEvent@8 @28
    SioMessage = rg_SioMessage@16 @29
DEF_EOF
"$CC" \
  -B"$SR/usr/lib/gcc/i686-w64-mingw32/16.2.0/" -B"$SR/usr/i686-w64-mingw32/" -B"$SR/usr/i686-w64-mingw32/lib/" \
  -isystem "$SR/usr/i686-w64-mingw32/include" -L"$SR/usr/i686-w64-mingw32/lib" \
  -shared -O1 -Wall -Wextra -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-ident \
  -mno-stack-arg-probe -nostdlib -nostartfiles -I"$HERE" \
  -o "$OUT" "$HERE/rg_ptt_wsc32.c" "$DEF" \
  -Wl,-e,_DllMain@12 -Wl,--no-insert-timestamp -Wl,--enable-stdcall-fixup \
  -Wl,--image-base,0x20100000 -Wl,-s -lkernel32 -luser32
echo "gebouwd: $(sha256sum "$OUT")"

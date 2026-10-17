#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Aanraking voor Infinity Blade FX zonder TeknoParrot (tools/rg-pqmt): de logica native getest (tests/pqmt_unit.c), en de DLL moet
# bouwen met dezelfde 16 exports (namen en ordinalen) als de echte PQMTClient.dll als de mingw-sysroot er is. Geen spel, geen Wine.
# Skip (77) zonder C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -g -Wall -Wextra -I"$ROOT/tools/rg-pqmt" -o "$T/pqmt_unit" "$ROOT/tests/pqmt_unit.c" 2>"$T/cc.err" \
  || { cat "$T/cc.err"; echo "FAIL: de eenheidstest bouwt niet"; exit 1; }
[[ ! -s "$T/cc.err" ]] || { cat "$T/cc.err"; echo "FAIL: waarschuwingen"; exit 1; }
"$T/pqmt_unit" || exit 1
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
if [[ -x "$SR/usr/bin/i686-w64-mingw32-gcc" ]]; then
  bash "$ROOT/tools/rg-pqmt/build.sh" --out "$T" >/dev/null 2>"$T/mingw.err" || { cat "$T/mingw.err"; echo "FAIL: PQMTClient.dll bouwt niet"; exit 1; }
  imports=$(objdump -p "$T/PQMTClient.dll" | sed -n 's/.*DLL Name: //p' | tr 'A-Z' 'a-z' | sort | tr '\n' ' ')
  [[ "$imports" == "kernel32.dll user32.dll " ]] || { echo "FAIL: onverwachte imports: $imports"; exit 1; }
  # de ordinalen van de echte client (gemeten in Binaries/Win32/PQMTClient.dll van de game)
  verwacht="1 2 3 4 ConnectServer DisconnectServer GetGestureName GetServerResolution SendRequest SendThreshold SetOnGetDeviceInfo SetOnReceiveError SetOnReceiveGesture SetOnReceivePointFrame SetOnServerBreak SetRawDataResolution"
  kreeg=$(objdump -p "$T/PQMTClient.dll" | sed -n '/Ordinal\/Name Pointer/,/^$/p' | sed -n 's/.*+base\[ *\([0-9]*\)\] *[0-9a-f]* \(.*\)$/\1 \2/p' | sort -n | awk '{ if ($2 ~ /^\?\?4/) printf "%s ", $1; else printf "%s ", $2 }')
  [[ "${kreeg% }" == "$verwacht" ]] || { echo "FAIL: exports wijken af:"; echo " verwacht: $verwacht"; echo " kreeg:    $kreeg"; exit 1; }
else
  echo "mingw-sysroot ontbreekt: DLL-bouw overgeslagen"
fi
echo "pqmt_test: ok"

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# rg-dinput8 (tools/rg-dinput8/): de eigen dinput8.dll met de geweren als DirectInput-joysticks (Bug Busters zonder TeknoParrot).
# Bouwt de rekenkern met de eenheidstest (tests/dinput8_unit.c) op Linux en draait hem: tabel lezen en afkeuren, bordbeeld naar
# X/Y/knoppen, flanken voor vlaggen en tellers, de GUID's in control.cfg, en elke meegeleverde rg-dinput8.ini
# (docs/research/wartran/patches/*/dropin/ en patches/*/dropin/). Staat BBPC.exe op de NAS (alleen gelezen), dan ook: elke patch
# van de tabel van Bug Busters klopt met de bytes van die exe. Als de mingw-sysroot er is ook de DLL zelf: bouwt, en exporteert
# DirectInput8Create zonder decoratie. Geen spel, geen Wine. Skip (77) zonder C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -g -std=c11 -Wall -Wextra -I"$ROOT/tools/rg-dinput8" -o "$T/dinput8_unit" \
      "$ROOT/tests/dinput8_unit.c" "$ROOT/tools/rg-dinput8/rg_dinput8_kern.c" 2>"$T/cc.err" \
  || { cat "$T/cc.err"; echo "FAIL: de eenheidstest bouwt niet"; exit 1; }
[[ ! -s "$T/cc.err" ]] || { cat "$T/cc.err"; echo "FAIL: waarschuwingen bij het bouwen van de test"; exit 1; }
tabellen=()
for f in "$ROOT"/docs/research/wartran/patches/*/dropin/rg-dinput8.ini "$ROOT"/patches/*/dropin/rg-dinput8.ini; do
  [[ -f $f ]] && tabellen+=("$f")
done
BB="$ROOT/docs/research/wartran/patches/BugBusters/dropin"
EXE="${RG_BUGBUSTERS_EXE:-$HOME/mnt/spidernas_emulation/Teknoparrot/Bug Busters/BBPC.exe}"
"$T/dinput8_unit" --cfg "$BB/control.cfg" --exe "$EXE" "$BB/rg-dinput8.ini" "${tabellen[@]}" || exit 1
echo "meegeleverde tabellen: ${#tabellen[@]}"
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
if [[ -x "$SR/usr/bin/i686-w64-mingw32-gcc" ]]; then
  bash "$ROOT/tools/rg-dinput8/build.sh" --out "$T/dll" >/dev/null 2>"$T/mingw.err" || { cat "$T/mingw.err"; echo "FAIL: dinput8.dll bouwt niet"; exit 1; }
  objdump -f "$T/dll/dinput8.dll" | grep -q "pei-i386" || { echo "FAIL: dinput8.dll is geen 32-bits PE"; exit 1; }
  exports=$(objdump -p "$T/dll/dinput8.dll" | sed -n '/Ordinal\/Name Pointer/,/^$/p' | sed -n 's/.*+base\[ *[0-9]*\] *[0-9a-f]* \(.*\)$/\1/p' | sort | tr '\n' ' ')
  [[ "$exports" == "DirectInput8Create DllCanUnloadNow DllGetClassObject DllRegisterServer DllUnregisterServer " ]] \
    || { echo "FAIL: exports: $exports"; exit 1; }
else
  echo "mingw-sysroot ontbreekt: DLL-bouw overgeslagen"
fi
echo "dinput8_test: ok"

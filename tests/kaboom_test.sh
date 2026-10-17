#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Het protocol van het nagebootste Kaboom-bord van Infinity Blade FX (tools/rg-kaboom/kaboom_proto.c): bouwt de eenheidstest
# (tests/kaboom_unit.c) met de gewone C-compiler van de Deck en draait hem. Er is geen spel en geen Wine nodig. Daarnaast moet de
# Windows-DLL zelf bouwen als de mingw-sysroot er is (anders overgeslagen, niet gefaald).
# Skip (77) zonder C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -g -Wall -Wextra -I"$ROOT/tools/rg-kaboom" -o "$T/kaboom_unit" "$ROOT/tests/kaboom_unit.c" "$ROOT/tools/rg-kaboom/kaboom_proto.c" 2>"$T/cc.err" \
  || { cat "$T/cc.err"; echo "FAIL: de eenheidstest bouwt niet"; exit 1; }
[[ ! -s "$T/cc.err" ]] || { cat "$T/cc.err"; echo "FAIL: waarschuwingen bij het bouwen van de test"; exit 1; }
"$T/kaboom_unit" || exit 1
SR="${RG_MINGW_SYSROOT:-$HOME/.cache/ravengun-mingw/sysroot}"
if [[ -x "$SR/usr/bin/i686-w64-mingw32-gcc" ]]; then
  bash "$ROOT/tools/rg-kaboom/build.sh" --out "$T" >/dev/null 2>"$T/mingw.err" || { cat "$T/mingw.err"; echo "FAIL: rg-kaboom.dll bouwt niet"; exit 1; }
  [[ -s "$T/rg-kaboom.dll" ]] || { echo "FAIL: geen rg-kaboom.dll"; exit 1; }
  # alleen kernel32 en user32: geen C-runtime die de prefix zou moeten leveren
  imports=$(objdump -p "$T/rg-kaboom.dll" | sed -n 's/.*DLL Name: //p' | tr 'A-Z' 'a-z' | sort | tr '\n' ' ')
  [[ "$imports" == "kernel32.dll user32.dll " ]] || { echo "FAIL: onverwachte imports: $imports"; exit 1; }
else
  echo "mingw-sysroot ontbreekt: DLL-bouw overgeslagen"
fi
echo "kaboom_test: ok"

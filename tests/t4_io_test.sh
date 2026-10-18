#!/usr/bin/env bash
# rg-t4-io (tools/rg-t4-io/rg_t4_io.c): de JAMMA-bordkant en het richtpunt van Terminator Salvation. Bouwt de bibliotheek 32-bits (zoals
# het spel; -DRG_T4_TEST legt een eigen geheugen onder de game-adressen) en draait tests/t4_io_test.py: frames op een proef-pty uit een
# proef-bordbeeld, en de richtpunt-floats. Geen spel nodig. Skip (77) zonder gcc -m32 of python3.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"
command -v "$CC" >/dev/null && command -v python3 >/dev/null || { echo "overgeslagen: geen C-compiler of python3"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/inc32/gnu"
[ -f /usr/include/gnu/stubs-32.h ] || : >"$T/inc32/gnu/stubs-32.h"
if ! { "$CC" -m32 -I"$T/inc32" -shared -fPIC -O2 -Wall -Wextra -DRG_T4_TEST -o "$T/librg-t4-io.so" "$ROOT/tools/rg-t4-io/rg_t4_io.c" -lpthread -ldl 2>"$T/err" \
  && "$CC" -m32 -I"$T/inc32" -O1 -Wall -o "$T/t4_io_unit" "$ROOT/tests/t4_io_unit.c" -ldl 2>>"$T/err"; }; then
  cat "$T/err"; echo "overgeslagen: geen gcc -m32"; exit 77
fi
[[ ! -s "$T/err" ]] || { cat "$T/err"; echo "FAIL: waarschuwingen bij het bouwen"; exit 1; }
python3 -I "$ROOT/tests/t4_io_test.py" "$T/librg-t4-io.so" "$T/t4_io_unit" "$T" || { echo "FAIL"; exit 1; }
echo "OK"

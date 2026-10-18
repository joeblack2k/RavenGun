#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Pull The Trigger zonder TeknoParrot (tools/rg-ptt): (1) de eenheidstest van de WkWin32-logica native, (2) de test van de
# importtabel-opbouw (mini-beeld, en met PTW.exe als dat er is). Geen spel, geen Wine. Skip (77) zonder C-compiler of python3.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
command -v python3 >/dev/null || { echo "overgeslagen: geen python3"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -Wall -Wextra -I"$ROOT/tools/rg-ptt" -o "$T/ptt_wk_unit" "$ROOT/tests/ptt_wk_unit.c" 2>"$T/err" || { cat "$T/err"; echo "FAIL: bouwt niet"; exit 1; }
"$T/ptt_wk_unit" || exit 1
# Het geweerbord en de kaartlezer van wsc32.dll (tools/rg-ptt/ptt_board.h): pakketten, commando's, kalibratie.
"$CC" -O1 -Wall -Wextra -I"$ROOT/tools/rg-ptt" -o "$T/ptt_board_unit" "$ROOT/tests/ptt_board_unit.c" -lm 2>"$T/err" || { cat "$T/err"; echo "FAIL: bouwt niet (ptt_board_unit)"; exit 1; }
"$T/ptt_board_unit" || exit 1
python3 -I "$ROOT/tests/ptt_imports_test.py" || exit 1

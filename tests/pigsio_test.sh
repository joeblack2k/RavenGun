#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Pigs Might Fly without TeknoParrot (tools/rg-pigsio): the synthetic HID I/O board logic of Shell.exe, native.
# No game, no Wine. Skip (77) without a C compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "skipped: no C compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -Wall -Wextra -Werror -I"$ROOT/tools/rg-pigsio" -o "$T/pigs" "$ROOT/tests/pigsio_board_test.c" \
  "$ROOT/tools/rg-pigsio/rg_pigs_board.c" 2>"$T/err" || { cat "$T/err"; echo "FAIL: does not build"; exit 1; }
"$T/pigs" || exit 1
echo "pigsio_test: ok"

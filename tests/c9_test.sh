#!/usr/bin/env bash
# rg-c9-table (tools/rg-c9-table/): Coopers's 9 without TeknoParrot. Builds the pure C parts with the unit test (tests/c9_unit.c)
# on Linux and runs it: the HASP HL answers (memory image, decrypt pairs, status codes) and the parallel-port gun/IO board
# (command protocol, replies from the board image, the shot handshake on status bit 0x80). The shipped drop-in
# (patches/Coopers9/dropin/dropin.ini) must parse and give the answers the game's code checks. The DLL itself is built by
# tools/rg-c9-table/build.sh (mingw). Skip (77) without a C compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "skipped: no C compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -g -std=c11 -Wall -Wextra -I"$ROOT/tools/rg-c9-table" -o "$T/c9_unit" \
      "$ROOT/tests/c9_unit.c" "$ROOT/tools/rg-c9-table/c9_hasp.c" "$ROOT/tools/rg-c9-table/c9_lpt.c" 2>"$T/cc.err" \
  || { cat "$T/cc.err"; echo "FAIL: the unit test does not build"; exit 1; }
[[ ! -s "$T/cc.err" ]] || { cat "$T/cc.err"; echo "FAIL: warnings while building the test"; exit 1; }
"$T/c9_unit" "$ROOT/patches/Coopers9/dropin/dropin.ini"

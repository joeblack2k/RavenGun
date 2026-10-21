#!/usr/bin/env bash
# rg-unisgun (tools/rg-unisgun/): RavenGun's own serial gun board for After Dark. Builds the core with the unit test
# (tests/unisgun_unit.c) on Linux and runs it: the table, the 26-byte report as the game checks it, y inversion, off-screen and the
# scanner for what the game writes. The shipped tables (patches/*/dropin/rg-unisgun.ini) must be valid. With the i686 mingw in
# ~/.cache/ravengun-mingw it also builds rg-unisgun.dll and rg-compat.dll (-Werror) and checks that rg-compat knows RG_COMPAT_LOAD,
# the channel that loads rg-unisgun.dll. Skip (77) without a C compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "skipped: no C compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
"$CC" -O1 -g -std=c11 -Wall -Wextra -I"$ROOT/tools/rg-unisgun" -o "$T/unisgun_unit" \
      "$ROOT/tests/unisgun_unit.c" "$ROOT/tools/rg-unisgun/rg_unisgun_kern.c" 2>"$T/cc.err" \
  || { cat "$T/cc.err"; echo "FAIL: the unit test does not build"; exit 1; }
[[ ! -s "$T/cc.err" ]] || { cat "$T/cc.err"; echo "FAIL: warnings while building the test"; exit 1; }
tables=()
for f in "$ROOT"/patches/*/dropin/rg-unisgun.ini; do [[ -f $f ]] && tables+=("$f"); done
"$T/unisgun_unit" "${tables[@]}" || exit 1
echo "shipped tables: ${#tables[@]}"
MINGW="${RAVENGUN_MINGW:-$HOME/.cache/ravengun-mingw}"
if [[ -x "$MINGW/sysroot/usr/bin/i686-w64-mingw32-gcc" ]]; then
  "$ROOT/tools/dev/build-unisgun.sh" --out "$T/dll" >/dev/null || { echo "FAIL: rg-unisgun.dll does not build"; exit 1; }
  "$ROOT/tools/dev/build-compat.sh" --out "$T/dll" >/dev/null || { echo "FAIL: rg-compat.dll does not build"; exit 1; }
  grep -q RG_COMPAT_LOAD "$T/dll/rg-compat.dll" || { echo "FAIL: rg-compat.dll has no RG_COMPAT_LOAD"; exit 1; }
  echo "DLLs built (i686): rg-unisgun.dll, rg-compat.dll"
else
  echo "DLL build skipped: no i686 mingw in $MINGW"
fi

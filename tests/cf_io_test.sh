#!/usr/bin/env bash
# rg-cf-io (tools/rg-cf-io/rg_cf_io.c): de kast van Crossfire: Maximum Paintball (iButton op /dev/p37c, /dev/mjg-records uit het bordbeeld, /dev/hda).
# Bouwt de bibliotheek 32-bits (zoals het spel; -DRG_CF_TEST legt een eigen geheugen onder de game-adressen) en draait tests/cf_io_unit.c met de
# bibliotheek in LD_PRELOAD. Geen spel nodig. Skip (77) zonder gcc -m32.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"
command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/inc32/gnu"
[ -f /usr/include/gnu/stubs-32.h ] || : >"$T/inc32/gnu/stubs-32.h"
if ! { "$CC" -m32 -shared -fPIC -O2 -Wall -Wextra -DRG_CF_TEST -o "$T/librg-cf-io.so" "$ROOT/tools/rg-cf-io/rg_cf_io.c" -ldl 2>"$T/err" \
  && "$CC" -m32 -I"$T/inc32" -O1 -Wall -o "$T/cf_io_unit" "$ROOT/tests/cf_io_unit.c" -lm 2>>"$T/err"; }; then
  cat "$T/err"; echo "overgeslagen: geen gcc -m32"; exit 77
fi
[[ ! -s "$T/err" ]] || { cat "$T/err"; echo "FAIL: waarschuwingen bij het bouwen"; exit 1; }
head -c 64 /dev/zero >"$T/shm"
printf 'EM-HYNJ5A194118\0\0\0\0\0\0' >"$T/adjust.dat"
cd "$T" || exit 1
timeout 30 env LD_PRELOAD="$T/librg-cf-io.so" "$T/cf_io_unit" "$T/shm" adjust.dat 2>"$T/log" || { cat "$T/log"; echo "FAIL"; exit 1; }
echo "OK"

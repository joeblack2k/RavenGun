#!/bin/bash
# Prove the name route still works, and that the laadadres-arithmetic fix matters for ET_EXEC.
set -u
R=~/projects/ravengun
DATA=~/.local/share/ravengun
TMP=$(mktemp -d)
INC=()
[ -e /usr/include/gnu/stubs-32.h ] || INC=(-I"$R/hasp/src/inc")

echo "############ 0. de 32-bits hook bouwen (die ontbrak in de vorige poging)"
gcc -m32 "${INC[@]}" -shared -fPIC -O2 -Wall -Wextra -Wno-format-truncation \
    -o "$TMP/libravengun-hasphook.so" "$R/tools/rt-hasphook/hasp_inline_hook.c" -ldl -lpthread \
  || { echo "bouwen mislukt"; exit 1; }
ls -l "$TMP/libravengun-hasphook.so"

for mode in pie nopie; do
  echo
  echo "############ $mode"
  FLAGS=(-m32 "${INC[@]}" -O1 -rdynamic)
  [ "$mode" = nopie ] && FLAGS+=(-no-pie)
  gcc "${FLAGS[@]}" -o "$TMP/selftest-$mode" "$R/tools/rt-hasphook/hasp_zelftest.c" || continue
  echo "  $(file -b "$TMP/selftest-$mode" | cut -c1-64)"
  echo "  --- zonder hook:"
  ( cd "$TMP" && HASP_ZELFTEST_WACHT=0 "./selftest-$mode" 2>&1 | tail -2 | sed 's/^/      /' )
  echo "  --- met hook, naam-route, emulator met de Halo-dongle:"
  ( cd "$TMP" && LD_PRELOAD="$TMP/libravengun-hasphook.so" RAVENGUN_HASP_VERBOSE=1 \
      RAVENGUN_HASP_GAME="Halo Fireteam" RAVENGUN_HASP_DIR="$DATA/hasp" \
      RAVENGUN_HASP_ELF_LOG="$TMP/$mode.log" RAVENGUN_HASP_ELF_WACHT=10 \
      "./selftest-$mode" 2>&1 | grep -E 'hasp_login ->|hasp_read|echte_aanroepen|RESULTAAT' \
      | tail -6 | sed 's/^/      /' )
  echo "  --- wat de hook zelf vond en berekende:"
  grep -E 'dynsym:|hasp-symbolen|spelbeeld|aangehaakt:|totaal' "$TMP/$mode.log" 2>/dev/null \
    | sed 's/^/      | /'
done
echo "== done ($TMP)"

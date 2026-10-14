#!/usr/bin/env bash
# rg-ptylink (tools/rg-ptylink/rg_ptylink.c): twee paden als één rauwe seriële verbinding (Target: Terror Gold: /dev/ttyS0 van
# de game en /tmp/ttyJamma van de bordemulator tt_emu.so, op de kast gekoppeld met socat). Bouwt de bibliotheek (gewoon en,
# als dat kan, 32-bits zoals het spel) en draait tests/ptylink_unit.c met de bibliotheek in LD_PRELOAD, met paden die niet
# bestaan (zoals in de sandbox). Tegenproef: zonder de bibliotheek faalt de open. Geen spel nodig. Skip (77) zonder C-compiler.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CC="${CC:-cc}"; command -v "$CC" >/dev/null || { echo "overgeslagen: geen C-compiler"; exit 77; }
T=$(mktemp -d) || exit 1
trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*"; exit 1; }
BRON="$ROOT/tools/rg-ptylink/rg_ptylink.c"
"$CC" -shared -fPIC -O2 -Wall -Wextra -o "$T/librg_ptylink.so" "$BRON" -ldl -lpthread 2>"$T/so.err" || { cat "$T/so.err"; fail "bouwt niet"; }
[[ ! -s "$T/so.err" ]] || { cat "$T/so.err"; fail "waarschuwingen bij het bouwen"; }
"$CC" -O1 -Wall -o "$T/ptylink_unit" "$ROOT/tests/ptylink_unit.c" || fail "de eenheidstest bouwt niet"
A=/rg-test-bestaat-niet/ttyS0; B="$T/geen/ttyJamma"; C=/rg-test-bestaat-niet/ttyS1; D=/rg-test-bestaat-niet/ttyJamma2
echo gewoon >"$T/gewoon"

# tegenproef: zonder de bibliotheek bestaan de paden niet
"$T/ptylink_unit" "$A" "$B" "$C" "$D" "$T/gewoon" 2>/dev/null && fail "tegenproef: zonder de bibliotheek ging de open toch"

timeout 20 env LD_PRELOAD="$T/librg_ptylink.so" RG_PTYLINK="$A=$B;$C=$D" RG_PTYLINK_LOG=1 \
  "$T/ptylink_unit" "$A" "$B" "$C" "$D" "$T/gewoon" 2>"$T/log" || { cat "$T/log"; fail "eenheidstest"; }
grep -q "koppel $A <-> $B gemaakt" "$T/log" || { cat "$T/log"; fail "geen meldregel van het koppel"; }

# 32-bits (zoals Target: Terror Gold); alleen bouwen en de eenheidstest, als gcc -m32 er is
mkdir -p "$T/inc32/gnu"; [ -f /usr/include/gnu/stubs-32.h ] || : >"$T/inc32/gnu/stubs-32.h"
if "$CC" -m32 -I"$T/inc32" -shared -fPIC -O2 -Wall -o "$T/librg_ptylink32.so" "$BRON" -ldl -lpthread 2>/dev/null \
   && "$CC" -m32 -I"$T/inc32" -O1 -o "$T/ptylink_unit32" "$ROOT/tests/ptylink_unit.c" 2>/dev/null; then
  timeout 20 env LD_PRELOAD="$T/librg_ptylink32.so" RG_PTYLINK="$A=$B;$C=$D" "$T/ptylink_unit32" "$A" "$B" "$C" "$D" "$T/gewoon" \
    || fail "eenheidstest 32-bits"
else
  echo "32-bits overgeslagen (geen gcc -m32)"
fi
echo "OK"

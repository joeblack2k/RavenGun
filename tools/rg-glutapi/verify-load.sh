#!/usr/bin/env bash
# verify-load.sh -- bewijst dat librg-glutapi.so laadt en dat de doorverwijzingen
# aankomen. Drie proeven, alle drie in kleine 32-bits processen (geen spel, geen Wine):
#
#   1. dlopen + dlsym van alle 161 namen, met de echte libglut als doel
#   2. LD_PRELOAD in een 32-bits proces (het bestand zit echt in het adresbeeld)
#   3. doorverwijzing naar een neplibglut: waarneembaar neveneffect + argumenten
set -uo pipefail

HIER="$(cd "$(dirname "$0")" && pwd)"
INC="$HIER/inc"
REAL="${RG_LIB32:-$HOME/.local/share/ravengun/runtime/lib32}"
SO="$HIER/librg-glutapi.so"
P="$HIER/proef"
UIT="$P/out"
CC="${CC:-gcc}"

mkdir -p "$UIT" "$INC/gnu"
if [[ ! -f /usr/include/gnu/stubs-32.h ]]; then
  cp -f /usr/include/gnu/stubs-64.h "$INC/gnu/stubs-32.h" 2>/dev/null || : > "$INC/gnu/stubs-32.h"
fi
[[ -f "$SO" ]] || { echo "FOUT: $SO bestaat niet; bouw eerst met build.sh"; exit 1; }

echo "=== proefprogramma's bouwen (i386) ==="
"$CC" -m32 -I"$INC" -O2 -Wall -Wextra -o "$UIT/proef_dlopen"  "$P/proef_dlopen.c"  -ldl || exit 1
"$CC" -m32 -I"$INC" -O2 -Wall -Wextra -o "$UIT/proef_preload" "$P/proef_preload.c"     || exit 1
"$CC" -m32 -I"$INC" -O2 -Wall -Wextra -o "$UIT/proef_slots"   "$P/proef_slots.c" -ldl || exit 1
"$CC" -m32 -I"$INC" -shared -fPIC -O2 -Wall -Wextra -o "$UIT/libglut-nepperd.so" "$P/neplibglut.c" || exit 1
file "$UIT/proef_dlopen" "$UIT/libglut-nepperd.so"

# De echte libglut heeft libXi.so.6 nodig; die staat in dezelfde runtime-map.
export LD_LIBRARY_PATH="$REAL${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

uitslag=0
echo
echo "=== proef 1: dlopen + dlsym van alle 161 namen (echte libglut) ==="
RG_GLUTAPI_LOG=1 "$UIT/proef_dlopen" "$SO" "$HIER/names_budgie.txt" || uitslag=1

echo
echo "=== proef 2: LD_PRELOAD in een 32-bits proces ==="
LD_PRELOAD="$SO" RG_GLUTAPI_LOG=1 "$UIT/proef_preload" || uitslag=1

echo
echo "=== proef 3: doorverwijzing naar de neplibglut (waarneembaar neveneffect) ==="
MERK="$UIT/merk.txt"
rm -f "$MERK"
RG_GLUT_REAL="$UIT/libglut-nepperd.so" RG_NEP_MERK="$MERK" RG_GLUTAPI_LOG=1 \
  "$UIT/proef_dlopen" "$SO" "$HIER/names_budgie.txt" nep "$MERK" || uitslag=1

echo
echo "=== proef 4: waar wijzen de 122 aanroepwijzers naartoe? ==="
RG_GLUTAPI_LOG=1 "$UIT/proef_slots" "$SO" "$HIER/names_budgie.txt" || uitslag=1

echo
if [[ "$uitslag" == "0" ]]; then
  echo "RESULTAAT: PASS -- alle vier de proeven geslaagd"
else
  echo "RESULTAAT: FAIL"
fi
exit "$uitslag"

#!/usr/bin/env bash
# Bouwt rg_cf_io.c (i386, header-vrij: geen 32-bits glibc-ontwikkelheaders nodig) en draait de eigen toets ertegen.
# Gebruik: bash docs/research/budgie/src/build.sh [uitvoermap]   (standaard een tijdelijke map; er wordt geen spel gestart)
set -euo pipefail
HIER="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$(mktemp -d)}"
mkdir -p "$OUT"
gcc -m32 -shared -fPIC -O2 -Wall -Wextra -o "$OUT/librg-cf-io.so" "$HIER/rg_cf_io.c"
gcc -m32 -O1 -Wall -fno-builtin -o "$OUT/test_cf_io" "$HIER/test_cf_io.c"
printf 'EM-HYNJ5A194118\0\0\0\0\0\0' > "$OUT/adjust.dat"     # proefserienummer in dezelfde vorm als het echte adjust.dat
( cd "$OUT" && LD_PRELOAD="$OUT/librg-cf-io.so" RG_CF_LOG=1 ./test_cf_io )

# Terminator: de bordkant van de seriele lijn (rg_t4_jamma.c), getoetst met een proef-pty. De header-fix: op SteamOS ontbreekt
# gnu/stubs-32.h; een lege kopie in een tijdelijke map is genoeg (zoals CMakeLists.txt voor librg_ptylink.so doet).
mkdir -p "$OUT/inc32/gnu"
[ -f /usr/include/gnu/stubs-32.h ] || : > "$OUT/inc32/gnu/stubs-32.h"
gcc -m32 -shared -fPIC -O2 -Wall -Wextra -I "$OUT/inc32" -o "$OUT/librg-t4-jamma.so" "$HIER/rg_t4_jamma.c" -lpthread -ldl
printf '#include <unistd.h>\nint main(void){sleep(4);return 0;}\n' > "$OUT/slaap.c"
gcc -m32 -I "$OUT/inc32" -o "$OUT/slaap32" "$OUT/slaap.c"
python3 -I "$HIER/test_t4_jamma.py" "$OUT/librg-t4-jamma.so" "$OUT/slaap32"

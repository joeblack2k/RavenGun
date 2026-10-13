#!/bin/bash
# Read-only: tool availability + the Halo dongle drop-in contents.
set -u
echo "############ tools on the Deck"
for t in readelf objdump nm python3 gcc file strings strace gdb xxd; do
  printf '  %-9s %s\n' "$t" "$(command -v "$t" 2>/dev/null || echo MISSING)"
done
echo "--- python3 modules"
python3 - <<'PY'
import importlib.util
for m in ("elftools", "capstone", "smbclient", "pefile"):
    print("  %-10s %s" % (m, bool(importlib.util.find_spec(m))))
PY

echo
echo "############ Halo drop-in"
D=~/projects/ravengun-patches/patches/Halo/dropin
echo "--- dropin.ini"
cat "$D/dropin.ini"
echo
echo "--- dongle.json"
cat "$D/ravengun-hasp/Halo/dongle.json"
echo
echo "--- memory files (name, size, first 16 bytes hex, sha256)"
for f in "$D"/ravengun-hasp/Halo/memory/*; do
  [ -f "$f" ] || continue
  printf '  %-40s %5s  %s  %s\n' "$(basename "$f")" "$(stat -c%s "$f")" \
    "$(xxd -p -l 16 "$f")" "$(sha256sum "$f" | cut -c1-16)"
done

echo
echo "############ the same files as they sit in the game folder (NAS, read-only)"
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo"
for f in "$G"/fff4 "$G"/00000000000000001111111111110100; do
  printf '  %-40s %5s  %s  %s\n' "$(basename "$f")" "$(stat -c%s "$f")" \
    "$(xxd -p -l 16 "$f")" "$(sha256sum "$f" | cut -c1-16)"
done
echo "--- do they differ from the drop-in copies?"
for n in fff4 00000000000000001111111111110100; do
  if cmp -s "$G/$n" "$D/ravengun-hasp/Halo/memory/$n"; then
    echo "  $n: IDENTICAL"
  else
    echo "  $n: DIFFERENT"
    diff <(xxd "$G/$n") <(xxd "$D/ravengun-hasp/Halo/memory/$n") | head -20
  fi
done

echo
echo "############ the repository's own dongle layer"
R=~/projects/ravengun
ls -l "$R/dongles" 2>/dev/null | head
echo "--- hasp source tree"
find "$R/hasp" -maxdepth 3 -type f 2>/dev/null | head -20
echo "--- ravengun-hasp source (dropin)"
find "$R" -maxdepth 4 -name 'ravengun-hasp*' 2>/dev/null | head
echo "--- does any Halo dongle memory exist elsewhere in the repo?"
find "$R" -maxdepth 5 -name 'fff4' -o -maxdepth 5 -name '00000000000000001111111111110100' 2>/dev/null | head

echo
echo "############ halo.so: what it needs and what it exports (Deck-side)"
SO="$G/halo.so"
file "$SO"
readelf -d "$SO" 2>/dev/null | head -20
echo "--- exported hasp/flh symbols:"
nm -D --defined-only "$SO" 2>/dev/null | grep -E ' hasp_| flh_| plthook_| GOT_FIX' | head -40
echo "--- undefined symbols:"
nm -D --undefined-only "$SO" 2>/dev/null | head -60

echo
echo "############ does the game mention a device description / serial for the FTDI board?"
strings -a -t x "$G/game" | grep -iE 'ftdi|ft232|rio2|irgun|guninfo|prom_|/dev/tty|hardlock' | head -60

echo "== done"

#!/bin/bash
# Wat staat er op librg_rt_input.so+0x1f44, en hoe schrijft de hulp in de schakelaartabel?
set -u
R=~/projects/ravengun
S="$R/tools/rt-input-shim/rg_rt_input.c"
SO="$R/build/librg_rt_input.so"
[ -f "$SO" ] || SO=$(find "$R" -name 'librg_rt_input.so' 2>/dev/null | head -1)

echo "############ 1. de bron: de tabel, de invoer en de puls"
echo "  bron: $S  ($(stat -c%s "$S" 2>/dev/null) bytes)"
grep -nE 'define|entry\(|TABLE_OFF|BOARD|base|MAP\[|pulse|until|active|static .*\(\*|lookup|maps|/proc|0x18|0x2200|0x50' "$S" | head -40 | sed 's/^/  /'

echo
echo "############ 2. de hele functie waarin de puls wordt gezet (regels 60-115)"
sed -n '55,120p' "$S" | cat -n | sed 's/^/  /'

echo
echo "############ 3. wat staat er op +0x1f44 in de gebouwde bibliotheek?"
echo "  bibliotheek: ${SO:-GEEN}"
if [ -n "${SO:-}" ] && [ -f "$SO" ]; then
  ls -l "$SO" | sed 's/^/  /'
  sha256sum "$SO" | cut -c1-16 | sed 's/^/  sha256 /'
  echo "  --- de dichtstbijzijnde symbolen:"
  nm -C "$SO" 2>/dev/null | sort | awk '{print $1, $3}' | grep -iE '^[0-9a-f]+ ' | head -20 | sed 's/^/    /'
  echo "  --- de instructies rond 0x1f44 (0x1f00 - 0x1f80):"
  objdump -d --start-address=0x1f00 --stop-address=0x1f80 "$SO" 2>/dev/null | sed 's/^/    /'
  echo "  --- welke functie bevat 0x1f44?"
  objdump -d "$SO" 2>/dev/null | awk '/^[0-9a-f]+ </{f=$0} /1f44:/{print "    in:", f; print "    regel:", $0}' | head -4
fi
echo "== done"

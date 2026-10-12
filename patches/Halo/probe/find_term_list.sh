#!/bin/bash
# Read-only: find the Terminator address list the other agent made, and show it.
set -u
PR=~/projects/ravengun-patches
echo "############ 1. adreslijsten in de patches-repo"
find "$PR/patches" -maxdepth 3 \( -name 'hasp-haken*' -o -name '*adres*' -o -name '*haken*' \) 2>/dev/null | sed 's/^/  /'
echo "--- alles in patches/Terminator dat op een adreslijst lijkt:"
ls -l "$PR/patches/Terminator/" | sed 's/^/  /'
echo "--- en de inhoud van de Terminator-lijst (als hij bestaat):"
for f in "$PR/patches/Terminator/hasp-haken.ini" "$PR/patches/Terminator/hasp-haken.txt"; do
  [ -f "$f" ] && { echo "=== $f"; cat "$f" | sed 's/^/  /'; }
done

echo
echo "############ 2. zoekt de Terminator-uitzoeker naar dezelfde variabele?"
grep -rn 'RAVENGUN_HASP_ELF_ADRESLIJST\|hasp-haken' "$PR/patches/Terminator/" 2>/dev/null | head -12 | sed 's/^/  /'

echo
echo "############ 3. waar staat de oude samenvattingsregel in mijn eigen documenten?"
grep -rn 'niet in de dynsym en niet in de adreslijst\|niet aangehaakt:' "$PR/patches/Halo/"*.md 2>/dev/null | sed 's/^/  /'
echo "== done"

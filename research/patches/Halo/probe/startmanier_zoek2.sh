#!/bin/bash
# Tweede pas: het TeknoParrot-profiel van Halo, de map eronder, en de findings-documentatie.
set -u
MNT=/home/deck/mnt/spidernas_emulation
TP="$MNT/Teknoparrot"

echo "############ 1. het TeknoParrot-profiel van Halo"
ls -1 "$MNT/emulators/Teknoparrot/GameProfiles/" 2>/dev/null | grep -i halo | sed 's/^/  /'
PROF=$(ls -1 "$MNT/emulators/Teknoparrot/GameProfiles/" 2>/dev/null | grep -i halo | head -1)
if [ -n "${PROF:-}" ]; then
  F="$MNT/emulators/Teknoparrot/GameProfiles/$PROF"
  echo "  --- $F ($(stat -c%s "$F") bytes):"
  cat "$F" | sed 's/^/    /'
fi

echo
echo "############ 2. de spellmap dieper: Halo Fireteam/g7/..."
find "$TP/Halo Fireteam" -maxdepth 3 2>/dev/null | head -25 | sed 's/^/  /'
echo "  --- en alles met bat/sh/cmd/ini/cfg/xml daaronder:"
find "$TP/Halo Fireteam" -maxdepth 4 \( -iname '*.bat' -o -iname '*.sh' -o -iname '*.cmd' -o -iname '*.ini' -o -iname '*.cfg' -o -iname '*.xml' -o -iname '*.txt' -o -iname '*.md' \) 2>/dev/null | head -20 | sed 's/^/    /'

echo
echo "############ 3. de findings-documentatie van TeknoParrot over Halo"
for f in "$TP/findings/Halo.md" "$TP/findings/16_halo_fireteam.md"; do
  [ -f "$f" ] || continue
  echo "  --- $f ($(stat -c%s "$f") bytes, $(wc -l < "$f") regels):"
  cat "$f" | sed 's/^/    /' | head -60
done

echo
echo "############ 4. wat er in die findings staat over argumenten, start en testmodus"
grep -rniE 'argument|arg |-f|-n|resolution|test|service|start|bat|launch|command' \
  "$TP/findings/Halo.md" "$TP/findings/16_halo_fireteam.md" 2>/dev/null | head -25 | sed 's/^/  /'
echo "== done"

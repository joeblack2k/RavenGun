#!/bin/bash
# Eindcontrole: de planning, de drop-in, de documenten. Read-only.
set -u
L=~/projects/ravengun-patches/patches/LuigisMansion
R=~/projects/ravengun

echo "############ 1. de planning: komt de preload-regel erin?"
cd "$R"
python3 scripts/launch-direct.py LuigisMansion --plan > /tmp/plan.txt 2>&1
echo "  exitcode $?"
for s in preload audit geen_audit errors; do
  grep -o "'$s': \[[^]]*\]" /tmp/plan.txt | head -1 | cut -c1-200 | sed 's/^/  /'
done
echo "  --- de bestanden die uitrollen:"
grep -o "'dest': '[^']*'" /tmp/plan.txt | sed 's/^/    /'

echo
echo "############ 2. de drop-in en de lijst"
ls -l "$L/dropin/" | sed 's/^/  /'
echo "  --- de secties en hun sleutels:"
grep -vE '^\s*;|^$' "$L/dropin/dropin.ini" | sed 's/^/    /'
echo "  --- de lijst: $(grep -c '^\[' "$L/dropin/dongle-haken.ini") regels, wachtroute: $(grep -E '^wacht' "$L/dropin/dongle-haken.ini" | tr '\n' ' ')"
echo "  --- antwoorden:"
grep -E '^\[|^antwoord' "$L/dropin/dongle-haken.ini" | paste - - | sed 's/^/    /'

echo
echo "############ 3. documenten: geen mojibake"
for f in "$L/MECHANISME-PE-HOOK.md" "$L/dropin/dongle-haken.ini" "$L/dropin/dropin.ini"; do
  printf '  %-28s %6s bytes  mojibake: %s\n' "$(basename "$f")" "$(stat -c%s "$f")" \
    "$(grep -c -P 'Ã.|Â.|â€' "$f" 2>/dev/null || true)"
done

echo
echo "############ 4. het recept is niet aangeraakt"
( cd "$R" && git status --porcelain packaging/native-games.json ) && echo "  (leeg = schoon)"
echo "== done"

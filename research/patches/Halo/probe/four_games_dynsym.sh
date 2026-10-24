#!/bin/bash
# Do the four HASP games have hasp_* symbols in their dynsym? Read through PT_DYNAMIC, because
# `readelf --dyn-syms` and `nm -D` return nothing on these files (zero section headers).
# Read-only.
set -u
N=/home/deck/mnt/spidernas_emulation/Teknoparrot
P=~/projects/ravengun-patches/patches/Halo/probe

for g in "Halo Fireteam/g7/halo/game" "Jurassic Park/game" "Galaga Assault/game" \
         "Terminator Salvation /game"; do
  [ -f "$N/$g" ] || { echo "== $g  ONTBREEKT"; continue; }
  python3 "$P/dynsym_probe.py" "$N/$g" hasp_
  echo
done

echo "############ controle: wat zeggen de standaardtools op hetzelfde bestand?"
for g in "Jurassic Park/game" "Terminator Salvation /game"; do
  [ -f "$N/$g" ] || continue
  echo "--- $g"
  echo -n "   nm -D --defined-only | grep -c hasp_ : "
  nm -D --defined-only "$N/$g" 2>&1 | grep -c 'hasp_' || true
  echo -n "   readelf --dyn-syms   | grep -c hasp_ : "
  readelf --dyn-syms -W "$N/$g" 2>&1 | grep -c 'hasp_' || true
  echo "   (sectiekop-punt: $(readelf -h "$N/$g" 2>/dev/null | grep -i 'section header' | tr -s ' ' | tr '\n' ' '))"
done
echo "== done"

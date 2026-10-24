#!/bin/bash
# Find the real folder names, then run the PT_DYNAMIC symbol probe on every HASP game.
# Read-only.
set -u
N=/home/deck/mnt/spidernas_emulation/Teknoparrot
P=~/projects/ravengun-patches/patches/Halo/probe

echo "############ de echte mapnamen op de NAS"
ls -1 "$N" | grep -iE 'galaga|terminator|jurassic|halo' | sed 's/^/  /'

echo
echo "############ per game: wat vindt de naam-route in de dynsym?"
for d in "Halo Fireteam/g7/halo" "Jurassic Park" "Terminator Salvation "; do
  g="$N/$d/game"
  [ -f "$g" ] || { echo "== $d/game ONTBREEKT"; continue; }
  python3 "$P/dynsym_probe.py" "$g" hasp_login hasp_read hasp_get_info
  echo
done

echo "############ Galaga: welke map en welk bestand?"
for cand in "Galaga Assault" "GalagaAssault" "Galaga"; do
  [ -d "$N/$cand" ] && echo "  map gevonden: '$cand'" && ls -1 "$N/$cand" | head -8 | sed 's/^/    /'
done
echo "== done"

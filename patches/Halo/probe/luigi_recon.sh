#!/bin/bash
# Recon for the Luigi's Mansion drop-in mechanism. Read-only.
set -u
PR=~/projects/ravengun-patches
R=~/projects/ravengun
N=/home/deck/mnt/spidernas_emulation/Teknoparrot

echo "############ 1. het Luigi-dossier"
ls -la "$PR/patches/LuigisMansion/" 2>/dev/null | sed 's/^/  /'
echo "--- en de dropin-map als die er is:"
ls -la "$PR/patches/LuigisMansion/dropin/" 2>/dev/null | sed 's/^/  /' || echo "  (nog geen dropin-map)"

echo
echo "############ 2. docs/dropins.md: het contract"
sed -n '1,120p' "$R/docs/dropins.md" 2>/dev/null

echo
echo "############ 3. waar staat de PE die de analist heeft gemeten?"
echo "--- VACUUM.exe en familie op de NAS:"
ls -la "$N/Luigi's Mansion Arcade/exe/x64/" 2>/dev/null | sed 's/^/  /'
echo "--- uitgepakte/ontsleutelde kopieën in de repo:"
find "$R/cores" -maxdepth 3 -iname '*vacuum*' -o -maxdepth 3 -iname '*lm*' 2>/dev/null | head -20 | sed 's/^/  /'
find "$PR/patches/LuigisMansion" -maxdepth 3 -type f -size +1M 2>/dev/null | sed 's/^/  /'

echo
echo "############ 4. wat zegt het dossier over de 17 prologues en de adressen?"
grep -rniE '0x2b3b70|prologue|proloog|e9 |13fff0000|amDongle|amHm|amMaster' \
  "$PR/patches/LuigisMansion/" 2>/dev/null | head -40 | sed 's/^/  /'
echo "== done"

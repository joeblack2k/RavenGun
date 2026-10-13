#!/bin/bash
# What channel did the Luigi worker prove, and what tooling is available? Read-only.
set -u
PR=~/projects/ravengun-patches
R=~/projects/ravengun
L="$PR/patches/LuigisMansion"

echo "############ 1. de conclusie over het kanaal (B0-LEGE-DLL + EXITBESLUIT + LADER-PLAN)"
for f in B0-LEGE-DLL.md EXITBESLUIT.md LADER-PLAN.md; do
  echo "=== $f"
  grep -nE '^#|^##|conclusie|CONCLUSIE|kanaal|Kanaal|DLL|dll|dropin|drop-in|Preload|LD_PRELOAD|DllOverride|WINEDLLOVERRIDES' "$L/$f" 2>/dev/null | head -40 | sed 's/^/  /'
  echo
done

echo "############ 2. het lege-DLL-experiment: wat bouwt en laadt het?"
for f in b0-leeg.c b0-leeg0.c b0-bouw.sh b0-bouw-kern.sh; do
  echo "=== $f"
  sed -n '1,45p' "$L/$f" 2>/dev/null | sed 's/^/  /'
  echo
done

echo "############ 3. is er een Windows-crosscompiler (mingw) en hoe bouwt de rest het?"
for t in x86_64-w64-mingw32-gcc i686-w64-mingw32-gcc x86_64-w64-mingw32-ld; do
  printf '  %-28s %s\n' "$t" "$(command -v "$t" 2>/dev/null || echo ONTBREEKT)"
done
echo "--- hoe bouwt de HASP-Windows-DLL (die bestaat al)?"
grep -nE 'mingw|CC=|gcc|dlltool|\.def|windows' "$R/scripts/build-hasp.sh" 2>/dev/null | head -12 | sed 's/^/  /'

echo
echo "############ 4. AM-LAAG.md: de tabel en de conclusies (de kern)"
sed -n '1,60p' "$L/AM-LAAG.md" | sed 's/^/  /'
echo "  ..."
grep -nE '^## |^### ' "$L/AM-LAAG.md" | sed 's/^/  /'

echo
echo "############ 5. waar staat de dump die de analist heeft gemeten?"
grep -nE 'dump|open\(|pad|PAD|/tmp|\.bin' "$L/b0-geheugen-dump.py" 2>/dev/null | head -20 | sed 's/^/  /'
echo "--- bestaande dumps op de Deck:"
find /home/deck /tmp -maxdepth 4 -iname '*dump*' -size +1M 2>/dev/null | head -12 | sed 's/^/  /'
ls -la ~/.local/share/ravengun/views/LuigisMansion 2>/dev/null | head | sed 's/^/  /'
echo "== done"

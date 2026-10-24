#!/bin/bash
# The spec I must implement, the channel, and the test material. Read-only.
set -u
L=~/projects/ravengun-patches/patches/LuigisMansion

echo "############ 1. AM-LAAG.md §5: wat de drop-in moet leveren (de spec)"
sed -n '/^## 5\./,/^## 6\./p' "$L/AM-LAAG.md" | sed 's/^/  /'

echo
echo "############ 2. AM-LAAG.md §6: het draaiboek voor de meting"
sed -n '/^## 6\./,/^## 7\./p' "$L/AM-LAAG.md" | sed 's/^/  /'

echo
echo "############ 3. de volledige 17-regel-tabel"
sed -n '/^| # | functie-start/,/^$/p' "$L/AM-LAAG.md" | sed 's/^/  /'

echo
echo "############ 4. het kanaal: hoe komt een DLL in het proces, en hoe zie je dat?"
grep -nE 'kanaal|LoadLibrary|argv\[1\]|dropin|DllOverride|WINEDLLOVERRIDES|geladen|modulelijst|To =|Preload' \
  "$L/B0-LEGE-DLL.md" "$L/LADER-PLAN.md" 2>/dev/null | head -30 | sed 's/^/  /'

echo
echo "############ 5. testmateriaal: bestaat de dump nog?"
ls -la /tmp/b0-geheugen-b/ 2>/dev/null | sed 's/^/  /' || echo "  /tmp/b0-geheugen-b bestaat niet meer"
find /tmp -maxdepth 3 -name 'image-*.bin' -size +1M 2>/dev/null | sed 's/^/  /'
ls -la /tmp/vacuum-dump/ 2>/dev/null | head -8 | sed 's/^/  /'

echo
echo "############ 6. de mingw-container (voor een PE-DLL)"
if command -v podman >/dev/null; then
  podman image inspect localhost/ravengun-mingw >/dev/null 2>&1 && echo "  localhost/ravengun-mingw: AANWEZIG" || echo "  localhost/ravengun-mingw: ONTBREEKT"
  podman images 2>/dev/null | head -6 | sed 's/^/  /'
else
  echo "  podman ONTBREEKT"
fi
echo "== done"

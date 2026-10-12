#!/usr/bin/env bash
# Bouwt librg-glutapi.so (i386) uit de bron naast dit script.
#
# Waarom -m32 met een eigen inc/gnu: op deze machine ontbreekt
# /usr/include/gnu/stubs-32.h (32-bits glibc-devheader). Het werkende recept uit
# scripts/build-elfloader.sh: kopieer stubs-64.h naar een eigen inc/gnu/stubs-32.h
# en geef die map met -I mee. Zonder dat stopt gcc -m32 op <features.h>.
#
# De sonaam is met opzet libglut.so.3: zo is dit bestand een drop-in voor de
# NEEDED-regel van het spel. De echte libglut moet dan wel onder een andere naam
# staan (bijv. libglut.so.3.real) of via RG_GLUT_REAL worden aangewezen, anders
# zoekt de resolver zichzelf. Zie README.md.
set -euo pipefail

HIER="$(cd "$(dirname "$0")" && pwd)"
CC="${CC:-gcc}"

# 1. de mechanische delen opnieuw genereren (122 stubs + 33 wikkels)
python3 "$HIER/gen.py"

# 2. de 32-bits headerstruc
INC="$HIER/inc"
if [[ ! -f /usr/include/gnu/stubs-32.h ]]; then
  mkdir -p "$INC/gnu"
  cp -f /usr/include/gnu/stubs-64.h "$INC/gnu/stubs-32.h" 2>/dev/null || : > "$INC/gnu/stubs-32.h"
  echo "inc/gnu/stubs-32.h aangemaakt uit stubs-64.h (stubs-32.h ontbreekt op deze machine)"
fi

# 3. bouwen
echo "bouwen: i386 -> librg-glutapi.so"
"$CC" -m32 -shared -fPIC -O2 -Wall -Wextra -I"$INC" \
      -Wl,-soname,libglut.so.3 \
      -o "$HIER/librg-glutapi.so" \
      "$HIER/rg_shim.c" "$HIER/rg_ext.c" "$HIER/rg_gen.c" "$HIER/rg_stubs.S" \
      -ldl

file "$HIER/librg-glutapi.so"
echo "exportnamen : $(nm -D --defined-only "$HIER/librg-glutapi.so" | grep -c '^[0-9a-f]* T' || true)"
echo "glut*-namen : $(nm -D --defined-only "$HIER/librg-glutapi.so" | awk '{print $3}' | grep -c '^glut' || true)"
echo
echo "Installeren (voorbeeld, buiten deze map):"
echo "  cp librg-glutapi.so <runtime>/libglut.so.3        # drop-in voor de NEEDED-regel"
echo "  cp <echte libglut.so.3> <runtime>/libglut.so.3.real"
echo "of laat de resolver de echte bibliotheek vinden met RG_GLUT_REAL=<pad naar de echte libglut.so.3>."

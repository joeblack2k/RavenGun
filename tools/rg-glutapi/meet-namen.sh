#!/usr/bin/env bash
# meet-namen.sh -- hoe de twee namenlijsten in deze map gemeten zijn.
# Alleen lezen; schrijft niets buiten deze map.
#
#   bash meet-namen.sh
#
# names_budgie.txt       : de 161 glut*-namen uit de exporttabel van BudgieLoader.exe
# names_real_libglut.txt : de 171 glut*-namen uit onze eigen 32-bits libglut.so.3
set -euo pipefail

HIER="$(cd "$(dirname "$0")" && pwd)"
R="$HOME/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2"
L="$HOME/.local/share/ravengun/runtime/lib32/libglut.so.3"

objdump -p "$R/BudgieLoader.exe" | grep -oE 'glut[A-Za-z0-9_]*' | LC_ALL=C sort -u > "$HIER/names_budgie.txt"
nm -D --defined-only "$L" | awk '{print $3}' | grep -E '^glut' | LC_ALL=C sort -u > "$HIER/names_real_libglut.txt"

echo "BudgieLoader.exe : $(wc -l < "$HIER/names_budgie.txt") glut-namen"
echo "onze libglut     : $(wc -l < "$HIER/names_real_libglut.txt") glut-namen"
echo "gedekt door onze libglut : $(LC_ALL=C comm -12 "$HIER/names_budgie.txt" "$HIER/names_real_libglut.txt" | wc -l)"
echo "zelf te maken            : $(LC_ALL=C comm -23 "$HIER/names_budgie.txt" "$HIER/names_real_libglut.txt" | wc -l)"
echo
echo "--- de namen die wij zelf moeten leveren ---"
LC_ALL=C comm -23 "$HIER/names_budgie.txt" "$HIER/names_real_libglut.txt"

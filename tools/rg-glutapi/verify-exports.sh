#!/usr/bin/env bash
# verify-exports.sh -- vergelijkt de exportlijst van librg-glutapi.so met de 161
# glut*-namen uit de exporttabel van BudgieLoader.exe. Doel: 0 ontbrekend, 0 te veel.
#
#   bash verify-exports.sh [pad naar librg-glutapi.so]
set -uo pipefail

HIER="$(cd "$(dirname "$0")" && pwd)"
SO="${1:-$HIER/librg-glutapi.so}"
[[ -f "$SO" ]] || { echo "FOUT: $SO bestaat niet; bouw eerst met build.sh"; exit 1; }

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

# Alle gedefinieerde dynamische symbolen (T = code, D/B/R = data; die horen er niet te zijn).
LC_ALL=C nm -D --defined-only "$SO" | awk '{print $3}' | LC_ALL=C sort -u > "$W/onze.txt"
LC_ALL=C nm -D --defined-only "$SO" | awk '$2=="T" || $2=="i" || $2=="W" {print $3}' | LC_ALL=C sort -u > "$W/onze_code.txt"
LC_ALL=C sort -u "$HIER/names_budgie.txt" > "$W/budgie.txt"

ONTBREEKT="$(LC_ALL=C comm -23 "$W/budgie.txt" "$W/onze.txt" | wc -l)"
TEVEEL="$(LC_ALL=C comm -13 "$W/budgie.txt" "$W/onze.txt" | wc -l)"
GEEN_CODE="$(LC_ALL=C comm -23 "$W/onze.txt" "$W/onze_code.txt" | wc -l)"

echo "librg-glutapi.so            : $SO"
echo "BudgieLoader, verwacht      : $(wc -l < "$W/budgie.txt") glut*-namen"
echo "onze .so, alle dyn.symbolen : $(wc -l < "$W/onze.txt")"
echo "onze .so, glut*-namen       : $(awk '/^glut/' "$W/onze.txt" | wc -l)"
echo "onze .so, code-symbolen     : $(wc -l < "$W/onze_code.txt")"
echo
echo "ontbrekend : $ONTBREEKT"
LC_ALL=C comm -23 "$W/budgie.txt" "$W/onze.txt" | sed 's/^/  ONTBREEKT /'
echo "te veel    : $TEVEEL"
LC_ALL=C comm -13 "$W/budgie.txt" "$W/onze.txt" | sed 's/^/  TEVEEL /'
echo "geen code  : $GEEN_CODE"
LC_ALL=C comm -23 "$W/onze.txt" "$W/onze_code.txt" | sed 's/^/  GEEN-CODE /'
echo
if [[ "$ONTBREEKT" == "0" && "$TEVEEL" == "0" && "$GEEN_CODE" == "0" ]]; then
    echo "RESULTAAT: PASS -- alle 161 namen gedekt, geen enkel extra symbool"
    exit 0
fi
echo "RESULTAAT: FAIL"
exit 1

#!/bin/bash
# WAT IS ER AL GELEZEN OVER DE GEWEERLAAG, EN WAT STAAT ER IN HET GEDEELDE HULPBESTAND?
set -u
R=~/projects/ravengun
P=~/projects/ravengun-patches/patches/Halo
S="$R/tools/rt-input-shim/rg_rt_input.c"

echo "############ 1. MIJN EIGEN DOSSIERS OVER DE GEWEERLAAG"
ls -1 "$P"/*.md | sed 's/^/  /'
echo "  --- de adressen die in mijn dossiers genoemd worden:"
grep -rnoE '0x1[0-9a-f]{5,7}|0x[0-9a-f]{6,7}|\+0x[0-9a-f]{2,3}' "$P"/*.md 2>/dev/null | awk -F: '{print $NF}' | sort | uniq -c | sort -rn | head -22 | sed 's/^/    /'

echo
echo "############ 2. DE GEWEERLAAG IN MIJN EIGEN WOORDEN (uit de dossiers)"
for f in "$P/GEWEER-CLASSIFICATIE.md" "$P/HALO-NATIVE-ROUTE.md" "$P/FABRIEKSINSTELLING.md"; do
  [ -f "$f" ] || continue
  echo "  === $(basename "$f") ($(wc -l < "$f") regels)"
  grep -inE 'geweer|gun|aim|asbyte|as-|kommagetal|float|apparaatblok|trekker|trigger|0x1a7|0xdccc|0x1f392' "$f" | head -18 | sed 's/^/    /'
done

echo
echo "############ 3. HET GEDEELDE HULPBESTAND: mijn regelsoorten EN het werk van de ander"
echo "  --- bestandsgrootte/sha:"
stat -c '    %s bytes  %n' "$S" 2>/dev/null; sha256sum "$S" 2>/dev/null | cut -c1-16 | sed 's/^/    sha256 /'
echo "  --- mijn regelsoorten (zet/teller/as) die de parser kent:"
grep -nE '"zet"|"teller"|"as"|regelsoort|SOORT_' "$S" 2>/dev/null | head -20 | sed 's/^/    /'
echo "  --- en alles wat op ASSEN, TREKKER of het tp-shm-formaat wijst (van mij of van hem):"
grep -nE 'as\[|assen|axis|as_|trekker|trigger|0x0c|offset 12|tp-shm|tpshm|shm_bit|kommagetal|float' "$S" 2>/dev/null | head -24 | sed 's/^/    /'

echo
echo "############ 4. WAT ER IN HET IJKPUNT ZIT (commit 5b18037) -- alleen de namen, niets wijzigen"
cd "$R" && git show --stat 5b18037 2>&1 | head -14 | sed 's/^/  /'
echo "  --- en de ongecommitte regels in dat bestand nu:"
git status --porcelain 2>/dev/null | sed 's/^/    /' | head -8
git diff --stat 2>/dev/null | tail -4 | sed 's/^/    /'

echo
echo "############ 5. DE ADRESSEN DIE DE COORDINATOR NOEMT -- komen ze uit mijn leeswerk?"
for a in 0x1a7cb20 0x3c 0xdccc80 0x1f392d0; do
  n=$(grep -rl "$a" "$P" "$R/docs" "$R/tools" 2>/dev/null | head -3 | tr '\n' ' ')
  printf '  %-12s gevonden in: %s\n' "$a" "${n:-NIETS}"
done
echo "== done"

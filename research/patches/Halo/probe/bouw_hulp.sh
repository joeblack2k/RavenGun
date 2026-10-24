#!/bin/bash
# BOUW DE HULP OPNIEUW UIT DE BRON EN CONTROLEER DAT MIJN REGELSOORTEN ERIN ZITTEN.
set -u
R=~/projects/ravengun
S="$R/tools/rt-input-shim/rg_rt_input.c"
SO="$R/build/librg_rt_input.so"

echo "############ 1. DE BRON: is die nieuwer dan het gebouwde bestand?"
stat -c '  bron : %s bytes, gewijzigd %y' "$S" 2>/dev/null
stat -c '  .so  : %s bytes, gewijzigd %y' "$SO" 2>/dev/null || echo "  .so  : bestaat niet"
sha256sum "$S" | cut -c1-16 | sed 's/^/  bron-sha256 /'

echo
echo "############ 2. BOUWEN"
cd "$R"
if cmake --build build -j4 --target rg_rt_input 2>&1 | tail -6 | sed 's/^/  /'; then
  echo "  (cmake-doel rg_rt_input)"
else
  echo "  cmake-doel mislukt; ik probeer het hele project"
  cmake --build build -j4 2>&1 | tail -6 | sed 's/^/  /'
fi

echo
echo "############ 3. HET NIEUWE ARTEFACT"
ls -l "$SO" 2>/dev/null | sed 's/^/  /'
sha256sum "$SO" 2>/dev/null | sed 's/^/  /'
echo "  --- zitten MIJN regelsoorten erin? (de letterlijke trefwoorden en de foutmeldingen)"
for w in 'zet' 'teller' 'soort=f32' 'RG_MAX_AS' 'buiten' 'modulo'; do
  n=$(strings -n 3 "$SO" 2>/dev/null | grep -c "$w")
  printf '    %-12s %s treffer(s)\n' "$w" "$n"
done
echo "  --- en de toevoegingen van de laderbouwer (tp-shm-kant)?"
for w in 'asbyte' 'trekker' 'RG_RT_TABLE'; do
  n=$(strings -n 3 "$SO" 2>/dev/null | grep -c "$w")
  printf '    %-12s %s treffer(s)\n' "$w" "$n"
done

echo
echo "############ 4. DE KANDIDAAT-TABEL EN DE OPROEP DIE IK GA GEBRUIKEN"
ls -l "$R/../ravengun-patches/patches/Halo/rt-invoer.kandidaat.ini" 2>/dev/null | sed 's/^/  /'
echo "  --- de eerste regels van de kandidaat-tabel:"
head -18 "$R/../ravengun-patches/patches/Halo/rt-invoer.kandidaat.ini" 2>/dev/null | sed 's/^/    /'
echo "  --- leest de hulp RG_RT_TABLE? (uit de bron)"
grep -n 'RG_RT_TABLE' "$S" | head -5 | sed 's/^/    /'
echo "  --- en de naam van de standaardtabel die hij zoekt:"
grep -nE '"[^"]*rt-invoer[^"]*"|rg-invoer|RG_RT_TABLE' "$S" | head -6 | sed 's/^/    /'
echo "== done"

#!/bin/bash
# WAT STAAT ER IN HET WartranTroopers-PROFIEL OVER DE ASSEN? (bench-vrij, voor stap d)
set -u
R=~/projects/ravengun
echo "############ 1. het WartranTroopers-profiel uit tp-shm-profiles.json"
python3 - "$R/packaging/tp-shm-profiles.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
prof = d.get("profiles", d) if isinstance(d, dict) else d
for naam, p in (prof.items() if isinstance(prof, dict) else []):
    if "wartran" in naam.lower():
        print("  profiel: %s" % naam)
        print("  " + json.dumps(p, indent=2, ensure_ascii=False)[:2600].replace("\n", "\n  "))
PY

echo
echo "############ 2. hoe de assen geschreven worden (de dialect-code)"
grep -rn -iE 'as|axis|axes|analoog|analog|offset|min|max|invert|byte_order|rust' \
  "$R/src/ravengun/iobridge/"*.hpp "$R/src/ravengun/iobridge/"*.cpp 2>/dev/null | grep -iE 'as|axis|axes|analog|invert' | head -20 | sed 's/^/  /'

echo
echo "############ 3. en welk deel van de spellentafel daarbij hoort"
grep -rn -iE '0xaea0|0x2200|0x50|schakelaar|tabel|aim|as' "$R/src/ravengun/iobridge/tp_shm"*.* 2>/dev/null | head -20 | sed 's/^/  /'

echo
echo "############ 4. wat het project over de tp-shm-indeling van HALO zegt"
grep -rn -iE 'wastran|wartran|halo' "$R/packaging/tp-shm-profiles.json" 2>/dev/null | head -10 | sed 's/^/  /'
grep -rn -iE 'WartranTroopers' "$R/docs/architecture.md" "$R/docs/decisions.md" 2>/dev/null | head -8 | sed 's/^/  /'
echo "== done"

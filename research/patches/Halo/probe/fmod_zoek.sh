#!/bin/bash
# Klopt de FMOD-stub die het recept klaarzet met wat de game VRAAAGT (libfmod.so.10)?
set -u
R=~/projects/ravengun
D=~/.local/share/ravengun

echo "############ 1. wat de game vraagt (uit mijn eigen ronde-1-dossier)"
grep -nE 'libfmod|libfmodstudio|DT_NEEDED|\.so\.' "$R/patches/Halo/HALO-NATIVE-ROUTE.md" 2>/dev/null | head -12 | sed 's/^/  /'

echo
echo "############ 2. wat het recept klaarzet: @data/runtime/lib-fmodstub -> /rg/libfmod"
python3 - "$R/packaging/native-games.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
def loop(o):
    if isinstance(o, dict):
        s = json.dumps(o)
        if "fmod" in s.lower():
            for k, v in o.items():
                if k in ("src", "dst", "note", "libs", "data") or "fmod" in str(v).lower():
                    print("  %-8s %s" % (k, json.dumps(v)[:200]))
        for v in o.values():
            loop(v)
    elif isinstance(o, list):
        for v in o:
            loop(v)
loop(d)
PY

echo
echo "############ 3. waar staat lib-fmodstub werkelijk, en wat zit erin?"
for p in "$R/data/runtime/lib-fmodstub" "$D/runtime/lib-fmodstub" "$R/data/runtime"; do
  [ -d "$p" ] && { echo "  --- $p:"; ls -l "$p" | sed 's/^/    /'; }
done
find "$R" -maxdepth 4 -iname 'lib-fmodstub' -o -maxdepth 4 -iname '*fmodstub*' 2>/dev/null | head -6 | sed 's/^/  gevonden: /'

echo
echo "############ 4. en de stub-bron: welke namen en versies maakt gen.sh?"
sed -n '1,40p' "$R/tools/fmod-stub/gen.sh" 2>/dev/null | sed 's/^/  /'
echo "  --- de eerste symbolen uit symbols.txt:"
head -5 "$R/tools/fmod-stub/symbols.txt" 2>/dev/null | sed 's/^/    /'
grep -c 'FMOD' "$R/tools/fmod-stub/symbols.txt" 2>/dev/null | sed 's/^/    aantal FMOD-regels: /'

echo
echo "############ 5. wat er in de view/runtime van Halo aanwezig is"
ls -l "$D/runtime" 2>/dev/null | head -12 | sed 's/^/  /'
find "$D" -maxdepth 3 -iname 'libfmod*' 2>/dev/null | head -8 | sed 's/^/    /'

echo
echo "############ 6. de eerdere GESLAAGDE meting: wat laadde de game toen?"
grep -nE 'FMOD|sound bank|attract|level1|Webcam|Network' "$R/patches/Halo/gamelog-run5.txt" 2>/dev/null | head -20 | sed 's/^/  /'
echo "== done"

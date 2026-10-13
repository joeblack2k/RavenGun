#!/bin/bash
# De OFFICIELE startmanier van Halo zoeken, en vergelijken met wat wij meegeven.
# Alles lezen; de NAS blijft read-only. Per bevinding noem ik het bestand en de regel.
set -u
P=~/projects/ravengun-patches/patches/Halo
R=~/projects/ravengun
MNT=/home/deck/mnt/spidernas_emulation

echo "############ 1. het profiel dat de launcher gebruikt (patches/Halo/game.ini)"
grep -nE '^\[|Path|Executable|Args|Config|Loader|Dongle|Source|Wine' "$P/game.ini" 2>/dev/null | head -40 | sed 's/^/  /'

echo
echo "############ 2. het recept: wat geeft het Halo mee?"
python3 - "$R/packaging/native-games.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
def loop(o, pad=""):
    if isinstance(o, dict):
        naam = str(o.get("name","")) + str(o.get("dir","")) + str(o.get("exe","")) + str(o.get("game",""))
        if "alo" in naam or "alo" in pad:
            for k in ("name","dir","dirs","exe","args","preload","libs","env","extra_ro","status","note"):
                if k in o:
                    print("  %-10s %s" % (k, json.dumps(o[k])[:300]))
        for k, v in o.items():
            loop(v, pad + "." + k)
    elif isinstance(o, list):
        for v in o:
            loop(v, pad)
loop(d)
PY

echo
echo "############ 3. de NAS-map van Halo vinden (met een DEEL van de naam)"
find "$MNT" -maxdepth 3 -iname '*halo*' 2>/dev/null | head -10 | sed 's/^/  /'
echo "  --- en alle spellmappen op het hoogste niveau (om de spelling te zien):"
ls -1 "$MNT/Teknoparrot/" 2>/dev/null | head -30 | sed 's/^/    /'

echo
echo "############ 4. startscripts en configs in en rond de Halo-map"
HALO=$(find "$MNT" -maxdepth 3 -iname '*halo*' -type d 2>/dev/null | head -1)
echo "  gekozen map: ${HALO:-GEEN}"
if [ -n "${HALO:-}" ]; then
  echo "  --- de map zelf:"
  ls -la "$HALO" | head -30 | sed 's/^/    /'
  echo "  --- alles met bat/sh/cmd/ini/cfg/xml/json/txt erin (tot 2 diep):"
  find "$HALO" -maxdepth 2 \( -iname '*.bat' -o -iname '*.sh' -o -iname '*.cmd' -o -iname '*.ini' -o -iname '*.cfg' -o -iname '*.xml' -o -iname '*.json' -o -iname '*.txt' \) 2>/dev/null | head -25 | sed 's/^/    /'
  echo "  --- de map erboven:"
  ls -la "$(dirname "$HALO")" 2>/dev/null | head -25 | sed 's/^/    /'
fi

echo
echo "############ 5. de TeknoParrot-profielen (die bevatten vaak de argumenten)"
find "$MNT" -maxdepth 4 \( -iname '*.xml' -o -iname '*.ini' \) -path '*rofile*' 2>/dev/null | head -15 | sed 's/^/  /'
echo "  --- en een profiel dat Halo noemt:"
grep -rli 'halo' "$MNT" --include='*.xml' --include='*.ini' 2>/dev/null | head -8 | sed 's/^/    /'

echo
echo "############ 6. draaiboek-vraag: staat er een test-/servicemodus of schakelaar in een config?"
grep -rniE 'testmode|servicemode|freeplay|freeply|credit|coin|debug|-t |-s ' \
  "$P/game.ini" "$R/packaging/native-games.json" 2>/dev/null | head -10 | sed 's/^/  /'
echo "== done"

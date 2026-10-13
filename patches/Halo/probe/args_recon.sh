#!/bin/bash
# Welke argumenten geeft het recept Halo mee, en is er een officieel startscript om mee te vergelijken?
set -u
R=~/projects/ravengun
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam Raven"

echo "############ 1. Halo in het recept"
python3 - "$R/packaging/native-games.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
lijst = d.get("games") if isinstance(d, dict) else d
if isinstance(lijst, dict):
    lijst = list(lijst.values())
for g in (lijst or []):
    if not isinstance(g, dict):
        continue
    naam = str(g.get("name", "")) + str(g.get("game", "")) + str(g.get("dir", ""))
    if "alo" in naam:
        for k in ("name", "dir", "exe", "args", "preload", "env", "loader", "status"):
            if k in g:
                print("  %-10s %s" % (k, g[k]))
PY

echo
echo "############ 2. de NAS-map: is er een officieel startscript?"
ls -1 "$NAS" 2>/dev/null | head -25 | sed 's/^/  /'
echo "  --- alles met bat/sh/cmd/run/lnk:"
find "$NAS" -maxdepth 1 \( -iname '*.bat' -o -iname '*.sh' -o -iname '*.cmd' -o -iname '*run*' -o -iname '*.lnk' \) 2>/dev/null | sed 's/^/    /'

echo
echo "############ 3. en onze vervangingen in de overlay (run_game.sh_original e.d.)"
find ~/.local/share/ravengun/native/Halo -maxdepth 4 2>/dev/null | grep -viE '/work/|pmuser|\.cache' | head -15 | sed 's/^/  /'

echo
echo "############ 4. de argumenten die WIJ gaven, uit de logboeken"
grep -ho 'cmd: .*' ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | sort -u | sed 's/^/  /'
echo "  --- en de FMOD-stub / overige preloads uit die logs:"
grep -ho 'preload=.*' ~/.local/state/ravengun/logs/native-Halo-*.log 2>/dev/null | sort -u | sed 's/^/  /'
echo "== done"

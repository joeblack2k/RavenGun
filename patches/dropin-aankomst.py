#!/usr/bin/env python3
"""Controleert of elke drop-in ook echt **aankomt**: staat er op de bestemming in de view een bestand
met dezelfde sha256 als wat wij in `patches/<Game>/dropin/` hebben staan?

`tools/verify-all.py` meldt "drop-ins die NIET aankomen: 0", maar dat kijkt naar het **plan** (de
dest-paden die de launcher zou schrijven), niet naar de bytes die er staan. Deze controle kijkt naar
de bytes, en meldt ook als er op de bestemming nog iets anders staat: een symlink naar de NAS (het
bestand van de fabrikant) of een bestand met een andere inhoud.

Gebruik:
    python3 patches/dropin-aankomst.py            # alle games met een dropin/
    python3 patches/dropin-aankomst.py TC5 LGI    # een paar games

Exitcode 0 als alles aankomt, 1 als er iets mis is.
"""
import hashlib
import json
import os
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
PATCHES = HIER
LOADER = os.environ.get("RAVENGUN_REPO", os.path.expanduser("~/projects/ravengun"))


def plan_van(game):
    r = subprocess.run([sys.executable, "scripts/launch-direct.py", game, "--plan", "--json"],
                       cwd=LOADER, capture_output=True, text=True)
    i = r.stdout.find("{")
    if i < 0:
        return None
    try:
        return json.loads(r.stdout[i:])
    except ValueError:
        return None


def sha(pad):
    with open(pad, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    games = sys.argv[1:] or sorted(
        d for d in os.listdir(PATCHES)
        if os.path.isdir(os.path.join(PATCHES, d, "dropin")) and not d.startswith("_"))
    print("%-26s %-6s %s" % ("GAME", "AANKOMST", "bijzonderheden"))
    goed = fout = leeg = 0
    for game in games:
        plan = plan_van(game)
        if not plan:
            print("%-26s %-6s %s" % (game, "-", "geen plan"))
            fout += 1
            continue
        bestanden = (plan.get("dropins") or {}).get("files") or []
        if not bestanden:
            print("%-26s %-6s %s" % (game, "leeg", "geen drop-in-bestanden"))
            leeg += 1
            continue
        mis = []
        for f in bestanden:
            dest, src, naam = f.get("dest") or "", f.get("src") or "", f.get("name") or "?"
            if not dest or not os.path.exists(dest):
                mis.append("%s: ontbreekt in de view" % naam)
                continue
            try:
                if os.path.islink(dest):
                    mis.append("%s: SYMLINK -> %s" % (naam, os.readlink(dest)))
                elif os.path.isfile(dest) and sha(dest) != sha(src):
                    mis.append("%s: andere inhoud dan de drop-in" % naam)
            except OSError as e:
                mis.append("%s: %s" % (naam, e))
        if mis:
            fout += 1
            print("%-26s %-6s %s" % (game, "MIS", "; ".join(mis)[:160]))
        else:
            goed += 1
            print("%-26s %-6s %d bestand(en), zelfde sha256" % (game, "ok", len(bestanden)))
    print()
    print("ok=%d  mis=%d  leeg=%d" % (goed, fout, leeg))
    return 1 if fout else 0


if __name__ == "__main__":
    sys.exit(main())

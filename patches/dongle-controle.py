#!/usr/bin/env python3
"""Laat elke dongle door het gereedschap van zijn eigen soort keuren, en zet de uitkomst op een rij.

`tools/verify-all.py` (loader-repo) meldt "waarvan script ontbreekt: 0" — dat zegt dat er een
gereedschap *is*, niet of dat gereedschap de beschrijving ook goedkeurt. Dit script draait per game
het gereedschap van zijn soort (`hasp.py`, `matrix.py`, `region.py`) en laat het oordeel zien. Zo
vond een eerdere ronde TC5 (onvolledig) en OG (gereedschap vond de exe niet).

Gebruik:
    python3 patches/dongle-controle.py            # alle games met een dongle.json
    python3 patches/dongle-controle.py OG TC5     # een paar games

Exitcode 0 als elke dongle door zijn gereedschap wordt goedgekeurd, 1 als er een rood is.
"""
import json
import os
import subprocess
import sys

PATCHES = os.path.dirname(os.path.abspath(__file__))
LOADER = os.environ.get("RAVENGUN_REPO", os.path.expanduser("~/projects/ravengun"))
TOOL = {"hasp": "hasp.py", "matrix": "matrix.py", "region": "region.py"}


def dongle_van(game):
    pad = os.path.join(PATCHES, game, "dropin/ravengun-hasp", game, "dongle.json")
    if not os.path.isfile(pad):
        return None
    try:
        with open(pad, encoding="utf-8") as f:
            return json.load(f)
    except ValueError:
        return {"kind": "onleesbaar"}


def keur(game, soort):
    tool = TOOL.get(soort)
    if not tool:
        return (None, "geen gereedschap voor deze soort")
    r = subprocess.run([sys.executable, "scripts/" + tool, "--game", game, "--json"],
                       cwd=LOADER, capture_output=True, text=True)
    t = r.stdout
    i = t.find("{")
    if i < 0:
        regels = [x for x in (r.stdout + r.stderr).split("\n") if x.strip()]
        return (None, regels[-1][:110] if regels else "(geen uitvoer)")
    try:
        j = json.loads(t[i:])
    except ValueError:
        return (None, "geen leesbare JSON")
    ok = bool(j.get("ok"))
    msg = (j.get("message") or "").strip()
    return (ok, msg[:110])


def main():
    games = sys.argv[1:] or sorted(
        d for d in os.listdir(PATCHES)
        if os.path.isdir(os.path.join(PATCHES, d)) and not d.startswith("_"))
    print("%-26s %-8s %-10s %s" % ("GAME", "SOORT", "GEREEDSCHAP", "OORDEEL"))
    rood = []
    for game in games:
        d = dongle_van(game)
        if d is None:
            continue
        soort = d.get("kind", "?")
        ok, oordeel = keur(game, soort)
        if ok is False:
            rood.append((game, oordeel))
            hint = ""
            if "noemt geen enkele amDongle" in oordeel or "noemt geen enkele regioleesroute" in oordeel:
                hint = ("  -> het gereedschap vond de exe niet; zet RAVENGUN_GAME_DIR op de map van de "
                        "game en probeer opnieuw (patches/OG/README.md)")
            oordeel = oordeel + hint
        print("%-26s %-8s %-10s %s" % (game, soort, TOOL.get(soort, "-"),
                                       ("ok" if ok else ("ROOD" if ok is False else "?")) + " -- " + oordeel))
    print()
    print("== dongles: %d  rood: %d" % (sum(1 for g in games if dongle_van(g)), len(rood)))
    return 1 if rood else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Splitst het oude packaging/game-options.json (een bestand, alle games) in patches/<Game>/options.json.

Gebruik: tools/dev/split-game-options.py SRC_JSON PATCHES_DIR [--dry-run]

De oude bron staat in de git-geschiedenis (voor 2026-10-16: git show <rev>:packaging/game-options.json). Per game komt in
PATCHES_DIR/<Game>/options.json alleen wat de app nog leest: title, nas_folder, platform, exe, options en memory_card.
Niet mee: id en tp_profile (de mapnaam is het profiel), status_en, recommended_route, effort (de status en de route staan in
game.ini), teknoparrot_ini (de app leest het niet) en generated/path_bases/target_kinds/memory_card_vocabulary (schema en
woordenlijst staan in docs/game-opties.md). Het formaat staat daar ook.

Een entry hoort bij de map PATCHES_DIR/<tp_profile>. Bestaat die map niet, dan wordt er niets aangemaakt: de entry komt in het
verslag (afgevallen met reden, of "niet toegewezen"). Bestaande options.json-bestanden worden overschreven; verder blijft alles staan.
Uitvoer: JSON met indent 2, ensure_ascii uit, afsluitende regel. Exitcode 0; 1 bij een leesfout.
"""
import argparse
import json
import os
import sys

# Entries die we bewust niet meenemen (Japanse titels die we overgeslagen hebben, of geen profiel).
DROPPED = {
    "otomedius": "Japanese title, skipped",
    "omatsuri_ganso_takoyaki": "Japanese title, skipped",
    "omatsuri_kingyo_sukui": "Japanese title, skipped",
    "medaru_no_gunman": "Japanese title, skipped",
    "rainbow_bombergirl": "Japanese title, skipped",
    "mighty_moto": "no TeknoParrot profile, no patches folder",
}

# tp_profile -> mapnaam, als die afwijkt. MKDX118: game.ini van MarioKartDX zegt dat de game versie 1.18.16 is en dat het profiel het
# sjabloon MKDX118 volgt. MKDX (1.10.22) heeft geen map.
FOLDER_FOR_PROFILE = {"MKDX118": "MarioKartDX"}

KEEP = ("title", "nas_folder", "platform", "exe", "options", "memory_card")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("src")
    ap.add_argument("patches")
    ap.add_argument("--dry-run", action="store_true", help="schrijf niets, toon alleen het verslag")
    a = ap.parse_args()
    try:
        with open(a.src, encoding="utf-8") as f:
            games = json.load(f)["games"]
    except (OSError, ValueError, KeyError) as e:
        print("error: %s: %s" % (a.src, e), file=sys.stderr)
        return 1
    if isinstance(games, list):
        games = {g.get("id") or g.get("tp_profile"): g for g in games}
    bad = 0
    written = 0
    for gid, g in games.items():
        prof = g.get("tp_profile")
        if gid in DROPPED:
            print("dropped   %-26s %s" % (gid, DROPPED[gid]))
            continue
        folder = FOLDER_FOR_PROFILE.get(prof, prof)
        if not folder:
            print("UNMAPPED  %-26s no tp_profile" % gid)
            bad += 1
            continue
        if not os.path.isdir(os.path.join(a.patches, folder)):
            print("UNMAPPED  %-26s no folder %s/%s" % (gid, a.patches, folder))
            bad += 1
            continue
        out = {k: g[k] for k in KEEP if g.get(k) not in (None, "", [], {})}
        text = json.dumps(out, indent=2, ensure_ascii=False) + "\n"
        path = os.path.join(a.patches, folder, "options.json")
        print("%s %-26s -> %s/options.json (%d options)" % ("would write" if a.dry_run else "wrote      ", gid, folder, len(g.get("options", []))))
        if not a.dry_run:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
        written += 1
    print("%d files %s, %d dropped on purpose, %d unmapped" % (written, "to write" if a.dry_run else "written",
                                                               sum(1 for k in games if k in DROPPED), bad))
    return 0


if __name__ == "__main__":
    sys.exit(main())

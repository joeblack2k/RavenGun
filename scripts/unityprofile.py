#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Het profiel van een Unity-game (Tomb Raider Arcade, Rabbids Hollywood Arcade): patches/<Game>/unity.json.

Tegenhanger van src/ravengun/launch/unity.cpp voor de scripts (tools/dev/aagames-setup.py, scripts/build-unity-plugin.sh):
 - het bestand is patches/<Game>/unity.json; de gebruikerslaag gaat voor en vervangt het meegeleverde bestand als geheel (de zoekvolgorde
   van gamesettings.patches_roots(), docs/per-game-patches.md); een kapot bestand is een fout, geen terugval;
 - de map van de game staat er als mapnaam in ("folder"), niet als pad: hij wordt gezocht in de gamemappen van library.json, met de regel
   van "@nas/<Map>" van de native route (<gamemap>/<folder>, anders een niveau dieper <gamemap>/*/<folder>; de naam moet exact kloppen,
   ook een spatie aan het eind). Alleen de gebruikerslaag mag een eigen pad geven ("nas_dir", ~ wordt ingevuld) en dat gaat voor;
 - de gedeelde instellingen (poort, versies) staan in packaging/unity-route.json; ontbreekt het, dan gelden de standaardwaarden.

Gebruik als programma:  python3 -I scripts/unityprofile.py games | nas GAME | src GAME | file GAME
  games   de games met een unity.json, een per regel
  nas     de map van de game op de NAS (de melding op stderr en afsluitcode 1 als hij niet te vinden is)
  src     de bronmap van de plugin onder plugins/unity/src voor deze game (tombraider of rabbids)
  file    het unity.json dat geldt
"""
import json
import os
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import gamesettings as gs   # noqa: E402
import ownprofile           # noqa: E402

ROUTE_FILE = os.path.join(ROOT, "packaging", "unity-route.json")
ROUTE_DEFAULTS = {"port": 33610, "demulshooter_version": "v17.9", "bepinex_version": "5.4.21.0"}
PLUGIN_SRC = {"tomb_raider": "tombraider", "rabbids_hollywood": "rabbids"}


class UnityError(Exception):
    """Een melding voor de gebruiker (Nederlands)."""


def plain_name(game):
    return bool(game) and len(game) <= 64 and all(c.isascii() and (c.isalnum() or c in "_-") for c in game)


def unity_file(game):
    """Het unity.json dat geldt: de bovenste laag die het heeft (gebruiker boven meegeleverd). None = geen."""
    if not plain_name(game):
        return None
    for root in reversed(gs.patches_roots()):
        f = os.path.join(root, game, "unity.json")
        if os.path.isfile(f):
            return f
    return None


def unity_games():
    """De games met een unity.json in een van de lagen, gesorteerd."""
    out = set()
    for root in gs.patches_roots():
        try:
            names = os.listdir(root)
        except OSError:
            continue
        out.update(n for n in names if plain_name(n) and os.path.isfile(os.path.join(root, n, "unity.json")))
    return sorted(out)


def load_route(path=None):
    """packaging/unity-route.json met de standaardwaarden voor wat ontbreekt."""
    route = dict(ROUTE_DEFAULTS)
    path = path or ROUTE_FILE
    if os.path.isfile(path):
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError) as e:
            raise UnityError(f"{path}: {e}")
        if not isinstance(data, dict):
            raise UnityError(f"{path}: geen JSON-object")
        route.update({k: data[k] for k in ROUTE_DEFAULTS if k in data})
    return route


def resolve_folder(folder, game_folders):
    """Dezelfde regel als resolve_unity_dir in unity.cpp en nas_for in native.cpp. None = niet gevonden."""
    if not folder:
        return None
    for root in game_folders:
        if os.path.isdir(os.path.join(root, folder)):
            return os.path.join(root, folder)
    for root in game_folders:
        try:
            names = sorted(os.listdir(root))
        except OSError:
            continue
        for n in names:
            if os.path.isdir(os.path.join(root, n, folder)):
                return os.path.join(root, n, folder)
    return None


def load_game(game, library=None):
    """(route, g, bron): het profiel met `nas_dir` ingevuld (de gevonden map, of het eigen pad van de gebruiker).
    library = pad naar library.json (tests); anders ownprofile.library_file()."""
    f = unity_file(game)
    if not f:
        raise UnityError(f"onbekende game {game!r}; kies uit: " + ", ".join(unity_games()))
    try:
        with open(f, encoding="utf-8") as fh:
            g = json.load(fh)
    except (OSError, ValueError) as e:
        raise UnityError(f"{f}: {e}")
    if not isinstance(g, dict):
        raise UnityError(f"{f}: geen JSON-object")
    g.setdefault("exe", "Game.exe")
    folder = g.get("folder") or ""
    if folder in (".", "..") or "/" in folder:
        raise UnityError(f"{f}: 'folder' is een mapnaam binnen een gamemap, geen pad (een eigen pad is 'nas_dir' in de gebruikerslaag)")
    if g.get("nas_dir"):
        nas = os.path.expanduser(g["nas_dir"])
        if not os.path.isabs(nas):
            raise UnityError(f"{f}: 'nas_dir' moet een absoluut pad zijn")
    elif not folder:
        raise UnityError(f"{f}: 'folder' ontbreekt (de mapnaam van de game in een gamemap)")
    else:
        folders = ownprofile.library_folders(library)
        nas = resolve_folder(folder, folders)
        if not nas:
            where = ", ".join(folders) or "geen gamemappen in library.json"
            raise UnityError(f"de map {folder!r} van {game} staat in geen gamemap ({where}); NAS gekoppeld, en de map toegevoegd in de app (Add Folder)?")
    g["nas_dir"] = nas
    return load_route(), g, f


def main(argv):
    if len(argv) < 2 or argv[1] not in ("games", "nas", "src", "file") or (argv[1] != "games" and len(argv) < 3):
        sys.exit(__doc__)
    try:
        if argv[1] == "games":
            print("\n".join(unity_games()))
        elif argv[1] == "file":
            print(unity_file(argv[2]) or "")
        else:
            if argv[1] == "nas":
                print(load_game(argv[2])[1]["nas_dir"])
            else:   # src: alleen het bestand, de map van de game hoeft er niet te zijn
                f = unity_file(argv[2])
                if not f:
                    raise UnityError(f"onbekende game {argv[2]!r}; kies uit: " + ", ".join(unity_games()))
                with open(f, encoding="utf-8") as fh:
                    print(PLUGIN_SRC[json.load(fh)["type"]])
    except UnityError as e:
        print(f"unityprofile: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

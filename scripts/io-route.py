#!/usr/bin/env python3
# Invoerroute van een game voor de startscripts (play-halo.sh): packaging/io-routes.json met daarover de keuze van de
# gebruiker uit $XDG_CONFIG_HOME/ravengun/io-routes.json (standaard ~/.config/ravengun/io-routes.json; de app schrijft dat
# bestand via Properties > General > Input route). Zelfde regels als de engine (src/ravengun/iobridge/routes.cpp,
# apply_io_routes_override): per game vervangen de genoemde velden die van de basis; een ongeldig gebruikersbestand wordt
# in zijn geheel genegeerd (melding op stderr).
#
# Gebruik: scripts/io-route.py [--root REPO] PROFIEL VELD
#   VELD: route (standaard de 'default', anders evdev) | layout | board | com_port (standaard COM4) | hid_vidpid (VVVV:PPPP
#         van het hid-bord, uit packaging/hid-boards.json) | source (base of user: waar de route vandaan komt)
# Uitvoer: de waarde op één regel (leeg als hij ontbreekt). Exitcode 0, ook bij een ontbrekend bestand (dan de standaard).
import json
import os
import sys

ROUTES = ("evdev", "tp-shm", "unity-tcp", "hid-board", "jvs")


def user_path():
    x = os.environ.get("XDG_CONFIG_HOME", "")
    if x.startswith("/"):
        return os.path.join(x, "ravengun", "io-routes.json")
    h = os.environ.get("HOME", "")
    return os.path.join(h, ".config", "ravengun", "io-routes.json") if h.startswith("/") else ""


def valid(name, r):
    route = r.get("route")
    if route not in ROUTES:
        return "%s: ongeldige 'route'" % name
    if route == "tp-shm" and not r.get("layout"):
        return "%s: route tp-shm vraagt een 'layout'" % name
    if route == "hid-board" and not r.get("board"):
        return "%s: route hid-board vraagt een 'board'" % name
    if route == "jvs" and not r.get("board") and not r.get("boards"):
        return "%s: route jvs vraagt een 'board' of 'boards'" % name
    return ""


def merged(root):
    try:
        with open(os.path.join(root, "packaging", "io-routes.json")) as f:
            base = json.load(f)
    except Exception:
        base = {}
    default = base.get("default", "evdev") if base.get("default") in ROUTES else "evdev"
    games = {k: dict(v) for k, v in base.get("games", {}).items() if isinstance(v, dict)}
    src = {k: "base" for k in games}
    p = user_path()
    if p and os.path.exists(p):
        try:
            with open(p) as f:
                text = f.read()
            if text.strip():
                user = json.loads(text)
                if not isinstance(user, dict):
                    raise ValueError("geen JSON-object")
                g2 = {k: dict(v) for k, v in games.items()}
                d2 = default
                if "default" in user:
                    if user["default"] not in ROUTES:
                        raise ValueError("ongeldige 'default'")
                    d2 = user["default"]
                ug = user.get("games", {})
                if not isinstance(ug, dict):
                    raise ValueError("'games' moet een object zijn")
                s2 = dict(src)
                for name, fields in ug.items():
                    if not isinstance(fields, dict):
                        raise ValueError("%s is geen object" % name)
                    r = g2.setdefault(name, {"route": d2})
                    for key in ("route", "layout", "board", "boards", "com_port", "coin_keys", "note"):
                        if key in fields:
                            r[key] = fields[key]
                    err = valid(name, r)
                    if err:
                        raise ValueError(err)
                    s2[name] = "user"
                games, default, src = g2, d2, s2
        except Exception as e:  # zelfde gedrag als de engine: het hele gebruikersbestand negeren
            print("io-route: gebruikersbestand %s genegeerd: %s" % (p, e), file=sys.stderr)
    return default, games, src


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if len(argv) >= 2 and argv[0] == "--root":
        root, argv = argv[1], argv[2:]
    if len(argv) != 2:
        print("Gebruik: io-route.py [--root REPO] PROFIEL route|layout|board|com_port|hid_vidpid|source", file=sys.stderr)
        return 2
    profile, field = argv
    default, games, src = merged(root)
    r = games.get(profile, {})
    if field == "route":
        print(r.get("route", default))
    elif field in ("layout", "board"):
        print(r.get(field, ""))
    elif field == "com_port":
        print(r.get("com_port", "COM4"))
    elif field == "source":
        print(src.get(profile, "base"))
    elif field == "hid_vidpid":
        try:
            with open(os.path.join(root, "packaging", "hid-boards.json")) as f:
                b = json.load(f)["boards"][r["board"]]
            v = lambda x: int(x, 0) if isinstance(x, str) else int(x)
            print("%04X:%04X" % (v(b["vendor"]), v(b["product"])))
        except Exception:
            print("")
    else:
        print("io-route: onbekend veld %s" % field, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

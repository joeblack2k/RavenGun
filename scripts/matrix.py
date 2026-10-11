#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# De Matrix/SBK-donglelaag van RavenGun: status per game, en het plaatsen van de drop-in.
#
# Dit is de tegenhanger van scripts/hasp.py voor de Matrix-dongle (de LPT-dongle van Rabbids Hollywood,
# Rampage en Tomb Raider). Het script doet drie dingen:
#
#   1. lezen wat deze game nodig heeft: patches/<Game>/dongle.json (aanwezig = deze game vraagt een
#      dongle; het veld "kind" is hier "matrix");
#   2. de drop-in plaatsen: alles wat patches/<Game>/dropin/dropin.ini noemt, komt in de view van de
#      game te staan (docs/dropins.md). Een symlink in de view wordt daarbij een eigen lokaal bestand,
#      net als scripts/launch-direct.py doet voor teknoparrot.ini. De NAS wordt nooit beschreven;
#   3. de WINEDLLOVERRIDES-waarde en de omgevingsvariabelen teruggeven die erbij horen, zodat de
#      launcher ze kan zetten.
#
# Welke route het is, staat in docs/matrix-layer.md: de Harmony-patch in plugins/unity/ is de
# standaardroute voor Rabbids Hollywood (geen bestand nodig in de view), de drop-in is het
# versie-onafhankelijke alternatief. Zet de eigenaar [Dongle] Enabled = true in zijn eigen laag
# (docs/dropins.md), dan plaatst de launcher de drop-in en zet hij RAVENGUN_MATRIX_DLL=1, waardoor de
# plugin zijn Matrix-patches uitzet en de DLL het alleen doet.
#
# Gebruik:
#   scripts/matrix.py --game <Game> [--json]        status van de laag voor die game
#   scripts/matrix.py --game <Game> --install       de drop-in in de view plaatsen
#   scripts/matrix.py --game <Game> --overrides     alleen de WINEDLLOVERRIDES-waarde
#   Opties: --view <pad> (de view; standaard ~/.local/share/ravengun/runtime/views/<Game>)
# Exitcodes: 0 = in orde, 1 = niet in orde, 2 = gebruik.
#
# Voor tests: RAVENGUN_PATCHES_DIR (de meegeleverde map), RAVENGUN_USER_PATCHES_DIR (de gebruikerslaag),
# RAVENGUN_VIEWS_DIR (de hoofdmap van de views), RAVENGUN_MATRIX_OUT (de gebouwde DLL's).
import configparser
import json
import os
import shutil
import sys

sys.dont_write_bytecode = True

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOME = os.environ.get("HOME", "/home/deck")

# De soorten dongles die dongle.json mag noemen (docs/dropins.md).
KINDS = ("hasp", "matrix", "region", "amdaemon")

# Onze eigen DLL's, zoals scripts/build-matrix.sh ze maakt.
DLL_NAMES = ("matrix32.dll", "matrix64.dll")


def data_home():
    x = os.environ.get("XDG_DATA_HOME", "")
    return x if x.startswith("/") else os.path.join(HOME, ".local/share")


def patches_dirs():
    """De mappen met de game-mappen, in volgorde: de gebruikerslaag eerst (die wint), dan de meegeleverde map."""
    uit = []
    for d in (os.environ.get("RAVENGUN_USER_PATCHES_DIR") or os.path.join(data_home(), "ravengun", "patches"),
              os.environ.get("RAVENGUN_PATCHES_DIR") or os.path.join(ROOT, "patches")):
        if d and d not in uit:
            uit.append(d)
    return uit


def out_dir():
    """Waar de gebouwde DLL's staan (scripts/build-matrix.sh)."""
    return os.environ.get("RAVENGUN_MATRIX_OUT") or os.path.join(ROOT, "matrix", "out")


def game_dir(game):
    """De map van deze game: de gebruikerslaag als die er is, anders de meegeleverde map."""
    for d in patches_dirs():
        p = os.path.join(d, game)
        if os.path.isdir(p):
            return p
    return os.path.join(patches_dirs()[-1], game)


def view_dir(game, override=None):
    if override:
        return override
    basis = os.environ.get("RAVENGUN_VIEWS_DIR") or os.path.join(data_home(), "ravengun", "runtime", "views")
    return os.path.join(basis, game)


def lees_dongle(game):
    """(beschrijving, foutenlijst) uit <Game>/dongle.json. Aanwezigheid van het bestand betekent: deze game vraagt een dongle."""
    d = game_dir(game)
    pad = os.path.join(d, "dongle.json")
    if not os.path.isfile(pad):
        return None, ["geen dongle.json voor '%s' (verwacht: %s); zonder dat bestand vraagt deze game geen dongle" % (game, pad)]
    try:
        with open(pad, encoding="utf-8") as f:
            desc = json.load(f)
    except (OSError, ValueError) as e:
        return None, ["kan %s niet lezen: %s" % (pad, e)]

    fouten = []
    kind = str(desc.get("kind", "")).strip()
    if not kind:
        fouten.append("dongle.json van '%s': veld 'kind' ontbreekt" % game)
    elif kind not in KINDS:
        fouten.append("dongle.json van '%s': kind '%s' is onbekend (verwacht: %s)" % (game, kind, ", ".join(KINDS)))
    for item in desc.get("memory", []) or []:
        bestand = str(item.get("file", "")).strip()
        if not bestand:
            fouten.append("dongle.json van '%s': een memory-item heeft geen 'file'" % game)
            continue
        if not os.path.isfile(os.path.join(d, bestand)):
            fouten.append("dongle.json van '%s' wijst naar '%s', dat er niet is" % (game, bestand))
    return desc, fouten


def lees_dropin(game):
    """(lijst met drop-ins, foutenlijst) uit <Game>/dropin/dropin.ini.

    Elke drop-in is {"file", "pad", "to", "override", "env"}:
      file     de bestandsnaam in dropin/
      pad      het volledige pad van dat bestand
      to       de doelmap in de view, relatief ('.' = de hoofdmap van de game)
      override de waarde voor WINEDLLOVERRIDES ('native' -> <naam>=n, 'builtin,native' -> <naam>=n,b)
      env      lijst met K=V die erbij hoort
    """
    d = os.path.join(game_dir(game), "dropin")
    ini = os.path.join(d, "dropin.ini")
    if not os.path.isdir(d) or not os.path.isfile(ini):
        return [], []

    cp = configparser.ConfigParser()
    cp.optionxform = str          # de bestandsnaam is de sectienaam; hoofdletters moeten blijven
    try:
        cp.read(ini, encoding="utf-8")
    except (OSError, configparser.Error) as e:
        return [], ["kan %s niet lezen: %s" % (ini, e)]

    uit, fouten = [], []
    for sectie in cp.sections():
        bestand = sectie.strip()
        bron = os.path.join(d, bestand)
        if not os.path.isfile(bron):
            fouten.append("dropin.ini noemt '%s', maar %s bestaat niet" % (bestand, bron))
            continue
        override = cp.get(sectie, "DllOverride", fallback="").strip().lower()
        if override and override not in ("native", "builtin,native"):
            fouten.append("dropin.ini: DllOverride '%s' van '%s' is onbekend (native of builtin,native)" % (override, bestand))
            continue
        env = [e.strip() for e in cp.get(sectie, "Env", fallback="").split(";") if e.strip()]
        for e in env:
            if "=" not in e:
                fouten.append("dropin.ini: Env '%s' van '%s' is geen K=V" % (e, bestand))
        uit.append({
            "file": bestand,
            "pad": bron,
            "to": cp.get(sectie, "To", fallback=".").strip() or ".",
            "override": override,
            "env": env,
            "preload": cp.getboolean(sectie, "Preload", fallback=False),
        })
    return uit, fouten


def wineliboverride(entries):
    """De WINEDLLOVERRIDES-waarde voor deze drop-ins, of '' als er geen is."""
    delen = []
    for e in entries:
        naam = os.path.splitext(e["file"])[0]
        if e["override"] == "native":
            delen.append("%s=n" % naam)
        elif e["override"] == "builtin,native":
            delen.append("%s=n,b" % naam)
    return ",".join(delen)


def dropin_env(entries):
    """De omgevingsvariabelen van alle drop-ins, als dict (latere secties winnen)."""
    uit = {}
    for e in entries:
        for kv in e["env"]:
            k, _, v = kv.partition("=")
            uit[k.strip()] = v.strip()
    return uit


def geplaatst(entry, view):
    """Het pad in de view waar deze drop-in hoort te staan."""
    return os.path.normpath(os.path.join(view, entry["to"], entry["file"]))


def plaats(game, view, entries):
    """Zet de drop-ins in de view. Geeft (geplaatst, fouten). Een symlink wordt een eigen lokaal bestand,
    net als scripts/launch-direct.py doet; er wordt nooit naar de NAS geschreven."""
    uit, fouten = [], []
    for e in entries:
        doel = geplaatst(e, view)
        try:
            os.makedirs(os.path.dirname(doel), exist_ok=True)
        except OSError as ex:
            fouten.append("kan %s niet maken: %s" % (os.path.dirname(doel), ex))
            continue
        try:
            if os.path.islink(doel):
                os.unlink(doel)       # een symlink in de view wordt ons eigen bestand
            elif os.path.isfile(doel):
                with open(e["pad"], "rb") as a, open(doel, "rb") as b:
                    if a.read() == b.read():
                        uit.append(doel)
                        continue
            tmp = doel + ".ravengun-tmp"
            shutil.copyfile(e["pad"], tmp)
            os.replace(tmp, doel)
            uit.append(doel)
        except OSError as ex:
            fouten.append("kan %s niet plaatsen: %s" % (doel, ex))
    return uit, fouten


def status(game=None, view_override=None, plaatsen=False):
    """De volledige status als dict."""
    uit = {
        "ok": False, "game": game, "kind": None, "dirs": patches_dirs(), "dir": None,
        "dongle": None, "dropin": [], "overrides": "", "env": {}, "view": None, "placed": [],
        "installed": False, "build": {}, "errors": [], "message": "",
    }
    if not game:
        uit["message"] = "geen --game opgegeven; gebruik: scripts/matrix.py --game <Game> --json"
        return uit

    d = game_dir(game)
    uit["dir"] = d
    if not os.path.isdir(d):
        uit["message"] = "geen map voor '%s' (verwacht: %s)" % (game, d)
        return uit

    desc, fouten = lees_dongle(game)
    uit["dongle"] = desc
    uit["errors"] = list(fouten)
    if desc is None:
        uit["message"] = "deze game vraagt geen Matrix-dongle: %s" % "; ".join(fouten)
        return uit
    uit["kind"] = str(desc.get("kind", "")).strip()

    entries, fouten2 = lees_dropin(game)
    uit["errors"] += fouten2
    uit["dropin"] = [{k: e[k] for k in ("file", "to", "override", "env", "preload")} for e in entries]
    uit["overrides"] = wineliboverride(entries)
    uit["env"] = dropin_env(entries)

    # Zijn onze DLL's gebouwd?
    for naam in DLL_NAMES:
        p = os.path.join(out_dir(), naam)
        uit["build"][naam] = {"pad": p, "aanwezig": os.path.isfile(p)}

    if uit["kind"] != "matrix":
        uit["errors"].append("deze laag gaat over kind 'matrix', maar dongle.json zegt '%s'" % uit["kind"])

    view = view_dir(game, view_override)
    uit["view"] = view
    uit["installed"] = bool(plaatsen)
    if entries:
        if not os.path.isdir(view):
            uit["errors"].append("de view van '%s' bestaat nog niet: %s" % (game, view))
        elif plaatsen:
            geplaatst_paden, fouten3 = plaats(game, view, entries)
            uit["placed"] = geplaatst_paden
            uit["errors"] += fouten3
        else:
            for e in entries:
                doel = geplaatst(e, view)
                if os.path.isfile(doel) or os.path.islink(doel):
                    uit["placed"].append(doel)

    if uit["errors"]:
        uit["message"] = ("de Matrix-laag van '%s' is niet in orde: %s" % (game, "; ".join(uit["errors"][:3])))
        return uit

    uit["ok"] = True
    if entries:
        uit["message"] = ("Matrix-dongle van '%s' in orde; %d drop-in(s) in %s; WINEDLLOVERRIDES=%s"
                          % (game, len(entries), view, uit["overrides"] or "(geen)"))
    else:
        uit["message"] = ("Matrix-dongle van '%s' in orde; geen drop-in nodig (de Harmony-patch in "
                          "plugins/unity/ beantwoordt de controle, zie docs/matrix-layer.md)." % game)
    return uit


def main(argv):
    game = None
    als_json = False
    doen = "status"
    view = None
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--json":
            als_json = True
        elif a == "--game" and i + 1 < len(argv):
            game = argv[i + 1]; i += 1
        elif a == "--install":
            doen = "install"
        elif a == "--overrides":
            doen = "overrides"
        elif a == "--view" and i + 1 < len(argv):
            view = argv[i + 1]; i += 1
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            print("onbekende optie: %s" % a, file=sys.stderr)
            return 2
        i += 1

    if not game:
        print(__doc__)
        return 2

    st = status(game, view, plaatsen=(doen == "install"))
    if doen == "overrides":
        print(st["overrides"])
        return 0 if st["ok"] else 1
    if als_json:
        print(json.dumps(st, ensure_ascii=False, indent=1))
    else:
        print(st["message"])
        for e in st["errors"]:
            print("  - %s" % e)
        if st["placed"]:
            print("  %s: %s" % ("geplaatst" if st["installed"] else "staat al in de view",
                                ", ".join(st["placed"])))
        if st["overrides"]:
            print("  WINEDLLOVERRIDES: %s" % st["overrides"])
        for k, v in sorted(st["env"].items()):
            print("  omgeving: %s=%s" % (k, v))
    return 0 if st["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

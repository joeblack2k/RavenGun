#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""De Matrix/SBK-donglelaag van RavenGun: de drop-in controleren, plaatsen en de WINEDLLOVERRIDES
teruggeven.

Een software-dongle is geen loaderbegrip meer (docs/geen-dongle-in-de-loader.md): het is een
drop-in -- een bibliotheek in patches/<Game>/dropin/ met zijn eigen gegevens ernaast, en de launcher
kopieert bestanden en zet omgevingsvariabelen. Dit script is het gereedschap daarbij, niet een laag
die de launcher aanroept:

  * het leest patches/<Game>/dropin/dropin.ini (docs/dropins.md) en zegt of de drop-in compleet is:
    staat elk genoemd bestand er, klopt de DllOverride, en staat de gegevensmap die Env noemt
    (RAVENGUN_MATRIX_DIR) naast de bibliotheek met een matrix.ini erin;
  * het kan de drop-in in de view van de game zetten (--install). Daarbij wordt een koppeling in de
    view een eigen lokaal bestand, en een map die via een koppeling op de NAS uitkomt wordt eerst
    lokaal gemaakt met koppelingen naar de rest. De NAS wordt nooit beschreven. Dat is precies wat
    de drop-in-motor moet gaan doen; tot die het kan is dit de route om de DLL te beproeven;
  * het geeft de WINEDLLOVERRIDES-waarde en de omgevingsvariabelen terug die erbij horen.

Gebruik:
  scripts/matrix.py --game <Game> [--json]        status van de drop-in van die game
  scripts/matrix.py --game <Game> --install       de drop-in in de view plaatsen
  scripts/matrix.py --game <Game> --overrides     alleen de WINEDLLOVERRIDES-waarde
  Opties: --view <pad>   de view (standaard ~/.local/share/ravengun/runtime/views/<Game>)
Exitcodes: 0 = in orde, 1 = niet in orde, 2 = gebruik.

Voor tests: RAVENGUN_PATCHES_DIR (de meegeleverde map), RAVENGUN_USER_PATCHES_DIR (de gebruikerslaag),
RAVENGUN_VIEWS_DIR (de hoofdmap van de views), RAVENGUN_MATRIX_OUT (de gebouwde DLL's).
"""
import configparser
import json
import os
import shutil
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I neemt de scriptmap niet mee
import gamesettings as gs  # noqa: E402  (de ene zoekvolgorde van patches/<Game>/)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOME = os.environ.get("HOME", "/home/deck")

# Onze eigen DLL's, zoals scripts/build-matrix.sh ze maakt.
DLL_NAMES = ("matrix32.dll", "matrix64.dll")

# De omgevingsvariabele waarmee de drop-in zijn gegevensmap aanwijst (matrix/matrix.ini).
DATA_ENV = "RAVENGUN_MATRIX_DIR"


def data_home():
    x = os.environ.get("XDG_DATA_HOME", "")
    return x if x.startswith("/") else os.path.join(HOME, ".local/share")


def patches_dirs():
    """De mappen met de game-mappen voor "eerste treffer wint": gamesettings.patches_roots() omgekeerd (ontwikkel-checkout,
    gebruiker, patches/ van de loader). Geen eigen kopie van de volgorde."""
    return list(reversed(gs.patches_roots()))


def out_dir():
    """Waar de gebouwde DLL's staan (scripts/build-matrix.sh)."""
    return os.environ.get("RAVENGUN_MATRIX_OUT") or os.path.join(ROOT, "matrix", "out")


def game_dir(game):
    """De map van deze game: de gebruikerslaag als die er is, anders de meegeleverde map.

    Heeft de gebruikerslaag alleen een game.ini (wat normaal is: daar staan de afwijkingen) en staat
    de drop-in in de meegeleverde map, dan wint de map MET de drop-in. Anders meldt dit script ten
    onrechte "geen drop-in" voor elke game met een gebruikerslaag. Dat is dezelfde volgorde als
    dropin_specs in scripts/launch-direct.py: een laag zonder dropin.ini doet niet mee."""
    kandidaten = [gs.find_game_dir(d, game) or os.path.join(d, game) for d in patches_dirs()]
    for p in kandidaten:
        if os.path.isfile(os.path.join(p, "dropin", "dropin.ini")):
            return p
    for p in kandidaten:
        if os.path.isdir(p):
            return p
    return kandidaten[-1]


def view_dir(game, override=None):
    if override:
        return override
    basis = os.environ.get("RAVENGUN_VIEWS_DIR") or os.path.join(data_home(), "ravengun", "runtime", "views")
    return os.path.join(basis, game)


def lees_dropin(game):
    """(lijst met drop-ins, foutenlijst) uit <Game>/dropin/dropin.ini.

    Elke drop-in is {"file", "pad", "to", "override", "env", "preload", "data"}:
      file     de bestandsnaam in dropin/ (de sectienaam)
      pad      het volledige pad van dat bestand
      to       de doelmap in de view, relatief ('.' = de hoofdmap van de game)
      override de waarde voor WINEDLLOVERRIDES ('native' -> <naam>=n, 'builtin,native' -> <naam>=n,b)
      env      lijst met K=V die erbij hoort
      data     de gegevensmap die Env noemt (RAVENGUN_MATRIX_DIR), als die er is
    """
    d = os.path.join(game_dir(game), "dropin")
    ini = os.path.join(d, "dropin.ini")
    if not os.path.isdir(d) or not os.path.isfile(ini):
        return [], ["geen drop-in voor '%s' (verwacht: %s)" % (game, ini)]

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
        data = None
        for e in env:
            k, _, v = e.partition("=")
            if k.strip() == DATA_ENV and v.strip():
                p = v.strip()
                if not os.path.isabs(p):
                    p = os.path.join(d, p)
                data = {"pad": p, "aanwezig": os.path.isdir(p),
                        "ini": os.path.join(p, "matrix.ini"),
                        "ini_aanwezig": os.path.isfile(os.path.join(p, "matrix.ini"))}
                if not data["aanwezig"]:
                    fouten.append("dropin.ini zet %s=%s, maar %s bestaat niet" % (DATA_ENV, v.strip(), p))
                elif not data["ini_aanwezig"]:
                    fouten.append("de gegevensmap %s heeft geen matrix.ini" % p)
        uit.append({
            "file": bestand,
            "pad": bron,
            "to": cp.get(sectie, "To", fallback=".").strip() or ".",
            "override": override,
            "env": env,
            "preload": cp.getboolean(sectie, "Preload", fallback=False),
            "data": data,
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


def zelfde_bestand(a, b):
    """True als twee bestanden dezelfde inhoud hebben. Een koppeling naar de NAS telt dus NIET mee als
    'onze drop-in staat in de view'."""
    try:
        if os.path.getsize(a) != os.path.getsize(b):
            return False
        with open(a, "rb") as x, open(b, "rb") as y:
            return x.read() == y.read()
    except OSError:
        return False


def geplaatst(entry, view):
    """Het pad in de view waar deze drop-in hoort te staan."""
    return os.path.normpath(os.path.join(view, entry["to"], entry["file"]))


def op_de_nas(pad):
    """True als dit pad (via symbolische koppelingen) op de NAS uitkomt. Daar schrijven we nooit."""
    echt = os.path.realpath(pad)
    mnt = os.path.join(HOME, "mnt")
    if echt == mnt or echt.startswith(mnt + os.sep):
        return True
    try:
        with open("/proc/self/mounts") as f:
            best, soort = "", ""
            for regel in f:
                delen = regel.split()
                if len(delen) >= 3:
                    mp = delen[1].replace("\\040", " ")
                    if (echt == mp or echt.startswith(mp.rstrip("/") + "/")) and len(mp) > len(best):
                        best, soort = mp, delen[2]
            return soort in ("cifs", "smb3", "smbfs", "nfs", "nfs4", "fuse.sshfs")
    except OSError:
        return False


def _maak_map_lokaal(pad):
    """Vervangt een symbolische koppeling naar een map door een echte map met koppelingen naar dezelfde
    inhoud. Zo kan één bestand in de view een eigen bestand worden zonder dat de NAS wordt beschreven.
    Dat is dezelfde gedachte als scripts/launch-direct.py bij teknoparrot.ini en als link_game() in
    scripts/aagames-setup.py."""
    doel = os.path.realpath(pad)
    tijdelijk = pad + ".ravengun-lokaal"
    if os.path.exists(tijdelijk):
        shutil.rmtree(tijdelijk)
    os.makedirs(tijdelijk)
    for naam in os.listdir(doel):
        os.symlink(os.path.join(doel, naam), os.path.join(tijdelijk, naam))
    os.unlink(pad)
    os.replace(tijdelijk, pad)


def _zorg_lokale_map(view, to):
    """Maakt elk deel van `to` onder de view een echte map. Nodig voor een drop-in die in een submap
    van de game hoort die in de view nog een koppeling naar de NAS is (bij Rabbids Hollywood:
    Game_Data/Plugins; de game laadt matrix64.dll daar vandaan, niet uit de hoofdmap)."""
    huidig = view
    for deel in [d for d in to.replace("\\", "/").split("/") if d not in ("", ".")]:
        huidig = os.path.join(huidig, deel)
        if os.path.islink(huidig):
            _maak_map_lokaal(huidig)
        elif not os.path.isdir(huidig):
            os.makedirs(huidig, exist_ok=True)


def plaats(game, view, entries):
    """Zet de drop-ins in de view. Geeft (geplaatst, fouten). Een symlink wordt een eigen lokaal bestand;
    er wordt nooit naar de NAS geschreven."""
    uit, fouten = [], []
    for e in entries:
        doel = geplaatst(e, view)
        try:
            _zorg_lokale_map(view, e["to"])
        except OSError as ex:
            fouten.append("kan %s niet lokaal maken in de view: %s" % (e["to"], ex))
            continue
        if op_de_nas(os.path.dirname(doel)):
            fouten.append("het doel %s komt op de NAS uit; daar wordt nooit geschreven" % doel)
            continue
        try:
            os.makedirs(os.path.dirname(doel), exist_ok=True)
        except OSError as ex:
            fouten.append("kan %s niet maken: %s" % (os.path.dirname(doel), ex))
            continue
        try:
            if os.path.islink(doel):
                os.unlink(doel)       # een symlink in de view wordt ons eigen bestand
            elif os.path.isfile(doel) and zelfde_bestand(doel, e["pad"]):
                uit.append(doel)
                kopieer_gegevens(e, view, uit, fouten)
                continue
            tmp = doel + ".ravengun-tmp"
            shutil.copyfile(e["pad"], tmp)
            os.replace(tmp, doel)
            uit.append(doel)
            kopieer_gegevens(e, view, uit, fouten)
        except OSError as ex:
            fouten.append("kan %s niet plaatsen: %s" % (doel, ex))
    return uit, fouten


def kopieer_gegevens(entry, view, uit, fouten):
    """Zet de gegevensmap van deze drop-in naast de bibliotheek in de view, zodat de bibliotheek hem
    vindt met een relatieve RAVENGUN_MATRIX_DIR (zoals 'matrix'). De gegevens horen naast de
    bibliotheek te staan (docs/geen-dongle-in-de-loader.md) en dit is de plek waar de bibliotheek
    zoekt: naast zichzelf."""
    d = entry.get("data")
    if not d or not d.get("aanwezig"):
        return
    doel = os.path.join(view, entry["to"], os.path.basename(d["pad"].rstrip("/")))
    try:
        shutil.copytree(d["pad"], doel, dirs_exist_ok=True, symlinks=False)
        uit.append(doel)
    except OSError as ex:
        fouten.append("kan de gegevensmap %s niet naar %s kopiëren: %s" % (d["pad"], doel, ex))


def status(game=None, view_override=None, plaatsen=False):
    """De volledige status als dict."""
    uit = {
        "ok": False, "game": game, "dirs": patches_dirs(), "dir": None, "dropin": [],
        "overrides": "", "env": {}, "view": None, "placed": [], "aanwezig": [],
        "installed": False, "build": {}, "data": [], "errors": [], "message": "",
    }
    if not game:
        uit["message"] = "geen --game opgegeven; gebruik: scripts/matrix.py --game <Game> --json"
        return uit

    d = game_dir(game)
    uit["dir"] = d
    if not os.path.isdir(d):
        uit["message"] = "geen map voor '%s' (verwacht: %s)" % (game, d)
        return uit

    entries, fouten = lees_dropin(game)
    uit["errors"] = list(fouten)
    uit["dropin"] = [{k: e[k] for k in ("file", "to", "override", "env", "preload")} for e in entries]
    uit["data"] = [e["data"] for e in entries if e.get("data")]
    uit["overrides"] = wineliboverride(entries)
    uit["env"] = dropin_env(entries)

    for naam in DLL_NAMES:
        p = os.path.join(out_dir(), naam)
        uit["build"][naam] = {"pad": p, "aanwezig": os.path.isfile(p)}

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
                if os.path.exists(doel):
                    uit["aanwezig"].append(doel)
                    if zelfde_bestand(doel, e["pad"]):
                        uit["placed"].append(doel)

    if uit["errors"]:
        uit["message"] = "de Matrix-drop-in van '%s' is niet in orde: %s" % (game, "; ".join(uit["errors"][:3]))
        return uit

    uit["ok"] = True
    if uit["placed"]:
        uit["message"] = ("Matrix-drop-in van '%s' in orde; hij staat in %s; WINEDLLOVERRIDES=%s"
                          % (game, view, uit["overrides"] or "(geen)"))
    else:
        uit["message"] = ("Matrix-drop-in van '%s' in orde; hij staat nog niet in %s "
                          "(plaatsen: scripts/matrix.py --game %s --install); WINEDLLOVERRIDES=%s"
                          % (game, view, game, uit["overrides"] or "(geen)"))
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
        for dd in st["data"]:
            print("  gegevens: %s (matrix.ini: %s)" % (dd["pad"], "ja" if dd["ini_aanwezig"] else "nee"))
        if st["placed"]:
            print("  %s: %s" % ("geplaatst" if st["installed"] else "onze drop-in staat in de view",
                                ", ".join(st["placed"])))
        elif st["aanwezig"]:
            print("  in de view staat nog een ander bestand dan onze drop-in: %s" % ", ".join(st["aanwezig"]))
        if st["overrides"]:
            print("  WINEDLLOVERRIDES: %s" % st["overrides"])
        for k, v in sorted(st["env"].items()):
            print("  omgeving: %s=%s" % (k, v))
    return 0 if st["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

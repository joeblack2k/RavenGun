#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# De eigen HASP-emulator van RavenGun: de dongle-soort "hasp" uit patches/<Game>/dongle.json.
#
# Dit is de tegenhanger van scripts/dongle.py. Waar dongle.py kijkt of de ECHTE, fysieke dongle van de eigenaar
# aangesloten is (USB pass-through) en of de officiele runtime van de fabrikant draait, kijkt dit script naar ONZE
# EIGEN emulator: is de bibliotheek er, is de donglebeschrijving van deze game er, en kloppen de geheugenbestanden.
#
# De gegevens staan in de map van de game zelf (docs/dropins.md), niet in een globale boom:
#
#   patches/<Game>/dongle.json         WELKE dongle deze game vraagt + de gegevens (kind = hasp)
#   patches/<Game>/dongle/memory/<id>  de geheugenimages; het pad in dongle.json is relatief aan patches/<Game>/
#
# De gebruikerslaag (~/.local/share/ravengun/patches/<Game>/) gaat voor op de meegeleverde map.
#
# De emulator zelf is NIET game-specifiek en staat in de bibliotheekmap:
#
#   ~/.local/share/ravengun/hasp/ravengun-hasp.so      32-bits, de Linux-games
#   ~/.local/share/ravengun/hasp/ravengun-hasp-x64.so  64-bits, de Linux-games
#   ~/.local/share/ravengun/hasp/windows/*.dll         de PE-DLL voor de Windows-games (Proton)
#
# De Linux-bibliotheek komt in het ELF-proces van de game. Dat mag NIET via LD_PRELOAD in de omgeving van de
# start: deze launcher start altijd Wine, en Wine geeft zijn omgeving door aan de Cygwin-lader van BudgieLoader,
# die aan een LD_PRELOAD-regel sterft (gemeten; docs/hasp-layer.md). Het pad gaat daarom mee als
# RG_DROPIN_PRELOAD; de native route en de eigen lader zetten dat in LD_PRELOAD van het ELF-proces zelf. Staat er
# een drop-in in patches/<Game>/dropin/ (docs/dropins.md), dan plaatst de launcher die in de view en bepaalt
# dropin.ini de rest; deze module meldt dan alleen waar de dongle staat.
#
# De bestandsnaam van een geheugenbestand is de 32-bits binaire fileid (hoogste bit eerst), zoals de HASP-runtime
# hem opvraagt: fileid 65524 -> 00000000000000001111111111110100. De hexvorm (fff4) wordt ook geaccepteerd.
#
# De API (tien hasp_-functies) en het dongle-formaat staan in docs/hasp-layer.md; de bron van de emulator staat in
# hasp/src/ (dongle.c, dongle.h, hasp_preload.c, hasp_preload_win.c, selftest.c); scripts/build-hasp.sh bouwt hem.
#
# Gebruik:
#   scripts/hasp.py [--game <Game>] [--json] [--bits 32|64] [--windows] [--list]
#     zonder --game: alleen kijken of de emulator er is
#     met --game:    ook de donglebeschrijving van die game controleren
#     --list:        alle games met een dongle.json op een rij
#     --bits:        de breedte van de game (bepaalt welke .so); standaard 64
#     --windows:     de game is een Windows-programma (de PE-DLL in plaats van LD_PRELOAD)
#   Exit 0 = in orde, 1 = niet in orde.
#
# Voor tests: RAVENGUN_PATCHES_DIR (de meegeleverde laag), RAVENGUN_USER_PATCHES_DIR (de gebruikerslaag),
#             RAVENGUN_HASP_LIB_DIR (waar de emulator staat).
import json
import os
import re
import sys

sys.dont_write_bytecode = True

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOME = os.environ.get("HOME", "/home/deck")

# De namen van de emulator, per doel.
EMULATOR_LINUX32 = "ravengun-hasp.so"
EMULATOR_LINUX64 = "ravengun-hasp-x64.so"
EMULATOR_WINDOWS = "hasp_windows_x64.dll"

# Het donglesoort dat dit script afhandelt. Andere soorten horen bij hun eigen script (docs/dropins.md).
KIND = "hasp"
KIND_SCRIPTS = {"hasp": "scripts/hasp.py", "matrix": "scripts/matrix.py",
                "region": "scripts/region.py", "amdaemon": "scripts/amdaemon.py"}

# De twee vormen van een fileid als bestandsnaam.
_BINARY_NAME = re.compile(r"^[01]{32}$")
_HEX_NAME = re.compile(r"^[0-9a-fA-F]{1,8}$")

# De velden die een dongle.json ten minste moet hebben.
REQUIRED_FIELDS = ("game",)


# ---------------------------------------------------------------------------------------------------------------------
# Mappen

def patches_dirs():
    """De mappen met de games, in volgorde: de gebruikerslaag eerst (die wint), dan de meegeleverde map."""
    uit = []
    for d in (os.environ.get("RAVENGUN_USER_PATCHES_DIR"),
              os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.join(HOME, ".local/share"), "ravengun", "patches"),
              os.environ.get("RAVENGUN_PATCHES_DIR") or os.path.join(ROOT, "patches")):
        if d and d not in uit:
            uit.append(d)
    return uit


def emulator_dirs():
    """Waar de emulator staat, in volgorde: de gebruikersmap eerst."""
    uit = []
    for d in (os.environ.get("RAVENGUN_HASP_LIB_DIR"), os.environ.get("RAVENGUN_USER_HASP_DIR"),
              os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.join(HOME, ".local/share"), "ravengun", "hasp"),
              os.path.join(ROOT, "hasp", "build"), os.path.join(ROOT, "hasp")):
        if d and d not in uit:
            uit.append(d)
    return uit


def _first_existing(rel):
    for d in emulator_dirs():
        p = os.path.join(d, rel)
        if os.path.exists(p):
            return p
    return None


def find_emulator(bits=64, windows=False):
    """Het pad naar de emulator voor deze breedte, of None.
    32-bits Linux -> ravengun-hasp.so; 64-bits Linux -> ravengun-hasp-x64.so; Windows -> de PE-DLL."""
    namen = []
    if windows:
        namen = [EMULATOR_WINDOWS, "hasp_windows_x64_105293.dll"]
    elif bits == 32:
        namen = [EMULATOR_LINUX32]
    else:
        namen = [EMULATOR_LINUX64]
    for naam in namen:
        p = _first_existing(naam)
        if p:
            return p
    if windows:   # de PE-DLL's staan in de bouwmap onder windows/
        for naam in namen:
            p = _first_existing(os.path.join("windows", naam))
            if p:
                return p
    return None


def windows_dlls():
    """De gebouwde PE-DLL's (naam -> pad)."""
    uit = {}
    for d in emulator_dirs():
        for sub in (os.path.join(d, "windows"), d):
            if not os.path.isdir(sub):
                continue
            for naam in sorted(os.listdir(sub)):
                if naam.startswith("hasp_windows") and naam.endswith(".dll") and naam not in uit:
                    uit[naam] = os.path.join(sub, naam)
    return uit


def emulator_bits(path):
    """32 of 64, uit de ELF-header. None als het geen ELF is (dan is het een Windows-DLL)."""
    try:
        with open(path, "rb") as f:
            kop = f.read(20)
    except OSError:
        return None
    if len(kop) < 20 or kop[:4] != b"\x7fELF":
        return None
    if kop[4] == 1:
        return 32
    if kop[4] == 2:
        return 64
    return None


# ---------------------------------------------------------------------------------------------------------------------
# De dongle van een game

def _norm(naam):
    """Een naam vergelijkbaar maken: kleine letters, zonder spaties, streepjes en lage streepjes."""
    return re.sub(r"[\s_-]+", "", str(naam or "")).lower()


def dongle_json(name):
    """(map van de game, pad van dongle.json, inhoud) van de eerste laag die er een heeft. Anders (None, None, None).
    `name` mag de mapnaam zijn (patches/<Game>/) of het veld "game" uit dongle.json."""
    gezocht = _norm(name)
    for d in patches_dirs():
        direct = os.path.join(d, str(name), "dongle.json")
        if os.path.isfile(direct):
            with open(direct, encoding="utf-8") as f:
                return os.path.join(d, str(name)), direct, json.load(f)
    # geen map met die naam: elke map met een dongle.json lezen en op het veld "game" vergelijken
    for d in patches_dirs():
        if not os.path.isdir(d):
            continue
        for sub in sorted(os.listdir(d)):
            if sub.startswith("_"):      # _template en _defaults zijn geen games
                continue
            pad = os.path.join(d, sub, "dongle.json")
            if not os.path.isfile(pad):
                continue
            try:
                with open(pad, encoding="utf-8") as f:
                    desc = json.load(f)
            except (OSError, ValueError):
                continue
            if _norm(desc.get("game", "")) == gezocht or _norm(sub) == gezocht:
                return os.path.join(d, sub), pad, desc
    return None, None, None


def games_met_dongle():
    """Alle games met een dongle.json: [(map, inhoud)] met de gebruikerslaag die de meegeleverde overschrijft."""
    uit, gezien = [], set()
    for d in patches_dirs():
        if not os.path.isdir(d):
            continue
        for sub in sorted(os.listdir(d)):
            if sub.startswith("_"):      # _template en _defaults zijn geen games
                continue
            if sub in gezien:
                continue
            pad = os.path.join(d, sub, "dongle.json")
            if not os.path.isfile(pad):
                continue
            try:
                with open(pad, encoding="utf-8") as f:
                    uit.append((os.path.join(d, sub), json.load(f)))
                gezien.add(sub)
            except (OSError, ValueError):
                continue
    return uit


def _fileid_van_naam(naam):
    """De fileid uit de bestandsnaam, of None. Accepteert de 32-bits binaire vorm en de hexvorm."""
    if _BINARY_NAME.match(naam):
        try:
            return int(naam, 2)
        except ValueError:
            return None
    if _HEX_NAME.match(naam):
        try:
            return int(naam, 16)
        except ValueError:
            return None
    return None


def _veilig_pad(game_dir, rel):
    """Het pad van een bestand uit dongle.json, met de controle dat het binnen de map van de game blijft."""
    if os.path.isabs(rel):
        return None, "het pad %r is absoluut; paden in dongle.json zijn relatief aan %s" % (rel, game_dir)
    vol = os.path.normpath(os.path.join(game_dir, rel))
    if vol != os.path.normpath(game_dir) and not vol.startswith(os.path.normpath(game_dir) + os.sep):
        return None, "het pad %r wijst buiten de map van de game" % rel
    return vol, ""


def lees_dongle(name):
    """(map, beschrijving, foutenlijst). Leest patches/<Game>/dongle.json en controleert de geheugenbestanden."""
    game_dir, pad, desc = dongle_json(name)
    fouten = []
    if not pad:
        return None, None, ["geen dongle.json voor '%s' (verwacht: %s)"
                            % (name, " of ".join(os.path.join(d, str(name), "dongle.json") for d in patches_dirs()))]

    soort = str(desc.get("kind", "") or desc.get("type", "")).strip().lower()
    if not soort:
        fouten.append("%s: veld 'kind' ontbreekt" % pad)
    elif soort != KIND and soort not in ("hasp-hl",):
        fouten.append("%s: deze dongle is van het soort '%s'; die hoort bij %s"
                      % (pad, soort, KIND_SCRIPTS.get(soort, "een eigen laag")))

    for veld in REQUIRED_FIELDS:
        if not str(desc.get(veld, "")).strip():
            fouten.append("%s: veld '%s' ontbreekt" % (pad, veld))

    items = desc.get("memory", []) or []
    if not items:
        fouten.append("%s: geen 'memory'-items: de dongle-image van deze game is nog niet vastgelegd. Zet de "
                      "geheugenbestanden in %s/dongle/memory/ en noem ze in 'memory' (docs/hasp-layer.md)"
                      % (pad, game_dir))

    gebruikt = set()
    for item in items:
        bestand = str(item.get("file", "")).strip()
        if not bestand:
            fouten.append("%s: een memory-item heeft geen 'file'" % pad)
            continue
        vol, fout = _veilig_pad(game_dir, bestand)
        if fout:
            fouten.append("%s: %s" % (pad, fout))
            continue
        naam = os.path.basename(bestand)
        gebruikt.add(naam)
        if not os.path.isfile(vol):
            fouten.append("%s wijst naar '%s', dat er niet is" % (pad, bestand))
            continue
        fid = _fileid_van_naam(naam)
        if fid is not None and item.get("fileid") not in (None, fid):
            fouten.append("%s hoort bij fileid %d, maar dongle.json zegt %s" % (bestand, fid, item.get("fileid")))
        try:
            echte = os.path.getsize(vol)
        except OSError as e:
            fouten.append("%s is niet te lezen: %s" % (bestand, e))
            continue
        if echte == 0:
            fouten.append("%s is leeg" % bestand)
        elif item.get("size") not in (None, echte):
            fouten.append("%s is %d bytes, maar dongle.json zegt %s" % (bestand, echte, item.get("size")))

    # Ongebruikte bestanden in dongle/: een andere schrijfwijze van dezelfde fileid (hex naast binair) is goed.
    mem = os.path.join(game_dir, "dongle", "memory")
    if os.path.isdir(mem):
        for naam in sorted(os.listdir(mem)):
            if naam in gebruikt or not os.path.isfile(os.path.join(mem, naam)):
                continue
            fid = _fileid_van_naam(naam)
            dubbel = fid is not None and any(_fileid_van_naam(g) == fid for g in gebruikt)
            if not dubbel:
                fouten.append("dongle/memory/%s staat in de map maar niet in dongle.json" % naam)

    return game_dir, desc, fouten


# ---------------------------------------------------------------------------------------------------------------------
# Status en injectie

def status(game=None, bits=64, windows=False):
    """De volledige status: {"ok", "game_dir", "dongle", "emulator", "bits", "windows", "errors", "message", "injection"}."""
    uit = {"ok": False, "game": game, "game_dir": None, "dongle": None, "emulator": None, "bits": bits,
           "windows": bool(windows), "errors": [], "injection": None, "patches_dirs": patches_dirs(),
           "emulator_dirs": emulator_dirs()}

    emu = find_emulator(bits, windows)
    if not emu:
        naam = EMULATOR_WINDOWS if windows else (EMULATOR_LINUX32 if bits == 32 else EMULATOR_LINUX64)
        uit["message"] = ("geen emulator (%s) in %s. Bouw hem met scripts/build-hasp.sh."
                          % (naam, " of ".join(emulator_dirs())))
        return uit
    uit["emulator"] = emu
    uit["bits"] = emulator_bits(emu) or bits

    if not game:
        uit["ok"] = True
        uit["message"] = "RavenGun software HASP gevonden: %s." % emu
        return uit

    game_dir, desc, fouten = lees_dongle(game)
    uit["game_dir"] = game_dir
    uit["dongle"] = desc
    uit["errors"] = fouten
    if fouten:
        uit["message"] = "de emulator staat in %s, maar de dongle van '%s' is niet in orde: %s" % (
            os.path.dirname(emu), game, "; ".join(fouten[:3]))
        return uit

    uit["injection"] = injection(game_dir, emu, windows)
    n = len(desc.get("memory", []) or [])
    via = uit["injection"]["via"]
    hoe = {"dropin": "de drop-in in patches/%s/dropin/" % os.path.basename(game_dir),
           "preload": "RG_DROPIN_PRELOAD van %s (de native route en de eigen lader zetten dat in LD_PRELOAD "
                      "van het ELF-proces)" % emu,
           "geen": "niets: er staat geen PE-DLL in patches/%s/dropin/" % os.path.basename(game_dir)}.get(via, via)
    uit["ok"] = True
    uit["message"] = ("RavenGun software HASP voor '%s': %d geheugenbestand(en) uit %s; %s."
                      % (desc.get("game") or game, n, game_dir, hoe))
    if via == "geen":
        uit["ok"] = True
        uit["message"] += (" Zonder drop-in krijgt een Windows-game de emulator niet geladen; zet hem in "
                           "patches/%s/dropin/ met een sectie in dropin.ini (docs/dropins.md)."
                           % os.path.basename(game_dir))
    return uit


def injection(game_dir, emu, windows=False):
    """Wat de launcher moet zetten om deze emulator in het spel te krijgen.

    Geeft {"env", "preload", "dll", "files", "via"}.
    De emulator leest zelf RAVENGUN_HASP_DIR + RAVENGUN_HASP_GAME. Staat er een eigen drop-in in
    patches/<Game>/dropin/ (docs/dropins.md), dan zet de drop-in-motor van launch-direct.py dat bestand in de
    view en bepaalt dropin.ini of het via LD_PRELOAD of via WINEDLLOVERRIDES gaat; dan zetten wij hier geen
    LD_PRELOAD. Zonder drop-in laden we de emulator uit de bibliotheekmap rechtstreeks vooraf (Linux-games).
    """
    game_dir = os.path.normpath(game_dir)
    env = {"RAVENGUN_HASP_DIR": os.path.dirname(game_dir),
           "RAVENGUN_HASP_GAME": os.path.basename(game_dir),
           "RAVENGUN_HASP_VERBOSE": os.environ.get("RAVENGUN_HASP_VERBOSE", "1")}
    dropin = os.path.join(game_dir, "dropin")
    # Bij een Windows-game telt alleen de PE-DLL mee; bij een Linux-game alleen de ELF-bibliotheek.
    begin = "hasp_windows" if windows else "ravengun-hasp"
    eigen = sorted(n for n in (os.listdir(dropin) if os.path.isdir(dropin) else [])
                   if n.startswith(begin) and (n.endswith(".dll") if windows else n.endswith(".so")))
    uit = {"env": env, "preload": [], "dll": None, "files": [], "via": ""}
    if eigen:
        uit["via"] = "dropin"
        uit["files"] = [(os.path.join(dropin, n), n) for n in eigen]
        uit["dll"] = next((n for n in eigen if n.endswith(".dll")), None)
    elif not windows:
        uit["via"] = "preload"
        uit["preload"] = [emu]
        # Het pad gaat mee als RG_DROPIN_PRELOAD. De native route (scripts/play-native.sh) en de eigen
        # lader zetten dat in LD_PRELOAD van het ELF-proces; in de omgeving van Wine kan het niet, want
        # Wine geeft die door aan de Cygwin-lader van BudgieLoader, die eraan sterft (gemeten; docs/hasp-layer.md).
        env["RG_DROPIN_PRELOAD"] = emu
    else:
        uit["via"] = "geen"
    return uit


def preload_for(game, bits=64):
    """(waarde voor LD_PRELOAD, de game, fout). Leeg als de bron niet bruikbaar is. Blijft bestaan voor
    aanroepers van buiten (de oude naam); gebruikt de nieuwe status()."""
    st = status(game, bits=bits)
    if not st["ok"]:
        return "", "", st["message"]
    return st["emulator"], game, ""


# ---------------------------------------------------------------------------------------------------------------------

def main(argv):
    game, als_json, bits, windows, toon_lijst = None, False, 64, False, False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--json":
            als_json = True
        elif a == "--list":
            toon_lijst = True
        elif a == "--windows":
            windows = True
        elif a == "--bits" and i + 1 < len(argv):
            bits = int(argv[i + 1]); i += 1
        elif a == "--game" and i + 1 < len(argv):
            game = argv[i + 1]; i += 1
        elif a in ("-h", "--help"):
            print(__doc__); return 0
        else:
            print("onbekende optie: %s" % a, file=sys.stderr); return 2
        i += 1

    if toon_lijst:
        rijen = []
        for map_, desc in games_met_dongle():
            rijen.append({"game": desc.get("game") or os.path.basename(map_), "dir": map_,
                          "kind": desc.get("kind") or desc.get("type") or "",
                          "memory": len(desc.get("memory", []) or [])})
        if als_json:
            print(json.dumps(rijen, ensure_ascii=False, indent=1))
        else:
            for r in rijen:
                print("%-28s %-8s %d  %s" % (r["game"], r["kind"], r["memory"], r["dir"]))
        return 0

    st = status(game, bits=bits, windows=windows)
    if als_json:
        print(json.dumps(st, ensure_ascii=False, indent=1))
    else:
        print(st["message"])
        for e in st["errors"]:
            print("  - %s" % e)
    return 0 if st["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

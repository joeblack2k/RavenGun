#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""
De regio/PCB-laag van RavenGun: welke game vraagt een regio- en printplaat-dongle, welke waarden
horen daarbij, en is die waarde vandaag ook echt een blokkade?

De laag is gegevens, geen binary. Een game die een regio-dongle vraagt heeft de beschrijving in de
drop-in van zijn eigen map staan, naast de rest van de laag (docs/dropins.md en
docs/geen-dongle-in-de-loader.md):

patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json

De oudere losse plaats patches/<Game>/dongle.json blijft ook werken (die staat er al voor Halo).

{
"kind": "region",
"region": { "field": "DongleRegion", "name": "JAPAN", "game_name": "JPN" },
"pcb":    { "field": "PcbRegion",    "name": "JAPAN", "game_name": "JPN" },
"enforced": false
}

Aanwezigheid van dat bestand betekent: deze game vraagt een dongle. Geen bestand = geen dongle.
Dit script leest dat bestand, toetst de waarden aan de keuzelijst van het TeknoParrot-profiel, aan
game.ini en aan de game-executable zelf, en zegt wat de laag wel en niet doet.

Er zijn drie verschillende dingen die in dit ecosysteem "regio" heten. Ze hebben niets met elkaar
te maken en dit script houdt ze uit elkaar:

dongle      de Sega Ring/Lindbergh-dongle: de game roept amDongleGetRegion() aan (LGI, LGI3D,
LGS, Operation G.H.O.S.T.). De waarden staan in de drop-in van de game.
gun-ini     de Taito Type X2-geweren: de game leest een REGION-regel uit zijn eigen ini
(MINIGUN.INI, MUSEUM.INI, HAUNTED2.INI) â€” GaiaAttack4, HauntedMuseum(I/II).
Dat is geen dongle; de launcher schrijft die regel uit het profielveld Region.
core-region het profielveld Region van de 64-bits kern (WartranTroopers/amdaemon-route),
o.a. RainbowBombergirl (Region = EXPORT). Ook geen dongle.

Gebruik:
scripts/region.py [--game <Game>] [--json]
zonder --game: alle games met een dongle.json van het soort region
met --game:    die ene game (met of zonder dongle.json)
Exit 0 = in orde, 1 = niet in orde, 2 = verkeerd gebruik.

Voor tests: RAVENGUN_LOADER_PATCHES_DIR, RAVENGUN_USER_PATCHES_DIR, RAVENGUN_PATCHES_DIR (volgorde: docs/per-game-patches.md,
"Zoekvolgorde"), RAVENGUN_TP_DIR, RAVENGUN_GAME_DIR.
"""

import json
import os
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ownprofile as op  # noqa: E402  (dezelfde gamemap-resolver als de launcher)
import gamesettings as gs  # noqa: E402  (de ene zoekvolgorde van patches/<Game>/)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOME = os.environ.get("HOME", "/home/deck")

# De soorten die dongle.json kent (docs/dropins.md). Alleen "region" gaat dit script aan.
DONGLE_KINDS = ("hasp", "matrix", "region", "amdaemon")

# De vijf regiowaarden die de TeknoParrot-profielen bieden en de vier namen die de games zelf
# gebruiken. De games kennen alleen JPN, USA, EXP en CHN (uit hun eigen foutmeldingen); JAP2N heeft
# geen eigen naam in de game en wordt hier als JPN-variant gemeld, met de aantekening dat dat een
# aanname is en geen meting.
REGION_GAME_NAMES = {"JAPAN": "JPN", "USA": "USA", "EXPORT": "EXP", "CHINA": "CHN", "JAP2N": "JPN"}

# Waaraan je in de game-executable ziet dat hij de dongle echt opvraagt. De eerste groep is de
# amDongle-API zelf, de tweede groep de leesroutes van fwAM (LGI/LGI3D) en de derde de uitkomst
# waarmee de game verdergaat als het antwoord uitblijft.
MARKERS_DONGLE = (
    b"amDongleGetRegion", b"amDongleOpen", b"amDongleSetupKeychip", b"amDongleGetModelType",
    b"amDongleGetPlatformId", b"amDongleGetGameId", b"amDongleBillingGetKeychipId",
    b"amDongleSendAndRecvEx", b"amDongle Ver.",
)
MARKERS_READ = (
    b"readRegionEeprom", b"readRegionBuSram", b"setupRegionEeprom", b"setupRegionBuSram",
    b"keychip.appboot.region",
)
MARKERS_FALLBACK = (b"DefaultRegion", b"Region is JPN.", b"Region is USA.", b"Region is EXP.",
                    b"Region is CHN.", b"Keychip Not Found", b"Dongle::initialize()")
MARKERS_GUN_INI = (b"REGION", b"MINIGUN.INI", b"MUSEUM.INI", b"MUSEUM.ini", b"HAUNTED2.INI", b"CNFNAME")

# Dezelfde merkers als tekst, om mee te vergelijken: de scan levert namen, geen bytes.
def _namen(groep):
    return tuple(m.decode("ascii", "replace") for m in groep)


MARKERS_DONGLE_N = _namen(MARKERS_DONGLE)
MARKERS_READ_N = _namen(MARKERS_READ)
MARKERS_GUN_INI_N = _namen(MARKERS_GUN_INI)

# De ini waarin de Taito Type X2-geweren hun REGION-regel lezen, per profiel.
GUN_INI = {
    "GaiaAttack4": "MINIGUN.INI",
    "HauntedMuseum": "MUSEUM.INI",
    "HauntedMuseum2": "HAUNTED2.INI",
}

# Wat er in de gedecrypteerde beelden van de TeknoParrot-kernen staat. Dit is de kern van de
# beoordeling: een kern die de regiokeys niet eens bij naam kent, kan ze niet gebruiken.
#
# Bron: cores/dump/*_TeknoParrot_dll.bin en *_TeknoParrot64_dll.bin (docs/14_unpacking.md).
CORE_EVIDENCE = {
    "TeknoParrot.dll (32-bits, Ring/Lindbergh/Type X2)": {
        "heeft": ["teknoparrot.ini", "FreePlay", "Windowed", "HideCursor", "Enable Outputs",
                  "FFB Blaster", "SEGA CORPORATION;I/O BD JVS;837-14572;Ver1.00;2005/10"],
        "mist": ["DongleRegion", "PcbRegion", "Region", "amDongle", "amEeprom", "amSram",
                 "JAPAN", "USA", "JAP2N", "EXPORT", "CHINA"],
        "regio_afgehandeld": False,
    },
    "TeknoParrot64.dll (64-bits, ALLS/amdaemon)": {
        "heeft": ["Region", "JAPAN", "EXPORT", "System_getRegionCode", "AllnetAuth_getRegionCode",
                  "amDongleGetNetworkAddress: %d", "System\\Sega\\SystemProperty\\keychip"],
        "mist": ["DongleRegion", "PcbRegion"],
        "regio_afgehandeld": True,
    },
}

# Welke kern een game krijgt: de 64-bits games gaan naar de 64-bits kern, de rest naar de 32-bits.
CORE_32 = "TeknoParrot.dll (32-bits, Ring/Lindbergh/Type X2)"
CORE_64 = "TeknoParrot64.dll (64-bits, ALLS/amdaemon)"


# ---------------------------------------------------------------------------------------------------------------------
# Mappen

def patches_dirs():
    """De mappen waarin gezocht wordt voor "eerste treffer wint": gamesettings.patches_roots() omgekeerd (ontwikkel-checkout,
    gebruiker, patches/ van de loader). Geen eigen kopie van de volgorde of van de omgevingsvariabelen."""
    return list(reversed(gs.patches_roots()))


def find_in_patches(game, rel):
    """Het eerste bestaande <patches>/<Game>/<rel>, of None. De gebruikerslaag gaat voor."""
    for d in patches_dirs():
        g = gs.find_game_dir(d, game)
        p = os.path.join(g, rel) if g else ""
        if p and os.path.exists(p):
            return p
    return None


# De laagmap onder dropin/ waarin een donglebeschrijving staat (docs/geen-dongle-in-de-loader.md:
# patches/<Game>/dropin/<laag>/dongle.json, naast de bibliotheek die hem leest).
DONGLE_LAAG = "ravengun-hasp"


def dongle_json_pad(game):
    """Het dongle.json van deze game, of None. Eerst de drop-in van de game
    (patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json), dan de oudere losse plaats
    patches/<Game>/dongle.json. De gebruikerslaag gaat per plaats voor op de meegeleverde map."""
    for d in patches_dirs():
        g = gs.find_game_dir(d, game)
        for rel in (os.path.join("dropin", DONGLE_LAAG, game, "dongle.json"), "dongle.json"):
            p = os.path.join(g, rel) if g else ""
            if p and os.path.isfile(p):
                return p
    return None


def heeft_dongle(game):
    """Of deze game een donglebeschrijving heeft (op een van de twee plaatsen)."""
    return dongle_json_pad(game) is not None


def find_tp_dir():
    """De TeknoParrot-runtime (met GameProfiles/ en UserProfiles/). $RAVENGUN_TP_DIR gaat voor."""
    env = os.environ.get("RAVENGUN_TP_DIR", "")
    if env and os.path.isdir(env):
        return env
    runtime = os.path.join(HOME, ".local/share/ravengun/runtime")
    kandidaten = []
    if os.path.isdir(runtime):
        for naam in sorted(os.listdir(runtime)):
            p = os.path.join(runtime, naam)
            if naam.startswith("TeknoParrotUi") and os.path.isdir(os.path.join(p, "GameProfiles")):
                kandidaten.append(p)
    return kandidaten[-1] if kandidaten else None


# ---------------------------------------------------------------------------------------------------------------------
# Lezen

def read_ini(path):
    """Een ini als {sectie: {sleutel: waarde}}. Commentaar: een regel die met # of ; begint, of een ;
    na witruimte achter een waarde. Sectie- en sleutelnamen blijven zoals ze staan."""
    uit = {}
    sectie = ""
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            regels = f.read().splitlines()
    except OSError:
        return uit
    for regel in regels:
        r = regel.strip()
        if not r or r[0] in "#;":
            continue
        if r.startswith("[") and r.endswith("]"):
            sectie = r[1:-1].strip()
            uit.setdefault(sectie, {})
            continue
        if "=" not in r:
            continue
        k, v = r.split("=", 1)
        if " ;" in v:
            v = v.split(" ;", 1)[0]
        uit.setdefault(sectie, {})[k.strip()] = v.strip()
    return uit


def read_game_ini(game):
    """game.ini uit de meegeleverde laag en de gebruikerslaag samengevoegd: de gebruikerslaag wint per
    sleutel (dezelfde regel als scripts/gamesettings.py)."""
    uit = {}
    for d in reversed(patches_dirs()):
        p = gs.game_ini_path(game, d)
        if not p or not os.path.isfile(p):
            continue
        for sectie, kv in read_ini(p).items():
            uit.setdefault(sectie, {}).update(kv)
    return uit


def profile_fields(tp, profile):
    """De ConfigValues van een profiel als {veldnaam: {"value", "type", "options", "category"}}.
    Het gebruikersprofiel gaat voor op het meegeleverde sjabloon."""
    if not tp:
        return {}
    for sub in ("UserProfiles", "GameProfiles"):
        p = os.path.join(tp, sub, profile + ".xml")
        if not os.path.isfile(p):
            continue
        try:
            root = ET.parse(p).getroot()
        except ET.ParseError:
            continue
        uit = {}
        for fi in root.iter("FieldInformation"):
            naam = (fi.findtext("FieldName") or "").strip()
            if not naam:
                continue
            uit[naam] = {
                "value": (fi.findtext("FieldValue") or "").strip(),
                "type": (fi.findtext("FieldType") or "").strip(),
                "options": [s.text.strip() for s in fi.iter("string") if s.text],
                "category": (fi.findtext("CategoryName") or "").strip(),
                "source": os.path.join(sub, profile + ".xml"),
            }
        return uit
    return {}


def profile_scalars(tp, profile):
    """De losse velden van een profiel (GamePath, ExecutableName, EmulatorType, EmulationProfile).
    Het gebruikersprofiel gaat voor op het meegeleverde sjabloon."""
    if not tp:
        return {}
    for sub in ("UserProfiles", "GameProfiles"):
        p = os.path.join(tp, sub, profile + ".xml")
        if not os.path.isfile(p):
            continue
        try:
            root = ET.parse(p).getroot()
        except ET.ParseError:
            continue
        uit = {}
        for kind in root:
            if kind.tag in ("ConfigValues", "JoystickButtons", "GameProfile"):
                continue
            if len(kind) == 0 and kind.text is not None:
                uit[kind.tag] = kind.text.strip()
        uit["_source"] = os.path.join(sub, profile + ".xml")
        return uit
    return {}


# ---------------------------------------------------------------------------------------------------------------------
# Wijziging van buiten deze sessie (2026-10-12, de patches-sessie). Voor de Claude-sessie in deze repo:
# game_dir() zocht de map van de game alleen via het TeknoParrot-profiel ([Game] GamePath of het GamePath van
# GameProfiles/<P>.xml). Voor een game zonder UserProfiles/<P>.xml en zonder GamePath in het sjabloon vindt dit
# script de exe niet, en dan luidt het oordeel ROOD ("de executable noemt geen enkele amDongle- of
# regioleesroute") terwijl er met de game niets mis is. OG is daar het voorbeeld van: de map op de NAS heet
# OAV6DU~3 en het profiel is leeg.
# De launcher lost dat al op met scripts/ownprofile.py: locate_game() kijkt naar [Game] GamePath van de
# gebruiker, dan naar een lokale view, dan naar de gamemappen aan de hand van [Identify]. Dat is nu de eerste
# weg die dit script probeert, zodat er een resolver is in plaats van twee. De oude terugval blijft staan voor
# het geval de bibliotheek van de app niet te vinden is.
# Dit is GEEN dongle- of HASP-werk en er verandert niets aan wat een dongle antwoordt: alleen welke map als de
# map van de game geldt. Je hoeft hier niets mee te doen.
# ---------------------------------------------------------------------------------------------------------------------
def game_dir(game, settings, scalars):
    """De map van de game-exe. $RAVENGUN_GAME_DIR gaat voor, dan dezelfde weg als de launcher
    (ownprofile.locate_game: een afwijkend pad van de gebruiker, een view, of de gamemappen), dan het
    GamePath van het profiel. None als geen van die wegen iets vindt."""
    env = os.environ.get("RAVENGUN_GAME_DIR", "")
    if env and os.path.isdir(env):
        return env
    try:
        pad, bron = op.locate_game(game, settings)
        if pad and os.path.isfile(pad):
            return os.path.dirname(pad)
    except Exception:   # de gamemappen zijn optioneel; dit mag het oordeel nooit stukmaken
        pass
    for pad in (settings.get("Game", {}).get("GamePath", ""), scalars.get("GamePath", "")):
        if pad and os.path.isfile(pad):
            return os.path.dirname(pad)
    return None


def exe_path(settings, scalars, gamedir):
    """Het pad naar de game-executable, of None. De naam komt uit game.ini, anders uit het profiel."""
    naam = settings.get("Game", {}).get("Executable", "") or scalars.get("ExecutableName", "")
    if gamedir and naam:
        p = os.path.join(gamedir, naam)
        if os.path.isfile(p):
            return p
    return None


def scan_markers(path, markers):
    """Welke van de merkers staan in het bestand, met hun aantal. Leest in brokken van 4 MiB."""
    uit = {}
    if not path or not os.path.isfile(path):
        return uit
    rest = b""
    try:
        with open(path, "rb") as f:
            while True:
                blok = f.read(4 << 20)
                if not blok:
                    break
                buf = rest + blok
                for m in markers:
                    n = buf.count(m)
                    if n:
                        uit[m.decode("ascii", "replace")] = uit.get(m.decode("ascii", "replace"), 0) + n
                rest = buf[-64:]
    except OSError:
        return uit
    return uit


def lees_dongle(game):
    """(beschrijving, pad, foutenlijst) van het dongle.json van de game (drop-in, anders de losse plaats)."""
    pad = dongle_json_pad(game)
    if not pad:
        return None, None, []
    try:
        with open(pad, encoding="utf-8") as f:
            desc = json.load(f)
    except (OSError, ValueError) as e:
        return None, pad, ["kan %s niet lezen: %s" % (pad, e)]
    if not isinstance(desc, dict):
        return None, pad, ["%s is geen JSON-object" % pad]
    return desc, pad, []


# ---------------------------------------------------------------------------------------------------------------------
# Beoordelen

def core_for(exe):
    """Welke kern deze game krijgt: de 64-bits kern als de executable 64-bits is, anders de 32-bits.
    Zonder executable gaan we uit van de 32-bits kern (de Ring/Lindbergh-games)."""
    pad = exe or ""
    if pad and os.path.isfile(pad):
        try:
            with open(pad, "rb") as f:
                f.seek(0x3c)
                off = int.from_bytes(f.read(4), "little")
                f.seek(off + 4 + 20)
                magic = int.from_bytes(f.read(2), "little")
            return CORE_64 if magic == 0x20b else CORE_32
        except OSError:
            pass
    return CORE_32


def mechanisme(desc, fields, merkers):
    """Welk regiomechanisme deze game gebruikt: dongle, gun-ini, core-region of geen."""
    if desc and desc.get("kind") == "region":
        return "dongle"
    if any(m in merkers for m in MARKERS_DONGLE_N):
        return "dongle"
    if "Region" in fields:
        if any(m in merkers for m in MARKERS_GUN_INI_N):
            return "gun-ini"
        return "core-region"
    return "geen"


def status(game=None):
    """De volledige status van de regiolaag: {"ok", "game", "mechanisme", "dongle", ...}."""
    uit = {"ok": False, "game": game, "mechanisme": None, "dongle": None, "dongle_path": None,
           "patches_dirs": patches_dirs(), "fields": {}, "merkers": {}, "core": None,
           "core_regio_afgehandeld": None, "enforced": None, "errors": [], "message": ""}

    tp = find_tp_dir()
    uit["tp"] = tp

    if not game:
        return samenvatting(uit)

    settings = read_game_ini(game)
    profiel = settings.get("Game", {}).get("Profile", "") or game
    fields = profile_fields(tp, profiel)
    scalars = profile_scalars(tp, profiel)
    uit["profile"] = profiel
    uit["profile_scalars"] = scalars
    gamedir = game_dir(game, settings, scalars)
    exe = exe_path(settings, scalars, gamedir)
    uit["game_dir"] = gamedir
    uit["exe"] = exe

    merkers = {}
    for groep in (MARKERS_DONGLE, MARKERS_READ, MARKERS_FALLBACK, MARKERS_GUN_INI):
        merkers.update(scan_markers(exe, groep))
    uit["merkers"] = merkers

    desc, pad, fouten = lees_dongle(game)
    uit["dongle"] = desc
    uit["dongle_path"] = pad
    uit["errors"] += fouten

    uit["mechanisme"] = mechanisme(desc, fields, merkers)

    # De regiovelden van het profiel: wat het profiel zelf zegt en wat game.ini kiest.
    for veld in ("DongleRegion", "PcbRegion", "Region"):
        if veld in fields or settings.get("Config", {}).get("General/" + veld):
            f = fields.get(veld, {})
            uit["fields"][veld] = {
                "profiel": f.get("value", ""),
                "opties": f.get("options", []),
                "game_ini": settings.get("Game", {}).get(veld, ""),
                "config": settings.get("Config", {}).get("General/" + veld, ""),
                "config_opties": (settings.get("ConfigOptions", {}).get("General/" + veld, "") or "").split("|"),
            }

    if uit["mechanisme"] == "geen":
        # Een game die helemaal niet bestaat zou stil "in orde" melden; dat mag niet.
        if not settings and not desc:
            uit["errors"].append("onbekende game: geen patches/%s/game.ini en geen dongle.json in zijn drop-in"
                                 % game)
            uit["message"] = ("%s is onbekend: er is geen game.ini en geen dongle.json in %s."
                              % (game, " of ".join(patches_dirs())))
            return uit
        uit["ok"] = True
        uit["message"] = ("%s heeft geen regio/PCB-dongle: geen dongle.json en geen regioveld in het "
                          "profiel." % game)
        return uit

    if uit["mechanisme"] == "gun-ini":
        veld = uit["fields"].get("Region", {})
        waarde = veld.get("game_ini") or veld.get("profiel") or ""
        ini = [m for m in ("MINIGUN.INI", "MUSEUM.INI", "MUSEUM.ini", "HAUNTED2.INI") if m in merkers]
        uit["gun_ini"] = ini[0] if ini else GUN_INI.get(profiel, "")
        uit["ok"] = bool(waarde)
        uit["message"] = ("%s leest de regio uit zijn eigen ini (%s) als REGION-regel: geen dongle maar "
                          "een bestand. Het profielveld Region staat op %r en de launcher schrijft die "
                          "regel (scripts/launch-direct.py, taito_gun_ini)."
                          % (game, ", ".join(ini) or uit["gun_ini"] or "?", waarde))
        if not waarde:
            uit["errors"].append("geen regiowaarde in het profiel of game.ini")
        return uit

    if uit["mechanisme"] == "core-region":
        veld = uit["fields"].get("Region", {})
        waarde = veld.get("game_ini") or veld.get("profiel") or ""
        uit["core"] = core_for(exe)
        uit["core_regio_afgehandeld"] = CORE_EVIDENCE[uit["core"]]["regio_afgehandeld"]
        uit["ok"] = bool(waarde)
        uit["message"] = ("%s gebruikt het profielveld Region (%r) op de %s; dat is geen regio/PCB-"
                          "dongle maar een kerninstelling. De kern van deze familie handelt de regio "
                          "af: %s." % (game, waarde, uit["core"].split(" (")[0],
                                       "ja" if uit["core_regio_afgehandeld"] else "nee"))
        if not waarde:
            uit["errors"].append("geen regiowaarde in het profiel of game.ini")
        return uit

    # mechanisme == "dongle": de eigenlijke regiolaag.
    uit["core"] = core_for(exe)
    uit["core_regio_afgehandeld"] = CORE_EVIDENCE[uit["core"]]["regio_afgehandeld"]

    if not desc:
        uit["errors"].append("geen dongle.json voor %s (verwacht: patches/%s/dropin/%s/%s/dongle.json "
                             "of patches/%s/dongle.json; de game vraagt wel een dongle: %s)"
                             % (game, game, DONGLE_LAAG, game, game,
                                ", ".join(sorted(merkers)[:3]) or "?"))
        uit["message"] = ("%s vraagt een regio-dongle, maar de beschrijving in zijn drop-in ontbreekt "
                          "(patches/%s/dropin/%s/%s/dongle.json)." % (game, game, DONGLE_LAAG, game))
        return uit

    if desc.get("kind") != "region":
        uit["errors"].append("dongle.json van %s heeft kind %r; dit script gaat over 'region'"
                             % (game, desc.get("kind")))
        uit["message"] = "dongle.json van %s is niet van het soort region." % game
        return uit

    if not any(m in merkers for m in MARKERS_DONGLE_N) and not any(m in merkers for m in MARKERS_READ_N):
        uit["errors"].append("de executable van %s noemt geen enkele amDongle- of regioleesroute; "
                             "klopt dongle.json wel?" % game)

    for sleutel, veldnaam in (("region", "DongleRegion"), ("pcb", "PcbRegion")):
        blok = desc.get(sleutel) or {}
        naam = str(blok.get("name", "")).strip()
        if not naam:
            uit["errors"].append("dongle.json van %s: %s.name ontbreekt" % (game, sleutel))
            continue
        if naam not in REGION_GAME_NAMES:
            uit["errors"].append("dongle.json van %s: %s = %r is geen Sega-regiowaarde (%s)"
                                 % (game, sleutel, naam, ", ".join(REGION_GAME_NAMES)))
        f = fields.get(veldnaam, {})
        if f and f.get("options") and naam not in f["options"]:
            uit["errors"].append("dongle.json van %s zet %s = %s, maar het profiel biedt alleen: %s"
                                 % (game, veldnaam, naam, ", ".join(f["options"])))
        g = settings.get("Game", {}).get(veldnaam, "")
        if g and naam and g != naam:
            uit["errors"].append("%s: game.ini zegt %s = %s, dongle.json zegt %s (dongle.json is de "
                                 "bron van waarheid)" % (game, veldnaam, g, naam))
        if blok.get("game_name") and REGION_GAME_NAMES.get(naam) != blok["game_name"]:
            uit["errors"].append("dongle.json van %s: %s.game_name = %s hoort niet bij %s"
                                 % (game, sleutel, blok["game_name"], naam))

    uit["enforced"] = bool(desc.get("enforced"))
    regio = (desc.get("region") or {}).get("name", "")
    pcb = (desc.get("pcb") or {}).get("name", "")

    if uit["errors"]:
        uit["message"] = ("de regiolaag van %s is niet in orde: %s" % (game, "; ".join(uit["errors"][:3])))
        return uit

    uit["ok"] = True
    if uit["enforced"]:
        uit["message"] = ("%s: regio %s / printplaat %s (dongle.json). De game start niet zonder dit "
                          "antwoord." % (game, regio, pcb))
    elif uit["core_regio_afgehandeld"]:
        uit["message"] = ("%s: regio %s / printplaat %s (dongle.json). De kern van deze familie "
                          "handelt de regio af." % (game, regio, pcb))
    else:
        uit["message"] = ("%s: regio %s / printplaat %s (dongle.json). De waarde is vastgelegd maar "
                          "niet afgedwongen: de kern van deze familie kent de regiokeys niet, de game "
                          "valt terug op zijn eigen DefaultRegion en start door. Zie docs/region-layer.md."
                          % (game, regio, pcb))
    return uit


def samenvatting(uit):
    """Zonder --game: alle games met een dongle.json, opgesomd. Mappen die met _ beginnen
    (_template, _defaults) zijn geen games en blijven buiten de lijst."""
    rijen = []
    gezien = set()
    for d in patches_dirs():
        if not os.path.isdir(d):
            continue
        for naam in sorted(os.listdir(d)):
            if naam.startswith("_") or naam in gezien:
                continue
            if not heeft_dongle(naam):
                continue
            gezien.add(naam)
            st = status(naam)
            rijen.append({"game": naam, "kind": (st["dongle"] or {}).get("kind"),
                          "mechanisme": st["mechanisme"], "ok": st["ok"],
                          "regio": ((st["dongle"] or {}).get("region") or {}).get("name", ""),
                          "pcb": ((st["dongle"] or {}).get("pcb") or {}).get("name", ""),
                          "enforced": st["enforced"], "errors": st["errors"],
                          "message": st["message"]})
    uit["games"] = rijen
    uit["ok"] = all(r["ok"] for r in rijen) if rijen else True
    if not rijen:
        uit["message"] = "geen enkele game heeft een dongle.json; er is dus geen regiolaag te melden."
    else:
        uit["message"] = ("%d game(s) met een dongle.json: %s."
                          % (len(rijen), ", ".join("%s(%s)" % (r["game"], r["kind"]) for r in rijen)))
    return uit


def main(argv):
    game = None
    als_json = False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--json":
            als_json = True
        elif a == "--game" and i + 1 < len(argv):
            game = argv[i + 1]
            i += 1
        elif a in ("-h", "--help"):
            print(__doc__)
            return 0
        else:
            print("onbekende optie: %s" % a, file=sys.stderr)
            return 2
        i += 1

    st = status(game)
    if als_json:
        print(json.dumps(st, ensure_ascii=False, indent=1))
    else:
        print(st["message"])
        for r in st.get("games", []) or []:
            print("  %-20s %-11s %s" % (r["game"], r["mechanisme"] or "-", r["message"]))
        for e in st["errors"]:
            print("  - %s" % e)
    return 0 if st["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

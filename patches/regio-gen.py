#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Maakt de tabel van patches/REGIO-PER-GAME.md en werkt hem bij tussen de markers.

Gebruik (op de Deck, vanuit de patches-repo):

    cd ~/projects/ravengun-patches
    python3 patches/regio-gen.py            # scant de games op de NAS en vult de tabel
    python3 patches/regio-gen.py --geen-scan   # alleen de tabel uit wat er al is (snel)

Wat het script zelf meet (en dus niet hoeft te worden overgetypt):

  * per game de NAS-map (NAS_MAP), de executable uit het TeknoParrot-profiel en de game.ini;
  * de merktekens in de bestanden van die map, op BYTENIVEAU en in EEN leesbeurt per bestand
    (hetzelfde als `strings -a -n 4 <exe> | grep -i -E 'amdongle|keychip|region|dongle'`);
  * de regiovelden van het profiel (DongleRegion / PcbRegion / Region) en van game.ini;
  * het dongle.json in de drop-in: het soort, de regiowaarden en de bron die het zelf noemt.

Wat met de hand wordt onderhouden: OORDEEL (ja/nee/onbekend + wat de game dan wel vraagt). Dat is
een beoordeling, geen meting. Het script waarschuwt als een oordeel tegen de meting in gaat, zodat
een verkeerd oordeel niet stil kan blijven staan.

Het schrijft alleen patches/REGIO-PER-GAME.md, het blok tussen BEGIN TABEL en END TABEL.
"""
import json
import os
import re
import sys

sys.dont_write_bytecode = True

PATCHES = "patches"
DOC = os.path.join(PATCHES, "REGIO-PER-GAME.md")
BEGIN = "<!-- BEGIN TABEL -->"
END = "<!-- END TABEL -->"
SKIP = {"_template", "_defaults"}
NAS = "/home/deck/mnt/spidernas_emulation/Teknoparrot"
MAX_BESTAND = 120 * 1024 * 1024
MAX_BESTANDEN = 3000

# ---------------------------------------------------------------------------------------------
# De NAS-map per game. De gamemap in het profiel is soms een SMALLE submap (HOTDSD: alleen de
# Binaries/Win64; LuigisMansion: alleen exe/x64), en de view van een game is een symlink-boerderij.
# Daarom staat hier de map die de game in zijn geheel bevat; die wordt helemaal doorzocht. Namen
# worden exact gematcht en anders met witruimte en hoofdletters genegeerd (op de NAS staan namen met
# een spatie op het eind, zoals "Rabbids Hollywood " en "Terminator Salvation ").
NAS_MAP = {
    "GaiaAttack4": ["Gaia Attack 4"],
    "GalagaAssault": ["Galaga Assault"],
    "Gashaaaan": ["Gashaaaan!!!!"],
    "Gashaaaan2": ["Gashaaaan Refill!!!!"],
    "GhostBusters": ["GhostBusters"],
    "GoGoStrike": ["Go Go Strike"],
    "HOTDSD": ["House of the Dead Scarlet Dawn"],
    "Halo": ["Halo Fireteam"],
    "HauntedMuseum": ["Haunted Museum"],
    "HauntedMuseumII": ["Haunted Museum II"],
    "InfinityBladeFX": ["Infinity Blade FX"],
    "JurassicPark": ["Jurassic Park"],
    "LGI": ["Let's Go Island Lost on the Island of Tropics"],
    "LGI3D": ["Let's Go Island 3D Lost on the Island of Tropics"],
    "LGS": ["Let's Go Safari"],
    "LethalEnforcers3": ["Lethal Enforcers 3"],
    "LuigisMansion": ["Luigi's Mansion Arcade"],
    "MedaruNoGunman": ["Medaru No Gunman"],
    "MusicGunGun2": ["Music GunGun 2"],
    "NERF": ["NERF Arcade"],
    "OG": ["OAV6DU~3"],
    "OmatsuriYasanGansoTakoyaki": ["Omatsuri Yasan - Ganso Takoyaki"],
    "OmatsuriYasanKingyoSukui": ["Omatsuri Yasan - Kingyo Sukui"],
    "Otomedius": ["Otomedius"],
    "PacmanChomp": ["Pacman Chomp-Mania"],
    "PigsMightFly": ["Pigs Might Fly"],
    "RabbidsHollywood": ["Rabbids Hollywood"],
    "RaccoonRampage": ["Raccoon Rampage"],
    "RainbowBombergirl": ["Rainbow Bombergirl"],
    "Rampage": ["Rampage"],
    "TC5": ["Time Crisis 5"],
    "Terminator": ["Terminator Salvation"],
    "TombRaider": ["Tomb Raider"],
    # Transformers: Shadows Rising zit in twee mappen; de map "Sega" bevat ook Transformers: Human
    # Alliance, een ANDER spel (met wel een DongleRegion). Daarom juist deze twee submappen.
    "TransformersShadowsRising": ["Sega/shell", "Sega/Transformers2"],
}

# ---------------------------------------------------------------------------------------------
# De merktekens. (naam, groep, hoofdletterongevoelig?)
MERKERS = [
    ("amDongleGetRegion", "dongle_api", False),
    ("amDongleSetupKeychip", "dongle_api", False),
    ("amDongleOpen", "dongle_api", False),
    ("amDongleInit", "dongle_api", False),
    ("amDongleIsAvailable", "dongle_api", False),
    ("amDongleBillingGetKeychipId", "dongle_api", False),
    ("amDongle Ver.", "dongle_api", False),
    ("amDongle", "dongle_api", True),
    ("readRegionEeprom", "regio_lees", False),
    ("readRegionBuSram", "regio_lees", False),
    ("setupRegionEeprom", "regio_lees", False),
    ("setupRegionBuSram", "regio_lees", False),
    ("DefaultRegion", "regio_terugval", False),
    ("Region is JPN.", "regio_terugval", False),
    ("Region is USA.", "regio_terugval", False),
    ("Region is EXP.", "regio_terugval", False),
    ("Region is CHN.", "regio_terugval", False),
    ("Wrong Region", "regio_terugval", False),
    ("WRONG REGION", "regio_terugval", False),
    ("keychip.appboot.region", "keychip", False),
    ("Keychip Not Found", "keychip", False),
    ("KEYCHIP ACCESS FAILED", "keychip", False),
    ("KeychipError", "keychip", False),
    ("getKeychipID", "keychip", False),
    ("GetKeychipId", "keychip", False),
    ("KeyRegion:", "regio_naam", False),
    ("BoardRegion:", "regio_naam", False),
    ("REGION_CHN", "regio_naam", False),
    ("REGION_EXP", "regio_naam", False),
    ("REGION_USA", "regio_naam", False),
    ("EAMDaemonRegionCode", "amdaemon", False),
    ("amdaemon", "amdaemon", True),
    ("MINIGUN.INI", "gun_ini", False),
    ("MUSEUM.INI", "gun_ini", True),
    ("HAUNTED2.INI", "gun_ini", True),
    ("hasp_login", "hasp", False),
    ("nethasp", "hasp", True),
    ("ReInitAladdinDongles", "hasp", False),
    ("DongleInitialize() FAILED", "hasp", False),
    ("hasp_windows", "hasp", True),
    ("matrix32", "matrix", True),
    ("matrix64", "matrix", True),
    ("MainThreadDongleScan", "sega_scan", False),
    ("DongleUID", "sega_scan", False),
    ("GAME PATH CHECK FAILED", "rawthrills", False),
]
SKIP_DIRS = {"ravengun-hasp", ".git", "ElfLdr2"}
# Media en tekst slaan we over: daar zit geen dongle-aanroep in, en het scheelt veel leeswerk.
MEDIA = {"wmv", "avi", "mpg", "mpeg", "m1v", "sfd", "wav", "mp3", "ogg", "flac", "png", "jpg",
         "jpeg", "bmp", "gif", "dds", "tga", "pdb", "ilk", "map", "txt", "md", "log", "xml",
         "json", "ini", "cfg", "csv", "xls", "xlsx", "lua", "js", "html", "css", "rsc", "assets",
         "ress", "pak", "cib", "afb", "mcs", "zip", "7z", "rar", "pkg", "pdf", "yml", "yaml"}

# ---------------------------------------------------------------------------------------------
# Het oordeel per game. Dit is het enige met de hand onderhouden deel.
#   "oordeel"  ja / nee / onbekend
#   "anders"   wat de game dan WEL vraagt (bij nee), of wat er nodig is om te beslissen (onbekend)
OORDEEL = {
    "LGI": ("ja", ""),
    "LGI3D": ("ja", ""),
    "LGS": ("ja", ""),
    "OG": ("ja", ""),

    "GaiaAttack4": ("nee", "een **bestandswaarde**: de REGION-regel in zijn eigen `MINIGUN.INI` "
                           "(het profielveld `Region`). Geen dongle."),
    "HauntedMuseum": ("nee", "een **bestandswaarde**: de REGION-regel in zijn eigen `MUSEUM.ini`. "
                             "Geen dongle."),
    "HauntedMuseumII": ("nee", "een **bestandswaarde**: de REGION-regel in zijn eigen `haunted2.ini`. "
                               "Daarnaast zit er een HASP/Aladdin-laag in `game.exe`. Geen dongle "
                               "van het soort region."),
    "RainbowBombergirl": ("nee", "het **profielveld `Region`** op de 64-bits kern (de ALLS/amdaemon-"
                                 "route). Geen dongle."),

    "GalagaAssault": ("nee", "een **HASP/Aladdin-dongle** (de eigen donglesoort `hasp`)."),
    "Halo": ("nee", "een **HASP-dongle**; die is al vastgelegd als `kind: hasp`."),
    "JurassicPark": ("nee", "een **HASP-dongle**; die is al vastgelegd als `kind: hasp`."),
    "PacmanChomp": ("nee", "een **HASP/Aladdin-dongle** (`pmrtstart`, `pmrtstart.x86_64`, `game`)."),
    "Terminator": ("nee", "een **HASP-dongle** (`ServiceDongleApp`, `t4-patch`); al vastgelegd als "
                          "`kind: hasp`."),
    "GhostBusters": ("nee", "een **HASP/Aladdin-dongle** (`game`, `pm/game`, `pm/utils/rtinit`) "
                            "**en** een **`pm`-map**: de map van de elf moet `pm` heten "
                            "(`GAME PATH CHECK FAILED`)."),
    "TC5": ("nee", "een **HASP-USB-dongle** (`hasp_windows_x64_105293.dll`, `hasp_windows_x64_106993.dll`); "
                   "al vastgelegd als `kind: hasp`. De regio-variabelen in `RSLauncher.exe` "
                   "(`region_id`, `varBoolRegionJPN/CHN/INT`) zijn profiel-/configuratievlaggen, geen "
                   "dongle-leesroute."),
    "NERF": ("nee", "een **HASP-dongle in managed code**: `Nerf_Data/Managed/Assembly-CSharp.dll` "
                    "(`UDongleControl`) met de BepInEx-plugin `BepInEx/plugins/NERFTPPlugin.dll` als "
                    "antwoord. Geen HASP-API-oppervlak in het Windows-spel."),

    "InfinityBladeFX": ("nee", "een **matrix/SBK-dongle** (`matrix32.dll`); al vastgelegd als "
                               "`kind: matrix`."),
    "RabbidsHollywood": ("nee", "een **matrix/SBK-dongle** (`matrix32.dll`, `matrix64.dll`, "
                                "`Game_Data/Plugins/matrix/matrix.ini`); al vastgelegd als `kind: matrix`."),
    "Rampage": ("nee", "een **matrix/SBK-dongle** (`matrix32.dll`, `matrix64.dll`); al vastgelegd als "
                       "`kind: matrix`."),
    "TombRaider": ("nee", "een **matrix/SBK-dongle** (`matrix32.dll`, `matrix64.dll`); al vastgelegd "
                          "als `kind: matrix`."),

    "TransformersShadowsRising": ("nee", "een **DK2-sleutel** die getrapt wordt gezocht "
                                         "(`MainThreadDongleScan` in `Shell.exe`) — de eigen donglesoort "
                                         "`sega-scan`, niet `region`. Het spel is speelbaar terwijl de "
                                         "scan faalt (`Cabinet SKU set to UNKNOWN`)."),
    "HOTDSD": ("onbekend", "De game-exe vraagt de regio aan **amdaemon**, maar de amdaemon-service die "
                           "met de game meekomt (`AMDaemon/amdaemon.exe`, `AMDaemon/hwvalue.exe`) roept "
                           "zélf `amDongleGetRegion` en `amDongleSetupKeychip` aan. Het profiel heeft "
                           "**geen** `DongleRegion`/`PcbRegion`, dus er is geen regiowaarde om te "
                           "antwoorden. Nodig om te beslissen: de game starten en het amdaemon-logboek "
                           "lezen, of meten welk antwoord `amDongleGetRegion` in het daemonproces "
                           "krijgt. De eigenaar noemt dit al een open punt (`scripts/amdaemon.py`: "
                           "\"de dongle/keychip die de daemon zelf controleert\")."),
    "MedaruNoGunman": ("nee", "de **keychip** van de Lindbergh/Ring-familie (facturatie, tracelading, "
                              "SSD-bewijs) — een eigen laag. Beslissing van de eigenaar, onderbouwd in "
                              "`patches/MedaruNoGunman/README.md`."),

    "Gashaaaan": ("nee", "niets: geen dongle-aanroep, geen regioveld, geen ini-regel."),
    "Gashaaaan2": ("nee", "niets."),
    "GoGoStrike": ("nee", "niets."),
    "LethalEnforcers3": ("nee", "niets."),
    "LuigisMansion": ("nee", "niets."),
    "MusicGunGun2": ("nee", "niets."),
    "OmatsuriYasanGansoTakoyaki": ("nee", "niets."),
    "OmatsuriYasanKingyoSukui": ("nee", "niets."),
    "Otomedius": ("nee", "niets."),
    "PigsMightFly": ("nee", "niets."),
    "RaccoonRampage": ("nee", "niets."),

    "3DCosplayMahjong": ("onbekend", "De game staat **niet op de NAS**, dus er is geen executable om "
                                     "te onderzoeken. Het profiel `TaitoTypeXGeneric` heeft geen "
                                     "`DongleRegion`/`PcbRegion`/`Region`. Nodig: de spelbestanden."),
    "TankTankTank": ("onbekend", "De game staat **niet op de NAS** (`n_tank_release`, ElfLdr2, "
                                 "profiel `DeadHeat`), dus er is geen executable om te onderzoeken. "
                                 "Het profiel heeft geen `DongleRegion`/`PcbRegion`/`Region`. Nodig: "
                                 "de spelbestanden."),
}


def lees_ini(pad):
    uit, sec = {}, ""
    if not os.path.isfile(pad):
        return uit
    try:
        with open(pad, encoding="utf-8-sig", errors="replace") as fh:
            regels = fh.read().splitlines()
    except OSError:
        return uit
    for regel in regels:
        r = regel.strip()
        if not r or r[0] in "#;":
            continue
        if r.startswith("[") and r.endswith("]"):
            sec = r[1:-1].strip()
            uit.setdefault(sec, {})
            continue
        if "=" not in r:
            continue
        k, v = r.split("=", 1)
        uit.setdefault(sec, {})[k.strip()] = v.strip()
    return uit


def xml_velden(pad):
    scal, cv = {}, {}
    if not os.path.isfile(pad):
        return scal, cv
    try:
        with open(pad, encoding="utf-8-sig", errors="replace") as fh:
            tekst = fh.read()
    except OSError:
        return scal, cv
    for m in re.finditer(r"<([A-Za-z0-9_]+)>([^<]*)</\1>", tekst):
        scal.setdefault(m.group(1), m.group(2).strip())
    for blok in re.findall(r"<FieldInformation>(.*?)</FieldInformation>", tekst, re.S):
        naam = re.search(r"<FieldName>(.*?)</FieldName>", blok, re.S)
        if not naam:
            continue
        w = re.search(r"<FieldValue>(.*?)</FieldValue>", blok, re.S)
        o = re.findall(r"<string>(.*?)</string>", blok, re.S)
        cv[naam.group(1).strip()] = {"value": (w.group(1).strip() if w else ""),
                                     "options": [x.strip() for x in o]}
    return scal, cv


def runtime_dir():
    basis = "/home/deck/.local/share/ravengun/runtime"
    k = []
    if os.path.isdir(basis):
        for naam in sorted(os.listdir(basis)):
            p = os.path.join(basis, naam)
            if naam.startswith("TeknoParrotUi") and os.path.isdir(os.path.join(p, "GameProfiles")):
                k.append(p)
    return k[-1] if k else None


def vind_nas_map(namen):
    """De NAS-map: het pad onder NAS moet bestaan. Namen met een spatie op het eind (zoals
    "Rabbids Hollywood " en "Terminator Salvation ") worden ook gevonden."""
    if not os.path.isdir(NAS):
        return []
    try:
        aanwezig = os.listdir(NAS)
    except OSError:
        return []
    norm = {n.strip().lower(): n for n in aanwezig}
    uit = []
    for naam in namen:
        p = os.path.join(NAS, naam)
        if os.path.isdir(p):
            uit.append(p)
            continue
        # een naam met een spatie op het eind, of met andere hoofdletters
        delen = naam.split("/")
        echt = norm.get(delen[0].strip().lower())
        if echt:
            p = os.path.join(NAS, echt, *delen[1:])
            if os.path.isdir(p):
                uit.append(p)
    return uit


def is_binary(pad):
    """Snel voorfilter: alleen echte uitvoerbare bestanden volledig lezen.

    Dit is de reden dat de ELF's zonder extensie (`game`, `bowling_usa`) meegaan: we kijken naar de
    eerste bytes (MZ = PE, \\x7fELF = ELF) en niet naar de bestandsnaam. Een mediabestand wordt zo
    nooit helemaal gelezen, wat op de CIFS-koppeling uren scheelt.
    """
    try:
        with open(pad, "rb") as fh:
            kop = fh.read(4)
    except OSError:
        return False
    if kop[:2] == b"MZ" or kop[:4] == b"\x7fELF":
        return True
    # een .dll/.exe/.so die (nog) geen MZ/ELF-kop heeft toch meenemen: dan is het waarschijnlijk een
    # verpakt of beschadigd bestand, en juist daar kan een merkteken in staan.
    return pad.lower().rsplit(".", 1)[-1] in ("exe", "dll", "so", "elf", "sys", "asi", "ocx")


def tel_bestand(pad):
    cs = {}
    ci = {}
    rest = b""
    try:
        with open(pad, "rb") as fh:
            while True:
                blok = fh.read(4 << 20)
                if not blok:
                    break
                buf = rest + blok
                for naam, _g, gevoelig in MERKERS:
                    if not gevoelig:
                        n = buf.count(naam.encode())
                        if n:
                            cs[naam] = cs.get(naam, 0) + n
                laag = buf.lower()
                for naam, _g, gevoelig in MERKERS:
                    if gevoelig:
                        n = laag.count(naam.encode().lower())
                        if n:
                            ci[naam] = ci.get(naam, 0) + n
                rest = buf[-32:]
    except OSError:
        return {}
    uit = dict(cs)
    for k, v in ci.items():
        uit[k] = uit.get(k, 0) + v
    return uit


def scan_game(game, tp):
    """De meting: welke merktekens staan in de bestanden van deze game, per bestand."""
    gd = os.path.join(PATCHES, game)
    gi = lees_ini(os.path.join(gd, "game.ini"))
    profiel = gi.get("Game", {}).get("Profile", "") or game
    scal, cv, bron = {}, {}, None
    for sub in ("UserProfiles", "GameProfiles"):
        p = os.path.join(tp, sub, profiel + ".xml") if tp else None
        if p and os.path.isfile(p):
            scal, cv = xml_velden(p)
            bron = "%s/%s.xml" % (sub, profiel)
            break
    uit = {"game": game, "profiel": profiel, "profiel_bron": bron,
           "profiel_regio": {k: cv[k] for k in ("DongleRegion", "PcbRegion", "Region") if k in cv},
           "game_ini_regio": {k: v for k, v in gi.get("Game", {}).items() if "Region" in k},
           "exe": gi.get("Game", {}).get("Executable", "") or scal.get("ExecutableName", ""),
           "nas_mappen": [], "n_bestanden": 0, "per_bestand": {}, "groepen": set(), "merken": set()}

    groepen_van = {n: g for n, g, _ in MERKERS}
    for basis in vind_nas_map(NAS_MAP.get(game, [])):
        uit["nas_mappen"].append(basis)
        n = 0
        for root, dirs, files in os.walk(basis):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            for f in sorted(files):
                p = os.path.join(root, f)
                # LET OP: de Raw Thrills-games zijn ELF's ZONDER extensie ("game", "bowling_usa").
                # Daarom niet op extensie selecteren maar op de eerste bytes (is_binary).
                if not is_binary(p):
                    continue
                try:
                    if os.path.getsize(p) > MAX_BESTAND:
                        continue
                except OSError:
                    continue
                n += 1
                if n > MAX_BESTANDEN:
                    break
                t = tel_bestand(p)
                if t:
                    rel = os.path.relpath(p, basis)
                    uit["per_bestand"][rel] = t
                    for k in t:
                        uit["groepen"].add(groepen_van[k])
                        uit["merken"].add(k)
            if n > MAX_BESTANDEN:
                break
        uit["n_bestanden"] += n

    # het dongle.json in de drop-in
    dj = os.path.join(gd, "dropin", "ravengun-hasp", game, "dongle.json")
    if not os.path.isfile(dj):
        dj = os.path.join(gd, "dongle.json")
    uit["dongle_json"] = None
    uit["dongle_json_pad"] = None
    if os.path.isfile(dj):
        try:
            with open(dj, encoding="utf-8") as fh:
                uit["dongle_json"] = json.load(fh)
            uit["dongle_json_pad"] = dj
        except (OSError, ValueError) as e:
            uit["dongle_json_pad"] = "ONLEESBAAR: %s" % e
    return uit


# De volgorde waarin merktekens in de bewijskolom komen, en de leesbare naam.
VOLGORDE = ["dongle_api", "regio_lees", "regio_terugval", "keychip", "regio_naam", "amdaemon",
            "gun_ini", "hasp", "matrix", "sega_scan", "rawthrills"]
GROEP_NAAM = {
    "dongle_api": "amDongle-API", "regio_lees": "regio-leesroute", "regio_terugval": "terugval",
    "keychip": "keychip", "regio_naam": "regionaam", "amdaemon": "amdaemon",
    "gun_ini": "eigen ini", "hasp": "HASP/Aladdin", "matrix": "matrix/SBK",
    "sega_scan": "DK2-donglescan", "rawthrills": "Raw Thrills/pm",
}


def bewijs_cel(r):
    """De bewijskolom: welk bestand, welke functie/string, welke regel."""
    stukken = []
    if not r["nas_mappen"]:
        stukken.append("**geen bestanden op de NAS om te onderzoeken** (de game staat er niet)")
    elif not r["per_bestand"]:
        stukken.append("geen enkel merkteken in %d bestanden" % r["n_bestanden"])
    else:
        per_groep = {}
        for bestand, t in sorted(r["per_bestand"].items()):
            for merk, aantal in t.items():
                g = next((x for x in VOLGORDE if x == _groep_van(merk)), "?")
                per_groep.setdefault(g, {}).setdefault(bestand, []).append(
                    merk if aantal == 1 else "%s(%d)" % (merk, aantal))
        for g in VOLGORDE:
            if g not in per_groep:
                continue
            for bestand, merken in sorted(per_groep[g].items()):
                stukken.append("`%s`: %s — %s" % (GROEP_NAAM.get(g, g), "`%s`" % bestand,
                                                  ", ".join(merken)))
    # de profielkant
    for veld in ("DongleRegion", "PcbRegion", "Region"):
        if veld in r["profiel_regio"]:
            info = r["profiel_regio"][veld]
            opties = (" (keuzes: %s)" % "|".join(info["options"])) if info["options"] else ""
            stukken.append("profiel `%s`: `%s = %s`%s" % (r["profiel_bron"], veld,
                                                          info["value"], opties))
        elif r["game_ini_regio"].get(veld):
            stukken.append("game.ini: `%s = %s`" % (veld, r["game_ini_regio"][veld]))

    # Het beslissende ONderscheid: een game die amDongle noemt maar NOOIT de regio leest, vraagt
    # een keychip en geen regio/printplaat-dongle. Dat moet in de bewijskolom staan, anders lijkt
    # het bewijs het oordeel tegen te spreken. (LGS heeft wél amDongleGetRegion maar geen
    # readRegion*; MedaruNoGunman heeft geen van beide. Daarom op de merknaam toetsen, niet op de
    # groep.)
    leest_regio = ("amDongleGetRegion" in r["merken"] or
                   any(m.startswith("readRegion") or m.startswith("setupRegion") for m in r["merken"]))
    heeft_region_json = bool(r["dongle_json"]) and \
        (r["dongle_json"].get("kind") or "").strip() == "region"
    if "dongle_api" in r["groepen"] and not leest_regio:
        if heeft_region_json:
            # OG: wel een regiodongle, maar de regio wordt via de keychip-opdracht gelezen.
            stukken.append("**let op:** leest de regio via `keychip.appboot.region`, niet via "
                           "`amDongleGetRegion`")
        else:
            # MedaruNoGunman/LuigisMansion: amDongle zonder enige regio-leesroute = keychip.
            stukken.append("**let op:** wel `amDongle`, maar **géén** `amDongleGetRegion` en **géén** "
                           "`readRegionEeprom`/`readRegionBuSram` — dus geen regio-leesroute")
    return "<br>".join(stukken) if stukken else "-"


def _groep_van(merk):
    for naam, g, _ in MERKERS:
        if naam == merk:
            return g
    return "?"


def dongle_cel(r):
    d = r["dongle_json"]
    if not d:
        return "nee"
    soort = (d.get("kind") or "?").strip()
    if soort != "region":
        return "nee (wel `kind: %s`)" % soort
    regio = (d.get("region") or {}).get("name") or "?"
    pcb = (d.get("pcb") or {}).get("name") or "?"
    return ("**ja** — regio **%s** / printplaat **%s**, `enforced = %s`"
            % (regio, pcb, str(d.get("enforced")).lower()))


def bron_cel(r):
    """Waar de regiowaarden van het dongle.json vandaan komen."""
    d = r["dongle_json"]
    if not d or (d.get("kind") or "").strip() != "region":
        return "-"
    stukken = []
    for veld, blok in (("DongleRegion", d.get("region") or {}), ("PcbRegion", d.get("pcb") or {})):
        if veld in r["profiel_regio"]:
            stukken.append("profiel `%s = %s`" % (veld, r["profiel_regio"][veld]["value"]))
        if r["game_ini_regio"].get(veld):
            stukken.append("game.ini `%s = %s`" % (veld, r["game_ini_regio"][veld]))
    if d.get("evidence", {}).get("findings"):
        stukken.append("findings")
    return "; ".join(stukken) if stukken else "geen bron in het bestand"


def tabel(rijen):
    uit = ["| Game | regio/PCB-dongle? | bewijs (bestand · functie/string · regel) | "
           "`dongle.json` met `kind: region`? | waar komen de regiowaarden vandaan | "
           "wat vraagt de game anders |",
           "|---|---|---|---|---|---|"]
    for r in rijen:
        oordeel, anders = OORDEEL.get(r["game"], ("?", ""))
        uit.append("| **%s** | %s | %s | %s | %s | %s |" % (
            r["game"], oordeel, bewijs_cel(r), dongle_cel(r), bron_cel(r), anders or "-"))
    return uit


def controleer(rijen):
    """Waarschuw als een handmatig oordeel tegen de meting in gaat."""
    waarschuwingen = []
    for r in rijen:
        oordeel = OORDEEL.get(r["game"], ("?", ""))[0]
        groepen = r["groepen"]
        regio_dongle = ("dongle_api" in groepen and
                        (("regio_lees" in groepen) or ("dongle_api" in groepen and
                                                       "keychip" in groepen)))
        heeft_region_json = bool(r["dongle_json"]) and \
            (r["dongle_json"].get("kind") or "").strip() == "region"
        if oordeel == "nee" and heeft_region_json:
            waarschuwingen.append("%s: oordeel nee, maar er is een dongle.json met kind region" % r["game"])
        if oordeel == "ja" and not heeft_region_json:
            waarschuwingen.append("%s: oordeel ja, maar er is geen dongle.json met kind region" % r["game"])
        if oordeel == "nee" and "dongle_api" in groepen:
            leest = ("amDongleGetRegion" in r["merken"] or
                     any(m.startswith("readRegion") or m.startswith("setupRegion") for m in r["merken"]))
            if leest:
                waarschuwingen.append("%s: oordeel nee, maar de exe noemt amDongle EN een "
                                      "regio-leesroute (amDongleGetRegion/readRegion*) — klopt het "
                                      "oordeel?" % r["game"])
        if oordeel == "onbekend" and not r["nas_mappen"]:
            # geen bestanden om te onderzoeken: dat hoort bij onbekend, geen waarschuwing
            pass
        if oordeel == "ja" and "dongle_api" not in groepen and r["nas_mappen"]:
            waarschuwingen.append("%s: oordeel ja, maar geen enkele amDongle-aanroep gemeten" % r["game"])
    return waarschuwingen


def tellingen(rijen):
    per = {"ja": [], "nee": [], "onbekend": []}
    for r in rijen:
        o = OORDEEL.get(r["game"], ("?", ""))[0]
        per.setdefault(o, []).append(r["game"])
    return [
        "## Tellingen",
        "",
        "| oordeel | aantal | games |",
        "|---|---|---|",
        "| **ja** (vraagt een regio/printplaat-dongle) | %d | %s |" % (
            len(per["ja"]), ", ".join("**%s**" % g for g in per["ja"]) or "-"),
        "| **nee** | %d | zie de tabel |" % len(per["nee"]),
        "| **onbekend** | %d | %s |" % (
            len(per["onbekend"]), ", ".join("**%s**" % g for g in per["onbekend"]) or "-"),
        "| games samen | %d | |" % len(rijen),
        "",
        "Wat de games met `nee` dan wél vragen, in groepen:",
        "",
        "| wat | games |",
        "|---|---|",
        "| een REGION-regel in hun eigen ini (Taito Type X2, geen dongle) | GaiaAttack4, HauntedMuseum, "
        "HauntedMuseumII |",
        "| het profielveld `Region` op de 64-bits kern (geen dongle) | RainbowBombergirl |",
        "| een HASP/Aladdin-dongle (`kind: hasp`) | GalagaAssault, Halo, JurassicPark, PacmanChomp, "
        "Terminator, GhostBusters, TC5, NERF |",
        "| een matrix/SBK-dongle (`kind: matrix`) | InfinityBladeFX, RabbidsHollywood, Rampage, TombRaider |",
        "| een DK2-sleutel (`kind: sega-scan`) | TransformersShadowsRising |",
        "| de amdaemon-laag (`kind: amdaemon`) | *HOTDSD staat op `onbekend`: de amdaemon-service "
        "roept zélf `amDongleGetRegion` aan* |",
        "| de keychip van de Lindbergh/Ring-familie (eigen laag) | MedaruNoGunman |",
        "| niets | Gashaaaan, Gashaaaan2, GoGoStrike, LethalEnforcers3, LuigisMansion, MusicGunGun2, "
        "OmatsuriYasanGansoTakoyaki, OmatsuriYasanKingyoSukui, Otomedius, PigsMightFly, RaccoonRampage |",
        "",
    ]


def main():
    if not os.path.isdir(PATCHES):
        sys.exit("FOUT: geen %s/ hier. Draai dit vanuit de patches-repo "
                 "(cd ~/projects/ravengun-patches)." % PATCHES)
    if not os.path.isfile(DOC):
        sys.exit("FOUT: %s bestaat niet" % DOC)
    tp = runtime_dir()
    games = sorted(d for d in os.listdir(PATCHES)
                   if d not in SKIP and not d.startswith("_")
                   and os.path.isdir(os.path.join(PATCHES, d)))
    rijen = []
    for g in games:
        print("scan %s ..." % g, file=sys.stderr)
        rijen.append(scan_game(g, tp))

    blok = "\n".join(tabel(rijen) + [""] + tellingen(rijen))
    with open(DOC, encoding="utf-8") as fh:
        tekst = fh.read()
    if BEGIN not in tekst or END not in tekst:
        sys.exit("FOUT: %s en %s niet gevonden in %s" % (BEGIN, END, DOC))
    kop, rest = tekst.split(BEGIN, 1)
    _, staart = rest.split(END, 1)
    with open(DOC, "w", encoding="utf-8") as fh:
        fh.write(kop + BEGIN + "\n" + blok + "\n" + END + staart)

    waarschuwingen = controleer(rijen)
    print("bijgewerkt: %s (%d games, runtime %s)" % (DOC, len(rijen), tp))
    if waarschuwingen:
        print("LET OP -- het oordeel gaat tegen de meting in:")
        for w in waarschuwingen:
            print("  - %s" % w)
        return 1
    print("geen tegenstrijdigheden tussen het oordeel en de meting.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

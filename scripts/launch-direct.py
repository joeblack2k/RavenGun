#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Directe start van een TeknoParrot-game zonder TeknoParrotUi (A10; docs/decisions.md, "A10: games direct starten").
#
# Wat TeknoParrotUi bij een start doet, en wat dit script daarvan overneemt (gelezen in de open bron van TeknoParrotUI,
# branch net8-migration, en gemeten aan een lopende sessie; eigen code, niets overgenomen):
#   1. Profiel lezen: het eigen profiel in patches/<P>/game.ini (meegeleverd + gebruikerslaag: [Game], [Config], [Identify], ...; het pad naar de
#      game wordt afgeleid uit de gamemappen, zie scripts/ownprofile.py), zonder UserProfiles/<P>.xml. Heeft game.ini geen eigen profiel, dan
#      UserProfiles/<P>.xml (met GameProfiles/<P>.xml voor ontbrekende velden). Plus de omgeving uit game.ini (gamesettings.py; terugval: het oude
#      UserProfiles/<P>.env) en --env. Revisie: ProfileRevision moet gelijk zijn aan die van het sjabloon, anders exit 4 (opnieuw importeren).
#   2. De lader kiezen per EmulatorType (antwoorden_ronde2.md vraag 8): ElfLdr2 -> ElfLdr2/BudgieLoader.exe of
#      ElfLdr2/x64/BudgieLoader_x64.exe; TeknoParrot -> OpenParrotLoader(64).exe met de kern TeknoParrot/TeknoParrot(64)
#      als eerste argument; OpenParrot -> idem met OpenParrot(64); enz. Aanroep: wine <lader> [<kern>] "<exe>" [extra].
#   3. Werkmap: de map van de game (ElfLdr2, N2, Lindbergh) of de runtime-map (de rest). Omgeving: TP_REMOTETHREAD=1,
#      tp_windowed, TP_LOGTOFILE, TP_MSAA, TEA_DIR, OPENSSL_ia32cap, WINEPREFIX, WINEDEBUG=-all, TP_LAUNCH_SESSION_ID.
#   4. teknoparrot.ini naast de exe (spelinstellingen uit het profiel). Wij schrijven nooit naar de NAS: staat het bestand
#      daar al met precies deze inhoud, dan is er niets te doen; wijkt het af, dan blijft het staan en melden we dat.
#   5. Gedeeld geheugen /dev/shm/TeknoParrot_JvsState (64 bytes) aanmaken; pipehelper.exe uit de runtime in de prefix
#      starten ("<pipe> 127.0.0.1 <poort> TeknoParrot_JvsState 64 <bron>"): die maakt \\.\pipe\TeknoParrot_JVS en de
#      Windows-mapping en spiegelt het bronbestand (bij route tp-shm het bestand van de engine). De TCP-kant van de pipe
#      bedient dit script: verbind-vlag (byte 0) bij JVS-reset/-adres, zoals TeknoParrotUi; overig JVS-verkeer wordt gelogd.
#   6. Gamescope als TP_GAMESCOPE=1: gamescope -W <b> -H <h> -S fit -f --backend wayland -- wine ...
#   7. Wachten tot de game weg is (lader en exe in de eigen cgroup), dan opruimen. Dit script draait als hoofdproces van
#      de unit ravengun-tpui (scripts/play-halo.sh --direct), op de plaats van TeknoParrotUi.
# Niet nagebouwd: keychip en integriteit (blijven in de lader/kern), en de activering: die moet al in de prefix
# staan (TeknoParrotUi zet hem daar bij een start); is hij er niet of niet actueel, dan stopt dit script met exitcode 3
# en valt play-halo.sh terug op TeknoParrotUi.
#
# Gebruik:
#   scripts/launch-direct.py PROFIEL --plan [--json]      afgeleid startplan tonen (geen bijwerkingen)
#   scripts/launch-direct.py PROFIEL --check              alleen controleren (exit 0 = direct kan, 3 = activering, 4 = niet ondersteund)
#   scripts/launch-direct.py PROFIEL --run [--shm-source PAD] [--log-full] [--env K=V]...
# Opties: --tp DIR (runtime-map), --shm-source PAD (bronbestand voor pipehelper; standaard /dev/shm/TeknoParrot_JvsState),
#         --log-full (TP_LOGTOFILE=1 en een A11-samenvatting van de regels van de kern in ~/.local/state/ravengun/a11/).
# Exitcodes: 0 klaar, 1 fout tijdens de start, 2 gebruik, 3 activering ontbreekt (val terug op TeknoParrotUi),
#            4 profiel niet ondersteund door de directe start (val terug op TeknoParrotUi), 5 een ingeschakelde patch is geweigerd
#            (geen terugval), 7 bij een profiel met twee exe's: een start was al weg voordat de volgende begon (halve
#            opstelling; de run is afgebroken). Anders de exitcode van de lader (bij twee exe's: die van het spel).
#
# Twee exe's (HasTwoExecutables): beide starts staan in --plan ("starts", met de orde en de wachttijd) en krijgen dezelfde
# lader, kern, werkmap, prefix en omgeving; uitvoer en exitcode per start in ~/.local/state/ravengun/starts/<Profiel>-<tijd>/.
# Zie docs/per-game-patches.md, "Twee exe's".
#
# Argumenten voor het spel: [Launch] Arguments in game.ini (standaard leeg), naast ExtraParameters/CustomArguments van het
# profiel. --plan laat zien welke er meegaan en hoe; de OpenParrot-lader krijgt ze als EEN parameter (meer neemt hij niet).
# Omgevingsvariabelen voor het spel: [Env] in game.ini; --plan laat zien welke er meegaan.
# Het spel vanaf een ander pad: --exe PAD of [Launch] Exe (bijvoorbeeld X:\exe\x64\spel.exe, zoals het eigen startscript het
# doet); alleen het pad dat de lader krijgt verandert, de rest van de opstelling blijft gelijk. Schijfletters voor de run:
# --drive X=MAP of [Launch] Drives; ze staan er precies zolang de run duurt. --plan laat beide zien.
# Het eigen startscript van het spel (game.bat, run_game.sh; [Launch] StartScript) wordt GELEZEN, niet uitgevoerd: --plan
# zegt welke variabelen, argumenten, programma's en werkmap de kast gebruikt en wij niet (scripts/startscript.py).
# Zie docs/per-game-patches.md, "Argumenten voor het spel" en "Het eigen startscript van het spel".
#
# Drop-ins (docs/dropins.md): patches/<Game>/dropin/ bevat bestanden die letterlijk in de view van de game komen,
# met per bestand een sectie in dropin.ini (To, DllOverride, Env, Preload). Dat is het algemene mechanisme waarmee een
# game onze eigen bibliotheken krijgt. Dat is het enige mechanisme: de loader weet niets van dongles
# (docs/geen-dongle-in-de-loader.md); een software-dongle is een gewone drop-in.
# Instellingen per game (lader, Wine, omgeving, patches): patches/<Game>/game.ini en patches.ini met de gebruikerslaag eronder
# (scripts/gamesettings.py, docs/per-game-patches.md). Het oude UserProfiles/<P>.env wordt nog één versie als laag gelezen.
import json
import os
import re
import select
import shlex
import signal
import socket
import subprocess
import sys
import threading
import time
import uuid
import xml.etree.ElementTree as ET
import hashlib

sys.dont_write_bytecode = True   # geen __pycache__ in scripts/
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I neemt de scriptmap niet mee
import gamesettings as gs  # noqa: E402  (instellingen en patches per game: patches/<Game>/game.ini, patches.ini)
import ownprofile as op  # noqa: E402  (het eigen profiel in game.ini; gamemappen en herkenning)
import patchfile as pf  # noqa: E402  (de patchmotor: Kind = file op de game zelf en Kind = memory op de hoofdmodule)
import startscript as ss  # noqa: E402  (het eigen startscript van een spel lezen, niet uitvoeren)

HOME = os.environ.get("HOME", "/home/deck")
DEFAULT_TP = os.path.join(HOME, ".local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64")
DEFAULT_WINE = os.path.join(HOME, ".local/share/ravengun/wine/proton-11.0-2/bin/wine")
TPUI_DATA = os.path.join(HOME, ".local/share/TeknoParrotUI")   # alleen nog voor de activation-map (TP-abonnement)
RG_PREFIXES = os.path.join(HOME, ".local/share/ravengun", "prefixes")   # onze eigen Wine-prefixes (voorheen onder TeknoParrotUI/prefixes; zie docs/research/loskoppelen-takenlijst.md)
RG_JVSBRIDGE = os.path.join(HOME, ".local/share/ravengun", "jvsbridge", "rg-jvsbridge.exe")   # onze eigen JVS-brug (vervangt TeknoParrots pipehelper.exe); scripts/build-jvsbridge.sh
SHARED_PREFIX = os.path.join(RG_PREFIXES, "shared", "wine")
SHM_NAME = "TeknoParrot_JvsState"
SHM_PATH = "/dev/shm/" + SHM_NAME
SHM_SIZE = 64
JVS_PIPE = "TeknoParrot_JVS"
STATE_DIR = os.path.join(os.environ.get("XDG_STATE_HOME", os.path.join(HOME, ".local/state")), "ravengun")

EXIT_ACTIVATION = 3
EXIT_UNSUPPORTED = 4
EXIT_PATCH = 5       # een ingeschakelde patch is geweigerd: niet terugvallen op TeknoParrotUi (die zou de game zonder patch starten)
EXIT_HALF = 7        # een profiel met meer dan een start: een start was al weg voordat de volgende begon (halve opstelling)

# Profielen waarvoor TeknoParrotUi extra argumenten of voorbereidingen heeft die hier niet zijn nagebouwd: die vallen
# terug op TeknoParrotUi (exit 4). Bron: GameLaunchArguments in TeknoParrotUI (net8-migration).
UNSUPPORTED_EMULATION = {
    "AfterBurnerClimax", "TaitoTypeXBattleGear", "NamcoMachStorm", "NamcoPokken", "GuiltyGearRE2", "GuiltyGearAPM3",
    "SiN", "ALLSSWDC", "ALLSSCHRONO", "NamcoMkdx", "EXVS2", "EXVS2XB", "SegaToolsIDZ", "APM3Direct", "Vf5Lindbergh",
    "Vf5cLindbergh",
}
OPENSSL_FIX = {"ALLSSWDC", "IDZ", "ALLSSCHRONO", "NxL2", "RawThrillsFNF", "ALLSHOTDSD", "ALLSFGO", "TimeCrisis5",
               "JojoLastSurvivor", "DenshaDeGo", "ALLSIDTA", "SegaOlympic2020"}
NO_JVS_PIPE = {"EuropaRFordRacing", "EuropaRSegaRally3", "Theatrhythm", "FastIo", "GunslingerStratos3"}
TAITO_GUN_INI = {"GaiaAttack4": ("MINIGUN.INI", "GA4"), "HauntedMuseum": ("MUSEUM.INI", "HM"),
                 "HauntedMuseum2": ("HAUNTED2.INI", "HM2")}
LINDBERGH_TEA = {"SegaInitialDLindbergh", "Vf5Lindbergh", "Vf5cLindbergh", "SegaRtv", "SegaJvsLetsGoJungle", "Rambo",
                 "TooSpicy", "SegaRTuned", "GSEVO", "HummerExtreme"}

# Regels van de kern over aanpassingen (A11). De Engelse vorm is wat de kern schrijft; de Nederlandse weergave staat in
# findings/antwoorden_ronde2.md en 13_opengestelde_cores.md ("aanpassing" = patch, "koppeling" = hook).
A11_PATTERNS = [
    ("overgeslagen", re.compile(r"skipped:\s*unexpected original bytes|overgeslagen:\s*onverwachte oorspronkelijke bytes", re.I)),
    ("toegepast", re.compile(r"\b(?:patch(?:ed)?|aangepast|aanpassing)\s+'[^']+'\s+@0x[0-9A-Fa-f]+", re.I)),
    ("koppeling", re.compile(r"\b(?:GOT hook|hook|koppeling)\b\s+'?[\w:~@]+'?\s+(?:@0x|installed|\(0x)", re.I)),
    ("versie", re.compile(r"\bRUNNING\s+\S|^BudgieLoader \d|^OpenParrot\S* \d|^(?:Loader|Core|Game):\s+\S|Version: \d|"
                          r"version \d+\.\d+|Detected Linux game:")),
    ("fout", re.compile(r"could not (?:hook|koppeling)|GAME PATH CHECK FAILED|DENYING|Failed to load dll|"
                        r"Failed to Load DLL|Unhandled exception|DEADC0DE", re.I)),
]
ANSI = re.compile(r"\x1b\[[0-9;]*m")


def log(msg):
    print("[launch-direct %s] %s" % (time.strftime("%H:%M:%S"), msg), flush=True)


class Unsupported(Exception):
    pass


class ActivationMissing(Exception):
    pass


class PatchRefused(Exception):
    """Een ingeschakelde patch kan niet (veilig) toegepast worden; de game start dan niet."""




class SettingRefused(PatchRefused):
    """Een instelling in game.ini (bv. DongleRegion) is niet toegestaan voor dit profiel; de game start dan niet."""


# ---------------------------------------------------------------------------------------------------------------------
# Profiel

def _scalars(root):
    return op.read_scalars(root)


def _fields(root):
    return op.read_fields(root)


def load_profile(tp, name):
    """Het profiel uit TeknoParrotUi's UserProfiles/<P>.xml (terugval voor een game zonder eigen profiel in game.ini)."""
    user = os.path.join(tp, "UserProfiles", name + ".xml")
    stock = os.path.join(tp, "GameProfiles", name + ".xml")
    if not os.path.isfile(user):
        raise Unsupported("geen gebruikersprofiel %s" % user)
    u = ET.parse(user).getroot()
    p = _scalars(u)
    p["ProfileName"] = name
    p["fields"] = _fields(u)
    if os.path.isfile(stock):
        s = ET.parse(stock).getroot()
        ss = _scalars(s)
        # TeknoParrotUi neemt het gebruikersprofiel alleen over als de revisie gelijk is; anders migreert hij het eerst.
        if ss.get("GameProfileRevision", "") != p.get("GameProfileRevision", ""):
            raise Unsupported("profielrevisie %s wijkt af van het sjabloon (%s); start eenmaal via TeknoParrotUi"
                              % (p.get("GameProfileRevision"), ss.get("GameProfileRevision")))
        for k, v in ss.items():
            p.setdefault(k, v)
        # Of een game onder het abonnement valt, bepaalt het meegeleverde sjabloon, niet een aangepast gebruikersprofiel.
        if "Patreon" in ss:
            p["Patreon"] = ss["Patreon"]
    return p


def load_own_profile(tp, name, settings):
    """Het eigen profiel uit game.ini (meegeleverd + gebruikerslaag): geen UserProfiles/<P>.xml nodig. Het pad naar de game wordt
    afgeleid uit de gamemappen ([Identify]); alleen een override van de gebruiker ([Game] GamePath) wordt gelezen.
    Revisiecontrole: ProfileRevision moet gelijk zijn aan GameProfileRevision van het sjabloon in de runtime, anders is het profiel
    gemaakt voor een andere versie van de velden (opnieuw importeren)."""
    stock = op.read_xml_profile(op.template_path(tp, name))
    have = settings.get("Game", {}).get("ProfileRevision", "")
    if stock is not None and stock[0].get("GameProfileRevision", "") != have:
        raise Unsupported("profielrevisie %s in game.ini wijkt af van het sjabloon (%s); importeer het profiel opnieuw: "
                          "scripts/import-tp-profiles.py %s" % (have or "(geen)", stock[0].get("GameProfileRevision", ""), name))
    game, how = op.locate_game(name, settings)
    p = op.profile_from_settings(name, settings, game)
    p["_game_source"] = how
    return p


def needs_activation(p):
    """Informatie-vlag: heeft TeknoParrot dit profiel achter zijn Patreon-abonnement gezet? Komt uit het sjabloon van de runtime en de
    lagen van game.ini. LET OP: dit wordt op onze eigen stack NIET meer afgedwongen (de launch roept check_activation niet meer aan; zie
    docs/decisions.md 2026-10-12). De vlag blijft als informatie (de app kan tonen dat TP hier Patreon voor vraagt)."""
    return p.get("Patreon", "").strip().lower() == "true"


def field(p, name, category=None):
    for f in p["fields"]:
        if f["name"] == name and (category is None or f["category"] == category):
            return f
    return None


def truthy(v):
    return str(v).strip().lower() == "true"


def parrot_data(tp):
    d = {}
    try:
        r = ET.parse(os.path.join(tp, "ParrotData.xml")).getroot()
        d = {c.tag: (c.text or "") for c in r if len(c) == 0}
    except (OSError, ET.ParseError):
        pass
    return d


# ---------------------------------------------------------------------------------------------------------------------
# Paden

def wine_path(p):
    return "Z:" + p.replace("/", "\\")


def on_nas(path):
    """True als het (gevolgde) pad op een netwerkschijf ligt: ~/mnt of cifs/smb/nfs (zelfde regel als de app)."""
    real = os.path.realpath(path)
    if real.startswith(os.path.join(HOME, "mnt") + "/"):
        return True
    try:
        best, fstype = "", ""
        with open("/proc/self/mounts") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 3:
                    mp = parts[1].replace("\\040", " ")
                    if (real == mp or real.startswith(mp.rstrip("/") + "/")) and len(mp) > len(best):
                        best, fstype = mp, parts[2]
        return fstype in ("cifs", "smb3", "smbfs", "nfs", "nfs4", "fuse.sshfs")
    except OSError:
        return False


# ---------------------------------------------------------------------------------------------------------------------
# teknoparrot.ini (zelfde opbouw als TeknoParrotUi: [GameInfo], [GlobalHotkeys], [GlobalScore], dan per categorie)

def build_ini(p, pd, windowed_override=False):
    out = ""
    if p.get("GameVersion"):
        out += "[GameInfo]\nGameVersion=%s\n" % p["GameVersion"]
    out += "[GlobalHotkeys]\nExitKey=%s\nPauseKey=%s\n" % (pd.get("ExitGameKey", "0x1B"), pd.get("PauseGameKey", "0x13"))
    if any(f["name"] == "Enable Submission" and f["value"] == "1" for f in p["fields"]):
        out += "[GlobalScore]\nSubmission ID=%s\nCollapseGUIKey=%s\n" % (pd.get("ScoreSubmissionID", ""),
                                                                          pd.get("ScoreCollapseGUIKey", ""))
    cats = []
    for f in p["fields"]:
        if f["category"] not in cats:
            cats.append(f["category"])
    for c in cats:
        out += "[%s]\n" % c
        for f in p["fields"]:
            if f["category"] != c:
                continue
            v = f["value"]
            if windowed_override and f["name"] == "Windowed":
                v = "1"
            if f["type"] == "DropdownIndex":
                v = str(f["options"].index(v)) if v in f["options"] else "-1"
            out += "%s=%s\n" % (f["name"], v)
    return out


def taito_gun_ini(p):
    """MINIGUN.INI / MUSEUM.INI / HAUNTED2.INI zoals TeknoParrotUi ze vóór de start schrijft (Taito Type X2-geweren)."""
    t = TAITO_GUN_INI.get(p.get("EmulationProfile", ""))
    if not t:
        return None
    name, cnf = t

    def num(n, d):
        f = field(p, n)
        try:
            return int(f["value"]) if f else d
        except ValueError:
            return d
    w, h = num("ResolutionWidth", 1280), num("ResolutionHeight", 720)
    region = (field(p, "Region") or {}).get("value", "")
    text = ("REGION\t\t%s\r\nCNFNAME\t\t.\\OpenParrot\\%s\r\nRANKFILE\t.\\OpenParrot\\\r\nPRJENABLE   \t1\r\n"
            "SCREEN_WIDTH\t%d\r\nSCREEN_HEIGHT\t%d\r\nRENDER_WIDTH\t%d\r\nRENDER_HEIGHT\t%d\r\n"
            "RENDER_WIDTH3D\t%d\r\nRENDER_HEIGHT3D\t%d\r\n" % (region, cnf, w, h, w, h, w, h))
    return name, text


def place_file(path, text, dry=False):
    """Schrijft een gegenereerd bestand naast de game, maar nooit op de NAS. Geeft (status, uitleg).
    dry: alleen zeggen wat er zou gebeuren."""
    data = text.encode()
    try:
        with open(path, "rb") as f:
            cur = f.read()
    except FileNotFoundError:
        cur = None
    if cur == data:
        return "gelijk", "%s is al actueel" % path
    if on_nas(path):
        return "nas", ("%s ligt op de NAS en %s; niet geschreven (de game gebruikt het bestaande bestand)"
                       % (path, "ontbreekt" if cur is None else "wijkt af"))
    if dry:
        return "schrijven", "%s wordt %s" % (path, "aangemaakt" if cur is None else "bijgewerkt")
    if os.path.islink(path):
        os.unlink(path)   # een symlink in een lokale view wordt een eigen lokaal bestand
    tmp = path + ".ravengun-tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)
    return "geschreven", "%s geschreven" % path


def place_binary(src, dest):
    """Kopieert een drop-in (docs/dropins.md) naar de view van de game. Nooit naar de NAS.
    Een symlink in de view wijst naar het origineel; die halen we eerst weg, zodat het origineel
    onaangeroerd blijft en de game onze eigen versie ziet. Geeft (status, uitleg)."""
    try:
        with open(src, "rb") as f:
            data = f.read()
    except OSError as e:
        return "fout", "%s is niet te lezen: %s" % (src, e)
    if on_nas(dest):
        return "nas", ("%s ligt op de NAS; niet geschreven. Maak eerst een lokale view "
                       "(scripts/tp-add-game.py <Game> <pad> --view)" % dest)
    try:
        if os.path.isfile(dest):
            with open(dest, "rb") as f:
                if f.read() == data:
                    return "gelijk", "%s is al actueel" % dest
    except OSError:
        pass
    try:
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        if os.path.islink(dest):
            os.unlink(dest)          # een symlink naar het origineel wordt ons eigen bestand
        tmp = dest + ".ravengun-tmp"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, dest)
        os.chmod(dest, 0o755)
    except OSError as e:
        return "fout", "%s niet te schrijven: %s" % (dest, e)
    return "geschreven", "%s geschreven (%d bytes)" % (dest, len(data))


# ---------------------------------------------------------------------------------------------------------------------
# Drop-ins (docs/dropins.md): patches/<Game>/dropin/ + dropin.ini

def dropin_specs(game):
    """De drop-ins van deze game: ([{name, to, dll, env, preload, src, ini}], foutenlijst).
    De gebruikerslaag (dropin.ini) gaat voor op de meegeleverde map; een sectie met dezelfde naam
    in de meegeleverde map wordt dan overgeslagen."""
    import configparser
    specs, fouten = {}, []
    for d in (gs.user_dir(), gs.shipped_dir()):
        pad = os.path.join(d, game, "dropin", "dropin.ini")
        if not os.path.isfile(pad):
            continue
        cp = configparser.RawConfigParser()
        cp.optionxform = str          # sleutelnamen hoofdlettergevoelig laten (To, DllOverride, Env, Preload)
        try:
            # utf-8-sig: een dropin.ini die met Windows-Kladblok is gemaakt begint met een BOM, en
            # configparser ziet de eerste regel dan niet als commentaar ("File contains no section
            # header"). Zonder dit wordt zo'n drop-in stil niet uitgerold.
            with open(pad, encoding="utf-8-sig") as f:
                cp.read_file(f)
        except (OSError, configparser.Error) as e:
            fouten.append("kan %s niet lezen: %s" % (pad, e))
            continue
        dropin_dir = os.path.dirname(pad)
        for naam in cp.sections():
            if naam in specs:
                continue              # de gebruikerslaag won al
            sectie = dict(cp.items(naam))
            # dest = view + To + sectienaam. Staat het pad ook al in de sectienaam, dan komt het
            # bestand dubbel in de view te staan (view/ravengun-hasp/Halo/ravengun-hasp/Halo/
            # dongle.json) en vindt de game het nooit. Dat is precies wat er bij de dongles misging.
            to = (sectie.get("To", "") or ".").strip() or "."
            if to != "." and naam.startswith(to.rstrip("/") + "/"):
                fouten.append("%s [%s]: de sectienaam begint ook met To = %s, dus het bestand komt "
                              "dubbel in de view te staan; laat To weg of zet To = . (docs/dropins.md)"
                              % (pad, naam, to))
                continue
            env = {}
            for stuk in (sectie.get("Env", "") or "").split(";"):
                stuk = stuk.strip()
                if not stuk:
                    continue
                if "=" not in stuk:
                    fouten.append("%s [%s]: Env-regel %r is geen SLEUTEL=WAARDE" % (pad, naam, stuk))
                    continue
                k, v = stuk.split("=", 1)
                env[k.strip()] = v.strip()
            override = (sectie.get("DllOverride", "") or "").strip().lower()
            if override and override not in ("native", "builtin,native", "builtin"):
                fouten.append("%s [%s]: DllOverride = %r (kies native of builtin,native)" % (pad, naam, override))
                continue
            specs[naam] = {"name": naam, "to": (sectie.get("To", "") or ".").strip() or ".",
                           "dll": override, "env": env, "preload": truthy(sectie.get("Preload", "")),
                           # Audit: mag dit bestand in LD_AUDIT van de Wine-start? Zie dropin_plan().
                           "audit": truthy(sectie.get("Audit", "true")),
                           "dir": dropin_dir, "ini": pad, "src": os.path.join(dropin_dir, naam)}
    return list(specs.values()), fouten


def _binnen(path, base):
    """True als `path` lexicaal binnen `base` ligt."""
    p, b = os.path.abspath(path), os.path.abspath(base)
    return p == b or p.startswith(b + os.sep)


def view_wortel(base):
    """De wortel van de view waar `base` in ligt: views/<Game>. Ligt `base` niet onder de views-map (een
    game in een gewone lokale map), dan is `base` zelf de wortel."""
    views = os.path.abspath(op.views_dir())
    b = os.path.abspath(base)
    if b != views and _binnen(b, views):
        return os.path.join(views, os.path.relpath(b, views).split(os.sep)[0])
    return b


def _dropin_keten(base, dest, wortel):
    """(wortel, [padonderdelen van de wortel tot en met de map van dest]) of (None, foutmelding)."""
    base = os.path.abspath(base)
    dest = os.path.abspath(dest)
    wortel = os.path.abspath(wortel) if wortel else base
    if not _binnen(dest, base):
        return None, "het doel %s ligt buiten de view %s" % (dest, base)
    if not _binnen(base, wortel):
        return None, "de gamemap %s ligt buiten de view %s" % (base, wortel)
    if wortel != base and os.path.islink(wortel):
        return None, "de view %s is zelf een symlink (naar %s); maak de view opnieuw" % (wortel, os.path.realpath(wortel))
    rel = os.path.relpath(os.path.dirname(dest), wortel)
    return wortel, ([] if rel == "." else rel.split(os.sep))


def dropin_toets(base, dest, wortel=None):
    """Kan `dest` lokaal in de view geplaatst worden? Verandert NIETS (voor --plan en voor de weigering
    vooraf). Geeft (ok, melding, [mappen die run() eerst lokaal maakt]).

    Een symlink naar een map op de weg is geen fout: die maakt dropin_materialiseer() lokaal. Wel fout:
    een doel buiten de view, een view die zelf een symlink is, en iets op de weg dat geen map is of
    een symlink naar iets dat geen map is. Dan komt het bestand nergens of buiten de view terecht, en
    een run zonder zijn drop-in is een ongeldige run."""
    wortel, delen = _dropin_keten(base, dest, wortel)
    if wortel is None:
        return False, delen, []
    # Geen view om de gamemap heen (de wortel IS de gamemap) en de gamemap loopt zelf via een symlink: dan
    # is er niets boven de gamemap dat lokaal gemaakt kan worden, en dropin_materialiseer() weigert het
    # doel achteraf ("komt ... nog buiten de view uit"). Dat hoort het plan al te zeggen; anders meldt
    # --plan een geldige start die --run daarna weigert. (Symlinks ONDER de gamemap maakt run() lokaal;
    # na het materialiseren ligt de map van het doel dus op realpath(gamemap) + het relatieve pad.)
    rel = os.path.relpath(os.path.dirname(os.path.abspath(dest)), wortel)
    if wortel == os.path.abspath(base) and not _binnen(os.path.join(os.path.realpath(wortel), rel), wortel):
        return False, ("de gamemap %s loopt via een symlink naar %s en er is geen view omheen om lokaal te "
                       "maken; het bestand zou buiten de gamemap terechtkomen (maak een view: "
                       "scripts/tp-add-game.py <Spel> <pad> --view)" % (wortel, os.path.realpath(wortel))), []
    lokaal, cur, werkelijk, gevolgd = [], wortel, wortel, False
    for deel in delen:
        cur = os.path.join(cur, deel)
        kandidaat = os.path.join(werkelijk, deel)
        if not os.path.lexists(kandidaat):
            break                       # bestaat nog niet: wordt een nieuwe lokale map, en alles eronder ook
        if os.path.islink(kandidaat) or gevolgd:
            doel = os.path.realpath(kandidaat)
            if not os.path.isdir(doel):
                return False, "%s wijst naar %s en dat is geen map" % (cur, doel), []
            lokaal.append(cur)
            werkelijk, gevolgd = doel, True
        elif os.path.isdir(kandidaat):
            werkelijk = kandidaat
        else:
            return False, "%s is een bestand, geen map" % cur, []
    return True, "de map van %s is of wordt lokaal" % os.path.relpath(os.path.abspath(dest), wortel), lokaal


def dropin_materialiseer(base, dest, log=None, wortel=None):
    """Zorg dat de map van `dest` een ECHTE map in de view is, ook als er een symlink naar de NAS
    tussen zit. Elke symlink-map op de weg wordt vervangen door een echte map met symlinks naar de
    inhoud, zodat alleen het drop-in-bestand zelf echt wordt en de rest hetzelfde blijft.

    Waarom: de views van de games zijn symlink-boerderijen naar de NAS (views/<Game>/Game_Data is
    bijvoorbeeld een symlink). Een drop-in met To = Game_Data/Plugins wijst dan via die symlink naar
    de NAS, en de regel "nooit naar de NAS schrijven" zou hem weigeren. In plaats van weigeren maken
    we de map lokaal -- dat is precies wat een view is.

    `wortel` is de wortel van de view (view_wortel). De symlink kan ook BOVEN de gamemap zitten:
    views/LuigisMansion/exe was een symlink naar de NAS en de game staat in exe/x64, dus een drop-in
    met To = . kwam via `exe` op de NAS uit. Daarom loopt de weg vanaf de wortel en niet vanaf de
    gamemap. Zonder `wortel` begint de weg bij `base` (zoals voorheen).

    Schrijft NOOIT op de NAS: er wordt alleen in de view gematerialiseerd en er wordt gecontroleerd
    dat de map van het doel daarna echt binnen de view uitkomt. Geeft (ok, melding)."""
    dest = os.path.abspath(dest)
    wortel, delen = _dropin_keten(base, dest, wortel)
    if wortel is None:
        return False, delen
    cur = wortel
    for deel in delen:
        cur = os.path.join(cur, deel)
        if os.path.islink(cur):
            doel = os.path.realpath(cur)
            if not os.path.isdir(doel):
                return False, "%s is een symlink naar iets dat geen map is" % cur
            op.maak_map_lokaal(cur)
            if log:
                log("%s was een symlink naar de NAS; vervangen door een echte map met symlinks"
                    % os.path.relpath(cur, wortel))
        elif os.path.lexists(cur) and not os.path.isdir(cur):
            return False, "%s is een bestand, geen map" % cur
        elif not os.path.isdir(cur):
            os.makedirs(cur, exist_ok=True)
    if os.path.islink(dest):
        os.unlink(dest)
    # De wortel zelf is geen symlink (hierboven getoetst); wat erboven ligt (bv. een gekoppelde
    # thuismap) mag wel gevolgd worden. Begint de weg bij de gamemap, dan geldt die letterlijk.
    anker = os.path.join(os.path.realpath(os.path.dirname(wortel)), os.path.basename(wortel)) \
        if wortel != os.path.abspath(base) else wortel
    if not _binnen(os.path.realpath(os.path.dirname(dest)), anker):
        return False, "de map van het doel %s komt na het materialiseren nog buiten de view uit" % dest
    return True, "de map van %s is lokaal" % os.path.relpath(dest, wortel)


def dropin_plan(plan):
    """Wat de drop-ins van deze game doen: welke bestanden naar de view gaan, welke omgevingsvariabelen
    en welke WINEDLLOVERRIDES/LD_PRELOAD erbij komen. Kopieert niets (dat doet run()).

    Preload = true betekent: dit bestand moet in het proces van de GAME geladen worden. Dat gaat hier NIET
    via LD_PRELOAD in de omgeving van de start. Gemeten (Halo, 2026-10-11): deze launcher start altijd Wine,
    en Wine geeft zijn omgeving door aan alles wat het start -- ook aan BudgieLoader, dat een Cygwin-programma
    is. Cygwin leest LD_PRELOAD en sterft eraan ("fatal error - error while loading shared libraries"), nog
    voordat de game begint. Het pad gaat daarom mee als RG_DROPIN_PRELOAD; de native route
    (scripts/play-native.sh) en de eigen lader zetten dat in LD_PRELOAD van het ELF-proces zelf. Zie
    docs/hasp-layer.md, "Waarom LD_PRELOAD hier niet in de omgeving van Wine gaat"."""
    game = plan["profile"]
    specs, fouten = dropin_specs(game)
    base = os.path.dirname(plan["game"])
    wortel = view_wortel(base)
    env, preload, overrides, bestanden = {}, [], [], []
    audit, geen_audit, materialiseren = [], [], []
    for s in specs:
        if not os.path.isfile(s["src"]):
            fouten.append("%s [%s]: %s ontbreekt in dropin/" % (s["ini"], s["name"], s["name"]))
            continue
        dest = os.path.normpath(os.path.join(base, s["to"], s["name"]))
        # De view van een game is een symlink-boerderij naar de NAS: views/<Game>/Game_Data is
        # bijvoorbeeld een symlink. Het lexicale pad ligt dan in de view terwijl het gevolgde pad op
        # de NAS uitkomt. Dat is geen fout: run() materialiseert die map (dropin_materialiseer).
        # Alleen als er geen view is (de game draait rechtstreeks van de NAS) of als het doel buiten
        # de view valt, weigeren we -- er wordt nooit op de NAS geschreven.
        #
        # Let op: daarom toetsen we het LEXICALE pad aan de views-map en niet aan on_nas(base). Bij
        # een game die diep in zijn view staat (TC5: views/TC5/TC5/Binaries/Win64) komt het gevolgde
        # pad op de NAS uit, terwijl de view er wel is; on_nas() zou die drop-in onterecht weigeren.
        if on_nas(base) and not _binnen(base, op.views_dir()):
            fouten.append("%s [%s]: deze game draait rechtstreeks van de NAS, dus er is geen view om "
                          "het bestand in te zetten; maak eerst een lokale view "
                          "(scripts/tp-add-game.py %s <pad> --view)" % (s["ini"], s["name"], game))
            continue
        if not _binnen(dest, base):
            fouten.append("%s [%s]: het doel %s ligt buiten de view %s"
                          % (s["ini"], s["name"], dest, base))
            continue
        # Vooraf toetsen of het bestand ook echt LOKAAL in de view kan komen. Kan dat niet, dan is het een
        # fout en geen waarschuwing: de run zou doorgaan zonder zijn drop-in.
        ok, waarom, lokaal = dropin_toets(base, dest, wortel)
        if not ok:
            fouten.append("%s [%s]: niet te plaatsen: %s" % (s["ini"], s["name"], waarom))
            continue
        materialiseren += [m for m in lokaal if m not in materialiseren]
        bestanden.append({"src": s["src"], "dest": dest, "name": s["name"]})
        if s["dll"]:
            dllnaam = s["name"][:-4] if s["name"].lower().endswith(".dll") else s["name"]
            overrides.append("%s=%s" % (dllnaam, "n" if s["dll"] == "native" else "n,b"))
        if s["preload"]:
            preload.append(dest)
            if s.get("audit", True):
                audit.append(dest)
            else:
                geen_audit.append(dest)
        env.update(s["env"])
    if overrides:
        delen = [x for x in (plan["env"].get("WINEDLLOVERRIDES", "") or "").split(";") if x]
        env["WINEDLLOVERRIDES"] = ";".join(delen + [o for o in overrides if o not in delen])
    if preload:
        # Waar het pad heen gaat hangt af van hoe de game start, en dat is gemeten werk:
        #   Wine + Linux-ELF (ElfLdr2)  -> LD_AUDIT. LD_PRELOAD is fataal: BudgieLoader is een
        #       Cygwin-programma en sterft eraan ("fatal error - error while loading shared
        #       libraries"). Cygwin kent LD_AUDIT niet, glibc wel, en de bibliotheek komt in
        #       hetzelfde adresbeeld als de game. Bewijs: docs/rt-glfix.md.
        #   echte native start          -> LD_PRELOAD, dat werkt daar gewoon.
        #   Wine + Windows-spel         -> TP_PRELOAD_DLL, de variabele die de OpenParrot-lader leest
        #       (OpenParrotLoader/src/main.cpp): na het laden van de kern laadt hij die ene DLL in het
        #       spelproces, voordat het spel verdergaat. RG_DROPIN_PRELOAD leest daar niemand; een
        #       drop-in met Preload = true kwam op deze route dus nooit in het spel (gemeten op
        #       Luigi's Mansion: 0 van de 2 bestanden in /proc/<pid>/maps).
        elf_onder_wine = bool(plan.get("wine")) and not is_pe(plan["game"])
        pe_onder_wine = bool(plan.get("wine")) and is_pe(plan["game"])
        if elf_onder_wine:
            var = "LD_AUDIT"
        elif not plan.get("wine"):
            var = "LD_PRELOAD"
        else:
            var = ""
        if pe_onder_wine:
            # De lader leest EEN pad (geen lijst, geen scheidingsteken) en geeft het aan LoadLibraryW in
            # het spelproces: het moet dus een pad zijn dat Wine begrijpt (Z:\...). Bestaat het bestand
            # niet of laadt het niet, dan beeindigt de lader het spel en geeft hij zelf 0 terug.
            lader = _lader_naam(plan)
            al = (plan["env"].get("TP_PRELOAD_DLL", "") or "").strip()
            if lader.lower() not in TP_PRELOAD_LADERS:
                fouten.append("Preload = true (%s): de lader %s leest geen TP_PRELOAD_DLL, dus op deze route "
                              "is er geen kanaal om een DLL in het spelproces te laden"
                              % (", ".join(os.path.basename(x) for x in preload), lader or "(onbekend)"))
            elif len(preload) > 1:
                fouten.append("Preload = true staat bij %d bestanden (%s), maar de lader laadt er via "
                              "TP_PRELOAD_DLL precies een; laat de ene DLL de andere laden"
                              % (len(preload), ", ".join(os.path.basename(x) for x in preload)))
            elif al and al != wine_path(preload[0]):
                fouten.append("Preload = true (%s): TP_PRELOAD_DLL staat al op %s en de lader laadt er "
                              "precies een" % (os.path.basename(preload[0]), al))
            else:
                env["TP_PRELOAD_DLL"] = wine_path(preload[0])
        if var:
            # LD_AUDIT laadt de bibliotheek in het proces van de Cygwin-lader (ElfLdr2 laadt de
            # game-ELF in dat proces). Dat is voor librt-glfix het werkende kanaal, maar een
            # inline-hook die de game wil aanhaken sterft er en neemt de lader mee: gemeten met
            # libravengun-hasphook (Halo exitcode 1, JurassicPark SIGSEGV). Een drop-in die dat
            # niet wil zet `Audit = false`; het pad blijft dan in RG_DROPIN_PRELOAD staan.
            bron = audit if var == "LD_AUDIT" else preload
            delen = [x for x in (plan["env"].get(var, "") or "").split(":") if x]
            waarde = ":".join(delen + [p for p in bron if p not in delen])
            if waarde:            # een lege LD_AUDIT hoort niet in de omgeving
                env[var] = waarde
        else:
            delen = [x for x in (plan["env"].get("RG_DROPIN_PRELOAD", "") or "").split(":") if x]
            env["RG_DROPIN_PRELOAD"] = ":".join(delen + [p for p in preload if p not in delen])
        # Het pad blijft altijd ook als RG_DROPIN_PRELOAD staan, zodat een eigen lader het kan oppakken.
        delen = [x for x in (plan["env"].get("RG_DROPIN_PRELOAD", "") or "").split(":") if x]
        env["RG_DROPIN_PRELOAD"] = ":".join(delen + [p for p in preload if p not in delen])
    # Per preload-bestand: in welke omgevingsvariabele het belandt en of die het ook LAADT. Zo hoeft een
    # meting niet achteraf te ontdekken dat een bestand nooit in het spelproces kwam.
    kanalen = []
    for pad in preload:
        if env.get("TP_PRELOAD_DLL") == wine_path(pad):
            kanalen.append({"file": pad, "var": "TP_PRELOAD_DLL", "value": env["TP_PRELOAD_DLL"], "loads": True,
                            "by": "de OpenParrot-lader (%s) laadt deze DLL na de kern in het spelproces" % _lader_naam(plan)})
        elif pad in (env.get("LD_AUDIT", "") or "").split(":"):
            kanalen.append({"file": pad, "var": "LD_AUDIT", "value": pad, "loads": True,
                            "by": "glibc laadt dit in het proces van de Cygwin-lader, waar de game-ELF in geladen wordt"})
        elif pad in (env.get("LD_PRELOAD", "") or "").split(":"):
            kanalen.append({"file": pad, "var": "LD_PRELOAD", "value": pad, "loads": True,
                            "by": "de dynamische linker van de native start"})
        else:
            kanalen.append({"file": pad, "var": "RG_DROPIN_PRELOAD", "value": pad, "loads": False,
                            "by": "NIEMAND op deze route: alleen de native route en de eigen ELF-lader lezen deze variabele"})
    return {"base": base, "root": wortel, "files": bestanden, "env": env, "overrides": overrides, "preload": preload,
            "preload_env": kanalen, "materialise": materialiseren,
            "audit": audit, "geen_audit": geen_audit,
            "specs": specs, "errors": fouten}


TP_PRELOAD_LADERS = ("openparrotloader.exe", "openparrotloader64.exe")   # deze laders lezen TP_PRELOAD_DLL (gemeten: strings)
EEN_PARAMETER_LADERS = TP_PRELOAD_LADERS   # en nemen precies EEN parameter voor het spel (OpenParrotLoader/src/main.cpp: argc 3 of 4)


def _lader_naam(plan):
    """De bestandsnaam van de lader in het startplan (het argument na wine); leeg als die er niet is."""
    argv, wine = plan.get("argv") or [], plan.get("wine")
    if wine and wine in argv and argv.index(wine) + 1 < len(argv):
        return os.path.basename(argv[argv.index(wine) + 1])
    return ""


def dropin_log_lines(plan):
    """Eén regel per drop-in, voor het logboek en voor --plan."""
    dp = plan.get("dropins") or {}
    uit = []
    for e in dp.get("errors", []):
        uit.append("FOUT: %s" % e)
    for m in dp.get("materialise", []):
        uit.append("%s loopt via een symlink; wordt eerst een echte map met symlinks" % m)
    for f in dp.get("files", []):
        uit.append("%s -> %s" % (f["name"], f["dest"]))
    if dp.get("overrides"):
        uit.append("WINEDLLOVERRIDES += %s" % ", ".join(dp["overrides"]))
    for k in dp.get("preload_env", []):
        if k["loads"]:
            uit.append("preload: %s -> %s=%s (%s)" % (os.path.basename(k["file"]), k["var"], k["value"], k["by"]))
        else:
            uit.append("preload: %s -> alleen %s=%s; WORDT NIET GELADEN door %s"
                       % (os.path.basename(k["file"]), k["var"], k["value"], k["by"]))
    # Audit = false zegt alleen iets over LD_AUDIT; een bestand dat langs een ander kanaal geladen wordt
    # (TP_PRELOAD_DLL, LD_PRELOAD) hoort hier niet nog eens als "niet geladen" te staan.
    geladen = {k["file"] for k in dp.get("preload_env", []) if k["loads"]}
    if [x for x in dp.get("geen_audit", []) if x not in geladen]:
        uit.append("niet in LD_AUDIT (Audit = false): %s -- deze bibliotheek hoort in het "
                   "spelproces en niet in het Cygwin-proces van de lader"
                   % ", ".join(x for x in dp["geen_audit"] if x not in geladen))
    return uit


def merge_env(plan, extra):
    """Zet omgevingsvariabelen in het plan (zowel env als launch_env, zodat het logboek ze toont).
    LD_PRELOAD en WINEDLLOVERRIDES worden samengevoegd in plaats van overschreven."""
    for k, v in (extra or {}).items():
        for doel in (plan["env"], plan["launch_env"]):
            if k == "LD_PRELOAD" and doel.get(k):
                delen = [x for x in doel[k].split(":") if x]
                if v not in delen:
                    delen.append(v)
                doel[k] = ":".join(delen)
            elif k == "WINEDLLOVERRIDES" and doel.get(k):
                delen = [x for x in doel[k].split(";") if x]
                for stuk in str(v).split(";"):
                    if stuk and stuk not in delen:
                        delen.append(stuk)
                doel[k] = ";".join(delen)
            else:
                doel[k] = v


def is_pe(path):
    """True als dit een Windows-programma is (MZ-header). De games onder Proton zijn dat."""
    try:
        with open(path, "rb") as f:
            return f.read(2) == b"MZ"
    except OSError:
        return False


# ---------------------------------------------------------------------------------------------------------------------
# Startplan

def plan_motor_patches(settings, exe, loader, core, materialize):
    """Wat de patchmotor (scripts/patchfile.py) toevoegt aan wat plan_file_patches() al doet.

    plan_file_patches() doet Kind = file op de lader en de kern. De motor doet daarnaast:
      * Kind = file met de GAME zelf als doel;
      * Kind = memory op de hoofdmodule, door Module + Offset via de sectie- of segmenttabel naar een
        bestandsplaats om te rekenen (scripts/addrmap.py).
    Patches die de launcher al kon, worden hier niet nog eens gedaan: de motor slaat ze over en dit
    verslag is alleen wat er nieuw bij komt.

    Geeft ({rol: pad van de gepatchte kopie}, [verslagregel]). Weigert een ingeschakelde patch die
    niet toe te passen is: dan start de game niet (net als bij plan_file_patches)."""
    try:
        rep = pf.apply_game_obj(settings, settings.profile, exe, loader=loader, core=core, materialize=False)
    except pf.Refused as e:
        raise PatchRefused("de patchmotor: %s" % e)
    except Exception as e:      # noqa: BLE001 -- de motor mag de launcher niet laten omvallen
        raise PatchRefused("de patchmotor: %s: %s" % (type(e).__name__, e))

    geweigerd = [r for r in rep.results if r.status == "geweigerd"]
    if geweigerd:
        raise PatchRefused("patch '%s' geweigerd: %s" % (geweigerd[0].name, geweigerd[0].reason))

    if materialize and any(r.status == "gecontroleerd" for r in rep.results):
        try:
            rep = pf.apply_game_obj(settings, settings.profile, exe, loader=loader, core=core, materialize=True)
        except pf.Refused as e:
            raise PatchRefused("de patchmotor: %s" % e)

    verslag = []
    for r in rep.results:
        if r.status not in ("toegepast", "gecontroleerd", "geweigerd"):
            continue
        verslag.append({"name": r.name, "kind": r.kind, "category": "", "version": "",
                        "target": r.target, "result": r.status, "plek": r.place,
                        "reden": r.reason, "status": r.status})
    return dict(rep.written), verslag


def resolve_loader(p, tp):
    et = p.get("EmulatorType", "")
    is64 = truthy(p.get("Is64Bit", "false"))
    exe = "OpenParrotx64/OpenParrotLoader64.exe" if is64 else "OpenParrotWin32/OpenParrotLoader.exe"
    dll = ""
    if et == "ElfLdr2":
        exe = "ElfLdr2/x64/BudgieLoader_x64.exe" if is64 else "ElfLdr2/BudgieLoader.exe"
    elif et == "Lindbergh":
        exe = "TeknoParrot/BudgieLoader.exe"
    elif et == "N2":
        exe = "N2/BudgieLoader.exe"
    elif et == "TeknoMacaw":
        exe = "TeknoParrot/TeknoMacaw64.exe" if is64 else "TeknoParrot/TeknoMacaw.exe"
    elif et == "OpenParrot":
        dll = "OpenParrotx64/OpenParrot64" if is64 else "OpenParrotWin32/OpenParrot"
    elif et == "OpenParrotKonami":
        exe = "OpenParrotWin32/OpenParrotKonamiLoader.exe"
    elif et == "TeknoParrot":
        dll = "TeknoParrot/TeknoParrot64" if is64 else "TeknoParrot/TeknoParrot"
    else:
        raise Unsupported("EmulatorType %s wordt niet direct gestart (externe emulator of eigen voorbereiding)" % et)
    return exe, dll, is64


OWN_BUILD_DEFAULT = "~/.local/share/ravengun/openparrot/current"   # scripts/build-openparrot.sh zet de verwijzing "current"


def own_build(loader_settings, et, exe_rel, dll_rel, is64=False):
    """[Loader] Build = ravengun: de OpenParrot-lader (en -kern) uit de eigen build (scripts/build-openparrot.sh) in plaats van
    uit de TeknoParrot-runtime. Geeft (lader, kern-map, kern-pad); (None, None, None) = alles uit de runtime (standaard).
      Build = teknoparrot | ravengun   lader en, bij EmulatorType OpenParrot, de kern
      Core  = teknoparrot | ravengun   alleen de kern apart (bv. eigen lader met de kern van TeknoParrot); leeg = volgt Build
      Path  = <map>                    map met OpenParrotWin32/ en OpenParrotx64/ (standaard: .../ravengun/openparrot/current)
    Core = ravengun geldt ook voor EmulatorType TeknoParrot (bisectie per game): dan komt onze OpenParrot-kern in de plaats
    van TeknoParrot(64).dll, als eerste argument van de lader (die plakt zelf ".dll" erachter). Alleen als Core er uitdrukkelijk
    staat: Build = ravengun alleen laat een TeknoParrot-profiel zijn eigen kern houden. kern-pad is het pad (zonder .dll,
    relatief aan kern-map) van de te gebruiken kern; het volgt is64 voor TeknoParrot, anders is het dll_rel."""
    build = loader_settings.get("Build", "").strip().lower()
    core = loader_settings.get("Core", "").strip().lower()
    for key, val in (("Build", build), ("Core", core)):
        if val not in ("", "teknoparrot", "ravengun"):
            raise SettingRefused("[Loader] %s = %s (kies teknoparrot of ravengun)" % (key, val))
    if not core and build == "ravengun" and et == "OpenParrot":
        core = "ravengun"   # Build volgt voor de kern alleen bij EmulatorType OpenParrot; TeknoParrot-profielen houden hun eigen kern
    if build != "ravengun" and core != "ravengun":
        return None, None, None
    base = gs.expand_home(loader_settings.get("Path", "").strip() or OWN_BUILD_DEFAULT)
    loader = None
    if build == "ravengun":
        if not os.path.basename(exe_rel).lower().startswith("openparrotloader"):
            raise SettingRefused("[Loader] Build = ravengun bestaat alleen voor OpenParrotLoader(64).exe, "
                                 "niet voor %s" % exe_rel)
        loader = os.path.join(base, exe_rel)
    core_dir, core_rel = None, None
    if core == "ravengun":
        if et not in ("OpenParrot", "TeknoParrot") or not dll_rel:
            raise SettingRefused("een eigen kern bestaat alleen voor EmulatorType OpenParrot of TeknoParrot (dit is %s)" % et)
        if not os.path.basename(exe_rel).lower().startswith("openparrotloader"):
            raise SettingRefused("[Loader] Core = ravengun past alleen bij OpenParrotLoader(64).exe, niet bij %s" % exe_rel)
        core_dir = base
        core_rel = dll_rel if et == "OpenParrot" else (
            "OpenParrotx64/OpenParrot64" if is64 else "OpenParrotWin32/OpenParrot")
    for f in (loader, os.path.join(core_dir, core_rel + ".dll") if core_dir else None):
        if f and not os.path.isfile(f):
            raise SettingRefused("eigen OpenParrot-build ontbreekt: %s (bouwen: scripts/build-openparrot.sh)" % f)
    return loader, core_dir, core_rel


def file_sha256(path):
    """sha256 (hex) van een bestand, of None als het er niet is; voor --plan en het logboek (reproduceerbaarheid)."""
    try:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return None


# ---------------------------------------------------------------------------------------------------------------------
# Patches (Kind = file): zoek-en-vervang op een lokale kopie van een bestand van de runtime (docs/per-game-patches.md).
# De kennis zelf (welke bytes, waarom) staat in patches.ini; dit is alleen de machine. Regels:
#  - het origineel wordt nooit gewijzigd; de kopie staat in ~/.local/share/ravengun/patched/<sha-origineel>-<sha-patchset>/;
#  - Find moet precies Count keer voorkomen (en Replace even lang zijn), anders weigert de patch en start de game niet;
#  - nooit een doel op de NAS (en de kopie ook niet).

def patched_root():
    return os.path.join(HOME, ".local/share/ravengun/patched")


def parse_hex(patch, key):
    raw = patch.get(key, "")
    try:
        return bytes.fromhex(re.sub(r"\s+", "", raw))
    except ValueError:
        raise PatchRefused("patch '%s': %s is geen hexadecimale bytereeks (%r)" % (patch.name, key, raw))


def patch_target(patch, targets, tp):
    """Het bestand dat een patch bedoelt: 'loader', 'core' of een pad (relatief aan de runtime) dat een van beide moet zijn."""
    t = patch.get("Target", "").strip()
    if not t:
        raise PatchRefused("patch '%s': Target ontbreekt (loader, core of een pad in de runtime)" % patch.name)
    if t in ("loader", "core"):
        if not targets.get(t):
            raise PatchRefused("patch '%s': deze game heeft geen %s om te patchen" % (patch.name, t))
        return t
    full = os.path.realpath(os.path.join(tp, t))
    for k, v in targets.items():
        if v and os.path.realpath(v) == full:
            return k
    raise PatchRefused("patch '%s': Target %s is niet de lader of de kern van deze game; alleen die twee kunnen gepatcht worden"
                       % (patch.name, t))


def plan_file_patches(game, targets, tp, materialize=False, exe_path=None):
    """Controleert de ingeschakelde patches van een game en geeft ({doel: pad van de gepatchte kopie}, [beschrijving per patch]).
    targets: {"loader": pad, "core": pad of None}. materialize: de kopie ook echt maken (zonder: alleen controleren en het pad
    uitrekenen; een mislukte controle weigert in beide gevallen)."""
    entries, groups = {}, {}
    order = []

    def entry(p, result, plek, reden="", **extra):
        e = {"name": p.name, "kind": p.kind, "category": p.category, "version": p.version, "target": p.get("Target") or p.get("Module"),
             "result": result, "plek": plek, "reden": reden, "status": "ok" if result == "toegepast" else (reden or result)}
        e.update(extra)
        entries[p.name] = e
        return e

    for err in game.patch_errors:
        if err["enabled"]:
            raise PatchRefused(err["message"])   # een afgewezen patch die aan staat: niet stilzwijgend zonder starten
        order.append(err["name"])
        entries[err["name"]] = {"name": err["name"], "kind": "?", "category": "", "version": "", "target": "", "result": "overgeslagen",
                                "plek": "-", "reden": "afgewezen: " + err["message"], "status": "afgewezen: " + err["message"]}
    for p in game.patches:
        order.append(p.name)
        if p.kind == "memory":
            plek = "%s+%s" % (p.get("Module") or "?", p.get("Offset") or "?")
        else:
            plek = p.get("Target") or "-"
        if not p.enabled:
            entry(p, "overgeslagen", plek, "uit")
            continue
        if p.kind != "file":
            entry(p, "overgeslagen", plek, "soort %s wordt niet door RavenGun toegepast%s" % (
                p.kind, " (AppliedBy = core: de OpenParrot-kern doet dit zelf)" if p.applied_by == "core" else ""))
            continue
        tname = patch_target(p, targets, tp)
        want_name = p.get("TargetName", "").strip().lower()
        if want_name and os.path.basename(targets[tname]).lower() != want_name:
            entry(p, "overgeslagen", tname, "niet van toepassing: het doel is %s, de patch geldt voor %s" % (
                os.path.basename(targets[tname]), p.get("TargetName")), target=targets[tname])
            continue
        groups.setdefault(tname, []).append(p)
    if groups and game.get("Game", "ExeSha256"):
        want = game.get("Game", "ExeSha256").strip().lower()
        if not exe_path or not os.path.isfile(exe_path):
            raise PatchRefused("ExeSha256 is gezet maar de exe van de game is er niet (%s)" % exe_path)
        h = hashlib.sha256()
        with open(exe_path, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        if h.hexdigest() != want:
            raise PatchRefused("de patches van %s gelden voor een exe met sha256 %s, maar %s heeft %s (zet ExeSha256 leeg in "
                               "je eigen game.ini om toch te patchen)" % (game.profile, want[:16], exe_path, h.hexdigest()[:16]))
    result = {}
    for tname, plist in groups.items():
        src = targets[tname]
        if on_nas(src):
            raise PatchRefused("patch '%s': het doel %s ligt op de NAS; er wordt nooit een bestand op de NAS gepatcht" % (plist[0].name, src))
        try:
            with open(src, "rb") as f:
                orig = f.read()
        except OSError as e:
            raise PatchRefused("patch '%s': doel %s niet te lezen: %s" % (plist[0].name, src, e))
        data, canon = orig, []
        for p in sorted(plist, key=lambda x: x.name):
            find, rep = parse_hex(p, "Find"), parse_hex(p, "Replace")
            try:
                count = int(p.get("Count", "1") or "1")
            except ValueError:
                raise PatchRefused("patch '%s': Count is geen getal (%r)" % (p.name, p.get("Count")))
            if not find or len(find) != len(rep):
                raise PatchRefused("patch '%s': Find en Replace moeten even lang en niet leeg zijn (%d en %d bytes)" % (p.name, len(find), len(rep)))
            if count < 1:
                raise PatchRefused("patch '%s': Count moet minstens 1 zijn" % p.name)
            found = data.count(find)
            if found != count:
                if p.version:
                    raise PatchRefused("patch '%s' verwacht gameversie %s; de bytes kloppen niet (Find komt %d keer voor in %s, verwacht %d); "
                                       "de game start niet. Zet de patch uit of pas Find aan." % (p.name, p.version, found, src, count))
                raise PatchRefused("patch '%s' geweigerd: Find komt %d keer voor in %s, verwacht %d (andere versie van het bestand?); "
                                   "de game start niet. Zet de patch uit of pas Find aan." % (p.name, found, src, count))
            first = data.find(find)
            data = data.replace(find, rep)
            canon.append("%s\0%s\0%s\0%d" % (p.name, find.hex(), rep.hex(), count))
            entry(p, "toegepast", "%s:0x%X" % (tname, first), target=src, count=found)
        sha_orig = hashlib.sha256(orig).hexdigest()
        sha_set = hashlib.sha256("\n".join(canon).encode()).hexdigest()
        dst_dir = os.path.join(patched_root(), "%s-%s" % (sha_orig[:16], sha_set[:16]))
        dst = os.path.join(dst_dir, os.path.basename(src))
        if on_nas(dst_dir):
            raise PatchRefused("de map voor gepatchte kopieën (%s) ligt op de NAS" % dst_dir)
        if materialize:
            try:
                with open(dst, "rb") as f:
                    have = f.read()
            except OSError:
                have = None
            if have != data:
                os.makedirs(dst_dir, exist_ok=True)
                with open(dst + ".tmp", "wb") as f:
                    f.write(data)
                os.replace(dst + ".tmp", dst)
                log("patch: kopie gemaakt: %s" % dst)
        for e in entries.values():
            if e.get("target") == src and e["result"] == "toegepast":
                e["patched"] = dst
        result[tname] = dst
    report = [entries[n] for n in order if n in entries]
    return result, report


def patch_log_lines(report):
    """De vaste logregel per patch, ook voor uitgeschakelde: <toegepast|overgeslagen> '<naam>' @<plek> [reden]."""
    return ["%s '%s' @%s%s" % (r["result"], r["name"], r["plek"], " [%s]" % r["reden"] if r.get("reden") else "") for r in report]


def resolve_wine(p, env):
    wine = p.get("WineRunnerPath", "")
    if not (wine and os.path.isfile(wine)):
        wine = env.get("TP_WINE", "") if os.path.isfile(env.get("TP_WINE", "")) else DEFAULT_WINE
    # Een Proton-distributie (files/bin/wine met een proton-script) zou via "proton run" moeten: niet nagebouwd.
    bindir = os.path.dirname(wine)
    if os.path.basename(os.path.dirname(bindir)) == "files" and \
            os.path.isfile(os.path.join(os.path.dirname(os.path.dirname(bindir)), "proton")):
        raise Unsupported("Proton-runner (%s) wordt niet direct gestart; gebruik plain Wine" % wine)
    return wine


def resolve_prefix(p, env, wine):
    """De prefix waarin de game draait: RG_WINEPREFIX bij de wrapper wine-prefix, anders onze eigen gedeelde of
    geïsoleerde prefix onder ~/.local/share/ravengun/prefixes/ (voorheen onder TeknoParrotUI/prefixes)."""
    if os.path.basename(wine) == "wine-prefix":
        pfx = env.get("RG_WINEPREFIX", "")
        if not pfx:
            raise Unsupported("WineRunnerPath is de wrapper wine-prefix maar RG_WINEPREFIX ontbreekt in %s.env"
                              % p["ProfileName"])
        return pfx, False
    if env.get("TP_WINEPREFIX"):
        return env["TP_WINEPREFIX"], True
    mode = p.get("WinePrefixMode", "")
    legacy = os.path.join(RG_PREFIXES, p["ProfileName"])
    if mode == "Isolated" or (mode == "" and os.path.isfile(os.path.join(legacy, "system.reg"))):
        return legacy, True
    return SHARED_PREFIX, True


# Gewone profielopties die game.ini mag kiezen ([Game] DongleRegion en PcbRegion): de launcher zet ze in de optie van het profiel zoals de
# rest van het profiel in teknoparrot.ini komt (het profielbestand zelf blijft ongemoeid). Alleen waarden uit de keuzelijst van de optie.
REGION_OPTIONS = ("DongleRegion", "PcbRegion")


def apply_profile_choices(p, settings):
    for key in REGION_OPTIONS:
        want = settings.get("Game", {}).get(key, "").strip()
        if not want:
            continue
        f = field(p, key)
        if f is None:
            raise SettingRefused("game.ini zet %s = %s, maar het profiel %s heeft die optie niet" % (key, want, p["ProfileName"]))
        if want not in f["options"]:
            raise SettingRefused("game.ini zet %s = %s; het profiel %s biedt alleen: %s" % (key, want, p["ProfileName"], ", ".join(f["options"]) or "(geen keuzelijst)"))
        f["value"] = want


def check_activation(prefix):
    """De activering is iets van de runtime: we kijken alleen of TeknoParrotUi hem al in deze prefix zette."""
    act = os.path.join(TPUI_DATA, "activation")
    if os.path.exists(os.path.join(act, "teknoparrot-license.revoked")):
        raise ActivationMissing("het abonnement is gedeactiveerd (teknoparrot-license.revoked)")
    seed = os.path.join(act, "teknoparrot-license.reg")
    if not os.path.isfile(seed):
        raise ActivationMissing("geen activering gevonden (%s)" % seed)
    with open(seed, "rb") as f:
        want = hashlib.sha256(f.read()).hexdigest().upper()
    try:
        with open(os.path.join(prefix, ".tpui-license-seed")) as f:
            have = f.read().strip().upper()
    except OSError:
        have = ""
    if have != want:
        raise ActivationMissing("de activering staat niet (actueel) in %s; start eenmaal via TeknoParrotUi" % prefix)
    # De waarde zelf lezen we niet; alleen of de sleutel er staat.
    ok, insec = False, False
    try:
        with open(os.path.join(prefix, "user.reg"), errors="replace") as f:
            for line in f:
                if line.startswith("["):
                    insec = line.lower().startswith("[software\\\\teknogods\\\\teknoparrot]")
                elif insec and line.startswith('"PatreonSerialKey"='):
                    ok = True
                    break
    except OSError:
        pass
    if not ok:
        raise ActivationMissing("de activering ontbreekt in het register van %s" % prefix)


def display_size(env):
    v = env.get("RG_GAMESCOPE_RES", "")
    m = re.match(r"^(\d+)x(\d+)$", v)
    if m:
        return int(m.group(1)), int(m.group(2))
    try:
        out = subprocess.run(["xrandr", "--current"], capture_output=True, text=True, timeout=5,
                             env=dict(os.environ, DISPLAY=env.get("DISPLAY", ":0"))).stdout
        for line in out.splitlines():
            if " connected primary " in line or " connected " in line:
                m = re.search(r" (\d+)x(\d+)\+\d+\+\d+", line)
                if m:
                    return int(m.group(1)), int(m.group(2))
    except (OSError, subprocess.SubprocessError):
        pass
    return 1920, 1080


# ---------------------------------------------------------------------------------------------------------------------
# Meer dan een start (profielen met HasTwoExecutables): een spel met een los hulpprogramma (een daemon) ernaast.

DEFAULT_SECOND_WAIT = 5.0   # seconden tussen twee starts als game.ini niets zegt ([Launch] SecondWait)
START_STAP = 0.25           # zo vaak wordt tijdens het wachten gekeken of wat al gestart is nog leeft


def exe_comms(*namen):
    """De procesnamen (comm, 15 tekens, kleine letters) van een of meer exe-namen ('a.exe;b.exe' of een pad)."""
    uit = []
    for naam in namen:
        for part in re.split(r"[;|]", naam or ""):
            b = part.replace("\\", "/").rsplit("/", 1)[-1].strip().lower()[:15]
            if b and b not in uit:
                uit.append(b)
    return uit


def in_dezelfde_view(game, second):
    """Het pad van de tweede exe BINNEN de view van de eerste, of (None, reden).

    Het profiel van TeknoParrotUi bewaart twee losse paden (GamePath en GamePath2). Wijst het eerste in een
    view en het tweede rechtstreeks naar de NAS, dan zou het tweede programma buiten de view draaien: zonder
    de drop-ins en de lokale bestanden van die view. Dat mag niet stil gebeuren. Ligt de tweede exe via de
    symlinks van de view op dezelfde plek, dan nemen we het pad in de view; anders is het een fout."""
    wortel = view_wortel(os.path.dirname(game))
    if wortel == os.path.abspath(os.path.dirname(game)):
        return second, ""                          # de eerste staat niet in een view: er valt niets gelijk te trekken
    if _binnen(second, wortel):
        return second, ""
    rel = os.path.relpath(os.path.abspath(game), wortel)
    echt = os.path.realpath(game)
    if echt.endswith(os.sep + rel):                # waar de view naar wijst: de map van het spel zelf
        bron = echt[:-len(rel)].rstrip(os.sep)
        echt2 = os.path.realpath(second)
        if _binnen(echt2, bron):
            kandidaat = os.path.join(wortel, os.path.relpath(echt2, bron))
            try:
                if os.path.isfile(kandidaat) and os.path.samefile(kandidaat, echt2):
                    return kandidaat, ""
            except OSError:
                pass
    return None, ("de tweede exe %s ligt buiten de view %s van de eerste; beide moeten in dezelfde view draaien "
                  "(maak de view opnieuw of zet [Game] GamePath2)" % (second, wortel))


# ---------------------------------------------------------------------------------------------------------------------
# Argumenten voor het spel (docs/per-game-patches.md, "Argumenten voor het spel")

def win_split(tekst):
    """Een opdrachtregel-staart opdelen in argumenten zoals een Windows-programma dat doet (de regels van de C-runtime):
    spaties scheiden, aanhalingstekens houden bij elkaar, 2n backslashes voor een aanhalingsteken worden er n, 2n+1 geven
    n backslashes en een letterlijk aanhalingsteken, en "" binnen aanhalingstekens is een letterlijk aanhalingsteken.
    Een backslash die niet voor een aanhalingsteken staat blijft staan (Windows-paden)."""
    uit, cur, in_q, bezig, i, n = [], [], False, False, 0, len(tekst)
    while i < n:
        c = tekst[i]
        if c == "\\":
            j = i
            while j < n and tekst[j] == "\\":
                j += 1
            aantal = j - i
            if j < n and tekst[j] == '"':
                cur.append("\\" * (aantal // 2))
                if aantal % 2:
                    cur.append('"')
                    j += 1
            else:
                cur.append("\\" * aantal)
            bezig, i = True, j
            continue
        if c == '"':
            if in_q and i + 1 < n and tekst[i + 1] == '"':
                cur.append('"')
                i += 2
                continue
            in_q, bezig = not in_q, True
        elif c in " \t" and not in_q:
            if bezig:
                uit.append("".join(cur))
                cur, bezig = [], False
        else:
            cur.append(c)
            bezig = True
        i += 1
    if bezig:
        uit.append("".join(cur))
    return uit


def spel_argumenten(lader, profiel_delen, eigen_ruw):
    """Hoe de argumenten bij het spel komen: {how, argv, param, list}.

    profiel_delen: de argumenten uit de velden van het TeknoParrot-profiel, al opgedeeld (zoals altijd).
    eigen_ruw:     de tekst van onze eigen sleutel in game.ini, zoals hij er staat.

    De OpenParrot-lader neemt precies EEN parameter voor het spel (OpenParrotLoader/src/main.cpp: `<DLL> <EXE> <param>`,
    argc 3 of 4) en plakt die letterlijk achter het pad van de exe: `"exe" <param>`. Krijgt hij er meer, dan toont hij
    zijn gebruiksregel, wacht op een toets en geeft 0 terug -- het spel start dan niet en niemand merkt het. Daarom
    gaat voor die lader alles als een parameter mee: de tekst uit game.ini letterlijk (zo komt hij bij het spel aan
    zoals hij in het eigen startscript van het spel staat, aanhalingstekens inbegrepen), en de delen uit het profiel
    met een spatie ertussen. Een profiel dat zijn argumenten als een geheel tussen aanhalingstekens zet (de gewoonte
    van TeknoParrot, juist om deze reden) blijft precies werken zoals het deed.

    Andere laders krijgen de argumenten los mee, elk als een eigen element; een argument met spaties blijft dan heel
    omdat er geen shell tussen zit."""
    eigen_ruw = (eigen_ruw or "").strip()
    if os.path.basename(lader).lower() in EEN_PARAMETER_LADERS:
        param = " ".join(x for x in (" ".join(profiel_delen), eigen_ruw) if x)
        return {"how": "one-parameter", "argv": [param] if param else [], "param": param or None, "list": win_split(param)}
    lijst = list(profiel_delen) + win_split(eigen_ruw)
    return {"how": "separate", "argv": lijst, "param": None, "list": lijst}


# ---------------------------------------------------------------------------------------------------------------------
# Het spel vanaf een ander pad starten (--exe, [Launch] Exe) en schijfletters voor een run (--drive, [Launch] Drives)

DRIVES_STATE = os.path.join(STATE_DIR, "drives.json")   # welke schijfletters wij in welke prefix hebben aangemaakt
VASTE_LETTERS = ("C", "Z")                                # de prefix zelf en de Wine-wortel: nooit om te leggen


def _zoek_zonder_hoofdletters(basis, rel):
    """Een pad onder `basis` vinden zoals Windows dat doet (hoofdletters maken niet uit), of None."""
    cur = basis
    for deel in [d for d in rel.replace("\\", "/").split("/") if d and d != "."]:
        if deel == "..":
            return None
        kandidaat = os.path.join(cur, deel)
        if not os.path.lexists(kandidaat):
            try:
                gelijk = [n for n in os.listdir(cur) if n.lower() == deel.lower()]
            except OSError:
                return None
            if len(gelijk) != 1:
                return None
            kandidaat = os.path.join(cur, gelijk[0])
        cur = kandidaat
    return cur


def plan_drives(ini_tekst, cli, wortel, prefix):
    """De schijfletters van deze run: [{letter, target, link, present}]. Uit [Launch] Drives ("X=.; Y=~/pad") en
    --drive X=PAD (die wint). Een relatief pad is relatief aan de view van het spel. Alleen rekenen en controleren;
    aanmaken doet run(). Weigert alles wat niet kan, zodat --plan het zegt voordat er iets start."""
    ruw = {}
    for stuk in [x for x in (ini_tekst or "").split(";")] + list(cli or []):
        stuk = stuk.strip()
        if not stuk:
            continue
        if "=" not in stuk:
            raise SettingRefused("schijfletter %r is geen LETTER=MAP" % stuk)
        letter, doel = stuk.split("=", 1)
        letter = letter.strip().rstrip(":").upper()
        if not re.match(r"^[A-Z]$", letter) or letter in VASTE_LETTERS:
            raise SettingRefused("schijfletter %r kan niet (een letter van A tot Y, niet C)" % stuk.split("=", 1)[0].strip())
        ruw[letter] = doel.strip()
    uit = []
    for letter in sorted(ruw):
        doel = gs.expand_home(ruw[letter])
        doel = os.path.normpath(doel if doel.startswith("/") else os.path.join(wortel, doel))
        if not os.path.isdir(doel):
            raise SettingRefused("schijf %s: de map %s bestaat niet" % (letter, doel))
        if on_nas(doel):
            raise SettingRefused("schijf %s: de map %s ligt op de NAS; een schijfletter van ons wijst naar een lokale map "
                                 "(de view van het spel, of een eigen map)" % (letter, doel))
        dd = os.path.join(prefix, "dosdevices")
        link = os.path.join(dd, letter.lower() + ":")
        if not os.path.isdir(dd):
            raise SettingRefused("schijf %s: de prefix %s heeft geen dosdevices-map (nog niet aangemaakt; start het spel "
                                 "een keer zonder schijfletters)" % (letter, prefix))
        staat = False
        if os.path.lexists(link):
            if not (os.path.islink(link) and os.path.realpath(link) == os.path.realpath(doel)):
                raise SettingRefused("schijf %s: bestaat al in de prefix %s en wijst naar %s, niet naar %s"
                                     % (letter, prefix, os.path.realpath(link), doel))
            staat = True
        uit.append({"letter": letter, "target": doel, "link": link, "present": staat})
    return uit


def plan_exe(opgegeven, game, wortel, drives, prefix):
    """Het spel vanaf een ANDER PAD starten: {given, passed, unix, same_file, drive} of None (geen override).

    Alleen het pad dat de lader voor het spel krijgt verandert; alles eromheen (prefix, werkmap van de start,
    omgeving, lader, kern, JVS-brug, teknoparrot.ini, drop-ins) blijft wat het was. Zo ligt een verschil in de
    uitkomst aan het pad en niet aan de opzet. Vormen: een Windows-pad met een schijfletter (X:/exe/spel.exe; de
    letter moet in de prefix bestaan of met --drive / [Launch] Drives zijn opgegeven), een Unix-pad, of een pad
    relatief aan de view. Het bestand moet bestaan en binnen de view van het spel liggen."""
    opgegeven = (opgegeven or "").strip().strip('"')
    if not opgegeven:
        return None
    m = re.match(r"^([A-Za-z]):[\\/]*(.*)$", opgegeven)
    letter = None
    if m and m.group(1).upper() != "Z":
        letter = m.group(1).upper()
        basis = next((d["target"] for d in drives if d["letter"] == letter), None)
        if basis is None:
            link = os.path.join(prefix, "dosdevices", letter.lower() + ":")
            if letter == "C" or not os.path.isdir(link):
                raise SettingRefused("--exe %s: schijf %s: bestaat niet in de prefix %s; geef hem op met --drive %s=<map> "
                                     "of [Launch] Drives (bijvoorbeeld %s=. voor de view van het spel)"
                                     % (opgegeven, letter, prefix, letter, letter))
            basis = os.path.realpath(link)
        unix = _zoek_zonder_hoofdletters(basis, m.group(2))
        door = letter + ":\\" + m.group(2).replace("/", "\\")
    else:
        pad = "/" + m.group(2).replace("\\", "/") if m else gs.expand_home(opgegeven)
        unix = os.path.normpath(pad if pad.startswith("/") else os.path.join(wortel, pad))
        door = wine_path(unix)
    if not unix or not os.path.isfile(unix):
        raise SettingRefused("--exe %s: dat bestand bestaat niet" % opgegeven)
    try:
        zelfde = os.path.samefile(unix, game)
    except OSError:
        zelfde = False
    if not zelfde and not _binnen(unix, wortel):
        raise SettingRefused("--exe %s: %s ligt buiten de view %s van het spel; de override verandert alleen het pad, "
                             "niet de opstelling" % (opgegeven, unix, wortel))
    return {"given": opgegeven, "passed": door, "unix": unix, "same_file": zelfde, "drive": letter}


def exe_log_lines(plan):
    """Welk pad de lader voor het spel krijgt, en de schijfletters van deze run (voor --plan en het logboek)."""
    ge = plan.get("game_exe") or {}
    ov = ge.get("override")
    if not ov:
        uit = ["spel: %s" % ge.get("passed", "")]
    else:
        uit = ["spel (OVERRIDE, opgegeven als %s): %s" % (ov["given"], ov["passed"]),
               "  %s; de rest van de opstelling is gelijk aan die zonder override"
               % ("hetzelfde bestand als in het profiel (%s), langs een ander pad" % plan["game"] if ov["same_file"]
                  else "een ANDER bestand dan in het profiel (%s): %s" % (plan["game"], ov["unix"]))]
    for d in plan.get("drives") or []:
        uit.append("schijfletter %s: -> %s (%s)" % (d["letter"], d["target"],
                                                   "staat al in de prefix" if d["present"] else
                                                   "wordt voor deze run in de prefix gezet en na afloop weer weggehaald"))
    return uit


def _drives_state():
    try:
        with open(DRIVES_STATE) as f:
            d = json.load(f)
        return d if isinstance(d, dict) else {}
    except (OSError, ValueError):
        return {}


def _drives_state_schrijf(d):
    os.makedirs(os.path.dirname(DRIVES_STATE), exist_ok=True)
    tmp = DRIVES_STATE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(d, f, indent=1)
        f.write("\n")
    os.replace(tmp, DRIVES_STATE)


def drives_opruimen(prefix, logf=None):
    """Haal de schijfletters weg die WIJ in deze prefix hebben gezet (ook die van een run die is afgebroken voordat
    hij kon opruimen). Een koppeling die intussen ergens anders heen wijst is niet meer van ons en blijft staan."""
    staat = _drives_state()
    for link, doel in list((staat.get(prefix) or {}).items()):
        if os.path.islink(link) and os.readlink(link) == doel:
            os.unlink(link)
            if logf:
                logf("schijfletter %s weggehaald uit de prefix" % os.path.basename(link).upper())
    if prefix in staat:
        del staat[prefix]
        _drives_state_schrijf(staat)


def drives_aan(plan, logf=None):
    """Zet de schijfletters van het plan in de prefix. Geeft '' of een foutmelding (dan is er niets blijven staan)."""
    prefix = plan["prefix"]
    drives_opruimen(prefix, logf)          # resten van een afgebroken run eerst weg
    staat = _drives_state()
    gezet = {}
    for d in plan["drives"]:
        link = d["link"]
        if os.path.lexists(link):
            if os.path.islink(link) and os.path.realpath(link) == os.path.realpath(d["target"]):
                continue                   # stond er al (niet van ons): laten staan, ook na afloop
            for l in gezet:
                os.unlink(l)
            return "schijf %s: bestaat al in de prefix en wijst naar %s" % (d["letter"], os.path.realpath(link))
        os.symlink(d["target"], link)
        gezet[link] = d["target"]
        staat[prefix] = dict(gezet)
        _drives_state_schrijf(staat)       # meteen vastleggen: ook na een harde stop weten we wat van ons is
        if logf:
            logf("schijfletter %s: -> %s (in de prefix %s)" % (d["letter"], d["target"], prefix))
    return ""


STARTSCRIPTS = ("game.bat", "Start.bat", "utils/run_game.sh", "pm/utils/run_game.sh")   # waar een spel zijn eigen start bewaart


def start_script(settings, gamedir):
    """Het eigen startscript van het spel: (pad, wortel) of (None, wortel). [Launch] StartScript in game.ini wijst het
    aan (een pad binnen de spelmap of de view; `none` = niet zoeken); anders het eerste bekende script dat er staat.
    Het wordt alleen GELEZEN (scripts/startscript.py), nooit uitgevoerd."""
    wortel = view_wortel(gamedir)
    rel = (settings.get("Launch", {}).get("StartScript", "") or "").strip().replace("\\", "/").strip("/")
    if rel.lower() in ("none", "geen", "-"):
        return None, wortel
    kandidaten = [rel] if rel else list(STARTSCRIPTS)
    for k in kandidaten:
        for basis in (wortel, gamedir):
            pad = os.path.join(basis, k)
            if os.path.isfile(pad) and _binnen(pad, wortel):
                return pad, wortel
    return ("?" + rel if rel else None), wortel


def start_script_plan(settings, gamedir, plan):
    """Wat het eigen startscript doet, naast ons startplan (voor --plan). None als het spel er geen heeft."""
    pad, wortel = start_script(settings, gamedir)
    if pad is None:
        return None
    if pad.startswith("?"):
        return {"script": pad[1:], "found": False,
                "lines": ["eigen startscript van het spel: [Launch] StartScript = %s -- NIET GEVONDEN in de spelmap" % pad[1:]]}
    try:
        feiten = ss.lees(pad, wortel)
        v = ss.vergelijk(feiten, dict(plan, _script_root=wortel))
    except (OSError, ValueError) as e:
        return {"script": os.path.relpath(pad, wortel), "found": False,
                "lines": ["eigen startscript van het spel: %s -- niet te lezen: %s" % (os.path.relpath(pad, wortel), e)]}
    return {"script": feiten["script"], "found": True, "facts": feiten, "compare": v, "lines": ss.regels(feiten, v)}


def env_log_lines(plan):
    """De omgevingsvariabelen die deze game uit game.ini meekrijgt ([Env]), voor --plan en het logboek."""
    eigen = plan.get("game_env") or {}
    if not eigen:
        return ["omgevingsvariabelen voor het spel ([Env] in game.ini): geen"]
    return ["omgevingsvariabelen voor het spel ([Env] in game.ini): %s"
            % "  ".join("%s=%s" % kv for kv in sorted(eigen.items()))]


def arg_log_lines(plan):
    """Welke argumenten er met elke start meegaan, en hoe (voor --plan en het logboek)."""
    uit = []
    for s in plan.get("starts") or []:
        a = s.get("args") or {}
        if not a.get("list"):
            uit.append("argumenten voor %s: geen" % s["name"])
            continue
        uit.append("argumenten voor %s (%d): %s" % (s["name"], len(a["list"]), "  ".join("[%s]" % x for x in a["list"])))
        if a.get("how") == "one-parameter":
            uit.append("  als een parameter aan de lader, letterlijk: %s" % a["param"])
        for b in a.get("sources") or []:
            uit.append("  uit %s = %s" % (b["key"], b["value"]))
    return uit


def second_start(p, settings, game):
    """De tweede start van een profiel met HasTwoExecutables: None (een gewone game) of
    {path, args, first, wait}. Alles komt uit het profiel en uit game.ini; hier staat geen spelnaam.

    Het pad: [Game] GamePath2 in game.ini, anders GamePath2 van het XML-profiel, anders [Identify] Path2
    relatief aan dezelfde spelmap als [Identify] Path. De orde: LaunchSecondExecutableFirst. De argumenten:
    [Launch] SecondArguments (letterlijk), anders SecondExecutableArguments van het profiel (zie spel_argumenten). De wachttijd tussen de twee
    starts: [Launch] SecondWait in seconden (standaard DEFAULT_SECOND_WAIT)."""
    if not truthy(p.get("HasTwoExecutables", "false")):
        return None
    launch = settings.get("Launch", {})
    pad = gs.expand_home((settings.get("Game", {}).get("GamePath2", "") or "").strip()) or (p.get("GamePath2", "") or "").strip()
    if not pad:
        path, _size, _sha = op.identify_of(settings)
        path2 = (settings.get(op.IDENTIFY, {}).get("Path2", "") or "").strip().replace("\\", "/").strip("/")
        if path2:
            root = game[:-len(path)].rstrip("/") if path and game.endswith("/" + path) else os.path.dirname(game)
            pad = os.path.join(root, path2)
    if not pad or not os.path.isfile(pad):
        raise Unsupported("het profiel heeft twee exe's (HasTwoExecutables) maar de tweede is niet gevonden: %r "
                          "(zet GamePath2 in het profiel, of [Identify] Path2 / [Game] GamePath2 in game.ini)" % pad)
    pad, waarom = in_dezelfde_view(game, pad)
    if pad is None:
        raise Unsupported(waarom)
    ruw = (launch.get("SecondWait", "") or "").strip().replace(",", ".")
    try:
        wacht = float(ruw) if ruw else DEFAULT_SECOND_WAIT
    except ValueError:
        wacht = -1.0
    if not 0 <= wacht <= 600:
        raise SettingRefused("[Launch] SecondWait = %s (een aantal seconden van 0 tot en met 600)" % ruw)
    # [Launch] SecondArguments (onze sleutel, letterlijk) gaat voor op SecondExecutableArguments van het profiel.
    eigen = launch.get("SecondArguments", "") if "SecondArguments" in launch else ""
    profiel = "" if "SecondArguments" in launch else (p.get("SecondExecutableArguments", "") or "")
    return {"path": pad, "profile_args": profiel, "own_args": eigen or "",
            "first": truthy(p.get("LaunchSecondExecutableFirst", "false")), "wait": wacht}


def start_log_lines(plan):
    """De starts van dit plan in de orde waarin ze gebeuren, met de wachttijd ertussen (voor --plan en het logboek)."""
    starts = plan.get("starts") or []
    uit = []
    for i, s in enumerate(starts):
        if i:
            uit.append("wachten: %g s; daarna alleen verder als elke eerdere start nog leeft" % s["wait_before"])
        uit.append("start %d/%d: %s (%s%s) -- %s" % (i + 1, len(starts), s["name"],
                                                    "het spel" if s["role"] == "main" else "tweede exe",
                                                    ", in gamescope" if s["gamescope"] else "",
                                                    " ".join(shlex.quote(a) for a in s["argv"])))
    return uit


def build_plan(name, tp, extra_env=None, shm_source=None, log_full=False, base_env=None, materialize_patches=False, use_own=True,
               exe=None, drives=None):
    """Alles wat de start nodig heeft, zonder bijwerkingen (behalve xrandr lezen); materialize_patches maakt ook de gepatchte
    kopieën (alleen bij --run). use_own=False negeert het eigen profiel in game.ini en leest altijd UserProfiles/<P>.xml (voor de vergelijking)."""
    # Instellingen van de game: patches/<Game>/game.ini (meegeleverd, dan de gebruiker); het oude .env als laagste laag.
    settings = gs.load_for_profile(name, tp, log=lambda m: print("[launch-direct] " + m, file=sys.stderr))
    for w in settings.warnings:
        print("[launch-direct] instellingen: " + w, file=sys.stderr)
    own = use_own and op.has_own_profile(settings.settings)
    p = load_own_profile(tp, name, settings.settings) if own else load_profile(tp, name)
    # Abonnement: het sjabloon van de runtime of een laag van game.ini met Patreon = true. Niets kan het uitzetten (ook geen eigen profiel).
    if op.patreon_in_layers(name, tp, p.get("EmulatorType", ""), user=gs.user_dir()):
        p["Patreon"] = "true"
    pd = parrot_data(tp)
    emu = p.get("EmulationProfile", "")
    if emu in UNSUPPORTED_EMULATION:
        raise Unsupported("EmulationProfile %s heeft eigen startargumenten in TeknoParrotUi (niet nagebouwd)" % emu)
    game = p.get("GamePath", "")
    if not game or not os.path.isfile(game):
        raise Unsupported("game niet gevonden: %r%s" % (game, " (voeg de map met de game toe als gamemap in de app, of zet [Game] GamePath in je eigen game.ini)" if own else ""))
    second = second_start(p, settings.settings, game)   # None bij een gewone game (een exe)
    exe_rel, dll_rel, is64 = resolve_loader(p, tp)
    et = p.get("EmulatorType", "")
    gamedir = os.path.dirname(game)
    elf_like = et in ("ElfLdr2", "N2", "Lindbergh")
    cwd = gamedir if elf_like else tp
    # TeknoParrotUi zoekt de lader van een 32-bit ElfLdr2/N2/Lindbergh-game relatief aan de map van de game (daarom
    # staat ElfLdr2 als symlink in de views); de 64-bit ElfLdr2-lader en alle andere relatief aan de runtime-map.
    loader = os.path.join(tp, exe_rel)
    if elf_like and not (et == "ElfLdr2" and is64) and os.path.isfile(os.path.join(gamedir, exe_rel)):
        loader = os.path.join(gamedir, exe_rel)
    if not os.path.isfile(loader):
        raise Unsupported("lader ontbreekt: %s" % loader)
    if dll_rel and not os.path.isfile(os.path.join(tp, dll_rel + ".dll")):
        raise Unsupported("kern ontbreekt: %s.dll" % os.path.join(tp, dll_rel))

    env = dict(base_env if base_env is not None else os.environ)
    apply_profile_choices(p, settings.settings)
    prof_env = gs.to_env(settings.settings, gamedir)
    env.update(prof_env)
    env.update(extra_env or {})
    legacy_fpfix = truthy(env.get(gs.LEGACY_LOADER_FPFIX, "0")) and not any(
        pt.name == gs.FPFIX_PATCH and pt.enabled for pt in settings.patches)
    if legacy_fpfix:   # RG_LOADER_FPFIX in de omgeving of via --env: zet de gedeelde lader-patch aan (oude naam)
        for pt in settings.patches:
            if pt.name == gs.FPFIX_PATCH:
                pt.fields["Enabled"] = "true"
    own_loader, own_core_dir, own_core_rel = own_build(settings.settings.get("Loader", {}), et, exe_rel, dll_rel, is64)   # [Loader] Build/Core/Path
    if own_loader:
        loader = own_loader
        for pt in settings.patches:   # de Wine-fix zit in de eigen lader zelf (broncodewijziging); de bytepatch past er niet op
            if pt.name == gs.FPFIX_PATCH:
                pt.fields["Enabled"] = "false"
    core_dir = own_core_dir or tp
    if own_core_rel:
        dll_rel = own_core_rel   # eigen kern in de plaats van de kern van het profiel (ook bij EmulatorType TeknoParrot)
    core_dll = os.path.join(core_dir, dll_rel + ".dll") if dll_rel else None
    targets = {"loader": loader, "core": os.path.join(core_dir, dll_rel + ".dll") if dll_rel else None}
    patched, patch_report = plan_file_patches(settings, targets, tp, materialize=materialize_patches, exe_path=game)
    # De patchmotor erbij: Kind = file op de game zelf en Kind = memory op de hoofdmodule. Additief,
    # zodat alles wat hierboven al kon dezelfde weg blijft gaan (docs/patchmotor.md).
    try:
        extra_patched, extra_verslag = plan_motor_patches(settings, game, loader, targets.get("core"), materialize_patches)
    except PatchRefused:
        raise
    patch_report.extend(extra_verslag)
    patched.update({k: v for k, v in extra_patched.items() if k not in patched})
    loader = patched.get("loader", loader)
    wine = resolve_wine(p, env)
    prefix, set_prefix = resolve_prefix(p, env, wine)

    windowed = any((f["name"] == "Windowed" and f["value"] == "1") or (f["name"] == "DisplayMode" and f["value"] == "Windowed")
                   for f in p["fields"])
    launch = {}
    if truthy(p.get("UseRemoteThread", "false")):
        launch["TP_REMOTETHREAD"] = "1"
    try:
        if int(p.get("msysType", "0") or 0) > 0:
            launch["tp_msysType"] = p["msysType"]
    except ValueError:
        pass
    if et in ("N2", "ElfLdr2"):
        launch["tp_windowed"] = "1" if windowed else "0"
        launch["TP_LOGTOFILE"] = "1" if (log_full or truthy(pd.get("Elfldr2LogToFile", "false"))) else "0"
        if pd.get("Elfldr2NetworkAdapterName"):
            launch["TP_ETH"] = pd["Elfldr2NetworkAdapterName"]
        msaa = field(p, "MSAA Level")
        if msaa is not None:
            launch["TP_MSAA"] = msaa["value"]
        if emu == "Vt3Lindbergh":
            launch["TEA_DIR"] = wine_path(os.path.dirname(gamedir)) + "\\"
        elif emu == "SegaJvsLetsGoJungle":
            launch["TEA_DIR"] = wine_path(gamedir) + "\\"
    elif et == "Lindbergh":
        if windowed:
            launch["tp_windowed"] = "1"
        if emu in LINDBERGH_TEA:
            launch["TEA_DIR"] = wine_path(gamedir) + "\\"
        elif emu == "Vt3Lindbergh":
            launch["TEA_DIR"] = wine_path(os.path.dirname(gamedir)) + "\\"
        if any(f["name"] == "EnableAmdFix" and f["value"] == "1" for f in p["fields"]):
            launch["tp_AMDCGGL"] = "1"
            if emu == "SegaInitialDLindbergh":
                launch["tp_D4AMDFix"] = "1"
        launch["REGAL_LOAD_GL"] = "opengl32.dll"
    if emu in OPENSSL_FIX:
        launch["OPENSSL_ia32cap"] = ":~0x20000000"
    if set_prefix:
        launch["WINEPREFIX"] = prefix
    launch.setdefault("TP_REMOTETHREAD", env.get("TP_REMOTETHREAD", "1"))   # onder Wine altijd (zie TeknoParrotUi)
    # TP_REMOTETHREAD=off in de .env: de variabele weglaten, zodat de lader zijn standaardroute neemt (entry-point-
    # trampoline op de hoofdthread; volgens het TeknoParrot-team de Wine-bestendige route, antwoorden_ronde3 vraag 14).
    # De lader kijkt alleen of de variabele bestaat, dus "0" zou niet werken.
    if launch["TP_REMOTETHREAD"].lower() in ("off", "0", "no", "false", ""):
        del launch["TP_REMOTETHREAD"]
        env.pop("TP_REMOTETHREAD", None)
    if log_full:
        launch["WINEDEBUG"] = env.get("RG_WINEDEBUG_FULL", "-all,+debugstr,err+module")   # +debugstr: OutputDebugString
    elif "WINEDEBUG" not in env:
        launch["WINEDEBUG"] = "-all"
    launch["TP_LAUNCH_SESSION_ID"] = str(uuid.uuid4())
    env.update(launch)

    # Het pad dat de lader voor het spel krijgt. Standaard het spel uit het profiel; --exe / [Launch] Exe geeft een ander
    # pad naar het spel (bijvoorbeeld vanaf een schijfletter, zoals het eigen startscript van het spel het doet) terwijl
    # al het andere in dit plan gelijk blijft. Schijfletters voor de run: --drive / [Launch] Drives.
    launch_ini = settings.settings.get("Launch", {})
    drive_plan = plan_drives(launch_ini.get("Drives", ""), drives, view_wortel(gamedir), prefix)
    exe_ov = plan_exe(exe if exe is not None else launch_ini.get("Exe", ""), game, view_wortel(gamedir), drive_plan, prefix)
    spel_win = exe_ov["passed"] if exe_ov else wine_path(game)
    args = [wine, loader]
    if dll_rel:
        args.append(wine_path(os.path.join(core_dir, dll_rel)))
    args.append(spel_win)
    # De argumenten voor het spel: de velden van het profiel (CustomArguments, ExtraParameters) en onze eigen sleutel
    # [Launch] Arguments in game.ini (standaard leeg). Hoe ze bij het spel komen hangt van de lader af: spel_argumenten().
    profiel_delen, bronnen = [], []
    for veld in ("CustomArguments", "ExtraParameters"):
        extra = p.get(veld, "")
        if extra.strip():
            profiel_delen += shlex.split(extra.replace("\\", "\\\\"))
            bronnen.append({"key": veld, "value": extra.strip()})
    if (launch_ini.get("Arguments", "") or "").strip():
        bronnen.append({"key": "[Launch] Arguments", "value": launch_ini["Arguments"].strip()})
    spel_args = spel_argumenten(loader, profiel_delen, launch_ini.get("Arguments", ""))
    spel_args["sources"] = bronnen
    basis = list(args[:-1])
    args += spel_args["argv"]
    # De tweede exe krijgt dezelfde wine, dezelfde lader en dezelfde kern als de eerste: anders haakt de kern er niet aan.
    args2, tweede_args = None, None
    if second:
        delen2 = shlex.split(second["profile_args"].replace("\\", "\\\\")) if second["profile_args"].strip() else []
        tweede_args = spel_argumenten(loader, delen2, second["own_args"])
        tweede_args["sources"] = [{"key": k, "value": v.strip()} for k, v in
                                  (("SecondExecutableArguments", second["profile_args"]),
                                   ("[Launch] SecondArguments", second["own_args"])) if v.strip()]
        args2 = basis + [wine_path(second["path"])] + tweede_args["argv"]

    gamescope = None
    if env.get("TP_GAMESCOPE") == "1" and env.get("TP_NO_GAMESCOPE") != "1":
        w, h = display_size(env)
        gs_bin = env.get("TP_GAMESCOPE_PATH") or "/usr/bin/gamescope"
        backend = env.get("TP_GAMESCOPE_BACKEND") or ("wayland" if env.get("WAYLAND_DISPLAY") else "")
        gamescope = [gs_bin, "-W", str(w), "-H", str(h), "-S", "fit", "-f"] + (["--backend", backend] if backend else []) + ["--"]
    windowed_override = bool(gamescope) and p.get("GamescopeGameWindowCompatibility") == "RequireWindowed" and \
        (field(p, "Windowed") or {}).get("value") == "0"

    files = [(os.path.join(gamedir, "teknoparrot.ini"), build_ini(p, pd, windowed_override))]
    if et == "TeknoMacaw" and os.path.basename(gamedir) == "modules":
        files.append((os.path.join(os.path.dirname(gamedir), "teknoparrot.ini"), files[0][1]))
    tg = taito_gun_ini(p)
    if tg:
        files.append((os.path.join(gamedir, tg[0]), tg[1]))
    # De kern leest teknoparrot.ini naast de exe waarin hij geladen is, dus ook naast de tweede. TeknoParrotUi schrijft
    # daar hetzelfde bestand (gemeten: de twee bestanden naast de twee exe's van zo'n spel zijn byte-gelijk).
    if second and os.path.dirname(second["path"]) != gamedir:
        files.append((os.path.join(os.path.dirname(second["path"]), "teknoparrot.ini"), files[0][1]))

    pipe_mode = "pipe" if emu not in NO_JVS_PIPE else "shm"
    src = shm_source or SHM_PATH
    helper = RG_JVSBRIDGE   # onze eigen JVS-brug; valt terug op TeknoParrots pipehelper.exe als die (nog) niet gebouwd is
    if not os.path.isfile(helper):
        helper = os.path.join(tp, "pipehelper.exe")
    if not os.path.isfile(helper):
        raise Unsupported("JVS-brug ontbreekt: bouw rg-jvsbridge (scripts/build-jvsbridge.sh) of zorg voor pipehelper.exe in de runtime-map")

    comms = {"budgieloader.ex", "budgieloader_x6", "openparrotloade", "teknomacaw.exe", "teknomacaw64.ex"}
    for key in ("ExecutableName", "GamePath"):
        for part in p.get(key, "").split(";"):
            b = part.replace("\\", "/").rsplit("/", 1)[-1].lower()[:15]
            if b:
                comms.add(b)
    # De starts in de orde waarin ze gebeuren. Een gewone game heeft er een, en die is gelijk aan "argv". De tweede
    # exe draait buiten gamescope (hij heeft geen venster), in dezelfde werkmap, prefix en omgeving als de eerste.
    hoofd = {"name": os.path.basename(game), "role": "main", "exe": game, "argv": (gamescope or []) + args, "cwd": cwd,
             "gamescope": bool(gamescope), "wait_before": 0.0, "args": spel_args, "exe_passed": spel_win,
             "comms": exe_comms(p.get("ExecutableName", ""), game, exe_ov["unix"] if exe_ov else "")}
    starts = [hoofd]
    if second:
        tweede = {"name": os.path.basename(second["path"]), "role": "second", "exe": second["path"], "argv": args2, "cwd": cwd,
                  "gamescope": False, "comms": exe_comms(p.get("ExecutableName2", ""), second["path"]), "wait_before": 0.0,
                  "args": tweede_args}
        starts = [tweede, hoofd] if second["first"] else [hoofd, tweede]
        starts[1]["wait_before"] = second["wait"]
    uit = {
        "profile": name, "tp": tp, "emulator_type": et, "emulation_profile": emu, "is64": is64,
        "game": game, "cwd": cwd, "wine": wine, "prefix": prefix, "argv": (gamescope or []) + args,
        "core_dll": core_dll, "core_sha256": file_sha256(core_dll) if core_dll else None,
        "core_source": "ravengun" if own_core_rel else "runtime",
        "gamescope": bool(gamescope), "launch_env": launch, "profile_env": prof_env, "env": env, "patches": patch_report,
        "settings_sources": settings.sources, "profile_source": "game.ini" if own else "UserProfiles/%s.xml" % name,
        "game_source": p.get("_game_source", "xml"),
        "files": files, "pipe_mode": pipe_mode, "shm_source": src, "pipehelper": helper, "game_comms": sorted(comms),
        "log_full": log_full, "windowed_override": windowed_override,
        "recipe": recipe_summary(tp, name),
        "needs_activation": needs_activation(p),
        "windows": is_pe(game),
        "starts": starts,
        "game_args": spel_args,
        # De variabelen die deze game uit game.ini meekrijgt ([Env]); ze zitten ook in "env", dus in elk gestart proces.
        "game_env": dict(settings.settings.get("Env", {})),
        # Het pad dat de lader voor het spel krijgt ("game" blijft het bestand uit het profiel), en de schijfletters van de run.
        "game_exe": {"passed": spel_win, "override": exe_ov},
        "drives": drive_plan,
    }
    # Het eigen startscript van het spel, gelezen en naast dit plan gelegd (niet uitgevoerd).
    uit["start_script"] = start_script_plan(settings.settings, gamedir, uit)
    # Drop-ins (docs/dropins.md): altijd, niet alleen bij een dongle. Alleen rekenen; kopiëren doet run().
    uit["dropins"] = dropin_plan(uit)
    # De dongle vooraf beoordelen zodat --plan laat zien wat er zou gebeuren (zonder bijwerkingen en
    # zonder te weigeren: dat doet main() bij --check en --run via dongle_gate).
    return uit


def recipe_summary(tp, name):
    """Het startvoorschrift (AndroidLaunchRecipes) ter controle; de Android-paden (elfloader.exe, E:\\) gelden hier niet."""
    try:
        with open(os.path.join(tp, "AndroidLaunchRecipes", name + ".json")) as f:
            r = json.load(f)
        return {k: r.get(k) for k in ("loaderExecutable", "libraryDirectory", "arguments", "inputProtocol",
                                      "displayMode", "compatibilityPreset") if k in r}
    except (OSError, ValueError):
        return None


# ---------------------------------------------------------------------------------------------------------------------
# Gedeeld geheugen en de JVS-pipe (TCP-kant)

def ensure_shm():
    """/dev/shm/TeknoParrot_JvsState: aanmaken als hij er niet is (bestaande inhoud blijft, zoals bij TeknoParrotUi)."""
    fd = os.open(SHM_PATH, os.O_RDWR | os.O_CREAT, 0o644)
    try:
        if os.fstat(fd).st_size < SHM_SIZE:
            os.ftruncate(fd, SHM_SIZE)
    finally:
        os.close(fd)


def set_connect_flag(value):
    try:
        fd = os.open(SHM_PATH, os.O_RDWR)
        try:
            os.pwrite(fd, bytes([value]), 0)
        finally:
            os.close(fd)
    except OSError:
        pass


def jvs_frames(buf):
    """Haalt complete JVS-pakketten (node, lengte, gegevens, som; zonder sync, escapes opgelost) uit buf.
    Geeft (pakketten, rest); de rest is een onvolledig pakket dat op meer bytes wacht."""
    frames = []
    while True:
        i = buf.find(b"\xe0")
        if i < 0:
            return frames, b""
        buf = buf[i:]
        raw, j, esc, state = bytearray(), 1, False, "meer"
        while j < len(buf):
            b = buf[j]
            if b == 0xE0:            # nieuwe sync voordat het pakket af was: het halve pakket vervalt
                state = "kapot"
                break
            j += 1
            if esc:
                raw.append((b + 1) & 0xFF)
                esc = False
            elif b == 0xD0:
                esc = True
            else:
                raw.append(b)
            if len(raw) >= 2 and len(raw) == raw[1] + 2:
                state = "af"
                break
        if state == "af":
            frames.append(bytes(raw))
            buf = buf[j:]
        elif state == "kapot":
            buf = buf[j:]
        else:
            return frames, buf


JVS_IDENT = b"SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551 ;Ver1.00;98/10\x00"
# Kenmerken van het nagebootste bord (code, p1, p2, p3): 4 spelers van 2 bytes schakelaars, 2 muntslots, 8 analoge
# kanalen van 16 bit, scherm-positie (x/y 10 bit, 4 kanalen) en 18 uitgangen. Het bord is een eerste gok voor Type X2 (A10).
JVS_FEATURES = bytes([0x01, 4, 16, 0, 0x02, 2, 0, 0, 0x03, 8, 16, 0, 0x06, 10, 10, 4, 0x12, 18, 0, 0, 0x00])


def configure_jvs(env):
    """Proefinstellingen (A10): [Input] JvsIdent (tekst) en JvsFeatures (hex van de functielijst, inclusief eindbyte 00)."""
    global JVS_IDENT, JVS_FEATURES
    if env.get("RG_JVS_IDENT"):
        JVS_IDENT = env["RG_JVS_IDENT"].encode("latin-1") + b"\x00"
    if env.get("RG_JVS_FEATURES"):
        JVS_FEATURES = bytes.fromhex(env["RG_JVS_FEATURES"])


def jvs_pack(body):
    """Antwoordpakket naar de host: sync, adres 0 (host), lengte, gegevens, som; E0 en D0 in de inhoud met escape."""
    raw = bytes([0x00, len(body) + 1]) + bytes(body)
    raw += bytes([sum(raw) & 0xFF])
    out = bytearray(b"\xe0")
    for b in raw:
        if b in (0xE0, 0xD0):
            out += bytes([0xD0, (b - 1) & 0xFF])
        else:
            out.append(b)
    return bytes(out)


def jvs_answer(data, inputs=None):
    """Beantwoordt een pakket met standaard JVS-opdrachten (data = opdrachten zonder node/lengte/som). Geeft het antwoord
    (zonder sync/adres/lengte/som: status 1 + rapporten) of None als er niets te antwoorden valt (alleen reset)."""
    inputs = inputs or {}
    out = bytearray([0x01])           # status: normaal
    i, known = 0, True
    while i < len(data):
        c = data[i]
        if c == 0xF0:                  # reset: geen antwoord
            return None
        elif c == 0x10:
            out += bytes([1]) + JVS_IDENT; i += 1
        elif c == 0x11:
            out += bytes([1, 0x13]); i += 1
        elif c == 0x12:
            out += bytes([1, 0x30]); i += 1
        elif c == 0x13:
            out += bytes([1, 0x10]); i += 1
        elif c == 0x14:
            out += bytes([1]) + JVS_FEATURES; i += 1
        elif c == 0x15:
            out += bytes([1]); i += 2
        elif c == 0x20 and i + 2 < len(data) + 0:
            players, nbytes = data[i + 1], data[i + 2]
            out += bytes([1, inputs.get("sys", 0) & 0xFF])
            for pl in range(players):
                out += bytes((inputs.get("p%d" % (pl + 1), 0) >> (8 * k)) & 0xFF for k in range(nbytes))
            i += 3
        elif c == 0x21 and i + 1 < len(data):
            out += bytes([1]) + bytes([0, 0]) * data[i + 1]; i += 2
        elif c == 0x22 and i + 1 < len(data):
            out += bytes([1]) + bytes([0x80, 0x00]) * data[i + 1]; i += 2
        elif c == 0x23 and i + 1 < len(data):
            out += bytes([1]) + bytes([0, 0]) * data[i + 1]; i += 2
        elif c == 0x24:
            out += bytes([1, 0]); i += 1
        elif c == 0x25 and i + 1 < len(data):
            out += bytes([1, 0, 0, 0, 0]); i += 2
        elif c == 0x26:
            out += bytes([1, 0, 0]); i += 1
        elif c in (0x30, 0x31) and i + 3 < len(data) + 1:
            out += bytes([1]); i += 4
        elif c == 0x32 and i + 1 < len(data):
            out += bytes([1]); i += 2 + data[i + 1]
        elif c == 0x35 and i + 1 < len(data):
            out += bytes([1]); i += 2 + data[i + 1]
        else:
            out += bytes([2]); known = False; break   # rapport 2: onbekende opdracht
    return bytes(out)


class JvsPipeServer(threading.Thread):
    """TCP-kant van \\\\.\\pipe\\TeknoParrot_JVS (pipehelper.exe verbindt hierheen zodra de game de pipe opent)."""

    def __init__(self, board=False, f1_once=False, debug=False, pty=None):
        self.f1_once = f1_once
        self.pty = pty              # RG_JVS_PTY=<pad>: de pty van de daemon (jvs-route); alle JVS-bytes gaan daarheen en terug
        self.debug = debug          # RG_JVS_DEBUG=true: de eerste 300 pakketten en antwoorden in het log
        super().__init__(daemon=True)
        self.board = board          # RG_JVS_BOARD=true in <P>.env: standaard JVS-bord achter de pipe (A10)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.bytes_in = 0
        self.packets = 0
        self.unanswered = 0
        self.answered = 0
        self.address = 1
        self.assigned = False
        self.rest_logged = 0
        self.connected = False

    def run(self):
        try:
            conn, _ = self.sock.accept()
        except OSError:
            return
        self.connected = True
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        log("JVS-pipe: game verbonden (pipehelper via TCP)")
        if self.pty:
            self.run_pty_bridge(conn)
            return
        buf = b""
        seen = set()
        while True:
            try:
                data = conn.recv(4096)
            except OSError:
                break
            if not data:
                break
            self.bytes_in += len(data)
            frames, buf = jvs_frames(buf + data)
            if buf and self.rest_logged < 10:
                self.rest_logged += 1
                log("JVS-pipe: onvolledig pakket in de buffer: %s" % buf.hex())
            for fr in frames:
                if len(fr) < 3:
                    continue
                self.packets += 1
                node, cmd = fr[0], fr[2]
                if self.debug and self.packets <= 300:
                    log("JVS-pipe: #%d van de game: %s" % (self.packets, fr.hex()))
                if cmd == 0xF0:       # reset: verbind-vlag uit
                    set_connect_flag(0)
                    self.assigned = False
                elif cmd == 0xF1 and self.f1_once and self.assigned:
                    # RG_JVS_F1_ONCE=true: alleen het eerste adres toekennen (einde van de keten). Een echt bord zwijgt dan;
                    # de game wacht zonder time-out, dus sturen we statusbyte 2 (onbekende opdracht) als "geen bord meer".
                    body = bytes([0x00, 0x02, 0x02])
                    try:
                        conn.sendall(b"\xe0" + body + bytes([sum(body) & 0xFF]))
                    except OSError:
                        pass
                    self.unanswered += 1
                elif cmd == 0xF1:     # adres: verbind-vlag aan, en het gewone antwoord (status 1, rapport 1)
                    set_connect_flag(1)
                    self.assigned = True
                    if len(fr) > 3:
                        self.address = fr[3]
                    body = bytes([0x00, 0x03, 0x01, 0x01])
                    try:
                        conn.sendall(b"\xe0" + body + bytes([sum(body) & 0xFF]))
                    except OSError:
                        pass
                else:
                    # Op een echte JVS-bus antwoordt alleen het bord met het toegekende adres (de game probeert alle knopen).
                    ans = jvs_answer(fr[2:-1]) if (self.board and 1 <= node <= self.address) else None
                    if ans is not None:
                        if self.debug and self.packets <= 300:
                            log("JVS-pipe: antwoord: %s" % jvs_pack(ans).hex())
                        try:
                            conn.sendall(jvs_pack(ans))
                        except OSError:
                            pass
                        self.answered += 1
                    else:
                        self.unanswered += 1
                key = (node, cmd)
                if key not in seen:
                    seen.add(key)
                    log("JVS-pipe: pakket node %02X commando %02X (%d bytes)%s" % (
                        node, cmd, len(fr), "" if cmd in (0xF0, 0xF1) else (" - beantwoord met het standaard JVS-bord (A10)" if self.board else " - niet beantwoord (geen JVS-bord in de directe start)")))
        log("JVS-pipe: verbinding dicht (%d bytes van de game, %d pakketten, %d beantwoord, %d niet beantwoord)"
            % (self.bytes_in, self.packets, self.answered, self.unanswered))

    def run_pty_bridge(self, conn):
        """De JVS-bytes van de game gaan ongewijzigd naar de pty van de daemon (die is het JVS-bord, dialect jvs) en wat de daemon
        terugschrijft gaat naar de game. Wij kijken alleen mee voor de verbind-vlag (byte 0 van het gedeelde geheugen: aan bij
        de adrestoekenning 0xF1, uit bij de reset 0xF0, zoals TeknoParrotUi)."""
        import termios
        try:
            fd = os.open(self.pty, os.O_RDWR | os.O_NOCTTY)
            attr = termios.tcgetattr(fd)
            attr[0] = attr[1] = attr[3] = 0                              # raw: geen echo, geen omzetting
            attr[2] = (attr[2] & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)) | termios.CS8 | termios.CLOCAL | termios.CREAD
            attr[6][termios.VMIN], attr[6][termios.VTIME] = 1, 0
            termios.tcsetattr(fd, termios.TCSANOW, attr)
        except (OSError, termios.error) as e:
            log("JVS-pipe: pty van de daemon niet te openen (%s): %s" % (self.pty, e))
            return
        stop = threading.Event()
        back = {"bytes": 0}

        def pty_to_game():
            while not stop.is_set():
                try:
                    r, _, _ = select.select([fd], [], [], 0.2)
                    if not r:
                        continue
                    d = os.read(fd, 4096)
                except OSError:
                    break
                if not d:
                    continue
                back["bytes"] += len(d)
                try:
                    conn.sendall(d)
                except OSError:
                    break
        t = threading.Thread(target=pty_to_game, daemon=True)
        t.start()
        buf, seen = b"", set()
        while True:
            try:
                data = conn.recv(4096)
            except OSError:
                break
            if not data:
                break
            self.bytes_in += len(data)
            try:
                os.write(fd, data)
            except OSError:
                break
            frames, buf = jvs_frames(buf + data)
            for fr in frames:
                if len(fr) < 3:
                    continue
                self.packets += 1
                cmd = fr[2]
                if cmd == 0xF0:
                    set_connect_flag(0)
                elif cmd == 0xF1:
                    set_connect_flag(1)
                if self.debug and self.packets <= 300:
                    log("JVS-pipe: #%d van de game: %s" % (self.packets, fr.hex()))
                if (fr[0], cmd) not in seen:
                    seen.add((fr[0], cmd))
                    log("JVS-pipe: pakket node %02X commando %02X (%d bytes) - naar de pty van de daemon" % (fr[0], cmd, len(fr)))
        stop.set()
        t.join(1)
        try:
            os.close(fd)
        except OSError:
            pass
        log("JVS-pipe: verbinding dicht (%d bytes van de game, %d pakketten, %d bytes van de daemon terug)"
            % (self.bytes_in, self.packets, back["bytes"]))

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------------------------------------------------
# Uitvoeren

def own_cgroup_pids():
    try:
        with open("/proc/self/cgroup") as f:
            cg = [l.split("::", 1)[1].strip() for l in f if l.startswith("0::")][0]
        with open("/sys/fs/cgroup" + cg + "/cgroup.procs") as f:
            return [int(x) for x in f.read().split()]
    except (OSError, IndexError, ValueError):
        return []


def comm_of(pid):
    try:
        with open("/proc/%d/comm" % pid) as f:
            return f.read().strip().lower()
    except OSError:
        return ""


def game_alive(comms):
    me = os.getpid()
    return any(comm_of(pid) in comms for pid in own_cgroup_pids() if pid != me)


def decode_console(raw):
    """Een regel uitvoer: de OpenParrot-lader schrijft UTF-16 naar de console, de rest UTF-8."""
    if raw.count(b"\0") * 3 >= len(raw) and len(raw) > 2:
        raw = raw.replace(b"\0", b"")
    return raw.decode("utf-8", "replace").rstrip("\r\n")


class A11Collector:
    def __init__(self, profile):
        self.profile = profile
        self.hits = {k: [] for k, _ in A11_PATTERNS}

    def feed(self, line):
        line = ANSI.sub("", line)
        if line.startswith("[gamescope]") or line.startswith("[launch-direct"):
            return
        for kind, rx in A11_PATTERNS:
            if rx.search(line):
                if line not in self.hits[kind] and len(self.hits[kind]) < 400:
                    self.hits[kind].append(line)
                break

    def write(self, extra_files=()):
        for path in extra_files:
            try:
                with open(path, errors="replace") as f:
                    for line in f:
                        self.feed(line.rstrip("\n"))
            except OSError:
                pass
        d = os.path.join(STATE_DIR, "a11")
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, "%s-%s.txt" % (self.profile, time.strftime("%Y%m%d-%H%M%S")))
        with open(path, "w") as f:
            for kind, lines in self.hits.items():
                f.write("## %s (%d)\n" % (kind, len(lines)))
                for l in lines:
                    f.write(l + "\n")
        log("A11: %s" % ", ".join("%s %d" % (k, len(v)) for k, v in self.hits.items()))
        log("A11: samenvatting in %s" % path)


def run(plan):
    """De run. Heeft het plan schijfletters, dan staan die er precies zolang de run duurt: ervoor gezet, erna weg, ook
    als de run misgaat. Zonder schijfletters gebeurt er hier niets extra's."""
    if not plan.get("drives"):
        return _run(plan)
    for line in exe_log_lines(plan):
        log(line)
    fout = drives_aan(plan, log)
    if fout:
        log("FOUT: %s -- de game wordt niet gestart" % fout)
        return EXIT_PATCH
    try:
        return _run(plan)
    finally:
        drives_opruimen(plan["prefix"], log)


def _run(plan):
    env = plan["env"]
    configure_jvs(env)
    # 0a. Drop-ins (docs/dropins.md): de bestanden in de view zetten en de omgeving bijwerken. Dit gebeurt
    # vóór alles wat de game opent, en nooit op de NAS.
    for line in dropin_log_lines(plan):
        log("drop-in: " + line)
    if plan["dropins"]["errors"]:
        # main() weigert dit al voor het zover komt; dit is voor wie run() rechtstreeks aanroept.
        log("drop-in: de game wordt niet gestart: een run zonder zijn drop-in is een ongeldige run")
        return EXIT_PATCH
    for f in plan["dropins"]["files"]:
        st, why = dropin_materialiseer(plan["dropins"]["base"], f["dest"], log, plan["dropins"].get("root"))
        if not st:
            log("drop-in: FOUT: %s -- de game wordt niet gestart" % why)
            return EXIT_PATCH
        st, why = place_binary(f["src"], f["dest"])
        log("drop-in: " + why)
        if st in ("fout", "nas"):
            log("drop-in: FOUT: %s is niet geplaatst -- de game wordt niet gestart" % f["name"])
            return EXIT_PATCH
    merge_env(plan, plan["dropins"]["env"])
    if plan.get("core_dll"):   # welke kern er echt draait, voor reproduceerbaarheid
        log("kern (%s): %s sha256 %s" % (plan.get("core_source"), plan["core_dll"], plan.get("core_sha256")))
    # 0. Eén regel per patch (ook uitgeschakelde), in vaste vorm.
    for line in patch_log_lines(plan["patches"]):
        log(line)
    # 1. Spelinstellingen naast de game.
    for path, text in plan["files"]:
        st, why = place_file(path, text)
        log("bestand: " + why)
    # 1b. Extra GStreamer-plug-ins (wmv-films, Luigi's Mansion): ontbreken ze, dan eenmalig ophalen (fetch-gst-wmv.sh).
    gst = env.get("GST_PLUGIN_PATH", "")
    if gst and not os.path.isfile(os.path.join(gst, "libgstasf.so")):
        fetch = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fetch-gst-wmv.sh")
        log("GStreamer-plug-ins ontbreken in %s; ophalen met %s" % (gst, fetch))
        rc = subprocess.call([fetch, os.path.dirname(gst.rstrip("/"))])
        if rc != 0:
            log("ophalen mislukt (exitcode %d); de films van de game spelen dan niet af" % rc)
    # 2. Gedeeld geheugen en bron voor de helper.
    ensure_shm()
    src = plan["shm_source"]
    for _ in range(100):
        if os.path.exists(src):
            break
        time.sleep(0.1)
    if not os.path.exists(src):
        log("bronbestand %s bestaat niet (start de IO-brug eerst)" % src)
        return 1
    # 3. JVS-pipe en pipehelper.exe in de prefix (vóór de game, zoals TeknoParrotUi).
    server = JvsPipeServer(board=truthy(plan["env"].get("RG_JVS_BOARD", "0")),
                           f1_once=truthy(plan["env"].get("RG_JVS_F1_ONCE", "0")),
                           debug=truthy(plan["env"].get("RG_JVS_DEBUG", "0")),
                           pty=plan["env"].get("RG_JVS_PTY") or None)
    server.start()
    hargs = [plan["wine"], plan["pipehelper"]]
    if plan["pipe_mode"] == "pipe":
        hargs += [JVS_PIPE, "127.0.0.1", str(server.port), SHM_NAME, str(SHM_SIZE), wine_path(src)]
    else:
        hargs += ["shm", SHM_NAME, str(SHM_SIZE), wine_path(src)]
    henv = dict(env)
    henv["TP_PIPEHELPER_OWNER_PID"] = str(os.getpid())
    log("pipehelper: %s" % " ".join(shlex.quote(a) for a in hargs[1:]))
    helper = subprocess.Popen(hargs, cwd=plan["tp"], env=henv, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                              text=True, errors="replace")
    ready = threading.Event()

    def helper_log():
        for line in helper.stderr:
            line = line.rstrip()
            if "pipehelper" in line:
                log("helper: " + line)
                if "created" in line or "shm mirror active" in line:
                    ready.set()
    threading.Thread(target=helper_log, daemon=True).start()
    if not ready.wait(20):
        log("pipehelper meldde zich niet binnen 20 s; toch verder")
    # 4. De lader (eventueel in gamescope). Een profiel met meer dan een start gaat via run_starts(); een gewone game
    # (een exe) neemt het pad hieronder, ongewijzigd.
    if len(plan.get("starts") or []) > 1:
        a11 = A11Collector(plan["profile"])
        return run_einde(plan, run_starts(plan, env, a11), helper, server, a11)
    log("start: cwd=%s" % plan["cwd"])
    log("start: %s" % " ".join(shlex.quote(a) for a in plan["argv"]))
    log("omgeving: %s" % " ".join("%s=%s" % kv for kv in sorted(plan["launch_env"].items())))
    a11 = A11Collector(plan["profile"])
    proc = subprocess.Popen(plan["argv"], cwd=plan["cwd"], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def pump():
        for raw in proc.stdout:
            line = decode_console(raw)
            sys.stdout.write(line + "\n")
            sys.stdout.flush()
            a11.feed(line)
    pt = threading.Thread(target=pump, daemon=True)
    pt.start()

    stop = {"flag": False}

    def on_term(signum, frame):
        stop["flag"] = True
    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, on_term)

    rc = None
    while not stop["flag"]:
        rc = proc.poll()
        if rc is not None:
            break
        time.sleep(0.5)
    # De lader kan eerder stoppen dan de game (OpenParrotLoader): volgen tot ook de game weg is.
    if rc is not None and not stop["flag"]:
        log("lader gestopt (exitcode %d); de game volgen" % rc)
        quiet = 0
        while not stop["flag"] and quiet < 3:
            quiet = quiet + 1 if not game_alive(set(plan["game_comms"])) else 0
            time.sleep(1)
    pt.join(2)
    return run_einde(plan, rc, helper, server, a11)


def run_einde(plan, rc, helper, server, a11):
    """Het einde van een run: de logbestanden van de kern in het logboek, de A11-oogst, de helper en de pipe dicht."""
    files = []
    if plan["log_full"]:
        for cand in ("teknoparrot_log.txt", "tp_log.txt", "TeknoParrot/teknoparrot_log.txt"):
            for base in (plan["cwd"], os.path.dirname(plan["game"]), plan["tp"]):
                pth = os.path.join(base, cand)
                if os.path.isfile(pth) and os.path.getmtime(pth) > time.time() - 7200 and pth not in files:
                    files.append(pth)
        for pth in files:   # TP_LOGTOFILE=1 stuurt de uitvoer van de lader naar dat bestand: hier alsnog in het logboek
            log("logbestand van de kern: %s" % pth)
            try:
                with open(pth, errors="replace") as f:
                    for line in f:
                        sys.stdout.write("  | " + line.rstrip("\n") + "\n")
            except OSError:
                pass
    a11.write(files)
    if helper.poll() is None:
        helper.terminate()
    server.close()
    log("klaar (exitcode %s)" % (rc if rc is not None else "gestopt"))
    return 0 if rc is None else (rc & 0xFF)


def _start_weg(r):
    """True als deze start echt weg is: het gestarte proces is geeindigd EN er leeft geen proces meer met de naam
    van zijn exe. De lader mag eerder stoppen dan het programma dat hij startte (OpenParrotLoader doet dat), dus het
    einde van de lader alleen zegt niets. Twee keer gemeten met een stap ertussen, zodat een programma dat net
    gestart is en zijn naam nog niet draagt niet voor dood wordt aangezien."""
    for poging in (0, 1):
        if r["proc"].poll() is None or game_alive(set(r["start"]["comms"])):
            return False
        if poging == 0:
            time.sleep(START_STAP)
    return True


def _stop_start(r, waarom):
    """Stop een start: het gestarte proces en wat er nog onder de naam van zijn exe leeft (alleen in onze eigen cgroup)."""
    if r["proc"].poll() is None:
        r["proc"].terminate()
    me, comms = os.getpid(), set(r["start"]["comms"])
    for sig, wachten in ((signal.SIGTERM, 20), (signal.SIGKILL, 4)):
        pids = [pid for pid in own_cgroup_pids() if pid != me and comm_of(pid) in comms]
        if not pids:
            break
        for pid in pids:
            try:
                os.kill(pid, sig)
            except OSError:
                pass
        for _ in range(wachten):
            if not any(comm_of(pid) in comms for pid in pids):
                break
            time.sleep(START_STAP)
    r["stopped_by"] = waarom


def run_starts(plan, env, a11):
    """Start de starts van het plan in hun vaste orde, met de wachttijd ertussen, en volg het spel tot het weg is.

    Elke start krijgt dezelfde omgeving (dus dezelfde Wine-prefix), dezelfde werkmap en dezelfde lader en kern.
    De uitvoer en de exitcode van elke start worden apart bewaard (<state>/starts/<Profiel>-<tijd>/), en elke
    regel in het logboek draagt de naam van de start die hem schreef.

    Is een start al weg voordat de volgende begint, dan wordt dat luid gemeld, alles wat al liep wordt gestopt en
    de run eindigt met EXIT_HALF: een halve opstelling is geen geldige run. Geeft de exitcode van de run."""
    starts = plan["starts"]
    n = len(starts)
    logdir = os.path.join(STATE_DIR, "starts", "%s-%s" % (plan["profile"], time.strftime("%Y%m%d-%H%M%S")))
    os.makedirs(logdir, exist_ok=True)
    for line in start_log_lines(plan):
        log(line)
    log("omgeving (voor elke start dezelfde): %s" % " ".join("%s=%s" % kv for kv in sorted(plan["launch_env"].items())))
    log("uitvoer en exitcode per start: %s" % logdir)
    stop = {"flag": False}

    def on_term(signum, frame):
        stop["flag"] = True
    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, on_term)

    lopend = []

    def begin(i, s):
        r = {"n": i + 1, "start": s, "log": os.path.join(logdir, "%d-%s.log" % (i + 1, s["name"])), "stopped_by": "",
             "meld": False}
        log("start %d/%d (%s): cwd=%s" % (i + 1, n, s["name"], s["cwd"]))
        r["proc"] = subprocess.Popen(s["argv"], cwd=s["cwd"], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        r["pid"] = getattr(r["proc"], "pid", None)
        r["started"] = time.strftime("%H:%M:%S")

        def pump():
            with open(r["log"], "w") as f:
                for raw in r["proc"].stdout:
                    line = decode_console(raw)
                    f.write(line + "\n")
                    f.flush()
                    sys.stdout.write("[%s] %s\n" % (s["name"], line))
                    sys.stdout.flush()
                    if s["role"] == "main":
                        a11.feed(line)
        r["thread"] = threading.Thread(target=pump, daemon=True)
        r["thread"].start()
        lopend.append(r)
        return r

    def verslag(rc_run):
        uit = []
        for r in lopend:
            r["thread"].join(2)
            code = r["proc"].poll()
            log("start %d/%d (%s): exitcode %s%s; uitvoer in %s"
                % (r["n"], n, r["start"]["name"], "nog lopend" if code is None else code,
                   " (gestopt door de launcher: %s)" % r["stopped_by"] if r["stopped_by"] else "", r["log"]))
            uit.append({"n": r["n"], "name": r["start"]["name"], "role": r["start"]["role"], "argv": r["start"]["argv"],
                        "pid": r["pid"], "started": r["started"], "exitcode": code,
                        "exitcode_of": "het gestarte proces (wine met de lader); de lader kan eerder stoppen dan de exe",
                        "stopped_by_launcher": r["stopped_by"], "output": r["log"]})
        for s in starts[len(lopend):]:
            uit.append({"n": starts.index(s) + 1, "name": s["name"], "role": s["role"], "argv": s["argv"],
                        "pid": None, "started": None, "exitcode": None, "not_started": True})
        with open(os.path.join(logdir, "starts.json"), "w") as f:
            json.dump({"profile": plan["profile"], "exitcode_run": rc_run, "starts": uit}, f, indent=1, ensure_ascii=False)
            f.write("\n")

    # De orde: start, wachten, controleren dat alles wat al liep nog leeft, volgende start.
    for i, s in enumerate(starts):
        if i:
            log("wachten: %g s voor start %d/%d (%s)" % (s["wait_before"], i + 1, n, s["name"]))
            dood = None
            for _ in range(int(-(-s["wait_before"] // START_STAP))):
                dood = next((r for r in lopend if _start_weg(r)), None)
                if dood or stop["flag"]:
                    break
                time.sleep(START_STAP)
            dood = dood or next((r for r in lopend if _start_weg(r)), None)
            if stop["flag"] and not dood:
                break
            if dood:
                log("FOUT: start %d/%d (%s) is GESTOPT (exitcode %s) voordat start %d/%d (%s) begon. De run wordt "
                    "afgebroken: een halve opstelling is geen geldige run. De uitvoer van de gestopte start staat in %s"
                    % (dood["n"], n, dood["start"]["name"], dood["proc"].poll(), i + 1, n, s["name"], dood["log"]))
                print("Halve opstelling: start %d/%d (%s) stopte voordat start %d/%d (%s) begon"
                      % (dood["n"], n, dood["start"]["name"], i + 1, n, s["name"]), file=sys.stderr)
                for r in lopend:
                    if r is not dood:
                        _stop_start(r, "een eerdere start was weg voordat de volgende begon")
                verslag(EXIT_HALF)
                return EXIT_HALF
        begin(i, s)

    # Volgen: de run duurt zolang het spel leeft. Stopt een andere start onderweg, dan staat dat een keer in het logboek.
    hoofd = next((r for r in lopend if r["start"]["role"] == "main"), None)
    rc = None
    while hoofd and not stop["flag"]:
        rc = hoofd["proc"].poll()
        if rc is not None:
            break
        for r in lopend:
            if r is not hoofd and not r["meld"] and _start_weg(r):
                r["meld"] = True
                log("LET OP: start %d/%d (%s) is gestopt (exitcode %s) terwijl het spel nog draait; uitvoer in %s"
                    % (r["n"], n, r["start"]["name"], r["proc"].poll(), r["log"]))
        time.sleep(0.5)
    if hoofd and rc is not None and not stop["flag"]:
        log("lader van %s gestopt (exitcode %d); het spel volgen" % (hoofd["start"]["name"], rc))
        quiet = 0
        while not stop["flag"] and quiet < 3:
            quiet = quiet + 1 if not game_alive(set(hoofd["start"]["comms"])) else 0
            time.sleep(1)
    # Het spel is weg (of de run is gestopt): wat er nog naast liep hoort er niet te blijven staan.
    for r in lopend:
        if r is not hoofd or stop["flag"]:
            if not _start_weg(r):
                _stop_start(r, "het spel is gestopt" if not stop["flag"] else "de run is gestopt")
    verslag(None if rc is None else (rc & 0xFF))
    return rc


def dropin_weiger(plan):
    """EXIT_PATCH (en de fouten op stderr) als een drop-in van deze game niet geplaatst of niet geladen kan
    worden; anders 0. Net als een geweigerde patch: niet starten en niet terugvallen op TeknoParrotUi, want
    zonder zijn drop-in is het een andere game dan bedoeld en is elke meting eraan ongeldig."""
    fouten = (plan.get("dropins") or {}).get("errors", [])
    for e in fouten:
        print("Drop-in geweigerd: %s" % e, file=sys.stderr)
    return EXIT_PATCH if fouten else 0


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="Directe start van een TeknoParrot-game zonder TeknoParrotUi")
    ap.add_argument("profile")
    ap.add_argument("--tp", default=os.environ.get("RAVENGUN_TP_DIR", DEFAULT_TP))
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--plan", action="store_true")
    g.add_argument("--check", action="store_true")
    g.add_argument("--run", action="store_true")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--shm-source")
    ap.add_argument("--log-full", action="store_true")
    ap.add_argument("--enforce-activation", action="store_true", help=argparse.SUPPRESS)  # TP-Patreon-poort toch afdwingen (uit; zie docs/decisions.md)
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--exe", help="start het spel vanaf dit pad (X:\\map\\spel.exe, een Unix-pad, of relatief aan de view); "
                                  "de rest van de opstelling blijft gelijk")
    ap.add_argument("--drive", action="append", default=[], metavar="LETTER=MAP",
                    help="zet deze schijfletter voor de duur van de run in de prefix (MAP relatief aan de view; herhaalbaar)")
    a = ap.parse_args(argv)
    extra = {}
    for e in a.env:
        if "=" not in e:
            print("--env vereist K=V", file=sys.stderr)
            return 2
        k, v = e.split("=", 1)
        extra[k] = v
    try:
        plan = build_plan(a.profile, a.tp, extra, a.shm_source, a.log_full, materialize_patches=a.run,
                          exe=a.exe, drives=a.drive)
        # TeknoParrots Patreon-activering is hun verdienmodel in hun software, geen beveiliging in de game zelf. Op
        # onze eigen, onafhankelijke stack (eigen lader/kern/launcher/JVS-brug) is er geen TeknoParrot-software meer
        # om voor te betalen, dus dwingen we die poort niet meer af (beslissing eigenaar 2026-10-12; docs/decisions.md).
        # plan["needs_activation"] blijft als informatie staan ("door TeknoParrot achter Patreon gezet"); de echte
        # game-beveiliging (dongle/keychip/integriteit) blijft onaangeroerd, en check_activation() wordt niet omzeild
        # of gekraakt -- hij wordt alleen niet meer aangeroepen.
        if plan["needs_activation"] and a.enforce_activation:
            check_activation(plan["prefix"])
    except SettingRefused as e:
        print("Instelling geweigerd: %s" % e, file=sys.stderr)
        return EXIT_PATCH
    except PatchRefused as e:
        print("Patch geweigerd: %s" % e, file=sys.stderr)
        return EXIT_PATCH
    except Unsupported as e:
        print("Niet direct te starten: %s" % e, file=sys.stderr)
        return EXIT_UNSUPPORTED
    except ActivationMissing as e:
        print("Activering: %s" % e, file=sys.stderr)
        return EXIT_ACTIVATION
    if a.plan:
        show = {k: v for k, v in plan.items() if k != "env"}
        if a.json:
            print(json.dumps(show, indent=1, ensure_ascii=False))
        else:
            for k, v in show.items():
                if k == "files":
                    for path, text in v:
                        print("bestand (%s) %s:\n  %s" % (place_file(path, text, dry=True)[0], path,
                                                         text.rstrip("\n").replace("\n", "\n  ")))
                else:
                    print("%s: %s" % (k, v))
            for line in dropin_log_lines(plan):
                print("drop-in: " + line)
            if len(plan["starts"]) > 1:
                for line in start_log_lines(plan):
                    print(line)
            for line in exe_log_lines(plan) + arg_log_lines(plan) + env_log_lines(plan):
                print(line)
            for line in (plan.get("start_script") or {}).get("lines", []):
                print(line)
        return dropin_weiger(plan)
    if dropin_weiger(plan):
        return EXIT_PATCH
    if a.check:
        print("ok: %s via %s" % (a.profile, os.path.basename(plan["argv"][plan["argv"].index(plan["wine"]) + 1])))
        return 0
    return run(plan)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

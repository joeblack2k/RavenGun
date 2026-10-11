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
#            (geen terugval). Anders de exitcode van de lader.
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

HOME = os.environ.get("HOME", "/home/deck")
DEFAULT_TP = os.path.join(HOME, ".local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64")
DEFAULT_WINE = os.path.join(HOME, ".local/share/ravengun/wine/proton-11.0-2/bin/wine")
TPUI_DATA = os.path.join(HOME, ".local/share/TeknoParrotUI")
SHARED_PREFIX = os.path.join(TPUI_DATA, "prefixes", "shared", "wine")
SHM_NAME = "TeknoParrot_JvsState"
SHM_PATH = "/dev/shm/" + SHM_NAME
SHM_SIZE = 64
JVS_PIPE = "TeknoParrot_JVS"
STATE_DIR = os.path.join(os.environ.get("XDG_STATE_HOME", os.path.join(HOME, ".local/state")), "ravengun")

EXIT_ACTIVATION = 3
EXIT_UNSUPPORTED = 4
EXIT_PATCH = 5       # een ingeschakelde patch is geweigerd: niet terugvallen op TeknoParrotUi (die zou de game zonder patch starten)

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
    """Alleen profielen met Patreon = true vallen onder het abonnement; de rest start zonder activering. De waarde komt van het sjabloon van
    de runtime en van de lagen van game.ini (build_plan): geen enkele laag kan het abonnement uitzetten."""
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
                           "dir": dropin_dir, "ini": pad, "src": os.path.join(dropin_dir, naam)}
    return list(specs.values()), fouten


def _binnen(path, base):
    """True als `path` lexicaal binnen `base` ligt."""
    p, b = os.path.abspath(path), os.path.abspath(base)
    return p == b or p.startswith(b + os.sep)


def dropin_materialiseer(base, dest, log=None):
    """Zorg dat de map van `dest` een ECHTE map in de view is, ook als er een symlink naar de NAS
    tussen zit. Elke symlink-map op de weg wordt vervangen door een echte map met symlinks naar de
    inhoud, zodat alleen het drop-in-bestand zelf echt wordt en de rest hetzelfde blijft.

    Waarom: de views van de games zijn symlink-boerderijen naar de NAS (views/<Game>/Game_Data is
    bijvoorbeeld een symlink). Een drop-in met To = Game_Data/Plugins wijst dan via die symlink naar
    de NAS, en de regel "nooit naar de NAS schrijven" zou hem weigeren. In plaats van weigeren maken
    we de map lokaal -- dat is precies wat een view is.

    Schrijft NOOIT op de NAS: er wordt alleen in de view gematerialiseerd en er wordt gecontroleerd
    dat de map van het doel daarna echt binnen de view uitkomt. Geeft (ok, melding)."""
    base = os.path.abspath(base)
    dest = os.path.abspath(dest)
    if not _binnen(dest, base):
        return False, "het doel %s ligt buiten de view %s" % (dest, base)
    ouder = os.path.dirname(dest)
    rel = os.path.relpath(ouder, base)
    delen = [] if rel == "." else rel.split(os.sep)
    cur = base
    for deel in delen:
        cur = os.path.join(cur, deel)
        if os.path.islink(cur):
            doel = os.path.realpath(cur)
            if not os.path.isdir(doel):
                return False, "%s is een symlink naar iets dat geen map is" % cur
            tijdelijk = cur + ".ravengun-tmp"
            if os.path.exists(tijdelijk):
                shutil.rmtree(tijdelijk)
            os.mkdir(tijdelijk)
            for naam in os.listdir(doel):
                os.symlink(os.path.join(doel, naam), os.path.join(tijdelijk, naam))
            os.unlink(cur)
            os.rename(tijdelijk, cur)
            if log:
                log("%s was een symlink naar de NAS; vervangen door een echte map met symlinks"
                    % os.path.relpath(cur, base))
        elif not os.path.isdir(cur):
            os.makedirs(cur, exist_ok=True)
    if os.path.islink(dest):
        os.unlink(dest)
    if not _binnen(os.path.realpath(os.path.dirname(dest)), base):
        return False, "de map van het doel %s komt na het materialiseren nog buiten de view uit" % dest
    return True, "de map van %s is lokaal" % os.path.relpath(dest, base)


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
    env, preload, overrides, bestanden = {}, [], [], []
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
        bestanden.append({"src": s["src"], "dest": dest, "name": s["name"]})
        if s["dll"]:
            dllnaam = s["name"][:-4] if s["name"].lower().endswith(".dll") else s["name"]
            overrides.append("%s=%s" % (dllnaam, "n" if s["dll"] == "native" else "n,b"))
        if s["preload"]:
            preload.append(dest)
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
        #   anders                      -> RG_DROPIN_PRELOAD, dat de eigen lader oppakt.
        elf_onder_wine = bool(plan.get("wine")) and not is_pe(plan["game"])
        if elf_onder_wine:
            var = "LD_AUDIT"
        elif not plan.get("wine"):
            var = "LD_PRELOAD"
        else:
            var = ""
        if var:
            delen = [x for x in (plan["env"].get(var, "") or "").split(":") if x]
            env[var] = ":".join(delen + [p for p in preload if p not in delen])
        else:
            delen = [x for x in (plan["env"].get("RG_DROPIN_PRELOAD", "") or "").split(":") if x]
            env["RG_DROPIN_PRELOAD"] = ":".join(delen + [p for p in preload if p not in delen])
        # Het pad blijft altijd ook als RG_DROPIN_PRELOAD staan, zodat een eigen lader het kan oppakken.
        delen = [x for x in (plan["env"].get("RG_DROPIN_PRELOAD", "") or "").split(":") if x]
        env["RG_DROPIN_PRELOAD"] = ":".join(delen + [p for p in preload if p not in delen])
    return {"base": base, "files": bestanden, "env": env, "overrides": overrides, "preload": preload,
            "specs": specs, "errors": fouten}


def dropin_log_lines(plan):
    """Eén regel per drop-in, voor het logboek en voor --plan."""
    dp = plan.get("dropins") or {}
    uit = []
    for e in dp.get("errors", []):
        uit.append("LET OP: %s" % e)
    for f in dp.get("files", []):
        uit.append("%s -> %s" % (f["name"], f["dest"]))
    if dp.get("overrides"):
        uit.append("WINEDLLOVERRIDES += %s" % ", ".join(dp["overrides"]))
    if dp.get("preload"):
        uit.append("RG_DROPIN_PRELOAD += %s (de native route en de eigen lader zetten dit in LD_PRELOAD van "
                   "het ELF-proces; in de omgeving van Wine kan het niet -- Cygwin sterft eraan)"
                   % ", ".join(dp["preload"]))
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


def own_build(loader_settings, et, exe_rel, dll_rel):
    """[Loader] Build = ravengun: de OpenParrot-lader (en -kern) uit de eigen build (scripts/build-openparrot.sh) in plaats van
    uit de TeknoParrot-runtime. Geeft (lader, kern-map); (None, None) = alles uit de runtime (standaard).
      Build = teknoparrot | ravengun   lader en, bij EmulatorType OpenParrot, de kern
      Core  = teknoparrot | ravengun   alleen de kern apart (bv. eigen lader met de kern van TeknoParrot); leeg = volgt Build
      Path  = <map>                    map met OpenParrotWin32/ en OpenParrotx64/ (standaard: .../ravengun/openparrot/current)"""
    build = loader_settings.get("Build", "").strip().lower()
    core = loader_settings.get("Core", "").strip().lower()
    for key, val in (("Build", build), ("Core", core)):
        if val not in ("", "teknoparrot", "ravengun"):
            raise SettingRefused("[Loader] %s = %s (kies teknoparrot of ravengun)" % (key, val))
    if not core and build == "ravengun" and et == "OpenParrot":
        core = "ravengun"   # Build volgt voor de kern alleen bij EmulatorType OpenParrot; TeknoParrot-profielen houden hun eigen kern
    if build != "ravengun" and core != "ravengun":
        return None, None
    base = gs.expand_home(loader_settings.get("Path", "").strip() or OWN_BUILD_DEFAULT)
    loader = None
    if build == "ravengun":
        if not os.path.basename(exe_rel).lower().startswith("openparrotloader"):
            raise SettingRefused("[Loader] Build = ravengun bestaat alleen voor OpenParrotLoader(64).exe, "
                                 "niet voor %s" % exe_rel)
        loader = os.path.join(base, exe_rel)
    core_dir = None
    if core == "ravengun":
        if et != "OpenParrot" or not dll_rel:
            raise SettingRefused("een eigen kern bestaat alleen voor EmulatorType OpenParrot (dit is %s)" % et)
        core_dir = base
    for f in (loader, os.path.join(core_dir, dll_rel + ".dll") if core_dir else None):
        if f and not os.path.isfile(f):
            raise SettingRefused("eigen OpenParrot-build ontbreekt: %s (bouwen: scripts/build-openparrot.sh)" % f)
    return loader, core_dir


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
    """De prefix waarin de game draait: RG_WINEPREFIX bij de wrapper wine-prefix, anders zoals TeknoParrotUi."""
    if os.path.basename(wine) == "wine-prefix":
        pfx = env.get("RG_WINEPREFIX", "")
        if not pfx:
            raise Unsupported("WineRunnerPath is de wrapper wine-prefix maar RG_WINEPREFIX ontbreekt in %s.env"
                              % p["ProfileName"])
        return pfx, False
    if env.get("TP_WINEPREFIX"):
        return env["TP_WINEPREFIX"], True
    mode = p.get("WinePrefixMode", "")
    legacy = os.path.join(TPUI_DATA, "prefixes", p["ProfileName"])
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


def build_plan(name, tp, extra_env=None, shm_source=None, log_full=False, base_env=None, materialize_patches=False, use_own=True):
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
    if truthy(p.get("HasTwoExecutables", "false")):
        raise Unsupported("profielen met twee exe's worden niet direct gestart")
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
    own_loader, own_core_dir = own_build(settings.settings.get("Loader", {}), et, exe_rel, dll_rel)   # [Loader] Build/Core/Path
    if own_loader:
        loader = own_loader
        for pt in settings.patches:   # de Wine-fix zit in de eigen lader zelf (broncodewijziging); de bytepatch past er niet op
            if pt.name == gs.FPFIX_PATCH:
                pt.fields["Enabled"] = "false"
    core_dir = own_core_dir or tp
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

    args = [wine, loader]
    if dll_rel:
        args.append(wine_path(os.path.join(core_dir, dll_rel)))
    args.append(wine_path(game))
    for extra in (p.get("CustomArguments", ""), p.get("ExtraParameters", "")):
        if extra.strip():
            args += shlex.split(extra.replace("\\", "\\\\"))

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

    pipe_mode = "pipe" if emu not in NO_JVS_PIPE else "shm"
    src = shm_source or SHM_PATH
    helper = os.path.join(tp, "pipehelper.exe")
    if not os.path.isfile(helper):
        raise Unsupported("pipehelper.exe ontbreekt in de runtime-map")

    comms = {"budgieloader.ex", "budgieloader_x6", "openparrotloade", "teknomacaw.exe", "teknomacaw64.ex"}
    for key in ("ExecutableName", "GamePath"):
        for part in p.get(key, "").split(";"):
            b = part.replace("\\", "/").rsplit("/", 1)[-1].lower()[:15]
            if b:
                comms.add(b)
    uit = {
        "profile": name, "tp": tp, "emulator_type": et, "emulation_profile": emu, "is64": is64,
        "game": game, "cwd": cwd, "wine": wine, "prefix": prefix, "argv": (gamescope or []) + args,
        "gamescope": bool(gamescope), "launch_env": launch, "profile_env": prof_env, "env": env, "patches": patch_report,
        "settings_sources": settings.sources, "profile_source": "game.ini" if own else "UserProfiles/%s.xml" % name,
        "game_source": p.get("_game_source", "xml"),
        "files": files, "pipe_mode": pipe_mode, "shm_source": src, "pipehelper": helper, "game_comms": sorted(comms),
        "log_full": log_full, "windowed_override": windowed_override,
        "recipe": recipe_summary(tp, name),
        "needs_activation": needs_activation(p),
        "windows": is_pe(game),
    }
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
    env = plan["env"]
    configure_jvs(env)
    # 0a. Drop-ins (docs/dropins.md): de bestanden in de view zetten en de omgeving bijwerken. Dit gebeurt
    # vóór alles wat de game opent, en nooit op de NAS.
    for line in dropin_log_lines(plan):
        log("drop-in: " + line)
    for f in plan["dropins"]["files"]:
        st, why = dropin_materialiseer(plan["dropins"]["base"], f["dest"], log)
        if not st:
            log("drop-in: LET OP: %s" % why)
            continue
        st, why = place_binary(f["src"], f["dest"])
        log("drop-in: " + why)
    merge_env(plan, plan["dropins"]["env"])
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
    # 4. De lader (eventueel in gamescope).
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
    ap.add_argument("--no-activation-check", action="store_true", help=argparse.SUPPRESS)  # alleen tests
    ap.add_argument("--env", action="append", default=[])
    a = ap.parse_args(argv)
    extra = {}
    for e in a.env:
        if "=" not in e:
            print("--env vereist K=V", file=sys.stderr)
            return 2
        k, v = e.split("=", 1)
        extra[k] = v
    try:
        plan = build_plan(a.profile, a.tp, extra, a.shm_source, a.log_full, materialize_patches=a.run)
        if plan["needs_activation"] and not a.no_activation_check:
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
        return 0
    if a.check:
        print("ok: %s via %s" % (a.profile, os.path.basename(plan["argv"][plan["argv"].index(plan["wine"]) + 1])))
        return 0
    return run(plan)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

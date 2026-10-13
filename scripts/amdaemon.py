#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# De amdaemon-laag van RavenGun: wat een Sega ALLS-titel (HOTD Scarlet Dawn) aan de AM Daemon heeft,
# en of die laag startklaar is.
#
# Dit is een HULPMIDDEL, geen launcherhaak. Sinds docs/geen-dongle-in-de-loader.md kent de lader geen
# donglesoorten meer: er is geen `kind`, geen dongle.json en geen poort. Wat een game nodig heeft staat
# in patches/<Game>/ (docs/dropins.md), en een software-dongle is daar een gewone drop-in.
#
# Dit script beantwoordt de vraag die daaraan voorafgaat: HOE ziet de amdaemon-laag van deze game eruit,
# en is er iets te plaatsen? Het leest daarvoor de bestanden van de game zelf:
#   * patches/<Game>/game.ini            het profiel: twee exe's, de volgorde, de paden
#   * UserProfiles/<P>.xml in de runtime GamePath (het pad naar de game)
#   * de spelmap of de view              de .bat-keten, AMDaemon/config.json, pxGetHwInfo.ini,
#                                        en de mappen TeknoParrot/<LETTER> (alleen namen en aantallen)
#
# Het script is READ-ONLY. Het leest NOOIT de inhoud van appfile.dat, sysfile.dat, amdaemon.log of een
# dongle-image: daar kunnen serienummers en keychip-identificaties in staan. Van zulke bestanden meldt
# het alleen of ze er zijn en hoe groot ze zijn.
#
# Gebruik:
#   scripts/amdaemon.py --game <Game> [--json] [--view <pad>] [--game-dir <pad>] [--verbose]
#   Exit 0 = de laag is in orde EN startbaar, 1 = niet in orde of nog niet startbaar.
#
# Voor tests: RAVENGUN_PATCHES_DIR, RAVENGUN_USER_PATCHES_DIR, RAVENGUN_VIEWS_DIR, RAVENGUN_TP_DIR,
# RAVENGUN_GAME_DIR.
import configparser
import glob
import json
import os
import re
import sys

sys.dont_write_bytecode = True

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I neemt de scriptmap niet mee
import gamesettings as gs  # noqa: E402  (de ene zoekvolgorde van patches/<Game>/)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOME = os.environ.get("HOME", "/home/deck")

# De bestanden van de startketen die we controleren, relatief aan de spelmap.
KETEN = (
    "game.bat",
    "App/Startup/AppStartup.bat",
    "App/Startup/99_StartApp.bat",
    "App/Hodzero/StartApp.bat",
)

# De schijfletters die de game zoekt. De kern van TeknoParrot zet ze naast de exe in TeknoParrot/<LETTER>/.
# X: is de image-root uit game.bat (IMAGE_ROOT_DIR); AMFS, D en Y staan in de spelmap zelf.
LETTERS = ("AMFS", "D", "Y")


# --------------------------------------------------------------------------- paden


def patches_dirs():
    """De mappen met patches/<Game>/ voor "eerste treffer wint": de zoekvolgorde van gamesettings.patches_roots(), omgekeerd
    (ontwikkel-checkout, gebruiker, patches/ van de loader). Geen eigen kopie van die volgorde."""
    return list(reversed(gs.patches_roots()))


def views_dir():
    """De map met de views van de games: $RAVENGUN_VIEWS_DIR, anders <data>/ravengun/runtime/views."""
    d = os.environ.get("RAVENGUN_VIEWS_DIR")
    if d:
        return d
    xdg = os.environ.get("XDG_DATA_HOME", "")
    base = xdg if xdg.startswith("/") else os.path.join(HOME, ".local/share")
    return os.path.join(base, "ravengun", "runtime", "views")


def view_dir(game):
    return os.path.join(views_dir(), game)


def runtime_dir():
    """De map van de TeknoParrotUi-runtime (voor UserProfiles/<P>.xml). $RAVENGUN_TP_DIR gaat voor."""
    d = os.environ.get("RAVENGUN_TP_DIR")
    if d:
        return d
    xdg = os.environ.get("XDG_DATA_HOME", "")
    base = xdg if xdg.startswith("/") else os.path.join(HOME, ".local/share")
    kandidaten = sorted(glob.glob(os.path.join(base, "ravengun", "runtime", "TeknoParrotUi-*")))
    return kandidaten[-1] if kandidaten else ""


def game_map(game):
    """De map patches/<Game>/ van de eerste laag die er een heeft, of None."""
    for d in patches_dirs():
        p = gs.find_game_dir(d, str(game))      # op [Game] Profile, niet alleen op de mapnaam
        if p:
            return p
    return None


# --------------------------------------------------------------------------- lezen


def lees_ini(pad, commentaar_achter_de_waarde=False):
    """Een ini-bestand als configparser, tolerant: geen interpolatie, dubbele sleutels toegestaan.
    Leest als UTF-8 en valt terug op Shift-JIS en latin-1 (de Sega-bestanden zijn cp932).

    `commentaar_achter_de_waarde` zet `;` ook als commentaarteken midden op de regel. Dat is nodig voor
    pxGetHwInfo.ini, waar de waarde en het Japanse commentaar op dezelfde regel staan
    (`platform-id = ACA0   ; ALLS UX専用なら ...`). Voor game.ini staat het uit: daar kan een `;` in een
    Note-veld zitten.
    """
    c = configparser.RawConfigParser(
        interpolation=None, strict=False,
        inline_comment_prefixes=(";",) if commentaar_achter_de_waarde else None)
    c.optionxform = str          # sleutels hoofdlettergevoelig houden
    for enc in ("utf-8", "cp932", "latin-1"):
        try:
            with open(pad, encoding=enc, errors="strict") as f:
                tekst = f.read()
        except (OSError, UnicodeDecodeError):
            continue
        try:
            c.read_string(tekst)
            return c
        except configparser.Error:
            continue
    return None


def zonder_commentaar(waarde):
    """De waarde tot het eerste `;`, en zonder omringende spaties en tabs. pxGetHwInfo.ini zet het
    commentaar achter de waarde op dezelfde regel; configparser laat dat staan als je het niet vraagt."""
    return str(waarde or "").split(";")[0].strip()


def lees_tekst(pad):
    """Een tekstbestand als string, met dezelfde encodingsreeks. Leeg als het niet lukt."""
    for enc in ("utf-8", "cp932", "latin-1"):
        try:
            with open(pad, encoding=enc, errors="strict") as f:
                return f.read()
        except (OSError, UnicodeDecodeError):
            continue
    return ""


def _get(c, sectie, sleutel, standaard=""):
    if c is None:
        return standaard
    try:
        return c.get(sectie, sleutel).strip()
    except (configparser.Error, KeyError):
        return standaard


def waar(waarde):
    return str(waarde or "").strip().lower() in ("1", "true", "yes", "ja", "on")


def _bat_variabele(tekst, naam):
    """De waarde van `set NAAM=...` uit een .bat, of "". De laatste toewijzing wint."""
    m = re.findall(r"(?im)^\s*set\s+%s\s*=\s*(.*?)\s*$" % re.escape(naam), tekst)
    return m[-1] if m else ""


def _expand(waarde, variabelen):
    """%NAAM% vervangen door de waarde uit het woordenboek. Twee ronden, zodat %A% dat zelf %B% bevat
    ook klopt (99_StartApp.bat zet APP_DIR=Y:\\%GAME_ID%)."""
    uit = str(waarde or "")
    for _ in range(2):
        nieuw = re.sub(r"%([A-Za-z_][A-Za-z0-9_]*)%",
                       lambda m: variabelen.get(m.group(1).upper(), m.group(0)), uit)
        if nieuw == uit:
            break
        uit = nieuw
    return uit


def _grootte(pad):
    try:
        return os.path.getsize(pad)
    except OSError:
        return None


# --------------------------------------------------------------------------- de spelmap vinden


def game_root(game, view=None, override=None):
    """(pad, herkomst) van de spelmap. Volgorde: --game-dir, RAVENGUN_GAME_DIR, de view, UserProfiles/<P>.xml."""
    if override:
        return override, "--game-dir"
    if os.environ.get("RAVENGUN_GAME_DIR"):
        return os.environ["RAVENGUN_GAME_DIR"], "RAVENGUN_GAME_DIR"
    if view and os.path.isdir(view):
        return view, "view (--view)"
    v = view_dir(game)
    if os.path.isdir(v):
        return v, "view"
    return _root_uit_profiel(game)


def _root_uit_profiel(game):
    """De spelmap afleiden uit UserProfiles/<P>.xml: GamePath minus het pad uit [Identify] Path."""
    tp = runtime_dir()
    if not tp:
        return None, ""
    xml = os.path.join(tp, "UserProfiles", str(game) + ".xml")
    if not os.path.isfile(xml):
        return None, ""
    tekst = lees_tekst(xml)
    m = re.search(r"<GamePath>(.*?)</GamePath>", tekst, re.S)
    if not m or not m.group(1).strip():
        return None, ""
    exe = m.group(1).strip()
    gm = game_map(game)
    rel = ""
    if gm:
        c = lees_ini(os.path.join(gm, "game.ini"))
        rel = _get(c, "Identify", "Path")
    rel_n = rel.replace("\\", "/").strip("/")
    exe_n = exe.replace("\\", "/")
    if rel_n and exe_n.endswith(rel_n):
        return exe_n[: -len(rel_n)].rstrip("/"), "UserProfiles/%s.xml" % game
    # geen [Identify] Path: dan de map van de exe
    return os.path.dirname(exe_n), "UserProfiles/%s.xml (map van de exe)" % game


def _veilig_binnen(basis, pad):
    """True als pad (na het oplossen van symbolische koppelingen) binnen basis ligt."""
    try:
        b = os.path.realpath(basis)
        p = os.path.realpath(pad)
    except OSError:
        return False
    return p == b or p.startswith(b + os.sep)


# --------------------------------------------------------------------------- de controles


def _profiel(uit, game):
    """Het profiel: heeft deze game echt twee programma's, en in welke volgorde?"""
    pr = {"bron": None, "twee_exe": False, "daemon_eerst": False, "executable": "", "executable2": "",
          "extra_parameters": "", "identify_path": "", "identify_path2": "", "launch_mode": "",
          "fouten": []}
    gm = game_map(game)
    if gm:
        p = os.path.join(gm, "game.ini")
        if os.path.isfile(p):
            pr["bron"] = p
            c = lees_ini(p)
            pr["twee_exe"] = waar(_get(c, "Game", "HasTwoExecutables", "false"))
            pr["daemon_eerst"] = waar(_get(c, "Game", "LaunchSecondExecutableFirst", "false"))
            pr["executable"] = _get(c, "Game", "Executable")
            pr["executable2"] = _get(c, "Game", "Executable2")
            pr["extra_parameters"] = _get(c, "Launch", "ExtraParameters")
            pr["identify_path"] = _get(c, "Identify", "Path")
            pr["identify_path2"] = _get(c, "Identify", "Path2")
            pr["launch_mode"] = _get(c, "Launch", "Mode")
    if not pr["bron"]:
        pr["fouten"].append("geen patches/%s/game.ini gevonden in %s" % (game, " of ".join(patches_dirs())))
    if not pr["twee_exe"]:
        pr["fouten"].append("game.ini zegt niet HasTwoExecutables = true; dan is er geen amdaemon-laag nodig")
    elif not pr["daemon_eerst"]:
        pr["fouten"].append("HasTwoExecutables = true maar LaunchSecondExecutableFirst staat niet aan")
    uit["profiel"] = pr
    return pr


def _keten(uit, root):
    """De .bat-keten: bestaat hij, en wat zegt hij over het tweede programma?"""
    keten = {"bestanden": {}, "waarden": {}, "fouten": []}
    for rel in KETEN:
        p = os.path.join(root, rel)
        keten["bestanden"][rel] = os.path.isfile(p)
        if not os.path.isfile(p):
            keten["fouten"].append("de startketen mist %s" % rel)

    start = os.path.join(root, "App/Hodzero/StartApp.bat")
    if os.path.isfile(start):
        t = lees_tekst(start)
        keten["waarden"] = {
            "DAEMON_DIR": _bat_variabele(t, "DAEMON_DIR"),
            "DAEMON_EXE_FILE_NAME": _bat_variabele(t, "DAEMON_EXE_FILE_NAME"),
            "DAEMON_WAIT_SECONDS": _bat_variabele(t, "DAEMON_WAIT_SECONDS"),
            "ROOT_DIR": _bat_variabele(t, "ROOT_DIR"),
            "daemon_opdracht": 'start /min "" "%DAEMON_EXE_PATH%" -c %DAEMON_CONFIG_PATH% -l "%DAEMON_LOG_PATH%"'
                               if '-c %DAEMON_CONFIG_PATH%' in t else "",
            "wacht_opdracht": "timeout %DAEMON_WAIT_SECONDS%" if "timeout %DAEMON_WAIT_SECONDS%" in t else "",
            "game_argument": '-launch="%DETAIL_LAUNCH_MODE%"' if '-launch="%DETAIL_LAUNCH_MODE%"' in t else "",
            "game_opdracht": 'start /wait "" "%APP_EXE_PATH%" %APP_EXE_ARGS%'
                             if 'start /wait "" "%APP_EXE_PATH%"' in t else "",
            "daemon_doden": 'taskkill /im "%DAEMON_EXE_FILE_NAME%"' if 'taskkill /im' in t else "",
        }
    start99 = os.path.join(root, "App/Startup/99_StartApp.bat")
    if os.path.isfile(start99):
        t = lees_tekst(start99)
        keten["waarden"]["GAME_ID"] = _bat_variabele(t, "GAME_ID")
        keten["waarden"]["APP_DIR"] = _bat_variabele(t, "APP_DIR")
        keten["waarden"]["LAUNCH_MODE"] = _bat_variabele(t, "LAUNCH_MODE")
        keten["waarden"]["APP_LOG_DIR"] = _bat_variabele(t, "APP_LOG_DIR")
    game_bat = os.path.join(root, "game.bat")
    if os.path.isfile(game_bat):
        t = lees_tekst(game_bat)
        keten["waarden"]["LAUNCH_ON_IMAGE"] = _bat_variabele(t, "LAUNCH_ON_IMAGE")
        keten["waarden"]["IMAGE_ROOT_DIR"] = _bat_variabele(t, "IMAGE_ROOT_DIR") or "X:\\"
    # %NAAM% uitrekenen: de keten zet APP_DIR=Y:\%GAME_ID% en de daemon leest die weer.
    keten["waarden_ruw"] = dict(keten["waarden"])
    for sleutel, waarde in list(keten["waarden"].items()):
        keten["waarden"][sleutel] = _expand(waarde, keten["waarden"])
    uit["keten"] = keten
    return keten


def _daemon(uit, root, game):
    """De daemon zelf: de exe, config.json, de mappen TeknoParrot/<LETTER>, en het origineel ernaast."""
    d = {"dir": None, "exe": None, "exe_grootte": None, "config": None, "config_waarden": {},
         "origineel": None, "origineel_grootte": None, "origineel_is_huls": None,
         "mounts": {}, "log": None, "fouten": [], "waarschuwingen": []}

    gm = game_map(game)
    rel2 = ""
    if gm:
        c = lees_ini(os.path.join(gm, "game.ini"))
        rel2 = _get(c, "Identify", "Path2")
    if rel2:
        d["exe"] = os.path.join(root, rel2.replace("\\", "/"))
    if not d["exe"] or not os.path.isfile(d["exe"]):
        p = os.path.join(root, "App/Hodzero/WindowsNoEditor/AMDaemon/amdaemon.exe")
        if os.path.isfile(p):
            d["exe"] = p
    if not d["exe"] or not os.path.isfile(d["exe"]):
        d["fouten"].append("amdaemon.exe niet gevonden onder %s (verwacht: %s)"
                           % (root, rel2 or "App/Hodzero/WindowsNoEditor/AMDaemon/amdaemon.exe"))
        uit["daemon"] = d
        return d

    d["dir"] = os.path.dirname(d["exe"])
    d["exe_grootte"] = _grootte(d["exe"])

    # De originelen die TeknoParrot naast de gepatchte bestanden bewaart. Alleen naam en grootte:
    # het zijn gepakte hulzen zonder leesbare Sega-strings (docs/research/amdaemon-mechanisme.md).
    for naam in ("amdaemon.exe.original", "amdaemon.exe.org", "amdaemon_org.exe"):
        p = os.path.join(d["dir"], naam)
        if os.path.isfile(p):
            d["origineel"] = naam
            d["origineel_grootte"] = _grootte(p)
            break

    cfg = os.path.join(d["dir"], "config.json")
    d["config"] = cfg if os.path.isfile(cfg) else None
    if not d["config"]:
        d["waarschuwingen"].append(
            "AMDaemon/config.json ontbreekt in %s. De .bat-keten geeft hem mee met -c, maar het "
            "TeknoParrot-profiel start amdaemon.exe zonder argumenten, dus de daemon neemt dan zijn "
            "eigen standaardwaarden." % d["dir"])
    else:
        try:
            with open(cfg, encoding="utf-8", errors="replace") as f:
                cj = json.load(f)
            alg = cj.get("common", {}) or {}
            fout = cj.get("error", {}) or {}
            vid = cj.get("video", {}) or {}
            cred = cj.get("credit", {}) or {}
            io = cj.get("usb_io", {}) or {}
            nodes = io.get("node", []) or []
            inp = cj.get("input", {}) or {}
            d["config_waarden"] = {
                "max_player": alg.get("max_player"),
                "language": alg.get("language"),
                "auto_reset": fout.get("auto_reset"),
                "video_enable": vid.get("enable"),
                "video_resolution": vid.get("resolution"),
                "video_ignore_unconnected": vid.get("ignore_unconnected"),
                "credit_enable": cred.get("enable"),
                "usb_io_enable": io.get("enable"),
                "usb_io_nodes": len(nodes),
                "usb_io_functies": sorted((nodes[0].get("required_function") or {}).keys()) if nodes else [],
                "input_system": len(inp.get("system", []) or []),
                "input_players": len(inp.get("players", []) or []),
            }
        except (OSError, ValueError) as e:
            d["fouten"].append("AMDaemon/config.json is niet te lezen: %s" % e)

    # De schijfletters. Alleen namen en aantallen: nooit de inhoud van de bestanden erin.
    tp = os.path.join(d["dir"], "TeknoParrot")
    for letter in LETTERS:
        p = os.path.join(tp, letter)
        if os.path.isdir(p):
            aantal = 0
            for _, _, bestanden in os.walk(p):
                aantal += len(bestanden)
            d["mounts"][letter] = {"aanwezig": True, "bestanden": aantal}
        else:
            d["mounts"][letter] = {"aanwezig": False, "bestanden": 0}
    if not os.path.isdir(tp):
        d["waarschuwingen"].append(
            "de map %s ontbreekt. De game zoekt de schijfletters AMFS, D en Y; zonder die map vindt hij "
            "niets. Onder TeknoParrot maakt de kern die map aan (omleiding naar "
            "<map van de exe>/TeknoParrot/<LETTER>/); RavenGun doet dat nog niet." % tp)

    log = os.path.join(d["dir"], "amdaemon.log")
    if os.path.isfile(log):
        # Alleen bestaan en grootte: het log kan een keychip-identificatie bevatten.
        d["log"] = {"pad": log, "grootte": _grootte(log), "gelezen": False}

    uit["daemon"] = d
    return d


def _hardware(uit, root):
    """pxGetHwInfo.ini: de hardware-eis, en of er in deze keten iets is dat hem afdwingt."""
    h = {"pad": None, "eisen": {}, "checker": False, "afgedwongen": False,
         "fouten": [], "waarschuwingen": []}
    p = os.path.join(root, "pxGetHwInfo.ini")
    if not os.path.isfile(p):
        h["fouten"].append("pxGetHwInfo.ini ontbreekt in %s" % root)
        uit["hardware"] = h
        return h
    h["pad"] = p
    c = lees_ini(p, commentaar_achter_de_waarde=True)
    for sleutel in ("platform-id", "sub-storage", "mem-size", "appdata-size",
                    "graphics-board-lane-width"):
        sectie = "System" if sleutel in ("platform-id", "sub-storage") else "Hardware"
        h["eisen"][sleutel] = zonder_commentaar(_get(c, sectie, sleutel))

    # Wie controleert dat? pxGetHwinfo.exe, en niets in de keten start dat programma.
    checker = os.path.join(root, "pxGetHwinfo.exe")
    h["checker"] = os.path.isfile(checker)
    genoemd = False
    for rel in KETEN:
        q = os.path.join(root, rel)
        if os.path.isfile(q) and "pxGetHwinfo" in lees_tekst(q):
            genoemd = True
    h["afgedwongen"] = bool(h["checker"] and genoemd)
    if h["checker"] and not genoemd:
        h["waarschuwingen"].append(
            "pxGetHwinfo.exe staat in de spelmap, maar geen enkel bestand van de startketen start het. "
            "De vier eisen hierboven worden in deze route dus niet gecontroleerd. (Onder TeknoParrot wordt "
            "game.bat ook niet gebruikt: het profiel start amdaemon.exe en de game-exe rechtstreeks.)")
    uit["hardware"] = h
    return h


def _view(uit, game, view):
    """Is er een view, en is die compleet genoeg? De views zijn symlink-boerderijen naar de NAS."""
    v = {"pad": view or view_dir(game), "bestaat": False, "koppelingen": 0, "kapot": [],
         "alles_binnen_de_view": True, "fouten": [], "waarschuwingen": []}
    pad = v["pad"]
    v["bestaat"] = os.path.isdir(pad)
    if not v["bestaat"]:
        uit["view"] = v
        return v
    basis = os.path.realpath(pad)
    naar_buiten = []
    for naam in sorted(os.listdir(pad)):
        p = os.path.join(pad, naam)
        if os.path.islink(p):
            v["koppelingen"] += 1
            if not os.path.exists(p):
                v["kapot"].append(naam)
            if not _veilig_binnen(basis, p):
                naar_buiten.append(naam)
    if naar_buiten:
        v["alles_binnen_de_view"] = False
    if v["kapot"]:
        v["fouten"].append("kapotte koppelingen in de view: %s" % ", ".join(v["kapot"][:5]))
    if naar_buiten:
        # Dit is geen fout van de laag maar wel de reden dat een drop-in met To = . hier niet werkt.
        v["waarschuwingen"].append(
            "%d koppeling(en) in deze view wijzen via de NAS naar buiten de view (o.a. %s). Een drop-in "
            "met To = . komt daardoor op de NAS terecht; de launcher hoort dat te weigeren."
            % (len(naar_buiten), ", ".join(naar_buiten[:3])))
    uit["view"] = v
    return v


# --------------------------------------------------------------------------- het startplan


def _plan(uit, root, game):
    """De twee programma's zoals de game zelf ze start, plus wat TeknoParrot doet.

    Dit is het recept voor wie de twee-programma-start in de lader bouwt; het script voert niets uit.
    """
    pr = uit.get("profiel") or {}
    d = uit.get("daemon") or {}
    k = (uit.get("keten") or {}).get("waarden", {})
    p = {"programmas": [], "onbekend": [], "teknoParrot_route": {}}

    daemon_exe = d.get("exe")
    game_exe = os.path.join(root, pr.get("identify_path", "").replace("\\", "/")) if pr.get("identify_path") else None
    if game_exe and not os.path.isfile(game_exe):
        game_exe = None

    # De werkmap die de .bat-keten opbouwt: `pushd ..\Hodzero` vanuit App/Startup -> App/Hodzero.
    werkmap = os.path.join(root, "App/Hodzero")
    if not os.path.isdir(werkmap):
        werkmap = None

    if daemon_exe:
        # De logbestemming uit StartApp.bat: COMMON_LOG_DIR is APP_LOG_DIR + \YYYYMMDD_hhmmss als de
        # keten die gezet heeft (99_StartApp.bat doet dat), en anders de map van de daemon zelf.
        app_log = k.get("APP_LOG_DIR") or ""
        logpad = (app_log.rstrip("\\/") + "\\<YYYYMMDD_hhmmss>\\amdaemon.log") if app_log \
            else os.path.join(d.get("dir") or "", "amdaemon.log")
        p["programmas"].append({
            "rol": "daemon",
            "exe": daemon_exe,
            "argumenten": ["-c", os.path.join(d.get("dir") or "", "config.json"), "-l", logpad],
            "werkmap": werkmap,
            "wachttijd_na_seconden": int(k.get("DAEMON_WAIT_SECONDS") or 5),
            "herkomst": "App/Hodzero/StartApp.bat: %s" % (k.get("daemon_opdracht") or ""),
            "let_op": "De logbestemming volgt COMMON_LOG_DIR: 99_StartApp.bat zet APP_LOG_DIR=%s, dus in "
                      "de .bat-route gaat het log naar %s en niet naar de map van de daemon." % (
                          app_log or "(leeg)", logpad),
        })
    if game_exe:
        p["programmas"].append({
            "rol": "game",
            "exe": game_exe,
            "argumenten": ['-launch="cabinet"'],
            "werkmap": werkmap,
            "wachttijd_na_seconden": 0,
            "herkomst": "App/Hodzero/StartApp.bat: %s (DETAIL_LAUNCH_MODE=cabinet, want 99_StartApp.bat "
                        "geeft 'target' mee en er is geen Dev\\AsTarget of Dev\\AsMiniCabinet)"
                        % (k.get("game_argument") or ""),
        })
    if not werkmap:
        p["onbekend"].append("de werkmap App/Hodzero ontbreekt in deze map; de .bat-keten zet hem met pushd")

    # Wat TeknoParrot zelf doet: het profiel start beide exe's kaal.
    p["teknoParrot_route"] = {
        "profiel": "GameProfiles/HOTDSD.xml",
        "ExecutableName2": pr.get("executable2"),
        "HasTwoExecutables": pr.get("twee_exe"),
        "LaunchSecondExecutableFirst": pr.get("daemon_eerst"),
        "ExtraParameters": pr.get("extra_parameters") or "(leeg)",
        "let_op": "Het profiel heeft ExtraParameters leeg: TeknoParrot start amdaemon.exe ZONDER -c en "
                  "zonder -l, dus zonder config.json. De daemon neemt dan zijn eigen standaardwaarden en "
                  "komt toch tot 'Process state: Advertise' (gemeten; zie findings/HOTDScarletDawn.md).",
    }
    uit["plan"] = p
    return p


# --------------------------------------------------------------------------- blokkades


def blokkades(uit):
    """Wat deze laag tegenhoudt, met de reden. Dit is het eerlijke deel: geen groen licht zonder route."""
    b = []

    # 1. De motor kan geen twee programma's starten.
    b.append({
        "naam": "twee-programma-start",
        "soort": "lader-werk",
        "wat": "de launcher start alleen profielen met één exe",
        "bewijs": "scripts/launch-direct.py: `if truthy(p.get(\"HasTwoExecutables\", \"false\")): "
                  "raise Unsupported(\"profielen met twee exe's worden niet direct gestart\")`; "
                  "gemeten: `python3 scripts/launch-direct.py HOTDSD --plan` geeft exitcode 4",
        "nodig": "de lader moet twee processen in volgorde starten (daemon eerst), met de wachttijd uit "
                 "StartApp.bat (DAEMON_WAIT_SECONDS=5) en met dezelfde lader en kern voor beide processen. "
                 "Dat is lader-werk; het recept staat in het veld 'plan'.",
    })

    # 2. De schijfletters die de game zoekt.
    d = uit.get("daemon") or {}
    mounts = d.get("mounts") or {}
    ontbreekt = [l for l, w in mounts.items() if not w.get("aanwezig")]
    b.append({
        "naam": "schijfletters",
        "soort": "lader-werk",
        "wat": "de game zoekt AMFS, D en Y; die letters moeten ergens heen wijzen",
        "bewijs": "de spelmap heeft een TeknoParrot-map met %s. amdaemon leest EN schrijft de mountpunten "
                  "in het register (System\\SEGA\\SystemProperty\\mount; hij importeert RegSetValueExW en "
                  "RegCreateKeyExW, en de strings amsSystemPropertySetMountAmfs, "
                  "amsSystemPropertySetMountAppdata en amsSystemPropertyMountInit staan erin). In geen "
                  "enkele Wine-prefix op deze machine staat een dosdevices met x:, y: of d:, dus het is "
                  "geen Wine-schijfmapping." % (
                      "AMFS/, D/ en Y/" if not ontbreekt else "deze ontbreken: " + ", ".join(ontbreekt)),
        "nodig": "een omleiding van X:\\, Y:\\, D:\\ en de AMFS-letter naar een map. Onder TeknoParrot doet "
                 "de kern dat (volgens het TeknoParrot-team door CreateFile te onderscheppen en elke letter "
                 "naar <map van de exe>/TeknoParrot/<LETTER>/ te leiden; dat de map daar staat en niet in de "
                 "werkmap past daarbij). RavenGun heeft die omleiding niet.",
    })

    # 3. De koppeling tussen game en daemon.
    b.append({
        "naam": "daemon-koppeling",
        "soort": "engine-werk",
        "wat": "de game praat met amdaemon over diens eigen IPC, niet over een poort",
        "bewijs": "amdaemon.exe bevat de naamruimten amdaemon::ipc:: (369 namen) en amdaemon::server:: "
                  "(189 namen), met amdaemon::ipc::SharedMemory, DaemonSharedPart, SharedDataList en "
                  "makeDaemonShared, plus de mutexnamen ALIVEHANDLEMUTEX/LOGVALUEMUTEX/DBMUTEX. Geen "
                  "enkele \"pipe\"- of \"40100\"-string en geen pcpa*-naam, terwijl de Ring-titels die "
                  "poorten wél gebruiken.",
        "nodig": "een eigen proces dat dezelfde gedeelde-memory-indeling en dezelfde mutexen levert, of "
                 "amdaemon zelf laten draaien. Dat is engine-werk.",
    })

    # 4. De dongle/keychip die de daemon zelf controleert.
    b.append({
        "naam": "keychip-in-de-daemon",
        "soort": "engine-werk",
        "wat": "amdaemon controleert zelf een keychip via een driver",
        "bewijs": "amdaemon.exe: am::dongle::Manager (libamw, amw_dongle_manager.cpp), amDongleOpen, "
                  "amDongleSetupKeychip, amDongleBillingGetKeychipId, amDongleGetGameId/ModelType/Region/"
                  "SystemFlag, het apparaatpad \\??\\FddDriver, en de foutregels \"Keychip Not Found\", "
                  "\"Keychip Access Failed\", \"Retry Keychip Access.\"",
        "nodig": "een antwoord op amDongle* binnen het daemonproces. Een drop-in die in de view gezet "
                 "wordt kan dat niet: de vrager is de daemon, niet de game.",
    })

    # 5. Het USB-IO-bord dat de daemonconfig eist.
    io = (uit.get("daemon") or {}).get("config_waarden", {})
    if io.get("usb_io_enable"):
        b.append({
            "naam": "usb-io-bord",
            "soort": "engine-werk",
            "wat": "config.json eist een USB-IO-bord met vaste functies",
            "bewijs": "AMDaemon/config.json: usb_io.enable = true, node[0].required_function = %s"
                      % ", ".join(io.get("usb_io_functies") or []),
            "nodig": "de daemon moet dat bord vinden (hij importeert hid.dll, setupapi.dll en cfgmgr32.dll "
                     "en somt HID-apparaten op). Zonder bord geen invoer en geen munt.",
        })

    return b


def dropin_antwoord(uit):
    """Is er iets te plaatsen in patches/<Game>/dropin/? Dat is sinds docs/geen-dongle-in-de-loader.md
    de enige vraag die overblijft voor het per-game-model."""
    b = blokkades(uit)
    lader = [x["naam"] for x in b if x["soort"] == "lader-werk"]
    engine = [x["naam"] for x in b if x["soort"] == "engine-werk"]
    return {
        "nodig": False,
        "wat": "",
        "waarom": (
            "Er is niets om te plaatsen. De game start niet zonder een tweede programma (amdaemon.exe) en "
            "de launcher start er maar één: dat is lader-werk (%s). En zelfs met twee processen moet er "
            "iets zijn dat de daemon zelf is: de game praat met hem over zijn eigen gedeelde geheugen en "
            "mutexen, hij controleert een keychip via \\??\\FddDriver, en hij eist een USB-IO-bord. Dat is "
            "engine-werk (%s). Een drop-in is een bestand in de view plus een omgevingsvariabele; daarmee "
            "is geen van beide op te lossen." % (", ".join(lader), ", ".join(engine))),
        "wat_een_dropin_wel_zou_kunnen": (
            "Zodra er een eigen daemonvervanger is, hoort die hier: "
            "patches/HOTDSD/dropin/amdaemon/ met de bibliotheek en zijn gegevens, en een sectie in "
            "dropin.ini die de weg geeft via Env (docs/geen-dongle-in-de-loader.md)."),
    }


# --------------------------------------------------------------------------- status


def status(game=None, view=None, game_dir=None):
    """De volledige status van de amdaemon-laag van een game."""
    uit = {"ok": False, "startbaar": False, "game": game, "verdict": "",
           "errors": [], "waarschuwingen": [], "blokkades": [], "message": ""}

    if not game:
        uit["message"] = "geef een game op: --game <Game>"
        uit["errors"].append("geen --game")
        return uit

    gm = game_map(game)
    uit["patches_dir"] = gm
    if not gm:
        uit["errors"].append("geen patches/%s/ gevonden (gezocht in: %s)" % (game, ", ".join(patches_dirs())))
        uit["message"] = "'%s' heeft geen map in patches/" % game
        return uit

    root, herkomst = game_root(game, view=view, override=game_dir)
    uit["game_root"] = root
    uit["game_root_herkomst"] = herkomst
    if not root or not os.path.isdir(root):
        uit["errors"].append("de spelmap van '%s' is niet gevonden (gebruik --game-dir of --view)" % game)
        uit["message"] = "spelmap van '%s' onbekend" % game
        return uit

    _profiel(uit, game)
    _keten(uit, root)
    _daemon(uit, root, game)
    _hardware(uit, root)
    _view(uit, game, view)
    _plan(uit, root, game)

    for deel in ("profiel", "keten", "daemon", "hardware"):
        uit["errors"].extend((uit.get(deel) or {}).get("fouten", []))
    for deel in ("daemon", "hardware", "view"):
        uit["waarschuwingen"].extend((uit.get(deel) or {}).get("waarschuwingen", []))

    uit["blokkades"] = blokkades(uit)
    uit["dropin"] = dropin_antwoord(uit)
    uit["startbaar"] = not uit["errors"] and not uit["blokkades"]
    uit["ok"] = uit["startbaar"]
    uit["verdict"] = ("startbaar" if uit["startbaar"]
                      else ("niet in orde" if uit["errors"] else "dit vraagt lader- en engine-werk"))

    d = uit.get("daemon") or {}
    k = (uit.get("keten") or {}).get("waarden", {})
    if uit["errors"]:
        uit["message"] = ("de amdaemon-laag van '%s' is niet in orde: %s"
                          % (game, "; ".join(uit["errors"][:3])))
    elif uit["blokkades"]:
        uit["message"] = (
            "amdaemon-laag van '%s': de beschrijving is compleet (daemon %s van %s bytes, twee programma's "
            "met de daemon eerst, wachttijd %s s, appdata %s, game-id %s), maar er is nog geen startroute. "
            "Blokkerend: %s. Er valt niets in patches/%s/dropin/ te plaatsen: de startketen zelf moet "
            "nagemaakt worden en dat is lader-werk. Zie docs/amdaemon-layer.md."
            % (game,
               os.path.basename(d.get("exe") or "amdaemon.exe"),
               d.get("exe_grootte") or "?",
               k.get("DAEMON_WAIT_SECONDS") or "5",
               k.get("APP_DIR") or "Y:\\<GAME_ID>",
               k.get("GAME_ID") or "?",
               ", ".join(b["naam"] for b in uit["blokkades"]),
               game))
    else:
        uit["message"] = "amdaemon-laag van '%s' is in orde en startbaar." % game
    return uit


def main(argv):
    game = None
    view = None
    game_dir = None
    als_json = False
    uitgebreid = False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--json":
            als_json = True
        elif a == "--verbose":
            uitgebreid = True
        elif a == "--game" and i + 1 < len(argv):
            game = argv[i + 1]; i += 1
        elif a == "--view" and i + 1 < len(argv):
            view = argv[i + 1]; i += 1
        elif a == "--game-dir" and i + 1 < len(argv):
            game_dir = argv[i + 1]; i += 1
        elif a in ("-h", "--help"):
            print(__doc__); return 0
        else:
            print("onbekende optie: %s" % a, file=sys.stderr); return 2
        i += 1

    st = status(game, view=view, game_dir=game_dir)
    if als_json:
        print(json.dumps(st, ensure_ascii=False, indent=1))
    else:
        print(st["message"])
        if uitgebreid:
            for e in st["errors"]:
                print("  fout     : %s" % e)
            for w in st["waarschuwingen"]:
                print("  let op   : %s" % w)
            for b in st["blokkades"]:
                print("  blokkade : %-22s %-12s %s" % (b["naam"], b["soort"], b["wat"]))
            for p in (st.get("plan") or {}).get("programmas", []):
                print("  plan     : %s -> %s %s (werkmap %s)"
                      % (p["rol"], p["exe"], " ".join(p["argumenten"]), p["werkmap"]))
        else:
            for e in st["errors"]:
                print("  - %s" % e)
            for b in st["blokkades"]:
                print("  - blokkade: %s" % b["naam"])
    return 0 if st["ok"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

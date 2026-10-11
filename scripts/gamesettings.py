#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Gedeelde lezer van de instellingen en patches per game (docs/per-game-patches.md). Alleen de standaardbibliotheek.

Drie lagen, per sleutel samengevoegd (een latere laag wint; een lege waarde haalt de sleutel weg):
    <patches>/_defaults/<Type>.ini      standaard per soort emulator (TeknoParrot, OpenParrot, ElfLdr2, Unity, ...)
    <patches>/<Game>/game.ini           meegeleverd (repo: patches/, installatie: <datadir>/patches/)
    <gebruiker>/<Game>/game.ini         gebruikerslaag, $XDG_DATA_HOME/ravengun/patches (standaard ~/.local/share/ravengun/patches)
Patches (patches.ini, secties "[Patch: <naam>]") volgen dezelfde lagen, met een extra laagje onderaan:
    <patches>/_defaults/<Type>.patches.ini   gedeelde definities (bv. een lader-patch voor alle games van dit type; Enabled = false)
    <patches>/<Game>/patches.ini             meegeleverd: eigen patches, of alleen "Enabled = true" bij een gedeelde patch
    <gebruiker>/<Game>/patches.ini           de gebruiker zet aan/uit (alleen "Enabled = ...") of voegt een eigen patch toe

Bestandsformaat: INI. Sectienamen en sleutels hoofdlettergevoelig en blijven zoals ze staan. Commentaar: een regel die met # of ;
begint, of een ';' na witruimte achter een waarde ("Sleutel = waarde ; uitleg"). Geen waarden over meer regels; regels beginnen
op kolom 0 (voorloop-witruimte wordt genegeerd). De C++-lezer (src/ravengun/gamesettings/) volgt dezelfde regels; beide worden
met tests/data/gamesettings-cases.ini gecontroleerd.

Gebruik (voor scripts en gebruikers):
    gamesettings.py list                               games met een map in de meegeleverde of de gebruikerslaag
    gamesettings.py show PROFIEL [--json]              samengevoegde instellingen en patches
    gamesettings.py get PROFIEL Sectie.Sleutel [--default X]
    gamesettings.py env PROFIEL [--tp DIR]             de omgevingsvariabelen (K=V per regel) die de instellingen opleveren
    gamesettings.py input PROFIEL route|layout|board|boards|com_port|hid_vidpid|source|launch   voor play-halo.sh
    gamesettings.py has-profile PROFIEL                exit 0 als game.ini een eigen profiel heeft (docs/per-game-patches.md, "Het profiel van een game")
    gamesettings.py comms PROFIEL                      procesnamen van de game (voor play-halo.sh)
    gamesettings.py write-xml PROFIEL                  UserProfiles/<P>.xml uit het eigen profiel (alleen voor een start via TeknoParrotUi)
    gamesettings.py dirs                               meegeleverde map en gebruikersmap
Opties: --patches DIR (meegeleverde map; ook $RAVENGUN_PATCHES_DIR), --user DIR (gebruikersmap; ook $RAVENGUN_USER_PATCHES_DIR).
"""
import configparser
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # python3 -I neemt de scriptmap niet mee (ownprofile, tpxml)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOME = os.environ.get("HOME", "/home/deck")
TPUI_DATA = os.path.join(HOME, ".local/share/TeknoParrotUI")
PREFIXES = os.path.join(TPUI_DATA, "prefixes")
SHARED_PREFIX = os.path.join(PREFIXES, "shared", "wine")
DEFAULT_TP = os.path.join(HOME, ".local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64")

ROUTES = ("evdev", "tp-shm", "unity-tcp", "hid-board", "jvs")
LAUNCH_MODES = {"direct": "direct", "tpui": "teknoparrot-ui", "teknoparrot-ui": "teknoparrot-ui"}
PATCH_PREFIX = "Patch:"
# Categorieen (docs/per-game-patches.md, "Categorie en beleid"): elke patch heeft er precies een, uit deze vaste lijst. De laatste vijf
# horen bij de afhandeling per game in de map van de game zelf (dongle, regio, keychip, integriteit, activering; docs/dropins.md en
# docs/community-model.md): een dongle- of regiopatch is een gewone patch zoals elke andere.
PATCH_CATEGORIES = ("graphics", "input", "timing", "audio", "paths", "crash", "network", "loader", "dongle", "region", "keychip", "integrity", "activation")
PATCH_KINDS = ("file", "registry", "memory")
TRUE_WORDS = ("true", "1", "yes", "on")
FALSE_WORDS = ("false", "0", "no", "off")


# ---------------------------------------------------------------------------------------------------------------------
# Mappen

def shipped_dir():
    """De meegeleverde map: $RAVENGUN_PATCHES_DIR, anders <repo>/patches.

    De patches staan in hun EIGEN repo (ravengun-patches), niet in die van de loader: het is
    gebruikersinhoud en de loader hoort schoon te zijn. Staat die repo niet naast de loader, dan
    bestaat deze map niet en leest de loader alleen de gebruikerslaag -- dat is goed."""
    return os.environ.get("RAVENGUN_PATCHES_DIR") or os.path.join(ROOT, "patches")


def user_dir():
    """De gebruikersmap: $RAVENGUN_USER_PATCHES_DIR, anders $XDG_DATA_HOME/ravengun/patches (standaard ~/.local/share/...)."""
    d = os.environ.get("RAVENGUN_USER_PATCHES_DIR")
    if d:
        return d
    x = os.environ.get("XDG_DATA_HOME", "")
    base = x if x.startswith("/") else os.path.join(os.environ.get("HOME", HOME), ".local/share")
    return os.path.join(base, "ravengun", "patches")


# ---------------------------------------------------------------------------------------------------------------------
# Lezen en samenvoegen

def parse_ini(text):
    """INI-tekst -> ({sectie: {sleutel: waarde}} in volgorde van voorkomen, [waarschuwingen])."""
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    # Een sleutel vóór de eerste sectie hoort nergens bij: eerst in een wegwerpsectie, zodat de rest gewoon wordt gelezen.
    text = "[\0orphan]\n" + "\n".join(line.lstrip() for line in text.split("\n"))
    cp = configparser.RawConfigParser(delimiters=("=",), comment_prefixes=("#", ";"), inline_comment_prefixes=(";",),
                                      strict=False, interpolation=None, default_section="\0default",
                                      empty_lines_in_values=False)
    cp.optionxform = str
    warn = []
    try:
        cp.read_string(text)
    except configparser.Error as e:   # de rest is wel ingelezen; regels met een fout vervallen
        warn.append(str(e).replace("\n", " "))
    out = {}
    for sec in cp.sections():
        if sec == "\0orphan":
            if cp.items(sec):
                warn.append("sleutel buiten een sectie genegeerd")
            continue
        out[sec] = {k: (v or "") for k, v in cp.items(sec)}
    return out, warn


def read_ini(path):
    try:
        with open(path, encoding="utf-8-sig") as f:
            return parse_ini(f.read())
    except FileNotFoundError:
        return {}, []
    except OSError as e:
        return {}, ["%s: %s" % (path, e)]


def merge_into(base, over):
    """Voegt `over` per sleutel in `base` samen; een lege waarde haalt de sleutel (en een lege sectie) weg."""
    for sec, kv in over.items():
        for k, v in kv.items():
            if v == "":
                if sec in base:
                    base[sec].pop(k, None)
            else:
                base.setdefault(sec, {})[k] = v
    return base


def truthy(v):
    return str(v).strip().lower() in TRUE_WORDS


def falsy(v):
    return str(v).strip().lower() in FALSE_WORDS


class Patch:
    """Een benoemde patch uit patches.ini, na samenvoegen van de lagen."""

    def __init__(self, name, fields):
        self.name = name
        self.fields = dict(fields)

    @property
    def enabled(self):
        return truthy(self.fields.get("Enabled", "false"))

    def get(self, key, default=""):
        return self.fields.get(key, default)

    @property
    def kind(self):
        return self.fields.get("Kind", "").strip().lower()

    @property
    def reason(self):
        return self.fields.get("Reason", "")

    @property
    def category(self):
        return self.fields.get("Category", "").strip().lower()

    @property
    def version(self):
        return self.fields.get("Version", "").strip()

    @property
    def applied_by(self):
        """'core' = de OpenParrot-kern past dit zelf al toe; RavenGun mag het niet nog eens doen (alleen documentatie)."""
        return self.fields.get("AppliedBy", "").strip().lower()

    def validate(self):
        """'' als de patch geldig is, anders een duidelijke melding (Nederlands). Elke laag telt mee: de controle gaat over de
        samengevoegde patch, dus een gebruikerslaag met een onbekende Category wordt net zo afgewezen."""
        if not self.kind:
            return "patch '%s' heeft geen definitie (Kind ontbreekt; definieer hem in patches.ini of in _defaults/<Type>.patches.ini)" % self.name
        if self.kind not in PATCH_KINDS:
            return "patch '%s': onbekend Kind '%s' (toegestaan: %s)" % (self.name, self.kind, ", ".join(PATCH_KINDS))
        if not self.category:
            return "patch '%s': Category ontbreekt (toegestaan: %s)" % (self.name, ", ".join(PATCH_CATEGORIES))
        if self.category not in PATCH_CATEGORIES:
            return "patch '%s': onbekende Category '%s' (toegestaan: %s)" % (self.name, self.fields.get("Category", ""), ", ".join(PATCH_CATEGORIES))
        return ""

    def to_dict(self):
        d = {"name": self.name, "enabled": self.enabled}
        d.update(self.fields)
        d["name"], d["enabled"] = self.name, self.enabled
        return d


class Game:
    """De samengevoegde instellingen en patches van een game."""

    def __init__(self, profile):
        self.profile = profile
        self.type = ""
        self.settings = {}       # {sectie: {sleutel: waarde}}
        self.patches = []        # [Patch], in volgorde van voorkomen (alleen geldige patches)
        self.patch_errors = []   # [{"name", "enabled", "message"}]: afgewezen patches (ontbrekende of onbekende Category, geen Kind)
        self.sources = []        # bestanden die zijn gelezen (voor weergave en logboek)
        self.warnings = []
        self.exists = False      # er is een game.ini in de meegeleverde of de gebruikerslaag (of een .env-terugval)

    def get(self, section, key, default=None):
        return self.settings.get(section, {}).get(key, default)

    def section(self, name):
        return dict(self.settings.get(name, {}))

    @property
    def launch_mode(self):
        """direct | teknoparrot-ui | "" (niet gezet)."""
        return LAUNCH_MODES.get(self.get("Launch", "Mode", "").strip().lower(), "")

    def input_route(self):
        """[Input] als dict met controle zoals de engine (docs/per-game-patches.md); (waarden, fout) met fout "" als het klopt."""
        i = self.section("Input")
        r = i.get("Route", "evdev") or "evdev"
        if r not in ROUTES:
            return i, "ongeldige Route %r" % r
        if r == "tp-shm" and not i.get("Layout"):
            return i, "route tp-shm vraagt een Layout"
        if r == "hid-board" and not i.get("Board"):
            return i, "route hid-board vraagt een Board (bordnaam)"
        if r == "jvs" and not i.get("Board") and not i.get("Boards"):
            return i, "route jvs vraagt een Board of Boards"
        return i, ""

    def to_dict(self):
        return {"profile": self.profile, "type": self.type, "settings": self.settings,
                "patches": [p.to_dict() for p in self.patches], "patch_errors": self.patch_errors, "sources": self.sources,
                "warnings": self.warnings}


def resolve_input(profile, tp=None, shipped=None, user=None, warn=None):
    """[Input] van een game, gecontroleerd zoals de engine: klopt het samengevoegde resultaat niet, dan telt de gebruikerslaag niet
    mee (melding via warn); klopt ook de meegeleverde laag niet, dan evdev. Geeft ({sleutel: waarde}, bron) met bron user|shipped."""
    warn = warn or (lambda m: None)
    g = load_for_profile(profile, tp, shipped=shipped, user=user)
    i, err = g.input_route()
    if not err:
        mine, _ = read_ini(os.path.join(user or user_dir(), profile, "game.ini"))
        return i, "user" if mine.get("Input", {}).get("Route") else "shipped"
    warn("%s: %s; de gebruikerslaag wordt genegeerd" % (profile, err))
    g2 = load_for_profile(profile, tp, shipped=shipped, user=os.path.join(os.devnull, "geen"))
    i, err = g2.input_route()
    if not err:
        return i, "shipped"
    warn("%s: %s; evdev" % (profile, err))
    return {"Route": "evdev"}, "shipped"


def hid_vidpid(board, root=None):
    """VVVV:PPPP van een bord uit packaging/hid-boards.json, of ''."""
    try:
        with open(os.path.join(root or ROOT, "packaging", "hid-boards.json")) as f:
            b = json.load(f)["boards"][board]
        v = lambda x: int(x, 0) if isinstance(x, str) else int(x)
        return "%04X:%04X" % (v(b["vendor"]), v(b["product"]))
    except Exception:
        return ""


def list_games(shipped=None, user=None):
    """Namen van de games met een map in een van beide lagen (geen map die met _ begint), gesorteerd."""
    names = set()
    for d in (shipped or shipped_dir(), user or user_dir()):
        try:
            for e in os.listdir(d):
                if not e.startswith((".", "_")) and os.path.isdir(os.path.join(d, e)):
                    names.add(e)
        except OSError:
            pass
    return sorted(names, key=str.lower)


def _patches_from(sections):
    out = {}
    for sec, kv in sections.items():
        if sec.startswith(PATCH_PREFIX):
            out[sec[len(PATCH_PREFIX):].strip()] = kv
    return out


def load_game(profile, type_hint="", shipped=None, user=None, legacy=None):
    """Leest de lagen van een game. type_hint: het soort als geen game.ini het noemt (bv. EmulatorType uit het profiel).
    legacy: (instellingen, {patchnaam: velden}) uit een oud .env-bestand (env_to_settings); laag onder het meegeleverde game.ini."""
    shipped, user = shipped or shipped_dir(), user or user_dir()
    g = Game(profile)
    layers = []   # (pad of naam, sections) van de game zelf, laag voor laag
    pl = []       # idem voor patches.ini
    for d in (shipped, user):
        gp = os.path.join(d, profile, "game.ini")
        s, w = read_ini(gp)
        if os.path.isfile(gp):
            g.exists = True
            g.sources.append(gp)
        g.warnings += ["%s: %s" % (gp, x) for x in w]
        layers.append(s)
        pp = os.path.join(d, profile, "patches.ini")
        s, w = read_ini(pp)
        if os.path.isfile(pp):
            g.sources.append(pp)
        g.warnings += ["%s: %s" % (pp, x) for x in w]
        pl.append(_patches_from(s))
    legacy_settings, legacy_patches = legacy if legacy else ({}, {})
    if legacy_settings or legacy_patches:
        g.exists = True
    # Het soort volgt uit de game zelf (de gebruikerslaag gaat voor), anders de hint.
    t = ""
    for s in [legacy_settings] + layers:
        t = s.get("Game", {}).get("Type", t) or t
    t = t or type_hint
    g.type = t
    merged = {}
    patches = {}
    order = []
    if t:
        dp = os.path.join(shipped, "_defaults", t + ".ini")
        s, w = read_ini(dp)
        if os.path.isfile(dp):
            g.sources.append(dp)
        g.warnings += ["%s: %s" % (dp, x) for x in w]
        merge_into(merged, s)
        dpp = os.path.join(shipped, "_defaults", t + ".patches.ini")
        s, w = read_ini(dpp)
        if os.path.isfile(dpp):
            g.sources.append(dpp)
        g.warnings += ["%s: %s" % (dpp, x) for x in w]
        shared = _patches_from(s)
    else:
        shared = {}
    merge_into(merged, legacy_settings)
    for s in layers:
        merge_into(merged, s)
    for layer in [shared, legacy_patches] + pl:
        for name, kv in layer.items():
            if name not in patches:
                order.append(name)
                patches[name] = {}
            for k, v in kv.items():
                if v == "":
                    patches[name].pop(k, None)
                else:
                    patches[name][k] = v
    for name in order:
        if not patches[name]:
            continue
        patch = Patch(name, patches[name])
        err = patch.validate()
        if err:
            g.patch_errors.append({"name": name, "enabled": patch.enabled, "message": err})
            g.warnings.append(err)
        else:
            g.patches.append(patch)
    g.settings = {s: kv for s, kv in merged.items() if kv}
    return g


# ---------------------------------------------------------------------------------------------------------------------
# Omgevingsvariabelen <-> instellingen. De launchers lezen (nog) omgevingsvariabelen; dit is de enige plek waar de naam van een
# sleutel aan een variabele hangt. Alles wat hier niet in staat, staat in [Env] (naam = variabele).

# (sectie, sleutel, variabele, soort); soort: "" letterlijk, "bool1"/"boolT" = waar is "1" resp. "true", "path" = ~ uitklappen
ENV_MAP = [
    ("Loader", "RemoteThread", "TP_REMOTETHREAD", ""),
    ("Loader", "PostStartMs", "TP_POSTSTART_REMOTETHREAD_MS", ""),
    ("Loader", "EntryPointMs", "TP_ENTRYPOINT_REMOTETHREAD_MS", ""),
    ("Launch", "WorkingDirectory", "TP_GAME_WORKING_DIRECTORY", "workdir"),
    ("Wine", "Prefix", "RG_WINEPREFIX", "prefix"),
    ("Wine", "PrefixDirect", "TP_WINEPREFIX", "prefix"),
    ("Wine", "Binary", "TP_WINE", "path"),
    ("Wine", "DllOverrides", "WINEDLLOVERRIDES", ""),
    ("Wine", "Debug", "WINEDEBUG", ""),
    ("Wine", "GStreamerPlugins", "GST_PLUGIN_PATH", "path"),
    ("Display", "Gamescope", "TP_GAMESCOPE", "bool1"),
    ("Display", "GamescopePath", "TP_GAMESCOPE_PATH", "path"),
    ("Display", "GamescopeBackend", "TP_GAMESCOPE_BACKEND", ""),
    ("Display", "GamescopeResolution", "RG_GAMESCOPE_RES", ""),
    ("Input", "JvsTransport", "RG_JVS_TRANSPORT", ""),
    ("Input", "JvsPipeBoard", "RG_JVS_BOARD", "boolT"),
    ("Input", "JvsF1Once", "RG_JVS_F1_ONCE", "boolT"),
    ("Input", "JvsDebug", "RG_JVS_DEBUG", "boolT"),
    ("Input", "JvsIdent", "RG_JVS_IDENT", ""),
    ("Input", "JvsFeatures", "RG_JVS_FEATURES", ""),
]
ENV_BY_VAR = {m[2]: m for m in ENV_MAP}
# Variabele die alleen de patch "gedeelde lader-patch" aanzet (oude naam; alleen voor de .env-terugval en de migratie).
LEGACY_LOADER_FPFIX = "RG_LOADER_FPFIX"
FPFIX_PATCH = "OpenParrotLoader zonder CONTEXT_FLOATING_POINT"
# Variabelen die de launchers zelf zetten of die nooit in een game-bestand horen (geheimen en sessiegegevens).
NEVER_IN_FILES = ("TOKEN", "SECRET", "PASSWORD", "PASSWD", "APIKEY", "API_KEY", "SERIAL", "LICENSE", "LICENCE", "PATREON")


def is_secret_name(name):
    u = name.upper()
    return any(w in u for w in NEVER_IN_FILES)


def expand_home(p):
    if p == "~" or p.startswith("~/"):
        return os.path.join(os.environ.get("HOME", HOME), p[2:]) if p != "~" else os.environ.get("HOME", HOME)
    return p


def shrink_home(p):
    h = os.environ.get("HOME", HOME).rstrip("/")
    if p == h:
        return "~"
    return "~/" + p[len(h) + 1:] if p.startswith(h + "/") else p


def prefix_path(v):
    """Wine.Prefix: 'shared', een naam onder ~/.local/share/TeknoParrotUI/prefixes/, of een pad (~ mag)."""
    v = v.strip()
    if v == "shared":
        return SHARED_PREFIX
    if v.startswith(("/", "~")):
        return expand_home(v)
    return os.path.join(PREFIXES, v)


def prefix_value(path):
    """Omgekeerd van prefix_path: de kortste vorm die terug dezelfde padtekst geeft."""
    if path == SHARED_PREFIX:
        return "shared"
    if path.startswith(PREFIXES + "/") and "/" not in path[len(PREFIXES) + 1:] and path[len(PREFIXES) + 1:] != "shared":
        return path[len(PREFIXES) + 1:]
    return shrink_home(path)


def win_path(p):
    return "Z:" + p.replace("/", "\\")


def to_env(settings, gamedir=None):
    """De omgevingsvariabelen die de instellingen opleveren: eerst de bekende sleutels, dan [Env] (die wint)."""
    env = {}
    for sec, key, var, kind in ENV_MAP:
        v = settings.get(sec, {}).get(key)
        if v is None or v == "":
            continue
        if kind in ("bool1", "boolT"):
            t, f = ("1", "0") if kind == "bool1" else ("true", "false")
            v = t if truthy(v) else f if falsy(v) else v
        elif kind == "path":
            v = expand_home(v)
        elif kind == "prefix":
            v = prefix_path(v)
        elif kind == "workdir":
            # Windows-pad (Z:\...) of absoluut pad blijft staan; anders relatief aan de map van de game-exe.
            v = expand_home(v)
            if not (re.match(r"^[A-Za-z]:[\\/]", v) or v.startswith("/")):
                if not gamedir:
                    continue
                v = win_path(os.path.normpath(os.path.join(gamedir, v)))
            elif v.startswith("/"):
                v = win_path(v)
        env[var] = v
    for k, v in settings.get("Env", {}).items():
        env[k] = v
    return env


def env_to_settings(env, gamedir=None):
    """Omgekeerd van to_env, voor de migratie en de .env-terugval. Geeft ({sectie: {sleutel: waarde}}, {patchnaam: velden}).
    Variabelen met een geheim in de naam worden niet overgenomen (migratie: nooit geheimen in bestanden)."""
    s, patches = {}, {}
    for var, val in env.items():
        if var == LEGACY_LOADER_FPFIX:
            if truthy(val):
                patches[FPFIX_PATCH] = {"Enabled": "true"}
            continue
        if is_secret_name(var):
            continue
        m = ENV_BY_VAR.get(var)
        if not m:
            s.setdefault("Env", {})[var] = val
            continue
        sec, key, _, kind = m
        out = val
        if kind == "bool1":
            out = "true" if val == "1" else "false" if val == "0" else val
        elif kind == "boolT":
            out = "true" if val == "true" else "false" if val == "false" else val
        elif kind == "path":
            out = shrink_home(val)
        elif kind == "prefix":
            out = prefix_value(val)
        elif kind == "workdir":
            m2 = re.match(r"^Z:(.*)$", val)
            if gamedir and m2:
                posix = m2.group(1).replace("\\", "/")
                rel = os.path.relpath(posix, gamedir) if posix == gamedir or posix.startswith(gamedir.rstrip("/") + "/") else None
                if rel is not None and to_env({"Launch": {"WorkingDirectory": rel}}, gamedir).get(var) == val:
                    out = rel
            if out == val:
                out = shrink_home(val) if val.startswith("/") else val
        s.setdefault(sec, {})[key] = out
    return s, patches


def read_env_file(path):
    """Oud formaat UserProfiles/<P>.env: K=V per regel; # = commentaar, ook achter een waarde (zoals launch-direct.py deed)."""
    env = {}
    try:
        with open(path) as f:
            for line in f:
                line = line.split("#", 1)[0].strip()
                if "=" in line:
                    k, v = line.split("=", 1)
                    env[k.strip()] = v.strip()
    except FileNotFoundError:
        pass
    return env


def own_game_path(profile, shipped=None, user=None):
    """Het pad naar de game volgens het eigen profiel in game.ini (afgeleid uit de gamemappen, een view of een override), of ''."""
    import ownprofile as op
    g = load_game(profile, shipped=shipped, user=user)
    if not op.has_own_profile(g.settings):
        return ""
    return op.locate_game(profile, g.settings)[0]


def profile_gamedir(tp, profile):
    """De map van de game-exe: uit het eigen profiel in game.ini (afgeleid), anders uit UserProfiles/<P>.xml (GamePath). Alleen gelezen."""
    own = own_game_path(profile)
    if own:
        return os.path.dirname(own)
    try:
        with open(os.path.join(tp, "UserProfiles", profile + ".xml"), encoding="utf-8-sig") as f:
            m = re.search(r"<GamePath>(.*?)</GamePath>", f.read(), re.S)
        return os.path.dirname(m.group(1).strip()) if m else ""
    except OSError:
        return ""


def profile_emulator_type(tp, profile):
    try:
        with open(os.path.join(tp, "UserProfiles", profile + ".xml"), encoding="utf-8-sig") as f:
            m = re.search(r"<EmulatorType>(.*?)</EmulatorType>", f.read(), re.S)
        return m.group(1).strip() if m else ""
    except OSError:
        return ""


def load_for_profile(profile, tp=None, type_hint="", shipped=None, user=None, use_legacy=True, log=None):
    """load_game met de oude UserProfiles/<P>.env als laag onder game.ini (alleen voor één versie; docs/per-game-patches.md,
    "Overgang"). Met RAVENGUN_NO_LEGACY_ENV=1 wordt het .env-bestand niet gelezen. log(tekst) krijgt de melding "legacy .env"."""
    tp = tp or os.environ.get("RAVENGUN_TP_DIR", DEFAULT_TP)
    legacy = None
    if use_legacy and os.environ.get("RAVENGUN_NO_LEGACY_ENV") != "1":
        path = os.path.join(tp, "UserProfiles", profile + ".env")
        env = read_env_file(path)
        if env:
            legacy = env_to_settings(env, profile_gamedir(tp, profile))
            if log:
                log("legacy .env gelezen: %s (game.ini wint; migreer met scripts/migrate-game-settings.py)" % path)
    return load_game(profile, type_hint or profile_emulator_type(tp, profile), shipped, user, legacy)


# ---------------------------------------------------------------------------------------------------------------------
# Schrijven (gebruikerslaag: app, tp-add-game)

def dump_ini(sections, header=""):
    """Eenvoudige INI-tekst: sectie voor sectie, 'Sleutel = waarde'. Waarden met een nieuwe regel of ' ;' kan het formaat niet
    dragen (ValueError)."""
    lines = [("; " + h).rstrip() for h in header.splitlines()] if header else []
    for sec, kv in sections.items():
        if not kv:
            continue
        if lines:
            lines.append("")
        lines.append("[%s]" % sec)
        for k, v in kv.items():
            v = str(v)
            if "\n" in v or "\r" in v or re.search(r"\s;", v) or v != v.strip() or v.startswith(("#", ";")):
                raise ValueError("waarde van %s.%s past niet in het INI-formaat: %r" % (sec, k, v))
            lines.append("%s = %s" % (k, v) if v else "%s =" % k)
    return "\n".join(lines) + "\n"


def edit_ini_text(text, section, key, value, erase=False):
    """INI-tekst met één sleutel gezet of weggehaald; commentaar en volgorde blijven staan (zelfde regels als de C++-lezer)."""
    lines = text.replace("\r\n", "\n").split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    in_target, header, last_kv, key_line = False, -1, -1, -1
    for i, raw in enumerate(lines):
        t = raw.strip()
        if not t or t[0] in "#;":
            continue
        if t[0] == "[":
            close = t.rfind("]")
            in_target = close >= 2 and t[1:close] == section
            if in_target and header < 0:
                header = i
            continue
        if not in_target or "=" not in t:
            continue
        last_kv = i
        if key_line < 0 and t.split("=", 1)[0].strip() == key:
            key_line = i
    new = "%s = %s" % (key, value) if value else "%s =" % key
    if key_line >= 0:
        if erase:
            del lines[key_line]
        else:
            lines[key_line] = new
    elif not erase:
        if header >= 0:
            lines.insert((last_kv if last_kv >= 0 else header) + 1, new)
        else:
            if lines and lines[-1].strip():
                lines.append("")
            lines += ["[%s]" % section, new]
    return "\n".join(lines) + "\n"


def _check_value(sec, k, v):
    if "\n" in v or "\r" in v or re.search(r"\s;", v) or v != v.strip() or v.startswith(("#", ";")):
        raise ValueError("waarde van %s.%s past niet in het INI-formaat: %r" % (sec, k, v))


def write_user_settings(profile, updates, patch_updates=None, user=None):
    """Zet sleutels (en Enabled van patches) in de gebruikerslaag; andere sleutels, commentaar en volgorde blijven staan. updates:
    {sectie: {sleutel: waarde}}; een lege waarde schrijft 'Sleutel =' (zet de sleutel terug op 'niet gezet', ook tegenover een
    meegeleverde waarde)."""
    user = user or user_dir()
    d = os.path.join(user, profile)
    os.makedirs(d, exist_ok=True)
    for fname, items in (("game.ini", updates or {}), ("patches.ini", {PATCH_PREFIX + " " + n: kv for n, kv in (patch_updates or {}).items()})):
        if not items:
            continue
        p = os.path.join(d, fname)
        try:
            with open(p, encoding="utf-8") as f:
                text = f.read()
        except FileNotFoundError:
            text = ""
        for sec, kv in items.items():
            for k, v in kv.items():
                _check_value(sec, k, str(v))
                text = edit_ini_text(text, sec, k, str(v))
        _atomic_write(p, text)


def _atomic_write(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, path)


# ---------------------------------------------------------------------------------------------------------------------
# Opdrachtregel

def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="Instellingen en patches per game (docs/per-game-patches.md)")
    ap.add_argument("--patches")
    ap.add_argument("--user")
    ap.add_argument("--tp", default=None)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    sub.add_parser("dirs")
    p = sub.add_parser("has-profile", help="exit 0 als game.ini een eigen profiel heeft (UserProfiles-XML niet nodig)")
    p.add_argument("profile")
    p = sub.add_parser("comms", help="procesnamen (comm) van de game, gescheiden door |, voor play-halo.sh")
    p.add_argument("profile")
    p = sub.add_parser("write-xml", help="UserProfiles/<P>.xml uit het eigen profiel schrijven (voor een start via TeknoParrotUi)")
    p.add_argument("profile")
    p.add_argument("--dry-run", action="store_true")
    p = sub.add_parser("show")
    p.add_argument("profile")
    p.add_argument("--json", action="store_true")
    p = sub.add_parser("get")
    p.add_argument("profile")
    p.add_argument("key")
    p.add_argument("--default", default="")
    p = sub.add_parser("env")
    p.add_argument("profile")
    p = sub.add_parser("input", help="invoerroute voor de startscripts (play-halo.sh)")
    p.add_argument("profile")
    p.add_argument("field", choices=["route", "layout", "board", "boards", "com_port", "hid_vidpid", "source", "launch"])
    a = ap.parse_args(argv)
    shipped, user = a.patches or shipped_dir(), a.user or user_dir()
    if a.cmd == "dirs":
        print("shipped: %s\nuser: %s" % (shipped, user))
        return 0
    if a.cmd == "list":
        print("\n".join(list_games(shipped, user)))
        return 0
    if a.cmd == "input":
        i, src = resolve_input(a.profile, a.tp, shipped, user, warn=lambda m: print("gamesettings: " + m, file=sys.stderr))
        if a.field == "route":
            print(i.get("Route", "evdev"))
        elif a.field in ("layout", "board", "boards"):
            print(i.get(a.field.capitalize(), ""))
        elif a.field == "com_port":
            print(i.get("ComPort", "") or "COM4")
        elif a.field == "hid_vidpid":
            print(hid_vidpid(i.get("Board", "")))
        elif a.field == "source":
            print(src)
        else:
            g = load_for_profile(a.profile, a.tp, shipped=shipped, user=user)
            print(g.launch_mode or "teknoparrot-ui")
        return 0
    if a.cmd in ("has-profile", "comms", "write-xml"):
        import ownprofile as op
        g = load_game(a.profile, shipped=shipped, user=user)
        if not op.has_own_profile(g.settings):
            return 1 if a.cmd == "has-profile" else 0     # zonder eigen profiel verandert er niets: play-halo.sh valt terug op de XML
        if a.cmd == "has-profile":
            return 0
        game, how = op.locate_game(a.profile, g.settings)
        if a.cmd == "comms":
            names = []
            for v in (g.get("Game", "Executable", ""), g.get("Game", "Executable2", ""), game):
                for part in re.split(r"[;|]", v or ""):
                    n = part.replace("\\", "/").rsplit("/", 1)[-1].lower()[:15]
                    if n and n not in names:
                        names.append(n)
            print("|".join(names))
            return 0
        tp = a.tp or os.environ.get("RAVENGUN_TP_DIR", DEFAULT_TP)
        if not game:
            print("gamesettings: game %s niet gevonden in de gamemappen (voeg de map toe in de app, of zet [Game] GamePath in je eigen game.ini)"
                  % a.profile, file=sys.stderr)
            return 1
        import tpxml
        status, unknown, _b = tpxml.write_user_xml(tp, a.profile, g.settings, game, dry=a.dry_run)
        if unknown:
            print("gamesettings: velden die het sjabloon niet kent: " + ", ".join(unknown), file=sys.stderr)
        print("UserProfiles/%s.xml: %s" % (a.profile, status))
        return 0
    g = load_for_profile(a.profile, a.tp, shipped=shipped, user=user, log=lambda m: print(m, file=sys.stderr))
    for w in g.warnings:
        print("gamesettings: " + w, file=sys.stderr)
    if a.cmd == "show":
        if a.json:
            print(json.dumps(g.to_dict(), indent=1, ensure_ascii=False))
        else:
            print("# %s (type %s)" % (g.profile, g.type or "-"))
            for sec, kv in g.settings.items():
                print("[%s]" % sec)
                for k, v in kv.items():
                    print("%s = %s" % (k, v))
            for pt in g.patches:
                print("[Patch: %s] enabled=%s kind=%s" % (pt.name, str(pt.enabled).lower(), pt.kind))
        return 0
    if a.cmd == "get":
        sec, _, key = a.key.partition(".")
        v = g.get(sec, key)
        if sec == "Launch" and key == "Mode":
            v = g.launch_mode or None
        print(a.default if v is None else v)
        return 0
    if a.cmd == "env":
        tp = a.tp or os.environ.get("RAVENGUN_TP_DIR", DEFAULT_TP)
        for k, v in to_env(g.settings, profile_gamedir(tp, a.profile)).items():
            print("%s=%s" % (k, v))
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

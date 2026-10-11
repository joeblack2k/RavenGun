#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Eigen spelprofielen van RavenGun (docs/per-game-patches.md, "Het profiel van een game"): het profiel dat TeknoParrotUi in
UserProfiles/<P>.xml bewaart, als onderdeel van patches/<Game>/game.ini. Alleen de standaardbibliotheek.

Dolphin-model: het meegeleverde game.ini (repo) bevat wat bij de game hoort (uit het sjabloon GameProfiles/<P>.xml van de runtime);
de gebruikerslaag bevat alleen wat afwijkt. Het pad naar de game staat nergens: de bibliotheek herkent de game in de gamemappen
(Dolphin: ISOPath0..N) aan [Identify] en leidt het pad daaruit af. Alleen een handmatig gekozen afwijkend pad staat als
[Game] GamePath in de gebruikerslaag.

Dit bestand is de enige plek die weet hoe een profielveld van TeknoParrot (XML) heet en hoe het in game.ini heet (PROFILE_FIELDS), hoe
de ConfigValues erin staan, hoe de game wordt herkend en hoe een UserProfiles-XML (uitvoerformaat voor TeknoParrotUi) eruit wordt gemaakt.
"""
import hashlib
import json
import os
import re
import xml.etree.ElementTree as ET

import gamesettings as gs

# (sectie, sleutel in game.ini, veld in het profiel, soort). Soort: "" = tekst, "bool" = true/false, "runner" = WineRunnerPath
# (pad naar wine of de wrapper; in game.ini "wrapper" of een pad met ~).
PROFILE_FIELDS = [
    ("Game", "Executable", "ExecutableName", ""),
    ("Game", "Executable2", "ExecutableName2", ""),
    ("Game", "Type", "EmulatorType", ""),
    ("Game", "EmulationProfile", "EmulationProfile", ""),
    ("Game", "Is64Bit", "Is64Bit", "bool"),
    ("Game", "ProfileRevision", "GameProfileRevision", ""),
    ("Game", "Patreon", "Patreon", "bool"),
    ("Game", "MsysType", "msysType", ""),
    ("Game", "GameVersion", "GameVersion", ""),
    ("Game", "HasTwoExecutables", "HasTwoExecutables", "bool"),
    ("Game", "LaunchSecondExecutableFirst", "LaunchSecondExecutableFirst", "bool"),
    ("Launch", "UseRemoteThread", "UseRemoteThread", "bool"),
    ("Launch", "ExtraParameters", "ExtraParameters", ""),
    ("Launch", "CustomArguments", "CustomArguments", ""),
    ("Display", "GamescopeCompatibility", "GamescopeGameWindowCompatibility", ""),
    ("Wine", "PrefixMode", "WinePrefixMode", ""),
    ("Wine", "Runner", "WineRunnerPath", "runner"),
]
BY_FIELD = {f[2]: f for f in PROFILE_FIELDS}
CONFIG, CONFIG_TYPE, CONFIG_OPTIONS = "Config", "ConfigType", "ConfigOptions"
IDENTIFY = "Identify"
RUNNER_WRAPPER = "wrapper"
OPTION_SEP = "|"


# ---------------------------------------------------------------------------------------------------------------------
# Paden

def data_home():
    """$XDG_DATA_HOME/ravengun (standaard ~/.local/share/ravengun)."""
    x = os.environ.get("XDG_DATA_HOME", "")
    base = x if x.startswith("/") else os.path.join(os.environ.get("HOME", gs.HOME), ".local/share")
    return os.path.join(base, "ravengun")


def views_dir():
    return os.path.join(data_home(), "runtime", "views")


def wrapper_path():
    return os.path.join(data_home(), "wrappers", "wine-prefix")


def default_wine():
    return os.path.join(data_home(), "wine", "proton-11.0-2", "bin", "wine")


def library_file():
    """De bibliotheek van de app (gamemappen): $RAVENGUN_LIBRARY_FILE, anders $XDG_CONFIG_HOME/ravengun/library.json."""
    f = os.environ.get("RAVENGUN_LIBRARY_FILE")
    if f:
        return f
    x = os.environ.get("XDG_CONFIG_HOME", "")
    base = x if x.startswith("/") else os.path.join(os.environ.get("HOME", gs.HOME), ".config")
    return os.path.join(base, "ravengun", "library.json")


def library_folders(path=None):
    """De gamemappen uit library.json (de app bewaart ze daar; Dolphin: ISOPath0..N), met ~ uitgeklapt. Een ontbrekend of kapot bestand
    geeft een lege lijst."""
    try:
        with open(path or library_file(), encoding="utf-8") as f:
            raw = json.load(f).get("folders", [])
    except (OSError, ValueError, AttributeError):
        return []
    out = []
    for v in raw:
        p = v if isinstance(v, str) else (v.get("path", "") if isinstance(v, dict) else "")
        if p:
            out.append(os.path.normpath(gs.expand_home(p)))
    return out


# ---------------------------------------------------------------------------------------------------------------------
# TeknoParrot-profielen (XML) lezen

def read_scalars(root):
    return {c.tag: (c.text or "") for c in root if len(c) == 0}


def read_fields(root):
    out = []
    cv = root.find("ConfigValues")
    if cv is None:
        return out
    for fi in cv.findall("FieldInformation"):
        out.append({"category": fi.findtext("CategoryName") or "", "name": fi.findtext("FieldName") or "",
                    "value": fi.findtext("FieldValue") or "", "type": fi.findtext("FieldType") or "",
                    "options": [s.text or "" for s in fi.findall("FieldOptions/string")]})
    return out


def read_xml_profile(path):
    """(scalars, fields) van een profielbestand; None als het er niet is."""
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError):
        return None
    return read_scalars(root), read_fields(root)


def template_path(tp, name):
    return os.path.join(tp, "GameProfiles", name + ".xml")


def user_xml_path(tp, name):
    return os.path.join(tp, "UserProfiles", name + ".xml")


# ---------------------------------------------------------------------------------------------------------------------
# ConfigValues in game.ini: [Config] "Categorie/Veld = waarde", [ConfigType] "Categorie/Veld = Bool|Text|...", [ConfigOptions] "Categorie/Veld = a|b|c"
# De sleutel wordt bij de eerste '/' gesplitst (categorienamen bevatten er geen; veldnamen wel, bv. "Keyboard/Button Axis"). Een lege
# waarde is "niet gezet" en dus een leeg veld: dezelfde regel als overal in game.ini.

def field_key(f):
    return "%s/%s" % (f["category"], f["name"])


def split_key(key):
    cat, _, name = key.partition("/")
    return cat, name


def check_representable(sec, key, value):
    """Meldt ValueError als een sleutel of waarde niet in het INI-formaat past (gamesettings.dump_ini heeft dezelfde regels)."""
    if "=" in key or key != key.strip() or key[:1] in "#;[" or re.search(r"\s;", key) or "\n" in key:
        raise ValueError("sleutel %s.%s past niet in het INI-formaat" % (sec, key))
    gs._check_value(sec, key, value)


def config_sections(fields, with_values=True):
    """{sectie: {sleutel: waarde}} voor een lijst velden (volgorde blijft). Velden zonder waarde komen niet in [Config]."""
    cfg, typ, opts = {}, {}, {}
    for f in fields:
        k = field_key(f)
        check_representable(CONFIG_TYPE, k, f["type"] or "Text")
        typ[k] = f["type"] or "Text"
        if f["options"]:
            ov = OPTION_SEP.join(f["options"])
            if any(OPTION_SEP in o for o in f["options"]):
                raise ValueError("keuzelijst van %s bevat '%s'" % (k, OPTION_SEP))
            check_representable(CONFIG_OPTIONS, k, ov)
            opts[k] = ov
        if with_values and f["value"]:
            check_representable(CONFIG, k, f["value"])
            cfg[k] = f["value"]
    return {CONFIG: cfg, CONFIG_TYPE: typ, CONFIG_OPTIONS: opts}


def fields_from_settings(settings):
    """De velden (category, name, value, type, options) uit de samengevoegde instellingen, in de volgorde van [ConfigType]."""
    typ = settings.get(CONFIG_TYPE, {})
    cfg, opts = settings.get(CONFIG, {}), settings.get(CONFIG_OPTIONS, {})
    out = []
    for k, t in typ.items():
        cat, name = split_key(k)
        out.append({"category": cat, "name": name, "value": cfg.get(k, ""), "type": t,
                    "options": opts[k].split(OPTION_SEP) if opts.get(k) else []})
    return out


# ---------------------------------------------------------------------------------------------------------------------
# Profiel <-> game.ini

def runner_to_ini(path):
    """WineRunnerPath -> waarde in game.ini: 'wrapper' voor de wrapper van RavenGun, '' voor de standaard-wine, anders een pad met ~."""
    if not path:
        return ""
    if os.path.normpath(path) == os.path.normpath(wrapper_path()):
        return RUNNER_WRAPPER
    if os.path.normpath(path) == os.path.normpath(default_wine()):
        return ""
    return gs.shrink_home(path)


def runner_from_ini(v):
    if not v:
        return ""
    return wrapper_path() if v == RUNNER_WRAPPER else gs.expand_home(v)


def to_ini_value(kind, value):
    return runner_to_ini(value) if kind == "runner" else value


def from_ini_value(kind, value):
    return runner_from_ini(value) if kind == "runner" else value


def scalar_sections(scalars, stock=None):
    """De profielvelden uit PROFILE_FIELDS die in `scalars` staan (leeg = niet gezet) als {sectie: {sleutel: waarde}}."""
    out = {}
    for sec, key, xmlf, kind in PROFILE_FIELDS:
        v = to_ini_value(kind, scalars.get(xmlf, ""))
        if v:
            out.setdefault(sec, {})[key] = v
    return out


def effective_scalars(user, stock):
    """Het profiel zoals TeknoParrotUi het gebruikt: het gebruikersprofiel met het sjabloon voor wat ontbreekt."""
    p = dict(stock or {})
    p.update(user or {})
    if stock and "Patreon" in stock:      # het sjabloon bepaalt of een game onder het abonnement valt
        p["Patreon"] = stock["Patreon"]
    return p


def static_sections(stock, fields, exe_fallback=""):
    """De statische delen van een profiel uit het sjabloon: {sectie: {sleutel: waarde}} met de profielvelden (alleen wat gezet is; false, 0 en
    Default tellen als niet gezet), ProfileRevision en de ConfigValues ([Config], [ConfigType], [ConfigOptions]). `fields`: de velden in de
    volgorde van teknoparrot.ini (waarde = die van het sjabloon). exe_fallback: de naam van het hoofdbestand als het sjabloon er geen noemt."""
    out = {}
    for sec, key, xmlf, kind in PROFILE_FIELDS:
        if kind == "runner":
            continue
        v = norm_value(kind, xmlf, stock.get(xmlf, ""))
        if key == "ProfileRevision":
            v = stock.get("GameProfileRevision", "")
        if v:
            out.setdefault(sec, {})[key] = v
    if exe_fallback and not out.get("Game", {}).get("Executable"):
        out.setdefault("Game", {})["Executable"] = exe_fallback
    for sec, kv in config_sections(fields).items():
        if kv:
            out[sec] = kv
    return out


def norm_value(kind, xmlf, v):
    """Waarden die voor de launcher hetzelfde betekenen als 'niet gezet' (false, 0, Default) tellen als leeg, zodat game.ini niet volstaat met
    standaardwaarden en de gebruikerslaag niet met ruis."""
    v = (v or "").strip()
    if kind == "bool":
        return "true" if v.lower() == "true" else ""
    if xmlf == "msysType":
        return "" if v in ("", "0") else v
    if xmlf == "GamescopeGameWindowCompatibility":
        return "" if v in ("", "Default") else v
    return v


def locate_root(game_path, name, folders):
    """(wortel, relatief pad) van het hoofdbestand van een game voor [Identify]: de view van de game (runtime/views/<Game>), anders de submap
    van een gamemap waarin het bestand staat (of de gamemap zelf), anders de map van het bestand."""
    view = os.path.join(views_dir(), name)
    if game_path.startswith(view + "/"):
        return view, os.path.relpath(game_path, view)
    for f in folders:
        for cand in (game_path, os.path.realpath(game_path)):
            if cand.startswith(f.rstrip("/") + "/"):
                rel = os.path.relpath(cand, f)
                head, _, rest = rel.partition("/")
                return (os.path.join(f, head), rest) if rest else (f, head)
    return os.path.dirname(game_path), os.path.basename(game_path)


def has_own_profile(settings):
    """Heeft game.ini een eigen profiel? (Een uitvoerbare naam is het minimum; zonder start launch-direct vanuit de XML.)"""
    g = settings.get("Game", {})
    return bool(g.get("Executable", "").strip() or g.get("ProfileRevision", "").strip())


def profile_from_settings(name, settings, game_path=""):
    """Het profiel als dict zoals launch-direct.py het gebruikt (de velden van het XML-profiel), uit de samengevoegde instellingen."""
    p = {"ProfileName": name, "GamePath": game_path}
    for sec, key, xmlf, kind in PROFILE_FIELDS:
        v = settings.get(sec, {}).get(key, "")
        if v:
            p[xmlf] = from_ini_value(kind, v)
    p["fields"] = fields_from_settings(settings)
    return p


# ---------------------------------------------------------------------------------------------------------------------
# De game vinden: [Identify] in de gamemappen

def first_name(exe_names):
    """De eerste bestandsnaam uit ExecutableName ('a.exe;b.exe' of 'a|b')."""
    return re.split(r"[;|]", exe_names or "")[0].strip()


def identify_of(settings):
    """(relatief pad, grootte of None, sha256 of '') uit [Identify]; het pad is anders de eerste uitvoerbare naam."""
    idn = settings.get(IDENTIFY, {})
    path = (idn.get("Path") or first_name(settings.get("Game", {}).get("Executable", ""))).strip().replace("\\", "/").strip("/")
    size = None
    if idn.get("Size", "").strip().isdigit():
        size = int(idn["Size"].strip())
    return path, size, idn.get("Sha256", "").strip().lower()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def matches(root, path, size, sha, folder=""):
    """Is root/path het hoofdbestand van de game? Grootte is een goedkope controle (stat), sha256 de zekere; folder (alleen bij games
    waarvan het hoofdbestand identiek is aan dat van een andere game, bv. twee Unity-games met dezelfde Game.exe) is de mapnaam."""
    full = os.path.join(root, path)
    try:
        if folder and os.path.basename(root.rstrip("/")).strip().lower() != folder.strip().lower():
            return False
        if not os.path.isfile(full):
            return False
        if size is not None and os.stat(full).st_size != size:
            return False
        return not sha or sha256_file(full) == sha
    except OSError:
        return False


def candidate_roots(folders):
    """Waar een game kan staan: elke submap van een gamemap, en de gamemap zelf (zoals de scanner van de app)."""
    for f in folders:
        try:
            subs = sorted((e for e in os.listdir(f) if not e.startswith(".")), key=str.lower)
        except OSError:
            continue
        for s in subs:
            d = os.path.join(f, s)
            if os.path.isdir(d):
                yield d
        yield f


def find_all_in_folders(settings, folders):
    """Alle paden in de gamemappen die bij [Identify] passen (de eerste telt; meer dan één betekent dat de herkenning niet eenduidig is)."""
    path, size, sha = identify_of(settings)
    if not path:
        return []
    folder = settings.get(IDENTIFY, {}).get("Folder", "")
    return [os.path.join(r, path) for r in candidate_roots(folders) if matches(r, path, size, sha, folder)]


def find_in_folders(settings, folders):
    """De game in de gamemappen herkennen aan [Identify]: het pad van het hoofdbestand of ''."""
    found = find_all_in_folders(settings, folders)
    return found[0] if found else ""


def locate_game(name, settings, folders=None, views=None):
    """Het pad van de game, afgeleid en niet opgeslagen. Volgorde: een afwijkend pad van de gebruiker ([Game] GamePath), een lokale view
    (runtime/views/<Game>/, door tp-add-game --view gemaakt) en dan de gamemappen. Geeft (pad, bron) met bron override|view|folders|''."""
    ov = gs.expand_home(settings.get("Game", {}).get("GamePath", "").strip())
    if ov:
        return ov, "override"
    path, size, sha = identify_of(settings)
    if not path:
        return "", ""
    view = os.path.join(views or views_dir(), name)
    if os.path.isdir(view) and matches(view, path, size, sha):   # een view is van deze game: Folder telt hier niet
        return os.path.join(view, path), "view"
    found = find_in_folders(settings, library_folders() if folders is None else folders)
    return (found, "folders") if found else ("", "")


def same_file(a, b):
    try:
        return os.path.normpath(a) == os.path.normpath(b) or os.path.samefile(a, b)
    except OSError:
        return False


# ---------------------------------------------------------------------------------------------------------------------
# Abonnement. Of een game onder het abonnement valt bepaalt het sjabloon van de runtime of het eigen profiel; geen enkele laag kan dat
# uitzetten (een gebruikerslaag met Patreon = false of een lege waarde telt niet). We lezen geen sleutelwaarde en importeren geen activering.

def patreon_in_layers(name, tp, type_hint="", shipped=None, user=None):
    """True als het sjabloon in GameProfiles/ of een laag van game.ini (standaard van het soort, meegeleverd, gebruiker) Patreon = true zegt."""
    st = read_xml_profile(template_path(tp, name))
    if st and gs.truthy(st[0].get("Patreon", "")):
        return True
    shipped, user = shipped or gs.shipped_dir(), user or gs.user_dir()
    layers = [os.path.join(shipped, name, "game.ini"), os.path.join(user, name, "game.ini")]
    typ = type_hint
    for lp in layers:
        t = gs.read_ini(lp)[0].get("Game", {}).get("Type", "")
        typ = t or typ
    if typ:
        layers.insert(0, os.path.join(shipped, "_defaults", typ + ".ini"))
    return any(gs.truthy(gs.read_ini(lp)[0].get("Game", {}).get("Patreon", "")) for lp in layers)


# ---------------------------------------------------------------------------------------------------------------------
# UserProfiles/<P>.xml als uitvoerformaat voor TeknoParrotUi (alleen voor de start via TeknoParrotUi, Mode = tpui en de terugval)

def xml_escape(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def set_tag(text, tag, value):
    """<tag>waarde</tag> in de XML-tekst zetten (of toevoegen voor </GameProfile>); lege waarde = tag leeg. Geeft de nieuwe tekst."""
    new = "<%s>%s</%s>" % (tag, xml_escape(value), tag)
    pat = re.compile(r"<%s>.*?</%s>|<%s\s*/>" % (tag, tag, tag), re.S)
    if pat.search(text):
        return pat.sub(lambda m: new, text, count=1)
    return text.replace("</GameProfile>", new + "\n</GameProfile>", 1)


def set_field_value(text, category, name, value):
    """De <FieldValue> van een veld in de XML-tekst; (tekst, gevonden)."""
    pat = re.compile(r"(<FieldInformation>(?:(?!</FieldInformation>).)*?<CategoryName>%s</CategoryName>\s*<FieldName>%s</FieldName>)(.*?)(</FieldInformation>)"
                     % (re.escape(xml_escape(category)), re.escape(xml_escape(name))), re.S)
    m = pat.search(text)
    if not m:
        return text, False
    body = m.group(2)
    if "<FieldValue>" in body or "<FieldValue/>" in body or "<FieldValue />" in body:
        body = re.sub(r"<FieldValue>.*?</FieldValue>|<FieldValue\s*/>", lambda _m: "<FieldValue>%s</FieldValue>" % xml_escape(value), body, count=1, flags=re.S)
    else:
        body = "<FieldValue>%s</FieldValue>" % xml_escape(value) + body
    return text[:m.start()] + m.group(1) + body + m.group(3) + text[m.end():], True


def render_user_xml(base_text, settings, game_path):
    """Past een profiel-XML (het bestaande gebruikersprofiel, of het sjabloon) aan met wat game.ini zegt: GamePath, WineRunnerPath en de
    waarden van de ConfigValues. De rest (knoppen, opmaak) blijft staan. Geeft (tekst, [velden die het profiel niet kent])."""
    text = set_tag(base_text, "GamePath", game_path)
    # Een tweede exe (HasTwoExecutables, bv. amdaemon.exe bij HOTDSD): [Identify] Path2 is relatief aan dezelfde gamemap als Path.
    path, _size, _sha = identify_of(settings)
    path2 = settings.get(IDENTIFY, {}).get("Path2", "").strip().replace("\\", "/").strip("/")
    if path2 and game_path:
        root = game_path[:-len(path)].rstrip("/") if path and game_path.endswith("/" + path) else os.path.dirname(game_path)
        if os.path.isfile(os.path.join(root, path2)):
            text = set_tag(text, "GamePath2", os.path.join(root, path2))
    runner = runner_from_ini(settings.get("Wine", {}).get("Runner", ""))
    if runner:
        text = set_tag(text, "WineRunnerPath", runner)
    unknown = []
    for f in fields_from_settings(settings):
        text, ok = set_field_value(text, f["category"], f["name"], f["value"])
        if not ok:
            unknown.append(field_key(f))
    return text, unknown

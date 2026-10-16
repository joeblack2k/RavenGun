#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Maakt patches/<Game>/game.ini (en patches.ini, README.md) uit de oude bronnen van de game-kennis
(docs/per-game-patches.md, "Overgang"):
  - packaging/io-routes.json      -> [Input] en [Launch] Mode
  - packaging/games-notes.json    -> [Status] (Status, Note = Nederlands, NoteEN = Engels)
  - <runtime>/UserProfiles/*.env  -> [Loader], [Wine], [Display], [Input] Jvs*, [Launch] WorkingDirectory, [Env] en de lader-patch
  - <runtime>/UserProfiles/*.xml  -> het soort (EmulatorType) en de map van de game (alleen gelezen)
  - packaging/game-options.json   -> [Game] Name (bron weg sinds 2026-10-16: git show <rev>:packaging/game-options.json)

Opnieuw te draaien (idempotent): het script leidt per bron alleen de sleutels af die die bron kent, haalt die eerst weg en zet ze
opnieuw; andere sleutels en secties in een bestaand game.ini (handwerk) blijven staan. Een bron die er niet is (bv. na het
verwijderen van de JSON-bestanden uit de repo) laat de bijbehorende sleutels met rust: geef hem dan op met --io-routes en
--notes, of met --rev REV (leest packaging/io-routes.json en games-notes.json uit die revisie, bv. een andere tak).
Waarden die gelijk zijn aan de standaard van het soort (patches/_defaults/<Type>.ini) worden niet herhaald; ontbreekt een
variabele die de standaard wel zet, dan komt de oude uitkomst expliciet in het bestand (anders zou de game veranderen).

Er worden nooit geheimen overgenomen (variabelen met TOKEN, SECRET, PASSWORD, SERIAL, LICENSE, ... in de naam) en geen pad
naar de NAS: de werkmap staat relatief aan de map van de game, paden onder $HOME als ~/... .
De .env-bestanden en het runtimeprofiel worden alleen gelezen. Schrijft alleen onder --out (standaard patches/ in de repo).

Gebruik: tools/dev/migrate-game-settings.py [--out DIR] [--io-routes F] [--notes F] [--rev REV] [--runtime TP] [--dry-run] [--check]
"""
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "scripts"))   # gamesettings
sys.dont_write_bytecode = True
import gamesettings as gs  # noqa: E402

ROOT = gs.ROOT
GEN_MARK = "Gegenereerd door tools/dev/migrate-game-settings.py"

# Spelnamen die game-options.json niet kent (eigen route of afwijkende naam); zelfde lijst als gen-compatibility.py vroeger had.
EXTRA_TITLES = {
    "TombRaider": "Tomb Raider: Firestorm Assault",
    "RabbidsHollywood": "Rabbids Hollywood Arcade",
    "LGS": "Let's Go Safari",
}

# Bestand met bevindingen op de NAS (findings/<naam>.md, naast de games) per profiel. Alleen deze games krijgen een README.md.
FINDINGS = {
    "GaiaAttack4": "GaiaAttack4", "GalagaAssault": "GalagaAssault", "Gashaaaan": "Gashaaaan", "Gashaaaan2": "GashaaaanRefill",
    "GhostBusters": "GhostBusters", "GoGoStrike": "GoGoStrike", "Halo": "Halo", "HauntedMuseum": "HauntedMuseum",
    "HauntedMuseumII": "HauntedMuseum", "InfinityBladeFX": "InfinityBladeFX", "JurassicPark": "JurassicPark",
    "LethalEnforcers3": "LethalEnforcers3", "LGI": "LetsGoIsland", "LGI3D": "LetsGoIsland3D", "LGS": "LetsGoSafari",
    "LuigisMansion": "LuigisMansion", "MedaruNoGunman": "MedaruNoGunman", "MusicGunGun2": "MusicGunGun2", "NERF": "NERF",
    "OmatsuriYasanGansoTakoyaki": "OmatsuriYasan", "OmatsuriYasanKingyoSukui": "OmatsuriYasan", "Otomedius": "Otomedius",
    "PacmanChomp": "PacmanChompMania", "PigsMightFly": "PigsMightFly", "RabbidsHollywood": "RabbidsHollywood",
    "RaccoonRampage": "RaccoonRampage", "Terminator": "Terminator",
}

# Informatie die alleen in notities en .env-commentaar stond over de eigen prefix (voor tp-add-game, niet voor de launcher).
PREFIX_INFO = {
    "LGI": {"VirtualDesktop": "1920x1080"},
    "LGI3D": {"VirtualDesktop": "1920x1080"},
    "MedaruNoGunman": {"NativeDlls": "d3dx9_40"},
}

SECTION_ORDER = ["Game", "Launch", "Loader", "Wine", "Display", "Input", "Env", "Status"]
ROUTE_KEYS = {"Launch": {"Mode"}, "Input": {"Route", "Layout", "Board", "Boards", "ComPort", "CoinKeys", "Note"}}
NOTE_KEYS = {"Status": {"Status", "Note", "NoteEN", "Findings"}}
ENV_KEYS = {}
for _sec, _key, _var, _kind in gs.ENV_MAP:
    ENV_KEYS.setdefault(_sec, set()).add(_key)


def log(msg):
    print(msg, file=sys.stderr)


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def git_show(rev, rel):
    return json.loads(subprocess.run(["git", "-C", ROOT, "show", "%s:%s" % (rev, rel)], check=True, capture_output=True,
                                     text=True).stdout)


def titles(options):
    t = {}
    for g in options.get("games", {}).values():
        prof = g.get("tp_profile")
        if prof and g.get("title"):
            t[prof] = g["title"]
    for k, v in EXTRA_TITLES.items():
        t.setdefault(k, v)
    return t


def profile_fields(path):
    """{veldnaam: (waarde, opties)} uit ConfigValues van een TeknoParrot-profiel (alleen gelezen); leeg als het bestand ontbreekt."""
    out = {}
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError):
        return out
    cv = root.find("ConfigValues")
    if cv is not None:
        for fi in cv.findall("FieldInformation"):
            out.setdefault(fi.findtext("FieldName") or "", ((fi.findtext("FieldValue") or "").strip(),
                                                              [o.text or "" for o in fi.findall("FieldOptions/string")]))
    return out


def profile_info(runtime, profile):
    """Wat game.ini over het profiel vastlegt: EmulationProfile (uit GameProfiles/<P>.xml, anders het gebruikersprofiel), de regio-keuzes
    DongleRegion en PcbRegion (de waarde die het gebruikersprofiel nu heeft; leeg als het profiel die optie niet kent) en, alleen ter
    informatie, DisplayMode en CustomResolution waar het profiel zulke opties heeft."""
    info = {}
    user = os.path.join(runtime, "UserProfiles", profile + ".xml")
    stock = os.path.join(runtime, "GameProfiles", profile + ".xml")
    for path in (stock, user):
        try:
            with open(path, encoding="utf-8-sig") as f:
                m = re.search(r"<EmulationProfile>(.*?)</EmulationProfile>", f.read(), re.S)
        except OSError:
            continue
        if m and m.group(1).strip():
            info["EmulationProfile"] = m.group(1).strip()
            break
    fields = profile_fields(user) or profile_fields(stock)
    for key in ("DongleRegion", "PcbRegion"):
        if key in fields and fields[key][0]:
            info[key] = fields[key][0]
    if "DisplayMode" in fields and fields["DisplayMode"][0]:
        info["DisplayMode"] = fields["DisplayMode"][0]
    on = [fields[k][0] for k in ("CustomResolution", "Use Custom Resolution") if k in fields]
    if on and on[0] == "1":
        for wk, hk in (("Custom Resolution Width", "Custom Resolution Height"), ("Resolution Width", "Resolution Height"),
                       ("ResolutionWidth", "ResolutionHeight")):
            if wk in fields and hk in fields and fields[wk][0].isdigit() and fields[hk][0].isdigit():
                info["CustomResolution"] = "%sx%s" % (fields[wk][0], fields[hk][0])
                break
    return info


def lead_comments(text):
    out = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith((";", "#")) or not s:
            out.append(line.rstrip())
        else:
            break
    while out and not out[-1]:
        out.pop()
    return out


def env_comments(path):
    """Commentaarregels uit een .env (zonder #), met paden onder $HOME als ~."""
    out = []
    home = os.environ.get("HOME", gs.HOME).rstrip("/")
    try:
        with open(path) as f:
            for line in f:
                s = line.strip()
                # Alleen uitleg; een uitgecommentarieerde toewijzing (#TP_X=1) is geen uitleg.
                if s.startswith("#") and not re.match(r"#\s*[A-Za-z_][A-Za-z0-9_]*=", s):
                    out.append(s.lstrip("#").strip().replace(home, "~").replace("packaging/games-notes.json", "[Status] in game.ini")
                               .replace("packaging/io-routes.json", "[Input] in game.ini"))
    except OSError:
        pass
    return [c for c in out if c]


def render_header(profile, extra):
    lines = ["%s (docs/per-game-patches.md). Sleutels die het script afleidt uit de oude bronnen worden bij" % GEN_MARK,
             "elke run opnieuw gezet; handwerk in andere sleutels blijft staan."]
    if extra:
        lines += ["", "Uit het oude UserProfiles/%s.env:" % profile] + extra
    return "\n".join(lines)


def section_comments(text):
    """Commentaarblokken vlak vóór een sectiekop in een bestaand bestand ({sectie: [regels]}); handwerk dat de migratie moet bewaren
    bij secties die ze niet zelf afleidt (bv. [Bindings])."""
    out, buf = {}, []
    for line in text.splitlines():
        st = line.strip()
        if st.startswith((";", "#")):
            buf.append(line.rstrip())
        elif st.startswith("[") and st.endswith("]"):
            if buf and st[1:-1] not in SECTION_ORDER:
                out[st[1:-1]] = buf
            buf = []
        elif st:
            buf = []
        else:
            buf = [b for b in buf] if buf and not buf[-1] == "" else buf   # lege regel binnen het blok: blok blijft
    return out


def read_text(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except OSError:
        return ""


def write_if_changed(path, text, dry, stats):
    cur = read_text(path)
    if cur == text:
        return False
    stats["changed"].append(path)
    if not dry:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path + ".tmp", "w", encoding="utf-8") as f:
            f.write(text)
        os.replace(path + ".tmp", path)
    return True


KEY_ORDER = {
    "Game": ["Name", "Profile", "Type", "EmulationProfile", "DongleRegion", "PcbRegion", "ExeSha256"],
    "Launch": ["Mode", "WorkingDirectory", "View", "ViewEmptyDirs"],
    "Loader": ["RemoteThread", "PostStartMs", "EntryPointMs"],
    "Wine": ["Prefix", "PrefixDirect", "Binary", "VirtualDesktop", "NativeDlls", "WindowsVersion", "DllOverrides", "Debug",
             "GStreamerPlugins"],
    "Display": ["Gamescope", "GamescopePath", "GamescopeBackend", "GamescopeResolution", "DisplayMode", "CustomResolution"],
    "Input": ["Route", "Layout", "Board", "Boards", "ComPort", "CoinKeys", "JvsTransport", "JvsPipeBoard", "JvsF1Once",
              "JvsDebug", "JvsIdent", "JvsFeatures", "Note"],
    "Status": ["Status", "Note", "NoteEN", "Findings"],
}


def ordered(sections):
    """Vaste volgorde van secties en sleutels (zelfde uitvoer bij elke run); onbekende sleutels volgen in hun oude volgorde."""
    names = [s for s in SECTION_ORDER if s in sections] + [s for s in sections if s not in SECTION_ORDER]
    out = {}
    for s in names:
        kv = sections[s]
        if not kv:
            continue
        known = [k for k in KEY_ORDER.get(s, []) if k in kv]
        out[s] = {k: kv[k] for k in known + [k for k in kv if k not in known]}
    return out


def drop_keys(sections, keyset):
    for sec, keys in keyset.items():
        if sec in sections:
            for k in list(sections[sec]):
                if keys is None or k in keys:
                    del sections[sec][k]


def defaults_for(out_dir, typ):
    if not typ:
        return {}
    s, _ = gs.read_ini(os.path.join(out_dir, "_defaults", typ + ".ini"))
    return s


def csv(v):
    return ",".join(v) if isinstance(v, list) else str(v)


def migrate(out_dir, io_routes, notes, runtime, dry=False):
    stats = {"changed": [], "games": 0}
    routes = (io_routes or {}).get("games", {})
    nl = (notes or {}).get("games", {})
    en = (notes or {}).get("games_en", {})
    status = (notes or {}).get("status", {})
    try:
        options = load_json(os.path.join(ROOT, "packaging", "game-options.json"))
    except (OSError, ValueError):
        options = {}
    names = titles(options)
    try:
        unity = set(n for n in os.listdir(os.path.join(ROOT, "patches")) if os.path.isfile(os.path.join(ROOT, "patches", n, "unity.json")))
    except OSError:
        unity = set()
    prof_dir = os.path.join(runtime, "UserProfiles") if runtime else ""
    profiles = set(routes) | set(nl) | set(en) | set(status)
    if prof_dir and os.path.isdir(prof_dir):
        for e in os.listdir(prof_dir):
            if e.endswith((".xml", ".env")):
                profiles.add(e.rsplit(".", 1)[0])
    for profile in sorted(profiles, key=str.lower):
        have_xml = bool(prof_dir) and os.path.isfile(os.path.join(prof_dir, profile + ".xml"))
        env_path = os.path.join(prof_dir, profile + ".env") if prof_dir else ""
        env_known = have_xml or (bool(env_path) and os.path.isfile(env_path))
        gdir = os.path.join(out_dir, profile)
        gpath = os.path.join(gdir, "game.ini")
        old_text = read_text(gpath)
        sections, _ = gs.parse_ini(old_text) if old_text else ({}, [])
        sections = {s: dict(kv) for s, kv in sections.items()}

        # Soort: het profiel (EmulatorType), een Unity-game, of wat er al stond.
        typ = gs.profile_emulator_type(runtime, profile) if have_xml else ""
        if profile in unity or (routes.get(profile, {}).get("route") == "unity-tcp"):
            typ = "Unity"
        typ = typ or sections.get("Game", {}).get("Type", "")
        game = sections.setdefault("Game", {})
        game["Profile"] = profile
        if profile in names:
            game["Name"] = names[profile]
        if typ:
            game["Type"] = typ

        if io_routes is not None:
            drop_keys(sections, ROUTE_KEYS)
            r = routes.get(profile)
            if r:
                inp = sections.setdefault("Input", {})
                inp["Route"] = r.get("route", io_routes.get("default", "evdev"))
                for src, dst in (("layout", "Layout"), ("board", "Board"), ("com_port", "ComPort"), ("note", "Note")):
                    if r.get(src):
                        inp[dst] = r[src]
                if r.get("boards"):
                    inp["Boards"] = csv(r["boards"])
                if r.get("coin_keys"):
                    inp["CoinKeys"] = csv(r["coin_keys"])
                if r.get("launch"):
                    sections.setdefault("Launch", {})["Mode"] = "tpui" if r["launch"] == "teknoparrot-ui" else r["launch"]
        if notes is not None:
            drop_keys(sections, NOTE_KEYS)
            st = sections.setdefault("Status", {})
            if profile in status:
                st["Status"] = status[profile]
            if profile in nl:
                st["Note"] = nl[profile]
            if profile in en:
                st["NoteEN"] = en[profile]
            if profile in FINDINGS and (profile in status or profile in nl):
                st["Findings"] = "findings/%s.md" % FINDINGS[profile]

        patches_enabled = None
        header_extra = None
        if env_known:
            drop_keys(sections, dict(ENV_KEYS, Env=None, Wine=ENV_KEYS["Wine"] | {"VirtualDesktop", "NativeDlls"},
                                     Game={"EmulationProfile", "DongleRegion", "PcbRegion"},
                                     Display=ENV_KEYS["Display"] | {"DisplayMode", "CustomResolution"}))
            if have_xml:
                for k, v in profile_info(runtime, profile).items():
                    sections.setdefault("Display" if k in ("DisplayMode", "CustomResolution") else "Game", {})[k] = v
            env = gs.read_env_file(env_path) if env_path else {}
            gamedir = gs.profile_gamedir(runtime, profile) if have_xml else ""
            derived, pat = gs.env_to_settings(env, gamedir)
            dflt = defaults_for(out_dir, typ)
            for sec, kv in derived.items():
                for k, v in kv.items():
                    if sec != "Env" and dflt.get(sec, {}).get(k) == v:
                        continue
                    sections.setdefault(sec, {})[k] = v
            for sec, kv in dflt.items():   # een standaard die de oude start niet had: de oude uitkomst vastleggen
                for k in kv:
                    if sec in ENV_KEYS and k in ENV_KEYS[sec] and k not in derived.get(sec, {}):
                        sections.setdefault(sec, {})[k] = ""
            for k, v in PREFIX_INFO.get(profile, {}).items():
                sections.setdefault("Wine", {})[k] = v
            patches_enabled = pat
            header_extra = env_comments(env_path) if env else []
        # Lege waarden ("reset") blijven staan; verder geen lege secties.
        sections = ordered(sections)
        if env_known:
            comments = render_header(profile, header_extra)
        else:
            lc = lead_comments(old_text)
            comments = "\n".join(c[1:].strip() if c.startswith((";", "#")) else c for c in lc) if lc else render_header(profile, [])
        text = gs.dump_ini(sections, comments)
        for sec, lines in section_comments(old_text).items():
            if sec in sections:
                text = text.replace("\n[%s]\n" % sec, "\n" + "\n".join(lines) + "\n[%s]\n" % sec, 1)
        stats["games"] += 1
        write_if_changed(gpath, text, dry, stats)

        # patches.ini: alleen de lader-patch (RG_LOADER_FPFIX), en alleen als de .env bekend is.
        if patches_enabled is not None:
            ppath = os.path.join(gdir, "patches.ini")
            psec, _ = gs.read_ini(ppath)
            key = gs.PATCH_PREFIX + " " + gs.FPFIX_PATCH
            if gs.FPFIX_PATCH in patches_enabled:
                psec.setdefault(key, {})["Enabled"] = "true"
            elif key in psec:
                psec[key].pop("Enabled", None)
            psec = {s: kv for s, kv in psec.items() if kv}
            if psec:
                write_if_changed(ppath, gs.dump_ini(psec, "%s. Zet een gedeelde patch aan (definitie in _defaults/<Type>.patches.ini); andere patches in dit bestand zijn handwerk en blijven staan." % GEN_MARK), dry, stats)
            elif os.path.exists(ppath) and GEN_MARK in read_text(ppath):
                stats["changed"].append(ppath)
                if not dry:
                    os.unlink(ppath)

        # README.md alleen met een verwijzing naar een bevindingenbestand.
        rpath = os.path.join(gdir, "README.md")
        if profile in FINDINGS and (profile in status or profile in nl):
            cur = read_text(rpath)
            if not cur or GEN_MARK in cur.splitlines()[0]:
                nm = sections.get("Game", {}).get("Name", profile)
                body = ["<!-- %s; verwijder deze regel als je het bestand zelf bewerkt. -->" % GEN_MARK, "# %s" % nm, "",
                        "Instellingen: `game.ini`%s." % (", patches: `patches.ini`" if os.path.exists(os.path.join(gdir, "patches.ini")) or
                                                         (patches_enabled and gs.FPFIX_PATCH in patches_enabled) else ""),
                        "Bevindingen en meetgeschiedenis: `findings/%s.md` (op de NAS naast de games, niet in deze repo)." % FINDINGS[profile], ""]
                write_if_changed(rpath, "\n".join(body), dry, stats)
    return stats


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="Maakt patches/<Game>/ uit io-routes.json, games-notes.json en de .env-bestanden")
    ap.add_argument("--out", default=os.path.join(ROOT, "patches"))
    ap.add_argument("--io-routes")
    ap.add_argument("--notes")
    ap.add_argument("--rev", metavar="REV", help="lees packaging/io-routes.json en games-notes.json uit deze git-revisie")
    ap.add_argument("--runtime", default=os.environ.get("RAVENGUN_TP_DIR", gs.DEFAULT_TP))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--check", action="store_true", help="exit 1 als de uitvoer zou veranderen (niets schrijven)")
    a = ap.parse_args(argv)
    io_routes = notes = None
    try:
        if a.rev:
            io_routes = git_show(a.rev, "packaging/io-routes.json")
            notes = git_show(a.rev, "packaging/games-notes.json")
        if a.io_routes:
            io_routes = load_json(a.io_routes)
        elif io_routes is None and os.path.isfile(os.path.join(ROOT, "packaging", "io-routes.json")):
            io_routes = load_json(os.path.join(ROOT, "packaging", "io-routes.json"))
        if a.notes:
            notes = load_json(a.notes)
        elif notes is None and os.path.isfile(os.path.join(ROOT, "packaging", "games-notes.json")):
            notes = load_json(os.path.join(ROOT, "packaging", "games-notes.json"))
    except (OSError, ValueError, subprocess.CalledProcessError) as e:
        log("bron niet te lezen: %s" % e)
        return 2
    if io_routes is None:
        log("geen io-routes.json (packaging/ of --io-routes/--rev): [Input] en [Launch] Mode blijven ongemoeid")
    if notes is None:
        log("geen games-notes.json (packaging/ of --notes/--rev): [Status] blijft ongemoeid")
    runtime = a.runtime if os.path.isdir(os.path.join(a.runtime, "UserProfiles")) else ""
    if not runtime:
        log("geen runtime met UserProfiles/ (%s): de .env-bestanden en profielen worden overgeslagen" % a.runtime)
    try:
        stats = migrate(a.out, io_routes, notes, runtime, dry=a.dry_run or a.check)
    except ValueError as e:
        log("fout: %s" % e)
        return 2
    verb = "zouden veranderen" if (a.dry_run or a.check) else "veranderd"
    log("%d games verwerkt, %d bestanden %s" % (stats["games"], len(stats["changed"]), verb))
    for p in stats["changed"]:
        log("  " + os.path.relpath(p, a.out))
    return 1 if (a.check and stats["changed"]) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Importeert TeknoParrot-profielen als eigen spelprofiel van RavenGun (docs/per-game-patches.md, "Het profiel van een game").

Leest uit de runtime (alleen gelezen) UserProfiles/<P>.xml en GameProfiles/<P>.xml en schrijft:
  - de statische profieldelen, uit het sjabloon, in <out>/<P>/game.ini (standaard patches/ in de repo): [Game] Executable, ProfileRevision,
    Is64Bit, Patreon, ..., [Identify] (hoe de game in een gamemap wordt herkend) en [Config]/[ConfigType]/[ConfigOptions] (de ConfigValues
    die in teknoparrot.ini terechtkomen);
  - in de gebruikerslaag (<user>/<P>/game.ini, standaard ~/.local/share/ravengun/patches) alleen wat afwijkt van het sjabloon: waarden
    van ConfigValues, afwijkende profielvelden, het eigen wine-pad ([Wine] Runner), en [Game] GamePath alleen als de game niet in de gamemappen
    wordt teruggevonden.
Het pad naar de game staat dus niet in de repo en normaal ook niet in de gebruikerslaag: het wordt afgeleid uit de gamemappen van de
app (library.json; Dolphin: ISOPath0..N) en [Identify]. De importer controleert dat de herkenning dezelfde game vindt als het huidige
GamePath van het profiel en schrijft anders een override.

Nooit overgenomen: JoystickButtons en andere invoertoewijzingen van TeknoParrotUi (onze invoer staat in [Input] en [Bindings]),
sleutels, serials, tokens of activering (gamesettings.NEVER_IN_FILES). Handwerk en commentaar in bestaande bestanden blijven staan; de
bronvermelding staat als commentaarregel boven [Identify]. Opnieuw te draaien (idempotent).

Gebruik: scripts/import-tp-profiles.py [--all | PROFIEL...] [--runtime TP] [--out DIR] [--user DIR] [--game-folder DIR]...
                                       [--hash] [--add-to-library] [--dry-run] [--check]
  --all            alle games met een map in --out (patches/) die een sjabloon in de runtime hebben
  --game-folder    gamemap waarin de games staan (herhaalbaar), naast die uit library.json; bepaalt de wortel van [Identify]
  --hash           ook de sha256 van het hoofdbestand vastleggen (traag op de NAS)
  --add-to-library de opgegeven --game-folder's toevoegen aan library.json van de app
  --user DIR       gebruikerslaag (standaard $RAVENGUN_USER_PATCHES_DIR of ~/.local/share/ravengun/patches)
Exit 0 als alles is gelukt; 1 als er (ook) profielen niet geïmporteerd konden worden; 2 bij een gebruiksfout.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import gamesettings as gs  # noqa: E402
import ownprofile as op  # noqa: E402

MARK = "; Profiel uit TeknoParrot-sjabloon"
MARK_USER = "; Profiel uit TeknoParrot-sjabloon: afwijkingen van GameProfiles/%s.xml (scripts/import-tp-profiles.py)"
MANAGED_BLOCK = (op.IDENTIFY, op.CONFIG, op.CONFIG_TYPE, op.CONFIG_OPTIONS)
SECRET_VALUE = re.compile(r"token|secret|passw|serial|licen[sc]e|patreon|api[_-]?key", re.I)


def log(msg):
    print(msg, file=sys.stderr)


# ---------------------------------------------------------------------------------------------------------------------
# Tekstbewerking van een bestaand game.ini (handwerk en commentaar blijven staan)

def drop_sections(text, names):
    """Haalt de genoemde secties uit de tekst, met de bronvermelding (MARK) en commentaarregels die er direct boven staan."""
    out, skip, pending = [], False, []
    for line in text.replace("\r\n", "\n").split("\n"):
        t = line.strip()
        if t.startswith("[") and t.endswith("]") and "]" in t:
            skip = t[1:-1] in names
            if skip:
                pending = []
                continue
            out += pending
            pending = []
        if skip:
            continue
        if t.startswith(MARK):
            pending.append(line)          # hoort bij de sectie die erna komt; blijft alleen als die niet wordt weggehaald
            continue
        if pending and t:
            out += pending
            pending = []
        out.append(line)
    out += pending
    while out and out[-1] == "":
        out.pop()
    return "\n".join(out) + ("\n" if out else "")


def render_block(sections, source, names):
    lines = [source]
    for sec in names:
        kv = sections.get(sec)
        if not kv:
            continue
        lines.append("[%s]" % sec)
        for k, v in kv.items():
            lines.append("%s = %s" % (k, v) if v else "%s =" % k)
        lines.append("")
    return "\n".join(lines).rstrip("\n") + "\n"


def apply_keys(text, wanted, managed):
    """Zet in `text` de sleutels uit wanted ({sectie: {sleutel: waarde}}) en haalt de beheerde sleutels (managed: [(sec, key)]) weg die er
    niet in staan."""
    for sec, key in managed:
        if key in wanted.get(sec, {}):
            text = gs.edit_ini_text(text, sec, key, wanted[sec][key])
        else:
            text = gs.edit_ini_text(text, sec, key, "", erase=True)
    return text


def merge_text(old, wanted, managed, block, source, names=MANAGED_BLOCK):
    text = drop_sections(old, names) if old else ""
    text = apply_keys(text, wanted, managed)
    text = re.sub(r"\n{3,}", "\n\n", text)
    if any(block.get(s) for s in names):
        text = text.rstrip("\n") + ("\n\n" if text.strip() else "") + render_block(block, source, names)
    return text


def read_text(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except OSError:
        return ""


def write_if_changed(path, text, dry, changed):
    if read_text(path) == text:
        return
    changed.append(path)
    if dry:
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + ".tmp", "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(path + ".tmp", path)


# ---------------------------------------------------------------------------------------------------------------------
# De game herkennen

# ---------------------------------------------------------------------------------------------------------------------

def real_root_name(game_path, root_rel):
    """Naam van de map van de game zoals ze echt heet (bij een view: de map waar de links naartoe wijzen)."""
    if not root_rel:
        return ""
    real = os.path.realpath(game_path)
    rel = root_rel[1]
    root = real[:-len(rel)].rstrip("/") if rel and real.endswith("/" + rel) or real.endswith(rel) else os.path.dirname(real)
    return os.path.basename(root).strip()


class Skip(Exception):
    pass


def import_profile(name, tp, out_dir, user_dir, folders, hash_it, dry, changed):
    """Importeert één profiel; geeft een lijst meldingen. Raise Skip als het niet kan."""
    notes = []
    st = op.read_xml_profile(op.template_path(tp, name))
    if st is None:
        raise Skip("geen sjabloon GameProfiles/%s.xml in de runtime" % name)
    stock, stock_fields = st
    us = op.read_xml_profile(op.user_xml_path(tp, name))
    user, user_fields = us if us else (None, None)
    rev = stock.get("GameProfileRevision", "")
    if user is not None and user.get("GameProfileRevision", "") != rev:
        notes.append("gebruikersprofiel heeft revisie %s, sjabloon %s: alleen het sjabloon is geïmporteerd, de waarden van het gebruikersprofiel niet "
                     "(start de game eenmaal via TeknoParrotUi, of laat het zo)" % (user.get("GameProfileRevision"), rev))
        user, user_fields = None, None
    game_path = (user or {}).get("GamePath", "").strip()

    # Veldenlijst: wat TeknoParrotUi in de teknoparrot.ini zet, in zijn volgorde (het gebruikersprofiel als dat er is).
    order = user_fields if user_fields is not None else stock_fields
    meta = {op.field_key(f): f for f in stock_fields}
    fields_static = []
    for f in order:
        s = meta.get(op.field_key(f), f)
        fields_static.append({"category": f["category"], "name": f["name"], "value": s["value"], "type": s["type"] or f["type"],
                              "options": s["options"] or f["options"]})
    if user_fields is not None and [op.field_key(f) for f in user_fields] != [op.field_key(f) for f in stock_fields]:
        notes.append("de veldenlijst van het gebruikersprofiel wijkt af van het sjabloon (volgorde of velden): die van het gebruikersprofiel is vastgelegd")

    # --- Statische delen (sjabloon)
    try:
        everything = op.static_sections(stock, fields_static, os.path.basename(game_path) if game_path else "")
    except ValueError as e:
        raise Skip("ConfigValues niet vast te leggen: %s" % e)
    block = {k: everything.pop(k) for k in (op.CONFIG, op.CONFIG_TYPE, op.CONFIG_OPTIONS) if k in everything}
    static = everything
    static.setdefault("Game", {})
    # Het eigen wine-pad: 'wrapper' hoort bij de game (de eigen prefix, [Wine] Prefix), een ander pad is van de machine.
    runner = op.runner_to_ini((user or {}).get("WineRunnerPath", ""))
    if runner == op.RUNNER_WRAPPER:
        static.setdefault("Wine", {})["Runner"] = runner

    # --- Identify
    ident = {}
    root_rel = None
    if game_path and os.path.isfile(game_path):
        root, rel = op.locate_root(game_path, name, folders)
        ident["Path"] = rel
        try:
            ident["Size"] = str(os.stat(game_path).st_size)
            if hash_it:
                ident["Sha256"] = op.sha256_file(game_path)
        except OSError as e:
            notes.append("grootte of hash van %s niet te lezen: %s" % (game_path, e))
        root_rel = (root, rel)
        # Een tweede exe (HOTDSD: amdaemon.exe) staat in dezelfde gamemap; het pad is nodig als de XML voor TeknoParrotUi opnieuw wordt gemaakt.
        gp2 = (user or {}).get("GamePath2", "").strip()
        if gp2 and os.path.isfile(gp2):
            for cand in (gp2, os.path.realpath(gp2)):
                for base in (root, os.path.realpath(root)):
                    if cand.startswith(base.rstrip("/") + "/"):
                        ident["Path2"] = os.path.relpath(cand, base)
    elif game_path:
        notes.append("GamePath %s bestaat niet: [Identify] niet vastgelegd (de game wordt niet herkend tot je hem opnieuw koppelt)" % game_path)
    else:
        first = op.first_name(static["Game"].get("Executable", ""))
        if first:
            ident["Path"] = first
    if ident:
        block[op.IDENTIFY] = ident

    # --- Gebruikerslaag: afwijkingen van het sjabloon
    wanted_user = {}
    if user is not None:
        eff = op.effective_scalars(user, stock)
        for sec, key, xmlf, kind in op.PROFILE_FIELDS:
            if kind == "runner" or key == "ProfileRevision":
                continue
            a, b = op.norm_value(kind, xmlf, eff.get(xmlf, "")), op.norm_value(kind, xmlf, stock.get(xmlf, ""))
            if key == "Executable" and not b:    # het sjabloon noemt geen uitvoerbaar bestand: de naam van de game zelf
                b = static["Game"].get("Executable", "")
                a = a or b
            if a != b:
                if a and SECRET_VALUE.search(a):
                    notes.append("%s.%s lijkt een geheim te bevatten en is niet overgenomen" % (sec, key))
                    continue
                wanted_user.setdefault(sec, {})[key] = a
        if runner and runner != op.RUNNER_WRAPPER:
            wanted_user.setdefault("Wine", {})["Runner"] = runner
        elif static.get("Wine", {}).get("Runner") and not runner:
            wanted_user.setdefault("Wine", {})["Runner"] = ""
        dev = {}
        for f in user_fields:
            k = op.field_key(f)
            if f["value"] != meta.get(k, {"value": ""})["value"]:
                if f["value"] and SECRET_VALUE.search(f["value"]) and SECRET_VALUE.search(f["name"]):
                    notes.append("veld %s lijkt een geheim en is niet overgenomen" % k)
                    continue
                dev[k] = f["value"]
        if dev:
            try:
                for k, v in dev.items():
                    op.check_representable(op.CONFIG, k, v)
            except ValueError as e:
                raise Skip("waarde niet vast te leggen: %s" % e)
            wanted_user[op.CONFIG] = dev

    # --- Controle: vindt de herkenning dezelfde game als het huidige GamePath? Zo niet: een override in de gebruikerslaag.
    merged = {s: dict(kv) for s, kv in static.items()}
    for s, kv in block.items():
        merged.setdefault(s, {}).update(kv)
    for s, kv in wanted_user.items():
        for k, v in kv.items():
            if v == "":
                merged.get(s, {}).pop(k, None)
            else:
                merged.setdefault(s, {})[k] = v
    if game_path and os.path.isfile(game_path):
        derived, how = op.locate_game(name, merged, folders)
        found = op.find_all_in_folders(merged, folders)
        if (len(found) > 1 or (found and not op.same_file(found[0], game_path))) and "Path" in ident:
            # Meer games in de gamemappen met hetzelfde hoofdbestand (pad, grootte en eventueel hash): eerst de sha256, dan de mapnaam.
            for extra, val in (("Sha256", None), ("Folder", real_root_name(game_path, root_rel))):
                try:
                    if extra == "Sha256":
                        if "Sha256" in ident:
                            continue
                        val = op.sha256_file(game_path)
                except OSError:
                    continue
                if not val:
                    continue
                ident[extra] = val
                block[op.IDENTIFY] = ident
                merged.setdefault(op.IDENTIFY, {})[extra] = val
                found = op.find_all_in_folders(merged, folders)
                notes.append("%s komt bij meer dan één game voor: %s is vastgelegd om hem te herkennen" % (ident["Path"], extra))
                if len(found) == 1 and op.same_file(found[0], game_path):
                    break
            derived, how = op.locate_game(name, merged, folders)
        if not derived or not op.same_file(derived, game_path):
            wanted_user.setdefault("Game", {})["GamePath"] = gs.shrink_home(game_path)
            notes.append("de gamemappen vinden %s niet terug (%s): GamePath staat als override in de gebruikerslaag; voeg de map toe met "
                         "--game-folder/--add-to-library of in de app" % (game_path, ("vond " + derived) if derived else "niets"))

    source = "%s GameProfiles/%s.xml, revisie %s (scripts/import-tp-profiles.py; opnieuw importeren in plaats van met de hand wijzigen)" % (MARK, name, rev)
    managed_static = [(s, k) for s, k, _x, kind in op.PROFILE_FIELDS if kind != "runner"] + [("Wine", "Runner")]
    managed_user = [(s, k) for s, k, _x, kind in op.PROFILE_FIELDS if k != "ProfileRevision"] + [("Game", "GamePath")]
    sp = os.path.join(out_dir, name, "game.ini")
    old = read_text(sp)
    if not old:
        static["Game"].setdefault("Profile", name)
    text = merge_text(old, static, managed_static, block, source)
    write_if_changed(sp, text, dry, changed)
    up = os.path.join(user_dir, name, "game.ini")
    uold = read_text(up)
    if wanted_user or uold:
        user_block = {op.CONFIG: wanted_user.pop(op.CONFIG, {})} if wanted_user else {}
        utext = merge_text(uold, wanted_user, managed_user, user_block, MARK_USER % name, names=(op.CONFIG,))
        if utext.strip() or uold:
            write_if_changed(up, utext, dry, changed)
    return notes, root_rel


def add_to_library(path, folders):
    try:
        with open(path, encoding="utf-8") as f:
            d = json.load(f)
    except (OSError, ValueError):
        d = {"schema": 1, "folders": []}
    have = {(v if isinstance(v, str) else v.get("path", "")) for v in d.get("folders", [])}
    new = [f for f in folders if f not in have and gs.shrink_home(f) not in have]
    for f in new:
        d.setdefault("folders", []).append({"path": f, "recursive": True})
    if new:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path + ".tmp", "w", encoding="utf-8") as fh:
            json.dump(d, fh, indent=2, ensure_ascii=False)
            fh.write("\n")
        os.replace(path + ".tmp", path)
    return new


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="Importeert TeknoParrot-profielen als eigen spelprofiel (game.ini)")
    ap.add_argument("profiles", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--runtime", default=os.environ.get("RAVENGUN_TP_DIR", gs.DEFAULT_TP))
    ap.add_argument("--out", default=gs.shipped_dir())
    ap.add_argument("--user", default=None)
    ap.add_argument("--game-folder", action="append", default=[])
    ap.add_argument("--hash", action="store_true")
    ap.add_argument("--add-to-library", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--check", action="store_true", help="exit 1 als de uitvoer zou veranderen (niets schrijven)")
    a = ap.parse_args(argv)
    if bool(a.all) == bool(a.profiles):
        ap.error("geef --all of een of meer profielen")
    if not os.path.isdir(os.path.join(a.runtime, "GameProfiles")):
        log("geen runtime met GameProfiles/: %s" % a.runtime)
        return 2
    user_dir = a.user or gs.user_dir()
    if os.path.realpath(user_dir) == os.path.realpath(a.out):
        log("--user en --out zijn dezelfde map")
        return 2
    extra = [os.path.normpath(gs.expand_home(f)) for f in a.game_folder]
    folders = extra + [f for f in op.library_folders() if f not in extra]
    names = a.profiles if a.profiles else gs.list_games(a.out, os.path.join(os.devnull, "geen"))
    dry = a.dry_run or a.check
    changed, failed, ok = [], [], 0
    for name in names:
        try:
            notes, _root = import_profile(name, a.runtime, a.out, user_dir, folders, a.hash, dry, changed)
        except Skip as e:
            if a.all:
                log("%-28s overgeslagen: %s" % (name, e))
            else:
                log("%-28s NIET geïmporteerd: %s" % (name, e))
                failed.append(name)
            continue
        ok += 1
        log("%-28s %s" % (name, "ok" if not notes else "ok, let op"))
        for n in notes:
            log("    " + n)
    if a.add_to_library and extra:
        for f in add_to_library(op.library_file(), extra):
            log("gamemap toegevoegd aan %s: %s" % (op.library_file(), f))
    verb = "zouden veranderen" if dry else "veranderd"
    log("%d profielen geïmporteerd, %d bestanden %s" % (ok, len(changed), verb))
    for p in changed:
        log("  " + p)
    if failed:
        log("niet geïmporteerd: %s" % ", ".join(failed))
    return 1 if (failed or (a.check and changed)) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

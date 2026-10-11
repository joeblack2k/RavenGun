#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""verify-all — zet alle games van de repo naast elkaar en laat zien wat er staat en wat ontbreekt.

Dit is het controleprogramma van `docs/dongle-per-game.md`-achtige tabellen: het haalt elke game
door de launcher zelf (`launch-direct.py --plan`), kijkt naar de dongle, de patches en de drop-ins,
en schrijft één tabel.

Gebruik (op de Deck, in ~/projects/ravengun):
    python3 tools/verify-all.py                       # alle games, tabel op het scherm
    python3 tools/verify-all.py LGI Halo              # alleen deze
    python3 tools/verify-all.py --md docs/per-game-stand.md   # ook als markdown wegschrijven
    python3 tools/verify-all.py --no-plan             # sla de launcher over (snel; geen lader/kern)

Wat het per game laat zien:
    game, type, emutype, lader, kern, dongle.json, kind, script, dropin, patches, plan_rc
"""
import argparse
import ast
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATCHES = os.path.join(REPO, "patches")
SCRIPTS = os.path.join(REPO, "scripts")
HOME = os.environ.get("HOME", "/home/deck")

# De donglesoorten uit docs/dropins.md en het script dat erbij hoort.
KIND_SCRIPTS = {
    "hasp": "hasp.py",
    "matrix": "matrix.py",
    "region": "region.py",
    "sega-scan": "sega_scan.py",
    "amdaemon": "amdaemon.py",
}

sys.path.insert(0, SCRIPTS)
sys.dont_write_bytecode = True


def games():
    out = []
    for name in sorted(os.listdir(PATCHES)):
        if name.startswith("_"):
            continue
        if os.path.isdir(os.path.join(PATCHES, name)):
            out.append(name)
    return out


def read_dongle_json(game):
    """(aanwezig, kind, bron) -- de gebruikerslaag gaat voor de meegeleverde laag."""
    for base in (os.path.join(HOME, ".local/share/ravengun/patches", game, "dongle.json"),
                 os.path.join(PATCHES, game, "dongle.json")):
        if os.path.isfile(base):
            try:
                with open(base, encoding="utf-8") as fh:
                    d = json.load(fh)
                return True, (d.get("kind") or "").strip().lower(), base
            except (OSError, ValueError) as e:
                return True, "FOUT: %s" % e, base
    return False, "", ""


def dropin_state(game):
    """(aantal bestanden, aantal secties in dropin.ini, of de ini er is)."""
    d = os.path.join(PATCHES, game, "dropin")
    if not os.path.isdir(d):
        return 0, 0, False
    files = [f for f in os.listdir(d) if f != "dropin.ini" and os.path.isfile(os.path.join(d, f))]
    ini = os.path.join(d, "dropin.ini")
    if not os.path.isfile(ini):
        return len(files), 0, False
    n = 0
    with open(ini, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if re.match(r"^\s*\[.+\]\s*$", line):
                n += 1
    return len(files), n, True


def patch_state(game, exe, loader="", core=""):
    """(aantal aan, aantal uit, aantal geweigerd) met de patchmotor in controlemodus."""
    try:
        import patchfile
    except ImportError:
        return None
    try:
        rep = patchfile.apply_game(game, exe or None, loader=loader or None, core=core or None,
                                   patches_dir=PATCHES, materialize=False)
    except Exception as e:                                   # noqa: BLE001 -- we rapporteren alleen
        return ("fout", str(e)[:60], 0)
    aan = sum(1 for r in rep.results if r.status in ("toegepast", "gecontroleerd"))
    uit = sum(1 for r in rep.results if r.status == "overgeslagen")
    gew = sum(1 for r in rep.results if r.status == "geweigerd")
    return (aan, uit, gew)


def run_plan(game, timeout=180):
    try:
        p = subprocess.run([sys.executable, "scripts/launch-direct.py", game, "--plan"],
                           cwd=REPO, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired:
        return -1, "(timeout)"
    except OSError as e:
        return -1, str(e)


def plan_field(out, name):
    m = re.search(r"^%s: (.*)$" % re.escape(name), out, re.M)
    return m.group(1).strip() if m else "-"


def classify(out):
    """(lader, kern, laderpad, kernpad).

    'ours' = uit onze eigen OpenParrot-build, 'tp' = uit de TeknoParrot-runtime. De paden komen uit
    de argv die de launcher zelf zou starten, zodat de patchcontrole het juiste doel heeft."""
    m = re.search(r"^argv: (\[.*\])$", out, re.M)
    if not m:
        return "?", "?", "", ""
    try:
        argv = ast.literal_eval(m.group(1))
    except (ValueError, SyntaxError):
        argv = []
    argv = [a.replace("\\", "/") for a in argv if isinstance(a, str)]
    # De Z:-paden zijn Windows-paden in de Wine-omgeving; reken ze terug naar het echte pad.
    def to_local(p):
        mm = re.match(r"^[A-Za-z]:/(.*)$", p)
        return "/" + mm.group(1) if mm else p
    local = [to_local(a) for a in argv]

    loader = "?"
    if any("openparrot/current" in a for a in local):
        loader = "ours"
    elif any("/runtime/" in a for a in local):
        loader = "tp"
    if any("BudgieLoader" in a for a in local):
        loader = "tp-budgie"
    if any("TeknoMacaw" in a for a in local):
        loader = "tp-macaw"

    loader_path = ""
    for a in local:
        if re.search(r"(OpenParrotLoader|BudgieLoader|TeknoMacaw|OpenParrotKonamiLoader)\w*\.exe$", a, re.I):
            loader_path = a
            break

    core = "-"
    core_path = ""
    for a in local:
        if a == loader_path or a.lower().endswith(".exe"):
            continue
        if re.search(r"/(TeknoParrot/TeknoParrot(64)?|OpenParrot(Win32|x64)/OpenParrot(64)?)$", a):
            core_path = a + ".dll"
            core = "ours" if "openparrot/current" in a else "tp"
            break
    return loader, core, loader_path, core_path


def ini_field(path, section, key):
    sec = None
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                s = line.strip()
                if not s or s[0] in ";#":
                    continue
                m = re.match(r"^\[(.+?)\]\s*$", s)
                if m:
                    sec = m.group(1).strip()
                    continue
                if sec == section and "=" in s:
                    k, v = s.split("=", 1)
                    if k.strip().lower() == key.lower():
                        return v.strip()
    except OSError:
        pass
    return None


def collect(names, with_plan=True):
    rows = []
    for g in names:
        ini = os.path.join(PATCHES, g, "game.ini")
        typ = ini_field(ini, "Game", "Type") or "-"
        d_present, d_kind, d_src = read_dongle_json(g)
        script = KIND_SCRIPTS.get(d_kind, "")
        script_ok = bool(script) and os.path.isfile(os.path.join(SCRIPTS, script))
        n_files, n_sec, has_ini = dropin_state(g)
        loader = core = "-"
        emutype = "-"
        loader_path = core_path = ""
        exe = ""
        rc = ""
        if with_plan:
            rc, out = run_plan(g)
            loader, core, loader_path, core_path = classify(out)
            emutype = plan_field(out, "emulator_type")
            exe = plan_field(out, "game")
            if exe == "-":
                exe = ""
        patches = patch_state(g, exe, loader_path, core_path)
        rows.append({
            "game": g, "type": typ, "emutype": emutype, "loader": loader, "core": core,
            "dongle": d_present, "kind": d_kind, "dongle_src": d_src,
            "script": script, "script_ok": script_ok,
            "dropin_files": n_files, "dropin_sections": n_sec, "dropin_ini": has_ini,
            "patches": patches, "plan_rc": rc,
        })
        print("  %-30s lader=%-9s kern=%-5s dongle=%-12s rc=%s" % (
            g, loader, core, (d_kind or ("ja" if d_present else "-")), rc))
        sys.stdout.flush()
    return rows


def as_markdown(rows):
    out = ["# Stand per game", "",
           "Automatisch gemaakt met `python3 tools/verify-all.py --md docs/per-game-stand.md`.", "",
           "| Game | Type | Lader | Kern | Dongle | Soort | Script | Drop-in | Patches | Start |",
           "|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        p = r["patches"]
        if isinstance(p, tuple) and len(p) == 3 and isinstance(p[0], int):
            ptxt = "%d aan / %d uit / %d geweigerd" % p
        elif isinstance(p, tuple):
            ptxt = str(p[0])
        else:
            ptxt = "-"
        dtxt = "ja" if r["dongle"] else "nee"
        drop = "-"
        if r["dropin_files"] or r["dropin_ini"]:
            drop = "%d bestand(en), %d sectie(s)" % (r["dropin_files"], r["dropin_sections"])
        scr = r["script"] or "-"
        if scr != "-":
            scr += " (aanwezig)" if r["script_ok"] else " (**ontbreekt**)"
        out.append("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            r["game"], r["type"], r["loader"], r["core"], dtxt, r["kind"] or "-", scr, drop, ptxt,
            "ok" if r["plan_rc"] == 0 else r["plan_rc"]))
    return "\n".join(out) + "\n"


def summary(rows):
    def n(f):
        return sum(1 for r in rows if f(r))
    lines = ["=== samenvatting ===",
             "games:                       %d" % len(rows),
             "lader van ons:               %d" % n(lambda r: r["loader"] == "ours"),
             "kern van ons:                %d" % n(lambda r: r["core"] == "ours"),
             "kern nog TeknoParrot:        %d" % n(lambda r: r["core"] == "tp"),
             "lader nog BudgieLoader:      %d" % n(lambda r: r["loader"] == "tp-budgie"),
             "met dongle.json:             %d" % n(lambda r: r["dongle"]),
             "  waarvan script ontbreekt:  %d" % n(lambda r: r["dongle"] and not r["script_ok"]),
             "met dropin/:                 %d" % n(lambda r: r["dropin_files"] or r["dropin_ini"]),
             "plan mislukt:                %d" % n(lambda r: r["plan_rc"] not in (0, ""))]
    for r in rows:
        if r["plan_rc"] not in (0, ""):
            lines.append("  !! %-28s rc=%s" % (r["game"], r["plan_rc"]))
    return lines


def main():
    ap = argparse.ArgumentParser(description="Zet alle games naast elkaar: lader, kern, dongle, patches, drop-ins.")
    ap.add_argument("games", nargs="*", help="alleen deze games (standaard: alle)")
    ap.add_argument("--md", default="", help="schrijf de tabel ook als markdown naar dit pad")
    ap.add_argument("--no-plan", action="store_true", help="sla launch-direct.py --plan over (sneller)")
    args = ap.parse_args()

    names = args.games or games()
    print("controleren van %d game(s)..." % len(names))
    rows = collect(names, with_plan=not args.no_plan)

    print()
    print("game,type,emutype,lader,kern,dongle,kind,script,dropin_bestanden,dropin_secties,patches_aan,patches_uit,patches_geweigerd,plan_rc")
    for r in rows:
        p = r["patches"] if isinstance(r["patches"], tuple) and len(r["patches"]) == 3 else ("-", "-", "-")
        print("%s,%s,%s,%s,%s,%s,%s,%s,%d,%d,%s,%s,%s,%s" % (
            r["game"], r["type"], r["emutype"], r["loader"], r["core"],
            "ja" if r["dongle"] else "nee", r["kind"] or "-",
            (r["script"] + ("" if r["script_ok"] else "!")) if r["dongle"] else "-",
            r["dropin_files"], r["dropin_sections"], p[0], p[1], p[2], r["plan_rc"]))

    print()
    for line in summary(rows):
        print(line)

    if args.md:
        path = args.md if os.path.isabs(args.md) else os.path.join(REPO, args.md)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(as_markdown(rows))
        print()
        print("markdown geschreven: %s" % path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

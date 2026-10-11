#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Vergelijkt voor ELK profiel in <runtime>/UserProfiles het startplan en de teknoparrot.ini van scripts/launch-direct.py via het eigen
profiel (patches/<Game>/game.ini, zonder UserProfiles-XML) met die via TeknoParrotUi's UserProfiles/<P>.xml. Verschil mag er alleen zijn
in de bronvermelding (profile_source, game_source, settings_sources). Alleen gelezen: geen wine, geen game, niets naar de runtime of
de echte gebruikerslaag.

Werkwijze: het eigen profiel wordt gemaakt door de importer (scripts/import-tp-profiles.py) in een tijdelijke kopie van patches/ en een
tijdelijke gebruikerslaag. Het eigen plan draait tegen een kopie van de runtime waarin UserProfiles/ leeg is (symlinks naar de rest), zodat
het aantoonbaar zonder de XML start. De gamemappen staan in een tijdelijk library.json (--game-folder).

Gebruik: tools/compare-launch-plans.py [--tp RUNTIME] [--patches DIR] [--game-folder DIR]... [--keep]
Exit 0 als alle profielen gelijk zijn (of op dezelfde manier geen plan geven), 1 bij een verschil."""
import contextlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ROOT, "scripts"))


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


IGNORED = ("profile_source", "game_source", "settings_sources", "tp", "patches")


def norm(plan, tp_from, tp_to):
    """Het plan als gewone JSON-waarde, zonder de bronvermelding en met het runtime-pad gelijkgetrokken."""
    text = json.dumps({k: v for k, v in plan.items() if k not in IGNORED}, default=str, sort_keys=True).replace(tp_from, tp_to)
    text = text.replace(tp_from.replace("/", "\\\\"), tp_to)   # het Wine-pad (Z:\\...\\) in JSON
    p = json.loads(text)
    for d in (p.get("launch_env", {}), p.get("env", {})):
        d.pop("TP_LAUNCH_SESSION_ID", None)
    return p


def patches_norm(plan):
    """De patches zonder het pad van de runtime (de gepatchte kopie staat onder patched/, gelijk voor beide)."""
    return [{k: v for k, v in e.items() if k not in ("target",)} for e in plan.get("patches", [])]


def main(argv):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--tp", default=os.path.expanduser("~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64"))
    ap.add_argument("--patches", default=os.path.join(ROOT, "patches"))
    ap.add_argument("--game-folder", action="append", default=[])
    ap.add_argument("--keep", action="store_true", help="tijdelijke mappen laten staan en tonen")
    a = ap.parse_args(argv)
    tmp = tempfile.mkdtemp(prefix="rg-compare-")
    try:
        # 1. Het eigen profiel: importer naar een tijdelijke kopie van patches/ en een tijdelijke gebruikerslaag.
        shipped, user = os.path.join(tmp, "patches"), os.path.join(tmp, "user")
        shutil.copytree(a.patches, shipped)
        lib = os.path.join(tmp, "library.json")
        folders = [os.path.normpath(os.path.expanduser(f)) for f in a.game_folder]
        with open(lib, "w") as f:
            json.dump({"schema": 1, "folders": [{"path": p, "recursive": True} for p in folders]}, f)
        os.environ.update({"RAVENGUN_PATCHES_DIR": shipped, "RAVENGUN_USER_PATCHES_DIR": user, "RAVENGUN_LIBRARY_FILE": lib})
        imp = [sys.executable, "-I", os.path.join(ROOT, "scripts", "import-tp-profiles.py"), "--all", "--runtime", a.tp, "--out", shipped,
               "--user", user] + sum((["--game-folder", f] for f in folders), [])
        r = subprocess.run(imp, capture_output=True, text=True)
        if r.returncode not in (0, 1):
            print(r.stderr)
            return 2
        # 2. Een runtime zonder UserProfiles/.
        tp2 = os.path.join(tmp, "tp")
        os.makedirs(os.path.join(tp2, "UserProfiles"))
        for e in os.listdir(a.tp):
            if e != "UserProfiles":
                os.symlink(os.path.join(a.tp, e), os.path.join(tp2, e))
        ld = load(os.path.join(ROOT, "scripts", "launch-direct.py"), "ld_cmp")
        base = {"PATH": "/usr/bin:/bin", "HOME": os.environ["HOME"], "WAYLAND_DISPLAY": "wayland-0", "RG_GAMESCOPE_RES": "1920x1080"}
        profiles = sorted(f[:-4] for f in os.listdir(os.path.join(a.tp, "UserProfiles")) if f.endswith(".xml"))
        same = nplan = diff = 0
        for name in profiles:
            res = {}
            for label, tp, own in (("xml", a.tp, False), ("own", tp2, True)):
                try:
                    with contextlib.redirect_stderr(io.StringIO()):   # de melding "legacy .env gelezen" hoort hier niet
                        res[label] = ("plan", ld.build_plan(name, tp, base_env=dict(base), use_own=own))
                except ld.Unsupported as e:
                    res[label] = ("unsupported", str(e).replace(tp2, a.tp))
                except Exception as e:   # PatchRefused e.d.
                    res[label] = ("error", "%s: %s" % (type(e).__name__, e))
            kx, ko = res["xml"][0], res["own"][0]
            if kx != "plan" or ko != "plan":
                # Beide zonder plan om dezelfde soort reden is goed (de tekst verschilt waar het profiel of het pad genoemd wordt).
                if kx == ko and kx != "plan":
                    nplan += 1
                    print("%-28s geen plan, beide: %s: %s" % (name, kx, res["own"][1][:90]))
                else:
                    diff += 1
                    print("%-28s VERSCHIL: xml=%s %s | eigen=%s %s" % (name, kx, str(res["xml"][1])[:80] if kx != "plan" else "",
                                                                      ko, str(res["own"][1])[:120] if ko != "plan" else ""))
                continue
            px, po = res["xml"][1], res["own"][1]
            nx, no = norm(px, a.tp, "TP"), norm(po, tp2, "TP")
            notes = []
            for k in sorted(set(nx) | set(no)):
                if nx.get(k) != no.get(k):
                    notes.append("%s: xml=%r eigen=%r" % (k, str(nx.get(k))[:140], str(no.get(k))[:140]))
            if patches_norm(px) != patches_norm(po):
                notes.append("patches: xml=%r eigen=%r" % (patches_norm(px), patches_norm(po)))
            ini_x = [(os.path.basename(f), t) for f, t in px["files"]]
            ini_o = [(os.path.basename(f), t) for f, t in po["files"]]
            if ini_x != ini_o:   # byte voor byte gelijk
                notes.append("teknoparrot.ini (of een ander bestand) verschilt")
            if px["needs_activation"] != po["needs_activation"]:
                notes.append("needs_activation: xml=%s eigen=%s" % (px["needs_activation"], po["needs_activation"]))
            if notes:
                diff += 1
                print("%-28s VERSCHIL" % name)
                for n in notes:
                    print("    " + n)
            else:
                same += 1
                print("%-28s gelijk  (profiel: %s, game: %s, teknoparrot.ini %d bytes%s)" % (
                    name, po["profile_source"], po["game_source"], len(po["files"][0][1].encode()),
                    ", abonnement" if po["needs_activation"] else ""))
        print("\n%d profielen: %d gelijk (startplan, teknoparrot.ini en abonnement), %d zonder startplan (xml en eigen profiel om dezelfde "
              "reden), %d met verschil" % (len(profiles), same, nplan, diff))
        if a.keep:
            print("tijdelijke mappen: %s" % tmp)
        return 1 if diff else 0
    finally:
        if not a.keep:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Vergelijkt het startplan van scripts/launch-direct.py vóór en na de overgang naar patches/<Game>/game.ini, voor elk profiel in
<runtime>/UserProfiles (alleen gelezen; geen wine, geen game, geen schrijfacties behalve een eventuele gepatchte kopie die al
bestond). Gebruik:
    tools/compare-launch-plans.py --old PAD/launch-direct-oud.py [--new scripts/launch-direct.py] [--tp RUNTIME]
Het oude script leest UserProfiles/<P>.env; het nieuwe wordt twee keer gedraaid: alleen met game.ini (RAVENGUN_NO_LEGACY_ENV=1,
dus zonder .env) en met de .env-terugval erbij. Verwachte verschillen: alleen het pad van de gepatchte lader (de cache onder
~/.local/share/ravengun/patched/ in plaats van .../loaders/). Exit 0 als er geen andere verschillen zijn.
Ook de omgeving die play-halo.sh aan de eenheid meegeeft (de K=V-regels van het .env, nu `gamesettings.py env`) wordt vergeleken."""
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.dont_write_bytecode = True


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.dirname(os.path.abspath(path)))
    spec.loader.exec_module(m)
    return m


def norm(plan, loader_map):
    p = json.loads(json.dumps({k: v for k, v in plan.items()}, default=str))
    p["launch_env"].pop("TP_LAUNCH_SESSION_ID", None)
    p["env"].pop("TP_LAUNCH_SESSION_ID", None)
    p["argv"] = [loader_map.get(a, a) for a in p["argv"]]
    for k in ("patches", "settings_sources"):   # alleen het nieuwe script kent ze
        p.pop(k, None)
    # RG_LOADER_FPFIX is vervangen door de patch "OpenParrotLoader zonder CONTEXT_FLOATING_POINT" (bedoeld verschil).
    p["env"].pop("RG_LOADER_FPFIX", None)
    p["profile_env"].pop("RG_LOADER_FPFIX", None)
    return p


def unit_env_diff(old, gsm, tp, name):
    """De omgeving die play-halo.sh aan de eenheid meegeeft: oude .env-regels tegen `gamesettings.py env`. '' = gelijk."""
    old_env = old.read_env_file(os.path.join(tp, "UserProfiles", name + ".env"))
    os.environ["RAVENGUN_NO_LEGACY_ENV"] = "1"
    new_env = gsm.to_env(gsm.load_for_profile(name, tp).settings, gsm.profile_gamedir(tp, name))
    old_cmp = {k: v for k, v in old_env.items() if k != gsm.LEGACY_LOADER_FPFIX}
    return "" if old_cmp == new_env else "unit-omgeving: oud=%r nieuw=%r" % (old_cmp, new_env)


def main(argv):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--old", required=True)
    ap.add_argument("--new", default=os.path.join(ROOT, "scripts", "launch-direct.py"))
    ap.add_argument("--tp", default=os.path.expanduser("~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64"))
    a = ap.parse_args(argv)
    base = {"PATH": "/usr/bin:/bin", "HOME": os.environ["HOME"], "WAYLAND_DISPLAY": "wayland-0", "RG_GAMESCOPE_RES": "1920x1080"}
    # Het oude script laadt (net als het nieuwe) niets uit de omgeving behalve via base_env; gamesettings kent zijn eigen paden.
    old = load(a.old, "ld_old")
    new = load(a.new, "ld_new")
    gsm = load(os.path.join(ROOT, "scripts", "gamesettings.py"), "gs_cmp")
    profiles = sorted(f[:-4] for f in os.listdir(os.path.join(a.tp, "UserProfiles")) if f.endswith(".xml"))
    same = diff = unsup = 0
    expected = 0
    for name in profiles:
        res = {}
        for label, mod, legacy in (("old", old, None), ("new", new, "0"), ("new+legacy", new, "1")):
            if legacy is not None:
                os.environ["RAVENGUN_NO_LEGACY_ENV"] = "1" if legacy == "0" else "0"
            try:
                res[label] = ("plan", mod.build_plan(name, a.tp, base_env=dict(base)))
            except mod.Unsupported as e:
                res[label] = ("unsupported", str(e))
            except Exception as e:   # PatchRefused e.d.
                res[label] = ("error", "%s: %s" % (type(e).__name__, e))
        kinds = {k: v[0] for k, v in res.items()}
        if kinds["old"] != "plan":
            ok = all(v == res["old"] for v in res.values())
            unsup += 1
            ue = unit_env_diff(old, gsm, a.tp, name)
            print("%-28s %s%s" % (name, "geen plan (%s): %s" % (kinds["old"], res["old"][1][:70]),
                                  "" if ok else "   <-- VERSCHIL: nieuw geeft %s" % res["new"][1][:80]))
            if ue:
                print("    " + ue)
            diff += 0 if (ok and not ue) else 1
            continue
        # De gepatchte lader: oud = loaders/<naam>-fpfix-<sha>.exe, nieuw = patched/<sha>-<sha>/<naam>.
        loader_map = {}
        for label in ("new", "new+legacy"):
            if res[label][0] != "plan":
                continue
            for pt in res[label][1].get("patches", []):
                if pt.get("patched"):
                    for o in res["old"][1]["argv"]:
                        if "-fpfix-" in o:
                            loader_map[pt["patched"]] = o
        po = norm(res["old"][1], {})
        ok_all, notes = True, []
        for label in ("new", "new+legacy"):
            if res[label][0] != "plan":
                ok_all = False
                notes.append("%s: %s" % (label, res[label][1][:100]))
                continue
            pn = norm(res[label][1], loader_map)
            for k in sorted(set(po) | set(pn)):
                if po.get(k) != pn.get(k):
                    ok_all = False
                    notes.append("%s: %s oud=%r nieuw=%r" % (label, k, str(po.get(k))[:120], str(pn.get(k))[:120]))
        # Verwacht verschil: alleen het laderpad.
        moved = bool(loader_map)
        # De omgeving voor de eenheid (play-halo.sh): oude .env-regels tegen gamesettings env.
        ue = unit_env_diff(old, gsm, a.tp, name)
        if ue:
            ok_all = False
            notes.append(ue)
        if ok_all:
            same += 1
            print("%-28s gelijk%s" % (name, " (lader-kopie: %s -> patched/)" % os.path.basename(next(iter(loader_map.values()))) if moved else ""))
            expected += 1 if moved else 0
        else:
            diff += 1
            print("%-28s VERSCHIL" % name)
            for n in notes:
                print("    " + n)
    print("\n%d profielen: %d gelijk (waarvan %d met alleen een andere pad van de gepatchte lader), %d zonder startplan (oud en nieuw "
          "gelijk), %d met verschil" % (len(profiles), same, expected, unsup, diff))
    return 1 if diff else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

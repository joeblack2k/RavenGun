#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Gouden vergelijking van de directe route: het plan van de engine (rg-direct --plan, src/ravengun/launch/direct.*) tegen dat van
scripts/launch-direct.py (build_plan, droog: geen Wine, geen game, niets geschreven), voor ELK profiel in patches/ met [Launch] Mode = direct.
Per game: dezelfde uitkomst (de engine start, het script beslist, of beide weigeren) en, als de engine start, dezelfde argv, werkmap,
Wine en prefix, de VOLLEDIGE omgeving (met de drop-ins, zonder TP_LAUNCH_SESSION_ID), dezelfde geplaatste bestanden (teknoparrot.ini
met inhoud, drop-ins met bron en doel, de mappen die eerst lokaal worden), de JVS-brug, de procesnamen, de regels per patch en per
drop-in. Daarnaast de invoerroute zoals play-halo.sh hem voor een directe start kiest, en de JVS-pakketten en -antwoorden.

Twee standen:
  (standaard) hermetisch: een tijdelijke HOME met een nep-runtime, nep-laders, de games als views (een met een koppeling naar een
              nep-NAS onder ~/mnt, zodat drop-ins een map lokaal moeten maken) en in gamemappen (een op de nep-NAS: drop-ins
              weigeren), het oude UserProfiles/<P>.env en de gebruikerslaag van deze machine als die er zijn (met HOME omgezet).
  --real      tegen deze machine zoals hij is (alleen gelezen): de echte runtime, views, gamemappen en gebruikerslaag.
Gebruik: tests/direct_golden.py --rg-direct build/rg-direct [--patches patches] [--real] [--verbose]
Exit 0 als alles gelijk is, 1 bij een verschil, 77 zonder patches/ of rg-direct."""
import argparse
import importlib.util
import json
import os
import random
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPTS = os.path.join(ROOT, "scripts")
sys.dont_write_bytecode = True
REAL_HOME = os.environ.get("HOME", "/home/deck")
REAL_TP = os.path.join(REAL_HOME, ".local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64")


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


# ---------------------------------------------------------------------------------------------------------------------------------
# De Python-kant (in een eigen proces: launch-direct.py leest HOME en de XDG-variabelen bij het importeren)

def python_side(profiles, tp):
    sys.path.insert(0, SCRIPTS)
    ld = load(os.path.join(SCRIPTS, "launch-direct.py"), "launch_direct")
    gs = ld.gs
    out = {}
    for name in profiles:
        r = {"verdict": "ok"}
        # De invoerroute zoals play-halo.sh die bij een directe start kiest.
        try:
            i, _src = gs.resolve_input(name, tp)
            route = i.get("Route", "evdev") or "evdev"
            env = gs.to_env(gs.load_for_profile(name, tp).settings, gs.profile_gamedir(tp, name))
            if route == "evdev":
                route = "tp-shm" if i.get("Layout") else "fallback"
            if route == "unity-tcp" or (route == "jvs" and env.get("RG_JVS_TRANSPORT") != "pipe"):
                route = "fallback"
            r["route"] = route
        except Exception as e:   # noqa: BLE001
            r["route"] = "fout: %s" % e
        try:
            plan = ld.build_plan(name, tp, base_env=dict(os.environ))
        except ld.SettingRefused as e:
            r.update(verdict="refused", why="Instelling geweigerd: %s" % e)
        except ld.PatchRefused as e:
            r.update(verdict="refused", why="Patch geweigerd: %s" % e)
        except ld.Unsupported as e:
            r.update(verdict="fallback", why="Niet direct te starten: %s" % e)
        except Exception as e:   # noqa: BLE001 -- een fout van het script zelf; dan valt play-halo.sh ook terug
            r.update(verdict="fallback", why="launch-direct.py: %s: %s" % (type(e).__name__, e))
        else:
            if plan["dropins"]["errors"]:
                r.update(verdict="refused", why="\n".join("Drop-in geweigerd: %s" % e for e in plan["dropins"]["errors"]))
            else:
                dl = ld.dropin_log_lines(plan)
                ld.merge_env(plan, plan["dropins"]["env"])
                r["plan"] = norm({
                    "argv": plan["argv"], "cwd": plan["cwd"], "wine": plan["wine"], "prefix": plan["prefix"], "game": plan["game"],
                    "env": plan["env"], "launch_env": plan["launch_env"], "files": [list(f) for f in plan["files"]],
                    "dropin_files": [[f["src"], f["dest"]] for f in plan["dropins"]["files"]],
                    "dropin_materialise": plan["dropins"]["materialise"], "dropin_lines": dl,
                    "pipe_mode": plan["pipe_mode"], "shm_source": plan["shm_source"], "pipehelper": plan["pipehelper"],
                    "game_comms": plan["game_comms"], "gamescope": plan["gamescope"], "windowed_override": plan["windowed_override"],
                    "core_dll": plan["core_dll"] or "", "patch_lines": ld.patch_log_lines(plan["patches"]), "cwd_is_tp": plan["cwd"] == tp,
                })
        out[name] = r
    print(json.dumps(out))


def norm(p):
    for k in ("env", "launch_env"):
        p[k] = {a: b for a, b in p[k].items() if a != "TP_LAUNCH_SESSION_ID"}
    return p


def cpp_side(rg, name, tp, patches, user, env):
    r = subprocess.run([rg, "--plan", name, "--tp", tp, "--patches", patches, "--user", user], capture_output=True, text=True, env=env)
    if r.returncode != 0:
        return {"verdict": "fout", "why": r.stderr.strip()}
    d = json.loads(r.stdout)
    out = {"verdict": d["verdict"], "why": d["why"], "input": d["input"], "launch_mode": d["launch_mode"]}
    if d["plan"]:
        p = d["plan"]
        out["plan"] = norm({
            "argv": p["argv"], "cwd": p["cwd"], "wine": p["wine"], "prefix": p["prefix"], "game": p["game"], "env": p["env"],
            "launch_env": p["launch_env"], "files": p["files"], "dropin_files": [[f["src"], f["dest"]] for f in p["dropin_files"]],
            "dropin_materialise": p["dropin_materialise"], "dropin_lines": p["dropin_lines"], "pipe_mode": p["pipe_mode"],
            "shm_source": p["shm_source"], "pipehelper": p["pipehelper"], "game_comms": p["game_comms"], "gamescope": p["gamescope"],
            "windowed_override": p["windowed_override"], "core_dll": p["core_dll"], "patch_lines": p["patch_lines"], "cwd_is_tp": p["cwd"] == tp,
        })
    return out


# ---------------------------------------------------------------------------------------------------------------------------------
# De hermetische opstelling

def write(path, data, mode="w"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, mode) as f:
        f.write(data)


def sparse(path, size, magic):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(magic)
        f.truncate(max(size, len(magic)))


def fixture(tmp, patches, profiles, gs):
    home = os.path.join(tmp, "home")
    tp = os.path.join(home, ".local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64")
    data = os.path.join(home, ".local/share/ravengun")
    user = os.path.join(data, "patches")
    os.makedirs(user, exist_ok=True)
    pd = os.path.join(REAL_TP, "ParrotData.xml")
    if os.path.isfile(pd):
        os.makedirs(tp, exist_ok=True)
        shutil.copy(pd, os.path.join(tp, "ParrotData.xml"))
    else:
        write(os.path.join(tp, "ParrotData.xml"), "<ParrotData><ExitGameKey>0x1B</ExitGameKey><Elfldr2NetworkAdapterName /></ParrotData>")
    for rel in ("ElfLdr2/BudgieLoader.exe", "ElfLdr2/x64/BudgieLoader_x64.exe", "OpenParrotx64/OpenParrotLoader64.exe",
                "OpenParrotWin32/OpenParrotLoader.exe", "TeknoParrot/TeknoParrot.dll", "TeknoParrot/TeknoParrot64.dll",
                "OpenParrotWin32/OpenParrot.dll", "OpenParrotx64/OpenParrot64.dll", "pipehelper.exe", "TeknoParrot/BudgieLoader.exe",
                "N2/BudgieLoader.exe", "OpenParrotWin32/OpenParrotKonamiLoader.exe"):
        write(os.path.join(tp, rel), "x")
    for rel in ("OpenParrotWin32/OpenParrotLoader.exe", "OpenParrotx64/OpenParrotLoader64.exe", "OpenParrotWin32/OpenParrot.dll",
                "OpenParrotx64/OpenParrot64.dll"):
        write(os.path.join(data, "openparrot/current", rel), "x")
    write(os.path.join(home, ".local/share/ravengun/wine/proton-11.0-2/bin/wine"), "x")
    write(os.path.join(data, "wrappers/wine-prefix"), "x")
    write(os.path.join(data, "jvsbridge/rg-jvsbridge.exe"), "x")
    os.makedirs(os.path.join(home, ".local/share/ravengun/prefixes/shared/wine"), exist_ok=True)
    views = os.path.join(data, "runtime/views")
    games = os.path.join(home, "games")
    nas = os.path.join(home, "mnt/nas")
    write(os.path.join(home, ".config/ravengun/library.json"), json.dumps({"folders": [games, "~/mnt/nas/games"]}))
    extra = []
    for n, name in enumerate(sorted(profiles)):
        g = gs.load_game(name)
        path = (g.get("Identify", "Path", "") or g.get("Game", "Executable", "").split(";")[0]).replace("\\", "/").strip("/")
        size = int(g.get("Identify", "Size", "0") or 0) if (g.get("Identify", "Size", "") or "").isdigit() else 64
        magic = b"\x7fELF" if g.get("Game", "Type", "") in ("ElfLdr2", "N2", "Lindbergh") else b"MZ"
        rev = g.get("Game", "ProfileRevision", "")
        write(os.path.join(tp, "GameProfiles", name + ".xml"),
              "<GameProfile><GameProfileRevision>%s</GameProfileRevision><Patreon>false</Patreon></GameProfile>" % rev)
        write(os.path.join(tp, "UserProfiles", name + ".xml"),
              "<GameProfile><EmulatorType>%s</EmulatorType><GamePath>/nergens/%s</GamePath></GameProfile>" % (g.get("Game", "Type", ""), name))
        if not path:
            continue
        if name == "TC5":
            # rechtstreeks op de (nep-)NAS in een gamemap: drop-ins kunnen daar niet geplaatst worden
            sparse(os.path.join(nas, "games", name, path), size, magic)
        elif name == "Rampage":
            # een view met een map die een koppeling naar de NAS is: de drop-in in Game_Data/Plugins maakt die eerst lokaal
            sparse(os.path.join(views, name, path), size, magic)
            os.makedirs(os.path.join(nas, name, "Game_Data/Plugins"), exist_ok=True)
            write(os.path.join(nas, name, "Game_Data/Plugins/al-aanwezig.dll"), "x")
            os.symlink(os.path.join(nas, name, "Game_Data"), os.path.join(views, name, "Game_Data"))
        elif n % 2:
            sparse(os.path.join(games, name, path), size, magic)
        else:
            sparse(os.path.join(views, name, path), size, magic)
        if g.get("Identify", "Sha256", ""):
            # De echte exe is er niet (sha256): het pad komt als override van de gebruiker ([Game] GamePath), zoals de app dat zet.
            extra.append((name, "\n[Game]\nGamePath = %s\n" % os.path.join(views if name == "Rampage" or not n % 2 else games, name, path)))
    # Het oude .env en de gebruikerslaag van deze machine, met de thuismap omgezet.
    for name in profiles:
        src = os.path.join(REAL_TP, "UserProfiles", name + ".env")
        if os.path.isfile(src):
            write(os.path.join(tp, "UserProfiles", name + ".env"), open(src).read().replace(REAL_HOME, home))
        src = os.path.join(REAL_HOME, ".local/share/ravengun/patches", name, "game.ini")
        if os.path.isfile(src):
            write(os.path.join(user, name, "game.ini"), open(src).read().replace(REAL_HOME, home))
    for name, text in extra:
        pth = os.path.join(user, name, "game.ini")
        write(pth, (open(pth).read() if os.path.isfile(pth) else "") + text)
    env = {"HOME": home, "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "XDG_DATA_HOME": os.path.join(home, ".local/share"),
           "XDG_STATE_HOME": os.path.join(home, ".local/state"), "XDG_CONFIG_HOME": os.path.join(home, ".config"),
           "RAVENGUN_TP_DIR": tp, "RAVENGUN_LOADER_PATCHES_DIR": patches, "RAVENGUN_USER_PATCHES_DIR": user,
           "WAYLAND_DISPLAY": "wayland-0", "DISPLAY": ":97", "XDG_RUNTIME_DIR": os.path.join(tmp, "run"), "LANG": "C.UTF-8"}
    return env, tp, user


# ---------------------------------------------------------------------------------------------------------------------------------

def diff(a, b, path=""):
    out = []
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a:
                out.append("%s.%s alleen in de engine: %r" % (path, k, b[k]))
            elif k not in b:
                out.append("%s.%s alleen in het script: %r" % (path, k, a[k]))
            else:
                out += diff(a[k], b[k], "%s.%s" % (path, k))
    elif a != b:
        out.append("%s: script %r, engine %r" % (path, a, b))
    return out


def jvs_compare(rg, ld):
    rnd = random.Random(1234)
    cases = ["e0010210f1", "e0ff03f00105", "e00102f0f3", "e001021011", "e00103140118", "e0010420020224e0010211",
             "e00105210222000000", "e001d0df0310", "e00102", "e0010414e0010210", "e00104150500"]
    for _ in range(300):
        node = rnd.choice([1, 2, 0xFF])
        body = bytes(rnd.choice([0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x30, 0x32, 0x35, 0x77, 0xE0, 0xD0, 2, 1])
                     for _ in range(rnd.randint(1, 6)))
        raw = bytes([node, len(body) + 1]) + body
        raw += bytes([sum(raw) & 0xFF])
        enc = bytearray(b"\xe0")
        for b in raw:
            enc += bytes([0xD0, (b - 1) & 0xFF]) if b in (0xE0, 0xD0) else bytes([b])
        cases.append(enc.hex() + (enc[:rnd.randint(0, len(enc))].hex() if rnd.random() < 0.3 else ""))
    want, skip = [], set()
    for i, c in enumerate(cases):
        frames, rest = ld.jvs_frames(bytes.fromhex(c))
        s = ""
        for fr in frames:
            s += fr.hex() + ":"
            if len(fr) >= 3:
                a = ld.jvs_answer(fr[2:-1])
                try:
                    s += "reset" if a is None else ld.jvs_pack(a).hex()
                except ValueError:   # een antwoord van meer dan 254 bytes: het script zelf loopt hier vast (de pipe-thread stopt)
                    skip.add(i)
            s += " "
        want.append("%srest=%s" % (s, rest.hex()))
    got = subprocess.run([rg, "--jvs-check"], input="\n".join(cases) + "\n", capture_output=True, text=True).stdout.splitlines()
    bad = [(c, w, g) for i, (c, w, g) in enumerate(zip(cases, want, got)) if w != g and i not in skip]
    return len(cases) - len(skip), bad + ([("aantal", len(want), len(got))] if len(got) != len(want) else [])


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--rg-direct", required=True)
    ap.add_argument("--patches", default=os.path.join(ROOT, "patches"))
    ap.add_argument("--real", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--python-side", nargs="+")   # intern: de Python-kant in een eigen proces
    ap.add_argument("--tp")
    a = ap.parse_args(argv)
    if a.python_side:
        python_side(a.python_side, a.tp)
        return 0
    patches = os.path.realpath(a.patches)
    if not os.path.isdir(patches) or not os.path.isfile(a.rg_direct):
        print("geen patches/ (%s) of geen rg-direct" % patches)
        return 77
    os.environ["RAVENGUN_LOADER_PATCHES_DIR"] = patches
    os.environ.pop("RAVENGUN_PATCHES_DIR", None)
    sys.path.insert(0, SCRIPTS)
    import gamesettings as gs   # noqa: E402
    profiles = [n for n in gs.list_games() if gs.load_game(n).launch_mode == "direct"]
    tmp = tempfile.mkdtemp(prefix="rg-direct-golden-")
    try:
        if a.real:
            env = dict(os.environ)
            env["RAVENGUN_LOADER_PATCHES_DIR"] = patches
            tp = env.get("RAVENGUN_TP_DIR", REAL_TP)
            user = env.get("RAVENGUN_USER_PATCHES_DIR") or os.path.join(REAL_HOME, ".local/share/ravengun/patches")
        else:
            env, tp, user = fixture(tmp, patches, profiles, gs)
        r = subprocess.run([sys.executable, "-I", os.path.abspath(__file__), "--rg-direct", a.rg_direct, "--tp", tp, "--python-side", *profiles],
                           capture_output=True, text=True, env=env)
        if r.returncode != 0:
            print("de Python-kant faalde:\n" + r.stderr)
            return 1
        py = json.loads(r.stdout.strip().splitlines()[-1])
        fails, rows = 0, []
        for name in profiles:
            p, c = py[name], cpp_side(a.rg_direct, name, tp, patches, user, env)
            notes = []
            if c["verdict"] == "ok" and p["verdict"] == "ok":
                d = diff(p["plan"], c["plan"])
                uitkomst = "engine start, plan GELIJK" if not d else "engine start, plan VERSCHILT"
                notes += d
            elif c["verdict"] == "fallback" and c["why"].startswith("nog niet in de engine"):
                uitkomst = "via het script (%s); script: %s" % (c["why"], p["verdict"])
            elif c["verdict"] == p["verdict"] and (c["why"] == p["why"] or c["verdict"] == "fallback"):
                uitkomst = "beide %s: %s" % (c["verdict"], c["why"].splitlines()[0][:160])
                if c["why"] != p["why"]:
                    notes.append("andere melding: script %r, engine %r" % (p["why"], c["why"]))
            else:
                uitkomst = "UITKOMST VERSCHILT"
                notes.append("script %s (%s), engine %s (%s)" % (p["verdict"], p["why"], c["verdict"], c["why"]))
            # De invoerroute (play-halo.sh) tegen die van de engine.
            ci = c.get("input", {})
            croute = ci.get("route") if ci.get("verdict") == "ok" else "fallback"
            if croute != p.get("route"):
                notes.append("invoerroute: script %r, engine %r" % (p.get("route"), croute))
            if c.get("launch_mode") != "direct":
                notes.append("[Launch] Mode volgens de engine: %r" % c.get("launch_mode"))
            hard = [x for x in notes if not x.startswith("andere melding")]
            fails += bool(hard)
            rows.append((name, uitkomst + ("" if not hard else "  <-- FOUT"), notes, croute))
        n, bad = jvs_compare(a.rg_direct, load(os.path.join(SCRIPTS, "launch-direct.py"), "ld_jvs"))
        print("directe route, %s: %d games met [Launch] Mode = direct" % ("deze machine" if a.real else "hermetisch", len(profiles)))
        for name, u, notes, route in rows:
            print("  %-26s %-9s %s" % (name, route, u))
            for x in notes if (a.verbose or "FOUT" in u) else []:
                print("      " + x)
        print("JVS-pakketten: %d gevallen, %d verschillen" % (n, len(bad)))
        for x in bad[:10]:
            print("   ", x)
        return 1 if fails or bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

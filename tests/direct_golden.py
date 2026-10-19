#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Gouden vergelijking van de directe route: het plan van de engine (rg-direct --plan, src/ravengun/launch/direct.*) tegen de
VASTGELEGDE plannen in tests/golden/direct/, voor ELK profiel in patches/ met [Launch] Mode = direct (plus FORCE_DIRECT).

Herkomst van de gouden bestanden: ze zijn op 2026-10-16 gemaakt toen scripts/launch-direct.py (de Python-tweeling, sindsdien
weg) er nog was. Toen gold voor elk profiel: dezelfde uitkomst als het script en, als de engine startte, hetzelfde plan (argv,
werkmap, Wine en prefix, de volledige omgeving, teknoparrot.ini met inhoud, drop-ins met bron en doel, de mappen die eerst lokaal
worden, de exe met de grotere stapel, de JVS-brug, de procesnamen, de regels per patch en per drop-in, de besturingspipe, de
starts); ook met de views van een start (--make-view) en dezelfde boom van die views. De JVS-pakketten en -antwoorden
(jvs.json) komen rechtstreeks uit het script. Alleen wat het script niet kende ([Launch] Dxvk, Gst32, NativeDlls, ViewCopies,
Registry) is de melding van de engine zelf.

Per profiel een bestand <Profiel>.json met:
  plan   de uitkomst van `rg-direct --plan` (verdict, why, invoerroute, [Launch] Mode en de vergeleken velden van het plan)
  start  idem met --make-view (de views zoals een start ze maakt)
en _views.json met de bomen van de views die bij een start nieuw gemaakt worden (een koppeling met zijn doel, een map, of een
bestand met zijn sha256).

Alles is hermetisch: een tijdelijke HOME met een nep-runtime, nep-laders, de games als views (een met een koppeling naar een
nep-NAS onder ~/mnt, zodat drop-ins een map lokaal moeten maken) en in gamemappen (een op de nep-NAS: drop-ins weigeren). Niets
van deze machine doet mee (geen ParrotData.xml, .env of gebruikerslaag van hier). Paden worden genormaliseerd: de tijdelijke
map wordt @TMP@ (de nep-HOME is @TMP@/home), patches/ wordt @PATCHES@ en de repo @REPO@, ook in de Wine-vorm (Z:\\...).

Wat het plan bepaalt staat ook in patches/ (een eigen repo, ravengun-patches): een bewuste wijziging daar, of in de engine,
verandert de gouden bestanden. Dan opnieuw vastleggen en het verschil nalezen:
    tests/direct_golden.py --rg-direct build/rg-direct --update
Daarna (niet bij --update) de games van TP_VRIJ_DIRECT en TP_VRIJ_UNITY zonder de TeknoParrot-map (tp_vrij_check): een eigen opstelling,
plannen met een map die niet bestaat, en rg-direct --tp-refs --expect-free (docs/loskoppelen-per-game.md, "Centrale afhankelijkheden").
Gebruik: tests/direct_golden.py --rg-direct build/rg-direct [--patches patches] [--golden DIR] [--update] [--verbose]
Exit 0 als alles gelijk is, 1 bij een verschil, 77 zonder patches/ of rg-direct."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPTS = os.path.join(ROOT, "scripts")
GOLDEN = os.path.join(HERE, "golden", "direct")
sys.dont_write_bytecode = True
# Games die in de vergelijking meedoen alsof [Launch] Mode = direct (de meegeleverde game.ini zegt nog tpui): de gebruikerslaag
# van de opstelling zet Mode = direct.
FORCE_DIRECT = ("HOTDSD",)   # twee exe's: amdaemon.exe eerst, 5 s wachten, dan het spel
# Invoer die in de gebruikerslaag komt (de meegeleverde game.ini zegt nog evdev): HOTDSD via tp-shm met de besturingspipe
# (ALLS USB-IO-rapport naar \\.\pipe\TeknoParrotPipe).
FORCE_INPUT = {"HOTDSD": "\n[Input]\nRoute = tp-shm\nLayout = HOTDSDPipe\nControlPipe = AllsUsbIo\n"}
PARROT_DATA = "<ParrotData><ExitGameKey>0x1B</ExitGameKey><Elfldr2NetworkAdapterName /></ParrotData>"
# Zonder de TeknoParrot-map (2026-10-17, docs/loskoppelen-per-game.md "Centrale afhankelijkheden"): deze games (eigen lader en kern,
# of de Unity-route) plannen in een eigen opstelling met een TeknoParrot-map die niet bestaat: verdict ok, geen enkel pad uit die map,
# en hetzelfde plan als met een volledige (nep-)map. Een game die later loskomt hoort hier bij; een game die terugvalt is een fout.
# OG staat er niet in: ook zonder TP, maar het plan vraagt xactengine3_3.dll uit redist/ en die heeft deze opstelling niet.
TP_VRIJ_DIRECT = ("Friction", "GaiaAttack4", "GG", "HauntedMuseum", "HauntedMuseumII", "InfinityBladeFX", "LGI", "LGI3D", "LGS", "LuigisMansion",
                  "MenInBlack", "MusicGunGun2", "PullTheTrigger", "SilentHill")
TP_VRIJ_UNITY = ("RabbidsHollywood", "TombRaider")


def starts_view(s):
    """Wat er van een start vergeleken wordt: orde (via de lijst), rol, exe, argv, werkmap, gamescope, wachttijd en procesnamen."""
    return {"name": s["name"], "role": s["role"], "exe": s["exe"], "argv": s["argv"], "cwd": s["cwd"], "gamescope": s["gamescope"],
            "wait_before": float(s["wait_before"]), "comms": s["comms"]}


def cpp_side(rg, name, tp, patches, user, env, extra=()):
    r = subprocess.run([rg, "--plan", name, "--tp", tp, "--patches", patches, "--user", user, *extra], capture_output=True, text=True, env=env)
    if r.returncode != 0:
        return {"verdict": "fout", "why": r.stderr.strip()}
    d = json.loads(r.stdout)
    out = {"verdict": d["verdict"], "why": d["why"], "input": d["input"], "launch_mode": d["launch_mode"]}
    if d["plan"]:
        p = d["plan"]
        out["plan"] = {
            "argv": p["argv"], "cwd": p["cwd"], "wine": p["wine"], "prefix": p["prefix"], "game": p["game"],
            "env": {k: v for k, v in p["env"].items() if k != "TP_LAUNCH_SESSION_ID"},
            "launch_env": {k: v for k, v in p["launch_env"].items() if k != "TP_LAUNCH_SESSION_ID"},
            "files": p["files"], "dropin_files": [[f["src"], f["dest"]] for f in p["dropin_files"]],
            "dropin_materialise": p["dropin_materialise"], "dropin_lines": p["dropin_lines"], "pipe_mode": p["pipe_mode"],
            "shm_source": p["shm_source"], "pipehelper": p["pipehelper"], "game_comms": p["game_comms"], "gamescope": p["gamescope"],
            "windowed_override": p["windowed_override"], "core_dll": p["core_dll"], "patch_lines": p["patch_lines"], "cwd_is_tp": p["cwd"] == tp,
            "starts": [starts_view(s) for s in p["starts"]],
            "control_pipe": {"profile": p["control_pipe"], "pipe": p["control_pipe_name"], "source": p["control_source"],
                             "size": p["control_size"], "interval_ms": p["control_interval_ms"]} if p.get("control_pipe") else None,
            "stack": {"reserve": p["stack_reserve"], "src": p["stack_src"], "dest": p["stack_dest"]} if p.get("stack_reserve") else None,
            "view_links": p.get("view_links", []),
            "view_writable": p.get("view_writable", []),
        }
        # [Launch] PrefixSetup en SecondBare (review 13, ronde 2): de stappen, de stempel en de kale tweede start horen ook vast
        # te liggen. Alleen als het plan ze heeft, zodat de gouden bestanden van de andere games niet veranderen; krijgt een
        # profiel ze er stil bij, dan valt dat op ("alleen in de engine").
        if p.get("prefix_setup"):
            out["plan"]["prefix_setup"] = p["prefix_setup"]
            out["plan"]["prefix_setup_mark"] = p.get("prefix_setup_mark", "")
        if p.get("second_bare"):
            out["plan"]["second_bare"] = True
    return out


class Norm:
    """Paden van deze run en deze machine naar vaste plaatshouders (ook in de Wine-vorm met backslashes)."""

    def __init__(self, tmp, patches):
        pairs = [(os.path.realpath(tmp), "@TMP@"), (tmp, "@TMP@"), (patches, "@PATCHES@"), (os.path.realpath(ROOT), "@REPO@"), (ROOT, "@REPO@")]
        self.pairs = []
        for a, b in pairs:
            for x in (a, a.replace("/", "\\")):
                if (x, b) not in self.pairs:
                    self.pairs.append((x, b))
        self.pairs.sort(key=lambda ab: -len(ab[0]))   # langste eerst: de tijdelijke map kan onder een andere liggen

    def __call__(self, v):
        if isinstance(v, str):
            for a, b in self.pairs:
                v = v.replace(a, b)
            return v
        if isinstance(v, list):
            return [self(x) for x in v]
        if isinstance(v, dict):
            return {self(k): self(x) for k, x in v.items()}
        return v


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


def in_gamemap(name):
    """Vaste keuze per naam (niet per plaats in de lijst): een nieuw profiel verschuift de andere niet."""
    if name in ("RobinHood", "MarioKartDX", "SegaGameShell", "TNXK0OC"):
        # Hun profiel vraagt een view (ViewWritable/ViewCopies/StackReserve); zie fixture(). Zonder view
        # weigert de motor en dan legt het gouden bestand alleen de weigering vast (review 16, punt 8).
        return False
    return zlib.crc32(name.encode()) % 2 == 1


def fixture(tmp, patches, profiles, gs):
    home = os.path.join(tmp, "home")
    tp = os.path.join(home, ".local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64")
    data = os.path.join(home, ".local/share/ravengun")
    user = os.path.join(data, "patches")
    os.makedirs(user, exist_ok=True)
    # Een synthetisch profiel dat WEL naar de TeknoParrot-map verwijst. Sinds 2026-10-19 zit er in de
    # repo geen enkel profiel meer op de gesloten runtime (Coopers 9 was de laatste), en dan meet
    # `rg-direct --tp-refs` niets meer: de zelfcontrole van dat meetinstrument hieronder zou dan
    # omvallen terwijl er niets stuk is. Dit profiel staat alleen in de nagemaakte gebruikerslaag.
    os.makedirs(os.path.join(user, "TPReferentie"), exist_ok=True)
    with open(os.path.join(user, "TPReferentie", "game.ini"), "w") as f:
        f.write("[Game]\nName = TP-referentie\nProfile = TPReferentie\nExecutable = game.exe\n"
                "Type = TeknoParrot\nEmulationProfile = WartranTroopers\n\n[Launch]\nMode = direct\n")
    write(os.path.join(tp, "ParrotData.xml"), PARROT_DATA)
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
    # De PhysX-systeemsoftware (scripts/setup-physx.sh) zoals op een ingerichte machine: anders plant een game met PhysX = true niet.
    physx = os.path.join(home, ".local/share/ravengun/prefixes/shared/wine/drive_c/Program Files (x86)/NVIDIA Corporation/PhysX/Engine/v2.8.1")
    os.makedirs(physx, exist_ok=True)
    open(os.path.join(physx, "PhysXCore.dll"), "wb").close()
    views = os.path.join(data, "runtime/views")
    os.makedirs(views, exist_ok=True)
    games = os.path.join(home, "games")
    nas = os.path.join(home, "mnt/nas")
    write(os.path.join(home, ".config/ravengun/library.json"), json.dumps({"folders": [games, "~/mnt/nas/games"]}))
    extra = []
    for name in sorted(profiles):
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
        elif in_gamemap(name):
            sparse(os.path.join(games, name, path), size, magic)
        else:
            sparse(os.path.join(views, name, path), size, magic)
            if name == "RobinHood":
                os.makedirs(os.path.join(nas, name, "log"), exist_ok=True)
                write(os.path.join(nas, name, "log", "PR0.CFG"), "van de NAS\n")
                os.symlink(os.path.join(nas, name, "log"), os.path.join(views, name, "log"))
            elif name == "MarioKartDX":
                os.makedirs(os.path.join(nas, name, "AMCUS"), exist_ok=True)
                write(os.path.join(nas, name, "AMCUS", "AMConfig.ini"), "van de NAS\n")
                os.makedirs(os.path.join(views, name, "AMCUS"), exist_ok=True)
                os.symlink(os.path.join(nas, name, "AMCUS", "AMConfig.ini"),
                           os.path.join(views, name, "AMCUS", "AMConfig.ini"))
                write(os.path.join(views, name, "AMCUS", "AMAuthd.exe"), "MZ")
                # [Launch] PrefixSetup van dit profiel begint met PreprocessAmAuth.exe in de view; die
                # moet er dus staan, anders weigert de motor de stap (review 13, punt 5) en meet het
                # gouden bestand alleen nog de weigering.
                write(os.path.join(views, name, "PreprocessAmAuth.exe"), "MZ")
                # Tot een echt plan (pass 2): de tweede exe (AMAuthd) en de schrijfmap AMCUS doen dan mee. De mod zoals
                # scripts/fetch-mods.py hem neerzet (stempel = TreeSha256, een lijst met een regel) en DXVK alleen voor deze game
                # ([Env] RAVENGUN_DXVK in de gebruikerslaag; een gedeelde nep-DXVK zou de plannen van andere games veranderen).
                mod = os.path.join(home, ".local/share/ravengun/mods", name, "english")
                write(os.path.join(mod, "tree-sha256"), (g.get("Mod english", "TreeSha256", "") or "") + "\n")
                write(os.path.join(mod, "files", "files.csv"), "target,file,offset\nData/voorbeeld.pac,voorbeeld.bin,replace\n")
                write(os.path.join(mod, "files", "voorbeeld.bin"), "x")
                dxvk = os.path.join(tmp, "dxvk-" + name)
                for arch in ("x32", "x64"):
                    for dll in ("d3d8.dll", "d3d9.dll", "d3d10core.dll", "d3d11.dll", "dxgi.dll"):
                        write(os.path.join(dxvk, arch, dll), "x")
                extra.append((name, "\n[Env]\nRAVENGUN_DXVK = %s\n" % dxvk))
        # Een tweede exe (HasTwoExecutables): [Identify] Path2, relatief aan dezelfde gamemap als Path.
        path2 = (g.get("Identify", "Path2", "") or "").replace("\\", "/").strip("/")
        if path2 and name not in ("TC5", "Rampage"):
            sparse(os.path.join(games if in_gamemap(name) else views, name, path2), 64, magic)
        if g.get("Identify", "Sha256", ""):
            # De echte exe is er niet (sha256): het pad komt als override van de gebruiker ([Game] GamePath), zoals de app dat zet.
            extra.append((name, "\n[Game]\nGamePath = %s\n" % os.path.join(views if name == "Rampage" or not in_gamemap(name) else games, name, path)))
    for name in profiles:
        if name in FORCE_DIRECT:
            extra.append((name, "\n[Launch]\nMode = direct\n"))
        if name in FORCE_INPUT:
            extra.append((name, FORCE_INPUT[name]))
    for name, text in extra:
        pth = os.path.join(user, name, "game.ini")
        write(pth, (open(pth).read() if os.path.isfile(pth) else "") + text)
    env = {"HOME": home, "PATH": "/usr/bin:/bin", "XDG_DATA_HOME": os.path.join(home, ".local/share"),
           "XDG_STATE_HOME": os.path.join(home, ".local/state"), "XDG_CONFIG_HOME": os.path.join(home, ".config"),
           "RAVENGUN_TP_DIR": tp, "RAVENGUN_LOADER_PATCHES_DIR": patches, "RAVENGUN_USER_PATCHES_DIR": user,
           "WAYLAND_DISPLAY": "wayland-0", "DISPLAY": ":97", "XDG_RUNTIME_DIR": os.path.join(tmp, "run"), "LANG": "C.UTF-8"}
    return env, tp, user, views


def boom(top, norm):
    """De boom van een view zonder links te volgen: {rel: ["L", doel] | ["D"] | ["F", sha256]}."""
    out = {}
    for dp, dirs, files in os.walk(top, followlinks=False):
        for n in dirs + files:
            p = os.path.join(dp, n)
            rel = os.path.relpath(p, top)
            if os.path.islink(p):
                out[rel] = ["L", norm(os.readlink(p))]
            elif os.path.isdir(p):
                out[rel] = ["D"]
            else:
                with open(p, "rb") as f:
                    out[rel] = ["F", hashlib.sha256(f.read()).hexdigest()]
    return dict(sorted(out.items()))


def diff(want, got, path=""):
    out = []
    if isinstance(want, dict) and isinstance(got, dict):
        for k in sorted(set(want) | set(got)):
            if k not in want:
                out.append("%s.%s alleen in de engine: %r" % (path, k, got[k]))
            elif k not in got:
                out.append("%s.%s alleen in het gouden bestand: %r" % (path, k, want[k]))
            else:
                out += diff(want[k], got[k], "%s.%s" % (path, k))
    elif want != got:
        out.append("%s: gouden %r, engine %r" % (path, want, got))
    return out


def jvs_check(rg, golden_dir):
    """De JVS-pakketten en -antwoorden van rg-direct --jvs-check tegen jvs.json (gemaakt met launch-direct.py)."""
    with open(os.path.join(golden_dir, "jvs.json")) as f:
        g = json.load(f)
    got = subprocess.run([rg, "--jvs-check"], input="\n".join(g["cases"]) + "\n", capture_output=True, text=True).stdout.splitlines()
    bad = [(c, w, x) for c, w, x in zip(g["cases"], g["want"], got) if w != x]
    if len(got) != len(g["want"]):
        bad.append(("aantal", len(g["want"]), len(got)))
    return len(g["cases"]), bad


def tp_vrij_check(rg, patches, gs, tmp):
    """TP_VRIJ_DIRECT en TP_VRIJ_UNITY zonder TeknoParrot-map. Een eigen opstelling (los van de gouden): elke game in een view (via
    [Game] GamePath, zoals de app dat zet), met DXVK, Gst32, BepInEx, de redistributables, de GStreamer-plug-ins en ons GE-Proton zoals
    op een ingerichte machine. Per game: rg-direct --plan met een niet-bestaande en met de volledige nep-map geeft ok, hetzelfde plan,
    tp leeg, tp_files leeg en nergens een pad uit die map. Daarna rg-direct --tp-refs --expect-free over dezelfde opstelling (ook de
    Unity-route). Geeft het aantal fouten."""
    base = os.path.join(tmp, "zonder-tp")
    alle = set(gs.list_games())
    names = sorted(n for n in TP_VRIJ_DIRECT if n in alle)
    fails = 0
    for n in sorted(set(TP_VRIJ_DIRECT + TP_VRIJ_UNITY) - alle):
        fails += 1
        print("  %-26s staat niet in patches/  <-- FOUT" % n)
    # Een game die nog naar de TeknoParrot-map wijst krijgt ook een view, zodat het meetinstrument met de nep-map iets vindt. Sinds
    # 2026-10-19 draaien AfterDark, DrakonsRealmKeepers, LostLandAdventure, PigsMightFly en MarioKartDX op onze eigen kern (en de
    # native games hebben geen @tpui meer, 2026-10-17): Coopers9 (gesloten kern), CrossfirePaintball (ElfLdr2-lader) en TC5 (lader
    # van de runtime) zijn de voorbeelden die overblijven.
    # Sinds 2026-10-19 zit er geen enkel profiel meer op de gesloten runtime (Coopers 9 was de laatste),
    # dus is het ijk-profiel het synthetische TPReferentie uit de nagemaakte gebruikerslaag.
    ijk = ["TPReferentie"] if "TPReferentie" in alle else []
    env, tp, user, views = fixture(base, patches, names + ijk, gs)
    env = {k: v for k, v in env.items() if k != "RAVENGUN_TP_DIR"}   # de map gaat alleen via --tp mee, niet in de omgeving van het plan
    home = env["HOME"]
    data = os.path.join(home, ".local/share/ravengun")
    for name in names + ijk:
        g = gs.load_game(name)
        path = (g.get("Identify", "Path", "") or g.get("Game", "Executable", "").split(";")[0]).replace("\\", "/").strip("/")
        exe = os.path.join(views, name, path)
        if not os.path.exists(exe):
            sparse(exe, 64, b"MZ")
        pth = os.path.join(user, name, "game.ini")
        write(pth, (open(pth).read() if os.path.isfile(pth) else "") + "\n[Game]\nGamePath = %s\n" % exe)
    for arch in ("x32", "x64"):
        for dll in ("d3d8.dll", "d3d9.dll", "d3d10core.dll", "d3d11.dll", "dxgi.dll"):
            write(os.path.join(data, "dxvk/current", arch, dll), "x")
    for arch in ("x86", "x64"):
        for dll in ("d3dx9_41.dll", "d3dx9_42.dll", "d3dcompiler_42.dll"):
            write(os.path.join(data, "redist", arch, dll), "x")
        for f in ("BepInEx/core/BepInEx.Preloader.dll", "winhttp.dll", "doorstop_config.ini"):
            write(os.path.join(data, "bepinex/5.4.21.0", arch, f), "x")
    write(os.path.join(data, "gst32/proton/libgstreamer-1.0.so.0"), "x")
    os.makedirs(os.path.join(data, "gst32/plugins"), exist_ok=True)
    write(os.path.join(data, "tools/gst-extra/plugins/libgstasf.so"), "x")
    proton = os.path.join(data, "proton/GE-Proton11-7-x86_64/proton")
    write(proton, "#!/bin/sh\n")
    os.chmod(proton, 0o755)
    geen_tp = os.path.join(base, "geen-teknoparrot")   # bestaat niet

    def plan(name, tpdir):
        r = subprocess.run([rg, "--plan", name, "--tp", tpdir, "--patches", patches, "--user", user, "--session-id", "x"],
                           capture_output=True, text=True, env=env)
        return json.loads(r.stdout) if r.returncode == 0 else {"verdict": "fout", "why": r.stderr.strip(), "plan": None}

    print("zonder TeknoParrot-map (--tp %s, bestaat niet):" % geen_tp)
    for name in names:
        zonder, met = plan(name, geen_tp), plan(name, tp)
        fout = []
        if zonder["verdict"] != "ok":
            fout.append("verdict %s: %s" % (zonder["verdict"], zonder["why"]))
        if met["verdict"] != "ok":
            fout.append("met de nep-map: verdict %s: %s" % (met["verdict"], met["why"]))
        if zonder.get("plan") and met.get("plan"):
            tekst = json.dumps(zonder["plan"])
            for t in (geen_tp, tp):
                for vorm in (t, ("Z:" + t).replace("/", "\\\\")):
                    if vorm in tekst:
                        fout.append("pad uit de TeknoParrot-map in het plan: %s" % vorm)
            if zonder["plan"].get("tp") or zonder["plan"].get("tp_files"):
                fout.append("tp=%r tp_files=%r" % (zonder["plan"].get("tp"), zonder["plan"].get("tp_files")))
            if zonder["plan"] != met["plan"]:
                verschil = diff(met["plan"], zonder["plan"])
                fout.append("anders dan met de nep-map: %s" % "; ".join(verschil[:3]))
        fails += bool(fout)
        print("  %-26s %-8s %s" % (name, zonder["verdict"], "zonder TeknoParrot-map" if not fout else "<-- FOUT"))
        for x in fout:
            print("      " + x)
    # Het meetinstrument zelf (rg-direct --tp-refs): de verwachte games hebben geen verwijzing, met en zonder de nep-map; met de nep-map
    # vindt het bij een game op de kern van TeknoParrot wel iets (anders meet het niets).
    expect = ",".join(names + [n for n in TP_VRIJ_UNITY if n in alle])
    for tpdir in (geen_tp, tp):
        r = subprocess.run([rg, "--tp-refs", "--json", "--tp", tpdir, "--patches", patches, "--user", user, "--expect-free", expect],
                           capture_output=True, text=True, env=env)
        try:
            d = json.loads(r.stdout)
        except ValueError:
            d = {"games": [], "expect_free_failures": ["geen JSON: " + r.stderr.strip()]}
        bad = d.get("expect_free_failures", [])
        met_refs = [g["game"] for g in d.get("games", []) if g["refs"]]
        ok = r.returncode == 0 and not bad and (tpdir == geen_tp or met_refs)
        fails += not ok
        print("  rg-direct --tp-refs --tp %s: %d games, %d met verwijzingen, verwacht zonder: %s" %
              ("<bestaat niet>" if tpdir == geen_tp else "<nep-map>", len(d.get("games", [])), len(met_refs), "ok" if ok else "FOUT"))
        for x in bad:
            print("      " + x)
    return fails


def dump(path, obj):
    with open(path, "w") as f:
        json.dump(obj, f, indent=1, sort_keys=True, ensure_ascii=False)
        f.write("\n")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--rg-direct", required=True)
    ap.add_argument("--patches", default=os.path.join(ROOT, "patches"))
    ap.add_argument("--golden", default=GOLDEN)
    ap.add_argument("--update", action="store_true", help="de gouden bestanden opnieuw schrijven uit de engine (nalezen!)")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args(argv)
    patches = os.path.realpath(a.patches)
    if not os.path.isdir(patches) or not os.path.isfile(a.rg_direct):
        print("geen patches/ (%s) of geen rg-direct" % patches)
        return 77
    os.environ["RAVENGUN_LOADER_PATCHES_DIR"] = patches
    os.environ.pop("RAVENGUN_PATCHES_DIR", None)
    tmp = tempfile.mkdtemp(prefix="rg-direct-golden-")
    # Welke games meedoen volgt alleen uit de meegeleverde patches, niet uit de gebruikerslaag van deze machine (een proef daar zou
    # anders een game toevoegen of weghalen).
    leeg = os.path.join(tmp, "geen-gebruikerslaag")
    os.makedirs(leeg, exist_ok=True)
    os.environ["RAVENGUN_USER_PATCHES_DIR"] = leeg
    sys.path.insert(0, SCRIPTS)
    import gamesettings as gs   # noqa: E402
    profiles = sorted(n for n in gs.list_games() if gs.load_game(n).launch_mode == "direct" or n in FORCE_DIRECT)
    try:
        env, tp, user, views = fixture(tmp, patches, profiles, gs)
        norm = Norm(tmp, patches)
        got = {name: {"plan": norm(cpp_side(a.rg_direct, name, tp, patches, user, env))} for name in profiles}
        # Een start ([Launch] View = auto, docs/per-game-patches.md "De view"): nog eens plannen met de views zoals een start ze
        # maakt. Een game op de nep-NAS (TC5) krijgt dan een view en zijn drop-ins kunnen geplaatst worden.
        voor = set(os.listdir(views))
        for name in profiles:
            got[name]["start"] = norm(cpp_side(a.rg_direct, name, tp, patches, user, env, ["--make-view"]))
        got_views = {v: boom(os.path.join(views, v), norm) for v in sorted(set(os.listdir(views)) - voor)}

        if a.update:
            os.makedirs(a.golden, exist_ok=True)
            for f in os.listdir(a.golden):
                if f.endswith(".json") and not f.startswith("_") and f != "jvs.json" and f[:-5] not in got:
                    os.unlink(os.path.join(a.golden, f))
                    print("weg: %s (geen profiel meer)" % f)
            for name, r in got.items():
                dump(os.path.join(a.golden, name + ".json"), r)
            dump(os.path.join(a.golden, "_views.json"), got_views)
            print("vastgelegd: %d profielen en %d nieuwe views in %s (jvs.json blijft)" % (len(got), len(got_views), a.golden))
            return 0

        fails = 0
        print("directe route tegen de gouden plannen: %d games met [Launch] Mode = direct (geforceerd: %s)"
              % (len(profiles), ", ".join(n for n in FORCE_DIRECT if n in profiles) or "geen"))
        for name in profiles:
            gpath = os.path.join(a.golden, name + ".json")
            if not os.path.isfile(gpath):
                fails += 1
                print("  %-26s GEEN GOUDEN BESTAND (nieuw profiel? leg vast met --update en lees het na)  <-- FOUT" % name)
                continue
            with open(gpath) as f:
                want = json.load(f)
            d = diff(want, got[name])
            fails += bool(d)
            r = got[name]["plan"]
            print("  %-26s %-8s %s%s" % (name, r["verdict"], "gelijk" if not d else "VERSCHILT  <-- FOUT",
                                          ("  [" + (r.get("why") or "")[:70] + "]") if r["verdict"] != "ok" else ""))
            for x in d[:40] if (d or a.verbose) else []:
                print("      " + x)
        weg = sorted(f[:-5] for f in os.listdir(a.golden)
                     if f.endswith(".json") and not f.startswith("_") and f != "jvs.json" and f[:-5] not in got)
        if weg:
            fails += 1
            print("gouden bestanden zonder profiel met [Launch] Mode = direct: %s  <-- FOUT" % ", ".join(weg))
        # [Launch] ViewWritable werkt alleen op de directe route (pass 2): een profiel met schrijfmappen en een native.json of
        # unity.json, of zonder Mode = direct, weigert de daemon; dat hoort hier al op te vallen.
        for name in gs.list_games():
            g = gs.load_game(name)
            if not (g.get("Launch", "ViewWritable", "") or "").strip():
                continue
            eigen = [f for f in ("native.json", "unity.json") if os.path.isfile(os.path.join(patches, name, f))]
            if g.launch_mode != "direct" or eigen:
                fails += 1
                print("  %-26s [Launch] ViewWritable zonder de directe route (Mode = %s%s)  <-- FOUT"
                      % (name, g.launch_mode, (", " + ", ".join(eigen)) if eigen else ""))
        with open(os.path.join(a.golden, "_views.json")) as f:
            want_views = json.load(f)
        dv = diff(want_views, got_views)
        fails += bool(dv)
        print("views gemaakt bij een start: %s (%s)" % (", ".join(got_views) or "geen", "gelijk" if not dv else "VERSCHILT  <-- FOUT"))
        for x in dv[:40]:
            print("    " + x)
        n, bad = jvs_check(a.rg_direct, a.golden)
        print("JVS-pakketten: %d gevallen, %d verschillen" % (n, len(bad)))
        for x in bad[:10]:
            print("   ", x)
        fails += tp_vrij_check(a.rg_direct, patches, gs, tmp)
        return 1 if fails or bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

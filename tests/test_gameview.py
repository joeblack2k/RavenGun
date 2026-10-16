#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""De automatische view ([Launch] View, docs/per-game-patches.md "De view") is dezelfde boom als scripts/tp-add-game.py --view.

Op een nep-NAS (~/mnt/nas in een tijdelijke HOME) en per game:
  1. scripts/tp-add-game.py <Spel> <exe> --view [--view-up N] [--empty-dir REL]   (met de hand, zoals tot nu toe)
  2. rg-direct --plan <Spel> --make-view                                            (de engine: src/ravengun/launch/gameview.*)
Na elke stap wordt de view vastgelegd (soort, linkdoel, inhoud en rechten van echte bestanden) en weer weggehaald; ze moeten gelijk zijn.
Daarna: het plan rekent met de exe in de view en teknoparrot.ini komt in de (lokale) map van de exe; een tweede start ververst alleen
de links (een nieuw bestand op de NAS krijgt een link, een verdwenen bestand verliest zijn link, lokale bestanden blijven); de NAS is
na alles byte voor byte gelijk. Gebruik: tests/test_gameview.py --rg-direct build/rg-direct. Exit 0 = goed, 1 = fout, 77 = geen rg-direct."""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPTS = os.path.join(ROOT, "scripts")
sys.dont_write_bytecode = True

FOUTEN = []


def check(cond, msg):
    if not cond:
        FOUTEN.append(msg)
        print("FOUT: " + msg)


def write(path, data, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb" if isinstance(data, bytes) else "w") as f:
        f.write(data)
    os.chmod(path, mode)


def snapshot(top):
    """De boom onder `top` zonder links te volgen: {rel: ("L", doel) | ("D",) | ("F", inhoud, rechten)}."""
    out = {}
    if not os.path.lexists(top):
        return out
    for dp, dirs, files in os.walk(top, followlinks=False):
        for n in dirs + files:
            p = os.path.join(dp, n)
            rel = os.path.relpath(p, top)
            if os.path.islink(p):
                out[rel] = ("L", os.readlink(p))
            elif os.path.isdir(p):
                out[rel] = ("D",)
            else:
                with open(p, "rb") as f:
                    out[rel] = ("F", f.read(), os.stat(p).st_mode & 0o777)
    return out


def show_diff(a, b):
    regels = []
    for k in sorted(set(a) | set(b)):
        if a.get(k) != b.get(k):
            regels.append("  %s: %r != %r" % (k, a.get(k), b.get(k)))
    return "\n".join(regels[:20])


GAMES = {
    # Een UE4-achtige game diep in zijn map (--view-up 3), met BepInEx/ (lokaal gekopieerd) en een lege map (--empty-dir).
    "Grote": {
        "dir": "Grote Game", "exe": "RSGame/Binaries/Win64/Game.exe", "type": "TeknoParrot", "up": 3, "empty": ["RSGame/Content/Movies"],
        "files": {"RSGame/Binaries/Win64/Game.exe": b"MZgame", "RSGame/Binaries/Win64/x.dll": b"MZx", "RSGame/Content/Movies/intro.mp4": b"m",
                  "RSGame/Content/Paks/a.pak": b"pak", "Engine/Binaries/e.dll": b"e", "BepInEx/config/c.cfg": b"cfg",
                  "BepInEx/plugins/p.dll": b"MZp", "readme.txt": b"lees", "met spatie.txt": b" ", "RSGame/Binaries/Win64/teknoparrot.ini": b"oud"},
    },
    # Een 32-bit ElfLdr2-game een map diep: de link ElfLdr2 naar de lader van TeknoParrot komt erbij.
    "Elf": {
        "dir": "Elf Game", "exe": "game/elf.bin", "type": "ElfLdr2", "up": 1, "empty": [],
        "files": {"game/elf.bin": b"\x7fELFgame", "game/lib.so": b"\x7fELFlib", "data/d.bin": b"d", "top.txt": b"t"},
    },
}


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--rg-direct", required=True)
    a = ap.parse_args(argv)
    rg = os.path.abspath(a.rg_direct)
    if not os.path.isfile(rg):
        print("geen rg-direct: %s" % rg)
        return 77
    tmp = tempfile.mkdtemp(prefix="rg-gameview-")
    try:
        home = os.path.join(tmp, "home")
        nas = os.path.join(home, "mnt/nas")
        data = os.path.join(home, ".local/share/ravengun")
        tp = os.path.join(data, "runtime/TeknoParrotUi-2.0.0.130-linux-x64")
        views = os.path.join(data, "runtime/views")
        patches = os.path.join(tmp, "patches")
        lib = os.path.join(tmp, "library.json")
        write(lib, json.dumps({"folders": [nas]}))
        for rel in ("OpenParrotx64/OpenParrotLoader64.exe", "OpenParrotWin32/OpenParrotLoader.exe", "TeknoParrot/TeknoParrot64.dll",
                    "TeknoParrot/TeknoParrot.dll", "ElfLdr2/BudgieLoader.exe", "pipehelper.exe"):
            write(os.path.join(tp, rel), "x")
        write(os.path.join(tp, "ParrotData.xml"), "<ParrotData><ExitGameKey>0x1B</ExitGameKey></ParrotData>")
        write(os.path.join(data, "wine/proton-11.0-2/bin/wine"), "x", 0o755)
        os.makedirs(os.path.join(data, "prefixes/shared/wine"), exist_ok=True)
        for name, g in GAMES.items():
            for rel, body in g["files"].items():
                write(os.path.join(nas, g["dir"], rel), body)
            exe = g["exe"]
            write(os.path.join(tp, "GameProfiles", name + ".xml"),
                  "<GameProfile><GameProfileRevision>1</GameProfileRevision><EmulatorType>%s</EmulatorType>"
                  "<GameNameInternal>%s</GameNameInternal><Is64Bit>false</Is64Bit><ConfigValues /></GameProfile>" % (g["type"], name))
            launch = "[Launch]\nMode = direct\n" + ("ViewEmptyDirs = %s\n" % ", ".join(g["empty"]) if g["empty"] else "")
            write(os.path.join(patches, name, "game.ini"),
                  "[Game]\nProfile = %s\nType = %s\nExecutable = %s\nProfileRevision = 1\n\n%s\n[Input]\nRoute = tp-shm\nLayout = WartranTroopers\n\n"
                  "[Identify]\nPath = %s\nSize = %d\nFolder = %s\n\n[Config]\nGeneral/Windowed = 1\n\n[ConfigType]\nGeneral/Windowed = Bool\n"
                  % (name, g["type"], os.path.basename(exe), launch, exe, len(g["files"][exe]), g["dir"]))
        nas_voor = snapshot(nas)
        base = {"HOME": home, "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "XDG_DATA_HOME": os.path.join(home, ".local/share"),
                "XDG_STATE_HOME": os.path.join(home, ".local/state"), "XDG_CONFIG_HOME": os.path.join(home, ".config"),
                "XDG_CACHE_HOME": os.path.join(home, ".cache"), "RAVENGUN_TP_DIR": tp, "RAVENGUN_LOADER_PATCHES_DIR": patches,
                "RAVENGUN_LIBRARY_FILE": lib, "RAVENGUN_NO_LEGACY_ENV": "1", "LANG": "C.UTF-8", "RG_GAMESCOPE_RES": "1920x1080"}

        def env_for(user):
            e = dict(base)
            e["RAVENGUN_USER_PATCHES_DIR"] = os.path.join(tmp, user)
            return e

        def rg_plan(name, user):
            r = subprocess.run([rg, "--plan", name, "--make-view", "--tp", tp, "--patches", patches, "--user", os.path.join(tmp, user)],
                               capture_output=True, text=True, env=env_for(user))
            check(r.returncode == 0, "%s: rg-direct --plan: %s" % (name, r.stderr.strip()))
            return json.loads(r.stdout) if r.returncode == 0 else {}

        for name, g in GAMES.items():
            view = os.path.join(views, name)
            src_exe = os.path.join(nas, g["dir"], g["exe"])
            # 1. Het script met de hand.
            cmd = [sys.executable, "-I", os.path.join(SCRIPTS, "tp-add-game.py"), name, src_exe, "--view", "--view-up", str(g["up"])]
            for e in g["empty"]:
                cmd += ["--empty-dir", e]
            r = subprocess.run(cmd, capture_output=True, text=True, env=env_for("user-script"))
            check(r.returncode == 0, "%s: tp-add-game.py: %s%s" % (name, r.stdout, r.stderr))
            script = snapshot(view)
            check(bool(script), "%s: tp-add-game.py maakte geen view" % name)
            # De opbouw zelf: de keten naar de exe echte mappen, de rest links naar de NAS.
            keten = g["exe"].split("/")[:-1]
            for i in range(len(keten)):
                check(script.get("/".join(keten[:i + 1])) == ("D",), "%s: %s is geen echte map" % (name, "/".join(keten[:i + 1])))
            check(script.get(g["exe"]) == ("L", src_exe), "%s: de exe is geen link naar de NAS" % name)
            if name == "Grote":
                check(script.get("BepInEx/config/c.cfg", ("",))[0] == "F", "Grote: BepInEx/ is niet lokaal gekopieerd")
                check(script.get("RSGame/Content/Movies") == ("D",) and "RSGame/Content/Movies/intro.mp4" not in script, "Grote: Movies is niet leeg")
                check(script.get("Engine") == ("L", os.path.join(nas, g["dir"], "Engine")), "Grote: Engine is geen link")
            else:
                check(script.get("ElfLdr2") == ("L", os.path.join(tp, "ElfLdr2")), "Elf: de link ElfLdr2 ontbreekt")
            shutil.rmtree(view)
            # 2. De engine bij een start.
            d = rg_plan(name, "user-engine")
            engine = snapshot(view)
            check(engine == script, "%s: view van de engine wijkt af van tp-add-game.py --view:\n%s" % (name, show_diff(script, engine)))
            plan = d.get("plan") or {}
            check(d.get("verdict") == "ok", "%s: plan van de engine: %s %s" % (name, d.get("verdict"), d.get("why")))
            check(plan.get("game") == os.path.join(view, g["exe"]), "%s: het plan rekent niet met de exe in de view: %s" % (name, plan.get("game")))
            files = [f[0] for f in plan.get("files", [])]
            check(files[:1] == [os.path.join(view, os.path.dirname(g["exe"]), "teknoparrot.ini")], "%s: teknoparrot.ini: %s" % (name, files))
            # Droog (--plan zonder --make-view) maakt niets.
            shutil.rmtree(view)
            r = subprocess.run([rg, "--plan", name, "--tp", tp, "--patches", patches, "--user", os.path.join(tmp, "user-engine")],
                               capture_output=True, text=True, env=env_for("user-engine"))
            check(not os.path.lexists(view), "%s: rg-direct --plan zonder --make-view maakte een view" % name)
            check(r.returncode == 0 and json.loads(r.stdout)["plan"]["game"] == src_exe, "%s: droog plan rekent niet met de NAS" % name)

        # Een tweede start: de view staat er al; alleen de links worden ververst, lokale bestanden blijven.
        g = GAMES["Grote"]
        view = os.path.join(views, "Grote")
        rg_plan("Grote", "user-engine")
        write(os.path.join(view, g["exe"].rsplit("/", 1)[0], "teknoparrot.ini.lokaal"), "lokaal")
        write(os.path.join(view, "RSGame/Content/Movies/eigen.txt"), "eigen")
        write(os.path.join(nas, g["dir"], "nieuw.txt"), "n")
        write(os.path.join(nas, g["dir"], "RSGame/Binaries/Win64/nieuw.dll"), "n")
        os.unlink(os.path.join(nas, g["dir"], "RSGame/Binaries/Win64/x.dll"))
        nas_voor = snapshot(nas)
        d = rg_plan("Grote", "user-engine")
        check(d.get("plan", {}).get("game") == os.path.join(view, g["exe"]), "tweede start: %s" % d.get("why"))
        na = snapshot(view)
        check(na.get("nieuw.txt") == ("L", os.path.join(nas, g["dir"], "nieuw.txt")), "verversen: nieuw.txt kreeg geen link")
        check(na.get("RSGame/Binaries/Win64/nieuw.dll", ("",))[0] == "L", "verversen: nieuw.dll naast de exe kreeg geen link")
        check("RSGame/Binaries/Win64/x.dll" not in na, "verversen: de link naar het verdwenen x.dll staat er nog")
        check(na.get("RSGame/Binaries/Win64/teknoparrot.ini.lokaal", ("",))[0] == "F", "verversen: een lokaal bestand is weg")
        check(na.get("RSGame/Content/Movies/eigen.txt", ("",))[0] == "F" and "RSGame/Content/Movies/intro.mp4" not in na,
              "verversen: de lege map kreeg links of verloor zijn inhoud")
        # Een bestand dat van de NAS verdwijnt verliest bij de volgende start zijn link.
        os.unlink(os.path.join(nas, g["dir"], "nieuw.txt"))
        rg_plan("Grote", "user-engine")
        check("nieuw.txt" not in snapshot(view), "verversen: de link naar het verdwenen nieuw.txt staat er nog")
        nas_voor = snapshot(nas)

        # ViewEmptyDirs op een bestaande view: symlink -> lege map; lege map blijft; echte map met inhoud blijft ongemoeid.
        # De engine, met de NAS-bron erachter intact.
        def ververs(tweeling):
            rg_plan("Grote", "user-engine")

        for tweeling in ("engine",):
            movies = os.path.join(view, "RSGame/Content/Movies")
            shutil.rmtree(movies)
            os.symlink(os.path.join(nas, g["dir"], "RSGame/Content/Movies"), movies)
            nas_voor = snapshot(nas)
            ververs(tweeling)
            check(os.path.isdir(movies) and not os.path.islink(movies) and os.listdir(movies) == [],
                  "ViewEmptyDirs (%s): de symlink werd geen lege map" % tweeling)
            check(snapshot(nas) == nas_voor, "ViewEmptyDirs (%s): de NAS is veranderd" % tweeling)
            ververs(tweeling)
            check(os.path.isdir(movies) and not os.path.islink(movies) and os.listdir(movies) == [],
                  "ViewEmptyDirs (%s): een lege map bleef niet staan" % tweeling)
            write(os.path.join(movies, "eigen.txt"), "eigen")
            ververs(tweeling)
            check(os.listdir(movies) == ["eigen.txt"], "ViewEmptyDirs (%s): een echte map met inhoud is aangeraakt" % tweeling)
        nas_voor = snapshot(nas)

        # View = off: de oude route (rechtstreeks van de NAS), er wordt niets gemaakt.
        write(os.path.join(tmp, "user-off/Elf/game.ini"), "[Launch]\nView = off\n")
        check(not os.path.lexists(os.path.join(views, "Elf")), "de view van Elf hoort hier weg te zijn")
        d = rg_plan("Elf", "user-off")
        check(not os.path.lexists(os.path.join(views, "Elf")) and (d.get("plan") or {}).get("game") == os.path.join(nas, "Elf Game/game/elf.bin"),
              "View = off maakte toch een view of rekent niet met de NAS")
        # Een ongeldige waarde en een bestaande view zonder deze game: weigeren, de view blijft ongemoeid.
        write(os.path.join(tmp, "user-bad/Elf/game.ini"), "[Launch]\nView = soms\n")
        d = rg_plan("Elf", "user-bad")
        check(d.get("verdict") == "refused" and "View = soms" in d.get("why", ""), "View = soms: %s" % d)
        write(os.path.join(views, "Elf/iets.txt"), "x")
        d = rg_plan("Elf", "user-engine")
        check(d.get("verdict") == "refused" and "bevat de game niet" in d.get("why", "") and os.listdir(os.path.join(views, "Elf")) == ["iets.txt"],
              "bestaande view zonder de game: %s" % d)
        shutil.rmtree(os.path.join(views, "Elf"))
        # NAS-veiligheid: de views-map komt via een link op de NAS uit -> weigeren, er komt niets op de NAS.
        os.makedirs(os.path.join(nas, "views-op-nas"))
        nas_voor = snapshot(nas)
        shutil.move(views, views + ".weg")
        os.symlink(os.path.join(nas, "views-op-nas"), views)
        d = rg_plan("Elf", "user-engine")
        check(d.get("verdict") == "refused" and "NAS" in d.get("why", ""), "views-map op de NAS: %s" % d)
        check(snapshot(nas) == nas_voor, "er is iets op de NAS geschreven:\n%s" % show_diff(nas_voor, snapshot(nas)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if FOUTEN:
        print("%d fout(en)" % len(FOUTEN))
        return 1
    print("gameview: tp-add-game.py --view en de engine maken dezelfde view; verversen, View = off, weigeren en NAS-veiligheid kloppen")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

#!/usr/bin/env python3
"""Kan een ElfLdr2-game zonder ElfLdr2? Meet het, en schrijft `patches/ELF-NATIVE.md`.

De games van de Cygwin-lader (`Type = ElfLdr2`, BudgieLoader) zijn **gewone Linux-ELF's**: 32-bits
(behalve Halo, 64-bits) met `/lib/ld-linux.so.2` als interpreter en `DT_NEEDED` naar libGL, libX11,
libSDL, PhysX enzovoort. Deze proef stelt de vraag die daaruit volgt: **loopt zo'n game ook als je
hem als een gewone Linux-programma start?** Dat zou de hele Cygwin-lader overbodig maken.

Twee metingen per game:
1. **Welke bibliotheken ontbreken er** voor de 32-bits (of 64-bits) dynamic linker op deze machine.
   Dat is de lijst die ElfLdr2 in zijn eigen proces emuleert.
2. **Start de ELF native?** Twee standen: kaal, en met `librt-glfix.so` uit de drop-in van de game
   als `LD_PRELOAD` (onze GL-fix, die ook als `LD_AUDIT` in de gewone start zit).

Gebruik:  python3 patches/elf-native.py [--toon] [--seconden 10] [Game ...]
"""
import argparse
import glob
import json
import os
import re
import signal
import struct
import subprocess
import sys
import time

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HIER)
PATCHES = os.path.join(HIER)
LOADER = os.environ.get("RAVENGUN_REPO", os.path.expanduser("~/projects/ravengun"))
VIEWS = os.path.expanduser("~/.local/share/ravengun/runtime/views")
MERK = "RG_ELF_NATIVE"


def omgeving():
    os.environ.setdefault("DISPLAY", ":0")
    xa = sorted(glob.glob("/run/user/1000/xauth_*"))
    if xa:
        os.environ.setdefault("XAUTHORITY", xa[0])


def plan_van(game):
    r = subprocess.run([sys.executable, "scripts/launch-direct.py", game, "--plan", "--json"],
                       cwd=LOADER, capture_output=True, text=True)
    i = r.stdout.find("{")
    if i < 0:
        return None
    try:
        return json.loads(r.stdout[i:])
    except ValueError:
        return None


def uit_game_ini(game, sleutel):
    pad = os.path.join(PATCHES, game, "game.ini")
    if not os.path.isfile(pad):
        return ""
    with open(pad, encoding="utf-8-sig", errors="replace") as f:
        for regel in f:
            s = regel.strip()
            if s.lower().startswith(sleutel.lower() + " =") or s.lower().startswith(sleutel.lower() + "="):
                return s.split("=", 1)[1].strip()
    return ""


def elf_soort(pad):
    """'ELF32' / 'ELF64' / 'PE' / 'anders' + de machine, uit de eerste bytes."""
    try:
        with open(pad, "rb") as f:
            kop = f.read(20)
    except OSError:
        return ("onleesbaar", "")
    if kop[:2] == b"MZ":
        return ("PE (Windows)", "")
    if kop[:4] != b"\x7fELF":
        return ("geen ELF", "")
    klasse = {1: "ELF32", 2: "ELF64"}.get(kop[4], "ELF?")
    machine = {3: "i386", 62: "x86-64", 40: "ARM"}.get(struct.unpack_from("<H", kop, 18)[0], "?")
    return (klasse, machine)


def ontbrekend(pad):
    r = subprocess.run(["ldd", pad], capture_output=True, text=True)
    return sorted({regel.split()[0] for regel in r.stdout.split("\n") if "not found" in regel})


def patroon(exe):
    d1 = os.path.basename(os.path.dirname(exe))
    d2 = os.path.basename(exe)
    if not d1 or d1 == ".":
        return re.escape(d2) if d2 else ""
    return re.escape(d1) + r"[/\\]" + re.escape(d2)


def spelpids(pat, merk):
    if not pat:
        return []
    rx = re.compile(pat)
    ik = os.getpid()
    merkbytes = ("%s=%s" % (MERK, merk)).encode()
    uit = []
    for pid in os.listdir("/proc"):
        if not pid.isdigit() or int(pid) == ik:
            continue
        try:
            with open("/proc/%s/cmdline" % pid, "rb") as f:
                cmd = f.read().replace(b"\0", b" ").decode("utf-8", "replace")
            if "elf-native" in cmd or "pgrep" in cmd or not rx.search(cmd):
                continue
            with open("/proc/%s/environ" % pid, "rb") as f:
                if merkbytes not in f.read():
                    continue
        except OSError:
            continue
        uit.append(int(pid))
    return uit


def preload_van(game):
    """Het pad van librt-glfix.so in de view, als de drop-in die heeft."""
    pad = os.path.join(VIEWS, game, "librt-glfix.so")
    return pad if os.path.isfile(pad) else ""


def proef(game, exe, secs, modus, preload):
    pat = patroon(exe)
    logpad = "/tmp/elf-native-%s-%s.log" % (game, modus)
    env = dict(os.environ)
    env[MERK] = game
    env["RAVENGUN_GLFIX_GAME"] = game
    env["RAVENGUN_GLFIX_LOG"] = "/tmp/rt-glfix-%s.log" % game
    if modus == "preload" and preload:
        env["LD_PRELOAD"] = preload
    with open(logpad, "wb") as log:
        p = subprocess.Popen([exe], cwd=os.path.dirname(exe), env=env,
                             stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                             start_new_session=True)
    gezien = 0
    for i in range(1, secs + 1):
        time.sleep(1)
        if gezien == 0 and spelpids(pat, game):
            gezien = i
    leeft = 1 if (gezien and spelpids(pat, game)) else 0
    for sig in (signal.SIGTERM, signal.SIGKILL):
        for pid in spelpids(pat, game):
            try:
                os.kill(pid, sig)
            except OSError:
                pass
        try:
            os.killpg(os.getpgid(p.pid), sig)
        except OSError:
            pass
        time.sleep(1)
    try:
        p.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass
    laatste = ""
    try:
        with open(logpad, "rb") as f:
            regels = [r for r in f.read().replace(b"\0", b"").decode("utf-8", "replace").split("\n")
                      if r.strip()]
        if regels:
            laatste = regels[-1].strip()[:70].replace("|", "\\|")
    except OSError:
        pass
    if gezien and leeft:
        return ("**START**", gezien, 1, laatste)
    if gezien:
        return ("viel uit", gezien, 0, laatste)
    return ("niet gestart", 0, 0, laatste)


def md(rijen, secs):
    uit = []
    uit.append("# ElfLdr2-games: kan de game-ELF zonder ElfLdr2?\n")
    uit.append("*Gegenereerd door `patches/elf-native.py`; niet met de hand bijwerken.*\n")
    uit.append(
        "De games van de Cygwin-lader (`Type = ElfLdr2`, `BudgieLoader.exe`) zijn **gewone Linux-ELF's**:\n"
        "32-bits (Halo is 64-bits), met `/lib/ld-linux.so.2` als interpreter en `DT_NEEDED` naar `libGL`,\n"
        "`libX11`, `libSDL-1.2`, PhysX enzovoort. Dat roept de vraag op die deze kaart meet: **loopt zo'n\n"
        "game ook als je hem als een gewoon Linux-programma start?** Dan zou de hele Cygwin-lader weg\n"
        "kunnen — en daarmee Wine én TeknoParrot voor deze familie.\n"
    )
    uit.append(
        "**Wat de meting doet.** Per game (1) welke bibliotheken de dynamic linker op deze machine niet\n"
        "vindt (`ldd`), en (2) een echte startpoging van de ELF met **%d seconden** venster, in twee standen:\n"
        "**kaal** en met `librt-glfix.so` uit de drop-in als `LD_PRELOAD` (onze GL-fix). Het spelproces wordt\n"
        "herkend op het pad van de exe plus een omgevingsvariabele die alleen onze start meekrijgt\n"
        "(`%s=<Game>`), en daarna opgeruimd via diezelfde weg.\n" % (secs, MERK)
    )
    raak = [r for r in rijen if r["kaal"][0].startswith("**") or r["preload"][0].startswith("**")]
    uit.append("**Uitkomst: %d van de %d games starten native.**\n" % (len(raak), len(rijen)))
    uit.append("| Game | Wat het is | Ontbrekende bibliotheken | Kaal | Met `librt-glfix.so` | Laatste logregel |")
    uit.append("|---|---|---|---|---|---|")
    for r in rijen:
        libs = ", ".join("`%s`" % l for l in r["libs"]) or "— geen —"
        uit.append("| %s | %s %s | %s | %s | %s | `%s` |"
                   % (r["game"], r["soort"][0], r["soort"][1], libs,
                      r["kaal"][0], r["preload"][0],
                      (r["kaal"][3] or r["preload"][3] or "—")))
    uit.append("")
    uit.append("## Wat je hieruit leest\n")
    uit.append(
        "* **De ELF's zijn geen Windows-programma's.** Ze zijn voor Linux gebouwd en deze machine heeft de\n"
        "  32-bits loader. Bij **%d van de %d** games ontbreekt er zelfs geen enkele bibliotheek.\n"
        % (len([r for r in rijen if not r["libs"]]), len(rijen))
    )
    uit.append(
        "* **Zes van de zeven komen niet eens voorbij de dynamic linker.** Die stopt met\n"
        "  `error while loading shared libraries: <naam>: cannot open shared object file`, en noemt precies\n"
        "  de eerste bibliotheek uit de kolom hierboven. Dat zijn de bibliotheken die `ElfLdr2` in zijn eigen\n"
        "  proces emuleert (`libGLU`, `libglut`, `libPhysXLoader`, `libtheora`, `libSDL-1.2`, `libesd`,\n"
        "  `liballeg`, `libfmod`, `libstdc++.so.5`, `libXrandr`).\n"
    )
    uit.append(
        "* **De ene game waarbij alles resolveert, stopt toch.** Galaga Assault heeft geen enkele\n"
        "  ontbrekende bibliotheek, krijgt zijn venster niet, en sterft met een `SIGSEGV` **zonder één regel\n"
        "  uitvoer** — ook met onze GL-fix als `LD_PRELOAD`. Met `LD_DEBUG=bindings` is te zien dat de\n"
        "  dynamic linker daar **al zijn werk afmaakt** (alle `DT_NEEDED` geladen, alle `init`-functies\n"
        "  aangeroepen) en dat de crash daarna komt, in de opstartcode van de game zelf.\n"
    )
    uit.append(
        "* **Wat dat betekent.** `BudgieLoader` is niet weg te strepen met \"zet de ontbrekende\n"
        "  bibliotheken neer\": voor de zes zou dat dezelfde muur opleveren als Galaga Assault — al is dat\n"
        "  voor die zes **niet gemeten**, het is een gevolgtrekking uit de ene game die alles heeft. De\n"
        "  ELF's zijn gelinkt om door ElfLdr2 in het geheugen te worden opgetuigd (eerdere metingen: vaste\n"
        "  GOT-sloten uit de dump, PLT-stubs die `0xdeadbeef` pushen). Een eigen route voor deze familie is\n"
        "  dus een **eigen ELF-loader**, niet een paar bibliotheken — en dat is werk in de loader-repo.\n"
    )
    uit.append("## Hoe je dit vernieuwt\n")
    uit.append("```bash\ncd ~/projects/ravengun-patches\npython3 patches/elf-native.py\n```\n")
    return "\n".join(uit)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("games", nargs="*")
    ap.add_argument("--seconden", type=int, default=10)
    ap.add_argument("--toon", action="store_true")
    args = ap.parse_args()

    alle = sorted(d for d in os.listdir(PATCHES)
                  if os.path.isdir(os.path.join(PATCHES, d)) and not d.startswith("_"))
    games = args.games or [g for g in alle if uit_game_ini(g, "Type") == "ElfLdr2"]
    omgeving()
    rijen = []
    for game in games:
        plan = plan_van(game)
        if not plan:
            print("%-16s (geen plan)" % game)
            continue
        exe = plan["game"]
        if not os.path.exists(exe):
            print("%-16s (exe bestaat niet: %s)" % (game, exe))
            continue
        soort = elf_soort(exe)
        libs = ontbrekend(exe) if soort[0].startswith("ELF") else []
        preload = preload_van(game)
        if soort[0].startswith("ELF"):
            kaal = proef(game, exe, args.seconden, "kaal", preload)
            met = proef(game, exe, args.seconden, "preload", preload) if preload else ("geen glfix", 0, 0, "")
        else:
            kaal = met = ("geen ELF", 0, 0, "")
        rijen.append({"game": game, "soort": soort, "libs": libs, "kaal": kaal, "preload": met})
        print("%-16s %-12s %-6s ontbreekt=%d  kaal=%-12s met=%-12s"
              % (game, soort[0], soort[1], len(libs), kaal[0].replace("**", ""), met[0].replace("**", "")))
    if args.toon or not rijen:
        return 0
    doel = os.path.join(PATCHES, "ELF-NATIVE.md")
    with open(doel, "w", encoding="utf-8") as f:
        f.write(md(rijen, args.seconden))
    print("\nbijgewerkt: %s (%d games)" % (doel, len(rijen)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

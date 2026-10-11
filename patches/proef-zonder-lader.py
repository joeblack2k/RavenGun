#!/usr/bin/env python3
"""Start elke game met ALLEEN wine en de exe, en schrijft `patches/ZONDER-LADER.md`.

Geen OpenParrotLoader, geen BudgieLoader, geen TeknoParrot-kern, geen drop-in en geen
patches: alleen `wine <exe>` in dezelfde prefix en dezelfde werkmap als de directe
start. Dat is de scherpste vraag die je over "loskoppelen van TeknoParrot" kunt
stellen: blijft de game op zichzelf staan?

**Wat deze proef wel zegt:** of het spelproces start en na N seconden nog leeft.
**Wat hij niet zegt:** of de game speelbaar is. Besturing, JVS-bord, dongle, regio en
soms het beeld gaan via de lader die we hier juist weglaten. "START" betekent dus
"deze game heeft de lader niet nodig om te blijven draaien", niet "deze game is af".

Gebruik:
    python3 patches/proef-zonder-lader.py                 # alle games, 20 s per game
    python3 patches/proef-zonder-lader.py --seconden 30
    python3 patches/proef-zonder-lader.py --toon LGI LGS  # alleen naar het scherm
"""
import argparse
import glob
import json
import os
import re
import signal
import subprocess
import sys
import time

HIER = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HIER)
PATCHES = os.path.join(REPO, "patches")
LOADER = os.environ.get("RAVENGUN_REPO", os.path.expanduser("~/projects/ravengun"))
MERK = "RG_PROEF_ZONDER_LADER"
VIEWS = os.path.expanduser("~/.local/share/ravengun/runtime/views")


def dropin_hashes(game):
    """sha256 van elk bestand in patches/<Game>/dropin/ (zonder dropin.ini), per bestandsnaam."""
    basis = os.path.join(PATCHES, game, "dropin")
    uit = {}
    if not os.path.isdir(basis):
        return uit
    import hashlib
    for map_, _, bestanden in os.walk(basis):
        for b in bestanden:
            if b == "dropin.ini":
                continue
            try:
                with open(os.path.join(map_, b), "rb") as f:
                    uit.setdefault(b, set()).add(hashlib.sha256(f.read()).hexdigest())
            except OSError:
                pass
    return uit


def in_view(game):
    """(aanwezig, totaal): hoeveel drop-in-bestanden van deze game staan (nog) in zijn view?

    De launcher zet de drop-in bij elke start in de view en haalt hem daarna niet weg. Een
    proef zonder lader is dus niet automatisch een proef zonder patch: wat een eerdere start
    heeft achtergelaten, laadt de game nog steeds. Daarom meet dit per game hoeveel het er zijn.

    Alleen bestanden met dezelfde naam als een drop-in-bestand worden gehasht; een
    gematerialiseerde view kan anders honderden megabytes zijn.
    """
    import hashlib
    wil = dropin_hashes(game)
    if not wil:
        return (0, 0)
    wortel = os.path.join(VIEWS, game)
    if not os.path.isdir(wortel):
        return (0, len(wil))
    gevonden = set()
    for map_, _, bestanden in os.walk(wortel):
        for b in bestanden:
            if b not in wil:
                continue
            pad = os.path.join(map_, b)
            try:
                if os.path.islink(pad) or not os.path.isfile(pad):
                    continue
                with open(pad, "rb") as f:
                    if hashlib.sha256(f.read()).hexdigest() in wil[b]:
                        gevonden.add(b)
            except OSError:
                pass
    return (len(gevonden), len(wil))


def omgeving():
    os.environ.setdefault("DISPLAY", ":0")
    xa = sorted(glob.glob("/run/user/1000/xauth_*"))
    if xa:
        os.environ.setdefault("XAUTHORITY", xa[0])


def plan_van(game):
    r = subprocess.run([sys.executable, "scripts/launch-direct.py", game, "--plan", "--json"],
                       cwd=LOADER, capture_output=True, text=True)
    t = r.stdout
    i = t.find("{")
    if i < 0:
        return None
    try:
        return json.loads(t[i:])
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


def patroon(exe):
    """De laatste twee pad delen, in beide schrijfwijzen -- net als starttest3.sh."""
    d1 = os.path.basename(os.path.dirname(exe))
    d2 = os.path.basename(exe)
    if not d1 or d1 == ".":
        return re.escape(d2) if d2 else ""
    return re.escape(d1) + r"[/\\]" + re.escape(d2)


def spelpids(pat, merk):
    """PIDs met dit pad in de commandoregel EN ons merk in de omgeving.

    Dat merk is er zodat een game die iemand anders tegelijk start niet als de onze
    telt: alleen processen die van onze eigen Popen afstammen hebben die variabele.
    """
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
            if "launch-direct" in cmd or "proef-zonder-lader" in cmd or "pgrep" in cmd:
                continue
            if not rx.search(cmd):
                continue
            with open("/proc/%s/environ" % pid, "rb") as f:
                if merkbytes not in f.read():
                    continue
        except OSError:
            continue
        uit.append(int(pid))
    return uit


def proef(game, secs, rijen):
    plan = plan_van(game)
    profiel = uit_game_ini(game, "EmulationProfile")
    soort = uit_game_ini(game, "Type")
    aanwezig, totaal = in_view(game)
    if not plan:
        rijen.append((game, "geen plan", 0, 0, "de directe start kan deze game niet plannen", profiel, soort,
                      (aanwezig, totaal)))
        return
    exe = plan["game"]
    pat = patroon(exe)
    logpad = "/tmp/rg-zonder-%s.log" % game
    env = dict(os.environ)
    env.update(plan.get("launch_env") or {})
    env.update(plan.get("profile_env") or {})
    env["RG_WINEPREFIX"] = plan["prefix"]
    env[MERK] = game
    with open(logpad, "wb") as log:
        p = subprocess.Popen([plan["wine"], exe], cwd=plan["cwd"], env=env,
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
    if gezien and leeft:
        res = "**START**"
    elif gezien:
        res = "viel uit"
    else:
        res = "niet gestart"
    laatste = ""
    try:
        with open(logpad, "rb") as f:
            regels = [r for r in f.read().replace(b"\0", b"").decode("utf-8", "replace").split("\n")
                      if r.strip()]
        if regels:
            laatste = regels[-1].strip()[:70].replace("|", "\\|")
    except OSError:
        pass
    rijen.append((game, res, gezien, leeft, laatste, profiel, soort, (aanwezig, totaal)))
    print("%-26s %-12s %-6s %-6s %s" % (game, res.replace("**", ""), gezien or "-", leeft, laatste))


def md(rijen, secs):
    start = [r for r in rijen if r[1].startswith("**")]
    uit = []
    uit.append("# Zonder lader: welke games starten op zichzelf?\n")
    uit.append("*Gegenereerd door `patches/proef-zonder-lader.py`; niet met de hand bijwerken.*\n")
    uit.append(
        "Elke game is gestart met **alleen `wine <exe>`** — geen `OpenParrotLoader`, geen `BudgieLoader` en\n"
        "geen `TeknoParrot(64).dll`. De prefix, de werkmap en de omgevingsvariabelen zijn precies die van de\n"
        "directe start (`scripts/launch-direct.py <Game> --plan`), maar de lader en de kern zijn uit de\n"
        "opdrachtregel gehaald. Dit is de scherpste vraag die je over **loskoppelen van TeknoParrot** kunt\n"
        "stellen.\n"
    )
    uit.append(
        "**Let op: dit is niet \"zonder patch\".** De launcher zet de drop-in van een game bij elke start in\n"
        "zijn view en haalt hem daarna **niet** weg. Wat een eerdere start heeft achtergelaten, laadt de game\n"
        "nog steeds — bij Rabbids Hollywood stond onze `matrix64.dll` nog in `Game_Data/Plugins/` en meldde\n"
        "het spel in deze proef `**** DONGLE FOUND: 69420` uit *onze* laag. De kolom **onze bestanden in de\n"
        "view** laat per game zien hoeveel bestanden uit `patches/<Game>/dropin/` er nog staan (vergeleken op\n"
        "sha256). Deze meting zegt dus: **zonder lader en zonder kern**, met wat er al in de view stond.\n"
    )
    uit.append(
        "**Wat een `START` hier betekent.** Het spelproces is verschenen en leefde na **%d seconden** nog.\n"
        "Dus: deze game heeft de lader niet nodig om te *blijven draaien*.\n" % secs
    )
    uit.append(
        "**Wat een `START` hier níet betekent.** Niet dat de game speelbaar is. Besturing, het JVS-bord, de\n"
        "dongle, de regio en soms het beeld gaan via de lader die we hier juist weglaten. Een game die hier\n"
        "START kan in de gewone start alsnog een dongle of een I/O-bord nodig hebben. Andersom is een\n"
        "`niet gestart` geen bewijs dat de lader onmisbaar is: het kan ook een ontbrekende afhankelijkheid\n"
        "of een verkeerde werkmap zijn.\n"
    )
    uit.append(
        "**En het venster is kort.** %d seconden. Een game die een dongle mist kan best starten en pas later\n"
        "afsluiten: Rabbids Hollywood stopt in de gewone start na ongeveer 57 seconden als de Matrix-dongle\n"
        "niet antwoordt. Een `START` zegt dus alleen dat het **proces** bleef lopen, niet dat de game zijn\n"
        "controles gehaald heeft. Kijk daarvoor naar de kolom *onze bestanden in de view* en naar de laatste\n"
        "logregel; daaronder staat een tweede meting met een venster van 70 seconden.\n" % secs
    )
    uit.append(
        "**Nog een beperking van de herkenning.** Een `viel uit` kan ook betekenen dat de exe zichzelf\n"
        "uitpakt en opnieuw start onder een andere naam (Luigi's Mansion en Pigs Might Fly zijn gepakt);\n"
        "dan verliest het patroon het spoor en lijkt de game gestopt terwijl hij loopt. Wie het zeker wil\n"
        "weten, kijkt in `/tmp/rg-zonder-<Game>.log` en in de proceslijst op dat moment.\n"
    )
    uit.append(
        "**Hoe het spelproces herkend wordt.** Op de laatste twee delen van het pad van de exe, in beide\n"
        "schrijfwijzen (`views/LGI/…` en `views\\LGI\\…`), én op een omgevingsvariabele die alleen onze eigen\n"
        "start meekrijgt (`%s=<Game>`). Zonder dat merk zou een game die iemand anders tegelijk start\n"
        "als de onze tellen. Opruimen gebeurt met datzelfde patroon plus onze eigen procesgroep; nooit met\n"
        "een leeg patroon (dat heeft eerder de SSH-verbinding omgelegd).\n" % MERK
    )
    uit.append("**Uitkomst: %d van de %d games starten zonder enige lader.**\n" % (len(start), len(rijen)))
    uit.append("| Game | Resultaat | Spelproces gezien (s) | Nog levend na %ds | Onze bestanden in de view | Laatste logregel | `Type` | `EmulationProfile` |"
               % secs)
    uit.append("|---|---|---|---|---|---|---|---|")
    for game, res, gezien, leeft, laatste, profiel, soort, (aanwezig, totaal) in rijen:
        if totaal:
            view = "%d van %d" % (aanwezig, totaal)
        else:
            view = "geen drop-in"
        uit.append("| %s | %s | %s | %s | %s | `%s` | %s | `%s` |"
                   % (game, res, gezien or "—", "ja" if leeft else "nee", view, laatste or "—",
                      soort or "—", profiel or "—"))
    uit.append("")
    uit.append("## Hoe je dit vernieuwt\n")
    uit.append("```bash\ncd ~/projects/ravengun-patches\npython3 patches/proef-zonder-lader.py\n```\n")
    uit.append(
        "Het script zet de game zelf neer en ruimt hem daarna op. Het raakt de NAS niet aan en schrijft\n"
        "alleen dit bestand plus `/tmp/rg-zonder-<Game>.log`. Draai het niet terwijl je ergens anders\n"
        "dezelfde game start: het herkent zijn eigen proces wel, maar twee games tegelijk maken de meting\n"
        "onnodig rommelig.\n"
    )
    return "\n".join(uit)


LANG_KOP = "## Langere meting: blijven ze ook na een dongle-timeout lopen?"


def md_lang(rijen, secs, oud):
    """Vervangt (of zet) het blok `## Langere meting` in een bestaande kaart."""
    start = [r for r in rijen if r[1].startswith("**")]
    blok = [LANG_KOP, ""]
    blok.append(
        "De tabel hierboven meet **20 seconden**. Dat is kort, en de drop-in van een eerdere start staat nog\n"
        "in de view (zie de kop). Daarom meet dit blok dezelfde games opnieuw met een **venster van %d\n"
        "seconden**: blijven ze ook lopen als een donglecontrole of een time-out later in het spel zou\n"
        "kunnen toeslaan?\n" % secs
    )
    blok.append("**Uitkomst: %d van de %d blijven ook na %d seconden lopen.**\n"
                % (len(start), len(rijen), secs))
    blok.append("| Game | Resultaat | Spelproces gezien (s) | Nog levend na %ds | Laatste logregel |" % secs)
    blok.append("|---|---|---|---|---|")
    for game, res, gezien, leeft, laatste, profiel, soort, _ in rijen:
        blok.append("| %s | %s | %s | %s | `%s` |"
                    % (game, res, gezien or "—", "ja" if leeft else "nee", laatste or "—"))
    blok.append("")
    tekst = "\n".join(blok)
    i = oud.find(LANG_KOP)
    if i >= 0:
        return oud[:i] + tekst
    return oud.rstrip("\n") + "\n\n---\n\n" + tekst


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("games", nargs="*", help="game(s); zonder dit alle games in patches/")
    ap.add_argument("--seconden", type=int, default=20)
    ap.add_argument("--toon", action="store_true", help="alleen naar het scherm")
    ap.add_argument("--lang", action="store_true",
                    help="de uitkomst als extra blok onderaan ZONDER-LADER.md zetten in plaats van een nieuw bestand")
    args = ap.parse_args()

    games = args.games or sorted(d for d in os.listdir(PATCHES)
                                if os.path.isdir(os.path.join(PATCHES, d)) and not d.startswith("_"))
    omgeving()
    print("%-26s %-12s %-6s %-6s %s" % ("GAME", "RESULTAAT", "GEZIEN", "LEEFT", "LAATSTE REGEL"))
    rijen = []
    for game in games:
        proef(game, args.seconden, rijen)
    if args.toon:
        return 0
    doel = os.path.join(PATCHES, "ZONDER-LADER.md")
    if args.lang:
        with open(doel, encoding="utf-8") as f:
            oud = f.read()
        with open(doel, "w", encoding="utf-8") as f:
            f.write(md_lang(rijen, args.seconden, oud))
        print("\nbijgewerkt: %s (langere meting, %d games)" % (doel, len(rijen)))
        return 0
    tekst = md(rijen, args.seconden)
    # een eerder gemeten langere meting blijft staan
    try:
        with open(doel, encoding="utf-8") as g:
            oud = g.read()
        i = oud.find(LANG_KOP)
        if i >= 0:
            tekst = tekst.rstrip("\n") + "\n\n---\n\n" + oud[i:]
    except OSError:
        pass
    with open(doel, "w", encoding="utf-8") as f:
        f.write(tekst)
    print("\nbijgewerkt: %s (%d games)" % (doel, len(rijen)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

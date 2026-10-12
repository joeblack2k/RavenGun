#!/usr/bin/env python3
"""De starttestresultaten samenvoegen tot patches/STARTTEST.md.

Leest de resultaatbestanden van starttest3.sh (kolommen GAME, RESULT, SPEL, LADER,
LAATSTE-REGEL), houdt per game de laatste meting, en schrijft een tabel met de methode en de
les over de maatstaf erbij.
"""
import glob
import io
import os
import sys
import time

UIT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    "~/projects/ravengun-patches/patches/STARTTEST.md")

paden = sorted(glob.glob("/tmp/rg-start3-*.txt"), key=os.path.getmtime)
metingen = {}
for pad in paden:
    with io.open(pad, "r", encoding="utf-8", errors="replace") as f:
        for regel in f:
            delen = regel.split(None, 4)
            if len(delen) < 4 or delen[0] == "GAME":
                continue
            game, res, spel, lader = delen[0], delen[1], delen[2], delen[3]
            laatste = delen[4].strip() if len(delen) > 4 else ""
            metingen[game] = (res, spel, lader, laatste)

namen = sorted(metingen)
start = [g for g in namen if metingen[g][0] == "START"]
viel = [g for g in namen if metingen[g][0] == "viel-uit"]
levend = [g for g in namen if metingen[g][0] == "lader-levend"]
niet = [g for g in namen if metingen[g][0] == "niet-gestart"]

regels = [
    "# Starttest per game",
    "",
    "Elke game is met de eigen lader gestart en daarna opgeruimd. Gemeten op %s uit de ruwe "
    "uitvoer van `starttest3.sh`." % (time.strftime("%Y-%m-%d %H:%M", time.localtime(os.path.getmtime(paden[-1])))
                                      if paden else "onbekend"),
    "",
    "De tabel voegt **alle** ruwe bestanden samen die `starttest3.sh` in `/tmp` heeft achtergelaten "
    "(`/tmp/rg-start3-*.txt`); komt een game in meerdere runs voor, dan wint de nieuwste run. Draai "
    "voor een schone meting eerst de oude bestanden weg.",
    "",
    "```bash",
    "# één game",
    "bash /home/deck/starttest3.sh 22 <Game>",
    "# alle games (elke run schrijft zijn eigen /tmp/rg-start3-<pid>.txt)",
    "bash /home/deck/starttest3.sh 22",
    "python3 patches/starttest-gen.py",
    "```",
    "",
    "## Wat er gemeten wordt",
    "",
    "| Kolom | Betekenis |",
    "|---|---|",
    "| SPEL | de seconde waarop het SPELPROCES voor het eerst gezien is (0 = nooit) |",
    "| LADER | leefde de launcher nog toen de meting afliep |",
    "| **START** | het spelproces is gezien en leefde aan het eind nog |",
    "| **viel-uit** | het spelproces is gezien maar was daarna weg |",
    "| **lader-levend** | geen spelproces gevonden, maar de launcher liep nog |",
    "| **niet-gestart** | geen van beide |",
    "",
    "Het spelproces wordt gezocht op de laatste twee delen van het pad van de exe, in BEIDE",
    "schrijfwijzen: een Wine-proces heeft het Windows-pad in zijn commandoregel",
    "(`Z:\\\\...\\\\views\\\\LGI\\\\...`), niet het Linux-pad.",
    "",
    "### Twee maten die niet werken (en waarom)",
    "",
    "1. **Wachten op `Have fun :)` of `Loading game...`.** Die twee regels komen uit de",
    "   OpenParrot-lader en bestaan in de **ElfLdr2-route niet**: 0 treffers in de zeven",
    "   ElfLdr2-game-ELF's, in de vier `BudgieLoader`-bestanden en in `ElfLdr2/TeknoParrot.dll`.",
    "   Elke ElfLdr2-game las daardoor `start-niet` terwijl hij liep.",
    "2. **Opruimen met `pkill -f <bestandsnaam van de exe>`.** Zes games hebben een exe die",
    "   letterlijk `game` heet, dus een starttest van de ene game doodde de start van een andere;",
    "   en met een leeg patroon matcht `pkill -f \"\"` elk proces. Er wordt nu op de map van de exe",
    "   gematcht, en nooit met een leeg patroon.",
    "",
    "| Game | Resultaat | SPEL (s) | LADER | Laatste logregel |",
    "|---|---|---|---|---|",
]
for g in namen:
    res, spel, lader, laatste = metingen[g]
    laatste = laatste.replace("|", "/")[:80]
    regels.append("| %s | %s | %s | %s | %s |" % (g, res, spel, lader, laatste or "-"))

regels += [
    "",
    "## Tellingen",
    "",
    "| | aantal |",
    "|---|---|",
    "| getest | %d |" % len(namen),
    "| START | %d |" % len(start),
    "| viel-uit | %d |" % len(viel),
    "| lader-levend | %d |" % len(levend),
    "| niet-gestart | %d |" % len(niet),
    "",
]
for titel, groep in (("START", start), ("viel-uit", viel), ("lader-levend", levend),
                     ("niet-gestart", niet)):
    if groep:
        regels.append("**%s:** %s" % (titel, ", ".join(groep)))
        regels.append("")

regels += [
    "## Wat een meting niet zegt",
    "",
    "Dit zegt dat de game **start en blijft draaien**, niet dat hij speelbaar is. Of het beeld",
    "goed is, of de ingang werkt en of de dongle beantwoord wordt staat er niet in. Voor de games",
    "met een dongle is dat een aparte meting (`scripts/hasp.py --game <Game> --json` voor de",
    "gegevens, en het logboek van de start voor de vraag of de bibliotheek geladen wordt).",
    "",
]

# De [Status] van elke game erbij: het rapport laat zien waar de status en de meting uit elkaar
# lopen. De woordenschat is playable | parked | untested, en geen daarvan betekent "start wel,
# speelbaarheid niet nagegaan" -- daarom wordt er niets omgezet, alleen getoond.
PATCHES = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(UIT))), "patches")


def status_van(game):
    pad = os.path.join(PATCHES, game, "game.ini")
    try:
        with io.open(pad, encoding="utf-8-sig") as f:
            for regel in f:
                m = regel.strip()
                if m.lower().startswith("status") and "=" in m:
                    return m.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


start_maar_niet_playable = []
playable_maar_geen_start = []
for g in namen:
    st = status_van(g)
    if metingen[g][0] == "START" and st in ("parked", "untested", ""):
        start_maar_niet_playable.append((g, st or "-"))
    if metingen[g][0] != "START" and st == "playable":
        playable_maar_geen_start.append((g, st))

regels += [
    "## Status versus meting",
    "",
    "De woordenschat van `[Status] Status` is `playable | parked | untested`. Geen van die drie",
    "betekent \"start wel, maar speelbaarheid is niet nagegaan\" — en dat is precies wat deze",
    "meting vaststelt. Daarom wordt er niets omgezet; hier staat waar de twee uit elkaar lopen.",
    "",
]
if start_maar_niet_playable:
    regels.append("**Start en blijft draaien, maar staat niet op `playable`** (%d):"
                  % len(start_maar_niet_playable))
    regels.append("")
    for g, st in start_maar_niet_playable:
        regels.append("* %s — `%s`" % (g, st))
    regels.append("")
    regels.append("Een `playable` zou hier meer claimen dan gemeten is: of het beeld goed is, of de")
    regels.append("ingang werkt en of de dongle beantwoord wordt, staat er niet in. Wie dat per game")
    regels.append("nakijkt, kan de status met onderbouwing omzetten.")
else:
    regels.append("Geen enkele game start zonder op `playable` te staan.")
regels.append("")
if playable_maar_geen_start:
    regels.append("**Staat op `playable` maar start niet** (%d): %s"
                  % (len(playable_maar_geen_start),
                     ", ".join("%s (`%s`)" % (g, st) for g, st in playable_maar_geen_start)))
    regels.append("")
regels.append("")

os.makedirs(os.path.dirname(UIT), exist_ok=True)
with io.open(UIT, "w", encoding="utf-8") as f:
    f.write("\n".join(regels))
print("geschreven: %s (%d games, %d START, %d viel-uit, %d lader-levend, %d niet-gestart)"
      % (UIT, len(namen), len(start), len(viel), len(levend), len(niet)))

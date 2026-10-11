#!/usr/bin/env python3
"""De starttestresultaten samenvoegen tot patches/STARTTEST.md.

Leest de resultaatbestanden van starttest-batch.sh, houdt per game de laatste meting, en
schrijft een tabel met de methode erbij zodat iemand hem kan overdoen.
"""
import glob
import io
import os
import sys
import time

UIT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser(
    "~/projects/ravengun-patches/patches/STARTTEST.md")

# De delen in volgorde van ouder naar nieuw; de nieuwste meting per game wint.
paden = ["/tmp/rg-starttest-deel1.txt"] + sorted(glob.glob("/tmp/rg-starttest-resultaat-*.txt"),
                                                 key=os.path.getmtime)
metingen = {}
for pad in paden:
    if not os.path.isfile(pad):
        continue
    with io.open(pad, "r", encoding="utf-8", errors="replace") as f:
        for regel in f:
            delen = regel.split(None, 3)
            if len(delen) < 3 or delen[0] == "GAME":
                continue
            game, res, levend = delen[0], delen[1], delen[2]
            laatste = delen[3].strip() if len(delen) > 3 else ""
            metingen[game] = (res, levend, laatste, os.path.basename(pad))

namen = sorted(metingen)
start = [g for g in namen if metingen[g][0] == "START"]
viel = [g for g in namen if metingen[g][0] == "viel-uit"]
niet = [g for g in namen if metingen[g][0] == "start-niet"]

regels = [
    "# Starttest per game",
    "",
    "Elke game is met de eigen lader gestart, %d seconden laten draaien, en daarna opgeruimd. "
    "Gemaakt op %s uit de ruwe uitvoer van `starttest-batch.sh`." % (30, time.strftime("%Y-%m-%d %H:%M")),
    "",
    "```bash",
    "# één game",
    "bash /home/deck/starttest-batch.sh 30 <Game>",
    "# alle games (elke run schrijft zijn eigen /tmp/rg-starttest-resultaat-<pid>.txt)",
    "bash /home/deck/starttest-batch.sh 30",
    "python3 patches/starttest-gen.py",
    "```",
    "",
    "**START** = de lader laadde de game (`Have fun :)` of `Loading game...`) en het proces leefde na "
    "30 seconden nog. **viel-uit** = de game begon te laden maar het proces was eerder weg. "
    "**start-niet** = de game is nooit begonnen; de laatste logregel zegt waarom.",
    "",
    "| Game | Resultaat | Laatste logregel |",
    "|---|---|---|",
]
for g in namen:
    res, levend, laatste, _ = metingen[g]
    laatste = laatste.replace("|", "/")[:90]
    regels.append("| %s | %s | %s |" % (g, res, laatste or "-"))

regels += [
    "",
    "## Tellingen",
    "",
    "| | aantal |",
    "|---|---|",
    "| getest | %d |" % len(namen),
    "| START | %d |" % len(start),
    "| viel-uit | %d |" % len(viel),
    "| start-niet | %d |" % len(niet),
    "",
]
if start:
    regels.append("**START:** " + ", ".join(start))
    regels.append("")
if viel:
    regels.append("**viel-uit:** " + ", ".join(viel))
    regels.append("")
if niet:
    regels.append("**start-niet:** " + ", ".join(niet))
    regels.append("")
regels += [
    "## Wat een meting niet zegt",
    "",
    "Dit zegt dat de game **start en blijft draaien**, niet dat hij speelbaar is: of het beeld goed "
    "is, of de ingang werkt en of de dongle wordt beantwoord staat er niet in. Voor de games met een "
    "dongle is de donglecontrole een aparte meting (`scripts/hasp.py --game <Game> --json` voor de "
    "gegevens, en het logboek van de start voor de vraag of de bibliotheek geladen wordt).",
    "",
]

os.makedirs(os.path.dirname(UIT), exist_ok=True)
with io.open(UIT, "w", encoding="utf-8") as f:
    f.write("\n".join(regels))
print("geschreven: %s (%d games, %d START, %d viel-uit, %d start-niet)"
      % (UIT, len(namen), len(start), len(viel), len(niet)))
for g in niet:
    print("  start-niet: %-26s %s" % (g, metingen[g][2][:70]))

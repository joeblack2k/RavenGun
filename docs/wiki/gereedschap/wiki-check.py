#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Controleert de GitHub-wiki van RavenGun (de map met de pagina's; standaard ../concept naast deze map).

De wiki is publiek en gaat over het SPEL en de LADER. Deze controle houdt vier dingen vast die met de hand
makkelijk scheef gaan:

1. De pagina's die er moeten zijn, bestaan, met de bestandsnamen die GitHub als paginanaam gebruikt.
2. Elke interne link wijst naar een pagina die bestaat (een dode link ziet niemand tot een lezer erop klikt).
3. Elke spelpagina heeft dezelfde opbouw: dezelfde tien koppen, in dezelfde orde (de canonieke opbouw).
4. Er staat niets in wat er niet in hoort: niets over de hardwarematige beveiligings- en licentielagen van de
   kasten, en geen paden of namen van deze ene machine.
5. Geen pagina somt op wat hij weglaat: een scoperegel is positief ("This page covers the game and the loader").

Gebruik: wiki-check.py [MAP]      (standaard ../concept naast deze map; exitcode 77 als die map er niet is)
"""
import os
import re
import sys

ALGEMEEN = ["Home", "Loader-Architecture", "Official-Start-Scripts", "Running-on-Linux"]
SPELLEN = ["Luigis-Mansion", "Rabbids-Hollywood", "Halo-Fireteam-Raven", "Terminator-Salvation",
           "Transformers-Shadows-Rising", "House-of-the-Dead-Scarlet-Dawn"]
NAVIGATIE = ["_Sidebar", "_Footer"]

# De canonieke opbouw van elke spelpagina (besluit van 2026-10-13, voor alle zes gelijk).
KOPPEN = ["1. Platform and cabinet", "2. What the game needs", "3. The official start script, or why there is none",
          "4. The route we use", "5. Input", "6. Timing", "7. What we saw on screen",
          "8. Current state and open questions", "9. What we tried and what did not work",
          "10. What is Linux-specific, and what differs on Windows"]

# Wat niet in de wiki hoort: de beveiligings- en licentielagen van de kasten. Hele woorden, hoofdletters maken niet uit.
VERBODEN = [r"dongles?", r"hasp\w*", r"key ?chips?", r"anti-?tamper\w*", r"anti-?debug\w*", r"packers?", r"unpack\w*",
            r"packed", r"drm", r"cracks?", r"cracked", r"bypass\w*", r"patreon", r"activation", r"security chip",
            r"copy protection", r"serial numbers?", r"licen[cs]e keys?", r"ptrace(?!_scope)\w*", r"virtuali[sz]ed function"]
# Een publieke pagina somt niet op wat hij weglaat (besluit van 2026-10-13): geen negatieve scoperegel.
SCOPE_NEGATIEF = [r"does not cover", r"do not cover", r"not covered", r"leaves? out", r"left out", r"omit\w*",
                  r"nothing (?:on|in) this (?:page|wiki) (?:concerns|is about)", r"hardware security",
                  r"licensing layers?", r"security (?:and|or) licen[cs]\w+"]
# En niets van deze ene machine.
LOKAAL = [r"/home/deck", r"spidernas", r"studiodamon", r"\bdamon\b"]

LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")


def controleer(wiki):
    fouten = []
    bestaand = {f[:-3] for f in os.listdir(wiki) if f.endswith(".md")}
    for naam in ALGEMEEN + SPELLEN + NAVIGATIE:
        if naam not in bestaand:
            fouten.append("%s.md ontbreekt" % naam)
    for naam in sorted(bestaand):
        if not re.match(r"^_?[A-Za-z0-9-]+$", naam):
            fouten.append("%s.md: de bestandsnaam is geen geldige paginanaam (letters, cijfers en streepjes)" % naam)
        pad = os.path.join(wiki, naam + ".md")
        with open(pad, encoding="utf-8") as f:
            tekst = f.read()
        # 2. links
        zonder_code = re.sub(r"```.*?```", "", tekst, flags=re.S)
        zonder_code = re.sub(r"`[^`\n]*`", "", zonder_code)
        for doel in LINK.findall(zonder_code):
            if re.match(r"^(https?:|mailto:|#)", doel):
                continue
            pagina = doel.split("#", 1)[0]
            if pagina not in bestaand:
                fouten.append("%s.md: de link naar '%s' wijst naar een pagina die niet bestaat" % (naam, doel))
        # 4. wat er niet in hoort
        for nr, regel in enumerate(tekst.split("\n"), 1):
            for patroon in VERBODEN:
                m = re.search(r"(?<![A-Za-z])(?:%s)(?![A-Za-z])" % patroon, regel, re.I)
                if m:
                    fouten.append("%s.md:%d: '%s' hoort niet in de wiki (beveiligings- of licentielaag)" % (naam, nr, m.group(0)))
            for patroon in SCOPE_NEGATIEF:
                m = re.search(r"(?<![A-Za-z])(?:%s)(?![A-Za-z])" % patroon, regel, re.I)
                if m:
                    fouten.append("%s.md:%d: '%s' somt op wat de pagina weglaat; gebruik een positieve scoperegel"
                                  % (naam, nr, m.group(0)))
            for patroon in LOKAAL:
                m = re.search(patroon, regel, re.I)
                if m:
                    fouten.append("%s.md:%d: '%s' is iets van deze ene machine" % (naam, nr, m.group(0)))
    # 3. de opbouw van de spelpagina's
    for naam in SPELLEN:
        pad = os.path.join(wiki, naam + ".md")
        if not os.path.isfile(pad):
            continue
        with open(pad, encoding="utf-8") as f:
            koppen = [r[3:].strip() for r in f.read().split("\n") if r.startswith("## ")]
        if koppen != KOPPEN:
            ontbreekt = [k for k in KOPPEN if k not in koppen]
            extra = [k for k in koppen if k not in KOPPEN]
            fouten.append("%s.md: de opbouw wijkt af van de andere spelpagina's%s%s%s"
                          % (naam, "; ontbreekt: " + ", ".join(ontbreekt) if ontbreekt else "",
                             "; extra: " + ", ".join(extra) if extra else "",
                             "; de orde klopt niet" if not ontbreekt and not extra else ""))
    # de navigatie noemt elke pagina
    for nav in ("Home", "_Sidebar"):
        pad = os.path.join(wiki, nav + ".md")
        if os.path.isfile(pad):
            with open(pad, encoding="utf-8") as f:
                doelen = set(LINK.findall(f.read()))
            for naam in ALGEMEEN[1:] + SPELLEN:
                if naam not in doelen:
                    fouten.append("%s.md noemt de pagina %s niet" % (nav, naam))
    return fouten, sorted(bestaand)


def main(argv):
    hier = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    wiki = argv[0] if argv else os.path.join(hier, "concept")
    if not os.path.isdir(wiki):
        print("overgeslagen: geen wiki in %s" % wiki)
        return 77
    fouten, paginas = controleer(wiki)
    for f in fouten:
        print("FOUT: " + f)
    if fouten:
        print("%d fout(en) in %s" % (len(fouten), wiki))
        return 1
    print("ok: %d pagina's in %s; links, opbouw en inhoud kloppen" % (len(paginas), wiki))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

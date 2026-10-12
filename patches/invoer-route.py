#!/usr/bin/env python3
"""Kaart: hoe komt de invoer bij elke game binnen, en wat betekent dat voor het loskoppelen?

Leest per game de [Input]-sectie van patches/<Game>/game.ini en zet die naast het [Game]-type en
het EmulationProfile. De vraag die deze kaart beantwoordt is de vraag die de hele opdracht
"loskoppelen van TeknoParrot" scherpstelt: bij welke games zit de gesloten kern nog IN het
invoerpad, en bij welke loopt de invoer er langsbuiten?

Bron voor de betekenis van de routes: docs/architecture.md (de IO-brug van de daemon, de dialecten
evdev, tp-shm, unity-tcp, hid-board en jvs) en docs/loskoppelen-per-game.md (de tabel "Wat een
eigen kern moet implementeren", met "De invoer uit het gedeelde geheugen" als onderdeel van de
kern). Schrijft patches/INVOER-ROUTE.md.

Let op: de route zegt alleen WIE de invoer levert. Of de kern daarbij de gesloten kern is of onze
eigen kern, hangt van het [Game] Type af -- daarom telt die hier apart mee.

Gebruik:  python3 patches/invoer-route.py
"""
import io
import os
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
UIT = os.path.join(HIER, "INVOER-ROUTE.md")

# Wat elke route betekent voor de vraag "zit er een kern in het invoerpad?".
# "kern"  = een kern leest de bordindeling en vertaalt hem naar de game.
# "brug"  = de IO-brug van de daemon levert de invoer, buiten elke kern om.
# "eigen" = de game leest zelf iets anders, of er is geen route gemeten.
BETEKENIS = {
    "tp-shm": ("kern", "de kern leest de bordindeling uit het gedeelde geheugen en vertaalt hem naar de game"),
    "jvs": ("brug", "de game praat zelf JVS; de daemon is het bord aan de andere kant"),
    "evdev": ("brug", "de brug schrijft rechtstreeks naar evdev"),
    "unity-tcp": ("brug", "de brug praat TCP met de Unity-game"),
    "hid-board": ("brug", "de brug maakt een eigen HID-bord"),
    "": ("eigen", "geen route in game.ini: de game leest zelf iets, of er is geen invoer gemeten"),
}

# Welke kern er in het invoerpad zit, volgt uit het Type van de game.
KERN_VAN_TYPE = {
    "TeknoParrot": "de gesloten TeknoParrot-kern",
    "OpenParrot": "onze eigen OpenParrot-kern",
    "ElfLdr2": "de Cygwin-lader (BudgieLoader)",
}


def lees_ini(pad):
    """Leest een ini in de vorm [sectie] / sleutel = waarde. Commentaar met ; of #."""
    secties, sectie = {}, ""
    try:
        with io.open(pad, encoding="utf-8-sig") as f:
            regels = f.read().split("\n")
    except OSError:
        return secties
    for regel in regels:
        r = regel.strip()
        if not r or r.startswith(";") or r.startswith("#"):
            continue
        if r.startswith("[") and r.endswith("]"):
            sectie = r[1:-1].strip()
            secties.setdefault(sectie, {})
            continue
        if "=" in r:
            k, _, v = r.partition("=")
            secties.setdefault(sectie, {})[k.strip()] = v.strip()
    return secties


def games():
    uit = []
    for naam in sorted(os.listdir(HIER)):
        map_ = os.path.join(HIER, naam)
        if not os.path.isdir(map_) or naam.startswith("_"):
            continue
        gi = os.path.join(map_, "game.ini")
        if not os.path.isfile(gi):
            continue
        s = lees_ini(gi)
        g, i = s.get("Game", {}), s.get("Input", {})
        route = i.get("Route", "")
        soort, uitleg = BETEKENIS.get(route, ("eigen", "onbekende route: %r" % route))
        type_ = g.get("Type", "-")
        uit.append({
            "game": naam,
            "type": type_,
            "profiel": g.get("EmulationProfile", "-"),
            "route": route or "-",
            "transport": i.get("JvsTransport", "-") or "-",
            "bord": i.get("Layout", "") or i.get("Board", "") or "-",
            "soort": soort,
            "uitleg": uitleg,
            "kern": KERN_VAN_TYPE.get(type_, "-"),
        })
    return uit


def main():
    rijen = games()
    if not rijen:
        print("geen games gevonden in %s" % HIER)
        return 1

    kern = [r for r in rijen if r["soort"] == "kern"]
    brug = [r for r in rijen if r["soort"] == "brug"]
    eigen = [r for r in rijen if r["soort"] == "eigen"]

    # De groep die echt werk vraagt: invoer via een kern EN die kern is de gesloten TeknoParrot-kern.
    nodig = [r for r in kern if r["type"] == "TeknoParrot"]
    al_van_ons = [r for r in kern if r["type"] == "OpenParrot"]
    andere_lader = [r for r in kern if r["type"] == "ElfLdr2"]

    uit = []
    A = uit.append
    A("# Invoer-route per game: wie levert de invoer?")
    A("")
    A("*Gegenereerd door `patches/invoer-route.py`; niet met de hand bijwerken.*")
    A("")
    A("Elke game heeft in `patches/<Game>/game.ini` een `[Input] Route`. Die route kiest welk dialect")
    A("de **IO-brug van de daemon** gebruikt (`iobridge start <Game>`, zie `docs/architecture.md` in de")
    A("lader-repo). Deze kaart zet die route naast het `Type` en het `EmulationProfile` van de game,")
    A("want daaruit volgt de vraag die de hele opdracht bepaalt: **zit er een kern in het invoerpad, en")
    A("is dat de gesloten kern of onze eigen kern?**")
    A("")
    A("Dat onderscheid is de kern van de zaak. Loopt de invoer buiten elke kern om, dan kan een game")
    A("van de gesloten kern af zonder dat er één per-game hook geschreven hoeft te worden. Staat er")
    A("wél een kern in het pad met route `tp-shm`, dan leest die kern de bordindeling uit het gedeelde")
    A("geheugen en vertaalt hem naar wat de game verwacht (`docs/loskoppelen-per-game.md`, tabel *Wat")
    A("een eigen kern moet implementeren*, regel *De invoer uit het gedeelde geheugen*).")
    A("")
    A("**Gemeten over %d games:**" % len(rijen))
    A("")
    A("| vraag | aantal |")
    A("|---|---|")
    A("| invoer loopt via een kern (`tp-shm`) | **%d** |" % len(kern))
    A("| waarvan op de **gesloten** TeknoParrot-kern → vraagt een eigen profiel | **%d** |" % len(nodig))
    A("| waarvan al op **onze eigen** OpenParrot-kern | %d |" % len(al_van_ons))
    A("| waarvan op de **Cygwin-lader** (ElfLdr2) → vraagt een eigen ELF-lader | %d |" % len(andere_lader))
    A("| invoer loopt buiten elke kern om (de brug doet het zelf) | %d |" % len(brug))
    A("| geen route in `game.ini` gemeten | %d |" % len(eigen))
    A("")
    A("## Alle games")
    A("")
    A("| Game | `Type` | `EmulationProfile` | `Route` | transport | indeling/bord | wie levert de invoer |")
    A("|---|---|---|---|---|---|---|")
    for r in rijen:
        wie = {"kern": "**een kern**", "brug": "de brug", "eigen": "—"}[r["soort"]]
        A("| %s | %s | `%s` | `%s` | %s | %s | %s |"
          % (r["game"], r["type"], r["profiel"], r["route"], r["transport"], r["bord"], wie))
    A("")
    A("## De gesloten kern zit in het invoerpad: %d games" % len(nodig))
    A("")
    A("Dit is de groep die een eigen `EmulationProfile` vraagt. Zonder dat profiel leest niemand de")
    A("bordindeling, en krijgt de game geen invoer — ook niet als hij verder start. De profielnaam die")
    A("erbij hoort staat in `EIGEN-KERN.md`.")
    A("")
    A("| Game | profiel dat nog van ons moet komen |")
    A("|---|---|")
    for r in nodig:
        A("| %s | `%s` |" % (r["game"], r["profiel"]))
    A("")
    A("## Onze eigen kern zit al in het invoerpad: %d games" % len(al_van_ons))
    A("")
    A("Deze vier draaien al op onze eigen kern en hebben hun invoer dus al van ons. Ze staan hier")
    A("alleen ter volledigheid: voor de invoer hoeft hier niets meer bij.")
    A("")
    for r in al_van_ons:
        A("* **%s** — profiel `%s`" % (r["game"], r["profiel"]))
    A("")
    if andere_lader:
        A("## De Cygwin-lader zit in het invoerpad: %d games" % len(andere_lader))
        A("")
        A("Bij deze games is de kern in het invoerpad **BudgieLoader**, niet de OpenParrot-kern. Zij")
        A("vragen dus geen profiel in `GameDetect.cpp` maar een eigen ELF-lader; zie `ELF-NATIVE.md`.")
        A("")
        for r in andere_lader:
            A("* **%s** — `Type = ElfLdr2`, profiel `%s`" % (r["game"], r["profiel"]))
        A("")
    A("## De brug levert de invoer: %d games" % len(brug))
    A("")
    A("Hier loopt de invoer buiten elke kern om. Voor de **invoer** is de gesloten kern hier niet")
    A("nodig; of hij nog ergens anders voor nodig is, is een aparte vraag en staat in `EIGEN-KERN.md`.")
    A("")
    A("| Game | `Type` | route | transport | wat de brug doet |")
    A("|---|---|---|---|---|")
    for r in brug:
        A("| %s | %s | `%s` | %s | %s |" % (r["game"], r["type"], r["route"], r["transport"], r["uitleg"]))
    A("")
    A("## Geen route in game.ini: %d games" % len(eigen))
    A("")
    A("| Game | `Type` | profiel | wat er gemeten is |")
    A("|---|---|---|---|")
    for r in eigen:
        A("| %s | %s | `%s` | %s |" % (r["game"], r["type"], r["profiel"], r["uitleg"]))
    A("")
    A("## Wat dit verandert aan de opdracht")
    A("")
    A("De opdracht *\"elke game zonder TeknoParrot\"* valt hiermee in drie stukken uiteen:")
    A("")
    A("1. **%d games waar de invoer buiten elke kern om loopt.** Daar is voor de invoer geen hook"
      % len(brug))
    A("   nodig, ongeacht welk `Type` er in `game.ini` staat.")
    A("2. **%d games waar de gesloten kern de invoer doet.** Dit is de groep die om een eigen"
      % len(nodig))
    A("   `EmulationProfile` vraagt, en dat is minder dan de 24 profielen die `EIGEN-KERN.md` in")
    A("   totaal noemt: de rest zit in groep 1 of 3.")
    A("3. **%d games waar de Cygwin-lader de invoer doet plus %d games zonder gemeten route.** Voor"
      % (len(andere_lader), len(eigen)))
    A("   groep 3 is nog niets over de invoer gemeten; dat is een open punt, geen conclusie.")
    A("")
    A("Let op: deze kaart gaat **alleen over de invoer**. Of een game ook nog een bytepatch, een")
    A("drop-in of een dongle nodig heeft staat in `PATCHES-PER-GAME.md`, `REGIO-PER-GAME.md` en")
    A("`STAND.md`.")
    A("")
    A("## Hoe je dit vernieuwt")
    A("")
    A("```bash")
    A("cd ~/projects/ravengun-patches")
    A("python3 patches/invoer-route.py")
    A("```")
    A("")

    with io.open(UIT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(uit))

    print("geschreven: %s" % UIT)
    print("  %d games" % len(rijen))
    print("  kern in het pad            : %d" % len(kern))
    print("    waarvan gesloten kern    : %d  <- vraagt een eigen profiel" % len(nodig))
    print("    waarvan onze eigen kern  : %d" % len(al_van_ons))
    print("    waarvan Cygwin-lader     : %d" % len(andere_lader))
    print("  brug doet het zelf         : %d" % len(brug))
    print("  geen route gemeten         : %d" % len(eigen))
    return 0


if __name__ == "__main__":
    sys.exit(main())

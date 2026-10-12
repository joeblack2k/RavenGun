#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Maakt de tabel van patches/STAND.md en werkt hem bij tussen de markers.

Gebruik (op de Deck, vanuit de patches-repo):

    cd ~/projects/ravengun-patches
    python3 patches/stand-gen.py

Of zonder dit bestand: het script in de bijlage van patches/STAND.md is hetzelfde script.

Het leest alleen: patches/<Game>/game.ini, patches/<Game>/dropin/ + dropin.ini (secties en hun To)
en het dongle.json in de drop-in. Het schrijft alleen patches/STAND.md, het blok tussen
BEGIN TABEL en END TABEL.
"""
import json
import os
import re
import sys

PATCHES = "patches"
STAND = os.path.join(PATCHES, "STAND.md")
BEGIN = "<!-- BEGIN TABEL -->"
END = "<!-- END TABEL -->"
SKIP = {"_template", "_defaults"}

# Wat er per game nog ontbreekt of opvalt. Dit is het enige met de hand onderhouden deel; de rest
# van de tabel komt uit de bestanden zelf. Zet hier NIET wat het script al zelf ziet (een bestand
# zonder sectie, een ontbrekende README, een ontbrekende [Status], een ontbrekende dongle): dat
# staat er al, en anders staat het er dubbel.
NOTES = {
    "3DCosplayMahjong": "geen `patches.ini`; niet direct te starten: het spel staat niet op de NAS (`--plan` rc=4)",
    "GaiaAttack4": "geparkeerd (geen beeld, zie `game.ini`); `patches.ini` aanwezig",
    "GalagaAssault": "het 64-bits `libravengun-hasphook-x64.so` heeft een eigen sectie en wordt dus uitgerold; niets laadt het in deze 32-bits game (zie punt 1 onderaan)",
    "Gashaaaan": "compleet; geen drop-in nodig",
    "Gashaaaan2": "compleet; geen drop-in nodig",
    "GhostBusters": "lege `dropin/`; geen `dongle.json` (hasp verwacht); GAME PATH CHECK FAILED is met `[Identify] Path = pm/game` opgelost, daarna komt de game niet in beeld",
    "GoGoStrike": "geen drop-in; vraagt de eigen `LD_PRELOAD`-kastnabootser voor de ElfLdr2-familie, die nog niet bestaat",
    "HOTDSD": "`--plan` rc=4: het profiel start twee exe's (`amdaemon.exe` eerst), dat doet `launch-direct.py` niet",
    "Halo": "drop-in compleet na reparatie (7 bestanden, 7 secties); de 32-bits hook heeft bewust geen `Preload`",
    "HauntedMuseum": "geparkeerd (geen beeld); `patches.ini` aanwezig",
    "HauntedMuseumII": "geparkeerd (geen beeld); `patches.ini` aanwezig",
    "InfinityBladeFX": "geparkeerd (UE3: wit of zwart beeld)",
    "JurassicPark": "het 64-bits `libravengun-hasphook-x64.so` heeft een eigen sectie en wordt dus uitgerold; niets laadt het in deze 32-bits game (zie punt 1 onderaan)",
    "LGI": "regio niet afgedwongen (`enforced = false`, gemeten: de game speelt)",
    "LGI3D": "regio niet afgedwongen (`enforced = false`, gemeten: de game speelt)",
    "LGS": "regio niet afgedwongen (`enforced = false`, gemeten: de game speelt)",
    "LethalEnforcers3": "geparkeerd (32-bits GStreamer ontbreekt)",
    "LuigisMansion": "map compleet; de game vraagt nog een eigen kern (gepakte exe, COM1-blokkade)",
    "MedaruNoGunman": "geparkeerd (3001 RAM DATA IS BAD bij elke verse start)",
    "MusicGunGun2": "geparkeerd (eigen D3D-foutvenster); `patches.ini` aanwezig",
    "NERF": "geen `dongle.json` nodig: de dongle zit in managed code en TeknoParrot antwoordt daar met de BepInEx-plugin `NERFTPPlugin.dll`",
    "OmatsuriYasanGansoTakoyaki": "geparkeerd (wit venster; online activering)",
    "OmatsuriYasanKingyoSukui": "geparkeerd (exitcode 8)",
    "Otomedius": "geparkeerd (COM2/Acio-micom)",
    "PacmanChomp": "geparkeerd (zwart beeld)",
    "PigsMightFly": "compleet",
    "RabbidsHollywood": "`matrix/matrix.ini` heeft een eigen sectie; onze `matrix64.dll` leest dat bestand zelf, dus zonder sectie zou het niet aankomen",
    "RaccoonRampage": "compleet",
    "RainbowBombergirl": "geparkeerd (personagekeuze komt niet)",
    "Rampage": "speelbaar; joystickspel, geen lightgun-game",
    "TC5": "dongle-image ontbreekt (`memory` is leeg, `hasp.py` meldt de dongle onvolledig)",
    "TankTankTank": "niet direct te starten: het spel staat niet op de NAS (`--plan` rc=4)",
    "Terminator": "drop-in met 3 bestanden, maar inert: `game` heeft 0 `hasp_`-symbolen in zijn dynsym, dus er valt niets aan te haken",
    "TombRaider": "speelbaar",
    "TransformersShadowsRising": "geen `dongle.json` nodig: de DK2-scan faalt in fase 1 en de game speelt door",
}

# Games waarvan docs/dropins.md en docs/todo.md zeggen dat ze een dongle vragen, met de soort.
# Staat er een dongle.json, dan komt de soort daaruit en doet deze lijst niets.
EXPECTED_DONGLE = {
    "GalagaAssault": "hasp",
    "GhostBusters": "hasp",
    "HOTDSD": "amdaemon",
    "JurassicPark": "hasp",
    "RabbidsHollywood": "matrix",
    "Terminator": "hasp",
}

# Games die wel een dongle vragen maar waar een dongle.json het verkeerde antwoord is. Dat is geen
# gat in de map, dus ze horen niet in EXPECTED_DONGLE; de reden staat erbij zodat niemand ze alsnog
# als ontbrekend gaat tellen.
GEEN_DROPIN_NODIG = {
    "NERF": "de dongle zit in managed code (`Assembly-CSharp.dll`, `UDongleControl` roept "
            "`hasp_login` aan) en TeknoParrot antwoordt daar met de BepInEx-plugin "
            "`BepInEx/plugins/NERFTPPlugin.dll`; er is geen statisch gelinkte HASP-client in het "
            "Windows-spel om aan te haken",
    "TransformersShadowsRising": "de DK2-scan (`MainThreadDongleScan` in `Shell.exe`) faalt in fase 1 "
                                 "en de game speelt door (`Cabinet SKU set to UNKNOWN`), dus er is "
                                 "niets te emuleren; voor het soort `sega-scan` bestaat nog geen laag",
}


def ini_waarde(pad, sectie, sleutel):
    """De waarde van [sectie] sleutel in een ini, of None."""
    if not os.path.isfile(pad):
        return None
    sec = None
    with open(pad, encoding="utf-8-sig", errors="replace") as fh:
        for regel in fh:
            s = regel.strip()
            if not s or s[0] in ";#":
                continue
            m = re.match(r"^\[(.+?)\]\s*$", s)
            if m:
                sec = m.group(1).strip()
                continue
            if sec == sectie and "=" in s:
                k, v = s.split("=", 1)
                if k.strip().lower() == sleutel.lower():
                    return v.strip()
    return None


def dropin_bestanden(d):
    """Alle bestanden in dropin/ behalve dropin.ini, relatief aan dropin/."""
    uit = []
    for root, _, files in os.walk(d):
        for f in files:
            if root == d and f == "dropin.ini":
                continue
            uit.append(os.path.relpath(os.path.join(root, f), d).replace(os.sep, "/"))
    return sorted(uit)


def dropin_secties(ini):
    """[(sectienaam, To)] in volgorde van het bestand."""
    if not os.path.isfile(ini):
        return []
    uit, sec = [], None
    with open(ini, encoding="utf-8-sig", errors="replace") as fh:
        for regel in fh:
            s = regel.strip()
            if not s or s[0] in ";#":
                continue
            m = re.match(r"^\[(.+?)\]\s*$", s)
            if m:
                sec = m.group(1).strip()
                uit.append([sec, "."])
                continue
            if sec and "=" in s:
                k, v = s.split("=", 1)
                if k.strip().lower() == "to":
                    uit[-1][1] = v.strip() or "."
    return uit


def dubbel_pad(naam, to):
    """True als de sectienaam het pad van `To` al bevat: dest wordt dan dubbel (docs/dropins.md)."""
    t = to.strip("/")
    if t in ("", "."):
        return False
    n = naam.strip("/")
    return n == t or n.startswith(t + "/")


def dongle_soort(gd, game):
    """(soort, pad) uit het dongle.json in de drop-in, of (None, None)."""
    kandidaten = [os.path.join(gd, "dropin", "ravengun-hasp", game, "dongle.json"),
                  os.path.join(gd, "dongle.json")]
    for root, _, files in os.walk(os.path.join(gd, "dropin")):
        if "dongle.json" in files:
            kandidaten.append(os.path.join(root, "dongle.json"))
    for k in kandidaten:
        if os.path.isfile(k):
            try:
                with open(k, encoding="utf-8") as fh:
                    d = json.load(fh)
            except (OSError, ValueError) as e:
                return "FOUT: %s" % e, os.path.relpath(k, gd)
            return (d.get("kind") or "").strip().lower() or "?", os.path.relpath(k, gd)
    return None, None


def lees_games():
    rijen = []
    for naam in sorted(os.listdir(PATCHES)):
        if naam in SKIP or naam.startswith("_"):
            continue
        gd = os.path.join(PATCHES, naam)
        if not os.path.isdir(gd):
            continue
        ini = os.path.join(gd, "game.ini")
        d = os.path.join(gd, "dropin")
        heeft_dropin = os.path.isdir(d)
        bestanden = dropin_bestanden(d) if heeft_dropin else []
        secties = dropin_secties(os.path.join(d, "dropin.ini")) if heeft_dropin else []
        zonder = [b for b in bestanden if b not in [s for s, _ in secties]]
        dubbel = [(s, t) for s, t in secties if dubbel_pad(s, t)]
        soort, dongle_pad = dongle_soort(gd, naam)
        rijen.append({
            "game": naam,
            "game_ini": os.path.isfile(ini),
            "status": ini_waarde(ini, "Status", "Status"),
            "dropin": heeft_dropin,
            "n_bestanden": len(bestanden),
            "n_secties": len(secties),
            "zonder": zonder,
            "dubbel": dubbel,
            "dongle": soort,
            "dongle_pad": dongle_pad,
            "verwacht": None if soort else EXPECTED_DONGLE.get(naam),
            "readme": os.path.isfile(os.path.join(gd, "README.md")),
            "notitie": NOTES.get(naam, ""),
        })
    return rijen


def cel(rij):
    """De kolom 'wat ontbreekt er nog': mechanische gaten vooraan, dan de notitie."""
    stukken = []
    if not rij["game_ini"]:
        stukken.append("geen `game.ini`")
    elif not rij["status"]:
        stukken.append("geen `[Status] Status`")
    if rij["dropin"] and not rij["n_bestanden"] and not rij["n_secties"]:
        stukken.append("lege `dropin/`")
    if rij["zonder"]:
        stukken.append("sectie ontbreekt voor %s" % ", ".join("`%s`" % z for z in rij["zonder"]))
    if rij["dubbel"]:
        stukken.append("dubbel pad in %d sectie(s)" % len(rij["dubbel"]))
    if rij["verwacht"]:
        stukken.append("geen `dongle.json` (%s verwacht)" % rij["verwacht"])
    if not rij["readme"]:
        stukken.append("geen README")
    if rij["notitie"]:
        stukken.append(rij["notitie"])
    return "; ".join(stukken) if stukken else "compleet"


def tabel(rijen):
    uit = ["| Game | `game.ini` | `[Status] Status` | drop-in (bestanden/secties) | zonder sectie | `To` ↔ sectie | dongle | README | wat ontbreekt er nog |",
           "|---|---|---|---|---|---|---|---|---|"]
    for r in rijen:
        if not r["dropin"]:
            drop = "geen `dropin/`"
        elif r["n_bestanden"] == r["n_secties"]:
            drop = "%d / %d" % (r["n_bestanden"], r["n_secties"])
        else:
            drop = "%d / **%d**" % (r["n_bestanden"], r["n_secties"])
        zonder = ", ".join("`%s`" % z for z in r["zonder"]) if r["zonder"] else "-"
        if not r["dropin"]:
            tokol = "-"
        elif r["dubbel"]:
            tokol = "**%d dubbel**" % len(r["dubbel"])
        else:
            tokol = "ok"
        uit.append("| **%s** | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            r["game"],
            "ja" if r["game_ini"] else "**nee**",
            r["status"] or "**-**",
            drop,
            zonder,
            tokol,
            r["dongle"] or "-",
            "ja" if r["readme"] else "**nee**",
            cel(r)))
    return uit


def to_blok(rijen):
    """De losse controle op `To` en de sectienaam (de motor rekent dest = view + To + sectienaam)."""
    uit = ["## `To` en de sectienaam: de dest van elk drop-in-bestand", "",
           "De drop-in-motor rekent `dest = view + To + sectienaam`, en de sectienaam is het pad onder "
           "`dropin/` (`docs/dropins.md`). Zet je het pad ook in `To`, dan komt het bestand een niveau "
           "te diep en vindt de bibliotheek het niet terug -- de motor meldt daar **geen fout** voor, "
           "dus dit is een stille fout. De controle hieronder is: is `To` gelijk aan `.`, of bevat de "
           "sectienaam geen pad dat al met `To` begint?", ""]
    fouten = [(r["game"], s, t) for r in rijen for s, t in r["dubbel"]]
    if not fouten:
        uit += ["**Uitkomst: geen enkele sectie heeft dit probleem.** Alle %d secties in de %d "
                "`dropin.ini`-bestanden staan goed." % (
                    sum(r["n_secties"] for r in rijen),
                    sum(1 for r in rijen if r["dropin"] and r["n_secties"])),
                "",
                "Deze fout heeft er **wel** in gestaan: in Halo, TC5, LGI, LGI3D, LGS en `_template` "
                "stond bij elke donglesectie het pad dubbel, bijvoorbeeld:", "",
                "```ini",
                "[ravengun-hasp/Halo/dongle.json]",
                "To = ravengun-hasp/Halo        ; dest werd view/ravengun-hasp/Halo/ravengun-hasp/Halo/dongle.json",
                "```", "",
                "Met `To = .` wordt dat `view/ravengun-hasp/Halo/dongle.json`, precies waar "
                "`RAVENGUN_HASP_DIR=ravengun-hasp` + `RAVENGUN_HASP_GAME=Halo` hem zoekt. Zie het "
                "kopje \"Reparaties\" voor wie dat gedaan heeft en hoe het gecontroleerd is."]
    else:
        uit += ["| Game | sectie | `To` | dest van de motor | hoort te zijn |",
                "|---|---|---|---|---|"]
        for game, s, t in fouten:
            uit.append("| **%s** | `%s` | `%s` | `%s/%s` | `%s` |" % (
                game, s, t, t.strip("/"), s, s))
    uit.append("")
    return uit


def tellingen(rijen):
    n = len(rijen)
    met_dropin = [r for r in rijen if r["dropin"]]
    heel = [r for r in met_dropin if not r["zonder"]]
    dongles = [r for r in rijen if r["dongle"]]
    verwacht = [r for r in rijen if r["verwacht"]]
    return [
        "## Tellingen",
        "",
        "**%d games** in `patches/` (zonder `_template` en `_defaults`)." % n,
        "",
        "| | aantal |",
        "|---|---|",
        "| games | %d |" % n,
        "| met `game.ini` | %d |" % sum(1 for r in rijen if r["game_ini"]),
        "| met `[Status] Status` | %d |" % sum(1 for r in rijen if r["status"]),
        "| met README.md | %d |" % sum(1 for r in rijen if r["readme"]),
        "| met een `dropin/`-map | %d |" % len(met_dropin),
        "| ... waarvan elk bestand een sectie heeft | %d |" % len(heel),
        "| ... waar nog een bestand zonder sectie staat | %d |" % (len(met_dropin) - len(heel)),
        "| met `dongle.json` | %d |" % len(dongles),
        "| dongle verwacht maar nog geen `dongle.json` | %d |" % len(verwacht),
        "| dongle gevraagd, maar geen `dropin/` nodig (met reden) | %d |" % len(GEEN_DROPIN_NODIG),
        "",
        "Dongles die er zijn: %s." % ", ".join(
            "**%s** (%s)" % (r["game"], r["dongle"]) for r in dongles),
        "",
        "Dongle verwacht maar nog niet aanwezig (uit `docs/dropins.md` en `docs/todo.md`): %s." % ", ".join(
            "**%s** (%s)" % (r["game"], r["verwacht"]) for r in verwacht),
        "",
        "**Geen `dongle.json` nodig, met een reden:** %s." % "; ".join(
            "**%s** (%s)" % (g, reden) for g, reden in sorted(GEEN_DROPIN_NODIG.items())),
        "",
        "Een bestand in `dropin/` zonder eigen sectie in `dropin.ini` wordt **niet** uitgerold; dat is "
        "de enige fout die de drop-in-motor stil laat passeren (`docs/dropins.md`).",
        "",
    ]


def main():
    if not os.path.isdir(PATCHES):
        sys.exit("FOUT: geen %s/ hier. Draai dit vanuit de patches-repo (cd ~/projects/ravengun-patches)." % PATCHES)
    if not os.path.isfile(STAND):
        sys.exit("FOUT: %s bestaat niet" % STAND)
    rijen = lees_games()
    blok = "\n".join(tabel(rijen) + [""] + to_blok(rijen) + tellingen(rijen))
    with open(STAND, encoding="utf-8") as fh:
        tekst = fh.read()
    if BEGIN not in tekst or END not in tekst:
        sys.exit("FOUT: %s en %s niet gevonden in %s" % (BEGIN, END, STAND))
    kop, rest = tekst.split(BEGIN, 1)
    _, staart = rest.split(END, 1)
    with open(STAND, "w", encoding="utf-8") as fh:
        fh.write(kop + BEGIN + "\n" + blok + "\n" + END + staart)
    print("bijgewerkt: %s (%d games)" % (STAND, len(rijen)))


if __name__ == "__main__":
    main()

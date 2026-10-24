#!/usr/bin/env python3
"""Schrijft de README van een game uit de kaarten, voor de games die nog de lege stub hebben.

Waarom: `patches/<Game>/` is de map die iemand aanraakt als hij één game wil bijwerken
(`docs/een-game-toevoegen.md`). Vijftien games hadden daar alleen de drie regels die
`scripts/migrate-game-settings.py` ooit neerzette. Dit script vult die aan met wat de kaarten al
weten: de lader en de kern, de dongle, de patches, de startmeting en wat een start zonder
TeknoParrot blokkeert.

Het script **raakt geen README aan die al inhoud heeft**: alleen de bestanden die nog de
generator-marker van `migrate-game-settings.py` hebben én kort zijn. Met `--game <Game>` forceer je
één game, met `--toon` schrijf je niets.

Gebruik:
    python3 patches/readme-gen.py
    python3 patches/readme-gen.py --toon GaiaAttack4
    python3 patches/readme-gen.py --game GaiaAttack4
"""
import argparse
import json
import os
import re
import sys

PATCHES = os.path.dirname(os.path.abspath(__file__))
STUB_MARKER = "Gegenereerd door scripts/migrate-game-settings.py"
NIEUWE_MARKER = ("<!-- Gegenereerd door patches/readme-gen.py uit de kaarten in deze map; bewerk de kaart "
                 "of verwijder deze regel. -->")


def lees(naam):
    pad = os.path.join(PATCHES, naam)
    if not os.path.isfile(pad):
        return ""
    with open(pad, encoding="utf-8", errors="replace") as f:
        return f.read()


def norm(s):
    """Kolomnaam of cel vergelijkbaar maken: backticks en dubbele spaties weg."""
    return re.sub(r"\s+", " ", s.replace("`", "").replace("*", "")).strip()


def tabel(tekst, naam_kolom="Game", eis=None, tussen=None):
    """{game: {kolomnaam: cel}} uit een markdown-tabel.

    De kolomnamen worden genormaliseerd (`norm`), de spelnaam komt uit de kolom `naam_kolom` -- niet
    uit de eerste kolom, want in `PATCHES-PER-GAME.md` staat daar een nummer.
    """
    if tussen:
        i = tekst.find(tussen[0])
        j = tekst.find(tussen[1], i + 1)
        if i >= 0 and j > i:
            tekst = tekst[i:j]
    regels = tekst.split("\n")
    kop = None
    start = 0
    for i, r in enumerate(regels):
        if not r.startswith("|") or "---" in r:
            continue
        cellen = [c.strip() for c in r.strip().strip("|").split("|")]
        if naam_kolom not in [norm(c) for c in cellen]:
            continue
        if eis and eis not in r:
            continue
        kop = [norm(c) for c in cellen]
        start = i + 2
        break
    if not kop:
        return {}
    idx = kop.index(naam_kolom)
    uit = {}
    for r in regels[start:]:
        if not r.startswith("|"):
            continue
        cellen = [c.strip() for c in r.strip().strip("|").split("|")]
        if len(cellen) < len(kop):
            continue
        naam = norm(cellen[idx])
        if not naam or naam == naam_kolom:
            continue
        uit[naam] = dict(zip(kop, cellen))
    return uit


def cel(t, game, *namen):
    """De eerste niet-lege cel uit een van deze kolomnamen."""
    rij = t.get(game) or {}
    for n in namen:
        v = rij.get(norm(n))
        if v and v.strip() not in ("", "-", "—"):
            return schoon(v)
    return ""


def dongle_van(game):
    pad = os.path.join(PATCHES, game, "dropin/ravengun-hasp", game, "dongle.json")
    if not os.path.isfile(pad):
        return None
    try:
        with open(pad, encoding="utf-8") as f:
            return json.load(f)
    except ValueError:
        return {"kind": "onleesbaar"}


def schoon(s):
    """Markdown-cellen bruikbaar maken: <br> wordt een komma, dubbele spaties weg."""
    s = s.replace("<br>", " ").replace("\\|", "|")
    return re.sub(r"\s+", " ", s).strip()


def dongle_velden(d):
    """De leesbare velden van een dongle.json als (naam, waarde)-paren.

    De bewijslijsten (`evidence`, `api`, `game_reads`) en de lange tekst (`notes`) krijgen een eigen
    regel; de rest tonen we als tabel zodat een regio- of matrixdongle zijn waarden laat zien.
    """
    overslaan = {"evidence", "api", "game_reads", "notes", "memory"}
    rijen = []
    for k, v in d.items():
        if k in overslaan:
            continue
        if isinstance(v, bool):
            rijen.append((k, "ja" if v else "nee"))
        elif isinstance(v, (str, int)):
            if str(v):
                rijen.append((k, "`%s`" % v if k != "notes" else v))
        elif isinstance(v, dict) and len(v) <= 4:
            rijen.append((k, ", ".join("%s = %s" % (kk, vv) for kk, vv in v.items() if vv != "")))
        elif isinstance(v, list) and len(v) <= 6:
            rijen.append((k, ", ".join("`%s`" % x for x in v)))
    return rijen


def md(game, kaarten):
    pp, regio, kern = kaarten["patches-per-game"], kaarten["regio"], kaarten["eigen-kern"]
    zonder, start, stand = kaarten["zonder-lader"], kaarten["starttest"], kaarten["stand"]
    d = dongle_van(game)

    uit = [NIEUWE_MARKER, "# %s\n" % game]
    uit.append("Instellingen: `game.ini`, patches: `patches.ini`.")
    uit.append("Bevindingen en meetgeschiedenis: `findings/%s.md` (op de NAS naast de games, niet in deze "
               "repo).\n" % game)

    lader = cel(kern, game, "Lader")
    profiel = cel(kern, game, "Profiel in game.ini")
    herkent = cel(kern, game, "Onze kern zegt")
    dongle_kolom = cel(stand, game, "dongle")
    dropin = cel(stand, game, "drop-in (bestanden/secties)")
    status = cel(stand, game, "[Status] Status")
    start_lader = cel(start, game, "Resultaat")
    start_zonder = cel(zonder, game, "Resultaat")

    uit.append("## In het kort\n")
    rijen = [
        ("lader", lader or "?"),
        ("profiel", ("`%s`" % norm(profiel)) if profiel else "?"),
        ("onze eigen kern herkent hem", herkent or "?"),
        ("dongle", ("`%s`" % d["kind"]) if d else ("regio/PCB: %s" % (cel(regio, game, "regio/PCB-dongle?") or "onbekend"))),
        ("patches.ini", "ja" if os.path.isfile(os.path.join(PATCHES, game, "patches.ini")) else "nee"),
        ("drop-in", dropin or "geen `dropin/`"),
        ("`[Status]`", status or "?"),
        ("start met de lader", start_lader or "niet getest"),
        ("start zonder enige lader", start_zonder or "niet gemeten"),
    ]
    uit.append("| | |")
    uit.append("|---|---|")
    for k, v in rijen:
        uit.append("| %s | %s |" % (k, v))
    uit.append("")

    if d:
        uit.append("## De dongle\n")
        uit.append("`dropin/ravengun-hasp/%s/dongle.json` beschrijft een **%s**-dongle."
                   % (game, d.get("kind", "?")))
        mem = d.get("memory") or []
        if mem:
            uit.append("Er staan %d geheugenbestand(en) in `memory/`." % len(mem))
        elif d.get("kind") == "hasp":
            uit.append("**Let op:** er staan geen geheugenbestanden in (`\"memory\": []`), dus "
                       "`scripts/hasp.py` meldt deze dongle als onvolledig.")
        velden = dongle_velden(d)
        if velden:
            uit.append("")
            uit.append("| veld | waarde |")
            uit.append("|---|---|")
            for k, v in velden:
                uit.append("| `%s` | %s |" % (k, v))
        for sleutel, kop in (("game_reads", "leesroutes die de game zelf gebruikt"),
                             ("api", "API die de dongle moet beantwoorden")):
            if d.get(sleutel):
                uit.append("\n**%s:** %s" % (kop, ", ".join("`%s`" % x for x in d[sleutel])))
        if d.get("notes"):
            uit.append("\nUit de beschrijving zelf:\n\n> %s" % d["notes"].strip())
        uit.append("")
    else:
        uit.append("## De dongle\n")
        oordeel = cel(regio, game, "regio/PCB-dongle?")
        wat = cel(regio, game, "wat vraagt de game anders")
        uit.append("Geen `dongle.json` in deze map. Oordeel over een regio/printplaat-dongle in "
                   "`patches/REGIO-PER-GAME.md`: %s." % (oordeel or "onbekend"))
        if wat:
            uit.append("\nWat de game dan wél vraagt: %s" % wat)
        if dongle_kolom and dongle_kolom not in ("-", "—"):
            uit.append("\nDe dongle-kolom van `patches/STAND.md` zegt: `%s`." % dongle_kolom)
        uit.append("")

    uit.append("## De patches\n")
    uit.append("* **Nodig:** %s" % (cel(pp, game, "Patch nodig") or "onbekend"))
    uit.append("* **Is dat er:** %s" % (cel(pp, game, "Er?") or "onbekend"))
    bewijs = cel(pp, game, "Bewijs (bestand + wat erin staat)")
    if bewijs:
        uit.append("* **Bewijs:** %s" % bewijs)
    uit.append("")

    uit.append("## Starten\n")
    uit.append("* Met de eigen lader: %s" % (start_lader or "niet getest"))
    uit.append("* Zonder enige lader (alleen `wine <exe>`): %s" % (start_zonder or "niet gemeten"))
    blok = cel(pp, game, "Wat een start zonder TeknoParrot blokkeert")
    if blok:
        uit.append("\n**Wat een start zonder TeknoParrot blokkeert:** %s" % blok)
    uit.append("")
    uit.append("## Waar de metingen staan\n")
    uit.append("| Kaart | Wat je erin vindt |")
    uit.append("|---|---|")
    uit.append("| `patches/STAND.md` | volledigheid van deze map (`game.ini`, `[Status]`, drop-in, dongle) |")
    uit.append("| `patches/STARTTEST.md` | of deze game met de eigen lader start en blijft draaien |")
    uit.append("| `patches/ZONDER-LADER.md` | of hij ook met alleen `wine <exe>` start |")
    uit.append("| `patches/PATCHES-PER-GAME.md` | welke patch nodig is en wat een start blokkeert |")
    uit.append("| `patches/REGIO-PER-GAME.md` | of hij een regio/printplaat-dongle vraagt |")
    uit.append("| `patches/EIGEN-KERN.md` | of onze eigen kern hem herkent, en welk profiel er nog moet komen |")
    uit.append("| `patches/INVOER-ROUTE.md` | wie de invoer levert: de brug van de daemon, of een kern |")
    uit.append("")
    return "\n".join(uit)


def is_stub(game):
    pad = os.path.join(PATCHES, game, "README.md")
    if not os.path.isfile(pad):
        return False
    with open(pad, encoding="utf-8", errors="replace") as f:
        t = f.read()
    return STUB_MARKER in t and len(t.split("\n")) <= 12


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("games", nargs="*")
    ap.add_argument("--game", action="append", default=[], help="forceer deze game")
    ap.add_argument("--toon", action="store_true")
    args = ap.parse_args()

    kaarten = {
        "patches-per-game": tabel(lees("PATCHES-PER-GAME.md"), eis="Patch nodig"),
        "regio": tabel(lees("REGIO-PER-GAME.md"), tussen=("<!-- BEGIN TABEL -->", "<!-- END TABEL -->")),
        "eigen-kern": tabel(lees("EIGEN-KERN.md")),
        "zonder-lader": tabel(lees("ZONDER-LADER.md")),
        "starttest": tabel(lees("STARTTEST.md")),
        "stand": tabel(lees("STAND.md")),
    }
    for naam, t in kaarten.items():
        if not t:
            print("!! de tabel van %s kon niet gelezen worden" % naam, file=sys.stderr)
            return 1

    alle = sorted(d for d in os.listdir(PATCHES)
                  if os.path.isdir(os.path.join(PATCHES, d)) and not d.startswith("_"))
    doen = args.games or args.game or [g for g in alle if is_stub(g)]
    if not doen:
        print("geen games om te doen (alles heeft al inhoud)")
        return 0
    for game in doen:
        tekst = md(game, kaarten)
        if args.toon:
            print("=" * 70)
            print(tekst)
            continue
        pad = os.path.join(PATCHES, game, "README.md")
        with open(pad, "w", encoding="utf-8") as f:
            f.write(tekst)
        print("bijgewerkt: %s" % pad)
    return 0


if __name__ == "__main__":
    sys.exit(main())

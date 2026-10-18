#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Kopieert uit de patches-map ALLEEN de levering (wat de lader, de daemon en de app lezen) naar een doelmap.

Dezelfde lijst als de install(DIRECTORY patches ...)-regel in CMakeLists.txt en de controle in tests/install_test.sh:
<Game>/*.ini, native.json, unity.json, options.json, dongle.json, README.md, heap.tsv, en eigen bestanden onder <Game>/dropin/, data/ en seed/.
Privé-donglegegevens, game-shaders, saves en game-afgeleide configuratie gaan nooit mee, ook niet als ze lokaal aanwezig zijn.
Alles daarbuiten (logboeken, metingen, foto's, notities, meetscripts) is dossier en gaat niet mee. Lege mappen ontstaan niet.

Gebruik: ship-patches.py BRON DOEL      (BRON = de patches-map, een koppeling mag; DOEL wordt aangemaakt)
Migratie: ship-patches.py --adopt-checkout BRON DOEL  (vervangt alleen een koppeling naar BRON; BRON blijft intact)
Privé: ship-patches.py --preserve-user BRON GEBRUIKERSLAAG  (kopieert alleen ontbrekende privébestanden, wijzigt geen bestaande sleutels)
De repository is privé: dongle- en HASP-bestanden horen juist IN de levering (een verse kloon moet de games
zonder extra stappen kunnen starten). preserve_user() blijft bestaan om een checkout die nog geen privébestanden
heeft bij te werken vanuit een gebruikerslaag.
"""
import configparser
import os
import re
import shutil
import sys

# Gelijk aan de install(DIRECTORY patches ...)-regels in CMakeLists.txt en de controles in tests/install_test.sh en
# tests/release_layout_test.sh (tests/test_patches_layout.py vergelijkt ze met de CMake-lijst).
LEVERING = re.compile(r"^[^/]+/(?:[^/]+\.ini|native\.json|unity\.json|options\.json|dongle\.json|README\.md|heap\.tsv)$|^[^/]+/(?:dropin|data|seed|private)/")


def user_supplied(rel):
    """Privégegevens en game-afgeleide bestanden: nooit in een publieke levering."""
    parts = rel.split("/")
    return (any(p in ("ravengun-hasp", "memory", "shaders", "SAVE") for p in parts)
            or any(p == "dongle.json" or p.startswith("dongle.") and p.endswith(".json") for p in parts)
            or rel.startswith("FarCryParadiseLost/dropin/") and parts[-1] in
               ("systemcfg.lua", "Systemcfgoverride.lua", "Arcade_Calibration.txt")
            or parts[-1] in ("heap.tsv", "WkWin32.dll"))


def ship(src, dst):
    src = os.path.realpath(src)
    n = 0
    for dp, dns, fns in os.walk(src):
        dns.sort()
        for f in sorted(fns):
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, src)
            # user_supplied() bepaalt alleen wat preserve_user() uit een gebruikerslaag haalt;
            # de levering zelf bevat alles wat de lader leest, privégegevens inbegrepen.
            if not LEVERING.match(rel) or not os.path.exists(p):
                continue
            t = os.path.join(dst, rel)
            os.makedirs(os.path.dirname(t), exist_ok=True)
            shutil.copyfile(p, t)
            os.chmod(t, 0o755 if os.access(p, os.X_OK) else 0o644)
            n += 1
    return n


def preserve_user(src, dst):
    """Bewaar lokale privébestanden zonder bestaande gebruikersbestanden te vervangen."""
    n = 0
    for dp, _, files in os.walk(src):
        for f in sorted(files):
            p = os.path.join(dp, f)
            rel = os.path.relpath(p, src)
            if not user_supplied(rel):
                continue
            out = os.path.join(dst, rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            if not os.path.lexists(out):
                shutil.copy2(p, out)
                n += 1
    for game in sorted(os.listdir(src)):
        original = os.path.join(src, game, "dropin", "dropin.ini")
        if not os.path.isfile(original):
            continue
        cp = configparser.RawConfigParser(strict=False)
        cp.optionxform = str
        cp.read(original, encoding="utf-8-sig")
        needed = [s for s in cp.sections() if user_supplied(game + "/dropin/" + s)]
        if not needed:
            continue
        out = os.path.join(dst, game, "dropin", "dropin.ini")
        current = configparser.RawConfigParser(strict=False)
        current.optionxform = str
        current.read(out, encoding="utf-8-sig")
        raw = open(original, encoding="utf-8-sig").read()

        def raw_section(text, name):
            """De sectie zoals hij in het bestand staat, inclusief meerregelige waarden en
            opmerkingen. De grenzen zijn die van configparser: een regel is een nieuwe
            sectiekop als hij NIET een voortzetting van de vorige waarde is en zijn afgetrimde
            vorm met '[' begint en een ']' bevat. Een kop met tekst erachter
            ('[x] ; toelichting') telt dus mee. Een voortzetting is een regel die verder
            ingesprongen is dan de sleutelregel erboven: '  [0x10] = 1' onder 'k = a' hoort bij
            de waarde van k en is geen kop. Lege regels en commentaarregels veranderen daar niets
            aan."""
            kop = re.compile(r"^\[.*\]")
            uit, binnen = [], False
            optie, inspringing = False, 0   # staat er een sleutelregel open, en hoe ver sprong die in
            for regel in text.splitlines(keepends=True):
                schoon = regel.strip()
                if not schoon or schoon[0] in "#;":
                    if binnen:
                        uit.append(regel)
                    continue
                diepte = len(regel) - len(regel.lstrip())
                if optie and diepte > inspringing:
                    if binnen:
                        uit.append(regel)
                    continue
                inspringing = diepte
                if kop.match(schoon):
                    optie = False
                    if binnen:
                        break
                    if schoon[1:schoon.rfind("]")] == name:
                        binnen = True
                        uit.append(regel)
                    continue
                optie = True
                if binnen:
                    uit.append(regel)
            return "".join(uit) if uit else None

        additions = []
        for section in needed:
            if current.has_section(section):
                continue
            block = raw_section(raw, section)
            if block:
                additions.append("\n" + block.strip("\r\n") + "\n")
        if additions:
            os.makedirs(os.path.dirname(out), exist_ok=True)
            # Append only: bestaande sleutels, opmerkingen en benchinstellingen blijven intact.
            with open(out, "a", encoding="utf-8") as f:
                f.write("".join(additions))
    return n


if __name__ == "__main__":
    args = sys.argv[1:]
    if args and args[0] == "--preserve-user":
        if len(args) != 3 or not os.path.isdir(args[1]):
            sys.exit(__doc__)
        print("%d privébestanden behouden in %s" % (preserve_user(args[1], args[2]), args[2]))
        sys.exit(0)
    adopt = bool(args and args[0] == "--adopt-checkout")
    if adopt:
        args = args[1:]
    if len(args) != 2 or not os.path.isdir(args[0]):
        sys.exit(__doc__)
    src, dst = args
    if adopt:
        # Een bestaande ontwikkelkoppeling vervangen, nooit de oude checkout zelf.
        if not os.path.islink(dst) or os.path.realpath(dst) != os.path.realpath(src):
            sys.exit("--adopt-checkout vereist een doelkoppeling naar de bron")
        stage = dst + ".import"
        if os.path.lexists(stage):
            sys.exit("importmap bestaat al: " + stage)
        try:
            n = ship(src, stage)
            if not n:
                raise SystemExit("lege import geweigerd")
            target = os.readlink(dst)
            os.unlink(dst)
            try:
                os.rename(stage, dst)
            except OSError:
                os.symlink(target, dst)
                raise
        except BaseException:
            # Nooit een halve importmap achterlaten (dan mislukt de volgende poging op "bestaat al").
            shutil.rmtree(stage, ignore_errors=True)
            raise
    else:
        n = ship(src, dst)
    if n == 0:
        sys.exit("geen enkel bestand in de levering gevonden in " + src)
    print("%d patchbestanden naar %s" % (n, dst))

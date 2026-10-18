#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""De indeling van een gamemap in patches/ (docs/per-game-patches.md, "Indeling van een gamemap"). Alleen de standaardbibliotheek.

EEN schema, in code, waar de tests (tests/test_patches_layout.py) en de documentatie op leunen. Een gamemap kan van de
patches-repo naar de eigen patches/ van de loader verhuizen (en terug); het schema is wat zegt of dat veilig is.

    LEVERING (shipped; verhuist mee)           game.ini (verplicht), patches.ini, native.json, unity.json, options.json, *.ini-tabellen, dropin/**, data/**, seed/**
    ONDERZOEK (research; verhuist NIET mee)    *.md, logs-*/, bewijs-*/, probe*/, measurements/, *.voor-correctie*, schermafbeeldingen,
                                               meetlogboeken, meetscripts (*.sh *.py *.c *.h *.txt), *-logs/, *-harness/, ...
    ONBEKEND                                   al het andere in de map; de test meldt het (hoort of in de levering, of bij het onderzoek)

Gebruik:  patchlayout.py [WORTEL ...]     controleert de gamemappen onder elke wortel (standaard: de meegeleverde lagen van gamesettings.patch_layers(), niet de gebruikerslaag).
"""
import configparser
import importlib.util
import fnmatch
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "scripts"))   # gamesettings; python3 -I neemt de scriptmap niet mee

_ship_spec = importlib.util.spec_from_file_location("ship_patches", os.path.join(os.path.dirname(__file__), "ship-patches.py"))
_ship = importlib.util.module_from_spec(_ship_spec)
_ship_spec.loader.exec_module(_ship)

REQUIRED = ("game.ini",)
# hasp-haken.ini is de adreslijst die de HASP-ELF-hook bij het draaien leest (RAVENGUN_HASP_ELF_ADRESLIJST,
# of naast de lib in dropin/). Het is dus LEVERING (runtime-materiaal dat meeverhuist), geen onderzoek.
# (Open: voor een game die hem ook in dropin/ heeft staan de twee kopieen dubbel; opruimen is een latere stap.)
# rt-invoer.ini, rio-invoer.ini en irtrack.ini zijn de invoertabellen die de daemon (src/ravengun/launch/native.cpp) bij het
# draaien aan de invoerhulp en de RIO-brug geeft: ook levering. native.json is het native profiel van de game
# (docs/native-profiel.md): levering. options.json zijn de opties en de memory card die de app toont (docs/game-opties.md): levering. unity.json is het profiel van een Unity-game (docs/per-game-patches.md, "Unity-games"): levering.
# lindbergh.ini zijn de instellingen van de Lindbergh-laag die de daemon bij elke start aanvult en aan lindbergh.so geeft
# (docs/lindbergh.md): levering.
SHIP_FILES = ("game.ini", "patches.ini", "native.json", "unity.json", "options.json", "hasp-haken.ini", "rt-invoer.ini", "rio-invoer.ini", "irtrack.ini",
              "lindbergh.ini", "heap.tsv")   # heap.tsv: de heap-indeling die de HASP-hook van Walking Dead leest
# seed/ is zaaigoed voor de schrijflaag van de Wine-routes (src/ravengun/games/seed.hpp): levering.
# private/ is de antwoordcache van de HASP-emulatie (native.json zaait die in de schrijflaag), dus levering.
SHIP_TREES = ("dropin", "data", "seed", "private")

# De gereserveerde naam op het hoogste niveau van een wortel: shared/ bevat GEDEELD materiaal (meer dan een game), geen
# gamemap. game_folders() slaat hem over. Moet gelijk zijn aan gamesettings.SHARED_DIR (test: test_shared_items).
SHARED_DIR = "shared"

# Onderzoeksmateriaal op elke diepte (dus ook verboden BINNEN dropin/ en data/, want dat is de levering):
RESEARCH_ANYWHERE = (
    "*.md", "*.md.*",                        # dossiers, uitkomsten, draaiboeken (ook *.md.voor-correctie-...)
    "logs-*", "bewijs-*",                    # logboeken en bewijsmappen van een meetrun
    "probe", "probes", "measurements",       # proefmappen en metingen
    "*.voor-correctie*",                     # de oude versie van een gecorrigeerd dossier
    "*.png", "*.jpg", "*.jpeg", "*.gif", "*.webp",   # schermafbeeldingen
    "*.log", "gamelog-*", "usb-tap*",        # logboeken
    "__pycache__",
)
# Onderzoeksmateriaal alleen op het hoogste niveau van de gamemap (in dropin/ of data/ kan zoiets echt bestand van de levering zijn):
RESEARCH_TOPLEVEL = (
    "*.txt", "*.csv",                        # ruwe uitvoer
    "*.sh", "*.py", "*.c", "*.h",            # meetscripts en proefprogramma's
    "*-logs", "*-harness",                   # mappen met logs of een proefopstelling
    "*.draft.*", "*.kandidaat.*", "*.schema-fragment.*",   # nog niet vastgestelde stukken
    "run*",                                  # de uitvoer van een genummerde run (run8-exit, run10-leven, ...)
)


def _match(name, patterns):
    return any(fnmatch.fnmatch(name, p) for p in patterns)


def classify(name):
    """Een naam op het hoogste niveau van een gamemap: "ship" | "research" | "unknown". De levering gaat voor."""
    if name in SHIP_FILES or name in SHIP_TREES:
        return "ship"
    if _match(name, RESEARCH_ANYWHERE) or _match(name, RESEARCH_TOPLEVEL):
        return "research"
    return "unknown"


def _entries(game_dir):
    try:
        return sorted(os.listdir(game_dir))
    except OSError:
        return []


def _walk_files(base):
    """Alle bestanden onder base (relatief aan base), zonder koppelingen naar mappen te volgen."""
    uit = []
    for dp, dns, fns in os.walk(base):
        dns.sort()
        for f in sorted(fns):
            uit.append(os.path.relpath(os.path.join(dp, f), base))
    return uit


def ship_set(game_dir):
    """Wat er van deze map meeverhuist: [relatieve paden]. game.ini, patches.ini en alles onder dropin/ en data/."""
    uit = []
    for e in _entries(game_dir):
        if classify(e) != "ship":
            continue
        p = os.path.join(game_dir, e)
        if os.path.isdir(p) and not os.path.islink(p):
            uit += [os.path.join(e, r) for r in _walk_files(p)]
        elif os.path.exists(p) or os.path.islink(p):
            uit.append(e)
    return uit


def research_set(game_dir):
    """Het onderzoeksmateriaal van deze map op het hoogste niveau: [namen]. Verhuist niet mee."""
    return [e for e in _entries(game_dir) if classify(e) == "research"]


def unknown_entries(game_dir):
    """Namen op het hoogste niveau die noch levering noch bekend onderzoek zijn."""
    return [e for e in _entries(game_dir) if classify(e) == "unknown"]


def research_in_ship(game_dir):
    """Onderzoeksmateriaal DAT IN DE LEVERING ZIT: elk pad van ship_set() (of een map ervan) dat op een RESEARCH_ANYWHERE-patroon
    past. Dit moet leeg zijn: anders sleept het verhuizen van de map dossiers en logs mee."""
    uit = []
    for rel in ship_set(game_dir):
        delen = rel.split(os.sep)
        if len(delen) == 1:
            continue                      # game.ini en patches.ini zelf
        if any(_match(d, RESEARCH_ANYWHERE) for d in delen[1:]):
            uit.append(rel)
    return uit


def dropin_references(game_dir):
    """De bestanden die dropin/dropin.ini noemt: [(sectienaam, verwacht pad)]. Elke sectie is een bestand (of map) in dropin/.
    Zonder dropin.ini: []. Een leesfout geeft ValueError (de aanroeper meldt hem)."""
    ini = os.path.join(game_dir, "dropin", "dropin.ini")
    if not os.path.isfile(ini):
        return []
    cp = configparser.RawConfigParser(strict=False)
    cp.optionxform = str
    try:
        with open(ini, encoding="utf-8-sig") as f:
            cp.read_file(f)
    except (OSError, configparser.Error) as e:
        raise ValueError("kan %s niet lezen: %s" % (ini, e))
    return [(s, os.path.join(game_dir, "dropin", s)) for s in cp.sections()]


def validate_game_folder(game_dir, check_profile=True):
    """De afwijkingen van een gamemap van het schema: [(code, tekst)]; leeg = in orde. Codes:
         missing-required     een verplicht bestand ontbreekt (game.ini)
         unknown-entry        een naam die noch levering noch bekend onderzoek is
         ship-contaminated    onderzoeksmateriaal binnen de levering (dropin/**, data/**)
         dropin-no-ini        dropin/ bestaat maar dropin.ini ontbreekt
         dropin-bad-ini       dropin.ini is niet te lezen
         dropin-missing-file  dropin.ini noemt een bestand dat er niet staat (de levering is niet compleet)
         profile-mismatch     [Game] Profile in game.ini is niet de mapnaam"""
    uit = []
    naam = os.path.basename(os.path.normpath(game_dir))
    for r in REQUIRED:
        if not os.path.isfile(os.path.join(game_dir, r)):
            uit.append(("missing-required", "%s: %s ontbreekt" % (game_dir, r)))
    for e in unknown_entries(game_dir):
        uit.append(("unknown-entry", "%s: %r is geen levering (game.ini, patches.ini, dropin/, data/, seed/) en geen bekend onderzoeksmateriaal" % (game_dir, e)))
    for rel in research_in_ship(game_dir):
        uit.append(("ship-contaminated", "%s: onderzoeksmateriaal in de levering: %s" % (game_dir, rel)))
    if os.path.isdir(os.path.join(game_dir, "dropin")):
        if not os.path.isfile(os.path.join(game_dir, "dropin", "dropin.ini")):
            uit.append(("dropin-no-ini", "%s: dropin/ zonder dropin.ini" % game_dir))
        else:
            try:
                for sectie, pad in dropin_references(game_dir):
                    # Deze secties beschrijven bestanden die de eigenaar in de gebruikerslaag
                    # moet leveren; de publieke checkout bevat bewust geen dongledumps of saves.
                    private = _ship.user_supplied(os.path.basename(game_dir) + "/dropin/" + sectie)
                    if not os.path.exists(pad) and not private:
                        uit.append(("dropin-missing-file", "%s: dropin.ini noemt [%s] maar dropin/%s bestaat niet" % (game_dir, sectie, sectie)))
            except ValueError as e:
                uit.append(("dropin-bad-ini", "%s: %s" % (game_dir, e)))
    if check_profile and os.path.isfile(os.path.join(game_dir, "game.ini")):
        import gamesettings as gs
        ident = gs.declared_profile(os.path.join(game_dir, "game.ini"))
        if ident and ident != naam:
            uit.append(("profile-mismatch", "%s: de map heet %r maar [Game] Profile = %r" % (game_dir, naam, ident)))
    return uit


def game_folders(root):
    """De gamemappen onder een wortel: mappen die niet met . of _ beginnen (plus _template, een sjabloon dat zelf aan het
    schema moet voldoen), en NIET de gereserveerde shared/ (gedeeld materiaal, geen game)."""
    uit = []
    for e in _entries(root):
        p = os.path.join(root, e)
        if e == SHARED_DIR:
            continue
        if os.path.isdir(p) and (not e.startswith((".", "_")) or e == "_template"):
            uit.append(p)
    return uit


def shared_item_folders(root):
    """De gedeelde items onder <wortel>/shared/: mappen die niet met . of _ beginnen."""
    uit = []
    for e in _entries(os.path.join(root, SHARED_DIR)):
        p = os.path.join(root, SHARED_DIR, e)
        if os.path.isdir(p) and not e.startswith((".", "_")):
            uit.append(p)
    return uit


def shared_item_problems(item_dir):
    """[(code, tekst)] voor een gedeeld item: het is LEVERING (runtime-materiaal dat meeverhuist), dus er hoort geen
    onderzoeksmateriaal in (een *.md, logs-*, meetscript, ...). Een leeg item is ook een afwijking."""
    uit = []
    files = _walk_files(item_dir)
    if not files:
        uit.append(("empty", "%s: het gedeelde item is leeg" % item_dir))
    for rel in files:
        for deel in rel.split(os.sep):
            if _match(deel, RESEARCH_ANYWHERE) or _match(deel, RESEARCH_TOPLEVEL):
                uit.append(("research-in-shared", "%s: %s lijkt onderzoeksmateriaal; een gedeeld item is louter levering" % (item_dir, rel)))
                break
    return uit


def shared_fingerprint(item_dir):
    """{relatief pad: sha256} van ALLE bestanden van een gedeeld item: wat moet kloppen als hetzelfde item op twee plaatsen staat."""
    return {rel: _digest(os.path.join(item_dir, rel)) for rel in _walk_files(item_dir)}


def validate_root(root):
    """{gamemap: [(code, tekst)]} voor elke gamemap onder de wortel met afwijkingen."""
    uit = {}
    for d in game_folders(root):
        problemen = validate_game_folder(d, check_profile=not os.path.basename(d).startswith("_"))
        if problemen:
            uit[d] = problemen
    return uit


def same_directory(a, b):
    """Of twee paden dezelfde map zijn (koppeling of niet): vergelijkt st_dev en st_ino, niet de tekst."""
    try:
        sa, sb = os.stat(a), os.stat(b)
    except OSError:
        return False
    return (sa.st_dev, sa.st_ino) == (sb.st_dev, sb.st_ino)


def _digest(path):
    import hashlib
    h = hashlib.sha256()
    try:
        with open(path, "rb") as f:
            for blok in iter(lambda: f.read(1 << 20), b""):
                h.update(blok)
    except OSError:
        return "onleesbaar"
    return h.hexdigest()


def ship_fingerprint(game_dir):
    """{relatief pad: sha256} van de levering van een map: wat er moet kloppen als dezelfde game op twee plaatsen staat."""
    return {rel: _digest(os.path.join(game_dir, rel)) for rel in ship_set(game_dir)}


def check_roots(loader, repo):
    """De eigen patches/ van de loader en de patches-repo controleren. Geeft een verslag:
         mode                  "same-inode" | "distinct" | "single" | "none"
         roots_validated       de mappen die tegen het schema gelegd zijn
         cross_repo_comparison True ALLEEN als er twee verschillende mappen waren en hun gedeelde games vergeleken zijn
         issues                {gamemap: [(code, tekst)]}
         conflicts             [tekst]: een game staat op beide plaatsen met een verschillende levering
         notes                 wat de controle wel en niet gedaan heeft (ook: "dezelfde map, geen vergelijking")

    Op een ontwikkelmachine is de patches/ van de loader een koppeling naar de patches-repo: dezelfde map (zelfde st_dev en
    st_ino). Dan is er EEN indeling, die tegen het schema gelegd wordt; er wordt NIET beweerd dat "de twee indelingen
    overeenkomen". Staan ze elders (of is een map verhuisd), dan worden beide gecontroleerd en de games die op beide
    plaatsen staan op hun levering vergeleken."""
    rep = {"loader": loader, "repo": repo, "mode": "none", "roots_validated": [], "cross_repo_comparison": False,
           "issues": {}, "conflicts": [], "notes": []}
    aanwezig = [p for p in (loader, repo) if p and os.path.isdir(p)]
    if not aanwezig:
        rep["notes"].append("geen van beide mappen bestaat (loader: %s, repo: %s); niets gecontroleerd" % (loader, repo))
        return rep
    if len(aanwezig) == 1:
        rep["mode"] = "single"
        rep["notes"].append("alleen %s bestaat; een map gecontroleerd, niets om mee te vergelijken" % aanwezig[0])
    elif same_directory(aanwezig[0], aanwezig[1]):
        rep["mode"] = "same-inode"
        aanwezig = aanwezig[:1]
        rep["notes"].append("de patches/ van de loader (%s) en de patches-repo (%s) zijn DEZELFDE map (zelfde st_dev/st_ino, koppeling): "
                            "die ene map is tegen het schema gelegd; er is GEEN vergelijking tussen twee repo's gedaan" % (loader, repo))
    else:
        rep["mode"] = "distinct"
    for r in aanwezig:
        rep["roots_validated"].append(r)
        rep["issues"].update(validate_root(r))
        for item in shared_item_folders(r):
            ps = shared_item_problems(item)
            if ps:
                rep["issues"][item] = ps
    if rep["mode"] == "distinct":
        rep["cross_repo_comparison"] = True
        namen = [{os.path.basename(d): d for d in game_folders(r)} for r in aanwezig]
        for n in sorted(set(namen[0]) & set(namen[1])):
            a, b = ship_fingerprint(namen[0][n]), ship_fingerprint(namen[1][n])
            if a != b:
                verschil = sorted(set(a) ^ set(b)) + sorted(k for k in set(a) & set(b) if a[k] != b[k])
                rep["conflicts"].append("%s staat op twee plaatsen met een verschillende levering: %s" % (n, ", ".join(verschil[:6])))
        # Gedeelde items net zo vergelijken: hetzelfde item op twee plaatsen moet byte voor byte kloppen, anders werkt een
        # game "alleen op mijn machine" doordat het item in de ene repo anders is dan in de andere.
        sitems = [{os.path.basename(d): d for d in shared_item_folders(r)} for r in aanwezig]
        for n in sorted(set(sitems[0]) & set(sitems[1])):
            a, b = shared_fingerprint(sitems[0][n]), shared_fingerprint(sitems[1][n])
            if a != b:
                verschil = sorted(set(a) ^ set(b)) + sorted(k for k in set(a) & set(b) if a[k] != b[k])
                rep["conflicts"].append("gedeeld item %s staat op twee plaatsen met verschillende inhoud: %s" % (n, ", ".join(verschil[:6])))
        rep["notes"].append("twee verschillende mappen: beide tegen het schema gelegd, %d gedeelde games en %d gedeelde items vergeleken"
                            % (len(set(namen[0]) & set(namen[1])), len(set(sitems[0]) & set(sitems[1]))))
    return rep


def main(argv):
    import gamesettings as gs
    # Standaard de meegeleverde lagen (loader, ontwikkel-checkout); de gebruikerslaag bevat alleen afwijkingen en hoeft geen game.ini te hebben.
    roots = argv or [p for soort, p in gs.patch_layers() if soort != "user"]
    vals = [r for r in roots if os.path.isdir(r)]
    uniek = []
    for r in vals:
        if any(same_directory(r, u) for u in uniek):
            print("%s: dezelfde map als een eerdere wortel (koppeling); eenmaal gecontroleerd, geen vergelijking tussen twee indelingen" % r)
            continue
        uniek.append(r)
    fout = 0
    for r in uniek:
        res = validate_root(r)
        print("%s: %d gamemappen, %d met afwijkingen" % (r, len(game_folders(r)), len(res)))
        for d, ps in res.items():
            for code, t in ps:
                print("  [%s] %s" % (code, t))
                fout += 1
    return 1 if fout else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

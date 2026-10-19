#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Het schema van een gamemap (tools/dev/patchlayout.py; docs/per-game-patches.md, "Indeling van een gamemap") en de scheiding tussen
levering en onderzoek. Uitvoeren: python3 -I tests/test_patches_layout.py

Twee soorten tests:
  - FIXTURES (tijdelijke mappen): bewijzen dat het schema kan FALEN: een map zonder game.ini, een dropin.ini die een bestand noemt dat
    er niet staat, onderzoeksmateriaal in de levering, een onbekende naam, een kopie die van de andere afwijkt.
  - DE ECHTE BOOM: de patches/ van de loader en de patches-repo. Op een ontwikkelmachine zijn dat DEZELFDE map (koppeling); de test
    stelt dat vast met st_dev/st_ino, legt die ene map tegen het schema en zegt hardop dat hij geen vergelijking tussen twee
    repo's heeft gedaan. Staan ze elders (of zijn gamemappen naar de loader verhuisd), dan worden beide gecontroleerd en vergeleken.
"""
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPTS = os.path.join(ROOT, "scripts")
sys.dont_write_bytecode = True
sys.path.insert(0, SCRIPTS)
DEVTOOLS = os.path.join(ROOT, "tools", "dev")   # patchlayout
sys.path.insert(0, DEVTOOLS)
import gamesettings as gs  # noqa: E402
import patchlayout as pl  # noqa: E402


def write(path, text="x"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def maak_game(root, naam, dropin=True, extra=()):
    """Een gamemap die aan het schema voldoet (en optioneel met extra bestanden)."""
    d = os.path.join(root, naam)
    write(os.path.join(d, "game.ini"), "[Game]\nProfile = %s\n" % naam)
    write(os.path.join(d, "patches.ini"), "[Patch: P]\nEnabled = true\n")
    if dropin:
        write(os.path.join(d, "dropin", "dropin.ini"), "[proef.dll]\nTo = .\n[sub/ding.json]\nTo = .\n")
        write(os.path.join(d, "dropin", "proef.dll"), "MZ")
        write(os.path.join(d, "dropin", "sub", "ding.json"), "{}")
    write(os.path.join(d, "data", "tabel.bin"), "1")
    for e in extra:
        write(os.path.join(d, e), "onderzoek")
    return d


def verhuis(van, naar, behalve=None):
    """Kopieer de levering (ship_set) van een gamemap naar een andere plek: wat een verhuizing naar de loader zou meenemen."""
    for rel in pl.ship_set(van):
        if behalve and rel.endswith(behalve):
            continue
        os.makedirs(os.path.dirname(os.path.join(naar, rel)), exist_ok=True)
        shutil.copy2(os.path.join(van, rel), os.path.join(naar, rel))


def codes(problemen):
    return sorted(c for c, _t in problemen)


class Fixtures(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = self._td.name
        self.root = os.path.join(self.tmp, "patches")

    def tearDown(self):
        self._td.cleanup()

    # ---- het schema -------------------------------------------------------------------------------------------------

    def test_een_voldoende_map_heeft_geen_afwijkingen(self):
        d = maak_game(self.root, "Goed", extra=["README.md", "logs-20261013/a.log", "bewijs-5min/x.txt", "probe/p.sh",
                                                 "RAPPORT.md.voor-correctie-2026-10-13", "foto.png", "run8-exit"])
        self.assertEqual(pl.validate_game_folder(d), [])

    def test_een_map_zonder_game_ini_faalt(self):
        d = maak_game(self.root, "Kaal")
        os.remove(os.path.join(d, "game.ini"))
        self.assertEqual(codes(pl.validate_game_folder(d)), ["missing-required"])
        self.assertIn(d, pl.validate_root(self.root))     # en de wortel meldt hem

    def test_een_onbekende_naam_wordt_gemeld(self):
        d = maak_game(self.root, "Rommel", extra=["raar.dat", "iets.bin"])
        problemen = pl.validate_game_folder(d)
        self.assertEqual(codes(problemen), ["unknown-entry", "unknown-entry"])
        self.assertTrue(any("raar.dat" in t for _c, t in problemen))

    def test_hasp_haken_ini_is_levering(self):
        # Beslissing 2026-10-13: de adreslijst van de HASP-ELF-hook is runtime-materiaal (LEVERING),
        # geen onbekende naam. Hij moet dus meeverhuizen en mag geen unknown-entry geven.
        d = maak_game(self.root, "MetHaken", extra=["hasp-haken.ini"])
        self.assertEqual(pl.classify("hasp-haken.ini"), "ship")
        self.assertEqual(codes(pl.validate_game_folder(d)), [])
        self.assertIn("hasp-haken.ini", pl.ship_set(d))

    def test_onderzoek_in_de_levering_faalt(self):
        d = maak_game(self.root, "Besmet", extra=["dropin/NOTITIES.md", "dropin/logs-oud/x.log", "data/bewijs-1/y", "data/screenshot.png",
                                                  "dropin/OUD.md.voor-correctie-1"])
        self.assertEqual(codes(pl.validate_game_folder(d)), ["ship-contaminated"] * 5)
        self.assertEqual(len(pl.research_in_ship(d)), 5)

    def test_gewone_leveringsbestanden_zijn_geen_onderzoek(self):
        # .py/.sh/.txt/.json binnen dropin/ kunnen echte levering zijn (alleen op het hoogste niveau zijn ze onderzoek)
        d = maak_game(self.root, "Levering", extra=["dropin/hulp.py", "dropin/start.sh", "dropin/lees.txt", "data/tabel.json"])
        self.assertEqual(pl.validate_game_folder(d), [])
        self.assertEqual(pl.research_set(d), [])

    def test_publieke_export_en_privebehoud(self):
        d = maak_game(self.root, "Goed")
        private = "Goed/dropin/ravengun-hasp/memory/dump"
        write(os.path.join(self.root, private), "private")
        write(os.path.join(d, "dropin/dropin.ini"), "[proef.dll]\nTo = .\n[" + private.split("/dropin/", 1)[1] + "]\nTo = .\n")
        public = os.path.join(self.tmp, "public")
        self.assertGreater(pl._ship.ship(self.root, public), 0)
        # De repository is privé: dongle- en HASP-gegevens horen juist in de levering.
        self.assertTrue(os.path.exists(os.path.join(public, private)))
        with open(os.path.join(public, private)) as f:
            self.assertEqual(f.read(), "private")
        self.assertEqual(pl.validate_game_folder(os.path.join(public, "Goed")), [])
        user = os.path.join(self.tmp, "user")
        write(os.path.join(user, private), "owner")
        write(os.path.join(user, "Goed/dropin/dropin.ini"), "[proef.dll]\nTo = custom\n")
        self.assertEqual(pl._ship.preserve_user(self.root, user), 0)
        with open(os.path.join(user, private)) as f:
            self.assertEqual(f.read(), "owner")
        with open(os.path.join(user, "Goed/dropin/dropin.ini")) as f:
            text = f.read()
        self.assertIn("To = custom", text)
        self.assertIn("[ravengun-hasp/memory/dump]", text)
        self.assertEqual(pl._ship.preserve_user(self.root, user), 0)
        with open(os.path.join(user, "Goed/dropin/dropin.ini")) as f:
            self.assertEqual(f.read(), text)

    def test_privebehoud_meerregelige_waarde_en_kopcommentaar(self):
        # De privésectie wordt ONBEWERKT overgenomen: een meerregelige waarde blijft één waarde
        # (geen rij sleutels) en een sectiekop met tekst erachter telt ook mee (configparser leest
        # '[x] ; toelichting' als sectie x).
        d = maak_game(self.root, "Goed")
        write(os.path.join(self.root, "Goed/dropin/ravengun-hasp/memory/dump"), "private")
        write(os.path.join(d, "dropin/dropin.ini"),
              "[proef.dll]\nTo = .\n\n"
              "[ravengun-hasp/memory/dump]  ; de dump van de kast\n"
              "To = .\n"
              "Sleutel = een\n"
              "  vervolg = twee\n"
              "  slot = drie\n")
        user = os.path.join(self.tmp, "user")
        write(os.path.join(user, "Goed/dropin/dropin.ini"), "[proef.dll]\nTo = custom\n")
        pl._ship.preserve_user(self.root, user)
        with open(os.path.join(user, "Goed/dropin/dropin.ini")) as f:
            text = f.read()
        self.assertIn("[ravengun-hasp/memory/dump]  ; de dump van de kast", text)
        self.assertIn("Sleutel = een\n  vervolg = twee\n  slot = drie\n", text)
        # De gebruikerslaag houdt zijn eigen sleutel en krijgt de privésectie erachter.
        self.assertIn("To = custom", text)
        self.assertLess(text.index("To = custom"), text.index("[ravengun-hasp/memory/dump]"))

    def test_privebehoud_ingesprongen_haakjesregel_is_een_voortzetting(self):
        # In configparser is een ingesprongen regel onder een sleutelregel een voortzetting van de waarde, ook als hij met '[' begint.
        # Alleen een regel die dat niet is, is een kop. De privesectie moet daarom ONBEWERKT en tot de echte volgende kop meekomen.
        import configparser
        d = maak_game(self.root, "Goed")
        write(os.path.join(self.root, "Goed/dropin/ravengun-hasp/memory/dump"), "private")
        origineel = ("[proef.dll]\nTo = .\n\n"
                     "[ravengun-hasp/memory/dump]\n"
                     "To = .\n"
                     "k = a\n"
                     "  [0x10] = 1\n"
                     "\n"
                     "  # commentaar midden in de waarde\n"
                     "  slot\n"
                     "[Volgende]\n"
                     "X = 1\n")
        write(os.path.join(d, "dropin/dropin.ini"), origineel)
        user = os.path.join(self.tmp, "user")
        write(os.path.join(user, "Goed/dropin/dropin.ini"), "[proef.dll]\nTo = custom\n")
        pl._ship.preserve_user(self.root, user)
        with open(os.path.join(user, "Goed/dropin/dropin.ini")) as f:
            text = f.read()
        self.assertIn("k = a\n  [0x10] = 1\n", text)
        self.assertIn("  slot\n", text)
        self.assertNotIn("[Volgende]", text)
        # De overgenomen sectie leest configparser hetzelfde als het origineel.
        oud, nieuw = (configparser.RawConfigParser(strict=False) for _ in range(2))
        for cp in (oud, nieuw):
            cp.optionxform = str
        oud.read_string(origineel)
        nieuw.read_string(text)
        sec = "ravengun-hasp/memory/dump"
        self.assertEqual(dict(nieuw.items(sec)), dict(oud.items(sec)))
        self.assertEqual(nieuw.get(sec, "k"), "a\n[0x10] = 1\n\nslot")
        # Een gewone, niet ingesprongen kop erna is wel een kop (en hoort er niet bij).
        self.assertIn("Volgende", oud.sections())
        self.assertNotIn("Volgende", nieuw.sections())

    def test_levering_regex_gelijk_aan_cmake_installatielijst(self):
        # ship-patches.py (LEVERING), de install(DIRECTORY patches ...)-regels in CMakeLists.txt en de controles in de shelltests zijn
        # drie schrijfwijzen van dezelfde lijst; een bestand dat in de een mist (zoals <Game>/dongle.json) komt anders niet mee.
        import re
        with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as f:
            cmake = f.read()
        m = re.search(r'REGEX "\^\$\{_rg_patches_rx\}/\[\^/\]\+/\(([^)]*)\)\$"', cmake)
        self.assertIsNotNone(m, "de bestandenlijst in CMakeLists.txt is niet gevonden")
        cmake_files = {x.replace("\\\\.", ".") for x in m.group(1).split("|")}
        cmake_trees = set(re.findall(r'REGEX "\^\$\{_rg_patches_rx\}/\[\^/\]\+/(\w+)/"', cmake))
        pat = pl._ship.LEVERING.pattern
        a = re.search(r"\^\[\^/\]\+/\(\?:([^)]*)\)\$", pat)
        b = re.search(r"\^\[\^/\]\+/\(\?:([^)]*)\)/", pat)
        self.assertTrue(a and b)
        ship_files = {x.replace("\\.", ".") for x in a.group(1).split("|")}
        ship_trees = set(b.group(1).split("|"))
        self.assertEqual(ship_files, cmake_files)
        self.assertEqual(ship_trees, cmake_trees)
        self.assertIn("dongle.json", ship_files)
        # En in de praktijk: een <Game>/dongle.json gaat mee, een <Game>/dongle.txt niet.
        d = maak_game(self.root, "Dongle")
        write(os.path.join(d, "dongle.json"), "{}")
        write(os.path.join(d, "dongle.txt"), "geen levering")
        uit = os.path.join(self.tmp, "uit")
        pl._ship.ship(self.root, uit)
        self.assertTrue(os.path.exists(os.path.join(uit, "Dongle", "dongle.json")))
        self.assertFalse(os.path.exists(os.path.join(uit, "Dongle", "dongle.txt")))
        self.assertIn("dongle.json", pl.ship_set(d))

    # ---- levering compleet ------------------------------------------------------------------------------------------

    def test_een_dropin_zonder_het_bestand_faalt(self):
        d = maak_game(self.root, "Leeg")
        os.remove(os.path.join(d, "dropin", "proef.dll"))
        problemen = pl.validate_game_folder(d)
        self.assertEqual(codes(problemen), ["dropin-missing-file"])
        self.assertIn("proef.dll", problemen[0][1])

    def test_een_geneste_sectie_zonder_bestand_faalt(self):
        d = maak_game(self.root, "Genest")
        os.remove(os.path.join(d, "dropin", "sub", "ding.json"))
        self.assertEqual(codes(pl.validate_game_folder(d)), ["dropin-missing-file"])

    def test_een_dropin_map_zonder_dropin_ini_faalt(self):
        d = maak_game(self.root, "ZonderIni")
        os.remove(os.path.join(d, "dropin", "dropin.ini"))
        self.assertEqual(codes(pl.validate_game_folder(d)), ["dropin-no-ini"])

    def test_een_kapotte_dropin_ini_wordt_gemeld(self):
        d = maak_game(self.root, "Kapot")
        write(os.path.join(d, "dropin", "dropin.ini"), "geen sectie\n")
        self.assertEqual(codes(pl.validate_game_folder(d)), ["dropin-bad-ini"])

    def test_een_game_zonder_dropin_hoeft_er_geen_te_hebben(self):
        d = maak_game(self.root, "ZonderDropin", dropin=False)
        self.assertEqual(pl.validate_game_folder(d), [])

    def test_de_naam_van_de_map_moet_het_profile_zijn(self):
        d = maak_game(self.root, "Naam")
        write(os.path.join(d, "game.ini"), "[Game]\nProfile = Anders\n")
        self.assertEqual(codes(pl.validate_game_folder(d)), ["profile-mismatch"])

    # ---- levering en onderzoek scheiden -----------------------------------------------------------------------------

    def test_ship_set_en_research_set(self):
        d = maak_game(self.root, "Scheiding", extra=["README.md", "logs-1/a", "bewijs-2/b", "probe/c", "OUD.md.voor-correctie-1", "x.png"])
        ship = pl.ship_set(d)
        self.assertEqual(sorted(ship), sorted(["game.ini", "patches.ini", "dropin/dropin.ini", "dropin/proef.dll", "dropin/sub/ding.json",
                                               "data/tabel.bin"]))
        self.assertEqual(pl.research_set(d), sorted(["README.md", "OUD.md.voor-correctie-1", "bewijs-2", "logs-1", "probe", "x.png"]))
        self.assertEqual(set(ship) & set(pl.research_set(d)), set())
        self.assertEqual(pl.research_in_ship(d), [])

    def test_onderzoek_gaat_niet_mee_met_het_verhuizen(self):
        # wat ship_set() noemt kopieren we naar de loader: er mag niets van het onderzoek bij zijn
        d = maak_game(self.root, "Verhuis", extra=["README.md", "logs-1/a", "probe/c", "run3-uitvoer/z"])
        doel = os.path.join(self.tmp, "loader", "Verhuis")
        verhuis(d, doel)
        self.assertEqual(pl.research_set(doel), [])
        self.assertEqual(pl.unknown_entries(doel), [])
        self.assertEqual(pl.validate_game_folder(doel, check_profile=False), [])   # dropin.ini + bestanden zijn meeverhuisd

    def test_verhuizen_van_alleen_het_halve_dropin_faalt(self):
        d = maak_game(self.root, "Half")
        doel = os.path.join(self.tmp, "loader", "Half")
        verhuis(d, doel, behalve="proef.dll")
        self.assertEqual(codes(pl.validate_game_folder(doel, check_profile=False)), ["dropin-missing-file"])

    def test_koppelingen_naar_mappen_in_de_levering_worden_niet_gevolgd(self):
        d = maak_game(self.root, "Lus")
        os.symlink(self.tmp, os.path.join(d, "dropin", "terug"))     # een lus: os.walk volgt hem niet
        pl.ship_set(d)                                               # stopt

    # ---- twee mappen of een ----------------------------------------------------------------------------------------

    def test_dezelfde_map_via_een_koppeling_wordt_herkend_en_niet_als_twee_gerekend(self):
        maak_game(self.root, "A")
        koppeling = os.path.join(self.tmp, "loader-patches")
        os.symlink(self.root, koppeling)
        self.assertTrue(pl.same_directory(koppeling, self.root))
        rep = pl.check_roots(koppeling, self.root)
        self.assertEqual(rep["mode"], "same-inode")
        self.assertFalse(rep["cross_repo_comparison"], "twee namen voor een map is GEEN vergelijking tussen twee repo's")
        self.assertEqual(len(rep["roots_validated"]), 1)
        self.assertIn("GEEN vergelijking", " ".join(rep["notes"]))
        self.assertEqual(rep["issues"], {})

    def test_dezelfde_map_met_een_afwijking_faalt_ook_dan(self):
        d = maak_game(self.root, "A", extra=["dropin/LEESMIJ.md"])
        koppeling = os.path.join(self.tmp, "loader-patches")
        os.symlink(self.root, koppeling)
        rep = pl.check_roots(koppeling, self.root)
        self.assertEqual(rep["mode"], "same-inode")
        self.assertEqual([os.path.realpath(k) for k in rep["issues"]], [os.path.realpath(d)])

    def test_twee_verschillende_mappen_worden_allebei_gecontroleerd_en_vergeleken(self):
        loader, repo = os.path.join(self.tmp, "loader"), os.path.join(self.tmp, "repo")
        maak_game(loader, "Beide")
        maak_game(repo, "Beide")
        maak_game(repo, "AlleenRepo")
        rep = pl.check_roots(loader, repo)
        self.assertEqual((rep["mode"], rep["cross_repo_comparison"]), ("distinct", True))
        self.assertEqual(sorted(rep["roots_validated"]), sorted([loader, repo]))
        self.assertEqual((rep["issues"], rep["conflicts"]), ({}, []))
        # nu een afwijking in elk van beide: beide worden gezien
        write(os.path.join(loader, "Beide", "dropin", "proef.dll"), "ANDERS")
        write(os.path.join(repo, "AlleenRepo", "stray.bin"), "x")
        rep = pl.check_roots(loader, repo)
        self.assertTrue(any("Beide" in c and "proef.dll" in c for c in rep["conflicts"]), rep["conflicts"])
        self.assertIn(os.path.join(repo, "AlleenRepo"), rep["issues"])
        os.remove(os.path.join(repo, "Beide", "game.ini"))
        self.assertIn(os.path.join(repo, "Beide"), pl.check_roots(loader, repo)["issues"])

    def test_geen_van_beide_bestaat_is_geen_succes_maar_een_melding(self):
        rep = pl.check_roots(os.path.join(self.tmp, "x"), os.path.join(self.tmp, "y"))
        self.assertEqual((rep["mode"], rep["roots_validated"]), ("none", []))
        self.assertTrue(rep["notes"])


class EchteBoom(unittest.TestCase):
    """De werkelijke patches/ van de loader (en de patches-repo ernaast). Wat hier faalt, is echte afwijking van het schema."""

    @classmethod
    def setUpClass(cls):
        cls.loader = gs.loader_dir()
        cls.repo = os.environ.get("RAVENGUN_PATCHES_DIR") or ""  # oude checkout alleen op expliciet verzoek vergelijken
        cls.rep = pl.check_roots(cls.loader, cls.repo)
        for n in cls.rep["notes"]:
            print("patches-indeling: " + n, file=sys.stderr)

    def setUp(self):
        if self.rep["mode"] == "none":
            self.skipTest("geen patches/ aanwezig (loader: %s, patches-repo: %s): niets te controleren" % (self.loader, self.repo))

    def _problemen(self, *soorten):
        return ["[%s] %s" % (c, t) for ps in self.rep["issues"].values() for c, t in ps if c in soorten]

    def test_de_modus_is_vastgesteld_en_gezegd(self):
        both = [p for p in (self.loader, self.repo) if os.path.isdir(p)]
        if len(both) == 2 and pl.same_directory(*both):
            self.assertEqual(self.rep["mode"], "same-inode")
            self.assertFalse(self.rep["cross_repo_comparison"])
            self.assertTrue(any("GEEN vergelijking" in n for n in self.rep["notes"]))
        elif len(both) == 2:
            self.assertEqual(self.rep["mode"], "distinct")
            self.assertTrue(self.rep["cross_repo_comparison"])
        self.assertEqual(len(self.rep["roots_validated"]), 1 if self.rep["mode"] in ("same-inode", "single") else 2)

    def test_elke_gamemap_heeft_de_verplichte_bestanden(self):
        self.assertEqual(self._problemen("missing-required"), [])

    def test_levering_is_compleet(self):
        # een dropin.ini die een bestand noemt dat er niet staat, of een dropin/ zonder dropin.ini
        self.assertEqual(self._problemen("dropin-missing-file", "dropin-no-ini", "dropin-bad-ini"), [])

    def test_levering_bevat_geen_onderzoek(self):
        self.assertEqual(self._problemen("ship-contaminated"), [])

    def test_geen_onbekende_namen_in_een_gamemap(self):
        self.assertEqual(self._problemen("unknown-entry"), [])

    def test_mapnaam_is_het_profile(self):
        self.assertEqual(self._problemen("profile-mismatch"), [])

    def test_beide_mappen_geven_geen_conflict_als_ze_verschillen(self):
        self.assertEqual(self.rep["conflicts"], [])


if __name__ == "__main__":
    unittest.main()

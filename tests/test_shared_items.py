#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Gedeelde items (docs/per-game-patches.md, "Gedeelde items"). Raakt alleen tijdelijke mappen. Uitvoeren: python3 -I tests/test_shared_items.py

Wat hier vastligt (en FAALT als het breekt):
  - shared/ is gedeeld materiaal, GEEN gamemap (patchlayout.game_folders slaat hem over; de twee SHARED_DIR's zijn gelijk)
  - niets staat impliciet aan: een game zonder [Shared] zet geen enkel item aan
  - de voorrang: binnen een wortel gaat de eigen map van de game voor de gedeelde standaard; een hogere wortel (gebruiker)
    gaat boven beide -- een wortel als buitenste sleutel, de soort (shared < game) als binnenste
  - elk opgelost bestand noemt de laag waaruit het komt (een run kan zeggen wat hij gebruikt heeft)
  - een aangezet item dat in geen enkele wortel bestaat is een HARDE fout, geen stilte
  - de driftcontrole (patchlayout.check_roots) ziet een gedeeld item dat op twee plaatsen verschilt, en wordt niet voor de
    gek gehouden door een koppeling die van de twee mappen een maakt
"""
import os
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
import gamesettings as gs        # noqa: E402
import patchlayout as pl         # noqa: E402

ENV_KEYS = ("RAVENGUN_LOADER_PATCHES_DIR", "RAVENGUN_USER_PATCHES_DIR", "RAVENGUN_PATCHES_DIR", "XDG_DATA_HOME", "RAVENGUN_NO_LEGACY_ENV")


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class Lagen(unittest.TestCase):
    """Drie echte wortels: loader (L), gebruiker (U), ontwikkel-checkout (D). Een game 'Spel' met de mapnaam = profiel."""

    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = self._td.name
        self.saved = dict(os.environ)
        self.L, self.U, self.D = (os.path.join(self.tmp, n) for n in ("loader", "user", "dev"))
        for d in (self.L, self.U, self.D):
            os.makedirs(d)
        self.zet(self.L, self.U, self.D)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self.saved)
        self._td.cleanup()

    def zet(self, L=None, U=None, D=None):
        for k in ENV_KEYS:
            os.environ.pop(k, None)
        for k, v in (("RAVENGUN_LOADER_PATCHES_DIR", L), ("RAVENGUN_USER_PATCHES_DIR", U), ("RAVENGUN_PATCHES_DIR", D)):
            if v:
                os.environ[k] = v
        os.environ["RAVENGUN_NO_LEGACY_ENV"] = "1"

    def game(self, root, shared_section="", profile="Spel"):
        """Een gamemap <root>/Spel met game.ini (profiel + optioneel een [Shared]-sectie)."""
        txt = "[Game]\nProfile = %s\n" % profile
        if shared_section:
            txt += "[Shared]\n" + shared_section
        write(os.path.join(root, profile, "game.ini"), txt)

    def shared_default(self, root, item, rel, inhoud):
        write(os.path.join(root, "shared", item, rel), inhoud)

    def game_override(self, root, item, rel, inhoud, profile="Spel"):
        write(os.path.join(root, profile, "shared", item, rel), inhoud)


class GereserveerdeNaam(Lagen):
    def test_de_twee_SHARED_DIR_constanten_zijn_gelijk(self):
        self.assertEqual(gs.SHARED_DIR, pl.SHARED_DIR)

    def test_shared_is_geen_gamemap(self):
        self.game(self.L)                                  # een echte game
        self.shared_default(self.L, "hasp-emulator", "lib.so", "x")
        namen = [os.path.basename(d) for d in pl.game_folders(self.L)]
        self.assertIn("Spel", namen)
        self.assertNotIn("shared", namen)                  # shared/ telt niet als game

    def test_items_in_de_keuzelijst_komen_uit_alle_wortels_uniek(self):
        self.shared_default(self.L, "hasp-emulator", "a", "1")
        self.shared_default(self.U, "raw-thrills-gun", "b", "2")
        self.shared_default(self.D, "hasp-emulator", "a", "3")   # zelfde naam in twee wortels: een keer
        self.assertEqual(gs.shared_items_available(), ["hasp-emulator", "raw-thrills-gun"])


class NietsImpliciet(Lagen):
    def test_geen_Shared_sectie_zet_niets_aan(self):
        self.game(self.L)                                  # geen [Shared]
        self.shared_default(self.L, "hasp-emulator", "lib.so", "x")   # bestaat wel, maar niet aangezet
        g = gs.load_game("Spel")
        self.assertEqual(g.shared, [])
        self.assertFalse([s for s in g.sources if "shared" in s])

    def test_uitgezet_item_is_uit(self):
        self.game(self.L, "hasp-emulator = off\n")
        self.shared_default(self.L, "hasp-emulator", "lib.so", "x")
        g = gs.load_game("Spel")
        recs = {r["item"]: r for r in g.shared}
        self.assertIn("hasp-emulator", recs)
        self.assertFalse(recs["hasp-emulator"]["enabled"])
        self.assertIsNone(recs["hasp-emulator"]["files"])
        self.assertFalse([s for s in g.sources if "shared" in s])


class Voorrang(Lagen):
    def test_aangezet_item_komt_uit_de_gedeelde_standaard(self):
        self.game(self.L, "hasp-emulator = on\n")
        self.shared_default(self.L, "hasp-emulator", "lib.so", "standaard")
        files = gs.shared_item_files("Spel", "hasp-emulator")
        self.assertIn("lib.so", files)
        pad, soort, _d = files["lib.so"]
        self.assertEqual(soort, "shared")
        self.assertEqual(read(pad), "standaard")

    def test_eigen_map_van_de_game_gaat_voor_de_gedeelde_standaard(self):
        self.game(self.L, "hasp-emulator = on\n")
        self.shared_default(self.L, "hasp-emulator", "lib.so", "standaard")
        self.game_override(self.L, "hasp-emulator", "lib.so", "eigen-van-de-game")
        files = gs.shared_item_files("Spel", "hasp-emulator")
        pad, soort, _d = files["lib.so"]
        self.assertEqual(soort, "game")                    # de eigen map van de game wint binnen dezelfde wortel
        self.assertEqual(read(pad), "eigen-van-de-game")

    def test_gebruiker_gaat_boven_beide(self):
        # loader heeft zowel de gedeelde standaard als een eigen map van de game; de gebruiker heeft alleen een gedeelde
        # standaard -- en die wint, want de gebruiker staat boven de loader (de wortel is de buitenste sleutel).
        self.game(self.L, "hasp-emulator = on\n")
        self.shared_default(self.L, "hasp-emulator", "lib.so", "loader-standaard")
        self.game_override(self.L, "hasp-emulator", "lib.so", "loader-eigen")
        self.shared_default(self.U, "hasp-emulator", "lib.so", "gebruiker-standaard")
        files = gs.shared_item_files("Spel", "hasp-emulator")
        pad, _soort, _d = files["lib.so"]
        self.assertEqual(read(pad), "gebruiker-standaard")

    def test_elk_bestand_noemt_zijn_laag(self):
        self.game(self.L, "hasp-emulator = on\n")
        self.shared_default(self.L, "hasp-emulator", "a.so", "A")
        self.game_override(self.L, "hasp-emulator", "b.so", "B")
        files = gs.shared_item_files("Spel", "hasp-emulator")
        self.assertEqual(files["a.so"][1], "shared")
        self.assertEqual(files["b.so"][1], "game")
        for rel, (pad, soort, d) in files.items():         # de laag is een echte map waar het bestand in zit
            self.assertTrue(os.path.isfile(pad))
            self.assertTrue(pad.startswith(d))


class HardeFout(Lagen):
    def test_aangezet_maar_nergens_is_een_harde_fout(self):
        self.game(self.L, "hasp-emulator = on\n")          # aangezet, maar shared/hasp-emulator bestaat in geen wortel
        g = gs.load_game("Spel")
        self.assertTrue(g.shared_errors)
        self.assertTrue(any("hasp-emulator" in w for w in g.warnings))
        rec = [r for r in g.shared if r["item"] == "hasp-emulator"][0]
        self.assertIsNotNone(rec["error"])
        self.assertIsNone(rec["files"])


class Gebruikerslaag(Lagen):
    def test_de_gebruikerslaag_kan_een_item_aanzetten_of_uitzetten(self):
        # loader zet het uit; de gebruikerslaag zet het aan (game.ini loopt via dezelfde samenvoeging, de latere wint)
        self.game(self.L, "hasp-emulator = off\n")
        self.game(self.U, "hasp-emulator = on\n")
        self.shared_default(self.L, "hasp-emulator", "lib.so", "x")
        g = gs.load_game("Spel")
        rec = [r for r in g.shared if r["item"] == "hasp-emulator"][0]
        self.assertTrue(rec["enabled"])
        self.assertIn("lib.so", rec["files"])


class Drift(unittest.TestCase):
    """patchlayout.check_roots: een gedeeld item dat op twee plaatsen verschilt is een conflict; een koppeling die van de
    twee een map maakt mag GEEN vals 'ze komen overeen' geven."""

    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = self._td.name

    def tearDown(self):
        self._td.cleanup()

    def test_verschillend_gedeeld_item_op_twee_plaatsen_is_een_conflict(self):
        a, b = os.path.join(self.tmp, "a"), os.path.join(self.tmp, "b")
        write(os.path.join(a, "shared", "hasp-emulator", "lib.so"), "versie-1")
        write(os.path.join(b, "shared", "hasp-emulator", "lib.so"), "versie-2")
        rep = pl.check_roots(a, b)
        self.assertEqual(rep["mode"], "distinct")
        self.assertTrue(any("hasp-emulator" in c for c in rep["conflicts"]), rep["conflicts"])

    def test_gelijk_gedeeld_item_op_twee_plaatsen_is_geen_conflict(self):
        a, b = os.path.join(self.tmp, "a"), os.path.join(self.tmp, "b")
        write(os.path.join(a, "shared", "hasp-emulator", "lib.so"), "zelfde")
        write(os.path.join(b, "shared", "hasp-emulator", "lib.so"), "zelfde")
        rep = pl.check_roots(a, b)
        self.assertEqual(rep["conflicts"], [])

    def test_een_koppeling_geeft_geen_valse_overeenkomst(self):
        a = os.path.join(self.tmp, "a")
        write(os.path.join(a, "shared", "hasp-emulator", "lib.so"), "x")
        koppeling = os.path.join(self.tmp, "koppeling")
        os.symlink(a, koppeling)
        rep = pl.check_roots(a, koppeling)
        self.assertEqual(rep["mode"], "same-inode")                 # een map, geen vergelijking tussen twee repo's
        self.assertFalse(rep["cross_repo_comparison"])

    def test_onderzoeksmateriaal_in_een_gedeeld_item_wordt_gemeld(self):
        a = os.path.join(self.tmp, "a")
        write(os.path.join(a, "shared", "hasp-emulator", "lib.so"), "x")
        write(os.path.join(a, "shared", "hasp-emulator", "NOTITIE.md"), "onderzoek")
        rep = pl.check_roots(a, "")
        item = os.path.join(a, "shared", "hasp-emulator")
        self.assertIn(item, rep["issues"])
        self.assertTrue(any(code == "research-in-shared" for code, _t in rep["issues"][item]))


if __name__ == "__main__":
    unittest.main(verbosity=2)

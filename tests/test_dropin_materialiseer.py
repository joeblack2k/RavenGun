#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor het materialiseren van een view-map bij een drop-in.

De views van de games zijn symlink-boerderijen naar de NAS: `views/<Game>/Game_Data` is bijvoorbeeld
een symlink. Een drop-in met `To = Game_Data/Plugins` wijst dan via die symlink naar de NAS. In
plaats van weigeren maakt `dropin_materialiseer()` die map lokaal -- dat is precies wat een view is.
Zie `docs/geen-dongle-in-de-loader.md`.

Deze tests bouwen hun eigen namaak-view en namaak-NAS, zodat ze niets aanraken.
"""
import importlib.util
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def laad_launcher():
    """scripts/launch-direct.py heeft een streepje in de naam, dus met importlib."""
    pad = os.path.join(REPO, "scripts", "launch-direct.py")
    spec = importlib.util.spec_from_file_location("launch_direct", pad)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["launch_direct"] = mod
    spec.loader.exec_module(mod)
    return mod


ld = laad_launcher()


class TestMaterialiseer(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-dropin-")
        # de namaak-NAS: een map met een submap en een bestand
        self.nas = os.path.join(self.tmp, "nas")
        os.makedirs(os.path.join(self.nas, "Game_Data", "Plugins"))
        with open(os.path.join(self.nas, "Game_Data", "Plugins", "origineel.dll"), "w") as f:
            f.write("origineel")
        with open(os.path.join(self.nas, "leesmij.txt"), "w") as f:
            f.write("nas")
        # de namaak-view: een echte map met daarin een symlink naar de NAS
        self.view = os.path.join(self.tmp, "view")
        os.makedirs(self.view)
        os.symlink(os.path.join(self.nas, "Game_Data"), os.path.join(self.view, "Game_Data"))
        os.symlink(os.path.join(self.nas, "leesmij.txt"), os.path.join(self.view, "leesmij.txt"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_symlink_map_wordt_echt(self):
        dest = os.path.join(self.view, "Game_Data", "Plugins", "nieuw.dll")
        self.assertTrue(os.path.islink(os.path.join(self.view, "Game_Data")))
        ok, msg = ld.dropin_materialiseer(self.view, dest)
        self.assertTrue(ok, msg)
        self.assertFalse(os.path.islink(os.path.join(self.view, "Game_Data")),
                         "Game_Data moet nu een echte map zijn")
        self.assertTrue(os.path.isdir(os.path.join(self.view, "Game_Data")))

    def test_de_inhoud_blijft_bereikbaar_via_symlinks(self):
        dest = os.path.join(self.view, "Game_Data", "Plugins", "nieuw.dll")
        ld.dropin_materialiseer(self.view, dest)
        oud = os.path.join(self.view, "Game_Data", "Plugins", "origineel.dll")
        self.assertTrue(os.path.isfile(oud), "het originele bestand moet bereikbaar blijven")
        with open(oud) as f:
            self.assertEqual(f.read(), "origineel")
        self.assertTrue(os.path.islink(oud), "en het moet een symlink naar de NAS zijn")

    def test_het_bestand_wordt_echt_geschreven(self):
        dest = os.path.join(self.view, "Game_Data", "Plugins", "nieuw.dll")
        ok, _ = ld.dropin_materialiseer(self.view, dest)
        self.assertTrue(ok)
        with open(dest, "w") as f:
            f.write("dropin")
        self.assertFalse(os.path.islink(dest))
        with open(dest) as f:
            self.assertEqual(f.read(), "dropin")

    def test_de_nas_blijft_ongemoeid(self):
        dest = os.path.join(self.view, "Game_Data", "Plugins", "nieuw.dll")
        ld.dropin_materialiseer(self.view, dest)
        with open(dest, "w") as f:
            f.write("dropin")
        nas_bestand = os.path.join(self.nas, "Game_Data", "Plugins", "origineel.dll")
        with open(nas_bestand) as f:
            self.assertEqual(f.read(), "origineel", "de NAS mag niet veranderd zijn")
        self.assertFalse(os.path.exists(os.path.join(self.nas, "Game_Data", "Plugins", "nieuw.dll")),
                         "het drop-in-bestand mag niet op de NAS belanden")

    def test_doel_buiten_de_view_wordt_geweigerd(self):
        ok, msg = ld.dropin_materialiseer(self.view, os.path.join(self.nas, "Game_Data", "x.dll"))
        self.assertFalse(ok)
        self.assertIn("buiten de view", msg)

    def test_bestand_dat_zelf_een_symlink_is(self):
        dest = os.path.join(self.view, "leesmij.txt")
        self.assertTrue(os.path.islink(dest))
        ok, _ = ld.dropin_materialiseer(self.view, dest)
        self.assertTrue(ok)
        self.assertFalse(os.path.islink(dest), "de symlink moet weg zijn zodat er echt geschreven wordt")
        # en de NAS-versie is nog intact
        with open(os.path.join(self.nas, "leesmij.txt")) as f:
            self.assertEqual(f.read(), "nas")

    def test_map_die_nog_niet_bestaat_wordt_gemaakt(self):
        dest = os.path.join(self.view, "NieuweMap", "diep", "x.dll")
        ok, msg = ld.dropin_materialiseer(self.view, dest)
        self.assertTrue(ok, msg)
        self.assertTrue(os.path.isdir(os.path.join(self.view, "NieuweMap", "diep")))

    def test_tweede_keer_is_geen_fout(self):
        dest = os.path.join(self.view, "Game_Data", "Plugins", "nieuw.dll")
        self.assertTrue(ld.dropin_materialiseer(self.view, dest)[0])
        self.assertTrue(ld.dropin_materialiseer(self.view, dest)[0], "idempotent")

    def test_binnen_helper(self):
        self.assertTrue(ld._binnen(os.path.join(self.view, "a", "b"), self.view))
        self.assertTrue(ld._binnen(self.view, self.view))
        self.assertFalse(ld._binnen(os.path.join(self.tmp, "elders"), self.view))


class TestDropinIni(unittest.TestCase):
    """De dropin.ini van een game wordt gelezen zoals hij op schijf staat.

    Een drop-in wordt door de GEBRUIKER gemaakt (patches/<Game>/dropin/dropin.ini). Wie dat in
    Windows-Kladblok doet, krijgt een UTF-8-BOM voor het bestand. configparser ziet de eerste regel
    dan niet als commentaar en weigert het hele bestand met "File contains no section header",
    waarna de drop-in stil niet wordt uitgerold. Twee meegeleverde drop-ins hadden die BOM al.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-dropinini-")
        self.shipped = os.path.join(self.tmp, "patches")
        self.user = os.path.join(self.tmp, "gebruiker")
        os.makedirs(os.path.join(self.shipped, "ProefGame", "dropin"))
        os.makedirs(self.user)
        self._user_dir, self._shipped_dir = ld.gs.user_dir, ld.gs.shipped_dir
        ld.gs.user_dir = lambda: self.user
        ld.gs.shipped_dir = lambda: self.shipped

    def tearDown(self):
        ld.gs.user_dir, ld.gs.shipped_dir = self._user_dir, self._shipped_dir
        shutil.rmtree(self.tmp, ignore_errors=True)

    def schrijf_ini(self, tekst, bom=False):
        with open(os.path.join(self.shipped, "ProefGame", "dropin", "dropin.ini"), "wb") as f:
            if bom:
                f.write(b"\xef\xbb\xbf")
            f.write(tekst.encode("utf-8"))

    def test_een_ini_met_bom_wordt_gelezen(self):
        self.schrijf_ini("[proef.dll]\nTo = .\nPreload = true\n", bom=True)
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(fouten, [])
        self.assertEqual([s["name"] for s in specs], ["proef.dll"])
        self.assertTrue(specs[0]["preload"])

    def test_een_ini_zonder_bom_blijft_werken(self):
        self.schrijf_ini("[proef.dll]\nTo = .\n")
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(fouten, [])
        self.assertEqual([s["name"] for s in specs], ["proef.dll"])

    def test_een_ini_met_alleen_commentaar_is_geen_fout(self):
        """Een lege drop-in (alleen uitleg) levert geen drop-ins en geen fout op."""
        self.schrijf_ini("; alleen commentaar\n", bom=False)
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(specs, [])
        self.assertEqual(fouten, [])

    def test_een_ini_die_echt_geen_sectie_heeft_wordt_gemeld(self):
        """Niet stil overslaan: een sleutel buiten een sectie is een fout die de eigenaar moet zien."""
        self.schrijf_ini("To = .\n", bom=False)
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(specs, [])
        self.assertEqual(len(fouten), 1)
        self.assertIn("no section header", fouten[0])


class TestNasControle(unittest.TestCase):
    """Een drop-in gaat nooit naar de NAS, maar een view die ZELF naar de NAS wijst is geen fout.

    De view van een game is een symlink-boerderij: views/TC5/TC5/Binaries/Win64 is een symlink naar
    de NAS. Het gevolgde pad van de gamemap komt dus op de NAS uit terwijl het lexicale pad in de
    view ligt. on_nas(base) zou die drop-in onterecht weigeren; run() materialiseert de map juist.
    Alleen een game die buiten de views staat (rechtstreeks van de NAS) wordt geweigerd.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-nascontrole-")
        self.shipped = os.path.join(self.tmp, "patches")
        self.user = os.path.join(self.tmp, "gebruiker")
        self.views = os.path.join(self.tmp, "views")
        self.nas = os.path.join(self.tmp, "nas")
        os.makedirs(os.path.join(self.shipped, "ProefGame", "dropin"))
        os.makedirs(self.user)
        os.makedirs(os.path.join(self.nas, "Binaries", "Win64"))
        os.makedirs(os.path.join(self.views, "ProefGame"))
        # de view: een echte map met daarin een symlink naar de NAS (precies een view)
        os.symlink(os.path.join(self.nas, "Binaries"),
                   os.path.join(self.views, "ProefGame", "Binaries"))
        with open(os.path.join(self.shipped, "ProefGame", "dropin", "dropin.ini"), "w") as f:
            f.write("[proef.dll]\nTo = .\n")
        with open(os.path.join(self.shipped, "ProefGame", "dropin", "proef.dll"), "w") as f:
            f.write("x")
        self._user_dir, self._shipped_dir = ld.gs.user_dir, ld.gs.shipped_dir
        self._views_dir, self._on_nas = ld.op.views_dir, ld.on_nas
        ld.gs.user_dir = lambda: self.user
        ld.gs.shipped_dir = lambda: self.shipped
        ld.op.views_dir = lambda: self.views
        ld.on_nas = lambda p: True          # net als op de Deck: alles onder de mount is "op de NAS"

    def tearDown(self):
        ld.gs.user_dir, ld.gs.shipped_dir = self._user_dir, self._shipped_dir
        ld.op.views_dir, ld.on_nas = self._views_dir, self._on_nas
        shutil.rmtree(self.tmp, ignore_errors=True)

    def plan(self, game):
        return {"profile": "ProefGame", "game": game, "env": {}}

    def test_een_game_in_zijn_view_komt_aan_ook_als_de_view_naar_de_nas_wijst(self):
        game = os.path.join(self.views, "ProefGame", "Binaries", "Win64", "game.exe")
        uit = ld.dropin_plan(self.plan(game))
        self.assertEqual(uit["errors"], [])
        self.assertEqual([f["name"] for f in uit["files"]], ["proef.dll"])
        self.assertEqual(uit["base"], os.path.dirname(game))

    def test_rechtstreeks_van_de_nas_wordt_geweigerd(self):
        game = os.path.join(self.nas, "Binaries", "Win64", "game.exe")
        uit = ld.dropin_plan(self.plan(game))
        self.assertEqual(uit["files"], [])
        self.assertEqual(len(uit["errors"]), 1)
        self.assertIn("rechtstreeks van de NAS", uit["errors"][0])


if __name__ == "__main__":
    unittest.main()

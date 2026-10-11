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


if __name__ == "__main__":
    unittest.main()

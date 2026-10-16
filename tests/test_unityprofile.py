#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor scripts/unityprofile.py (patches/<Game>/unity.json voor de scripts): de lagen (gebruiker wint als geheel, kapot = fout,
geen terugval), de mapnaam in de gamemappen (regel van @nas/<Map>), het eigen pad van de gebruiker, en de gedeelde instellingen.
Hermetisch: eigen lagen en een eigen library.json in een tijdelijke map."""
import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "scripts"))
import unityprofile as up  # noqa: E402


def schrijf(pad, tekst):
    os.makedirs(os.path.dirname(pad), exist_ok=True)
    with open(pad, "w", encoding="utf-8") as f:
        f.write(tekst)


class UnityProfile(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-unityprofile-")
        self.shipped = os.path.join(self.tmp, "patches")
        self.user = os.path.join(self.tmp, "user")
        self.games = os.path.join(self.tmp, "nas")
        self.lib = os.path.join(self.tmp, "library.json")
        os.makedirs(self.shipped)
        self.saved = {k: os.environ.get(k) for k in ("RAVENGUN_LOADER_PATCHES_DIR", "RAVENGUN_USER_PATCHES_DIR", "RAVENGUN_PATCHES_DIR")}
        os.environ["RAVENGUN_LOADER_PATCHES_DIR"] = self.shipped
        os.environ["RAVENGUN_USER_PATCHES_DIR"] = self.user
        os.environ.pop("RAVENGUN_PATCHES_DIR", None)
        schrijf(self.lib, json.dumps({"folders": [{"path": self.games, "recursive": True}]}))
        schrijf(os.path.join(self.shipped, "RabbidsHollywood", "unity.json"),
                json.dumps({"name": "Rabbids", "type": "rabbids_hollywood", "folder": "Rabbids Hollywood ", "width": 1280}))
        os.makedirs(os.path.join(self.games, "Rabbids Hollywood "))

    def tearDown(self):
        for k, v in self.saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_mapnaam_in_de_gamemappen_met_spatie_aan_het_eind(self):
        _route, g, bron = up.load_game("RabbidsHollywood", self.lib)
        self.assertEqual(g["nas_dir"], os.path.join(self.games, "Rabbids Hollywood "))
        self.assertEqual(g["exe"], "Game.exe")
        self.assertEqual(bron, os.path.join(self.shipped, "RabbidsHollywood", "unity.json"))

    def test_de_naam_moet_exact_kloppen(self):
        shutil.rmtree(os.path.join(self.games, "Rabbids Hollywood "))
        os.makedirs(os.path.join(self.games, "Rabbids Hollywood"))
        with self.assertRaisesRegex(up.UnityError, "staat in geen gamemap"):
            up.load_game("RabbidsHollywood", self.lib)

    def test_een_niveau_dieper_en_de_eerste_directe_treffer_gaat_voor(self):
        shutil.rmtree(os.path.join(self.games, "Rabbids Hollywood "))
        diep = os.path.join(self.tmp, "diep", "Teknoparrot", "Rabbids Hollywood ")
        os.makedirs(diep)
        schrijf(self.lib, json.dumps({"folders": [os.path.join(self.tmp, "diep")]}))
        self.assertEqual(up.load_game("RabbidsHollywood", self.lib)[1]["nas_dir"], diep)
        direct = os.path.join(self.tmp, "direct", "Rabbids Hollywood ")
        os.makedirs(direct)
        schrijf(self.lib, json.dumps({"folders": [os.path.join(self.tmp, "diep"), os.path.join(self.tmp, "direct")]}))
        self.assertEqual(up.load_game("RabbidsHollywood", self.lib)[1]["nas_dir"], direct)

    def test_geen_gamemappen_geeft_een_melding(self):
        schrijf(self.lib, "{}")
        with self.assertRaisesRegex(up.UnityError, "geen gamemappen"):
            up.load_game("RabbidsHollywood", self.lib)

    def test_gebruikerslaag_vervangt_het_bestand_als_geheel(self):
        schrijf(os.path.join(self.user, "RabbidsHollywood", "unity.json"), json.dumps({"type": "rabbids_hollywood", "folder": "Rabbids Hollywood ", "width": 1600}))
        _r, g, bron = up.load_game("RabbidsHollywood", self.lib)
        self.assertEqual(g["width"], 1600)
        self.assertNotIn("name", g)           # niet uit het meegeleverde bestand aangevuld
        self.assertEqual(bron, os.path.join(self.user, "RabbidsHollywood", "unity.json"))

    def test_eigen_pad_in_de_gebruikerslaag_gaat_voor(self):
        schrijf(os.path.join(self.user, "RabbidsHollywood", "unity.json"), json.dumps({"type": "rabbids_hollywood", "folder": "Rabbids Hollywood ", "nas_dir": "~/elders/Rabbids"}))
        self.assertEqual(up.load_game("RabbidsHollywood", self.lib)[1]["nas_dir"], os.path.expanduser("~/elders/Rabbids"))
        schrijf(os.path.join(self.user, "RabbidsHollywood", "unity.json"), json.dumps({"type": "rabbids_hollywood", "nas_dir": "relatief"}))
        with self.assertRaisesRegex(up.UnityError, "absoluut"):
            up.load_game("RabbidsHollywood", self.lib)

    def test_kapot_in_de_gebruikerslaag_is_een_fout_geen_terugval(self):
        schrijf(os.path.join(self.user, "RabbidsHollywood", "unity.json"), "{kapot")
        with self.assertRaisesRegex(up.UnityError, "unity.json"):
            up.load_game("RabbidsHollywood", self.lib)

    def test_folder_is_geen_pad_en_ontbreekt_niet(self):
        schrijf(os.path.join(self.shipped, "Pad", "unity.json"), json.dumps({"type": "tomb_raider", "folder": "a/b"}))
        schrijf(os.path.join(self.shipped, "Leeg", "unity.json"), json.dumps({"type": "tomb_raider"}))
        with self.assertRaisesRegex(up.UnityError, "geen pad"):
            up.load_game("Pad", self.lib)
        with self.assertRaisesRegex(up.UnityError, "'folder' ontbreekt"):
            up.load_game("Leeg", self.lib)

    def test_games_en_onbekend(self):
        schrijf(os.path.join(self.user, "Eigen", "unity.json"), "{}")
        schrijf(os.path.join(self.shipped, "ZonderUnity", "game.ini"), "[Game]\n")
        self.assertEqual(up.unity_games(), ["Eigen", "RabbidsHollywood"])
        self.assertIsNone(up.unity_file("ZonderUnity"))
        self.assertIsNone(up.unity_file("../x"))
        with self.assertRaisesRegex(up.UnityError, "onbekende game"):
            up.load_game("ZonderUnity", self.lib)

    def test_gedeelde_instellingen(self):
        self.assertEqual(up.load_route(os.path.join(self.tmp, "bestaat-niet.json")), up.ROUTE_DEFAULTS)
        schrijf(os.path.join(self.tmp, "route.json"), json.dumps({"port": 40000}))
        r = up.load_route(os.path.join(self.tmp, "route.json"))
        self.assertEqual((r["port"], r["bepinex_version"]), (40000, "5.4.21.0"))
        schrijf(os.path.join(self.tmp, "kapot.json"), "{")
        with self.assertRaises(up.UnityError):
            up.load_route(os.path.join(self.tmp, "kapot.json"))
        # het meegeleverde bestand in de repo
        echt = up.load_route()
        self.assertEqual(echt["port"], 33610)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor tools/dev/gen-dongle-inventaris.py: de donglekolom komt uit het dongle.json in de drop-in van de game
(patches/<Game>/dropin/ravengun-hasp/<Game>/dongle.json, veld kind, docs/dropins.md); een los patches/<Game>/dongle.json
blijft ook werken. Een ontbrekend bestand betekent "geen dongle", en de rest van de tabel komt uit game.ini. Raakt alleen
tijdelijke mappen. Uitvoeren: python3 -I tests/test_dongle_inventaris.py"""
import importlib.util
import json
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPTS = os.path.join(ROOT, "scripts")
sys.dont_write_bytecode = True
sys.path.insert(0, SCRIPTS)

GAME = ("; test\n\n[Game]\nName = %s\nProfile = %s\nType = %s\nEmulationProfile = %s\n\n"
        "[Status]\nStatus = %s\nNote = %s\n")


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def load_module():
    spec = importlib.util.spec_from_file_location("inv", os.path.join(ROOT, "tools", "dev", "gen-dongle-inventaris.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class InventarisTest(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = self._td.name
        self.patches = os.path.join(self.tmp, "patches")
        write(os.path.join(self.patches, "Halo", "game.ini"),
              GAME % ("Halo Fireteam", "Halo", "ElfLdr2", "WartranTroopers", "playable", "Werkt volledig."))
        write(os.path.join(self.patches, "Rustig", "game.ini"),
              GAME % ("Rustig Spel", "Rustig", "TeknoParrot", "WartranTroopers", "parked", "Geen dongle nodig."))
        self.mod = load_module()

    def tearDown(self):
        self._td.cleanup()

    def rows(self):
        return {r["game"]: r for r in self.mod.game_rows(self.patches)}

    def test_kind_comes_from_dongle_json(self):
        """Aanwezigheid van dongle.json betekent: deze game vraagt een dongle; het veld kind bepaalt de soort."""
        write(os.path.join(self.patches, "Halo", "dongle.json"),
              json.dumps({"kind": "hasp", "game": "Halo Fireteam",
                          "memory": [{"name": "read/write", "fileid": 65524, "size": 112,
                                      "file": "dongle/memory/00000000000000001111111111110100"}]}))
        r = self.rows()
        self.assertEqual((r["Halo"]["kind"], r["Halo"]["source"]), ("hasp", "drop-in (dongle.json)"))
        self.assertEqual(r["Halo"]["status"], "playable")
        text = self.mod.render(self.mod.game_rows(self.patches))
        self.assertIn("| **Halo** | ElfLdr2 | WartranTroopers | hasp | drop-in (dongle.json) | playable |", text)
        self.assertIn("* **Halo** \u2014 hasp (dongle/ met 1 geheugenbestand)", text)
        # De statusnotitie is een voorproefje van 150 tekens uit [Status] Note.
        self.assertIn("* **Halo** \u2014 Werkt volledig.", text)

    def test_de_dongle_uit_de_dropin_van_de_game(self):
        """De gewone plaats is de drop-in van de game; de geheugenbestanden staan in memory/ ernaast."""
        d = os.path.join(self.patches, "Halo", "dropin", "ravengun-hasp", "Halo")
        write(os.path.join(d, "dongle.json"),
              json.dumps({"kind": "hasp", "game": "Halo Fireteam",
                          "memory": [{"name": "read/write", "fileid": 65524, "size": 112,
                                      "file": "memory/00000000000000001111111111110100"}]}))
        write(os.path.join(d, "memory", "00000000000000001111111111110100"), "x")
        r = self.rows()
        self.assertEqual((r["Halo"]["kind"], r["Halo"]["source"]), ("hasp", "drop-in (dongle.json + dropin/)"))
        text = self.mod.render(self.mod.game_rows(self.patches))
        self.assertIn("| **Halo** | ElfLdr2 | WartranTroopers | hasp | drop-in (dongle.json + dropin/) | playable |", text)
        self.assertIn("* **Halo** \u2014 hasp (memory/ met 1 geheugenbestand, dropin/)", text)

    def test_de_dropin_gaat_voor_het_losse_bestand(self):
        """Staan ze er allebei, dan wint de drop-in: dat is de plaats die de patchmotor uitrolt."""
        write(os.path.join(self.patches, "Halo", "dongle.json"), json.dumps({"kind": "hasp"}))
        write(os.path.join(self.patches, "Halo", "dropin", "ravengun-hasp", "Halo", "dongle.json"),
              json.dumps({"kind": "matrix"}))
        self.assertEqual(self.rows()["Halo"]["kind"], "matrix")

    def test_missing_dongle_json_means_no_dongle(self):
        r = self.rows()
        self.assertEqual((r["Halo"]["kind"], r["Halo"]["source"]), ("", "-"))
        self.assertEqual((r["Rustig"]["kind"], r["Rustig"]["source"]), ("", "-"))
        text = self.mod.render(self.mod.game_rows(self.patches))
        self.assertIn("| **Rustig** | TeknoParrot | WartranTroopers | - | - | parked |", text)
        self.assertIn("* **Rustig** \u2014 -", text)

    def test_dropin_dir_is_named_in_the_source(self):
        """Een dropin/-map naast dongle.json komt in de bron en in het detail te staan."""
        write(os.path.join(self.patches, "Halo", "dongle.json"), json.dumps({"kind": "matrix"}))
        write(os.path.join(self.patches, "Halo", "dropin", "dropin.ini"), "[matrix64.dll]\nTo = .\n")
        r = self.rows()["Halo"]
        self.assertEqual(r["source"], "drop-in (dongle.json + dropin/)")
        self.assertIn("dropin/", r["detail"])

    def test_enforced_false_is_named(self):
        """enforced = false betekent: de game gaat zonder dongle verder; dat hoort er dan bij te staan."""
        write(os.path.join(self.patches, "Halo", "dongle.json"), json.dumps({"kind": "region", "enforced": False}))
        r = self.rows()["Halo"]
        self.assertEqual(r["kind"], "region")
        self.assertIn("niet afgedwongen", r["detail"])

    def test_unknown_kind_does_not_crash(self):
        """Een onbekende soort wordt gemeld in plaats van stil als een dongle geteld."""
        write(os.path.join(self.patches, "Halo", "dongle.json"), json.dumps({"kind": "emulator"}))
        r = self.rows()["Halo"]
        self.assertEqual(r["kind"], "emulator")
        self.assertIn("onbekende soort", r["detail"])

    def test_broken_dongle_json(self):
        write(os.path.join(self.patches, "Halo", "dongle.json"), "{geen json")
        r = self.rows()["Halo"]
        self.assertEqual(r["kind"], "?")
        self.assertIn("onleesbaar", r["detail"])

    def test_render_is_stable(self):
        """Twee keer renderen geeft hetzelfde (het bestand is reproduceerbaar)."""
        text = self.mod.render(self.mod.game_rows(self.patches))
        self.assertEqual(text, self.mod.render(self.mod.game_rows(self.patches)))
        self.assertTrue(text.endswith("\n"))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor de runtime-lijst van de patchmotor (patchfile.runtime_list).

De bestandsroute dekt wat in het bestand van een module staat. De runtime-lijst dekt de twee
gevallen waarin dat niet kan: een DLL die pas later geladen wordt, en een gepakte exe. Zie
docs/memory-engine.md voor het formaat en de motor die hem leest.
"""
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.join(os.path.dirname(HERE), "scripts")
sys.dont_write_bytecode = True
sys.path.insert(0, SCRIPTS)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools", "dev"))   # patchfile

import gamesettings as gs  # noqa: E402
import patchfile as pf     # noqa: E402


class TestRuntimeLijst(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-rtlijst-")
        self.patches = os.path.join(self.tmp, "patches")
        self.game = "ProefGame"
        os.makedirs(os.path.join(self.patches, self.game))
        with open(os.path.join(self.patches, self.game, "game.ini"), "w") as f:
            f.write("[Game]\nName = Proef\nProfile = %s\nType = TeknoParrot\n" % self.game)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def laad(self, ini):
        with open(os.path.join(self.patches, self.game, "patches.ini"), "w") as f:
            f.write(ini)
        return gs.load_game(self.game, shipped=self.patches)

    def regels(self, tekst):
        return [r for r in tekst.splitlines() if r and not r.startswith("#")]

    def test_dll_patch_komt_in_de_lijst(self):
        g = self.laad(
            "[Patch: dll]\nEnabled = true\nKind = memory\nCategory = input\n"
            "Module = UnityPlayer.dll\nOffset = 0x1234\nAction = replace-bytes\nReplace = DE AD BE EF\n")
        tekst, verslag = pf.runtime_list(g, exe_path="/ergens/Game.exe")
        self.assertEqual(self.regels(tekst), ["UnityPlayer.dll|0x1234||deadbeef"])
        self.assertEqual(verslag[0][1], "in de lijst")

    def test_hoofdmodule_gaat_niet_naar_de_lijst(self):
        g = self.laad(
            "[Patch: hoofd]\nEnabled = true\nKind = memory\nCategory = input\n"
            "Module = Game.exe\nOffset = 0x1000\nAction = nop\nSize = 4\n")
        tekst, verslag = pf.runtime_list(g, exe_path="/ergens/Game.exe")
        self.assertEqual(self.regels(tekst), [])
        self.assertIn("hoofdmodule", verslag[0][2])

    def test_find_komt_als_verwacht_in_de_lijst(self):
        g = self.laad(
            "[Patch: met controle]\nEnabled = true\nKind = memory\nCategory = input\n"
            "Module = spul.dll\nOffset = 0x40\nAction = replace-bytes\n"
            "Find = 48 89 5C 24 08\nReplace = 90 90 90 90 90\n")
        tekst, _ = pf.runtime_list(g, exe_path="/ergens/Game.exe")
        self.assertEqual(self.regels(tekst), ["spul.dll|0x40|48895c2408|9090909090"])

    def test_bestandspatch_hoort_er_niet_in(self):
        g = self.laad(
            "[Patch: bestand]\nEnabled = true\nKind = file\nCategory = loader\nTarget = loader\n"
            "Find = AA BB\nReplace = CC DD\nCount = 1\n")
        tekst, verslag = pf.runtime_list(g, exe_path="/ergens/Game.exe")
        self.assertEqual(self.regels(tekst), [])
        self.assertIn("hoort niet in de runtime-lijst", verslag[0][2])

    def test_uitgeschakelde_patch_komt_er_niet_in(self):
        g = self.laad(
            "[Patch: uit]\nEnabled = false\nKind = memory\nCategory = input\n"
            "Module = spul.dll\nOffset = 0x40\nAction = nop\nSize = 4\n")
        tekst, _ = pf.runtime_list(g, exe_path="/ergens/Game.exe")
        self.assertEqual(self.regels(tekst), [])

    def test_applied_by_core_wordt_overgeslagen(self):
        g = self.laad(
            "[Patch: kern doet het]\nEnabled = true\nKind = memory\nCategory = input\n"
            "AppliedBy = core\nModule = spul.dll\nOffset = 0x40\nAction = nop\nSize = 4\n")
        tekst, verslag = pf.runtime_list(g, exe_path="/ergens/Game.exe")
        self.assertEqual(self.regels(tekst), [])
        self.assertIn("AppliedBy = core", verslag[0][2])
        # en met de vlag erbij wel
        tekst2, _ = pf.runtime_list(g, exe_path="/ergens/Game.exe", take_over_core=True)
        self.assertEqual(len(self.regels(tekst2)), 1)

    def test_zonder_module_wordt_geweigerd(self):
        g = self.laad(
            "[Patch: geen module]\nEnabled = true\nKind = memory\nCategory = input\n"
            "Offset = 0x40\nAction = nop\nSize = 4\n")
        tekst, verslag = pf.runtime_list(g, exe_path="/ergens/Game.exe")
        self.assertEqual(self.regels(tekst), [])
        self.assertEqual(verslag[0][1], "geweigerd")

    def test_schrijven_maakt_het_bestand(self):
        g = self.laad(
            "[Patch: dll]\nEnabled = true\nKind = memory\nCategory = input\n"
            "Module = spul.dll\nOffset = 0x40\nAction = nop\nSize = 4\n")
        pad = os.path.join(self.tmp, "patchmem.txt")
        uit, aantal, _ = pf.schrijf_runtime_list(pad, g, exe_path="/ergens/Game.exe")
        self.assertEqual(uit, pad)
        self.assertEqual(aantal, 1)
        with open(pad, encoding="utf-8") as f:
            inhoud = f.read()
        self.assertIn("spul.dll|0x40||90909090", inhoud)

    def test_geen_lijst_betekent_geen_bestand(self):
        g = self.laad("[Patch: niets]\nEnabled = false\nKind = memory\nCategory = input\n"
                      "Module = spul.dll\nOffset = 0x40\nAction = nop\nSize = 4\n")
        pad = os.path.join(self.tmp, "patchmem.txt")
        uit, aantal, _ = pf.schrijf_runtime_list(pad, g, exe_path="/ergens/Game.exe")
        self.assertIsNone(uit)
        self.assertEqual(aantal, 0)
        self.assertFalse(os.path.exists(pad), "een lege lijst hoort geen bestand te maken")


if __name__ == "__main__":
    unittest.main()

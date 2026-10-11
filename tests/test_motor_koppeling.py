#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bewijst dat de patchmotor via de launcher ook echt TOEPAST, niet alleen rapporteert.

De launcher koppelt de motor sinds ronde 15 (plan_motor_patches in scripts/launch-direct.py). Deze
test bouwt een namaak-game met een memory-patch op de hoofdmodule, haalt hem door die koppeling en
kijkt of de bytes in het bestand echt veranderd zijn.

Waarom een namaak-game: een echte game staat op de NAS en daar wordt nooit geschreven. Hier is de
exe een kopie in een tijdelijke map, zodat de test alles zelf kan opruimen.
"""
import importlib.util
import os
import shutil
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPTS = os.path.join(REPO, "scripts")
sys.dont_write_bytecode = True
sys.path.insert(0, SCRIPTS)


def laad_launcher():
    pad = os.path.join(SCRIPTS, "launch-direct.py")
    spec = importlib.util.spec_from_file_location("launch_direct", pad)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["launch_direct"] = mod
    spec.loader.exec_module(mod)
    return mod


ld = laad_launcher()
import gamesettings as gs  # noqa: E402


def maak_pe(secties, image_base=0x140000000):
    """Een minimaal PE-bestand. secties: [(naam, vaddr, rawptr, rawsize)]."""
    opt_size = 240
    e_lfanew = 0x80
    coff = e_lfanew + 4
    opt = coff + 20
    sec = opt + opt_size
    size = sec + 40 * len(secties)
    for _n, _v, rawptr, rawsize in secties:
        size = max(size, rawptr + rawsize)
    d = bytearray(size)
    d[0:2] = b"MZ"
    struct.pack_into("<I", d, 0x3C, e_lfanew)
    d[e_lfanew:e_lfanew + 4] = b"PE\0\0"
    struct.pack_into("<H", d, coff + 2, len(secties))
    struct.pack_into("<H", d, coff + 16, opt_size)
    struct.pack_into("<H", d, opt, 0x20B)
    struct.pack_into("<Q", d, opt + 24, image_base)
    for i, (naam, vaddr, rawptr, rawsize) in enumerate(secties):
        off = sec + i * 40
        d[off:off + 8] = naam.encode()[:8].ljust(8, b"\0")
        struct.pack_into("<IIII", d, off + 8, rawsize, vaddr, rawsize, rawptr)
        for j in range(rawsize):
            d[rawptr + j] = 0xAA
    return bytes(d)


class TestKoppeling(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-motor-")
        self.patches = os.path.join(self.tmp, "patches")
        self.game = "ProefGame"
        os.makedirs(os.path.join(self.patches, self.game))
        with open(os.path.join(self.patches, self.game, "game.ini"), "w") as f:
            f.write("[Game]\nName = Proef\nProfile = %s\nType = TeknoParrot\n" % self.game)
        # De exe: .text op RVA 0x1000 met bestandsplaats 0x400 -- die verschillen, dus dit bewijst
        # dat de adresomrekening gebeurt en niet het adres als bestandsplaats wordt gebruikt.
        self.exe = os.path.join(self.tmp, "ProefGame.exe")
        with open(self.exe, "wb") as f:
            f.write(maak_pe([(".text", 0x1000, 0x400, 0x200)]))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def schrijf_patches(self, tekst):
        with open(os.path.join(self.patches, self.game, "patches.ini"), "w") as f:
            f.write(tekst)

    def test_memory_patch_wordt_toegepast(self):
        self.schrijf_patches(
            "[Patch: proef op de tekstsectie]\n"
            "Enabled = true\n"
            "Kind = memory\n"
            "Category = input\n"
            "Module = ProefGame.exe\n"
            "Offset = 0x1000\n"
            "Action = replace-bytes\n"
            "Replace = DE AD BE EF\n")
        g = gs.load_game(self.game, shipped=self.patches)
        _, verslag = ld.plan_motor_patches(g, self.exe, None, None, materialize=False)
        self.assertEqual(len(verslag), 1, verslag)
        self.assertEqual(verslag[0]["status"], "gecontroleerd")
        self.assertIn("bestand 0x400", verslag[0]["reden"])
        # controleren schrijft niets
        with open(self.exe, "rb") as f:
            self.assertEqual(f.read()[0x400:0x404], b"\xaa\xaa\xaa\xaa")

    def test_materialiseren_schrijft_op_de_goede_plek(self):
        self.schrijf_patches(
            "[Patch: proef op de tekstsectie]\n"
            "Enabled = true\n"
            "Kind = memory\n"
            "Category = input\n"
            "Module = ProefGame.exe\n"
            "Offset = 0x1000\n"
            "Action = replace-bytes\n"
            "Replace = DE AD BE EF\n")
        g = gs.load_game(self.game, shipped=self.patches)
        with open(self.exe, "rb") as f:
            voor = f.read()
        _, verslag = ld.plan_motor_patches(g, self.exe, None, None, materialize=True)
        self.assertEqual(verslag[0]["status"], "toegepast")
        with open(self.exe, "rb") as f:
            data = f.read()
        self.assertEqual(data[0x400:0x404], bytes.fromhex("deadbeef"),
                         "op bestandsplaats 0x400, niet op 0x1000")
        self.assertEqual(len(data), len(voor), "het bestand mag niet groter of kleiner worden")
        # Alleen die vier bytes mogen veranderd zijn.
        verschil = [i for i in range(len(voor)) if voor[i] != data[i]]
        self.assertEqual(verschil, [0x400, 0x401, 0x402, 0x403], "alleen de vier bytes op 0x400")

    def test_uitgeschakelde_patch_doet_niets(self):
        self.schrijf_patches(
            "[Patch: uit]\nEnabled = false\nKind = memory\nCategory = input\n"
            "Module = ProefGame.exe\nOffset = 0x1000\nAction = nop\nSize = 4\n")
        g = gs.load_game(self.game, shipped=self.patches)
        patched, verslag = ld.plan_motor_patches(g, self.exe, None, None, materialize=True)
        self.assertEqual(verslag, [])
        self.assertEqual(patched, {})

    def test_geweigerde_patch_laat_de_start_stoppen(self):
        self.schrijf_patches(
            "[Patch: verkeerde Find]\nEnabled = true\nKind = memory\nCategory = input\n"
            "Module = ProefGame.exe\nOffset = 0x1000\nAction = replace-bytes\n"
            "Replace = DE AD BE EF\nFind = 11 22 33 44\n")
        g = gs.load_game(self.game, shipped=self.patches)
        with self.assertRaises(ld.PatchRefused):
            ld.plan_motor_patches(g, self.exe, None, None, materialize=True)

    def test_de_launcher_zet_een_uitgeschakelde_patch_niet_aan(self):
        """De launcher zet de lader-patch uit bij de eigen lader; de motor moet dat overnemen."""
        self.schrijf_patches(
            "[Patch: lader-patch]\nEnabled = true\nKind = file\nCategory = loader\n"
            "Target = loader\nTargetName = OpenParrotLoader.exe\n"
            "Find = AA AA AA AA\nReplace = BB BB BB BB\nCount = 1\n")
        g = gs.load_game(self.game, shipped=self.patches)
        # precies wat de launcher doet bij de eigen lader
        for p in g.patches:
            if p.name == "lader-patch":
                p.fields["Enabled"] = "false"
        _, verslag = ld.plan_motor_patches(g, self.exe, None, None, materialize=False)
        self.assertEqual(verslag, [], "een uitgezette patch mag de motor niet laten weigeren")


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""[Launch] StackReserve in scripts/launch-direct.py (de engine: src/ravengun/launch/pe_stack.*, tests/test_direct_launch.cpp).

Onder Wine krijgt de hoofdthread van een Windows-game precies SizeOfStackReserve uit de PE-kop. TeknoParrot start Mario Kart
Arcade GP DX onder Wine daarom vanaf een kopie van de exe met 16 MiB in dat veld. De launcher doet hetzelfde in de view van de
game. Wat hier wordt afgedwongen:

1. De waarde en de PE-kop: alleen het veld SizeOfStackReserve verandert (PE32 en PE32+).
2. Het plan noemt de bron (het origineel op de NAS) en het doel (de exe in de view), en is gelijk aan dat van de engine
   (tests/direct_golden.py vergelijkt "stack").
3. De run maakt van de koppeling in de view een lokale kopie met de grotere stapel; de koppeling naar het origineel blijft als
   <exe>.ravengun-orig staan, op de NAS verandert niets, en een tweede run schrijft niets.
4. Wat niet kan wordt in het plan geweigerd (exit 5, er start niets): een onzinnige waarde, of een view die zelf op de NAS ligt.
Er wordt niets gestart (tests/launcher_nep.py).
"""
import os
import sys
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launcher_nep as nep  # noqa: E402
from launcher_nep import SHIPPED, cli, ld, profiel, schrijf  # noqa: E402

MiB = 1 << 20


def pe(plus=False, reserve=MiB):
    """Een kleine PE32 of PE32+: e_lfanew 0x80, optionele kop op 0x98, SizeOfStackReserve op 0x98 + 72."""
    d = bytearray(0x400)
    d[0:2] = b"MZ"
    d[0x3c] = 0x80
    d[0x80:0x84] = b"PE\0\0"
    d[0x84:0x86] = (0x8664 if plus else 0x14c).to_bytes(2, "little")
    d[0x94] = 0xF0 if plus else 0xE0
    d[0x98:0x9a] = (0x20b if plus else 0x10b).to_bytes(2, "little")
    w = 8 if plus else 4
    d[0x98 + 72:0x98 + 72 + w] = reserve.to_bytes(w, "little")
    return bytes(d)


class Waarden(unittest.TestCase):
    def test_grootte(self):
        for tekst, n in (("16M", 16 * MiB), (" 16 MiB ", 16 * MiB), ("16mb", 16 * MiB), ("512k", 512 * 1024),
                         ("0x1000000", 16 * MiB), ("16777216", 16 * MiB)):
            self.assertEqual(ld.parse_stack_size(tekst), n, tekst)
        for tekst in ("", "16G", "zestien", "0x", "-1M"):
            self.assertIsNone(ld.parse_stack_size(tekst), tekst)
        self.assertEqual(ld.stack_size_text(16 * MiB), "16 MiB (16777216)")
        self.assertEqual(ld.stack_size_text(96 * 1024), "96 KiB (98304)")

    def test_pe_kop(self):
        for plus in (False, True):
            d = pe(plus)
            self.assertEqual(ld.pe_stack_reserve(d), MiB)
            n = ld.pe_set_stack_reserve(d, 16 * MiB)
            self.assertEqual(ld.pe_stack_reserve(n), 16 * MiB)
            self.assertEqual(len(n), len(d))
            self.assertEqual(sum(a != b for a, b in zip(n, d)), 2)   # 0x00100000 -> 0x01000000
        with self.assertRaises(ValueError):
            ld.pe_set_stack_reserve(pe(False), 1 << 32)
        with self.assertRaises(ValueError):
            ld.pe_stack_reserve(b"MZ" + b"\0" * 62)

    def test_kop_crc(self):
        k = bytearray(pe())
        for i in range(0x100, 0x400):
            k[i] = (i * 7) & 0xff
        k = bytes(k)
        c = zlib.crc32(k[:0x400])
        n = ld.pe_set_stack_reserve(k, 16 * MiB)
        self.assertNotEqual(zlib.crc32(n[:0x400]), c)
        n = ld.pe_keep_header_crc(n, c)
        self.assertEqual(zlib.crc32(n[:0x400]), c)
        self.assertEqual(ld.pe_stack_reserve(n), 16 * MiB)
        with self.assertRaises(ValueError):
            ld.pe_keep_header_crc(n[:0x200], c)


class Route(nep.Route):
    def setUp(self):
        super().setUp()
        self.nas_exe = os.path.join(self.nas_spel, "exe", "x64", "SPEL.exe")
        schrijf(self.nas_exe, pe(), "wb")
        profiel(self.GAME, self.spel)
        self.ini("[Launch]\nStackReserve = 16M\n")

    def ini(self, tekst):
        schrijf(os.path.join(SHIPPED, self.GAME, "game.ini"), tekst)

    def test_plan_run_en_tweede_run(self):
        self.view_goed()
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        self.assertEqual(plan["stack"], {"reserve": 16 * MiB, "src": os.path.realpath(self.nas_exe), "dest": self.spel})
        rc, spel, _ = self.start()
        self.assertIsNotNone(spel, self.uitvoer.getvalue() + self.fouten.getvalue())
        self.assertFalse(os.path.islink(self.spel))
        self.assertTrue(os.path.islink(self.spel + ld.STACK_ORIG_SUFFIX))
        with open(self.spel, "rb") as f:
            kopie = f.read()
        self.assertEqual(ld.pe_stack_reserve(kopie), 16 * MiB)
        # De kern herkent de game aan CRC32(eerste 0x400 bytes): gelijk aan die van het origineel.
        self.assertEqual(zlib.crc32(kopie[:0x400]), zlib.crc32(pe()[:0x400]))
        anders = [i for i, (a, b) in enumerate(zip(kopie, pe())) if a != b]
        self.assertTrue(all(0x78 <= i < 0x7c or 0x98 + 72 <= i < 0x98 + 76 for i in anders), anders)
        with open(self.nas_exe, "rb") as f:
            self.assertEqual(f.read(), pe(), "het origineel op de NAS is veranderd")
        self.assertIn("stapel: ", self.uitvoer.getvalue() + self.fouten.getvalue())
        # Een tweede plan gaat van het origineel uit; de run schrijft niets.
        rc, plan, err = self.plan_json()
        self.assertEqual(plan["stack"]["src"], os.path.realpath(self.nas_exe))
        voor = os.stat(self.spel).st_mtime_ns
        self.start()
        self.assertIn("is al actueel", self.uitvoer.getvalue() + self.fouten.getvalue())
        self.assertEqual(os.stat(self.spel).st_mtime_ns, voor)

    def test_zonder_sleutel_geen_kopie(self):
        self.view_goed()
        self.ini("[Launch]\n")
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        self.assertIsNone(plan["stack"])
        self.start()
        self.assertTrue(os.path.islink(self.spel))

    def test_onzinnige_waarde_geweigerd(self):
        self.view_goed()
        self.ini("[Launch]\nStackReserve = 1G\n")
        rc, out, err = cli(self.GAME, "--plan")
        self.assertEqual(rc, ld.EXIT_PATCH, out + err)
        self.assertIn("StackReserve = 1G", err)
        rc, _, nieuw = self.start()
        self.assert_niets_gestart(rc, nieuw)

    def test_view_op_de_nas_geweigerd(self):
        self.view_is_zelf_symlink()
        rc, out, err = cli(self.GAME, "--plan")
        self.assertEqual(rc, ld.EXIT_PATCH, out + err)
        self.assertIn("StackReserve vraagt een view", err)
        with open(self.nas_exe, "rb") as f:
            self.assertEqual(f.read(), pe())


if __name__ == "__main__":
    unittest.main()

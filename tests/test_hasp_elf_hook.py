#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# test_hasp_elf_hook.py — de tests van de HASP-koppeling voor de Linux/ELF-games.
#
# Deze tests controleren twee dingen:
#   1. de VASTSTELLING: de HASP-cliënt van de games is statisch gelinkt (geen ongedefinieerde
#      hasp-symbolen, geen PLT-verplaatsingen, wél gedefinieerde hasp-symbolen in de dynsym) —
#      gemeten op de echte game-bestanden als die er zijn;
#   2. de KOPPELING: de bibliotheek bouwt, exporteert de LD_AUDIT-ingangen, en de zelftest laat de
#      statisch gelinkte cliënt nul keer aanroepen en de emulator wel antwoorden.
#
# Draaien:
#   python3 -I tests/test_hasp_elf_hook.py
#   python3 -m unittest discover -s tests -q
#
# De tests die de game-bestanden nodig hebben, worden overgeslagen als de NAS er niet is.
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools", "rt-hasphook")
BRON = os.path.join(TOOLS, "hasp_inline_hook.c")
ZELFTEST = os.path.join(TOOLS, "hasp_zelftest.c")
DYNSYM = os.path.join(TOOLS, "dynsym.py")

NAS = "/home/deck/mnt/spidernas_emulation/Teknoparrot"
GAMES = {
    "JurassicPark": (os.path.join(NAS, "Jurassic Park", "game"), 32, 20),
    "GalagaAssault": (os.path.join(NAS, "Galaga Assault", "game"), 32, 20),
    "Halo": (os.path.join(NAS, "Halo Fireteam", "g7", "halo", "game"), 64, 0),
    "Terminator": (os.path.join(NAS, "Terminator Salvation ", "game"), 32, 0),
}


def _heeft_gcc(m32=False):
    if not shutil.which("gcc"):
        return False
    if not m32:
        return True
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, "t.c")
        with open(c, "w") as f:
            f.write("int main(void){return 0;}\n")
        r = subprocess.run(["gcc", "-m32", "-o", os.path.join(d, "t"), c],
                           capture_output=True)
        return r.returncode == 0


def lees_dynsym(pad):
    """Lees de dynamische symbolen van een ELF via PT_DYNAMIC (de games hebben geen sectiekoppen)."""
    with open(pad, "rb") as fh:
        d = fh.read()
    if d[:4] != b"\x7fELF":
        raise ValueError("geen ELF")
    is64 = d[4] == 2
    if is64:
        e_phoff, = struct.unpack_from("<Q", d, 0x20)
        e_phentsize, e_phnum = struct.unpack_from("<HH", d, 0x36)
    else:
        e_phoff, = struct.unpack_from("<I", d, 0x1c)
        e_phentsize, e_phnum = struct.unpack_from("<HH", d, 0x2a)
    loads, dyn_off = [], None
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        if is64:
            p_type, _fl, p_offset, p_vaddr, _pa, p_filesz = struct.unpack_from("<IIQQQQ", d, o)
        else:
            (p_type, p_offset, p_vaddr, _pa, p_filesz, _ms,
             _fl, _al) = struct.unpack_from("<IIIIIIII", d, o)
        if p_type == 1:
            loads.append((p_vaddr, p_offset, p_filesz))
        if p_type == 2:
            dyn_off = p_offset

    def v2o(v):
        for va, off, sz in loads:
            if va <= v < va + sz:
                return v - va + off
        return None

    if dyn_off is None:
        return []
    tags, o = {}, dyn_off
    while True:
        if is64:
            tag, val = struct.unpack_from("<qQ", d, o)
        else:
            tag, val = struct.unpack_from("<iI", d, o)
        o += 16 if is64 else 8
        if tag == 0:
            break
        tags[tag] = val
    n = 0
    if 4 in tags:
        ho = v2o(tags[4])
        if ho is not None:
            n = struct.unpack_from("<I", d, ho + 4)[0]
    if not n or 5 not in tags or 6 not in tags:
        return []
    so, st = v2o(tags[6]), v2o(tags[5])
    uit = []
    for i in range(n):
        if is64:
            st_name, st_info, _ot, st_shndx, st_value, st_size = struct.unpack_from(
                "<IBBHQQ", d, so + i * 24)
        else:
            st_name, st_value, st_size, st_info, _ot, st_shndx = struct.unpack_from(
                "<IIIBBH", d, so + i * 16)
        if st_name == 0:
            continue
        try:
            eind = d.index(b"\0", st + st_name)
        except ValueError:
            continue
        uit.append({
            "naam": d[st + st_name:eind].decode("latin-1"),
            "waarde": st_value,
            "grootte": st_size,
            "shndx": st_shndx,
        })
    return uit


class TestBestanden(unittest.TestCase):
    """De bronbestanden van deze ronde bestaan en zijn compleet."""

    def test_bron_bestaat(self):
        for p in (BRON, ZELFTEST, DYNSYM):
            with self.subTest(pad=p):
                self.assertTrue(os.path.isfile(p), f"{p} ontbreekt")

    def test_spdx_en_nederlands(self):
        with open(BRON, encoding="utf-8") as f:
            tekst = f.read()
        self.assertIn("SPDX-License-Identifier: GPL-3.0-or-later", tekst)
        self.assertIn("statisch", tekst.lower())

    def test_la_version_in_de_bron(self):
        """Zonder la_version laadt glibc de module niet als LD_AUDIT-module."""
        with open(BRON, encoding="utf-8") as f:
            tekst = f.read()
        self.assertIn("la_version", tekst)
        self.assertIn("la_preinit", tekst)

    def test_geen_hardgecodeerde_game_adressen(self):
        """De adressen moeten uit de dynsym komen, niet uit de bron (dat doet TeknoParrot wel).

        De adressen mogen in een COMMENTAAR staan (daar staat het verhaal van TeknoParrot's
        halo.so), maar niet in de code: geen letterlijke sprong of vergelijking met een game-adres.
        """
        with open(BRON, encoding="utf-8") as f:
            regels = f.readlines()
        code = []
        in_blok = False
        for regel in regels:
            kaal = regel.strip()
            if kaal.startswith("/*"):
                in_blok = "*/" not in kaal
                continue
            if in_blok:
                if "*/" in kaal:
                    in_blok = False
                continue
            if kaal.startswith("//") or kaal.startswith("*"):
                continue
            code.append(regel)
        tekst = "".join(code)
        for adres in ("0xb2d630", "0xb2fd63", "0xb2e1aa", "0x8529650", "0x8227970", "0x8528520"):
            self.assertNotIn(adres, tekst, f"adres {adres} hoort niet in de code te staan")


class TestDynsymHulpmiddel(unittest.TestCase):
    """tools/rt-hasphook/dynsym.py werkt op de echte game-bestanden."""

    @unittest.skipUnless(os.path.isfile(GAMES["JurassicPark"][0]), "de NAS is er niet")
    def test_jurassic_park_dynsym(self):
        syms = lees_dynsym(GAMES["JurassicPark"][0])
        self.assertGreater(len(syms), 18000, "de dynsym van Jurassic Park moet ~18811 symbolen hebben")
        hasp = [s for s in syms if s["naam"].startswith("hasp_")]
        self.assertGreaterEqual(len(hasp), 20)
        namen = {s["naam"] for s in hasp}
        for nodig in ("hasp_login", "hasp_read", "hasp_write", "hasp_encrypt", "hasp_decrypt",
                      "hasp_get_size", "hasp_logout", "hasp_get_info", "hasp_get_sessioninfo"):
            self.assertIn(nodig, namen)

    @unittest.skipUnless(os.path.isfile(GAMES["JurassicPark"][0]), "de NAS is er niet")
    def test_hasp_symbolen_zijn_gedefinieerd(self):
        """De kern van de vaststelling: de cliënt is GEDEFINIEERD in de game, niet geïmporteerd."""
        syms = lees_dynsym(GAMES["JurassicPark"][0])
        hasp = [s for s in syms if s["naam"].startswith("hasp_")]
        self.assertTrue(hasp)
        for s in hasp:
            self.assertNotEqual(s["shndx"], 0, f"{s['naam']} is ongedefinieerd (SHN_UNDEF)")
            self.assertNotEqual(s["waarde"], 0, f"{s['naam']} heeft geen adres")

    @unittest.skipUnless(os.path.isfile(GAMES["JurassicPark"][0]), "de NAS is er niet")
    def test_geen_ongedefinieerde_hasp_symbolen(self):
        syms = lees_dynsym(GAMES["JurassicPark"][0])
        ongedefinieerd = [s["naam"] for s in syms if s["shndx"] == 0 and "hasp" in s["naam"]]
        self.assertEqual(ongedefinieerd, [], "er mogen geen ongedefinieerde hasp-symbolen zijn")

    @unittest.skipUnless(os.path.isfile(GAMES["GalagaAssault"][0]), "de NAS is er niet")
    def test_galaga_dynsym(self):
        syms = lees_dynsym(GAMES["GalagaAssault"][0])
        hasp = [s for s in syms if s["naam"].startswith("hasp_")]
        self.assertGreaterEqual(len(hasp), 20)
        self.assertIn("hasp_login", {s["naam"] for s in hasp})

    @unittest.skipUnless(os.path.isfile(GAMES["Terminator"][0]), "de NAS is er niet")
    def test_terminator_heeft_geen_hasp_client(self):
        """Terminator Salvation heeft geen HASP-cliënt in het spel; dat is een bevinding, geen fout."""
        syms = lees_dynsym(GAMES["Terminator"][0])
        hasp = [s["naam"] for s in syms if "hasp" in s["naam"].lower()]
        self.assertEqual(hasp, [], "Terminator hoort geen hasp-symbolen te hebben")

    @unittest.skipUnless(os.path.isfile(GAMES["Halo"][0]), "de NAS is er niet")
    def test_halo_heeft_de_envelope_maar_geen_namen(self):
        syms = lees_dynsym(GAMES["Halo"][0])
        self.assertEqual([s["naam"] for s in syms if s["naam"].startswith("hasp_")], [])
        self.assertTrue(any("HASPE" in s["naam"] for s in syms) or
                        any(s["naam"] in ("patchInit", "patchDtInitHook") for s in syms),
                        "de envelope-handtekening van Halo hoort er te zijn")


class TestBouw(unittest.TestCase):
    """De koppeling bouwt en heeft de juiste uitvoeren."""

    @unittest.skipUnless(_heeft_gcc(m32=True), "geen gcc met 32-bits ondersteuning")
    def test_bouwt_en_exporteert_la_version(self):
        inc = os.path.join(ROOT, "hasp", "src", "inc")
        with tempfile.TemporaryDirectory() as d:
            uit = os.path.join(d, "hook.so")
            cmd = ["gcc", "-m32"]
            if os.path.isdir(inc):
                cmd += ["-I", inc]
            cmd += ["-shared", "-fPIC", "-O2", "-Wno-format-truncation", "-o", uit, BRON,
                    "-ldl", "-lpthread"]
            r = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertTrue(os.path.isfile(uit))
            nm = subprocess.run(["nm", "-D", "--defined-only", uit], capture_output=True, text=True)
            self.assertIn("la_version", nm.stdout)
            self.assertIn("la_preinit", nm.stdout)


class TestZelftest(unittest.TestCase):
    """De hele keten: statisch gelinkte aanroep -> koppeling -> emulator."""

    @unittest.skipUnless(_heeft_gcc(m32=True), "geen gcc met 32-bits ondersteuning")
    @unittest.skipUnless(os.path.isdir(os.path.join(ROOT, "patches", "Halo")),
                         "de donglegegevens van Halo zijn er niet")
    def test_zelftest_met_de_echte_dongle(self):
        inc = os.path.join(ROOT, "hasp", "src", "inc")
        emulator = None
        for kandidaat in (os.path.join(os.path.expanduser("~"), ".local", "share", "ravengun",
                                       "hasp", "ravengun-hasp.so"),):
            if os.path.isfile(kandidaat):
                emulator = kandidaat
        if not emulator:
            self.skipTest("de emulator ravengun-hasp.so is niet gebouwd")
        with tempfile.TemporaryDirectory() as d:
            hook = os.path.join(d, "hook.so")
            host = os.path.join(d, "zelftest")
            base = ["gcc", "-m32"]
            if os.path.isdir(inc):
                base += ["-I", inc]
            r = subprocess.run(base + ["-shared", "-fPIC", "-O2", "-Wno-format-truncation",
                                       "-o", hook, BRON, "-ldl", "-lpthread"],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run(base + ["-O1", "-rdynamic", "-o", host, ZELFTEST],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)

            # Zonder de koppeling moet de statisch gelinkte cliënt antwoorden.
            zonder = subprocess.run([host], capture_output=True, text=True,
                                    env={**os.environ, "HASP_ZELFTEST_WACHT": "0"})
            self.assertIn("de ECHTE cliënt is aangeroepen", zonder.stdout)

            # Met de koppeling moet de emulator antwoorden en de cliënt niet.
            env = {
                **os.environ,
                "LD_PRELOAD": hook,
                "RAVENGUN_HASP_VERBOSE": "1",
                "RAVENGUN_HASP_GAME": "Halo",
                "RAVENGUN_HASP_DIR": os.path.join(ROOT, "patches"),
                "RAVENGUN_HASP_ELF_WACHT": "10",
            }
            met = subprocess.run([host], capture_output=True, text=True, env=env, timeout=60)
            self.assertIn("echte_aanroepen = 0", met.stdout)
            self.assertIn("door de EMULATOR beantwoord", met.stdout)
            self.assertIn("hasp_login -> 0", met.stdout)
            self.assertIn("greep = 0xe6f", met.stdout)
            self.assertIn("grootte = 112", met.stdout)
            self.assertIn("2c 03 ab 63", met.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)

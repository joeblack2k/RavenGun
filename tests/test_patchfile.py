#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor de patchmotor: de adresomrekening en de byte-acties.

Deze tests bouwen hun eigen PE- en ELF-koppen, zodat ze overal draaien en niet van een game op de
NAS afhangen. De proeven op echte binaries staan in `re/test_patchfile2.sh` en
`re/verify_addrmap.sh`.
"""
import os
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "scripts"))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools", "dev"))   # patchfile, addrmap
sys.dont_write_bytecode = True

import addrmap          # noqa: E402
import patchfile        # noqa: E402


def make_pe(sections, image_base=0x140000000, magic=0x20B):
    """Een minimaal PE-bestand. sections: [(naam, vaddr, rawptr, rawsize)]."""
    opt_size = 240 if magic == 0x20B else 224
    e_lfanew = 0x80
    coff = e_lfanew + 4
    opt = coff + 20
    sec = opt + opt_size
    size = sec + 40 * len(sections)
    for _n, _v, rawptr, rawsize in sections:
        size = max(size, rawptr + rawsize)
    d = bytearray(size)
    d[0:2] = b"MZ"
    struct.pack_into("<I", d, 0x3C, e_lfanew)
    d[e_lfanew:e_lfanew + 4] = b"PE\0\0"
    struct.pack_into("<H", d, coff + 2, len(sections))       # NumberOfSections
    struct.pack_into("<H", d, coff + 16, opt_size)           # SizeOfOptionalHeader
    struct.pack_into("<H", d, opt, magic)
    if magic == 0x20B:
        struct.pack_into("<Q", d, opt + 24, image_base)
    else:
        struct.pack_into("<I", d, opt + 28, image_base)
    for i, (name, vaddr, rawptr, rawsize) in enumerate(sections):
        off = sec + i * 40
        d[off:off + 8] = name.encode()[:8].ljust(8, b"\0")
        struct.pack_into("<IIII", d, off + 8, rawsize, vaddr, rawsize, rawptr)
        for j in range(rawsize):
            d[rawptr + j] = (rawptr + j) & 0xFF
    return bytes(d)


def make_elf(loads, base_cls=1):
    """Een minimaal ELF-bestand. loads: [(vaddr, offset, filesz)].

    Let op de volgorde: het eerste segment begint op bestandsplaats 0 en bevat dus de ELF-header
    zelf (zoals bij echte ELF-bestanden). Daarom vullen we eerst de segmenten en schrijven we de
    header daarna -- anders overschrijft de vulling de header."""
    phoff = 52 if base_cls == 1 else 64
    phentsize = 32 if base_cls == 1 else 56
    size = phoff + phentsize * len(loads)
    for _v, offset, filesz in loads:
        size = max(size, offset + filesz)
    d = bytearray(size)
    # 1. de segmenten vullen
    for _v, offset, filesz in loads:
        for j in range(filesz):
            d[offset + j] = (offset + j) & 0xFF
    # 2. de program headers
    for i, (vaddr, offset, filesz) in enumerate(loads):
        off = phoff + i * phentsize
        if base_cls == 1:
            struct.pack_into("<IIIIIIII", d, off, 1, offset, vaddr, vaddr, filesz, filesz, 5, 0x1000)
        else:
            struct.pack_into("<IIQQQQQQ", d, off, 1, 5, offset, vaddr, vaddr, filesz, filesz, 0x1000)
    # 3. de ELF-header als laatste, zodat niets hem nog overschrijft
    d[:4] = b"\x7fELF"
    d[4] = base_cls
    d[5] = 1                                   # little endian
    if base_cls == 1:
        struct.pack_into("<I", d, 0x1C, phoff)
        struct.pack_into("<H", d, 0x2A, phentsize)
        struct.pack_into("<H", d, 0x2C, len(loads))
    else:
        struct.pack_into("<Q", d, 0x20, phoff)
        struct.pack_into("<H", d, 0x36, phentsize)
        struct.pack_into("<H", d, 0x38, len(loads))
    return bytes(d)


class TestPe(unittest.TestCase):
    def setUp(self):
        # .text op RVA 0x1000 met bestandsplaats 0x400 -- dat is het punt: die verschillen.
        self.data = make_pe([(".text", 0x1000, 0x400, 0x200), (".data", 0x2000, 0x600, 0x100)])
        self.m = addrmap.Module("proef.exe", self.data, addrmap.PE)

    def test_basis(self):
        self.assertEqual(self.m.base, 0x140000000)

    def test_bestandsplaats_verschilt_van_adres(self):
        self.assertEqual(self.m.offset_for(0x1000), 0x400)
        self.assertEqual(self.m.offset_for(0x1100), 0x500)
        self.assertEqual(self.m.offset_for(0x2000), 0x600)

    def test_sectienaam(self):
        self.assertEqual(self.m.region_of(0x1000), ".text")
        self.assertEqual(self.m.region_of(0x2000), ".data")

    def test_buiten_het_bestand(self):
        with self.assertRaises(addrmap.AddressOutsideImage):
            self.m.offset_for(0x900000)

    def test_lezen(self):
        self.assertEqual(self.m.read(0x1000, 4), self.data[0x400:0x404])

    def test_pe32(self):
        d = make_pe([(".text", 0x1000, 0x400, 0x100)], image_base=0x400000, magic=0x10B)
        m = addrmap.Module("proef32.exe", d, addrmap.PE)
        self.assertEqual(m.base, 0x400000)
        self.assertEqual(m.offset_for(0x1000), 0x400)


class TestElf(unittest.TestCase):
    def setUp(self):
        # Twee segmenten, zoals Jurassic Park: de tweede begint hoger.
        self.loads = [(0x8048000, 0x0, 0x1000), (0x8adc000, 0x2000, 0x1000)]
        self.m = addrmap.Module("proef", make_elf(self.loads), addrmap.ELF)

    def test_basis_is_het_laagste_segment(self):
        self.assertEqual(self.m.base, 0x8048000)

    def test_omrekenen(self):
        self.assertEqual(self.m.offset_for(0x100), 0x100)
        self.assertEqual(self.m.va_to_offset(0x8048100), 0x100)
        # Het tweede segment: vaddr 0x8adc100 -> bestand 0x2000 + 0x100
        self.assertEqual(self.m.va_to_offset(0x8adc100), 0x2100)

    def test_elf64(self):
        loads = [(0x400000, 0x0, 0x1000), (0x600000, 0x2000, 0x800)]
        m = addrmap.Module("proef64", make_elf(loads, base_cls=2), addrmap.ELF)
        self.assertEqual(m.base, 0x400000)
        self.assertEqual(m.va_to_offset(0x600100), 0x2100)

    def test_buiten_de_segmenten(self):
        with self.assertRaises(addrmap.AddressOutsideImage):
            self.m.va_to_offset(0x7000000)

    def test_geen_pt_load(self):
        d = bytearray(make_elf(self.loads))
        struct.pack_into("<H", d, 0x2C, 0)          # phnum = 0
        with self.assertRaises(addrmap.NotAnImage):
            addrmap.Module("leeg", bytes(d), addrmap.ELF)


class TestActions(unittest.TestCase):
    class P:
        def __init__(self, **kw):
            self.name = "proef"
            self.fields = kw

        def get(self, k, d=""):
            return self.fields.get(k, d)

    def test_nop(self):
        data, size = patchfile.memory_bytes(self.P(Action="nop", Size="4"), 32)
        self.assertEqual(data, b"\x90" * 4)
        self.assertEqual(size, 4)

    def test_nop_zonder_size(self):
        with self.assertRaises(patchfile.Refused):
            patchfile.memory_bytes(self.P(Action="nop"), 32)

    def test_returns_true(self):
        data, size = patchfile.memory_bytes(self.P(Action="jmp-returns-true", Size="8"), 64)
        self.assertEqual(data[:6], b"\xb8\x01\x00\x00\x00\xc3")
        self.assertEqual(len(data), 8)
        self.assertEqual(data[6:], b"\x90\x90")

    def test_returns_true_te_klein(self):
        with self.assertRaises(patchfile.Refused):
            patchfile.memory_bytes(self.P(Action="jmp-returns-true", Size="4"), 64)

    def test_replace_bytes(self):
        data, size = patchfile.memory_bytes(self.P(Action="replace-bytes", Replace="DE AD BE EF"), 64)
        self.assertEqual(data, bytes.fromhex("deadbeef"))
        self.assertEqual(size, 4)

    def test_replace_bytes_size_klopt_niet(self):
        with self.assertRaises(patchfile.Refused):
            patchfile.memory_bytes(self.P(Action="replace-bytes", Replace="DE AD", Size="4"), 64)

    def test_onbekende_actie(self):
        with self.assertRaises(patchfile.Refused):
            patchfile.memory_bytes(self.P(Action="mysterieus", Size="4"), 64)


class TestBestandspatch(unittest.TestCase):
    class P:
        def __init__(self, **kw):
            self.name = "proef"
            self.fields = kw

        def get(self, k, d=""):
            return self.fields.get(k, d)

    def test_zoek_en_vervang(self):
        data = b"\x00\x11\x22\x33\x00"
        nieuw, n = patchfile._patch_file_bytes(data, self.P(Find="11 22", Replace="AA BB", Count="1"))
        self.assertEqual(nieuw, b"\x00\xAA\xBB\x33\x00")
        self.assertEqual(n, 2)

    def test_count_klopt_niet(self):
        data = b"\x11\x11\x11"
        with self.assertRaises(patchfile.Refused):
            patchfile._patch_file_bytes(data, self.P(Find="11", Replace="22", Count="1"))

    def test_lengte_verschilt(self):
        with self.assertRaises(patchfile.Refused):
            patchfile._patch_file_bytes(b"\x11", self.P(Find="11", Replace="2222", Count="1"))

    def test_geen_find(self):
        with self.assertRaises(patchfile.Refused):
            patchfile._patch_file_bytes(b"\x11", self.P(Replace="22"))


class TestNas(unittest.TestCase):
    def test_nas_wordt_herkend(self):
        self.assertTrue(patchfile.on_nas(os.path.join(os.path.expanduser("~"), "mnt", "x", "y")))

    def test_lokaal_is_geen_nas(self):
        self.assertFalse(patchfile.on_nas("/tmp/patchtest/work/Game.exe"))


if __name__ == "__main__":
    unittest.main()

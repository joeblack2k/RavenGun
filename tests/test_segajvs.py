#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""De hulpmiddelen voor de Sega Ring-spellen zonder TeknoParrot: tools/dev/segajvs-scan.py (statische scan van een exe)
en tools/dev/pcp-listen.py (meetluisteraar voor de AM-daemonpoorten). Alles op synthetische gegevens; geen spel, geen Wine.
Uitvoeren: python3 -I tests/test_segajvs.py
"""
import importlib.util
import os
import socket
import struct
import sys
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.dont_write_bytecode = True


def load(name, rel):
    spec = importlib.util.spec_from_file_location(name, os.path.join(ROOT, rel))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


scan = load("segajvs_scan", "tools/dev/segajvs-scan.py")
pcp = load("pcp_listen", "tools/dev/pcp-listen.py")

BASE = 0x400000
TEXT_VA, TEXT_OFF = 0x1000, 0x400
RDATA_VA, RDATA_OFF = 0x2000, 0x1400


def build_pe(text, rdata):
    """Minimale PE32 met een code-sectie en een data-sectie; ruim genoeg voor de scan."""
    text = text.ljust(0x1000, b"\xCC")
    rdata = rdata.ljust(0x1000, b"\0")
    dos = b"MZ" + b"\0" * 0x3A + struct.pack("<I", 0x80)
    dos = dos.ljust(0x80, b"\0")
    opt = bytearray(224)
    struct.pack_into("<H", opt, 0, 0x10B)
    struct.pack_into("<I", opt, 28, BASE)
    coff = struct.pack("<HHIIIHH", 0x14C, 2, 0, 0, 0, len(opt), 0x102)
    sec = lambda n, vs, va, rs, ro, f: n.ljust(8, b"\0") + struct.pack("<IIIIIIHHI", vs, va, rs, ro, 0, 0, 0, 0, f)
    hdr = dos + b"PE\0\0" + coff + bytes(opt)
    hdr += sec(b".text", len(text), TEXT_VA, len(text), TEXT_OFF, 0x60000020)
    hdr += sec(b".rdata", len(rdata), RDATA_VA, len(rdata), RDATA_OFF, 0x40000040)
    hdr = hdr.ljust(TEXT_OFF, b"\0")
    return hdr + text + rdata


def va_of_rdata(off):
    return BASE + RDATA_VA + off


class ScanTest(unittest.TestCase):
    def setUp(self):
        # rdata: de naam van een functie, de poortnaam en de versietekst
        self.rd = b"amMasterOpen\0COM4\0\\\\.\\mxjvs\x00127.0.0.1\x00amMaster Ver.1.13 Build:Sep  4 2012 00:17:34\0libpcp Ver.1.08 Build:Jun 13 2012 00:06:16\0"
        self.name_va = va_of_rdata(0)
        pad = b"\xCC" * 4
        # functie 1: begint na de opvulling, laadt 40100 in een register en gebruikt de naam in een push
        f1 = b"\x55\x8B\xEC" + b"\xB8" + struct.pack("<I", 40100) + b"\x68" + struct.pack("<I", self.name_va) + b"\xC3"
        # functie 2: 16-bits poort in een geheugenveld (mov word ptr [esi+0x508], 0x9CA7) en een 32-bits (mov [esi+8], 40106)
        f2 = b"\x56" + b"\x66\xC7\x86\x08\x05\x00\x00" + struct.pack("<H", 40103) + b"\xC7\x46\x08" + struct.pack("<I", 40106) + b"\xC3"
        # een CC als operandbyte midden in een functie mag geen begin zijn (0x52CC3C bevat een CC)
        f3 = b"\x55\x8B\xEC" + b"\x68" + struct.pack("<I", 0x52CC3C) + b"\x68" + struct.pack("<I", self.name_va) + b"\xC3"
        self.f1_off = TEXT_OFF + len(pad)
        self.f2_off = self.f1_off + len(f1) + len(pad)
        self.f3_off = self.f2_off + len(f2) + len(pad)
        self.text = pad + f1 + pad + f2 + pad + f3
        self.data = build_pe(self.text, self.rd)

    def f_va(self, off):
        return BASE + TEXT_VA + off - TEXT_OFF

    def test_modules_and_literals(self):
        rep = scan.scan(self.data)
        self.assertEqual(rep["pe"], "pe32")
        self.assertEqual(rep["am_modules"]["amMaster"]["ver"], "1.13")
        self.assertEqual(rep["am_modules"]["libpcp"]["ver"], "1.08")
        self.assertEqual([x["text"] for x in rep["literals"]["com"]], ["COM4"])
        self.assertEqual(len(rep["literals"]["mxjvs"]), 1)
        self.assertEqual(len(rep["literals"]["loopback"]), 1)

    def test_ports_found_in_all_forms(self):
        rep = scan.scan(self.data)
        self.assertEqual(sorted(rep["ports"]), ["40100", "40103", "40106"])
        self.assertEqual(rep["ports"]["40100"][0]["form"], "mov r32,imm32")
        self.assertEqual(rep["ports"]["40103"][0]["form"], "mov [m],imm16")
        self.assertEqual(rep["ports"]["40106"][0]["form"], "mov [m],imm32")
        # de functiebegin-schatting ligt op het eerste byte na de opvulling
        self.assertEqual(rep["ports"]["40100"][0]["func"], self.f_va(self.f1_off))
        self.assertEqual(rep["ports"]["40106"][0]["func"], self.f_va(self.f2_off))

    def test_function_name_to_start(self):
        rep = scan.scan(self.data)
        starts = rep["functions"]["amMasterOpen"]
        self.assertIn(self.f_va(self.f1_off), starts)
        # f3 bevat een operand met een CC-byte vlak voor de push van de naam: begint op f3, niet in het midden
        self.assertIn(self.f_va(self.f3_off), starts)
        self.assertEqual(len(starts), 2)

    def test_constant_outside_range_is_ignored(self):
        text = b"\xCC\xCC\x55\xB8" + struct.pack("<I", 40099) + b"\xB8" + struct.pack("<I", 40116) + b"\xC3"
        rep = scan.scan(build_pe(text, self.rd))
        self.assertEqual(rep["ports"], {})

    def test_not_a_pe(self):
        with self.assertRaises(ValueError):
            scan.PE(b"hello world")


class PcpTest(unittest.TestCase):
    def test_printable(self):
        self.assertEqual(pcp.printable(b"a=b\r\n&c\x01"), "a=b\\r\\n&c\\x01")

    def test_split_requests(self):
        lines, rest = pcp.split_requests(b"keychip.version\r\n&x=1\r\npart")
        self.assertEqual(lines, [b"keychip.version", b"&x=1"])
        self.assertEqual(rest, b"part")
        lines, rest = pcp.split_requests(b"")
        self.assertEqual((lines, rest), ([], b""))

    def _free_port(self):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        p = s.getsockname()[1]
        s.close()
        return p

    def _run(self, **kw):
        port = self._free_port()
        lines = []
        lst = pcp.Listener([port], log=lines.append, **kw)
        lst.start()
        self.addCleanup(lst.close)
        c = socket.create_connection(("127.0.0.1", port), timeout=2)
        self.addCleanup(c.close)
        return port, lines, c

    def wait_for(self, lines, text, secs=3.0):
        end = time.time() + secs
        while time.time() < end:
            if any(text in l for l in lines):
                return True
            time.sleep(0.02)
        return False

    def test_greet_sends_prompt_and_logs_request(self):
        _port, lines, c = self._run(greet=True)
        self.assertEqual(c.recv(10), b">")
        c.sendall(b"keychip.appboot.region\r\n")
        self.assertTrue(self.wait_for(lines, "regel: keychip.appboot.region"))

    def test_no_greet_by_default_and_prompt_after_request(self):
        _port, lines, c = self._run(prompt_after=True, idle=0.1)
        c.settimeout(0.3)
        with self.assertRaises(socket.timeout):
            c.recv(1)
        c.settimeout(2)
        c.sendall(b"keychip.version\r\n")
        self.assertEqual(c.recv(10), b">")
        self.assertTrue(self.wait_for(lines, "prompt '>' na 17 byte"))

    def test_incomplete_request_is_reported(self):
        _port, lines, c = self._run()
        c.sendall(b"keychip.vers")
        self.assertTrue(self.wait_for(lines, "zonder afsluiting: keychip.vers"))

    def test_port_in_use_is_an_error(self):
        port = self._free_port()
        a = pcp.Listener([port], log=lambda s: None)
        self.addCleanup(a.close)
        with self.assertRaises(OSError):
            pcp.Listener([port], log=lambda s: None)


if __name__ == "__main__":
    unittest.main()

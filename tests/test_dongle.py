#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor scripts/dongle.py: detectie van een fysieke dongle via een nep-sysfs, de runtime-controle (nep-/proc, nep-poort,
nep-installatiemap) en de regel dat een serienummer nergens gelezen of getoond wordt. Raakt alleen tijdelijke mappen en een
poort op 127.0.0.1. Uitvoeren: python3 -I tests/test_dongle.py"""
import builtins
import json
import os
import socket
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPTS = os.path.join(ROOT, "scripts")
sys.dont_write_bytecode = True
sys.path.insert(0, SCRIPTS)
import dongle as dg  # noqa: E402

SERIAL = "SN-SECRET-0042-DO-NOT-LEAK"


def write(path, text=""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def closed_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Fake(unittest.TestCase):
    """Een nep-sysfs, nep-/proc, nep-wortel en een gegevensbestand waarvan de poorten dicht zijn (de echte poorten 1947 en 22350 kunnen op
    de machine waar de test draait open staan)."""

    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = self._td.name
        self.sysfs = os.path.join(self.tmp, "sys")
        self.proc = os.path.join(self.tmp, "proc")
        self.root = os.path.join(self.tmp, "root")
        os.makedirs(self.sysfs)
        os.makedirs(self.proc)
        os.makedirs(self.root)
        with open(os.path.join(ROOT, "packaging", "dongle-vendors.json"), encoding="utf-8") as f:
            self.data = json.load(f)
        for r in self.data["runtimes"].values():
            r["tcp_port"] = closed_port()
        self.vendors = os.path.join(self.tmp, "vendors.json")
        self.save_vendors()
        self.saved = dict(os.environ)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self.saved)
        self._td.cleanup()

    def save_vendors(self):
        write(self.vendors, json.dumps(self.data))

    def usb(self, name, vid, pid, manufacturer=None, product=None, serial=SERIAL):
        d = os.path.join(self.sysfs, name)
        write(os.path.join(d, "idVendor"), vid + "\n")
        write(os.path.join(d, "idProduct"), pid + "\n")
        if manufacturer is not None:
            write(os.path.join(d, "manufacturer"), manufacturer + "\n")
        if product is not None:
            write(os.path.join(d, "product"), product + "\n")
        if serial is not None:
            write(os.path.join(d, "serial"), serial + "\n")
        return d

    def process(self, pid, comm):
        write(os.path.join(self.proc, str(pid), "comm"), comm + "\n")
        write(os.path.join(self.proc, str(pid), "cmdline"), "geheim-argument-" + SERIAL)

    def detect(self):
        return dg.detect(sysfs=self.sysfs, proc_root=self.proc, root=self.root, vendors_file=self.vendors)

    def env(self):
        return dict(os.environ, RAVENGUN_USB_SYSFS=self.sysfs, RAVENGUN_PROC_ROOT=self.proc, RAVENGUN_DONGLE_ROOT=self.root,
                    RAVENGUN_DONGLE_VENDORS=self.vendors)


class VendorFileTest(unittest.TestCase):
    def test_shipped_file(self):
        vendors, runtimes, err = dg.load_vendors(os.path.join(ROOT, "packaging", "dongle-vendors.json"))
        self.assertEqual(err, "")
        self.assertEqual(sorted(vendors), ["04b9", "0529", "064f"])
        self.assertEqual(vendors["0529"]["runtime"], "sentinel-ldk")
        self.assertEqual(vendors["04b9"]["runtime"], "sentinel-ldk")
        self.assertEqual(vendors["064f"]["runtime"], "codemeter")
        self.assertEqual(runtimes["sentinel-ldk"]["tcp_port"], 1947)
        self.assertEqual(runtimes["codemeter"]["tcp_port"], 22350)
        self.assertIn("aksusbd", runtimes["sentinel-ldk"]["processes"])
        self.assertIn("hasplmd", runtimes["sentinel-ldk"]["processes"])
        self.assertIn("CodeMeterLin", runtimes["codemeter"]["processes"])
        for r in runtimes.values():
            self.assertTrue(r["install_url"].startswith("https://"))

    def test_udev_rule_matches_vendor_file(self):
        """packaging/udev/72-ravengun-dongle.rules geeft uaccess aan precies de vendor-ID's uit het gegevensbestand, en doet verder niets."""
        import re
        vendors, _r, _e = dg.load_vendors(os.path.join(ROOT, "packaging", "dongle-vendors.json"))
        with open(os.path.join(ROOT, "packaging", "udev", "72-ravengun-dongle.rules"), encoding="utf-8") as f:
            rules = [l.strip() for l in f if l.strip() and not l.lstrip().startswith("#")]
        ids = set()
        for r in rules:
            self.assertRegex(r, r'^SUBSYSTEM=="usb", ENV\{DEVTYPE\}=="usb_device", ATTR\{idVendor\}=="[0-9a-f]{4}", TAG\+="uaccess"$')
            ids.add(re.search(r'idVendor\}=="([0-9a-f]{4})"', r).group(1))
        self.assertEqual(ids, set(vendors))
        self.assertEqual(len(rules), len(vendors))
        with open(os.path.join(ROOT, "scripts", "udev-ravengun.sh"), encoding="utf-8") as f:
            self.assertIn("72-ravengun-dongle.rules", f.read())

    def test_missing_file(self):
        vendors, runtimes, err = dg.load_vendors("/nonexistent/dongle-vendors.json")
        self.assertEqual((vendors, runtimes), ({}, {}))
        self.assertTrue(err)


class ScanTest(Fake):
    def test_no_usb_no_dongle(self):
        self.usb("usb1", "1d6b", "0002", "Linux", "xHCI Host Controller")
        info = self.detect()
        self.assertEqual(info["dongles"], [])
        ok, msg, _ = dg.evaluate(info)
        self.assertFalse(ok)
        self.assertIn("No dongle found", msg)

    def test_empty_or_missing_sysfs(self):
        self.assertEqual(dg.detect(sysfs=os.path.join(self.tmp, "nope"), proc_root=self.proc, root=self.root, vendors_file=self.vendors)["dongles"], [])
        self.assertEqual(self.detect()["dongles"], [])

    def test_finds_hasp_sentinel_and_codemeter(self):
        self.usb("1-1", "0529", "0001", "Aladdin Knowledge Systems", "HASP HL 3.25")
        self.usb("1-1:1.0", "0000", "0000")      # interface zonder eigen idVendor-waarde van een dongle
        self.usb("1-2", "04B9", "0300", "SafeNet", "Sentinel HL")
        self.usb("2-1", "064f", "2af9", "WIBU-SYSTEMS", "WIBU-KEY")
        self.usb("usb1", "1d6b", "0002", "Linux", "xHCI Host Controller")
        self.usb("1-3", "046d", "c52b", "Logitech", "USB Receiver")
        d = self.detect()["dongles"]
        self.assertEqual([(x["vendor_id"], x["product_id"]) for x in d], [("0529", "0001"), ("04b9", "0300"), ("064f", "2af9")])
        self.assertEqual(d[0]["manufacturer"], "Aladdin Knowledge Systems")
        self.assertEqual(d[0]["product"], "HASP HL 3.25")
        self.assertEqual(d[2]["runtime"], "codemeter")
        self.assertEqual(dg.describe(d[0]), "Aladdin Knowledge Systems HASP HL 3.25 (0529:0001) - runtime: not installed")

    def test_names_fall_back_to_vendor_file(self):
        self.usb("1-1", "0529", "0001", None, None)
        d = self.detect()["dongles"][0]
        self.assertEqual(dg.dongle_label(d), "Aladdin Knowledge Systems (HASP)")

    def test_strange_strings_are_cleaned(self):
        self.usb("1-1", "0529", "zzzz", "Ala\x1b[31mddin\n" + "x" * 200, "")
        d = self.detect()["dongles"][0]
        self.assertEqual(d["product_id"], "????")
        self.assertNotIn("\x1b", d["manufacturer"])
        self.assertLessEqual(len(d["manufacturer"]), 64)


class SerialNeverTest(Fake):
    """Het serienummer (en elke andere unieke identificatie) wordt nergens gelezen en komt nergens in de uitvoer."""

    def setUp(self):
        super().setUp()
        self.usb("1-1", "0529", "0001", "Aladdin Knowledge Systems", "HASP HL 3.25")
        self.usb("1-2", "064f", "2af9", "Wibu-Systems", "CodeMeter")
        self.usb("1-3", "046d", "c52b", "Logitech", "USB Receiver")
        write(os.path.join(self.sysfs, "1-1", "uevent"), "PRODUCT=529/1/100\nSERIAL=" + SERIAL + "\n")
        self.process(100, "aksusbd")

    def test_serial_file_is_never_opened(self):
        opened = []
        real_open, real_os_open = builtins.open, os.open

        def spy_open(file, *a, **k):
            opened.append(os.fspath(file))
            return real_open(file, *a, **k)

        def spy_os_open(path, *a, **k):
            opened.append(os.fspath(path))
            return real_os_open(path, *a, **k)

        builtins.open, os.open = spy_open, spy_os_open
        try:
            info = self.detect()
            ok, msg, _ = dg.evaluate(info)
        finally:
            builtins.open, os.open = real_open, real_os_open
        self.assertTrue(ok)
        in_sys = [p for p in opened if p.startswith(self.sysfs)]
        self.assertTrue(in_sys)
        self.assertEqual({os.path.basename(p) for p in in_sys} - {"idVendor", "idProduct", "manufacturer", "product"}, set())
        for p in opened:
            self.assertNotIn("serial", os.path.basename(p).lower())
            self.assertNotIn("uevent", p)
        # Van een apparaat dat geen dongle is worden alleen de ID's gelezen, niet de namen.
        other = [os.path.basename(p) for p in in_sys if "/1-3/" in p]
        self.assertEqual(other, ["idVendor"])
        # In /proc alleen `comm`, nooit de opdrachtregel.
        self.assertEqual({os.path.basename(p) for p in opened if p.startswith(self.proc)}, {"comm"})

    def test_reading_other_attributes_is_refused(self):
        with self.assertRaises(ValueError):
            dg._read_attr(os.path.join(self.sysfs, "1-1"), "serial")
        self.assertNotIn("serial", dg.ALLOWED_ATTRS)

    def test_serial_not_in_any_output(self):
        info = self.detect()
        blob = json.dumps(info) + dg.evaluate(info)[1] + "".join(dg.describe(d) for d in info["dongles"])
        self.assertNotIn(SERIAL, blob)
        self.assertNotIn("SN-SECRET", blob)
        for args in ([], ["--json"]):
            p = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "dongle.py")] + args, capture_output=True, text=True, env=self.env())
            self.assertEqual(p.returncode, 0, p.stderr)
            self.assertNotIn("SN-SECRET", p.stdout + p.stderr)
            self.assertIn("0529:0001", p.stdout)
        self.assertNotIn("serial", json.dumps(info).lower())


class RuntimeTest(Fake):
    def setUp(self):
        super().setUp()
        self.usb("1-1", "0529", "0001", "Aladdin Knowledge Systems", "HASP HL 3.25")

    def status(self):
        return self.detect()["dongles"][0]["runtime_status"]

    def test_not_installed(self):
        s = self.status()
        self.assertEqual(s["status"], dg.NOT_INSTALLED)
        self.assertEqual(s["name"], "Sentinel LDK Runtime (Thales)")
        ok, msg, _ = dg.evaluate(self.detect())
        self.assertFalse(ok)
        self.assertIn("not installed", msg)
        self.assertIn("https://cpl.thalesgroup.com", msg)
        self.assertIn("does not install", msg)

    def test_installed_not_running(self):
        write(os.path.join(self.root, "usr/sbin/aksusbd"), "")
        self.assertEqual(self.status()["status"], dg.NOT_RUNNING)
        ok, msg, _ = dg.evaluate(self.detect())
        self.assertFalse(ok)
        self.assertIn("not running", msg)

    def test_running_by_process(self):
        self.process(321, "hasplmd")
        s = self.status()
        self.assertEqual((s["status"], s["how"]), (dg.RUNNING, "process"))
        ok, msg, d = dg.evaluate(self.detect())
        self.assertTrue(ok)
        self.assertEqual(msg, "Dongle detected: Aladdin Knowledge Systems HASP HL 3.25 (0529:0001) - runtime: running")

    def test_running_by_port(self):
        srv = socket.socket()
        srv.bind(("127.0.0.1", 0))
        srv.listen(1)
        try:
            self.data["runtimes"]["sentinel-ldk"]["tcp_port"] = srv.getsockname()[1]
            self.save_vendors()
            s = self.status()
            self.assertEqual((s["status"], s["how"]), (dg.RUNNING, "port"))
        finally:
            srv.close()

    def test_codemeter_uses_its_own_runtime(self):
        self.usb("2-1", "064f", "2af9", "Wibu-Systems", "CodeMeter")
        self.process(7, "aksusbd")     # de Sentinel-daemon doet niets voor een Wibu-dongle
        d = self.detect()["dongles"]
        self.assertEqual(d[0]["runtime_status"]["status"], dg.RUNNING)
        self.assertEqual(d[1]["runtime_status"]["status"], dg.NOT_INSTALLED)
        self.assertEqual(d[1]["runtime_status"]["name"], "CodeMeter Runtime (Wibu-Systems)")
        self.process(8, "CodeMeterLin")
        self.assertEqual(self.detect()["dongles"][1]["runtime_status"]["status"], dg.RUNNING)

    def test_unknown_runtime(self):
        self.data["vendors"].append({"vendor_id": "dead", "name": "Onbekende fabrikant", "runtime": ""})
        self.save_vendors()
        self.usb("3-1", "dead", "beef", "Mystery Ltd", "Key")
        d = self.detect()["dongles"][1]
        self.assertIsNone(d["runtime_status"])
        self.assertEqual(dg.describe(d), "Mystery Ltd Key (dead:beef) - runtime: unknown")
        ok, msg, _ = dg.evaluate({"dongles": [d], "error": ""})
        self.assertTrue(ok)
        self.assertIn("runtime of this maker is unknown", msg)

    def test_broken_vendor_file(self):
        info = dg.detect(sysfs=self.sysfs, proc_root=self.proc, root=self.root, vendors_file=os.path.join(self.tmp, "weg.json"))
        ok, msg, _ = dg.evaluate(info)
        self.assertFalse(ok)
        self.assertIn("Cannot read the dongle vendor list", msg)

    def test_cli_exit_codes(self):
        run = lambda: subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "dongle.py")], capture_output=True, text=True, env=self.env())
        self.assertEqual(run().returncode, 1)
        self.process(1, "aksusbd")
        p = run()
        self.assertEqual(p.returncode, 0)
        self.assertIn("runtime: running", p.stdout)
        p = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "dongle.py"), "--json"], capture_output=True, text=True, env=self.env())
        j = json.loads(p.stdout)
        self.assertTrue(j["ok"])
        self.assertEqual(j["dongles"][0]["vendor_id"], "0529")
        self.assertEqual(j["dongles"][0]["runtime_status"]["status"], "running")


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""De besturingspipe in scripts/launch-direct.py (ControlPipeServer, gelijk aan tools/rg-direct): de TCP-kant van
\\\\.\\pipe\\TeknoParrotPipe stuurt elke interval_ms het rapport uit het bestand van de daemon, ongewijzigd, en wacht na een
verbreking op de volgende verbinding (rg-jvsbridge maakt dan een nieuwe pipe). Geen Wine, geen game: een gewone TCP-klant speelt de
helper. Het plan zelf (welke game een besturingspipe krijgt) vergelijkt tests/direct_golden.py met dat van de engine."""
import importlib.util
import os
import socket
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.join(os.path.dirname(HERE), "scripts")
sys.dont_write_bytecode = True
sys.path.insert(0, SCRIPTS)
spec = importlib.util.spec_from_file_location("launch_direct", os.path.join(SCRIPTS, "launch-direct.py"))
ld = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ld)


def lees(sock, n):
    data = b""
    while len(data) < n:
        stuk = sock.recv(n - len(data))
        if not stuk:
            break
        data += stuk
    return data


class Besturingspipe(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-besturingspipe-")
        self.bron = os.path.join(self.tmp, "control")
        self.rapport = bytes(25) + b"\x03" + bytes(2) + b"\x80" + bytes(35)
        with open(self.bron, "wb") as f:
            f.write(self.rapport)
        ld.log = lambda m: None
        self.s = ld.ControlPipeServer({"profile": "AllsUsbIo", "pipe": "TeknoParrotPipe", "source": self.bron, "size": 64, "interval_ms": 15})
        self.s.start()

    def tearDown(self):
        self.s.close()
        self.s.join(2)
        for n in os.listdir(self.tmp):
            os.unlink(os.path.join(self.tmp, n))
        os.rmdir(self.tmp)

    def test_rapporten_ongewijzigd_en_in_tempo(self):
        c = socket.create_connection(("127.0.0.1", self.s.port))
        t0 = time.monotonic()
        data = lees(c, 64 * 6)
        duur = time.monotonic() - t0
        c.close()
        self.assertEqual(data, self.rapport * 6)
        self.assertGreater(duur, 0.06)   # zes rapporten om de 15 ms: niet sneller dan het tempo
        self.assertLess(duur, 2.0)

    def test_een_nieuwe_verbinding_na_een_verbreking(self):
        c = socket.create_connection(("127.0.0.1", self.s.port))
        self.assertEqual(lees(c, 64), self.rapport)
        c.close()
        with open(self.bron, "r+b") as f:   # de daemon schrijft een nieuw rapport (START los)
            f.seek(28)
            f.write(b"\x00")
        c2 = socket.create_connection(("127.0.0.1", self.s.port))
        r = lees(c2, 64)
        c2.close()
        self.assertEqual(len(r), 64)
        self.assertEqual(r[28], 0)
        self.assertEqual(r[25], 3)

    def test_een_kort_bestand_wordt_aangevuld_met_nullen(self):
        with open(self.bron, "wb") as f:
            f.write(b"\x01\x02")
        c = socket.create_connection(("127.0.0.1", self.s.port))
        r = lees(c, 64)
        c.close()
        self.assertEqual(r, b"\x01\x02" + bytes(62))


if __name__ == "__main__":
    unittest.main()

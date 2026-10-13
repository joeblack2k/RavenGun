#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""scripts/camera.py: welke camera een spel krijgt. Hermetisch: eigen patchmappen, eigen instellingen en een namaak-/sys.

Wat hier wordt afgedwongen: zonder [Camera] krijgt een spel niets; alleen de rol 'game' geeft een camera door ('setup' niet);
de camera komt uit game.ini, anders uit de instellingen van de app, anders de eerste van de machine; 'geen' in de instellingen
wint van een spel dat erom vraagt; de gebruikerslaag gaat per sleutel voor; en een game.ini kan niets anders dan /dev/videoN
de sandbox in vragen.
"""
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("camera", os.path.join(REPO, "scripts", "camera.py"))
cam = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cam)


class Camera(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(prefix="rg-cam-")
        self.addCleanup(shutil.rmtree, self.d, True)
        self.mee, self.eigen, self.sys = (os.path.join(self.d, n) for n in ("mee", "eigen", "sys"))
        self.conf = os.path.join(self.d, "ravengun-app.conf")
        for n, naam, index in (("video0", "USB Camera-B4.09.24.1", "0"), ("video1", "USB Camera-B4.09.24.1", "1"),
                               ("video2", "Tweede camera", "0"), ("video10", "Tiende", "0")):
            os.makedirs(os.path.join(self.sys, n))
            for veld, w in (("name", naam), ("index", index)):
                with open(os.path.join(self.sys, n, veld), "w") as f:
                    f.write(w + "\n")

    def ini(self, laag, tekst, game="Spel"):
        os.makedirs(os.path.join(laag, game), exist_ok=True)
        with open(os.path.join(laag, game, "game.ini"), "w") as f:
            f.write(tekst)

    def keuze(self, tekst):
        with open(self.conf, "w") as f:
            f.write(tekst)

    def los(self, game="Spel"):
        return cam.resolve(game, shipped=self.mee, user=self.eigen, conf=self.conf, sys_root=self.sys)

    def test_de_lijst(self):
        # per camera een regel (de tweede node van dezelfde camera telt niet), op nummer en niet op alfabet
        self.assertEqual(cam.lijst(self.sys), [("/dev/video0", "USB Camera-B4.09.24.1"), ("/dev/video2", "Tweede camera"),
                                               ("/dev/video10", "Tiende")])
        self.assertEqual(cam.lijst(os.path.join(self.d, "bestaat-niet")), [])

    def test_zonder_instelling_niets(self):
        self.assertEqual(self.los()[:2], ("", "none"))                      # geen game.ini
        self.ini(self.mee, "[Launch]\nMode = direct\n")
        self.assertEqual(self.los()[:2], ("", "none"))                      # game.ini zonder [Camera]
        self.keuze("[Games]\ncamera=/dev/video2\n")
        self.assertEqual(self.los()[:2], ("", "none"))                      # ook met een gekozen camera: het spel vraagt er geen

    def test_de_rol(self):
        self.ini(self.mee, "[Camera]\nUse = game\n")
        self.assertEqual(self.los()[:2], ("/dev/video0", "game"))            # de eerste camera van de machine
        self.ini(self.mee, "[Camera]\nUse = setup\n")
        apparaat, rol, uitleg = self.los()
        self.assertEqual((apparaat, rol), ("", "setup"))
        self.assertIn("inrichting", uitleg)
        self.ini(self.mee, "[Camera]\nUse = none\nDevice = /dev/video2\n")
        self.assertEqual(self.los()[:2], ("", "none"))
        self.ini(self.mee, "[Camera]\nUse = GAME\n")                          # hoofdletters maken niet uit
        self.assertEqual(self.los()[:2], ("/dev/video0", "game"))
        self.ini(self.mee, "[Camera]\nUse = altijd\n")
        with self.assertRaises(cam.Fout):
            self.los()
        # presence: het spel wil alleen dat er een camera IS. Een virtuele, en NOOIT de echte -- wat er ook gekozen is,
        # in game.ini of in de instellingen, en ook als daar 'geen camera' staat.
        for keuze in ("auto", "/dev/video2", "none"):
            self.keuze("[Games]\ncamera=%s\n" % keuze)
            for apparaat in ("", "Device = /dev/video10\n", "Device = none\n"):
                self.ini(self.mee, "[Camera]\nUse = presence\n" + apparaat)
                uit = self.los()
                self.assertEqual(uit[:2], ("virtual", "presence"), (keuze, apparaat, uit))
                self.assertNotIn("/dev/video", uit[0])

    def test_welke_camera(self):
        self.ini(self.mee, "[Camera]\nUse = game\n")
        self.keuze("[General]\nx=1\n[Games]\ncamera=/dev/video2\n")
        self.assertEqual(self.los()[0], "/dev/video2")                       # de keuze uit de instellingen
        self.keuze("[Games]\ncamera=none\n")
        apparaat, rol, uitleg = self.los()
        self.assertEqual((apparaat, rol), ("", "game"))                      # 'geen' in de instellingen wint
        self.assertIn("geen camera", uitleg)
        self.keuze("[Games]\ncamera=auto\n")
        self.assertEqual(self.los()[0], "/dev/video0")
        self.keuze("[Andere]\ncamera=/dev/video2\n")                         # de sleutel in een andere sectie telt niet
        self.assertEqual(self.los()[0], "/dev/video0")
        self.ini(self.mee, "[Camera]\nUse = game\nDevice = /dev/video10\n")   # game.ini noemt er zelf een
        self.keuze("[Games]\ncamera=/dev/video2\n")
        self.assertEqual(self.los()[0], "/dev/video10")
        self.ini(self.mee, "[Camera]\nUse = game\nDevice = none\n")
        self.assertEqual(self.los()[0], "")
        # een machine zonder camera: /dev/video0, voor als hij er komt (de doorgeving is -try)
        self.ini(self.mee, "[Camera]\nUse = game\n")
        os.remove(self.conf)
        self.assertEqual(cam.resolve("Spel", shipped=self.mee, user=self.eigen, conf=self.conf,
                                     sys_root=os.path.join(self.d, "leeg"))[0], "/dev/video0")

    def test_de_gebruikerslaag_gaat_per_sleutel_voor(self):
        self.ini(self.mee, "[Camera]\nUse = game\nDevice = /dev/video2\n")
        self.ini(self.eigen, "[Camera]\nUse = setup\n")
        self.assertEqual(self.los()[:2], ("", "setup"))                      # de gebruiker zet hem uit
        self.ini(self.eigen, "[Camera]\nDevice = /dev/video10\n")
        self.assertEqual(self.los()[:2], ("/dev/video10", "game"))            # alleen het apparaat anders, de rol blijft
        self.ini(self.mee, "[Launch]\nMode = direct\n")
        self.ini(self.eigen, "[Camera]\nUse = game\n")
        self.assertEqual(self.los()[:2], ("/dev/video0", "game"))             # de gebruiker zet hem aan

    def test_alleen_een_camera_mag_de_sandbox_in(self):
        for slecht in ("/dev/sda", "/dev/video0/../sda", "/dev/input/event3", "video0", "/dev/video", "/dev/video0 /dev/sda", "/etc/passwd"):
            self.ini(self.mee, "[Camera]\nUse = game\nDevice = %s\n" % slecht)
            with self.assertRaises(cam.Fout, msg=slecht):
                self.los()
        self.ini(self.mee, "[Camera]\nUse = game\n")
        self.keuze("[Games]\ncamera=/dev/sda\n")
        with self.assertRaises(cam.Fout):
            self.los()

    def test_de_opdrachtregel(self):
        self.ini(self.mee, "[Camera]\nUse = game\nDevice = /dev/video2\n")
        self.ini(self.mee, "[Camera]\nUse = soms\n", game="Fout")
        env = dict(os.environ, RAVENGUN_PATCHES_DIR=self.mee, RAVENGUN_USER_PATCHES_DIR=self.eigen, XDG_CONFIG_HOME=os.path.join(self.d, "cfg"))
        def draai(*a):
            return subprocess.run([sys.executable, "-I", os.path.join(REPO, "scripts", "camera.py")] + list(a), env=env, capture_output=True, text=True)
        r = draai("resolve", "Spel")
        self.assertEqual((r.returncode, r.stdout.split("\t")[:2]), (0, ["/dev/video2", "game"]))
        r = draai("resolve", "Onbekend")
        self.assertEqual((r.returncode, r.stdout.split("\t")[:2]), (0, ["-", "none"]))     # geen camera: een streepje, geen leeg veld
        r = draai("resolve", "Fout")
        self.assertEqual(r.returncode, 2)
        self.assertIn("geen rol", r.stderr)
        self.assertEqual(r.stdout, "")


if __name__ == "__main__":
    unittest.main()

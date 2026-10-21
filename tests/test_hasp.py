#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor de HASP-laag: scripts/hasp.py (de dongle-soort "hasp" uit patches/<Game>/dongle.json).
(De drop-in-motor van de start test tests/test_direct_launch.cpp; de dongle-dispatch van launch-direct.py
bestond al niet meer en het script zelf is weg.)

Alles draait in tijdelijke mappen; er wordt niets buiten de testmap geschreven en er komt geen netwerk of
echte game aan te pas. Uitvoeren: python3 -I tests/test_hasp.py"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPTS = os.path.join(ROOT, "scripts")
sys.dont_write_bytecode = True
sys.path.insert(0, SCRIPTS)
import hasp as hs  # noqa: E402

IMAGE = bytes(range(112))          # een nep-dongle-image van 112 bytes
BINARY_65524 = "00000000000000001111111111110100"


def schrijf(pad, data=b""):
    os.makedirs(os.path.dirname(pad), exist_ok=True)
    with open(pad, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode())


def dongle_json(game="TestGame", kind="hasp", fileid=65524, bestand=None, size=112, extra=None):
    d = {"kind": kind, "game": game,
         "memory": [{"name": "read/write", "fileid": fileid, "size": size,
                     "file": bestand or ("dongle/memory/" + BINARY_65524)}]}
    if extra:
        d.update(extra)
    return d


class Basis(unittest.TestCase):
    """Een nep-gebruikerslaag, een nep-meegeleverde laag en een nep-bibliotheekmap."""

    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = self._td.name
        self.shipped = os.path.join(self.tmp, "patches")
        self.user = os.path.join(self.tmp, "userpatches")
        self.lib = os.path.join(self.tmp, "lib")
        os.makedirs(self.shipped)
        os.makedirs(self.user)
        os.makedirs(self.lib)
        schrijf(os.path.join(self.lib, "ravengun-hasp-x64.so"), b"\x7fELF\x02" + b"\0" * 15)
        schrijf(os.path.join(self.lib, "ravengun-hasp.so"), b"\x7fELF\x01" + b"\0" * 15)
        # De PE-DLL's staan in een submap (zoals scripts/build-hasp.sh ze neerzet).
        schrijf(os.path.join(self.lib, "windows", "hasp_windows_x64.dll"), b"MZ" + b"\0" * 30)
        schrijf(os.path.join(self.lib, "windows", "hasp_windows_x64_105293.dll"), b"MZ" + b"\0" * 30)
        self.saved = dict(os.environ)
        # Volledig hermetisch: geen enkele echte map van de machine mag meedoen.
        self.libuser = os.path.join(self.tmp, "libuser")
        self.xdg = os.path.join(self.tmp, "xdg")
        os.makedirs(self.libuser)
        os.makedirs(self.xdg)
        os.environ.pop("RAVENGUN_PATCHES_DIR", None)          # een ontwikkel-checkout komt BOVEN de gebruikerslaag; "meegeleverd" is hier de onderste laag
        os.environ["RAVENGUN_LOADER_PATCHES_DIR"] = self.shipped
        os.environ["RAVENGUN_USER_PATCHES_DIR"] = self.user
        os.environ["RAVENGUN_HASP_LIB_DIR"] = self.lib
        os.environ["RAVENGUN_USER_HASP_DIR"] = self.libuser
        os.environ["XDG_DATA_HOME"] = self.xdg
        os.environ.pop("RAVENGUN_HASP_DIR", None)
        os.environ.pop("RAVENGUN_HASP_GAME", None)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self.saved)
        self._td.cleanup()

    def maak_game(self, game="TestGame", laag=None, **kw):
        """Zet patches/<Game>/dongle.json + het geheugenbestand neer. Geeft de map van de game."""
        basis = laag or self.shipped
        d = os.path.join(basis, game)
        schrijf(os.path.join(d, "dongle.json"), json.dumps(dongle_json(game=game, **kw)))
        schrijf(os.path.join(d, "dongle", "memory", BINARY_65524), IMAGE)
        return d


class DongleZoeken(Basis):
    def test_op_mapnaam_en_op_het_veld_game(self):
        self.maak_game("TestGame")
        for naam in ("TestGame", "TestGame".lower(), "test game", "TEST_GAME"):
            with self.subTest(naam=naam):
                map_, pad, desc = hs.dongle_json(naam)
                self.assertIsNotNone(pad, naam)
                self.assertEqual(desc["game"], "TestGame")

    def test_op_het_veld_game_met_een_andere_mapnaam(self):
        self.maak_game("Halo")
        # het veld "game" noemt de spelnaam; de map heet anders
        schrijf(os.path.join(self.shipped, "Halo", "dongle.json"),
                json.dumps(dongle_json(game="Halo Fireteam")))
        map_, pad, desc = hs.dongle_json("Halo Fireteam")
        self.assertTrue(pad.endswith(os.path.join("Halo", "dongle.json")), pad)

    def test_gebruikerslaag_wint(self):
        self.maak_game("TestGame", laag=self.shipped)
        d = self.maak_game("TestGame", laag=self.user)
        schrijf(os.path.join(d, "dongle.json"), json.dumps(dongle_json(game="TestGame", extra={"haspid": 42})))
        map_, pad, desc = hs.dongle_json("TestGame")
        self.assertEqual(desc.get("haspid"), 42)
        self.assertTrue(map_.startswith(self.user), map_)

    def test_onbekende_game(self):
        self.assertEqual(hs.dongle_json("BestaatNiet"), (None, None, None))


class DongleLezen(Basis):
    def test_goede_dongle(self):
        self.maak_game()
        map_, desc, fouten = hs.lees_dongle("TestGame")
        self.assertEqual(fouten, [])
        self.assertEqual(desc["kind"], "hasp")

    def test_hex_alias_is_geen_fout(self):
        d = self.maak_game()
        schrijf(os.path.join(d, "dongle", "memory", "fff4"), IMAGE)
        self.assertEqual(hs.lees_dongle("TestGame")[2], [])

    def test_kind_ontbreekt(self):
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps({"game": "TestGame",
                                                            "memory": [{"fileid": 65524, "file": "dongle/memory/" + BINARY_65524}]}))
        schrijf(os.path.join(d, "dongle", "memory", BINARY_65524), IMAGE)
        fouten = hs.lees_dongle("TestGame")[2]
        self.assertTrue(any("kind" in f for f in fouten), fouten)

    def test_ander_soort_wijst_naar_het_eigen_script(self):
        self.maak_game(kind="matrix")
        fouten = hs.lees_dongle("TestGame")[2]
        self.assertTrue(any("scripts/matrix.py" in f for f in fouten), fouten)

    def test_geen_memory(self):
        d = self.maak_game()
        schrijf(os.path.join(d, "dongle.json"), json.dumps({"kind": "hasp", "game": "TestGame", "memory": []}))
        fouten = hs.lees_dongle("TestGame")[2]
        self.assertTrue(any("memory" in f for f in fouten), fouten)

    def test_bestand_ontbreekt(self):
        d = self.maak_game()
        os.unlink(os.path.join(d, "dongle", "memory", BINARY_65524))
        fouten = hs.lees_dongle("TestGame")[2]
        self.assertTrue(any("dat er niet is" in f for f in fouten), fouten)

    def test_grootte_wijkt_af(self):
        d = self.maak_game()
        schrijf(os.path.join(d, "dongle.json"), json.dumps(dongle_json(game="TestGame", size=999)))
        fouten = hs.lees_dongle("TestGame")[2]
        self.assertTrue(any("999" in f for f in fouten), fouten)

    def test_pad_buiten_de_map_wordt_geweigerd(self):
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(self.shipped, "geheim"), IMAGE)
        schrijf(os.path.join(d, "dongle.json"),
                json.dumps({"kind": "hasp", "game": "TestGame",
                            "memory": [{"fileid": 65524, "size": 112, "file": "../geheim"}]}))
        fouten = hs.lees_dongle("TestGame")[2]
        self.assertTrue(any("buiten de map" in f for f in fouten), fouten)

    def test_absoluut_pad_wordt_geweigerd(self):
        d = self.maak_game()
        schrijf(os.path.join(d, "dongle.json"),
                json.dumps({"kind": "hasp", "game": "TestGame",
                            "memory": [{"fileid": 65524, "size": 112, "file": "/etc/passwd"}]}))
        fouten = hs.lees_dongle("TestGame")[2]
        self.assertTrue(any("absoluut" in f for f in fouten), fouten)


class StatusEnInjectie(Basis):
    def test_status_zonder_game(self):
        st = hs.status()
        self.assertTrue(st["ok"], st["message"])
        self.assertTrue(st["emulator"].endswith("ravengun-hasp-x64.so"))

    def test_breedte_kiest_de_juiste_bibliotheek(self):
        self.assertTrue(hs.status(bits=32)["emulator"].endswith("ravengun-hasp.so"))
        self.assertTrue(hs.status(bits=64)["emulator"].endswith("ravengun-hasp-x64.so"))

    def test_status_van_een_goede_game(self):
        self.maak_game()
        st = hs.status("TestGame")
        self.assertTrue(st["ok"], st["message"])
        self.assertEqual(st["injection"]["via"], "preload")
        self.assertEqual(st["injection"]["preload"], [st["emulator"]])
        self.assertEqual(st["injection"]["env"]["RAVENGUN_HASP_GAME"], "TestGame")
        self.assertEqual(st["injection"]["env"]["RAVENGUN_HASP_DIR"], self.shipped)
        self.assertEqual(st["injection"]["env"]["RG_DROPIN_PRELOAD"], st["emulator"])

    def test_een_dropin_geeft_de_voorkeur_aan_de_dropin(self):
        d = self.maak_game()
        schrijf(os.path.join(d, "dropin", "ravengun-hasp-x64.so"), b"\x7fELF\x02" + b"\0" * 15)
        st = hs.status("TestGame")
        self.assertTrue(st["ok"], st["message"])
        self.assertEqual(st["injection"]["via"], "dropin")
        self.assertEqual(st["injection"]["preload"], [])
        self.assertEqual(st["injection"]["files"][0][1], "ravengun-hasp-x64.so")

    def test_windows_zonder_dropin_meldt_dat(self):
        self.maak_game()
        st = hs.status("TestGame", windows=True)
        self.assertTrue(st["ok"], st["message"])
        self.assertEqual(st["injection"]["via"], "geen")
        self.assertIn("dropin.ini", st["message"])

    def test_windows_met_een_pe_dll(self):
        d = self.maak_game()
        schrijf(os.path.join(d, "dropin", "hasp_windows_x64_105293.dll"), b"MZ" + b"\0" * 30)
        st = hs.status("TestGame", windows=True)
        self.assertTrue(st["ok"], st["message"])
        self.assertEqual(st["injection"]["via"], "dropin")
        self.assertEqual(st["injection"]["dll"], "hasp_windows_x64_105293.dll")

    def test_geen_emulator(self):
        shutil.rmtree(self.lib)
        os.makedirs(self.lib)
        st = hs.status("TestGame")
        self.assertFalse(st["ok"])
        self.assertIn("build-hasp.sh", st["message"])

    def test_een_verkeerd_soort_geeft_een_fout(self):
        self.maak_game(kind="region")
        st = hs.status("TestGame")
        self.assertFalse(st["ok"])
        self.assertIn("scripts/region.py", st["message"])

    def test_lijst(self):
        self.maak_game("TestGame")
        rijen = hs.games_met_dongle()
        self.assertEqual([r[1]["game"] for r in rijen], ["TestGame"])


class Opdrachtregel(Basis):
    def run_cli(self, *args):
        env = dict(os.environ)
        return subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "hasp.py")] + list(args),
                              capture_output=True, text=True, env=env)

    def test_goede_game_geeft_exit_0_en_json(self):
        self.maak_game()
        p = self.run_cli("--game", "TestGame", "--json")
        self.assertEqual(p.returncode, 0, p.stderr)
        j = json.loads(p.stdout)
        self.assertTrue(j["ok"])
        self.assertEqual(j["injection"]["env"]["RAVENGUN_HASP_GAME"], "TestGame")

    def test_onbekende_game_geeft_exit_1(self):
        p = self.run_cli("--game", "BestaatNiet", "--json")
        self.assertEqual(p.returncode, 1)
        self.assertFalse(json.loads(p.stdout)["ok"])

    def test_lijst(self):
        self.maak_game()
        p = self.run_cli("--list", "--json")
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(json.loads(p.stdout)[0]["kind"], "hasp")


DRIVER_C = r"""
#include "dongle.h"
#include <stdio.h>
#include <unistd.h>
int main(int argc, char **argv) {
    dongle_t d;
    if (dongle_load(argv[1], &d) != 0) return 2;     /* relative to the start directory */
    if (chdir(argv[2]) != 0) return 3;              /* the game moves elsewhere afterwards */
    unsigned char b[4] = {0};
    if (dongle_read_at(&d, 65524, 8, b, 4) != 4) return 4;
    return (b[0] == 8 && b[3] == 11) ? 0 : 5;
}
"""


@unittest.skipUnless(shutil.which("cc"), "no C compiler")
class RelatieveMap(unittest.TestCase):
    """dongle_load makes a relative dongle dir absolute: a game that changes its working directory after
    the DLL loaded (Lost Land Adventure's UE3 game, 2026-10-21) must still read its memory files."""

    def test_lezen_na_chdir(self):
        with tempfile.TemporaryDirectory() as t:
            src = os.path.join(ROOT, "hasp", "src")
            drv = os.path.join(t, "drv.c")
            schrijf(drv, DRIVER_C)
            exe = os.path.join(t, "drv")
            subprocess.run(["cc", "-std=c99", "-D_DEFAULT_SOURCE", "-I", src, "-o", exe, drv,
                            os.path.join(src, "dongle.c")], check=True, capture_output=True)
            spel = os.path.join(t, "spel")
            schrijf(os.path.join(spel, "dongle", "dongle.json"), json.dumps(dongle_json()))
            schrijf(os.path.join(spel, "dongle", "dongle", "memory", BINARY_65524), IMAGE)
            os.makedirs(os.path.join(spel, "Binaries", "Win64"))
            p = subprocess.run([exe, "dongle", os.path.join(spel, "Binaries", "Win64")], cwd=spel)
            self.assertEqual(p.returncode, 0)


if __name__ == "__main__":
    unittest.main()

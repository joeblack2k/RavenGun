#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor de HASP-laag: scripts/hasp.py (de dongle-soort "hasp" uit patches/<Game>/dongle.json) en de
dongle-dispatch plus de drop-in-motor van scripts/launch-direct.py.

Alles draait in tijdelijke mappen; er wordt niets buiten de testmap geschreven en er komt geen netwerk of
echte game aan te pas. Uitvoeren: python3 -I tests/test_hasp.py"""
import importlib.util
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


def _load_launch_direct():
    """launch-direct.py heeft een streepje in de naam en is dus geen gewone module."""
    pad = os.path.join(SCRIPTS, "launch-direct.py")
    spec = importlib.util.spec_from_file_location("launch_direct", pad)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ld = _load_launch_direct()


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
        os.environ["RAVENGUN_PATCHES_DIR"] = self.shipped
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


class DropinMotor(Basis):
    """De drop-in-motor van launch-direct.py (docs/dropins.md)."""

    def setUp(self):
        super().setUp()
        self.view = os.path.join(self.tmp, "view")
        os.makedirs(self.view)
        self.game = os.path.join(self.view, "game")
        schrijf(self.game, b"\x7fELF\x02" + b"\0" * 15)
        self.saved_home = ld.HOME
        ld.HOME = self.tmp
        # gamesettings leest dezelfde omgevingsvariabelen; die staan al in Basis.setUp.

    def tearDown(self):
        ld.HOME = self.saved_home
        super().tearDown()

    def plan(self, game="TestGame"):
        return {"profile": game, "game": self.game, "is64": True, "env": {}, "launch_env": {},
                "wine": "/usr/bin/wine",   # deze launcher start altijd Wine; dat bepaalt de preload-regel
                "dongle": {"enabled": False, "source": "usb-passthrough", "error": ""}}

    def dropin(self, game="TestGame", ini="", bestanden=()):
        d = os.path.join(self.shipped, game, "dropin")
        schrijf(os.path.join(d, "dropin.ini"), ini)
        for naam, data in bestanden:
            schrijf(os.path.join(d, naam), data)

    def test_geen_dropin_doet_niets(self):
        p = self.plan()
        dp = ld.dropin_plan(p)
        self.assertEqual(dp["files"], [])
        self.assertEqual(dp["env"], {})
        self.assertEqual(dp["errors"], [])
        self.assertEqual(ld.dropin_log_lines({"dropins": dp, "profile": "TestGame"}), [])

    def test_bestand_gaat_naar_de_map_van_de_game(self):
        self.dropin(ini="[libtest.so]\nTo = .\nPreload = true\n", bestanden=[("libtest.so", b"\x7fELF")])
        p = self.plan()
        dp = ld.dropin_plan(p)
        self.assertEqual(dp["errors"], [], dp["errors"])
        self.assertEqual(dp["files"][0]["dest"], os.path.join(self.view, "libtest.so"))
        self.assertEqual(dp["preload"], [os.path.join(self.view, "libtest.so")])
        # In de omgeving van Wine gaat het NIET als LD_PRELOAD (Cygwin sterft eraan), maar als RG_DROPIN_PRELOAD.
        self.assertNotIn("LD_PRELOAD", dp["env"])
        self.assertEqual(dp["env"]["RG_DROPIN_PRELOAD"], os.path.join(self.view, "libtest.so"))

    def test_preload_gaat_wel_als_ld_preload_bij_een_native_start(self):
        self.dropin(ini="[libtest.so]\nTo = .\nPreload = true\n", bestanden=[("libtest.so", b"\x7fELF")])
        p = self.plan()
        p["wine"] = ""          # geen Wine: een echte native ELF-start
        dp = ld.dropin_plan(p)
        self.assertEqual(dp["env"]["LD_PRELOAD"], os.path.join(self.view, "libtest.so"))
        self.assertEqual(dp["env"]["RG_DROPIN_PRELOAD"], os.path.join(self.view, "libtest.so"))

    def test_to_met_een_submap(self):
        self.dropin(ini="[libtest.so]\nTo = Binaries/Win64\n", bestanden=[("libtest.so", b"\x7fELF")])
        dp = ld.dropin_plan(self.plan())
        self.assertEqual(dp["files"][0]["dest"], os.path.join(self.view, "Binaries", "Win64", "libtest.so"))

    def test_dlloverride(self):
        self.dropin(ini="[matrix64.dll]\nTo = .\nDllOverride = native\n"
                        "[andere.dll]\nTo = .\nDllOverride = builtin,native\n",
                    bestanden=[("matrix64.dll", b"MZ"), ("andere.dll", b"MZ")])
        dp = ld.dropin_plan(self.plan())
        self.assertEqual(sorted(dp["overrides"]), ["andere=n,b", "matrix64=n"])
        self.assertIn("matrix64=n", dp["env"]["WINEDLLOVERRIDES"])

    def test_dlloverride_bestond_al(self):
        p = self.plan()
        p["env"]["WINEDLLOVERRIDES"] = "winegstreamer="
        self.dropin(ini="[matrix64.dll]\nTo = .\nDllOverride = native\n", bestanden=[("matrix64.dll", b"MZ")])
        dp = ld.dropin_plan(p)
        self.assertEqual(dp["env"]["WINEDLLOVERRIDES"], "winegstreamer=;matrix64=n")

    def test_env_puntkommagescheiden(self):
        self.dropin(ini="[libtest.so]\nTo = .\nEnv = A=1; B=twee\n", bestanden=[("libtest.so", b"x")])
        dp = ld.dropin_plan(self.plan())
        self.assertEqual(dp["env"]["A"], "1")
        self.assertEqual(dp["env"]["B"], "twee")

    def test_env_zonder_is_een_fout(self):
        self.dropin(ini="[libtest.so]\nTo = .\nEnv = ALOOG\n", bestanden=[("libtest.so", b"x")])
        dp = ld.dropin_plan(self.plan())
        self.assertTrue(any("SLEUTEL=WAARDE" in e for e in dp["errors"]), dp["errors"])

    def test_onbekende_dlloverride_is_een_fout(self):
        self.dropin(ini="[libtest.so]\nTo = .\nDllOverride = magisch\n", bestanden=[("libtest.so", b"x")])
        dp = ld.dropin_plan(self.plan())
        self.assertTrue(any("DllOverride" in e for e in dp["errors"]), dp["errors"])

    def test_bestand_ontbreekt(self):
        self.dropin(ini="[libtest.so]\nTo = .\n")
        dp = ld.dropin_plan(self.plan())
        self.assertTrue(any("ontbreekt in dropin/" in e for e in dp["errors"]), dp["errors"])

    def test_doel_op_de_nas_wordt_geweigerd(self):
        nas = os.path.join(self.tmp, "mnt", "nas")
        os.makedirs(nas)
        p = self.plan()
        p["game"] = os.path.join(nas, "game")
        schrijf(p["game"], b"\x7fELF\x02" + b"\0" * 15)
        self.dropin(ini="[libtest.so]\nTo = .\n", bestanden=[("libtest.so", b"x")])
        dp = ld.dropin_plan(p)
        self.assertEqual(dp["files"], [])
        self.assertTrue(any("NAS" in e for e in dp["errors"]), dp["errors"])

    def test_gebruikerslaag_wint(self):
        self.dropin(ini="[libtest.so]\nTo = .\nEnv = WIE=meegeleverd\n", bestanden=[("libtest.so", b"x")])
        d = os.path.join(self.user, "TestGame", "dropin")
        schrijf(os.path.join(d, "dropin.ini"), "[libtest.so]\nTo = .\nEnv = WIE=gebruiker\n")
        schrijf(os.path.join(d, "libtest.so"), b"x")
        dp = ld.dropin_plan(self.plan())
        self.assertEqual(dp["env"]["WIE"], "gebruiker")

    def test_place_binary_schrijft_en_vervangt_een_symlink(self):
        bron = os.path.join(self.tmp, "bron.so")
        schrijf(bron, b"inhoud")
        doel = os.path.join(self.view, "doel.so")
        schrijf(os.path.join(self.tmp, "origineel.so"), b"origineel")
        os.symlink(os.path.join(self.tmp, "origineel.so"), doel)
        st, _ = ld.place_binary(bron, doel)
        self.assertEqual(st, "geschreven")
        self.assertFalse(os.path.islink(doel))
        with open(doel, "rb") as f:
            self.assertEqual(f.read(), b"inhoud")
        with open(os.path.join(self.tmp, "origineel.so"), "rb") as f:
            self.assertEqual(f.read(), b"origineel")     # het origineel is onaangeroerd
        self.assertEqual(ld.place_binary(bron, doel)[0], "gelijk")

    def test_place_binary_weigert_de_nas(self):
        nas = os.path.join(self.tmp, "mnt", "nas")
        os.makedirs(nas)
        bron = os.path.join(self.tmp, "bron.so")
        schrijf(bron, b"inhoud")
        self.assertEqual(ld.place_binary(bron, os.path.join(nas, "doel.so"))[0], "nas")
        self.assertFalse(os.path.exists(os.path.join(nas, "doel.so")))

    def test_is_pe(self):
        schrijf(os.path.join(self.tmp, "x.exe"), b"MZ" + b"\0" * 10)
        self.assertTrue(ld.is_pe(os.path.join(self.tmp, "x.exe")))
        self.assertFalse(ld.is_pe(self.game))


@unittest.skip("de dongle-poort is verwijderd: de loader weet niets meer van dongles, zie docs/geen-dongle-in-de-loader.md")
class DongleDispatch(Basis):
    """dongle_gate(): vier soorten, twee bronnen, geen stil vangnet (docs/dropins.md)."""

    def setUp(self):
        super().setUp()
        self.view = os.path.join(self.tmp, "view")
        os.makedirs(self.view)
        self.game = os.path.join(self.view, "game")
        schrijf(self.game, b"\x7fELF\x02" + b"\0" * 15)
        self.saved_home = ld.HOME
        ld.HOME = self.tmp
        # De laag-scripts van de andere soorten (scripts/matrix.py, scripts/region.py, ...) bestaan inmiddels
        # echt. Om de dispatch deterministisch te testen wijzen we _kind_script naar een eigen, lege map.
        self.kinds = os.path.join(self.tmp, "kinds")
        os.makedirs(self.kinds)
        self.saved_kind_script = ld._kind_script
        ld._kind_script = lambda script: os.path.join(self.kinds, script)

    def tearDown(self):
        ld._kind_script = self.saved_kind_script
        ld.HOME = self.saved_home
        super().tearDown()

    def nep_script(self, naam, uit, rc=0):
        """Een nep-laag-script dat de afgesproken JSON op zijn uitvoer zet."""
        schrijf(os.path.join(self.kinds, naam),
                "import json, sys\nprint(json.dumps(%r))\nsys.exit(%d)\n" % (uit, rc))

    def plan(self, enabled=True, source="drop-in", **extra):
        p = {"profile": "TestGame", "game": self.game, "is64": True, "env": {}, "launch_env": {},
             "wine": "/usr/bin/wine",
             "dongle": {"enabled": enabled, "source": source, "error": ""}, "windows": False}
        p.update(extra)
        return p

    def test_uit_doet_niets(self):
        self.assertIsNone(ld.dongle_gate(self.plan(enabled=False), verbose=False))

    def test_onbekende_bron_wordt_geweigerd(self):
        with self.assertRaises(ld.DongleRefused) as cm:
            ld.dongle_gate(self.plan(source="software-hasp"), verbose=False)
        self.assertIn("bekend: usb-passthrough, drop-in", str(cm.exception))

    def test_lege_bron_wordt_geweigerd(self):
        with self.assertRaises(ld.DongleRefused):
            ld.dongle_gate(self.plan(source=""), verbose=False)

    def test_een_andere_fout_van_gamesettings_wordt_niet_genegeerd(self):
        p = self.plan()
        p["dongle"]["error"] = "[Dongle] Enabled = misschien (use true or false)"
        with self.assertRaises(ld.DongleRefused) as cm:
            ld.dongle_gate(p, verbose=False)
        self.assertIn("misschien", str(cm.exception))

    def test_drop_in_zonder_dongle_json(self):
        with self.assertRaises(ld.DongleRefused) as cm:
            ld.dongle_gate(self.plan(), verbose=False)
        self.assertIn("dongle.json", str(cm.exception))

    def test_drop_in_zonder_kind(self):
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps({"game": "TestGame", "memory": []}))
        with self.assertRaises(ld.DongleRefused) as cm:
            ld.dongle_gate(self.plan(), verbose=False)
        self.assertIn("kind", str(cm.exception))

    def test_onbekende_soort(self):
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps({"kind": "flipperkast", "game": "TestGame"}))
        with self.assertRaises(ld.DongleRefused) as cm:
            ld.dongle_gate(self.plan(), verbose=False)
        self.assertIn("onbekende dongle-soort", str(cm.exception))

    def test_soort_waarvan_het_script_ontbreekt(self):
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps({"kind": "matrix", "game": "TestGame"}))
        with self.assertRaises(ld.DongleRefused) as cm:
            ld.dongle_gate(self.plan(), verbose=False)
        self.assertIn("scripts/matrix.py", str(cm.exception))
        self.assertIn("bestaat nog niet", str(cm.exception))

    def test_soort_region_ook(self):
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps({"kind": "region", "game": "TestGame"}))
        with self.assertRaises(ld.DongleRefused) as cm:
            ld.dongle_gate(self.plan(), verbose=False)
        self.assertIn("scripts/region.py", str(cm.exception))

    def test_soort_met_een_script_dat_er_is(self):
        self.nep_script("matrix.py", {"ok": True, "env": {"RAVENGUN_MATRIX_GAME": "TestGame"},
                                      "message": "de Matrix-laag is er"})
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps({"kind": "matrix", "game": "TestGame"}))
        p = self.plan()
        inj = ld.dongle_gate(p, verbose=False)
        self.assertEqual(inj["kind"], "matrix")
        self.assertEqual(inj["script"], "scripts/matrix.py")
        self.assertEqual(p["env"]["RAVENGUN_MATRIX_GAME"], "TestGame")
        self.assertEqual(p["launch_env"]["RAVENGUN_MATRIX_GAME"], "TestGame")

    def test_soort_met_een_script_dat_weigert(self):
        self.nep_script("region.py", {"ok": False, "message": "geen regiogegevens voor TestGame"}, rc=1)
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps({"kind": "region", "game": "TestGame"}))
        with self.assertRaises(ld.DongleRefused) as cm:
            ld.dongle_gate(self.plan(), verbose=False)
        self.assertIn("geen regiogegevens", str(cm.exception))

    def test_soort_met_een_script_dat_onleesbare_uitvoer_geeft(self):
        schrijf(os.path.join(self.kinds, "region.py"), "import sys\nprint('geen json')\nsys.exit(1)\n")
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps({"kind": "region", "game": "TestGame"}))
        with self.assertRaises(ld.DongleRefused) as cm:
            ld.dongle_gate(self.plan(), verbose=False)
        self.assertIn("geen json", str(cm.exception))

    def test_hasp_zet_de_omgeving(self):
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps(dongle_json(game="TestGame")))
        schrijf(os.path.join(d, "dongle", "memory", BINARY_65524), IMAGE)
        p = self.plan()
        inj = ld.dongle_gate(p, verbose=False)
        self.assertEqual(inj["kind"], "hasp")
        self.assertEqual(p["env"]["RAVENGUN_HASP_GAME"], "TestGame")
        self.assertEqual(p["env"]["RAVENGUN_HASP_DIR"], self.shipped)
        self.assertEqual(p["launch_env"]["RAVENGUN_HASP_GAME"], "TestGame")

    def test_hasp_met_een_onvolledige_dongle_weigert(self):
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps({"kind": "hasp", "game": "TestGame", "memory": []}))
        with self.assertRaises(ld.DongleRefused) as cm:
            ld.dongle_gate(self.plan(), verbose=False)
        self.assertIn("build-hasp.sh", str(cm.exception))

    def test_usb_passthrough_blijft_de_fysieke_detectie(self):
        self.saved_detect, self.saved_eval = ld.dg.detect, ld.dg.evaluate
        ld.dg.detect = lambda *a, **k: {"dongles": []}
        ld.dg.evaluate = lambda info: (False, "Dongle: No dongle found", [])
        try:
            with self.assertRaises(ld.DongleRefused) as cm:
                ld.dongle_gate(self.plan(source="usb-passthrough"), verbose=False)
            self.assertIn("No dongle found", str(cm.exception))
            ld.dg.evaluate = lambda info: (True, "Dongle detected: Aladdin HASP (0529:0001) - runtime: running", [])
            self.assertIsNone(ld.dongle_gate(self.plan(source="usb-passthrough"), verbose=False))
        finally:
            ld.dg.detect, ld.dg.evaluate = self.saved_detect, self.saved_eval

    def test_voorkennis_van_een_onbekende_bron_bij_gamesettings(self):
        # gamesettings.py kent 'drop-in' misschien nog niet; de dispatch hier wel. Alleen die melding negeren we.
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps(dongle_json(game="TestGame")))
        schrijf(os.path.join(d, "dongle", "memory", BINARY_65524), IMAGE)
        p = self.plan()
        p["dongle"]["error"] = "[Dongle] Source = drop-in (known: usb-passthrough)"
        inj = ld.dongle_gate(p, verbose=False)
        self.assertEqual(inj["kind"], "hasp")

    def test_preview_weigert_niet(self):
        p = self.plan()
        uit = ld.dongle_preview(p)
        self.assertIn("error", uit)
        self.assertIn("dongle.json", uit["error"])

    def test_preview_van_een_goede_dongle(self):
        d = os.path.join(self.shipped, "TestGame")
        schrijf(os.path.join(d, "dongle.json"), json.dumps(dongle_json(game="TestGame")))
        schrijf(os.path.join(d, "dongle", "memory", BINARY_65524), IMAGE)
        uit = ld.dongle_preview(self.plan())
        self.assertEqual(uit["kind"], "hasp")


if __name__ == "__main__":
    unittest.main()

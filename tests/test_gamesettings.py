#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor scripts/gamesettings.py (lagen, lege waarden, patches aan/uit), de migratie (tools/dev/migrate-game-settings.py) en de
nieuwe tp-add-game.py (gebruikerslaag en [Bindings]). Raakt alleen tijdelijke mappen. De bestandspatch-machine zelf (Find/Count,
cachepad, NAS) zat in tests/launch_direct_test.sh (weg met scripts/launch-direct.py; de engine past geen bestandspatches toe); de motor zelf: tests/test_patchfile.py. Uitvoeren: python3 -I tests/test_gamesettings.py"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SCRIPTS = os.path.join(ROOT, "scripts")
sys.dont_write_bytecode = True
sys.path.insert(0, SCRIPTS)
import gamesettings as gs  # noqa: E402


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


class Tmp(unittest.TestCase):
    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = self._td.name
        self.shipped = os.path.join(self.tmp, "shipped")
        self.user = os.path.join(self.tmp, "user")
        self.saved = dict(os.environ)
        os.environ.pop("RAVENGUN_NO_LEGACY_ENV", None)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self.saved)
        self._td.cleanup()

    def load(self, name, **kw):
        return gs.load_game(name, shipped=self.shipped, user=self.user, **kw)


class ParserTest(Tmp):
    def test_conformance_with_cpp_cases(self):
        """Hetzelfde bestand en dezelfde uitkomst als de C++-lezer (tests/test_gamesettings.cpp)."""
        d = os.path.join(HERE, "data")
        with open(os.path.join(d, "gamesettings-cases.ini"), encoding="utf-8") as f:
            parsed, warn = gs.parse_ini(f.read())
        with open(os.path.join(d, "gamesettings-cases.expected.json"), encoding="utf-8") as f:
            self.assertEqual(parsed, json.load(f))
        self.assertTrue(warn)

    @unittest.skip("de dongle-poort is verwijderd: de loader weet niets meer van dongles, zie docs/geen-dongle-in-de-loader.md")
    def test_dongle_conformance_with_cpp_cases(self):
        """[Dongle]: dezelfde gevallen en uitkomsten als gs::dongle_of in tests/test_gamesettings.cpp."""
        with open(os.path.join(HERE, "data", "dongle-cases.json"), encoding="utf-8") as f:
            cases = json.load(f)
        self.assertGreaterEqual(len(cases), 8)
        for c in cases:
            write(os.path.join(self.shipped, "D", "game.ini"), c["ini"])
            got = self.load("D").dongle()
            self.assertEqual(got, (c["enabled"], c["source"], c["error"]), c["ini"])

    @unittest.skip("de dongle-poort is verwijderd: de loader weet niets meer van dongles, zie docs/geen-dongle-in-de-loader.md")
    def test_dongle_sources_are_two(self):
        """[Dongle] Source kent twee bronnen, in deze volgorde (docs/dropins.md): de fysieke dongle van de eigenaar of de
        drop-in uit de map van de game zelf. Welke soort dongle een game vraagt staat in patches/<Game>/dongle.json."""
        self.assertEqual(gs.DONGLE_SOURCES, ("usb-passthrough", "drop-in"))
        self.assertEqual(gs.DONGLE_DEFAULT_SOURCE, "usb-passthrough")

    @unittest.skip("de dongle-poort is verwijderd: de loader weet niets meer van dongles, zie docs/geen-dongle-in-de-loader.md")
    def test_dongle_layers(self):
        """De gebruikerslaag wint per sleutel; zonder [Dongle] is alles uit."""
        write(os.path.join(self.shipped, "D", "game.ini"), "[Dongle]\nEnabled = false\n")
        self.assertEqual(self.load("D").dongle(), (False, "usb-passthrough", ""))
        write(os.path.join(self.user, "D", "game.ini"), "[Dongle]\nEnabled = true\n")
        self.assertEqual(self.load("D").dongle(), (True, "usb-passthrough", ""))
        write(os.path.join(self.user, "D", "game.ini"), "[Dongle]\nEnabled =\n")
        self.assertEqual(self.load("D").dongle(), (False, "usb-passthrough", ""))
        write(os.path.join(self.user, "D", "game.ini"), "[Dongle]\nSource = drop-in\n")
        self.assertEqual(self.load("D").dongle(), (False, "drop-in", ""))

    def test_comments_case_and_empty(self):
        s, _ = gs.parse_ini("[S]\n# a\n; b\nKey = v ; c\nkey = w\nEmpty =\nSemi = a;b\n")
        self.assertEqual(s["S"], {"Key": "v", "key": "w", "Empty": "", "Semi": "a;b"})   # hoofdlettergevoelig, ; zonder spatie blijft


class LayerTest(Tmp):
    def setUp(self):
        super().setUp()
        write(os.path.join(self.shipped, "_defaults", "OpenParrot.ini"),
              "[Loader]\nRemoteThread = off\n[Wine]\nDllOverrides = d3d9=b\n[Input]\nJvsPipeBoard = true\n")
        write(os.path.join(self.shipped, "_defaults", "OpenParrot.patches.ini"),
              "[Patch: Lader]\nEnabled = false\nKind = file\nCategory = loader\nTarget = loader\nFind = 00\nReplace = 01\nReason = omdat\n")
        write(os.path.join(self.shipped, "G", "game.ini"),
              "[Game]\nName = G\nType = OpenParrot\n[Input]\nRoute = tp-shm\nLayout = X\n[Loader]\nRemoteThread =\n")
        write(os.path.join(self.shipped, "G", "patches.ini"), "[Patch: Lader]\nEnabled = true\n")

    def test_three_layers_merge_per_key(self):
        g = self.load("G")
        self.assertTrue(g.exists)
        self.assertEqual(g.type, "OpenParrot")
        self.assertEqual(g.get("Wine", "DllOverrides"), "d3d9=b")          # uit _defaults
        self.assertEqual(g.get("Input", "JvsPipeBoard"), "true")
        self.assertEqual(g.get("Input", "Route"), "tp-shm")                # uit het meegeleverde game.ini
        self.assertIsNone(g.get("Loader", "RemoteThread"))                 # lege waarde in game.ini zet de standaard terug
        write(os.path.join(self.user, "G", "game.ini"), "[Input]\nRoute = evdev\n[Env]\nFOO = bar\n")
        g = self.load("G")
        self.assertEqual(g.get("Input", "Route"), "evdev")                 # de gebruiker wint per sleutel
        self.assertEqual(g.get("Input", "Layout"), "X")                    # de rest blijft
        self.assertEqual(g.get("Env", "FOO"), "bar")

    def test_empty_value_resets_in_user_layer(self):
        write(os.path.join(self.user, "G", "game.ini"), "[Wine]\nDllOverrides =\n[Input]\nLayout =\n")
        g = self.load("G")
        self.assertIsNone(g.get("Wine", "DllOverrides"))                   # standaard van het soort weggehaald
        self.assertIsNone(g.get("Input", "Layout"))                        # meegeleverde waarde weggehaald
        self.assertEqual(g.get("Input", "Route"), "tp-shm")

    def test_patch_enable_override(self):
        g = self.load("G")
        self.assertEqual([(p.name, p.enabled) for p in g.patches], [("Lader", True)])
        p = g.patches[0]
        self.assertEqual((p.kind, p.get("Target"), p.get("Find"), p.reason), ("file", "loader", "00", "omdat"))   # definitie uit _defaults
        write(os.path.join(self.user, "G", "patches.ini"), "[Patch: Lader]\nEnabled = false\n")
        g = self.load("G")
        self.assertFalse(g.patches[0].enabled)
        self.assertEqual(g.patches[0].get("Find"), "00")                   # alleen Enabled is overschreven
        # Een eigen patch in de gebruikerslaag komt erbij; een game zonder de gedeelde definitie ziet hem niet.
        write(os.path.join(self.user, "G", "patches.ini"),
              "[Patch: Eigen]\nEnabled = true\nKind = file\nCategory = timing\nTarget = core\nFind = 11\nReplace = 22\n")
        g = self.load("G")
        self.assertEqual([(p.name, p.enabled) for p in g.patches], [("Lader", True), ("Eigen", True)])
        # Zonder Enabled is een patch uit.
        write(os.path.join(self.user, "G", "patches.ini"), "[Patch: Zonder]\nKind = file\nCategory = audio\n")
        self.assertFalse([p for p in self.load("G").patches if p.name == "Zonder"][0].enabled)

    def test_type_hint_and_user_type(self):
        h = self.load("Onbekend", type_hint="OpenParrot")
        self.assertFalse(h.exists)
        self.assertEqual(h.get("Loader", "RemoteThread"), "off")
        self.assertEqual(len(h.patches), 1)
        write(os.path.join(self.user, "G", "game.ini"), "[Game]\nType = TeknoParrot\n")
        g = self.load("G")
        self.assertEqual(g.type, "TeknoParrot")
        self.assertIsNone(g.get("Wine", "DllOverrides"))

    def test_list_games_and_input_resolution(self):
        write(os.path.join(self.user, "Eigen", "game.ini"), "[Game]\nName = E\n")
        os.makedirs(os.path.join(self.user, "_intern"))
        self.assertEqual(gs.list_games(self.shipped, self.user), ["Eigen", "G"])
        warns = []
        i, src = gs.resolve_input("G", shipped=self.shipped, user=self.user, warn=warns.append)
        self.assertEqual((i["Route"], src), ("tp-shm", "shipped"))
        write(os.path.join(self.user, "G", "game.ini"), "[Input]\nRoute = evdev\n")
        i, src = gs.resolve_input("G", shipped=self.shipped, user=self.user, warn=warns.append)
        self.assertEqual((i["Route"], src, warns), ("evdev", "user", []))
        write(os.path.join(self.user, "G", "game.ini"), "[Input]\nRoute = hid-board\n")   # zonder Board: ongeldig, gebruikerslaag telt niet
        i, src = gs.resolve_input("G", shipped=self.shipped, user=self.user, warn=warns.append)
        self.assertEqual((i["Route"], src), ("tp-shm", "shipped"))
        self.assertTrue(warns)
        i, _ = gs.resolve_input("Nergens", shipped=self.shipped, user=self.user)
        self.assertEqual(i.get("Route", "evdev"), "evdev")

    def test_cli_get_and_env(self):
        write(os.path.join(self.shipped, "G", "game.ini"),
              "[Game]\nType = OpenParrot\n[Wine]\nPrefix = shared\n[Display]\nGamescope = true\n[Env]\nA = b\n")
        env = dict(os.environ, RAVENGUN_PATCHES_DIR=self.shipped, RAVENGUN_LOADER_PATCHES_DIR=os.path.join(self.tmp, "geen-loader"), RAVENGUN_USER_PATCHES_DIR=self.user, RAVENGUN_NO_LEGACY_ENV="1")
        out = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "gamesettings.py"), "--tp", self.tmp, "env", "G"],
                             capture_output=True, text=True, env=env).stdout.split("\n")
        self.assertIn("TP_GAMESCOPE=1", out)
        self.assertIn("RG_WINEPREFIX=" + gs.SHARED_PREFIX, out)
        self.assertIn("A=b", out)
        self.assertIn("WINEDLLOVERRIDES=d3d9=b", out)
        out = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "gamesettings.py"), "get", "G", "Wine.Prefix", "--default", "x"],
                             capture_output=True, text=True, env=env).stdout.strip()
        self.assertEqual(out, "shared")


class PatchPolicyTest(Tmp):
    """Category (verplicht, vaste lijst), Version, AppliedBy, EmulationProfile en de regiokeuzes."""

    def setUp(self):
        super().setUp()
        write(os.path.join(self.shipped, "_defaults", "OpenParrot.patches.ini"),
              "[Patch: Goed]\nEnabled = false\nKind = file\nCategory = Loader\nVersion = 1.08\nTarget = loader\n"
              "[Patch: ZonderCategory]\nEnabled = false\nKind = file\n[Patch: Onbekend]\nEnabled = true\nKind = file\nCategory = security\n"
              "[Patch: Geheugen]\nEnabled = false\nKind = memory\nCategory = input\nAppliedBy = Core\nModule = game.exe\nOffset = 0x10\n")
        write(os.path.join(self.shipped, "G", "game.ini"), "[Game]\nType = OpenParrot\n")
        write(os.path.join(self.shipped, "G", "patches.ini"), "[Patch: Goed]\nEnabled = true\n")

    def test_categories(self):
        self.assertEqual(gs.PATCH_CATEGORIES, ("graphics", "input", "timing", "audio", "paths", "crash", "network", "loader",
                                               "dongle", "region", "keychip", "integrity", "activation"))
        self.assertFalse({"security", "protection"} & set(gs.PATCH_CATEGORIES))
        g = self.load("G")
        self.assertEqual([p.name for p in g.patches], ["Goed", "Geheugen"])
        self.assertEqual((g.patches[0].category, g.patches[0].version, g.patches[0].enabled), ("loader", "1.08", True))
        self.assertEqual((g.patches[1].applied_by, g.patches[1].kind, g.patches[1].get("Find")), ("core", "memory", ""))
        errs = {e["name"]: e for e in g.patch_errors}
        self.assertEqual(set(errs), {"ZonderCategory", "Onbekend"})
        self.assertIn("Category ontbreekt", errs["ZonderCategory"]["message"])
        self.assertIn("graphics, input, timing, audio, paths, crash, network, loader, dongle, region, keychip, integrity, activation",
                      errs["ZonderCategory"]["message"])
        self.assertIn("onbekende Category 'security'", errs["Onbekend"]["message"])
        self.assertTrue(errs["Onbekend"]["enabled"] and not errs["ZonderCategory"]["enabled"])
        self.assertTrue(g.warnings)

    def test_patch_categories_data(self):
        """De categorieenlijst en de nieuwe categorieen: hetzelfde bestand en dezelfde uitkomst als de C++-lezer
        (tests/data/patch-categories.json), zodat Python en C++ niet uit elkaar lopen."""
        with open(os.path.join(HERE, "data", "patch-categories.json"), encoding="utf-8") as f:
            data = json.load(f)
        self.assertEqual(gs.PATCH_CATEGORIES, tuple(data["categories"]))
        self.assertGreaterEqual(len(data["cases"]), 8)
        for c in data["cases"]:
            write(os.path.join(self.shipped, "_defaults", "OpenParrot.patches.ini"), c["ini"])
            g = self.load("G")
            got = [p for p in g.patches if p.name == "Proef"]
            self.assertEqual(bool(got), c["accepted"], c["ini"])
            if got:
                self.assertEqual(got[0].category, c["category"], c["ini"])
            else:
                self.assertTrue(any(e["name"] == "Proef" for e in g.patch_errors), c["ini"])

    def test_user_layer_is_checked_too(self):
        write(os.path.join(self.user, "G", "patches.ini"),
              "[Patch: Goed]\nEnabled = false\n[Patch: Eigen]\nEnabled = true\nKind = file\n[Patch: Eigen2]\nEnabled = true\nKind = file\nCategory = onbekend\n"
              "[Patch: Eigen3]\nEnabled = true\nKind = file\nCategory = timing\n")
        g = self.load("G")
        self.assertFalse(g.patches[0].enabled)                                  # een Enabled-override is gewoon goed
        self.assertIn("Eigen3", [p.name for p in g.patches])
        names = [e["name"] for e in g.patch_errors]
        self.assertIn("Eigen", names)
        self.assertIn("Eigen2", names)
        write(os.path.join(self.user, "G", "patches.ini"), "[Patch: Goed]\nCategory = onbekend\n")
        g = self.load("G")
        self.assertNotIn("Goed", [p.name for p in g.patches])                   # categorie van een meegeleverde patch naar iets onbekends
        self.assertIn("Goed", [e["name"] for e in g.patch_errors])

    def test_repo_tree(self):
        shipped = os.path.join(ROOT, "patches")
        for name in gs.list_games(shipped, os.path.join(self.tmp, "geen")):
            g = gs.load_game(name, shipped=shipped, user=os.path.join(self.tmp, "geen"))
            self.assertEqual(g.patch_errors, [], name)
            self.assertTrue(all(p.category in gs.PATCH_CATEGORIES for p in g.patches))
            self.assertNotIn("KeychipRegion", g.section("Game"))
        lgi = gs.load_game("LGI", shipped=shipped, user=os.path.join(self.tmp, "geen"))
        self.assertEqual(lgi.get("Game", "EmulationProfile"), "SegaJvsLetsGoIsland")
        self.assertTrue(lgi.get("Game", "DongleRegion") and lgi.get("Game", "PcbRegion"))
        self.assertTrue(gs.load_game("RaccoonRampage", shipped=shipped, user=os.path.join(self.tmp, "geen")).get("Display", "DisplayMode"))
        mem = [(n, p) for n in ("HauntedMuseum", "HauntedMuseumII", "GaiaAttack4", "MusicGunGun2")
               for p in gs.load_game(n, shipped=shipped, user=os.path.join(self.tmp, "geen")).patches if p.kind == "memory"]
        self.assertEqual(len(mem), 14)
        for n, p in mem:
            self.assertFalse(p.enabled)
            self.assertEqual(p.applied_by, "core")
            self.assertTrue(p.get("Source").startswith("OpenParrot TypeX2Generic.cpp (GPL-3.0), regel "))
            if p.get("Find"):
                # De Find is op onze eigen dump gemeten en precies zo lang als de patch schrijft.
                self.assertEqual(len(p.get("Find").split()), int(p.get("Size")))
        # Geen Find waar die niets zou zeggen: de 2101J-set hoort bij de JP 1.01-build (onze dump is
        # de USA 1.00-build: CRC 0x5a7fa241 -> GameID::HauntedMuseum2100) en de crash-NOP van 0x1391
        # staat in onze dump al op 0x90, dus daar is een Find triviaal waar.
        self.assertEqual(sorted(p.name for n, p in mem if not p.get("Find")), [
            "Crash-NOP, versie 2100 (0x1391)",
            "Firmware-foutmelding uit, versie 2101J (0x1105)",
            "Gun-schrijfacties uit, versie 2101J (0x9B920)",
            "Volumeschrijfactie uit, versie 2101J (0x9DA08)",
        ])
        versions = sorted(p.version for n, p in mem if n == "HauntedMuseumII")
        self.assertEqual(versions, ["2100", "2100", "2100", "2101J", "2101J", "2101J"])
        self.assertTrue(gs.load_game("GaiaAttack4", shipped=shipped, user=os.path.join(self.tmp, "geen")).patch_errors == [])
        p = [p for p in gs.load_game("GaiaAttack4", shipped=shipped, user=os.path.join(self.tmp, "geen")).patches if p.kind == "file"][0]
        self.assertEqual(p.category, "loader")


class EnvMappingTest(Tmp):
    def test_round_trip(self):
        home = os.environ.get("HOME", gs.HOME)
        env = {"TP_REMOTETHREAD": "off", "TP_POSTSTART_REMOTETHREAD_MS": "3000", "RG_WINEPREFIX": gs.PREFIXES + "/ravengun-test-x",
               "WINEDLLOVERRIDES": "winhttp=n,b;d3d11,d3d10core,dxgi=n,b", "GST_PLUGIN_PATH": home + "/gst/plugins", "TP_GAMESCOPE": "1",
               "TP_GAMESCOPE_PATH": "/usr/bin/gamescope", "RG_JVS_TRANSPORT": "pipe", "RG_JVS_BOARD": "true", "FOO": "bar",
               "TP_GAME_WORKING_DIRECTORY": "Z:\\" + home.strip("/").replace("/", "\\") + "\\spel\\bin"}
        s, patches = gs.env_to_settings(dict(env, RG_LOADER_FPFIX="true"), gamedir=home + "/spel/bin")
        self.assertEqual(s["Loader"], {"RemoteThread": "off", "PostStartMs": "3000"})
        self.assertEqual(s["Wine"]["Prefix"], "ravengun-test-x")
        self.assertEqual(s["Wine"]["GStreamerPlugins"], "~/gst/plugins")      # nooit een thuismap in een bestand
        self.assertEqual(s["Display"], {"Gamescope": "true", "GamescopePath": "/usr/bin/gamescope"})
        self.assertEqual(s["Launch"]["WorkingDirectory"], ".")                 # relatief aan de map van de game
        self.assertEqual(s["Env"], {"FOO": "bar"})
        self.assertEqual(patches, {gs.FPFIX_PATCH: {"Enabled": "true"}})
        self.assertEqual(gs.to_env(s, home + "/spel/bin"), env)                # precies dezelfde omgeving terug

    def test_no_secrets(self):
        s, _ = gs.env_to_settings({"PATREON_SERIAL_KEY": "x", "MY_TOKEN": "y", "OK": "1"})
        self.assertEqual(s, {"Env": {"OK": "1"}})
        self.assertTrue(gs.is_secret_name("RG_API_KEY") or gs.is_secret_name("SOME_PASSWORD"))

    def test_legacy_env_is_lowest_layer(self):
        tp = os.path.join(self.tmp, "tp")
        write(os.path.join(tp, "UserProfiles", "G.env"), "# oud\nTP_REMOTETHREAD=off\nTP_GAMESCOPE=1\n")
        write(os.path.join(self.shipped, "G", "game.ini"), "[Display]\nGamescope = false\n")
        msgs = []
        g = gs.load_for_profile("G", tp, shipped=self.shipped, user=self.user, log=msgs.append)
        self.assertEqual(g.get("Loader", "RemoteThread"), "off")               # alleen in het oude .env
        self.assertEqual(g.get("Display", "Gamescope"), "false")               # game.ini wint
        self.assertTrue(msgs and "legacy .env" in msgs[0])
        os.environ["RAVENGUN_NO_LEGACY_ENV"] = "1"
        msgs.clear()
        g = gs.load_for_profile("G", tp, shipped=self.shipped, user=self.user, log=msgs.append)
        self.assertIsNone(g.get("Loader", "RemoteThread"))
        self.assertEqual(msgs, [])

    def test_write_user_settings_keeps_comments(self):
        p = os.path.join(self.user, "G", "game.ini")
        write(p, "; mijn notitie\n[Input]\nRoute = evdev ; eigen keuze\n")
        gs.write_user_settings("G", {"Input": {"Layout": "L"}, "Env": {"A": "b"}}, {"P": {"Enabled": "true"}}, user=self.user)
        text = read(p)
        self.assertIn("; mijn notitie", text)
        s, _ = gs.parse_ini(text)
        self.assertEqual((s["Input"]["Route"], s["Input"]["Layout"], s["Env"]["A"]), ("evdev", "L", "b"))
        s, _ = gs.read_ini(os.path.join(self.user, "G", "patches.ini"))
        self.assertEqual(s["Patch: P"], {"Enabled": "true"})


class MigrationTest(Tmp):
    def setUp(self):
        super().setUp()
        sys.path.insert(0, SCRIPTS)
        import importlib.util
        spec = importlib.util.spec_from_file_location("mig", os.path.join(ROOT, "tools", "dev", "migrate-game-settings.py"))
        self.mig = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.mig)
        self.out = os.path.join(self.tmp, "patches")
        write(os.path.join(self.out, "_defaults", "TeknoParrot.ini"), "[Loader]\nRemoteThread = off\n")
        write(os.path.join(self.out, "_defaults", "OpenParrot.ini"),
              "[Loader]\nRemoteThread = off\n[Wine]\nDllOverrides = d3d9=b\n")
        self.tp = os.path.join(self.tmp, "tp")
        home = os.environ["HOME"]
        for prof, et in (("A", "TeknoParrot"), ("B", "OpenParrot"), ("C", "ElfLdr2")):
            write(os.path.join(self.tp, "UserProfiles", prof + ".xml"),
                  "<GameProfile><EmulatorType>%s</EmulatorType><GamePath>%s/spel/%s/game.exe</GamePath></GameProfile>" % (et, home, prof))
        # Profielgegevens voor game.ini: EmulationProfile uit GameProfiles, regiokeuzes en weergave uit het gebruikersprofiel.
        def fi(n, v, t="Text", opts=()):
            return ("<FieldInformation><CategoryName>General</CategoryName><FieldName>%s</FieldName><FieldValue>%s</FieldValue><FieldType>%s</FieldType>"
                    "<FieldOptions>%s</FieldOptions></FieldInformation>" % (n, v, t, "".join("<string>%s</string>" % o for o in opts)))
        write(os.path.join(self.tp, "GameProfiles", "A.xml"), "<GameProfile><EmulationProfile>EmuA</EmulationProfile></GameProfile>")
        write(os.path.join(self.tp, "UserProfiles", "A.xml"),
              "<GameProfile><EmulatorType>TeknoParrot</EmulatorType><GamePath>%s/spel/A/game.exe</GamePath><EmulationProfile>OudA</EmulationProfile><ConfigValues>%s</ConfigValues></GameProfile>"
              % (home, fi("DongleRegion", "USA", "Dropdown", ["JAPAN", "USA"]) + fi("PcbRegion", "JAPAN", "Dropdown", ["JAPAN", "USA"]) +
                 fi("DisplayMode", "Windowed", "Dropdown", ["Windowed", "Fullscreen"]) + fi("Use Custom Resolution", "1", "Bool") +
                 fi("Custom Resolution Width", "1920") + fi("Custom Resolution Height", "1080")))
        write(os.path.join(self.tp, "UserProfiles", "A.env"),
              "# uitleg bij A\nTP_POSTSTART_REMOTETHREAD_MS=3000\nRG_WINEPREFIX=%s/.local/share/ravengun/prefixes/own\n"
              "#TP_WINEPREFIX=/nooit\nWINEDLLOVERRIDES=winhttp=n,b\nSECRET_TOKEN=geheim\nEXTRA=1\n" % home)
        write(os.path.join(self.tp, "UserProfiles", "B.env"), "TP_REMOTETHREAD=off\nWINEDLLOVERRIDES=d3d9=b\nRG_LOADER_FPFIX=true\n")
        self.routes = {"default": "evdev", "games": {"A": {"route": "tp-shm", "layout": "L", "launch": "direct",
                                                          "coin_keys": ["lshift", "t"], "note": "route-notitie"}}}
        self.notes = {"games": {"A": "Nederlandse notitie; met puntkomma."}, "games_en": {"A": "English note."}, "status": {"A": "playable"}}

    def run_migration(self, **kw):
        return self.mig.migrate(self.out, kw.get("routes", self.routes), kw.get("notes", self.notes), self.tp)

    def test_generates_and_is_idempotent(self):
        st = self.run_migration()
        self.assertTrue(st["changed"])
        a, _ = gs.read_ini(os.path.join(self.out, "A", "game.ini"))
        self.assertEqual(a["Game"]["Profile"], "A")
        self.assertEqual((a["Game"]["EmulationProfile"], a["Game"]["DongleRegion"], a["Game"]["PcbRegion"]), ("EmuA", "USA", "JAPAN"))
        self.assertEqual((a["Display"]["DisplayMode"], a["Display"]["CustomResolution"]), ("Windowed", "1920x1080"))
        self.assertEqual(a["Input"]["Route"], "tp-shm")
        self.assertEqual(a["Input"]["CoinKeys"], "lshift,t")
        self.assertEqual(a["Launch"]["Mode"], "direct")
        self.assertEqual(a["Status"]["Note"], "Nederlandse notitie; met puntkomma.")      # Nederlandse tekst exact
        self.assertEqual(a["Status"]["NoteEN"], "English note.")
        self.assertEqual(a["Loader"], {"PostStartMs": "3000", "RemoteThread": ""})        # standaard off geldt niet voor A: expliciet teruggezet
        self.assertEqual(a["Wine"]["Prefix"], "own")
        self.assertEqual(a["Wine"]["DllOverrides"], "winhttp=n,b")
        self.assertEqual(a["Env"], {"EXTRA": "1"})                                         # geen geheim, geen uitgecommentarieerde regel
        self.assertNotIn("geheim", read(os.path.join(self.out, "A", "game.ini")))
        b, _ = gs.read_ini(os.path.join(self.out, "B", "game.ini"))
        self.assertNotIn("Loader", b)                                                      # gelijk aan de standaard: niet herhaald
        self.assertNotIn("EmulationProfile", b["Game"])                                    # het profiel noemt er geen
        self.assertNotIn("Wine", b)
        self.assertEqual(b["Game"]["Type"], "OpenParrot")
        p, _ = gs.read_ini(os.path.join(self.out, "B", "patches.ini"))
        self.assertEqual(p, {"Patch: " + gs.FPFIX_PATCH: {"Enabled": "true"}})            # RG_LOADER_FPFIX -> de gedeelde patch aan
        self.assertFalse(os.path.exists(os.path.join(self.out, "C", "patches.ini")))
        self.assertNotIn("/home/", read(os.path.join(self.out, "A", "game.ini")).replace(os.environ["HOME"], "HOME-LEAK"))
        # Opnieuw draaien: niets verandert.
        self.assertEqual(self.run_migration()["changed"], [])
        # Handwerk blijft: een eigen sleutel en een eigen sectie met commentaar.
        path = os.path.join(self.out, "A", "game.ini")
        text = read(path).replace("[Status]", "; mijn knoppen\n[Bindings]\nHorn = kbd:E\n\n[Status]")
        write(path, text)
        self.run_migration()
        a, _ = gs.read_ini(path)
        self.assertEqual(a["Bindings"], {"Horn": "kbd:E"})
        self.assertIn("; mijn knoppen", read(path))
        # Een gewijzigde bron werkt door; een ontbrekende bron laat zijn sleutels met rust.
        notes2 = {"games": {"A": "Nieuwe tekst."}, "games_en": {"A": "New."}, "status": {"A": "parked"}}
        self.mig.migrate(self.out, None, notes2, self.tp)
        a, _ = gs.read_ini(path)
        self.assertEqual((a["Status"]["Status"], a["Status"]["Note"], a["Input"]["Route"]), ("parked", "Nieuwe tekst.", "tp-shm"))
        write(os.path.join(self.tp, "UserProfiles", "A.env"), "TP_REMOTETHREAD=1\n")
        self.run_migration(notes=notes2)
        a, _ = gs.read_ini(path)
        self.assertEqual(a["Loader"], {"RemoteThread": "1"})
        self.assertNotIn("Wine", a)                                                        # uit het .env verdwenen
        # Een profiel zonder runtime: de .env-afgeleide sleutels blijven ongemoeid.
        self.mig.migrate(self.out, self.routes, notes2, "")
        a, _ = gs.read_ini(path)
        self.assertEqual(a["Loader"], {"RemoteThread": "1"})

    def test_default_loader_route_is_explicit_when_env_lacks_it(self):
        write(os.path.join(self.tp, "UserProfiles", "A.env"), "")
        self.run_migration()
        a, _ = gs.read_ini(os.path.join(self.out, "A", "game.ini"))
        self.assertEqual(a["Loader"], {"RemoteThread": ""})
        g = gs.load_game("A", shipped=self.out, user=os.path.join(self.tmp, "nope"))
        self.assertEqual(gs.to_env(g.settings), {})                                        # net als zonder .env: geen TP_REMOTETHREAD


class TpAddGameTest(Tmp):
    def test_user_layer_and_bindings(self):
        tp = os.path.join(self.tmp, "tp")
        home = os.path.join(self.tmp, "home")
        os.makedirs(home)

        def template(prof, etype):
            btn = "".join("<JoystickButtons><ButtonName>%s</ButtonName></JoystickButtons>" % n
                          for n in ("Button 1 (red)", "Test", "Coin", "Wheel Axis Left", "Player 1 Start"))
            write(os.path.join(tp, "GameProfiles", prof + ".xml"),
                  "<GameProfile><GameNameInternal>Naam %s</GameNameInternal><GamePath></GamePath><EmulatorType>%s</EmulatorType>"
                  "<EmulationProfile>Emu%s</EmulationProfile>%s</GameProfile>" % (prof, etype, prof, btn))
        template("LGS", "TeknoParrot")
        template("XX2", "OpenParrot")
        write(os.path.join(self.shipped, "LGS", "game.ini"), "[Bindings]\nButton 1 (red) = kbd:A\nWheel Axis Left = kbd:Left\n")
        write(os.path.join(self.shipped, "_defaults", "OpenParrot.ini"), "[NewGame]\nEnablePatches = Lader, Tweede\n")
        for prof in ("LGS", "XX2"):
            exe = os.path.join(self.tmp, "spel", prof, "game.exe")
            write(exe, "x")
            env = dict(os.environ, HOME=home, XDG_DATA_HOME=os.path.join(home, ".local/share"), RAVENGUN_TP_DIR=tp,
                       RAVENGUN_PATCHES_DIR=self.shipped, RAVENGUN_LOADER_PATCHES_DIR=os.path.join(self.tmp, "geen-loader"), RAVENGUN_USER_PATCHES_DIR=self.user)
            r = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "tp-add-game.py"), prof, exe, "--env", "TP_POSTSTART_REMOTETHREAD_MS=3000"]
                               if prof == "LGS" else [sys.executable, "-I", os.path.join(SCRIPTS, "tp-add-game.py"), prof, exe],
                               capture_output=True, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        xml = read(os.path.join(tp, "UserProfiles", "LGS.xml"))
        self.assertIn("<KeyboardKey>A</KeyboardKey>", xml)          # [Bindings] van de game
        self.assertIn("<KeyboardKey>Left</KeyboardKey>", xml)
        self.assertIn("<KeyboardKey>F2</KeyboardKey>", xml)          # de vaste afspraak voor Test blijft
        self.assertIn("<KeyboardKey>D5</KeyboardKey>", xml)          # Coin
        self.assertFalse(os.path.exists(os.path.join(tp, "UserProfiles", "LGS.env")))   # geen .env meer
        s, _ = gs.read_ini(os.path.join(self.user, "LGS", "game.ini"))
        exe = os.path.join(self.tmp, "spel", "LGS", "game.exe")
        # Het hele profiel uit het sjabloon staat in de gebruikerslaag (er is geen meegeleverd profiel); het pad alleen als override, want er is
        # geen gamemap die de game terugvindt.
        self.assertEqual(s["Game"], {"Profile": "LGS", "Type": "TeknoParrot", "EmulationProfile": "EmuLGS", "Name": "Naam LGS",
                                     "Executable": "game.exe", "GamePath": exe})
        self.assertEqual(s["Identify"], {"Path": "game.exe", "Size": "1"})
        self.assertEqual(s["Loader"], {"PostStartMs": "3000", "RemoteThread": ""})   # eigen injectie: de standaardroute van het soort uit
        s, _ = gs.read_ini(os.path.join(self.user, "XX2", "game.ini"))
        self.assertEqual(s["Game"]["Type"], "OpenParrot")
        p, _ = gs.read_ini(os.path.join(self.user, "XX2", "patches.ini"))
        self.assertEqual(p, {"Patch: Lader": {"Enabled": "true"}, "Patch: Tweede": {"Enabled": "true"}})   # [NewGame] EnablePatches


if __name__ == "__main__":
    unittest.main(verbosity=1)

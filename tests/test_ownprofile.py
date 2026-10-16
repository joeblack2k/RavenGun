#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor het eigen spelprofiel in game.ini (docs/per-game-patches.md, "Het profiel van een game"): ConfigValues schrijven en lezen,
de importer (scripts/import-tp-profiles.py) met round-trip en behoud van handwerk, het afleiden van het pad uit de gamemappen, en de XML
als uitvoerformaat voor TeknoParrotUi. (De start zelf, met de revisiecontrole, test tests/test_direct_launch.cpp; de Patreon-poort
bestond alleen in scripts/launch-direct.py, dat weg is.)
Raakt alleen tijdelijke mappen (geen wine, geen game). Uitvoeren: python3 -I tests/test_ownprofile.py"""
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
import gamesettings as gs  # noqa: E402
import ownprofile as op  # noqa: E402


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def fi(cat, name, value, typ, opts=()):
    return ("<FieldInformation><CategoryName>%s</CategoryName><FieldName>%s</FieldName><FieldValue>%s</FieldValue><FieldType>%s</FieldType>"
            "<FieldOptions>%s</FieldOptions></FieldInformation>" % (cat, name, value, typ, "".join("<string>%s</string>" % o for o in opts)))


BUTTONS = ("<JoystickButtons><ButtonName>Test</ButtonName></JoystickButtons><JoystickButtons><ButtonName>Coin</ButtonName>"
           "<BindName>Keyboard X</BindName></JoystickButtons>")


def template_xml(patreon="false", rev="7"):
    return ("<GameProfile><GamePath></GamePath><EmulationProfile>EmuA</EmulationProfile><GameProfileRevision>%s</GameProfileRevision>"
            "<EmulatorType>OpenParrot</EmulatorType><ExecutableName>game.exe</ExecutableName><Patreon>%s</Patreon><Is64Bit>false</Is64Bit>"
            "<ConfigValues>%s%s%s%s</ConfigValues>%s</GameProfile>" % (
                rev, patreon,
                fi("General", "Input API", "DirectInput", "Dropdown", ["DirectInput", "RawInput"]),
                fi("General", "Windowed", "1", "Bool"),
                fi("General", "Keyboard/Button Axis", "5", "Slider"),
                fi("General", "Nickname", "", "Text"), BUTTONS))


def user_xml(game, rev="7", extra=""):
    x = template_xml(rev=rev).replace("<GamePath></GamePath>", "<GamePath>%s</GamePath>" % game)
    x = x.replace(fi("General", "Windowed", "1", "Bool"), fi("General", "Windowed", "0", "Bool"))
    x = x.replace(fi("General", "Nickname", "", "Text"), fi("General", "Nickname", "Jan", "Text"))
    return x.replace("</GameProfile>", extra + "</GameProfile>")


class Env(unittest.TestCase):
    """Een nep-runtime, nep-gamemap en lege lagen in een tijdelijke map; de omgeving wijst ernaar."""

    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = self._td.name
        self.saved = dict(os.environ)
        self.home = os.path.join(self.tmp, "home")
        self.tp = os.path.join(self.tmp, "tp")
        self.shipped = os.path.join(self.tmp, "patches")
        self.user = os.path.join(self.tmp, "user")
        self.games = os.path.join(self.tmp, "games")
        self.lib = os.path.join(self.tmp, "library.json")
        os.makedirs(self.home)
        os.environ.update({"HOME": self.home, "XDG_DATA_HOME": os.path.join(self.home, ".local/share"), "RAVENGUN_TP_DIR": self.tp,
                           "RAVENGUN_LOADER_PATCHES_DIR": self.shipped, "RAVENGUN_USER_PATCHES_DIR": self.user,   # "meegeleverd" = de onderste laag (loader); RAVENGUN_PATCHES_DIR zou erboven komen en de gebruikerslaag overrulen
                            "RAVENGUN_LIBRARY_FILE": self.lib,
                           "RAVENGUN_NO_LEGACY_ENV": "1"})
        os.environ.pop("XDG_CONFIG_HOME", None)
        os.environ.pop("RAVENGUN_PATCHES_DIR", None)
        for rel in ("OpenParrotWin32/OpenParrotLoader.exe", "OpenParrotWin32/OpenParrot.dll", "pipehelper.exe"):
            write(os.path.join(self.tp, rel), "x")
        write(os.path.join(self.tp, "ParrotData.xml"), "<ParrotData><ExitGameKey>0x1B</ExitGameKey><PauseGameKey>0x13</PauseGameKey></ParrotData>")
        wine = os.path.join(self.tmp, "wine", "bin", "wine")
        write(wine, "#!/bin/sh\n")
        self.wine = wine
        self.exe = os.path.join(self.games, "Spel A", "game.exe")
        write(self.exe, "abc")
        self.set_library([self.games])
        write(os.path.join(self.tp, "GameProfiles", "A.xml"), template_xml())
        write(os.path.join(self.tp, "UserProfiles", "A.xml"), user_xml(self.exe, extra="<WineRunnerPath>%s</WineRunnerPath>" % wine))
        write(os.path.join(self.shipped, "A", "game.ini"), "; handwerk\n[Game]\nName = Spel A\n\n[Status]\n; mijn notitie\nStatus = playable\n")

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self.saved)
        self._td.cleanup()

    def set_library(self, folders):
        write(self.lib, json.dumps({"schema": 1, "folders": [{"path": f, "recursive": True} for f in folders]}))

    def importer(self, *args):
        r = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "import-tp-profiles.py"), "--runtime", self.tp, "--out", self.shipped,
                            "--user", self.user] + list(args), capture_output=True, text=True, env=dict(os.environ))
        return r.returncode, r.stderr

def located(name="A"):
    """Waar de start de game vindt (ownprofile.locate_game: override, view of gamemappen)."""
    return op.locate_game(name, gs.load_game(name).settings)[0]


class ConfigValuesTest(Env):
    def test_round_trip_and_format(self):
        _, fields = op.read_xml_profile(os.path.join(self.tp, "UserProfiles", "A.xml"))
        sec = op.config_sections(fields)
        self.assertEqual(list(sec[op.CONFIG_TYPE]), ["General/Input API", "General/Windowed", "General/Keyboard/Button Axis", "General/Nickname"])
        self.assertEqual(sec[op.CONFIG_OPTIONS], {"General/Input API": "DirectInput|RawInput"})
        self.assertNotIn("General/Nickname=", sec[op.CONFIG])            # leeg = niet gezet
        text = gs.dump_ini(sec)
        back, _ = gs.parse_ini(text)
        again = op.fields_from_settings(back)
        self.assertEqual([(f["category"], f["name"], f["value"], f["type"], f["options"]) for f in again],
                         [(f["category"], f["name"], f["value"], f["type"], f["options"]) for f in fields])

    def test_user_layer_overrides_per_key_and_empty_clears(self):
        settings, _ = gs.parse_ini("[ConfigType]\nG/A = Text\nG/B = Text\n[Config]\nG/A = 1\nG/B = 2\n")
        user, _ = gs.parse_ini("[Config]\nG/A = 9\nG/B =\n")
        merged = gs.merge_into({s: dict(kv) for s, kv in settings.items()}, user)
        vals = {(f["category"], f["name"]): f["value"] for f in op.fields_from_settings(merged)}
        self.assertEqual(vals, {("G", "A"): "9", ("G", "B"): ""})
        # De app schrijft alleen het verschil naar de gebruikerslaag (set_user_value in C++); in Python: write_user_settings.
        gs.write_user_settings("A", {"Config": {"General/Windowed": "0"}}, user=self.user)
        s, _ = gs.read_ini(os.path.join(self.user, "A", "game.ini"))
        self.assertEqual(s, {"Config": {"General/Windowed": "0"}})

    def test_unrepresentable_is_refused(self):
        with self.assertRaises(ValueError):
            op.config_sections([{"category": "G", "name": "A", "value": "x ; y", "type": "Text", "options": []}])
        with self.assertRaises(ValueError):
            op.config_sections([{"category": "G", "name": "A=B", "value": "x", "type": "Text", "options": []}])


class TablesTest(unittest.TestCase):
    def test_cpp_tables_equal_python(self):
        """De tabellen in src/ravengun/gamesettings/gamesettings.cpp (kProfileFields, kEnvMap) zijn gelijk aan PROFILE_FIELDS en ENV_MAP."""
        import re
        text = read(os.path.join(ROOT, "src", "ravengun", "gamesettings", "gamesettings.cpp"))

        def table(name):
            body = text[text.index(name):]
            body = body[:body.index("};")]
            return [tuple(m) for m in re.findall(r'\{"([^"]*)", "([^"]*)", "([^"]*)", "([^"]*)"\}', body)]
        self.assertEqual(table("kProfileFields[]"), [tuple(f) for f in op.PROFILE_FIELDS])
        self.assertEqual(table("kEnvMap[]"), [tuple(m) for m in gs.ENV_MAP])


class ImporterTest(Env):
    def test_import_static_and_user_layer(self):
        rc, err = self.importer("A", "--game-folder", self.games)
        self.assertEqual(rc, 0, err)
        s, _ = gs.read_ini(os.path.join(self.shipped, "A", "game.ini"))
        self.assertEqual(s["Game"]["Executable"], "game.exe")
        self.assertEqual(s["Game"]["ProfileRevision"], "7")
        self.assertEqual(s["Game"]["Type"], "OpenParrot")
        self.assertEqual(s["Game"]["Name"], "Spel A")                            # handwerk blijft staan
        self.assertEqual(s["Identify"], {"Path": "game.exe", "Size": "3"})
        self.assertEqual(s["Config"]["General/Windowed"], "1")                   # de waarde van het sjabloon
        self.assertNotIn("General/Nickname", s["Config"])
        self.assertIn("; mijn notitie", read(os.path.join(self.shipped, "A", "game.ini")))
        self.assertIn("; Profiel uit TeknoParrot-sjabloon GameProfiles/A.xml, revisie 7", read(os.path.join(self.shipped, "A", "game.ini")))
        u, _ = gs.read_ini(os.path.join(self.user, "A", "game.ini"))
        self.assertEqual(u["Config"], {"General/Windowed": "0", "General/Nickname": "Jan"})   # alleen wat afwijkt
        self.assertEqual(u["Wine"], {"Runner": self.wine.replace(self.home, "~")} if self.wine.startswith(self.home) else {"Runner": self.wine})
        self.assertNotIn("GamePath", u.get("Game", {}))                          # het pad wordt afgeleid, niet opgeslagen
        for text in (read(os.path.join(self.shipped, "A", "game.ini")),):
            self.assertNotIn(self.games, text)
            self.assertNotIn("Joystick", text)
            self.assertNotIn("Bind", text)

    def test_idempotent_and_check(self):
        self.assertEqual(self.importer("A", "--game-folder", self.games)[0], 0)
        before = (read(os.path.join(self.shipped, "A", "game.ini")), read(os.path.join(self.user, "A", "game.ini")))
        self.assertEqual(self.importer("A", "--game-folder", self.games, "--check")[0], 0)
        self.assertEqual(self.importer("A", "--game-folder", self.games)[0], 0)
        self.assertEqual(before, (read(os.path.join(self.shipped, "A", "game.ini")), read(os.path.join(self.user, "A", "game.ini"))))
        # Een waarde in het gebruikersprofiel die terug gelijk is aan het sjabloon verdwijnt uit de gebruikerslaag.
        write(os.path.join(self.tp, "UserProfiles", "A.xml"), user_xml(self.exe).replace(fi("General", "Windowed", "0", "Bool"), fi("General", "Windowed", "1", "Bool")))
        self.assertEqual(self.importer("A", "--game-folder", self.games)[0], 0)
        u, _ = gs.read_ini(os.path.join(self.user, "A", "game.ini"))
        self.assertNotIn("General/Windowed", u.get("Config", {}))

    def test_game_not_in_folders_gets_override(self):
        self.set_library([])
        rc, err = self.importer("A")
        self.assertEqual(rc, 0, err)
        self.assertIn("override", err)
        u, _ = gs.read_ini(os.path.join(self.user, "A", "game.ini"))
        self.assertEqual(u["Game"]["GamePath"], gs.shrink_home(self.exe))
        # De override telt: de start vindt de game zonder gamemap.
        self.assertEqual(located(), self.exe)

    def test_game_folder_flag_needs_add_to_library(self):
        # --game-folder bepaalt de wortel van [Identify], maar alleen wat in library.json staat vindt de launcher straks terug.
        self.set_library([])
        rc, err = self.importer("A", "--game-folder", self.games)
        self.assertEqual(rc, 0, err)
        self.assertIn("override", err)
        u, _ = gs.read_ini(os.path.join(self.user, "A", "game.ini"))
        self.assertIn("GamePath", u["Game"])
        self.assertEqual(gs.read_ini(os.path.join(self.shipped, "A", "game.ini"))[0]["Identify"]["Path"], "game.exe")   # wortel: Spel A
        rc, err = self.importer("A", "--game-folder", self.games, "--add-to-library")
        self.assertEqual(rc, 0, err)
        self.assertEqual(op.library_folders(), [self.games])
        u, _ = gs.read_ini(os.path.join(self.user, "A", "game.ini"))
        self.assertNotIn("GamePath", u.get("Game", {}))
        self.assertEqual(located(), self.exe)

    def test_no_secrets_and_no_input_assignments(self):
        write(os.path.join(self.tp, "UserProfiles", "A.xml"), user_xml(self.exe, extra="<ExtraParameters>-token=abc123</ExtraParameters><PatreonSerialKey>geheim</PatreonSerialKey>"))
        self.assertEqual(self.importer("A", "--game-folder", self.games)[0], 0)
        text = read(os.path.join(self.shipped, "A", "game.ini")) + read(os.path.join(self.user, "A", "game.ini"))
        self.assertNotIn("abc123", text)
        self.assertNotIn("geheim", text)
        self.assertNotIn("Keyboard X", text)           # de bindingen van TeknoParrotUi

    def test_identical_main_file_gets_folder(self):
        other = os.path.join(self.games, "Spel B", "game.exe")      # dezelfde bytes: zelfde grootte en hash
        write(other, "abc")
        write(os.path.join(self.tp, "GameProfiles", "B.xml"), template_xml())
        write(os.path.join(self.tp, "UserProfiles", "B.xml"), user_xml(other))
        write(os.path.join(self.shipped, "B", "game.ini"), "[Game]\nName = B\n")
        self.assertEqual(self.importer("A", "B", "--game-folder", self.games)[0], 0)
        for name, exe in (("A", self.exe), ("B", other)):
            g = gs.load_game(name)
            self.assertEqual(op.locate_game(name, g.settings, [self.games])[0], exe, name)

    def test_revision_mismatch_imports_template_only(self):
        write(os.path.join(self.tp, "UserProfiles", "A.xml"), user_xml(self.exe, rev="6"))
        rc, err = self.importer("A", "--game-folder", self.games)
        self.assertEqual(rc, 0, err)
        self.assertIn("revisie 6", err)
        self.assertFalse(os.path.exists(os.path.join(self.user, "A", "game.ini")) and "Nickname" in read(os.path.join(self.user, "A", "game.ini")))

    def test_repo_tree_has_no_machine_paths(self):
        # Het meegeleverde profiel (de repo) bevat geen pad naar de game, geen NAS en geen thuismap, en geen invoertoewijzingen.
        for name in gs.list_games(os.path.join(ROOT, "patches"), os.path.join(self.tmp, "geen")):
            text = read(os.path.join(ROOT, "patches", name, "game.ini"))
            self.assertNotIn("GamePath", text, name)
            self.assertNotIn("/home/deck", text, name)
            self.assertNotIn("mnt/spidernas", text, name)
            self.assertNotIn("JoystickButtons", text, name)
            for var in gs.NEVER_IN_FILES:
                for line in text.splitlines():
                    key = line.split("=", 1)[0].strip().upper() if "=" in line and not line.lstrip().startswith((";", "#")) else ""
                    self.assertNotIn(var, key.replace("PATREON", "") if var == "PATREON" else key, "%s: %s" % (name, line))


class TpAddGameTest(Env):
    def run_add(self, *args):
        r = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "tp-add-game.py"), "A", self.exe, *args], capture_output=True, text=True, env=dict(os.environ))
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        return r

    def test_with_shipped_profile_only_differences(self):
        self.assertEqual(self.importer("A", "--game-folder", self.games)[0], 0)
        shutil.rmtree(os.path.join(self.user, "A"), ignore_errors=True)
        self.run_add("--force", "--fullscreen")
        u, _ = gs.read_ini(os.path.join(self.user, "A", "game.ini"))
        self.assertNotIn("Executable", u.get("Game", {}))                      # het profiel staat al in het meegeleverde game.ini
        self.assertNotIn("Identify", u)
        self.assertNotIn("GamePath", u.get("Game", {}))                        # de gamemap vindt de game
        self.assertEqual(u["Config"], {"General/Input API": "RawInput", "General/Windowed": "0"})   # alleen wat van het sjabloon afwijkt
        xml = read(os.path.join(self.tp, "UserProfiles", "A.xml"))
        self.assertIn("<GamePath>%s</GamePath>" % self.exe, xml)
        self.assertIn("<FieldName>Input API</FieldName><FieldValue>RawInput</FieldValue>", xml)
        # Opnieuw uitvoeren verandert niets aan de gebruikerslaag.
        before = read(os.path.join(self.user, "A", "game.ini"))
        self.run_add("--force", "--fullscreen")
        self.assertEqual(read(os.path.join(self.user, "A", "game.ini")), before)

    def test_new_game_gets_whole_profile_in_user_layer(self):
        shutil.rmtree(os.path.join(self.shipped, "A"))
        os.unlink(os.path.join(self.tp, "UserProfiles", "A.xml"))
        self.run_add()
        u, _ = gs.read_ini(os.path.join(self.user, "A", "game.ini"))
        self.assertEqual(u["Game"]["Executable"], "game.exe")
        self.assertEqual(u["Game"]["ProfileRevision"], "7")
        self.assertIn("General/Windowed", u["ConfigType"])
        self.assertEqual(u["Identify"]["Size"], "3")
        self.assertNotIn("GamePath", u["Game"])                                  # de gamemap in de bibliotheek vindt hem terug
        self.assertEqual(op.locate_game("A", gs.load_game("A").settings, [self.games])[0], self.exe)


class LocateTest(Env):
    def test_derived_from_folders_view_and_override(self):
        self.assertEqual(self.importer("A", "--game-folder", self.games)[0], 0)
        g = gs.load_game("A")
        self.assertEqual(op.locate_game("A", g.settings), (self.exe, "folders"))
        self.set_library([])
        self.assertEqual(op.locate_game("A", g.settings), ("", ""))
        # Een lokale view (runtime/views/<Game>) gaat voor de gamemappen.
        view_exe = os.path.join(op.views_dir(), "A", "game.exe")
        write(view_exe, "abc")
        self.assertEqual(op.locate_game("A", g.settings, [self.games]), (view_exe, "view"))
        os.unlink(view_exe)
        # Een andere versie van het bestand (andere grootte) wordt niet herkend.
        write(self.exe, "abcdef")
        self.assertEqual(op.locate_game("A", g.settings, [self.games]), ("", ""))
        # De override van de gebruiker wint.
        gs.write_user_settings("A", {"Game": {"GamePath": "/ergens/anders/game.exe"}}, user=self.user)
        self.assertEqual(op.locate_game("A", gs.load_game("A").settings, [self.games]), ("/ergens/anders/game.exe", "override"))


class CheapLocateTest(Env):
    """Eerst wat goedkoop is (gelijk aan gamesettings::locate_game in C++): [Identify] Folder, dan library.json, dan zoeken; de sha256 uit
    de cache (zelfde bestand en indeling als launch::HashCache)."""

    def settings(self, **ident):
        return {"Game": {"Executable": "game.exe"}, "Identify": dict({"Path": "game.exe", "Size": "3"}, **ident)}

    def test_order_and_hints(self):
        other = os.path.join(self.games, "Een andere", "game.exe")   # sorteert voor "Spel A"
        write(other, "abc")
        s = self.settings()
        self.assertEqual(op.locate_game("A", s), (other, "folders"))                       # zoeken: de eerste in de gamemap
        self.assertEqual(op.locate_game("A", self.settings(Folder="Spel A")), (self.exe, "folders"))
        # library.json kent de map van dit profiel: die gaat voor het zoeken (folder, of de map van exe).
        with open(self.lib, encoding="utf-8") as f:
            lib = json.load(f)
        lib["games"] = [{"profile": "A", "folder": os.path.dirname(self.exe) + "/"}, {"profile": "B", "folder": os.path.dirname(other)}]
        write(self.lib, json.dumps(lib))
        self.assertEqual(op.library_roots("A", "game.exe"), [os.path.dirname(self.exe)])
        self.assertEqual(op.locate_game("A", s), (self.exe, "folders"))
        self.assertEqual(op.locate_game("A", s, [self.games]), (other, "folders"))       # een eigen lijst mappen: geen hints
        lib["games"] = [{"profile": "A", "exe": self.exe}]
        write(self.lib, json.dumps(lib))
        self.assertEqual(op.library_roots("A", "game.exe"), [os.path.dirname(self.exe)])
        self.assertEqual(op.library_roots("A", "anders.exe"), [])

    def test_hash_cache(self):
        cache = os.path.join(self.tmp, "cache", "sha256-cache.tsv")
        os.environ["RAVENGUN_HASH_CACHE"] = cache
        sha = op.sha256_file(self.exe)
        calls = []
        real = op.sha256_file
        op.sha256_file = lambda p: calls.append(p) or real(p)
        try:
            op._hash_mem.clear()
            op._hash_loaded.clear()
            self.assertEqual(op.locate_game("A", self.settings(Sha256=sha)), (self.exe, "folders"))
            self.assertEqual(op.locate_game("A", self.settings(Sha256=sha)), (self.exe, "folders"))
            self.assertEqual(calls, [self.exe])                                              # een keer gehasht
            with open(cache, encoding="utf-8") as f:
                lines = f.read().splitlines()
            st = os.stat(self.exe)
            self.assertEqual(lines[1], "%s\t%d\t%d\t%s" % (sha, st.st_size, st.st_mtime_ns, self.exe))
            op._hash_mem.clear()                                                             # een nieuw proces: uit het bestand
            op._hash_loaded.clear()
            self.assertEqual(op.sha256_cached(self.exe), sha)
            self.assertEqual(len(calls), 1)
            write(self.exe, "xyz")                                                           # andere inhoud en mtime: opnieuw
            os.utime(self.exe, ns=(st.st_atime_ns, st.st_mtime_ns + 5 * 10**9))
            self.assertNotEqual(op.sha256_cached(self.exe), sha)
            self.assertEqual(len(calls), 2)
        finally:
            op.sha256_file = real
            op._hash_mem.clear()
            op._hash_loaded.clear()


class XmlOutputTest(Env):
    def test_write_xml_creates_and_updates(self):
        self.assertEqual(self.importer("A", "--game-folder", self.games)[0], 0)
        os.unlink(os.path.join(self.tp, "UserProfiles", "A.xml"))
        write(os.path.join(self.shipped, "A", "game.ini"), read(os.path.join(self.shipped, "A", "game.ini")) + "\n[Bindings]\nCoin = kbd:C\n")
        r = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "gamesettings.py"), "write-xml", "A"], capture_output=True, text=True, env=dict(os.environ))
        self.assertEqual(r.returncode, 0, r.stderr)
        xml = read(os.path.join(self.tp, "UserProfiles", "A.xml"))
        self.assertIn("<GamePath>%s</GamePath>" % self.exe, xml)
        self.assertIn("<FieldName>Windowed</FieldName><FieldValue>0</FieldValue>", xml)
        self.assertIn("<FieldName>Nickname</FieldName><FieldValue>Jan</FieldValue>", xml)
        self.assertIn("<KeyboardKey>C</KeyboardKey>", xml)                     # [Bindings] van de game
        self.assertIn("<FieldName>Input API</FieldName><FieldValue>DirectInput</FieldValue>", xml)     # game.ini bepaalt, ook voor de XML
        # Een bestaand bestand blijft op zijn plaats (knoppen), alleen pad en waarden worden bijgewerkt.
        write(os.path.join(self.tp, "UserProfiles", "A.xml"), xml.replace("RavenGun Virtual Keyboard C", "Mijn toets"))
        gs.write_user_settings("A", {"Config": {"General/Windowed": "1", "General/Input API": "RawInput"}}, user=self.user)
        subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "gamesettings.py"), "write-xml", "A"], check=True, capture_output=True, env=dict(os.environ))
        xml2 = read(os.path.join(self.tp, "UserProfiles", "A.xml"))
        self.assertIn("<FieldName>Windowed</FieldName><FieldValue>1</FieldValue>", xml2)
        self.assertIn("<FieldName>Input API</FieldName><FieldValue>RawInput</FieldValue>", xml2)
        self.assertIn("Mijn toets", xml2)
        # Zonder eigen profiel doet het commando niets.
        r = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "gamesettings.py"), "write-xml", "Onbekend"], capture_output=True, text=True, env=dict(os.environ))
        self.assertEqual((r.returncode, r.stdout), (0, ""))

    def test_second_executable_path(self):
        """Bij twee exe's (HOTDSD) komt GamePath2 uit [Identify] Path2, in dezelfde gamemap als de hoofdexe."""
        write(os.path.join(self.games, "Spel A", "AM", "daemon.exe"), "x")
        settings = {"Game": {"Executable": "game.exe"}, "Identify": {"Path": "game.exe", "Path2": "AM/daemon.exe"}, "ConfigType": {}}
        text, _unknown = op.render_user_xml("<GameProfile><GamePath2></GamePath2></GameProfile>", settings, self.exe)
        self.assertIn("<GamePath2>%s</GamePath2>" % os.path.join(self.games, "Spel A", "AM", "daemon.exe"), text)
        text, _unknown = op.render_user_xml("<GameProfile></GameProfile>", {"Identify": {"Path": "game.exe", "Path2": "weg.exe"}}, self.exe)
        self.assertNotIn("GamePath2", text)          # een bestand dat er niet is, wordt niet gezet

    def test_has_profile_and_comms(self):
        self.assertEqual(self.importer("A", "--game-folder", self.games)[0], 0)
        run = lambda *a: subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "gamesettings.py")] + list(a), capture_output=True, text=True, env=dict(os.environ))
        self.assertEqual(run("has-profile", "A").returncode, 0)
        self.assertEqual(run("has-profile", "Onbekend").returncode, 1)
        self.assertEqual(run("comms", "A").stdout.strip(), "game.exe")


if __name__ == "__main__":
    unittest.main(verbosity=1)

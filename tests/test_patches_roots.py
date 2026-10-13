#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""De zoekvolgorde van patches/<Game>/ en het vinden van een game op identiteit (docs/per-game-patches.md, "Zoekvolgorde" en
"Identiteit"). Raakt alleen tijdelijke mappen. Uitvoeren: python3 -I tests/test_patches_roots.py

    gamesettings.patch_layers()/patches_roots()   de ENE volgorde: loader-patches/ (onderaan), gebruiker, $RAVENGUN_PATCHES_DIR (bovenaan)
    gamesettings.find_game_dir()                  op [Game] Profile, niet alleen op de mapnaam
"""
import contextlib
import importlib.util
import io
import os
import re
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

ENV_KEYS = ("RAVENGUN_LOADER_PATCHES_DIR", "RAVENGUN_USER_PATCHES_DIR", "RAVENGUN_PATCHES_DIR", "XDG_DATA_HOME", "RAVENGUN_NO_LEGACY_ENV")


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def laad(naam):
    """Een script uit scripts/ als module laden (amdaemon, region, matrix, hasp)."""
    spec = importlib.util.spec_from_file_location("rgtest_" + naam, os.path.join(SCRIPTS, naam + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Lagen(unittest.TestCase):
    """Drie echte mappen: loader (L), gebruiker (U), ontwikkel-checkout (D)."""

    def setUp(self):
        self._td = tempfile.TemporaryDirectory()
        self.tmp = self._td.name
        self.saved = dict(os.environ)
        self.L, self.U, self.D = (os.path.join(self.tmp, n) for n in ("loader", "user", "dev"))
        for d in (self.L, self.U, self.D):
            os.makedirs(d)
        self.zet(L=self.L, U=self.U, D=self.D)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self.saved)
        self._td.cleanup()

    def zet(self, L=None, U=None, D=None):
        for k in ENV_KEYS:
            os.environ.pop(k, None)
        for k, v in (("RAVENGUN_LOADER_PATCHES_DIR", L), ("RAVENGUN_USER_PATCHES_DIR", U), ("RAVENGUN_PATCHES_DIR", D)):
            if v:
                os.environ[k] = v
        os.environ["RAVENGUN_NO_LEGACY_ENV"] = "1"


class Volgorde(Lagen):
    def test_de_gedocumenteerde_volgorde(self):
        # onderaan de loader, dan de gebruiker, bovenaan de ontwikkel-checkout
        self.assertEqual(gs.patches_roots(), [self.L, self.U, self.D])
        self.assertEqual([k for k, _p in gs.patch_layers()], ["loader", "user", "dev"])

    def test_standaard_onderste_laag_is_de_patches_van_de_repo(self):
        # zonder verhuizing is de onderste laag <repo>/patches (ook als hij op deze machine een koppeling is)
        os.environ.pop("RAVENGUN_LOADER_PATCHES_DIR")
        eerste = gs.patch_layers(only_existing=False)[0]
        self.assertEqual(eerste, ("loader", os.path.join(ROOT, "patches")))

    def test_ontbrekende_lagen_vallen_weg_de_rest_blijft_in_volgorde(self):
        self.zet(L=self.L, U=self.U, D=None)
        self.assertEqual(gs.patches_roots(), [self.L, self.U])
        self.zet(L=self.L, U=os.path.join(self.tmp, "bestaat-niet"), D=self.D)
        self.assertEqual(gs.patches_roots(), [self.L, self.D])
        self.zet(L=os.path.join(self.tmp, "geen"), U=self.U, D=self.D)
        self.assertEqual(gs.patches_roots(), [self.U, self.D])

    def test_dezelfde_map_twee_keer_komt_een_keer_voor_op_de_hoogste_plek(self):
        # de ontwikkel-checkout is (via een koppeling) de patches/ van de loader: een keer, bovenaan
        koppeling = os.path.join(self.tmp, "koppeling")
        os.symlink(self.L, koppeling)
        self.zet(L=self.L, U=self.U, D=koppeling)
        self.assertEqual(gs.patches_roots(), [self.U, koppeling])

    def test_afgeleide_functies_komen_uit_dezelfde_volgorde(self):
        self.assertEqual((gs.shipped_dir(), gs.user_dir()), (self.D, self.U))   # shipped = bovenste meegeleverde laag
        self.zet(L=self.L, U=self.U, D=None)
        self.assertEqual((gs.shipped_dir(), gs.user_dir()), (self.L, self.U))

    def test_expliciete_mappen_vervangen_de_meegeleverde_lagen(self):
        s, u = os.path.join(self.tmp, "s"), os.path.join(self.tmp, "u")
        os.makedirs(s)
        os.makedirs(u)
        self.assertEqual(gs.patches_roots(s, u), [s, u])     # geen loader, geen ontwikkel-checkout ertussen
        self.assertEqual(gs.patches_roots(shipped=s), [s, self.U])

    def test_cli_dirs_toont_de_lagen_in_volgorde(self):
        env = dict(os.environ)
        r = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "gamesettings.py"), "dirs"], capture_output=True, text=True, env=env)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual([l.split(":")[0] for l in r.stdout.splitlines()], ["loader", "user", "dev"], r.stdout)


class Samenvoegen(Lagen):
    def test_de_ontwikkelcheckout_komt_BOVENOP_en_vervangt_niets(self):
        # een sleutel die in elke laag anders staat: de bovenste wint; wat alleen onderin staat blijft
        write(os.path.join(self.L, "Spel", "game.ini"), "[Game]\nProfile = Spel\n[K]\nx = loader\ny = loader\nz = loader\nw = loader\n")
        write(os.path.join(self.U, "Spel", "game.ini"), "[K]\nx = user\nz = user\nw = user\n")
        write(os.path.join(self.D, "Spel", "game.ini"), "[K]\nx = dev\nw =\n")        # w leeg: haalt de sleutel weg, ook onderin
        write(os.path.join(self.L, "AlleenLoader", "game.ini"), "[Game]\nName = alleen in de loader\n")
        g = gs.load_game("Spel")
        self.assertEqual((g.get("K", "x"), g.get("K", "y"), g.get("K", "z"), g.get("K", "w")), ("dev", "loader", "user", None))
        self.assertEqual([os.path.dirname(os.path.dirname(s)) for s in g.sources], [self.L, self.U, self.D])
        # RAVENGUN_PATCHES_DIR vervangt de patches/ van de loader NIET: een game die alleen daar staat blijft vindbaar
        self.assertEqual(gs.load_game("AlleenLoader").get("Game", "Name"), "alleen in de loader")
        self.assertIn("AlleenLoader", gs.list_games())

    def test_zonder_ontwikkelcheckout_wint_de_gebruiker_boven_de_loader(self):
        self.zet(L=self.L, U=self.U, D=None)
        write(os.path.join(self.L, "Spel", "game.ini"), "[K]\nx = loader\n")
        write(os.path.join(self.U, "Spel", "game.ini"), "[K]\nx = user\n")
        self.assertEqual(gs.load_game("Spel").get("K", "x"), "user")

    def test_patches_ini_volgt_dezelfde_lagen(self):
        for wortel, enabled in ((self.L, "false"), (self.U, "true"), (self.D, "")):
            body = "[Patch: P]\nKind = file\nCategory = paths\nFind = aa\nReplace = bb\nEnabled = %s\n" % enabled if wortel == self.L else "[Patch: P]\nEnabled = %s\n" % enabled
            write(os.path.join(wortel, "Spel", "patches.ini"), body)
        write(os.path.join(self.L, "Spel", "game.ini"), "[Game]\nProfile = Spel\n")
        g = gs.load_game("Spel")
        # D heeft Enabled leeg: haalt de sleutel weg -> de patch valt terug op "uit" (default), niet op de waarde van U
        self.assertEqual([(p.name, p.enabled) for p in g.patches], [("P", False)])
        write(os.path.join(self.D, "Spel", "patches.ini"), "[Patch: P]\nEnabled = true\n")
        self.assertEqual([(p.name, p.enabled) for p in gs.load_game("Spel").patches], [("P", True)])

    def test_defaults_komen_uit_de_meegeleverde_lagen_in_volgorde(self):
        write(os.path.join(self.L, "_defaults", "T.ini"), "[K]\na = loader\nb = loader\n")
        write(os.path.join(self.D, "_defaults", "T.ini"), "[K]\na = dev\n")
        write(os.path.join(self.U, "_defaults", "T.ini"), "[K]\nb = user-negeert\n")    # de gebruikerslaag levert geen _defaults
        write(os.path.join(self.L, "Spel", "game.ini"), "[Game]\nType = T\n")
        g = gs.load_game("Spel")
        self.assertEqual((g.get("K", "a"), g.get("K", "b")), ("dev", "loader"))


class EenKopie(Lagen):
    """De volgorde staat op een plek. amdaemon, region, matrix en hasp leiden hem af; geen van hen mag er een eigen kopie van hebben."""

    def test_de_scripts_geven_dezelfde_wortels_als_gamesettings(self):
        verwacht = list(reversed(gs.patches_roots()))        # "eerste treffer wint": bovenste laag eerst
        self.assertEqual(verwacht, [self.D, self.U, self.L])
        for naam in ("amdaemon", "region", "matrix", "hasp"):
            mod = laad(naam)
            self.assertEqual(mod.patches_dirs(), verwacht, naam)

    def test_ook_zonder_ontwikkelcheckout_en_met_ontbrekende_laag(self):
        self.zet(L=self.L, U=os.path.join(self.tmp, "geen-gebruiker"), D=None)
        for naam in ("amdaemon", "region", "matrix", "hasp"):
            self.assertEqual(laad(naam).patches_dirs(), list(reversed(gs.patches_roots())), naam)
        self.assertEqual(gs.patches_roots(), [self.L])

    def test_geen_tweede_kopie_van_de_volgorde_in_de_scripts(self):
        # Wie RAVENGUN_(USER_)PATCHES_DIR zelf uit de omgeving leest, heeft een eigen volgorde. Alleen gamesettings.py mag dat.
        lek = []
        for f in sorted(os.listdir(SCRIPTS)):
            if not f.endswith((".py", ".sh")) or f == "gamesettings.py":
                continue
            with open(os.path.join(SCRIPTS, f), encoding="utf-8") as fh:
                for n, regel in enumerate(fh, 1):
                    code = regel.split("#", 1)[0]
                    if not re.search(r"RAVENGUN_(USER_|LOADER_)?PATCHES_DIR", code):
                        continue
                    if (f.endswith(".py") and re.search(r"environ|getenv", code)) or (f.endswith(".sh") and re.search(r"\$\{?RAVENGUN_", code)):
                        lek.append("%s:%d: %s" % (f, n, regel.strip()))
        self.assertEqual(lek, [], "een eigen kopie van de zoekvolgorde buiten gamesettings.py:\n" + "\n".join(lek))


class Identiteit(Lagen):
    def test_een_hernoemde_map_wordt_op_profile_gevonden_met_een_luide_waarschuwing(self):
        write(os.path.join(self.L, "OudeNaam", "game.ini"), "[Game]\nProfile = Echt\nName = Hernoemd\n[K]\nx = 1\n")
        fout = io.StringIO()
        with contextlib.redirect_stderr(fout):
            g = gs.load_game("Echt")
        self.assertTrue(g.exists, "de map met [Game] Profile = Echt moet gevonden worden")
        self.assertEqual((g.get("Game", "Name"), g.get("K", "x")), ("Hernoemd", "1"))
        self.assertTrue(any("OudeNaam" in w and "Echt" in w for w in g.warnings), g.warnings)
        self.assertIn("WAARSCHUWING", fout.getvalue())
        self.assertIn("OudeNaam", fout.getvalue())
        self.assertIn("Echt", gs.list_games())                # de lijst noemt wat te laden is, niet de verkeerde mapnaam

    def test_snelle_weg_met_een_profile_dat_niet_de_mapnaam_is_waarschuwt_en_zoekt_verder(self):
        # map "Foo" zegt Profile = Bar; een andere map "Baz" zegt Profile = Foo. Wie "Foo" vraagt krijgt Baz, met waarschuwing.
        write(os.path.join(self.L, "Foo", "game.ini"), "[Game]\nProfile = Bar\n[K]\nwie = foo-map\n")
        write(os.path.join(self.L, "Baz", "game.ini"), "[Game]\nProfile = Foo\n[K]\nwie = baz-map\n")
        warnings = []
        with contextlib.redirect_stderr(io.StringIO()):
            pad = gs.find_game_dir(self.L, "Foo", warnings)
        self.assertEqual(pad, os.path.join(self.L, "Baz"))
        self.assertTrue(any("Profile = 'Bar'" in w for w in warnings), warnings)

    def test_mapnaam_klopt_maar_profile_erin_niet_en_niets_anders_past(self):
        # geen andere map claimt "Foo": de map "Foo" wordt gebruikt (er is niets beters), luid
        write(os.path.join(self.L, "Foo", "game.ini"), "[Game]\nProfile = Bar\n[K]\nx = 1\n")
        fout = io.StringIO()
        with contextlib.redirect_stderr(fout):
            g = gs.load_game("Foo")
        self.assertTrue(g.exists)
        self.assertEqual(g.get("K", "x"), "1")
        self.assertTrue(any("Profile = 'Bar'" in w for w in g.warnings), g.warnings)
        self.assertIn("WAARSCHUWING", fout.getvalue())

    def test_klopt_alles_dan_geen_waarschuwing(self):
        write(os.path.join(self.L, "Goed", "game.ini"), "[Game]\nProfile = Goed\n")
        fout = io.StringIO()
        with contextlib.redirect_stderr(fout):
            g = gs.load_game("Goed")
        self.assertEqual((g.exists, g.warnings, fout.getvalue()), (True, [], ""))

    def test_een_game_die_er_niet_is_geeft_een_duidelijk_niet_gevonden_en_geen_crash(self):
        write(os.path.join(self.L, "Anders", "game.ini"), "[Game]\nProfile = Anders\n")
        with contextlib.redirect_stderr(io.StringIO()) as fout:
            g = gs.load_game("BestaatNiet")
            self.assertIsNone(gs.find_game_dir(self.L, "BestaatNiet"))
            self.assertEqual(gs.locate_game_dirs("BestaatNiet"), [])
        self.assertFalse(g.exists)
        self.assertEqual((g.settings, g.sources), ({}, []))
        self.assertEqual(fout.getvalue(), "")        # niet gevonden is geen mismatch: geen valse alarm

    def test_ontbrekende_wortel_en_lege_naam_crashen_niet(self):
        self.assertIsNone(gs.find_game_dir(os.path.join(self.tmp, "geen-wortel"), "X"))
        self.assertIsNone(gs.find_game_dir(self.L, ""))

    def test_gebruikerslaag_en_loader_vinden_dezelfde_game_onder_verschillende_mapnamen(self):
        write(os.path.join(self.L, "Spel", "game.ini"), "[Game]\nProfile = Spel\n[K]\nx = loader\ny = loader\n")
        write(os.path.join(self.U, "mijn-kopie", "game.ini"), "[Game]\nProfile = Spel\n[K]\nx = user\n")
        with contextlib.redirect_stderr(io.StringIO()):
            g = gs.load_game("Spel")
        self.assertEqual((g.get("K", "x"), g.get("K", "y")), ("user", "loader"))

    def test_schrijven_naar_de_gebruikerslaag_blijft_in_de_hernoemde_map(self):
        write(os.path.join(self.U, "mijn-kopie", "game.ini"), "[Game]\nProfile = Spel\n")
        with contextlib.redirect_stderr(io.StringIO()):
            gs.write_user_settings("Spel", {"K": {"x": "1"}})
        self.assertFalse(os.path.exists(os.path.join(self.U, "Spel")), "er mag geen tweede map Spel naast mijn-kopie komen")
        with open(os.path.join(self.U, "mijn-kopie", "game.ini"), encoding="utf-8") as f:
            self.assertIn("x = 1", f.read())

    def test_de_scripts_vinden_een_hernoemde_map_ook(self):
        # hasp: dongle.json in een hernoemde map; region/amdaemon/matrix lopen via dezelfde find_game_dir
        write(os.path.join(self.L, "OudeNaam", "game.ini"), "[Game]\nProfile = Echt\n")
        write(os.path.join(self.L, "OudeNaam", "dropin", "ravengun-hasp", "Echt", "dongle.json"), '{"kind": "region", "game": "Echt"}')
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(laad("amdaemon").game_map("Echt"), os.path.join(self.L, "OudeNaam"))
            self.assertIsNotNone(laad("region").dongle_json_pad("Echt"))
            self.assertEqual(laad("hasp").dongle_json("Echt")[0], os.path.join(self.L, "OudeNaam"))


if __name__ == "__main__":
    unittest.main()

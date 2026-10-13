#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests: onze artefacten in een aagames-view of TeknoParrot-view komen niet op de (alleen-lezen) NAS uit.

Ontwerp (besluit van de eigenaar, 2026-10-12): de games wonen op de NAS; wat een game zelf schrijft (saves, instellingen, NVRAM,
logs in zijn eigen spelmap) mag daar staan en is geen fout. Onze EIGEN artefacten niet: drop-ins, patchbestanden, onze plugin-DLL's
met hun config/cache/log (de BepInEx-boom), hulpbestanden. Gemeten (2026-10-11/12, Rabbids Hollywood): Game_Data/Plugins was een
symlink naar de NAS (aagames-setup.py) en op de NAS stonden BepInEx/config, cache en LogOutput.log van onze plugin (de
TeknoParrot-view uit tp-add-game.py --view linkte BepInEx/).

Plekken van onze artefacten die hier gecontroleerd worden (onafhankelijk van de code onder test):
  - de view zelf (geen symlink, niet op de NAS);
  - BepInEx/ met alles erin (plugins, config, cache, LogOutput.log);
  - Game_Data/Plugins met alles erin (daar komen de drop-ins).
Eigen bestanden van de game (StreamingAssets, ControllerBaseConfig.json, ...) worden NIET gecontroleerd: dat die als symlink naar de
NAS staan mag (expliciete test). De aagames-route zet er om gemak wel lokale kopieen van neer; dat staat in de docstring van
scripts/aagames-setup.py en wordt hier als gedrag getest, niet als NAS-regel.

De namaak-tests bouwen hun eigen NAS in een tijdelijke map. De test op de echte, al klaargezette views leest alleen
(os.walk/readlink) en slaat zichzelf over als de view of de NAS er niet is.

Om te laten zien dat de test het oude gedrag vangt: RG_SCRIPTS_DIR=<map met een oude kopie van scripts/> (bijv. via
`git archive <oude commit> scripts | tar -x -C <tijdelijke map>`).
"""
import importlib.util
import json
import os
import shutil
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPTS = os.environ.get("RG_SCRIPTS_DIR") or os.path.join(REPO, "scripts")
ECHTE_NAS = os.path.realpath(os.path.expanduser("~/mnt/spidernas_emulation"))
VIEWS_AAGAMES = os.path.expanduser("~/.local/share/ravengun/runtime/views-aagames")


def schrijf(pad, tekst):
    with open(pad, "w") as f:
        f.write(tekst)


def leesbestand(pad):
    with open(pad) as f:
        return f.read()


def laad(naam, pad):
    spec = importlib.util.spec_from_file_location(naam, pad)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[naam] = mod
    spec.loader.exec_module(mod)
    return mod


def setup_module():
    return laad("aagames_setup_onder_test", os.path.join(SCRIPTS, "aagames-setup.py"))


def onder(pad, wortel):
    p, w = os.path.realpath(pad), os.path.realpath(wortel)
    return p == w or p.startswith(w + os.sep)


def schrijfpaden(view, gamedata=True):
    """Plekken in de view waar ONZE artefacten komen (map-ingangen en bestanden): de view, BepInEx/ en Game_Data/Plugins.
    gamedata=False laat Game_Data weg: bij de TeknoParrot-view wordt die map pas bij het plaatsen van een drop-in lokaal
    gemaakt (launch-direct.dropin_materialiseer, getest in test_dropin_materialiseer.py)."""
    uit = [view]
    for rel in ("BepInEx",) + (("Game_Data/Plugins",) if gamedata else ()):
        top = os.path.join(view, rel)
        if not os.path.lexists(top):
            continue
        uit.append(top)
        if os.path.isdir(top) and not os.path.islink(top):
            for d, dirs, files in os.walk(top, followlinks=False):
                uit += [os.path.join(d, x) for x in dirs + files]
    return uit


def schendingen(view, nas, gamedata=True):
    """Plekken van onze artefacten die een symlink zijn die onder de NAS uitkomt, of die via zo'n link op de NAS liggen."""
    fouten = []
    for p in schrijfpaden(view, gamedata):
        if os.path.islink(p) and onder(p, nas):
            fouten.append("%s -> %s" % (p, os.path.realpath(p)))
        elif onder(p, nas):
            fouten.append("%s ligt op de NAS (via %s)" % (p, os.path.realpath(p)))
    return fouten


def maak_namaak_nas(nas):
    """Een namaak-NAS zoals die van Rabbids: TeknoParrot-BepInEx, Game_Data met Plugins en StreamingAssets."""
    def w(rel, inhoud="x"):
        pad = os.path.join(nas, rel)
        os.makedirs(os.path.dirname(pad), exist_ok=True)
        with open(pad, "w") as f:
            f.write(inhoud)

    w("Game.exe", "MZ" * 100)
    w("UnityPlayer.dll", "u" * 300000)
    w("ControllerBaseConfig.json", "{}")
    w("Game.run", "")
    w("ForceReboot", "")
    w("groot.json", "j" * (64 * 1024 + 10))
    w("teknoparrot.ini", "[TP]")
    w("BepInEx/core/BepInEx.dll", "tp")
    w("BepInEx/config/BepInEx.cfg", "tp")
    w("BepInEx/plugins/UnityPlugin_BepInEx_RHA.dll", "tp")
    w("BepInEx/LogOutput.log", "tp")
    w("Game_Data/boot.config", "gfx-enable=1")
    w("Game_Data/app.info", "Studio\nGame")
    w("Game_Data/level0", "L" * 1000)
    w("Game_Data/resources.assets", "R" * 1000)
    w("Game_Data/Managed/Assembly-CSharp.dll", "M" * 1000)
    w("Game_Data/Mono/etc/mono/config", "c")
    w("Game_Data/Plugins/matrix64.dll", "orig")
    w("Game_Data/Plugins/sxCoreSettings.txt", "instellingen")
    w("Game_Data/Plugins/sub/extra.ini", "e")
    w("Game_Data/StreamingAssets/localassetbundle", "b")


def nas_snapshot(nas):
    uit = {}
    for d, dirs, files in os.walk(nas):
        for x in dirs + files:
            p = os.path.join(d, x)
            uit[p] = (os.lstat(p).st_mtime_ns, os.lstat(p).st_size)
    return uit


class Basis(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-schrijfpaden-")
        self.nas = os.path.join(self.tmp, "nas", "Game ")  # met spatie aan het eind, zoals Rabbids
        maak_namaak_nas(self.nas)
        self.nas_wortel = os.path.join(self.tmp, "nas")
        self.voor = nas_snapshot(self.nas_wortel)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def nas_ongewijzigd(self):
        self.assertEqual(self.voor, nas_snapshot(self.nas_wortel), "de (namaak-)NAS is gewijzigd")


class TestAagamesSetup(Basis):
    def setUp(self):
        super().setUp()
        self.m = setup_module()
        self.m.VIEWS = os.path.join(self.tmp, "views-aagames")
        self.g = {"nas_dir": self.nas, "exe": "Game.exe", "width": 1920, "height": 1080}
        # een namaak-BepInEx-release en -plugin, zodat build_view (incl. install_ravengun) echt draait
        bep = os.path.join(self.tmp, "release", "5.4.21.0", "x64")
        os.makedirs(os.path.join(bep, "BepInEx", "core"))
        for f in ("BepInEx/core/BepInEx.dll", "winhttp.dll", "doorstop_config.ini"):
            schrijf(os.path.join(bep, f), "release")
        self.m.BEPINEX = os.path.join(self.tmp, "release")
        src = os.path.join(self.tmp, "src")
        os.makedirs(src)
        cs = os.path.join(src, "a.cs")
        schrijf(cs, "//")
        os.utime(cs, (time.time() - 100, time.time() - 100))
        self.m.PLUGIN_SRC = src
        self.m.BUILD_OUT = os.path.join(self.tmp, "build")
        os.makedirs(os.path.join(self.m.BUILD_OUT, "G"))
        schrijf(os.path.join(self.m.BUILD_OUT, "G", "RavenGun.G.dll"), "plugin")

    def bouw(self):
        return self.m.build_view("G", self.g, "v", "ravengun", {}, False, False)

    def test_schrijfpaden_zijn_lokaal(self):
        view = self.bouw()
        self.assertEqual([], schendingen(view, self.nas_wortel))
        for rel in ("Game_Data", "Game_Data/Plugins", "Game_Data/StreamingAssets", "BepInEx"):
            p = os.path.join(view, rel)
            self.assertTrue(os.path.isdir(p) and not os.path.islink(p), "%s moet een echte map zijn" % rel)
        # de inhoud van Plugins is gekopieerd (echte bestanden), ook de submap
        for rel in ("matrix64.dll", "sxCoreSettings.txt", "sub/extra.ini"):
            p = os.path.join(view, "Game_Data", "Plugins", rel)
            self.assertTrue(os.path.isfile(p) and not os.path.islink(p), "%s moet een echt bestand zijn" % rel)
        self.nas_ongewijzigd()

    def test_grote_alleen_lezen_delen_blijven_symlink(self):
        view = self.bouw()
        for rel in ("UnityPlayer.dll", "groot.json", "Game.exe", "Game_Data/Managed", "Game_Data/Mono",
                    "Game_Data/level0", "Game_Data/resources.assets"):
            p = os.path.join(view, rel)
            self.assertTrue(os.path.islink(p), "%s hoort een symlink te blijven (ruimte)" % rel)

    def test_configuratiebestanden_zijn_lokaal(self):
        view = self.bouw()
        for rel in ("ControllerBaseConfig.json", "Game.run", "ForceReboot", "Game_Data/boot.config", "Game_Data/app.info"):
            p = os.path.join(view, rel)
            self.assertTrue(os.path.isfile(p) and not os.path.islink(p), "%s moet een lokale kopie zijn" % rel)

    def test_een_dropin_komt_niet_op_de_nas(self):
        view = self.bouw()
        for rel in ("Game_Data/Plugins/matrix64.dll", "Game_Data/Plugins/nieuw.dll", "Game_Data/Plugins/matrix/matrix.ini",
                    "Game_Data/StreamingAssets/cfg.json", "BepInEx/config/x.cfg", "ControllerBaseConfig.json"):
            p = os.path.join(view, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w") as f:
                f.write("drop-in")
        self.assertEqual([], schendingen(view, self.nas_wortel))
        self.nas_ongewijzigd()
        self.assertEqual("orig", leesbestand(os.path.join(self.nas, "Game_Data/Plugins/matrix64.dll")))

    def test_oude_view_wordt_gerepareerd(self):
        """Een view uit de oude versie (Plugins en BepInEx als symlink naar de NAS) wordt bij een nieuwe aanroep lokaal."""
        view = os.path.join(self.m.VIEWS, "G")
        os.makedirs(os.path.join(view, "Game_Data"))
        os.symlink(os.path.join(self.nas, "Game_Data", "Plugins"), os.path.join(view, "Game_Data", "Plugins"))
        os.symlink(os.path.join(self.nas, "Game_Data", "StreamingAssets"), os.path.join(view, "Game_Data", "StreamingAssets"))
        os.symlink(os.path.join(self.nas, "BepInEx"), os.path.join(view, "BepInEx"))
        self.assertNotEqual([], schendingen(view, self.nas_wortel), "de detector moet de oude toestand vangen")
        self.bouw()
        self.assertEqual([], schendingen(view, self.nas_wortel))
        self.nas_ongewijzigd()

    def test_lokale_dropin_blijft_bij_nieuwe_aanroep(self):
        view = self.bouw()
        eigen = os.path.join(view, "Game_Data", "Plugins", "matrix64.dll")
        with open(eigen, "w") as f:
            f.write("onze matrix")
        self.bouw()
        self.assertEqual("onze matrix", leesbestand(eigen))
        self.assertEqual([], schendingen(view, self.nas_wortel))

    def test_oude_symlink_in_plugins_wordt_vervangen(self):
        """De drop-in-materialisatie (launch-direct) laat symlinks naar NAS-bestanden in een echte Plugins-map achter."""
        view = self.bouw()
        p = os.path.join(view, "Game_Data", "Plugins", "matrix64.dll")
        os.unlink(p)
        os.symlink(os.path.join(self.nas, "Game_Data", "Plugins", "matrix64.dll"), p)
        self.assertNotEqual([], schendingen(view, self.nas_wortel))
        self.bouw()
        self.assertEqual([], schendingen(view, self.nas_wortel))

    def test_eigen_bestanden_van_de_game_op_de_nas_stoppen_de_setup_niet(self):
        """De spelmap staat op de NAS (dat is de bedoeling), en eigen bestanden van de game mogen daar als symlink naar wijzen:
        StreamingAssets, ControllerBaseConfig.json, boot.config. Dat is geen overtreding en pruef_schrijfpaden() meldt niets."""
        view = self.bouw()
        for rel in ("Game_Data/StreamingAssets", "ControllerBaseConfig.json", "Game_Data/boot.config"):
            p = os.path.join(view, rel)
            if os.path.isdir(p) and not os.path.islink(p):
                shutil.rmtree(p)
            else:
                os.unlink(p)
            os.symlink(os.path.join(self.nas, rel), p)
        self.assertEqual([], schendingen(view, self.nas_wortel))
        if hasattr(self.m, "pruef_schrijfpaden"):
            self.assertEqual([], self.m.pruef_schrijfpaden(view, self.nas_wortel))
        self.m.build_view("G", self.g, "v", "ravengun", {}, False, False)   # een nieuwe aanroep met die symlinks stopt niet
        self.nas_ongewijzigd()

    def test_pruef_schrijfpaden_meldt_onze_artefacten(self):
        if not hasattr(self.m, "pruef_schrijfpaden"):
            self.skipTest("oude versie zonder pruef_schrijfpaden")
        view = self.bouw()
        shutil.rmtree(os.path.join(view, "Game_Data", "Plugins"))
        os.symlink(os.path.join(self.nas, "Game_Data", "Plugins"), os.path.join(view, "Game_Data", "Plugins"))
        self.assertTrue(self.m.pruef_schrijfpaden(view, self.nas_wortel))

    def test_view_op_de_nas_wordt_geweigerd(self):
        self.m.VIEWS = os.path.join(self.nas, "views")  # <VIEWS>/G ligt dan in de gamemap op de NAS
        with self.assertRaises(SystemExit):
            self.m.link_game("G", self.g)
        self.nas_ongewijzigd()


class TestTpView(Basis):
    """scripts/tp-add-game.py --view (de TeknoParrot-route): link_tree zet BepInEx/ (ons artefact) lokaal; de eigen bestanden van
    de game blijven symlinks naar de NAS (ontwerp: de game woont daar)."""

    def setUp(self):
        super().setUp()
        self.t = laad("tp_add_game_onder_test", os.path.join(SCRIPTS, "tp-add-game.py"))

    def test_link_tree_zet_bepinex_lokaal(self):
        view = os.path.join(self.tmp, "views", "G")
        self.t.link_tree(self.nas, view, [])
        self.assertEqual([], schendingen(view, self.nas_wortel, gamedata=False))
        self.assertTrue(os.path.isdir(os.path.join(view, "BepInEx")) and not os.path.islink(os.path.join(view, "BepInEx")))
        self.assertTrue(os.path.isfile(os.path.join(view, "BepInEx", "LogOutput.log")))
        self.nas_ongewijzigd()

    def test_eigen_bestanden_van_de_game_blijven_symlinks_naar_de_nas(self):
        """Besluit van de eigenaar (2026-10-12): de game woont op de NAS en schrijft daar zijn eigen instellingen; niet lokaal kopieren."""
        view = os.path.join(self.tmp, "views", "G")
        self.t.link_tree(self.nas, view, [])
        for naam in ("ControllerBaseConfig.json", "teknoparrot.ini", "groot.json", "Game.run", "ForceReboot", "UnityPlayer.dll", "Game_Data"):
            self.assertTrue(os.path.islink(os.path.join(view, naam)), "%s hoort een symlink naar de NAS te blijven" % naam)
        # en dat is geen overtreding: de controle over onze artefacten blijft leeg
        self.assertEqual([], schendingen(view, self.nas_wortel, gamedata=False))

    def test_link_tree_repareert_een_oude_bepinex_symlink(self):
        view = os.path.join(self.tmp, "views", "G")
        os.makedirs(view)
        os.symlink(os.path.join(self.nas, "BepInEx"), os.path.join(view, "BepInEx"))
        self.assertNotEqual([], schendingen(view, self.nas_wortel, gamedata=False))
        self.t.link_tree(self.nas, view, [])
        self.assertEqual([], schendingen(view, self.nas_wortel, gamedata=False))
        self.nas_ongewijzigd()

    def test_link_tree_laat_een_gematerialiseerde_map_staan(self):
        """Een echte map in de view (bijv. Game_Data door dropin_materialiseer) blijft bij een nieuwe aanroep staan."""
        view = os.path.join(self.tmp, "views", "G")
        os.makedirs(os.path.join(view, "Game_Data", "Plugins"))
        self.t.link_tree(self.nas, view, [])
        self.assertTrue(os.path.isdir(os.path.join(view, "Game_Data")) and not os.path.islink(os.path.join(view, "Game_Data")))


@unittest.skipUnless(os.path.isdir(ECHTE_NAS), "de NAS is niet gekoppeld")
class TestEchteViews(unittest.TestCase):
    """Leest de echte, al klaargezette views (alleen lezen) en eist dat geen schrijfpad op de echte NAS uitkomt."""

    def controleer(self, game):
        view = os.path.join(VIEWS_AAGAMES, game)
        if not os.path.isdir(view):
            self.skipTest("view %s bestaat niet; draai scripts/aagames-setup.py %s" % (view, game))
        self.assertEqual([], schendingen(view, ECHTE_NAS),
                         "schrijfpaden van %s komen op de NAS uit; draai scripts/aagames-setup.py %s opnieuw" % (game, game))
        plugins = os.path.join(view, "Game_Data", "Plugins")
        self.assertTrue(os.path.isdir(plugins) and not os.path.islink(plugins), "Game_Data/Plugins moet een echte map zijn")
        self.assertTrue([e for e in os.listdir(plugins) if os.path.isfile(os.path.join(plugins, e))],
                        "Game_Data/Plugins is leeg")

    def test_rabbids_hollywood(self):
        self.controleer("RabbidsHollywood")

    def test_tomb_raider(self):
        self.controleer("TombRaider")


if __name__ == "__main__":
    unittest.main()

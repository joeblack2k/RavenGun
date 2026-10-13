#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests: komen ONZE artefacten in een view (via een symlink) op de alleen-lezen NAS uit?

Ontwerp (besluit van de eigenaar, 2026-10-12): de games wonen op de NAS en worden daarvandaan gestart. Wat een game of de kern
zelf in zijn eigen spelmap schrijft (Ram/, Log/, eigen *.ini/*.json/*.conf, NVRAM, <exe>.dxvk-cache, het teknoparrot.ini dat de
kern in de werkmap schrijft) is NORMAAL en wordt hier NIET als fout gemeld (klasse a, "ontwerp"). Wat een regel blijft: onze eigen
artefacten (drop-ins, patchbestanden, onze plugin-DLL's met hun config/cache/log = de BepInEx-boom, hulpbestanden) komen nooit
stilzwijgend in een spelmap op de NAS (klasse b). Zie docs/research/nas-schrijfpaden-inventaris.md.

De controle zelf staat in tools/nas-schrijfpaden-scan.py (ontwerp_view, artefacten_view, restanten_op_nas, globale_artefacten);
dit bestand test die controle (laag A) en past hem toe op de echte views (laag B).

LAAG A -- deterministisch, namaak-NAS in een tijdelijke map, raakt niets echts aan. Bewijst:
  * de controle VANGT onze artefacten op de NAS: BepInEx/ (met plugin van ons) als symlink, BepInEx/plugins als symlink, een keten
    van symlinks, de doelmap van een drop-in, Game_Data/Plugins van een aagames-view, de map voor gepatchte binaries;
  * de controle LAAT TOE wat ontwerp is: een werkmap/exe-map/schrijfmap/eigen configuratiebestand van de game op de NAS is
    geen overtreding (expliciete tests), net als de eigen BepInEx van een game uit de TeknoParrot-dump;
  * een schone view geeft niets, en de controle schrijft niets.
    python3 -B tests/test_nas_schrijfpaden_views.py LaagA_Checker

LAAG B -- de echte views onder ~/.local/share/ravengun/runtime/views en views-aagames, alleen lezen
  (`launch-direct.py --plan --json` start niets). FAALT alleen als een plek van ONZE artefacten op de echte NAS uitkomt zonder
  bescherming (status "open") en noemt dan elke view met plek, doel en rol. Restanten die al op de NAS staan (status "restant") en
  drop-indoelen die launch-direct bij het plaatsen lokaal maakt ("gedekt") worden alleen getoond. Slaat zichzelf over als de
  NAS niet gekoppeld is.
    python3 -B tests/test_nas_schrijfpaden_views.py LaagB_EchteViews
Alles: python3 -B tests/test_nas_schrijfpaden_views.py
"""
import importlib.util
import os
import shutil
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def laad_scan():
    pad = os.path.join(REPO, "tools", "nas-schrijfpaden-scan.py")
    spec = importlib.util.spec_from_file_location("nas_schrijfpaden_scan", pad)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["nas_schrijfpaden_scan"] = mod
    spec.loader.exec_module(mod)
    return mod


scan = laad_scan()
OUD = time.mktime((1996, 12, 24, 23, 32, 0, 0, 0, -1))   # zo staan de bestanden van de dumps op de NAS


def schrijf(pad, tekst="x", mtime=None):
    os.makedirs(os.path.dirname(pad), exist_ok=True)
    with open(pad, "w") as f:
        f.write(tekst)
    if mtime is not None:
        os.utime(pad, (mtime, mtime))


class LaagA_Checker(unittest.TestCase):
    """Laag A: de controle op een namaak-NAS."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-nasview-")
        self.nas = os.path.join(self.tmp, "nas")
        g = os.path.join(self.nas, "Teknoparrot", "Game ")
        # NAS-kant: exe/x64 met de exe, Log, Ram, de BepInEx van de dump, Game_Data/Plugins, een configuratiebestand
        schrijf(os.path.join(g, "exe", "x64", "Game.exe"), "MZ", OUD)
        schrijf(os.path.join(g, "exe", "x64", "Game.dxvk-cache"), "oud", OUD)
        schrijf(os.path.join(g, "Log", "play_log.csv"), "oud", OUD)
        schrijf(os.path.join(g, "Ram", "SRAM_0x1.bin"), "oud", OUD)
        schrijf(os.path.join(g, "BepInEx", "config", "BepInEx.cfg"), "oud", OUD)
        schrijf(os.path.join(g, "BepInEx", "plugins", "GamePlugin.dll"), "oud", OUD)
        schrijf(os.path.join(g, "Game_Data", "Plugins", "matrix64.dll"), "oud", OUD)
        schrijf(os.path.join(g, "Game_Data", "level0"), "L" * 100, OUD)
        schrijf(os.path.join(g, "ControllerBaseConfig.json"), "{}", OUD)
        schrijf(os.path.join(g, "Data", "model.bin"), "M" * 100, OUD)
        self.g = g
        self.view = os.path.join(self.tmp, "view")
        os.makedirs(self.view)
        self.voor = self.momentopname()

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def momentopname(self):
        uit = {}
        for d, dirs, files in os.walk(self.nas):
            for x in dirs + files:
                p = os.path.join(d, x)
                uit[p] = (os.lstat(p).st_mtime_ns, os.lstat(p).st_size)
        return uit

    def link(self, rel, doel_rel):
        pad = os.path.join(self.view, rel)
        os.makedirs(os.path.dirname(pad), exist_ok=True)
        os.symlink(os.path.join(self.g, doel_rel), pad)

    def onze_plugin_op_nas(self):
        """De BepInEx op de NAS bevat een plugin van ons (zoals Rabbids Hollywood op 2026-10-11)."""
        schrijf(os.path.join(self.g, "BepInEx", "plugins", "RavenGun.Game.dll"), "plugin", OUD)

    def lokaal_exe(self):
        schrijf(os.path.join(self.view, "exe", "x64", "Game.exe"), "MZ", OUD)
        os.makedirs(os.path.join(self.view, "Log"))
        os.makedirs(os.path.join(self.view, "Ram"))
        self.link("Data", "Data")

    def artefacten(self, **kw):
        return scan.artefacten_view(self.view, nas=self.nas, **kw)

    def ontwerp(self, **kw):
        kw.setdefault("spel", os.path.join(self.view, "exe", "x64", "Game.exe"))
        kw.setdefault("workdir", os.path.join(self.view, "exe", "x64"))
        return scan.ontwerp_view(self.view, nas=self.nas, **kw)

    def ernstig(self, bevindingen):
        return [x for x in bevindingen if scan.ernstig(x)]

    # ---- klasse (b): onze artefacten worden gevangen ------------------------------------------------------------------

    def test_schone_view_geeft_geen_bevindingen(self):
        self.lokaal_exe()
        os.makedirs(os.path.join(self.view, "BepInEx", "plugins"))
        self.assertEqual([], self.artefacten())
        self.assertEqual([], self.artefacten(aagames=True, dropin_doelen=[os.path.join(self.view, "Game_Data", "Plugins", "m.dll")]))

    def test_bepinex_met_onze_plugin_als_symlink_wordt_gevangen(self):
        self.onze_plugin_op_nas()
        self.link("BepInEx", "BepInEx")
        b = self.ernstig(self.artefacten())
        self.assertEqual(["BepInEx"], [x["map"] for x in b])
        self.assertEqual(["bepinex"], b[0]["rollen"])

    def test_bepinex_plugins_symlink_met_onze_plugin_wordt_gevangen(self):
        self.onze_plugin_op_nas()
        os.makedirs(os.path.join(self.view, "BepInEx", "config"))
        self.link("BepInEx/plugins", "BepInEx/plugins")
        b = self.ernstig(self.artefacten())
        self.assertEqual([os.path.join("BepInEx", "plugins")], [x["map"] for x in b])

    def test_bepinex_van_een_aagames_view_is_altijd_van_ons(self):
        """In een aagames-view zetten wij BepInEx neer: een symlink naar de NAS is dan altijd een overtreding."""
        self.link("BepInEx", "BepInEx")   # de NAS-BepInEx bevat hier alleen de plugin van de game
        b = self.ernstig(self.artefacten(aagames=True))
        self.assertEqual(["BepInEx"], [x["map"] for x in b])

    def test_keten_van_symlinks_wordt_gevangen(self):
        """view/BepInEx -> view/tussen -> NAS/BepInEx: realpath volgt de hele keten."""
        self.onze_plugin_op_nas()
        os.symlink(os.path.join(self.g, "BepInEx"), os.path.join(self.view, "tussen"))
        os.symlink(os.path.join(self.view, "tussen"), os.path.join(self.view, "BepInEx"))
        b = self.ernstig(self.artefacten())
        self.assertEqual(["BepInEx"], [x["map"] for x in b])

    def test_aagames_plugins_symlink_wordt_gevangen(self):
        os.makedirs(os.path.join(self.view, "Game_Data"))
        self.link("Game_Data/Plugins", "Game_Data/Plugins")
        b = self.ernstig(self.artefacten(aagames=True))
        self.assertEqual([os.path.join("Game_Data", "Plugins")], [x["map"] for x in b])
        self.assertIn("plugins", b[0]["rollen"])

    def test_aagames_game_data_symlink_wordt_gevangen(self):
        self.link("Game_Data", "Game_Data")
        b = self.ernstig(self.artefacten(aagames=True))
        self.assertEqual(["Game_Data"], [x["map"] for x in b])

    def test_dropin_doelmap_via_symlink_is_open_zonder_bescherming(self):
        """De doelmap van een drop-in (To = Game_Data/Plugins) is een symlink naar de NAS."""
        os.makedirs(os.path.join(self.view, "Game_Data"))
        self.link("Game_Data/Plugins", "Game_Data/Plugins")
        dest = os.path.join(self.view, "Game_Data", "Plugins", "matrix64.dll")
        b = self.ernstig(self.artefacten(dropin_doelen=[dest], gedekt=False))
        self.assertEqual([os.path.join("Game_Data", "Plugins")], [x["map"] for x in b])
        self.assertIn("dropin-doel", b[0]["rollen"])

    def test_dropin_doelmap_via_symlink_is_gedekt_door_launch_direct(self):
        """Op de TeknoParrot-route maakt dropin_materialiseer die map lokaal: wel getoond (gedekt), geen overtreding."""
        os.makedirs(os.path.join(self.view, "Game_Data"))
        self.link("Game_Data/Plugins", "Game_Data/Plugins")
        dest = os.path.join(self.view, "Game_Data", "Plugins", "matrix", "matrix.ini")
        b = self.artefacten(dropin_doelen=[dest], gedekt=True)
        self.assertEqual(1, len(b))
        self.assertEqual("gedekt", b[0]["status"])
        self.assertEqual([], self.ernstig(b))

    def test_dropin_doelmap_die_nog_niet_bestaat_telt_de_eerste_bestaande_map(self):
        self.link("Game_Data", "Game_Data")
        dest = os.path.join(self.view, "Game_Data", "Nieuw", "x.dll")
        b = self.ernstig(self.artefacten(dropin_doelen=[dest]))
        self.assertEqual(["Game_Data"], [x["map"] for x in b])

    def test_lokale_dropin_doelmap_is_goed(self):
        os.makedirs(os.path.join(self.view, "Game_Data", "Plugins"))
        dest = os.path.join(self.view, "Game_Data", "Plugins", "matrix64.dll")
        self.assertEqual([], self.artefacten(dropin_doelen=[dest]))

    def test_map_voor_gepatchte_binaries_op_de_nas_wordt_gevangen(self):
        """Patchbestanden horen in ~/.local/share/ravengun/patched, nooit naast het origineel op de NAS."""
        self.assertEqual([], scan.globale_artefacten(self.nas, patched_root=os.path.join(self.tmp, "lokaal", "patched")))
        op_nas = os.path.join(self.g, "patched")
        b = scan.globale_artefacten(self.nas, patched_root=op_nas)
        self.assertEqual(["gepatcht"], b[0]["rollen"])
        self.assertTrue(scan.ernstig(b[0]))
        # ook via een symlink
        os.symlink(self.g, os.path.join(self.tmp, "viaLink"))
        self.assertTrue(scan.globale_artefacten(self.nas, patched_root=os.path.join(self.tmp, "viaLink", "patched")))

    def test_de_echte_map_voor_gepatchte_binaries_is_lokaal(self):
        self.assertEqual([], scan.globale_artefacten())

    def test_restanten_van_ons_op_de_nas_worden_getoond_maar_zijn_geen_overtreding(self):
        self.onze_plugin_op_nas()
        schrijf(os.path.join(self.g, "BepInEx", "LogOutput.log"), "log")           # mtime nu: BepInEx schreef dit na onze run
        schrijf(os.path.join(self.g, "exe", "x64", "dropin.dll"), "onze drop-in")
        bron = os.path.join(self.tmp, "dropin.dll")
        schrijf(bron, "onze drop-in")
        r = scan.restanten_op_nas(self.g, [(bron, os.path.join(self.g, "exe", "x64", "dropin.dll"))])
        kaart = {x["map"] for x in r}
        self.assertIn(os.path.join("BepInEx", "plugins", "RavenGun.Game.dll"), kaart)
        self.assertIn(os.path.join("BepInEx", "LogOutput.log"), kaart)
        self.assertIn(os.path.join("exe", "x64", "dropin.dll"), kaart, "een identiek drop-inbestand op de NAS is een restant")
        self.assertNotIn(os.path.join("BepInEx", "plugins", "GamePlugin.dll"), kaart, "de plugin van de game is niet van ons")
        self.assertEqual([], self.ernstig(r), "een restant is informatief: opruimen is aan de eigenaar")
        self.assertTrue(all(x["status"] == "restant" for x in r))

    # ---- klasse (a): ontwerp is TOEGESTAAN -------------------------------------------------------------------------------

    def test_werkmap_en_exe_map_van_de_game_op_de_nas_is_toegestaan(self):
        """Het geval van LuigisMansion: views/<Game>/exe -> NAS/.../exe, werkmap = die map. Ontwerp, geen overtreding."""
        self.link("exe", "exe")
        o = self.ontwerp()
        self.assertTrue(o, "wel zichtbaar in de inventaris")
        x = {y["map"]: y for y in o}[os.path.join("exe", "x64")]
        self.assertEqual("ontwerp", x["klasse"])
        self.assertIn("bekend en geaccepteerd", x["status"])
        self.assertIn("exe-map", x["rollen"])
        self.assertIn("werkmap-game", x["rollen"])
        self.assertEqual("exe", x["via"])
        self.assertEqual([], self.ernstig(o), "de werkmap van de game op de NAS is geen overtreding")
        self.assertEqual([], self.artefacten(), "en er komt geen artefact van ons op de NAS")

    def test_eigen_schrijfmappen_van_de_game_op_de_nas_zijn_toegestaan(self):
        """Log/ en Ram/ als symlink naar de NAS: de game schrijft daar zijn logs en NVRAM. Ontwerp."""
        self.lokaal_exe()
        os.rmdir(os.path.join(self.view, "Log"))
        os.rmdir(os.path.join(self.view, "Ram"))
        self.link("Log", "Log")
        self.link("Ram", "Ram")
        o = self.ontwerp()
        self.assertEqual(["Log", "Ram"], sorted(x["map"] for x in o))
        self.assertEqual([], self.ernstig(o))

    def test_eigen_configuratiebestand_van_de_game_op_de_nas_is_toegestaan(self):
        """ControllerBaseConfig.json als symlink waar de game recent in schreef: de game woont op de NAS. Ontwerp."""
        self.lokaal_exe()
        os.utime(os.path.join(self.g, "ControllerBaseConfig.json"), None)
        self.link("exe/x64/ControllerBaseConfig.json", "ControllerBaseConfig.json")
        o = self.ontwerp()
        self.assertEqual(["bestand"], [x["soort"] for x in o])
        self.assertEqual([], self.ernstig(o))
        self.assertEqual(["ontwerp"], [x["klasse"] for x in o])

    def test_teknoparrot_ini_van_de_kern_in_de_werkmap_is_bekend_en_geaccepteerd(self):
        self.lokaal_exe()
        schrijf(os.path.join(self.g, "exe", "x64", "teknoparrot.ini"), "[Error]\nUnsupported Executable=x")
        self.link("exe/x64/teknoparrot.ini", "exe/x64/teknoparrot.ini")
        o = self.ontwerp()
        self.assertEqual(["teknoparrot.ini"], [os.path.basename(x["map"]) for x in o])
        self.assertIn("bekend en geaccepteerd", o[0]["status"])
        self.assertEqual([], self.ernstig(o))

    def test_dxvk_cache_naast_de_exe_op_de_nas_is_toegestaan(self):
        self.lokaal_exe()
        self.link("exe/x64/Game.dxvk-cache", "exe/x64/Game.dxvk-cache")
        o = self.ontwerp()
        self.assertEqual([], self.ernstig(o))

    def test_eigen_bepinex_van_de_game_op_de_nas_is_ontwerp(self):
        """BepInEx uit de TeknoParrot-dump (zonder plugin van ons) met een symlink naar de NAS: de game zijn eigen zaak."""
        os.makedirs(os.path.join(self.view, "BepInEx"))
        self.link("BepInEx/plugins", "BepInEx/plugins")
        self.assertEqual([], self.artefacten(), "geen artefact van ons: geen klasse-b-bevinding")
        o = scan.ontwerp_view(self.view, nas=self.nas)
        self.assertEqual([os.path.join("BepInEx", "plugins")], [x["map"] for x in o])
        self.assertIn("bepinex-van-de-game", o[0]["rollen"])
        self.assertEqual([], self.ernstig(o))

    def test_aagames_werkmap_is_de_view_en_de_nas_als_bron_is_toegestaan(self):
        """De spelmap op de NAS is de bron van de symlinks van een aagames-view: dat is de bedoeling."""
        schrijf(os.path.join(self.view, "Game.exe"), "MZ", OUD)
        os.makedirs(os.path.join(self.view, "BepInEx"))
        os.makedirs(os.path.join(self.view, "Game_Data", "Plugins"))
        self.link("Game_Data/Managed", "exe")
        self.link("Data", "Data")
        self.assertEqual([], self.ernstig(self.artefacten(aagames=True)))
        self.assertEqual([], self.ernstig(scan.ontwerp_view(self.view, nas=self.nas, spel=os.path.join(self.view, "Game.exe"),
                                                           cwd=self.view, aagames=True)))

    def test_de_controle_schrijft_niets(self):
        self.link("exe", "exe")
        self.link("BepInEx", "BepInEx")
        self.ontwerp()
        self.artefacten(aagames=True, dropin_doelen=[os.path.join(self.view, "exe", "x64", "d.dll")])
        scan.restanten_op_nas(self.g)
        self.assertEqual(self.voor, self.momentopname(), "de (namaak-)NAS is gewijzigd")


class LaagB_EchteViews(unittest.TestCase):
    """Laag B: de echte views. Faalt alleen als een plek van ONZE artefacten op de echte NAS uitkomt zonder bescherming."""

    @classmethod
    def setUpClass(cls):
        if not os.path.isdir(scan.NAS):
            raise unittest.SkipTest("de NAS is niet gekoppeld: %s" % scan.NAS)
        cls.res = scan.scan_alles(plan=True)

    def test_geen_artefact_van_ons_landt_op_de_nas(self):
        slecht = {}
        for v in self.res:
            fouten = [b for b in v["bevindingen"] if scan.ernstig(b)]
            if fouten:
                slecht[v["view"] + " (" + v["soort"] + ")"] = fouten
        if slecht:
            regels = ["%d van %d views hebben een plek waar een artefact van ons op de alleen-lezen NAS zou landen:" % (len(slecht), len(self.res))]
            for naam, fouten in sorted(slecht.items()):
                regels.append("  VIEW %s" % naam)
                for b in fouten:
                    regels.append("    - %s %s -> %s  [%s; via %s]" % (b["soort"], b["map"], b["wijst_naar"].replace(scan.NAS, "NAS"),
                                                                    ", ".join(b["rollen"]), b["via"]))
            regels.append("Volledige lijst: python3 -I tools/nas-schrijfpaden-scan.py ; uitleg: docs/research/nas-schrijfpaden-inventaris.md")
            self.fail("\n" + "\n".join(regels))

    def test_de_aagames_views_zijn_schoon(self):
        """De aagames-views (play-unity.sh) hebben BepInEx en Game_Data/Plugins lokaal en moeten dat blijven."""
        slecht = [(v["view"], b["map"]) for v in self.res if v["soort"] == "aagames" for b in v["bevindingen"] if scan.ernstig(b)]
        self.assertEqual([], slecht)
        for v in self.res:
            if v["soort"] == "aagames":
                for rel in ("BepInEx", os.path.join("Game_Data", "Plugins")):
                    p = os.path.join(scan.VIEWS_AAGAMES, v["view"], rel)
                    self.assertTrue(os.path.isdir(p) and not os.path.islink(p), "%s moet een echte map zijn" % p)

    def test_ontwerp_van_de_game_telt_niet_mee(self):
        """Dat een game zijn werkmap/exe-map/Ram/Log op de NAS heeft (klasse a) is geen fout: de uitkomst hangt er niet van af."""
        ontwerp = [(v["view"], b["map"]) for v in self.res for b in v["bevindingen"] if b["klasse"] == "ontwerp"]
        self.assertTrue(all(not scan.ernstig(b) for v in self.res for b in v["bevindingen"] if b["klasse"] == "ontwerp"))
        if ontwerp:
            sys.stderr.write("\n[laag B] ontwerp (a), informatief: %d plekken in %d views (bekend en geaccepteerd)\n"
                             % (len(ontwerp), len({v for v, _ in ontwerp})))

    def test_restanten_en_gedekte_plekken_worden_alleen_getoond(self):
        info = [(v["view"], b["status"], b["map"]) for v in self.res for b in v["bevindingen"]
                if b["klasse"] == "artefact" and b["status"] in ("restant", "gedekt")]
        if info:
            sys.stderr.write("\n[laag B] klasse (b), informatief (geen overtreding): %d regels, o.a. %s\n" % (len(info), info[:3]))
        self.assertTrue(all(not scan.ernstig(b) for v in self.res for b in v["bevindingen"] if b["status"] in ("restant", "gedekt")))


if __name__ == "__main__":
    unittest.main()

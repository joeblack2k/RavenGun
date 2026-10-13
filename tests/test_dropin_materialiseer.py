#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor het materialiseren van een view-map bij een drop-in.

De views van de games zijn symlink-boerderijen naar de NAS: `views/<Game>/Game_Data` is bijvoorbeeld
een symlink. Een drop-in met `To = Game_Data/Plugins` wijst dan via die symlink naar de NAS. In
plaats van weigeren maakt `dropin_materialiseer()` die map lokaal -- dat is precies wat een view is.
Zie `docs/geen-dongle-in-de-loader.md`.

Deze tests bouwen hun eigen namaak-view en namaak-NAS, zodat ze niets aanraken.
"""
import importlib.util
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def laad_launcher():
    """scripts/launch-direct.py heeft een streepje in de naam, dus met importlib."""
    pad = os.path.join(REPO, "scripts", "launch-direct.py")
    spec = importlib.util.spec_from_file_location("launch_direct", pad)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["launch_direct"] = mod
    spec.loader.exec_module(mod)
    return mod


ld = laad_launcher()


class TestMaterialiseer(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-dropin-")
        # de namaak-NAS: een map met een submap en een bestand
        self.nas = os.path.join(self.tmp, "nas")
        os.makedirs(os.path.join(self.nas, "Game_Data", "Plugins"))
        with open(os.path.join(self.nas, "Game_Data", "Plugins", "origineel.dll"), "w") as f:
            f.write("origineel")
        with open(os.path.join(self.nas, "leesmij.txt"), "w") as f:
            f.write("nas")
        # de namaak-view: een echte map met daarin een symlink naar de NAS
        self.view = os.path.join(self.tmp, "view")
        os.makedirs(self.view)
        os.symlink(os.path.join(self.nas, "Game_Data"), os.path.join(self.view, "Game_Data"))
        os.symlink(os.path.join(self.nas, "leesmij.txt"), os.path.join(self.view, "leesmij.txt"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_symlink_map_wordt_echt(self):
        dest = os.path.join(self.view, "Game_Data", "Plugins", "nieuw.dll")
        self.assertTrue(os.path.islink(os.path.join(self.view, "Game_Data")))
        ok, msg = ld.dropin_materialiseer(self.view, dest)
        self.assertTrue(ok, msg)
        self.assertFalse(os.path.islink(os.path.join(self.view, "Game_Data")),
                         "Game_Data moet nu een echte map zijn")
        self.assertTrue(os.path.isdir(os.path.join(self.view, "Game_Data")))

    def test_de_inhoud_blijft_bereikbaar_via_symlinks(self):
        dest = os.path.join(self.view, "Game_Data", "Plugins", "nieuw.dll")
        ld.dropin_materialiseer(self.view, dest)
        oud = os.path.join(self.view, "Game_Data", "Plugins", "origineel.dll")
        self.assertTrue(os.path.isfile(oud), "het originele bestand moet bereikbaar blijven")
        with open(oud) as f:
            self.assertEqual(f.read(), "origineel")
        self.assertTrue(os.path.islink(oud), "en het moet een symlink naar de NAS zijn")

    def test_het_bestand_wordt_echt_geschreven(self):
        dest = os.path.join(self.view, "Game_Data", "Plugins", "nieuw.dll")
        ok, _ = ld.dropin_materialiseer(self.view, dest)
        self.assertTrue(ok)
        with open(dest, "w") as f:
            f.write("dropin")
        self.assertFalse(os.path.islink(dest))
        with open(dest) as f:
            self.assertEqual(f.read(), "dropin")

    def test_de_nas_blijft_ongemoeid(self):
        dest = os.path.join(self.view, "Game_Data", "Plugins", "nieuw.dll")
        ld.dropin_materialiseer(self.view, dest)
        with open(dest, "w") as f:
            f.write("dropin")
        nas_bestand = os.path.join(self.nas, "Game_Data", "Plugins", "origineel.dll")
        with open(nas_bestand) as f:
            self.assertEqual(f.read(), "origineel", "de NAS mag niet veranderd zijn")
        self.assertFalse(os.path.exists(os.path.join(self.nas, "Game_Data", "Plugins", "nieuw.dll")),
                         "het drop-in-bestand mag niet op de NAS belanden")

    def test_doel_buiten_de_view_wordt_geweigerd(self):
        ok, msg = ld.dropin_materialiseer(self.view, os.path.join(self.nas, "Game_Data", "x.dll"))
        self.assertFalse(ok)
        self.assertIn("buiten de view", msg)

    def test_bestand_dat_zelf_een_symlink_is(self):
        dest = os.path.join(self.view, "leesmij.txt")
        self.assertTrue(os.path.islink(dest))
        ok, _ = ld.dropin_materialiseer(self.view, dest)
        self.assertTrue(ok)
        self.assertFalse(os.path.islink(dest), "de symlink moet weg zijn zodat er echt geschreven wordt")
        # en de NAS-versie is nog intact
        with open(os.path.join(self.nas, "leesmij.txt")) as f:
            self.assertEqual(f.read(), "nas")

    def test_map_die_nog_niet_bestaat_wordt_gemaakt(self):
        dest = os.path.join(self.view, "NieuweMap", "diep", "x.dll")
        ok, msg = ld.dropin_materialiseer(self.view, dest)
        self.assertTrue(ok, msg)
        self.assertTrue(os.path.isdir(os.path.join(self.view, "NieuweMap", "diep")))

    def test_tweede_keer_is_geen_fout(self):
        dest = os.path.join(self.view, "Game_Data", "Plugins", "nieuw.dll")
        self.assertTrue(ld.dropin_materialiseer(self.view, dest)[0])
        self.assertTrue(ld.dropin_materialiseer(self.view, dest)[0], "idempotent")

    def test_binnen_helper(self):
        self.assertTrue(ld._binnen(os.path.join(self.view, "a", "b"), self.view))
        self.assertTrue(ld._binnen(self.view, self.view))
        self.assertFalse(ld._binnen(os.path.join(self.tmp, "elders"), self.view))


class TestDropinIni(unittest.TestCase):
    """De dropin.ini van een game wordt gelezen zoals hij op schijf staat.

    Een drop-in wordt door de GEBRUIKER gemaakt (patches/<Game>/dropin/dropin.ini). Wie dat in
    Windows-Kladblok doet, krijgt een UTF-8-BOM voor het bestand. configparser ziet de eerste regel
    dan niet als commentaar en weigert het hele bestand met "File contains no section header",
    waarna de drop-in stil niet wordt uitgerold. Twee meegeleverde drop-ins hadden die BOM al.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-dropinini-")
        self.shipped = os.path.join(self.tmp, "patches")
        self.user = os.path.join(self.tmp, "gebruiker")
        os.makedirs(os.path.join(self.shipped, "ProefGame", "dropin"))
        os.makedirs(self.user)
        self._user_dir, self._shipped_dir = ld.gs.user_dir, ld.gs.shipped_dir
        ld.gs.user_dir = lambda: self.user
        ld.gs.shipped_dir = lambda: self.shipped

    def tearDown(self):
        ld.gs.user_dir, ld.gs.shipped_dir = self._user_dir, self._shipped_dir
        shutil.rmtree(self.tmp, ignore_errors=True)

    def schrijf_ini(self, tekst, bom=False):
        with open(os.path.join(self.shipped, "ProefGame", "dropin", "dropin.ini"), "wb") as f:
            if bom:
                f.write(b"\xef\xbb\xbf")
            f.write(tekst.encode("utf-8"))

    def test_een_ini_met_bom_wordt_gelezen(self):
        self.schrijf_ini("[proef.dll]\nTo = .\nPreload = true\n", bom=True)
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(fouten, [])
        self.assertEqual([s["name"] for s in specs], ["proef.dll"])
        self.assertTrue(specs[0]["preload"])

    def test_een_ini_zonder_bom_blijft_werken(self):
        self.schrijf_ini("[proef.dll]\nTo = .\n")
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(fouten, [])
        self.assertEqual([s["name"] for s in specs], ["proef.dll"])

    def test_een_verdubbeld_pad_wordt_gemeld(self):
        """To + sectienaam: staat het pad ook al in de sectienaam, dan komt het bestand dubbel."""
        self.schrijf_ini("[ravengun-hasp/Halo/dongle.json]\nTo = ravengun-hasp/Halo\n")
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(specs, [])
        self.assertEqual(len(fouten), 1)
        self.assertIn("dubbel in de view", fouten[0])

    def test_to_punt_zet_het_bestand_in_de_submap(self):
        """De goede vorm: het pad in de sectienaam en To = ."""
        self.schrijf_ini("[ravengun-hasp/Halo/dongle.json]\nTo = .\n")
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(fouten, [])
        self.assertEqual(specs[0]["to"], ".")

    def test_audit_false_haalt_het_bestand_uit_ld_audit(self):
        """Een inline-hook die de lader meeneemt hoort niet in LD_AUDIT van het Cygwin-proces."""
        self.schrijf_ini("[libravengun-hasphook.so]\nTo = .\nPreload = true\nAudit = false\n")
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(fouten, [])
        self.assertTrue(specs[0]["preload"])
        self.assertFalse(specs[0]["audit"])

    def test_audit_staat_standaard_aan(self):
        """Zonder Audit-sleutel mag het bestand in LD_AUDIT (dat is de oude werking)."""
        self.schrijf_ini("[librt-glfix.so]\nTo = .\nPreload = true\n")
        specs, _ = ld.dropin_specs("ProefGame")
        self.assertTrue(specs[0]["audit"])

    def test_een_ini_met_alleen_commentaar_is_geen_fout(self):
        """Een lege drop-in (alleen uitleg) levert geen drop-ins en geen fout op."""
        self.schrijf_ini("; alleen commentaar\n", bom=False)
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(specs, [])
        self.assertEqual(fouten, [])

    def test_een_ini_die_echt_geen_sectie_heeft_wordt_gemeld(self):
        """Niet stil overslaan: een sleutel buiten een sectie is een fout die de eigenaar moet zien."""
        self.schrijf_ini("To = .\n", bom=False)
        specs, fouten = ld.dropin_specs("ProefGame")
        self.assertEqual(specs, [])
        self.assertEqual(len(fouten), 1)
        self.assertIn("no section header", fouten[0])


class TestNasControle(unittest.TestCase):
    """Een drop-in gaat nooit naar de NAS, maar een view die ZELF naar de NAS wijst is geen fout.

    De view van een game is een symlink-boerderij: views/TC5/TC5/Binaries/Win64 is een symlink naar
    de NAS. Het gevolgde pad van de gamemap komt dus op de NAS uit terwijl het lexicale pad in de
    view ligt. on_nas(base) zou die drop-in onterecht weigeren; run() materialiseert de map juist.
    Alleen een game die buiten de views staat (rechtstreeks van de NAS) wordt geweigerd.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-nascontrole-")
        self.shipped = os.path.join(self.tmp, "patches")
        self.user = os.path.join(self.tmp, "gebruiker")
        self.views = os.path.join(self.tmp, "views")
        self.nas = os.path.join(self.tmp, "nas")
        os.makedirs(os.path.join(self.shipped, "ProefGame", "dropin"))
        os.makedirs(self.user)
        os.makedirs(os.path.join(self.nas, "Binaries", "Win64"))
        os.makedirs(os.path.join(self.views, "ProefGame"))
        # de view: een echte map met daarin een symlink naar de NAS (precies een view)
        os.symlink(os.path.join(self.nas, "Binaries"),
                   os.path.join(self.views, "ProefGame", "Binaries"))
        with open(os.path.join(self.shipped, "ProefGame", "dropin", "dropin.ini"), "w") as f:
            f.write("[proef.dll]\nTo = .\n")
        with open(os.path.join(self.shipped, "ProefGame", "dropin", "proef.dll"), "w") as f:
            f.write("x")
        self._user_dir, self._shipped_dir = ld.gs.user_dir, ld.gs.shipped_dir
        self._views_dir, self._on_nas = ld.op.views_dir, ld.on_nas
        ld.gs.user_dir = lambda: self.user
        ld.gs.shipped_dir = lambda: self.shipped
        ld.op.views_dir = lambda: self.views
        ld.on_nas = lambda p: True          # net als op de Deck: alles onder de mount is "op de NAS"

    def tearDown(self):
        ld.gs.user_dir, ld.gs.shipped_dir = self._user_dir, self._shipped_dir
        ld.op.views_dir, ld.on_nas = self._views_dir, self._on_nas
        shutil.rmtree(self.tmp, ignore_errors=True)

    def plan(self, game):
        return {"profile": "ProefGame", "game": game, "env": {}}

    def test_een_game_in_zijn_view_komt_aan_ook_als_de_view_naar_de_nas_wijst(self):
        game = os.path.join(self.views, "ProefGame", "Binaries", "Win64", "game.exe")
        uit = ld.dropin_plan(self.plan(game))
        self.assertEqual(uit["errors"], [])
        self.assertEqual([f["name"] for f in uit["files"]], ["proef.dll"])
        self.assertEqual(uit["base"], os.path.dirname(game))

    def test_audit_false_blijft_uit_ld_audit_en_in_rg_dropin_preload(self):
        """In het plan: Audit = false houdt het bestand uit LD_AUDIT maar niet uit de drop-in."""
        with open(os.path.join(self.shipped, "ProefGame", "dropin", "dropin.ini"), "w") as f:
            f.write("[libravengun-hasphook.so]\nTo = .\nPreload = true\nAudit = false\n")
        with open(os.path.join(self.shipped, "ProefGame", "dropin", "libravengun-hasphook.so"), "w") as f:
            f.write("x")
        spel = os.path.join(self.views, "ProefGame", "Binaries", "Win64", "game.exe")
        with open(spel, "w") as f:
            f.write("geen PE")            # dus een ELF onder Wine: de LD_AUDIT-route
        plan = {"profile": "ProefGame", "game": spel, "env": {}, "wine": "/bin/true"}
        uit = ld.dropin_plan(plan)
        self.assertEqual(uit["errors"], [])
        self.assertEqual([f["name"] for f in uit["files"]], ["libravengun-hasphook.so"])
        self.assertNotIn("LD_AUDIT", uit["env"])
        self.assertIn("libravengun-hasphook.so", uit["env"].get("RG_DROPIN_PRELOAD", ""))
        self.assertEqual(len(uit["geen_audit"]), 1)

    def test_rechtstreeks_van_de_nas_wordt_geweigerd(self):
        game = os.path.join(self.nas, "Binaries", "Win64", "game.exe")
        uit = ld.dropin_plan(self.plan(game))
        self.assertEqual(uit["files"], [])
        self.assertEqual(len(uit["errors"]), 1)
        self.assertIn("rechtstreeks van de NAS", uit["errors"][0])


class TestPreloadKanaalEnKeten(unittest.TestCase):
    """Waar een Preload = true-bestand heen gaat, en de view waarin de symlink BOVEN de gamemap zit.

    Gemeten op Luigi's Mansion: de drop-in-DLL kwam nooit in het spelproces (0 van 2 bestanden in
    /proc/<pid>/maps), omdat de launcher het pad alleen in RG_DROPIN_PRELOAD zette en de OpenParrot-lader
    TP_PRELOAD_DLL leest. En views/LuigisMansion/exe was een symlink naar de NAS met de game in exe/x64,
    waardoor een drop-in met To = . niet te plaatsen was en de run met een waarschuwing doorging.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-preload-")
        self.shipped = os.path.join(self.tmp, "patches")
        self.user = os.path.join(self.tmp, "gebruiker")
        self.views = os.path.join(self.tmp, "views")
        self.nas = os.path.join(self.tmp, "nas")
        self.dropin = os.path.join(self.shipped, "ProefGame", "dropin")
        os.makedirs(self.dropin)
        os.makedirs(self.user)
        os.makedirs(os.path.join(self.nas, "exe", "x64"))
        os.makedirs(os.path.join(self.nas, "Ram"))
        with open(os.path.join(self.nas, "exe", "x64", "VACUUM.exe"), "wb") as f:
            f.write(b"MZ" + b"\0" * 62)          # een Windows-programma
        with open(os.path.join(self.nas, "exe", "leesmij.txt"), "w") as f:
            f.write("nas")
        os.makedirs(os.path.join(self.views, "ProefGame"))
        os.symlink(os.path.join(self.nas, "exe"), os.path.join(self.views, "ProefGame", "exe"))   # de symlink boven de gamemap
        os.symlink(os.path.join(self.nas, "Ram"), os.path.join(self.views, "ProefGame", "Ram"))
        self.spel = os.path.join(self.views, "ProefGame", "exe", "x64", "VACUUM.exe")
        self._bewaard = (ld.gs.user_dir, ld.gs.shipped_dir, ld.op.views_dir, ld.on_nas)
        ld.gs.user_dir = lambda: self.user
        ld.gs.shipped_dir = lambda: self.shipped
        ld.op.views_dir = lambda: self.views
        ld.on_nas = lambda p: os.path.realpath(p).startswith(self.nas + os.sep)

    def tearDown(self):
        ld.gs.user_dir, ld.gs.shipped_dir, ld.op.views_dir, ld.on_nas = self._bewaard
        shutil.rmtree(self.tmp, ignore_errors=True)

    def ini(self, tekst, *bestanden):
        with open(os.path.join(self.dropin, "dropin.ini"), "w") as f:
            f.write(tekst)
        for b in bestanden:
            with open(os.path.join(self.dropin, b), "w") as f:
                f.write("x")

    def plan(self, lader="OpenParrotLoader64.exe", wine="/bin/wine", env=None, spel=None):
        spel = spel or self.spel
        argv = [wine, "/tp/OpenParrotx64/" + lader, "Z:\\kern", "Z:\\spel"] if wine else [spel]
        return {"profile": "ProefGame", "game": spel, "env": dict(env or {}), "wine": wine, "argv": argv}

    def nas_lijst(self):
        uit = []
        for wortel, mappen, bestanden in os.walk(self.nas):
            for n in sorted(mappen + bestanden):
                uit.append(os.path.relpath(os.path.join(wortel, n), self.nas))
        return sorted(uit)

    # --- het kanaal ---

    def test_windows_spel_onder_wine_krijgt_tp_preload_dll_als_wine_pad(self):
        self.ini("[rg-pe-hook.dll]\nTo = .\nPreload = true\n", "rg-pe-hook.dll")
        uit = ld.dropin_plan(self.plan())
        self.assertEqual(uit["errors"], [])
        dest = os.path.join(os.path.dirname(self.spel), "rg-pe-hook.dll")
        self.assertEqual(uit["env"]["TP_PRELOAD_DLL"], "Z:" + dest.replace("/", "\\"))
        self.assertNotIn("/", uit["env"]["TP_PRELOAD_DLL"])
        self.assertEqual(uit["env"]["RG_DROPIN_PRELOAD"], dest)          # blijft staan voor wie het leest
        self.assertNotIn("LD_PRELOAD", uit["env"])
        self.assertNotIn("LD_AUDIT", uit["env"])
        self.assertEqual([(k["var"], k["loads"]) for k in uit["preload_env"]], [("TP_PRELOAD_DLL", True)])
        regels = "\n".join(ld.dropin_log_lines({"dropins": uit}))
        self.assertIn("rg-pe-hook.dll -> TP_PRELOAD_DLL=Z:", regels)

    def test_twee_preloads_voor_een_windows_spel_is_een_fout(self):
        self.ini("[a.dll]\nPreload = true\n[b.dll]\nPreload = true\n", "a.dll", "b.dll")
        uit = ld.dropin_plan(self.plan())
        self.assertNotIn("TP_PRELOAD_DLL", uit["env"])
        self.assertEqual(len(uit["errors"]), 1)
        self.assertIn("precies een", uit["errors"][0])

    def test_een_lader_die_de_variabele_niet_leest_is_een_fout(self):
        self.ini("[a.dll]\nPreload = true\n", "a.dll")
        uit = ld.dropin_plan(self.plan(lader="TeknoMacaw64.exe"))
        self.assertNotIn("TP_PRELOAD_DLL", uit["env"])
        self.assertEqual(len(uit["errors"]), 1)
        self.assertIn("geen kanaal", uit["errors"][0])

    def test_een_al_gezette_tp_preload_dll_is_een_fout(self):
        self.ini("[a.dll]\nPreload = true\n", "a.dll")
        uit = ld.dropin_plan(self.plan(env={"TP_PRELOAD_DLL": "Z:\\elders\\RgExitTrace64.dll"}))
        self.assertNotIn("TP_PRELOAD_DLL", uit["env"])
        self.assertIn("staat al op", uit["errors"][0])

    def test_de_native_route_blijft_ld_preload(self):
        spel = os.path.join(self.tmp, "lokaal", "game")
        os.makedirs(os.path.dirname(spel))
        with open(spel, "wb") as f:
            f.write(b"\x7fELF")
        self.ini("[lib.so]\nPreload = true\n", "lib.so")
        uit = ld.dropin_plan(self.plan(wine="", spel=spel))
        self.assertEqual(uit["errors"], [])
        self.assertEqual(uit["env"]["LD_PRELOAD"], os.path.join(os.path.dirname(spel), "lib.so"))
        self.assertNotIn("TP_PRELOAD_DLL", uit["env"])
        self.assertEqual([(k["var"], k["loads"]) for k in uit["preload_env"]], [("LD_PRELOAD", True)])

    def test_een_elf_onder_wine_blijft_ld_audit(self):
        with open(os.path.join(self.nas, "exe", "x64", "game"), "wb") as f:
            f.write(b"\x7fELF")
        self.ini("[lib.so]\nPreload = true\n[hook.so]\nPreload = true\nAudit = false\n", "lib.so", "hook.so")
        spel = os.path.join(self.views, "ProefGame", "exe", "x64", "game")
        uit = ld.dropin_plan(self.plan(lader="BudgieLoader.exe", spel=spel))
        self.assertEqual(uit["errors"], [])
        self.assertEqual(uit["env"]["LD_AUDIT"], os.path.join(os.path.dirname(spel), "lib.so"))
        self.assertNotIn("TP_PRELOAD_DLL", uit["env"])
        self.assertEqual([(k["var"], k["loads"]) for k in uit["preload_env"]],
                         [("LD_AUDIT", True), ("RG_DROPIN_PRELOAD", False)])
        self.assertIn("WORDT NIET GELADEN", "\n".join(ld.dropin_log_lines({"dropins": uit})))

    # --- de keten boven de gamemap ---

    def test_symlink_boven_de_gamemap_staat_in_het_plan_en_wordt_lokaal(self):
        self.ini("[rg-pe-hook.dll]\nTo = .\nPreload = true\n", "rg-pe-hook.dll")
        voor = self.nas_lijst()
        uit = ld.dropin_plan(self.plan())
        self.assertEqual(uit["errors"], [])
        view = os.path.join(self.views, "ProefGame")
        self.assertEqual(uit["root"], view)
        self.assertEqual(uit["materialise"], [os.path.join(view, "exe"), os.path.join(view, "exe", "x64")])
        self.assertEqual(self.nas_lijst(), voor)                         # het plan verandert niets
        self.assertTrue(os.path.islink(os.path.join(view, "exe")))
        dest = uit["files"][0]["dest"]
        # zonder de wortel van de view (het oude gedrag) komt de map buiten de view uit
        ok, waarom = ld.dropin_materialiseer(uit["base"], dest)
        self.assertFalse(ok)
        self.assertIn("buiten de view", waarom)
        ok, waarom = ld.dropin_materialiseer(uit["base"], dest, None, uit["root"])
        self.assertTrue(ok, waarom)
        for d in (os.path.join(view, "exe"), os.path.join(view, "exe", "x64")):
            self.assertTrue(os.path.isdir(d) and not os.path.islink(d), d)
        self.assertTrue(os.path.islink(self.spel))                       # de game zelf blijft een symlink naar de NAS
        with open(self.spel, "rb") as f:
            self.assertEqual(f.read(2), b"MZ")                           # en is nog vindbaar
        self.assertTrue(os.path.islink(os.path.join(view, "exe", "leesmij.txt")))
        self.assertEqual(ld.place_binary(uit["files"][0]["src"], dest)[0], "geschreven")
        self.assertTrue(os.path.isfile(dest) and not os.path.islink(dest))
        self.assertEqual(self.nas_lijst(), voor)                         # niets op de NAS erbij of eraf
        self.assertEqual(ld.dropin_plan(self.plan())["materialise"], []) # de tweede keer valt er niets meer te doen

    def test_een_onmogelijke_bestemming_is_een_fout_in_het_plan(self):
        with open(os.path.join(self.nas, "exe", "x64", "sub"), "w") as f:
            f.write("een bestand, geen map")
        self.ini("[a.dll]\nTo = sub\n", "a.dll")
        plan = self.plan()
        plan["dropins"] = ld.dropin_plan(plan)
        self.assertEqual(plan["dropins"]["files"], [])
        self.assertEqual(len(plan["dropins"]["errors"]), 1)
        self.assertIn("niet te plaatsen", plan["dropins"]["errors"][0])
        self.assertTrue(any(r.startswith("FOUT: ") for r in ld.dropin_log_lines(plan)))
        self.assertEqual(ld.dropin_weiger(plan), ld.EXIT_PATCH)          # --plan, --check en --run geven dit terug
        self.assertEqual(ld.run(plan), ld.EXIT_PATCH)                    # en run() start niets

    def test_een_view_die_zelf_een_symlink_is_wordt_geweigerd(self):
        shutil.rmtree(os.path.join(self.views, "ProefGame"))
        os.symlink(self.nas, os.path.join(self.views, "ProefGame"))
        self.ini("[a.dll]\nTo = .\n", "a.dll")
        voor = self.nas_lijst()
        uit = ld.dropin_plan(self.plan())
        self.assertEqual(uit["files"], [])
        self.assertIn("zelf een symlink", uit["errors"][0])
        ok, _ = ld.dropin_materialiseer(uit["base"], os.path.join(uit["base"], "a.dll"), None, uit["root"])
        self.assertFalse(ok)
        self.assertEqual(self.nas_lijst(), voor)

    def test_zonder_fouten_weigert_de_launcher_niet(self):
        self.ini("[a.dll]\nTo = .\n", "a.dll")
        plan = self.plan()
        plan["dropins"] = ld.dropin_plan(plan)
        self.assertEqual(ld.dropin_weiger(plan), 0)

    # --- de view-maker ---

    def test_de_view_maker_maakt_de_keten_naar_de_exe_lokaal(self):
        spec = importlib.util.spec_from_file_location(
            "tp_add_game_onder_test", os.path.join(os.path.dirname(os.path.abspath(ld.__file__)), "tp-add-game.py"))
        t = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(t)
        view = os.path.join(self.tmp, "views", "Vers")
        voor = self.nas_lijst()
        t.link_tree(self.nas, view, [])
        self.assertTrue(os.path.islink(os.path.join(view, "exe")))       # zo maakte de view-maker het tot nu toe
        gedaan = t.maak_keten_lokaal(self.nas, view, os.path.join("exe", "x64"))
        self.assertEqual(gedaan, [os.path.join(view, "exe"), os.path.join(view, "exe", "x64")])
        for d in gedaan:
            self.assertTrue(os.path.isdir(d) and not os.path.islink(d), d)
        spel = os.path.join(view, "exe", "x64", "VACUUM.exe")
        self.assertTrue(os.path.islink(spel) and os.path.isfile(spel))
        self.assertTrue(os.path.islink(os.path.join(view, "Ram")))       # de rest van de view blijft zoals hij was
        # een drop-in van een vorige keer blijft staan als de view opnieuw gemaakt wordt, en de links worden ververst
        with open(os.path.join(view, "exe", "x64", "rg-pe-hook.dll"), "w") as f:
            f.write("onze drop-in")
        os.unlink(spel)
        t.link_tree(self.nas, view, [])
        t.maak_keten_lokaal(self.nas, view, os.path.join("exe", "x64"))
        self.assertTrue(os.path.islink(spel) and os.path.isfile(spel))
        with open(os.path.join(view, "exe", "x64", "rg-pe-hook.dll")) as f:
            self.assertEqual(f.read(), "onze drop-in")
        self.assertEqual(t.maak_keten_lokaal(self.nas, view, ""), [])    # de exe in de wortel: niets te doen
        self.assertEqual(self.nas_lijst(), voor)


if __name__ == "__main__":
    unittest.main()

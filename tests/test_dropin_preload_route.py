#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""De drop-in-route van begin tot eind, tegen een nep-runtime (geen Wine, geen spel, niets op de echte NAS).

Twee fouten kostten samen drie benchruns, en deze test houdt ze gerepareerd:

1. Een drop-in met `Preload = true` kwam voor een Windows-spel onder Wine nooit in het spelproces. De
   launcher zette het pad alleen in RG_DROPIN_PRELOAD, terwijl de OpenParrot-lader TP_PRELOAD_DLL leest.
2. Een drop-in-bestemming die via een symlink buiten de view viel, werd met een WAARSCHUWING gemeld
   waarna de run gewoon doorging: een run zonder zijn drop-in, en dus een ongeldige meting.

`tests/test_dropin_materialiseer.py` toetst de losse functies. Deze test gaat door de voordeur: de echte
opdrachtregel (`launch-direct.py <profiel> --plan`), en de omgeving die `run()` werkelijk aan het
spelproces meegeeft. Voor dat laatste wordt alleen het starten zelf vervangen (subprocess.Popen, het
gedeelde geheugen, de JVS-pipe); al het andere is de echte code. Er wordt dus niets gestart.

De regel die hier wordt afgedwongen: een run gaat NOOIT door zonder zijn drop-in. Of het bestand staat
als echt bestand lokaal in de view (en er is niets buiten de view geschreven), of `--plan` meldt een
fout en `--check`/`--run` weigeren met EXIT_PATCH voordat er iets gestart is.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launcher_nep as nep  # noqa: E402  (de nep-wereld; laadt de launcher met een tijdelijke thuismap)
from launcher_nep import NAS, REPO, TMP, VIEWS, WINE, boom, cli, dropin, ld, profiel, schrijf  # noqa: E402


class PreloadKomtInHetSpelproces(nep.Route):
    """Punt 1: Preload = true op de Wine/TeknoParrot-route belandt in TP_PRELOAD_DLL van het SPELPROCES."""

    def setUp(self):
        super().setUp()
        self.view_goed()
        profiel(self.GAME, self.spel)
        dropin(self.GAME, "[rg-hook.dll]\nTo = .\nPreload = true\n", "rg-hook.dll")
        self.dest = os.path.join(self.view, "exe", "x64", "rg-hook.dll")
        self.wine_pad = "Z:" + self.dest.replace("/", "\\")

    def test_het_plan_zet_tp_preload_dll_in_wine_formaat(self):
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        dp = plan["dropins"]
        self.assertEqual(dp["errors"], [])
        self.assertEqual(os.path.basename(plan["argv"][plan["argv"].index(plan["wine"]) + 1]), "OpenParrotLoader64.exe")
        # de variabele die de lader werkelijk leest, niet alleen RG_DROPIN_PRELOAD
        self.assertEqual(dp["env"].get("TP_PRELOAD_DLL"), self.wine_pad)
        self.assertEqual([(k["var"], k["value"], k["loads"]) for k in dp["preload_env"]],
                         [("TP_PRELOAD_DLL", self.wine_pad, True)])
        # het padformaat: een Wine-pad (Z:\...), een enkel pad, geen Unix-pad en geen lijst
        waarde = dp["env"]["TP_PRELOAD_DLL"]
        self.assertTrue(waarde.startswith("Z:\\"), waarde)
        self.assertNotIn("/", waarde)
        self.assertNotIn(";", waarde)
        self.assertEqual(waarde.count(":"), 1, "een pad, geen lijst: %s" % waarde)
        self.assertEqual(waarde[2:].replace("\\", "/"), self.dest)
        self.assertEqual([f["dest"] for f in dp["files"]], [self.dest])
        # de Wine-route krijgt het bestand NIET in LD_PRELOAD of LD_AUDIT (Cygwin sterft daaraan)
        for var in ("LD_PRELOAD", "LD_AUDIT"):
            self.assertNotIn(var, dp["env"])

    def test_het_leesbare_plan_noemt_de_variabele(self):
        rc, out, err = cli(self.GAME, "--plan")
        self.assertEqual(rc, 0, err)
        self.assertIn("drop-in: preload: rg-hook.dll -> TP_PRELOAD_DLL=" + self.wine_pad, out)
        self.assertNotIn("WORDT NIET GELADEN", out)
        self.assertNotIn("FOUT", out)

    def test_het_plan_verandert_niets(self):
        voor = (boom(VIEWS), boom(NAS))
        self.plan_json()
        cli(self.GAME, "--plan")
        cli(self.GAME, "--check")
        self.assertEqual((boom(VIEWS), boom(NAS)), voor)

    def test_het_spelproces_krijgt_tp_preload_dll_en_het_bestand_staat_er(self):
        nas_voor = boom(NAS)
        rc, spel, _ = self.start("--env", "RG_TEST_VERWACHT_BESTAND=" + self.dest)
        self.assertEqual(rc, 0, self.uitvoer.getvalue() + self.fouten.getvalue())
        self.assertIsNotNone(spel, "de lader is niet gestart")
        self.assertEqual(os.path.basename(spel.argv[spel.argv.index(WINE) + 1]), "OpenParrotLoader64.exe")
        # de omgeving die werkelijk aan het spelproces wordt meegegeven
        self.assertEqual(spel.env.get("TP_PRELOAD_DLL"), self.wine_pad)
        self.assertNotIn(self.dest, spel.env.get("LD_PRELOAD", ""))
        self.assertNotIn(self.dest, spel.env.get("LD_AUDIT", ""))
        # en het bestand stond er als ECHT bestand in de view op het moment van starten
        self.assertTrue(spel.bestand_stond_er, "rg-hook.dll stond niet in de view toen het spel startte")
        with open(self.dest) as f:
            self.assertEqual(f.read(), "drop-in rg-hook.dll")
        self.assertIn("TP_PRELOAD_DLL=" + self.wine_pad, self.uitvoer.getvalue())   # ook in het logboek
        self.assertEqual(boom(NAS), nas_voor, "er is iets op de NAS veranderd")

    def test_alleen_rg_dropin_preload_is_niet_genoeg(self):
        """De oude fout in een zin: het pad stond alleen in RG_DROPIN_PRELOAD. Dat mag nooit meer 'geladen' heten."""
        rc, plan, _ = self.plan_json()
        self.assertEqual(rc, 0)
        niet_geladen = [k for k in plan["dropins"]["preload_env"] if not k["loads"]]
        self.assertEqual(niet_geladen, [])
        self.assertTrue(all(k["var"] != "RG_DROPIN_PRELOAD" for k in plan["dropins"]["preload_env"]))

    def test_een_preload_zonder_kanaal_weigert_in_plaats_van_door_te_gaan(self):
        """Twee preloads: de lader laadt er een. Geen van beide stil laten vallen -- weigeren."""
        dropin(self.GAME, "[a.dll]\nPreload = true\n[b.dll]\nPreload = true\n", "a.dll", "b.dll")
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, ld.EXIT_PATCH)
        self.assertNotIn("TP_PRELOAD_DLL", plan["dropins"]["env"])
        self.assertIn("Drop-in geweigerd", err)
        self.assertEqual(cli(self.GAME, "--check")[0], ld.EXIT_PATCH)
        rc, _, nieuw = self.start()
        self.assert_niets_gestart(rc, nieuw)


class BuitenDeViewFaaltLuid(nep.Route):
    """Punt 2: een bestemming die buiten de view valt gaat nooit stil door.

    Per geval een van de twee toegestane uitkomsten: (a) de launcher maakt de weg lokaal en het bestand
    staat daarna echt in de view, of (b) --plan meldt een FOUT en --check/--run weigeren. Wat niet meer
    mag: een run die start terwijl het bestand er niet staat, of die op de NAS geschreven heeft."""

    INI = "[rg-hook.dll]\nTo = .\nPreload = true\n"

    def weigert(self, tekst, ini=None):
        """--plan (leesbaar en JSON), --check en --run weigeren alle vier, luid, en er verandert niets."""
        dropin(self.GAME, ini or self.INI, "rg-hook.dll")
        voor = (boom(NAS), boom(os.path.join(TMP, "elders")) if os.path.isdir(os.path.join(TMP, "elders")) else None)
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, ld.EXIT_PATCH, "--plan --json moet met EXIT_PATCH eindigen: %s" % plan["dropins"])
        self.assertEqual(plan["dropins"]["files"], [])
        self.assertEqual(len(plan["dropins"]["errors"]), 1, plan["dropins"]["errors"])
        self.assertIn(tekst, plan["dropins"]["errors"][0])
        self.assertNotIn("TP_PRELOAD_DLL", plan["dropins"]["env"])
        self.assertIn("Drop-in geweigerd", err)
        rc, out, err = cli(self.GAME, "--plan")
        self.assertEqual(rc, ld.EXIT_PATCH)
        self.assertIn("drop-in: FOUT: ", out)                       # in het plan zelf, als fout en niet als waarschuwing
        self.assertIn(tekst, out)
        self.assertIn("Drop-in geweigerd", err)
        rc, out, err = cli(self.GAME, "--check")
        self.assertEqual(rc, ld.EXIT_PATCH)
        self.assertNotIn("ok:", out)
        rc, _, nieuw = self.start()
        self.assert_niets_gestart(rc, nieuw)
        self.assertIn("Drop-in geweigerd", self.fouten.getvalue())
        na = (boom(NAS), boom(os.path.join(TMP, "elders")) if os.path.isdir(os.path.join(TMP, "elders")) else None)
        self.assertEqual(na, voor, "er is buiten de view iets veranderd")

    def wordt_lokaal(self, lokaal_te_maken):
        """De weg wordt lokaal gemaakt: het plan zegt welke mappen, en na de start staat het bestand in de view."""
        dropin(self.GAME, self.INI, "rg-hook.dll")
        nas_voor = boom(NAS)
        dest = os.path.join(self.view, "exe", "x64", "rg-hook.dll")
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        self.assertEqual(plan["dropins"]["errors"], [])
        self.assertEqual(plan["dropins"]["materialise"], lokaal_te_maken)
        rc, out, _ = cli(self.GAME, "--plan")
        self.assertEqual(rc, 0)
        for m in lokaal_te_maken:
            self.assertIn("drop-in: %s loopt via een symlink; wordt eerst een echte map met symlinks" % m, out)
        self.assertEqual(boom(NAS), nas_voor, "--plan heeft de NAS veranderd")
        self.assertFalse(os.path.lexists(os.path.join(self.nas_spel, "exe", "x64", "rg-hook.dll")))
        rc, spel, _ = self.start("--env", "RG_TEST_VERWACHT_BESTAND=" + dest)
        self.assertEqual(rc, 0, self.uitvoer.getvalue() + self.fouten.getvalue())
        self.assertIsNotNone(spel)
        self.assertTrue(spel.bestand_stond_er, "het spel is gestart terwijl de drop-in er niet stond")
        self.assertEqual(spel.env.get("TP_PRELOAD_DLL"), "Z:" + dest.replace("/", "\\"))
        # het bestand staat in de view en NIET waar de symlink naartoe wees
        self.assertEqual(os.path.realpath(dest), dest)
        self.assertTrue(ld._binnen(os.path.realpath(os.path.dirname(dest)), self.view))
        for m in lokaal_te_maken:
            self.assertTrue(os.path.isdir(m) and not os.path.islink(m), m)
        self.assertTrue(os.path.islink(self.spel) and os.path.isfile(self.spel), "het spel zelf blijft een link naar de NAS")
        self.assertEqual(boom(NAS), nas_voor, "er is op de NAS geschreven")

    # (a) de launcher repareert het

    def test_de_gamemap_zelf_is_een_symlink_naar_buiten(self):
        """HET geval dat twee runs kostte: de map van de exe is zelf een symlink naar de NAS."""
        self.view_base_is_symlink()
        profiel(self.GAME, self.spel)
        self.assertTrue(os.path.islink(os.path.dirname(self.spel)))
        self.wordt_lokaal([os.path.join(self.view, "exe", "x64")])

    def test_een_symlink_boven_de_gamemap(self):
        """Luigi's Mansion: views/<Spel>/exe is de symlink, de game staat in exe/x64."""
        self.view_exe_is_symlink()
        profiel(self.GAME, self.spel)
        self.wordt_lokaal([os.path.join(self.view, "exe"), os.path.join(self.view, "exe", "x64")])

    # (b) het kan niet: luid weigeren

    def test_de_gamemap_is_een_symlink_en_er_is_geen_view_omheen(self):
        """De gamemap zelf is een symlink naar buiten en er is geen view-wortel erboven om lokaal te maken."""
        echt = os.path.join(TMP, "elders", "spel")
        schrijf(os.path.join(echt, "SPEL.exe"), b"MZ" + b"\0" * 62, "wb")
        os.makedirs(os.path.join(TMP, "los"))
        os.symlink(echt, os.path.join(TMP, "los", "spel"))
        profiel(self.GAME, os.path.join(TMP, "los", "spel", "SPEL.exe"))
        self.weigert("buiten")
        self.assertFalse(os.path.lexists(os.path.join(echt, "rg-hook.dll")))

    def test_de_view_is_zelf_een_symlink(self):
        self.view_is_zelf_symlink()
        profiel(self.GAME, self.spel)
        self.weigert("zelf een symlink")

    def test_een_game_rechtstreeks_van_de_nas(self):
        profiel(self.GAME, os.path.join(self.nas_spel, "exe", "x64", "SPEL.exe"))
        self.weigert("rechtstreeks van de NAS")

    def test_to_wijst_lexicaal_buiten_de_view(self):
        self.view_goed()
        profiel(self.GAME, self.spel)
        self.weigert("buiten de view", "[rg-hook.dll]\nTo = ../../../..\nPreload = true\n")

    def test_de_weg_loopt_door_een_bestand(self):
        self.view_goed()
        profiel(self.GAME, self.spel)
        self.weigert("niet te plaatsen", "[rg-hook.dll]\nTo = data.bin\nPreload = true\n")

    def test_een_symlink_op_de_weg_naar_iets_dat_geen_map_is(self):
        self.view_goed()
        os.symlink(os.path.join(self.nas_spel, "exe", "leesmij.txt"), os.path.join(self.view, "exe", "x64", "sub"))
        profiel(self.GAME, self.spel)
        self.weigert("niet te plaatsen", "[rg-hook.dll]\nTo = sub\nPreload = true\n")


class NativeRouteBlijftLdPreload(nep.Route):
    """Punt 3: de native route is niet veranderd. Die gebruikt LD_PRELOAD en dat werkte al."""

    def setUp(self):
        super().setUp()
        self.native = os.path.join(TMP, "los", "native", "game")
        schrijf(self.native, b"\x7fELF" + b"\0" * 60, "wb")
        dropin(self.GAME, "[lib-hook.so]\nTo = .\nPreload = true\n", "lib-hook.so")
        self.dest = os.path.join(os.path.dirname(self.native), "lib-hook.so")

    def plan(self, env=None):
        """Een start zonder Wine: het spel is zelf het programma."""
        return {"profile": self.GAME, "game": self.native, "env": dict(env or {}), "launch_env": {},
                "wine": "", "argv": [self.native]}

    def test_zonder_wine_gaat_het_pad_in_ld_preload_als_unix_pad(self):
        uit = ld.dropin_plan(self.plan())
        self.assertEqual(uit["errors"], [])
        self.assertEqual(uit["env"].get("LD_PRELOAD"), self.dest)
        self.assertTrue(uit["env"]["LD_PRELOAD"].startswith("/"))
        self.assertNotIn("\\", uit["env"]["LD_PRELOAD"])
        self.assertEqual([(k["var"], k["value"], k["loads"]) for k in uit["preload_env"]],
                         [("LD_PRELOAD", self.dest, True)])

    def test_de_wine_variabelen_komen_er_niet_bij(self):
        uit = ld.dropin_plan(self.plan())
        for var in ("TP_PRELOAD_DLL", "LD_AUDIT"):
            self.assertNotIn(var, uit["env"])
        self.assertIn("preload: lib-hook.so -> LD_PRELOAD=" + self.dest, "\n".join(ld.dropin_log_lines({"dropins": uit})))

    def test_een_bestaande_ld_preload_blijft_staan(self):
        """Samenvoegen, niet overschrijven: in het plan en in de omgeving die run() opbouwt (merge_env)."""
        plan = self.plan(env={"LD_PRELOAD": "/al/aanwezig.so"})
        uit = ld.dropin_plan(plan)
        self.assertEqual(uit["env"]["LD_PRELOAD"], "/al/aanwezig.so:" + self.dest)
        ld.merge_env(plan, {"LD_PRELOAD": self.dest})
        self.assertEqual(plan["env"]["LD_PRELOAD"], "/al/aanwezig.so:" + self.dest)
        ld.merge_env(plan, {"LD_PRELOAD": self.dest})                 # twee keer is geen dubbele vermelding
        self.assertEqual(plan["env"]["LD_PRELOAD"], "/al/aanwezig.so:" + self.dest)

    def test_meer_dan_een_preload_mag_op_de_native_route(self):
        """LD_PRELOAD is een lijst; de beperking 'precies een' geldt alleen voor TP_PRELOAD_DLL."""
        dropin(self.GAME, "[a.so]\nPreload = true\n[b.so]\nPreload = true\n", "a.so", "b.so")
        uit = ld.dropin_plan(self.plan())
        self.assertEqual(uit["errors"], [])
        d = os.path.dirname(self.native)
        self.assertEqual(uit["env"]["LD_PRELOAD"], os.path.join(d, "a.so") + ":" + os.path.join(d, "b.so"))

    def test_play_native_zet_preload_nog_in_ld_preload(self):
        """scripts/play-native.sh is de native start zelf. Het hele script draaien kan hier niet (het wil de
        NAS, bwrap en een beeldscherm), dus dit is een bewaking op de bron: het veld `preload` uit het
        gegevensbestand gaat naar LD_PRELOAD van het spelproces, en de Wine-variabele komt er niet in voor."""
        with open(os.path.join(REPO, "scripts", "play-native.sh")) as f:
            bron = f.read()
        self.assertIn('BW+=(--setenv LD_PRELOAD "${V[preload]}")', bron)
        self.assertNotIn("TP_PRELOAD_DLL", bron)


if __name__ == "__main__":
    unittest.main()

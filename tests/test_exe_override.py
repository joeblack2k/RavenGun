#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Het spel vanaf een ander pad starten (--exe, [Launch] Exe) en schijfletters voor een run (--drive, [Launch] Drives).

Het eigen startscript van een spel start het soms vanaf een schijfletter (`x:\\exe\\x64\\spel.exe`). Om dat na te
bootsen moet de launcher het spel vanaf dat pad kunnen starten -- en dan mag er NIETS anders veranderen. Anders weet
je na de run niet of een verschil aan het spel lag of aan onze opzet. Wat hier wordt afgedwongen:

1. Zonder override verandert er niets.
2. Met override verandert alleen het pad dat de lader voor het spel krijgt: het plan en de werkelijke start (de
   JVS-brug, de omgeving, de werkmap, de bestanden naast het spel, de drop-ins) zijn verder gelijk.
3. `--plan` laat zien welk pad eruit komt en dat het een override is.
4. Een schijfletter staat er precies zolang de run duurt: ervoor gezet, erna weg, ook als de run misgaat; een
   letter die al bestaat en ergens anders heen wijst wordt geweigerd; wat een afgebroken run liet staan wordt
   opgeruimd.
5. Wat niet kan wordt in het plan geweigerd, voordat er iets start.

Er wordt niets gestart (tests/launcher_nep.py). Dat Wine een programma werkelijk vanaf zo'n letter start is op
2026-10-13 onder Wine gemeten, in een eigen testprefix en met de functies van de launcher zelf (drives_aan,
drives_opruimen): een programma start vanaf X:, een kindproces met werkmap X:\\exe\\x64 ziet die werkmap, en na het
opruimen bestaat X: voor Wine niet meer -- ook als er al een wineserver liep toen de letter erbij kwam. Niet gemeten:
de lader die zelf een spel vanaf X: start (dat vraagt een kern en een echt spel, dus de bench).
"""
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launcher_nep as nep  # noqa: E402
from launcher_nep import SHIPPED, TMP, cli, dropin, ld, profiel, schrijf  # noqa: E402

PREFIX = ld.SHARED_PREFIX
DOS = os.path.join(PREFIX, "dosdevices")


def zonder_sessie(d):
    return {k: v for k, v in d.items() if k != "TP_LAUNCH_SESSION_ID"}


class Basis(nep.Route):
    def setUp(self):
        super().setUp()
        self.view_goed()
        profiel(self.GAME, self.spel)
        self.exe_win = "Z:" + self.spel.replace("/", "\\")
        nep.shutil.rmtree(PREFIX, ignore_errors=True)
        os.makedirs(DOS)
        os.symlink("/", os.path.join(DOS, "z:"))
        if os.path.exists(ld.DRIVES_STATE):
            os.unlink(ld.DRIVES_STATE)
        self.x = os.path.join(DOS, "x:")

    def plan(self, *extra):
        rc, plan, err = self.plan_json(*extra)
        self.assertEqual(rc, 0, err)
        return plan

    def weigert(self, tekst, *extra):
        rc, out, err = cli(self.GAME, "--plan", *extra)
        self.assertEqual(rc, ld.EXIT_PATCH, out + err)
        self.assertIn(tekst, err)
        self.assertEqual(cli(self.GAME, "--check", *extra)[0], ld.EXIT_PATCH)
        rc, _, nieuw = self.start(*extra)
        self.assert_niets_gestart(rc, nieuw)
        self.assertFalse(os.path.lexists(self.x) and os.readlink(self.x) == self.view, "er is een schijfletter blijven staan")


OVERRIDE = ("--exe", "x:\\exe\\x64\\SPEL.exe", "--drive", "X=.")


class ZonderOverride(Basis):
    def test_het_plan_is_het_oude(self):
        plan = self.plan()
        self.assertEqual(plan["argv"][-1], self.exe_win)
        self.assertEqual(plan["game_exe"], {"passed": self.exe_win, "override": None})
        self.assertEqual(plan["drives"], [])
        rc, out, _ = cli(self.GAME, "--plan")
        self.assertIn("spel: " + self.exe_win, out)
        self.assertNotIn("OVERRIDE", out)
        self.assertNotIn("schijfletter", out)

    def test_de_run_raakt_de_prefix_niet(self):
        voor = sorted(os.listdir(DOS))
        rc, spel, _ = self.start()
        self.assertEqual(rc, 0, self.uitvoer.getvalue())
        self.assertEqual(spel.argv[-1], self.exe_win)
        self.assertEqual(sorted(os.listdir(DOS)), voor)
        self.assertFalse(os.path.exists(ld.DRIVES_STATE))


class AlleenHetPadVerandert(Basis):
    """Punt 2 en 3: de hulpketen is met en zonder override gelijk."""

    def test_het_plan_verschilt_alleen_in_het_pad_van_het_spel(self):
        dropin(self.GAME, "[rg-hook.dll]\nTo = .\nPreload = true\n", "rg-hook.dll")
        schrijf(os.path.join(SHIPPED, self.GAME, "game.ini"), "[Env]\nGAMEID = SDCF\n[Launch]\nArguments = -a \"b c\"\n")
        zonder, met = self.plan(), self.plan(*OVERRIDE)
        # het pad dat de lader krijgt
        self.assertEqual(met["game_exe"]["passed"], "X:\\exe\\x64\\SPEL.exe")
        self.assertTrue(met["game_exe"]["override"]["same_file"])
        self.assertEqual(met["game_exe"]["override"]["unix"], self.spel)
        i = zonder["argv"].index(self.exe_win)
        self.assertEqual(met["argv"][i], "X:\\exe\\x64\\SPEL.exe")
        self.assertEqual(zonder["argv"][:i] + zonder["argv"][i + 1:], met["argv"][:i] + met["argv"][i + 1:])
        self.assertEqual(met["drives"], [{"letter": "X", "target": self.view, "link": self.x, "present": False}])
        # en verder niets: elke andere sleutel van het plan is gelijk
        anders = set()
        for k in set(zonder) | set(met):
            a, b = zonder.get(k), met.get(k)
            if k in ("env", "launch_env"):
                a, b = zonder_sessie(a), zonder_sessie(b)
            if a != b:
                anders.add(k)
        self.assertEqual(anders, {"argv", "starts", "game_exe", "drives"}, anders)
        s0, s1 = dict(zonder["starts"][0]), dict(met["starts"][0])
        for s in (s0, s1):
            s.pop("argv")
            s.pop("exe_passed")
        self.assertEqual(s0, s1)                                     # ook de start zelf: alleen argv en het doorgegeven pad
        # met name: het spel uit het profiel, de werkmap, de prefix, de bestanden naast het spel en de drop-ins
        for k in ("game", "cwd", "prefix", "wine", "core_dll", "files", "dropins", "pipehelper", "shm_source", "game_comms",
                  "game_args", "game_env", "profile_env", "patches"):
            self.assertEqual(zonder[k], met[k], k)

    def test_de_werkelijke_start_verschilt_alleen_in_het_pad_van_het_spel(self):
        dropin(self.GAME, "[rg-hook.dll]\nTo = .\nPreload = true\n", "rg-hook.dll")
        schrijf(os.path.join(SHIPPED, self.GAME, "game.ini"), "[Env]\nGAMEID = SDCF\n")
        ini = os.path.join(os.path.dirname(self.spel), "teknoparrot.ini")
        hook = os.path.join(os.path.dirname(self.spel), "rg-hook.dll")

        def een_run(*extra):
            for f in (ini, hook):
                if os.path.exists(f):
                    os.unlink(f)
            voor = len(nep.NepProces.gestart)
            self.gestart_voor = voor
            rc, spel, nieuw = self.start(*extra)
            self.assertEqual(rc, 0, self.uitvoer.getvalue())
            helper = [p for p in nieuw if p is not spel][0]
            with open(ini) as f:
                ini_tekst = f.read()
            self.assertTrue(os.path.isfile(hook))
            return spel, helper, ini_tekst

        spel0, helper0, ini0 = een_run()
        spel1, helper1, ini1 = een_run(*OVERRIDE)
        # de JVS-brug: zelfde programma, zelfde werkmap, zelfde omgeving
        self.assertEqual(helper0.argv[:2] + helper0.argv[4:], helper1.argv[:2] + helper1.argv[4:])   # [2:4] = host en poort van de pipe
        self.assertEqual(helper0.cwd, helper1.cwd)
        self.assertEqual(zonder_sessie(helper0.env), zonder_sessie(helper1.env))
        # het spel: zelfde werkmap, zelfde omgeving (prefix, kern-instellingen, variabelen van de game, preload van de drop-in)
        self.assertEqual(spel0.cwd, spel1.cwd)
        self.assertEqual(zonder_sessie(spel0.env), zonder_sessie(spel1.env))
        self.assertEqual(spel0.env["WINEPREFIX"], PREFIX)
        self.assertEqual(spel0.env["GAMEID"], "SDCF")
        self.assertTrue(spel0.env["TP_PRELOAD_DLL"])
        # zelfde wine, zelfde lader, zelfde kern; alleen het pad van het spel is anders
        self.assertEqual(spel0.argv[:-1], spel1.argv[:-1])
        self.assertEqual((spel0.argv[-1], spel1.argv[-1]), (self.exe_win, "X:\\exe\\x64\\SPEL.exe"))
        self.assertEqual(ini0, ini1)                                  # teknoparrot.ini: zelfde plek, zelfde inhoud

    def test_het_plan_zegt_dat_het_een_override_is(self):
        rc, out, err = cli(self.GAME, "--plan", *OVERRIDE)
        self.assertEqual(rc, 0, err)
        self.assertIn("spel (OVERRIDE, opgegeven als x:\\exe\\x64\\SPEL.exe): X:\\exe\\x64\\SPEL.exe", out)
        self.assertIn("hetzelfde bestand als in het profiel (%s), langs een ander pad" % self.spel, out)
        self.assertIn("schijfletter X: -> %s (wordt voor deze run in de prefix gezet en na afloop weer weggehaald)" % self.view, out)
        self.assertIn("argumenten voor SPEL.exe: geen", out)
        self.assertFalse(os.path.lexists(self.x), "--plan heeft een schijfletter aangemaakt")

    def test_de_vormen_van_het_pad(self):
        rel = self.plan("--exe", "exe/x64/SPEL.exe")
        self.assertEqual(rel["game_exe"]["passed"], self.exe_win)                 # relatief aan de view
        unix = self.plan("--exe", self.spel)
        self.assertEqual(unix["game_exe"]["passed"], self.exe_win)                # een Unix-pad
        z = self.plan("--exe", self.exe_win)
        self.assertEqual(z["game_exe"]["passed"], self.exe_win)                   # een Z:-pad
        hoofd = self.plan("--exe", "X:\\EXE\\X64\\spel.exe", "--drive", "x:=.")
        self.assertEqual(hoofd["game_exe"]["passed"], "X:\\EXE\\X64\\spel.exe")  # zoals opgegeven; Windows let niet op hoofdletters
        self.assertEqual(hoofd["game_exe"]["override"]["unix"], self.spel)

    def test_uit_game_ini_en_de_opdrachtregel_wint(self):
        schrijf(os.path.join(SHIPPED, self.GAME, "game.ini"), "[Launch]\nExe = x:\\exe\\x64\\SPEL.exe\nDrives = X=.\n")
        plan = self.plan()
        self.assertEqual(plan["game_exe"]["passed"], "X:\\exe\\x64\\SPEL.exe")
        self.assertEqual([d["letter"] for d in plan["drives"]], ["X"])
        self.assertEqual(self.plan("--exe", self.spel)["game_exe"]["passed"], self.exe_win)
        self.assertEqual(self.plan("--exe", "")["game_exe"]["override"], None)    # een lege --exe zet de override uit

    def test_een_ander_bestand_in_de_view_mag_en_wordt_genoemd(self):
        ander = os.path.join(self.view, "exe", "x64", "ANDER.exe")
        schrijf(ander, b"MZ" + b"\0" * 62, "wb")
        rc, out, err = cli(self.GAME, "--plan", "--exe", "exe/x64/ANDER.exe")
        self.assertEqual(rc, 0, err)
        self.assertIn("een ANDER bestand dan in het profiel", out)
        plan = self.plan("--exe", "exe/x64/ANDER.exe")
        self.assertFalse(plan["game_exe"]["override"]["same_file"])
        self.assertIn("ander.exe", plan["starts"][0]["comms"])


class Schijfletters(Basis):
    """Punt 4: een schijfletter staat er precies zolang de run duurt."""

    def test_de_letter_staat_er_tijdens_de_run_en_is_daarna_weg(self):
        gezien = {}

        def gedrag(argv):
            gezien[" ".join(argv)[-20:]] = os.readlink(self.x) if os.path.islink(self.x) else None
            return None
        nep.NepProces.gedrag = gedrag
        rc, spel, _ = self.start(*OVERRIDE)
        self.assertEqual(rc, 0, self.uitvoer.getvalue())
        self.assertEqual(set(gezien.values()), {self.view}, "bij het starten stond X: er niet")
        self.assertFalse(os.path.lexists(self.x), "X: is na de run blijven staan")
        with open(ld.DRIVES_STATE) as f:
            self.assertEqual(json.load(f), {})
        logboek = self.uitvoer.getvalue()
        self.assertIn("schijfletter X: -> %s (in de prefix %s)" % (self.view, PREFIX), logboek)
        self.assertIn("schijfletter X: weggehaald uit de prefix", logboek)

    def test_ook_weg_als_de_run_misgaat(self):
        rc, _, nieuw = self.start(*OVERRIDE, "--shm-source", os.path.join(TMP, "bestaat-niet"))
        self.assertEqual(rc, 1)
        self.assertFalse(os.path.lexists(self.x))
        nep.NepProces.gedrag = lambda argv: (_ for _ in ()).throw(RuntimeError("de start valt om"))
        with self.assertRaises(RuntimeError):
            self.start(*OVERRIDE)
        self.assertFalse(os.path.lexists(self.x), "X: is blijven staan na een fout")

    def test_een_letter_die_er_al_goed_staat_blijft_staan(self):
        os.symlink(self.view, self.x)
        plan = self.plan(*OVERRIDE)
        self.assertTrue(plan["drives"][0]["present"])
        rc, _, _ = self.start(*OVERRIDE)
        self.assertEqual(rc, 0)
        self.assertEqual(os.readlink(self.x), self.view)             # niet van ons: laten staan
        # en zonder --drive: de letter die al in de prefix staat volstaat voor de override
        self.assertEqual(self.plan("--exe", "x:\\exe\\x64\\SPEL.exe")["game_exe"]["passed"], "X:\\exe\\x64\\SPEL.exe")

    def test_een_letter_die_ergens_anders_heen_wijst_wordt_geweigerd(self):
        elders = os.path.join(TMP, "elders")
        os.makedirs(elders)
        os.symlink(elders, self.x)
        self.weigert("bestaat al in de prefix", *OVERRIDE)
        self.assertEqual(os.readlink(self.x), elders)                # en we blijven er vanaf

    def test_resten_van_een_afgebroken_run_worden_opgeruimd(self):
        """Een run die hard gestopt is laat X: staan; de volgende run (ook zonder schijfletters ervoor) ruimt hem op."""
        os.symlink(self.view, self.x)
        ld._drives_state_schrijf({PREFIX: {self.x: self.view}})
        rc, _, _ = self.start(*OVERRIDE)                             # opnieuw dezelfde letter: geen "bestaat al"
        self.assertEqual(rc, 0, self.uitvoer.getvalue())
        self.assertFalse(os.path.lexists(self.x))

    def test_een_letter_die_wine_bij_zijn_start_weghaalt_wordt_opnieuw_gelegd(self):
        """Gemeten op de gedeelde prefix: een koppeling die er voor de wineserver stond was daarna weg. De launcher
        kijkt daarom na de eerste Wine-start (de JVS-brug) opnieuw en legt hem terug voordat het spel start."""
        gezien = {}

        def gedrag(argv):
            if "OpenParrotLoader" not in " ".join(argv):
                if os.path.islink(self.x):
                    os.unlink(self.x)                        # "Wine" haalt hem weg terwijl de brug start
            else:
                gezien["spel"] = os.readlink(self.x) if os.path.islink(self.x) else None
            return None
        nep.NepProces.gedrag = gedrag
        rc, spel, _ = self.start(*OVERRIDE)
        self.assertEqual(rc, 0, self.uitvoer.getvalue())
        self.assertEqual(gezien.get("spel"), self.view, "bij het starten van het spel stond X: er niet")
        self.assertIn("was na de eerste Wine-start verdwenen uit de prefix; opnieuw gelegd", self.uitvoer.getvalue())
        self.assertFalse(os.path.lexists(self.x), "X: is na de run blijven staan")

    def test_een_letter_die_bij_de_start_ergens_anders_heen_wijst_stopt_de_run(self):
        elders = os.path.join(TMP, "elders")
        os.makedirs(elders)

        def gedrag(argv):
            if "OpenParrotLoader" not in " ".join(argv) and os.path.islink(self.x):
                os.unlink(self.x)
                os.symlink(elders, self.x)                   # iets heeft X: omgelegd terwijl de brug startte
            return None
        nep.NepProces.gedrag = gedrag
        rc, spel, nieuw = self.start(*OVERRIDE)
        self.assertEqual(rc, ld.EXIT_PATCH, self.uitvoer.getvalue())
        self.assertIsNone(spel, "het spel is gestart terwijl X: verkeerd stond")
        self.assertIn("wijst vlak voor de start naar", self.uitvoer.getvalue())
        self.assertEqual(os.readlink(self.x), elders)        # niet van ons: laten staan

    def test_wat_niet_van_ons_is_ruimen_we_niet_op(self):
        elders = os.path.join(TMP, "elders")
        os.makedirs(elders)
        os.symlink(elders, self.x)                                   # iemand heeft X: intussen zelf gezet
        ld._drives_state_schrijf({PREFIX: {self.x: self.view}})
        ld.drives_opruimen(PREFIX)
        self.assertEqual(os.readlink(self.x), elders)


class WatNietKan(Basis):
    """Punt 5: geweigerd in het plan, voordat er iets start."""

    def test_een_letter_die_niet_bestaat(self):
        self.weigert("schijf X: bestaat niet in de prefix", "--exe", "x:\\exe\\x64\\SPEL.exe")

    def test_een_bestand_dat_er_niet_is(self):
        self.weigert("dat bestand bestaat niet", "--exe", "x:\\exe\\x64\\NIET.exe", "--drive", "X=.")
        self.weigert("dat bestand bestaat niet", "--exe", "exe/x64/NIET.exe")

    def test_een_bestand_buiten_de_view(self):
        buiten = os.path.join(TMP, "los", "ANDER.exe")
        schrijf(buiten, b"MZ" + b"\0" * 62, "wb")
        self.weigert("buiten de view", "--exe", buiten)

    def test_een_schijfletter_naar_een_map_die_er_niet_is(self):
        self.weigert("bestaat niet", "--drive", "Y=hier/niet")

    def test_een_schijfletter_naar_de_nas(self):
        self.weigert("ligt op de NAS", "--drive", "Y=" + self.nas_spel)

    def test_de_vaste_letters(self):
        for letter in ("C", "Z", "XY", "1"):
            self.weigert("kan niet", "--drive", letter + "=.")
        self.weigert("geen LETTER=MAP", "--drive", "X")

    def test_een_prefix_zonder_dosdevices(self):
        nep.shutil.rmtree(DOS)
        self.weigert("heeft geen dosdevices-map", "--drive", "X=.")


class MetTweeExes(Basis):
    def test_de_override_raakt_alleen_het_spel(self):
        schrijf(os.path.join(self.nas_spel, "hulp", "HULP.exe"), b"MZ" + b"\0" * 62, "wb")
        os.makedirs(os.path.join(self.view, "hulp"))
        hulp = os.path.join(self.view, "hulp", "HULP.exe")
        os.symlink(os.path.join(self.nas_spel, "hulp", "HULP.exe"), hulp)
        profiel(self.GAME, self.spel, extra="<HasTwoExecutables>true</HasTwoExecutables><ExecutableName2>HULP.exe</ExecutableName2>"
                                            "<GamePath2>%s</GamePath2><LaunchSecondExecutableFirst>true</LaunchSecondExecutableFirst>" % hulp)
        zonder, met = self.plan(), self.plan(*OVERRIDE)
        self.assertEqual(zonder["starts"][0], met["starts"][0])                      # de tweede exe: ongewijzigd
        self.assertEqual(met["starts"][1]["argv"][-1], "X:\\exe\\x64\\SPEL.exe")
        self.assertEqual(zonder["starts"][1]["argv"][:-1], met["starts"][1]["argv"][:-1])


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Argumenten voor het spel, per game ([Launch] Arguments in game.ini), en hoe ze heel bij het spel aankomen.

Het gat: de launcher gaf het spel alleen mee wat hij zelf bedacht. Het eigen startscript van een spel geeft soms
iets mee dat ertoe doet (een UE4-spel wachtte 60 seconden op een foutvenster dat `-UNATTENDED` had onderdrukt), en
er was geen plek om dat per game te zeggen.

Wat hier wordt afgedwongen:
1. De standaard is leeg: zonder de sleutel verandert er niets aan de start.
2. `--plan` laat zien WELKE argumenten er meegaan, en hoe.
3. Een argument met spaties of aanhalingstekens blijft heel.
4. De OpenParrot-lader krijgt nooit meer dan EEN parameter voor het spel. Hij neemt er precies een
   (`<DLL> <EXE> <param>`); bij meer toont hij zijn gebruiksregel, wacht op een toets en geeft 0 terug, en het spel
   start dan niet. Dat was met twee losse argumenten in het profiel al zo, stil.

Er wordt niets gestart (tests/launcher_nep.py). De stap van Wine naar de lader is hier nagerekend met de regels van
de Windows-opdrachtregel. Onder Wine is hij op 2026-10-13 met de echte OpenParrotLoader64.exe gemeten (de lader drukt
`Arguments: ...` af voordat hij iets start): de parameter met aanhalingstekens, een spatie in een pad en een
backslash voor een aanhalingsteken kwam letterlijk aan; twee losse argumenten gaven de gebruiksregel en exitcode 0.
"""
import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launcher_nep as nep  # noqa: E402
from launcher_nep import SHIPPED, TP, WINE, cli, ld, profiel, schrijf  # noqa: E402

OFFICIEEL = '-launch="cabinet" -UserDir="C:\\log map\\nu" -NotInstalled -UNATTENDED'
OFFICIEEL_LIJST = ["-launch=cabinet", "-UserDir=C:\\log map\\nu", "-NotInstalled", "-UNATTENDED"]


def wat_het_spel_ziet(exe, param):
    """De lader maakt de opdrachtregel `"exe" <param>` (OpenParrotLoader/src/main.cpp); het spel deelt die zelf op."""
    return ld.win_split('"%s" %s' % (exe, param))[1:]


def door_wine(argv):
    """Wine maakt van de losse elementen een Windows-opdrachtregel en de lader deelt die weer op. Nagerekend met
    dezelfde regels (subprocess.list2cmdline is de omgekeerde bewerking van win_split)."""
    return ld.win_split(subprocess.list2cmdline(argv))


class Basis(nep.Route):
    def setUp(self):
        super().setUp()
        self.view_goed()
        self.exe_win = "Z:" + self.spel.replace("/", "\\")

    def ini(self, tekst):
        schrijf(os.path.join(SHIPPED, self.GAME, "game.ini"), tekst)

    def na_de_exe(self, plan):
        return plan["argv"][plan["argv"].index(self.exe_win) + 1:]


class DeStandaardIsLeeg(Basis):
    def test_zonder_sleutel_gaat_er_niets_mee(self):
        profiel(self.GAME, self.spel)
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        self.assertEqual(self.na_de_exe(plan), [])
        self.assertEqual(plan["argv"][-1], self.exe_win)
        self.assertEqual((plan["game_args"]["list"], plan["game_args"]["param"]), ([], None))

    def test_een_lege_sleutel_is_hetzelfde(self):
        profiel(self.GAME, self.spel)
        self.ini("[Launch]\nArguments =\n")
        _, plan, _ = self.plan_json()
        self.assertEqual(self.na_de_exe(plan), [])

    def test_het_plan_zegt_dat_er_niets_meegaat(self):
        profiel(self.GAME, self.spel)
        rc, out, _ = cli(self.GAME, "--plan")
        self.assertEqual(rc, 0)
        self.assertIn("argumenten voor SPEL.exe: geen", out)


class PerGameArgumenten(Basis):
    def setUp(self):
        super().setUp()
        profiel(self.GAME, self.spel)
        self.ini("[Launch]\nArguments = %s\n" % OFFICIEEL)

    def test_het_plan_laat_zien_wat_er_meegaat(self):
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        a = plan["game_args"]
        self.assertEqual(a["list"], OFFICIEEL_LIJST)                 # zo ziet het spel ze: vier, de spatie heel
        self.assertEqual(a["how"], "one-parameter")
        self.assertEqual(a["param"], OFFICIEEL)                      # letterlijk, zoals in het startscript
        self.assertEqual(a["sources"], [{"key": "[Launch] Arguments", "value": OFFICIEEL}])
        self.assertEqual(plan["starts"][0]["args"]["list"], OFFICIEEL_LIJST)
        rc, out, _ = cli(self.GAME, "--plan")
        self.assertIn("argumenten voor SPEL.exe (4): [-launch=cabinet]  [-UserDir=C:\\log map\\nu]  [-NotInstalled]  [-UNATTENDED]", out)
        self.assertIn("als een parameter aan de lader, letterlijk: " + OFFICIEEL, out)
        self.assertIn("uit [Launch] Arguments = " + OFFICIEEL, out)

    def test_de_lader_krijgt_precies_een_parameter(self):
        _, plan, _ = self.plan_json()
        self.assertEqual(self.na_de_exe(plan), [OFFICIEEL])
        # wine, lader, kern, exe, parameter: voor de lader is dat argc 4, zijn maximum
        self.assertEqual(len(plan["argv"]), 5)

    def test_het_argument_met_spaties_en_aanhalingstekens_blijft_heel_tot_in_het_spel(self):
        _, plan, _ = self.plan_json()
        # 1. van de launcher naar de lader (via Wine): de lader ziet dezelfde elementen, de parameter als een geheel
        bij_de_lader = door_wine(plan["argv"][1:])
        self.assertEqual(bij_de_lader, plan["argv"][1:])
        self.assertEqual(len(bij_de_lader) - 1, 3)                   # <DLL> <EXE> <param>
        # 2. van de lader naar het spel: "exe" <param>, door het spel zelf opgedeeld
        self.assertEqual(wat_het_spel_ziet(self.exe_win, bij_de_lader[-1]), OFFICIEEL_LIJST)

    def test_de_start_geeft_het_argument_ongeschonden_door(self):
        """run() geeft een lijst aan het proces, geen tekst aan een shell: er valt niets uit elkaar."""
        rc, spel, _ = self.start()
        self.assertEqual(rc, 0, self.uitvoer.getvalue())
        self.assertEqual(spel.argv[-1], OFFICIEEL)
        self.assertEqual(spel.argv[-2], self.exe_win)
        self.assertEqual(len(spel.argv), 5)


class HetProfielBlijftWerken(Basis):
    """De velden van het TeknoParrot-profiel (ExtraParameters, CustomArguments) doen wat ze deden."""

    def plan_met(self, extra, ini=""):
        profiel(self.GAME, self.spel, extra=extra)
        if ini:
            self.ini(ini)
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        return plan

    def test_argumenten_als_een_geheel_tussen_aanhalingstekens(self):
        """De gewoonte van TeknoParrot: alles tussen aanhalingstekens, zodat het een parameter is."""
        plan = self.plan_met("<ExtraParameters>&quot;-f -c config.json&quot;</ExtraParameters>")
        self.assertEqual(self.na_de_exe(plan), ["-f -c config.json"])
        self.assertEqual(plan["game_args"]["list"], ["-f", "-c", "config.json"])

    def test_een_enkel_argument(self):
        plan = self.plan_met("<ExtraParameters>-windowed</ExtraParameters>")
        self.assertEqual(self.na_de_exe(plan), ["-windowed"])

    def test_twee_losse_argumenten_worden_een_parameter(self):
        """Dit ging stil mis: twee elementen is argc 5, en dan start de lader het spel niet."""
        plan = self.plan_met("<ExtraParameters>-a -b</ExtraParameters>")
        self.assertEqual(self.na_de_exe(plan), ["-a -b"])
        self.assertEqual(plan["game_args"]["list"], ["-a", "-b"])

    def test_profiel_en_game_ini_samen_blijven_een_parameter(self):
        plan = self.plan_met("<CustomArguments>-x</CustomArguments><ExtraParameters>-y</ExtraParameters>",
                             "[Launch]\nArguments = -z=\"een twee\"\n")
        self.assertEqual(self.na_de_exe(plan), ['-x -y -z="een twee"'])
        self.assertEqual(plan["game_args"]["list"], ["-x", "-y", "-z=een twee"])
        self.assertEqual([b["key"] for b in plan["game_args"]["sources"]],
                         ["CustomArguments", "ExtraParameters", "[Launch] Arguments"])

    def test_nooit_meer_dan_een_parameter_voor_de_openparrot_lader(self):
        for extra, ini in (("<ExtraParameters>-a -b -c</ExtraParameters>", ""),
                           ("<CustomArguments>-a</CustomArguments><ExtraParameters>-b</ExtraParameters>", ""),
                           ("", "[Launch]\nArguments = -a -b \"c d\"\n"),
                           ("<ExtraParameters>&quot;-a -b&quot;</ExtraParameters>", "[Launch]\nArguments = -c\n")):
            plan = self.plan_met(extra, ini)
            self.assertLessEqual(len(self.na_de_exe(plan)), 1, (extra, ini, plan["argv"]))
            schrijf(os.path.join(SHIPPED, self.GAME, "game.ini"), "")


class AndereLaders(Basis):
    """Een lader die geen 'een parameter' kent krijgt de argumenten los, elk als een eigen element."""

    def test_een_elf_onder_wine_krijgt_losse_argumenten(self):
        elf = os.path.join(self.view, "exe", "x64", "game")
        schrijf(os.path.join(self.nas_spel, "exe", "x64", "game"), b"\x7fELF" + b"\0" * 60, "wb")
        os.symlink(os.path.join(self.nas_spel, "exe", "x64", "game"), elf)
        profiel(self.GAME, elf, emu_type="ElfLdr2", is64="false")
        self.ini("[Game]\nType = ElfLdr2\n[Launch]\nArguments = -f1920x1080 -n \"een twee\"\n")
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        self.assertEqual(os.path.basename(plan["argv"][plan["argv"].index(WINE) + 1]), "BudgieLoader.exe")
        self.assertEqual(plan["argv"][-3:], ["-f1920x1080", "-n", "een twee"])
        self.assertEqual(plan["game_args"]["how"], "separate")
        self.assertEqual(plan["game_args"]["list"], ["-f1920x1080", "-n", "een twee"])
        self.assertIsNone(plan["game_args"]["param"])

    def test_de_keuze_hangt_alleen_van_de_lader_af(self):
        self.assertEqual(ld.spel_argumenten("/x/OpenParrotLoader.exe", ["-a", "-b"], "")["argv"], ["-a -b"])
        self.assertEqual(ld.spel_argumenten("/x/OpenParrotLoader64.exe", [], "-a -b")["argv"], ["-a -b"])
        self.assertEqual(ld.spel_argumenten("/x/TeknoMacaw64.exe", ["-a", "-b"], "-c")["argv"], ["-a", "-b", "-c"])


class DeOpdeling(unittest.TestCase):
    """win_split deelt op zoals een Windows-programma zijn eigen opdrachtregel opdeelt."""

    def test_de_regels(self):
        for tekst, lijst in (
                ("", []),
                ("  a   b  ", ["a", "b"]),
                ('"a b" c', ["a b", "c"]),
                ('-k="a b"', ["-k=a b"]),
                ("C:\\map\\bestand.txt", ["C:\\map\\bestand.txt"]),          # een backslash blijft staan
                ('"C:\\een map\\\\" x', ["C:\\een map\\", "x"]),              # 2 backslashes voor " -> 1, " sluit
                ('a\\"b', ['a"b']),                                          # \" is een letterlijk aanhalingsteken
                ('"a""b"', ['a"b']),                                         # "" binnen aanhalingstekens
                ('""', [""]),                                                # een leeg argument telt
        ):
            self.assertEqual(ld.win_split(tekst), lijst, tekst)

    def test_heen_en_terug_met_de_regels_van_windows(self):
        for lijst in (["a b", 'c"d', "e\\", "f\\\\ g", "-k=x y", ""], OFFICIEEL_LIJST, ['"', "\\", ' ']):
            self.assertEqual(ld.win_split(subprocess.list2cmdline(lijst)), lijst)


if __name__ == "__main__":
    unittest.main()

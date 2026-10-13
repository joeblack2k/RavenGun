#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Omgevingsvariabelen per game ([Env]) en het eigen startscript van een spel (game.bat, run_game.sh).

De launcher startte een spel met wat hij zelf bedacht. Het eigen startscript van het spel zet variabelen, kiest een
werkmap, start soms eerst iets anders en geeft argumenten mee -- en dat sloegen wij over. Wat hier wordt afgedwongen:

1. Variabelen uit [Env] in game.ini komen in de omgeving van het SPELPROCES (en van de tweede exe), ongeschonden, en
   --plan laat zien welke er meegaan. De standaard is: geen.
2. Het startscript wordt GELEZEN en nooit uitgevoerd; --plan zegt wat de kast doet en wij niet: variabelen,
   argumenten, programma's die eerst starten, de werkmap, de schijfletters.
3. Wat het script uit de bestanden beslist (`if exist`, `if "%X%"=="y"`), beslist de lezer ook; wat van de kast
   afhangt wordt niet geraden maar "voorwaardelijk" genoemd.
4. Heeft game.ini de variabelen en argumenten van het script, dan zegt --plan dat ze gelijk zijn.

Er wordt niets gestart (tests/launcher_nep.py).
"""
import importlib.util
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launcher_nep as nep  # noqa: E402
from launcher_nep import REPO, SHIPPED, cli, ld, profiel, schrijf  # noqa: E402

spec = importlib.util.spec_from_file_location("startscript_test", os.path.join(REPO, "scripts", "startscript.py"))
ss = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ss)

TWEE = ("<HasTwoExecutables>true</HasTwoExecutables><ExecutableName2>HULP.exe</ExecutableName2>"
        "<GamePath2>%s</GamePath2><LaunchSecondExecutableFirst>true</LaunchSecondExecutableFirst>")


class Basis(nep.Route):
    def setUp(self):
        super().setUp()
        self.view_goed()
        profiel(self.GAME, self.spel)

    def ini(self, tekst):
        schrijf(os.path.join(SHIPPED, self.GAME, "game.ini"), tekst)

    def bat(self, rel, tekst, codec="utf-8"):
        pad = os.path.join(self.view, rel)
        os.makedirs(os.path.dirname(pad), exist_ok=True)
        with open(pad, "wb") as f:
            f.write(tekst.replace("\n", "\r\n").encode(codec))
        return pad

    def plan_tekst(self):
        rc, out, err = cli(self.GAME, "--plan")
        self.assertEqual(rc, 0, err)
        return out


class OmgevingPerGame(Basis):
    """Punt 1: [Env] in game.ini."""

    def test_de_standaard_is_geen(self):
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        self.assertEqual(plan["game_env"], {})
        self.assertIn("omgevingsvariabelen voor het spel ([Env] in game.ini): geen", self.plan_tekst())

    def test_het_plan_laat_de_variabelen_zien(self):
        self.ini("[Env]\nGAMEID = SDCF\nIMAGE_ROOT_DIR = X:\\\nLAUNCH_ON_IMAGE = 1\n")
        _, plan, _ = self.plan_json()
        self.assertEqual(plan["game_env"], {"GAMEID": "SDCF", "IMAGE_ROOT_DIR": "X:\\", "LAUNCH_ON_IMAGE": "1"})
        self.assertIn("omgevingsvariabelen voor het spel ([Env] in game.ini): GAMEID=SDCF  IMAGE_ROOT_DIR=X:\\  LAUNCH_ON_IMAGE=1",
                      self.plan_tekst())

    def test_het_spelproces_krijgt_ze_ongeschonden(self):
        self.ini("[Env]\nGAMEID = SDCF\nIMAGE_ROOT_DIR = X:\\\nMet_Spatie = een twee \"drie\"\nkleineletters = Ja\n")
        rc, spel, _ = self.start()
        self.assertEqual(rc, 0, self.uitvoer.getvalue())
        self.assertEqual(spel.env.get("GAMEID"), "SDCF")
        self.assertEqual(spel.env.get("IMAGE_ROOT_DIR"), "X:\\")
        self.assertEqual(spel.env.get("Met_Spatie"), 'een twee "drie"')
        self.assertEqual(spel.env.get("kleineletters"), "Ja")          # de naam blijft zoals hij geschreven is
        self.assertNotIn("gameid", spel.env)

    def test_zonder_env_komt_er_niets_bij(self):
        rc, spel, _ = self.start()
        self.assertEqual(rc, 0)
        self.assertNotIn("GAMEID", spel.env)

    def test_beide_starts_krijgen_dezelfde_variabelen(self):
        schrijf(os.path.join(self.nas_spel, "hulp", "HULP.exe"), b"MZ" + b"\0" * 62, "wb")
        os.makedirs(os.path.join(self.view, "hulp"))
        hulp = os.path.join(self.view, "hulp", "HULP.exe")
        os.symlink(os.path.join(self.nas_spel, "hulp", "HULP.exe"), hulp)
        profiel(self.GAME, self.spel, extra=TWEE % hulp)
        self.ini("[Env]\nGAMEID = SDCF\n[Launch]\nSecondWait = 0\n")
        nep.NepProces.gedrag = lambda argv: {"poll": [None, 0]} if "SPEL.exe" in argv[-1] else {"poll": [None]}
        rc, _, nieuw = self.start()
        starts = [p for p in nieuw if "OpenParrotLoader" in " ".join(p.argv)]
        self.assertEqual(len(starts), 2, self.uitvoer.getvalue())
        self.assertEqual([p.env.get("GAMEID") for p in starts], ["SDCF", "SDCF"])


class HetStartscriptInHetPlan(Basis):
    """Punt 2 en 4: het script wordt gelezen, naast ons plan gelegd, en nooit uitgevoerd."""

    SCRIPT = ("@echo off\n\nsetlocal\n\nrem spel-ID\nset GAMEID=SDCF\n\nif not exist Y:\\%GAMEID% (\n    rmdir /S /Q Y:\\\n"
              "    mkdir Y:\\%GAMEID%\n)\n\nstart /wait sendFlash COM1 LMAIO.bin\n\n"
              "start /wait x:\\exe\\x64\\SPEL.exe -launch=\"cabinet\" -UserDir=\"C:\\log map\" -UNATTENDED\n")

    def test_wat_de_kast_doet_en_wij_niet(self):
        self.bat("game.bat", "\ufeff" + self.SCRIPT)            # met een BOM, zoals het echte bestand
        uit = self.plan_tekst()
        self.assertIn("eigen startscript van het spel: game.bat -- gelezen, NIET uitgevoerd", uit)
        self.assertIn("zet GAMEID=SDCF -- WIJ GEVEN DIE NIET MEE", uit)
        self.assertIn("sendFlash COM1 LMAIO.bin -- WIJ NIET", uit)
        self.assertIn('start het spel: SPEL.exe -launch="cabinet" -UserDir="C:\\log map" -UNATTENDED', uit)
        self.assertIn("argumenten die WIJ NIET meegeven: [-launch=cabinet]  [-UserDir=C:\\log map]  [-UNATTENDED]", uit)
        self.assertIn("gebruikt de schijfletters X:, Y:", uit)
        self.assertIn("kastding (daarom niet uitvoeren): rmdir /S /Q Y:\\ (voorwaardelijk)", uit)
        rc, plan, _ = self.plan_json()
        v = plan["start_script"]["compare"]
        self.assertEqual([e["name"] for e in v["env_missing"]], ["GAMEID"])
        self.assertEqual(v["args_missing"], ["-launch=cabinet", "-UserDir=C:\\log map", "-UNATTENDED"])
        self.assertEqual([r["exe"] for r in v["other_runs"]], ["sendFlash"])

    def test_met_de_gegevens_in_game_ini_is_het_gelijk(self):
        self.bat("game.bat", self.SCRIPT)
        self.ini('[Env]\nGAMEID = SDCF\n[Launch]\nArguments = -launch="cabinet" -UserDir="C:\\log map" -UNATTENDED\n')
        uit = self.plan_tekst()
        self.assertIn("zet GAMEID=SDCF -- wij ook", uit)
        self.assertIn("de argumenten zijn gelijk aan de onze", uit)
        self.assertNotIn("WIJ GEVEN DIE NIET MEE", uit)
        self.assertNotIn("argumenten die WIJ NIET meegeven", uit)
        self.assertIn("sendFlash COM1 LMAIO.bin -- WIJ NIET", uit)   # dat blijft: wij doen die stap niet

    def test_een_andere_waarde_wordt_genoemd(self):
        self.bat("game.bat", self.SCRIPT)
        self.ini("[Env]\nGAMEID = ANDERS\n")
        self.assertIn("zet GAMEID=SDCF -- wij geven ANDERS mee", self.plan_tekst())

    def test_het_script_wordt_nooit_uitgevoerd(self):
        self.bat("game.bat", self.SCRIPT)
        rc, spel, nieuw = self.start()
        self.assertEqual(rc, 0, self.uitvoer.getvalue())
        alles = " ".join(" ".join(p.argv) for p in nieuw)
        for woord in ("game.bat", "sendFlash", "rmdir", "cmd"):
            self.assertNotIn(woord, alles)
        self.assertEqual(spel.argv[-1], "Z:" + self.spel.replace("/", "\\"))      # onze eigen start, ongewijzigd
        self.assertNotIn("GAMEID", spel.env)                                      # en niets stil overgenomen

    def test_het_plan_verandert_niets_aan_de_start(self):
        """Het script lezen is informatie: argv en omgeving zijn met en zonder script gelijk."""
        _, zonder, _ = self.plan_json()
        self.bat("game.bat", self.SCRIPT)
        _, met, _ = self.plan_json()
        self.assertIsNone(zonder["start_script"])
        self.assertTrue(met["start_script"]["found"])
        self.assertEqual(zonder["argv"], met["argv"])
        self.assertEqual(zonder["game_env"], met["game_env"])
        self.assertEqual(zonder["starts"], met["starts"])

    def test_startscript_aanwijzen_en_uitzetten(self):
        self.bat("start/Eigen.bat", "set A=1\nstart SPEL.exe -x\n")
        self.bat("game.bat", self.SCRIPT)
        self.ini("[Launch]\nStartScript = start/Eigen.bat\n")
        self.assertIn("eigen startscript van het spel: start/Eigen.bat", self.plan_tekst())
        self.ini("[Launch]\nStartScript = none\n")
        self.assertNotIn("eigen startscript", self.plan_tekst())
        self.ini("[Launch]\nStartScript = start/Bestaat-niet.bat\n")
        self.assertIn("[Launch] StartScript = start/Bestaat-niet.bat -- NIET GEVONDEN", self.plan_tekst())

    def test_zonder_startscript_staat_er_niets(self):
        self.assertNotIn("eigen startscript", self.plan_tekst())


class DeLezer(unittest.TestCase):
    """Punt 3: wat de bestanden beslissen wordt beslist; wat van de kast afhangt heet voorwaardelijk."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="rg-startscript-")
        self.addCleanup(__import__("shutil").rmtree, self.tmp, True)

    def zet(self, rel, tekst=""):
        pad = os.path.join(self.tmp, rel)
        os.makedirs(os.path.dirname(pad), exist_ok=True)
        with open(pad, "w", newline="") as f:
            f.write(tekst.replace("\n", "\r\n"))
        return pad

    def test_een_keten_met_if_exist_else_en_goto(self):
        """De vorm van een echte kast: game.bat roept een tweede script aan, dat kiest de exe die er is."""
        self.zet("App/Game/Bin/Game-Shipping.exe")
        self.zet("App/Game/Daemon/daemon.exe")
        self.zet("App/Startup/Start.bat",
                 "set MODE=target\nset ID=SDET\npushd ..\\Game\ncall Run.bat %MODE%\npopd\n")
        self.zet("App/Game/Run.bat",
                 'if exist "..\\..\\Dev\\AsTarget" (\n  set DETAIL=target\n  goto Klaar\n)\n'
                 'if "%1"=="" (\n  set DETAIL=pc\n) else (\n  set DETAIL=cabinet\n)\n:Klaar\n'
                 "set DIR=Bin\n"
                 'if exist "%DIR%\\Game-Debug.exe" (\n  set EXE=%DIR%\\Game-Debug.exe\n) else if exist "%DIR%\\Game-Shipping.exe" (\n'
                 "  set EXE=%DIR%\\Game-Shipping.exe\n) else (\n  set EXE=%DIR%\\Game-Test.exe\n)\n"
                 'if "%MODE%"=="pc" goto Spel\n'
                 'start /min "" "Daemon\\daemon.exe" -c config.json\ntimeout 5\n:Spel\n'
                 'set ARGS=%ARGS% -launch="%DETAIL%"\n'
                 'if not %ERRORLEVEL%==0 set ARGS=%ARGS% -fout\n'
                 'start /wait "" "%EXE%" %ARGS% -UNATTENDED\n'
                 "taskkill /im daemon.exe\ngoto Einde\n:Fout\nstart Nooit.exe\n:Einde\n")
        script = self.zet("game.bat", 'set OP_IMAGE=1\nif "%ROOT%"=="" set ROOT=X:\\\npushd "%ROOT%\\App\\Startup"\ncall Start.bat\npopd\nexit /b 0\n')
        f = ss.lees(script, self.tmp)
        self.assertEqual(f["files"], ["game.bat", "App/Startup/Start.bat", "App/Game/Run.bat"])
        zet = {e["name"]: (e["value"], e["conditional"]) for e in f["env"]}
        self.assertEqual(zet["ROOT"], ("X:\\", False))                 # "%ROOT%"=="" is te beslissen: hij was leeg
        self.assertEqual(zet["DETAIL"], ("cabinet", False))            # AsTarget bestaat niet, %1 is "target": de else-tak
        self.assertEqual(zet["EXE"], ("Bin\\Game-Shipping.exe", False))    # de exe die er IS, niet de laatste in het script
        self.assertEqual([(r["exe"], r["args"], r["conditional"], r["wait"]) for r in f["runs"]],
                         [("daemon.exe", "-c config.json", False, False),
                          ("Game-Shipping.exe", '-launch="cabinet" -UNATTENDED', False, True)])
        self.assertEqual(f["runs"][1]["cwd"], "App/Game")
        self.assertEqual(f["runs"][1]["arglist"], ["-launch=cabinet", "-UNATTENDED"])
        self.assertNotIn("Nooit.exe", [r["exe"] for r in f["runs"]])       # na `goto Einde` komt het script daar niet
        self.assertEqual(f["drives"], ["X"])
        self.assertEqual([c["command"] for c in f["cabinet"]], ["taskkill /im daemon.exe"])
        # de errorlevel hangt van de kast af: die tak is voorwaardelijk, niet stil genomen of stil overgeslagen
        self.assertIn(("ARGS", True), [(e["name"], e["conditional"]) for e in f["env"]])

    def test_wat_van_de_kast_afhangt_is_voorwaardelijk(self):
        script = self.zet("game.bat", "probe.exe -s app.ini\nif %ERRORLEVEL% == 0 (\nstart /wait Spel.exe\n)\n"
                                      "if not exist Y:\\DATA (\n  rmdir /S /Q Y:\\\n)\n")
        f = ss.lees(script, self.tmp)
        self.assertEqual([(r["exe"], r["conditional"]) for r in f["runs"]], [("probe.exe", False), ("Spel.exe", True)])
        self.assertEqual([(c["command"], c["conditional"]) for c in f["cabinet"]], [("rmdir /S /Q Y:\\", True)])

    def test_een_shellscript(self):
        self.zet("pm/game")
        script = self.zet("utils/run_game.sh",
                          "#!/bin/sh\nexport DISPLAY=:0\nPMDIR=/pm\nUSERDIR=\"${PMDIR}/user\"\n/pm/utils/rtinit -ct\n"
                          "${PMDIR}/game -f1280x720 -e -mg\nERR=\"$?\"\ncase \"$ERR\" in\n  222) reboot;\n  240) rm -rf ${USERDIR};\nesac\n")
        f = ss.lees(script, self.tmp)
        self.assertEqual([(r["exe"], r["args"]) for r in f["runs"]], [("rtinit", "-ct"), ("game", "-f1280x720 -e -mg")])
        self.assertEqual(f["runs"][1]["path"], "pm/game")
        self.assertEqual([(e["name"], e.get("exported")) for e in f["env"]][:2], [("DISPLAY", True), ("PMDIR", False)])
        self.assertEqual([(c["command"], c["conditional"]) for c in f["cabinet"]], [("reboot", True), ("rm -rf /pm/user", True)])
        # alleen wat geexporteerd is bereikt het spel: PMDIR telt niet als "ontbreekt"
        v = ss.vergelijk(f, {"env": {}, "starts": [{"name": "game", "role": "main", "exe": os.path.join(self.tmp, "pm/game"),
                                                    "args": {"list": ["-f1280x720", "-e"]}}], "_script_root": self.tmp})
        self.assertEqual([e["name"] for e in v["env_missing"]], ["DISPLAY"])
        self.assertEqual(v["args_missing"], ["-mg"])
        self.assertEqual([(r["exe"], r["before_game"]) for r in v["other_runs"]], [("rtinit", True)])

    def test_een_variabele_voor_een_enkel_commando(self):
        """`NAAM=waarde commando args`: zo start een kastscript zijn spel. De variabele bereikt het spel, en het
        commando is een start -- niet een toekenning van de hele regel."""
        self.zet("pm/game2")
        script = self.zet("utils/run_game.sh",
                          "GAMEDIR=/pm\nSCREEN_RES=1920x1080\ncd /pm\n"
                          "LD_PRELOAD=/pm/hulp.so ${GAMEDIR}/game2 -f${SCREEN_RES} -n\n"
                          "A=1 B='twee woorden' /pm/utils/tool --x\n"
                          "XRANDR=`which xrandr`\nRES=$(cat /pm/res | awk '{ print $3 }')\n")
        f = ss.lees(script, self.tmp)
        self.assertEqual([(r["exe"], r["args"]) for r in f["runs"]], [("game2", "-f1920x1080 -n"), ("tool", "--x")])
        voor = [(e["name"], e["value"]) for e in f["env"] if e.get("for_command")]
        self.assertEqual(voor, [("LD_PRELOAD", "/pm/hulp.so"), ("A", "1"), ("B", "twee woorden")])
        gewoon = {e["name"]: e["value"] for e in f["env"] if not e.get("for_command")}
        self.assertEqual(gewoon.get("XRANDR"), "`which xrandr`")      # een toekenning met spaties in de waarde, geen commando
        self.assertTrue(gewoon.get("RES", "").startswith("$(cat"))
        self.assertEqual(f["runs"][0]["cwd"], "pm")
        # en in de vergelijking: de variabele telt als iets dat het spel bereikt
        v = ss.vergelijk(f, {"env": {}, "starts": [{"name": "game2", "role": "main", "exe": os.path.join(self.tmp, "pm/game2"),
                                                    "args": {"list": ["-n"]}}], "_script_root": self.tmp})
        self.assertIn("LD_PRELOAD", [e["name"] for e in v["env_missing"]])
        self.assertEqual(v["args_missing"], ["-f1920x1080"])

    def test_cp932_en_een_leeg_script(self):
        pad = os.path.join(self.tmp, "game.bat")
        with open(pad, "wb") as f:
            f.write("rem 各種変数を定義する\r\nset A=1\r\n".encode("cp932"))
        self.assertEqual([(e["name"], e["value"]) for e in ss.lees(pad, self.tmp)["env"]], [("A", "1")])
        leeg = self.zet("leeg.bat", "")
        f = ss.lees(leeg, self.tmp)
        self.assertEqual((f["env"], f["runs"]), ([], []))

    def test_het_script_komt_de_spelmap_niet_uit(self):
        """Een pad dat buiten de spelmap wijst wordt niet gevolgd en niet gelezen."""
        buiten = tempfile.mkdtemp(prefix="rg-buiten-")
        self.addCleanup(__import__("shutil").rmtree, buiten, True)
        with open(os.path.join(buiten, "Geheim.bat"), "w") as f:
            f.write("set LEK=1\n")
        script = self.zet("spel/game.bat", "call ..\\..\\%s\\Geheim.bat\n" % os.path.basename(buiten))
        f = ss.lees(script, os.path.join(self.tmp, "spel"))
        self.assertEqual(f["env"], [])
        self.assertEqual(len(f["unfollowed"]), 1)


if __name__ == "__main__":
    unittest.main()

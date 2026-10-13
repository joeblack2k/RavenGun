#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Een profiel met twee uitvoerbare bestanden (HasTwoExecutables): een spel met een los hulpprogramma ernaast.

De launcher weigerde zo'n profiel (exitcode 4). Nu start hij beide, in een vaste orde, met een instelbare
wachttijd ertussen, in dezelfde view, dezelfde Wine-prefix en dezelfde omgeving, en bewaart hij van elke start
apart de uitvoer en de exitcode. Deze test dwingt af:

1. `--plan` laat BEIDE starts zien, met de orde en de wachttijd, voordat er iets gestart wordt.
2. De uitvoering houdt die orde en die wachttijd aan, met dezelfde omgeving en werkmap voor beide.
3. Is de ene start al weg voordat de andere begint, dan wordt dat luid gemeld en stopt de run (EXIT_HALF) --
   geen halve opstelling. Een lader die eerder stopt dan zijn programma is GEEN dode start.
4. Een game met een enkele exe werkt precies zoals voorheen.

Er wordt niets gestart: tests/launcher_nep.py vervangt alleen het starten zelf.
"""
import glob
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launcher_nep as nep  # noqa: E402
from launcher_nep import NAS, OMGEVING, SHIPPED, TIJDLIJN, TP, VIEWS, WINE, NepProces, cli, ld, profiel, schrijf  # noqa: E402

STARTS_MAP = os.path.join(OMGEVING["XDG_STATE_HOME"], "ravengun", "starts")
TWEE = ("<HasTwoExecutables>true</HasTwoExecutables><ExecutableName2>HULP.exe</ExecutableName2>"
        "<GamePath2>%s</GamePath2><LaunchSecondExecutableFirst>%s</LaunchSecondExecutableFirst>"
        "<SecondExecutableArguments>%s</SecondExecutableArguments>")


def welke(argv):
    """'hulp', 'spel' of 'helper': welke start is dit?"""
    laatste = " ".join(argv)
    if "HULP.exe" in laatste:
        return "hulp"
    if "SPEL.exe" in laatste:
        return "spel"
    return "helper"


class TweeStartsBasis(nep.Route):
    def setUp(self):
        super().setUp()
        self.view_goed()
        # het hulpprogramma staat in een andere map van hetzelfde spel, en in dezelfde view
        schrijf(os.path.join(self.nas_spel, "hulp", "HULP.exe"), b"MZ" + b"\0" * 62, "wb")
        os.makedirs(os.path.join(self.view, "hulp"))
        os.symlink(os.path.join(self.nas_spel, "hulp", "HULP.exe"), os.path.join(self.view, "hulp", "HULP.exe"))
        self.hulp = os.path.join(self.view, "hulp", "HULP.exe")
        self.leeft = set()             # procesnamen die "nog leven" volgens de nep-cgroup
        ld.game_alive = lambda comms: bool(set(comms) & self.leeft)

    def twee(self, eerst="true", args="", pad2=None, ini=""):
        profiel(self.GAME, self.spel, extra=TWEE % (pad2 or self.hulp, eerst, args))
        if ini:
            schrijf(os.path.join(SHIPPED, self.GAME, "game.ini"), ini)

    def spel_starts(self, nieuw):
        return [p for p in nieuw if welke(p.argv) != "helper"]

    def starts_json(self):
        mappen = glob.glob(os.path.join(STARTS_MAP, self.GAME + "-*"))
        self.assertEqual(len(mappen), 1, "een map met de uitvoer per start: %r" % mappen)
        with open(os.path.join(mappen[0], "starts.json")) as f:
            return mappen[0], json.load(f)


class HetPlanLaatBeideStartsZien(TweeStartsBasis):
    def test_beide_starts_met_orde_en_wachttijd(self):
        self.twee(eerst="true", args="&quot;-f -c config.json&quot;")
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        s = plan["starts"]
        self.assertEqual([(x["name"], x["role"]) for x in s], [("HULP.exe", "second"), ("SPEL.exe", "main")])
        self.assertEqual([x["wait_before"] for x in s], [0.0, ld.DEFAULT_SECOND_WAIT])
        # dezelfde wine, dezelfde lader en dezelfde kern voor beide; alleen de exe en de argumenten verschillen
        self.assertEqual(s[0]["argv"][:3], s[1]["argv"][:3])
        self.assertEqual(s[0]["argv"][0], WINE)
        self.assertEqual(os.path.basename(s[0]["argv"][1]), "OpenParrotLoader64.exe")
        self.assertEqual(s[0]["argv"][3:], ["Z:" + self.hulp.replace("/", "\\"), "-f -c config.json"])
        self.assertEqual(s[1]["argv"][3:], ["Z:" + self.spel.replace("/", "\\")])
        self.assertEqual(s[1]["argv"], plan["argv"])                       # "argv" blijft de start van het spel
        self.assertEqual(s[0]["cwd"], s[1]["cwd"])                         # dezelfde werkmap
        self.assertEqual(plan["cwd"], s[1]["cwd"])

    def test_het_leesbare_plan(self):
        self.twee(ini="[Launch]\nSecondWait = 2.5\n")
        rc, out, err = cli(self.GAME, "--plan")
        self.assertEqual(rc, 0, err)
        regels = [r for r in out.splitlines() if r.startswith(("start ", "wachten: "))]
        self.assertEqual(len(regels), 3, out)
        self.assertTrue(regels[0].startswith("start 1/2: HULP.exe (tweede exe) -- "), regels[0])
        self.assertTrue(regels[1].startswith("wachten: 2.5 s"), regels[1])
        self.assertTrue(regels[2].startswith("start 2/2: SPEL.exe (het spel) -- "), regels[2])

    def test_de_orde_volgt_het_profiel(self):
        self.twee(eerst="false")
        _, plan, _ = self.plan_json()
        self.assertEqual([x["name"] for x in plan["starts"]], ["SPEL.exe", "HULP.exe"])
        self.assertEqual([x["wait_before"] for x in plan["starts"]], [0.0, ld.DEFAULT_SECOND_WAIT])

    def test_wachttijd_en_argumenten_uit_game_ini(self):
        self.twee(args="-oud", ini="[Launch]\nSecondWait = 0\nSecondArguments = -c a.json b.json\n")
        _, plan, _ = self.plan_json()
        self.assertEqual(plan["starts"][1]["wait_before"], 0.0)
        # een parameter: de lader neemt er precies een voor de exe en plakt hem letterlijk achter het pad
        self.assertEqual(plan["starts"][0]["argv"][4:], ["-c a.json b.json"])
        self.assertEqual(plan["starts"][0]["args"]["list"], ["-c", "a.json", "b.json"])

    def test_een_onzinnige_wachttijd_wordt_geweigerd(self):
        for waarde in ("vijf", "-1", "601"):
            self.twee(ini="[Launch]\nSecondWait = %s\n" % waarde)
            rc, out, err = cli(self.GAME, "--plan")
            self.assertEqual(rc, ld.EXIT_PATCH, waarde)
            self.assertIn("SecondWait", err)

    def test_teknoparrot_ini_komt_naast_beide_exes(self):
        self.twee()
        _, plan, _ = self.plan_json()
        paden = [f[0] for f in plan["files"]]
        self.assertEqual(paden, [os.path.join(os.path.dirname(self.spel), "teknoparrot.ini"),
                                 os.path.join(os.path.dirname(self.hulp), "teknoparrot.ini")])
        self.assertEqual(plan["files"][0][1], plan["files"][1][1])

    def test_de_tweede_exe_ontbreekt(self):
        self.twee(pad2=os.path.join(self.view, "hulp", "BESTAAT-NIET.exe"))
        rc, out, err = cli(self.GAME, "--plan")
        self.assertEqual(rc, ld.EXIT_UNSUPPORTED)
        self.assertIn("de tweede is niet gevonden", err)
        rc, _, nieuw = self.start()
        self.assertEqual((rc, nieuw), (ld.EXIT_UNSUPPORTED, []))


class DezelfdeView(TweeStartsBasis):
    """De tweede exe draait in DEZELFDE view als de eerste, ook als het profiel hem op de NAS aanwijst."""

    def test_een_pad_naar_de_nas_wordt_het_pad_in_de_view(self):
        """Zo staat het in een profiel van TeknoParrotUi: GamePath in de view, GamePath2 rechtstreeks naar de NAS."""
        self.twee(pad2=os.path.join(self.nas_spel, "hulp", "HULP.exe"))
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        tweede = plan["starts"][0]
        self.assertEqual(tweede["exe"], self.hulp)
        self.assertEqual(tweede["argv"][3], "Z:" + self.hulp.replace("/", "\\"))
        self.assertTrue(tweede["exe"].startswith(VIEWS + os.sep))

    def test_een_tweede_exe_die_niet_in_de_view_staat_wordt_geweigerd(self):
        elders = os.path.join(NAS, "AnderSpel", "HULP.exe")
        schrijf(elders, b"MZ" + b"\0" * 62, "wb")
        self.twee(pad2=elders)
        rc, out, err = cli(self.GAME, "--plan")
        self.assertEqual(rc, ld.EXIT_UNSUPPORTED)
        self.assertIn("buiten de view", err)
        self.assertEqual(cli(self.GAME, "--check")[0], ld.EXIT_UNSUPPORTED)
        rc, _, nieuw = self.start()
        self.assertEqual((rc, nieuw), (ld.EXIT_UNSUPPORTED, []))

    def test_het_pad_uit_identify_path2(self):
        """Het eigen profiel in game.ini kent geen GamePath2: het pad komt uit [Identify] Path2, in dezelfde spelmap."""
        instellingen = {"Identify": {"Path": "exe/x64/SPEL.exe", "Path2": "hulp/HULP.exe"}, "Launch": {}, "Game": {}}
        p = {"HasTwoExecutables": "true", "LaunchSecondExecutableFirst": "true"}
        uit = ld.second_start(p, instellingen, self.spel)
        self.assertEqual(uit["path"], self.hulp)
        self.assertTrue(uit["first"])


class DeUitvoering(TweeStartsBasis):
    def setUp(self):
        super().setUp()
        self.twee(ini="[Launch]\nSecondWait = 2.5\n")

    def gedrag(self, hulp=None, spel=None):
        standaard = {"hulp": {"poll": [None], "uit": [b"hulp: klaar om te dienen\n"]},
                     "spel": {"poll": [None, None, 3], "uit": [b"spel: gestart\n", b"spel: afgelopen\n"]}}
        if hulp is not None:
            standaard["hulp"] = hulp
        if spel is not None:
            standaard["spel"] = spel
        NepProces.gedrag = lambda argv: standaard.get(welke(argv))

    def test_orde_wachttijd_en_dezelfde_omgeving(self):
        self.gedrag()
        rc, _, nieuw = self.start()
        hulp, spel = self.spel_starts(nieuw)
        self.assertEqual((welke(hulp.argv), welke(spel.argv)), ("hulp", "spel"))
        # de orde in de tijd: het hulpprogramma, dan 2,5 s wachten, dan het spel
        orde = [(w, welke(x)) if w != "slaap" else (w, x) for w, x in TIJDLIJN if w == "slaap" or welke(x) != "helper"]
        i_hulp, i_spel = orde.index(("start", "hulp")), orde.index(("start", "spel"))
        self.assertLess(i_hulp, i_spel)
        self.assertAlmostEqual(sum(x for w, x in orde[i_hulp:i_spel] if w == "slaap"), 2.5)
        # dezelfde omgeving (dus dezelfde prefix en dezelfde sessie), dezelfde werkmap, dezelfde lader en kern
        self.assertEqual(hulp.env, spel.env)
        self.assertTrue(hulp.env.get("WINEPREFIX"))
        self.assertEqual(hulp.cwd, spel.cwd)
        self.assertEqual(hulp.argv[:3], spel.argv[:3])
        # de exitcode van de run is die van het spel
        self.assertEqual(rc, 3, self.uitvoer.getvalue())

    def test_uitvoer_en_exitcode_per_start_apart(self):
        self.gedrag()
        self.start()
        logboek = self.uitvoer.getvalue()
        self.assertIn("[HULP.exe] hulp: klaar om te dienen", logboek)       # in het logboek zie je wie wat zegt
        self.assertIn("[SPEL.exe] spel: gestart", logboek)
        self.assertIn("start 1/2: HULP.exe (tweede exe)", logboek)
        self.assertIn("wachten: 2.5 s", logboek)
        map_, verslag = self.starts_json()
        with open(os.path.join(map_, "1-HULP.exe.log")) as f:
            self.assertEqual(f.read(), "hulp: klaar om te dienen\n")
        with open(os.path.join(map_, "2-SPEL.exe.log")) as f:
            self.assertEqual(f.read(), "spel: gestart\nspel: afgelopen\n")
        eerste, tweede = verslag["starts"]
        self.assertEqual((eerste["name"], tweede["name"]), ("HULP.exe", "SPEL.exe"))
        self.assertEqual(tweede["exitcode"], 3)
        self.assertEqual(verslag["exitcode_run"], 3)
        self.assertEqual(eerste["output"], os.path.join(map_, "1-HULP.exe.log"))

    def test_na_het_spel_wordt_het_hulpprogramma_gestopt(self):
        self.gedrag()
        _, _, nieuw = self.start()
        hulp, spel = self.spel_starts(nieuw)
        self.assertTrue(hulp.gestopt, "het hulpprogramma bleef staan nadat het spel weg was")
        self.assertFalse(spel.gestopt)
        _, verslag = self.starts_json()
        self.assertEqual(verslag["starts"][0]["exitcode"], -15)
        self.assertIn("het spel is gestopt", verslag["starts"][0]["stopped_by_launcher"])

    # --- punt 3: geen halve opstelling ---

    def test_de_eerste_start_stopt_voor_de_tweede_begint(self):
        self.gedrag(hulp={"poll": [None, None, 1], "uit": [b"hulp: fout bij het openen\n"]})
        rc, _, nieuw = self.start()
        self.assertEqual(rc, ld.EXIT_HALF, self.uitvoer.getvalue())
        self.assertEqual([welke(p.argv) for p in self.spel_starts(nieuw)], ["hulp"], "het spel is toch gestart")
        logboek = self.uitvoer.getvalue()
        self.assertIn("FOUT: start 1/2 (HULP.exe) is GESTOPT (exitcode 1) voordat start 2/2 (SPEL.exe) begon", logboek)
        self.assertIn("halve opstelling", logboek)
        self.assertIn("Halve opstelling", self.fouten.getvalue())
        map_, verslag = self.starts_json()
        self.assertEqual(verslag["exitcode_run"], ld.EXIT_HALF)
        self.assertEqual(verslag["starts"][0]["exitcode"], 1)
        self.assertTrue(verslag["starts"][1]["not_started"])
        with open(os.path.join(map_, "1-HULP.exe.log")) as f:
            self.assertIn("fout bij het openen", f.read())               # je kunt nalezen waarom hij stopte
        self.assertFalse(os.path.exists(os.path.join(map_, "2-SPEL.exe.log")))

    def test_een_lader_die_eerder_stopt_dan_zijn_programma_is_geen_dode_start(self):
        """De lader stopt (exitcode 0) maar het programma dat hij startte leeft nog: gewoon doorgaan."""
        self.gedrag(hulp={"poll": [0]})
        self.leeft = {"hulp.exe"}
        rc, _, nieuw = self.start()
        self.assertEqual([welke(p.argv) for p in self.spel_starts(nieuw)], ["hulp", "spel"])
        self.assertEqual(rc, 3)
        self.assertNotIn("GESTOPT", self.uitvoer.getvalue())

    def test_een_lader_die_stopt_zonder_dat_er_een_programma_leeft_is_wel_een_dode_start(self):
        self.gedrag(hulp={"poll": [0]})
        rc, _, nieuw = self.start()
        self.assertEqual(rc, ld.EXIT_HALF)
        self.assertEqual([welke(p.argv) for p in self.spel_starts(nieuw)], ["hulp"])

    def test_andere_orde_het_spel_stopt_voor_het_hulpprogramma_begint(self):
        self.twee(eerst="false", ini="[Launch]\nSecondWait = 1\n")
        self.gedrag(spel={"poll": [None, 9]})
        rc, _, nieuw = self.start()
        self.assertEqual(rc, ld.EXIT_HALF)
        self.assertEqual([welke(p.argv) for p in self.spel_starts(nieuw)], ["spel"])
        self.assertIn("FOUT: start 1/2 (SPEL.exe) is GESTOPT (exitcode 9) voordat start 2/2 (HULP.exe) begon",
                      self.uitvoer.getvalue())

    def test_het_hulpprogramma_stopt_tijdens_het_spel_staat_in_het_logboek(self):
        self.twee(ini="[Launch]\nSecondWait = 0\n")
        self.gedrag(hulp={"poll": [None, None, 2]}, spel={"poll": [None] * 6 + [0]})
        rc, _, _ = self.start()
        self.assertEqual(rc, 0)
        self.assertIn("LET OP: start 1/2 (HULP.exe) is gestopt (exitcode 2) terwijl het spel nog draait",
                      self.uitvoer.getvalue())

    def test_drie_starts_in_orde(self):
        """De motor kent geen 'twee': hij loopt een lijst af. Een derde start gaat op dezelfde manier."""
        argv = [WINE, os.path.join(TP, "OpenParrotx64/OpenParrotLoader64.exe"), "Z:\\kern"]

        def s(naam, rol, wacht):
            return {"name": naam, "role": rol, "exe": "/x/" + naam, "argv": argv + ["Z:\\x\\" + naam], "cwd": TP,
                    "gamescope": False, "comms": [naam.lower()[:15]], "wait_before": wacht}
        plan = {"profile": self.GAME, "launch_env": {}, "starts": [s("A.exe", "second", 0.0), s("B.exe", "second", 1.0),
                                                                    s("SPEL.exe", "main", 0.5)]}
        NepProces.gedrag = lambda a: {"poll": [None, None, 0]} if "SPEL.exe" in a[-1] else {"poll": [None]}
        import contextlib
        import io
        uit = io.StringIO()
        with contextlib.redirect_stdout(uit):
            rc = ld.run_starts(plan, {"WINEPREFIX": "/p"}, nep._NepA11(self.GAME))
        self.assertEqual(rc, 0)
        orde = [x[-1].rsplit("\\", 1)[-1] for w, x in TIJDLIJN if w == "start"]
        self.assertEqual(orde, ["A.exe", "B.exe", "SPEL.exe"])
        gestopt = [x[-1].rsplit("\\", 1)[-1] for w, x in TIJDLIJN if w == "stop"]
        self.assertEqual(sorted(gestopt), ["A.exe", "B.exe"])


class EenExeBlijftZoalsHetWas(nep.Route):
    """Punt 4: een game met een enkele exe gaat precies de oude weg."""

    def setUp(self):
        super().setUp()
        self.view_goed()
        profiel(self.GAME, self.spel)
        NepProces.gedrag = lambda argv: {"poll": [None, 4], "uit": [b"een regel van het spel\n"]} if welke(argv) == "spel" else None

    def test_het_plan_heeft_een_start_en_die_is_argv(self):
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        self.assertEqual(len(plan["starts"]), 1)
        enige = plan["starts"][0]
        self.assertEqual((enige["name"], enige["role"], enige["wait_before"]), ("SPEL.exe", "main", 0.0))
        self.assertEqual(enige["argv"], plan["argv"])
        self.assertEqual(plan["argv"], [WINE, os.path.join(TP, "OpenParrotx64/OpenParrotLoader64.exe"),
                                        "Z:" + os.path.join(TP, "TeknoParrot/TeknoParrot64").replace("/", "\\"),
                                        "Z:" + self.spel.replace("/", "\\")])
        self.assertEqual([f[0] for f in plan["files"]], [os.path.join(os.path.dirname(self.spel), "teknoparrot.ini")])

    def test_het_leesbare_plan_heeft_geen_startregels(self):
        rc, out, _ = cli(self.GAME, "--plan")
        self.assertEqual(rc, 0)
        self.assertEqual([r for r in out.splitlines() if r.startswith(("start ", "wachten: "))], [])

    def test_de_uitvoering_is_de_oude(self):
        rc, spel, nieuw = self.start()
        self.assertEqual(rc, 4)                                          # de exitcode van het ene proces
        self.assertEqual([welke(p.argv) for p in nieuw], ["helper", "spel"])
        logboek = self.uitvoer.getvalue()
        self.assertIn("start: cwd=" + TP, logboek)                      # de oude logregels, ongewijzigd
        self.assertIn("] start: " + WINE, logboek)
        self.assertIn("] omgeving: ", logboek)
        self.assertIn("\neen regel van het spel\n", logboek)            # de uitvoer zonder naam ervoor
        self.assertNotIn("[SPEL.exe]", logboek)
        self.assertNotIn("start 1/", logboek)
        self.assertNotIn("wachten:", logboek)
        self.assertFalse(os.path.exists(STARTS_MAP), "een enkele exe schrijft geen map met starts")
        self.assertFalse(spel.gestopt)
        self.assertNotIn(("slaap", ld.START_STAP), TIJDLIJN)             # geen wachtlus van de nieuwe motor

    def test_has_two_executables_false_is_een_gewone_game(self):
        profiel(self.GAME, self.spel, extra="<HasTwoExecutables>false</HasTwoExecutables><ExecutableName2>x.exe</ExecutableName2>")
        rc, plan, err = self.plan_json()
        self.assertEqual(rc, 0, err)
        self.assertEqual(len(plan["starts"]), 1)


if __name__ == "__main__":
    unittest.main()

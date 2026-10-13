#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""De launcher zegt zelf welke launcher hij is: zijn pad, zijn sha256, en of het de versie uit de repo of de
geinstalleerde is -- in `--plan` en bovenaan elk run-logboek.

Drie metingen in een week gingen verloren aan een oude kopie waarvan niemand wist dat hij draaide (een oude lader,
een oude drop-in-DLL, een oude geinstalleerde launcher). Wat hier wordt afgedwongen:

1. De sha256 die het plan noemt IS die van het bestand dat draaide (nagerekend), ook voor de hulpmodules.
2. De soort klopt: repo, geinstalleerd (met het versienummer) of een losse kopie.
3. Vanuit de repo zegt het plan of de geinstalleerde launcher dezelfde is of een andere.
4. Het staat bovenaan het logboek van elke run, voor alles wat er gestart wordt.
5. Het kan de start nooit laten omvallen (geen git, geen VERSION-bestand).

Er wordt niets gestart (tests/launcher_nep.py).
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import launcher_nep as nep  # noqa: E402
from launcher_nep import HOME, OMGEVING, REPO, SCRIPT, TMP, TP, cli, ld, profiel  # noqa: E402

SHA = "[0-9a-f]{64}"


def sha(pad):
    with open(pad, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def kopie_van_de_scripts(doel):
    """Een kopie van scripts/ (alleen de Python-bestanden: de launcher en zijn hulpmodules)."""
    os.makedirs(doel)
    for n in os.listdir(os.path.join(REPO, "scripts")):
        if n.endswith(".py"):
            shutil.copy(os.path.join(REPO, "scripts", n), os.path.join(doel, n))
    return os.path.join(doel, "launch-direct.py")


def plan_van(script, *extra, json_uit=True, pad=None):
    e = dict(os.environ, **OMGEVING)
    if pad is not None:
        e["PATH"] = pad
    args = [sys.executable, "-I", script, nep.Route.GAME, "--plan", "--tp", TP, *extra] + (["--json"] if json_uit else [])
    p = subprocess.run(args, capture_output=True, text=True, env=e)
    return p.returncode, (json.loads(p.stdout) if json_uit and p.returncode == 0 else p.stdout), p.stderr


class Basis(nep.Route):
    def setUp(self):
        super().setUp()
        self.view_goed()
        profiel(self.GAME, self.spel)
        self.inst = os.path.join(HOME, ".local/opt/ravengun")
        shutil.rmtree(self.inst, ignore_errors=True)
        for d in ("installatie", "los"):
            shutil.rmtree(os.path.join(TMP, d), ignore_errors=True)


class UitDeRepo(Basis):
    def test_het_plan_noemt_pad_en_de_echte_sha256(self):
        rc, plan, err = plan_van(SCRIPT)
        self.assertEqual(rc, 0, err)
        li = plan["launcher"]
        self.assertEqual(li["path"], os.path.realpath(SCRIPT))
        self.assertEqual(li["sha256"], sha(SCRIPT))                      # nagerekend: de sha van het bestand dat draaide
        self.assertEqual(li["kind"], "repo")
        self.assertIsNone(li["installed"])                               # er is geen installatie om mee te vergelijken
        self.assertEqual(list(plan)[0], "launcher")                      # het eerste wat je in het plan ziet

    def test_de_hulpmodules_horen_erbij(self):
        _, plan, _ = plan_van(SCRIPT)
        li = plan["launcher"]
        namen = [m["name"] for m in li["modules"]]
        for n in ("gamesettings.py", "ownprofile.py", "patchfile.py", "startscript.py"):
            self.assertIn(n, namen)
        for m in li["modules"]:
            self.assertEqual(m["sha256"], sha(os.path.join(REPO, "scripts", m["name"])), m["name"])
        samen = hashlib.sha256("".join("%s:%s\n" % (m["name"], m["sha256"]) for m in li["modules"]).encode()).hexdigest()
        self.assertEqual(li["modules_sha256"], samen)

    def test_het_leesbare_plan_begint_ermee(self):
        rc, out, err = plan_van(SCRIPT, json_uit=False)
        self.assertEqual(rc, 0, err)
        regels = out.splitlines()
        self.assertEqual(regels[0], "launcher: " + os.path.realpath(SCRIPT))
        self.assertIn("de versie uit de REPO", regels[1])
        self.assertIn("sha256 " + sha(SCRIPT), regels[1])
        self.assertRegex(regels[1], "hulpmodules \\(\\d+\\) sha256 " + SHA)
        self.assertNotIn("launcher: {", out)                             # niet nog eens als ruwe gegevens

    def test_zonder_git_werkt_het_ook(self):
        leeg = os.path.join(TMP, "leeg-pad")
        os.makedirs(leeg, exist_ok=True)
        rc, plan, err = plan_van(SCRIPT, pad=leeg)
        self.assertEqual(rc, 0, err)
        li = plan["launcher"]
        self.assertEqual((li["kind"], li["detail"], li["modified"], li["sha256"]), ("repo", "", None, sha(SCRIPT)))


class DeGeinstalleerdeVersie(Basis):
    def installeer(self, versie="v9.9.9-proef\n"):
        prefix = os.path.join(TMP, "installatie")
        os.makedirs(os.path.join(prefix, "bin"))
        script = kopie_van_de_scripts(os.path.join(prefix, "share", "ravengun", "scripts"))
        if versie is not None:
            with open(os.path.join(prefix, "share", "ravengun", "VERSION"), "w") as f:
                f.write(versie)
        return script

    def test_de_installatie_noemt_zichzelf_geinstalleerd_met_het_versienummer(self):
        script = self.installeer()
        rc, plan, err = plan_van(script)
        self.assertEqual(rc, 0, err)
        li = plan["launcher"]
        self.assertEqual((li["kind"], li["detail"], li["path"], li["sha256"]), ("geinstalleerd", "v9.9.9-proef", script, sha(script)))
        rc, out, _ = plan_van(script, json_uit=False)
        self.assertEqual(out.splitlines()[0], "launcher: " + script)
        self.assertIn("de GEINSTALLEERDE versie (v9.9.9-proef); sha256 " + sha(script), out.splitlines()[1])
        self.assertNotIn("REPO", out.splitlines()[1])

    def test_zonder_versiebestand_blijft_het_geinstalleerd(self):
        script = self.installeer(versie=None)
        rc, plan, err = plan_van(script)
        self.assertEqual(rc, 0, err)
        self.assertEqual((plan["launcher"]["kind"], plan["launcher"]["detail"]), ("geinstalleerd", ""))

    def test_een_oude_installatie_heeft_een_andere_sha256(self):
        """De valkuil zelf: de installatie loopt achter op de repo. De twee plannen noemen elk hun eigen sha256."""
        script = self.installeer()
        with open(script, "a") as f:
            f.write("\n# een oudere of nieuwere regel\n")
        _, oud, _ = plan_van(script)
        _, nieuw, _ = plan_van(SCRIPT)
        self.assertNotEqual(oud["launcher"]["sha256"], nieuw["launcher"]["sha256"])
        self.assertEqual(oud["launcher"]["sha256"], sha(script))

    def test_een_losse_kopie_is_geen_repo_en_geen_installatie(self):
        script = kopie_van_de_scripts(os.path.join(TMP, "los", "x", "scripts"))
        rc, plan, err = plan_van(script)
        self.assertEqual(rc, 0, err)
        self.assertEqual(plan["launcher"]["kind"], "onbekend")
        rc, out, _ = plan_van(script, json_uit=False)
        self.assertIn("een losse kopie (geen repo, geen installatie)", out.splitlines()[1])


class RepoNaastDeInstallatie(Basis):
    """Vanuit de repo: is de geinstalleerde launcher dezelfde?"""

    def zet_installatie(self, anders):
        d = os.path.join(self.inst, "share/ravengun/scripts")
        os.makedirs(d)
        doel = os.path.join(d, "launch-direct.py")
        shutil.copy(SCRIPT, doel)
        if anders:
            with open(doel, "a") as f:
                f.write("\n# de installatie loopt achter\n")
        return doel

    def test_een_andere_installatie_wordt_genoemd(self):
        doel = self.zet_installatie(anders=True)
        rc, plan, err = plan_van(SCRIPT)
        self.assertEqual(rc, 0, err)
        self.assertEqual(plan["launcher"]["installed"], {"path": doel, "sha256": sha(doel), "same": False})
        rc, out, _ = plan_van(SCRIPT, json_uit=False)
        self.assertIn("de geinstalleerde launcher (%s) is een ANDERE versie (sha256 %s)" % (doel, sha(doel)), out.splitlines()[2])

    def test_een_gelijke_installatie_ook(self):
        doel = self.zet_installatie(anders=False)
        rc, out, err = plan_van(SCRIPT, json_uit=False)
        self.assertEqual(rc, 0, err)
        self.assertEqual(out.splitlines()[2], "  de geinstalleerde launcher (%s) is dezelfde" % doel)

    def test_zonder_installatie_staat_er_geen_regel(self):
        rc, out, _ = plan_van(SCRIPT, json_uit=False)
        self.assertNotIn("de geinstalleerde launcher", out)


class BovenaanHetLogboek(Basis):
    def test_de_eerste_regels_van_een_run(self):
        rc, spel, _ = self.start()
        self.assertEqual(rc, 0, self.uitvoer.getvalue())
        regels = self.uitvoer.getvalue().splitlines()
        self.assertTrue(regels[0].endswith("launcher: " + os.path.realpath(SCRIPT)), regels[0])
        self.assertIn("sha256 " + sha(SCRIPT), regels[1])
        self.assertIn("de versie uit de REPO", regels[1])
        eerste_start = next(i for i, r in enumerate(regels) if "] start: " in r or "] pipehelper: " in r)
        self.assertGreater(eerste_start, 1)                              # voor alles wat er gestart wordt

    def test_ook_bij_een_run_met_schijfletters(self):
        prefix = ld.SHARED_PREFIX
        shutil.rmtree(prefix, ignore_errors=True)
        os.makedirs(os.path.join(prefix, "dosdevices"))
        rc, _, _ = self.start("--exe", "x:\\exe\\x64\\SPEL.exe", "--drive", "X=.")
        self.assertEqual(rc, 0, self.uitvoer.getvalue())
        regels = self.uitvoer.getvalue().splitlines()
        self.assertTrue(regels[0].endswith("launcher: " + os.path.realpath(SCRIPT)), regels[0])
        self.assertEqual(sum(1 for r in regels if "] launcher: " in r), 1)       # een keer, niet dubbel

    def test_wie_de_launcher_is_laat_de_start_niet_omvallen(self):
        """In deze test is subprocess vervangen: git kan niet draaien. De run gaat gewoon door."""
        rc, spel, _ = self.start()
        self.assertEqual(rc, 0)
        self.assertIsNotNone(spel)


if __name__ == "__main__":
    unittest.main()

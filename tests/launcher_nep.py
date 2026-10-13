#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Een nep-wereld om scripts/launch-direct.py door de voordeur te testen zonder iets te starten.

Gedeeld door tests/test_dropin_preload_route.py en tests/test_twee_starts.py. Bij het laden van deze module
ontstaat een tijdelijke thuismap met een nep-runtime, een nep-NAS onder ~/mnt en een views-map; de launcher
wordt daarna geladen (hij leest HOME bij het laden). De klasse Route vervangt in de launcher alleen het starten
zelf: subprocess.Popen, het gedeelde geheugen, de JVS-pipe, wachten en signalen. Al het andere is de echte code.
"""
import atexit
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCRIPT = os.path.join(REPO, "scripts", "launch-direct.py")

# De launcher leest HOME bij het laden (ld.HOME, de regel "onder ~/mnt is de NAS"). Daarom staat de
# nep-thuismap er al voordat de module geladen wordt, net als in tests/launch_direct_test.sh.
TMP = tempfile.mkdtemp(prefix="rg-preloadroute-")
atexit.register(shutil.rmtree, TMP, True)
HOME = os.path.join(TMP, "home")
DATA = os.path.join(HOME, ".local", "share")
OMGEVING = {"HOME": HOME, "XDG_DATA_HOME": DATA, "XDG_STATE_HOME": os.path.join(HOME, ".local", "state"),
            "RAVENGUN_PATCHES_DIR": os.path.join(TMP, "patches")}
for _k in ("RAVENGUN_USER_PATCHES_DIR", "RAVENGUN_VIEWS_DIR", "RAVENGUN_TP_DIR", "TP_PRELOAD_DLL", "RG_DROPIN_PRELOAD"):
    os.environ.pop(_k, None)
os.environ.update(OMGEVING)
os.makedirs(HOME)


def laad_launcher():
    spec = importlib.util.spec_from_file_location("launch_direct_preloadroute", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    sys.modules["launch_direct_preloadroute"] = mod
    spec.loader.exec_module(mod)
    return mod


ld = laad_launcher()

TP = os.path.join(TMP, "tp")
SHIPPED = OMGEVING["RAVENGUN_PATCHES_DIR"]
VIEWS = os.path.join(DATA, "ravengun", "runtime", "views")
NAS = os.path.join(HOME, "mnt", "nas")            # onder ~/mnt: voor de launcher is dit de NAS
WINE = os.path.join(TMP, "wine", "bin", "wine")
WINE_MERK = os.path.join(TMP, "wine-is-gestart")  # de nep-wine schrijft dit als hij ooit uitgevoerd wordt
SHM_BRON = os.path.join(TMP, "shm-bron")


def schrijf(pad, data="x", mode="w"):
    os.makedirs(os.path.dirname(pad), exist_ok=True)
    with open(pad, mode) as f:
        f.write(data)


def boom(wortel):
    """Alles onder `wortel` (mappen, bestanden, symlinks) als gesorteerde lijst met inhoud of linkdoel."""
    uit = []
    for d, mappen, bestanden in os.walk(wortel):
        for n in sorted(mappen + bestanden):
            p = os.path.join(d, n)
            rel = os.path.relpath(p, wortel)
            if os.path.islink(p):
                uit.append((rel, "link", os.readlink(p)))
            elif os.path.isdir(p):
                uit.append((rel, "map", ""))
            else:
                with open(p, "rb") as f:
                    uit.append((rel, "bestand", f.read()))
    return sorted(uit)


def nep_runtime():
    for rel in ("OpenParrotx64/OpenParrotLoader64.exe", "OpenParrotWin32/OpenParrotLoader.exe",
                "ElfLdr2/BudgieLoader.exe", "ElfLdr2/x64/BudgieLoader_x64.exe",
                "TeknoParrot/TeknoParrot64.dll", "TeknoParrot/TeknoParrot.dll", "pipehelper.exe"):
        schrijf(os.path.join(TP, rel))
    schrijf(os.path.join(TP, "ParrotData.xml"),
            "<ParrotData><ExitGameKey>0x1B</ExitGameKey><PauseGameKey>0x13</PauseGameKey>"
            "<Elfldr2LogToFile>false</Elfldr2LogToFile></ParrotData>")
    schrijf(WINE, "#!/bin/sh\necho gestart >> '%s'\n" % WINE_MERK)
    os.chmod(WINE, 0o755)
    schrijf(SHM_BRON, "\0" * 64)


def profiel(naam, spel, emu_type="TeknoParrot", is64="true", extra=""):
    xml = ('<?xml version="1.0"?><GameProfile><GamePath>%s</GamePath><EmulationProfile>Proef</EmulationProfile>'
           '<GameProfileRevision>3</GameProfileRevision><EmulatorType>%s</EmulatorType><Is64Bit>%s</Is64Bit>'
           '<ExecutableName>%s</ExecutableName><WineRunnerPath>%s</WineRunnerPath>%s<ConfigValues></ConfigValues>'
           '</GameProfile>' % (spel, emu_type, is64, os.path.basename(spel), WINE, extra))
    schrijf(os.path.join(TP, "UserProfiles", naam + ".xml"), xml)
    schrijf(os.path.join(TP, "GameProfiles", naam + ".xml"),
            "<GameProfile><GameProfileRevision>3</GameProfileRevision><ResetHint>false</ResetHint></GameProfile>")


def dropin(naam, ini, *bestanden):
    d = os.path.join(SHIPPED, naam, "dropin")
    shutil.rmtree(d, ignore_errors=True)
    schrijf(os.path.join(d, "dropin.ini"), ini)
    for b in bestanden:
        schrijf(os.path.join(d, b), "drop-in " + b)


def cli(*args):
    """De echte opdrachtregel, in een eigen proces. Alleen --plan en --check: die starten niets."""
    assert "--run" not in args, "--run gaat in deze test nooit door de echte opdrachtregel"
    e = dict(os.environ, **OMGEVING)
    p = subprocess.run([sys.executable, "-I", SCRIPT, *args, "--tp", TP], capture_output=True, text=True, env=e)
    return p.returncode, p.stdout, p.stderr


TIJDLIJN = []      # wat er in welke orde gebeurde: ("start", argv), ("slaap", seconden), ("stop", argv)


class NepProces:
    """Staat in voor subprocess.Popen: onthoudt waarmee er gestart ZOU worden en start niets.

    `gedrag` (een functie argv -> dict of None) bepaalt per start wat het nep-proces doet:
      uit   regels uitvoer (bytes) die het proces schrijft
      poll  de antwoorden van poll() op rij; het laatste blijft gelden (None = loopt nog)."""
    gestart = []
    gedrag = None
    _pid = 40000

    def __init__(self, argv, cwd=None, env=None, **kw):
        self.argv, self.cwd, self.env = list(argv), cwd, dict(env or {})
        g = (NepProces.gedrag(self.argv) if NepProces.gedrag else None) or {}
        self._poll = list(g.get("poll", [0]))
        self.gestopt = False
        NepProces._pid += 1
        self.pid = NepProces._pid
        TIJDLIJN.append(("start", self.argv))
        # Wat er op dat moment op schijf staat: het bestand moet er zijn VOOR het spel start.
        doel = self.env.get("RG_TEST_VERWACHT_BESTAND", "")
        self.bestand_stond_er = bool(doel) and os.path.isfile(doel) and not os.path.islink(doel)
        self.stderr = iter(["pipehelper: pipe TeknoParrot_JVS created, waiting for game...\n"])
        self.stdout = iter(list(g.get("uit", [])))
        NepProces.gestart.append(self)

    def poll(self):
        return self._poll.pop(0) if len(self._poll) > 1 else self._poll[0]

    def wait(self, timeout=None):
        return self.poll()

    def terminate(self):
        self.gestopt = True
        self._poll = [-15]
        TIJDLIJN.append(("stop", self.argv))


class _NepSubprocess:
    """subprocess, maar zonder iets te starten."""
    PIPE, DEVNULL, STDOUT = subprocess.PIPE, subprocess.DEVNULL, subprocess.STDOUT
    Popen = NepProces

    @staticmethod
    def call(*a, **kw):
        raise AssertionError("subprocess.call in een test: %r" % (a,))

    run = check_output = check_call = call


class _NepPipe:
    port = 1

    def __init__(self, **kw):
        pass

    def start(self):
        pass

    def close(self):
        pass


class _NepA11:
    def __init__(self, profile):
        pass

    def feed(self, line):
        pass

    def write(self, extra_files=()):
        pass


class _NepTijd:
    """time, maar zonder te wachten."""
    def __getattr__(self, naam):
        import time
        return getattr(time, naam)

    @staticmethod
    def sleep(s):
        TIJDLIJN.append(("slaap", s))


class _NepSignaal:
    SIGTERM, SIGINT, SIGKILL = 15, 2, 9

    @staticmethod
    def signal(_nr, _handler):
        pass


class Route(unittest.TestCase):
    """Basis: een nep-runtime, een nep-NAS onder ~/mnt en een view, en een launcher die niets kan starten."""
    GAME = "ProefSpel"

    @classmethod
    def setUpClass(cls):
        nep_runtime()

    def setUp(self):
        for d in (VIEWS, NAS, SHIPPED, os.path.join(TMP, "los"), os.path.join(TMP, "elders")):
            shutil.rmtree(d, ignore_errors=True)
        if os.path.exists(WINE_MERK):
            os.unlink(WINE_MERK)
        # de nep-NAS: <NAS>/ProefSpel/exe/x64/SPEL.exe plus wat het spel er zelf heeft staan
        self.nas_spel = os.path.join(NAS, self.GAME)
        schrijf(os.path.join(self.nas_spel, "exe", "x64", "SPEL.exe"), b"MZ" + b"\0" * 62, "wb")
        schrijf(os.path.join(self.nas_spel, "exe", "x64", "data.bin"), "van het spel")
        schrijf(os.path.join(self.nas_spel, "exe", "leesmij.txt"), "nas")
        schrijf(os.path.join(self.nas_spel, "Ram", "save.bin"), "save")
        self.view = os.path.join(VIEWS, self.GAME)
        self.spel = os.path.join(self.view, "exe", "x64", "SPEL.exe")
        self.gestart_voor = len(NepProces.gestart)
        NepProces.gedrag = None
        del TIJDLIJN[:]
        shutil.rmtree(os.path.join(OMGEVING["XDG_STATE_HOME"], "ravengun", "starts"), ignore_errors=True)
        self.shm_geraakt = []
        self._bewaard = {n: getattr(ld, n) for n in
                         ("subprocess", "time", "signal", "JvsPipeServer", "A11Collector", "ensure_shm",
                          "set_connect_flag", "game_alive", "own_cgroup_pids")}
        ld.subprocess, ld.time, ld.signal = _NepSubprocess, _NepTijd(), _NepSignaal
        ld.JvsPipeServer, ld.A11Collector = _NepPipe, _NepA11
        ld.ensure_shm = lambda: self.shm_geraakt.append("ensure_shm")
        ld.set_connect_flag = lambda v: self.shm_geraakt.append("set_connect_flag")
        ld.game_alive = lambda comms: False
        ld.own_cgroup_pids = lambda: []          # een test stuurt nooit een signaal naar een echt proces

    def tearDown(self):
        for n, v in self._bewaard.items():
            setattr(ld, n, v)

    # --- de soorten view ---

    def view_goed(self):
        """Zoals de view-maker hem nu maakt: echte mappen tot en met de map van de exe, symlinks voor de inhoud."""
        os.makedirs(os.path.join(self.view, "exe", "x64"))
        for n in ("SPEL.exe", "data.bin"):
            os.symlink(os.path.join(self.nas_spel, "exe", "x64", n), os.path.join(self.view, "exe", "x64", n))
        os.symlink(os.path.join(self.nas_spel, "exe", "leesmij.txt"), os.path.join(self.view, "exe", "leesmij.txt"))
        os.symlink(os.path.join(self.nas_spel, "Ram"), os.path.join(self.view, "Ram"))

    def view_exe_is_symlink(self):
        """Luigi's Mansion zoals het was: views/<Spel>/exe is een symlink naar de NAS, de game staat in exe/x64."""
        os.makedirs(self.view)
        os.symlink(os.path.join(self.nas_spel, "exe"), os.path.join(self.view, "exe"))
        os.symlink(os.path.join(self.nas_spel, "Ram"), os.path.join(self.view, "Ram"))

    def view_base_is_symlink(self):
        """De gamemap ZELF is een symlink naar buiten: views/<Spel>/exe is echt, exe/x64 wijst naar de NAS."""
        os.makedirs(os.path.join(self.view, "exe"))
        os.symlink(os.path.join(self.nas_spel, "exe", "x64"), os.path.join(self.view, "exe", "x64"))

    def view_is_zelf_symlink(self):
        """De hele view is een symlink naar de NAS: er is niets lokaals om het bestand in te zetten."""
        os.makedirs(VIEWS, exist_ok=True)
        os.symlink(self.nas_spel, self.view)

    # --- starten zonder te starten ---

    def plan_json(self, *extra):
        rc, out, err = cli(self.GAME, "--plan", "--json", *extra)
        try:
            return rc, json.loads(out), err
        except ValueError:
            self.fail("--plan --json gaf geen JSON (exit %d): %s%s" % (rc, out, err))

    def start(self, *extra):
        """main(--run) in dit proces, met het starten zelf vervangen. Geeft (exitcode, het nep-spelproces of None)."""
        import contextlib
        import io
        self.uitvoer, self.fouten = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(self.uitvoer), contextlib.redirect_stderr(self.fouten):
            rc = ld.main([self.GAME, "--run", "--tp", TP, "--shm-source", SHM_BRON, *extra])
        nieuw = NepProces.gestart[self.gestart_voor:]
        spel = [p for p in nieuw if any("OpenParrotLoader" in a or "BudgieLoader" in a for a in p.argv)]
        return rc, (spel[0] if spel else None), nieuw

    def assert_niets_gestart(self, rc, nieuw):
        self.assertEqual(rc, ld.EXIT_PATCH, "de start moet geweigerd worden met EXIT_PATCH\n" + self.uitvoer.getvalue())
        self.assertEqual(nieuw, [], "er is toch iets gestart: %r" % [p.argv for p in nieuw])
        self.assertEqual(self.shm_geraakt, [], "het gedeelde geheugen is aangeraakt voor de weigering")
        self.assertFalse(os.path.exists(WINE_MERK), "wine is uitgevoerd")

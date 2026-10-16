#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests voor de geheugen-motor van RavenGun (memory/src/, docs/memory-engine.md).

De motor past bytepatches toe in een LOPEND proces. Deze tests bouwen de motor, starten een
proefprogramma met een bekend patroon op een bekend adres, en kijken of de bytes inderdaad
veranderen. Ze dekken:

  * een patch in het programma zelf, via LD_PRELOAD (Linux);
  * een patch in een functie (mov eax,1 ; ret);
  * een niet-PIE programma, waarbij de modulebasis 0 is;
  * een bibliotheek die PAS LATER geladen wordt: zowel via de dlopen-onderschepping als via de
    achtergrondthread;
  * een `verwacht` dat niet klopt: er mag NIETS geschreven worden;
  * dat het BESTAND onaangeroerd blijft (het is procesgeheugen, geen bestandspatch);
  * de PE-DLL onder Wine via WINEDLLOVERRIDES (wordt overgeslagen als Wine of de DLL ontbreekt).

De proefprogramma's staan in memory/test/. Dezelfde proeven met de hand:
re/proef_laat.sh, re/proef_wine.sh en re/proef_extra.sh in de werkmap van de eigenaar.
"""
import hashlib
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "memory", "out")
BUILD = os.path.join(ROOT, "tools", "dev", "build-memory.sh")

MOTOR = os.path.join(OUT, "ravengun-patchmem.so")
PROEF = os.path.join(OUT, "probe_linux")
PROEF_NOPIE = os.path.join(OUT, "probe_linux_nopie")
LAAT_SO = os.path.join(OUT, "libravengun-proeflaat.so")
GEPAKT_SO = os.path.join(OUT, "libravengun-gepakt.so")

# De twee doelen van het proefprogramma, zoals ze in memory/test/probe_linux.c staan.
DOEL_ORIGINEEL = "112233445566778899aabbccddeeff00"
DOEL_GEPATCHT = "deadbeefdeadbeefdeadbeefdeadbeef"
LAAT_ORIGINEEL = "aabbccddeeff0102030405060708090a"
RETURNS_TRUE = "b801000000c3"

# De nagebootste gepakte module: dit staat in het BESTAND, en dat wordt bij het laden uitgepakt.
GEPAKT_IN_BESTAND = "f0e19687b4a55b58595e5f5c5d525350"
GEPAKT_IN_GEHEUGEN = "aabbccddeeff0102030405060708090a"

WINE = os.environ.get("RAVENGUN_WINE") or os.path.expanduser(
    "~/.local/share/ravengun/wine/proton-11.0-2/bin/wine")
PFX = os.environ.get("RAVENGUN_MEMORY_PFX") or os.path.expanduser(
    "~/.cache/ravengun-memory-proef-pfx")
WIN_OUT = OUT


# ---------------------------------------------------------------------------------------------
# Gereedschap


def draai(cmd, env=None, cwd=None, timeout=180):
    """Een programma draaien met een aangepaste omgeving. None in env betekent: weghalen."""
    e = dict(os.environ)
    for k, v in (env or {}).items():
        if v is None:
            e.pop(k, None)
        else:
            e[k] = v
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, errors="replace", env=e, cwd=cwd, timeout=timeout)


def lees(tekst):
    """De `sleutel=waarde`-regels van het proefprogramma naar een dict."""
    d = {}
    for regel in tekst.replace("\r", "").splitlines():
        if "=" in regel:
            k, _, v = regel.partition("=")
            k = k.strip()
            if k and " " not in k:
                d[k] = v.strip()
    return d


def sha256(pad):
    h = hashlib.sha256()
    with open(pad, "rb") as fh:
        for brok in iter(lambda: fh.read(1 << 20), b""):
            h.update(brok)
    return h.hexdigest()


def schrijf_lijst(pad, regels):
    with open(pad, "w", encoding="utf-8") as fh:
        for r in regels:
            fh.write(r + "\n")
    return pad


def nulmeting(proef, args=None):
    """Het proefprogramma draaien ZONDER de motor: dat geeft de offsets en de originele bytes."""
    p = draai([proef] + (args or ["zelf"]), env={"LD_PRELOAD": None,
                                                 "RAVENGUN_PATCHMEM_FILE": None})
    return lees(p.stdout)


def met_motor(proef, lijst, args=None, verbose=True, extra=None, preload=True):
    """Het proefprogramma draaien MET de motor."""
    env = {}
    if lijst:
        env["RAVENGUN_PATCHMEM_FILE"] = lijst
    else:
        env["RAVENGUN_PATCHMEM_FILE"] = None
    env["RAVENGUN_PATCHMEM_VERBOSE"] = "1" if verbose else None
    if preload:
        env["LD_PRELOAD"] = MOTOR
    else:
        env["LD_PRELOAD"] = None
    env.update(extra or {})
    p = draai([proef] + (args or ["zelf"]), env=env)
    return lees(p.stdout), p.stderr, p.returncode


def setUpModule():
    if not os.path.exists(BUILD):
        raise unittest.SkipTest("tools/dev/build-memory.sh ontbreekt; niets te testen")
    if os.environ.get("RAVENGUN_MEMORY_SKIP_BUILD") != "1":
        r = subprocess.run(["bash", BUILD, "--no-windows", "--no-install", "--no-test"],
                           cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, errors="replace", timeout=900)
        if r.returncode != 0:
            raise AssertionError("het bouwen mislukte:\n" + r.stdout)


def eis_proef(testcase, pad):
    if not os.path.exists(pad):
        testcase.skipTest("%s ontbreekt (bouw met tools/dev/build-memory.sh)" % pad)


# ---------------------------------------------------------------------------------------------
# 1. Het formaat van de patchlijst


class TestFormaat(unittest.TestCase):
    """Wat de motor met rare regels doet. Alles wat niet klopt wordt gemeld, niet stil opgeslokt."""

    @classmethod
    def setUpClass(cls):
        eis_proef(cls, PROEF)
        cls.nul = nulmeting(PROEF)
        cls.lijst = os.path.join(OUT, "test-formaat.txt")

    def test_commentaar_en_lege_regels(self):
        schrijf_lijst(self.lijst, [
            "# commentaar",
            "",
            "   ",
            "# nog een commentaar",
            "%s|%s||%s" % (self.nul["module"], self.nul["doel_offset"], DOEL_GEPATCHT),
        ])
        uit, fout, _ = met_motor(PROEF, self.lijst)
        self.assertEqual(uit.get("doel"), DOEL_GEPATCHT)
        self.assertIn("1 toegepast", fout)

    def test_te_weinig_velden(self):
        schrijf_lijst(self.lijst, ["game.exe|0x100|"])
        _, fout, _ = met_motor(PROEF, self.lijst)
        self.assertIn("verwacht 4 velden", fout)

    def test_geen_hex_bytereeks(self):
        schrijf_lijst(self.lijst, ["%s|%s|zz|aabb" % (self.nul["module"], self.nul["doel_offset"])])
        _, fout, _ = met_motor(PROEF, self.lijst)
        self.assertIn("geen hex-bytreeks", fout)

    def test_offset_moet_een_getal_zijn(self):
        schrijf_lijst(self.lijst, ["%s|xyz||aabb" % self.nul["module"]])
        _, fout, _ = met_motor(PROEF, self.lijst)
        self.assertIn("geen getal", fout)

    def test_lege_nieuw_is_een_fout(self):
        schrijf_lijst(self.lijst, ["%s|%s||" % (self.nul["module"], self.nul["doel_offset"])])
        _, fout, _ = met_motor(PROEF, self.lijst)
        self.assertIn("is leeg", fout)

    def test_decimaal_offset_mag_ook(self):
        decimaal = str(int(self.nul["doel_offset"], 16))
        schrijf_lijst(self.lijst, [
            "%s|%s||%s" % (self.nul["module"], decimaal, DOEL_GEPATCHT)])
        uit, fout, _ = met_motor(PROEF, self.lijst)
        self.assertEqual(uit.get("doel"), DOEL_GEPATCHT, fout)

    def test_zonder_patchmem_file_gebeurt_er_niets(self):
        uit, fout, _ = met_motor(PROEF, None)
        self.assertEqual(uit.get("doel"), DOEL_ORIGINEEL)
        self.assertNotIn("patchmem:", fout)


# ---------------------------------------------------------------------------------------------
# 2. De motor op Linux


class TestLinuxMotor(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        eis_proef(cls, PROEF)
        eis_proef(cls, MOTOR)
        cls.nul = nulmeting(PROEF)
        cls.lijst = os.path.join(OUT, "test-linux.txt")

    def test_data_patch_verandert_de_bytes(self):
        schrijf_lijst(self.lijst, [
            "%s|%s|%s|%s" % (self.nul["module"], self.nul["doel_offset"],
                             DOEL_ORIGINEEL[:8], DOEL_GEPATCHT)])
        uit, fout, _ = met_motor(PROEF, self.lijst)
        self.assertEqual(uit.get("doel"), DOEL_GEPATCHT, fout)
        self.assertIn("toegepast", fout)
        # De verbose regel moet module, offset en voor/na noemen.
        self.assertIn(self.nul["module"] + "+" + self.nul["doel_offset"], fout)
        self.assertIn(DOEL_ORIGINEEL + " -> " + DOEL_GEPATCHT, fout)

    def test_functie_patch_laat_de_functie_1_teruggeven(self):
        schrijf_lijst(self.lijst, [
            "%s|%s|%s|%s" % (self.nul["module"], self.nul["functie_offset"],
                             self.nul["functie_bytes"], RETURNS_TRUE)])
        uit, fout, _ = met_motor(PROEF, self.lijst)
        self.assertEqual(uit.get("functie"), "1", fout)
        self.assertIn("toegepast", fout)

    def test_leeg_verwacht_controleert_niet(self):
        schrijf_lijst(self.lijst, [
            "%s|%s||%s" % (self.nul["module"], self.nul["doel_offset"], DOEL_GEPATCHT)])
        uit, fout, _ = met_motor(PROEF, self.lijst)
        self.assertEqual(uit.get("doel"), DOEL_GEPATCHT, fout)

    def test_verwacht_klopt_niet_dan_niets_geschreven(self):
        schrijf_lijst(self.lijst, [
            "%s|%s|99999999|ffffffffffffffffffffffffffffffff"
            % (self.nul["module"], self.nul["doel_offset"])])
        uit, fout, _ = met_motor(PROEF, self.lijst)
        self.assertEqual(uit.get("doel"), DOEL_ORIGINEEL, "er is geschreven terwijl verwacht niet klopte")
        self.assertIn("GEWEIGERD", fout)
        self.assertIn("NIETS geschreven", fout)
        self.assertIn("99999999", fout)          # wat de lijst zei
        self.assertIn(DOEL_ORIGINEEL[:8], fout)  # wat er werkelijk stond

    def test_module_niet_geladen_wordt_overgeslagen(self):
        schrijf_lijst(self.lijst, [
            "bestaat-niet.dll|0x1000|aabb|ccdd"])
        uit, fout, rc = met_motor(PROEF, self.lijst)
        self.assertEqual(uit.get("doel"), DOEL_ORIGINEEL)
        self.assertIn("wacht", fout)
        self.assertIn("niet geladen", fout)

    def test_offset_buiten_de_module_valt_niet_om(self):
        """Een verkeerde offset mag het spel NIET laten crashen. De motor moet het bereik eerst
        controleren en anders weigeren -- een patchmotor die de game laat omvallen is erger dan een
        die weigert."""
        schrijf_lijst(self.lijst, [
            "%s|0x7fff0000|aabb|ccdd" % self.nul["module"]])
        uit, fout, rc = met_motor(PROEF, self.lijst)
        self.assertEqual(rc, 0, "het proefprogramma is omgevallen (exit %r)" % rc)
        self.assertEqual(uit.get("doel"), DOEL_ORIGINEEL)
        self.assertIn("niet in toegewezen geheugen", fout)
        self.assertIn("NIETS gelezen of geschreven", fout)

    def test_offset_net_over_het_einde_van_het_bestand(self):
        """De laatste bytes van de module plus wat er niet meer is: ook dat moet geweigerd worden,
        niet gelezen."""
        schrijf_lijst(self.lijst, [
            "%s|0x10000000|aabb|ccdd" % self.nul["module"]])
        uit, fout, rc = met_motor(PROEF, self.lijst)
        self.assertEqual(rc, 0)
        self.assertIn("MISLUKT", fout)

    def test_het_bestand_blijft_onaangeroerd(self):
        """Dit is het bewijs dat de motor in GEHEUGEN werkt en niet in een bestand. Precies daarom
        kan een GEPAKTE exe gepatcht worden: de bytes op schijf zijn dan niet de bytes die draaien."""
        voor = sha256(PROEF)
        schrijf_lijst(self.lijst, [
            "%s|%s||%s" % (self.nul["module"], self.nul["doel_offset"], DOEL_GEPATCHT)])
        uit, _, _ = met_motor(PROEF, self.lijst)
        self.assertEqual(uit.get("doel"), DOEL_GEPATCHT)
        self.assertEqual(sha256(PROEF), voor, "het proefprogramma is op schijf veranderd")

    def test_al_goede_bytes_worden_niet_nog_eens_geschreven(self):
        """`nieuw` gelijk aan wat er al staat: de motor meldt dat en schrijft niet. Zo blijft een
        tweede ronde (van de achtergrondthread) onschadelijk."""
        schrijf_lijst(self.lijst, [
            "%s|%s||%s" % (self.nul["module"], self.nul["doel_offset"], DOEL_ORIGINEEL)])
        uit, fout, _ = met_motor(PROEF, self.lijst)
        self.assertEqual(uit.get("doel"), DOEL_ORIGINEEL)
        self.assertIn("stond er al", fout)
        self.assertNotIn("GEWEIGERD", fout)
        self.assertNotIn("MISLUKT", fout)


class TestNietPie(unittest.TestCase):
    """Een niet-PIE programma heeft modulebasis 0. Dat is de lastige randvoorwaarde: 'gevonden met
    basis 0' en 'niet gevonden' zien er hetzelfde uit."""

    @classmethod
    def setUpClass(cls):
        eis_proef(cls, PROEF_NOPIE)
        cls.nul = nulmeting(PROEF_NOPIE)
        cls.lijst = os.path.join(OUT, "test-nopie.txt")

    def test_de_basis_is_echt_nul(self):
        # Zonder PIE is de RVA gelijk aan het adres zelf, dus ruim boven 0x400000.
        self.assertGreater(int(self.nul["doel_offset"], 16), 0x400000)

    def test_patch_werkt_met_basis_nul(self):
        schrijf_lijst(self.lijst, [
            "%s|%s|%s|%s" % (self.nul["module"], self.nul["doel_offset"],
                             DOEL_ORIGINEEL[:8], DOEL_GEPATCHT),
            "%s|%s|%s|%s" % (self.nul["module"], self.nul["functie_offset"],
                             self.nul["functie_bytes"], RETURNS_TRUE),
        ])
        uit, fout, _ = met_motor(PROEF_NOPIE, self.lijst)
        self.assertEqual(uit.get("doel"), DOEL_GEPATCHT, fout)
        self.assertEqual(uit.get("functie"), "1", fout)


# ---------------------------------------------------------------------------------------------
# 3. Een module die PAS LATER geladen wordt


class TestLateModule(unittest.TestCase):
    """Het geval uit docs/community-model.md paragraaf 3: de bytes staan niet in de exe."""

    @classmethod
    def setUpClass(cls):
        eis_proef(cls, PROEF)
        eis_proef(cls, LAAT_SO)
        cls.nul = nulmeting(PROEF, ["laat", LAAT_SO])
        cls.lijst = os.path.join(OUT, "test-laat.txt")
        schrijf_lijst(cls.lijst, [
            "%s|%s|%s|%s" % (cls.nul["laat_module"], cls.nul["laat_offset"],
                             LAAT_ORIGINEEL, DOEL_GEPATCHT)])

    def test_de_bibliotheek_is_bij_de_start_niet_geladen(self):
        """De nulmeting draait zonder de motor: dan is de bibliotheek nog ongerept."""
        self.assertEqual(self.nul["laat_direct"], LAAT_ORIGINEEL)
        self.assertEqual(self.nul["laat_na"], LAAT_ORIGINEEL)
        self.assertEqual(self.nul["laat_functie_na"], "0")

    def test_dlopen_wordt_onderschept(self):
        """Meteen na dlopen staat de patch er al: dat is de onderschepping, niet de thread."""
        uit, fout, _ = met_motor(PROEF, self.lijst, args=["laat", LAAT_SO])
        self.assertEqual(uit.get("laat_direct"), DOEL_GEPATCHT, fout)
        self.assertEqual(uit.get("laat_na"), DOEL_GEPATCHT, fout)
        self.assertIn("niet geladen; de patch wacht", fout)

    def test_achtergrondthread_doet_het_als_de_haak_uit_staat(self):
        """Met RAVENGUN_PATCHMEM_GEEN_DLOPEN=1 doet de onderschepping niets. Dan moet de
        achtergrondthread de module alsnog patchen -- dat is het eigenlijke bewijs dat hij werkt."""
        uit, fout, _ = met_motor(PROEF, self.lijst, args=["laat", LAAT_SO],
                                 extra={"RAVENGUN_PATCHMEM_GEEN_DLOPEN": "1"})
        self.assertEqual(uit.get("laat_direct"), LAAT_ORIGINEEL,
                         "de haak stond uit, maar er is toch meteen gepatcht")
        self.assertEqual(uit.get("laat_na"), DOEL_GEPATCHT, fout)
        self.assertTrue("wachtende patches toegepast" in fout or "klaar --" in fout,
                        "geen melding van de overgang van wachten naar toegepast:\n" + fout)

    def test_wacht_nul_zet_de_thread_uit(self):
        uit, fout, _ = met_motor(PROEF, self.lijst, args=["laat", LAAT_SO],
                                 extra={"RAVENGUN_PATCHMEM_WACHT": "0",
                                        "RAVENGUN_PATCHMEM_GEEN_DLOPEN": "1"})
        self.assertEqual(uit.get("laat_direct"), LAAT_ORIGINEEL)
        self.assertEqual(uit.get("laat_na"), LAAT_ORIGINEEL,
                         "de thread stond uit, maar de module is toch gepatcht")
        self.assertIn("geen achtergrondthread", fout)

    def test_openstaand_wordt_gemeld(self):
        """Een module die nooit komt wordt bij het aflopen van de wachttijd gemeld, niet verzwegen."""
        lijst = os.path.join(OUT, "test-laat-nooit.txt")
        schrijf_lijst(lijst, ["bestaat-niet.dll|0x1000||aabb"])
        _, fout, _ = met_motor(PROEF, lijst, args=["laat", LAAT_SO],
                               extra={"RAVENGUN_PATCHMEM_WACHT": "1",
                                      "RAVENGUN_PATCHMEM_POLL": "100",
                                      "RAVENGUN_PATCHMEM_GEEN_DLOPEN": "1"})
        self.assertIn("NIET toegepast", fout)


# ---------------------------------------------------------------------------------------------
# 3b. Een GEPAKTE module: bestand en geheugen verschillen


class TestGepakteModule(unittest.TestCase):
    """Het geval uit docs/community-model.md paragraaf 3: een GEPAKTE exe (Pigs Might Fly, Luigi's
    Mansion). In het bestand staat niet de code die draait.

    memory/test/packed_module.c bootst dat na: de bytes staan versleuteld in het bestand en een
    constructor pakt ze bij het laden uit. Daarmee is meetbaar wat het verschil betekent:
    een patch die op het BESTAND is gebaseerd wordt geweigerd, een patch op het GEHEUGEN lukt.
    """

    @classmethod
    def setUpClass(cls):
        eis_proef(cls, PROEF)
        eis_proef(cls, GEPAKT_SO)
        cls.nul = nulmeting(PROEF, ["gepakt", GEPAKT_SO])
        cls.lijst = os.path.join(OUT, "test-gepakt.txt")
        with open(GEPAKT_SO, "rb") as fh:
            cls.bestand = fh.read()

    def test_het_bestand_bevat_de_versleutelde_bytes(self):
        self.assertIn(bytes.fromhex(GEPAKT_IN_BESTAND), self.bestand,
                      "de versleutelde bytes staan niet in het bestand; de proef deugt niet")

    def test_de_echte_bytes_staan_niet_in_het_bestand(self):
        self.assertNotIn(bytes.fromhex(GEPAKT_IN_GEHEUGEN), self.bestand,
                         "de uitgepakte bytes staan wel in het bestand; dan is er niets nagebootst")

    def test_het_geheugen_heeft_de_uitgepakte_bytes(self):
        self.assertEqual(self.nul["gepakt_direct"], GEPAKT_IN_GEHEUGEN,
                         "de module is niet uitgepakt bij het laden")

    def test_patch_op_de_geheugen_bytes_lukt(self):
        schrijf_lijst(self.lijst, [
            "%s|%s|%s|%s" % (self.nul["gepakt_module"], self.nul["gepakt_offset"],
                             GEPAKT_IN_GEHEUGEN, DOEL_GEPATCHT)])
        uit, fout, _ = met_motor(PROEF, self.lijst, args=["gepakt", GEPAKT_SO])
        self.assertEqual(uit.get("gepakt_na"), DOEL_GEPATCHT, fout)
        self.assertIn("toegepast", fout)

    def test_patch_op_de_bestands_bytes_wordt_geweigerd(self):
        """Dit is precies wat de bestandspatch-motor niet kan: de bytes in het bestand zijn niet de
        bytes die draaien."""
        schrijf_lijst(self.lijst, [
            "%s|%s|%s|%s" % (self.nul["gepakt_module"], self.nul["gepakt_offset"],
                             GEPAKT_IN_BESTAND, DOEL_GEPATCHT)])
        uit, fout, _ = met_motor(PROEF, self.lijst, args=["gepakt", GEPAKT_SO])
        self.assertEqual(uit.get("gepakt_na"), GEPAKT_IN_GEHEUGEN,
                         "er is geschreven terwijl verwacht niet klopte")
        self.assertIn("GEWEIGERD", fout)
        self.assertIn("NIETS geschreven", fout)
        self.assertIn(GEPAKT_IN_GEHEUGEN[:8], fout)   # wat er werkelijk stond

    def test_ook_de_functie_in_een_gepakte_module(self):
        schrijf_lijst(self.lijst, [
            "%s|%s||%s" % (self.nul["gepakt_module"], self.nul["gepakt_functie_offset"],
                           RETURNS_TRUE)])
        uit, fout, _ = met_motor(PROEF, self.lijst, args=["gepakt", GEPAKT_SO])
        self.assertEqual(uit.get("gepakt_functie_na"), "1", fout)


# ---------------------------------------------------------------------------------------------
# 3c. De 32-bits Linux-bibliotheek


class TestLinux32(unittest.TestCase):
    """Dezelfde proeven met de 32-bits .so. Die wordt gebouwd voor de 32-bits ELF-games, dus het is
    niet genoeg hem alleen te compileren: hij moet ook echt draaien."""

    @classmethod
    def setUpClass(cls):
        cls.proef = os.path.join(OUT, "probe_linux32")
        cls.motor = os.path.join(OUT, "ravengun-patchmem32.so")
        cls.laat = os.path.join(OUT, "libravengun-proeflaat32.so")
        eis_proef(cls, cls.proef)
        eis_proef(cls, cls.motor)
        cls.nul = nulmeting(cls.proef)
        cls.lijst = os.path.join(OUT, "test-linux32.txt")

    def _draai(self, regels, args=None, extra=None):
        schrijf_lijst(self.lijst, regels)
        env = {"RAVENGUN_PATCHMEM_FILE": self.lijst,
               "RAVENGUN_PATCHMEM_VERBOSE": "1",
               "LD_PRELOAD": self.motor}
        env.update(extra or {})
        p = draai([self.proef] + (args or ["zelf"]), env=env)
        return lees(p.stdout), p.stderr

    def test_het_is_echt_een_32_bits_programma(self):
        with open(self.proef, "rb") as fh:
            kop = fh.read(20)
        self.assertEqual(kop[:4], b"\x7fELF")
        self.assertEqual(kop[4], 1, "dit is geen 32-bits ELF-bestand")

    def test_data_en_functiepatch(self):
        uit, fout = self._draai([
            "%s|%s|%s|%s" % (self.nul["module"], self.nul["doel_offset"],
                             DOEL_ORIGINEEL[:8], DOEL_GEPATCHT),
            "%s|%s|%s|%s" % (self.nul["module"], self.nul["functie_offset"],
                             self.nul["functie_bytes"], RETURNS_TRUE),
        ])
        self.assertEqual(uit.get("doel"), DOEL_GEPATCHT, fout)
        self.assertEqual(uit.get("functie"), "1", fout)

    def test_verwacht_klopt_niet_dan_niets_geschreven(self):
        uit, fout = self._draai([
            "%s|%s|99999999|ffffffffffffffffffffffffffffffff"
            % (self.nul["module"], self.nul["doel_offset"])])
        self.assertEqual(uit.get("doel"), DOEL_ORIGINEEL)
        self.assertIn("NIETS geschreven", fout)

    def test_later_geladen_bibliotheek_32_bits(self):
        nul = nulmeting(self.proef, ["laat", self.laat])
        uit, fout = self._draai(
            ["%s|%s|%s|%s" % (nul["laat_module"], nul["laat_offset"],
                              LAAT_ORIGINEEL, DOEL_GEPATCHT)],
            args=["laat", self.laat])
        self.assertEqual(uit.get("laat_direct"), DOEL_GEPATCHT, fout)


# ---------------------------------------------------------------------------------------------
# 4. De PE-DLL onder Wine


def wine_beschikbaar():
    if os.environ.get("RAVENGUN_MEMORY_SKIP_WINE") == "1":
        return False
    if not os.path.exists(WINE):
        return False
    return all(os.path.exists(os.path.join(WIN_OUT, n)) for n in (
        "ravengun-patchmem.dll", "ravengun-laat.dll", "probe_win.exe"))


@unittest.skipUnless(wine_beschikbaar(), "geen Wine of geen PE-DLL's (bouw met tools/dev/build-memory.sh)")
class TestWineMotor(unittest.TestCase):
    """WINEDLLOVERRIDES=<naam>=n laadt alleen een NATIVE DLL voor een module die het programma ook
    werkelijk importeert. Daarom importeert probe_win.exe de DLL: precies zoals een game dat doet."""

    @classmethod
    def setUpClass(cls):
        cls.env = {
            "WINEPREFIX": PFX,
            "WINEDEBUG": "-all",
            "WINEDLLOVERRIDES": "ravengun-patchmem=n;ravengun-laat=n",
            "RAVENGUN_PATCHMEM_FILE": None,
        }
        if not os.path.isdir(PFX):
            draai([WINE, "wineboot", "-i"], env=cls.env, timeout=600)
            draai([WINE, "cmd", "/c", "exit"], env=cls.env, timeout=600)
        cls.proef = os.path.join(WIN_OUT, "probe_win.exe")
        cls.nul = lees(draai([WINE, cls.proef], env=cls.env, cwd=WIN_OUT, timeout=300).stdout)
        cls.lijst_unix = os.path.join(OUT, "test-wine.txt")
        # Eerst een leeg bestand, anders heeft winepath niets om te noemen.
        schrijf_lijst(cls.lijst_unix, ["# nog leeg"])
        cls.lijst_win = draai([WINE, "winepath", "-w", cls.lijst_unix],
                              env=cls.env, timeout=120).stdout.replace("\r", "").strip()

    @classmethod
    def tearDownClass(cls):
        """De wineserver van DEZE prefix blijft na de laatste test nog staan. Andere partijen bewaken de bench op de
        procesnamen wine en wineserver, dus een achtergebleven wineserver laat hun run weigeren terwijl er niets
        draait. Opruimen dus, en uitsluitend met de eigen prefix: `wineserver -k` zonder WINEPREFIX richt zich op
        een andere prefix, en in de gedeelde prefix van de games kan op dat moment een spel draaien."""
        gedeeld = os.path.realpath(os.path.expanduser("~/.local/share/ravengun/prefixes"))
        if os.path.realpath(PFX).startswith(gedeeld + os.sep):
            return                               # nooit de wineserver van een prefix waarin games draaien
        wineserver = os.path.join(os.path.dirname(WINE), "wineserver")
        if not os.path.exists(wineserver):
            return
        for optie in ("-k", "-w"):               # stoppen, en wachten tot hij weg is
            try:
                draai([wineserver, optie], env={"WINEPREFIX": PFX}, timeout=30)
            except (OSError, subprocess.TimeoutExpired):
                pass

    def _draai(self, regels, extra=None, timeout=300):
        schrijf_lijst(self.lijst_unix, regels)
        env = dict(self.env)
        env["RAVENGUN_PATCHMEM_FILE"] = self.lijst_win
        env.update(extra or {})
        p = draai([WINE, self.proef], env=env, cwd=WIN_OUT, timeout=timeout)
        return lees(p.stdout), p.stderr.replace("\r", ""), p.returncode

    def test_nulmeting_laadt_de_dll_maar_doet_niets(self):
        self.assertEqual(self.nul.get("motor", "").startswith("ravengun-patchmem"), True,
                         "de PE-DLL is niet geladen: %r" % self.nul)
        self.assertEqual(self.nul.get("doel"), DOEL_ORIGINEEL)
        self.assertEqual(self.nul.get("laat_direct"), LAAT_ORIGINEEL)

    def test_exe_wordt_gepatcht(self):
        uit, fout, _ = self._draai([
            "%s|%s|%s|%s" % (self.nul["module"], self.nul["offset"],
                             DOEL_ORIGINEEL[:8], DOEL_GEPATCHT)])
        self.assertEqual(uit.get("doel_na"), DOEL_GEPATCHT, fout)
        self.assertIn("toegepast", fout)

    def test_functie_wordt_gepatcht(self):
        uit, fout, _ = self._draai([
            "%s|%s|%s|%s" % (self.nul["module"], self.nul["functie_offset"],
                             self.nul["functie_bytes"], RETURNS_TRUE)])
        self.assertEqual(uit.get("functie_na"), "1", fout)

    def test_later_geladen_dll_wordt_gepatcht(self):
        uit, fout, _ = self._draai([
            "%s|%s|%s|%s" % (self.nul["laat_module"], self.nul["laat_offset"],
                             LAAT_ORIGINEEL, DOEL_GEPATCHT)])
        self.assertEqual(uit.get("laat_na"), DOEL_GEPATCHT, fout)
        self.assertIn("wacht", fout)          # bij de eerste ronde was hij nog niet geladen

    def test_verwacht_klopt_niet_onder_wine(self):
        uit, fout, _ = self._draai([
            "%s|%s|99999999|ffffffffffffffffffffffffffffffff"
            % (self.nul["module"], self.nul["offset"])])
        self.assertEqual(uit.get("doel_na"), DOEL_ORIGINEEL,
                         "er is geschreven terwijl verwacht niet klopte")
        self.assertIn("NIETS geschreven", fout)


if __name__ == "__main__":
    unittest.main(verbosity=2)

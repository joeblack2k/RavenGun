#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""De BepInEx-plugins van de WartranTroopers-familie lezen het bordbeeld van RavenGun zelf (plugins/bepinex/shared/Bordbeeld.cs).
Hun tabel (welke bit welk signaal is, waar het bitveld en de assen staan, de grens voor buiten beeld) moet precies die van
packaging/tp-shm-profiles.json zijn, profiel WartranTroopers: daar schrijft de IO-brug van de daemon naar. Verder: de plugins
gebruiken die tabel (geen losse bitnummers) en elke plugin heeft een managed.txt voor scripts/build-bepinex-plugin.sh."""
import json
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BORDBEELD = os.path.join(ROOT, "plugins", "bepinex", "shared", "Bordbeeld.cs")
PROFIELEN = os.path.join(ROOT, "packaging", "tp-shm-profiles.json")
PLUGINS = ["MarsSortie", "WildWestShootout", "AfterDark2"]


def lees(pad):
    with open(pad, encoding="utf-8") as f:
        return f.read()


def constanten(cs):
    enkel = {m.group(1): int(m.group(2)) for m in re.finditer(r"public const int (\w+) = (\d+);", cs)}
    rijen = {m.group(1): [int(x) for x in m.group(2).split(",")]
             for m in re.finditer(r"public static readonly int\[\] (\w+) = \{([\d,\s]+)\};", cs)}
    return enkel, rijen


class Bordbeeld(unittest.TestCase):
    def setUp(self):
        self.cs = lees(BORDBEELD)
        self.enkel, self.rijen = constanten(self.cs)
        prof = json.loads(lees(PROFIELEN))["profiles"]["WartranTroopers"]
        self.bits = {b["signal"]: (b["offset"], b["bit"]) for b in prof["bits"]}
        self.assen = {a["signal"]: a for a in prof["axes"]}
        self.prof = prof

    def test_grootte_en_offsets(self):
        self.assertEqual(self.enkel["Grootte"], self.prof["size"])
        offsets = {o for o, _ in self.bits.values()}
        self.assertEqual(offsets, {self.enkel["BitsOffset"]}, "alle knoppen in een bitveld")
        for p in range(4):
            for i, as_ in enumerate("xy"):
                a = self.assen["p%d.%s" % (p + 1, as_)]
                self.assertEqual(a["offset"], self.enkel["AsOffset"] + 2 * p + i)
                self.assertEqual((a["bytes"], a["min"], a["max"], a["invert"]), (1, 0, 255, False))

    def test_losse_signalen(self):
        for naam, signaal in [("Test", "test"), ("Service", "service"), ("Munt1", "coin1"), ("Munt2", "coin2"),
                              ("VolumeOp", "volume_up"), ("VolumeNeer", "volume_down")]:
            self.assertEqual(self.enkel[naam], self.bits[signaal][1], naam)

    def test_per_speler(self):
        for naam, signaal in [("Start", "start"), ("Trekker", "trigger"), ("Herladen", "reload"), ("Granaat", "grenade"),
                              ("Aux1", "aux1")]:
            self.assertEqual(self.rijen[naam], [self.bits["p%d.%s" % (p + 1, signaal)][1] for p in range(4)], naam)

    def test_buiten_beeld(self):
        # tp-shm-profiles.json: "de kern leest een as <= 1 of >= 254 als buiten beeld"
        self.assertIn("<= 1 of >= 254", self.prof["description"])
        self.assertEqual((self.enkel["BuitenOnder"], self.enkel["BuitenBoven"]), (1, 254))

    def test_mapping_naam(self):
        self.assertIn('MappingNaam = "TeknoParrot_JvsState"', self.cs)

    def test_plugins_gebruiken_de_tabel(self):
        for g in PLUGINS:
            d = os.path.join(ROOT, "plugins", "bepinex", g)
            self.assertTrue(os.path.isfile(os.path.join(d, "managed.txt")), g + ": managed.txt ontbreekt")
            cs = "".join(lees(os.path.join(d, f)) for f in sorted(os.listdir(d)) if f.endswith(".cs"))
            self.assertIn("RavenGun.Bepinex.Gedeeld", cs, g)
            # bits alleen via Indeling.*, nooit als getal in Neer/Op/Ingedrukt
            losse = re.findall(r"\.(?:Neer|Op|Ingedrukt)\(\s*\d+", cs)
            self.assertEqual(losse, [], g + ": losse bitnummers")
            # de plugin zet de TeknoParrot-plugin van de game uit, zodat er maar een bediening is
            self.assertRegex(cs, r'TpPlugin\.ZetUit\("com\.tp\.\w+"', g)
            self.assertRegex(cs, r'BepInDependency\("com\.tp\.\w+", BepInDependency\.DependencyFlags\.SoftDependency\)', g)


if __name__ == "__main__":
    unittest.main()

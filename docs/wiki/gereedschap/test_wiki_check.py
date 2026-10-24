#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""wiki-check.py (naast deze test): de controle van de publieke wiki. Hermetisch: een eigen kleine wiki in een tijdelijke map.

De wiki zelf is een andere git-repository; deze test dwingt af dat de CONTROLE doet wat hij belooft: een dode link, een
spelpagina met een afwijkende opbouw, een ontbrekende pagina, een woord over de beveiligings- of licentielaag en een pad
van deze ene machine worden elk gevonden. Zonder deze test zou een controle die altijd "ok" zegt niemand opvallen.
"""
import importlib.util
import os
import shutil
import tempfile
import unittest

HIER = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("wiki_check", os.path.join(HIER, "wiki-check.py"))
wc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wc)


class WikiCheck(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(prefix="rg-wiki-")
        self.addCleanup(shutil.rmtree, self.d, True)
        links = "\n".join("- [%s](%s)" % (n, n) for n in wc.ALGEMEEN[1:] + wc.SPELLEN)
        self.zet("Home", "# Wiki\n\n" + links + "\n")
        self.zet("_Sidebar", links + "\n")
        self.zet("_Footer", "See [Home](Home).\n")
        for n in wc.ALGEMEEN[1:]:
            self.zet(n, "# %s\n\nSee [Home](Home).\n" % n)
        for n in wc.SPELLEN:
            self.zet(n, "# %s\n\n" % n + "".join("## %s\n\ntext\n\n" % k for k in wc.KOPPEN))

    def zet(self, naam, tekst):
        with open(os.path.join(self.d, naam + ".md"), "w", encoding="utf-8") as f:
            f.write(tekst)

    def lees(self, naam):
        with open(os.path.join(self.d, naam + ".md"), encoding="utf-8") as f:
            return f.read()

    def fouten(self):
        return wc.controleer(self.d)[0]

    def test_een_goede_wiki(self):
        self.assertEqual(self.fouten(), [])
        self.assertEqual(wc.main([self.d]), 0)

    def test_geen_wiki_is_overgeslagen(self):
        self.assertEqual(wc.main([os.path.join(self.d, "bestaat-niet")]), 77)

    def test_een_ontbrekende_pagina(self):
        os.unlink(os.path.join(self.d, "Halo-Fireteam-Raven.md"))
        f = self.fouten()
        self.assertTrue(any("Halo-Fireteam-Raven.md ontbreekt" in x for x in f), f)
        self.assertTrue(any("wijst naar een pagina die niet bestaat" in x for x in f), f)   # Home linkt er nog naar
        self.assertEqual(wc.main([self.d]), 1)

    def test_een_dode_link(self):
        self.zet("Loader-Architecture", "# x\n\nSee [the other page](Loader-Architectuur) and [Home](Home#terms).\n")
        f = self.fouten()
        self.assertEqual(len(f), 1, f)
        self.assertIn("Loader-Architectuur", f[0])

    def test_een_link_in_code_telt_niet(self):
        self.zet("Loader-Architecture", "# x\n\n`[x](Nergens)`\n\n```\n[y](OokNergens)\n```\n\n[web](https://example.org/x)\n")
        self.assertEqual(self.fouten(), [])

    def test_een_spelpagina_met_een_afwijkende_opbouw(self):
        tekst = self.lees("Luigis-Mansion").replace("## 6. Timing\n", "## 6. Speed\n")
        self.zet("Luigis-Mansion", tekst)
        f = self.fouten()
        self.assertEqual(len(f), 1, f)
        self.assertIn("ontbreekt: 6. Timing", f[0])
        self.assertIn("extra: 6. Speed", f[0])
        # een verwisselde orde
        k = list(wc.KOPPEN)
        k[4], k[5] = k[5], k[4]
        self.zet("Luigis-Mansion", "# x\n\n" + "".join("## %s\n\n" % x for x in k))
        self.assertIn("de orde klopt niet", self.fouten()[0])

    def test_wat_er_niet_in_hoort(self):
        for woord in ("dongle", "Dongles", "HASP", "keychip", "key chip", "anti-tamper", "packer", "unpacked", "bypass",
                      "serial number", "copy protection", "ptrace"):
            self.zet("Running-on-Linux", "# x\n\nThe game needs its %s to start.\n" % woord)
            f = self.fouten()
            self.assertEqual(len(f), 1, (woord, f))
            self.assertIn("hoort niet in de wiki", f[0])
            self.assertIn("Running-on-Linux.md:3", f[0])
        # woorden die erop lijken maar iets anders zijn
        self.zet("Running-on-Linux", "# x\n\nThe ptrace_scope setting; a package; a backpack; a cracker biscuit; licensed under GPL-3.0.\n")
        self.assertEqual(self.fouten(), [])

    def test_een_negatieve_scoperegel(self):
        for zin in ("Nothing on this page concerns the cabinet's other layers.", "This wiki does not cover that.",
                    "It deliberately leaves out the rest.", "The hardware security side is elsewhere.",
                    "Details were omitted here.", "The licensing layer is a separate subject."):
            self.zet("Running-on-Linux", "# x\n\n%s\n" % zin)
            f = self.fouten()
            self.assertEqual(len(f), 1, (zin, f))
            self.assertIn("positieve scoperegel", f[0])
        # de positieve scoperegel zelf, en gewone zinnen die erop lijken
        self.zet("Running-on-Linux", "# x\n\nThis page covers the game and the loader: what you need to run this title and "
                                     "where it gets stuck. The sandbox covers /usr; the leaves fall out.\n")
        self.assertEqual(self.fouten(), [])

    def test_iets_van_deze_ene_machine(self):
        self.zet("Running-on-Linux", "# x\n\nStarted from /home/deck/mnt/spidernas_emulation/game.\n")
        f = self.fouten()
        self.assertEqual(len(f), 2, f)
        self.assertTrue(all("deze ene machine" in x for x in f))

    def test_de_navigatie_noemt_elke_pagina(self):
        self.zet("_Sidebar", "- [Home](Home)\n")
        f = self.fouten()
        self.assertEqual(len(f), len(wc.ALGEMEEN) - 1 + len(wc.SPELLEN), f)

    def test_een_vreemde_bestandsnaam(self):
        self.zet("Luigi's Mansion", "# x\n")
        self.assertTrue(any("geen geldige paginanaam" in x for x in self.fouten()))


if __name__ == "__main__":
    unittest.main()

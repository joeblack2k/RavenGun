#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Leest de overzichten van de exit-trace (OpenParrot-patch 0007, <spoor>.overzicht-<nr>.txt) en zegt wat er in het
proces is AANGERAAKT. Alleen lezen.

  exit-trace-overzicht.py aangeraakt OVERZICHT [--prefix WINEPREFIX]
      elke geëxporteerde functie waarvan de eerste 16 bytes in het geheugen afwijken van het DLL-bestand op schijf
      (een haak is een sprong in die bytes), met het sprongdoel als de eerste instructie een E9/EB/FF25-sprong is.
  exit-trace-overzicht.py verschil OVERZICHT_A OVERZICHT_B [--prefix WINEPREFIX]
      de aangeraakte functies van A en B naast elkaar: alleen in A, alleen in B, in beide (met of zonder zelfde bytes),
      en de items van de import-adres-tabel van de hoofdmodule die in A en B ergens anders heen wijzen.

De schijfbytes komen uit het pad dat het overzicht zelf noemt (C:\\... onder de prefix, Z:\\... is de Linux-root). Een
module zonder bestand op schijf (of een functie buiten de secties van het bestand) wordt als "niet te vergelijken"
geteld, niet stil overgeslagen."""
import argparse
import os
import re
import struct
import sys

DEFAULT_PREFIX = os.path.expanduser("~/.local/share/ravengun/prefixes/shared/wine")


def lees(pad):
    """-> (paden {module: windows-pad}, bases {module: basis}, exports {(module, naam): (rva, bytes)}, iat {sleutel: doel})"""
    paden, bases, exports, iat, laatste = {}, {}, {}, {}, None
    with open(pad, "rb") as f:
        for regel in f.read().decode("latin-1").splitlines():
            if regel.startswith("pad "):
                _, basis, win = regel.split(" ", 2)
                laatste = win
            elif regel.startswith("module "):
                m = re.match(r"module (\S+) basis=(\S+)", regel)
                if m:
                    paden[m.group(1)] = laatste
                    bases[m.group(1)] = int(m.group(2), 16)
            elif regel.startswith("export "):
                m = re.match(r"export ([^!]+)!(\S+) rva=0x([0-9a-f]+) bytes=(.*)$", regel)
                if m:
                    try:
                        b = bytes.fromhex(m.group(4))
                    except ValueError:
                        b = None   # "<niet leesbaar>"
                    exports[(m.group(1), m.group(2))] = (int(m.group(3), 16), b)
            elif regel.startswith("iat ") and " -> " in regel:
                m = re.match(r"iat (\d+)/(\d+) (\S+) slot-rva=0x[0-9a-f]+ -> (.*)$", regel)
                if m:
                    iat["%s/%s %s" % (m.group(1), m.group(2), m.group(3))] = re.sub(r" prot=0x[0-9a-f]+$", "", m.group(4))
    return paden, bases, exports, iat


def host_pad(win, prefix):
    if win is None:
        return None
    win = win.replace("\\", "/")
    if win[:2].lower() == "z:":
        return win[2:]
    if win[:2].lower() == "c:":
        return os.path.join(prefix, "drive_c", win[3:])
    return None


class Schijf:
    """De bytes van een PE-bestand op een RVA."""

    def __init__(self, pad):
        with open(pad, "rb") as f:
            self.d = f.read()
        pe = struct.unpack_from("<I", self.d, 0x3C)[0]
        n = struct.unpack_from("<H", self.d, pe + 6)[0]
        opt = struct.unpack_from("<H", self.d, pe + 20)[0]
        self.secties = [struct.unpack_from("<8sIIII", self.d, pe + 24 + opt + 40 * i) for i in range(n)]

    def op(self, rva, n):
        for _, vsize, va, rsize, roff in self.secties:
            if va <= rva and rva + n <= va + rsize:
                return self.d[rva - va + roff:rva - va + roff + n]
        return None


def sprongdoel(adres, b):
    if b[0] == 0xE9:
        return adres + 5 + struct.unpack_from("<i", b, 1)[0]
    if b[0] == 0xEB:
        return adres + 2 + struct.unpack_from("<b", b, 1)[0]
    return None


def in_module(adres, bases, paden_groottes):
    for naam, (basis, grootte) in paden_groottes.items():
        if basis <= adres < basis + grootte:
            return "%s+0x%x" % (naam, adres - basis)
    return "buiten elke module"


def aangeraakt(pad, prefix):
    """-> ({(module, naam): tekst}, niet_te_vergelijken, aantal vergeleken)"""
    paden, bases, exports, _ = lees(pad)
    groottes = {}
    with open(pad, "rb") as f:
        for m in re.finditer(r"module (\S+) basis=(\S+) grootte=0x([0-9a-f]+)", f.read().decode("latin-1")):
            groottes[m.group(1)] = (int(m.group(2), 16), int(m.group(3), 16))
    schijven, uit, niet, vergeleken = {}, {}, {}, 0
    for (module, naam), (rva, b) in sorted(exports.items()):
        if module not in schijven:
            h = host_pad(paden.get(module), prefix)
            try:
                schijven[module] = Schijf(h) if h and os.path.isfile(h) else None
            except (OSError, struct.error):
                schijven[module] = None
        s = schijven[module]
        orig = s.op(rva, 16) if s else None
        if b is None or orig is None:
            niet[module] = niet.get(module, 0) + 1
            continue
        vergeleken += 1
        if b != orig:
            doel = sprongdoel(bases[module] + rva, b)
            uit[(module, naam)] = "%s  (schijf: %s)%s" % (
                b.hex(" "), orig.hex(" "), "  sprong naar 0x%x = %s" % (doel, in_module(doel, bases, groottes)) if doel else "")
    return uit, niet, vergeleken


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("opdracht", choices=("aangeraakt", "verschil"))
    ap.add_argument("overzicht", nargs="+")
    ap.add_argument("--prefix", default=DEFAULT_PREFIX)
    a = ap.parse_args()

    if a.opdracht == "aangeraakt":
        for pad in a.overzicht:
            uit, niet, n = aangeraakt(pad, a.prefix)
            print("== %s: %d functies vergeleken met de schijf, %d aangeraakt" % (pad, n, len(uit)))
            for module, aantal in sorted(niet.items()):
                print("   niet te vergelijken: %s (%d functies)" % (module, aantal))
            for (module, naam), tekst in sorted(uit.items()):
                print("   %s!%s  %s" % (module, naam, tekst))
        return 0

    if len(a.overzicht) != 2:
        ap.error("verschil vraagt precies twee overzichten")
    (ua, na, ca), (ub, nb, cb) = aangeraakt(a.overzicht[0], a.prefix), aangeraakt(a.overzicht[1], a.prefix)
    print("A = %s: %d vergeleken, %d aangeraakt" % (a.overzicht[0], ca, len(ua)))
    print("B = %s: %d vergeleken, %d aangeraakt" % (a.overzicht[1], cb, len(ub)))
    for label, niet in (("A", na), ("B", nb)):
        for module, aantal in sorted(niet.items()):
            print("   %s niet te vergelijken: %s (%d functies)" % (label, module, aantal))
    for titel, sleutels in (("alleen in A aangeraakt", sorted(set(ua) - set(ub))),
                            ("alleen in B aangeraakt", sorted(set(ub) - set(ua))),
                            ("in beide aangeraakt", sorted(set(ua) & set(ub)))):
        print("-- %s: %d" % (titel, len(sleutels)))
        for k in sleutels:
            print("   %s!%s" % k)
            if k in ua:
                print("      A %s" % ua[k])
            if k in ub:
                print("      B %s" % ub[k])
    ia, ib = lees(a.overzicht[0])[3], lees(a.overzicht[1])[3]
    anders = [k for k in sorted(set(ia) | set(ib)) if ia.get(k) != ib.get(k)]
    print("-- import-adres-tabel: %d items in A, %d in B, %d wijzen ergens anders heen" % (len(ia), len(ib), len(anders)))
    for k in anders:
        print("   %s\n      A %s\n      B %s" % (k, ia.get(k, "<ontbreekt>"), ib.get(k, "<ontbreekt>")))
    return 0


if __name__ == "__main__":
    sys.exit(main())

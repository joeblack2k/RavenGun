#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""controleer-abi.py - vergelijkt de exportlijst en de stdcall-argumentgroottes van twee
matrix-DLL's.

Waarom: onze matrix32.dll moet een echte drop-in zijn voor het origineel. De naam-tabel moet
kloppen, maar ook de stapelopruiming: een stdcall-functie in het origineel eindigt op `ret N`, en N
is het aantal argumentbytes. Wijken die af, dan loopt de stapel van de game scheef en crasht hij.

Gebruik:
    python3 matrix/src/controleer-abi.py <onze.dll> [<origineel.dll>]

Zonder origineel worden alleen onze eigen namen en `ret N` getoond. Met origineel worden ze per
naam vergeleken; het script eindigt met exitcode 1 bij een verschil.
"""
import re
import subprocess
import sys


def objdump(*args):
    return subprocess.run(["objdump"] + list(args), capture_output=True, text=True).stdout


def lees(pad):
    """Geeft (namen, base, naam -> ret-bytes)."""
    p = objdump("-p", pad)
    base = 0
    for r in p.splitlines():
        if r.startswith("ImageBase"):
            base = int(r.split()[1], 16)
            break
    eat = {}
    for m in re.finditer(r"\[\s*\d+\]\s*\+base\[\s*(\d+)\]\s+([0-9a-f]{8,})\s+Export RVA", p):
        eat[int(m.group(1))] = int(m.group(2), 16)
    namen = {}
    for m in re.finditer(r"\[\s*\d+\]\s*\+base\[\s*(\d+)\]\s+[0-9a-f]{4}\s+(\S+)", p):
        namen[m.group(2)] = eat.get(int(m.group(1)), 0)

    txt = objdump("-d", "-M", "intel", "-j", ".text", pad)
    regels = []
    for r in txt.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\t([0-9a-f ]+)\t(.*)$", r)
        if m:
            regels.append((int(m.group(1), 16), m.group(3).strip()))
    idx = {a: i for i, (a, _) in enumerate(regels)}

    rets = {}
    for naam, rva in namen.items():
        if rva == 0:
            rets[naam] = None
            continue
        start = base + rva
        i = idx.get(start)
        if i is None:
            # Het adres valt midden in een instructie van de lineaire sweep (objdump disassembleert
            # de hele sectie achter elkaar). Dan gericht een venster disassembleren.
            rets[naam] = zoek_ret(pad, start)
            continue
        gevonden = None
        for j in range(i, min(i + 4000, len(regels))):
            adres, ins = regels[j]
            if adres - start > 4096:
                break
            if ins.startswith("ret"):
                m2 = re.match(r"ret\s+0x([0-9a-f]+)", ins)
                gevonden = int(m2.group(1), 16) if m2 else 0
                break
        if gevonden is None:
            # Sommige functies in het origineel zijn groot en eindigen op een gemeenschappelijke
            # epiloog verderop; dan gericht een venster disassembleren.
            gevonden = zoek_ret(pad, start)
        rets[naam] = gevonden
    return namen, base, rets


def zoek_ret(pad, start):
    """Disassembleert vanaf exact dit adres en geeft de bytes van de eerste `ret`.

    Het venster is ruim: sommige functies in het origineel zijn groot en eindigen op een
    gemeenschappelijke epiloog verderop (LogIn_MatrixNet springt bijvoorbeeld naar 0x5f6c)."""
    uit = objdump("-d", "-M", "intel", "-j", ".text",
                  "--start-address=0x%x" % start, "--stop-address=0x%x" % (start + 4096), pad)
    for r in uit.splitlines():
        m = re.match(r"^\s*[0-9a-f]+:\t[0-9a-f ]+\t(.*)$", r)
        if not m:
            continue
        ins = m.group(1).strip()
        if ins.startswith("ret"):
            m2 = re.match(r"ret\s+0x([0-9a-f]+)", ins)
            return int(m2.group(1), 16) if m2 else 0
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    onze = sys.argv[1]
    n_onze, _, r_onze = lees(onze)
    print("%s: %d exports" % (onze, len(n_onze)))

    if len(sys.argv) < 3:
        for naam in sorted(n_onze):
            print("  %-52s ret %s" % (naam, r_onze[naam]))
        return 0

    orig = sys.argv[2]
    n_orig, _, r_orig = lees(orig)
    print("%s: %d exports" % (orig, len(n_orig)))
    print()

    fouten = 0
    ontbreekt = sorted(set(n_orig) - set(n_onze))
    extra = sorted(set(n_onze) - set(n_orig))
    if ontbreekt:
        print("ONTBREEKT in %s: %s" % (onze, ", ".join(ontbreekt)))
        fouten += len(ontbreekt)
    if extra:
        print("EXTRA in %s: %s" % (onze, ", ".join(extra)))
        fouten += len(extra)
    if not ontbreekt and not extra:
        print("naam-tabel: identiek (%d namen)" % len(n_orig))

    print()
    print("%-52s %-6s %-6s" % ("naam", "wij", "origineel"))
    for naam in sorted(n_orig):
        if naam not in n_onze:
            continue
        a, b = r_onze[naam], r_orig[naam]
        if a != b:
            print("%-52s %-6s %-6s  VERSCHIL" % (naam, a, b))
            fouten += 1
    if fouten == 0:
        print("(geen verschillen in de argumentgroottes)")
    print()
    print("VERSCHILLEN: %d" % fouten)
    return 1 if fouten else 0


if __name__ == "__main__":
    sys.exit(main())

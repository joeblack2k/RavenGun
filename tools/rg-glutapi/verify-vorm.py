#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify-vorm.py -- controleert per glut-naam WAT librg-glutapi.so hem levert:

  * een sprongstub uit rg_stubs.S (bevat 'jmp *%ecx'): de 122 namen die de echte
    libglut al heeft. De stub kent geen signatuur en kan zichzelf niet aanroepen;
  * een zelfgeschreven C-functie (geen jmp): de 39 namen die wij zelf maken.

Verwacht: precies 122 stubs en precies 39 eigen functies, en wel precies de
namen die gen.py daarvoor aanwijst. Zo is per naam aantoonbaar dat er geen
'call zichzelf' in zit.
"""
import os
import re
import subprocess
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
SO = os.path.join(HIER, "librg-glutapi.so")


def lees(naam):
    with open(os.path.join(HIER, naam), "r", encoding="utf-8", errors="replace") as fh:
        return sorted({r.strip() for r in fh if r.strip()})


def main():
    if not os.path.exists(SO):
        raise SystemExit("geen %s; bouw eerst met build.sh" % SO)

    budgie = lees("names_budgie.txt")
    echt = set(lees("names_real_libglut.txt"))
    verwacht_stub = {n for n in budgie if n in echt}
    verwacht_eigen = {n for n in budgie if n not in echt}

    uit = subprocess.run(["nm", "-D", "--defined-only", SO],
                         capture_output=True, text=True, check=True).stdout
    syms = []
    for regel in uit.splitlines():
        delen = regel.split()
        if len(delen) == 3 and delen[1] == "T":
            syms.append((int(delen[0], 16), delen[2]))
    syms.sort()

    stubs, eigen, fout = [], [], []
    for i, (adres, naam) in enumerate(syms):
        eind = syms[i + 1][0] if i + 1 < len(syms) else adres + 64
        dis = subprocess.run(
            ["objdump", "-d", "--start-address=%d" % adres, "--stop-address=%d" % eind, SO],
            capture_output=True, text=True, check=True).stdout
        # Een sprongstub is herkenbaar aan zijn eigen vorm: hij roept de eerstvolgende
        # instructie aan (de 'call 1f' uit rg_stubs.S, in de disassembly zichtbaar als
        # 'call <adres> <naam+0x5>'), haalt daarna de GOT-basis op en SPRINGT naar de
        # aanroepwijzer. GCC gebruikt in de eigen functies de benoemde
        # __x86.get_pc_thunk.* in plaats van deze zelfgemaakte call/pop.
        is_stub = (re.search(r"call\s+[0-9a-f]+ <%s\+0x5>" % re.escape(naam), dis) is not None
                   and "jmp    *%ecx" in dis)
        (stubs if is_stub else eigen).append(naam)
        if is_stub and naam not in verwacht_stub:
            fout.append("%s is een stub maar hoort eigen te zijn" % naam)
        if not is_stub and naam not in verwacht_eigen:
            fout.append("%s is een eigen functie maar hoort een stub te zijn" % naam)

    print("symbolen in de .so        : %d" % len(syms))
    print("sprongstubs (jmp *%%ecx)   : %d (verwacht %d)" % (len(stubs), len(verwacht_stub)))
    print("eigen C-functies          : %d (verwacht %d)" % (len(eigen), len(verwacht_eigen)))
    if set(stubs) != verwacht_stub:
        fout.append("stubverzameling wijkt af: %s" % sorted(set(stubs) ^ verwacht_stub))
    if set(eigen) != verwacht_eigen:
        fout.append("eigenverzameling wijkt af: %s" % sorted(set(eigen) ^ verwacht_eigen))
    for regel in fout:
        print("  FOUT: %s" % regel)
    if fout or len(syms) != 161:
        print("RESULTAAT: FAIL")
        return 1
    print("RESULTAAT: PASS -- 122 sprongstubs en 39 eigen functies, precies de bedoelde namen")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# DE LEZERS LEZEN: de 3 verwijzingen naar 0x1f392a0 (aim x) en een paar van de 45 naar 0x1a7cb20 (blok).
# Doel: WELKE VOORWAARDE (branch) bewaakt de lezer? Niet alleen het adres.
import subprocess

BIN = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
DELTA = 0x400000

def dis(va, voor, na):
    r = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                        "--adjust-vma=0x%x" % DELTA,
                        "--start-address=%d" % (va - voor), "--stop-address=%d" % (va + na), BIN],
                       capture_output=True, text=True)
    return [l.rstrip() for l in r.stdout.splitlines() if l.strip() and ":" in l and l.strip()[0].isalnum()]

DOELEN = [
    ("AIM X 0x1f392a0", 0xdd5c7a),
    ("AIM X 0x1f392a0", 0xdd5cb0),
    ("AIM X 0x1f392a0", 0xdd6c7a),
    ("BLOK 0x1a7cb20", 0xdcf243),
    ("BLOK 0x1a7cb20", 0xdcf390),
    ("BLOK 0x1a7cb20", 0xdcf3ed),
]

for naam, va in DOELEN:
    print()
    print("############ %s -- rond 0x%x" % (naam, va))
    regels = dis(va, 160, 120)
    # het venster tonen, met de verwijzing zelf gemarkeerd
    for l in regels:
        mark = " <<<" if ("0x%x" % va) in l else ""
        print("    " + l.strip() + mark)
    print("    --- de VERTAKKINGEN in dit venster (wat wordt er getest?):")
    for l in regels:
        t = l.lower()
        if any(k in t for k in ("test", "cmp", "jne", "je ", "jz", "jnz", "jg", "jl", "ja ", "jb ", "jae", "jbe", "setne", "sete")):
            print("      " + l.strip())

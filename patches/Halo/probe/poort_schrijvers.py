#!/usr/bin/env python3
# WIE SCHRIJFT 0x1aa37c0 (de poortbyte per speler)? Lezen vs schrijven, per verwijzing.
# Scannner is eerst gekalibreerd op een bekend antwoord: 0x61e8b4 -> 0x180ae80.
import re, struct, subprocess

BIN = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
d = open(BIN, "rb").read()
DELTA = 0x400000
CODE_LO, CODE_HI = 0x400000, 0x1462000
DOEL = 0x1aa37c0
ANKER = 0x180ae80          # bekend antwoord om de scanner te kalibreren

def dis(va, voor, na):
    r = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                        "--adjust-vma=0x%x" % DELTA,
                        "--start-address=%d" % (va - voor), "--stop-address=%d" % (va + na), BIN],
                       capture_output=True, text=True)
    return [l.strip() for l in r.stdout.splitlines() if l.strip() and ":" in l and l.strip()[0].isalnum()]

def scan(doel):
    uit = []
    for pre1 in (0x48, 0x4C):
        for pre2 in (0x8D, 0x8B):
            for op in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
                pat = bytes([pre1, pre2, op]); i = d.find(pat)
                while i >= 0:
                    va = i + DELTA
                    if CODE_LO <= va < CODE_HI:
                        disp = struct.unpack_from("<i", d, i + 3)[0]
                        if va + 7 + disp == doel:
                            uit.append(va)
                    i = d.find(pat, i + 1)
    return sorted(set(uit))

print("############ 0. KALIBRATIE VAN DE SCANNER (bekend antwoord)")
print("  verwijzingen naar het anker 0x%x: %d  (verwacht: 22)" % (ANKER, len(scan(ANKER))))

refs = scan(DOEL)
print()
print("############ 1. VERWIJZINGEN NAAR 0x%x (de poortbyte): %d" % (DOEL, len(refs)))
for va in refs:
    print("    0x%08x" % va)

print()
print("############ 2. PER VERWIJZING: LEEST of SCHRIJFT de code daar?")
LEZER = re.compile(r'\b(cmp|test|movz|movs|movzb|movsw)\w*\b')
SCHRIJVER = re.compile(r'\bmov\w*\s+%\w+,.*\(|mov[bwlq]?\s+\$0x[0-9a-f]+,.*\(|inc[bwlq]?\s|dec[bwlq]?\s|or[bwlq]?\s|and[bwlq]?\s')
oordeel = {}
for va in refs:
    regels = dis(va, 24, 40)
    schrijft, leest = [], []
    for l in regels:
        if "0x%x" % DOEL in l or True:
            body = l.split("\t")[-1] if "\t" in l else l
            if LEZER.search(body):
                leest.append(l)
            if SCHRIJVER.search(body) and "(%r" in body:
                schrijft.append(l)
    # de regel direct na de lea is het interessantst
    na = regels[[i for i, l in enumerate(regels) if "0x%x" % DOEL in l][0]:][:4] if any("0x%x" % DOEL in l for l in regels) else []
    print("  === 0x%08x" % va)
    for l in na:
        print("      " + l)
    if schrijft:
        print("      -> MOGELIJK SCHRIJVEND: %s" % schrijft[0])
        oordeel[va] = "schrijft?"
    else:
        print("      -> alleen lezend in dit venster")
        oordeel[va] = "leest"

print()
print("############ 3. OORDEEL")
s = [v for v, o in oordeel.items() if o == "schrijft?"]
print("  verwijzingen met een mogelijke SCHRIJF in de buurt: %d van %d" % (len(s), len(refs)))
for va in s[:8]:
    print("    === 0x%08x:" % va)
    for l in dis(va, 32, 56)[:14]:
        print("        " + l)

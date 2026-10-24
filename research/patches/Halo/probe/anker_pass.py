#!/usr/bin/env python3
# DE BEVESTIGDE-ANKER-PASS: wie raakt 0x180ae80 aan, en WAT staat er naast de schakelaars?
import struct, subprocess

BIN = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
d = open(BIN, "rb").read()
DELTA = 0x400000
CODE_LO, CODE_HI = 0x400000, 0x1462000
DATA_LO, DATA_HI = 0x1661000, 0x23D3000
ANKER = 0x180ae80

def dis(va, voor, na, n=40):
    r = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                        "--adjust-vma=0x%x" % DELTA,
                        "--start-address=%d" % (va - voor), "--stop-address=%d" % (va + na), BIN],
                       capture_output=True, text=True)
    return [l.strip() for l in r.stdout.splitlines() if l.strip() and ":" in l and l.strip()[0].isalnum()][:n]

print("############ 1. ALLE RIP-RELATIEVE VERWIJZINGEN NAAR HET ANKER 0x%x" % ANKER)
patronen = []
for op in (0x05, 0x0d, 0x15, 0x1d, 0x25, 0x2d, 0x35, 0x3d):
    for pre in (b"\x48\x8d", b"\x48\x8b", b"\x4c\x8d", b"\x4c\x8b"):
        patronen.append((pre + bytes([op]), 7))
plekken = []
for pat, ln in patronen:
    i = d.find(pat)
    while i >= 0:
        va = i + DELTA
        if CODE_LO <= va < CODE_HI:
            disp = struct.unpack_from("<i", d, i + len(pat) - 4)[0]
            if va + ln + disp == ANKER:
                plekken.append((va, d[i:i+3].hex()))
        i = d.find(pat, i + 1)
plekken.sort()
print("  aantal verwijzingen: %d" % len(plekken))
for va, b in plekken[:12]:
    print("    0x%08x  (%s)" % (va, b))

print()
print("############ 2. WAT STAAT ER IN DIE FUNCTIES NOG MEER AAN DATA? (de buren van de schakelaars)")
buren = {}
for va, _b in plekken[:12]:
    regels = dis(va, 200, 120, 90)
    for l in regels:
        if "#" in l and "0x" in l.split("#")[-1]:
            try:
                tgt = int(l.split("#")[-1].strip().split()[0], 16)
            except Exception:
                continue
            if DATA_LO <= tgt < DATA_HI and tgt != ANKER:
                buren.setdefault(tgt, []).append(va)
for tgt in sorted(buren, key=lambda t: -len(buren[t]))[:14]:
    print("    0x%08x  genoemd vanuit %d functie(s)/plek(ken): %s" %
          (tgt, len(buren[tgt]), ", ".join("0x%x" % v for v in buren[tgt][:4])))

print()
print("############ 3. DE EERSTE TWEE VERWIJZINGEN, INSTRUCTIE VOOR INSTRUCTIE")
for va, _b in plekken[:2]:
    print("  === rond 0x%x:" % va)
    for l in dis(va, 96, 96, 34):
        print("      " + l)

print()
print("############ 4. EN DE STRUCTUUR ZELF: wat staat er in het bordblok naast de schakelaars?")
print("  bord 0 begint op 0x%x; 32 schakelaars van 0x50 = 0x%x bytes -> tot 0x%x" % (ANKER, 32*0x50, ANKER+32*0x50))
print("  per bord 0x2200, dus de rest van het bordblok (0x%x..0x%x) is 'iets anders'" % (ANKER+32*0x50, ANKER+0x2200))
# laat de eerste 32 bytes van een paar schakelaaringangen zien zoals ze in het BESTAND staan (beginwaarden)
for k in (0, 1, 2, 4):
    off = ANKER + k*0x50 - DELTA
    if 0 <= off < len(d):
        rij = struct.unpack_from("<8I", d, off)
        print("    in het bestand: ingang %d (0x%x): %s" % (k, ANKER + k*0x50, " ".join("%08x" % x for x in rij)))

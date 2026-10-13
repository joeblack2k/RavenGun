#!/usr/bin/env python3
# HERLEZING VANAF DE FUNCTIENAMEN: vind de namen, vind wie ze aanraakt, en kijk wat daar staat.
# Werkende disassembler: objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000
import struct, subprocess

BIN = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
d = open(BIN, "rb").read()
DELTA = 0x400000                      # geldt voor BEIDE segmenten (vaddr - offset)
CODE_LO, CODE_HI = 0x400000, 0x1462000
DATA_LO, DATA_HI = 0x1661000, 0x23D3000

def off2va(off):
    va = off + DELTA
    return va
def va2off(va):
    return va - DELTA

def dis(va, voor=48, na=48, n=16):
    r = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                        "--adjust-vma=0x%x" % DELTA,
                        "--start-address=%d" % (va - voor), "--stop-address=%d" % (va + na), BIN],
                       capture_output=True, text=True)
    regels = [l.strip() for l in r.stdout.splitlines() if l.strip() and l.strip()[0].isalnum() and ":" in l]
    return regels[:n]

namen = ["GunCalProc", "GunCameraTestProc", "GunInfo_NumAttachedGuns_VALUE",
         "GunInfo_AttachedSerials_VALUE", "QRSetupProc", "GunCamTest"]

print("############ 1. DE NAMEN EN HUN ADRESSEN")
adressen = {}
for naam in namen:
    i = d.find(naam.encode())
    if i < 0:
        print("  %-34s niet gevonden als tekst" % naam)
        continue
    # het adres van de STRING (in welk segment dan ook)
    va = off2va(i)
    seg = "CODE/rodata" if va < CODE_HI else "data"
    adressen[naam] = va
    print("  %-34s tekst op 0x%08x (%s), bestand 0x%x" % (naam, va, seg, i))

print()
print("############ 2. WIE VERWIJST ER NAAR DIE TEKST? (RIP-relatief, lea/mov)")
# patronen: 48 8d 05/0d/15/1d/25/2d/35/3d (lea r64, [rip+disp32]) en 48 8b .. idem, en 4c 8d ..
patronen = []
for op in (0x05, 0x0d, 0x15, 0x1d, 0x25, 0x2d, 0x35, 0x3d):
    patronen.append((bytes([0x48, 0x8d, op]), 7))
    patronen.append((bytes([0x48, 0x8b, op]), 7))
    patronen.append((bytes([0x4c, 0x8d, op]), 7))
treffers = {}
for pat, ln in patronen:
    i = d.find(pat)
    while i >= 0:
        va = off2va(i)
        if CODE_LO <= va < CODE_HI:
            disp = struct.unpack_from("<i", d, i + len(pat) - 4)[0]
            tgt = va + ln + disp
            for naam, ada in adressen.items():
                if tgt == ada:
                    treffers.setdefault(naam, []).append(va)
        i = d.find(pat, i + 1)

for naam, vas in treffers.items():
    print("  %-34s %d verwijzing(en): %s" % (naam, len(vas), ", ".join("0x%x" % v for v in vas[:5])))
if not treffers:
    print("  geen enkele RIP-relatieve verwijzing naar die namen (dan is het een naamtabel zonder lezer)")

print()
print("############ 3. DE CODE ROND DE EERSTE VERWIJZING (wat bouwt hij daar?)")
for naam, vas in list(treffers.items())[:2]:
    for va in vas[:1]:
        print("  === %s, rond 0x%x:" % (naam, va))
        for l in dis(va, 64, 64, 18):
            print("      " + l)

print()
print("############ 4. STAAT ER EEN TABEL MET FUNCTIEPUNTNERS ROND DIE NAMEN? (data -> code-verwijzingen)")
# zoek in het datasegment naar 8-byte waarden die in het codevenster vallen, in de buurt van de namen
for naam, ada in adressen.items():
    lo, hi = max(DATA_LO, ada - 0x400), min(DATA_HI, ada + 0x400)
    if lo >= hi:
        continue
    ptrs = []
    for va in range(lo & ~7, hi, 8):
        off = va2off(va)
        if off + 8 > len(d):
            continue
        val = struct.unpack_from("<Q", d, off)[0]
        if CODE_LO <= val < CODE_HI:
            ptrs.append((va, val))
    if ptrs:
        print("  === in de buurt van %s (%d functiepunten):" % (naam, len(ptrs)))
        for va, val in ptrs[:8]:
            print("      0x%08x -> 0x%08x" % (va, val))

#!/usr/bin/env python3
# DE TABEL: waar staat de 8-byte pointer naar 0xdd57e0, en WIE leest die tabel?
import struct, subprocess

BIN = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
d = open(BIN, "rb").read()
DELTA = 0x400000
CODE_LO, CODE_HI = 0x400000, 0x1462000
DATA_LO, DATA_HI = 0x1661000, 0x23D3000
FUNC = 0xdd57e0
ANKER = 0x180ae80          # kalibratie van de scanner

def dis(va, voor, na, n=26):
    r = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                        "--adjust-vma=0x%x" % DELTA,
                        "--start-address=%d" % (va - voor), "--stop-address=%d" % (va + na), BIN],
                       capture_output=True, text=True)
    return [l.strip() for l in r.stdout.splitlines() if l.strip() and ":" in l and l.strip()[0].isalnum()][:n]

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

print("############ 0. KALIBRATIE: verwijzingen naar het anker 0x%x = %d (verwacht 22)" % (ANKER, len(scan(ANKER))))

print()
print("############ 1. WAAR STAAT DE 8-BYTE POINTER NAAR 0x%x?" % FUNC)
pat = struct.pack("<Q", FUNC)
hits, i = [], d.find(pat)
while i >= 0:
    hits.append(i + DELTA)
    i = d.find(pat, i + 1)
print("  aantal plekken: %d" % len(hits))
tabel_adres = None
for va in hits[:6]:
    off = va - DELTA
    rij = []
    for k in range(-3, 5):
        o = off + k * 8
        if 0 <= o <= len(d) - 8:
            rij.append("%08x" % struct.unpack_from("<Q", d, o)[0])
    seg = "CODE/rodata" if va < CODE_HI else "DATA"
    print("    0x%08x (%s)  %s" % (va, seg, " ".join(rij)))
    if tabel_adres is None and DATA_LO <= va < DATA_HI:
        tabel_adres = va - 8      # een tabel begint meestal een paar woorden eerder
print("  ik neem als tabelbasis: 0x%x" % (tabel_adres or 0))

print()
print("############ 2. WIE LEEST DIE TABEL? (RIP-relatief naar de tabelbasis of zijn buren)")
kandidaten = [tabel_adres, tabel_adres + 8, tabel_adres + 16, tabel_adres + 24] if tabel_adres else []
for t in kandidaten:
    if not t:
        continue
    refs = scan(t)
    print("  tabel 0x%x -> %d verwijzing(en): %s" % (t, len(refs), ", ".join("0x%x" % v for v in refs[:6])))

print()
print("############ 3. DE CODE ROND DE EERSTE VERWIJZINGEN (wat doet de lezer ermee?)")
for t in kandidaten:
    if not t:
        continue
    for va in scan(t)[:2]:
        print("  === tabel 0x%x, gelezen op 0x%x:" % (t, va))
        for l in dis(va, 48, 40):
            print("      " + l)
        break

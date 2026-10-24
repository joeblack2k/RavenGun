#!/usr/bin/env python3
# Het echte entry blijkt 0xdd57e8 te zijn (dat staat als DATAwoord in het bestand). Zoek de tabel en de lezer.
import struct, subprocess

BIN = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
d = open(BIN, "rb").read()
DELTA = 0x400000
CODE_LO, CODE_HI = 0x400000, 0x1462000
DATA_LO, DATA_HI = 0x1661000, 0x23D3000
DOEL = 0xdd57e8

def dis(va, voor, na, n=24):
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

print("############ 1. WAAR STAAT 0x%x ALS 8-BYTE WOORD? (dat is de tabel)" % DOEL)
pat = struct.pack("<Q", DOEL)
plekken, i = [], d.find(pat)
while i >= 0:
    plekken.append(i + DELTA); i = d.find(pat, i + 1)
print("  aantal: %d" % len(plekken))
tabel = None
for va in plekken[:5]:
    off = va - DELTA
    rij = []
    for k in range(-4, 8):
        o = off + k * 8
        if 0 <= o <= len(d) - 8:
            rij.append("%08x" % struct.unpack_from("<Q", d, o)[0])
    seg = "CODE" if va < CODE_HI else "DATA"
    print("    0x%08x (%s): %s" % (va, seg, " ".join(rij)))
    if tabel is None and DATA_LO <= va < DATA_HI:
        tabel = va - 32
print("  tabelbasis (geschat): 0x%x" % (tabel or 0))

print()
print("############ 2. WIE LEEST DIE TABEL?")
for t in ([tabel, tabel + 8, tabel + 16, tabel + 24, tabel + 32] if tabel else []):
    refs = scan(t)
    print("  0x%x -> %d verwijzing(en): %s" % (t, len(refs), ", ".join("0x%x" % v for v in refs[:5])))

print()
print("############ 3. DE EERSTE LEZER, INSTRUCTIE VOOR INSTRUCTIE")
if tabel:
    for t in (tabel, tabel + 8, tabel + 16, tabel + 24, tabel + 32):
        refs = scan(t)
        if refs:
            print("  === tabelwoord 0x%x, gelezen op 0x%x:" % (t, refs[0]))
            for l in dis(refs[0], 56, 48):
                print("      " + l)
            break

#!/usr/bin/env python3
# HERLEZING: hoe wordt de invoerstructuur werkelijk benaderd?
#   1) wat doet de getter op 0x61e890?
#   2) wie roept hem aan (call e8 rel32)?
#   3) bestaan de CONSTANTEN van mijn lezing wel in de code? (0x1348 blokstap, 0xc8 knopoffset, 8 aimstap)
import struct, subprocess

BIN = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
d = open(BIN, "rb").read()
phoff = struct.unpack_from("<Q", d, 0x20)[0]
phentsize, phnum = struct.unpack_from("<HH", d, 0x36)
segs = []
for i in range(phnum):
    o = phoff + i * phentsize
    if struct.unpack_from("<I", d, o)[0] == 1:
        off, va, pa, fsz, ms, fl = struct.unpack_from("<QQQQQQ", d, o + 8)
        segs.append((off, va, fsz, fl))

def va2off(va):
    for o, v, fsz, _f in segs:
        if v <= va < v + fsz:
            return o + (va - v)
    return None
def off2va(off):
    for o, v, fsz, _f in segs:
        if o <= off < o + fsz:
            return v + (off - o)
    return None
def dis(va, n_voor=96, n_na=64):
    r = subprocess.run(["objdump", "-D", "--start-address=%d" % (va - n_voor), "--stop-address=%d" % (va + n_na), BIN],
                       capture_output=True, text=True)
    return [l.rstrip() for l in r.stdout.splitlines() if "\t" in l]

CODE_LO, CODE_FSZ = segs[0][1], segs[0][2]
print("  codevenster: 0x%x - 0x%x" % (CODE_LO, CODE_LO + CODE_FSZ))

GETTER = 0x61e890
print()
print("############ 1. DE GETTER OP 0x%x" % GETTER)
for l in dis(GETTER, 64, 96):
    print("    " + l.strip())

print()
print("############ 2. WIE ROEPT DE GETTER AAN? (call e8 rel32 -> 0x%x)" % GETTER)
callers = []
for i in range(segs[0][0], segs[0][0] + CODE_FSZ - 5):
    if d[i] == 0xE8:
        rel = struct.unpack_from("<i", d, i + 1)[0]
        va = off2va(i)
        if va is not None and va + 5 + rel == GETTER:
            callers.append(va)
print("  aantal call-plaatsen: %d" % len(callers))
for va in callers[:4]:
    print("  --- call vanuit 0x%x:" % va)
    for l in dis(va, 48, 40):
        print("      " + l.strip())

print()
print("############ 3. BESTAAN DE CONSTANTEN VAN MIJN LEZING IN DE CODE?")
for naam, waarde, breedte in (("blokstap 0x1348", 0x1348, 4), ("blokstap 0x1348 (2 byte)", 0x1348, 2),
                              ("knopoffset 0xc8 (4 byte)", 0xC8, 4), ("knopoffset 0xc8 (1 byte)", 0xC8, 1),
                              ("spelernr-offset 0x3c (1 byte)", 0x3C, 1)):
    pat = struct.pack("<H", waarde) if breedte == 2 else (struct.pack("<I", waarde) if breedte == 4 else bytes([waarde]))
    treffers, i = [], d.find(pat, segs[0][0])
    while i >= 0 and len(treffers) < 30:
        va = off2va(i)
        if va is not None and CODE_LO <= va < CODE_LO + CODE_FSZ:
            treffers.append(va)
        i = d.find(pat, i + 1)
    print("  %-28s %d treffer(s) in de CODE" % (naam, len(treffers)))
    for va in treffers[:3]:
        print("      --- rond 0x%x:" % va)
        for l in dis(va, 12, 8)[:6]:
            print("          " + l.strip())

print()
print("############ 4. EN DE AIM-REEKS: wordt er met een 8-byte stap over floats gelopen?")
print("  (zoek 'movss' en 'add ...,8' in de buurt van een lea naar een groot adres in het datasegment)")
r = subprocess.run(["objdump", "-D", "--start-address=%d" % CODE_LO, "--stop-address=%d" % (CODE_LO + 0x400000), BIN],
                   capture_output=True, text=True)
regels = [l for l in r.stdout.splitlines() if "movss" in l]
print("  movss-instructies in de eerste 4 MB code: %d" % len(regels))
for l in regels[:6]:
    print("      " + l.strip())

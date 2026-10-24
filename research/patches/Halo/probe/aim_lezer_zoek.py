#!/usr/bin/env python3
# LEEST DE GAME 0x1f392a0? Zoek verwijzingen naar dat adres in de CODE (en naar 0x1f392d0 en 0x1a7cb20).
import struct, subprocess, sys

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
print("  segmenten (offset, vaddr, filesz, flags):")
for s in segs:
    rechten = "R-X" if (s[3] & 1 and not s[3] & 2) else "RW-"
    print("    0x%08x  0x%08x  0x%08x  %s" % (s[0], s[1], s[2], rechten))

def off2va(off):
    for o, va, fsz, _f in segs:
        if o <= off < o + fsz:
            return va + (off - o)
    return None

doelen = {
    "aim x (0x1f392a0)": 0x1f392a0,
    "aim y (0x1f392a4)": 0x1f392a4,
    "spelersaantal (0x1f392d0)": 0x1f392d0,
    "blok 0 (0x1a7cb20)": 0x1a7cb20,
    "blok+0x3c (0x1a7cb5c)": 0x1a7cb5c,
}

for naam, adr in doelen.items():
    print()
    print("############ %s" % naam)
    for bytes_ in (4, 8):
        pat = struct.pack("<I", adr) if bytes_ == 4 else struct.pack("<Q", adr)
        offs, i = [], d.find(pat)
        while i >= 0 and len(offs) < 12:
            offs.append(i)
            i = d.find(pat, i + 1)
        if not offs:
            print("  %d-byte patroon: geen enkele treffer" % bytes_)
            continue
        print("  %d-byte patroon: %d treffer(s)" % (bytes_ if bytes_ == 4 else 8, len(offs)))
        for off in offs:
            va = off2va(off)
            in_code = va is not None and segs[0][1] <= va < segs[0][1] + segs[0][2]
            print("    bestand 0x%08x -> vaddr %s  %s" %
                  (off, ("0x%08x" % va) if va else "?", "CODE" if in_code else "DATA/overig"))

print()
print("############ DE CODE ROND DE EERSTE CODE-TREFFER VAN 0x1f392a0 (objdump)")
pat = struct.pack("<I", 0x1f392a0)
offs, i = [], d.find(pat)
while i >= 0 and len(offs) < 8:
    offs.append(i); i = d.find(pat, i + 1)
code_vas = [off2va(o) for o in offs if off2va(o) and segs[0][1] <= off2va(o) < segs[0][1] + segs[0][2]]
if not code_vas:
    print("  geen code-treffer: het adres wordt nergens met een immediate aangeraakt")
    print("  (dat kan ook betekenen dat de code het via een berekening doet, bv. basis + 8*speler)")
else:
    for va in code_vas[:3]:
        print("  --- rond 0x%x:" % va)
        r = subprocess.run(["objdump", "-D", "--start-address=%d" % (va - 64), "--stop-address=%d" % (va + 32), BIN],
                           capture_output=True, text=True)
        out = [l for l in r.stdout.splitlines() if l.strip() and not l.startswith("Disassembly")]
        for l in out[-12:]:
            print("    " + l.strip())
print()
print("############ OOK: wordt 0x1f392a0 als BASIS + 8*speler gebruikt? (zoek de buurt van de reeks)")
pat = struct.pack("<I", 0x1f392a0)
for off in offs[:4]:
    va = off2va(off)
    if not va: continue
    r = subprocess.run(["objdump", "-D", "--start-address=%d" % (va - 160), "--stop-address=%d" % (va + 96), BIN],
                       capture_output=True, text=True)
    regels = [l for l in r.stdout.splitlines() if "\t" in l]
    if regels:
        print("  --- 0x%x (%d instructies ervoor):" % (va, len(regels)))
        for l in regels[:14]:
            print("    " + l.strip())

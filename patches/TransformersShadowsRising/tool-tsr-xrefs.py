#!/usr/bin/env python3
"""Xrefs in .text van Shell.exe: zoekt absolute 32-bits verwijzingen naar een lijst VAs en
meldt per treffer de instructie (voor `mov [imm],..` = schrijver, anders lezer/kandidaat)."""
import struct
import sys

PATH = sys.argv[1]
VAS = [int(x, 0) for x in sys.argv[2:]]
data = open(PATH, "rb").read()
e = struct.unpack_from("<I", data, 0x3C)[0]
coff = e + 4
numsec = struct.unpack_from("<H", data, coff + 2)[0]
optsize = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
imagebase = struct.unpack_from("<I", data, opt + 28)[0]
secs = []
so = opt + optsize
for i in range(numsec):
    s = so + i * 40
    name = data[s:s + 8].rstrip(b"\0").decode("latin1")
    vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", data, s + 8)
    secs.append((name, vaddr, vsize, rptr, rsize))

for name, vaddr, vsize, rptr, rsize in secs:
    if name != ".text":
        continue
    blob = data[rptr:rptr + rsize]
    for va in VAS:
        pat = struct.pack("<I", va)
        hits = []
        i = blob.find(pat)
        while i != -1:
            # alleen op instructiegrenzen is niet te bepalen zonder disassembler;
            # toon de 8 bytes ervoor zodat het om een [imm] operand te herkennen is.
            pre = blob[max(0, i - 8):i]
            hits.append((imagebase + vaddr + i, pre.hex()))
            i = blob.find(pat, i + 1)
        print("VA 0x%08x  xrefs=%d" % (va, len(hits)))
        for a, pre in hits:
            print("    0x%08x  vooraf=%s" % (a, pre))

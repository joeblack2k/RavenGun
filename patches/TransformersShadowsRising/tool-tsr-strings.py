#!/usr/bin/env python3
"""Print alle strings in een VA-bereik van een PE32 (hier .rdata van Shell.exe rond 0x533800)."""
import struct
import sys

PATH = sys.argv[1]
LO = int(sys.argv[2], 0)
HI = int(sys.argv[3], 0)
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
    lo_va = imagebase + vaddr
    hi_va = lo_va + rsize
    if hi_va < LO or lo_va > HI:
        continue
    blob = data[rptr:rptr + rsize]
    start = None
    for i, ch in enumerate(blob):
        p = 32 <= ch < 127
        if p and start is None:
            start = i
        elif not p and start is not None:
            va = lo_va + start
            if LO <= va <= HI and i - start >= 3:
                print("0x%08x  %r" % (va, blob[start:i].decode("latin1")))
            start = None
    if start is not None and len(blob) - start >= 3:
        va = lo_va + start
        if LO <= va <= HI:
            print("0x%08x  %r" % (va, blob[start:].decode("latin1")))

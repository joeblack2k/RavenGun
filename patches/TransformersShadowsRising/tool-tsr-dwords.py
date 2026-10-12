#!/usr/bin/env python3
"""Dump N dwords (little-endian) vanaf een VA in een PE32, met hun VA ernaast."""
import struct
import sys

PATH = sys.argv[1]
VA = int(sys.argv[2], 0)
N = int(sys.argv[3]) if len(sys.argv) > 3 else 16
BASE = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0
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

def va2off(va):
    rva = va - imagebase
    for name, vaddr, vsize, rptr, rsize in secs:
        if vaddr <= rva < vaddr + rsize:
            return rptr + (rva - vaddr)
    return None

for i in range(N):
    a = VA + i * 4
    off = va2off(a)
    if off is None:
        print("0x%08x  <buiten data>" % a)
        continue
    v = struct.unpack_from("<I", data, off)[0]
    tgt = ""
    if BASE and imagebase <= v < imagebase + 0x2000000:
        tgt = "  -> 0x%08x" % v
    print("0x%08x  [%2d]  0x%08x%s" % (a, i, v, tgt))

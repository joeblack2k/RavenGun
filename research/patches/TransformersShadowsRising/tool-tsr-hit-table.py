#!/usr/bin/env python3
"""Zoek in Shell.exe naar alle plekken waar een pointer naar de naamtabel (0x533800-0x533d00)
in de data staat; dat zijn de knopen van de 'bekende HID-apparaten'-tabel. Print de 12 bytes
ervoor (verwacht: keyA=UsagePage, keyB=Usage) en de naam."""
import struct
import sys

PATH = sys.argv[1]
LO, HI = 0x533800, 0x533d00
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

def cstr(va):
    off = va2off(va)
    if off is None:
        return "?"
    end = off
    while data[end] != 0:
        end += 1
    return data[off:end].decode("latin1")

# Zoek in .data en .rdata naar dwords die naar de naamtabel wijzen.
for name, vaddr, vsize, rptr, rsize in secs:
    if name not in (".data", ".rdata"):
        continue
    blob = data[rptr:rptr + rsize]
    for i in range(0, len(blob) - 4, 4):
        val = struct.unpack_from("<I", blob, i)[0]
        if LO <= val < HI:
            va = imagebase + vaddr + i
            pre = blob[max(0, i - 12):i]
            a = struct.unpack_from("<I", blob, i - 8)[0] if i >= 8 else 0
            b = struct.unpack_from("<I", blob, i - 4)[0] if i >= 4 else 0
            print("naamptr@0x%08x  keyA=0x%04x keyB=0x%04x  %-16r  (sec %s, vooraf %s)"
                  % (va, a & 0xFFFF, b & 0xFFFF, cstr(val), name, pre.hex()))

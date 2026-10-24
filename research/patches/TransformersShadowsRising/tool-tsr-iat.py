#!/usr/bin/env python3
"""Dump de IAT-plekken van Shell.exe met hun importnaam, zodat de 'call DWORD PTR ds:0x....'-regels
in de objdump-uitvoer te lezen zijn."""
import struct
import sys

PATH = sys.argv[1]
data = open(PATH, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
numsec = struct.unpack_from("<H", data, coff + 2)[0]
optsize = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
imagebase = struct.unpack_from("<I", data, opt + 28)[0]
dd = opt + 96                      # PE32
imp_rva, imp_size = struct.unpack_from("<II", data, dd + 8)

secs = []
so = opt + optsize
for i in range(numsec):
    s = so + i * 40
    name = data[s:s + 8].rstrip(b"\0").decode("latin1")
    vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", data, s + 8)
    secs.append((name, vaddr, vsize, rptr, rsize))

def rva2off(rva):
    for name, vaddr, vsize, rptr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return rptr + (rva - vaddr)
    return None

def cstr(off):
    end = off
    while data[end] != 0:
        end += 1
    return data[off:end].decode("latin1")

d = rva2off(imp_rva)
rows = []
while True:
    oft, ts, fc, name_rva, first_thunk = struct.unpack_from("<IIIII", data, d)
    if oft == 0 and name_rva == 0 and first_thunk == 0:
        break
    dll = cstr(rva2off(name_rva))
    thunk = oft or first_thunk
    to = rva2off(thunk)
    i = 0
    while True:
        val = struct.unpack_from("<I", data, to + i * 4)[0]
        if val == 0:
            break
        slot = imagebase + first_thunk + i * 4
        if val & 0x80000000:
            fname = "ord#%d" % (val & 0xFFFF)
        else:
            fname = cstr(rva2off(val) + 2)
        rows.append((slot, dll, fname))
        i += 1
    d += 20

for slot, dll, fname in sorted(rows):
    print("0x%08x  %-14s %s" % (slot, dll, fname))

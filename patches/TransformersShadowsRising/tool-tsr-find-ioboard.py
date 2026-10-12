#!/usr/bin/env python3
"""Vind de HID-IO-bord-code in Shell.exe (PE32) zonder capstone: strings met VA, en xrefs
(absolute imm32) naar die strings. Daarna disassembleren we het venster met objdump -d."""
import struct
import sys

PATH = sys.argv[1] if len(sys.argv) > 1 else "/home/deck/mnt/spidernas_emulation/Teknoparrot/Sega/shell/Shell.exe"
WANT = [
    "HIDioThread : IOBOARD_INITIALISE",
    "HIDioThread : IOBOARD_IDREQUEST1",
    "HIDioThread : IOBOARD_IDREQUEST1_REPLY",
    "HIDioThread : IOBOARD_IDREQUEST2",
    "HIDioThread : IOBOARD_IDREQUEST2_REPLY",
    "HIDioThread : IOBOARD_UNLOCK1",
    "HIDioThread : COMMAND_UNLOCK2",
    "HIDioThread : COMMAND ENABLE KEYPAD",
    "InitIOBoard : Couldn't find IOBoard device",
    "InitIOBoard : Didn't find any HID collections",
    "InitIOBoard : Error reading Device PID and Version",
    "InitIOBoard : Initialised OK",
    "InitIOBoard : SetupDiEnumInterfaceDevice() returned %d",
    "IOBoardVer",
    "ioboard",
]

data = open(PATH, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
assert data[e_lfanew:e_lfanew + 4] == b"PE\0\0", "geen PE"
coff = e_lfanew + 4
machine, numsec = struct.unpack_from("<HH", data, coff)
optsize = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
magic = struct.unpack_from("<H", data, opt)[0]
imagebase = struct.unpack_from("<I", data, opt + 28)[0]
print("machine=0x%04x sections=%d magic=0x%x imagebase=0x%x" % (machine, numsec, magic, imagebase))

secs = []
so = opt + optsize
for i in range(numsec):
    s = so + i * 40
    name = data[s:s + 8].rstrip(b"\0").decode("latin1")
    vsize, vaddr, rsize, rptr = struct.unpack_from("<IIII", data, s + 8)
    secs.append((name, vaddr, vsize, rptr, rsize))
    print("  sec %-8s va=0x%08x vsize=0x%x raw=0x%x rsize=0x%x" % (name, vaddr, vsize, rptr, rsize))

def rva2off(rva):
    for name, vaddr, vsize, rptr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return rptr + (rva - vaddr)
    return None

# strings per sectie, met VA
found = {}
for name, vaddr, vsize, rptr, rsize in secs:
    blob = data[rptr:rptr + rsize]
    start = None
    for i, ch in enumerate(blob):
        printable = 32 <= ch < 127
        if printable and start is None:
            start = i
        elif not printable and start is not None:
            if i - start >= 5:
                found.setdefault(blob[start:i].decode("latin1"), imagebase + vaddr + start)
            start = None
    if start is not None and len(blob) - start >= 5:
        found.setdefault(blob[start:].decode("latin1"), imagebase + vaddr + start)

text = None
for name, vaddr, vsize, rptr, rsize in secs:
    if name == ".text":
        text = (vaddr, rptr, rsize)

for w in WANT:
    va = found.get(w)
    if va is None:
        # prefix-match
        for k, v in found.items():
            if k.startswith(w):
                va, w = v, k
                break
    if va is None:
        print("STRING NOT FOUND: %r" % w)
        continue
    pat = struct.pack("<I", va)
    xrefs = []
    blob = data[text[1]:text[1] + text[2]]
    idx = blob.find(pat)
    while idx != -1:
        xrefs.append(imagebase + text[0] + idx)
        idx = blob.find(pat, idx + 1)
    print("VA=0x%08x  xrefs=%s  %r" % (va, " ".join("0x%x" % x for x in xrefs) or "-", w))

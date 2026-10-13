#!/usr/bin/env python3
"""Final checks on `game`: binding flags, the PLT thunks, and the RIO2/FTDI split."""
import struct, os, sys
G = sys.argv[1] if len(sys.argv) > 1 else r"Z:\Teknoparrot\Halo Fireteam\g7\halo"
d = open(os.path.join(G, "game"), "rb").read()

e_phoff = struct.unpack_from("<Q", d, 32)[0]
e_phentsize, e_phnum = struct.unpack_from("<HH", d, 54)
dyn = None
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    if struct.unpack_from("<I", d, off)[0] == 2:
        dyn = struct.unpack_from("<QQQ", d, off + 8)
o, v, fs = dyn
raw = {}
for k in range(0, fs, 16):
    dt, dv = struct.unpack_from("<qQ", d, o + k)
    if dt == 0:
        break
    raw[dt] = dv
print("DT_FLAGS   (30) = 0x%x   DF_BIND_NOW=0x8 -> %s" % (raw.get(30, 0), bool(raw.get(30, 0) & 8)))
print("DT_FLAGS_1 (0x6ffffffb) = 0x%x  DF_1_NOW=0x1 -> %s"
      % (raw.get(0x6ffffffb, 0), bool(raw.get(0x6ffffffb, 0) & 1)))
print("DT_BIND_NOW (24) present: %s" % (24 in raw))
print("DT_PLTGOT=0x%x  DT_JMPREL=0x%x  DT_PLTRELSZ=0x%x  DT_RELA=0x%x  DT_RELASZ=0x%x"
      % (raw[3], raw[23], raw[2], raw[7], raw[8]))

# how many `ff 25 disp32 ; 68 efbeadde ; e9 rel32` thunks are there, and what
# do their GOT slots point at relative to DT_PLTGOT?
print()
print("PLT-style thunks that push 0xdeadbeef:")
n = 0
i = d.find(struct.pack("<I", 0xdeadbeef))
while i >= 0:
    if d[i - 1:i] == b"\x68" and d[i + 4:i + 5] == b"\xe9" and d[i - 7:i - 6] == b"\xff":
        slot_vaddr = None
        if d[i - 8:i - 7] == b"\x25":
            disp = struct.unpack_from("<i", d, i - 6)[0]
            insn = i - 8
            slot_vaddr = insn + 0x400000 + 6 + disp
        if n < 6:
            print("   thunk at vaddr 0x%08x  GOT slot vaddr %s  (DT_PLTGOT+0x%x)"
                  % (i - 8 + 0x400000, hex(slot_vaddr) if slot_vaddr else "?",
                     slot_vaddr - raw[3] if slot_vaddr else 0))
        n += 1
    i = d.find(struct.pack("<I", 0xdeadbeef), i + 1)
print("   total thunks: %d" % n)

print()
print("RIO2 vs gun-board: which string families exist")
for needle in (b"rio2_", b"ir_", b"cmgr_", b"irgun", b"/dev/tty", b"rio2_init",
               b"io_rio2_loop", b"ir_Fprog_OpenSerial", b"prom_", b"guninfo"):
    print("   %-22s x%d" % (needle.decode(), d.count(needle)))

#!/usr/bin/env python3
"""Window scan around FTDI VID/PID immediates in `game` (read-only, mirror file
verified byte-identical to the NAS copy by sha256)."""
import struct

import sys
GAME = (sys.argv[1] if len(sys.argv) > 1
        else r"Z:\Teknoparrot\Halo Fireteam\g7\halo") + "/game"
with open(GAME, "rb") as f:
    d = f.read()


def hx(b):
    return " ".join("%02x" % x for x in b)


print("=== every mov r32, 0x0403 / 0x6001 with the following 28 bytes")
for val, name in ((0x0403, "0x0403 (FTDI vendor)"), (0x6001, "0x6001 (FT232R product)")):
    pat = struct.pack("<I", val)
    print("--- %s" % name)
    for pre in (b"", b"\x41", b"\x49"):
        for opc in range(0xB8, 0xC0):
            p = pre + bytes([opc]) + pat
            i = d.find(p)
            while i >= 0:
                print("  file 0x%08x vaddr 0x%08x  %-12s | %s"
                      % (i, i + 0x400000, hx(p), hx(d[i + len(p):i + len(p) + 28])))
                i = d.find(p, i + 1)

print()
print("=== adjacent mov pairs (vendor immediately followed by product)")
pairs = [
    ("ba03040000" + "be01600000", "mov edx,0x403 ; mov esi,0x6001"),
    ("be03040000" + "ba01600000", "mov esi,0x403 ; mov edx,0x6001"),
    ("bf03040000" + "be01600000", "mov edi,0x403 ; mov esi,0x6001"),
    ("be03040000" + "bf01600000", "mov esi,0x403 ; mov edi,0x6001"),
    ("b803040000" + "b901600000", "mov eax,0x403 ; mov ecx,0x6001"),
    ("b903040000" + "ba01600000", "mov ecx,0x403 ; mov edx,0x6001"),
    ("bf03040000" + "ba01600000", "mov edi,0x403 ; mov edx,0x6001"),
    ("ba03040000" + "bf01600000", "mov edx,0x403 ; mov edi,0x6001"),
]
for hexpat, what in pairs:
    pat = bytes.fromhex(hexpat)
    i, n, offs = d.find(pat), 0, []
    while i >= 0:
        n += 1
        if len(offs) < 8:
            offs.append(hex(i))
        i = d.find(pat, i + 1)
    if n:
        print("  x%d  %-38s %s" % (n, what, offs))

print()
print("=== 0xdeadbeef occurrences")
pat = struct.pack("<I", 0xEFBEADDE)
i, n = d.find(pat), 0
while i >= 0 and n < 40:
    print("  file 0x%08x vaddr 0x%08x  before: %s"
          % (i, i + 0x400000, hx(d[i - 12:i + 4])))
    n += 1
    i = d.find(pat, i + 1)
print("  total: %d" % d.count(pat))

print()
print("=== 0xdeadbeef in game_original")
with open(GAME.rsplit("/", 1)[0] + "/game_original", "rb") as f:
    o = f.read()
print("  total: %d" % o.count(pat))

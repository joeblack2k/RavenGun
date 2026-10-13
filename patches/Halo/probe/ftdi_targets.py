#!/usr/bin/env python3
"""Resolve the call targets of the six libftdi discovery call sites in `game`,
so a diagnostic preload can inline-hook them by address."""
import struct
import sys
G = (sys.argv[1] if len(sys.argv) > 1
     else r"Z:\Teknoparrot\Halo Fireteam\g7\halo") + "/game"
d = open(G, "rb").read()
BASE = 0x400000

sites = [0x9d0ad1, 0x9d213b, 0x9d3b8f, 0x9d9e52, 0x9da54a, 0x9de23e,
         0x9de1e9]
for s in sites:
    # find the first `e8 rel32` within 64 bytes after the mov pair
    for k in range(s, s + 80):
        if d[k] == 0xE8:
            rel = struct.unpack_from("<i", d, k + 1)[0]
            vaddr = k + BASE
            target = vaddr + 5 + rel
            print("site file 0x%08x (vaddr 0x%08x): call at file 0x%08x -> vaddr 0x%08x"
                  % (s, s + BASE, k, target))
            break
    else:
        print("site file 0x%08x: no call found in the next 80 bytes" % s)

print()
print("call targets seen more than once (these are library functions):")
import collections
c = collections.Counter()
for s in sites:
    for k in range(s, s + 80):
        if d[k] == 0xE8:
            c[k + BASE + 5 + struct.unpack_from("<i", d, k + 1)[0]] += 1
            break
for t, n in c.most_common():
    print("   vaddr 0x%08x  x%d" % (t, n))

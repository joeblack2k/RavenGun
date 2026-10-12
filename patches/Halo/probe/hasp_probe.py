#!/usr/bin/env python3
import struct, hashlib, os, sys
G = sys.argv[1] if len(sys.argv) > 1 else r"Z:\Teknoparrot\Halo Fireteam\g7\halo"
d = open(os.path.join(G, "game"), "rb").read()
h = open(os.path.join(G, "halo.so"), "rb").read()
print("halo.so sha256[:16]:", hashlib.sha256(h).hexdigest()[:16])
pat = struct.pack("<I", 0xdeadbeef)
i, n = d.find(pat), 0
while i >= 0 and n < 40:
    print("  deadbeef file 0x%08x vaddr 0x%08x ctx %s"
          % (i, i + 0x400000, " ".join("%02x" % b for b in d[i - 8:i + 8])))
    n += 1
    i = d.find(pat, i + 1)
print("  game deadbeef total:", d.count(pat))
for needle in (b"comaladdin", b"envspoony", b"HASPE_LINUX_LOADER", b"hasp_login",
               b"hasp_read", b"hasp_get_info", b"/dev/Hardlock", b"libfmod") :
    print("  %-22s game x%-4d halo.so x%d"
          % (needle.decode(), d.count(needle), h.count(needle)))
# what does halo.so know about the game?
print()
print("  halo.so hook-target-looking strings:")
import re
for m in sorted(set(re.findall(rb"[ -~]{6,}", h))):
    t = m.decode()
    if any(k in t for k in ("hasp", "HASP", "comaladdin", "g7", "game", "vendor", "SENTINEL",
                            "sentinel", "aladdin", "Aladdin")):
        print("    ", t[:120])

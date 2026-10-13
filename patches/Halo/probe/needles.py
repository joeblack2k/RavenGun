#!/usr/bin/env python3
"""Read-only needle scan over binaries: total occurrences + first offsets + context."""
import sys, os, hashlib

NEEDLES = sys.argv[2:]


def show_ctx(d, off, before=64, after=120):
    a, b = max(0, off - before), min(len(d), off + after)
    return "".join(chr(c) if 32 <= c < 127 else "." for c in d[a:b])


for path in sys.argv[1].split(";"):
    with open(path, "rb") as f:
        d = f.read()
    print("=" * 78)
    print("== %s (%d bytes) sha256=%s" % (os.path.basename(path), len(d),
                                          hashlib.sha256(d).hexdigest()[:16]))
    for n in NEEDLES:
        nb = n.encode()
        offs, i, total = [], d.find(nb), 0
        while i >= 0:
            total += 1
            if len(offs) < 8:
                offs.append(i)
            i = d.find(nb, i + 1)
        if total:
            print("   [%s] x%d  %s" % (n, total, [hex(x) for x in offs]))
            for off in offs[:3]:
                print("        @" + hex(off) + "  " + show_ctx(d, off))
        else:
            print("   [%s] ABSENT" % n)

#!/usr/bin/env python3
"""Hex+ASCII dump of a file range, plus a list of the printable strings in a range.
Read-only. Usage: dump_range.py <file> <hex_off> <hex_len>"""
import sys, hashlib

path, off, ln = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
with open(path, "rb") as f:
    f.seek(off)
    d = f.read(ln)
print("== %s  offset 0x%x  len 0x%x" % (path, off, ln))
for i in range(0, len(d), 16):
    row = d[i:i + 16]
    hexs = " ".join("%02x" % b for b in row)
    txt = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
    print("%08x  %-47s  %s" % (off + i, hexs, txt))
print()
print("-- strings (min 4) --")
cur, start = b"", off
for i, b in enumerate(d):
    if 32 <= b < 127:
        if not cur:
            start = off + i
        cur += bytes([b])
    else:
        if len(cur) >= 4:
            print("  0x%08x  %s" % (start, cur.decode()))
        cur = b""
if len(cur) >= 4:
    print("  0x%08x  %s" % (start, cur.decode()))

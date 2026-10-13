#!/usr/bin/env python3
"""Dump DT_STRTAB, DT_HASH counts and the relocation->symbol index map for an ELF64.
Read-only."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elf_probe import parse

path = sys.argv[1]
with open(path, "rb") as f:
    d = f.read()
r = parse(path)
loads = r["loads"]


def v2o(v):
    for o, va, fs, ms, fl in loads:
        if va <= v < va + fs:
            return v - va + o
    return None


# re-read raw dynamic entries to keep the file offsets we need
e_phoff = struct.unpack_from("<Q", d, 32)[0]
e_phentsize, e_phnum = struct.unpack_from("<HH", d, 54)
dyn = None
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type = struct.unpack_from("<I", d, off)[0]
    if p_type == 2:
        dyn = struct.unpack_from("<QQQ", d, off + 8)  # offset, vaddr, filesz
        break
raw = {}
o, v, fs = dyn
for k in range(0, fs, 16):
    dt, dv = struct.unpack_from("<qQ", d, o + k)
    if dt == 0:
        break
    raw[dt] = dv

print("== %s" % path)
for k in sorted(raw):
    print("   tag 0x%x = 0x%x (%d)" % (k, raw[k], raw[k]))

strv, strsz = raw[5], raw[10]
stro = v2o(strv)
blob = d[stro:stro + strsz]
names = [s.decode("utf-8", "replace") for s in blob.split(b"\0")]
print("   DT_STRTAB: %d bytes, %d strings" % (strsz, len(names)))

hasho = v2o(raw[4])
nbucket, nchain = struct.unpack_from("<II", d, hasho)
print("   DT_HASH: nbucket=%d nchain=%d" % (nbucket, nchain))

for key in ("FMOD", "fmod", "hasp", "ftdi", "usb", "gl", "GL", "lapack", "iw_",
            "patch", "cmgr", "pmrt", "X11", "XQuery", "RIO", "rio"):
    hits = sorted({s for s in names if key in s})
    print("   strtab *%s*: %d  %s" % (key, len(hits), hits[:12]))

# relocations
def relocs(off_tag, sz_tag, ent_tag):
    if off_tag not in raw:
        return []
    ro = v2o(raw[off_tag])
    n = raw[sz_tag] // raw[ent_tag]
    out = []
    for i in range(n):
        r_offset, r_info = struct.unpack_from("<QQ", d, ro + i * raw[ent_tag])
        out.append((r_offset, r_info >> 32, r_info & 0xffffffff))
    return out

allr = relocs(7, 8, 9) + relocs(23, 2, 9)
print("   RELA total: %d (DT_RELA %d, DT_JMPREL %d)"
      % (len(allr), len(relocs(7, 8, 9)), len(relocs(23, 2, 9))))
maxsym = max((s for _, s, _ in allr), default=0)
print("   highest symbol index referenced by a relocation: %d (nchain=%d)" % (maxsym, nchain))

#!/usr/bin/env python3
"""Dump the dynamic symbol table of an ELF32 or ELF64 through PT_DYNAMIC.

Needed because the Raw Thrills `game` files have ZERO section headers, so
`readelf --dyn-syms` returns nothing for them (it looks for SHT_DYNSYM in the section
table). Everything here comes from the program headers. Handles DT_HASH and DT_GNU_HASH.

Read-only. Usage: dynsym_probe.py <elf> [filter...]
"""
import sys, struct

FILTERS = sys.argv[2:] or ["hasp_"]


def parse(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF":
        raise SystemExit("not ELF")
    is64 = d[4] == 2
    e_type, e_machine = struct.unpack_from("<HH", d, 16)
    if is64:
        e_entry = struct.unpack_from("<Q", d, 24)[0]
        e_phoff = struct.unpack_from("<Q", d, 32)[0]
        e_phentsize, e_phnum = struct.unpack_from("<HH", d, 54)
    else:
        e_entry = struct.unpack_from("<I", d, 24)[0]
        e_phoff = struct.unpack_from("<I", d, 28)[0]
        e_phentsize, e_phnum = struct.unpack_from("<HH", d, 42)

    loads, dyn = [], None
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        if is64:
            p_type, p_flags = struct.unpack_from("<II", d, o)
            p_offset, p_vaddr, _, p_filesz, p_memsz, _ = struct.unpack_from("<QQQQQQ", d, o + 8)
        else:
            p_type = struct.unpack_from("<I", d, o)[0]
            p_offset, p_vaddr, _, p_filesz, p_memsz, p_flags = struct.unpack_from("<IIIIII", d, o + 4)
        if p_type == 1:
            loads.append((p_offset, p_vaddr, p_filesz, p_memsz, p_flags))
        elif p_type == 2:
            dyn = (p_offset, p_vaddr, p_filesz)

    def v2o(v):
        for o, va, fs, ms, fl in loads:
            if va <= v < va + fs:
                return v - va + o
        return None

    tags = {}
    if dyn:
        o, v, fs = dyn
        step, fmt = (16, "<qQ") if is64 else (8, "<iI")
        for k in range(0, fs, step):
            dt, dv = struct.unpack_from(fmt, d, o + k)
            if dt == 0:
                break
            tags.setdefault(dt, []).append(dv)

    strtab = (tags.get(5) or [0])[0]      # DT_STRTAB
    symtab = (tags.get(6) or [0])[0]      # DT_SYMTAB
    hashv = (tags.get(4) or [None])[0]    # DT_HASH
    gnuhash = (tags.get(0x6ffffef5) or [None])[0]

    n, bron = 0, "geen DT_HASH en geen DT_GNU_HASH"
    if hashv:
        ho = v2o(hashv)
        if ho is not None:
            _, n = struct.unpack_from("<II", d, ho)
            bron = "DT_HASH (nchain)"
    elif gnuhash:
        go = v2o(gnuhash)
        if go is not None:
            nbuckets, symoffset, bloom_size, _ = struct.unpack_from("<IIII", d, go)
            psz = 8 if is64 else 4
            buckets = go + 16 + bloom_size * psz
            bv = struct.unpack_from("<%dI" % nbuckets, d, buckets)
            mx = max(bv) if bv else 0
            n = symoffset
            if mx:
                chains = buckets + nbuckets * 4
                idx = mx
                while True:
                    h = struct.unpack_from("<I", d, chains + (idx - n) * 4)[0]
                    idx += 1
                    if h & 1:
                        break
                n = idx
            bron = "DT_GNU_HASH (nbuckets=%d, symoffset=%d)" % (nbuckets, symoffset)

    syms = []
    if symtab and strtab and n:
        so, sto = v2o(symtab), v2o(strtab)
        entsz = 24 if is64 else 16
        for i in range(n):
            if is64:
                st_name, st_info, st_other, st_shndx, st_value, st_size = \
                    struct.unpack_from("<IBBHQQ", d, so + i * entsz)
            else:
                st_name, st_value, st_size, st_info, st_other, st_shndx = \
                    struct.unpack_from("<IIIBBH", d, so + i * entsz)
            if st_name == 0:
                continue
            e = d.index(b"\0", sto + st_name)
            syms.append((d[sto + st_name:e].decode("utf-8", "replace"), st_shndx, st_value, st_size))
    return {"is64": is64, "e_type": e_type, "e_machine": e_machine, "e_entry": e_entry,
            "loads": loads, "n": n, "syms": syms, "bron": bron}


if __name__ == "__main__":
    for path in sys.argv[1].split(";"):
        r = parse(path)
        laag = min(l[1] for l in r["loads"])
        undef = [s for s in r["syms"] if s[1] == 0]
        defi = [s for s in r["syms"] if s[1] != 0]
        print("== %s" % path)
        print("   class=%s type=%s machine=%s entry=0x%x  syms=%d (%d undef, %d defined)  bron: %s"
              % ("ELF64" if r["is64"] else "ELF32", r["e_type"], r["e_machine"], r["e_entry"],
                 len(r["syms"]), len(undef), len(defi), r["bron"]))
        print("   laagste PT_LOAD-vaddr (wat de hook 'laag' noemt): 0x%x" % laag)
        for f in FILTERS:
            hits = [s for s in r["syms"] if f in s[0]]
            print("   symbolen met %r: %d" % (f, len(hits)))
            for naam, shndx, val, size in sorted(hits, key=lambda s: s[2]):
                print("      %-28s %-16s st_value=0x%08x size=%d"
                      % (naam, "ONGEDEFINIEERD" if shndx == 0 else "gedefinieerd", val, size))

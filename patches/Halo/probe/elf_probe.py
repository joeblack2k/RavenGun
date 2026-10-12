#!/usr/bin/env python3
"""Read-only ELF probe for the Halo: Fireteam Raven game folder on the NAS.

Works on binaries whose section-header table is stripped: every dynamic fact is
read from the PT_DYNAMIC *segment*, and the symbol tables from
DT_SYMTAB/DT_STRTAB/DT_HASH (elf.h layout, never from section headers).

Measured, without executing anything:
  - ELF header / PT_LOAD / PT_DYNAMIC
  - DT_NEEDED / DT_RPATH / DT_RUNPATH / DT_SONAME / DT_FLAGS
  - all UNDEFINED dynamic symbols (imports), grouped
  - whether the binary also carries a real SHT_DYNSYM section table

Usage: elf_probe.py <root> [names...]
"""
import sys, os, struct, collections

DT = {0: "DT_NULL", 1: "DT_NEEDED", 2: "DT_PLTRELSZ", 3: "DT_PLTGOT", 4: "DT_HASH",
      5: "DT_STRTAB", 6: "DT_SYMTAB", 7: "DT_RELA", 8: "DT_RELASZ", 9: "DT_RELAENT",
      10: "DT_STRSZ", 11: "DT_SYMENT", 12: "DT_INIT", 13: "DT_FINI", 14: "DT_SONAME",
      15: "DT_RPATH", 16: "DT_SYMBOLIC", 17: "DT_REL", 18: "DT_RELSZ", 19: "DT_RELENT",
      20: "DT_PLTREL", 21: "DT_DEBUG", 22: "DT_TEXTREL", 23: "DT_JMPREL",
      24: "DT_BIND_NOW", 25: "DT_INIT_ARRAY", 26: "DT_FINI_ARRAY",
      27: "DT_INIT_ARRAYSZ", 28: "DT_FINI_ARRAYSZ", 29: "DT_RUNPATH",
      30: "DT_FLAGS", 32: "DT_PREINIT_ARRAY", 33: "DT_PREINIT_ARRAYSZ",
      0x6ffffef5: "DT_GNU_HASH", 0x6ffffff9: "DT_RELACOUNT",
      0x6ffffffa: "DT_RELCOUNT", 0x6ffffffe: "DT_VERNEED",
      0x6fffffff: "DT_VERNEEDNUM", 0x6ffffff0: "DT_VERSYM"}


def parse(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"\x7fELF":
        return None
    is64 = d[4] == 2
    if not is64:
        raise SystemExit("32-bit binary, this probe only handles ELF64: %s" % path)
    e_type, e_machine = struct.unpack_from("<HH", d, 16)
    e_entry = struct.unpack_from("<Q", d, 24)[0]
    e_phoff = struct.unpack_from("<Q", d, 32)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", d, 54)
    e_shoff = struct.unpack_from("<Q", d, 40)[0]
    e_shnum, e_shstrndx = struct.unpack_from("<HH", d, 60)

    loads, dynamic_ph = [], None
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", d, off)
        p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = \
            struct.unpack_from("<QQQQQQ", d, off + 8)
        if p_type == 1:
            loads.append((p_offset, p_vaddr, p_filesz, p_memsz, p_flags))
        elif p_type == 2:
            dynamic_ph = (p_offset, p_vaddr, p_filesz)

    def v2o(vaddr):
        for o, v, fs, ms, fl in loads:
            if v <= vaddr < v + fs:
                return vaddr - v + o
        return None

    tags = {}
    needed, rpath, runpath, soname = [], None, None, None
    if dynamic_ph:
        o, v, fs = dynamic_ph
        for k in range(0, fs, 16):
            dt, dv = struct.unpack_from("<qQ", d, o + k)
            if dt == 0:
                break
            tags.setdefault(DT.get(dt, hex(dt)), []).append(dv)
        # DT_NEEDED / SONAME / RPATH / RUNPATH d_val is a byte offset into
        # DT_STRTAB, not a virtual address.
        strv = (tags.get("DT_STRTAB") or [None])[0]
        stro = v2o(strv) if strv is not None else None

        def s_at(offoff):
            if stro is None:
                return None
            so = stro + offoff
            return d[so:d.index(b"\0", so)].decode("utf-8", "replace")

        for dv in tags.get("DT_NEEDED", []):
            needed.append(s_at(dv))
        if tags.get("DT_RPATH"):
            rpath = s_at(tags["DT_RPATH"][0])
        if tags.get("DT_RUNPATH"):
            runpath = s_at(tags["DT_RUNPATH"][0])
        if tags.get("DT_SONAME"):
            soname = s_at(tags["DT_SONAME"][0])

    symtab = tags.get("DT_SYMTAB", [None])[0]
    strtab = tags.get("DT_STRTAB", [None])[0]
    syment = (tags.get("DT_SYMENT") or [24])[0]
    n = None
    if "DT_HASH" in tags:
        ho = v2o(tags["DT_HASH"][0])
        _, nchain = struct.unpack_from("<II", d, ho)
        n = nchain
    elif "DT_GNU_HASH" in tags:
        go = v2o(tags["DT_GNU_HASH"][0])
        nbuckets, symoffset, bloom_size, bloom_shift = struct.unpack_from("<IIII", d, go)
        buckets = go + 16 + bloom_size * 8
        buckets_v = struct.unpack_from("<%dI" % nbuckets, d, buckets)
        mx = max(buckets_v) if buckets_v else 0
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

    undef, defined = [], []
    if symtab and strtab and n:
        so, o = v2o(symtab), v2o(strtab)
        for i in range(n):
            st_name, st_info, st_other, st_shndx, st_value, st_size = \
                struct.unpack_from("<IBBHQQ", d, so + i * syment)
            if st_name == 0:
                continue
            e = d.index(b"\0", o + st_name)
            name = d[o + st_name:e].decode("utf-8", "replace")
            (undef if st_shndx == 0 else defined).append(name)

    return {"size": len(d), "e_type": e_type, "e_machine": e_machine,
            "e_entry": e_entry, "loads": loads, "tags": tags, "needed": needed,
            "rpath": rpath, "runpath": runpath, "soname": soname,
            "undef": undef, "defined": defined,
            "shnum": e_shnum, "shstrndx": e_shstrndx, "shoff": e_shoff}


def classify(s):
    for p, g in (("_ZNK4FMOD", "FMOD-cxx"), ("_ZN4FMOD", "FMOD-cxx"),
                 ("FMOD_", "FMOD-c"), ("hasp_", "hasp"), ("ftdi_", "ftdi"),
                 ("usb_", "libusb"), ("libusb_", "libusb"),
                 ("X", "X11"), ("SDL_", "SDL"), ("ntoh", "net"),
                 ("gl", "GL"), ("cmgr_", "cmgr"), ("pmrt_", "pmrt")):
        if s.startswith(p):
            return g
    return "libc/other"


def main():
    root = sys.argv[1]
    names = sys.argv[2:] or ["game", "game_original", "halo.so",
                             os.path.join("utils", "rtinit"),
                             os.path.join("utils", "rtinit.so"),
                             os.path.join("utils", "rtinit_original"), "Patch"]
    for name in names:
        path = os.path.join(root, name)
        if not os.path.isfile(path):
            print("== %-28s MISSING" % name); continue
        r = parse(path)
        print("=" * 78)
        if r is None:
            print("== %-28s %d bytes  NOT-ELF" % (name, os.path.getsize(path))); continue
        print("== %s  (%d bytes)" % (name, r["size"]))
        print("   type=%s machine=%s entry=0x%x  sections: %d (shstrndx=%d) %s"
              % (r["e_type"], r["e_machine"], r["e_entry"], r["shnum"], r["shstrndx"],
                 "STRIPPED" if r["shnum"] == 0 else ""))
        for o, v, fs, ms, fl in r["loads"]:
            print("   PT_LOAD off 0x%08x vaddr 0x%08x filesz 0x%08x memsz 0x%08x flags %d"
                  % (o, v, fs, ms, fl))
        print("   dynamic tags: %s" % ", ".join(sorted(r["tags"])))
        print("   SONAME=%s RPATH=%s RUNPATH=%s" % (r["soname"], r["rpath"], r["runpath"]))
        print("   DT_NEEDED (%d):" % len(r["needed"]))
        for x in r["needed"]:
            print("      %s" % x)
        print("   dynamic symbols: %d undefined, %d defined"
              % (len(r["undef"]), len(r["defined"])))
        groups = collections.defaultdict(list)
        for s in r["undef"]:
            groups[classify(s)].append(s)
        for g in sorted(groups, key=lambda k: -len(groups[k])):
            print("      [%s] %d" % (g, len(groups[g])))
            if g in ("FMOD-cxx", "FMOD-c", "hasp", "ftdi", "libusb", "cmgr", "pmrt"):
                for s in sorted(groups[g]):
                    print("          %s" % s)
        groups2 = collections.defaultdict(list)
        for s in r["defined"]:
            groups2[classify(s)].append(s)
        print("   defined by prefix: %s"
              % {k: len(v) for k, v in sorted(groups2.items(), key=lambda kv: -len(kv[1]))})


if __name__ == "__main__":
    main()

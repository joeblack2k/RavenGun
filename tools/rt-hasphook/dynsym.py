#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# dynsym.py — leest de dynamische symbolentabel van een ELF via PT_DYNAMIC.
#
# WAAROM DIT NODIG IS
# -------------------
# De `game`-bestanden van de ElfLdr2-games zijn door de pakker herschreven: ze hebben **geen
# sectiekoppen** meer. `readelf --dyn-syms` en `nm -D` vallen daarover terug op de sectiekoppen en
# melden dan niets of onzin ("Warning SHT_DYNSYM not found in section headers" — precies wat
# BudgieLoader zelf ook zegt). De dynamische tabel staat er echter nog: hij hangt aan `PT_DYNAMIC`.
# Dit programma loopt die weg, en dat is dezelfde weg die de koppeling in
# tools/rt-hasphook/hasp_inline_hook.c tijdens het draaien gebruikt.
#
# Gebruik:
#   python3 tools/rt-hasphook/dynsym.py <bestand> [<bestand> ...]
#   python3 tools/rt-hasphook/dynsym.py --hasp <bestand>       alleen de hasp-symbolen
#
# Uitvoer per bestand: de PT_LOAD-segmenten, de dynamische tags die er toe doen, het aantal
# symbolen, en (met --hasp) elke `hasp_*`/`comaladdin*`-naam met waarde, grootte en sectie-index.
import struct
import sys


def lees(pad, alleen_hasp=False):
    d = open(pad, "rb").read()
    if d[:4] != b"\x7fELF":
        print("  geen ELF")
        return 1
    is64 = d[4] == 2
    if is64:
        e_phoff, = struct.unpack_from("<Q", d, 0x20)
        e_phentsize, e_phnum = struct.unpack_from("<HH", d, 0x36)
        e_type, e_machine, e_entry = struct.unpack_from("<HHI", d, 0x10)
    else:
        e_phoff, = struct.unpack_from("<I", d, 0x1c)
        e_phentsize, e_phnum = struct.unpack_from("<HH", d, 0x2a)
        e_type, e_machine, e_entry = struct.unpack_from("<HHI", d, 0x10)

    loads = []
    dyn_off = None
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        if is64:
            p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz = struct.unpack_from(
                "<IIQQQQ", d, o)
        else:
            (p_type, p_offset, p_vaddr, p_paddr, p_filesz, _p_memsz,
             p_flags, _p_align) = struct.unpack_from("<IIIIIIII", d, o)
        if p_type == 1:
            loads.append((p_vaddr, p_offset, p_filesz, p_flags))
        if p_type == 2:
            dyn_off = p_offset

    def v2o(v):
        for va, off, sz, _ in loads:
            if va <= v < va + sz:
                return v - va + off
        return None

    print(f"  klasse {'64' if is64 else '32'}-bits, type {e_type}, machine {e_machine}, "
          f"entry {e_entry:#x}, {e_phnum} program headers")
    for va, off, sz, fl in loads:
        r = ("r" if fl & 4 else "-") + ("w" if fl & 2 else "-") + ("x" if fl & 1 else "-")
        print(f"    PT_LOAD {va:#010x} offset {off:#08x} {sz:#x} bytes {r}")
    if dyn_off is None:
        print("  GEEN PT_DYNAMIC: dit bestand heeft geen dynamische tabel")
        return 1

    tags = {}
    o = dyn_off
    while True:
        if is64:
            tag, val = struct.unpack_from("<qQ", d, o)
        else:
            tag, val = struct.unpack_from("<iI", d, o)
        o += 16 if is64 else 8
        if tag == 0:
            break
        tags[tag] = val

    namen = {"DT_HASH": 4, "DT_STRTAB": 5, "DT_SYMTAB": 6, "DT_SYMENT": 11, "DT_JMPREL": 23,
             "DT_PLTRELSZ": 2, "DT_PLTGOT": 3, "DT_GNU_HASH": 0x6FFFFEF5, "DT_RELSZ": 18,
             "DT_RELASZ": 19}
    for naam, tag in namen.items():
        if tag in tags:
            off = v2o(tags[tag])
            print(f"  {naam:12s} {tags[tag]:#x}"
                  + (f" -> offset {off:#x}" if off is not None else " -> BUITEN DE PT_LOAD"))

    n = 0
    if 4 in tags:
        ho = v2o(tags[4])
        if ho is not None:
            n = struct.unpack_from("<I", d, ho + 4)[0]
    if n == 0 and 0x6FFFFEF5 in tags:
        go = v2o(tags[0x6FFFFEF5])
        if go is not None:
            nbuckets, symoffset, bloom_size = struct.unpack_from("<III", d, go)
            w = 8 if is64 else 4
            bo = go + 16 + bloom_size * w
            buckets = struct.unpack_from(f"<{nbuckets}I", d, bo)
            hoogste = max(buckets) if buckets else 0
            if hoogste >= symoffset:
                n = hoogste + 1
                co = bo + nbuckets * 4
                while True:
                    v, = struct.unpack_from("<I", d, co + (n - symoffset) * 4)
                    if v & 1:
                        break
                    n += 1
    print(f"  aantal dynamische symbolen: {n}")
    if n == 0 or 6 not in tags or 5 not in tags:
        return 1

    so, st = v2o(tags[6]), v2o(tags[5])
    res = []
    for i in range(n):
        if is64:
            st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from(
                "<IBBHQQ", d, so + i * 24)
        else:
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
                "<IIIBBH", d, so + i * 16)
        if st_name == 0:
            continue
        try:
            eind = d.index(b"\0", st + st_name)
        except ValueError:
            continue
        naam = d[st + st_name:eind].decode("latin-1")
        if alleen_hasp and not naam.startswith(("hasp_", "comaladdin", "__hasp", "_hasp")):
            continue
        res.append((naam, st_value, st_size, st_info, st_shndx))

    if alleen_hasp:
        print(f"  {len(res)} hasp-symbolen:")
        for naam, v, s, info, shndx in res:
            soort = {0: "UND", 1: "OBJ", 2: "FUNC", 3: "SECT", 4: "FILE"}.get(info & 0xF, "?")
            print(f"    {naam:34s} {soort:4s} waarde {v:#010x}  grootte {s:5d}  shndx {shndx}")
    return 0


def main():
    args = sys.argv[1:]
    alleen = False
    if args and args[0] == "--hasp":
        alleen = True
        args = args[1:]
    if not args:
        print(__doc__ or "gebruik: dynsym.py [--hasp] <bestand> ...")
        return 2
    for pad in args:
        print(f"===== {pad}")
        try:
            lees(pad, alleen)
        except Exception as e:                       # noqa: BLE001 - meetprogramma
            print(f"  FOUT: {e}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

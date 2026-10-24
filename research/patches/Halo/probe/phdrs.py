#!/usr/bin/env python3
"""Dump every ELF64 program header (read-only)."""
import sys, struct

PT = {0: "PT_NULL", 1: "PT_LOAD", 2: "PT_DYNAMIC", 3: "PT_INTERP", 4: "PT_NOTE",
      5: "PT_SHLIB", 6: "PT_PHDR", 7: "PT_TLS", 0x6474e550: "PT_GNU_EH_FRAME",
      0x6474e551: "PT_GNU_STACK", 0x6474e552: "PT_GNU_RELRO",
      0x6474e553: "PT_GNU_PROPERTY"}

for path in sys.argv[1].split(";"):
    with open(path, "rb") as f:
        d = f.read()
    e_phoff = struct.unpack_from("<Q", d, 32)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", d, 54)
    e_ident_class = d[4]
    print("== %s  class=%d e_phoff=0x%x e_phentsize=%d e_phnum=%d"
          % (path, e_ident_class, e_phoff, e_phentsize, e_phnum))
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", d, off)
        p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = \
            struct.unpack_from("<QQQQQQ", d, off + 8)
        inside = "IN" if p_offset + p_filesz <= len(d) else "OUT-OF-FILE"
        print("   [%2d] %-14s flags=%d off=0x%08x vaddr=0x%08x filesz=0x%08x "
              "memsz=0x%08x align=0x%x %s"
              % (i, PT.get(p_type, hex(p_type)), p_flags, p_offset, p_vaddr,
                 p_filesz, p_memsz, p_align, inside))

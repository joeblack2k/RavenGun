#!/usr/bin/env python3
# patches/Terminator/classify-elf.py - read-only classification of Terminator Salvation/game (ELF32 i386).
# Markers of the HASPE-packed class:
#   SHNUM=0, PLT stubs of the form ff 25 <got32> 68 ef be ad de, prefilled GOT with 0xb7xxxxxx,
#   DT_HASH.nchain lowered against the real symbol count (DT_STRTAB-DT_SYMTAB)/16.
import struct, sys, os, collections

P = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation /game"
data = open(P, "rb").read()
print("file            :", P)
print("size            :", len(data))

assert data[:4] == b"\x7fELF"
ei_class, ei_data, ei_ver = data[4], data[5], data[6]
print("EI_CLASS        :", "ELF32" if ei_class == 1 else "ELF64")
print("EI_DATA         :", "LSB" if ei_data == 1 else "MSB")

(e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags,
 e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
print("e_type          :", e_type)
print("e_machine       :", e_machine, "(3=EM_386)")
print("e_entry         : 0x%08x" % e_entry)
print("e_phoff/e_phnum : %d / %d" % (e_phoff, e_phnum))
print("e_shoff/e_shnum : %d / %d" % (e_shoff, e_shnum))
print("e_shstrndx      :", e_shstrndx)
print("MARKER SHNUM=0  :", "YES (packed marker!)" if e_shnum == 0 else "no (section headers present)")

# ---- program headers ----
PT = {0:"PT_NULL",1:"PT_LOAD",2:"PT_DYNAMIC",3:"PT_INTERP",4:"PT_NOTE",5:"PT_SHLIB",
      6:"PT_PHDR",7:"PT_TLS",0x6474e550:"PT_GNU_EH_FRAME",0x6474e551:"PT_GNU_STACK",
      0x6474e552:"PT_GNU_RELRO",0x6474e553:"PT_GNU_PROPERTY"}
ph = []
for i in range(e_phnum):
    o = e_phoff + i * e_phentsize
    p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from("<IIIIIIII", data, o)
    ph.append(dict(i=i,type=p_type,off=p_offset,vaddr=p_vaddr,paddr=p_paddr,
                   filesz=p_filesz,memsz=p_memsz,flags=p_flags,align=p_align))

print("\n=== PROGRAM HEADERS ===")
lo = hi = None
for p in ph:
    f = ("R" if p["flags"]&4 else "-")+("W" if p["flags"]&2 else "-")+("X" if p["flags"]&1 else "-")
    print("  [%2d] %-16s off=0x%08x vaddr=0x%08x filesz=0x%08x memsz=0x%08x %s align=0x%x"
          % (p["i"], PT.get(p["type"], hex(p["type"])), p["off"], p["vaddr"], p["filesz"], p["memsz"], f, p["align"]))
    if p["type"] == 1:
        lo = p["vaddr"] if lo is None else min(lo, p["vaddr"])
        hi = p["vaddr"]+p["memsz"] if hi is None else max(hi, p["vaddr"]+p["memsz"])
print("  PT_LOAD extent : 0x%08x .. 0x%08x (%.2f MB of address space)" % (lo, hi, (hi-lo)/1048576.0))

# entry point inside which PT_LOAD?
for p in ph:
    if p["type"] == 1 and p["vaddr"] <= e_entry < p["vaddr"]+p["memsz"]:
        print("  e_entry in PT_LOAD[%d] (%s), file offset 0x%x"
              % (p["i"], ("R" if p["flags"]&4 else "-")+("W" if p["flags"]&2 else "-")+("X" if p["flags"]&1 else "-"),
                 p["off"] + (e_entry - p["vaddr"])))

# ---- dynamic ----
dyn = None
for p in ph:
    if p["type"] == 2:
        dyn = p
if not dyn:
    print("NO PT_DYNAMIC"); sys.exit(1)
DT = {}
n = dyn["filesz"] // 8
raw = []
for i in range(n):
    tag, val = struct.unpack_from("<iI", data, dyn["off"] + i*8)
    raw.append((tag, val))
    DT.setdefault(tag, []).append(val)
    if tag == 0:
        break

NAME = {1:"DT_NEEDED",2:"DT_PLTRELSZ",3:"DT_PLTGOT",4:"DT_HASH",5:"DT_STRTAB",6:"DT_SYMTAB",
        7:"DT_RELA",8:"DT_RELASZ",9:"DT_RELAENT",10:"DT_STRSZ",11:"DT_SYMENT",12:"DT_INIT",
        13:"DT_FINI",14:"DT_SONAME",15:"DT_RPATH",16:"DT_SYMBOLIC",17:"DT_REL",18:"DT_RELSZ",
        19:"DT_RELENT",20:"DT_PLTREL",21:"DT_DEBUG",22:"DT_TEXTREL",23:"DT_JMPREL",24:"DT_BIND_NOW",
        25:"DT_INIT_ARRAY",26:"DT_FINI_ARRAY",27:"DT_INIT_ARRAYSZ",28:"DT_FINI_ARRAYSZ",
        29:"DT_RUNPATH",30:"DT_FLAGS",32:"DT_PREINIT_ARRAY",33:"DT_PREINIT_ARRAYSZ",
        0x6ffffef5:"DT_GNU_HASH",0x6ffffff0:"DT_VERSYM",0x6ffffff9:"DT_RELACOUNT",
        0x6ffffffa:"DT_RELCOUNT",0x6ffffffb:"DT_FLAGS_1",0x6ffffffc:"DT_VERDEF",
        0x6ffffffd:"DT_VERDEFNUM",0x6ffffffe:"DT_VERNEED",0x6fffffff:"DT_VERNEEDNUM"}

def vaddr2off(va):
    for p in ph:
        if p["type"] == 1 and p["vaddr"] <= va < p["vaddr"]+p["filesz"]:
            return p["off"] + (va - p["vaddr"])
    return None

print("\n=== DYNAMIC (%d entries) ===" % len(raw))
for tag, val in raw:
    print("  0x%08x %-16s 0x%08x %d" % (tag & 0xffffffff, NAME.get(tag, ""), val, val))

# DT_NEEDED strings
if 5 in DT:
    strtab_off = vaddr2off(DT[5][0])
    strsz = DT.get(10, [0])[0]
    print("\n=== DT_NEEDED (%d) ===" % len(DT.get(1, [])))
    for off in DT.get(1, []):
        s = data[strtab_off+off:data.index(b"\0", strtab_off+off)].decode("latin1")
        print("  ", s)

# DT_HASH nchain vs real symbol count
if 4 in DT:
    h = vaddr2off(DT[4][0])
    nbucket, nchain = struct.unpack_from("<II", data, h)
    nchain_real = (DT[5][0] - DT[6][0]) // 16 if (5 in DT and 6 in DT) else None
    print("\n=== DT_HASH ===")
    print("  nbucket        :", nbucket)
    print("  nchain         :", nchain)
    print("  real sym count :", nchain_real, "= (DT_STRTAB - DT_SYMTAB)/16")
    if nchain_real:
        print("  nchain == real :", nchain == nchain_real, " (a LOWERED nchain is the packer marker)")
else:
    print("\n=== DT_HASH: ABSENT (no DT_HASH) ===")
print("  DT_GNU_HASH    :", "present" if 0x6ffffef5 in DT else "absent")

# ---- symbol table ----
symoff = vaddr2off(DT[6][0])
stroff = vaddr2off(DT[5][0])
strsz  = DT.get(10, [0])[0]
nsym = (DT[5][0] - DT[6][0]) // 16
print("\n=== DYNSYM: %d entries ===" % nsym)

def sname(idx):
    if idx == 0: return ""
    o = stroff + idx
    e = data.index(b"\0", o)
    return data[o:e].decode("latin1", "replace")

defined = []; undefined = []
for i in range(nsym):
    st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", data, symoff+i*16)
    nm = sname(st_name)
    rec = (i, nm, st_value, st_info >> 4, st_info & 0xf, st_shndx)
    (defined if st_shndx != 0 else undefined).append(rec)
print("  defined (st_shndx != 0)  :", len(defined))
print("  undefined (st_shndx == 0) :", len(undefined))
funcs = sum(1 for r in undefined if r[3] == 2)
objs  = sum(1 for r in undefined if r[3] == 1)
print("  undefined FUNC / OBJECT  : %d / %d" % (funcs, objs))
empty = sum(1 for r in undefined if r[1] == "")
print("  undefined with EMPTY name:", empty, "(erased-name marker)")
weird = [r for r in undefined if r[1] and not all(32 <= ord(c) < 127 for c in r[1])]
print("  undefined w/ non-ASCII   :", len(weird))
hasp = [r for r in undefined if "hasp" in r[1].lower() or "aladdin" in r[1].lower()]
print("  undefined hasp*/aladdin* :", len(hasp))
print("\n  first 25 undefined imports:")
for r in undefined[:25]:
    print("    [%3d] %-40s bind=%d type=%d" % (r[0], r[1], r[3], r[4]))
print("\n  last 10 undefined imports:")
for r in undefined[-10:]:
    print("    [%3d] %-40s bind=%d type=%d" % (r[0], r[1], r[3], r[4]))

# library-symbol namespace split
libs = collections.Counter(r[1].split("@")[1] for r in undefined if "@" in r[1])
print("\n  versioned undefined:", sum(libs.values()), "over", len(libs), "versions:",
      dict(list(libs.items())[:8]))
dups = [k for k, v in collections.Counter(r[1] for r in undefined).items() if v > 1]
print("  duplicate undefined names:", len(dups), dups[:10])
emptyG = sum(1 for r in defined if r[1] == "")
print("  defined with EMPTY name:", emptyG)

# ---- relocations ----
RT = {0:"R_386_NONE",1:"R_386_32",2:"R_386_PC32",3:"R_386_GOT32",4:"R_386_PLT32",5:"R_386_COPY",
      6:"R_386_GLOB_DAT",7:"R_386_JMP_SLOT",8:"R_386_RELATIVE",9:"R_386_GOTOFF",10:"R_386_GOTPC",
      14:"R_386_TLS_TPOFF",15:"R_386_TLS_IE",16:"R_386_TLS_GOTIE",17:"R_386_TLS_LE",
      18:"R_386_TLS_GD",19:"R_386_TLS_LDM",35:"R_386_TLS_DTPMOD32",36:"R_386_TLS_DTPOFF32",
      37:"R_386_TLS_TPOFF32",42:"R_386_IRELATIVE",43:"R_386_GOT32X"}
def dump_relocs(va, sz, ent, label):
    if va is None or not sz: 
        print("  %s: none" % label); return collections.Counter()
    off = vaddr2off(va)
    c = collections.Counter(); copies = []
    for i in range(sz // ent):
        r_offset, r_info = struct.unpack_from("<II", data, off + i*ent)
        t = r_info & 0xff; sym = r_info >> 8
        c[RT.get(t, "type%d" % t)] += 1
        if t == 5:
            copies.append((r_offset, sym, sname(struct.unpack_from("<I", data, symoff+sym*16)[0])))
    print("  %-10s %d entries at 0x%08x:" % (label+":", sz//ent, va))
    for k, v in c.most_common():
        print("      %-22s %d" % (k, v))
    for r_off, sym, nm in copies[:40]:
        print("      R_386_COPY  offset=0x%08x sym[%d]=%s" % (r_off, sym, nm))
    return c

print("\n=== RELOCATIONS ===")
tot = collections.Counter()
tot += dump_relocs(DT.get(17, [None])[0], DT.get(18, [0])[0], DT.get(19, [0])[0] or 8, "DT_REL")
tot += dump_relocs(DT.get(23, [None])[0], DT.get(2, [0])[0], 8, "DT_JMPREL")
print("  total:", dict(tot))

# ---- PLT analysis: look for the HASPE stub form ff 25 <disp32> 68 ef be ad de ----
print("\n=== PLT / PACKED-STUB SCAN ===")
pos = 0; hits = []
while True:
    i = data.find(b"\xff\x25", pos)
    if i < 0: break
    if i+10 <= len(data):
        disp = struct.unpack_from("<I", data, i+2)[0]
        imm  = data[i+6:i+10]
        hits.append((i, disp, imm))
    pos = i+1
de = [h for h in hits if h[2] == b"\xef\xbe\xad\xde"]
print("  'ff 25' occurrences total        :", len(hits))
print("  ...with 68 ef be ad de (0xdeadbeef):", len(de), "  <-- HASPE packed-stub marker")
print("  raw 0xdeadbeef (ef be ad de) count:", data.count(b"\xef\xbe\xad\xde"))

# walk into .plt-looking region: find the contiguous chain of ff 25 stubs
if hits:
    # find longest run where each stub is 16 bytes apart
    best = []; cur = [hits[0]]
    for h in hits[1:]:
        if h[0] - cur[-1][0] == 16: cur.append(h)
        else:
            if len(cur) > len(best): best = cur
            cur = [h]
    if len(cur) > len(best): best = cur
    print("  longest 16-byte-spaced chain     :", len(best), "stubs @0x%x..0x%x" % (best[0][0], best[-1][0]))
    print("    of which 0xdeadbeef            :", sum(1 for h in best if h[2] == b"\xef\xbe\xad\xde"))
    print("    first 5 disps: ", ["0x%08x" % h[1] for h in best[:5]])

# ---- GOT in-file value over the JMP_SLOT slots: THE packed marker ----
print("\n=== GOT in-file value over the JMP_SLOT slots (THE packed marker) ===")
# Packed games have these slots prefilled IN THE FILE with absolute addresses from a foreign address
# image (0xb7xxxxxx). Unpacked games hold their textbook lazy-PLT value (PLT stub + 6 = 0x08xxxxxx).
if 23 in DT and 2 in DT:
    o = vaddr2off(DT[23][0])
    njmp = DT[2][0] // 8
    pre = lazy = other = 0
    for i in range(njmp):
        r_off, r_info = struct.unpack_from("<II", data, o + i * 8)
        off = vaddr2off(r_off)
        if off is None:
            other += 1
            continue
        v = struct.unpack_from("<I", data, off)[0]
        if (v & 0xff000000) == 0xb7000000:   pre += 1
        elif (v & 0xff000000) == 0x08000000: lazy += 1
        else:                                other += 1
    print("  JMP_SLOT slots                        :", njmp)
    print("  0xb7xxxxxx (foreign absolute, PACKED) :", pre, "   <-- must be 0 for the unpacked class")
    print("  0x08xxxxxx (lazy PLT stub, UNPACKED)  :", lazy)
    print("  other/zero                            :", other)
else:
    print("  (no DT_JMPREL / DT_PLTRELSZ)")

b7 = data[3::4].count(0xb7)
print("\n=== raw byte scan (context only: NOT the marker, it counts code/data noise too) ===")
print("  0xb7xxxxxx-aligned u32 anywhere in file:", b7, "(of %d positions)" % (len(data)//4))

# ---- interp ----
for p in ph:
    if p["type"] == 3:
        print("\n=== PT_INTERP ===")
        print("  ", data[p["off"]:p["off"]+p["filesz"]].rstrip(b"\0").decode("latin1"))
print("\n=== PT_TLS ===")
anytls = False
for p in ph:
    if p["type"] == 7:
        anytls = True
        print("  off=0x%x filesz=0x%x memsz=0x%x align=0x%x" % (p["off"], p["filesz"], p["memsz"], p["align"]))
if not anytls: print("  (none in the executable itself)")
print("  DT_TLS-ish relocs:", {k: v for k, v in tot.items() if "TLS" in k} or "none")

#!/usr/bin/env python3
# Is 0xdd57e0 een GEKENDE/GEËXPORTEERDE functie (dynamische symbolentabel), of wijst er een pointer naar een buuradres?
import struct

BIN = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
d = open(BIN, "rb").read()
DELTA = 0x400000
FUNC = 0xdd57e0

# --- de dynamische sectie lezen (die is er wel; sectiekoppen zijn er niet) ---
phoff = struct.unpack_from("<Q", d, 0x20)[0]
phentsize, phnum = struct.unpack_from("<HH", d, 0x36)
dyn = None
for i in range(phnum):
    o = phoff + i * phentsize
    if struct.unpack_from("<I", d, o)[0] == 2:      # PT_DYNAMIC
        off, va, pa, fsz = struct.unpack_from("<QQQQ", d, o + 8)
        dyn = (off, fsz)
        break
print("############ 1. DE DYNAMISCHE SYMBOLENTABEL")
if not dyn:
    print("  geen PT_DYNAMIC gevonden")
else:
    off, fsz = dyn
    tags = {}
    p = off
    while p < off + fsz:
        tag, val = struct.unpack_from("<QQ", d, p)
        if tag == 0:
            break
        tags.setdefault(tag, val)
        p += 16
    SYMTAB, STRTAB, STRSZ, SYMENT = tags.get(6), tags.get(5), tags.get(10), tags.get(11, 24)
    HASH = tags.get(4)
    print("  DT_SYMTAB=0x%x  DT_STRTAB=0x%x  DT_STRSZ=0x%x  DT_SYMENT=%d  DT_HASH=%s" %
          (SYMTAB or 0, STRTAB or 0, STRSZ or 0, SYMENT, hex(HASH) if HASH else "-"))
    if SYMTAB and STRTAB:
        # aantal symbolen via de hash-tabel (nbucket, nchain) of via een grens
        def lees_naam(soff):
            o = STRTAB - DELTA + soff
            e = d.find(b"\0", o)
            return d[o:e].decode("latin1")
        n = None
        if HASH:
            nb, nc = struct.unpack_from("<II", d, HASH - DELTA)
            n = nc
        print("  aantal symbolen (via hash): %s" % n)
        gevonden = []
        limiet = n or 40000
        for k in range(limiet):
            o = SYMTAB - DELTA + k * SYMENT
            if o + SYMENT > len(d):
                break
            nameoff, info, other, shndx, value, size = struct.unpack_from("<IBBHQQ", d, o)
            if value and abs(value - FUNC) <= 0x2000:
                gevonden.append((value, size, lees_naam(nameoff), info))
        print("  symbolen binnen 0x2000 van 0x%x: %d" % (FUNC, len(gevonden)))
        for value, size, naam, info in sorted(gevonden)[:14]:
            soort = "FUNC" if (info & 0xF) == 2 else "OBJ" if (info & 0xF) == 1 else str(info & 0xF)
            print("    0x%08x  %-6s %-40s maat %d" % (value, soort, naam[:40], size))

print()
print("############ 2. WIJST ER EEN 8-BYTE POINTER NAAR EEN BUUR VAN 0x%x?" % FUNC)
for doel in range(FUNC - 0x40, FUNC + 0x40, 4):
    pat = struct.pack("<Q", doel)
    hits, i = [], d.find(pat)
    while i >= 0 and len(hits) < 3:
        hits.append(i + DELTA)
        i = d.find(pat, i + 1)
    if hits:
        print("  pointer naar 0x%08x op: %s" % (doel, ", ".join("0x%x" % v for v in hits)))

print()
print("############ 3. EN STAAT HET ADRES TOCH ERGENS ALS 4-BYTE WAARDE?")
for doel in (FUNC, FUNC + 4, FUNC - 4):
    pat = struct.pack("<I", doel)
    hits, i = [], d.find(pat)
    while i >= 0 and len(hits) < 4:
        hits.append(i + DELTA)
        i = d.find(pat, i + 1)
    print("  0x%08x als 4-byte: %s" % (doel, ", ".join("0x%x" % v for v in hits) if hits else "niets"))

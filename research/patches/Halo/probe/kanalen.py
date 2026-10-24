#!/usr/bin/env python3
# Waar komen de kanaalnummers van de per-beeld-invoervuller UIT, en wat voor structuren geeft de
# getter voor die nummers terug? Bench-vrij; alleen /tmp/io-brug-game.bin lezen.
import struct, sys
G='/tmp/io-brug-game.bin'
d=open(G,'rb').read()
VA=0x400000
def u32(a): return struct.unpack_from('<I', d, a-VA)[0]
def i32(a): return struct.unpack_from('<i', d, a-VA)[0]
def u64(a): return struct.unpack_from('<Q', d, a-VA)[0]

print("############ 1. DE SCHAKELAAR-SIGNAALNUMMERS (globals die de lezer 0x49eb40 uit het geheugen haalt)")
print("   0x16acc60..0x16acc90 statisch:")
for r in range(0, 0x30, 16):
    a = 0x16acc60 + r
    w = struct.unpack_from('<4I', d, a-VA)
    print("      %#x  %-47s  %s" % (a, " ".join("%02x"%x for x in d[a-VA:a-VA+16]),
          " ".join("%d (0x%x)" % (x, x) for x in w)))
print("   (en als bord/vak volgens de 0x239-tafel: bord=(n-0x239)>>5, vak=(n-0x239)&31)")
for r in range(0, 0x30, 4):
    n = u32(0x16acc60 + r)
    if n == 0: print("      %#x -> 0 (leeg)" % (0x16acc60+r)); continue
    if 0x239 <= n <= 0x298:
        bord=(n-0x239)>>5; vak=(n-0x239)&31
        print("      %#x -> %d (0x%x) = bord %d vak %d  (tafeladres %#x)" % (0x16acc60+r, n, n, bord, vak, 0x1815d20+bord*0x2200+vak*0x50))
    else:
        print("      %#x -> %d (0x%x) = BUITEN de 0x239..0x298-familie" % (0x16acc60+r, n, n))

print()
print("############ 2. DE TAKTIEK VAN DE GETTERS VOOR DE KANAALNUMMERS 793/794 (0x319/0x31a)")
print("   De getter kiest een tak op het nummer; binnen een tak staat een SPRONGTABEL met 4-byte offsets.")
def sprongtabel(tabel, index, wat):
    a = tabel + 4*index
    off = i32(a)
    doel = tabel + off
    return doel

for naam, getter, tabel, basis_van_index, maxidx in (
        ("ANALOOG-getter 0x61ee70", 0x61ee70, 0x11bcd70, 0x201, 0x1c4),
        ("SCHAKELAAR-getter 0x61e890", 0x61e890, 0x11bc68c, 0x204, 0x1b8)):
    print("   --- %s (tak n-0x%x, max %#x, sprongtabel %#x)" % (naam, basis_van_index, maxidx, tabel))
    for n in (0x319, 0x31a, 0x31b, 0x31c, 0x31d, 0x2C2, 0x2C3, 0x2C4, 0x2C5, 0x239, 0x256, 0x257):
        idx = n - basis_van_index
        if idx < 0 or idx > maxidx: print("      n=%#x -> buiten deze tak" % n); continue
        doel = sprongtabel(tabel, idx, naam)
        # het doel is meestal een klein stubje 'lea <vast adres>,%rax; ret'
        code = d[doel-VA:doel-VA+16]
        vast = None
        if code[:3] in (b'\x48\x8d\x05', b'\x48\x8d\x0d'):
            disp = struct.unpack_from('<i', code, 3)[0]
            vast = doel + 7 + disp
        print("      n=%d (0x%x) index %d -> sprongtabeldoel %#x  %s   %s"
              % (n, n, idx, doel, code[:8].hex(),
                 ("geeft vast adres %#x" % vast) if vast else "(geen lea-stub: echte code)"))

print()
print("############ 3. WAT STAAT ER OP DIE VASTE ADRESSEN? (32 dwords, als dwords/bytes)")
for adres in (0x181ce18,):
    off = adres-VA
    print("   %#x:" % adres)
    for r in range(0, 0x40, 16):
        w = struct.unpack_from('<4I', d, off+r)
        print("      +%02x  %-47s  %s" % (r, " ".join("%02x"%x for x in d[off+r:off+r+16]),
              " ".join("%d" % x for x in w)))

print()
print("############ 4. DE GLOBALS ROND DE MASKER-A-LEZER (0x1766f40 buurt kent bekende soortgenoten?)")
for a in (0x1766f40, 0x1771850, 0x177b858):
    print("   %#x = %d (0x%x)" % (a, u32(a), u32(a)))

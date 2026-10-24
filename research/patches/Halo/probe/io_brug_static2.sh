#!/bin/bash
# IO-BRUG-ROUTE stap 2 (bench-vrij): de LEZERS van de twee maskers, de statische inhoud, en de
# statische tafelkaart van bord 0..2. Alleen lezen.
set -u
G=/tmp/io-brug-game.bin
[ -s "$G" ] || { echo "geen $G"; exit 1; }
echo "############ IO-BRUG-ROUTE stap 2 $(date '+%F %H:%M:%S')  sha256:"; sha256sum "$G"

echo
echo "############ A. MASKER A 0x1766f40 -- de drie verwijzingen (0x4af8f0, 0x4af900, 0x4af910)"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 \
  --start-address=0x4af860 --stop-address=0x4af9c0 "$G" 2>&1 | tail -n +8

echo
echo "############ B. MASKER B 0x181d8a0 -- de zeven verwijzingen (0x61cc34 .. 0x61f280)"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 \
  --start-address=0x61cbf0 --stop-address=0x61cd60 "$G" 2>&1 | tail -n +8
echo "  --- en de laatste twee (0x61f280 en omgeving):"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 \
  --start-address=0x61f260 --stop-address=0x61f2c0 "$G" 2>&1 | tail -n +8

echo
echo "############ C. DE STATISCHE INHOUD ROND BEIDE MASKERS (0x80 bytes elk)"
python3 - "$G" <<'PY'
import struct, sys
d=open(sys.argv[1],'rb').read()
def toon(adres, n=0x80):
    off = adres - 0x400000
    print("    %#x:" % adres)
    if off < 0 or off+n > len(d): print("        BUITEN HET BESTAND"); return
    b = d[off:off+n]
    for r in range(0, n, 16):
        print("        +%02x  %-47s  %s" % (r, " ".join("%02x"%x for x in b[r:r+16]),
              " ".join("%08x"%struct.unpack_from('<I',b,r+k)[0] for k in range(0,16,4))))
for a in (0x1766f40, 0x181d8a0): toon(a)

print()
print("    --- en de twee maskers als u32/u64:")
for a in (0x1766f40, 0x181d8a0):
    off=a-0x400000
    print("        %#x: u32=%d (%#x)  bit0x8=%s   u64=%d (%#x)"
          % (a, struct.unpack_from('<I',d,off)[0], struct.unpack_from('<I',d,off)[0],
             "GEZET" if struct.unpack_from('<I',d,off)[0] & 8 else "niet gezet",
             struct.unpack_from('<Q',d,off)[0], struct.unpack_from('<Q',d,off)[0]))
PY

echo
echo "############ D. DE STATISCHE TAFELKAART: bord 0..2, 32 vakken van 0x50 vanaf 0x1812d20"
python3 - "$G" <<'PY'
import struct, sys
d=open(sys.argv[1],'rb').read()
BASIS = 0x180ae80 + 0xaea0
print("    tabelbasis bord 0 = 0x%x (bewezen: mov 0xaea0(%rsi,%rdx,1) in de getter op 0x61e8d9)" % BASIS)
for bord in range(4):
    b = BASIS + bord*0x2200
    print("    --- bord %d = %#x" % (bord, b))
    for s in range(32):
        a = b + s*0x50; off = a-0x400000
        if off < 0 or off+0x50 > len(d): print("        %2d  %#x BUITEN" % (s,a)); continue
        w = struct.unpack_from('<16I', d, off)
        soort = w[0]
        naam = {0:"ANALOOG",1:"schakelaar",2:"schakelaar"}.get(soort, "LEEG/anders")
        print("        vak %2d  %#x  soort=%d (%-12s) [1]=%d [2]=%d [3]=%d [4]=%d [5]=%d [8]=%d"
              % (s, a, soort, naam, w[1], w[2], w[3], w[4], w[5], w[8]))
PY

echo
echo "############ E. alle RIP-verwijzingen naar de HULP-TAFELVAKKEN (bord 0, k=0..12)"
python3 - "$G" <<'PY'
import struct, sys
d=open(sys.argv[1],'rb').read()
PATS = [bytes.fromhex(x) for x in (
 '488d05','488d0d','488d15','488d1d','488d25','488d2d','488d35','488d3d',
 '0f2805','0f2905','f30f1005','f30f1105','8b05','8905','488b05','488905',
 '833d','803d','0fb605','0fbe05','c705','c605','8d05')]
def rip_doelen(doel):
    hits=[]
    for pat in PATS:
        i = d.find(pat)
        while i >= 0:
            disp = struct.unpack_from('<i', d, i+len(pat))[0]
            tgt = (i + 0x400000) + len(pat) + 4 + disp
            if tgt == doel: hits.append(hex(i+0x400000))
            i = d.find(pat, i+1)
    return hits
for k in range(13):
    a = 0x180ae80 + 0xaea0 + k*0x50
    print("    k=%2d  %#x  %s" % (k, a, rip_doelen(a) or "-"))
PY
echo "############ KLAAR $(date '+%F %H:%M:%S')"

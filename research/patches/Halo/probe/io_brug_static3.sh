#!/bin/bash
# IO-BRUG-ROUTE stap 3: de tafelkaart STATISCH, met het bewezen adresmodel.
#   entry(bord, vak) = 0x180ae80 + 0xaea0 + bord*0x2200 + vak*0x50 = 0x1815d20 + bord*0x2200 + vak*0x50
#   entry[0] = soort: 0 = ANALOOG KANAAL, 1 of 2 = SCHAKELAAR  (bewezen uit beide getters)
set -u
G=/tmp/io-brug-game.bin
[ -s "$G" ] || { echo "geen $G"; exit 1; }
echo "############ IO-BRUG-ROUTE stap 3 $(date '+%F %H:%M:%S')"
python3 - "$G" <<'PY'
import struct, sys
d=open(sys.argv[1],'rb').read()
BASIS = 0x180ae80 + 0xaea0
print("    tabelbasis = 0x%x   (controle: 0x180ae80 + 0xaea0)" % BASIS)
print("    eerste bytes van de tafel:")
for r in range(0, 0x100, 16):
    off = BASIS + r
    if off+16 > len(d): break
    b = d[off:off+16]
    print("      +%03x  %-47s  %s" % (r, " ".join("%02x"%x for x in b),
          " ".join("%08x"%struct.unpack_from('<I',d,off+k)[0] for k in range(0,16,4))))
print()
print("    soorten per bord (0=ANALOOG, 1/2=SCHAKELAAR, .=0/leeg):")
for bord in range(8):
    b = BASIS + bord*0x2200
    soorten=[]; extra=[]
    for s in range(32):
        off = b + s*0x50
        if off+0x50 > len(d): soorten.append('?'); continue
        w = struct.unpack_from('<16I', d, off)
        soorten.append(str(w[0]))
        if any(w[1:]): extra.append((s, w))
    print("      bord %d (%#x): %s" % (bord, b, " ".join("%2s"%x for x in soorten)))
    for s, w in extra[:6]:
        print("           vak %2d niet-nul: [0]=%d [1]=%d [2]=%d [3]=%d [4]=%d [5]=%d [6]=%d [8]=%d"
              % (s, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[8]))
PY
echo "############ KLAAR $(date '+%F %H:%M:%S')"

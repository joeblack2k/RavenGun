#!/bin/bash
# 1) NAS-controle: is er echt iets veranderd, of was mijn vingerafdruk de eerste keer onvolledig?
# 2) De rodata-constanten waar de per-beeld-invoervuller zijn KANAALNUMMERS uit laadt.
set -u
NAS="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam"
G=/tmp/io-brug-game.bin
echo "############ 1. NAS-CONTROLE $(date '+%F %H:%M:%S')"
echo "--- dezelfde vingerafdruk twee keer achter elkaar (moet nu identiek zijn):"
for i in 1 2; do
  timeout 60 find "$NAS/g7" -maxdepth 3 -printf '%p %s %T@\n' 2>/dev/null | sort | md5sum | sed "s/^/    keer $i: /"
done
echo "--- welke bestanden in g7 zijn recent (nieuwste 8 op mtime)?"
timeout 60 find "$NAS/g7" -printf '%T@ %TY-%Tm-%Td %TH:%TM %s %p\n' 2>/dev/null | sort -rn | head -8 | sed 's/^/    /'
echo "--- schrijfrechten op de mount (moet read-only zijn voor ons):"
timeout 20 stat -c '    %n  mode=%A  uid=%u gid=%g' "$NAS/g7/halo/game" "$NAS/g7/halo" "$NAS/g7" 2>&1
echo "--- is de mount read-only gemount?"
mount | grep -i spidernas | head -3
echo "--- bestaande bestanden met een mtime van VANDAAG:"
timeout 90 find "$NAS/g7" -newermt '2026-10-13 00:00' -printf '    %TY-%Tm-%Td %TH:%TM %p\n' 2>/dev/null | head -20

echo
echo "############ 2. DE RODATA-CONSTANTEN MET DE KANAALNUMMERS (0x11bf3a0 en 0x11bf3b0)"
python3 - "$G" <<'PY'
import struct, sys
d=open(sys.argv[1],'rb').read()
for basis in (0x11bf3a0, 0x11bf3b0):
    off = basis - 0x400000
    print("    %#x:" % basis)
    for r in range(0, 0x30, 16):
        b = d[off+r:off+r+16]
        w = struct.unpack_from('<4I', d, off+r)
        print("      +%02x  %-47s  %s" % (r, " ".join("%02x"%x for x in b),
              " ".join("%d (0x%x)" % (x, x) for x in w)))
        # en als bord/vak volgens de getter-formule
        for x in w:
            if x == 0: continue
            if 0x239 <= x <= 0x298:
                bord = (x - 0x239) >> 5; vak = (x - 0x239) & 31
                print("           %d (0x%x) -> bord %d, vak %d  (adres %#x)" % (x, x, bord, vak, 0x1815d20 + bord*0x2200 + vak*0x50))
            else:
                print("           %d (0x%x) -> BUITEN 0x239..0x298" % (x, x))
PY

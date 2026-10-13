#!/bin/bash
# Vóór de proef: wat staat er OP de twee analoogkanalen, en wat is de constante waarmee de aim
# vergeleken wordt? Plus de tak waar succes landt (0x6243f0).
set -u
G=/tmp/io-brug-game.bin
echo "############ A. DE TWEE ANALOOGKANALEN STATISCH (0x181d418 en 0x181d538, 0x40 bytes)"
python3 - "$G" <<'PY'
import struct, sys
d=open(sys.argv[1],'rb').read()
for a in (0x181d418, 0x181d538):
    off=a-0x400000
    print("    %#x:" % a)
    for r in range(0, 0x40, 16):
        w=struct.unpack_from('<4I',d,off+r); f=struct.unpack_from('<4f',d,off+r)
        print("      +%02x  %-47s u32=%s  f32=%s" % (r, " ".join("%02x"%x for x in d[off+r:off+r+16]),
              " ".join("%d"%x for x in w), " ".join("%g"%x for x in f)))
    print("      +0x14 = %#x -> f32=%g" % (a+0x14, struct.unpack_from('<f',d,off+0x14)[0]))
    print("      +0x18 = %#x -> f32=%g" % (a+0x18, struct.unpack_from('<f',d,off+0x18)[0]))
PY
echo
echo "############ B. DE CONSTANTEN DIE DE VULLER GEBRUIKT"
python3 - "$G" <<'PY'
import struct, sys
d=open(sys.argv[1],'rb').read()
for a in (0x1139ba4, 0x113862c, 0x113d808, 0x11398ac, 0x113c2a0):
    off=a-0x400000
    u=struct.unpack_from('<I',d,off)[0]; f=struct.unpack_from('<f',d,off)[0]
    print("    %#x : u32=0x%08x f32=%g" % (a,u,f))
PY
echo
echo "############ C. DE TAK WAAR SUCCES LANDT (0x6243f0 e.v.) en de kalibratie 0xdccc80"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 --start-address=0x6243e0 --stop-address=0x624450 "$G" 2>&1 | tail -n +8
echo "  --- en 0xdd6c60 (de lezer van het aim-paar, met de kalibratie erin)"
objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 --start-address=0xdd6c60 --stop-address=0xdd6d20 "$G" 2>&1 | tail -n +8

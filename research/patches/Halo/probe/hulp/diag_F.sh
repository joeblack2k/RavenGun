#!/bin/bash
# Sectie F apart draaien, met veel praatjes, om te zien waar hij sterft.
G=/tmp/io-brug-game.bin
echo "start $(date '+%T')  geheugen:"; free -m | head -2
python3 -u - "$G" <<'PY' 2>&1
import struct, sys, traceback
try:
    d = open(sys.argv[1],'rb').read()
    print("gelezen: %d bytes" % len(d), flush=True)
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
    for t in (0x180ae80, 0x180aec8, 0x1812d20, 0x1766f40, 0x181d8a0, 0x1f392a0):
        print("  %#x -> %s" % (t, rip_doelen(t)), flush=True)
    print("patroonscan klaar", flush=True)
except Exception:
    traceback.print_exc()
PY
echo "einde $(date '+%T')"

#!/bin/bash
# De binary heeft GEEN section headers; objdump -D loopt dan niets af. Oplossing: als ruwe blob lezen,
# met de juiste architectuur en met --adjust-vma=0x400000 (beide segmenten hebben vaddr - offset = 0x400000).
B="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
DIS() { objdump -D -b binary -m i386:x86-64 --adjust-vma=0x400000 --start-address=$1 --stop-address=$2 "$B" 2>&1 | grep -E '^\s+[0-9a-f]+:' | head -${3:-20}; }

echo "############ 1. DE GETTER OP 0x61e890 (wat geeft hij terug?)"
DIS 0x61e880 0x61e920 24 | sed 's/^/  /'

echo
echo "############ 2. EEN AANROEP VANUIT 0x49ebc2 (hoe wordt het resultaat gebruikt?)"
DIS 0x49eba0 0x49ec00 26 | sed 's/^/  /'

echo
echo "############ 3. ZOEK IN DIE VENSTERS NAAR EEN BASIS: lea/mov met een groot datasegment-adres"
DIS 0x49eba0 0x49ec00 200 | grep -iE 'lea|movabs|mov.*0x1[0-9a-f]{6}' | head -12 | sed 's/^/  /'

echo
echo "############ 4. EN DE KALE VRAAG: staat er in de hele code een RIP-relatieve verwijzing naar 0x1f392a0?"
python3 - <<'PY'
import struct
B="/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
d=open(B,'rb').read()
# beide segmenten: offset -> vaddr met delta 0x400000
doel={0x1f392a0:'aim x',0x1f392a4:'aim y',0x1f392d0:'spelersaantal',0x1a7cb20:'blok0',0x1a7cb5c:'blok+0x3c'}
# scan op lea/mov rip-relatieve vormen: 48 8d 05/0d/15/1d/25/2d/35/3d disp32  en  f3 0f 10 05 disp32 (movss)
patronen=[(b'\x48\x8d\x05',7),(b'\x48\x8d\x0d',7),(b'\x48\x8d\x15',7),(b'\x48\x8d\x1d',7),
          (b'\x48\x8b\x05',7),(b'\x48\x8b\x0d',7),(b'\x48\x8b\x15',7),(b'\x48\x8b\x1d',7),
          (b'\xf3\x0f\x10\x05',8),(b'\xf3\x0f\x11\x05',8),(b'\xf3\x0f\x10\x0d',8)]
hits={}
for pat,ln in patronen:
    i=d.find(pat)
    while i>=0:
        disp=struct.unpack_from('<i',d,i+len(pat)-4)[0]
        tgt=(i+0x400000)+ln+disp
        if tgt in doel: hits.setdefault(doel[tgt],[]).append(i+0x400000)
        i=d.find(pat,i+1)
if hits:
    for k,v in hits.items(): print("  %-14s RIP-relatief aangeraakt op: %s" % (k, ", ".join(hex(x) for x in v[:6])))
else:
    print("  GEEN enkele RIP-relatieve verwijzing naar die vijf adressen (lea/mov/movss-vormen)")
PY

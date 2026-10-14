#!/usr/bin/env python3
# build-set.py <out.so> <id> [<id> ...]  -- mash EXACTLY the given id set on RIO_SW_Count.
# Cave carved from unused RIO_SW_State (0xa540, 112 bytes). Chain: for each id: cmp edi,id; je mash.
# then jmp a5b8 (real body); mash: rdtsc; ret.  Entry a5b0 -> jmp 0xa540.
import sys, hashlib
BASE="/home/deck/.local/share/ravengun/rio/NERF/librio.so"
BASE_SHA="51ec8ad6c84d0cffc9dceeaa81874d4373f7655b2603bc8e73f023cc66b78b67"
CAVE=0xa540; BODY=0xa5b8   # real table body
out=sys.argv[1]; ids=[int(x) for x in sys.argv[2:]]
assert ids and all(0<=i<=127 for i in ids)
data=bytearray(open(BASE,"rb").read())
assert hashlib.sha256(bytes(data)).hexdigest()==BASE_SHA
assert data[0xa5b0:0xa5b8]==bytes.fromhex("8d47dc83f8277738")
chain_len=5*len(ids)            # each: cmp(3)+je(2)
jmp_pos=CAVE+chain_len
mash=jmp_pos+5
assert mash+3 <= 0xa5b0, "cave overflow"   # must stay inside RIO_SW_State body
buf=bytearray()
for i,idv in enumerate(ids):
    je_pos=CAVE+i*5+3
    rel8=mash-(je_pos+2); assert 0<=rel8<=127
    buf += bytes([0x83,0xff,idv, 0x74,rel8])
rel32=BODY-(jmp_pos+5)
buf += bytes([0xe9])+ (rel32 & 0xffffffff).to_bytes(4,'little')
buf += bytes([0x0f,0x31,0xc3])
data[CAVE:CAVE+len(buf)]=buf
data[0xa5b0:0xa5b8]=bytes([0xe9,0x8b,0xff,0xff,0xff,0x90,0x90,0x90])
open(out,"wb").write(data)
print(f"set {ids}: wrote {out} sha={hashlib.sha256(bytes(data)).hexdigest()[:16]} cavelen={len(buf)}")

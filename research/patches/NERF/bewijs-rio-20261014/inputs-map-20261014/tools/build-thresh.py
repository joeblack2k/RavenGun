#!/usr/bin/env python3
# build-thresh.py <le|ge> <K> <out.so>
# Threshold subset-mash on RIO_SW_Count, 8-byte entry patch (no cave):
#   le K: cmp edi,K ; ja  a5b8 ; rdtsc ; ret   -> mashes ids {0..K}
#   ge K: cmp edi,K ; jb  a5b8 ; rdtsc ; ret   -> mashes ids {K..up}
# Every id NOT in the mashed set runs the untouched real body (stable -> not pressed).
import sys, hashlib
BASE="/home/deck/.local/share/ravengun/rio/NERF/librio.so"
BASE_SHA="51ec8ad6c84d0cffc9dceeaa81874d4373f7655b2603bc8e73f023cc66b78b67"
OFF=0xa5b0; ORIG=bytes.fromhex("8d47dc83f8277738")
mode=sys.argv[1]; K=int(sys.argv[2]); out=sys.argv[3]
assert 0<=K<=127 and mode in ("le","ge")
data=open(BASE,"rb").read()
assert hashlib.sha256(data).hexdigest()==BASE_SHA, "base sha mismatch"
assert data[OFF:OFF+8]==ORIG, "RIO_SW_Count not pristine"
jmp = 0x77 if mode=="le" else 0x72        # ja (le-set) / jb (ge-set)
stub=bytes([0x83,0xff,K, jmp,0x03, 0x0f,0x31, 0xc3])
buf=bytearray(data); buf[OFF:OFF+8]=stub
open(out,"wb").write(buf)
print(f"{mode}{K}: wrote {out} sha={hashlib.sha256(buf).hexdigest()[:16]} stub={stub.hex()}")

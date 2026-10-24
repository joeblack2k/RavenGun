#!/usr/bin/env python3
# build-pair.py <A> <B> <out.so>  -- mash EXACTLY ids {A,B} on RIO_SW_Count; every other id runs the real body.
# Entry a5b0 -> jmp into a cave carved from the unused RIO_SW_State body at 0xa540 (0 calls measured, ni2).
# cave@0xa540:  cmp edi,A; je mash; cmp edi,B; je mash; jmp a5b8(real body); mash: rdtsc; ret
import sys, hashlib
BASE="/home/deck/.local/share/ravengun/rio/NERF/librio.so"
BASE_SHA="51ec8ad6c84d0cffc9dceeaa81874d4373f7655b2603bc8e73f023cc66b78b67"
A=int(sys.argv[1]); B=int(sys.argv[2]); out=sys.argv[3]
data=bytearray(open(BASE,"rb").read())
assert hashlib.sha256(bytes(data)).hexdigest()==BASE_SHA, "base sha mismatch"
assert data[0xa5b0:0xa5b8]==bytes.fromhex("8d47dc83f8277738"), "RIO_SW_Count not pristine"
# cave at 0xa540 (18 bytes): offsets computed in comments above
cave = bytes([0x83,0xff,A, 0x74,0x0a, 0x83,0xff,B, 0x74,0x05,
              0xe9,0x69,0x00,0x00,0x00, 0x0f,0x31,0xc3])
data[0xa540:0xa540+len(cave)] = cave
# entry patch at 0xa5b0: jmp 0xa540 (rel32 = 0xa540-(0xa5b0+5) = -0x75) + 3 nops; a5b8 untouched
data[0xa5b0:0xa5b8] = bytes([0xe9,0x8b,0xff,0xff,0xff, 0x90,0x90,0x90])
open(out,"wb").write(data)
print(f"pair {{{A},{B}}}: wrote {out} sha={hashlib.sha256(bytes(data)).hexdigest()[:16]}")

#!/usr/bin/env python3
# build-sel.py <target_id> <out.so>
# Base = the connected-patched NERF librio.so (RIO_Connected already -> 'xor eax,eax;ret' at 0xa300;
# RIO_SW_Count pristine at 0xa5b0). This adds an 8-byte SELECTIVE stub at RIO_SW_Count (0xa5b0):
#   83 ff NN   cmp edi, target_id
#   75 03      jne +3  -> a5b8 (the real table-index body, valid for the game's ids 0..18)
#   0f 31      rdtsc    (target id only: ever-changing count -> that one switch reads 'mashed')
#   c3         ret
# Every OTHER id falls through to the untouched real body (stable count -> not pressed) = built-in control.
import sys, hashlib, shutil
BASE = "/home/deck/.local/share/ravengun/rio/NERF/librio.so"
BASE_SHA = "51ec8ad6c84d0cffc9dceeaa81874d4373f7655b2603bc8e73f023cc66b78b67"
OFF = 0xa5b0                      # file offset == vaddr in this .so's .text
ORIG = bytes.fromhex("8d47dc83f8277738")  # the pristine first 8 bytes of RIO_SW_Count
tid = int(sys.argv[1]); out = sys.argv[2]
assert 0 <= tid <= 127, "target id must fit cmp imm8"
data = open(BASE,"rb").read()
got = hashlib.sha256(data).hexdigest()
if got != BASE_SHA:
    sys.exit(f"REFUSE: base sha {got[:16]} != expected {BASE_SHA[:16]} (not the connected base)")
if data[OFF:OFF+8] != ORIG:
    sys.exit(f"REFUSE: bytes at 0x{OFF:x} are {data[OFF:OFF+8].hex()}, not the pristine RIO_SW_Count")
stub = bytes([0x83,0xff,tid, 0x75,0x03, 0x0f,0x31, 0xc3])
buf = bytearray(data); buf[OFF:OFF+8] = stub
open(out,"wb").write(buf)
print(f"id={tid}  wrote {out}  sha={hashlib.sha256(buf).hexdigest()[:16]}  stub@0x{OFF:x}={stub.hex()}")

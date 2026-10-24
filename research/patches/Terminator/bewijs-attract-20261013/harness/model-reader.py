#!/usr/bin/env python3
"""Model of the game's receive path as read from the disassembly (81dacb3 framing, 81d6405 decode,
'S' handler 81d9026 with table 08721e60, Inp poll 8063630).  Self-test aid only: it shares my reading
of the code, so it checks the tool against that reading, not against the game."""
import os, sys, time
TAB = [(2,4,1),(2,0,2),(3,4,3),(3,0,4),(4,4,5),(4,0,6),(5,4,7),(5,0,8),(6,4,0xc),(6,0,0xd),(7,4,0xb),(7,0,0xa),(8,0,9),(8,4,0x11)]
EV = {1:("g1a",0x16),2:("g2a",0x18),3:("g1b",0x12),4:("g2b",0x2a),5:("START1",0x26),6:("START2",0x2e),7:("COIN1",0x30),8:("COIN2",0x32),9:("BILL",0x3a),0xa:("SERVICE",0x3c),0xb:("TEST",0x34),0xc:("VOLUP",0x36),0xd:("VOLDOWN",0x38)}
fd = os.open(sys.argv[1], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
end = time.time() + float(sys.argv[2])
inmsg = False; cur = b""; synced = False; cnt = [0]*0x14; off = [0]*0x14; seen = [0]*0x14
while time.time() < end:
    try: data = os.read(fd, 255)
    except BlockingIOError: time.sleep(0.016); continue
    for c in data:
        if c == 0x0a: inmsg = True; cur = b""; continue
        if not inmsg: continue
        if c != 0x0d: cur += bytes([c]); continue
        inmsg = False
        if not cur or cur[0] != ord('S'): print("other msg", cur); continue
        hx = bytes(x for x in cur[1:] if chr(x) in "0123456789abcdefABCDEF")
        m = b"S" + bytes.fromhex(hx[:len(hx)//2*2].decode())
        if len(m) != 9: print("S with wrong length", len(m)); continue
        if not m[1] & 0x80: synced = False; print("unsync"); continue
        if not synced:
            synced = True
            for bi, sh, idx in TAB: off[idx] = (cnt[idx] - ((m[bi] >> sh) & 0xf)) & 0xf
            seen = cnt[:]; print("SYNC (baseline taken, no events)"); continue
        for bi, sh, idx in TAB:
            tgt = (((m[bi] >> sh) & 0xf) + off[idx]) & 0xf
            while cnt[idx] & 0xf != tgt: cnt[idx] += 1
        for idx, (name, code) in EV.items():
            while seen[idx] < cnt[idx]:
                print("InpPost(0x%02x) %s %s" % (code + (seen[idx] & 1), name, "RELEASE" if seen[idx] & 1 else "PRESS"))
                seen[idx] += 1

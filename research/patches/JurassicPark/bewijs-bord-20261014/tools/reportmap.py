#!/usr/bin/env python3
# Which bytes of the RIO input report (type 2) carry which switch id / lightgun channel, read from the game's own decoder
# parse_device_input_report (0x836d011, 1832 bytes) via dis.sh. Output: (function, id, [payload byte, nibble, ...]).
# Payload byte N = report byte 3+N (byte 0 = message type, 1..2 = sequence, 63 = checksum).
import re, subprocess, sys, os
here = os.path.dirname(os.path.abspath(__file__))
L = subprocess.run([os.path.join(here, 'dis.sh'), '0x0836d011', '1832'], capture_output=True, text=True).stdout.splitlines()
blk = []
for l in L:
    blk.append(l)
    m = re.search(r'call\s+0x\S+\s+<(process_sw|process_adc|process_cb)>', l)
    if not m: continue
    ids = [int(x.group(1), 16) for x in (re.search(r'movl\s+\$0x([0-9a-f]+),0x8\(%esp\)', y) for y in blk) if x]
    reads = []
    for y in blk:
        r = re.search(r'movz[bw]l\s+0x([0-9a-f]+)\(%eax\),%eax', y)
        if r: reads.append(int(r.group(1), 16))
        if re.search(r'movzbl\s+\(%eax\),%eax', y): reads.append(0)
        if re.search(r'shr\s+\$0x4,%al', y): reads.append('hi')
        if re.search(r'and\s+\$0xf,%eax', y): reads.append('lo')
        if re.search(r'shl\s+\$0x8,%eax', y): reads.append('<<8')
    print((m.group(1), ids[-1] if ids else None, reads))
    blk = []

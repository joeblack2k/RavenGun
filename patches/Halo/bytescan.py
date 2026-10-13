# Alignment-independent cross-check of "who calls X" and "who touches address Y": scans every byte offset of the code
# segment, so a call or reference hidden by a misaligned linear disassembly is still found. Calibrated on known answers.
import sys, struct
d = open(sys.argv[1], 'rb').read()
CODE_END = 0x11bc000 - 0x400000
code = d[:CODE_END]
def va(o): return o + 0x400000

def callers(target):
    hits = []
    for op in (0xE8, 0xE9):
        i = code.find(bytes([op]))
        while i != -1 and i + 5 <= CODE_END:
            rel = struct.unpack_from('<i', code, i + 1)[0]
            if va(i) + 5 + rel == target: hits.append((va(i), 'call' if op == 0xE8 else 'jmp'))
            i = code.find(bytes([op]), i + 1)
    return sorted(hits)

def refs(target):
    """RIP-relative disp32 that resolves to target (for 0, 1, 2 or 4 immediate bytes after it), and absolute 4-byte uses."""
    rip = []
    mv = memoryview(code)
    for j in range(0, CODE_END - 8):
        disp = int.from_bytes(mv[j:j + 4], 'little', signed=True)
        base = va(j) + 4 + disp
        if base == target or base + 1 == target or base + 2 == target or base + 4 == target:
            rip.append(va(j))
    ab = []
    p = struct.pack('<I', target); i = code.find(p)
    while i != -1: ab.append(va(i)); i = code.find(p, i + 1)
    return rip, ab

if sys.argv[2] == 'callers':
    for t in sys.argv[3:]:
        t = int(t, 16); h = callers(t)
        print("%#x: %d  %s" % (t, len(h), " ".join("%x%s" % (a, '' if k == 'call' else '(jmp)') for a, k in h[:60])))
else:
    for t in sys.argv[3:]:
        t = int(t, 16); rip, ab = refs(t)
        print("%#x: rip-relative at %s | absolute at %s" % (t, " ".join("%x" % a for a in rip), " ".join("%x" % a for a in ab)))

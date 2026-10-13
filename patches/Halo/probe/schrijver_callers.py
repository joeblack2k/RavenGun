#!/usr/bin/env python3
# WIE ROEPT DE SCHRIJVER AAN? Zoek het functiebegin rond 0xdd590b en daarna de callers (e8 rel32).
import struct, subprocess

BIN = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
d = open(BIN, "rb").read()
DELTA = 0x400000
CODE_LO, CODE_HI = 0x400000, 0x1462000
SITE = 0xdd590b          # de schrijfplek movl $0x0,(%rax,%rbp,4)

def dis(va, voor, na):
    r = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                        "--adjust-vma=0x%x" % DELTA,
                        "--start-address=%d" % (va - voor), "--stop-address=%d" % (va + na), BIN],
                       capture_output=True, text=True)
    return [l.strip() for l in r.stdout.splitlines() if l.strip() and ":" in l and l.strip()[0].isalnum()]

def va2off(va):
    return va - DELTA

print("############ 1. HET FUNCTIEBEGIN ROND DE SCHRIJFPLek 0x%x" % SITE)
# zoek achteruit naar de dichtstbijzijnde 'ret' (c3) gevolgd door opvulling of een prologue
kand = []
off = va2off(SITE)
for back in range(1, 0x600):
    if d[off - back] == 0xC3:
        start = SITE - back + 1
        # sla opvulling over (nop/cc/int3) tot de eerste echte instructie
        while d[va2off(start)] in (0x90, 0xCC, 0x66, 0x0F, 0x1F, 0x00):
            start += 1
            if start > SITE:
                break
        kand.append(start)
        if len(kand) >= 4:
            break
print("  kandidaten voor het begin: %s" % ", ".join("0x%x" % k for k in kand))
START = kand[-1] if kand else SITE - 0x100
print("  ik neem het verst teruggevondene: 0x%x" % START)
print("  --- de eerste instructies van die functie:")
for l in dis(START, 0, 64)[:14]:
    print("      " + l)
print("  --- en de schrijfplek zelf, met omgeving:")
for l in dis(SITE, 32, 32):
    print("      " + l + ("   <<<" if ("0x%x" % SITE) in l else ""))

print()
print("############ 2. WIE ROEPT DIE FUNCTIE AAN? (call e8 rel32 -> 0x%x)" % START)
callers = []
for i in range(0, CODE_HI - CODE_LO - 5):
    if d[i] == 0xE8:
        rel = struct.unpack_from("<i", d, i + 1)[0]
        va = i + DELTA
        if va + 5 + rel == START:
            callers.append(va)
print("  aantal callers: %d" % len(callers))
for va in callers[:6]:
    print("    call vanuit 0x%x" % va)

print()
print("############ 3. DE EERSTE TWEE CALLERS, INSTRUCTIE VOOR INSTRUCTIE")
for va in callers[:2]:
    print("  === rond 0x%x:" % va)
    for l in dis(va, 72, 56)[:26]:
        print("      " + l)
    print("  --- de vertakkingen in dat venster:")
    for l in dis(va, 120, 60):
        t = l.lower()
        if any(k in t for k in ("test", "cmp", "jne", "je ", "jz", "jnz", "jg", "jl", "call")):
            print("      " + l)

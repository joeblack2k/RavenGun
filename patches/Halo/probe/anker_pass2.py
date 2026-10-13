#!/usr/bin/env python3
# HERSTELDE SCANNER. De fout: ik las de disp32 op `i+len(pat)-4` terwijl len(pat)=3 -> `i-1`.
# Correct: de instructielengte is 7 (lea/mov r64,[rip+disp32]) en de disp staat op i+3.
# Gevolg: ALLE eerdere RIP-relatieve scans waren ongeldig (de absolute-literaal-scan was wel goed).
import struct, subprocess

BIN = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Halo Fireteam/g7/halo/game"
d = open(BIN, "rb").read()
DELTA = 0x400000
CODE_LO, CODE_HI = 0x400000, 0x1462000

def dis(va, voor, na, n=34):
    r = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386:x86-64",
                        "--adjust-vma=0x%x" % DELTA,
                        "--start-address=%d" % (va - voor), "--stop-address=%d" % (va + na), BIN],
                       capture_output=True, text=True)
    return [l.strip() for l in r.stdout.splitlines() if l.strip() and ":" in l and l.strip()[0].isalnum()][:n]

def scan(doel):
    """Alle RIP-relatieve verwijzingen naar `doel` in het codevenster."""
    uit = []
    # 7-byte vormen: 48/4c 8d/8b <op> disp32
    for pre1 in (0x48, 0x4C):
        for pre2 in (0x8D, 0x8B):
            for op in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D):
                pat = bytes([pre1, pre2, op])
                i = d.find(pat)
                while i >= 0:
                    va = i + DELTA
                    if CODE_LO <= va < CODE_HI:
                        disp = struct.unpack_from("<i", d, i + 3)[0]
                        if va + 7 + disp == doel:
                            uit.append((va, "len7"))
                    i = d.find(pat, i + 1)
    # 8-byte vorm: f3 0f 10/11 <op> disp32  (movss)
    for pre in (b"\xf3\x0f\x10", b"\xf3\x0f\x11"):
        for op in (0x05, 0x0D, 0x15, 0x1D):
            pat = pre + bytes([op])
            i = d.find(pat)
            while i >= 0:
                va = i + DELTA
                if CODE_LO <= va < CODE_HI:
                    disp = struct.unpack_from("<i", d, i + 4)[0]
                    if va + 8 + disp == doel:
                        uit.append((va, "movss"))
                i = d.find(pat, i + 1)
    return sorted(set(uit))

doelen = {
    "ANKER 0x180ae80 (schakelaartafel)": 0x180ae80,
    "0x1f392a0 (mijn aim x)": 0x1f392a0,
    "0x1f392a4 (mijn aim y)": 0x1f392a4,
    "0x1f392d0 (mijn spelersaantal)": 0x1f392d0,
    "0x1a7cb20 (mijn blok 0)": 0x1a7cb20,
}
for naam, adr in doelen.items():
    hits = scan(adr)
    print("############ %s" % naam)
    print("  RIP-relatieve verwijzingen (HERSTELDE scanner): %d" % len(hits))
    for va, soort in hits[:10]:
        print("    0x%08x  (%s)" % (va, soort))
    if not hits:
        print("    (geen: de code raakt dit adres niet RIP-relatief aan)")

print()
print("############ DE CODE ROND DE EERSTE VERWIJZINGEN NAAR HET ANKER")
hits = scan(0x180ae80)
for va, _s in hits[:3]:
    print("  === rond 0x%x:" % va)
    for l in dis(va, 80, 96, 26):
        print("      " + l)

print()
print("############ EN WAT VOOR DATA-ADRESSEN NOEMT DIE CODE NOG MEER? (de buren)")
DATA_LO = 0x1661000
buren = {}
for va, _s in hits[:8]:
    for l in dis(va, 200, 120, 80):
        if "#" in l:
            try:
                tgt = int(l.split("#")[-1].strip().split()[0], 16)
            except Exception:
                continue
            if DATA_LO <= tgt < 0x2400000 and tgt != 0x180ae80:
                buren.setdefault(tgt, set()).add(va)
for tgt in sorted(buren, key=lambda t: -len(buren[t]))[:16]:
    print("    0x%08x  vanuit %d plek(ken)" % (tgt, len(buren[tgt])))

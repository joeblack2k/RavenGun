"""xref.py <pe> <pub> <rva hex>...   toon de plaatsen in .text met een rel32-verwijzing (call/jmp/rip-relatief) naar rva."""
import sys
import numpy as np, pefile
from ds import load_pub, sym

exe, pubf = sys.argv[1], sys.argv[2]
pe = pefile.PE(exe, fast_load=True)
rvas, names = load_pub(pubf)
for s in pe.sections:
    if s.Name.startswith(b'.text'):
        t = s
base = t.VirtualAddress
raw = pe.get_data(base, t.Misc_VirtualSize)
arr = np.frombuffer(raw, dtype=np.uint8)
n = len(arr) - 8
for targ in sys.argv[3:]:
    T = int(targ, 16)
    hits = []
    for k in (0, 1, 2, 4):
        # int32 aan elke offset p: bouw uit 4 bytes
        v = (arr[0:n].astype(np.int64) | (arr[1:n + 1].astype(np.int64) << 8) | (arr[2:n + 2].astype(np.int64) << 16)
             | (arr[3:n + 3].astype(np.int64) << 24))
        v = np.where(v >= 2 ** 31, v - 2 ** 32, v)
        pos = np.arange(n, dtype=np.int64)
        target = base + pos + 4 + k + v
        idx = np.nonzero(target == T)[0]
        for p in idx:
            p = int(p)
            op = arr[p - 1]
            # filter: call E8 / jmp E9 (k=0) of rip-relatief (modrm mod=00 rm=101 vlak voor disp)
            modrm = arr[p - 1]
            if k == 0 and op in (0xE8, 0xE9):
                hits.append((base + p - 1, 'call' if op == 0xE8 else 'jmp'))
            elif (modrm & 0xC7) == 0x05:
                hits.append((base + p - 1, 'rip k%d' % k))
    print('== %s %s' % (targ, sym(rvas, names, T)))
    for a, kind in sorted(set(hits)):
        print('  %x %s  in %s' % (a, kind, sym(rvas, names, a)))

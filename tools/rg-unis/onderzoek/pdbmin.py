"""Minimale PDB-lezer (MSF 7): streams, DBI, publieke symbolen (S_PUB32) -> RVA. Alleen lezen, mmap."""
import mmap, struct, sys, math, pickle, os

class MSF:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.m = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        m = self.m
        assert m[:32].startswith(b'Microsoft C/C++ MSF 7.00')
        self.bs, _, self.nblocks, dirsize, _, mapaddr = struct.unpack_from('<6I', m, 32)
        nb = math.ceil(dirsize / self.bs)
        dirblocks = struct.unpack_from('<%dI' % nb, m, mapaddr * self.bs)
        d = b''.join(m[b * self.bs:(b + 1) * self.bs] for b in dirblocks)[:dirsize]
        ns = struct.unpack_from('<I', d, 0)[0]
        sizes = struct.unpack_from('<%dI' % ns, d, 4)
        off = 4 + 4 * ns
        self.streams = []
        for s in sizes:
            if s == 0xFFFFFFFF:
                s = 0
            n = math.ceil(s / self.bs)
            blocks = struct.unpack_from('<%dI' % n, d, off)
            off += 4 * n
            self.streams.append((s, blocks))

    def read(self, i, start=0, length=None):
        s, blocks = self.streams[i]
        if length is None:
            length = s - start
        out = bytearray()
        bs = self.bs
        b0 = start // bs
        b1 = (start + length - 1) // bs
        for bi in range(b0, b1 + 1):
            out += self.m[blocks[bi] * bs:(blocks[bi] + 1) * bs]
        o = start - b0 * bs
        return bytes(out[o:o + length])

def publics(path, sections):
    """sections = lijst (VirtualAddress) per sectie, 1-gebaseerd -> RVA."""
    p = MSF(path)
    dbi = p.read(3, 0, 64)
    symrec = struct.unpack_from('<H', dbi, 20)[0]
    data = p.read(symrec)
    res = []
    o = 0
    n = len(data)
    while o + 4 <= n:
        ln, ty = struct.unpack_from('<HH', data, o)
        if ty == 0x110e:  # S_PUB32
            flags, off, seg = struct.unpack_from('<IIH', data, o + 4)
            name = data[o + 14:o + 2 + ln].split(b'\0')[0].decode('latin1')
            if 1 <= seg <= len(sections):
                res.append((sections[seg - 1] + off, name))
        o += 2 + ln
    return res

if __name__ == '__main__':
    import pefile
    exe, pdb, out = sys.argv[1:4]
    pe = pefile.PE(exe, fast_load=True)
    secs = [s.VirtualAddress for s in pe.sections]
    r = publics(pdb, secs)
    r.sort()
    with open(out, 'w') as f:
        for rva, name in r:
            f.write('%x %s\n' % (rva, name))
    print(len(r))

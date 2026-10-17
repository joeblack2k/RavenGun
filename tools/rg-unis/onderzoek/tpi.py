"""Minimale TPI-lezer: struct/class-indeling dumpen op naam. Gebruik: tpi.py <pdb> <naam>..."""
import struct, sys
from pdbmin import MSF

PRIM = {0x03: 'void', 0x10: 'char', 0x20: 'uchar', 0x11: 'short', 0x21: 'ushort', 0x12: 'int16?', 0x74: 'int', 0x75: 'uint', 0x70: 'char', 0x71: 'wchar',
        0x40: 'float', 0x41: 'double', 0x30: 'bool', 0x31: 'bool16', 0x32: 'bool32', 0x68: 'int8', 0x69: 'uint8', 0x72: 'int16', 0x73: 'uint16',
        0x76: 'int64', 0x77: 'uint64', 0x13: 'int64', 0x23: 'uint64', 0x22: 'ulong', 0x74: 'int32', 0x75: 'uint32', 0x7a: 'char16', 0x7b: 'char32'}

class TPI:
    def __init__(self, path):
        self.p = MSF(path)
        hdr = self.p.read(2, 0, 56)
        ver, hsz, self.tbeg, self.tend, recbytes = struct.unpack_from('<5I', hdr, 0)
        self.data = self.p.read(2, hsz, recbytes)
        self.off = []
        o = 0
        d = self.data
        n = len(d)
        while o < n:
            ln = struct.unpack_from('<H', d, o)[0]
            self.off.append(o)
            o += 2 + ln
        print('typen', len(self.off), file=sys.stderr)

    def rec(self, ti):
        o = self.off[ti - self.tbeg]
        ln, kind = struct.unpack_from('<HH', self.data, o)
        return kind, self.data[o + 4:o + 2 + ln]

    def num(self, b, o):
        v = struct.unpack_from('<H', b, o)[0]
        if v < 0x8000:
            return v, o + 2
        t = v
        if t == 0x8000: return struct.unpack_from('<b', b, o + 2)[0], o + 3
        if t == 0x8001: return struct.unpack_from('<h', b, o + 2)[0], o + 4
        if t == 0x8002: return struct.unpack_from('<H', b, o + 2)[0], o + 4
        if t == 0x8003: return struct.unpack_from('<i', b, o + 2)[0], o + 6
        if t == 0x8004: return struct.unpack_from('<I', b, o + 2)[0], o + 6
        if t == 0x800a: return struct.unpack_from('<Q', b, o + 2)[0], o + 10
        return 0, o + 2

    def name(self, b, o):
        e = b.index(b'\0', o)
        return b[o:e].decode('latin1'), e + 1

    def tname(self, ti, depth=0):
        if ti < self.tbeg:
            base = ti & 0xFF
            mode = ti & 0xF00
            s = PRIM.get(base, 'prim%x' % base)
            return s + ('*' if mode else '')
        kind, b = self.rec(ti)
        if kind in (0x1504, 0x1505, 0x1506, 0x1507):
            if kind == 0x1507:
                n, o = self.name(b, 14) if False else (self._ename(b), 0)
                return 'enum ' + n
            return self._sname(b)
        if kind == 0x1002:  # pointer
            return self.tname(struct.unpack_from('<I', b, 0)[0]) + '*'
        if kind == 0x1001:
            return 'const ' + self.tname(struct.unpack_from('<I', b, 0)[0])
        if kind == 0x1503:  # array
            et, it = struct.unpack_from('<II', b, 0)
            sz, o = self.num(b, 8)
            return '%s[%d bytes]' % (self.tname(et), sz)
        if kind == 0x1205:
            bt, ln, pos = struct.unpack_from('<IBB', b, 0)
            return 'bits<%s:%d@%d>' % (self.tname(bt), ln, pos)
        return 'kind%x' % kind

    def _ename(self, b):
        cnt, prop, ut, fl = struct.unpack_from('<HHII', b, 0)
        return self.name(b, 12)[0]

    def _sname(self, b):
        o = 16
        sz, o = self.num(b, o)
        return self.name(b, o)[0]

    def find(self, nm):
        d = self.data
        res = []
        for i, o in enumerate(self.off):
            kind = struct.unpack_from('<H', d, o + 2)[0]
            if kind in (0x1504, 0x1505):
                b = d[o + 4:o + 2 + struct.unpack_from('<H', d, o)[0]]
                cnt, prop, fl, der, vs = struct.unpack_from('<HHIII', b, 0)
                sz, o2 = self.num(b, 16)
                n = self.name(b, o2)[0]
                if n == nm and not (prop & 0x80):
                    res.append((i + self.tbeg, sz, fl))
        return res

    def members(self, fl):
        out = []
        while fl:
            kind, b = self.rec(fl)
            o = 0
            nxt = 0
            while o < len(b):
                k = struct.unpack_from('<H', b, o)[0] if o + 2 <= len(b) else 0
                if k == 0x150d:
                    attr, ty = struct.unpack_from('<HI', b, o + 2)
                    off, o2 = self.num(b, o + 8)
                    nm, o3 = self.name(b, o2)
                    out.append((off, nm, self.tname(ty)))
                    o = o3
                elif k == 0x1404:
                    nxt = struct.unpack_from('<I', b, o + 4)[0]
                    o += 8
                elif k in (0x150e, 0x1510, 0x1409):
                    o = self.name(b, o + 8)[1] if k != 0x1409 else o + 8
                elif k == 0x150f:
                    o = self.name(b, o + 8)[1]
                elif k == 0x1511:
                    attr = struct.unpack_from('<H', b, o + 2)[0]
                    extra = 4 if ((attr >> 2) & 7) in (4, 6) else 0
                    o = self.name(b, o + 8 + extra)[1]
                elif k == 0x1400:
                    attr, ti = struct.unpack_from('<HI', b, o + 2)
                    off, o2 = self.num(b, o + 8)
                    out.append((off, '<base>', self.tname(ti)))
                    o = o2
                else:
                    break
                # padding
                while o < len(b) and b[o] >= 0xf0:
                    o += b[o] & 0x0f
            fl = nxt
        return out

if __name__ == '__main__':
    t = TPI(sys.argv[1])
    for nm in sys.argv[2:]:
        r = t.find(nm)
        print('==', nm, r[:3])
        for ti, sz, fl in r[:1]:
            print('size', sz)
            for off, n, ty in sorted(t.members(fl)):
                print('  +%-4d %-40s %s' % (off, n, ty))

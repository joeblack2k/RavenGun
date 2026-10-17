#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Vergelijkt de exporttabel van een gebouwde rg-konami-DLL met het origineel (namen en ordinalen) en controleert of hij alleen
systeem-DLL's importeert. Leest het origineel alleen (de NAS wordt nooit beschreven).

    vergelijk_exports.py EIGEN.dll ORIGINEEL.dll
    vergelijk_exports.py --def BESTAND.def EIGEN.dll     (zonder origineel: tegen de .def)

Ook: welke namen de game uit deze DLL importeert, met --importeurs MAP: elke .exe/.dll in MAP die deze DLL importeert moet al zijn
namen in onze exporttabel vinden (anders laadt Wine de game niet)."""
import argparse
import os
import struct
import sys

TOEGESTAAN = {'kernel32.dll', 'msvcrt.dll', 'user32.dll'}


class PE:
    def __init__(self, pad):
        self.d = open(pad, 'rb').read()
        e = struct.unpack_from('<I', self.d, 0x3c)[0]
        nsec = struct.unpack_from('<H', self.d, e + 6)[0]
        optsz = struct.unpack_from('<H', self.d, e + 20)[0]
        opt = e + 24
        self.magic = struct.unpack_from('<H', self.d, opt)[0]
        dd = opt + (96 if self.magic == 0x10b else 112)
        self.dirs = [struct.unpack_from('<II', self.d, dd + 8 * i) for i in range(16)]
        self.secs = []
        so = opt + optsz
        for i in range(nsec):
            _, vs, va, rs, ro = struct.unpack_from('<8sIIII', self.d, so + 40 * i)
            self.secs.append((va, max(vs, rs), ro))

    def off(self, rva):
        for va, sz, ro in self.secs:
            if va <= rva < va + sz:
                return rva - va + ro
        raise ValueError(hex(rva))

    def cstr(self, rva):
        o = self.off(rva)
        return self.d[o:self.d.index(b'\0', o)].decode('latin1')

    def exports(self):
        rva, _ = self.dirs[0]
        if not rva:
            return {}
        o = self.off(rva)
        base, nf, nn, af, an, ao = struct.unpack_from('<IIIIII', self.d, o + 16)
        uit = {}
        for i in range(nn):
            nr = struct.unpack_from('<I', self.d, self.off(an) + 4 * i)[0]
            oi = struct.unpack_from('<H', self.d, self.off(ao) + 2 * i)[0]
            uit[self.cstr(nr)] = base + oi
        return uit

    def imports(self):
        rva, _ = self.dirs[1]
        uit = {}
        if not rva:
            return uit
        o = self.off(rva)
        while True:
            ilt, _, _, nm, iat = struct.unpack_from('<IIIII', self.d, o)
            if not (ilt or nm or iat):
                break
            dll = self.cstr(nm).lower()
            t = ilt or iat
            i = 0
            while True:
                v = struct.unpack_from('<I', self.d, self.off(t) + 4 * i)[0]
                if not v:
                    break
                uit.setdefault(dll, []).append('#%d' % (v & 0xffff) if v & 0x80000000 else self.cstr(v + 2))
                i += 1
            o += 20
        return uit


def lees_def(pad):
    uit = {}
    for regel in open(pad):
        regel = regel.split(';')[0].strip()
        if not regel or regel.startswith(('LIBRARY', 'EXPORTS')):
            continue
        # "?naam@@...@Z"=c_naam @ordinaal  of  Naam=c_naam @ordinaal; het ordinaal staat na de laatste @.
        naam = regel[1:regel.index('"', 1)] if regel.startswith('"') else regel.split('=')[0].strip()
        uit[naam] = int(regel.rsplit('@', 1)[1])
    return uit


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('eigen')
    ap.add_argument('origineel', nargs='?')
    ap.add_argument('--def', dest='deffile')
    ap.add_argument('--importeurs')
    a = ap.parse_args()
    fout = 0
    eigen = PE(a.eigen)
    if eigen.magic != 0x10b:
        print('FOUT %s: geen 32-bits PE' % a.eigen)
        fout += 1
    ex = eigen.exports()
    if a.origineel:
        want = PE(a.origineel).exports()
        bron = a.origineel
    else:
        want = lees_def(a.deffile)
        bron = a.deffile
    for n, o in sorted(want.items(), key=lambda kv: kv[1]):
        if n not in ex:
            print('FOUT %s: export %s ontbreekt' % (a.eigen, n))
            fout += 1
        elif ex[n] != o:
            print('FOUT %s: %s heeft ordinaal %d, origineel %d' % (a.eigen, n, ex[n], o))
            fout += 1
    for n in ex:
        if n not in want:
            print('FOUT %s: extra export %s' % (a.eigen, n))
            fout += 1
    for dll, namen in eigen.imports().items():
        if dll not in TOEGESTAAN:
            print('FOUT %s: importeert %s (%s)' % (a.eigen, dll, ', '.join(namen[:5])))
            fout += 1
    if a.importeurs:
        mijn = os.path.basename(a.eigen).lower()
        for f in sorted(os.listdir(a.importeurs)):
            if not f.lower().endswith(('.exe', '.dll')):
                continue
            try:
                imp = PE(os.path.join(a.importeurs, f)).imports()
            except Exception:
                continue
            for n in imp.get(mijn, []):
                if n not in ex:
                    print('FOUT %s importeert %s uit %s, die hebben wij niet' % (f, n, mijn))
                    fout += 1
    print('%s: %d exports, %s tegen %s' % (a.eigen, len(ex), 'in orde' if not fout else '%d fout(en)' % fout, bron))
    return 1 if fout else 0


if __name__ == '__main__':
    sys.exit(main())

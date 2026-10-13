#!/usr/bin/env python3
# rk.py -- lees/schrijf instrument op het gastgeheugen van de lopende Halo, van BUITEN het proces.
# Bewijst eerst dat het LEZEN klopt (ELF-kop) en dat het SCHRIJVEN landt (schrijf en lees direct terug).
#
# gebruik:
#   rk.py <pid|auto> rapport
#   rk.py <pid|auto> lees 0xADRES [aantal]
#   rk.py <pid|auto> schrijf 0xADRES u32|f32 0xWAARDE|getal
#   rk.py <pid|auto> tafel            (de 32 vakken van bord 0 en de 13 k van de hulp)
#   rk.py <pid|auto> lijst 0x1771850  (de runtime-lijst van de 0x4axxxx-subsysteem: aantal + 8-byte paren)
import os, sys, struct

def pid_auto():
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try: exe = os.readlink('/proc/%s/exe' % p)
        except OSError: continue
        if exe.endswith('halo/game'): return int(p)
    return None

def lees(fd, adres, n):
    try: d = os.pread(fd, n, adres)
    except OSError as e: return None, 'LEESFOUT: %s' % e
    if len(d) != n: return None, 'kort gelezen'
    return d, None

def schrijf(fd, adres, n, data):
    try: k = os.pwrite(fd, data, adres)
    except OSError as e: return 'SCHRIJFFOUT: %s' % e
    return None if k == n else 'kort geschreven (%d van %d)' % (k, n)

def controleer_lezen(fd):
    d, e = lees(fd, 0x400000, 16)
    if d is None or d[:4] != b'\x7fELF':
        print('  KALIBRATIE LEZEN GEZAKT (%s / %s)' % (e, d.hex() if d else '-')); return False
    d2, e2 = lees(fd, 0x1000, 4)
    if d2 is not None:
        print('  KALIBRATIE LEZEN GEZAKT: ongemapt 0x1000 gaf toch %s' % d2.hex()); return False
    print('  KALIBRATIE LEZEN GOED (ELF-kop op 0x400000, ongemapt adres faalt)')
    return True

def lees_en_schrijf(fd, adres, n, data):
    """schrijf en lees DIRECT terug: bewijst dat de SCHRIJFweg naar precies dit adres landt."""
    oud, _ = lees(fd, adres, n)
    fout = schrijf(fd, adres, n, data)
    if fout:
        print('    %#x: %s' % (adres, fout)); return False
    nieuw, _ = lees(fd, adres, n)
    goed = (nieuw == data)
    print('    %#x: oud=%s nieuw=%s  -> %s' % (adres, oud.hex() if oud else '?',
          nieuw.hex() if nieuw else '?', 'SCHRIJF LANDT' if goed else 'SCHRIJF LANDT NIET'))
    return goed

VASTE = [
    ('spelersaantal 0x1f392d0', 0x1f392d0, 4),
    ('aim-paar x 0x1f392a0', 0x1f392a0, 4),
    ('aim-paar y 0x1f392a4', 0x1f392a4, 4),
    ('poortbyte 0x1aa37c0', 0x1aa37c0, 4),
    ('dword-array 0x1aa37a0', 0x1aa37a0, 8),
    ('apparaatblok0 0x1a7cb20', 0x1a7cb20, 8),
    ('blok0 spelernr 0x1a7cb5c', 0x1a7cb5c, 4),
    ('aanwijzer 0x181f7e0', 0x181f7e0, 8),
    ('vlaggenmasker A 0x1766f40', 0x1766f40, 4),
    ('vlaggenmasker B 0x181d8a0', 0x181d8a0, 4),
    ('analoog A (793) 0x181d418', 0x181d418, 0x30),
    ('analoog B (794) 0x181d538', 0x181d538, 0x30),
    ('lijstteller 0x1771850', 0x1771850, 8),
]

def rapport(fd):
    print('  --- de vaste adressen')
    for naam, adres, n in VASTE:
        d, e = lees(fd, adres, n)
        if d is None: print('    %-26s %s' % (naam, e)); continue
        if n == 4:
            u = struct.unpack('<I', d)[0]; f = struct.unpack('<f', d)[0]
            print('    %-26s u32=%-12d 0x%-10x f32=%g' % (naam, u, u, f))
        elif n == 8:
            u = struct.unpack('<2I', d)
            print('    %-26s u32=%s  u64=%d' % (naam, u, struct.unpack('<Q', d)[0]))
        else:
            print('    %-26s %s' % (naam, d.hex()))
    # de per-speler structuur waar de aim-logica zijn vlag in zet
    d, e = lees(fd, 0x181f7e0, 8)
    if d:
        p = struct.unpack('<Q', d)[0]
        print('  --- [0x181f7e0] = %#x  (de per-speler structuur; stap 0x48 per speler)' % p)
        if 0x400000 < p < 0x40000000:
            for sp in range(4):
                for off in (0, 0x120):
                    dd, ee = lees(fd, p + sp*0x48 + off, 4)
                    if dd is None: continue
                    u = struct.unpack('<I', dd)[0]
                    print('      speler %d +%#5x : u32=0x%08x   bit 0x10000=%s'
                          % (sp, off, u, 'GEZET' if u & 0x10000 else 'niet gezet'))
        else:
            print('      (geen zinnige aanwijzer; de aim-logica zou hier niet schrijven)')

def tafel(fd):
    print('  --- bord 0, 32 vakken (0x1815d20 + vak*0x50); soort 0=analoog, 1/2=schakelaar')
    for s in range(32):
        d, e = lees(fd, 0x1815d20 + s*0x50, 0x50)
        if d is None: print('    vak %2d %s' % (s, e)); continue
        w = struct.unpack_from('<16I', d, 0)
        print('    vak %2d %#x  [0]=%d [1]=%d [2]=%d [3]=%d [4]=%d [5]=%d [6]=%d [7]=%d [8]=%d'
              % (s, 0x1815d20+s*0x50, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8]))

def lijst(fd, adres):
    d, e = lees(fd, adres, 8)
    if d is None: print('  %s' % e); return
    n = struct.unpack_from('<I', d, 0)[0]
    print('  teller op %#x = %d ; tabel op %#x' % (adres, n, adres+8))
    if n <= 0 or n > 4096: print('  (geen zinnige lijst)'); return
    dd, e = lees(fd, adres+8, min(n,64)*8)
    if dd is None: print('  %s' % e); return
    for i in range(min(n, 64)):
        a, b = struct.unpack_from('<2I', dd, i*8)
        extra = ''
        if 0x239 <= b <= 0x298:
            extra = ' = bord %d vak %d' % ((b-0x239)>>5, (b-0x239)&31)
        print('    [%2d] %d (0x%x) , %d (0x%x)%s' % (i, a, a, b, b, extra))

def main():
    if len(sys.argv) < 3: print(__doc__ or 'gebruik: rk.py <pid|auto> <opdracht>'); return 1
    pid = pid_auto() if sys.argv[1] == 'auto' else int(sys.argv[1])
    if pid is None: print('GEEN GAME-PROCES'); return 1
    fd = os.open('/proc/%d/mem' % pid, os.O_RDWR)
    op = sys.argv[2]
    if op == 'rapport':
        print('### rk rapport pid=%d' % pid)
        if not controleer_lezen(fd): return 2
        rapport(fd); return 0
    if op == 'tafel':
        if not controleer_lezen(fd): return 2
        tafel(fd); return 0
    if op == 'lijst':
        if not controleer_lezen(fd): return 2
        lijst(fd, int(sys.argv[3], 0)); return 0
    if op == 'lees':
        adres = int(sys.argv[3], 0); n = int(sys.argv[4], 0) if len(sys.argv) > 4 else 16
        d, e = lees(fd, adres, n)
        print('  %#x (%d bytes): %s' % (adres, n, d.hex() if d else e)); return 0
    if op == 'schrijf':
        adres = int(sys.argv[3], 0); soort = sys.argv[4]
        if soort == 'f32': data = struct.pack('<f', float(sys.argv[5]))
        elif soort == 'u32': data = struct.pack('<I', int(sys.argv[5], 0))
        elif soort == 'u8': data = struct.pack('<B', int(sys.argv[5], 0))
        else: print('soort moet u32, f32 of u8 zijn'); return 1
        print('### rk schrijf %#x %s %s' % (adres, soort, sys.argv[5]))
        if not controleer_lezen(fd): return 2
        lees_en_schrijf(fd, adres, len(data), data)
        return 0
    print('onbekende opdracht %s' % op); return 1

if __name__ == '__main__':
    sys.exit(main())

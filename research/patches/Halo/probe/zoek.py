#!/usr/bin/env python3
# zoek.py -- zoekt een 4-byte patroon in het HELE beeld van het spelproces (via /proc/pid/maps).
#
# Instrumentregel: dit script bewijst zichzelf eerst.
#   POSITIEVE CONTROLE: een patroon dat we ZELF net in de tafel hebben geschreven MOET gevonden worden
#                       op de plek waar we het schreven. Vindt hij dat niet, dan is de zoeker stuk en
#                       betekent "niets gevonden" niets.
#   NEGATIEVE CONTROLE: een patroon dat NERGENS staat mag niet gevonden worden.
#
# gebruik: zoek.py <pid|auto> 0xPATROON [0xPATROON ...]
import os, sys

def pid_auto():
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try: exe = os.readlink('/proc/%s/exe' % p)
        except OSError: continue
        if exe.endswith('halo/game'): return int(p)
    return None

def kaarten(pid):
    """alle leesbare gebieden uit /proc/pid/maps, zonder bestandsafbeeldingen die we toch kennen"""
    uit = []
    for regel in open('/proc/%d/maps' % pid):
        d = regel.split()
        if len(d) < 2 or 'r' not in d[1][:1]: continue
        begin, eind = d[0].split('-')
        begin = int(begin, 16); eind = int(eind, 16)
        naam = d[5] if len(d) > 5 else ''
        # /dev/ en [vvar] e.d. overslaan (lezen kan EIO geven)
        if naam.startswith('/dev/') or naam.startswith('['): 
            if naam not in ('[heap]', '[stack]'): continue
        if eind - begin > 256*1024*1024: continue
        uit.append((begin, eind, naam))
    return uit

def main():
    if len(sys.argv) < 3: print(__doc__ or 'gebruik: zoek.py <pid|auto> patroon...'); return 1
    pid = pid_auto() if sys.argv[1] == 'auto' else int(sys.argv[1])
    if pid is None: print('GEEN GAME-PROCES'); return 1
    patronen = []
    for a in sys.argv[2:]:
        v = int(a, 0)
        patronen.append((a, v.to_bytes(4, 'little')))
    fd = os.open('/proc/%d/mem' % pid, os.O_RDONLY)
    kaart = kaarten(pid)
    totaal = sum(e-b for b, e, _ in kaart)
    print('### zoek pid=%d  %d gebieden, samen %.1f MB' % (pid, len(kaart), totaal/1048576.0))
    treffers = {a: [] for a, _ in patronen}
    blok = 4*1024*1024
    gelezen = 0
    for begin, eind, naam in kaart:
        pos = begin
        while pos < eind:
            n = min(blok, eind-pos)
            try:
                d = os.pread(fd, n, pos)
            except OSError:
                pos += n; continue
            if not d: break
            gelezen += len(d)
            for a, pat in patronen:
                p = d.find(pat)
                while p >= 0:
                    # overlapping treffers in hetzelfde 4-byte venster overslaan
                    abs_adres = pos + p
                    if not treffers[a] or treffers[a][-1] != abs_adres:
                        treffers[a].append(abs_adres)
                    p = d.find(pat, p+1)
            # randgeval: een patroon dat over de blokgrens valt
            if len(d) == n:
                try:
                    staart = os.pread(fd, 3, pos+n)
                except OSError:
                    staart = b''
                for a, pat in patronen:
                    for k in range(1, 4):
                        if d[-k:] + staart[:4-k] == pat:
                            treffers[a].append(pos + n - k)
            pos += n
    print('    gelezen: %.1f MB' % (gelezen/1048576.0))
    for a, _ in patronen:
        t = treffers[a]
        print('  patroon %s : %d treffer(s)' % (a, len(t)))
        for x in t[:12]:
            regio = next(('%#x-%#x %s' % (b, e, nm) for b, e, nm in kaart if b <= x < e), '?')
            print('        %#x   in %s' % (x, regio))
    return 0

if __name__ == '__main__':
    sys.exit(main())

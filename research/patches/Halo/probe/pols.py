#!/usr/bin/env python3
# pols.py -- pollt een vak van de schakelaartafel (0x50 bytes) van BUITEN het proces en print alleen
# WIJZIGINGEN. Doel: een indruk van de hulp vangen, zodat de externe lezer en het eigen logboek van de
# hulp (rg_rt_input: "indruk bit ... [6]=N") hetzelfde zeggen. Twee instrumenten, een antwoord.
#
# Gebruik: pols.py <pid|auto> <adres> <seconden> <interval_ms>
import os, sys, time, struct

def pid_auto():
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try: exe = os.readlink('/proc/%s/exe' % p)
        except OSError: continue
        if exe.endswith('halo/game'): return int(p)
    return None

def main():
    pid = pid_auto() if sys.argv[1] == 'auto' else int(sys.argv[1])
    adres = int(sys.argv[2], 0)
    sec = float(sys.argv[3]); iv = float(sys.argv[4])/1000.0
    if pid is None: print('pols: geen game-proces'); return 1
    fd = os.open('/proc/%d/mem' % pid, os.O_RDONLY)
    print('pols: pid=%d adres=%#x gedurende %.0f s, elke %.0f ms' % (pid, adres, sec, iv*1000))
    t0 = time.time(); vorige = None; n = 0
    while time.time() - t0 < sec:
        try: d = os.pread(fd, 0x50, adres)
        except OSError as e:
            print('pols: lezen mislukt: %s' % e); time.sleep(iv); continue
        w = struct.unpack_from('<16I', d, 0)
        regel = '[0]=%d [1]=%d [2]=%d [3]=%d [4]=%d [5]=%d [6]=%d [7]=%d' % (
            w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7])
        if regel != vorige:
            print('  t=%5.2f  %s' % (time.time()-t0, regel)); sys.stdout.flush()
            vorige = regel; n += 1
        time.sleep(iv)
    print('pols: klaar, %d verschillende standen gezien' % n)
    return 0

if __name__ == '__main__':
    sys.exit(main())

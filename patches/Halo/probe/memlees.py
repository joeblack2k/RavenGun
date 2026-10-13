#!/usr/bin/env python3
# memlees.py -- leest het gastgeheugen van de lopende Halo (/pm/g7/halo/game) van BUITEN het proces.
#
# HUISREGEL VAN DIT PROJECT: een instrument dat niet kan bewijzen dat het leeft, mag geen negativo
# melden. Daarom doet dit script ALTIJD eerst vier kalibraties op antwoorden die we al kennen, en pas
# daarna de metingen. Zakt een kalibratie, dan zegt het script dat en geeft het geen waarden door als
# bewijs (exitcode 2).
#
# Gebruik:  memlees.py <pid|auto> <uitmap> [label]
#   auto = zelf de pid zoeken via /proc/*/exe -> */halo/game
#
# Kalibraties:
#   K1  het beeld is het juiste bestand  (/proc/pid/exe eindigt op halo/game)
#   K2  de ELF-kop staat op vaddr 0x400000 (7f 45 4c 46 ...)  -- een antwoord dat vaststaat
#   K3  bestand EN proces geven dezelfde bytes op 0x400000 en op 0x180ae80  -- twee instrumenten
#   K4  een ONGEBRUIKT adres (0x1000) moet FALEN  -- anders leest dit script geen adressen maar iets anders
import os, sys, struct, json, time

V2O = 0x400000          # vaddr - bestandsafstand (volgt uit de programmaheaders)
BESTAND = os.environ.get('RG_GAME_BIN', '/tmp/io-brug-game.bin')

def pid_auto():
    for p in os.listdir('/proc'):
        if not p.isdigit(): continue
        try:
            exe = os.readlink('/proc/%s/exe' % p)
        except OSError:
            continue
        if exe.endswith('/halo/game') or exe.endswith('/g7/halo/game'):
            return int(p)
    return None

def lees(fd, adres, n):
    try:
        d = os.pread(fd, n, adres)
    except OSError as e:
        return None, str(e)
    if len(d) != n:
        return None, 'kort gelezen (%d van %d)' % (len(d), n)
    return d, None

def main():
    if len(sys.argv) < 3:
        print(__doc__ or 'gebruik: memlees.py <pid|auto> <uitmap> [label]'); return 1
    pid = pid_auto() if sys.argv[1] == 'auto' else int(sys.argv[1])
    uitmap = sys.argv[2]
    label = sys.argv[3] if len(sys.argv) > 3 else 'meting'
    os.makedirs(uitmap, exist_ok=True)
    if pid is None:
        print('GEEN GAME-PROCES GEVONDEN'); return 1
    print('### memlees %s  pid=%d  %s' % (label, pid, time.strftime('%F %H:%M:%S')))
    fd = os.open('/proc/%d/mem' % pid, os.O_RDONLY)
    fout = []

    # ---------------- A. KALIBRATIE ----------------
    print('--- A. KALIBRATIE (eerst bewijzen dat dit instrument leeft)')
    exe = os.readlink('/proc/%d/exe' % pid)
    k1 = exe.endswith('halo/game')
    print('  K1 exe = %s   -> %s' % (exe, 'GOED' if k1 else 'FOUT (niet de Halo-game)'))
    if not k1: fout.append('K1')

    kop, e = lees(fd, 0x400000, 16)
    k2 = kop is not None and kop[:4] == b'\x7fELF'
    print('  K2 ELF-kop op 0x400000: %s   -> %s' % (kop.hex() if kop else ('MISLUKT: %s' % e),
          'GOED' if k2 else 'FOUT'))
    if not k2: fout.append('K2')

    fb = open(BESTAND, 'rb') if os.path.exists(BESTAND) else None
    if fb is None:
        print('  K3 overgeslagen: %s bestaat niet' % BESTAND); fout.append('K3')
    else:
        k3 = True
        for adres, n in ((0x400000, 4096), (0x180ae80, 4096), (0x1f392a0, 4096)):
            pm, e = lees(fd, adres, n)
            fb.seek(adres - V2O); fm = fb.read(n)
            if pm is None:
                print('  K3 %#x: proces lezen MISLUKT (%s)' % (adres, e)); k3 = False; continue
            gelijk = sum(1 for a, b in zip(pm, fm) if a == b)
            # de eerste 64 bytes van elke 64-byte regel vergelijken zou te streng zijn: het spel wijzigt data
            print('  K3 %#x: %d van %d bytes gelijk tussen BESTAND en PROCES (%.1f%%)  eerste verschil op +%s'
                  % (adres, gelijk, n, 100.0*gelijk/n,
                     next((hex(i) for i in range(n) if pm[i] != fm[i]), 'geen')))
            if gelijk < n * 0.5: k3 = False
        print('  K3 twee instrumenten (bestand vs proces): %s' % ('GOED' if k3 else 'FOUT'))
        if not k3: fout.append('K3')

    k4 = True
    for adres in (0x1000, 0x7fff00000000):
        d, e = lees(fd, adres, 4)
        goed = (d is None)
        print('  K4 ongemapt %#x moet FALEN: %s   -> %s' % (adres, 'faalde inderdaad (%s)' % e if goed else 'GAF TOCH %s' % d.hex(), 'GOED' if goed else 'FOUT'))
        if not goed: k4 = False
    if not k4: fout.append('K4')

    levend = not fout
    print('  ==> INSTRUMENT %s%s' % ('GEKALIBREERD' if levend else 'NIET GEKALIBREERD',
                                     '' if levend else ' (gezakt: %s) -- waarden hieronder zijn GEEN bewijs' % ','.join(fout)))

    # ---------------- B. DE TWEE MASKERS ----------------
    # De lezers staan vast (disassembler): masker A 0x1766f40 wordt gelezen met and $0x6 / and $0x8 /
    # and $0x400 (0x4af8f0, 0x4af900, 0x4af910); masker B 0x181d8a0 met test $0x2/$0x4/$0x6 en heeft
    # een zetter (or, 0x61cd10), een wisser (and-not, 0x61cd20) en een toetser (test, 0x61cd40).
    print('--- B. DE TWEE MASKERS (de vraag: is bit 0x8 gezet?)')
    for naam, adres in (('0x1766f40', 0x1766f40), ('0x181d8a0', 0x181d8a0)):
        d, e = lees(fd, adres, 8)
        if d is None:
            print('  %s: LEZEN MISLUKT (%s)' % (naam, e)); continue
        w = struct.unpack('<2I', d)
        print('  %s: bytes %s | u32[0]=%d (%#x) u32[1]=%d (%#x) | bit 0x8 in u32[0]: %s | in u32[1]: %s'
              % (naam, d.hex(), w[0], w[0], w[1], w[1],
                 'GEZET' if w[0] & 0x8 else 'niet gezet', 'GEZET' if w[1] & 0x8 else 'niet gezet'))
        print('        de bits die de code toetst: in u32[0]: 0x2=%s 0x4=%s 0x6=%s 0x8=%s 0x400=%s'
              % (bool(w[0] & 2), bool(w[0] & 4), bool(w[0] & 6), bool(w[0] & 8), bool(w[0] & 0x400)))
        buurt, e2 = lees(fd, adres - 0x20, 0x60)
        if buurt:
            print('        de buurt (-0x20 .. +0x40): %s' % buurt.hex())

    # ---------------- C. DE HULP-TAFEL, bord 0, k=0..12 ----------------
    # Het adresmodel is bewezen uit de getter op 0x61e8d9: mov 0xaea0(%rsi,%rdx,1),%eax met rsi=0x180ae80,
    # rdx = bord*0x2200 + vak*0x50. Bord 0 vak 0 = 0x180ae80+0xaea0 = 0x1815d20.
    print('--- C. DE TAFEL WAAR DE HULP SCHRIJFT: bord 0 = 0x1815d20, k=0..12 van 0x50')
    print('        k   adres      [0]  [1]  [2]  [3]  [4]  [5]  [6]  [7]  [8]  (dwords)')
    with open(os.path.join(uitmap, 'hulptafel-%s.bin' % label), 'wb') as uit:
        for k in range(13):
            adres = 0x1815d20 + k * 0x50
            d, e = lees(fd, adres, 0x50)
            if d is None: print('        %2d  %#x  LEZEN MISLUKT (%s)' % (k, adres, e)); continue
            uit.write(d)
            w = struct.unpack_from('<16I', d, 0)
            print('        %2d  %#x  %4d %4d %4d %4d %4d %4d %4d %4d %4d'
                  % (k, adres, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8]))

    # ---------------- D. DE TAFEL, bord 0..3, 32 vakken ----------------
    print('--- D. DE GETTER-TAFEL (zelfde tafel!): bord 0..3 x 32 vakken; soort = vak[0]')
    print('        soort 0 = ANALOOG KANAAL (zustergetter 0x61ee70), 1 of 2 = SCHAKELAAR (getter 0x61e890)')
    for bord in range(4):
        basis = 0x1815d20 + bord * 0x2200
        d, e = lees(fd, basis, 32 * 0x50)
        if d is None: print('  bord %d (%#x): LEZEN MISLUKT (%s)' % (bord, basis, e)); continue
        with open(os.path.join(uitmap, 'tafel-bord%d-%s.bin' % (bord, label)), 'wb') as uit: uit.write(d)
        soorten = [struct.unpack_from('<I', d, s*0x50)[0] for s in range(32)]
        print('  bord %d (%#x) soorten: %s' % (bord, basis, ' '.join('%2d' % x for x in soorten)))
        for s in range(32):
            w = struct.unpack_from('<16I', d, s*0x50)
            if any(w):
                print('        vak %2d  %#x  [0]=%d [1]=%d [2]=%d [3]=%d [4]=%d [5]=%d [6]=%d [7]=%d [8]=%d'
                      % (s, basis + s*0x50, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8]))

    # ---------------- E. HET APPARAATBLOK EN DE OUDE ADRESSEN ----------------
    print('--- E. de eerder gelezen adressen (staan ze stil of beweegt het spel ze?)')
    for naam, adres, n in (('apparaatblok 0x1a7cb20', 0x1a7cb20, 0x80),
                           ('poortbyte 0x1aa37c0 (8 bytes)', 0x1aa37c0, 8),
                           ('aim-paar 0x1f392a0', 0x1f392a0, 16),
                           ('spelersaantal 0x1f392d0', 0x1f392d0, 4)):
        d, e = lees(fd, adres, n)
        print('  %-32s %s' % (naam, d.hex() if d else 'MISLUKT (%s)' % e))

    fd_json = os.path.join(uitmap, 'memlees-%s.json' % label)
    with open(fd_json, 'w') as j:
        json.dump({'pid': pid, 'tijd': time.time(), 'gekalibreerd': levend, 'gezakt': fout}, j)
    print('### einde memlees %s (gekalibreerd=%s)' % (label, levend))
    return 0 if levend else 2

if __name__ == '__main__':
    sys.exit(main())

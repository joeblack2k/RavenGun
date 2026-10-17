#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Herbouwt de importtabel van Pull The Trigger (PTW.exe) uit het spelbestand zelf.

WAAROM
  PTW.exe is ingepakt met WibuKey (secties .wkcrypt, stub in .text op 0x4d2000). De stub versleutelt precies een
  stuk van het spel: de importtabel (14 x IMAGE_IMPORT_DESCRIPTOR = 280 bytes). Die ontsleutelt hij met de WIBU-BOX
  (firmcode 250010, usercode 13) via WkWin32.dll (WkbCrypt2, commando 0x102), en daarna laadt de stub zelf alle
  DLL's en vult de IAT. Alles wat daarbij hoort ligt wel in het bestand zonder sleutel: de opzoektabellen (OFT) met
  hint/naam-verwijzingen, de IAT-groepen (FT) en de namen van de DLL's. Een WkWin32.dll zonder doos kan dus de
  platte tekst leveren als wij die tabel zelf opbouwen. Dat doet dit script. De code van het spel is niet
  versleuteld (entropie normaal, geen aanroep uit het spel naar de stub; zie docs/research/losse/PullTheTrigger.md).

HOE
  1. De stub-gegevens (.data van de stub): RVA en grootte van het versleutelde blok staan op vaste plekken die we
     uit de stub lezen (hij leest ze zelf op 0x4d40f0 en 0x4d40f8).
  2. Direct na het blok liggen de opzoektabellen (OFT), per DLL een groep, elke groep sluit af met een dword 0.
     Ordinalen hebben bit 31 gezet; de rest zijn RVA's van IMAGE_IMPORT_BY_NAME.
  3. De IAT-groepen (FT) beginnen op de plek waar de tabel van de eerste groep een kopie van heeft (zelfde inhoud):
     we zoeken de eerste OFT-dword in de sectie en lezen daar groepen met dezelfde lengtes.
  4. De namen van de DLL's: alle ASCII-strings op '.dll' in het gebied achter de tabellen. De linker zet de groepen
     in volgorde van de naam (hoofdletters vóór kleine letters, dus ADVAPI32 ... WSC32, ijl11, ole32); we kennen
     groep i toe aan de i-de naam en controleren dat bij elke groep met functienamen de naam direct achter het
     laatste hint/naam-blok van die groep staat.
Uitvoer: 280 bytes (14 descriptors; de laatste is nul) en een C-header.
"""
import struct
import sys


def lees_pe(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    secs = []
    so = pe + 24 + opt
    for i in range(nsec):
        _n, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, so + i * 40)
        secs.append((va, vs, ro, rs))
    return secs


def rva2off(secs, rva):
    for va, vs, ro, rs in secs:
        if va <= rva < va + max(vs, rs):
            return ro + rva - va
    raise ValueError('rva %#x ligt in geen sectie' % rva)


def lees_groepen(d, secs, rva, eind_rva):
    """Groepen dwords, elk afgesloten met 0, vanaf rva tot de eerste dword die geen RVA of ordinaal is."""
    groepen = []
    huidig = []
    start = rva
    o = rva
    while o < eind_rva:
        v = struct.unpack_from('<I', d, rva2off(secs, o))[0]
        if v == 0:
            groepen.append((start, huidig))
            huidig = []
            start = o + 4
            if len(groepen) >= 64:
                break
        else:
            huidig.append(v)
        o += 4
    return groepen


def hint_naam_einde(d, secs, rva):
    """RVA direct achter het IMAGE_IMPORT_BY_NAME op rva (2 bytes hint, naam, nul, aangevuld tot even)."""
    o = rva2off(secs, rva) + 2
    n = 2
    while d[o] != 0:
        o += 1
        n += 1
    n += 1
    if n % 2:
        n += 1
    return rva + n


def bouw_tabel(d, blok_rva, blok_grootte):
    """Geeft (bytes, beschrijving). Gooit ValueError als iets niet klopt."""
    secs = lees_pe(d)
    n_desc = blok_grootte // 20
    n_dll = n_desc - 1
    if blok_grootte % 20 or n_dll < 1:
        raise ValueError('blokgrootte %d is geen veelvoud van 20' % blok_grootte)

    # 2. OFT-groepen: direct achter het versleutelde blok, tot we n_dll groepen met inhoud hebben.
    oft = []
    o = blok_rva + blok_grootte
    while len(oft) < n_dll:
        # een groep: dwords tot de eerste 0
        inhoud = []
        start = o
        while True:
            v = struct.unpack_from('<I', d, rva2off(secs, o))[0]
            o += 4
            if v == 0:
                break
            inhoud.append(v)
        if not inhoud:
            raise ValueError('lege OFT-groep op %#x' % start)
        oft.append((start, inhoud))
    oft_einde = o

    # 3. IAT-groepen: zoek de plek waar de eerste dwords van de eerste groep nog eens staan.
    eerste = oft[0][1]
    patroon = b''.join(struct.pack('<I', v) for v in eerste) + b'\0\0\0\0'
    sec_va, _vs, sec_ro, sec_rs = next(s for s in secs if s[0] <= blok_rva < s[0] + max(s[1], s[3]))
    sec = d[sec_ro:sec_ro + sec_rs]
    pos = -1
    start_zoek = 0
    while True:
        pos = sec.find(patroon, start_zoek)
        if pos < 0:
            raise ValueError('IAT-kopie van de eerste groep niet gevonden')
        iat_rva = sec_va + pos
        if iat_rva != oft[0][0]:
            break
        start_zoek = pos + 1
    ft = []
    o = iat_rva
    for _s, inh in oft:
        ft.append(o)
        o += 4 * (len(inh) + 1)
    # controle: de IAT-groepen hebben dezelfde inhoud als de OFT-groepen
    for (_s, inh), f in zip(oft, ft):
        for i, v in enumerate(inh):
            if struct.unpack_from('<I', d, rva2off(secs, f + 4 * i))[0] != v:
                raise ValueError('IAT-groep op %#x wijkt af van zijn OFT-groep' % f)

    # 4. DLL-namen
    namen = []
    gebied_lo = oft_einde
    gebied_hi = sec_va + sec_rs
    blob = d[rva2off(secs, gebied_lo):rva2off(secs, gebied_lo) + (gebied_hi - gebied_lo)]
    i = 0
    while i < len(blob):
        j = blob.find(b'\0', i)
        if j < 0:
            break
        s = blob[i:j]
        if len(s) >= 5 and s.lower().endswith(b'.dll') and all(32 <= c < 127 for c in s):
            namen.append((gebied_lo + i, s.decode()))
        i = j + 1
    if len(namen) != n_dll:
        raise ValueError('%d DLL-namen gevonden, %d verwacht: %r' % (len(namen), n_dll, namen))
    gesorteerd = sorted(namen, key=lambda t: t[1].encode())
    # 'ijl11.dll' < 'ole32.dll' < ... : de linker sorteert bytewijs

    # controle bij groepen met functienamen: de linker zet de naam van een DLL direct achter een van zijn hint/naam-blokken
    # (niet altijd het laatste: de blokken van een groep staan soms door elkaar).
    for (start, inh), (nrva, nnaam) in zip(oft, gesorteerd):
        if inh[0] & 0x80000000:
            continue
        einden = {hint_naam_einde(d, secs, v) for v in inh if not (v & 0x80000000)}
        if not (einden & {nrva, nrva - 1}):
            raise ValueError('groep op %#x: toegekend %s (op %#x), maar geen enkel hint/naam-blok van de groep eindigt daar'
                             % (start, nnaam, nrva))

    uit = bytearray()
    beschrijving = []
    for (start, inh), f, (nrva, nnaam) in zip(oft, ft, gesorteerd):
        uit += struct.pack('<IIIII', start, 0, 0, nrva, f)
        beschrijving.append((nnaam, start, f, nrva, len(inh)))
    uit += b'\0' * 20
    assert len(uit) == blok_grootte
    return bytes(uit), beschrijving


def stub_blok(d):
    """RVA en grootte van het versleutelde blok uit de stub-gegevens (.data van de stub, VA 0x4d40f0 / 0x4d40f8)."""
    secs = lees_pe(d)
    rva = struct.unpack_from('<I', d, rva2off(secs, 0xd40f0))[0]
    grootte = struct.unpack_from('<I', d, rva2off(secs, 0xd40f8))[0]
    return rva, grootte


def main(argv):
    if len(argv) < 2:
        print('gebruik: rg_ptt_imports.py PTW.exe [--bin uit.bin] [--header uit.h]')
        return 2
    d = open(argv[1], 'rb').read()
    rva, grootte = stub_blok(d)
    tabel, besch = bouw_tabel(d, rva, grootte)
    print('blok RVA %#x, %d bytes, %d DLL\'s' % (rva, grootte, len(besch)))
    for naam, oft, ft, nrva, n in besch:
        print('  %-14s OFT %#x FT %#x Name %#x  %d imports' % (naam, oft, ft, nrva, n))
    ciph = d[rva2off(lees_pe(d), rva):rva2off(lees_pe(d), rva) + grootte]
    if '--bin' in argv:
        open(argv[argv.index('--bin') + 1], 'wb').write(tabel)
    if '--header' in argv:
        regels = ['/* SPDX-License-Identifier: GPL-3.0-or-later */',
                  '/* Gegenereerd door tools/rg-ptt/rg_ptt_imports.py uit %s; niet met de hand wijzigen. */' % argv[1].split('/')[-1],
                  '#define PTT_BLOCK_RVA 0x%xu' % rva, '#define PTT_BLOCK_SIZE %du' % grootte, '']
        for naam, arr in (('ptt_cipher', ciph), ('ptt_plain', tabel)):
            regels.append('static const unsigned char %s[PTT_BLOCK_SIZE] = {' % naam)
            for i in range(0, len(arr), 12):
                regels.append('    ' + ', '.join('0x%02x' % b for b in arr[i:i + 12]) + ',')
            regels.append('};')
            regels.append('')
        open(argv[argv.index('--header') + 1], 'w').write('\n'.join(regels))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

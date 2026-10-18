#!/usr/bin/env python3
"""RavenGun: maakt een opgeslagen instelling ("adj") of audit-bestand in het formaat van de Raw Thrills-kern (RTDT).

Waarom: de games van Raw Thrills / Play Mechanix (Ghostbusters, Walking Dead, Halo) lezen hun operator-instellingen uit
<pmuser>/adj/save/<groep>/<naam>.<n>.txt en hun audits uit <pmuser>/aud/save/<naam>.<n>.xml (n = 0, 1, 2: drie kopieen, de
game neemt de eerste die klopt). Een bestand die niet klopt wordt genegeerd en de game valt terug op zijn standaardwaarde.
Dit gereedschap schrijft zulke bestanden zelf, zodat een seed uit eigen hand komt en geen kopie van een dump is.

Formaat (gemeten 2026-10-18 aan bestanden die de game zelf schreef, sha1 klopt bij alle gecontroleerde):
    u32 20 | sha1(lading) [20 bytes] | u32 len(lading) | lading
  adj-lading   : ASCII met CRLF:  "# RTDT Data Type AdjValStore\\r\\nname = X\\r\\nversion = 0\\r\\ntime_set = 1\\r\\nval = V\\r\\n" + NUL
  audit-lading : UTF-16LE met BOM (ff fe), de XML-tekst van de audit

Gebruik:
  rg-rtdt-seed.py adj  <uitmap> <groep> <naam> <waarde>          -> <uitmap>/<groep>/<naam>.{0,1,2}.txt
  rg-rtdt-seed.py aud  <uitmap> <naam> <xml-bronbestand-utf8>    -> <uitmap>/<naam>.{0,1,2}.xml
"""
import hashlib
import os
import struct
import sys


def verpak(lading: bytes) -> bytes:
    return struct.pack('<I', 20) + hashlib.sha1(lading).digest() + struct.pack('<I', len(lading)) + lading


def adj_lading(naam: str, waarde: str) -> bytes:
    t = ('# RTDT Data Type AdjValStore\r\nname = %s\r\nversion = 0\r\ntime_set = 1\r\nval = %s\r\n' % (naam, waarde))
    return t.encode('ascii') + b'\0'


def aud_lading(xml_utf8: str) -> bytes:
    return b'\xff\xfe' + xml_utf8.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-16-le')


def schrijf(pad: str, data: bytes) -> None:
    os.makedirs(os.path.dirname(pad), exist_ok=True)
    with open(pad, 'wb') as f:
        f.write(data)


def main(a):
    if len(a) == 6 and a[1] == 'adj':
        _, _, uit, groep, naam, waarde = a
        for n in range(3):
            schrijf(os.path.join(uit, groep, '%s.%d.txt' % (naam, n)), verpak(adj_lading(naam, waarde)))
        return 0
    if len(a) == 5 and a[1] == 'aud':
        _, _, uit, naam, bron = a
        xml = open(bron, encoding='utf-8').read()
        for n in range(3):
            schrijf(os.path.join(uit, '%s.%d.xml' % (naam, n)), verpak(aud_lading(xml)))
        return 0
    sys.stderr.write(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))

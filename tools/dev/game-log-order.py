#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Haal de meldingen van een Windows-game uit een Wine-logboek (WINEDEBUG=...,warn+seh) en vergelijk twee runs.

Een game die OutputDebugString gebruikt, geeft onder Wine met `warn+seh` regels als
    0108:warn:seh:dispatch_exception "*INF* Game Start\\n"
Dit script zet ze op volgorde (met regelnummer), decodeert Shift-JIS en toont bij twee logboeken waar de volgorde voor het eerst afwijkt.

Gebruik:  tools/dev/game-log-order.py LOG [LOG2]
  een logboek: alle meldingen;  twee logboeken: de gemeenschappelijke kop en het eerste verschil (tekst na `*XXX* ` vergeleken).
"""
import re
import sys

PAT = re.compile(rb'dispatch_exception "(\*[A-Z]{3}\*) (.*?)(?:\\n)?"\s*$')


def unescape(b: bytes) -> bytes:
    # de logger schrijft niet-ASCII als \xNN
    return re.sub(rb'\\x([0-9a-fA-F]{2})', lambda m: bytes([int(m.group(1), 16)]), b)


def read(path):
    out = []
    with open(path, 'rb') as f:
        for n, line in enumerate(f, 1):
            m = PAT.search(line.rstrip(b'\r\n'))
            if not m:
                continue
            text = unescape(m.group(2))
            try:
                t = text.decode('cp932')
            except UnicodeDecodeError:
                t = text.decode('latin-1')
            out.append((n, m.group(1).decode(), t))
    return out


def show(rows, label, start=0, limit=None):
    print(f'== {label}: {len(rows)} meldingen')
    for n, lvl, t in rows[start:(start + limit if limit else None)]:
        print(f'  {n:6d} {lvl} {t}')


def main(argv):
    if len(argv) not in (2, 3):
        print(__doc__)
        return 2
    a = read(argv[1])
    if len(argv) == 2:
        show(a, argv[1])
        return 0
    b = read(argv[2])
    i = 0
    while i < len(a) and i < len(b) and a[i][2] == b[i][2]:
        i += 1
    print(f'gemeenschappelijke kop: {i} meldingen (A {len(a)}, B {len(b)})')
    show(a, 'A (' + argv[1] + ') vanaf het verschil', i, 12)
    show(b, 'B (' + argv[2] + ') vanaf het verschil', i, 12)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

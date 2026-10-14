#!/usr/bin/env python3
"""Genereert docs/compatibility.md (Engels, voor gebruikers) uit de meegeleverde instellingen per game (patches/<Game>/game.ini:
[Game] Name en [Status] Status, NoteEN). De lijst is alleen een weergave van die bestanden: pas ze daar aan, draai dit script
opnieuw en commit het resultaat. De gebruikerslaag telt hier niet mee (de lijst moet reproduceerbaar zijn).

Gebruik: scripts/gen-compatibility.py            schrijft docs/compatibility.md
         scripts/gen-compatibility.py --check    controleert dat docs/compatibility.md actueel is (exit 1 anders; voor ctest)
         scripts/gen-compatibility.py --stdout   toont de tekst
Geen markdown-tabellen (docs/ gebruikt lijsten)."""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ROOT, 'scripts'))
import gamesettings as gs  # noqa: E402

OUT = os.path.join(ROOT, 'docs', 'compatibility.md')
PATCHES = gs.shipped_dir()   # de bovenste meegeleverde laag, afgeleid van gamesettings.patch_layers()

SECTIONS = [
    ('working', 'Working',
     'These games run on RavenGun\'s own loader and start straight from RavenGun (double-click), without TeknoParrot; they were '
     'played with the lightgun (PS Move or mouse) and checked on the TV. The note says what was tested.'),
    ('playable', 'Playable',
     'These games start from RavenGun and can be played with the lightgun. Some are not yet confirmed with a real PS Move in '
     'every detail; the note says what was tested.'),
    ('untested', 'Not tested yet',
     'These games are set up, but nobody has tried them with the lightgun yet.'),
    ('parked', 'Not working yet',
     'These games do not start, or are blocked by something RavenGun does not work around (for example copy protection). '
     'They are listed so you know what to expect; they may start working in a later version.'),
]


def build():
    names, status, en = {}, {}, {}
    nowhere = os.path.join(os.devnull, 'geen-gebruikerslaag')
    for game in gs.list_games(PATCHES, nowhere):
        g = gs.load_game(game, shipped=PATCHES, user=nowhere)
        if g.get('Game', 'Name'):
            names[game] = g.get('Game', 'Name')
        if g.get('Status', 'Status'):
            status[game] = g.get('Status', 'Status')
        if g.get('Status', 'NoteEN'):
            en[game] = g.get('Status', 'NoteEN')
    lines = [
        '# Game compatibility',
        '',
        '<!-- Generated from patches/<Game>/game.ini by scripts/gen-compatibility.py. Do not edit by hand. -->',
        '',
        'This list shows what has been tried so far, on one setup (a Steam Deck with a PS Move ZCM1 and a PS3 Eye). It is a '
        'snapshot, not a promise: arcade games differ a lot, and some depend on the TeknoParrot runtime or on your own dump.',
        'RavenGun ships no games. You need your own legally obtained copies.',
        '',
    ]
    for key, heading, intro in SECTIONS:
        members = sorted((p for p, s in status.items() if s == key), key=lambda p: names.get(p, p).lower())
        if not members:
            continue
        lines += ['## %s (%d)' % (heading, len(members)), '', intro, '']
        for p in members:
            lines.append('- **%s**: %s' % (names.get(p, p), en.get(p, 'No note.').strip()))
        lines.append('')
    unknown = sorted(p for p, s in status.items() if s not in {k for k, _, _ in SECTIONS})
    if unknown:
        raise SystemExit('unknown status values for: ' + ', '.join(unknown))
    missing = sorted(set(status) - set(en))
    if missing:
        raise SystemExit('no English note (NoteEN in [Status]) for: ' + ', '.join(missing))
    return '\n'.join(lines).rstrip() + '\n'


def main():
    text = build()
    if '--stdout' in sys.argv:
        sys.stdout.write(text)
        return 0
    if '--check' in sys.argv:
        try:
            with open(OUT, encoding='utf-8') as f:
                current = f.read()
        except OSError:
            current = ''
        if current != text:
            print('docs/compatibility.md is out of date: run scripts/gen-compatibility.py', file=sys.stderr)
            return 1
        return 0
    with open(OUT, 'w', encoding='utf-8') as f:
        f.write(text)
    print('written: ' + OUT)
    return 0


if __name__ == '__main__':
    sys.exit(main())

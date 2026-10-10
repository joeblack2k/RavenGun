#!/usr/bin/env python3
"""Genereert docs/compatibility.md (Engels, voor gebruikers) uit packaging/games-notes.json ("games_en" en "status") en de
spelnamen uit packaging/game-options.json. De lijst is alleen een weergave van die bestanden: pas ze daar aan, draai dit script
opnieuw en commit het resultaat.

Gebruik: scripts/gen-compatibility.py            schrijft docs/compatibility.md
         scripts/gen-compatibility.py --check    controleert dat docs/compatibility.md actueel is (exit 1 anders; voor ctest)
         scripts/gen-compatibility.py --stdout   toont de tekst
Geen markdown-tabellen (docs/ gebruikt lijsten)."""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOTES = os.path.join(ROOT, 'packaging', 'games-notes.json')
OPTIONS = os.path.join(ROOT, 'packaging', 'game-options.json')
OUT = os.path.join(ROOT, 'docs', 'compatibility.md')

SECTIONS = [
    ('playable', 'Playable',
     'These games start from RavenGun and can be played with the lightgun. Some are not yet confirmed with a real PS Move in '
     'every detail; the note says what was tested.'),
    ('untested', 'Not tested yet',
     'These games are set up, but nobody has tried them with the lightgun yet.'),
    ('parked', 'Not working yet',
     'These games do not start, or are blocked by something RavenGun does not work around (for example copy protection). '
     'They are listed so you know what to expect; they may start working in a later version.'),
]


def load(path):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def titles(options):
    """Profielnaam (TeknoParrot-profiel) -> spelnaam uit game-options.json."""
    t = {}
    for g in options.get('games', {}).values():
        prof = g.get('tp_profile')
        if prof and g.get('title'):
            t[prof] = g['title']
    return t


# Profielen zonder tp_profile in game-options.json (eigen route) of met een afwijkende naam.
EXTRA_TITLES = {
    'TombRaider': 'Tomb Raider: Firestorm Assault',
    'RabbidsHollywood': 'Rabbids Hollywood Arcade',
    'LGS': "Let's Go Safari",
}


def build():
    notes = load(NOTES)
    names = titles(load(OPTIONS))
    names.update({k: v for k, v in EXTRA_TITLES.items() if k not in names})
    status = notes['status']
    en = notes['games_en']
    lines = [
        '# Game compatibility',
        '',
        '<!-- Generated from packaging/games-notes.json by scripts/gen-compatibility.py. Do not edit by hand. -->',
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
        raise SystemExit('no English note (games_en) for: ' + ', '.join(missing))
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

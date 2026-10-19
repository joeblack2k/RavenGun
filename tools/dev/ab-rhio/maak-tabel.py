#!/usr/bin/env python3
# Zet de uitkomst van de A/B-proef (ab2.sh) om in een markdown-tabel met een telling per variant.
# Elke start staat in resultaten.txt, ook een start waarbij de game stopte (review 14, ronde 2). De
# negende kolom is de reden: draait | gestopt | ongeldig-daemon-bezet. Oudere bestanden hebben die
# kolom niet; daar volgt hij uit "draait" (ja = draait, anders gestopt). Een ongeldige start (de daemon
# was al bezet) staat wel in de lijst maar telt niet mee in de samenvatting, behalve in zijn eigen kolom.
import io, os, sys, collections

standaard = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'rg-rhio', 'out', 'ab-rhio',
                         'resultaten.txt')
pad = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(standaard)
regels = [r.split() for r in io.open(pad, encoding='utf-8').read().split('\n') if r.strip()]

per = collections.defaultdict(lambda: {'n': 0, 'ok': 0, 'draait': 0, 'gestopt': 0, 'ongeldig': 0, 'foto': [],
                                       'sha': set()})
rijen = []
for r in regels:
    # variant ronde sha foto ok draait logregels bordlezers [reden]
    if len(r) < 8:
        continue
    v, ronde, sha, foto, ok, draait, lr, bord = r[:8]
    reden = r[8] if len(r) > 8 else ('draait' if draait == 'ja' else 'gestopt')
    d = per[v]
    rijen.append((v, ronde, sha, foto, ok, draait, lr, bord, reden))
    if reden == 'ongeldig-daemon-bezet':
        d['ongeldig'] += 1
        continue
    d['n'] += 1
    d['ok'] += 1 if ok == 'ja' else 0
    d['draait'] += 1 if draait == 'ja' else 0
    d['gestopt'] += 1 if draait != 'ja' else 0
    d['foto'].append(int(foto))
    d['sha'].add(sha)

print('| variant | start | sha1 | foto (bytes) | voorbij BOOTING | draait nog | logregels | bordlezers | reden |')
print('|---|---|---|---|---|---|---|---|---|')
for rij in rijen:
    print('| %s | %s | %s | %s | %s | %s | %s | %s | %s |' % rij)

print()
print('| variant | geldige starts | voorbij BOOTING | draait nog | gestopt | ongeldig (daemon bezet) '
      '| foto kleinste | foto grootste | sha1 |')
print('|---|---|---|---|---|---|---|---|---|')
for v in ('OLD', 'NEW', 'NOSTRIP'):
    d = per.get(v)
    if not d:
        continue
    print('| %s | %d | %d | %d | %d | %d | %s | %s | %s |' % (
        v, d['n'], d['ok'], d['draait'], d['gestopt'], d['ongeldig'],
        min(d['foto']) if d['foto'] else '-', max(d['foto']) if d['foto'] else '-', ','.join(sorted(d['sha']))))

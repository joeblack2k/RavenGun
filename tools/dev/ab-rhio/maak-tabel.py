#!/usr/bin/env python3
# Zet de uitkomst van de A/B-proef (ab.sh) om in een markdown-tabel met een telling per variant.
import io, sys, collections

pad = sys.argv[1] if len(sys.argv) > 1 else '/tmp/rg-ab/resultaten.txt'
regels = [r.split() for r in io.open(pad, encoding='utf-8').read().split('\n') if r.strip()]

per = collections.defaultdict(lambda: {'n': 0, 'ok': 0, 'draait': 0, 'foto': [], 'sha': set()})
rijen = []
for r in regels:
    # variant ronde sha foto ok draait logregels bordlezers
    if len(r) < 8:
        continue
    v, ronde, sha, foto, ok, draait, lr, bord = r[:8]
    d = per[v]
    d['n'] += 1
    d['ok'] += 1 if ok == 'ja' else 0
    d['draait'] += 1 if draait == 'ja' else 0
    d['foto'].append(int(foto))
    d['sha'].add(sha)
    rijen.append((v, ronde, sha, foto, ok, draait, lr, bord))

print('| variant | start | sha1 | foto (bytes) | voorbij BOOTING | draait nog | logregels | bordlezers |')
print('|---|---|---|---|---|---|---|---|')
for v, ronde, sha, foto, ok, draait, lr, bord in rijen:
    print('| %s | %s | %s | %s | %s | %s | %s | %s |' % (v, ronde, sha, foto, ok, draait, lr, bord))

print()
print('| variant | starts | voorbij BOOTING | draait nog | foto kleinste | foto grootste | sha1 |')
print('|---|---|---|---|---|---|---|')
for v in ('OLD', 'NEW', 'NOSTRIP'):
    d = per.get(v)
    if not d:
        continue
    print('| %s | %d | %d | %d | %d | %d | %s |' % (
        v, d['n'], d['ok'], d['draait'], min(d['foto']), max(d['foto']), ','.join(sorted(d['sha']))))

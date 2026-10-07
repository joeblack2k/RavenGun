#!/usr/bin/env python3
"""Kompasrichting (tilt-gecompenseerd) per stilstandsegment in een RavenGun-opname.

Gebruik: python3 -I tools/mag_segments.py OPNAME.jsonl KALIBRATIE.json [--gyro-max 0.08] [--min-s 2.5]

Per segment waarin de gyro stil is (|w| < gyro-max rad/s gedurende >= min-s): gemiddelde accel
(omhoog-richting) en gekalibreerd veld; daaruit de heading van een horizontale apparaatas. Verschillen
tussen segmenten waarin op hetzelfde punt gericht wordt, zijn de herhaalbaarheid (hand + magnetometer).
Alleen getallen; geen beelden.
"""
import json, math, sys


def load_cal(p):
    c = json.load(open(p))
    # Fase 4: B = diag(-1,+1,-1) * A (ruw - b) ("b_map"); oude bestanden (alleen y_flip) zijn het
    # tegengestelde teken. Alleen de dip hangt daarvan af (positief = veld omlaag).
    neg = c.get('b_map', 'diag(-1,1,-1)') in ('diag(-1,1,-1)', 'diag(-1,-1,-1)')
    return c['offset'], c['soft'], c.get('y_flip', True), neg


def apply(cal, raw):
    off, A, yf, neg = cal
    d = [raw[i] - off[i] for i in range(3)]
    m = [sum(A[i][j] * d[j] for j in range(3)) for i in range(3)]
    if yf:
        m[1] = -m[1]
    if neg:
        m = [-x for x in m]
    return m


def norm(v):
    return math.sqrt(sum(x * x for x in v))


def unit(v):
    n = norm(v)
    return [x / n for x in v]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def main():
    args = sys.argv[1:]
    gmax, mins = 0.08, 2.5
    if '--gyro-max' in args:
        gmax = float(args[args.index('--gyro-max') + 1])
    if '--min-s' in args:
        mins = float(args[args.index('--min-s') + 1])
    rec, calp = args[0], args[1]
    cal = load_cal(calp)
    rows = []
    for line in open(rec):
        o = json.loads(line)
        if o.get('k') != 'r' or 'm' not in o:
            continue
        a, g = o['a'], o['g']
        acc = [(a[i] + a[i + 3]) / 2 for i in range(3)]
        gyr = [(g[i] + g[i + 3]) / 2 for i in range(3)]
        rows.append((o['t'] / 1e9, acc, gyr, apply(cal, o['m'])))
    # stilstandsegmenten
    segs, cur = [], []
    for r in rows:
        if norm(r[2]) < gmax:
            cur.append(r)
        else:
            if cur and cur[-1][0] - cur[0][0] >= mins:
                segs.append(cur)
            cur = []
    if cur and cur[-1][0] - cur[0][0] >= mins:
        segs.append(cur)
    if not segs:
        print('geen stilstandsegmenten gevonden')
        return
    info = []
    for s in segs:
        k = len(s) // 5  # randen weglaten
        core = s[k:len(s) - k] or s
        up = unit([sum(r[1][i] for r in core) / len(core) for i in range(3)])
        m = [sum(r[3][i] for r in core) / len(core) for i in range(3)]
        h = unit([m[i] - dot(m, up) * up[i] for i in range(3)])
        east = cross(h, up)
        dip = math.degrees(math.asin(max(-1, min(1, -dot(unit(m), up)))))
        info.append((s[0][0], s[-1][0], up, h, east, dip, norm(m)))
    # horizontale apparaatas: as met kleinste gemiddelde verticale component
    axes = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    v = min(axes, key=lambda ax: sum(abs(dot(ax, x[2])) for x in info))
    print(f'{len(segs)} stilstandsegmenten (|w|<{gmax} rad/s, >= {mins} s); referentie-as apparaat {v}')
    h0 = None
    for i, (t0, t1, up, h, east, dip, fm) in enumerate(info):
        vh = [v[j] - dot(v, up) * up[j] for j in range(3)]
        hd = math.degrees(math.atan2(dot(vh, east), dot(vh, h)))
        if h0 is None:
            h0 = hd
        d = (hd - h0 + 180) % 360 - 180
        tilt = math.degrees(math.asin(max(-1, min(1, dot(v, up)))))
        print(f'  #{i + 1:2d}  {t0:7.1f}-{t1:7.1f} s ({t1 - t0:4.1f} s)  heading {d:+7.2f} gr  as-helling {tilt:+6.1f} gr  dip {dip:+6.1f}  |m| {fm:.3f}')


if __name__ == '__main__':
    main()

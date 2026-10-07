#!/usr/bin/env python3
"""Vergelijkt de magnetometer-heading met de gyro-geïntegreerde heading over een opname.

Gebruik: python3 -I tools/mag_vs_gyro.py OPNAME.jsonl KALIBRATIE.json [--bias-s 5] [--axis 1]

Let op: beide headings tellen tegen de klok in (om +Z); het verschil is alleen zinvol als beide
hetzelfde draaizin hebben (fase 4: tekenfout hersteld). Het globale teken van de magnetometer (b_map)
maakt voor dit verschil niets uit. De C++-variant met de echte fusie: ravengun-analyze fusion.

De gyro (bias geschat uit de eerste --bias-s seconden stilstand) geeft over ~1 minuut een betrouwbare
relatieve oriëntatie. 'Omhoog' komt uit diezelfde integratie (niet uit de accelerometer, die tijdens
bewegen versnellingen ziet). Het verschil mag-heading min gyro-heading, rond nul gezet, is de fout van
de magnetometer als functie van de stand (plus de gyrodrift). Alleen getallen.
"""
import json, math, sys


def qmul(a, b):
    w1, x1, y1, z1 = a; w2, x2, y2, z2 = b
    return (w1*w2-x1*x2-y1*y2-z1*z2, w1*x2+x1*w2+y1*z2-z1*y2, w1*y2-x1*z2+y1*w2+z1*x2, w1*z2+x1*y2-y1*x2+z1*w2)


def qrot(q, v):  # v in apparaat -> wereld
    w, x, y, z = q
    p = (0, v[0], v[1], v[2])
    r = qmul(qmul(q, p), (w, -x, -y, -z))
    return r[1:]


def qinv_rot(q, v):  # wereld -> apparaat
    w, x, y, z = q
    return qrot((w, -x, -y, -z), v)


def unit(v):
    n = math.sqrt(sum(c*c for c in v)); return [c/n for c in v]


def dot(a, b): return sum(x*y for x, y in zip(a, b))


def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def main():
    a = sys.argv[1:]
    bias_s = float(a[a.index('--bias-s')+1]) if '--bias-s' in a else 5.0
    axis = int(a[a.index('--axis')+1]) if '--axis' in a else 1
    c = json.load(open(a[1])); off, A, yf = c['offset'], c['soft'], c.get('y_flip', True)
    rows = []
    for line in open(a[0]):
        o = json.loads(line)
        if o.get('k') == 'r' and 'm' in o:
            d = [o['m'][i]-off[i] for i in range(3)]
            m = [sum(A[i][j]*d[j] for j in range(3)) for i in range(3)]
            if yf: m[1] = -m[1]
            rows.append((o['t']/1e9, o['a'], o['g'], m))
    t0 = rows[0][0]
    still = [r for r in rows if r[0]-t0 < bias_s]
    bias = [sum((r[2][i]+r[2][i+3])/2 for r in still)/len(still) for i in range(3)]
    up_dev0 = unit([sum((r[1][i]+r[1][i+3])/2 for r in still)/len(still) for i in range(3)])
    # wereldstelsel: Z omhoog = up_dev0 bij q=identiteit; we werken met q: apparaat->wereld met
    # wereld := apparaatstelsel op t0, en 'omhoog' in wereld = up_dev0.
    up_w = up_dev0
    v = [0, 0, 0]; v[axis] = 1
    q = (1.0, 0.0, 0.0, 0.0)
    prev = rows[0][0]
    diffs = []
    ref = None
    for t, acc, g, m in rows:
        dt = t - prev; prev = t
        for k in (0, 3):
            w = [g[k+i]-bias[i] for i in range(3)]
            h = dt/2
            ang = math.sqrt(sum(x*x for x in w))*h
            if ang > 0:
                s = math.sin(ang/2)/ (ang/h) if False else math.sin(ang/2)
                ax = [x/ math.sqrt(sum(y*y for y in w)) for x in w]
                dq = (math.cos(ang/2), ax[0]*s, ax[1]*s, ax[2]*s)
                q = qmul(q, dq)
        # mag naar wereld (via gyro-oriëntatie), horizontaal projecteren t.o.v. up_w
        mw = qrot(q, m)
        mh = [mw[i]-dot(mw, up_w)*up_w[i] for i in range(3)]
        vw = qrot(q, v)
        vh = [vw[i]-dot(vw, up_w)*up_w[i] for i in range(3)]
        if math.sqrt(dot(vh, vh)) < 0.5:  # as te steil: heading slecht bepaald
            continue
        # heading van de as t.o.v. magnetisch noorden (in wereld) ...
        # Oost = omhoog x noord (rechtshandig, Z omhoog): kompasrichting met de klok mee, net als hd_gyr
        # hieronder (x0 -> y0 = omhoog x x0). Fase 4: hier stond cross(n, up_w); dan draaide de
        # magnetometer-heading tegengesteld aan de gyro-heading en verdubbelde elke draai in het verschil
        # (de -11..+17 graden tijdens bewegen in 175604-richttest).
        n = unit(mh); e = cross(up_w, n)
        hd_mag = math.degrees(math.atan2(dot(vh, e), dot(vh, n)))
        # ... en t.o.v. een vaste wereldrichting (gyro)
        x0 = unit([1 - up_w[0]*up_w[0], -up_w[0]*up_w[1], -up_w[0]*up_w[2]]) if abs(up_w[0]) < 0.9 else unit([-up_w[1]*up_w[0], 1-up_w[1]*up_w[1], -up_w[1]*up_w[2]])
        y0 = cross(up_w, x0)
        hd_gyr = math.degrees(math.atan2(dot(vh, y0), dot(vh, x0)))
        d = hd_mag - hd_gyr
        if ref is None: ref = d
        d = (d - ref + 180) % 360 - 180
        rate = math.degrees(math.sqrt(sum((g[i]-bias[i])**2 for i in range(3))))
        diffs.append((t-t0, d, rate))
    ds = sorted(x[1] for x in diffs)
    def pct(p): return ds[min(len(ds)-1, int(p*len(ds)))]
    print(f'{len(diffs)} samples, bias-venster {bias_s} s, as {axis}')
    print(f'mag - gyro (graden, rond 0 gezet): p5 {pct(0.05):+.2f}  p50 {pct(0.5):+.2f}  p95 {pct(0.95):+.2f}  min {ds[0]:+.2f}  max {ds[-1]:+.2f}')
    for lo in range(0, int(diffs[-1][0])+1, 5):
        seg = [x for x in diffs if lo <= x[0] < lo+5]
        if seg:
            print(f'  {lo:3d}-{lo+5:3d} s: gem {sum(x[1] for x in seg)/len(seg):+6.2f}  bereik {min(x[1] for x in seg):+6.2f}..{max(x[1] for x in seg):+6.2f}  max draaisnelheid {max(x[2] for x in seg):6.0f} gr/s')


if __name__ == '__main__':
    main()

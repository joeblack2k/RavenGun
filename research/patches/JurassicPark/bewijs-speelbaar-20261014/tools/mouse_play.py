#!/usr/bin/env python3
# Speelt met de echte muis van de Steam Controller (injectie in /dev/input/event7): richten langs een lus, trekker vasthouden.
import math, os, struct, sys, time
fd = os.open("/dev/input/event7", os.O_WRONLY)
def ev(t, c, v):
    s = time.time(); os.write(fd, struct.pack("llHHi", int(s), int((s % 1) * 1e6), t, c, v))
def syn(): ev(0, 0, 0)
def move(dx, dy):
    n = max(1, int(max(abs(dx), abs(dy)) // 12))
    for i in range(n):
        ev(2, 0, int(dx / n)); ev(2, 1, int(dy / n)); syn(); time.sleep(0.006)
W = 1920.0; H = 1920.0 * 9 / 16       # counts per scherm (x), y op dezelfde fysieke schaal (16/9)
# naar linksboven trekken (de daemon klemt op 0..1), dan naar het midden
move(-3000, -3000); move(W / 2, H / 2)
x, y = 0.5, 0.5
dur = float(sys.argv[1]) if len(sys.argv) > 1 else 120
t0 = time.time(); k = 0
while time.time() - t0 < dur:
    k += 1
    tx = 0.5 + 0.33 * math.sin(k * 0.37); ty = 0.5 + 0.25 * math.cos(k * 0.53)
    move((tx - x) * W, (ty - y) * H); x, y = tx, ty
    ev(1, 0x110, 1); syn()
    for i in range(8):                  # trekker vast, kleine beweging erbij (zoals een speler)
        move(6 * math.sin(i), 4 * math.cos(i)); time.sleep(0.1)
    ev(1, 0x110, 0); syn(); time.sleep(0.15)
os.close(fd)
print("muis-lus klaar na", k, "rondes")

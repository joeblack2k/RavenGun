#!/usr/bin/env python3
# Muisbewegingen en -knoppen INJECTEREN in een echt muisapparaat (/dev/input/eventN, schrijfbaar via de ACL van de gebruiker):
# de kernel geeft ze aan alle lezers van dat apparaat, dus ook aan de daemon (mouse-gun) -- precies zoals een echte muis.
# Gebruik: puck_inject.py /dev/input/event7 rel DX DY | left down|up|tap | right down|up|tap
import os, struct, sys, time

dev, cmd = sys.argv[1], sys.argv[2]
fd = os.open(dev, os.O_WRONLY)


def ev(t, c, v):
    s = time.time()
    os.write(fd, struct.pack("llHHi", int(s), int((s % 1) * 1e6), t, c, v))


def syn(): ev(0, 0, 0)


if cmd == "rel":
    dx, dy = int(sys.argv[3]), int(sys.argv[4])
    n = max(1, max(abs(dx), abs(dy)) // 16)
    for i in range(n):
        ev(2, 0, dx // n); ev(2, 1, dy // n); syn(); time.sleep(0.004)
else:
    code = {"left": 0x110, "right": 0x111}[cmd]
    act = sys.argv[3]
    if act in ("down", "tap"): ev(1, code, 1); syn()
    if act == "tap": time.sleep(0.12)
    if act in ("up", "tap"): ev(1, code, 0); syn()
os.close(fd)

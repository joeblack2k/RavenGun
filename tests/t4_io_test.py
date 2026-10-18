"""Eigen toets voor tools/rg-t4-io/rg_t4_io.c: een proef-pty als JAMMA-lijn, een proef-bordbeeld als shm, een 32-bits proefproces dat
een eigen geheugen onder de game-adressen legt. Gebruik: python3 -I t4_io_test.py <librg-t4-io.so> <t4_io_unit> <tmpmap>. Geen spel."""
import os, pty, select, struct, subprocess, sys, time, tty

lib, prog, tmp = sys.argv[1:4]
shm = os.path.join(tmp, "JvsState")


def beeld(bits=0, p1=(128, 128), p2=(0, 0)):
    b = bytearray(64)
    struct.pack_into("<I", b, 8, bits)
    b[12], b[16], b[20], b[24] = p1[0], p1[1], p2[0], p2[1]
    with open(shm, "r+b" if os.path.exists(shm) else "wb") as f:   # op zijn plaats, zoals de daemon (gedeeld geheugen)
        f.write(b)


beeld()
meester, slaaf = pty.openpty()
tty.setraw(slaaf)
env = dict(os.environ, RG_T4_JAMMA_DEV=os.ttyname(slaaf), RG_T4_DRAIN_DEV="", RG_T4_SHM=shm, RG_T4_LOG="1")
env["LD_LIBRARY_PATH"] = ""
p = subprocess.Popen([prog, lib, "4"], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
data = b""


def lees(sec):
    global data
    t0 = time.time()
    while time.time() - t0 < sec:
        r, _, _ = select.select([meester], [], [], 0.05)
        if r:
            data += os.read(meester, 4096)


lees(0.6)
beeld(1 << 4)            # START 1 in
lees(0.3)
beeld(0)                 # los
lees(0.3)
beeld(1 << 5 | 1 << 7)   # trekker 1 + herladen 1 (pomp 1)
lees(0.3)
beeld(1 << 5 | 1 << 7 | 1 << 6)   # granaat erbij: pomp 1 blijft ingedrukt, GEEN nieuwe flank
lees(0.3)
beeld(0)
lees(0.3)
beeld(1 << 0, p1=(2, 253), p2=(255, 10))   # TEST; richtpunt 1 linksboven-ish, speler 2 buiten beeld
lees(0.3)
out, err = p.communicate(timeout=10)
frames = [f for f in data.split(b"\r") if f]
verwacht = [
    b"\nS8000000000000000",   # nulmeting
    b"\nS8000001000000000",   # START 1 in
    b"\nS8000002000000000",   # los
    b"\nS8010002000000000",   # trekker 1 in
    b"\nS8010102000000000",   # pomp 1 in
    b"\nS8020102000000000",   # trekker los
    b"\nS8020202000000000",   # pomp los (pas als alle pomp-bits los zijn)
    b"\nS8020202000001000",   # TEST in (byte 7 hoog)
]
fout = []
if frames != verwacht:
    fout.append("frames:\n  %s\nverwacht:\n  %s" % (frames, verwacht))
# richtpunt 1: byte 2 -> 0, byte 253 -> 800 / 600 (binnen het scherm 2..253); speler 2: 255 = buiten beeld -> -1,-1
if b"AIM 0.0 600.0 -1.0 -1.0" not in out:
    fout.append("richtpunt: %r | %s" % (out, err.decode()))
if fout:
    print("MISLUKT\n" + "\n".join(fout))
    sys.exit(1)
print("ALLES GOED")

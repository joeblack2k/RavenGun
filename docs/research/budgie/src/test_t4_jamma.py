"""Eigen toets voor rg_t4_jamma.c: een proef-pty als bord, een 32-bits proefproces dat de bibliotheek laadt.
Gebruik: python3 -I test_t4_jamma.py <librg-t4-jamma.so> <32-bits proefprogramma dat ~4 s slaapt>
Er wordt geen spel gestart."""
import os, pty, select, subprocess, sys, time, tty

lib, prog = sys.argv[1], sys.argv[2]
meester, slaaf = pty.openpty()
tty.setraw(slaaf)
pad = os.ttyname(slaaf)
env = dict(os.environ, LD_PRELOAD=lib, RG_T4_JAMMA_DEV=pad, RG_T4_AUTOSTART="1", RG_T4_LOG="1")
p = subprocess.Popen([prog], env=env)
data = b""
t0 = time.time()
# het spel schrijft ook naar de lijn: de bibliotheek moet dat leeglezen (hier 100 kB; zonder lezer blokkeert dit)
os.set_blocking(meester, False)
geschreven = 0
while time.time() - t0 < 3.5:
    r, _, _ = select.select([meester], [], [], 0.2)
    if r:
        data += os.read(meester, 4096)
    while geschreven < 100000:
        try:
            geschreven += os.write(meester, b"P1ff\n" * 200)
        except BlockingIOError:
            break
p.wait()
if geschreven < 100000:
    print("MISLUKT: de bibliotheek las de lijn niet leeg (%d bytes geschreven)" % geschreven)
    sys.exit(1)
frames = [f for f in data.split(b"\r") if f]
print("frames:", frames)
verwacht = [b"\nS8000000000000000", b"\nS8000001000000000", b"\nS8000002000000000"]
if frames[:3] != verwacht:
    print("MISLUKT: verwacht", verwacht)
    sys.exit(1)
print("ALLES GOED")

#!/usr/bin/env python3
"""b0-momenten.py -- bemonstert een lopend VACUUM.exe op vaste momenten (root nodig).

Doel: de TIJDlijn van ONZE kern naast die van de gesloten kern. Per moment antwoorden op drie
vragen, met alleen de adressen die ertoe doen:
  1. staat de entry-point-TRAMPOLINE er nog (48 B8 <imm64> FF E0), of is hij teruggezet naar de
     ongerepte Capcom-stub (eb 08 ...), of iets anders?
  2. is .text nog nul (niets ontpakt) of gevuld?
  3. staan de 3494 bytes op 0x14120d000 in .capcom1 er zoals in het bestand, of wijken ze af?

Draait als EEN root-proces dat zelf op het spelproces wacht, zodat er niet per moment een sudo
nodig is. Het spelproces wordt herkend aan comm VACUUM.exe plus LuigisMansion in de commandoregel.

Gebruik:  echo deck | sudo -S python3 b0-momenten.py <uit> <tag> [max_seconden]
"""
import os, sys, time, hashlib

BASE = 0x140000000
EP = BASE + 0xF23125
TEXT_VA, TEXT_LEN = BASE + 0x1000, 3496500
CAP_VA, CAP_LEN = BASE + 0x120D000, 3494          # het enige verschil dat geen trampoline is
CAP_FILE_OFF = 0x400 + (0x120D000 - 0xF15000)
BESTAND = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Luigi's Mansion Arcade/exe/x64/VACUUM.exe"
MOMENTEN = [2, 3, 3.5, 4, 5, 6, 8, 10, 12]
BLOK = 0x10000


def spelpid():
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            if open("/proc/%s/comm" % d).read().strip() != "VACUUM.exe":
                continue
            cmd = open("/proc/%s/cmdline" % d, "rb").read().decode("utf-8", "replace")
            if "LuigisMansion" in cmd:
                return int(d)
        except OSError:
            pass
    return None


def threads(pid):
    try:
        return len(os.listdir("/proc/%d/task" % pid))
    except OSError:
        return 0


def main():
    uit, tag = sys.argv[1], sys.argv[2]
    maxs = float(sys.argv[3]) if len(sys.argv) > 3 else 16.0
    os.makedirs(uit, exist_ok=True)

    best = b""
    if os.path.exists(BESTAND):
        f = open(BESTAND, "rb"); f.seek(CAP_FILE_OFF); best = f.read(CAP_LEN)

    pid = None; t0 = None
    regels = []
    einde = "de run stopte"
    t = 0.0
    while t < maxs:
        if pid is None:
            p = spelpid()
            if p:
                pid = p; t0 = t
                regels.append("t=%.1f  spelproces %d verschenen" % (t, p))
        else:
            if not os.path.exists("/proc/%d" % pid):
                regels.append("t=%.1f  PROCES WEG (het spel is gestopt)" % t)
                einde = "proces weg op t=%.1f" % t
                break
            dt = t - t0
            while MOMENTEN and dt >= MOMENTEN[0]:
                m = MOMENTEN.pop(0)
                try:
                    mem = open("/proc/%d/mem" % pid, "rb", buffering=0)
                    mem.seek(EP); ep = mem.read(16)
                    # .text
                    nul = tot = 0; off = 0
                    while off < TEXT_LEN:
                        mem.seek(TEXT_VA + off)
                        b = mem.read(min(BLOK, TEXT_LEN - off))
                        if not b: break
                        nul += b.count(0); tot += len(b); off += len(b)
                    pct = (100.0 * nul / tot) if tot else -1.0
                    # de 3494 bytes
                    mem.seek(CAP_VA); cap = mem.read(CAP_LEN)
                    dv = sum(1 for a, b in zip(best, cap) if a != b) if len(best) == len(cap) else -1
                    mem.close()
                    if ep[:2] == b"\xeb\xfe":
                        oordeel = "EB FE (spinlus van RunTo)"
                    elif ep[:2] == b"\x48\xb8":
                        doel = int.from_bytes(ep[2:10], "little")
                        oordeel = "TRAMPOLINE 48 B8 -> 0x%x" % doel
                    elif ep[:2] == b"\xeb\x08":
                        oordeel = "ongerept eb 08 (Capcom-stub, trampoline teruggezet)"
                    else:
                        oordeel = "onbekend"
                    regels.append("t=%5.1f  entry: %-46s  .text %5.1f%% nul  capcom1@0x14120d000: %s  threads %d"
                                  % (m, oordeel + " [" + ep[:12].hex(" ") + "]", pct,
                                     ("gelijk aan bestand" if dv == 0 else "%d bytes anders" % dv),
                                     threads(pid)))
                except OSError as e:
                    regels.append("t=%5.1f  kon niet lezen: %s" % (m, e))
        time.sleep(0.1); t += 0.1
    if not regels:
        regels.append("GEEN spelproces gezien in %.0f s" % maxs)
    kop = "=== %s ===\n" % tag + "\n".join(regels) + "\n"
    open(os.path.join(uit, "momenten-%s.txt" % tag), "w").write(kop)
    sys.stdout.write(kop); sys.stdout.flush()


if __name__ == "__main__":
    main()


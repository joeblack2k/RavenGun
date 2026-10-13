#!/usr/bin/env python3
"""b0-vijfmin-sampler.py -- bemonstert een 5-minutenproef van Luigi's Mansion (root nodig).

Elke 30 s game-tijd (gemeten vanaf het eerste verschijnen van het spelproces):
  * leeft het proces, en hoeveel threads heeft het;
  * staat de entry-point-trampoline er nog (48 B8 <imm64> FF E0) of is hij teruggezet (eb 08);
  * is .text nog nul of gevuld;
  * hoeveel bestanden van het SPEL staan er open (via /proc/<pid>/fd naar de view se Data-mappen).

Dat laatste is bewust NIET via WINEDEBUG=+file: die trace vertraagt het proces en verandert de
levensduur (5-7 s met -all tegen 25-30 s met +file). Open file descriptors zijn een meting zonder
het spel aan te raken en zonder de timing te beinvloeden.

Stopt op de stopgrens (standaard 300 s game-tijd) of als het proces weg is.

Gebruik: echo deck | sudo -S python3 b0-vijfmin-sampler.py <uit> <tag> [stop_s] [hard_s]
"""
import os, sys, time

BASE = 0x140000000
EP = BASE + 0xF23125
TEXT_VA, TEXT_LEN = BASE + 0x1000, 3496500
BLOK = 0x10000
VIEWS = ("/home/deck/.local/share/ravengun/runtime/views/LuigisMansion/Data",
         "/home/deck/.local/share/ravengun/runtime/views/LuigisMansion/DataGlobal")
CADANS = 30.0
# Namen (basenamen) van drop-in-bestanden die in het PROCES van de game moeten zitten.
# Dat is het bewijs dat een drop-in echt geladen is en niet alleen in de view staat.
DROPIN = [x for x in os.environ.get("RG_B0_DROPIN_MAPS", "").split(":") if x]
# De entry-point-trampoline van de lader zet de originele 20 entry-point-bytes terug. Daarom
# vergelijken we 20 bytes met het BESTAND: dat is het harde bewijs dat de trampoline is afgelopen,
# niet alleen dat de eerste twee bytes toevallig op eb 08 lijken.
BESTAND = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Luigi's Mansion Arcade/exe/x64/VACUUM.exe"
EP_FILE_OFF = 0x400 + (0xF23125 - 0xF15000)
EP_LEN = 20


def spelpid():
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            if open("/proc/%s/comm" % d).read().strip() != "VACUUM.exe":
                continue
            if "LuigisMansion" in open("/proc/%s/cmdline" % d, "rb").read().decode("utf-8", "replace"):
                return int(d)
        except OSError:
            pass
    return None


def dropin_in_proces(pid):
    """Hoeveel van de verwachte drop-in-bestanden staan in de maps van dit proces?"""
    if not DROPIN:
        return 0
    try:
        maps = open("/proc/%d/maps" % pid).read()
    except OSError:
        return 0
    return sum(1 for n in DROPIN if n and n in maps)


def threads(pid):
    try:
        return len(os.listdir("/proc/%d/task" % pid))
    except OSError:
        return 0


def eigen_fds(pid):
    """Aantal open bestanden dat naar de Data-mappen van DEZE game wijst."""
    n = 0; paden = set()
    try:
        for fd in os.listdir("/proc/%d/fd" % pid):
            try:
                t = os.readlink("/proc/%d/fd/%s" % (pid, fd))
            except OSError:
                continue
            if any(t.startswith(v) for v in VIEWS):
                n += 1; paden.add(t)
    except OSError:
        pass
    return n, paden


def main():
    uit, tag = sys.argv[1], sys.argv[2]
    stop = float(sys.argv[3]) if len(sys.argv) > 3 else 300.0
    hard = float(sys.argv[4]) if len(sys.argv) > 4 else 480.0
    os.makedirs(uit, exist_ok=True)
    EP_FILE = b""
    if os.path.exists(BESTAND):
        f = open(BESTAND, "rb"); f.seek(EP_FILE_OFF); EP_FILE = f.read(EP_LEN); f.close()
    regels = ["# %s: stopgrens %.0f s game-tijd, harde grens %.0f s" % (tag, stop, hard),
              "# entry-point-bytes in het BESTAND: %s" % (EP_FILE.hex(" ") if EP_FILE else "<niet gelezen>")]
    pid = None; t0 = None; t = 0.0; volgende = 0.0
    gezien_paden = set()
    uitkomst = "onbekend"
    while t < hard:
        if pid is None:
            p = spelpid()
            if p:
                pid = p; t0 = t; volgende = 0.0
                regels.append("t=   0.0  spelproces %d verschenen" % p)
        else:
            if not os.path.exists("/proc/%d" % pid):
                regels.append("t=%6.1f  PROCES WEG" % (t - t0))
                uitkomst = "proces weg op %.1f s game-tijd" % (t - t0)
                break
            gt = t - t0
            if gt >= volgende:
                try:
                    mem = open("/proc/%d/mem" % pid, "rb", buffering=0)
                    mem.seek(EP); ep = mem.read(EP_LEN)
                    nul = tot = 0; off = 0
                    while off < TEXT_LEN:
                        mem.seek(TEXT_VA + off)
                        b = mem.read(min(BLOK, TEXT_LEN - off))
                        if not b: break
                        nul += b.count(0); tot += len(b); off += len(b)
                    mem.close()
                    pct = (100.0 * nul / tot) if tot else -1.0
                    if ep[:2] == b"\x48\xb8":
                        ep_txt = "TRAMPOLINE -> 0x%x" % int.from_bytes(ep[2:10], "little")
                    elif ep[:2] == b"\xeb\xfe":
                        ep_txt = "EB FE spinlus"
                    elif ep == EP_FILE:
                        ep_txt = "TERUGGEZET, 20 bytes identiek aan bestand"
                    elif ep[:2] == b"\xeb\x08":
                        ep_txt = "eb 08 maar %d/20 bytes wijkt af van bestand" % sum(
                            1 for x, y in zip(ep, EP_FILE) if x != y)
                    else:
                        ep_txt = "onbekend " + ep[:8].hex(" ")
                    nfd, paden = eigen_fds(pid)
                    gezien_paden |= paden
                    regels.append("t=%6.1f  leeft ja  threads %2d  entry %-34s  .text %5.1f%% nul  "
                                  "eigen bestanden open %2d  drop-in in proces %d/%d"
                                  % (gt, threads(pid), ep_txt, pct, nfd,
                                     dropin_in_proces(pid), len(DROPIN)))
                except OSError as e:
                    regels.append("t=%6.1f  kon niet lezen: %s" % (gt, e))
                volgende += CADANS
            if gt >= stop:
                uitkomst = "STOPGRENS gehaald op %.1f s game-tijd, proces leefde nog" % gt
                regels.append("t=%6.1f  STOPGRENS %.0f s gehaald" % (gt, stop))
                break
        time.sleep(0.25); t += 0.25
    else:
        uitkomst = "harde grens %.0f s bereikt" % hard
    regels.append("UITKOMST: %s" % uitkomst)
    regels.append("TOTAAL eigen databestanden gezien via open fds: %d" % len(gezien_paden))
    for p in sorted(gezien_paden)[:15]:
        regels.append("   " + p)
    kop = "\n".join(regels) + "\n"
    open(os.path.join(uit, "vijfmin-%s.txt" % tag), "w").write(kop)
    sys.stdout.write(kop); sys.stdout.flush()


if __name__ == "__main__":
    main()


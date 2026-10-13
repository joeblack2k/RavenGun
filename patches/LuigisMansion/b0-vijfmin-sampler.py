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

# De 17 am-functies in het spel (patches/LuigisMansion/dongle-haken.ini, AM-LAAG.md). Van elk
# willen we de ORIGINELE 5 prologue-bytes, gelezen NADAT het spel zich uitgepakt heeft. Daarmee
# kan elke regel in de adreslijst een verwachte waarde krijgen en weigert de hook een verkeerde
# spelversie in plaats van te vertrouwen op "het lijkt een prologue".
AM_ADRESSEN = [
    0x1402b3b70, 0x1402b3d30, 0x1402b3ff0, 0x1402b40b0, 0x1402b4170, 0x1402b4270,
    0x1402b4320, 0x1402b4740, 0x1402b50f0, 0x1402b5180, 0x1402b5200, 0x1402b55a0,
    0x1402b56f0, 0x1402b5860, 0x1402b5cb0, 0x1402b6050, 0x1402b6160,
]
# De bemonstering is DICHT in de eerste seconden en daarna 30 s: een spel dat op 6 s sterft
# wordt anders helemaal nooit bemonsterd -- dat is precies misgegaan in de eerste drop-in-runs,
# waar alleen t=0,2 en daarna PROCES WEG in het verslag stond.
DICHT_TOT = float(os.environ.get("RG_B0_DICHT_TOT", "10"))
DICHT_STAP = float(os.environ.get("RG_B0_DICHT_STAP", "0.5"))


def cadans(gt):
    return DICHT_STAP if gt < DICHT_TOT else CADANS
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
    max_dropin = 0
    am_laatst = {}
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
                    am_nu = {}
                    for a in AM_ADRESSEN:
                        mem.seek(a)
                        am_nu[a] = mem.read(5)
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
                    am_laatst = am_nu
                    nfd, paden = eigen_fds(pid)
                    dp_n = dropin_in_proces(pid)
                    if dp_n > max_dropin:
                        max_dropin = dp_n
                    gezien_paden |= paden
                    regels.append("t=%6.1f  leeft ja  threads %2d  entry %-34s  .text %5.1f%% nul "
                                  "(%d van %d B gelezen)  eigen bestanden %2d  drop-in %d/%d"
                                  % (gt, threads(pid), ep_txt, pct, tot, TEXT_LEN, nfd,
                                     dropin_in_proces(pid), len(DROPIN)))
                    regels.append("        prologues: " + " ".join(
                        "%x=%s" % (a & 0xFFFFFF, am_nu[a].hex()) for a in AM_ADRESSEN))
                except OSError as e:
                    regels.append("t=%6.1f  kon niet lezen: %s" % (gt, e))
                volgende += cadans(gt)
            if gt >= stop:
                uitkomst = "STOPGRENS gehaald op %.1f s game-tijd, proces leefde nog" % gt
                regels.append("t=%6.1f  STOPGRENS %.0f s gehaald" % (gt, stop))
                break
        time.sleep(0.25); t += 0.25
    else:
        uitkomst = "harde grens %.0f s bereikt" % hard
    regels.append("MAX drop-in in proces %d/%d" % (max_dropin, len(DROPIN)))
    regels.append("")
    regels.append("ORIGINELE 5 PROLOGUE-BYTES OP DE 17 AM-ADRESSEN (laatste meting voor het einde):")
    regels.append("(een 0x1402b4270 die cc cc cc cc blijft, is int3-opvulling: dat adres klopt dan niet in deze build)")
    for a in AM_ADRESSEN:
        b = am_laatst.get(a, b"")
        regels.append("  0x%09x  %s%s" % (a, b.hex(" ") if b else "<niet gelezen>",
                                          "   <- nog allemaal nul: op dit moment niet ontpakt" if b and not any(b) else ""))
    regels.append("UITKOMST: %s" % uitkomst)
    regels.append("TOTAAL eigen databestanden gezien via open fds: %d" % len(gezien_paden))
    for p in sorted(gezien_paden)[:15]:
        regels.append("   " + p)
    kop = "\n".join(regels) + "\n"
    open(os.path.join(uit, "vijfmin-%s.txt" % tag), "w").write(kop)
    sys.stdout.write(kop); sys.stdout.flush()


if __name__ == "__main__":
    main()


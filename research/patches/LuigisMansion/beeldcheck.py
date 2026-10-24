#!/usr/bin/env python3
"""beeldcheck.py -- is dit VACUUM.exe-proces een GESLAAGDE run, of een vastgelopen rest?

Waarom dit bestaat: een dump van /proc/<pid>/mem is alleen iets waard als het proces
werkelijk ontpakt is en tot het spel gekomen is. Een proces dat in de parkeer-/spinlus
van de lader blijft hangen (OpenParrotLoader schrijft `EB FE` op het entrypoint en haalt
dat alleen weg als hij het terugziet) heeft een leeg beeld: `.text` staat vol nullen en
alleen de versleutelde `.capcom1` van de Capcom-pakker is aanwezig. Zo'n dump levert geen
ontpakte code op, en de verkeerde conclusie ("de code staat ergens anders") ligt dan voor
de hand.

Gebruik:  sudo python3 beeldcheck.py <pid>
          sudo python3 beeldcheck.py --zoek          # alle VACUUM.exe-processen

Alleen lezen. Leest /proc, en /proc/<pid>/mem voor de sectiescan (dat laatste vraagt root:
ptrace_scope=1 staat geen vreemde lezer toe).
"""
import os, sys, time, glob, re

BASE = 0x140000000
SECTIONEN = [
    (".text",    0x00001000, 3496500),
    (".rdata",   0x00357000, 1329995),
    (".data",    0x0049c000, 9083712),
    (".pdata",   0x00d46000, 172140),
    (".capcom0", 0x00d71000, 1719331),
    (".capcom1", 0x00f15000, 3434464),
]
ENTRY_RVA = 0xf23125
BLOK = 0x10000


def lees_stat(pid):
    with open("/proc/%d/stat" % pid) as f:
        s = f.read()
    # comm kan spaties bevatten: knip af na de laatste ')'
    i = s.rfind(")")
    veld = s[i + 2:].split()
    return {"state": veld[0], "utime": int(veld[11]), "stime": int(veld[12]),
            "threads": int(veld[17]), "starttime": int(veld[19])}


def status(pid):
    d = {}
    try:
        for regel in open("/proc/%d/status" % pid):
            k, _, v = regel.partition(":")
            d[k.strip()] = v.strip()
    except OSError:
        pass
    return d


def cmdline(pid):
    try:
        return open("/proc/%d/cmdline" % pid, "rb").read().replace(b"\0", b" ").decode("utf-8", "replace").strip()
    except OSError:
        return "?"


def maps_regels(pid):
    try:
        return open("/proc/%d/maps" % pid).read().splitlines()
    except OSError:
        return []


def samenvatting(pid):
    print("=" * 78)
    print("pid %d   %s" % (pid, cmdline(pid)))
    st = status(pid)
    for k in ("Name", "State", "PPid", "Threads", "VmSize", "VmRSS"):
        if k in st:
            print("  %-8s %s" % (k, st[k]))
    a = lees_stat(pid)
    time.sleep(2.0)
    b = lees_stat(pid)
    ticks = os.sysconf("SC_CLK_TCK")
    dt = b["utime"] + b["stime"] - a["utime"] - a["stime"]
    print("  CPU      %.0f%% van een kern (%.2f s CPU in 2 s)" % (100.0 * dt / (2.0 * ticks), dt / ticks))
    try:
        uptime = float(open("/proc/uptime").read().split()[0])
        print("  looptijd %.0f s" % (uptime - b["starttime"] / ticks))
    except OSError:
        pass

    # Beeldgebied in de maps: file-backed of anoniem?
    print("  maps in het beeldbereik 0x%x..0x%x:" % (BASE, BASE + 0x125e000))
    for r in maps_regels(pid):
        m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S+)\s+\S+\s+\S+\s+\S+\s*(.*)", r)
        if not m:
            continue
        lo, hi = int(m.group(1), 16), int(m.group(2), 16)
        if hi <= BASE or lo >= BASE + 0x125e000:
            continue
        print("    0x%09x-0x%09x %s %s" % (lo, hi, m.group(3), m.group(4) or "(anoniem)"))

    # Sectiescan
    print("  sectiescan:")
    try:
        mem = open("/proc/%d/mem" % pid, "rb", buffering=0)
    except OSError as e:
        print("    /proc/%d/mem niet te openen (%s) -- draai met sudo." % (pid, e.strerror))
        return
    for naam, rva, grootte in SECTIONEN:
        va = BASE + rva
        off = 0; nul = 0; tot = 0; eerste = b""
        while off < grootte:
            n = min(BLOK, grootte - off)
            mem.seek(va + off)
            try:
                buf = mem.read(n)
            except OSError:
                break
            if not buf:
                break
            if off == 0:
                eerste = buf[:16]
            nul += buf.count(0); tot += len(buf); off += len(buf)
        if tot == 0:
            print("    %-8s 0x%09x  NIET TE LEZEN" % (naam, va)); continue
        print("    %-8s 0x%09x  %9d B  %5.1f%% nul   eerste: %s"
              % (naam, va, grootte, 100.0 * nul / tot, eerste.hex(" ")))
    mem.seek(BASE + ENTRY_RVA)
    ep = mem.read(32)
    print("    entrypoint 0x%x in het PROCES : %s" % (BASE + ENTRY_RVA, ep[:16].hex(" ")))
    # Vergelijk met de bytes in het BESTAND. Dat is de harde vraag: heeft iemand het
    # entrypoint aangeraakt? (De pakker zelf mag dat ook doen -- daarom is dit INFORMATIE
    # en geen oordeel.)
    bestand = None
    for pad in ("/home/deck/mnt/spidernas_emulation/Teknoparrot/Luigi's Mansion Arcade/exe/x64/VACUUM.exe",):
        try:
            with open(pad, "rb") as f:
                f.seek(0x400 + (ENTRY_RVA - 0xf15000))   # .capcom1 begint op bestandsoffset 0x400
                bestand = f.read(16)
            break
        except OSError:
            pass
    if bestand:
        print("    entrypoint in het BESTAND : %s" % bestand.hex(" "))
        if bestand[:2] == b"\xeb\x08":
            print("      (EB 08 = de ongerepte eerste sprong van de Capcom-stub, zoals in pakker-meting.md)")
        if ep[:len(bestand)] != bestand:
            print("      ^^ VERSCHIL: het entrypoint is in het proces overschreven.")
            if ep[:2] == b"\xeb\xfe":
                print("         EB FE = 'jmp $'. Dat is de parkeer-/spinlus van RunTo() in")
                print("         OpenParrotLoader64.exe. LET OP: dit is een vermoeden, geen bewijs --")
                print("         een gemeten geval op deze game had andere bytes (zie hieronder).")
            elif ep[:2] == b"\x48\xb8":
                print("         48 B8 <imm64> = 'movabs rax, <doel>'. Dat is de vorm van de")
                print("         entryJump die de lader schrijft in InstallEntryPointInitializer()")
                print("         (main.cpp), maar een pakker kan dezelfde vorm gebruiken. Meet het")
                print("         doeladres na: ligt het in een anoniem r-x-gebied van DEZE maps, dan")
                print("         is het de trampoline van de lader.")
                doel = int.from_bytes(ep[2:10], "little")
                print("         doeladres = 0x%x" % doel)
                for r in maps_regels(pid):
                    m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S+)\s+\S+\s+\S+\s+\S+\s*(.*)", r)
                    if m and int(m.group(1), 16) <= doel < int(m.group(2), 16):
                        print("         dat ligt in 0x%s-0x%s %s %s"
                              % (m.group(1), m.group(2), m.group(3), m.group(4) or "(anoniem)"))
            else:
                print("         onbekende vorm; vergelijk met de bytes in het bestand hierboven.")
        else:
            print("      ^^ GELIJK aan het bestand: dit entrypoint is ongemoeid.")
    print()
    print("  OORDEEL:")
    if b["threads"] <= 2 and dt > 1.5 * ticks:
        print("    VASTGELOPEN. Een enkele thread die 100%% draait is geen spel dat speelt;")
        print("    het beeld hierboven vertelt waar hij staat. Opruimen en opnieuw starten.")
    elif tot and 100.0 * nul / tot > 99.0:
        print("    NOG NIET ONTPAKT (.text vrijwel helemaal nul). Niet dumpen.")
    else:
        print("    Lijkt een levend, ontpakt proces. Dumpen heeft zin.")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--zoek":
        pids = []
        for d in glob.glob("/proc/[0-9]*"):
            try:
                if open(os.path.join(d, "comm")).read().strip().lower().startswith("vacuum"):
                    pids.append(int(os.path.basename(d)))
            except OSError:
                pass
        if not pids:
            print("geen VACUUM.exe gevonden")
        for p in pids:
            samenvatting(p)
    elif len(sys.argv) > 1:
        samenvatting(int(sys.argv[1]))
    else:
        print(__doc__)


#!/usr/bin/env python3
"""b0-geheugen-dump.py -- leest het beeld van een lopend VACUUM.exe (root nodig).

Doel (instrument A): op een VAST vroeg moment het spelbeeld onder twee kernen vastleggen en
vergelijken. Drie vragen, in deze orde:
  a. heeft de kern iets in het spel VERANDERD dat de andere niet verandert?
  b. is het spel onder de ene kern verder ONTPAKT dan onder de andere?
  c. zijn de anonieme uitvoerbare gebieden anders (groottes/aantallen)?

Wat het wegschrijft in <uit>/:
  image-<tag>.bin     het hele nominale beeldbereik (ImageBase .. ImageBase+SizeOfImage)
  maps-<tag>.txt      de /proc/<pid>/maps van dit moment
  samenvatting-<tag>.txt
  levenscheck-<tag>.txt   de uitvoer van beeldcheck.py op hetzelfde moment

Gebruik:  echo deck | sudo -S python3 b0-geheugen-dump.py <pid> <uit> <tag>
"""
import hashlib, os, re, subprocess, sys

BASE_DEFAULT = 0x140000000
SPAN_DEFAULT = 0x125E000            # nominale beeldgrootte van VACUUM.exe
# secties uit pakker-meting.md: naam, RVA, virtuele grootte, bytes in het bestand
SECTIES = [
    (".text",    0x00001000, 3496500, 0),
    (".rdata",   0x00357000, 1329995, 0),
    (".data",    0x0049c000, 9083712, 0),
    (".pdata",   0x00d46000, 172140,  0),
    (".capcom0", 0x00d71000, 1719331, 0),
    (".capcom1", 0x00f15000, 3434464, 3434496),   # enige sectie met bytes in het bestand
]
ENTRY_RVA = 0xF23125
BLOK = 0x10000
BESTAND = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Luigi's Mansion Arcade/exe/x64/VACUUM.exe"


def lees_mem(f, va, n):
    try:
        f.seek(va)
        return f.read(n)
    except OSError:
        return b""


def main():
    pid = int(sys.argv[1]); uit = sys.argv[2]; tag = sys.argv[3]
    os.makedirs(uit, exist_ok=True)
    maps = open("/proc/%d/maps" % pid).read()
    open(os.path.join(uit, "maps-%s.txt" % tag), "w").write(maps)

    # de image-regio's en de anonieme r-x-regio's uit de maps halen
    image_lo = None
    anon_rx = []
    for r in maps.splitlines():
        m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S+)\s+\S+\s+\S+\s+\S+\s*(.*)", r)
        if not m:
            continue
        lo, hi, perm, pad = int(m.group(1), 16), int(m.group(2), 16), m.group(3), m.group(4)
        if "VACUUM.exe" in pad:
            image_lo = lo if image_lo is None else min(image_lo, lo)
        elif not pad and "x" in perm:
            anon_rx.append((lo, hi))

    base = image_lo or BASE_DEFAULT
    span = SPAN_DEFAULT
    mem = open("/proc/%d/mem" % pid, "rb", buffering=0)

    uitlijnen = []
    def Z(s):
        uitlijnen.append(s); print(s)

    Z("pid            %d" % pid)
    Z("ImageBase     0x%x   (uit de maps)" % base)
    Z("beeldbereik   0x%x .. 0x%x  (%d B)" % (base, base + span, span))
    Z("")
    Z("%-9s %-12s %10s %8s" % ("sectie", "VA", "grootte", "%nul"))
    per_sectie = {}
    for naam, rva, grootte, raw in SECTIES:
        va = base + rva
        off = 0; nul = 0; tot = 0
        while off < grootte:
            b = lees_mem(mem, va + off, min(BLOK, grootte - off))
            if not b:
                break
            nul += b.count(0); tot += len(b); off += len(b)
        pct = (100.0 * nul / tot) if tot else -1.0
        per_sectie[naam] = (va, grootte, tot, pct)
        Z("%-9s 0x%09x %10d %7.1f%%" % (naam, va, grootte, pct))

    Z("")
    ep = lees_mem(mem, base + ENTRY_RVA, 32)
    Z("entrypoint    0x%x : %s" % (base + ENTRY_RVA, ep[:16].hex(" ")))
    if ep[:2] == b"\xeb\xfe":
        Z("  -> EB FE: spinlus (parkeerroute van de lader); nog niets ontpakt")
    elif ep[:2] == b"\x48\xb8":
        Z("  -> 48 B8 <imm64>: movabs, dus het entrypoint is overschreven (lader-trampoline of pakker)")
    elif ep[:2] == b"\xeb\x08":
        Z("  -> EB 08: de ongerepte eerste sprong van de Capcom-stub")

    Z("")
    Z("anonieme uitvoerbare gebieden: %d" % len(anon_rx))
    for lo, hi in anon_rx:
        Z("  0x%09x-0x%09x  %9d B" % (lo, hi, hi - lo))

    # het hele beeld wegschrijven
    data = lees_mem(mem, base, span)
    pad = os.path.join(uit, "image-%s.bin" % tag)
    open(pad, "wb").write(data)
    Z("")
    Z("weggeschreven  %s  (%d B, sha256 %s)" % (pad, len(data), hashlib.sha256(data).hexdigest()[:16]))

    # geheugen tegen BESTAND, voor de enige twee gebieden die in het bestand staan
    if os.path.exists(BESTAND):
        f = open(BESTAND, "rb")
        kop_f = f.read(0x400)
        f.seek(0x400)
        cap_f = f.read(0x346800)
        kop_m = lees_mem(mem, base, 0x400)
        cap_m = lees_mem(mem, base + 0xF15000, 0x346800)
        Z("")
        Z("geheugen tegen BESTAND:")
        Z("  headers  (0x400 B):  %s" % ("GELIJK" if kop_m == kop_f else "VERSCHIL: %d van %d bytes anders"
                                          % (sum(1 for a, b in zip(kop_f, kop_m) if a != b), len(kop_f))))
        if len(cap_f) == len(cap_m):
            d = sum(1 for a, b in zip(cap_f, cap_m) if a != b)
            Z("  .capcom1 (3434496 B): %s" % ("GELIJK aan het bestand (stub onaangeroerd)"
                                              if d == 0 else "%d bytes anders (%.1f%%) -- in het geheugen ontpakt of gepatcht"
                                              % (d, 100.0 * d / len(cap_f))))
        else:
            Z("  .capcom1: kon niet vergelijken (%d vs %d B)" % (len(cap_f), len(cap_m)))
    else:
        Z("  (bestand niet leesbaar: %s)" % BESTAND)

    open(os.path.join(uit, "samenvatting-%s.txt" % tag), "w").write("\n".join(uitlijnen) + "\n")


if __name__ == "__main__":
    main()


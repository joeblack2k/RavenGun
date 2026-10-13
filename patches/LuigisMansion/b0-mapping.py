#!/usr/bin/env python3
"""b0-mapping.py -- WELKE mapping dekt het am-adres, en wat staat erin?

Vraagt: dekt 0x1402b3b70 dezelfde soort mapping als het entry point in .capcom1 (waar mijn lezer
het WEL zag veranderen)? Is het file-backed of anoniem, welk bestand, welke offset, welke rechten?
En als het file-backed is: staan dezelfde bytes ook in het BESTAND op die offset?

Gebruik: echo deck | sudo -S python3 b0-mapping.py <pid>
"""
import os, re, sys

ADRESSEN = {
    "am-adres (0x1402b3b70)": 0x1402b3b70,
    "tweede am-adres (0x1402b4740)": 0x1402b4740,
    "entry point .capcom1 (0x140f23125)": 0x140f23125,
    "begin .text (0x140001000)": 0x140001000,
    "eerste pagina (0x140000000)": 0x140000000,
}
BESTAND = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Luigi's Mansion Arcade/exe/x64/VACUUM.exe"
# sectie -> bestandsoffset van de ruwe data (alleen .capcom1 heeft bytes in het bestand)
RAW = {0xf15000: 0x400}
BASE = 0x140000000


def main():
    pid = int(sys.argv[1])
    maps = open("/proc/%d/maps" % pid).read().splitlines()
    print("pid %d -- %d maps-regels" % (pid, len(maps)))
    print()
    print("=== alle regels die het beeldbereik of het bestand raken ===")
    for r in maps:
        m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S+)\s+(\S+)\s+\S+\s+\S+\s*(.*)", r)
        if not m:
            continue
        lo, hi = int(m.group(1), 16), int(m.group(2), 16)
        if "VACUUM" in m.group(5) or (0x13fff0000 <= lo <= 0x141260000) or (0x140000000 <= lo < 0x14125e000):
            print("  %s  off=%-10s %s" % (r.split()[0] + " " + m.group(3), m.group(4), m.group(5) or "(anoniem)"))
    print()
    mem = open("/proc/%d/mem" % pid, "rb", buffering=0)
    f = open(BESTAND, "rb") if os.path.exists(BESTAND) else None
    print("=== per adres: dekkende mapping, mijn bytes, en het BESTAND ===")
    for naam, va in ADRESSEN.items():
        dek = None
        for r in maps:
            m = re.match(r"([0-9a-f]+)-([0-9a-f]+) (\S+)\s+(\S+)\s+\S+\s+\S+\s*(.*)", r)
            if not m:
                continue
            lo, hi = int(m.group(1), 16), int(m.group(2), 16)
            if lo <= va < hi:
                dek = (m.group(3), m.group(4), m.group(5) or "(anoniem)", lo, hi)
                break
        try:
            mem.seek(va); bytes_mem = mem.read(8)
        except OSError as e:
            bytes_mem = b"<fout %s>" % str(e).encode()
        rva = va - BASE
        bestand = b""
        if f and rva in RAW:
            f.seek(RAW[rva]); bestand = f.read(8)
        elif f and rva < 0x400:
            f.seek(rva); bestand = f.read(8)
        print("  %s" % naam)
        if dek:
            print("    dekkende mapping : %s-%s rechten %s  %s" % (hex(dek[3]), hex(dek[4]), dek[0], dek[2]))
            if dek[2] != "(anoniem)":
                print("    file-offset      : %s" % dek[1])
        else:
            print("    dekkende mapping : GEEN (niet gemapt)")
        print("    mijn bytes       : %s" % bytes_mem.hex(" "))
        print("    bestand          : %s" % (bestand.hex(" ") if bestand else "(geen ruwe data voor dit bereik)"))
        if bytes_mem and bestand:
            print("    mem == bestand   : %s" % (bytes_mem == bestand))
        print()
    mem.close()
    if f:
        f.close()


if __name__ == "__main__":
    main()


#!/usr/bin/env python3
# find-hasp-adressen.py  (STATISCH; ALLEEN LEZEN; start niets)
#
# Zoekt de HASP-HL-functies in de gestripte `game` van een Raw Thrills/Play Mechanix-titel aan de hand van een ANKER: een
# ander programma uit dezelfde spelmap met dezelfde statisch gelinkte HASP-cliënt en mét `hasp_`-symbolen (bij Aliens
# Armageddon: `patch`, 20 hasp_-symbolen in de dynsym). Per ankerfunctie worden de bytes van de functie gezocht in het
# uitvoerbare segment van `game`; velden die tussen builds verschuiven (rel32 van call/jmp/jcc en absolute adressen in het
# beeld) zijn jokers. Alleen een UNIEKE treffer over de hele functie telt; meerdere of nul treffers worden gemeld, niet geraden.
#
# Gebruik: find-hasp-adressen.py ANKER GAME [GAME-NAAM]   -> drukt een hasp-haken.ini af (zie patches/Terminator/hasp-haken.ini)
import re
import struct
import subprocess
import sys


def load(p):
    d = open(p, "rb").read()
    entry, phoff = struct.unpack_from("<II", d, 24)
    phesz, phnum = struct.unpack_from("<HH", d, 42)
    segs = []
    for i in range(phnum):
        t, o, v, pa, fsz, msz, fl, al = struct.unpack_from("<IIIIIIII", d, phoff + i * phesz)
        if t == 1:
            segs.append((v, o, fsz, fl))
    return d, segs, entry


def v2o(segs, v):
    for sv, so, sz, fl in segs:
        if sv <= v < sv + sz:
            return v - sv + so
    return None


def pattern(code, lo, hi):
    """Regex over de functiebytes; rel32 na E8/E9/0F 8x en 4-byte waarden in [lo, hi) worden jokers."""
    joker = [False] * len(code)
    i = 0
    while i < len(code):
        b = code[i]
        if b in (0xE8, 0xE9) and i + 5 <= len(code):
            for k in range(1, 5):
                joker[i + k] = True
            i += 5
            continue
        if b == 0x0F and i + 6 <= len(code) and 0x80 <= code[i + 1] <= 0x8F:
            for k in range(2, 6):
                joker[i + k] = True
            i += 6
            continue
        i += 1
    for i in range(len(code) - 3):
        v = struct.unpack_from("<I", code, i)[0]
        if lo <= v < hi:
            for k in range(4):
                joker[i + k] = True
    out = b""
    for c, j in zip(code, joker):
        out += b"." if j else re.escape(bytes([c]))
    return out, sum(1 for j in joker if not j)


def main():
    anker, game = sys.argv[1], sys.argv[2]
    naam = sys.argv[3] if len(sys.argv) > 3 else "?"
    ad, asegs, _ = load(anker)
    gd, gsegs, gentry = load(game)
    lo = min(s[0] for s in asegs)
    hi = max(s[0] + s[2] for s in asegs) + 0x1000000
    exe = [(v, o, sz) for v, o, sz, fl in gsegs if fl & 1]
    syms = []
    out = subprocess.run(["readelf", "--dyn-syms", "-W", anker], capture_output=True, text=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 8 and f[3] == "FUNC" and f[7].startswith("hasp_") and int(f[2]) > 0:
            syms.append((f[7], int(f[1], 16), int(f[2])))
    gevonden = {}
    print("# gegenereerd door find-hasp-adressen.py; anker %s" % anker, file=sys.stderr)
    for name, va, size in sorted(syms):
        o = v2o(asegs, va)
        code = ad[o:o + size]
        pat, vast = pattern(code, lo, hi)
        hits = []
        for sv, so, sz in exe:
            for m in re.finditer(pat, gd[so:so + sz], re.S):
                hits.append(sv + m.start())
        status = "uniek" if len(hits) == 1 else ("geen" if not hits else "%d treffers" % len(hits))
        print("%-26s anker 0x%08x %4d B (%3d vast) -> %s %s" % (name, va, size, vast, status,
                                                               " ".join("0x%08x" % h for h in hits[:4])), file=sys.stderr)
        if len(hits) == 1:
            gevonden[name] = hits[0]
    print("game    = %s" % naam)
    print("arch    = i386")
    print("basis   = absoluut")
    print("grootte = %d" % len(gd))
    print("ingang  = 0x%x" % gentry)
    for name in sorted(gevonden):
        print("%-20s = 0x%08x" % (name, gevonden[name]))


main()

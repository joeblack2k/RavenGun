#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Statische scan van een Sega Ring-spel (RingWide/RingEdge, PE32): wat vraagt de ingebakken AM Library van de omgeving?

De Sega-spellen die op de gesloten TeknoParrot-kern draaien (LGI, LGI3D, LGS, GG, OG, SDR, Transformers) hebben de AM Library
(amMaster, amDongle, amInstall, amStorage, amNetwork, libpcp, ...) statisch in de exe. Op een echte kast antwoordt de AM-daemon
op 127.0.0.1:40100..40115; de gesloten kern vervangt dat. Dit hulpmiddel haalt uit de exe zelf (alleen lezen, geen uitvoering) wat
een eigen vervanging moet kennen: de versies van de AM-modules, de poorten, de seriele poort/JVS-naam, en per functienaam het
beginadres (zodat een hook of een meting erop kan wijzen).

Gebruik:  python3 -I tools/dev/segajvs-scan.py <exe> [--json] [--names REGEX]

Het zoekt geen verborgen gegevens: elke bevinding is een tekenreeks of een constante in de exe, met het adres erbij. Een
functiebeginadres is een schatting (het eerste punt na een reeks int3-opvulling), geen disassemblage.
"""
import json
import re
import struct
import sys

PORT_LOW, PORT_HIGH = 40100, 40115          # amMaster 40100, amInstall 40102/3, amDongle 40106/7, amStorage 40114/5

AM_VERSION_RX = re.compile(rb"(am[A-Z][A-Za-z]+|libpcp|SEGA AM Library) Ver\.(\d+\.\d+) Build:([^\r\n\x00]{8,40})")
NAME_RX = re.compile(rb"^(am|pcp)[A-Z][A-Za-z0-9]{3,}$")


class PE:
    """Net genoeg PE32/PE32+ om secties, importen en adresomrekening te geven."""

    def __init__(self, data):
        d = self.d = data
        if d[:2] != b"MZ":
            raise ValueError("geen MZ-kop")
        e = struct.unpack_from("<I", d, 0x3C)[0]
        if d[e:e + 4] != b"PE\0\0":
            raise ValueError("geen PE-kop")
        nsec, = struct.unpack_from("<H", d, e + 6)
        optsz, = struct.unpack_from("<H", d, e + 20)
        o = e + 24
        self.pe64 = struct.unpack_from("<H", d, o)[0] == 0x20B
        self.base = struct.unpack_from("<Q", d, o + 24)[0] if self.pe64 else struct.unpack_from("<I", d, o + 28)[0]
        self.secs = []
        so = o + optsz
        for i in range(nsec):
            name = d[so + 40 * i:so + 40 * i + 8].rstrip(b"\0").decode("latin1")
            vsize, va, rsize, roff = struct.unpack_from("<IIII", d, so + 40 * i + 8)
            flags, = struct.unpack_from("<I", d, so + 40 * i + 36)
            self.secs.append((name, va, vsize, roff, rsize, flags))

    def off2va(self, off):
        for _n, va, _vs, ro, rs, _f in self.secs:
            if ro <= off < ro + rs:
                return self.base + va + off - ro
        return None

    def va2off(self, va):
        rva = va - self.base
        for _n, sva, vs, ro, rs, _f in self.secs:
            if sva <= rva < sva + max(vs, rs):
                return ro + rva - sva
        return None

    def code_ranges(self):
        """(bestandsbegin, bestandseinde) van elke sectie met het code-bit (0x20)."""
        return [(ro, ro + rs) for _n, _va, _vs, ro, rs, f in self.secs if f & 0x20]


def find_ports(pe):
    """Constanten 40100..40115 die als directe waarde in code geladen worden: (va, poort, vorm).

    Herkende vormen: `mov r32, imm32` (B8+r), `mov r16, imm16` (66 B8+r), `mov [m], imm32` (C7 /0) en `mov [m], imm16` (66 C7 /0).
    Het is geen disassemblage: een toevallige bytereeks in data die als code-sectie telt kan meetellen. De adressen zijn daarom
    een aanwijzing waar je met objdump kijkt, geen bewijs.
    """
    d = pe.d
    out = []
    for lo, hi in pe.code_ranges():
        for off in range(lo, hi - 4):
            b = d[off]
            if 0xB8 <= b <= 0xBF:                       # mov r32, imm32
                v = struct.unpack_from("<I", d, off + 1)[0]
                if PORT_LOW <= v <= PORT_HIGH:
                    out.append((pe.off2va(off), v, "mov r32,imm32"))
                elif off > lo and d[off - 1] == 0x66:   # mov r16, imm16
                    v16 = struct.unpack_from("<H", d, off + 1)[0]
                    if PORT_LOW <= v16 <= PORT_HIGH:
                        out.append((pe.off2va(off - 1), v16, "mov r16,imm16"))
            elif b == 0xC7 and (d[off + 1] >> 3) & 7 == 0:   # mov [m], imm: modrm (+sib, disp), dan imm32 (imm16 na een 66-voorvoegsel)
                imm_at = off + 1 + _modrm_len(d, off + 1)
                if imm_at + 4 > hi:
                    continue
                if off > lo and d[off - 1] == 0x66:
                    v = struct.unpack_from("<H", d, imm_at)[0]
                    if PORT_LOW <= v <= PORT_HIGH:
                        out.append((pe.off2va(off - 1), v, "mov [m],imm16"))
                else:
                    v = struct.unpack_from("<I", d, imm_at)[0]
                    if PORT_LOW <= v <= PORT_HIGH:
                        out.append((pe.off2va(off), v, "mov [m],imm32"))
    seen, uniq = set(), []
    for va, p, k in out:
        if (va, p) not in seen:
            seen.add((va, p))
            uniq.append((va, p, k))
    return uniq


def _modrm_len(d, off):
    """Lengte van modrm (+ sib + disp) op `off` voor 32-bits adressering."""
    modrm = d[off]
    mod, rm = modrm >> 6, modrm & 7
    n = 1
    if mod != 3 and rm == 4:
        n += 1
    if mod == 1:
        n += 1
    elif mod == 2 or (mod == 0 and rm == 5):
        n += 4
    return n


def func_start(pe, off):
    """Eerste adres na de int3-opvulling (minstens twee bytes) die vóór `off` ligt: schatting van het begin van de functie."""
    d = pe.d
    i = off
    lo = min((a for a, _b in pe.code_ranges()), default=0)
    while i > lo + 2:
        if d[i - 1] == 0xCC and d[i - 2] == 0xCC and d[i] != 0xCC:   # twee int3: één CC kan ook een operandbyte zijn
            return pe.off2va(i)
        i -= 1
    return pe.off2va(lo)


def string_refs(pe, va):
    """Offsets in code die `va` als directe waarde gebruiken met push (68) of mov (B8+r)."""
    needle = struct.pack("<I", va)
    d = pe.d
    out = []
    for lo, hi in pe.code_ranges():
        start = lo
        while True:
            i = d.find(needle, start, hi)
            if i < 0:
                break
            if i > lo and (d[i - 1] == 0x68 or 0xB8 <= d[i - 1] <= 0xBF):
                out.append(i - 1)
            start = i + 1
    return out


def named_functions(pe, name_rx=NAME_RX):
    """{functienaam: [beginadressen]} voor elke tekenreeks die als naam in een foutmelding voorkomt (am*/pcp*)."""
    res = {}
    for m in re.finditer(rb"[\x20-\x7e]{4,}", pe.d):
        if not name_rx.match(m.group()):
            continue
        va = pe.off2va(m.start())
        if va is None:
            continue
        starts = sorted({func_start(pe, off) for off in string_refs(pe, va)})
        if starts:
            res.setdefault(m.group().decode(), []).extend(s for s in starts if s not in res.get(m.group().decode(), []))
    return res


def scan(data, names_rx=None):
    pe = PE(data)
    d = pe.d
    versions = {}
    for m in AM_VERSION_RX.finditer(d):
        versions[m.group(1).decode()] = {"ver": m.group(2).decode(), "build": m.group(3).decode().strip()}
    lits = {}
    for key, rx in (("com", rb"(?<![A-Za-z0-9])COM[0-9]{1,2}(?![A-Za-z0-9])"), ("mxjvs", rb"\\\\\.\\mxjvs"),
                    ("loopback", rb"127\.0\.0\.1")):
        lits[key] = sorted({(pe.off2va(m.start()), m.group().decode()) for m in re.finditer(rx, d) if pe.off2va(m.start())})
    ports = {}
    for va, p, kind in find_ports(pe):
        ports.setdefault(p, []).append({"va": va, "form": kind, "func": func_start(pe, pe.va2off(va))})
    funcs = named_functions(pe, re.compile(names_rx.encode()) if names_rx else NAME_RX)
    return {"pe": "pe64" if pe.pe64 else "pe32", "image_base": pe.base, "am_modules": versions,
            "literals": {k: [{"va": a, "text": t} for a, t in v] for k, v in lits.items()},
            "ports": {str(p): v for p, v in sorted(ports.items())},
            "functions": {k: v for k, v in sorted(funcs.items())}}


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    as_json = "--json" in argv
    names = None
    if "--names" in argv:
        names = argv[argv.index("--names") + 1]
        args = [a for a in args if a != names]
    if len(args) != 1:
        print(__doc__)
        return 2
    with open(args[0], "rb") as f:
        rep = scan(f.read(), names)
    if as_json:
        print(json.dumps(rep, indent=1, default=lambda x: "0x%x" % x))
        return 0
    print("%s image base 0x%x" % (rep["pe"], rep["image_base"]))
    for k, v in rep["am_modules"].items():
        print("  %-18s Ver.%s  %s" % (k, v["ver"], v["build"]))
    for k, v in rep["literals"].items():
        print("  %-9s %s" % (k, ", ".join("%s@0x%x" % (x["text"], x["va"]) for x in v) or "-"))
    for p, v in rep["ports"].items():
        print("  poort %s: %s" % (p, ", ".join("0x%x (functie ~0x%x)" % (x["va"], x["func"]) for x in v)))
    for k, v in rep["functions"].items():
        print("  %-40s %s" % (k, " ".join("0x%x" % x for x in v)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

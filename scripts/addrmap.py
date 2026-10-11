#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""addrmap — reken een virtueel adres om naar een bestandsplaats in een PE- of ELF-bestand.

Waarom dit bestaat
------------------
Een patch in `patches/<Game>/patches.ini` van de soort `memory` noemt een adres:

    Kind = memory
    Module = game.exe
    Offset = 0x113E80
    Action = jmp-returns-true

Voor de **hoofd-exe** is dat adres terug te rekenen naar een plaats in het bestand: het adres is
`ImageBase + RVA` (PE) of een `p_vaddr` (ELF), en via de sectie- of segmenttabel weet je welk stuk
van het bestand daarbij hoort. Daarmee is zo'n patch een gewone bestandspatch, en is er geen
injectie en geen procesgeheugen nodig. Zie `docs/community-model.md`, punt 1 en 2.

Wat het niet kan
----------------
- Een patch in een **DLL die pas later geladen wordt**: die staat niet in het bestand van de exe.
- Een **gepakte** exe: de bytes in het bestand zijn dan niet de bytes die het spel uitvoert.
- Een aanpassing die pas **tijdens het draaien** moet gebeuren.

Gebruik
-------
    >>> import addrmap
    >>> m = addrmap.open_module("Game.exe")
    >>> m.base                      # ImageBase (PE) of de laagste p_vaddr (ELF)
    >>> m.rva_to_offset(0x113E80)   # RVA (PE) -> bestandsplaats
    >>> m.va_to_offset(0x8ADD63C)   # absoluut adres (ELF) -> bestandsplaats
    >>> m.offset_for("0x113E80")    # wat de ini bedoelt: Module + Offset

De module leest alleen; hij schrijft nooit.
"""
import struct

__all__ = ["Module", "open_module", "NotAnImage", "AddressOutsideImage"]

PE = "pe"
ELF = "elf"

PT_LOAD = 1


class NotAnImage(Exception):
    """Het bestand is geen PE- of ELF-image (of te kort om het te kunnen zijn)."""


class AddressOutsideImage(Exception):
    """Het adres valt in geen enkele sectie of segment dat in het bestand staat."""


class Module:
    """Een PE- of ELF-bestand met de gegevens die nodig zijn om adressen om te rekenen."""

    def __init__(self, path, data, fmt):
        self.path = path
        self.data = data
        self.format = fmt
        self.base = 0
        # (naam, virtueel_begin, virtuele_grootte, bestandsplaats, bestandsgrootte)
        self.regions = []
        if fmt == PE:
            self._read_pe()
        else:
            self._read_elf()

    # -- inlezen ---------------------------------------------------------------------------------

    def _read_pe(self):
        d = self.data
        if len(d) < 0x40:
            raise NotAnImage("te kort voor een DOS-header")
        e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
        if e_lfanew + 24 > len(d) or d[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            raise NotAnImage("geen PE-handtekening")
        coff = e_lfanew + 4
        nsec = struct.unpack_from("<H", d, coff + 2)[0]
        opt_size = struct.unpack_from("<H", d, coff + 16)[0]
        opt = coff + 20
        magic = struct.unpack_from("<H", d, opt)[0]
        if magic == 0x20B:          # PE32+
            self.base = struct.unpack_from("<Q", d, opt + 24)[0]
        elif magic == 0x10B:        # PE32
            self.base = struct.unpack_from("<I", d, opt + 28)[0]
        else:
            raise NotAnImage("onbekende optional-header magic 0x%04x" % magic)
        sec = opt + opt_size
        for i in range(nsec):
            off = sec + i * 40
            if off + 40 > len(d):
                break
            name = d[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", d, off + 8)
            span = max(vsize, rawsize)
            self.regions.append((name, self.base + vaddr, span, rawptr, rawsize))

    def _read_elf(self):
        d = self.data
        if len(d) < 0x40 or d[:4] != b"\x7fELF":
            raise NotAnImage("geen ELF-handtekening")
        cls, data_enc = d[4], d[5]
        if data_enc == 1:
            end = "<"
        elif data_enc == 2:
            end = ">"
        else:
            raise NotAnImage("onbekende ELF-gegevenscodering %d" % data_enc)
        # Let op: e_phoff, e_phentsize en e_phnum staan NIET naast elkaar in de ELF-header.
        # Tussen e_phoff en e_phentsize zitten e_shoff, e_flags en e_ehsize.
        if cls == 1:                # 32-bits
            phoff = struct.unpack_from(end + "I", d, 0x1C)[0]
            phentsize = struct.unpack_from(end + "H", d, 0x2A)[0]
            phnum = struct.unpack_from(end + "H", d, 0x2C)[0]
            p_fmt, p_size = end + "IIIIIIII", 32
        elif cls == 2:              # 64-bits
            phoff = struct.unpack_from(end + "Q", d, 0x20)[0]
            phentsize = struct.unpack_from(end + "H", d, 0x36)[0]
            phnum = struct.unpack_from(end + "H", d, 0x38)[0]
            p_fmt, p_size = end + "IIQQQQQQ", 56
        else:
            raise NotAnImage("onbekende ELF-klasse %d" % cls)
        step = phentsize or p_size
        loads = []
        for i in range(phnum):
            off = phoff + i * step
            if off + step > len(d):
                break
            vals = struct.unpack_from(p_fmt, d, off)
            p_type = vals[0]
            if p_type != PT_LOAD:
                continue
            if cls == 1:
                p_offset, p_vaddr, p_filesz = vals[1], vals[2], vals[4]
            else:
                p_offset, p_vaddr, p_filesz = vals[2], vals[3], vals[5]
            loads.append((p_vaddr, p_filesz, p_offset))
        if not loads:
            raise NotAnImage("geen PT_LOAD-segment")
        self.base = min(v for v, _, _ in loads)
        for n, (vaddr, filesz, offset) in enumerate(sorted(loads)):
            self.regions.append(("LOAD%d" % n, vaddr, filesz, offset, filesz))

    # -- omrekenen -------------------------------------------------------------------------------

    def _find(self, va):
        for name, start, span, fptr, fsize in self.regions:
            if start <= va < start + span:
                delta = va - start
                if delta >= fsize:
                    # Het adres zit in het virtuele deel dat niet in het bestand staat (bss).
                    raise AddressOutsideImage(
                        "%s: 0x%x valt in %s maar voorbij de bytes in het bestand (%d bytes)"
                        % (self.path, va, name, fsize))
                return fptr + delta, name
        raise AddressOutsideImage("%s: 0x%x valt in geen enkele sectie of segment" % (self.path, va))

    def va_to_offset(self, va):
        """Een absoluut virtueel adres naar een bestandsplaats."""
        return self._find(va)[0]

    def rva_to_offset(self, rva):
        """Een RVA (PE: ten opzichte van ImageBase) naar een bestandsplaats."""
        return self._find(self.base + rva)[0]

    def offset_for(self, offset):
        """Wat `Module` + `Offset` in een patches.ini bedoelt: het adres ten opzichte van de basis."""
        return self._find(self.base + offset)[0]

    def region_of(self, offset):
        """Naam van de sectie of het segment waarin `Offset` valt (voor de melding aan de gebruiker)."""
        return self._find(self.base + offset)[1]

    def read(self, offset, size):
        """De bytes op `Module + Offset`, zoals ze in het bestand staan."""
        off = self.offset_for(offset)
        if off + size > len(self.data):
            raise AddressOutsideImage(
                "%s: %d bytes op 0x%x lopen voorbij het einde van het bestand" % (self.path, size, offset))
        return self.data[off:off + size]

    def describe(self):
        return "%s (%s, basis 0x%x, %d secties/segmenten)" % (
            self.path, self.format, self.base, len(self.regions))


def open_module(path):
    """Open een PE- of ELF-bestand en geef een Module terug."""
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) >= 4 and data[:4] == b"\x7fELF":
        return Module(path, data, ELF)
    if len(data) >= 2 and data[:2] == b"MZ":
        return Module(path, data, PE)
    raise NotAnImage("%s: geen MZ- en geen ELF-handtekening" % path)


def is_image(path):
    try:
        open_module(path)
        return True
    except (NotAnImage, OSError):
        return False


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("gebruik: addrmap.py <bestand> [offset ...]", file=sys.stderr)
        raise SystemExit(2)
    m = open_module(sys.argv[1])
    print(m.describe())
    for name, start, span, fptr, fsize in m.regions:
        print("  %-10s virtueel 0x%-10x bestand 0x%-8x %d bytes" % (name, start, fptr, fsize))
    for arg in sys.argv[2:]:
        off = int(arg, 0)
        try:
            print("  0x%x -> bestand 0x%x (%s)" % (off, m.offset_for(off), m.region_of(off)))
        except AddressOutsideImage as e:
            print("  0x%x -> %s" % (off, e))

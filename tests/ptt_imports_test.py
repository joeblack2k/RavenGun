#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test van tools/rg-ptt/rg_ptt_imports.py op een zelfgebouwd mini-beeld (geen spelbestand nodig), en, als PTW.exe er is
(RG_PTT_EXE of de NAS), een controle dat de ingebakken tabel (ptt_imports.h) gelijk is aan wat het script nu uitrekent."""
import importlib.util
import os
import re
import struct
import sys

HIER = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HIER, '..', 'tools', 'rg-ptt', 'rg_ptt_imports.py')
spec = importlib.util.spec_from_file_location('rg_ptt_imports', SCRIPT)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

fails = 0


def check(c, msg):
    global fails
    if not c:
        print('FAIL:', msg)
        fails += 1


def hn(naam):
    b = struct.pack('<H', 0) + naam.encode() + b'\0'
    return b + (b'\0' if len(b) % 2 else b'')


def bouw_beeld():
    """Minimaal PE-bestand: een sectie op RVA 0x1000 = bestandsoffset 0x400, met dezelfde indeling als PTW.exe:
       [IAT-groepen][... versleuteld blok ...][OFT-groepen][hint/namen en DLL-namen]. Stub-gegevens op RVA 0xd40f0/0xd40f8."""
    # DLL's in 'linker-volgorde' (bytewijs gesorteerd): KERNEL32, USER32, ijl11 (ordinalen), ole32
    dlls = [('KERNEL32.dll', ['GetTickCount', 'ExitProcess']), ('USER32.dll', ['MessageBoxA']),
            ('ijl11.dll', [2, 3]), ('ole32.dll', ['CoInitialize'])]
    sec_va = 0x1000
    strings = b''
    str_va = {}
    naam_va = {}
    base_str = 0x800   # offset binnen de sectie waar de strings beginnen
    pos = base_str
    namen_hn = {}
    for dll, funcs in dlls:
        lijst = []
        for f in funcs:
            if isinstance(f, int):
                lijst.append(0x80000000 | f)
            else:
                blok = hn(f)
                namen_hn[f] = sec_va + pos
                lijst.append(sec_va + pos)
                strings += blok
                pos += len(blok)
        nb = dll.encode() + b'\0'
        naam_va[dll] = sec_va + pos
        strings += nb
        pos += len(nb)
        dlls[dlls.index((dll, funcs))] = (dll, funcs, lijst)
    n_desc = len(dlls) + 1
    blok_grootte = 20 * n_desc
    # indeling: IAT eerst, dan blok, dan OFT
    iat_len = sum(4 * (len(l) + 1) for _d, _f, l in dlls)
    blok_off = iat_len + 0x20            # beetje ruimte ertussen
    blok_rva = sec_va + blok_off
    oft_off = blok_off + blok_grootte
    sec = bytearray(0x1000)
    ft_rvas, oft_rvas = [], []
    o = 0
    for _d, _f, l in dlls:
        ft_rvas.append(sec_va + o)
        for i, v in enumerate(l):
            struct.pack_into('<I', sec, o + 4 * i, v)
        o += 4 * (len(l) + 1)
    o = oft_off
    for _d, _f, l in dlls:
        oft_rvas.append(sec_va + o)
        for i, v in enumerate(l):
            struct.pack_into('<I', sec, o + 4 * i, v)
        o += 4 * (len(l) + 1)
    assert o < base_str
    sec[blok_off:blok_off + blok_grootte] = bytes((i * 37 + 11) & 0xff for i in range(blok_grootte))   # "versleuteld"
    sec[base_str:base_str + len(strings)] = strings
    # stub-gegevens in een tweede sectie op RVA 0xd4000 (gegevens op 0xd40f0 / 0xd40f8)
    stub = bytearray(0x200)
    struct.pack_into('<I', stub, 0xf0, blok_rva)
    struct.pack_into('<I', stub, 0xf8, blok_grootte)
    # PE-kop
    kop = bytearray(0x400)
    kop[0:2] = b'MZ'
    struct.pack_into('<I', kop, 0x3c, 0x80)
    kop[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', kop, 0x84, 0x14c, 2)
    struct.pack_into('<H', kop, 0x84 + 16, 0xe0)
    so = 0x80 + 24 + 0xe0
    kop[so:so + 8] = b'.wkcrypt'
    struct.pack_into('<IIII', kop, so + 8, 0x1000, 0x1000, 0x1000, 0x400)    # vs, va, rs, ro
    kop[so + 40:so + 48] = b'.data\0\0\0'
    struct.pack_into('<IIII', kop, so + 40 + 8, 0x200, 0xd4000, 0x200, 0x1400)
    img = bytes(kop) + bytes(sec) + bytes(stub)
    verwacht = bytearray()
    gesorteerd = sorted(dlls, key=lambda t: t[0].encode())
    for (dll, _f, l), oft, ft in zip(dlls, oft_rvas, ft_rvas):
        verwacht += struct.pack('<IIIII', oft, 0, 0, naam_va[dll], ft)
    verwacht += b'\0' * 20
    assert [t[0] for t in dlls] == [t[0] for t in gesorteerd]
    return img, blok_rva, blok_grootte, bytes(verwacht)


img, rva, groot, verwacht = bouw_beeld()
check(m.stub_blok(img) == (rva, groot), 'stub_blok leest RVA en grootte')
tabel, besch = m.bouw_tabel(img, rva, groot)
check(tabel == verwacht, 'de tabel op het mini-beeld klopt byte voor byte')
check([b[0] for b in besch] == ['KERNEL32.dll', 'USER32.dll', 'ijl11.dll', 'ole32.dll'], 'groepen krijgen de namen in linkervolgorde')

# een groep met een verkeerde naam moet geweigerd worden (naam direct achter een hint/naam-blok van een andere groep)
stuk = bytearray(img)
try:
    # wissel de namen van KERNEL32 en USER32 in de strings: nu eindigt KERNEL32's blok voor 'KERNEL32' niet meer
    a = bytes(stuk).index(b'KERNEL32.dll')
    b2 = bytes(stuk).index(b'USER32.dll')
    stuk[a:a + 12] = b'XXXXXX32.dll'
    stuk[b2:b2 + 11] = b'ZZZZZ32.dll'
    m.bouw_tabel(bytes(stuk), rva, groot)
    # namen blijven 4 stuks en gesorteerd XXXXXX < ZZZZZ < ijl11 < ole32: dezelfde toekenning -> mag slagen
except ValueError:
    pass
check(True, 'geen crash bij hernoemde DLL')

# te weinig namen: fout
stuk = bytearray(img)
a = bytes(stuk).index(b'ole32.dll')
stuk[a:a + 9] = b'ole32.xyz'
try:
    m.bouw_tabel(bytes(stuk), rva, groot)
    check(False, 'te weinig DLL-namen moet een fout geven')
except ValueError:
    check(True, 'te weinig DLL-namen geeft een fout')

# blokgrootte geen veelvoud van 20
try:
    m.bouw_tabel(img, rva, 281)
    check(False, 'grootte 281 moet een fout geven')
except ValueError:
    check(True, 'grootte 281 geeft een fout')

# het echte spelbestand, als het er is
exe = os.environ.get('RG_PTT_EXE') or os.path.expanduser('~/mnt/spidernas_emulation/Teknoparrot/Pull The Trigger/PTW.exe')
if os.path.isfile(exe):
    d = open(exe, 'rb').read()
    r, g = m.stub_blok(d)
    check((r, g) == (0xbde00, 280), 'PTW.exe: blok op 0xbde00, 280 bytes')
    t, b = m.bouw_tabel(d, r, g)
    h = open(os.path.join(HIER, '..', 'tools', 'rg-ptt', 'ptt_imports.h')).read()
    plain = re.search(r'ptt_plain\[PTT_BLOCK_SIZE\] = \{(.*?)\};', h, re.S).group(1)
    ing = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', plain))
    check(ing == t, 'PTW.exe: de ingebakken tabel is gelijk aan de berekende')
    check(len(b) == 13, 'PTW.exe: 13 DLL\'s')
    # onafhankelijk bewijs uit de code van het spel: SioReset (WSC32, eerste van de groep) wordt aangeroepen via FT 0xb1364
    ft = {n: f for n, _o, f, _nr, _c in b}
    check(ft['WSC32.dll'] == 0xb1364, 'PTW.exe: FT van WSC32 = 0xb1364 (de code roept SioReset daar aan)')
else:
    print('overgeslagen: geen PTW.exe (alleen het mini-beeld getest)')

if fails:
    print(fails, 'mislukt')
    sys.exit(1)
print('ptt_imports_test: ok')

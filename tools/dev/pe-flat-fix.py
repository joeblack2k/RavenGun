#!/usr/bin/env python3
"""Repair the section table of a PE that is a flat memory image (Coopers 9).

Some arcade dumps (Coopers 9's game.exe) are written as a *flat image*: the
file at offset X holds the byte that belongs at RVA X, headers included.  The
PE header however still carries the section table of the packed exe it was
dumped from, so every section whose raw offset differed from its RVA points at
the wrong bytes.  For Coopers 9 that hid the whole import directory (it lives
in .protect, which the stale table placed 0x10A000 bytes too early): the image
loaded, no import was seen, and the game died on its first API call.

This script rewrites PointerToRawData to the section's VirtualAddress and
SizeOfRawData to the aligned VirtualSize, then checks the result: the import
directory must parse and name at least one DLL.  It never touches the source
file (NAS) and only writes the output path.

Usage: pe-flat-fix.py <in.exe> <out.exe> [--expect-dll NAME]...
Exit code 0 when the output parses, 1 when it does not.
"""
import struct
import sys


def align(x, a=0x1000):
    return (x + a - 1) // a * a


def sections(d):
    e = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, e + 6)[0]
    optsz = struct.unpack_from('<H', d, e + 20)[0]
    opt = e + 24
    out = []
    for i in range(nsec):
        o = opt + optsz + i * 40
        name = bytes(d[o:o + 8]).rstrip(b'\0').decode('latin1')
        vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
        out.append((name, o, va, vs, ro, rs))
    ndd = struct.unpack_from('<I', d, opt + 92)[0]
    dirs = [struct.unpack_from('<II', d, opt + 96 + i * 8) for i in range(ndd)]
    return out, dirs


def cstr(d, off):
    end = d.index(b'\0', off)
    return d[off:end].decode('latin1')


def descriptors(d, rva):
    out = []
    for i in range(64):
        oft, tds, fc, name, ft = struct.unpack_from('<IIIII', d, rva + i * 20)
        if not (oft or name or ft):
            break
        out.append((cstr(d, name), oft, ft))
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    src, dst = argv[1], argv[2]
    expect = [a.split('=', 1)[1] for a in argv[3:] if a.startswith('--expect-dll=')]
    d = bytearray(open(src, 'rb').read())
    secs, dirs = sections(d)
    changed = 0
    for name, o, va, vs, ro, rs in secs:
        if ro != va or rs != align(vs):
            struct.pack_into('<I', d, o + 20, va)
            struct.pack_into('<I', d, o + 16, align(vs))
            changed += 1
            print("section %-10s raw %08x -> %08x size %08x -> %08x"
                  % (name, ro, va, rs, align(vs)))
    open(dst, 'wb').write(d)
    print("written %s (%d section(s) changed)" % (dst, changed))
    # check: import directory parses and names DLLs
    imp_rva, imp_size = dirs[1]
    if not imp_rva:
        print("check: no import directory")
        return 1
    try:
        descs = descriptors(bytes(d), imp_rva)
    except Exception as exc:                                  # noqa: BLE001
        print("check: import directory does not parse: %s" % exc)
        return 1
    names = [n for n, _, _ in descs]
    print("check: %d descriptors, %d dlls: %s"
          % (len(descs), len(names), ", ".join(names) if names else "-"))
    if not names:
        return 1
    missing = [n for n in expect if n.lower() not in [x.lower() for x in names]]
    for n in expect:
        print("check: %-14s %s" % (n, "absent" if n in missing else "present"))
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

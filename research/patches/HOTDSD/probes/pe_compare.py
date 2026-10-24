#!/usr/bin/env python3
"""Read-only PE comparison for HOTD Scarlet Dawn.

Compares, per pair:
  * the working (patched) image vs the "_org"/".original" shell next to it
  * the game copy of amdaemon.exe vs the SegaShell copy in the project repo

Prints DOS-stub/Rich-header/PE-header/section-table facts, import summary and a
byte-level summary of the first 0x1000 bytes.  Nothing is written anywhere.

Usage:  python pe_compare.py
"""
import struct
import sys
import hashlib
import os

G = r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
AMD = G + r"\App\Hodzero\WindowsNoEditor\AMDaemon"
W64 = G + r"\App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64"
REPO = r"C:\Users\Homelab\Documents\Default Project\TeknoParrotUi-2.0.0.131-win-x64"

PAIRS = [
    ("game exe", os.path.join(W64, "Hodzero-Win64-Shipping.exe"),
     os.path.join(W64, "Hodzero-Win64-Shipping_org.exe")),
    ("daemon", os.path.join(AMD, "amdaemon.exe"),
     os.path.join(AMD, "amdaemon.exe.original")),
    ("daemon vs repo SegaShell", os.path.join(AMD, "amdaemon.exe"),
     os.path.join(REPO, r"loaders\SegaShell\bin\amdaemon.exe")),
    ("hwvalue vs repo", os.path.join(AMD, "hwvalue.exe"),
     os.path.join(REPO, r"loaders\SegaShell\bin\hwvalue.exe")),
]


def rd(path, off=0, n=None):
    with open(path, "rb") as f:
        f.seek(off)
        return f.read(n) if n is not None else f.read()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def pe(path):
    d = rd(path, 0, 0x1000)
    if d[:2] != b"MZ":
        return {"error": "no MZ"}
    e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
    if d[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        return {"error": "no PE sig at %#x" % e_lfanew}
    coff = e_lfanew + 4
    machine, nsec, ts, psym, nsym, optsz, chars = struct.unpack_from("<HHIIIHH", d, coff)
    opt = coff + 20
    magic = struct.unpack_from("<H", d, opt)[0]
    pe32p = magic == 0x20B
    ep = struct.unpack_from("<I", d, opt + 16)[0]
    imagebase = struct.unpack_from("<Q", d, opt + 24)[0] if pe32p else struct.unpack_from("<I", d, opt + 28)[0]
    sectalign, filealign = struct.unpack_from("<II", d, opt + 32)
    sizeofimage = struct.unpack_from("<I", d, opt + 56)[0]
    subsys = struct.unpack_from("<H", d, opt + 68)[0]
    dllchar = struct.unpack_from("<H", d, opt + 70)[0]
    ndd = struct.unpack_from("<I", d, opt + 108 if pe32p else opt + 92)[0]
    dd = opt + (112 if pe32p else 96)
    dirs = {}
    for i in range(min(ndd, 16)):
        va, sz = struct.unpack_from("<II", d, dd + 8 * i)
        dirs[i] = (va, sz)
    sects = []
    st = opt + optsz
    for i in range(nsec):
        o = st + 40 * i
        name = d[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", d, o + 8)
        ch = struct.unpack_from("<I", d, o + 36)[0]
        sects.append(dict(name=name, vsize=vsize, vaddr=vaddr, rawsize=rawsize,
                          rawptr=rawptr, chars=ch))
    # Rich header: search for the "Rich" marker between 0x80 and e_lfanew
    rich = None
    idx = d.find(b"Rich", 0x40, e_lfanew)
    if idx != -1:
        rich = dict(richtag=idx, dans_off=d.find(b"DanS", 0x40, idx))
    # import DLL names
    imports = []
    if 1 in dirs and dirs[1][1]:
        iva = rva2off(sects, dirs[1][0])
        if iva is not None:
            k = 0
            while True:
                o = iva + 20 * k
                ent = rd(path, o, 20)
                if len(ent) < 20 or ent == b"\0" * 20:
                    break
                name_rva = struct.unpack_from("<I", ent, 12)[0]
                if name_rva == 0:
                    break
                no = rva2off(sects, name_rva)
                nm = b""
                if no is not None:
                    nm = rd(path, no, 64).split(b"\0")[0].decode("latin1", "replace")
                imports.append(nm)
                k += 1
    return dict(size=os.path.getsize(path), sha256=sha256(path), machine=machine,
                nsec=nsec, timestamp=ts, optsz=optsz, chars=chars, magic=magic,
                entrypoint=ep, imagebase=imagebase, sectionalign=sectalign,
                filealign=filealign, sizeofimage=sizeofimage, subsystem=subsys,
                dllchar=dllchar, ndd=ndd, dirs=dirs, sects=sects, rich=rich,
                imports=imports, e_lfanew=e_lfanew, dos_stub=d[:0x40].hex())


def rva2off(sects, rva):
    for s in sects:
        if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rawsize"]):
            if s["rawptr"] == 0:
                return None
            return s["rawptr"] + (rva - s["vaddr"])
    return None


def show(tag, path, info):
    if "error" in info:
        print("  %-10s %s  ERROR %s" % (tag, path, info["error"]))
        return
    print("  %-10s %s" % (tag, path))
    print("      size=%d sha256=%s" % (info["size"], info["sha256"][:16]))
    print("      PE32+ =%s machine=%#x nsec=%d timestamp=%#x (%s) ep=%#x imagebase=%#x"
          % (info["magic"] == 0x20B, info["machine"], info["nsec"], info["timestamp"],
             _ts(info["timestamp"]), info["entrypoint"], info["imagebase"]))
    print("      optsz=%#x sectalign=%#x filealign=%#x sizeofimage=%#x subsystem=%d dllchar=%#x"
          % (info["optsz"], info["sectionalign"], info["filealign"], info["sizeofimage"],
             info["subsystem"], info["dllchar"]))
    print("      darf=%d e_lfanew=%#x rich=%s" % (info["ndd"], info["e_lfanew"], info["rich"]))
    print("      dirs: %s" % {k: (hex(v[0]), hex(v[1])) for k, v in sorted(info["dirs"].items()) if v[1]})
    for s in info["sects"]:
        print("        %-10s vaddr=%#010x vsize=%#010x rawptr=%#010x rawsize=%#010x ch=%#010x"
              % (s["name"], s["vaddr"], s["vsize"], s["rawptr"], s["rawsize"], s["chars"]))
    print("      imports(%d): %s" % (len(info["imports"]), ", ".join(info["imports"])))


def _ts(t):
    import datetime
    try:
        return datetime.datetime.utcfromtimestamp(t).isoformat() + "Z"
    except Exception:
        return "?"


def firstdiff(a, b, n=0x2000):
    da = rd(a, 0, n)
    db = rd(b, 0, n)
    for i in range(min(len(da), len(db))):
        if da[i] != db[i]:
            return i, da[i], db[i]
    return None


def main():
    for label, a, b in PAIRS:
        print("=" * 78)
        print("### %s" % label)
        print("=" * 78)
        for p in (a, b):
            if not os.path.exists(p):
                print("  MISSING: %s" % p)
        if not (os.path.exists(a) and os.path.exists(b)):
            continue
        ia, ib = pe(a), pe(b)
        show("A", a, ia)
        show("B", b, ib)
        fd = firstdiff(a, b)
        print("  first byte difference in first 0x2000: %s" % (fd,))
        if "error" not in ia and "error" not in ib:
            print("  same DOS stub(0x40)      : %s" % (ia["dos_stub"] == ib["dos_stub"]))
            print("  same timestamp           : %s" % (ia["timestamp"] == ib["timestamp"]))
            print("  same imagebase           : %s" % (ia["imagebase"] == ib["imagebase"]))
            print("  same entrypoint          : %s" % (ia["entrypoint"] == ib["entrypoint"]))
            print("  same sizeofimage         : %s" % (ia["sizeofimage"] == ib["sizeofimage"]))
            print("  same section names       : %s"
                  % ([s["name"] for s in ia["sects"]] == [s["name"] for s in ib["sects"]]))
            print("  same imports             : %s" % (ia["imports"] == ib["imports"]))
            sa = sum(s["rawsize"] for s in ia["sects"])
            sb = sum(s["rawsize"] for s in ib["sects"])
            print("  sum(rawsize) A=%d B=%d" % (sa, sb))
            fa = max((s["rawptr"] + s["rawsize"]) for s in ia["sects"])
            fb = max((s["rawptr"] + s["rawsize"]) for s in ib["sects"])
            print("  end-of-sections A=%d B=%d  file sizes %d / %d  slack A=%d B=%d"
                  % (fa, fb, ia["size"], ib["size"], ia["size"] - fa, ib["size"] - fb))


if __name__ == "__main__":
    main()

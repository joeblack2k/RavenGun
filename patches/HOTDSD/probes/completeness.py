#!/usr/bin/env python3
"""Structural completeness check for House of the Dead: Scarlet Dawn.

Method: re\volledigheidscontrole.md, the same four checks the other five games went
through, adapted to a Windows/PC-based Unreal Engine title instead of an ElfLdr2 ELF:

  1. does the executable the profile names exist, at the size the profile records;
  2. are there empty files (0 bytes);
  3. is the image structurally sound -- for an ELF that is PT_LOAD/DT_NEEDED, for a PE
     it is "every section fits inside the file" (not truncated) plus "every imported
     DLL is resolvable" (system, or shipped in the folder);
  4. is the size and the directory structure logical for the kind of game.

It also cross-checks every path referenced by the game's own batch/config files for
existence, and reports which of those absences are expected.

READ-ONLY on the NAS.  Writes nothing anywhere.  Usage: python completeness.py
"""
import json
import os
import re
import struct
import sys

G = r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"

# --- oracle sources (local, no NAS needed for these) -------------------------
PROFILE = (r"C:\Users\Homelab\Documents\Default Project"
           r"\TeknoParrotUi-2.0.0.131-win-x64\GameProfiles\HOTDSD.xml")
SETUP = (r"C:\Users\Homelab\Documents\Default Project"
         r"\TeknoParrotUi-2.0.0.131-win-x64\GameSetup\HOTDSD.xml")
GAME_INI = (r"C:\Users\Homelab\Documents\Default Project"
            r"\TeknoParrotUi-2.0.0.131-win-x64\staging\ravengun-patches"
            r"\patches\HOTDSD\game.ini")

# Windows / Wine system modules: resolvable without the game shipping them.
SYSTEM_DLLS = set("""
advapi32.dll bcrypt.dll cfgmgr32.dll combase.dll crypt32.dll dbghelp.dll dnsapi.dll
dsound.dll dwmapi.dll d3d9.dll d3d11.dll dxgi.dll gdi32.dll hid.dll imm32.dll
iphlpapi.dll kernel32.dll mfplat.dll mpr.dll msacm32.dll msvcp140.dll msvcr110.dll
msvcp110.dll ntdll.dll ole32.dll oleaut32.dll opengl32.dll powrprof.dll propsys.dll
setupapi.dll shell32.dll shlwapi.dll ucrtbase.dll user32.dll vcruntime140.dll
version.dll winhttp.dll wininet.dll winmm.dll ws2_32.dll wlanapi.dll
x3daudio1_7.dll xapofx1_5.dll xinput1_3.dll kernelbase.dll
""".split())

# Runtime DLLs that a UE4 title normally resolves from the *ship* rather than the OS.
# Presence in the folder is what we test; the name list is only used for labelling.
EXPECTED_SHIPPED_DLL_HINTS = ["physx", "apex", "nvcloth", "pxfoundation", "pxpvsdk",
                              "libogg", "libvorbis", "gfsdk_aftermath", "winpixevent"]


def walk(root):
    files = {}
    for dirpath, dirnames, filenames in os.walk(root):
        for fn in filenames:
            p = os.path.join(dirpath, fn)
            try:
                files[os.path.relpath(p, root)] = os.path.getsize(p)
            except OSError as e:
                files[os.path.relpath(p, root)] = -1
                print("  !! cannot stat %s: %s" % (p, e), file=sys.stderr)
    return files


def pe_sections(path):
    with open(path, "rb") as f:
        head = f.read(0x1000)
    if head[:2] != b"MZ":
        return None
    e = struct.unpack_from("<I", head, 0x3C)[0]
    if head[e:e + 4] != b"PE\0\0":
        return None
    nsec = struct.unpack_from("<H", head, e + 6)[0]
    ts = struct.unpack_from("<I", head, e + 8)[0]
    optsz = struct.unpack_from("<H", head, e + 20)[0]
    st = e + 24 + optsz
    sects = []
    for i in range(nsec):
        o = st + 40 * i
        nm = head[o:o + 8].rstrip(b"\0").decode("latin1")
        vs, va, rs, rp = struct.unpack_from("<IIII", head, o + 8)
        sects.append(dict(name=nm, vsize=vs, vaddr=va, rawsize=rs, rawptr=rp))
    return dict(nsec=nsec, timestamp=ts, sections=sects)


def imports_of(path, sects):
    """Return the imported DLL names, by walking the import directory."""
    with open(path, "rb") as f:
        head = f.read(0x1000)
    e = struct.unpack_from("<I", head, 0x3C)[0]
    optsz = struct.unpack_from("<H", head, e + 20)[0]
    magic = struct.unpack_from("<H", head, e + 24)[0]
    dd = e + 24 + (112 if magic == 0x20B else 96)
    iva, isz = struct.unpack_from("<II", head, dd + 8)
    if not isz:
        return []
    off = rva2off(sects, iva)
    if off is None:
        return []
    out = []
    with open(path, "rb") as f:
        for k in range(64):
            f.seek(off + 20 * k)
            ent = f.read(20)
            if len(ent) < 20 or ent == b"\0" * 20:
                break
            nrva = struct.unpack_from("<I", ent, 12)[0]
            if not nrva:
                break
            no = rva2off(sects, nrva)
            if no is None:
                continue
            f.seek(no)
            out.append(f.read(64).split(b"\0")[0].decode("latin1"))
    return out


def rva2off(sects, rva):
    for s in sects:
        if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rawsize"]):
            return s["rawptr"] + (rva - s["vaddr"]) if s["rawptr"] else None
    return None


def human(n):
    for u in ("B", "KB", "MB", "GB"):
        if n < 1024 or u == "GB":
            return "%.2f %s" % (n, u) if u != "B" else "%d B" % n
        n /= 1024.0


def A(s):
    """ASCII-safe: the console here is cp1252 and the .acf is not."""
    return str(s).encode("ascii", "replace").decode("ascii")


PAK_MAGIC = b"\xe1\x12\x6f\x5a"          # 0x5A6F12E1 little-endian


def pak_candidates(path, size, window=1024):
    """UE4 pak: the magic sits at the start of the FPakInfo struct, and for the
    versions we care about that struct is written at the END of the file (with the
    magic repeated as the last four bytes).  Rather than assume a struct size, find
    every magic in the head and tail windows and parse forward from each, then judge
    each candidate by whether its index lands inside the file."""
    out = []
    with open(path, "rb") as f:
        head = f.read(window)
        f.seek(max(0, size - window))
        tail = f.read(window)
    tail_base = max(0, size - window)
    spots = [(o, "head") for o in find_all(head, PAK_MAGIC)]
    spots += [(tail_base + o, "tail") for o in find_all(tail, PAK_MAGIC)]
    for off, where in spots:
        with open(path, "rb") as f:
            f.seek(off)
            buf = f.read(80)
        if len(buf) < 44:
            continue
        magic, ver = struct.unpack_from("<Ii", buf, 0)
        ioff, isize = struct.unpack_from("<qq", buf, 8)
        enc = buf[36]
        guid = buf[37:53]
        c = dict(off=off, where=where, ver=ver, ioff=ioff, isize=isize, enc=enc,
                 guid=guid.hex(),
                 ends_before_eof=(ioff + isize) <= size,
                 at_eof_minus_4=(off == size - 4),
                 sane=(1 <= ver <= 20 and 0 < ioff < size and 0 <= isize < size))
        out.append(c)
    return out


def find_all(hay, needle):
    r, i = [], hay.find(needle)
    while i != -1:
        r.append(i)
        i = hay.find(needle, i + 1)
    return r


def main():
    if not os.path.isdir(G):
        print("GAME FOLDER NOT REACHABLE: %s" % G)
        return 1
    print("=" * 78)
    print("HOTD Scarlet Dawn -- structural completeness check")
    print("oracle: re\\volledigheidscontrole.md (four checks, structural, NOT hash-by-hash)")
    print("=" * 78)

    files = walk(G)
    total = sum(v for v in files.values() if v >= 0)
    print("\n### 0. Inventory")
    print("  files            : %d" % len(files))
    print("  total bytes      : %d  (%s)" % (total, human(total)))
    empty = sorted(k for k, v in files.items() if v == 0)
    print("  empty files      : %d %s" % (len(empty), empty[:20]))

    # ---- check 4 first: structure -----------------------------------------
    print("\n### 1. Directory structure (check 4: logical for the kind of game)")
    agg = {}
    for rel, sz in files.items():
        parts = rel.split(os.sep)
        for depth in range(1, min(len(parts), 5) + 1):
            key = os.sep.join(parts[:depth])
            if depth == len(parts):
                a = agg.setdefault(key, [0, 0, 0])
                a[0] += 1
                a[1] += max(sz, 0)
    dirs = {}
    for rel, sz in files.items():
        d = os.path.dirname(rel) or "."
        a = dirs.setdefault(d, [0, 0])
        a[0] += 1
        a[1] += max(sz, 0)
    for d in sorted(dirs, key=lambda x: -dirs[x][1])[:40]:
        print("  %-72s %5d files  %10s" % (d, dirs[d][0], human(dirs[d][1])))

    # ---- check 1: the executable(s) the profile names ---------------------
    print("\n### 2. Check 1: the executables the profile names exist")
    prof = open(PROFILE, encoding="utf-8-sig").read()
    setup = open(SETUP, encoding="utf-8-sig").read()
    ini = open(GAME_INI, encoding="utf-8-sig", errors="replace").read()
    exes = re.findall(r"<ExecutableName2?>(.*?)</ExecutableName2?>", prof)
    locs = re.findall(r"<GameExecutableLocation2?>(.*?)</GameExecutableLocation2?>", setup)
    print("  profile ExecutableName/2 : %s" % exes)
    print("  setup   Locations        : %s" % locs)
    print("  game.ini Identify sizes  : %s"
          % re.findall(r"^(Path2?|Size)\s*=\s*(.*)$", ini, re.M))
    for loc in locs:
        p = os.path.join(G, loc.replace("\\", os.sep))
        ok = os.path.isfile(p)
        print("  %-70s %s %s" % (loc, "PRESENT" if ok else "** MISSING **",
                                 human(os.path.getsize(p)) if ok else ""))
    # the fallback executables StartApp.bat would pick, and firewall.cfg's second name
    for extra in (r"App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero.exe",
                  r"App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero-Win64-Test.exe",
                  r"App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero-Win64-Debug.exe"):
        p = os.path.join(G, extra.replace("\\", os.sep))
        print("  %-70s %s" % (extra, "present" if os.path.isfile(p) else "absent (fallback only)"))

    # ---- check 3: PE structural soundness ---------------------------------
    print("\n### 3. Check 3a: PE structural soundness (not truncated)")
    pefiles = [k for k in files if k.lower().endswith((".exe", ".dll"))]
    print("  %d PE files" % len(pefiles))
    for rel in sorted(pefiles):
        p = os.path.join(G, rel)
        info = pe_sections(p)
        if not info:
            print("  %-70s NOT A PE" % rel)
            continue
        end = max(s["rawptr"] + s["rawsize"] for s in info["sections"])
        sz = files[rel]
        slack = sz - end
        flag = "OK" if slack == 0 else ("OK(+%d overlay)" % slack if slack > 0 else "** TRUNCATED **")
        if rel.lower().endswith(("amdaemon.exe", "loading.exe")) or "Hodzero" in rel or slack != 0:
            print("  %-58s sec=%2d end=%9d size=%9d  %s"
                  % (rel, info["nsec"], end, sz, flag))
    trunc = []
    for rel in pefiles:
        info = pe_sections(os.path.join(G, rel))
        if not info:
            continue
        end = max(s["rawptr"] + s["rawsize"] for s in info["sections"])
        if files[rel] < end:
            trunc.append(rel)
    print("  truncated PE files: %d %s" % (len(trunc), trunc))

    # ---- check 3b: import resolvability -----------------------------------
    print("\n### 4. Check 3b: imported DLLs are resolvable (the DT_NEEDED analogue)")
    shipped = {}
    for rel in files:
        shipped.setdefault(os.path.basename(rel).lower(), rel)
    for rel in [r"App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero-Win64-Shipping.exe",
                r"App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe",
                r"App\Hodzero\WindowsNoEditor\AMDaemon\hwvalue.exe",
                r"pxGetHwinfo.exe"]:
        p = os.path.join(G, rel)
        if not os.path.isfile(p):
            continue
        info = pe_sections(p)
        imps = imports_of(p, info["sections"])
        missing = [d for d in imps if d.lower() not in SYSTEM_DLLS
                   and d.lower() not in shipped]
        fromsys = [d for d in imps if d.lower() in SYSTEM_DLLS]
        fromship = [d for d in imps if d.lower() not in SYSTEM_DLLS and d.lower() in shipped]
        print("  %s" % os.path.basename(rel))
        print("     imports=%d  system=%d  shipped-locally=%d  UNRESOLVED=%d"
              % (len(imps), len(fromsys), len(fromship), len(missing)))
        if fromship:
            print("     shipped locally : %s" % ", ".join(sorted(fromship)))
        if missing:
            print("     ** UNRESOLVED ** : %s" % ", ".join(sorted(missing)))

    # ---- check 4 continued: the pak and the content ------------------------
    print("\n### 5. Check 4: content packaging (UE4 pak) and the expected asset dirs")
    paks = [k for k in files if k.lower().endswith(".pak")]
    for rel in paks:
        p = os.path.join(G, rel)
        sz = files[rel]
        print("  %s  %s" % (rel, human(sz)))
        with open(p, "rb") as f:
            print("     first 64 bytes : %s" % f.read(64).hex(" "))
            f.seek(sz - 64)
            print("     last  64 bytes : %s" % f.read(64).hex(" "))
        for c in pak_candidates(p, sz):
            print("     magic @%-12d (%s)  version=%-3d index_offset=%-14d "
                  "index_size=%-12d enc=%d guid=%s"
                  % (c["off"], c["where"], c["ver"], c["ioff"], c["isize"],
                     c["enc"], c["guid"]))
            print("        magic is last 4 bytes of file : %s" % c["at_eof_minus_4"])
            print("        index ends inside the file     : %s" % c["ends_before_eof"])
            print("        parse is sane (version/offsets) : %s" % c["sane"])
    sigs = [k for k in files if k.lower().endswith(".sig")]
    print("  .sig files present: %d %s" % (len(sigs), sigs))
    acfs = [k for k in files if k.lower().endswith(".acf")]
    for rel in acfs:
        print("  %s  (%d bytes) ->" % (rel, files[rel]))
        with open(os.path.join(G, rel), "rb") as f:
            txt = f.read(600).decode("latin1", "replace").replace("\n", "\n     ")
        print("     " + A(txt))
        print("     ...head bytes: %s"
              % open(os.path.join(G, rel), "rb").read(48).hex(" "))
    for want in ["App/Hodzero/WindowsNoEditor/Hodzero/Content/Paks",
                 "App/Hodzero/WindowsNoEditor/Hodzero/Content",
                 "App/Hodzero/WindowsNoEditor/Hodzero/Saved",
                 "App/Hodzero/WindowsNoEditor/Hodzero/Saved/Config/WindowsNoEditor",
                 "App/Hodzero/WindowsNoEditor/Hodzero/Saved/Logs",
                 "App/Hodzero/WindowsNoEditor/Hodzero/Config",
                 "App/Hodzero/WindowsNoEditor/Engine",
                 "App/Hodzero/WindowsNoEditor/Engine/Content",
                 "App/Hodzero/WindowsNoEditor/Engine/Binaries/ThirdParty",
                 "App/Hodzero/WindowsNoEditor/Engine/Plugins",
                 "App/Hodzero/WindowsNoEditor/Hodzero/Plugins"]:
        p = os.path.join(G, want.replace("/", os.sep))
        print("  %-62s %s" % (want, "present" if os.path.isdir(p) else "ABSENT"))

    # ---- referenced-but-missing sweep -------------------------------------
    print("\n### 6. Paths the game's own scripts/config name, checked for existence")
    textfiles = [k for k in files if k.lower().endswith((".bat", ".cfg", ".ini", ".json"))]
    refs = set()
    pat = re.compile(r"[A-Za-z0-9_\\/\.\-]+\.(?:exe|dll|json|ini|bat|dat|pak|hex|bin|usm)")
    for rel in textfiles:
        try:
            raw = open(os.path.join(G, rel), "rb").read()
        except OSError:
            continue
        for m in pat.findall(raw.decode("latin1", "replace")):
            refs.add(m.replace("\\", os.sep))
    names_present = sorted(refs)
    for r in names_present:
        base = os.path.basename(r)
        found = [k for k in files if os.path.basename(k).lower() == base.lower()]
        print("  %-34s %s %s" % (r, "PRESENT" if found else "absent",
                                 ("<- " + found[0]) if found else ""))
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(errors="replace")
    except Exception:
        pass
    sys.exit(main())

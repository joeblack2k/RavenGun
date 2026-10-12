#!/usr/bin/env python3
"""Read-only follow-up probe: what does the GAME expect from amdaemon?

Prints (a) the game's kernel32 imports related to shared memory / mutexes,
(b) whether the daemon's IPC object-name formats appear in the game,
(c) every game string that mentions the IPC types,
(d) the drive-letter and launch-arg literals.

Writes nothing.  Usage: python ipc_probe.py
"""
import re
import struct

G = r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
GAME = G + r"\App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero-Win64-Shipping.exe"
DAEMON = G + r"\App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe"

IPC_NAMES = [b"ALIVEHANDLEMUTEX", b"ALIVEVALUEMUTEX", b"LOGHANDLEMUTEX", b"LOGVALUEMUTEX",
             b"LEDHANDLEMUTEX", b"LEDVALUEMUTEX", b"LIBMUTEX", b"LPNTDBMUTEX",
             b"LPNTNFCMUTEX", b"COMPMUTEX", b"NFCBufMutex", b"R/W MUTEX", b"DBMUTEX",
             b"DaemonSharedPart", b"ClientRequest", b"ServerResponse", b"DaemonShared",
             b"amdaemon::ipc::", b"amdaemon::server::", b"amdaemon::process::"]

KM32_API = [b"CreateFileMapping", b"OpenFileMapping", b"MapViewOfFile", b"UnmapViewOfFile",
            b"CreateMutex", b"OpenMutex", b"CreateEvent", b"OpenEvent", b"CreateSemaphore",
            b"CreateProcess", b"ReadProcessMemory", b"DuplicateHandle", b"ConnectNamedPipe",
            b"CreateNamedPipe", b"CreateFileA", b"CreateFileW"]

LITERALS = [rb"X:\\", rb"Y:\\", rb"D:\\", rb"AMFS", rb"amfs_share", rb"SDET",
            rb"-launch", rb"launch=", rb"cabinet", rb"DebugWithoutAMDaemon",
            rb"AMDaemon", rb"amdaemon.exe", rb"appfile", rb"sysfile"]


def load(p):
    with open(p, "rb") as f:
        return f.read()


def strings(data):
    asc = re.findall(rb"[\x20-\x7e]{4,}", data)
    u16 = b"".join(re.findall(rb"(?:[\x20-\x7e]\x00){4,}", data)).replace(b"\x00", b"")
    return asc, u16


def imports(path):
    d = load(path[:0] + path) if False else None
    with open(path, "rb") as f:
        head = f.read(0x1000)
    e = struct.unpack_from("<I", head, 0x3C)[0]
    opt = e + 24
    optsz = struct.unpack_from("<H", head, e + 20)[0]
    nsec = struct.unpack_from("<H", head, e + 6)[0]
    st = opt + optsz
    sects = []
    for i in range(nsec):
        o = st + 40 * i
        nm = head[o:o + 8].rstrip(b"\0").decode("latin1")
        vs, va, rs, rp = struct.unpack_from("<IIII", head, o + 8)
        sects.append((nm, va, vs, rp, rs))
    dd = opt + 112
    iva, isz = struct.unpack_from("<II", head, dd + 8)
    for nm, va, vs, rp, rs in sects:
        if va <= iva < va + max(vs, rs):
            off = rp + (iva - va)
            break
    else:
        return []
    out = []
    with open(path, "rb") as f:
        for k in range(64):
            f.seek(off + 20 * k)
            ent = f.read(20)
            if len(ent) < 20 or ent == b"\0" * 20:
                break
            nrva = struct.unpack_from("<I", ent, 12)[0]
            for nm, va, vs, rp, rs in sects:
                if va <= nrva < va + max(vs, rs):
                    f.seek(rp + (nrva - va))
                    out.append(f.read(64).split(b"\0")[0].decode("latin1"))
                    break
    return out


def main():
    for label, path in (("GAME", GAME), ("DAEMON", DAEMON)):
        print("=" * 74)
        print("### %s" % label)
        data = load(path)
        asc, u16 = strings(data)
        blob = b"\n".join(asc) + b"\n" + u16
        print("  -- IPC object-name / type markers --")
        for n in IPC_NAMES:
            c = blob.count(n)
            if c:
                print("     %-24s %d" % (n.decode(), c))
        print("  -- own kernel32 import names of interest --")
        imps = imports(path)
        hits = sorted({i for i in imps if any(a.decode().lower() in i.lower() for a in KM32_API)})
        # import names are DLLs here; we need the function names instead
        print("     (import DLLs: %d)" % len(imps))
        for a in KM32_API:
            for enc in (a, a + b"A", a + b"W"):
                c = blob.count(enc + b"\x00")
                if c:
                    print("     %-22s %d" % (enc.decode(), c))
        print("  -- literals --")
        for lit in LITERALS:
            c = blob.count(lit)
            if c:
                print("     %-24s %d" % (lit.decode(), c))
        print("  -- sample strings containing 'ipc::' --")
        s = [x for x in asc if b"ipc::" in x]
        print("     count=%d" % len(s))
        for x in s[:8]:
            print("       %s" % x[:140].decode("latin1"))


if __name__ == "__main__":
    main()

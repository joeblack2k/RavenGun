#!/usr/bin/env python3
"""Read-only string probe for HOTD Scarlet Dawn.

Answers: what does the GAME expect from amdaemon, and what does the DAEMON expose?
Reads the patched game exe and the daemon over the SMB mount; writes nothing.

Usage: python string_probe.py
"""
import re
import os

G = r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
GAME = G + r"\App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64\Hodzero-Win64-Shipping.exe"
DAEMON = G + r"\App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe"

PATTERNS = [
    # the daemon / its IPC
    r"amdaemon", r"AMDaemon", r"AM Daemon", r"DaemonShared", r"makeDaemonShared",
    r"SharedDataList", r"ALIVEHANDLEMUTEX", r"LIBMUTEX", r"DBMUTEX", r"NFCBufMutex",
    # bypass / debug switches
    r"DebugWithoutAMDaemon", r"WithoutAMDaemon", r"UseAMDaemon", r"CheckUseAMDaemon",
    # ALLS platform
    r"AMFS", r"amfs", r"sysfile", r"appfile", r"SDET", r"ACA0", r"ALLS",
    # keychip / dongle / aime
    r"[Kk]eychip", r"[Dd]ongle", r"aime", r"AiMe", r"FddDriver", r"alpb",
    # security / shell
    r"TFM", r"SECURITY_CHALLENGE", r"SHELL_MSG",
    # network
    r"ALL\.?Net", r"allnet", r"PowerOn", r"naominet", r"40100", r"40106",
    # launch switches of the batch chain
    r"-launch", r"cabinet", r"UNATTENDED", r"NotInstalled", r"ABSLOG",
    # file system roots
    r"X:\\\\", r"Y:\\\\", r"D:\\\\",
    # registry
    r"System\\\\SEGA", r"SystemProperty",
]


def load(path):
    with open(path, "rb") as f:
        return f.read()


def ascii_strings(data, minlen=5):
    return re.findall(rb"[\x20-\x7e]{%d,}" % minlen, data)


def utf16_strings(data, minlen=4):
    return re.findall(rb"(?:[\x20-\x7e]\x00){%d,}" % minlen, data)


def probe(label, path):
    print("=" * 76)
    print("### %s" % label)
    print("    %s" % path)
    data = load(path)
    print("    size=%d" % len(data))
    asc = ascii_strings(data)
    u16 = utf16_strings(data)
    blob = (b"\n".join(asc) + b"\n" + b"".join(u16))
    print("    ascii>=5 strings: %d   utf16>=4 strings: %d" % (len(asc), len(u16)))
    for p in PATTERNS:
        pat = re.compile(p.encode())
        n = len(pat.findall(blob))
        if n:
            print("    %-26s %5d" % (p, n))
    # a few full-length examples for the most informative names
    print("    --- context lines ---")
    for needle in (b"amdaemon", b"DebugWithoutAMDaemon", b"AMFS", b"Keychip",
                   b"aime", b"-launch", b"cabinet", b"SDET", b"FddDriver"):
        hits = [s for s in asc + u16 if needle in s]
        seen = set()
        out = []
        for h in hits:
            if h in seen:
                continue
            seen.add(h)
            out.append(h[:150])
            if len(out) >= 6:
                break
        if out:
            print("      [%s]" % needle.decode())
            for o in out:
                print("         %s" % o.decode("latin1"))


if __name__ == "__main__":
    probe("PATCHED GAME EXE", GAME)
    probe("AM DAEMON", DAEMON)

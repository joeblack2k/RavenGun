#!/usr/bin/env python3
"""Read-only probe: candidate default mount/paths inside amdaemon.exe.

Only looks at extracted printable strings (not raw bytes), so no code noise.
Writes nothing.  Usage: python mount_probe2.py
"""
import re

DAEMON = (r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
          r"\App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe")

NEEDLES = [b"mount", b"Mount", b"AMFS", b"amfs", b"AppData", b"appdata",
           b"SystemProperty", b"DriveLetter", b"amfs_share", b"Option",
           b"Static", b"\\\\?\\", b"Volume", b"GetLogicalDrive", b"QueryDosDevice"]


def main():
    with open(DAEMON, "rb") as f:
        data = f.read()
    asc = re.findall(rb"[\x20-\x7e]{4,}", data)
    u16raw = re.findall(rb"(?:[\x20-\x7e]\x00){4,}", data)
    u16 = [x.replace(b"\x00", b"") for x in u16raw]
    allstr = asc + u16

    print("=== strings that look like an absolute Windows path ===")
    pat = re.compile(rb"^[A-Za-z]:\\")
    for s in sorted({x for x in allstr if pat.match(x)}):
        print("   %s" % s[:130].decode("latin1"))

    print()
    print("=== strings containing a backslash and a known mount word ===")
    for n in NEEDLES:
        hits = sorted({x for x in allstr if n in x and b"\\" in x})
        if hits:
            print("  [%s]" % n.decode())
            for h in hits[:12]:
                print("      %s" % h[:130].decode("latin1"))

    print()
    print("=== the raw mount / SystemProperty strings (any) ===")
    for n in (b"SystemProperty", b"mount", b"Mount", b"AMFS", b"amfs"):
        hits = sorted({x for x in allstr if n in x})
        print("  [%s] %d" % (n.decode(), len(hits)))
        for h in hits[:20]:
            print("      %s" % h[:130].decode("latin1"))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Read-only: the complete wide-string table of hwvalue.exe.

hwvalue.exe is only 202 KB and is a genuine client of the same amyDongle/amPlatform
ABI as amdaemon.exe.  Its usage/help text therefore documents the keychip query
surface in plain English, and its value names name the registry contract.

Writes nothing.  Usage: python hwvalue_dump.py
"""
import re

HWV = (r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
       r"\App\Hodzero\WindowsNoEditor\AMDaemon\hwvalue.exe")


def main():
    with open(HWV, "rb") as f:
        data = f.read()
    ws = [(m.start(), m.group().replace(b"\x00", b"").decode("latin1"))
          for m in re.finditer(rb"(?:[\x20-\x7e]\x00){3,}", data)]
    print("hwvalue.exe wide strings: %d" % len(ws))
    for off, s in ws:
        print("  %#08x  %s" % (off, s[:170]))


if __name__ == "__main__":
    main()

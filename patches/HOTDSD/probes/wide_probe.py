#!/usr/bin/env python3
"""Read-only UTF-16LE probe of the SEGA keychip / AiMe / authenticity surface.

The daemon keeps some of its driver and adapter names as wide strings, which an
ASCII-only sweep misses.  This prints every wide string that mentions the keychip
driver, the AiMe reader adapters, the device namespace or Windows' signature API,
with a context window, plus the same for hwvalue.exe.

Writes nothing.  Usage: python wide_probe.py
"""
import re

G = r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
TARGETS = [
    ("amdaemon.exe", G + r"\App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe"),
    ("hwvalue.exe", G + r"\App\Hodzero\WindowsNoEditor\AMDaemon\hwvalue.exe"),
]

KEYS = [r"aime", r"thincat", r"indicdll", r"Fdd", r"WinTrust", r"WinVerify",
        r"\?\?", r"DosDevices", r"DeviceIoControl", r"IoControl", r"IOCTL",
        r"driver", r"Driver", r"\.dll", r"\\\.\\", r"GUID", r"Interface",
        r"keychip", r"Keychip", r"dongle", r"Dongle", r"NFC", r"card",
        r"Usb", r"USB", r"ICF", r"icf", r"sgsegaboot", r"auth", r"Auth"]


def wide_strings(data, minlen=4):
    out = []
    for m in re.finditer(rb"(?:[\x20-\x7e]\x00){%d,}" % minlen, data):
        out.append((m.start(), m.group().replace(b"\x00", b"").decode("latin1")))
    return out


def main():
    for label, path in TARGETS:
        with open(path, "rb") as f:
            data = f.read()
        ws = wide_strings(data)
        print("=" * 76)
        print("### %s : %d wide strings" % (label, len(ws)))
        hits = [(i, o, s) for i, (o, s) in enumerate(ws)
                if any(re.search(k, s) for k in KEYS)]
        print("--- %d matching wide strings ---" % len(hits))
        seen = set()
        for i, o, s in hits:
            for j in range(max(0, i - 2), min(len(ws), i + 3)):
                if j in seen:
                    continue
                seen.add(j)
                print("  %#010x  %s" % (ws[j][0], ws[j][1][:180]))


if __name__ == "__main__":
    main()

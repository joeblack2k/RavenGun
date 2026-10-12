#!/usr/bin/env python3
"""Read-only probe of the SEGA keychip/dongle surface.

1. amdaemon.exe: every string that mentions the dongle, the keychip, the driver
   layer (`amy*`), NVRAM, the device path, or the billing/ALL.Net layer -- WITH a
   context window, because the order of strings in a PE often mirrors the order of
   the protocol steps.
2. hwvalue.exe: the complete string table (it is only 202 KB) -- it is the tool the
   SEGA boot uses to fill HKLM\\SYSTEM\\SEGA\\SYSTEMPROPERTY, so it shows exactly
   which hardware values exist and where they come from.

Writes nothing; prints to stdout.

Usage: python keychip_probe.py
"""
import re
import sys

G = r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
AMD = G + r"\App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe"
HWV = G + r"\App\Hodzero\WindowsNoEditor\AMDaemon\hwvalue.exe"

# keywords that mark the keychip/dongle/AiMe/billing surface
KEYS = [b"dongle", b"Dongle", b"DONGLE", b"keychip", b"Keychip", b"KEYCHIP",
        b"amy", b"Amy", b"AMY", b"FddDriver", b"fdd",
        b"nvram", b"Nvram", b"NVRAM", b"geometry", b"Geometry",
        b"encrypt", b"Encrypt", b"decrypt", b"Decrypt",
        b"alpb", b"alib", b"alabex", b"amMaster", b"amsSystemProperty",
        b"poweron", b"PowerOn", b"billing", b"Billing",
        b"aime", b"Aime", b"card", b"Card", b"odometer", b"playlimit",
        b"serial", b"Serial", b"icf", b"Icf", b"ICF",
        b"region", b"Region", b"modeltype", b"ModelType", b"dipsw", b"DipSwitch"]


def strings_with_offsets(data, minlen=4):
    out = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, data):
        out.append((m.start(), m.group().decode("latin1")))
    return out


def main():
    with open(AMD, "rb") as f:
        daemon = f.read()
    ss = strings_with_offsets(daemon)
    print("### amdaemon.exe: %d ascii strings" % len(ss))

    print("\n=== A. all dongle/keychip/driver strings, in file order ===")
    for off, s in ss:
        hit = any(k.decode("latin1") in s for k in KEYS)
        if hit and (b"Dongle" in s.encode() or b"dongle" in s.encode()
                    or b"Keychip" in s.encode() or b"keychip" in s.encode()
                    or s.startswith("amy") or "Fdd" in s):
            print("  %#010x  %s" % (off, s[:200]))

    print("\n=== B. context windows around the driver layer (amy*/FddDriver) ===")
    idx = [i for i, (o, s) in enumerate(ss)
           if s.startswith("amy") or "FddDriver" in s or "amyDriver" in s]
    printed = set()
    for i in idx:
        for j in range(max(0, i - 4), min(len(ss), i + 5)):
            if j in printed:
                continue
            printed.add(j)
            print("  %#010x  %s" % (ss[j][0], ss[j][1][:200]))

    print("\n=== C. context windows around NVRAM / geometry / crypt ===")
    idx = [i for i, (o, s) in enumerate(ss)
           if any(k.decode("latin1") in s for k in (b"nvram", b"Nvram", b"NVRAM",
                                                    b"geometry", b"Geometry"))]
    printed = set()
    for i in idx[:12]:
        for j in range(max(0, i - 3), min(len(ss), i + 4)):
            if j in printed:
                continue
            printed.add(j)
            print("  %#010x  %s" % (ss[j][0], ss[j][1][:200]))

    print("\n=== D. ALL.Net / billing / amMaster / alpb ===")
    for off, s in ss:
        if any(k.decode("latin1") in s for k in (b"alpb", b"alAbEx", b"amMaster",
                                                 b"allnet", b"ALLNet", b"PowerOn",
                                                 b"poweron", b"billing", b"Billing",
                                                 b"odometer", b"playlimit")):
            print("  %#010x  %s" % (off, s[:220]))

    print("\n=== E. registry / system property surface ===")
    for off, s in ss:
        if "SystemProperty" in s or "SYSTEMPROPERTY" in s or "SYSTEM\\SEGA" in s:
            print("  %#010x  %s" % (off, s[:220]))

    print("\n=== F. dlls the daemon loads at runtime ===")
    for off, s in ss:
        if s.lower().endswith(".dll") and "\\" not in s and "/" not in s:
            print("  %#010x  %s" % (off, s[:120]))

    print("\n\n### hwvalue.exe: complete string table")
    with open(HWV, "rb") as f:
        hw = f.read()
    hs = strings_with_offsets(hw, 4)
    print("(%d strings)" % len(hs))
    for off, s in hs:
        print("  %#08x  %s" % (off, s[:200]))


if __name__ == "__main__":
    main()

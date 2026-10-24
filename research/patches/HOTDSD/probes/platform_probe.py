#!/usr/bin/env python3
"""Read-only: what does am::platform::Manager::initialize() look for?

The daemon aborts in amPlatformGetPlatformInformation() after 1 second, before the
keychip and before the mount letters.  This pulls every string in amdaemon.exe that
belongs to the amPlatform / device-enumeration surface, including device-interface
GUIDs and device-class names, so we can see whether the check is data we can present.

Writes nothing.  Usage: python platform_probe.py
"""
import re

G = r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
AMD = G + r"\App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe"
HWV = G + r"\App\Hodzero\WindowsNoEditor\AMDaemon\hwvalue.exe"

KEYS = [b"amPlatform", b"Platform", b"platformId", b"platform_id",
        b"DEVINTERFACE", b"DevInterface", b"DeviceInterface",
        b"SetupDi", b"SetupAPI", b"Setupapi", b"CM_", b"cfgmgr",
        b"GUID", b"Guid", b"{", b"Enum", b"device", b"Device",
        b"NVAPI", b"nvapi", b"GPU", b"gpu", b"Memory", b"memory",
        b"SubStorage", b"sub_storage", b"LaneWidth", b"lane",
        b"SysInfo", b"SystemInfo", b"Board", b"board",
        b"ACA0", b"ACA1", b"ALLS", b"ModelType", b"model"]

GUID_RE = re.compile(rb"\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
                     rb"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}")


def strings(data, minlen=4):
    asc = re.findall(rb"[\x20-\x7e]{%d,}" % minlen, data)
    wide = [x.replace(b"\x00", b"")
            for x in re.findall(rb"(?:[\x20-\x7e]\x00){%d,}" % minlen, data)]
    return asc, wide


def report(label, path):
    with open(path, "rb") as f:
        data = f.read()
    asc, wide = strings(data)
    print("=" * 78)
    print("### %s (%d bytes, %d ascii + %d wide strings)" % (label, len(data), len(asc), len(wide)))
    allstr = asc + wide

    # 1. GUIDs are the real prize: a device interface GUID tells us exactly what the
    #    platform manager is enumerating.
    guids = sorted({m.group().decode() for m in
                    (GUID_RE.search(s) for s in allstr) if m})
    print("-- device-interface GUIDs (%d):" % len(guids))
    for g in guids:
        print("     %s" % g)

    # 2. the amPlatform / device-enumeration function and message surface
    print("-- amPlatform / enumeration surface:")
    for s in allstr:
        if s.startswith(b"amPlatform") or s.startswith(b"amSys") or b"DEVINTERFACE" in s \
           or s.startswith(b"amsSystemProperty"):
            print("     %s" % s[:170].decode("latin1"))

    # 3. anything naming a device class or a hardware value
    print("-- hardware/platform value names and messages:")
    seen = set()
    for s in allstr:
        for k in (b"memory", b"Memory", b"graphic", b"Graphic", b"lane", b"Lane",
                  b"platformId", b"platform_id", b"sub-storage", b"sub_storage",
                  b"SubStorage", b"modeltype", b"ModelType", b"ACA0", b"ACA1"):
            if k in s and s not in seen and 4 < len(s) < 140:
                seen.add(s)
                print("     [%s] %s" % (k.decode("latin1"), s.decode("latin1")))
                break


if __name__ == "__main__":
    report("amdaemon.exe", AMD)
    report("hwvalue.exe", HWV)

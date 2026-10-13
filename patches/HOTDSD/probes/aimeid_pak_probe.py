#!/usr/bin/env python3
"""Read-only probes for the aimeId error and the map/case question.

A. amdaemon-side and game-side strings for the aimeId failure: the validator's format,
   the fields it checks, and the AMDaemon plugin's card/login surface.
B. the UE4 pak index.  The index holds the file NAMES the pak serves, so reading it
   answers two things at once: which case the pak stores paths in, and whether the
   loose Content/Hodzero_V1/ADX content is also inside the pak.

Writes nothing.  Usage: python aimeid_pak_probe.py
"""
import re
import struct

G = r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
GAME = (G + r"\App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64"
        r"\Hodzero-Win64-Shipping.exe")
DAEMON = G + r"\App\Hodzero\WindowsNoEditor\AMDaemon\amdaemon.exe"
PAK = (G + r"\App\Hodzero\WindowsNoEditor\Hodzero\Content\Paks"
       r"\Hodzero-WindowsNoEditor.pak")

AIME_KEYS = [b"aimeId", b"aimeid", b"aime_id", b"aime_serial_number", b"AimeId",
             b"aime", b"Aime", b"AiMe",
             b"is invalid", b"invalid.", b"Exception caused",
             b"LogAMDaemon", b"AMDaemon", b"AMDaemonPlugin",
             b"GetAimeId", b"SetAimeId", b"Login", b"LockGamePlayMode",
             b"card", b"Card", b"CardId", b"card_id",
             b"access_code", b"AccessCode", b"Access Code"]


def strings(path, minlen=4):
    with open(path, "rb") as f:
        data = f.read()
    asc = re.findall(rb"[\x20-\x7e]{%d,}" % minlen, data)
    wide = [x.replace(b"\x00", b"")
            for x in re.findall(rb"(?:[\x20-\x7e]\x00){%d,}" % minlen, data)]
    return asc, wide


def part_a():
    for label, path in (("GAME", GAME), ("AM DAEMON", DAEMON)):
        asc, wide = strings(path)
        blob = b"\n".join(asc) + b"\n" + b"\n".join(wide)
        print("=" * 78)
        print("### A. %s -- aime / validator / login surface" % label)
        for k in AIME_KEYS:
            c = blob.count(k)
            if c:
                print("   %-22s %d" % (k.decode("latin1"), c))
        print("   -- the validator and the aime fields, with context --")
        for needle in (b"is invalid", b"aimeId", b"aime_id", b"aimeid",
                       b"aime_serial_number", b"Exception caused", b"LogAMDaemon"):
            hits = sorted({x for x in asc + wide if needle in x})
            for h in hits[:8]:
                print("      [%s] %s" % (needle.decode("latin1"), h[:170].decode("latin1")))
        print()


def part_b():
    print("=" * 78)
    print("### B. the pak index: which paths does the pak serve?")
    import os
    size = os.path.getsize(PAK)
    with open(PAK, "rb") as f:
        f.seek(size - 44)
        foot = f.read(44)
    magic, ver = struct.unpack_from("<Ii", foot, 0)
    ioff, isize = struct.unpack_from("<qq", foot, 8)
    print("   footer: magic=%#x version=%d index_offset=%d index_size=%d"
          % (magic, ver, ioff, isize))
    with open(PAK, "rb") as f:
        f.seek(ioff)
        idx = f.read(isize)
    print("   read %d bytes of index" % len(idx))
    strings_in_index = re.findall(rb"[\x20-\x7e]{5,}", idx)
    print("   %d ascii strings of >=5 chars in the index" % len(strings_in_index))

    def count_with(needle):
        n = sum(1 for s in strings_in_index if needle in s)
        return n

    for needle in (b"ADX", b"ADX/", b".usm", b".uasset", b"Fonts", b"HodZero_v1",
                   b"Hodzero_V1", b"Hodzero", b"Engine/Content", b"Content/",
                   b"Stage0", b"SL_", b".ufont", b".pak"):
        print("   %-18s %d" % (needle.decode("latin1"), count_with(needle)))

    print("   -- sample index strings that look like content paths --")
    shown = 0
    for s in strings_in_index:
        if b"/" in s and shown < 40:
            print("      %s" % s[:150].decode("latin1"))
            shown += 1
    print("   -- how many index strings contain 'ADX' --")
    for s in strings_in_index:
        if b"ADX" in s:
            print("      %s" % s[:150].decode("latin1"))


if __name__ == "__main__":
    try:
        import sys
        sys.stdout.reconfigure(errors="replace")
    except Exception:
        pass
    part_a()
    part_b()

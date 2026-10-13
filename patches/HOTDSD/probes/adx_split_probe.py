#!/usr/bin/env python3
"""Read-only follow-up: the ADX content split between pak and loose files, the case the
CRIWARE manifest uses for the movies, and the game-side aimeId fields.

Answers: is the loose Content/Hodzero_V1/ADX set also inside the pak, which files exist
ONLY loose (those are the case-sensitive risk), and what the CRI manifest calls them.

Writes nothing.  Usage: python adx_split_probe.py
"""
import os
import re
import struct
from collections import Counter

G = r"Z:\Teknoparrot\House of the Dead Scarlet Dawn"
PAK = (G + r"\App\Hodzero\WindowsNoEditor\Hodzero\Content\Paks"
       r"\Hodzero-WindowsNoEditor.pak")
ADX = G + r"\App\Hodzero\WindowsNoEditor\Hodzero\Content\Hodzero_V1\ADX"
ACF = ADX + r"\NewProject.acf"

# ---------------------------------------------------------------- pak index
size = os.path.getsize(PAK)
with open(PAK, "rb") as f:
    f.seek(size - 44)
    foot = f.read(44)
ioff, isize = struct.unpack_from("<qq", foot, 8)
with open(PAK, "rb") as f:
    f.seek(ioff)
    idx = f.read(isize)
idxstrings = re.findall(rb"[\x20-\x7e]{5,}", idx)

adx_entries = [s for s in idxstrings if b"ADX/" in s or b"ADX\\" in s]
print("### pak index: %d entries, of which %d mention ADX/"
      % (len(idxstrings), len(adx_entries)))
ext = Counter()
for s in adx_entries:
    m = re.search(rb"\.([A-Za-z0-9]{2,6})$", s)
    ext[m.group(1).lower().decode() if m else "(no ext)"] += 1
print("   extensions of the pak's ADX entries:")
for k, v in ext.most_common():
    print("      %-10s %d" % (k, v))

print("\n   -- the exact spelling the pak uses for that folder --")
spellings = Counter()
for s in idxstrings:
    if b"ADX/" in s:
        m = re.search(rb"Content/([A-Za-z0-9_]+)/ADX/", s)
        if m:
            spellings[m.group(1).decode()] += 1
for k, v in spellings.most_common():
    print("      Content/%s/ADX/   %d entries" % (k, v))

# ---------------------------------------------------------------- loose files
loose = []
for dirpath, dirnames, filenames in os.walk(ADX):
    for fn in filenames:
        loose.append((os.path.relpath(os.path.join(dirpath, fn), ADX),
                      os.path.getsize(os.path.join(dirpath, fn))))
lext = Counter(os.path.splitext(n)[1].lower() or "(no ext)" for n, _ in loose)
print("\n### loose files in Content/Hodzero_V1/ADX: %d" % len(loose))
for k, v in lext.most_common():
    print("      %-10s %d" % (k, v))

# which loose names does the pak ALSO have (by basename)?
pak_basenames = set()
for s in adx_entries:
    pak_basenames.add(os.path.basename(s.decode("latin1")).lower())
only_loose = [(n, sz) for n, sz in loose
              if os.path.basename(n).lower() not in pak_basenames]
print("\n   loose files whose basename is NOT in the pak's ADX set: %d" % len(only_loose))
oext = Counter(os.path.splitext(n)[1].lower() for n, _ in only_loose)
for k, v in oext.most_common():
    print("      %-10s %d" % (k, v))
print("   examples:")
for n, sz in sorted(only_loose, key=lambda x: -x[1])[:12]:
    print("      %10d  %s" % (sz, n))

# ---------------------------------------------------------------- the CRI manifest
print("\n### NewProject.acf (CRI, %d bytes): what names does it contain?"
      % os.path.getsize(ACF))
with open(ACF, "rb") as f:
    blob = f.read()
print("   first 16 bytes: %s" % blob[:16].hex(" "))
for k in (b".usm", b"usm", b"ADX", b"HodZero", b"Hodzero", b"Movie", b"movie",
          b".acb", b".awb", b"NewProject"):
    print("   %-12s %d" % (k.decode(), len(re.findall(re.escape(k), blob))))
print("   -- printable strings of >=6 chars in the acf --")
for s in re.findall(rb"[\x20-\x7e]{6,}", blob)[:40]:
    print("      %s" % s[:120].decode("latin1"))

# ---------------------------------------------------------------- game-side fields
print("\n### the game exe: every string that mentions aime or the card identity")
GAME = (G + r"\App\Hodzero\WindowsNoEditor\Hodzero\Binaries\Win64"
        r"\Hodzero-W64-Shipping.exe".replace("W64", "Win64"))
with open(GAME, "rb") as f:
    gd = f.read()
asc = re.findall(rb"[\x20-\x7e]{4,}", gd)
wide = [x.replace(b"\x00", b"")
        for x in re.findall(rb"(?:[\x20-\x7e]\x00){4,}", gd)]
seen = set()
for s in asc + wide:
    if re.search(rb"aime", s, re.I) and s not in seen and len(s) < 120:
        seen.add(s)
        print("   %s" % s.decode("latin1"))

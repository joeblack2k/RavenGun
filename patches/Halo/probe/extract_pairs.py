#!/usr/bin/env python3
"""Walk the interleaved (name, version) string blob the game uses for its own
runtime symbol resolution. Read-only."""
import sys, collections

path = sys.argv[1]
start = int(sys.argv[2], 16)
end = int(sys.argv[3], 16)
with open(path, "rb") as f:
    f.seek(start)
    d = f.read(end - start)

cur, off = [], start
pairs = []
i = 0
while i < len(d):
    j = d.find(b"\0", i)
    if j < 0:
        break
    s = d[i:j]
    if not s:
        i = j + 1
        continue
    try:
        txt = s.decode("ascii")
    except UnicodeDecodeError:
        break
    pairs.append((i + start, txt))
    i = j + 1

# keep only the leading run of strings that look like symbol/version names
def looks(t):
    if t.startswith("GLIBC_") or t.startswith("LIBUDEV_") or t.startswith("GLIBCXX_"):
        return True
    if t.startswith("_Z") or t.startswith("_ITM") or t.startswith("__"):
        return True
    return all(33 <= ord(c) <= 126 for c in t) and 1 <= len(t) <= 200

names = []
i = 0
while i < len(pairs) and looks(pairs[i][1]):
    names.append(pairs[i][1])
    i += 1
print("== %s  blob 0x%x..0x%x" % (path, start, end))
print("   parsed strings: %d ; accepted: %d" % (len(pairs), len(names)))
print("   first: %s" % names[:6])
print("   last : %s" % names[-6:])
c = collections.Counter()
for n in names:
    if n.startswith(("GLIBC_", "LIBUDEV_", "GLIBCXX_")):
        c["VERSION"] += 1
    elif n.startswith(("_ZNK4FMOD", "_ZN4FMOD")):
        c["FMOD-cxx"] += 1
    elif n.startswith("FMOD_"):
        c["FMOD-c"] += 1
    elif n.startswith("gl") or n.startswith("glu"):
        c["GL"] += 1
    elif n.startswith(("udev_", "libudev")):
        c["udev"] += 1
    elif n.startswith("X"):
        c["X11"] += 1
    elif n.startswith("iw_"):
        c["iw"] += 1
    else:
        c["other/libc"] += 1
print("   groups: %s" % dict(c))
print("   -- FMOD names (%d) --" % (c["FMOD-cxx"] + c["FMOD-c"]))
for n in names:
    if "FMOD" in n:
        print("      %s" % n)
print("   -- GL names (%d) --" % c["GL"])
gl = [n for n in names if n.startswith("gl")]
print("      %s" % gl[:60])

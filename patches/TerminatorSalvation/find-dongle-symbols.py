#!/usr/bin/env python3
# find-dongle-symbols.py - static, READ-ONLY, starts nothing.
#
# Finds the dongle / HASP-HL functions inside the STRIPPED `game` by fingerprinting them against the
# SAME functions in `ServiceDongleApp`, which is unstripped and carries full DWARF.
#
# Method: take a 256-byte window at the named function's entry, wildcard every 4-byte little-endian
# value that is a plausible address in EITHER binary (absolute immediates and jump targets move
# between builds), then scan `game`'s executable segment accepting a position when at most
# MAXMISS of the non-wildcard bytes differ. That tolerates small cross-build drift.
#
# Success = the entry points named below are reported with an address in `game`.
import struct, subprocess, re, sys

T = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
SDA  = T + "/ServiceDongleApp/ServiceDongleApp"
GAME = T + "/game"
WIN    = 256
MAXMISS = 0.06      # accept <=6% differing non-wildcard bytes

def load(path):
    d = open(path, "rb").read()
    (et,em,ev,entry,phoff,shoff,ef,ehsz,phesz,phnum,shesz,shnum,shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", d, 16)
    ph = []
    for i in range(phnum):
        t,off,va,pa,fsz,msz,fl,al = struct.unpack_from("<IIIIIIII", d, phoff+i*phesz)
        ph.append((t,off,va,fsz,msz,fl))
    segs = [(va,off,fsz) for (t,off,va,fsz,msz,fl) in ph if t==1 and (fl & 1)]
    return d, segs, ph

sda, sda_segs, sda_ph = load(SDA)
game, g_segs, g_ph = load(GAME)

def bytes_at(d, segs, va, n):
    for v,o,sz in segs:
        if v <= va < v+sz:
            k = min(n, v+sz-va)
            return d[o+(va-v): o+(va-v)+k]
    return b""

def good_addr(v):
    for (t,o,va,fsz,msz,fl) in sda_ph + g_ph:
        if t==1 and va <= v < va+msz:
            return True
    return False

def wildcard(pat):
    w=[False]*len(pat)
    for i in range(len(pat)-3):
        v = struct.unpack_from("<I", pat, i)[0]
        if good_addr(v):
            for j in range(4): w[i+j]=True
    return w

def named(path, rx=r"hasp|Dongle|Aladdin"):
    out = subprocess.run(["readelf","-sW",path],capture_output=True,text=True).stdout
    res={}
    for ln in out.splitlines():
        f=ln.split()
        if len(f)>=8 and f[3]=="FUNC" and re.search(rx,f[7]):
            a=int(f[1],16)
            if a and f[7] not in res: res[f[7]]=(a,int(f[2]))
    return res

def search(pat, wild, need):
    parts=[i for i in range(len(pat)) if not wild[i]]
    if len(parts) < need: return []
    a0 = parts[0]
    hits=[]
    for v,o,sz in g_segs:
        blob = game[o:o+sz]
        i = blob.find(pat[a0:a0+1])
        while i != -1:
            s = i - a0
            if 0 <= s and s+len(pat) <= len(blob):
                miss = sum(1 for p in parts if blob[s+p] != pat[p])
                if miss <= int(MAXMISS*len(parts)):
                    hits.append((v+s, miss, len(parts)))
            i = blob.find(pat[a0:a0+1], i+1)
    hits.sort(key=lambda h: h[1])
    return hits

fns = named(SDA)
print("named dongle/HASP FUNCs in ServiceDongleApp: %d" % len(fns))
print("searching `game` (window %d B, <=%d%% of the non-wildcard bytes may differ)" % (WIN, MAXMISS*100))
print()
located={}; missing=[]
for name,(va,size) in sorted(fns.items()):
    pat = bytes_at(sda, sda_segs, va, min(size, WIN))
    if len(pat) < 64: continue
    wild = wildcard(pat)
    hits = search(pat, wild, 60)
    if not hits:
        # fallback: the entry may be dominated by address immediates; fingerprint a later window
        for off in range(64, min(size, 1024), 64):
            p2 = bytes_at(sda, sda_segs, va+off, min(128, size-off))
            if len(p2) < 64: break
            w2 = wildcard(p2)
            h2 = search(p2, w2, 48)
            if h2:
                hits = [(h2[0][0]-off, h2[0][1], h2[0][2])]
                break
    if hits:
        located[name] = hits[0][0]
        print("  %-28s SDA 0x%08x (%4dB) -> game 0x%08x   [%d/%d bytes differ]"
              % (name, va, size, hits[0][0], hits[0][1], hits[0][2]))
    else:
        missing.append(name)
print()
print("located in game : %d / %d" % (len(located), len(located)+len(missing)))
print("NOT located     : %s" % ", ".join(missing))
print()
WANT = ["DongleInitialize","DongleInit","DongleClose","DongleIsPresent","DongleGetID",
        "AcquireAladdinDongle","GetAladdinHLPass","DongleFeatureCheck","hasp_login","hasp_read"]
print("=== the entry points the loader/dongle layer needs ===")
ok=True
for w in WANT:
    if w in located: print("  FOUND    %-24s game 0x%08x" % (w, located[w]))
    else:            print("  NOT FOUND %-23s" % w); ok=False
print()
print("NEXT-MEASUREMENT RESULT: %s" % ("SUCCESS - all key entry points located in the stripped `game`"
      if ok else "PARTIAL - see the NOT FOUND list; widen WIN or relax MAXMISS"))
sys.exit(0 if ok else 2)

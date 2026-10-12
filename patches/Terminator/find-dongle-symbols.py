#!/usr/bin/env python3
# find-dongle-symbols.py  (final; STATIC; READ-ONLY; starts nothing)
#
# Builds the dongle / HASP-HL symbol map for the STRIPPED `game`, using the UNSTRIPPED,
# DWARF-bearing `ServiceDongleApp` from the same folder as the source of names. It is the same
# engine, which is why its symbol table is usable as a map for `game`.
#
# Two evidence methods, each hit printed with the evidence it rests on and a confidence grade:
#
#   [BYTES]   a block at (or just inside) the SDA function entry matches `game`. The mismatch ratio
#             is measured over the NON-ADDRESS bytes only: absolute addresses and jump targets move
#             between the two builds and are wildcarded, everything else has to agree.
#   [STRING]  a string referenced inside SDA's function F is referenced in `game` too. The owning SDA
#             function of that xref is looked up in the symbol table, and the instruction block around
#             the two xrefs is compared. On a match the entry projects as
#                 game_entry = game_xref - (sda_xref - sda_entry)
#
# Grades:  A = 0 differing bytes over >= 64 non-address bytes (byte-identical)
#          B = <= 3 % differing
#          C = <= 6 % differing   -> "probable, single method", not proof
#          UNKNOWN = no evidence. Deliberately NOT guessed: 43 with "unknown" beats 58 with guesswork.
#
# Two structural facts this script also establishes (both matter, see the README):
#  * "DongleInitialize() FAILED!" is referenced inside SDA's `main`, so `game`'s xref at 0x080c2cdb
#    is inside `game`'s `main` -- NOT inside DongleInitialize.
#  * "ReInitAladdinDongles called: %d times" and "Too many consexutive dongle errs (%d), bailing"
#    exist ONLY in `game`, not in ServiceDongleApp -> those functions can have no anchor here at all.
import struct, subprocess, re, sys

T    = "/home/deck/mnt/spidernas_emulation/Teknoparrot/Terminator Salvation "
SDA  = T + "/ServiceDongleApp/ServiceDongleApp"
GAME = T + "/game"

def load(p):
    d=open(p,"rb").read()
    (et,em,ev,entry,phoff,shoff,ef,ehsz,phesz,phnum,shesz,shnum,shstrndx)=struct.unpack_from("<HHIIIIIHHHHHH",d,16)
    ph=[]
    for i in range(phnum):
        t,o,v,pa,fsz,msz,fl,al=struct.unpack_from("<IIIIIIII",d,phoff+i*phesz); ph.append((t,o,v,fsz,msz,fl))
    return d,[(v,o,f) for (t,o,v,f,m,fl) in ph if t==1 and (fl&1)],ph
sda,ss,sp=load(SDA); game,gs,gp=load(GAME)
def ba(d,segs,va,n):
    for v,o,sz in segs:
        if v<=va<v+sz: return d[o+(va-v):o+(va-v)+min(n,v+sz-va)]
    return b""
def ini(va,ph): return any(t==1 and v<=va<v+m for (t,o,v,f,m,fl) in ph)
def wc(pat):
    w=[False]*len(pat)
    for i in range(len(pat)-3):
        if ini(struct.unpack_from("<I",pat,i)[0],sp) or ini(struct.unpack_from("<I",pat,i)[0],gp):
            for j in range(4): w[i+j]=True
    return w
def mb(pat,need,mm):
    w=wc(pat); parts=[i for i in range(len(pat)) if not w[i]]
    if len(parts)<need: return None
    a0=parts[0]; best=None
    for v,o,sz in gs:
        blob=game[o:o+sz]; i=blob.find(pat[a0:a0+1])
        while i!=-1:
            s=i-a0
            if 0<=s and s+len(pat)<=len(blob):
                miss=sum(1 for q in parts if blob[s+q]!=pat[q])
                if miss<=int(mm*len(parts)) and (best is None or miss<best[1]): best=(v+s,miss,len(parts))
            i=blob.find(pat[a0:a0+1],i+1)
    return best
def grade(m,tot):
    if m==0: return "A"
    r=100.0*m/tot
    return "B" if r<=3.0 else ("C" if r<=6.0 else None)

out=subprocess.run(["readelf","-sW",SDA],capture_output=True,text=True).stdout
named={}; funcs=[]
for ln in out.splitlines():
    f=ln.split()
    if len(f)>=8 and f[3]=="FUNC":
        try: a=int(f[1],16)
        except ValueError: continue
        if a: funcs.append((a,f[7]))
        if a and re.search(r"hasp|Dongle|Aladdin",f[7]): named.setdefault(f[7],(a,int(f[2])))
funcs.sort()
def owner(addr):
    b=None
    for a,n in funcs:
        if a<=addr and (b is None or a>b[0]): b=(a,n)
    return b

_d={}
def dis(p,lo,hi,tag):
    k=(tag,lo,hi)
    if k not in _d:
        _d[k]=subprocess.run(["objdump","-d","--no-show-raw-insn","--start-address=%d"%lo,
                              "--stop-address=%d"%hi,p],capture_output=True,text=True).stdout
    return _d[k]
def xrefs(p,lo,hi,va,tag):
    return [(int(m.group(1),16), m.group(2).strip())
            for m in re.finditer(r"^\s*([0-9a-f]+):\t(.*)$",dis(p,lo,hi,tag),re.M)
            if ("0x%x"%va) in m.group(2)]
def vaddr_of(ph,off):
    for (t,o,v,f,m,fl) in ph:
        if t==1 and o<=off<o+f: return v+(off-o)

slo=min(v for v,o,f in ss); shi=max(v+f for v,o,f in ss)
glo=min(v for v,o,f in gs); ghi=max(v+f for v,o,f in gs)
STR=[b"DongleInitialize() FAILED!\n", b"version.txt", b"NO VERSION", b"%d-%d-%d %d:%d:%d",
     b"%s%s", b"dongle\x00", b"nethasp.ini", b"haspformat", b"haspscope"]
strmap=[]
for S in STR:
    so=sda.find(S); go=game.find(S)
    if so<0 or go<0: continue
    sv=vaddr_of(sp,so); gv=vaddr_of(gp,go)
    strmap.append((S, xrefs(SDA,slo,shi,sv,"s"), gv, xrefs(GAME,glo,ghi,gv,"g")))

print("="*104)
print("Dongle / HASP-HL symbol map for the STRIPPED `game`  --  proof-based, graded")
print("  names from : %s  (unstripped, DWARF)" % SDA.split("/")[-1])
print("  target     : %s  (stripped)" % GAME.split("/")[-1])
print("="*104)

results={}
for name,(va,size) in sorted(named.items()):
    best=None
    pat=ba(sda,ss,va,min(size,256))
    if len(pat)>=64:
        h=mb(pat,60,0.06)
        if h and grade(h[1],h[2]):
            best=(h[0], grade(h[1],h[2]),
                  "BYTES: entry window %dB, %d/%d non-address bytes differ (%.1f%%)"
                  % (len(pat),h[1],h[2],100.0*h[1]/h[2]))
    if not best:
        for o2 in range(64,min(size,1024),64):
            p2=ba(sda,ss,va+o2,min(128,size-o2))
            if len(p2)<64: break
            h2=mb(p2,48,0.06)
            if h2 and grade(h2[1],h2[2]):
                best=(h2[0]-o2, grade(h2[1],h2[2]),
                      "BYTES: interior window +0x%x (%dB), %d/%d differ (%.1f%%), projected back to the entry"
                      % (o2,len(p2),h2[1],h2[2],100.0*h2[1]/h2[2]))
                break
    for (S, sx, gv, gx) in strmap:
        got=None
        for (xa,ins) in sx:
            ow=owner(xa)
            if not ow or ow[0]!=va: continue
            blk=ba(sda,ss,xa,128)
            if len(blk)<64: continue
            h=mb(blk,40,0.06)
            if h and grade(h[1],h[2]):
                got=(h[0]-(xa-va), grade(h[1],h[2]),
                     "STRING: owns the reference to %r; the %dB instruction block around that xref matches "
                     "game 0x%08x with %d/%d differ (%.1f%%); entry = game_xref - 0x%x"
                     % (S[:34].decode("latin1"), len(blk), h[0], h[1], h[2], 100.0*h[1]/h[2], xa-va))
                break
        if got and (best is None or got[1] < best[1]): best=got
    if best: results[name]=best

for name in sorted(named):
    if name in results:
        a,g,ev = results[name]
        print("%-30s game 0x%08x   [grade %s]" % (name,a,g))
        print("      - %s" % ev)
    else:
        print("%-30s %s" % (name,"UNKNOWN - no evidence; deliberately not guessed"))
print()
print("-"*104)
print("located with evidence : %d / %d" % (len(results), len(named)))
byg={}
for n,(a,g,e) in results.items(): byg.setdefault(g,[]).append(n)
for g in "ABC":
    if g in byg: print("  grade %s (%2d): %s" % (g, len(byg[g]), ", ".join(sorted(byg[g]))))
unk=[n for n in sorted(named) if n not in results]
print("  UNKNOWN  (%2d): %s" % (len(unk), ", ".join(unk)))
print()
print("structural notes:")
for S in (b"ReInitAladdinDongles called: %d times\n", b"Too many consexutive dongle errs (%d), bailing\n"):
    print("  %-46r in SDA: %-5s | in game: %s" % (S[:44], sda.find(S)>=0, game.find(S)>=0))
fx=xrefs(SDA,slo,shi,vaddr_of(sp,sda.find(b"DongleInitialize() FAILED!\n")),"q")
print("  'DongleInitialize() FAILED!' is referenced inside SDA's %s -> game's xref 0x080c2cdb is in game's %s"
      % (owner(fx[0][0])[1] if fx else "?", "main"))
print()
print("LEADS for the UNKNOWN functions (measured xref addresses; names are INFERRED, not proven):")
for S in (b"ReInitAladdinDongles called: %d times\n", b"Too many consexutive dongle errs (%d), bailing\n",
          b"failed to find aladdin dongle", b"failed to read dongle id", b"Unable to read seed from dongle"):
    go=game.find(S)
    if go<0:
        print("  %-46r  not in game" % S[:44]); continue
    gv=vaddr_of(gp,go)
    xs=xrefs(GAME,glo,ghi,gv,"g")
    print("  %-46r game vaddr 0x%08x -> xref(s): %s"
          % (S[:44], gv, ", ".join("0x%08x" % a for a,_ in xs) or "(none with an immediate operand)"))
sys.exit(0)

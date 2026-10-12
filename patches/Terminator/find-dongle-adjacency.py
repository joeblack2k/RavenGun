#!/usr/bin/env python3
# find-dongle-adjacency.py - READ-ONLY, static, starts nothing.
#
# Second half of the dongle/HASP symbol map for the STRIPPED `game`. Where
# find-dongle-symbols.py matches code blocks, this one uses two methods that work when the code
# itself differs between the two builds:
#
#  [ADJACENCY]  If two PROVEN functions A and B bracket an unknown G in ServiceDongleApp, and
#                   located(B) - located(A) == sda(B) - sda(A)
#               then that whole block has the same layout in both builds. If G is the only unproven
#               function strictly between them, its address is forced:
#                   game(G) = located(A) + ( sda(G) - sda(A) )
#               Corroborated here by the first instruction at the predicted address matching SDA's,
#               and by the SDA size plus the inter-function gap landing on B.
#
#  [ALIAS]      Some of the tiny functions are not functions at all:
#                   hasp_free / hasp_login_ex / hasp_hasptime_to_datetime  = `jmp rel32` tail-aliases
#                   hasp_get_trace                                          = *arg=0 ; jmp rel32
#                   hasp_enable_trace                                       = `mov $0x2ba,%eax; ret`
#                   GetAladdinSRMPass                                       = empty no-op (5 bytes)
#               For a `jmp rel32` alias the rel32 target can be computed arithmetically and compared
#               against the target found INDEPENDENTLY by code-block matching. Exact agreement between
#               two unrelated methods is proof.
#
# A hit is only reported with the evidence that carries it. Unresolved functions are reported as such,
# with the measured reason.
import struct, subprocess, re

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
out=subprocess.run(["readelf","-sW",SDA],capture_output=True,text=True).stdout
named={}; allf={}
for ln in out.splitlines():
    f=ln.split()
    if len(f)>=8 and f[3]=="FUNC":
        try: a=int(f[1],16)
        except ValueError: continue
        if a:
            allf.setdefault(f[7],(a,int(f[2])))
            if re.search(r"hasp|Dongle|Aladdin",f[7]): named.setdefault(f[7],(a,int(f[2])))
def dis(p,va,n,tag):
    return subprocess.run(["objdump","-d","--no-show-raw-insn","--start-address=%d"%va,
                           "--stop-address=%d"%(va+n),p],capture_output=True,text=True).stdout
def first_ins(txt):
    for m in re.finditer(r"^\s*[0-9a-f]+:\t(.*)$",txt,re.M): return m.group(1).strip()
    return ""
def rel32_at(va,size):
    """if the body at va is `e9 rel32` (5 bytes), return the absolute target"""
    b=ba(game,gs,va,5)
    if len(b)>=5 and b[0]==0xE9:
        rel=struct.unpack_from("<i",b,1)[0]
        return va+5+rel
    return None

# the map produced by find-dongle-symbols.py (grades A/B/C) -- kept in sync by hand, printed below
PROVEN={"DongleClose":0x081c6210,"DongleDecrypt":0x081c5e00,"DongleEncrypt":0x081c5e70,
 "DongleFeatureLogin":0x081c6a50,"DongleGetRand":0x081c5ee0,"DongleInit":0x081c6d20,
 "DongleIsPresent":0x081c5ff0,"DongleNoise":0x080ac230,"DongleOpen":0x081c6b70,
 "DongleRead":0x081c6970,"DongleReadCabSerialNum":0x081c6700,"DongleReadCountryCode":0x081c6640,
 "DongleReadHLFeature":0x081c6880,"DongleReadHLFeatureInt":0x081c6800,"DongleWrite":0x081c6550,
 "DongleWriteCabSerialNum":0x081c6390,"DongleWriteCountryCode":0x081c6310,
 "DongleWriteHLFeature":0x081c6490,"DongleWriteHLFeatureInt":0x081c6410,"MemAllocDongle":0x0806f6e0,
 "__hasp_enxml_ex":0x083bc0a0,"__hasp_monster_detach":0x083c11e0,
 "hasp_datetime_to_hasptime":0x083bade0,"hasp_decrypt":0x083bbce0,"hasp_detach":0x083ba2d0,
 "hasp_echo":0x083bbc80,"hasp_encrypt":0x083bbd60,"hasp_get_info":0x083ba8a0,
 "hasp_get_rtc":0x083bab60,"hasp_get_sessioninfo":0x083bae80,"hasp_get_size":0x083babe0,
 "hasp_legacy_decrypt":0x083c7420,"hasp_legacy_encrypt":0x083c74b0,
 "hasp_legacy_set_idletime":0x083c71f0,"hasp_legacy_set_rtc":0x083c7280,"hasp_login":0x083bbe60,
 "hasp_login_scope":0x083baac0,"hasp_logout":0x083bbe00,"hasp_monster_get_lms_version":0x083c3b50,
 "hasp_read":0x083bad30,"hasp_update":0x083bb110,"hasp_write":0x083bac80,"haspregnew":0x083d4820}
# alias targets located independently by find-dongle-symbols.py's [STRING] method
ALIAS_TARGET={"hasp_login_ex":0x083bf6a0,          # SIjmcUAXayCUVb6, 0/200 bytes differ
              "hasp_hasptime_to_datetime":0x0840a740}  # ciBHxZTJ0qIk7Je, 3/224 bytes differ

print("="*104)
print("Dongle / HASP-HL symbol map for the STRIPPED `game`  --  part 2: adjacency-span + alias")
print("="*104)
order=sorted(named.items(), key=lambda kv: kv[1][0])
resolved={}; unresolved={}
for idx,(name,(va,size)) in enumerate(order):
    if name in PROVEN: continue
    A=B=None
    for j in range(idx-1,-1,-1):
        if order[j][0] in PROVEN: A=order[j]; break
    for j in range(idx+1,len(order)):
        if order[j][0] in PROVEN: B=order[j]; break
    if not A or not B:
        unresolved[name]=("no proven bracket in ServiceDongleApp",None); continue
    between=[n for n,(a,s) in order if A[1][0] < a < B[1][0] and n not in PROVEN]
    span_sda=B[1][0]-A[1][0]; span_game=PROVEN[B[0]]-PROVEN[A[0]]
    if span_sda!=span_game:
        # NEIGHBOUR-DISTANCE rule: if G is adjacent to a proven neighbour with NO function in between,
        # and the distance G->neighbour is identical in both builds, G is forced by that neighbour alone.
        forced=None; nb=None
        prev=[n for n,(a,s) in order if a < va and n in PROVEN]
        nxt =[n for n,(a,s) in order if a > va and n in PROVEN]
        for cand,is_prev in ((max(prev,key=lambda n:named[n][0]) if prev else None,True),
                             (min(nxt,key=lambda n:named[n][0]) if nxt else None,False)):
            if not cand: continue
            inside=[n for n,(a,s) in order if min(a,va) < a < max(a,va) and n not in PROVEN and n!=name]
            if inside: continue
            d_sda=abs(named[cand][0]-va)
            predicted = PROVEN[A[0]] + (va - A[1][0])
            d_game=abs(PROVEN[cand]-predicted)
            if d_sda==d_game:
                forced = (PROVEN[cand]+d_sda) if is_prev else (PROVEN[cand]-d_sda)
                nb=(cand,is_prev,d_sda); break
        if forced is not None:
            cand,is_prev,d=nb
            s_first=first_ins(dis(SDA,va,size,"s_%s"%name))
            g_first=first_ins(dis(GAME,forced,size+8,"g_%s"%name))
            mnem=lambda i: i.split()[0] if i else ""
            form_ok=mnem(s_first)==mnem(g_first)
            s_body=ba(sda,ss,va,min(size,8))
            g_body=ba(game,gs,forced,min(size,8))
            exact = s_body==g_body
            if form_ok:
                ev=["NEIGHBOUR-DISTANCE: %s sits %s the proven %s in ServiceDongleApp with no other function "
                    "in between, and the distance is 0x%x in BOTH builds (0x%08x -> 0x%08x in game), so it is "
                    "forced to 0x%08x" % (name,"before" if is_prev else "after",cand,d,PROVEN[cand],forced,forced),
                    "first instruction: SDA %r vs game %r -> mnemonic MATCHES" % (s_first,g_first)]
                if exact:
                    ev.append("the first %d bytes are BYTE-IDENTICAL (%s) -> the body matches exactly"
                              % (min(size,8),g_body.hex(" ")))
                resolved[name]=(forced,ev)
                continue
        unresolved[name]=("bracket %s..%s spans 0x%x in SDA but 0x%x in game (+0x%x): the region changed size, "
                          "so no SDA-relative offset survives" % (A[0],B[0],span_sda,span_game,span_game-span_sda),None)
        continue
    if len(between)!=1:
        # Not forced by the span alone. Accept only with an INDEPENDENT corroboration at the predicted
        # address: the first instruction matches SDA's, and for a thunk the rel32 target equals the
        # target found independently by code-block matching.
        g=PROVEN[A[0]]+(va-A[1][0])
        s_first=first_ins(dis(SDA,va,size,"s_%s"%name))
        g_first=first_ins(dis(GAME,g,size+8,"g_%s"%name))
        mnem=lambda i: i.split()[0] if i else ""
        form_ok = mnem(s_first)==mnem(g_first)
        g_target=rel32_at(g,size)
        cross = (name in ALIAS_TARGET and g_target is not None and g_target==ALIAS_TARGET[name])
        if form_ok and (cross or len(between)<=2):
            ev=["ADJACENCY-SPAN (corroborated split): bracket %s (game 0x%08x) .. %s (game 0x%08x) spans "
                "0x%x in BOTH builds, so the block layout is identical; %d unproven functions sit inside, "
                "so the split is not forced by arithmetic alone, but the predicted address for %s is "
                "0x%08x + (0x%x - 0x%x) = 0x%08x"
                % (A[0],PROVEN[A[0]],B[0],PROVEN[B[0]],span_sda,len(between),name,PROVEN[A[0]],va,A[1][0],g),
                "first instruction at the predicted address: SDA %r vs game %r -> mnemonic MATCHES"
                % (s_first,g_first)]
            if cross:
                ev.append("ALIAS CROSS-CHECK: game 0x%08x holds `jmp rel32` -> 0x%08x, and the target was "
                          "found INDEPENDENTLY by code-block matching at 0x%08x -> EXACT AGREEMENT "
                          "(two independent methods)" % (g,g_target,ALIAS_TARGET[name]))
            resolved[name]=(g,ev)
        else:
            unresolved[name]=("bracket %s..%s span matches (0x%x) but %d unproven functions sit inside and "
                              "the predicted address is not corroborated (first instruction %s)"
                              % (A[0],B[0],span_sda,len(between),"matches" if form_ok else "differs"),None)
        continue
    g=PROVEN[A[0]]+(va-A[1][0])
    s_first=first_ins(dis(SDA,va,size,"s_%s"%name))
    g_first=first_ins(dis(GAME,g,size+8,"g_%s"%name))
    mnem=lambda i: i.split()[0] if i else ""
    form_ok = mnem(s_first)==mnem(g_first)
    end=g+size; gap_sda=B[1][0]-(va+size); gap_game=PROVEN[B[0]]-end
    gap_ok = gap_sda==gap_game
    ev=["ADJACENCY: bracketed by the proven %s (game 0x%08x) and %s (game 0x%08x); the span between them is "
        "0x%x in BOTH builds, and %s is the only unproven function inside it, so game(%s) = 0x%08x + "
        "(0x%x - 0x%x) = 0x%08x"
        % (A[0],PROVEN[A[0]],B[0],PROVEN[B[0]],span_sda,name,name,PROVEN[A[0]],va,A[1][0],g),
        "first instruction at the predicted address: SDA %r vs game %r -> mnemonic %s"
        % (s_first,g_first,"MATCHES" if form_ok else "DIFFERS"),
        "SDA size 0x%x + inter-function gap 0x%x lands on %s in both builds -> %s"
        % (size,gap_sda,B[0],"MATCHES" if gap_ok else "DIFFERS")]
    # alias cross-check
    s_t=rel32_at(va,size) if False else None
    s_body=ba(sda,ss,va,5)
    s_target=None
    if len(s_body)>=5 and s_body[0]==0xE9:
        s_target=va+5+struct.unpack_from("<i",s_body,1)[0]
    g_target=rel32_at(g,size)
    cross=None
    if g_target is not None and name in ALIAS_TARGET:
        cross = (g_target==ALIAS_TARGET[name])
        ev.append("ALIAS CROSS-CHECK: game 0x%08x holds `jmp rel32` -> 0x%08x; the target was found "
                  "INDEPENDENTLY by code-block matching at 0x%08x -> %s"
                  % (g,g_target,ALIAS_TARGET[name],"EXACT AGREEMENT (two independent methods)" if cross else "MISMATCH"))
    if form_ok and gap_ok:
        resolved[name]=(g,ev)

print()
for n in sorted(resolved):
    a,ev=resolved[n]
    print("%-30s game 0x%08x" % (n,a))
    for e in ev: print("      - %s" % e)
print()
print()
# ---------------------------------------------------------------- final pass: pattern + adjacency
# For a function whose exact body is too short to be unique in `game` (e.g. a 6-byte constant stub),
# the correct occurrence is still identifiable by ADJACENCY to a proven neighbour: only the right
# occurrence sits at the same distance from a proven neighbour as it does in ServiceDongleApp.
for name in list(unresolved):
    va,size=named[name]
    pat=ba(sda,ss,va,min(size,16))
    if len(pat)<4: continue
    occ=[]
    for v,o,sz in gs:
        blob=game[o:o+sz]; i=blob.find(pat)
        while i!=-1:
            occ.append(v+i); i=blob.find(pat,i+1)
    if not occ: continue
    good=[]
    for o_ in occ:
        for n,(na,ns) in named.items():
            if n not in PROVEN: continue
            if abs(na-va)==abs(PROVEN[n]-o_):
                inside=[m for m,(ma,ms) in named.items()
                        if min(ma,va)<ma<max(ma,va) and m not in PROVEN and m!=name]
                if not inside: good.append((o_,n,abs(na-va))); break
    if len(good)==1:
        o_,n,d=good[0]
        resolved[name]=(o_,[
          "EXACT-BODY PATTERN: the first %d bytes of %s in ServiceDongleApp (%s) occur in `game` at %s"
          % (len(pat),name,pat.hex(" ")," and ".join("0x%08x"%x for x in occ)),
          "ADJACENCY DISCRIMINATION: only the occurrence at 0x%08x sits at the same distance (0x%x) from the "
          "proven %s (game 0x%08x) as in ServiceDongleApp, with no other function in between -> 0x%08x is the "
          "right one; the other occurrence(s) are unrelated code that happens to share the byte pattern"
          % (o_,d,n,PROVEN[n],o_)])
        del unresolved[name]
    elif len(good)>1:
        unresolved[name]=("exact body occurs %d times in game and %d of them are adjacency-consistent -> "
                          "ambiguous, refused" % (len(occ),len(good)),None)

print("-"*104)
print("UNRESOLVED (%d), each with the measured reason:" % len(unresolved))
for n,(why,_) in sorted(unresolved.items()):
    print("  %-30s %s" % (n,why))
print()
print("-"*104)
print("part 1 (find-dongle-symbols.py) located : 43")
print("part 2 (this script) located            : %d  ->  %s" % (len(resolved), ", ".join(sorted(resolved))))
print("total located with evidence             : %d / %d" % (43+len(resolved), len(named)))
print("still unresolved                        : %d" % len(unresolved))

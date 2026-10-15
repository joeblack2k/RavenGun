# align.py <probe> <game> <root-of-unpacked-14.04.5> [json-overrides] [out.json]
# Fit candidate libraries against the stale first-GOT addresses: a page-aligned base per library, chosen by a
# best-path search over the sorted stale values (a run of consecutive matches at one base scores +1 each, a switch
# costs P). IFUNC implementations are named through their resolver (ifuncmap.py), since no -dbg package was reachable.
import sys,subprocess,re,collections,json,os
sys.path.insert(0,sys.argv[1]); from slots import load,stale; from ifuncmap import impls
R=sys.argv[3]; L=R+'/lib/i386-linux-gnu'; U=R+'/usr/lib/i386-linux-gnu'
TP=os.path.expanduser('~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs')
LIBS={
 'libc.so.6':L+'/libc-2.19.so','libpthread.so.0':L+'/libpthread-2.19.so','libm.so.6':L+'/libm-2.19.so','libdl.so.2':L+'/libdl-2.19.so',
 'libgcc_s.so.1':L+'/libgcc_s.so.1','libz.so.1':L+'/libz.so.1.2.8','libssl.so.1.0.0':L+'/libssl.so.1.0.0','libcrypto.so.1.0.0':L+'/libcrypto.so.1.0.0',
 'libstdc++.so.6':U+'/libstdc++.so.6.0.19','libX11.so.6':U+'/libX11.so.6.3.0','libXext.so.6':U+'/libXext.so.6.4.0','libasound.so.2':U+'/libasound.so.2.0.0',
 'libcaca.so.0':U+'/libcaca.so.0.99.18','libpulse.so.0':U+'/libpulse.so.0.16.2','libpulse-simple.so.0':U+'/libpulse-simple.so.0.0.4',
 'libGLU.so.1':U+'/libGLU.so.1.3.1','libcsv.so':TP+'/libcsv.so',
}
WIN={}; FIXED={}
if len(sys.argv)>4 and sys.argv[4]:
    o=json.loads(sys.argv[4]); LIBS.update(o.get('libs',{})); WIN.update({k:tuple(v) for k,v in o.get('win',{}).items()})
    FIXED.update({k:int(v,16) for k,v in o.get('fixed',{}).items()})  # second pass: small libraries whose base the layout pins
    for k in o.get('drop',[]): LIBS.pop(k,None)
OUT=sys.argv[5] if len(sys.argv)>5 else 'align.json'
def syms(f):
    out=subprocess.run(['readelf','-sW','--dyn-syms',f],capture_output=True,text=True).stdout; r=[]
    for l in out.splitlines():
        p=l.replace('<OS specific>: 10','IFUNC').split()
        if len(p)>=8 and p[3] in('FUNC','IFUNC') and p[6]!='UND':
            r.append((int(p[1],16),p[3],p[7].split('@')[0],'@@' in p[7] or '@' not in p[7]))
    return r
tab={}
for lib,f in LIBS.items():
    t=collections.defaultdict(lambda:{'dyn':[],'ifimpl':[]})
    for a,ty,n,dflt in syms(f):
        if a and ty=='FUNC': (t[a]['dyn'].insert(0,n) if dflt else t[a]['dyn'].append(n))
    for a,names in impls(f).items(): t[a]['ifimpl']+=names
    tab[lib]=dict(t)
    print('ref',lib,len(t),'addrs',file=sys.stderr)
g,sl=load(sys.argv[2])
vals=sorted(set(v for *_,v in sl if stale(v)))
# the envelope GOT's bound values are named anchors too: they join the fit (not the table)
for off,info in g.rels(23,2):
    v=g.u32(off)
    if stale(v) and v not in vals: vals.append(v)
vals.sort()
P=2.5; carry={}; gbest=(0.0,None); node=[]
for i,v in enumerate(vals):
    new={}
    for lib,t in tab.items():
        for a in t:
            d=v-a; w=WIN.get(lib)
            if w:
                if not (w[0]<=d<=w[1]): continue
            elif d&0xfff: continue
            if not (0xb6000000<=d<0xb8000000): continue
            key=(lib,d); c=carry.get(key)
            s1=c[0]+1 if c else -1e9; s2=gbest[0]-P+1
            new[key]=(s1,c[1]) if s1>=s2 else (s2,gbest[1])
    for key,(s,prev) in new.items():
        node.append((i,key,prev)); idx=len(node)-1; carry[key]=(s,idx)
        if s>gbest[0]: gbest=(s,idx)
assign={}; idx=gbest[1]
while idx is not None:
    i,key,prev=node[idx]; assign[vals[i]]=key; idx=prev
def pick(lib,a):
    e=tab[lib][a]
    if e['dyn']:
        c=sorted(set(e['dyn']),key=lambda n:(n.startswith('_'),len(n))); return c[0],'export',sorted(set(e['dyn']))
    if e['ifimpl']:
        c=sorted(set(e['ifimpl']),key=lambda n:(n.startswith('_'),len(n))); return c[0],'ifunc-impl',c
    return None,'none',[]
# second pass: a library too small to win a run (1-2 imports) is accepted at a base fixed by its neighbours
for lib,base in FIXED.items():
    hit=[v for v in vals if v not in assign and (v-base) in tab[lib]]
    for v in hit: assign[v]=(lib,base)
    print('fixed',lib,'%#x'%base,len(hit),'hits',file=sys.stderr)
runs=collections.Counter(assign.values()); res={}
for v in vals:
    if v in assign:
        lib,d=assign[v]; n,how,al=pick(lib,v-d); res[v]=dict(lib=lib,shift=d,name=n,how=how,aliases=al,run=runs[(lib,d)],off=v-d)
json.dump({hex(k):v for k,v in res.items()},open(sys.argv[1]+'/'+OUT if '/' not in OUT else OUT,'w'),indent=0)
print('values',len(vals),'assigned',len(res),'named',sum(1 for r in res.values() if r['name']))
segs=[]
for v in vals:
    k=assign.get(v)
    if segs and segs[-1][0]==k: segs[-1][2]=v; segs[-1][3]+=1
    else: segs.append([k,v,v,1])
for k,a,b,n in segs: print('  %#x..%#x n=%3d  %s'%(a,b,n,('%s base %#x'%k) if k else 'UNASSIGNED'))

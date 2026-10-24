import sys,subprocess,re,collections,json,os
sys.path.insert(0,sys.argv[1]); from slots import load
D=sys.argv[3]; EL=os.path.expanduser('~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs')
LIBS={ # libname: [files...]  (first = stripped lib with dynsym, others add .symtab names)
 'libc.so.6':[D+'/libc6_6/lib/i386-linux-gnu/libc-2.19.so',D+'/libc6dbg_6/usr/lib/debug/lib/i386-linux-gnu/libc-2.19.so'],
 'libpthread.so.0':[D+'/libc6_6/lib/i386-linux-gnu/libpthread-2.19.so',D+'/libc6dbg_6/usr/lib/debug/lib/i386-linux-gnu/libpthread-2.19.so'],
 'libm.so.6':[D+'/libc6_6/lib/i386-linux-gnu/libm-2.19.so',D+'/libc6dbg_6/usr/lib/debug/lib/i386-linux-gnu/libm-2.19.so'],
 'libdl.so.2':[D+'/libc6_6/lib/i386-linux-gnu/libdl-2.19.so'],
 'librt.so.1':[D+'/libc6_6/lib/i386-linux-gnu/librt-2.19.so'],
 'libstdc++.so.6':[D+'/stdcxx/usr/lib/i386-linux-gnu/libstdc++.so.6.0.19',D+'/stdcxxdbg/usr/lib/debug/usr/lib/i386-linux-gnu/libstdc++.so.6.0.19'],
 'libSDL-1.2.so.0':[D+'/sdl/usr/lib/i386-linux-gnu/libSDL-1.2.so.0.11.4'],
 'libGLU.so.1':[D+'/glu/usr/lib/i386-linux-gnu/libGLU.so.1.3.1'],
 'libglut.so.3':[D+'/glut/usr/lib/i386-linux-gnu/libglut.so.3.9.0'],
 'libX11.so.6':[D+'/x11/usr/lib/i386-linux-gnu/libX11.so.6.3.0'],
 'libudev.so.1':[D+'/udev/lib/i386-linux-gnu/libudev.so.1.3.5'],
 'libogg.so.0':[D+'/ogg/usr/lib/i386-linux-gnu/libogg.so.0.8.1'],
 'libtheora.so.0':[D+'/theora/usr/lib/i386-linux-gnu/libtheora.so.0.3.10'],
 'libPhysXLoader.so.1':[EL+'/libPhysXLoader.so.1'],
}
WIN={'libc.so.6':(0xb7140000,0xb7141000),'libdl.so.2':(0xb75ee000,0xb75f1000)}
if len(sys.argv)>4:
    o=json.loads(sys.argv[4]); LIBS.update(o.get('libs',{})); WIN.update({k:tuple(v) for k,v in o.get('win',{}).items()})
    for k in o.get('drop',[]): LIBS.pop(k,None)
OUT=sys.argv[5] if len(sys.argv)>5 else 'align.json'
def syms(f,dyn):
    out=subprocess.run(['readelf','-sW','--dyn-syms' if dyn else '--syms',f],capture_output=True,text=True).stdout
    r=[]
    for l in out.splitlines():
        p=l.split()
        if len(p)>=8 and p[3] in('FUNC','IFUNC','GNU_IFUNC') and p[6]!='UND':
            r.append((int(p[1],16),p[3],p[4],p[7].split('@')[0], '@@' in p[7] or '@' not in p[7]))
    return r
tab={}  # lib -> addr -> {'dyn':set,'loc':set,'ifunc':bool}
ifuncs={}
for lib,fs in LIBS.items():
    t=collections.defaultdict(lambda:{'dyn':[], 'loc':[]}); ifs=set()
    for a,ty,b,n,dflt in syms(fs[0],True):
        if ty!='FUNC': ifs.add(n)
        if a: (t[a]['dyn'].insert(0,n) if dflt else t[a]['dyn'].append(n))
    for f in fs[1:]:
        for a,ty,b,n,_ in syms(f,False):
            if a: t[a]['loc'].append(n)
    tab[lib]=dict(t); ifuncs[lib]=ifs
    print('ref',lib,len(t),'func addrs',len(ifs),'ifunc exports',file=sys.stderr)
g,sl=load(sys.argv[2])
vals=sorted(set(v for _,_,_,v in sl if v>>24==0xb7))
P=2.5
carry={}; gbest=(0.0,None)  # score, backptr
bp=[]  # per value: dict key-> (score, prevptr)
node=[] # list of (i,key,prev_index)
def add(i,key,prev): node.append((i,key,prev)); return len(node)-1
for i,v in enumerate(vals):
    new={}
    for lib,t in tab.items():
        for a in t:
            d=v-a
            w=WIN.get(lib)
            if w:
                if not (w[0]<=d<=w[1]): continue
            elif d&0xfff: continue
            if not (0xb7000000<=d<0xb7800000): continue
            key=(lib,d)
            c=carry.get(key)
            s1=c[0]+1 if c else -1e9
            s2=gbest[0]-P+1
            new[key]=(s1,c[1]) if s1>=s2 else (s2,gbest[1])
    for key,(s,prev) in new.items():
        idx=add(i,key,prev); carry[key]=(s,idx)
        if s>gbest[0]: gbest=(s,idx)
# backtrack
assign={}
idx=gbest[1]
while idx is not None:
    i,key,prev=node[idx]; assign[vals[i]]=key; idx=prev
VAR=re.compile(r'^__?(.*?)_(sse2_bsf|sse2_rep|sse2|ssse3_rep|ssse3|sse4_2|sse42|sse4|ia32|i386|i586|i686|sse2_unaligned|fma|avx|sse3|sse)$')
def pick(lib,a):
    e=tab[lib][a]
    if e['dyn']:
        c=sorted(set(e['dyn']),key=lambda n:(n.startswith('_'),len(n)))
        return c[0],'export',sorted(set(e['dyn']))
    for n in e['loc']:
        m=VAR.match(n)
        if m and m.group(1) in ifuncs[lib]: return m.group(1),'ifunc-impl:'+n,[]
    for n in e['loc']:
        m=VAR.match(n)
        if m: return m.group(1),'ifunc-impl?:'+n,[]
    return None,'local-only:'+','.join(sorted(set(e['loc']))[:3]),[]
# run lengths
runs=collections.Counter(assign.values())
res={}
for v in vals:
    if v in assign:
        lib,d=assign[v]; n,how,al=pick(lib,v-d); res[v]=dict(lib=lib,shift=d,name=n,how=how,aliases=al,run=runs[(lib,d)],off=v-d)
json.dump({hex(k):v for k,v in res.items()},open(sys.argv[1]+'/'+OUT,'w'),indent=0)
print('values',len(vals),'assigned',len(res),'with exported/ifunc name',sum(1 for r in res.values() if r['name']))
segs=[]
for v in vals:
    k=assign.get(v)
    if segs and segs[-1][0]==k: segs[-1][2]=v; segs[-1][3]+=1
    else: segs.append([k,v,v,1])
for k,a,b,n in segs:
    print('  %#x..%#x n=%3d  %s'%(a,b,n,('%s shift %#x'%k) if k else 'UNASSIGNED'))

# mktable.py <probe> <game> <align.json> <callers.pkl> <root-14.04.5> <libGL file> <out.tsv>
# Writes the first-PLT slot table for Walking Dead from the fit (align.py), the relocation table and the caller counts.
import sys,json,subprocess,os,collections,pickle
sys.path.insert(0,sys.argv[1]); from slots import load,stale
g,sl=load(sys.argv[2]); A=json.load(open(sys.argv[3])); C=pickle.load(open(sys.argv[4],'rb'))
R=sys.argv[5]; L=R+'/lib/i386-linux-gnu'; U=R+'/usr/lib/i386-linux-gnu'; GL=sys.argv[6]; OUT=sys.argv[7]
TP=os.path.expanduser('~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs')
RT=os.path.expanduser('~/.local/share/ravengun/runtime/lib32')
F={'libc.so.6':L+'/libc-2.19.so','libpthread.so.0':L+'/libpthread-2.19.so','libm.so.6':L+'/libm-2.19.so','libdl.so.2':L+'/libdl-2.19.so',
 'libgcc_s.so.1':L+'/libgcc_s.so.1','libz.so.1':L+'/libz.so.1.2.8','libssl.so.1.0.0':L+'/libssl.so.1.0.0','libcrypto.so.1.0.0':L+'/libcrypto.so.1.0.0',
 'libstdc++.so.6':U+'/libstdc++.so.6.0.19','libX11.so.6':U+'/libX11.so.6.3.0','libXext.so.6':U+'/libXext.so.6.4.0','libasound.so.2':U+'/libasound.so.2.0.0',
 'libcaca.so.0':U+'/libcaca.so.0.99.18','libpulse.so.0':U+'/libpulse.so.0.16.2','libpulse-simple.so.0':U+'/libpulse-simple.so.0.0.4',
 'libGLU.so.1':U+'/libGLU.so.1.3.1','libcsv.so':TP+'/libcsv.so','libGL.so.1':GL}
PKG={'libc.so.6':'libc6 2.19-0ubuntu6.9','libpthread.so.0':'libc6 2.19-0ubuntu6.9 libpthread','libm.so.6':'libc6 2.19-0ubuntu6.9 libm',
 'libdl.so.2':'libc6 2.19-0ubuntu6.9 libdl','libgcc_s.so.1':'libgcc1 1:4.9.3-0ubuntu4','libz.so.1':'zlib1g 1:1.2.8.dfsg-1ubuntu1',
 'libssl.so.1.0.0':'libssl1.0.0 1.0.1f-1ubuntu2.19','libcrypto.so.1.0.0':'libssl1.0.0 1.0.1f-1ubuntu2.19 libcrypto','libstdc++.so.6':'libstdc++6 4.8.4-2ubuntu1~14.04.3',
 'libX11.so.6':'libx11-6 2:1.6.2-1ubuntu2','libXext.so.6':'libxext6 2:1.3.2-1ubuntu0.0.14.04.1','libasound.so.2':'libasound2 1.0.27.2-3ubuntu7',
 'libcaca.so.0':'libcaca0 0.99.beta18-1ubuntu5','libpulse.so.0':'libpulse0 1:4.0-0ubuntu11.1','libpulse-simple.so.0':'libpulse0 1:4.0-0ubuntu11.1 libpulse-simple',
 'libGLU.so.1':'libglu1-mesa 9.0.0-2','libcsv.so':'TeknoParrot ElfLdr2/libs libcsv.so','libGL.so.1':'NVIDIA 375.20..378.13 GLVND libGL.so.1.0.0'}
SRC='Ubuntu 14.04.5 desktop i386 image'
def dynsyms(f):
    out=subprocess.run(['readelf','-sW','--dyn-syms',f],capture_output=True,text=True).stdout; r=[]
    for l in out.splitlines():
        p=l.replace('<OS specific>: 10','IFUNC').split()
        if len(p)>=8 and p[6]!='UND' and p[3] in('FUNC','IFUNC','OBJECT','NOTYPE','TLS'):
            r.append((int(p[1],16),p[7].split('@')[0],('@' not in p[7]) or ('@@' in p[7]),p[7]))
    return r
VER={lib:collections.defaultdict(list) for lib in F}
for lib,f in F.items():
    for a,n,dflt,full in dynsyms(f): VER[lib][a].append((n,dflt,full))
# what the native route will load (packaging/native-games.json "libs" + host /usr/lib32); libcaca has no host copy
HOST=['/usr/lib32/libc.so.6','/usr/lib32/libm.so.6','/usr/lib32/libpthread.so.0','/usr/lib32/libdl.so.2','/usr/lib32/libstdc++.so.6',
 '/usr/lib32/libgcc_s.so.1','/usr/lib32/libGL.so.1','/usr/lib32/libz.so.1','/usr/lib32/libX11.so.6','/usr/lib32/libXext.so.6',
 '/usr/lib32/libasound.so.2','/usr/lib32/libpulse.so.0','/usr/lib32/libpulse-simple.so.0',RT+'/libGLU.so.1',TP+'/libcsv.so',TP+'/libssl.so.1.0.0',TP+'/libcrypto.so.1.0.0']
hostall=set()
for h in HOST:
    if os.path.exists(h): hostall|=set(n for _,n,_,_ in dynsyms(h))
gamenames=set(g.symname(i) for i in range(1,g.nsyms()))
anch={}
for off,info in g.rels(23,2):
    v=g.u32(off)
    if stale(v): anch[v]=g.symname(info>>8)
jmp={off:g.symname(info>>8) for off,info in g.rels(23,2)}
def prefer(names):
    return sorted(set(names),key=lambda n:(n not in gamenames,n.startswith('_'),n not in hostall,len(n),n))
rows=[];stat=collections.Counter();conf=collections.Counter();miss=[];notes=collections.Counter()
for i,stub,slot,val in sl:
    name=lib=meth=cf=''
    if val==stub+6:
        name=jmp.get(slot,'?');lib='(JMPREL)';meth='DT_JMPREL entry for this slot (symbol index intact, lazy slot = stub+6)';cf='certain'
    elif val==0:
        name='__gmon_start__';lib='(weak, stays 0)';meth='slot is 0 in dump; only caller is DT_INIT 0x80529e8 (call 0x8053910 guarded by test/je on GOT-8, crti pattern)';cf='high'
    elif hex(val) in A:
        r=A[hex(val)]; lib=r['lib']; off=r['off']; run=r['run']
        ents=VER[lib].get(off,[])
        if r['how']=='ifunc-impl':
            cands=prefer(r['aliases']); name=cands[0]
            meth='IFUNC implementation at offset %#x of %s (no -dbg package: named through the resolver of the exported IFUNC) at page-aligned base %#x (%d matches)'%(off,PKG[lib],r['shift'],run)
            if len(cands)>1: meth+='; same implementation also returned for: '+','.join(cands[1:])
            cf='high'
        else:
            names=[n for n,_,_ in ents]; name=prefer(names)[0]
            meth='%s offset %#x at page-aligned base %#x (%d matches)'%(PKG[lib],off,r['shift'],run)
            if len(set(names))>1: meth+='; aliases at same address: '+','.join(x for x in sorted(set(names)) if x!=name)
            dflt=[full for n,d,full in ents if n==name]
            if dflt and not any(d for n,d,full in ents if n==name):
                meth+='; ONLY a non-default version (%s) lives here: dlsym(name) returns the default version'%dflt[0]; notes['nondefault']+=1
            cf='high'
        if lib in('libpulse-simple.so.0','libGLU.so.1','libcsv.so'):
            meth+='; base pinned by its neighbours in the NEEDED-order layout (layout.py, no gap)'
        if val in anch:
            ok = anch[val]==name or anch[val] in [n for n,_,_ in ents] or anch[val] in r['aliases']
            if ok: name=anch[val]; meth+='; confirmed by envelope JMPREL name'; cf='certain'
            else: meth+='; CONFLICT with envelope JMPREL name '+anch[val]; cf='conflict'
    elif val in anch:
        name=anch[val];meth='envelope JMPREL name only';cf='certain'
    hostok=(name in hostall) if name and name!='__gmon_start__' else True
    if name and not hostok: miss.append((name,lib))
    stat[lib]+=1; conf[cf or 'none']+=1
    rows.append((stub,slot,val,name or '?',lib,meth or 'no fit',cf or 'none','yes' if hostok else 'NO',len(C.get(i,[]))))
with open(OUT,'w') as f:
    f.write('# The Walking Dead (Raw Thrills) `game` (sha256 efbd9b523dc5...2a0006, 19165184 bytes): first-PLT slot -> import name\n')
    f.write('# generated by probe/mktable.py from the dumped image + %s (2.19-0ubuntu6.9 libc) + NVIDIA 375.20..378.13 GLVND libGL\n'%SRC)
    f.write('plt_stub_vaddr\tgot_slot_vaddr\tstale_value_in_dump\tname\tcabinet_library\tmethod\tconfidence\tname_exported_by_host_libs\tdirect_call_refs\n')
    for r in rows: f.write('%#010x\t%#010x\t%#010x\t%s\t%s\t%s\t%s\t%s\t%d\n'%r)
print('rows',len(rows)); print('by lib',dict(stat)); print('confidence',dict(conf)); print('notes',dict(notes))
print('names not exported by host libs:',miss)
d=[(n,c) for n,c in collections.Counter(r[3] for r in rows).items() if c>1]; print('duplicate names',d)

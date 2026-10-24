import sys,json,subprocess,os,re,collections,pickle
sys.path.insert(0,sys.argv[1]); from slots import load
W=sys.argv[1]; g,sl=load(sys.argv[2]); A=json.load(open(W+'/alignA.json')); D=sys.argv[3]
C=pickle.load(open(W+'/callers.pkl','rb'))
def dyn(f,defined=True):
    out=subprocess.run(['readelf','-sW','--dyn-syms',f],capture_output=True,text=True).stdout; s=set()
    for l in out.splitlines():
        l=l.replace('<OS specific>: 10','IFUNC'); p=l.split()
        if len(p)>=8 and p[6]!='UND' and p[3] in('FUNC','IFUNC','OBJECT','NOTYPE','TLS'): s.add(p[7].split('@')[0])
    return s
libc_exp=dyn(D+'/libc6_6/lib/i386-linux-gnu/libc-2.19.so')|dyn(D+'/libc6_6/lib/i386-linux-gnu/libm-2.19.so')
R=os.path.expanduser('~/.local/share/ravengun/runtime')
host={}
for n,p in [('libc.so.6','/usr/lib32/libc.so.6'),('libm.so.6','/usr/lib32/libm.so.6'),('libpthread.so.0','/usr/lib32/libpthread.so.0'),('libdl.so.2','/usr/lib32/libdl.so.2'),('libstdc++.so.6','/usr/lib32/libstdc++.so.6'),('libgcc_s.so.1','/usr/lib32/libgcc_s.so.1'),('libudev.so.1','/usr/lib32/libudev.so.1'),('libGL.so.1','/usr/lib32/libGL.so.1'),('libogg.so.0','/usr/lib32/libogg.so.0'),('libSDL-1.2.so.0',R+'/lib32/libSDL-1.2.so.0'),('libGLU.so.1',R+'/lib32/libGLU.so.1'),('libglut.so.3',R+'/lib32/libglut.so.3'),('libtheora.so.0',R+'/lib32/libtheora.so.0'),('libPhysXLoader.so.1',R+'/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs/libPhysXLoader.so.1')]:
    host[n]=dyn(p)
hostall=set().union(*host.values())
anch={}
for off,info in g.rels(23,2):
    v=g.u32(off)
    if v is not None and v>>24==0xb7: anch[v]=g.symname(info>>8)
MAN={0xb715a990:('__libc_start_main','libc.so.6','exact offset 0x19990 of libc6 2.19-0ubuntu6 at base 0xb7141000 + only caller is _start (e_entry)','high'),
 0xb71658b0:('setlocale','libc.so.6','libc offset 0x248b0 between matched runs (ref setlocale 0x24be0) + callers hid_init/localizationInit','medium'),
 0xb73043f0:('_Unwind_Resume','libgcc_s.so.1','libgcc1 4.9-20140406 offset 0x153f0 at page-aligned base 0xb72ef000 (ends where libudev starts) + 1199 C++ callers','high'),
 0xb7380200:('__errno_location','libpthread.so.0','libpthread offset 0x11200 (ref 0x11170, +0x90) + 138 callers in errno-using code (libusb/zlib/OpenSSL)','high'),
 0xb7398620:('glutBitmapCharacter','libglut.so.3','freeglut3 2.8.1-1 at page-aligned base 0xb738b000 (ends where libGLU starts) + caller render_text','high'),
 0xb73c79e0:('gluErrorString','libGLU.so.1','libglu1-mesa 9.0.0-2 at page-aligned base 0xb73c5000 (2 of 2 hits) + caller LogGLErrors','high'),
 0xb73db290:('gluBuild2DMipmaps','libGLU.so.1','libglu1-mesa 9.0.0-2 at page-aligned base 0xb73c5000 (2 of 2 hits) + caller imgLoad','high'),
 0xb76e68e4:('NxGetCookingLib','libPhysXLoader.so.1','ElfLdr2/libs libPhysXLoader.so.1 offset 0x88e4 at page-aligned base 0xb76de000 (ends where libogg starts) + caller main','high')}
EXACT={'libudev.so.1':'libudev1 204-5ubuntu20','libm.so.6':'libc6 2.19-0ubuntu6 libm','libpthread.so.0':'libc6 2.19-0ubuntu6 libpthread','libSDL-1.2.so.0':'libsdl1.2debian 1.2.15-8ubuntu1.1','libdl.so.2':'libc6 2.19-0ubuntu6 libdl','libstdc++.so.6':'libstdc++6 4.8.2-19ubuntu1','libogg.so.0':'libogg0 1.3.1-1ubuntu1','libtheora.so.0':'libtheora0 1.1.1+dfsg.1-3.2','libGL.so.1':'NVIDIA 346.59 libGL (130/130 unique)'}
rows=[];stat=collections.Counter();conf=collections.Counter();miss=[]
for i,stub,slot,val in sl:
    name=lib=meth='';cf=''
    if i==0 and False: pass
    if val==stub+6:
        name='fork';lib='libc.so.6';meth='DT_JMPREL entry (symbol index intact, lazy slot)';cf='certain'
    elif val==0:
        name='__gmon_start__';lib='(weak, stays 0)';meth='slot is 0 in dump; only caller is DT_INIT guarded by test/je (crti pattern)';cf='high'
    elif val in MAN:
        name,lib,meth,cf=MAN[val]
    elif hex(val) in A:
        r=A[hex(val)]; lib=r['lib']; name=r['name']
        if r['how'].startswith('ifunc'):
            impl=r['how'].split(':')[1]
            cands=[c for c in ('__'+name,name) if c in libc_exp]
            if name.endswith('_chk'): cands=[c for c in ('__'+name,) if c in libc_exp] or cands
            name=cands[-1] if cands else name
            meth='IFUNC implementation %s in libc6-dbg 2.19-0ubuntu6 at shift %#x (run of %d)'%(impl,r['shift'],r['run']); cf='high' if cands else 'medium'; meth+=' -> exported IFUNC symbol' if cands else ''
        elif lib=='libc.so.6':
            meth='libc6 2.19-0ubuntu6 symbol at piecewise shift %#x (run of %d consecutive matches)'%(r['shift'],r['run']); cf='high' if r['run']>=7 else 'medium'
        else:
            meth='%s at page-aligned base %#x (%d matches)'%(EXACT.get(lib,lib),r['shift'],r['run']); cf='high'
        if val in anch:
            ok = anch[val]==name or anch[val] in r['aliases']
            if ok: name=anch[val]; meth+='; confirmed by envelope JMPREL name'; cf='certain'
            else: meth+='; CONFLICT with envelope name '+anch[val]
        elif len(r['aliases'])>1: meth+='; aliases at same address: '+','.join(a for a in r['aliases'] if a!=name)
    elif val in anch:
        name=anch[val];meth='envelope JMPREL name';cf='certain'
    hostok = (name in hostall) if name and name!='__gmon_start__' else True
    if name and not hostok: miss.append((name,lib))
    stat[lib]+=1; conf[cf or 'none']+=1
    rows.append((stub,slot,val,name or '?',lib,meth,cf or 'none','yes' if hostok else 'NO',len(C.get(i,[]))))
with open(W+'/jp-slots.tsv','w') as f:
    f.write('# Jurassic Park Arcade `game` (sha256 c18a9329e72a...b4269, 17694720 bytes): first-PLT slot -> import name\n# generated by work/mktable.py from the dumped image + Ubuntu 14.04 i386 packages + NVIDIA 346.59\n')
    f.write('plt_stub_vaddr\tgot_slot_vaddr\tstale_value_in_dump\tname\tcabinet_library\tmethod\tconfidence\tname_exported_by_host_libs\tdirect_call_refs\n')
    for r in rows: f.write('%#010x\t%#010x\t%#010x\t%s\t%s\t%s\t%s\t%s\t%d\n'%r)
print('rows',len(rows)); print('by lib',dict(stat)); print('confidence',dict(conf)); print('names not exported by host libs:',miss)
names=[r[3] for r in rows]; d=[(n,c) for n,c in collections.Counter(names).items() if c>1]; print('duplicate names',d)

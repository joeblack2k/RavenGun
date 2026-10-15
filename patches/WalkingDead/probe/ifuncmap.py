# ifuncmap.py <lib.so> : name the IFUNC implementations of a stripped library without its -dbg package.
# Each IFUNC resolver is i386 PIC: call get_pc_thunk ; add $GOT,%ebx ; lea disp(%ebx),%eax ... ret.
# Every lea target is one implementation the resolver can return; map it to the exported IFUNC name.
import sys,subprocess,re,json
def ifuncs(lib):
    out=subprocess.run(['readelf','-sW','--dyn-syms',lib],capture_output=True,text=True).stdout
    r={}
    for l in out.splitlines():
        p=l.replace('<OS specific>: 10','IFUNC').split()
        if len(p)>=8 and p[3]=='IFUNC' and p[6]!='UND':
            r.setdefault(int(p[1],16),[]).append((int(p[2]),p[7].split('@')[0]))
    return r
def impls(lib):
    res={}
    for a,names in ifuncs(lib).items():
        sz=max(n for n,_ in names) or 0x80
        dis=subprocess.run(['objdump','-d','--start-address=%#x'%a,'--stop-address=%#x'%(a+sz),lib],capture_output=True,text=True).stdout
        ebx=None; prev=None
        for l in dis.splitlines():
            m=re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S+)\s*(.*)',l)
            if not m: continue
            ad=int(m.group(1),16); op=m.group(2); arg=m.group(3)
            mm=re.match(r'\$0x([0-9a-f]+),%ebx',arg)
            if op=='add' and mm and ebx is None: ebx=ad+int(mm.group(1),16)   # thunk returns the address of this add
            mm=re.match(r'(-?)0x([0-9a-f]+)\(%ebx\),%e[a-d]x',arg)
            if op=='lea' and mm and ebx is not None:
                t=(ebx+(-1 if mm.group(1) else 1)*int(mm.group(2),16))&0xffffffff
                nm=sorted(set(n for _,n in names),key=lambda n:(n.startswith('_'),len(n)))
                res.setdefault(t,set()).update(nm)
    return {t:sorted(v) for t,v in res.items()}
if __name__=='__main__':
    m=impls(sys.argv[1]); print(len(m),'implementation addresses',file=sys.stderr)
    json.dump({hex(k):v for k,v in m.items()},sys.stdout,indent=0)

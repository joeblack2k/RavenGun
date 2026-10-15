# glfit.py <probe> <game> <nvdir> [lo hi] : fit every libGL*.so* under <nvdir>/x*/ against the stale values in the
# libGL gap (default 0xb76ee000..0xb775e000 = end of libGLU .. start of libcsv). Prints best page-aligned base per file.
import sys,subprocess,collections,glob,os
sys.path.insert(0,sys.argv[1]); from slots import load
g,sl=load(sys.argv[2]); lo,hi=(int(sys.argv[4],16),int(sys.argv[5],16)) if len(sys.argv)>5 else (0xb76ee000,0xb775e000)
vals=sorted(set(v for *_,v in sl if lo<=v<hi))
rows=[]
for l in sorted(glob.glob(sys.argv[3]+'/x*/libGL.so.*')):
    out=subprocess.run(['readelf','-sW','--dyn-syms',l],capture_output=True,text=True).stdout; sy={}
    for ln in out.splitlines():
        p=ln.split()
        if len(p)>=8 and p[3]=='FUNC' and p[6]!='UND': sy.setdefault(int(p[1],16),[]).append(p[7])
    h=collections.Counter()
    for v in vals:
        for a in sy:
            if (v-a)&0xfff==0: h[v-a]+=1
    d,n=h.most_common(1)[0] if h else (0,0)
    sz=os.path.getsize(l)
    rows.append((n,l.split('/')[-2]+'/'+l.split('/')[-1],d,sz))
    print('%-40s best base %#x : %3d of %d   (file %d B)'%(rows[-1][1],d,n,len(vals),sz),flush=True)

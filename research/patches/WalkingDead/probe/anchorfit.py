# anchorfit.py <probe> <game> <lib.so> [lo hi] : for every envelope-GOT anchor (named, bound to a stale address),
# print stale - symbol address in this candidate library; one shared delta = this build at that base.
import sys,subprocess,collections
sys.path.insert(0,sys.argv[1]); from slots import load,stale
g,sl=load(sys.argv[2])
anch={}
for off,info in g.rels(23,2):
    v=g.u32(off)
    if stale(v): anch[g.symname(info>>8)]=v
out=subprocess.run(['readelf','-sW','--dyn-syms',sys.argv[3]],capture_output=True,text=True).stdout
sy={}
for l in out.splitlines():
    p=l.replace('<OS specific>: 10','IFUNC').split()
    if len(p)>=8 and p[6]!='UND' and p[3] in('FUNC','IFUNC'): sy.setdefault(p[7].split('@')[0],int(p[1],16))
h=collections.Counter(); rows=[]
for n,v in sorted(anch.items(),key=lambda x:x[1]):
    if n in sy: d=v-sy[n]; h[d]+=1; rows.append((n,v,d))
for n,v,d in rows: print('  %-22s %#x  delta %#x'%(n,v,d))
print(sys.argv[3].split('/')[-4:],'deltas:',[(hex(d),c) for d,c in h.most_common(4)])

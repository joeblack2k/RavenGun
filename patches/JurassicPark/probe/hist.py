import sys,subprocess,collections
sys.path.insert(0,sys.argv[1]); from slots import load
g,sl=load(sys.argv[2]); lo=int(sys.argv[4],16); hi=int(sys.argv[5],16)
vals=sorted(set(v for _,_,_,v in sl if lo<=v<=hi))
out=subprocess.run(['readelf','-sW','--dyn-syms',sys.argv[3]],capture_output=True,text=True).stdout
sy={}
for l in out.splitlines():
    p=l.split()
    if len(p)>=8 and p[3] in('FUNC','IFUNC') and p[6]!='UND': sy.setdefault(int(p[1],16),[]).append(p[7])
h=collections.Counter()
for v in vals:
    for a in sy: h[v-a]+=1
print(len(vals),'values;',len(sy),'func addrs; top deltas:',[(hex(d),n) for d,n in h.most_common(6)])
pa=sorted(((n,hex(d)) for d,n in h.items() if d&0xfff==0),reverse=True)[:6]; print("page-aligned:",pa)
d=h.most_common(1)[0][0]
if len(sys.argv)>6:
    for v in vals: print('  %#x %s'%(v,sy.get(v-d,'-')))

import sys,subprocess,collections,glob,json
sys.path.insert(0,sys.argv[1]); from slots import load
g,sl=load(sys.argv[2]); vals=sorted(set(v for _,_,_,v in sl if 0xb74f0000<=v<=0xb750d000))
res={}
for l in sorted(glob.glob(sys.argv[3]+'/x*/**/libGL.so.*',recursive=True)):
    out=subprocess.run(['readelf','-sW','--dyn-syms',l],capture_output=True,text=True).stdout
    sy={}
    for ln in out.splitlines():
        p=ln.split()
        if len(p)>=8 and p[3]=='FUNC' and p[6]!='UND': sy.setdefault(int(p[1],16),[]).append(p[7])
    h=collections.Counter()
    for v in vals:
        for a in sy:
            if (v-a)&0xfff==0: h[v-a]+=1
    d,n=h.most_common(1)[0]
    ver=l.split('libGL.so.')[1]
    res[ver]=(d,n,{v:tuple(sorted(sy.get(v-d,['-']))) for v in vals})
vers=sorted(res)
ref=[v for v in vers if res[v][1]==126]
print('full-match versions:',ref)
base=res[ref[0]][2]
for v in ref[1:]:
    diff=[(hex(a),base[a],res[v][2][a]) for a in vals if res[v][2][a]!=base[a]]
    print(v,'base %#x'%res[v][0],'name differences vs',ref[0],':',len(diff),diff[:4])
multi=[(hex(a),n) for a,n in base.items() if len(n)>1]
print('addresses with >1 alias:',multi)
json.dump({hex(a):list(n) for a,n in base.items()},open('glnames.json','w'))
print('base of',ref[0],hex(res[ref[0]][0]))

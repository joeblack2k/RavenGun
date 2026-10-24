import sys,subprocess,collections
sys.path.insert(0,sys.argv[1]); from elfp import E
g=E(sys.argv[2]); n=g.nsyms(); guest={}
for i in range(1,n):
    nm,val,sz,info,oth,shn=g.sym(i)
    if shn!=0 and (info>>4) in(1,2) and val: guest[g.symname(i)]=(val,info&15)
paths=[l.strip() for l in open(sys.argv[1]+'/loaded_paths2.txt') if l.strip()]
tot=collections.Counter(); per={}
defs=collections.defaultdict(set)
for p in paths:
    out=subprocess.run(['readelf','-sW','--dyn-syms',p],capture_output=True,text=True).stdout
    und=set()
    for l in out.splitlines():
        q=l.replace('<OS specific>: 10','IFUNC').split()
        if len(q)>=8:
            nm=q[7].split('@')[0]
            if q[6]=='UND': und.add(nm)
            else: defs[nm].add(p.split('/')[-1])
    per[p]=und
print('loaded host objects:',len(paths),'; guest exported defined names:',len(guest))
rows=[]
for p,und in per.items():
    hit=sorted(x for x in und if x in guest)
    if hit: rows.append((len(hit),p.split('/')[-1],hit))
allhit=set()
for c,p,hit in sorted(rows,reverse=True):
    print('  %4d %-28s %s'%(c,p,' '.join(hit[:14])+(' ...' if len(hit)>14 else ''))); allhit|=set(hit)
print('distinct guest names that host libraries import:',len(allhit))
print('guest-defined names that a loaded host lib ALSO defines (collision set):',sum(1 for x in guest if x in defs))

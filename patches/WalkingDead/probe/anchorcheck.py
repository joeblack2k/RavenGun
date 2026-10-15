# anchorcheck.py <probe> <game> <align.json> : every envelope-JMPREL name bound to a stale address against the fit's name(s)
import sys,json
sys.path.insert(0,sys.argv[1]); from slots import load,stale
g,sl=load(sys.argv[2]); A=json.load(open(sys.argv[3])); first=set(v for *_,v in sl)
ok=bad=0
for off,info in g.rels(23,2):
    v=g.u32(off); n=g.symname(info>>8)
    if not stale(v): continue
    r=A.get(hex(v)); names=set(r['aliases'])|{r['name']} if r else set()
    good=n in names; ok+=good; bad+=not good
    print('%-22s %#x %-5s in-first-GOT=%-3s fit: %s %s'%(n,v,'OK' if good else 'BAD','yes' if v in first else 'no',r['lib'] if r else '-',','.join(sorted(names))))
print('anchors',ok+bad,'agree',ok,'disagree',bad)

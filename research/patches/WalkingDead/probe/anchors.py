import sys
sys.path.insert(0,sys.argv[1]); from slots import load,stale
g,sl=load(sys.argv[2]); vals=set(v for *_,v in sl if stale(v))
for off,info in g.rels(23,2):
    v=g.u32(off); n=g.symname(info>>8)
    print('%#x %#010x %-30s %s'%(off,v or 0,n,'IN-FIRST-GOT' if v in vals else ''))

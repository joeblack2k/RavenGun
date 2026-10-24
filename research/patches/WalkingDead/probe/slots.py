import sys,struct
sys.path.insert(0,sys.argv[1]); from elfp import E
# Walking Dead `game` (sha256 efbd9b52...0006): 813 first-PLT stubs at 0x8052a20..0x8055ce0, GOT slots 0x8a2700c..0x8a27cbc
P0=0x8052a20; N=813
def stale(v): return v is not None and 0xb6000000<=v<0xb8000000
def load(path):
    g=E(path); out=[]
    for i in range(N):
        stub=P0+16*i; b=g.bytes(stub,16); assert b[:2]==b'\xff\x25'
        slot,=struct.unpack_from('<I',b,2); out.append((i,stub,slot,g.u32(slot)))
    return g,out
if __name__=='__main__':
    g,sl=load(sys.argv[2])
    vals=sorted(set(v for _,_,_,v in sl if stale(v)))
    print(len(vals),'distinct stale values of',sum(1 for s in sl if stale(s[3])))
    cl=[[vals[0]]]
    for v in vals[1:]:
        if v-cl[-1][-1]>0x30000: cl.append([v])
        else: cl[-1].append(v)
    for c in cl: print('  %#x..%#x span %#8x n=%d  align16=%d'%(c[0],c[-1],c[-1]-c[0],len(c),sum(1 for v in c if v%16==0)))

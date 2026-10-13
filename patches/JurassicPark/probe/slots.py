import sys,struct
sys.path.insert(0,sys.argv[1]); from elfp import E
def load(path):
    g=E(path); out=[]
    for i in range(578):
        stub=0x8139740+16*i; b=g.bytes(stub,16); assert b[:2]==b'\xff\x25'
        slot,=struct.unpack_from('<I',b,2); out.append((i,stub,slot,g.u32(slot)))
    return g,out
if __name__=='__main__':
    g,sl=load(sys.argv[2])
    vals=sorted(set(v for _,_,_,v in sl if v>>24==0xb7))
    print(len(vals),'distinct b7 values of',sum(1 for s in sl if s[3]>>24==0xb7))
    cl=[[vals[0]]]
    for v in vals[1:]:
        if v-cl[-1][-1]>0x30000: cl.append([v])
        else: cl[-1].append(v)
    for c in cl: print('  %#x..%#x span %#8x n=%d  align16=%d'%(c[0],c[-1],c[-1]-c[0],len(c),sum(1 for v in c if v%16==0)))

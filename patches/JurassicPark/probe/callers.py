import sys,struct,bisect,collections,pickle
sys.path.insert(0,sys.argv[1]); from elfp import E
g=E(sys.argv[2])
n=g.nsyms(); fs=[]
for i in range(1,n):
    nm,val,sz,info,oth,shn=g.sym(i)
    if shn!=0 and info&15==2 and 0x813bb60<=val<0x8977200: fs.append((val,g.symname(i)))
fs.sort(); fa=[a for a,_ in fs]
lo,hi=0x813bb60,0x89771d0
t=g.bytes(lo,hi-lo)
P0,P1=0x8139740,0x8139740+578*16
calls=collections.defaultdict(list)
i=t.find(b'\xe8')
while i!=-1 and i+5<=len(t):
    rel,=struct.unpack_from('<i',t,i+1); tgt=(lo+i+5+rel)&0xffffffff
    if P0<=tgt<P1 and (tgt-P0)%16==0:
        k=bisect.bisect_right(fa,lo+i)-1
        calls[(tgt-P0)//16].append((lo+i,fs[k][1] if k>=0 else '?','call'))
    i=t.find(b'\xe8',i+1)
i=t.find(b'\xe9')
while i!=-1 and i+5<=len(t):
    rel,=struct.unpack_from('<i',t,i+1); tgt=(lo+i+5+rel)&0xffffffff
    if P0<=tgt<P1 and (tgt-P0)%16==0:
        k=bisect.bisect_right(fa,lo+i)-1
        calls[(tgt-P0)//16].append((lo+i,fs[k][1] if k>=0 else '?','jmp'))
    i=t.find(b'\xe9',i+1)
# address-taken: imm32 equal to stub address anywhere in image (text+data)
pickle.dump(dict(calls),open(sys.argv[1]+'/callers.pkl','wb'))
nz=sum(1 for k in range(578) if calls.get(k))
print('stubs with >=1 direct call/jmp reference:',nz,'of 578; total refs',sum(len(v) for v in calls.values()))
for k in sys.argv[3:]:
    k=int(k); c=calls.get(k,[])
    print('stub',k,hex(P0+16*k),'refs',len(c),collections.Counter(x[1] for x in c).most_common(8),[hex(x[0]) for x in c[:4]])

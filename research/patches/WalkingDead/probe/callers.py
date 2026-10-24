# callers.py <probe> <game> [stub-index...] : direct call/jmp rel32 references per first-PLT stub (second opinion on a name).
# Scans 0x8055d30 (end of the stubs) .. 0x08959494 (GNU_EH_FRAME); with no section headers .text and .rodata are not
# separated, so a count can include a stray e8/e9 byte in data. Function names come from the 320-entry dynsym (few).
import sys,struct,bisect,collections,pickle
sys.path.insert(0,sys.argv[1]); from elfp import E; from slots import P0,N
g=E(sys.argv[2]); fs=[]
for i in range(1,g.nsyms()):
    nm,val,sz,info,oth,shn=g.sym(i)
    if shn!=0 and info&15==2 and 0x8055d30<=val<0x08959494: fs.append((val,g.symname(i)))
fs.sort(); fa=[a for a,_ in fs]
lo,hi=0x8055d30,0x08959494; t=g.bytes(lo,hi-lo); P1=P0+N*16
calls=collections.defaultdict(list)
for op,kind in ((b'\xe8','call'),(b'\xe9','jmp')):
    i=t.find(op)
    while i!=-1 and i+5<=len(t):
        rel,=struct.unpack_from('<i',t,i+1); tgt=(lo+i+5+rel)&0xffffffff
        if P0<=tgt<P1 and (tgt-P0)%16==0:
            k=bisect.bisect_right(fa,lo+i)-1
            calls[(tgt-P0)//16].append((lo+i,fs[k][1] if k>=0 else '?',kind))
        i=t.find(op,i+1)
pickle.dump(dict(calls),open(sys.argv[1]+'/callers.pkl' if len(sys.argv)<4 or not sys.argv[3].endswith('.pkl') else sys.argv[3],'wb'))
nz=sum(1 for k in range(N) if calls.get(k))
print('stubs with >=1 direct call/jmp reference:',nz,'of',N,'; total refs',sum(len(v) for v in calls.values()))
for k in sys.argv[3:]:
    if k.endswith('.pkl'): continue
    k=int(k); c=calls.get(k,[])
    print('stub',k,hex(P0+16*k),'refs',len(c),collections.Counter(x[1] for x in c).most_common(8),[hex(x[0]) for x in c[:4]])

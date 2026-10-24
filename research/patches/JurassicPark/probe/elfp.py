import struct,sys
class E:
    def __init__(s,path):
        s.d=open(path,'rb').read()
        d=s.d
        (s.entry,s.phoff)=struct.unpack_from('<II',d,24)
        s.phnum=struct.unpack_from('<H',d,44)[0]
        s.ph=[struct.unpack_from('<8I',d,s.phoff+32*i) for i in range(s.phnum)]
        s.loads=[(p[2],p[1],p[4],p[5]) for p in s.ph if p[0]==1]
        dyn=[p for p in s.ph if p[0]==2][0]
        s.dyn=[];o=dyn[1]
        while True:
            t,v=struct.unpack_from('<II',d,o);o+=8
            if t==0:break
            s.dyn.append((t,v))
        s.dt={t:v for t,v in s.dyn if t!=1}
    def off(s,va):
        for v,o,fs,ms in s.loads:
            if v<=va<v+fs: return o+va-v
        return None
    def u32(s,va):
        o=s.off(va)
        return None if o is None else struct.unpack_from('<I',s.d,o)[0]
    def bytes(s,va,n):
        o=s.off(va); return s.d[o:o+n]
    def cstr(s,va):
        o=s.off(va); e=s.d.index(b'\0',o); return s.d[o:e].decode('latin1')
    def sym(s,i):
        o=s.off(s.dt[6])+16*i
        n,val,sz,info,oth,shn=struct.unpack_from('<IIIBBH',s.d,o)
        return n,val,sz,info,oth,shn
    def symname(s,i):
        return s.cstr(s.dt[5]+s.sym(i)[0])
    def nsyms(s):
        return s.u32(s.dt[4]+4)
    def rels(s,tag,sztag):
        o=s.off(s.dt[tag]); n=s.dt[sztag]//8
        return [struct.unpack_from('<II',s.d,o+8*i) for i in range(n)]

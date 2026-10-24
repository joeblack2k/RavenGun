import sys,struct
from elftools.elf.elffile import ELFFile
f=open(sys.argv[1],'rb'); e=ELFFile(f); data=open(sys.argv[1],'rb').read()
segs=[s for s in e.iter_segments()]
def v2o(v):
    for s in segs:
        if s['p_type']=='PT_LOAD' and s['p_vaddr']<=v<s['p_vaddr']+s['p_filesz']:
            return v-s['p_vaddr']+s['p_offset']
dyn=[s for s in segs if s['p_type']=='PT_DYNAMIC'][0]
tags={}
for t in dyn.iter_tags(): tags[t.entry.d_tag]=t.entry.d_val
print({k:hex(v) for k,v in tags.items() if isinstance(v,int)},file=sys.stderr)
symtab=v2o(tags['DT_SYMTAB']); strtab=v2o(tags['DT_STRTAB'])
# number of syms: from DT_HASH nchain
if 'DT_HASH' in tags:
    h=v2o(tags['DT_HASH']); nb,nc=struct.unpack_from('<II',data,h); n=nc
else:
    n=(strtab-symtab)//16
out=[]
for i in range(n):
    name,val,size,info,oth,shn=struct.unpack_from('<IIIBBH',data,symtab+16*i)
    nm=data[strtab+name:data.index(b'\0',strtab+name)].decode('latin1')
    out.append((val,size,info,shn,nm))
for val,size,info,shn,nm in sorted(out):
    print('%08x %6d %02x %5d %s'%(val,size,info,shn,nm))

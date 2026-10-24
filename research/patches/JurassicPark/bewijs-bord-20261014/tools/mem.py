import sys,struct,bisect
from elftools.elf.elffile import ELFFile
G="/home/deck/mnt/spidernas_emulation/Teknoparrot/Jurassic Park/game"
data=open(G,'rb').read()
segs=[s for s in ELFFile(open(G,'rb')).iter_segments() if s['p_type']=='PT_LOAD']
def v2o(v):
    for s in segs:
        if s['p_vaddr']<=v<s['p_vaddr']+s['p_filesz']: return v-s['p_vaddr']+s['p_offset']
    return None
syms=[]
import os, subprocess
_S=os.path.join(os.path.dirname(os.path.abspath(__file__)),'jp-syms.txt')
if not os.path.exists(_S): open(_S,'w').write(subprocess.run([sys.executable,'-I',os.path.join(os.path.dirname(_S),'syms.py'),G],capture_output=True,text=True).stdout)
for l in open(_S):
    p=l.split()
    if len(p)>=5: syms.append((int(p[0],16),int(p[1]),p[4]))
syms.sort(); addrs=[s[0] for s in syms]
def name(a):
    i=bisect.bisect_right(addrs,a)-1
    if i>=0:
        v,sz,nm=syms[i]
        if a==v: return nm
        if a<v+max(sz,1): return '%s+0x%x'%(nm,a-v)
    return ''
def u32(v): o=v2o(v); return None if o is None else struct.unpack_from('<I',data,o)[0]
def cstr(v): o=v2o(v); return data[o:data.index(b'\0',o)].decode('latin1') if o is not None else None

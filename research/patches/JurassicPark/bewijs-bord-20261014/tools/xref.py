import sys,struct
sys.path.insert(0,__file__.rsplit('/',1)[0])
from mem import *
# scan text segment for E8 rel32 calls / E9 jmps to targets
s=segs[0]; base=s['p_vaddr']; off=s['p_offset']; n=s['p_filesz']
targets={}
for a in sys.argv[1:]:
    try: v=int(a,16)
    except ValueError:
        v=[x for x in syms if x[2]==a][0][0]
    targets[v]=name(v) or a
seg=data[off:off+n]
import re
for i in range(len(seg)-5):
    b=seg[i]
    if b in (0xe8,0xe9):
        rel=struct.unpack_from('<i',seg,i+1)[0]
        t=(base+i+5+rel)&0xffffffff
        if t in targets:
            print('%s  %08x %s -> %s'%('call' if b==0xe8 else 'jmp ',base+i,name(base+i),targets[t]))
# also absolute references (push imm / mov imm) to targets
for t,nm in targets.items():
    pat=struct.pack('<I',t)
    for m in re.finditer(re.escape(pat),data):
        print('ref   file+%x %s'%(m.start(),nm))

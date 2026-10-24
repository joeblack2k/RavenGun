import sys,struct,collections,json
sys.path.insert(0,sys.argv[1]); from elfp import E
g=E(sys.argv[2]); o=E(sys.argv[3])
A=json.load(open(sys.argv[1]+'/alignA.json'))
regions=[('ELF hdr/dynsym/dynstr/versions/rel (0x08048000-0x08139730)',0x08048000,0x08139730),('.plt+.text (0x08139730-0x089771f0)',0x08139730,0x089771f0),('.rodata/eh_frame (0x089771f0-0x08adc000)',0x089771f0,0x08adc000),
 ('page-alias + init/fini arrays/.dynamic (0x08adc000-0x08add000)',0x08adc000,0x08add000),('first GOT.PLT (0x08add000-0x08add914)',0x08add000,0x08add914),('.data (0x08add914-0x08b4b900)',0x08add914,0x08b4b900),
 ('.bss incl. COPY targets (0x08b4b900-0x09044000)',0x08b4b900,0x09044000),('envelope code/thunks (0x09044000-0x090e73f8)',0x09044000,0x090e73f8),('envelope GOT (0x090e73f8-0x090e7590)',0x090e73f8,0x090e7590),('envelope data/state (0x090e7590-0x09128000)',0x090e7590,0x09128000)]
jm={off:g.symname(info>>8) for off,info in g.rels(23,2)}
for nm,lo,hi in regions:
    b=g.bytes(lo,hi-lo); hits=[]
    for i in range(0,len(b)-3,4):
        v,=struct.unpack_from('<I',b,i)
        if 0xb7000000<=v<0xb7800000: hits.append((lo+i,v))
    newh=[h for h in hits if o.u32(h[0])!=h[1]]
    print('%-62s aligned b7 dwords: %4d ; of which differing from game_original (=written at runtime on the cabinet): %d'%(nm,len(hits),len(newh)))
    if 'GOT' not in nm or 'envelope' in nm:
        for a,v in newh[:60]:
            r=A.get(hex(v)); print('      %#010x = %#010x  %s %s'%(a,v,jm.get(a,''),(r['lib']+':'+str(r['name'])) if r else ''))
# unaligned scan in data-ish regions, runtime-written only
for nm,lo,hi in regions[5:]:
    b=g.bytes(lo,hi-lo); ob=o.d[o.off(lo):o.off(lo)+hi-lo] if o.off(lo) is not None else b''
    n=0
    for i in range(len(b)-3):
        if i%4 and b[i+3]==0xb7 and b[i:i+4]!=ob[i:i+4] and b[i+2]<0x80: n+=1
    print('unaligned runtime-written b7 dwords in',nm,':',n)

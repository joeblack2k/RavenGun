import sys,re,bisect
syms=[]
for l in open(sys.argv[1]):
    p=l.split()
    if len(p)>=5: syms.append((int(p[0],16),int(p[1]),p[4]))
syms.sort(); addrs=[s[0] for s in syms]
def name(a):
    i=bisect.bisect_right(addrs,a)-1
    if i>=0:
        v,sz,nm=syms[i]
        if a==v: return nm
        if a<v+max(sz,1): return '%s+0x%x'%(nm,a-v)
    return None
for line in sys.stdin:
    line=line.rstrip('\n')
    m=re.search(r'(call|jmp|j[a-z]+)\s+0x([0-9a-f]+)',line)
    extra=''
    if m:
        n=name(int(m.group(2),16))
        if n: extra='   <'+n+'>'
    for mm in re.finditer(r'\$0x([0-9a-f]{7,8})|0x(8[0-9a-f]{6})\b',line):
        v=int(mm.group(1) or mm.group(2),16)
        n=name(v)
        if n and not m: extra+='   ['+n+']'
    print(line+extra)

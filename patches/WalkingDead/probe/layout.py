# layout.py <probe> <align.json> <root-14.04.5> <libGL file> : per fitted library, base + mapped size (PT_LOAD end, page
# rounded) against the next library's base. A consistent cabinet layout has no overlap; small gaps are expected.
import sys,json,subprocess,os,re
sys.path.insert(0,sys.argv[1])
A=json.load(open(sys.argv[2])); R=sys.argv[3]; L=R+'/lib/i386-linux-gnu'; U=R+'/usr/lib/i386-linux-gnu'
TP=os.path.expanduser('~/.local/share/ravengun/runtime/TeknoParrotUi-2.0.0.130-linux-x64/ElfLdr2/libs')
F={'libc.so.6':L+'/libc-2.19.so','libpthread.so.0':L+'/libpthread-2.19.so','libm.so.6':L+'/libm-2.19.so','libdl.so.2':L+'/libdl-2.19.so',
 'libgcc_s.so.1':L+'/libgcc_s.so.1','libz.so.1':L+'/libz.so.1.2.8','libssl.so.1.0.0':L+'/libssl.so.1.0.0','libcrypto.so.1.0.0':L+'/libcrypto.so.1.0.0',
 'libstdc++.so.6':U+'/libstdc++.so.6.0.19','libX11.so.6':U+'/libX11.so.6.3.0','libXext.so.6':U+'/libXext.so.6.4.0','libasound.so.2':U+'/libasound.so.2.0.0',
 'libcaca.so.0':U+'/libcaca.so.0.99.18','libpulse.so.0':U+'/libpulse.so.0.16.2','libpulse-simple.so.0':U+'/libpulse-simple.so.0.0.4',
 'libGLU.so.1':U+'/libGLU.so.1.3.1','libcsv.so':TP+'/libcsv.so','libGL.so.1':sys.argv[4]}
bases={}
for r in A.values(): bases.setdefault(r['lib'],set()).add(r['shift'])
rows=[]
for lib,bs in bases.items():
    assert len(bs)==1,(lib,bs); b=bs.pop()
    out=subprocess.run(['readelf','-lW',F[lib]],capture_output=True,text=True).stdout
    end=max(int(p[2],16)+int(p[5],16) for p in (l.split() for l in out.splitlines()) if p and p[0]=='LOAD')
    rows.append((b,lib,(end+0xfff)&~0xfff))
rows.sort()
for i,(b,lib,sz) in enumerate(rows):
    nxt=rows[i+1][0] if i+1<len(rows) else None
    gap=(nxt-(b+sz)) if nxt else 0
    print('%#010x  %-22s mapped %#8x  ends %#010x  next %s  gap %#x %s'%(b,lib,sz,b+sz,'%#010x'%nxt if nxt else '-',gap,'OVERLAP' if gap<0 else ''))

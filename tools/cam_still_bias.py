import json,sys,math,bisect,statistics as st
R=[];O=[]
for l in open(sys.argv[1]):
    o=json.loads(l)
    if o.get('k')=='r': R.append((o['t']/1e9,[(o['g'][i]+o['g'][i+3])/2 for i in range(3)],[(o['a'][i]+o['a'][i+3])/2 for i in range(3)]))
    elif o.get('k')=='o' and o.get('v'): O.append((o['tf']/1e9,o['x'],o['y'],o['r']))
bias=[math.radians(x) for x in (1.45,-2.33,0.75)]
rt=[r[0] for r in R]
print('rapporten',len(R),'geldige frames',len(O),'duur %.0f s'%(R[-1][0]-R[0][0]))
for thr in (0.15,0.3,0.6,1.0):
    res=[];w=0.5;i=0
    while i<len(O):
        j=i
        while j<len(O) and O[j][0]-O[i][0]<w: j+=1
        seg=O[i:j]
        if len(seg)>=24 and seg[-1][0]-seg[0][0]>0.4:
            sx=st.pstdev([s[1] for s in seg]); sy=st.pstdev([s[2] for s in seg]); sr=st.pstdev([s[3] for s in seg])
            if max(sx,sy)<thr and sr<thr:
                a=bisect.bisect_left(rt,seg[0][0]); b=bisect.bisect_right(rt,seg[-1][0])
                if b-a>=20:
                    g=[sum(R[k][1][c] for k in range(a,b))/(b-a)-bias[c] for c in range(3)]
                    up=[sum(R[k][2][c] for k in range(a,b))/(b-a) for c in range(3)]; n=math.sqrt(sum(u*u for u in up)); up=[u/n for u in up]
                    res.append((seg[0][0],math.degrees(sum(g[c]*up[c] for c in range(3))),[math.degrees(x) for x in g]))
            i=j
        else: i+=1
    if res:
        v=[r[1] for r in res]
        print('bol stil < %.2f px: %d vensters (%.0f s); rest-draaisnelheid om verticaal: mediaan %+.3f, gem %+.3f, sd %.3f gr/s; mediaan xyz %+.2f %+.2f %+.2f'%(thr,len(res),len(res)*w,st.median(v),st.mean(v),st.pstdev(v),*[st.median([r[2][c] for r in res]) for c in range(3)]))
        if thr==0.3: print('   tijden:',[round(r[0]) for r in res][:40])

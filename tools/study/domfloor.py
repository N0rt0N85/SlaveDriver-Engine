import sys, os, glob
from collections import Counter, defaultdict
sys.path.insert(0, os.path.join(os.getcwd(), 'tools'))
import lev
print("%-14s %7s %7s %7s %7s %7s %7s"%("LEV","fcells","top1H%","top3H%","top1HT%","nH","nHT"))
agg=[]
for p in sorted(glob.glob('refs/extract/PS/*.LEV')):
    r=lev.Reader(p); lev.parse_sky(r); L=lev.parse_level_block(r)
    tex=L['texture']
    byH=Counter(); byHT=Counter(); tot=0
    for si,s in enumerate(L['sectors']):
        for wi in range(s['firstWall'], s['lastWall']+1):
            w=L['walls'][wi]; n=w['normal']
            ax=[abs(n[0]),abs(n[1]),abs(n[2])]
            if not (ax[1]>=max(ax[0],ax[2]) and n[1]>0): continue
            if not (w['flags']&0x01): 
                cells=w['lastFace']-w['firstFace']+1
                byH[s['floorLevel']]+=cells; tot+=cells
                continue
            nc=w['tileLength']*w['tileHeight']
            base=w['textures']
            for k in range(nc):
                t=tex[base+2*k+1]
                byHT[(s['floorLevel'],t)]+=1
            byH[s['floorLevel']]+=nc; tot+=nc
    if tot==0: continue
    t1=byH.most_common(1)[0][1]; t3=sum(v for _,v in byH.most_common(3))
    t1t=byHT.most_common(1)[0][1] if byHT else 0
    print("%-14s %7d %6.1f%% %6.1f%% %6.1f%% %7d %7d"%(os.path.basename(p),tot,100*t1/tot,100*t3/tot,100*t1t/tot,len(byH),len(byHT)))
    agg.append((100*t1/tot,100*t3/tot,100*t1t/tot))
import statistics
print("median top1H %.1f%%  top3H %.1f%%  top1(H,tile) %.1f%%"%tuple(statistics.median([a[i] for a in agg]) for i in range(3)))

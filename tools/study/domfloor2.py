import sys, os, glob, statistics
from collections import Counter
sys.path.insert(0, os.path.join(os.getcwd(),'tools'))
import lev
def cells_and_tiles(L, w):
    tex=L['texture']
    if w['flags']&0x01:
        nc=w['tileLength']*w['tileHeight']; base=w['textures']
        return [tex[base+2*k+1] for k in range(nc)]
    return [L['faces'][i]['tile'] for i in range(w['firstFace'], w['lastFace']+1)]
print("%-14s %7s %7s %7s %7s | %7s %7s"%("LEV","Fcells","top1H%","top1HT%","top3HT%","Ccells","c1HT%"))
agg=[]
for p in sorted(glob.glob('refs/extract/PS/*.LEV')):
    r=lev.Reader(p); lev.parse_sky(r); L=lev.parse_level_block(r)
    byH=Counter(); byHT=Counter(); tot=0
    cHT=Counter(); ctot=0
    for si,s in enumerate(L['sectors']):
        for wi in range(s['firstWall'], s['lastWall']+1):
            w=L['walls'][wi]; n=w['normal']
            ax=[abs(n[0]),abs(n[1]),abs(n[2])]
            if ax[1]<max(ax[0],ax[2]): continue
            ts=cells_and_tiles(L,w)
            if n[1]>0:
                byH[s['floorLevel']]+=len(ts); tot+=len(ts)
                for t in ts: byHT[(s['floorLevel'],t)]+=1
            else:
                # ceiling height: use plane d/normal
                h=w['d']//(n[1] if n[1] else 1)
                ctot+=len(ts)
                for t in ts: cHT[(h,t)]+=1
    if not tot: continue
    t1=byH.most_common(1)[0][1]; t1t=byHT.most_common(1)[0][1]
    t3t=sum(v for _,v in byHT.most_common(3))
    c1=cHT.most_common(1)[0][1] if cHT else 0
    print("%-14s %7d %6.1f%% %6.1f%% %6.1f%% | %7d %6.1f%%"%(os.path.basename(p),tot,100*t1/tot,100*t1t/tot,100*t3t/tot,ctot,100*c1/max(ctot,1)))
    agg.append((100*t1/tot,100*t1t/tot,100*t3t/tot,100*c1/max(ctot,1)))
print("MEDIAN top1H %.1f%%  top1(H,tile) %.1f%%  top3(H,tile) %.1f%%  ceil top1 %.1f%%"%tuple(statistics.median([a[i] for a in agg]) for i in range(4)))

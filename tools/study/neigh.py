import sys, os, glob, statistics
from collections import Counter, deque
sys.path.insert(0, os.path.join(os.getcwd(),'tools'))
import lev
def tiles_of(L,w):
    tex=L['texture']
    if w['flags']&0x01:
        nc=w['tileLength']*w['tileHeight']; b=w['textures']
        return [tex[b+2*k+1] for k in range(nc)]
    return [L['faces'][i]['tile'] for i in range(w['firstFace'], w['lastFace']+1)]
print("%-14s %8s %8s %8s"%("LEV","hop1%","hop2%","hop3%"))
allr=[]
for p in sorted(glob.glob('refs/extract/PS/*.LEV')):
    r=lev.Reader(p); lev.parse_sky(r); L=lev.parse_level_block(r)
    nsec=len(L['sectors'])
    adj=[[] for _ in range(nsec)]
    finfo=[]   # (height, Counter(tiles))
    for si,s in enumerate(L['sectors']):
        c=Counter(); h=s['floorLevel']
        for wi in range(s['firstWall'], s['lastWall']+1):
            w=L['walls'][wi]; n=w['normal']
            if w['nextSector']>=0: adj[si].append(w['nextSector'])
            ax=[abs(n[0]),abs(n[1]),abs(n[2])]
            if ax[1]>=max(ax[0],ax[2]) and n[1]>0:
                for t in tiles_of(L,w): c[t]+=1
        finfo.append((h,c))
    res={1:[],2:[],3:[]}
    for si in range(nsec):
        h,c=finfo[si]
        if not c: continue
        dom=c.most_common(1)[0][0]
        seen={si}; frontier=[si]
        for d in (1,2,3):
            nf=[]
            for x in frontier:
                for y in adj[x]:
                    if 0<=y<nsec and y not in seen: seen.add(y); nf.append(y)
            frontier=nf
            tot=0; match=0
            for x in seen:
                hh,cc=finfo[x]
                for t,k in cc.items():
                    tot+=k
                    if hh==h and t==dom: match+=k
            if tot: res[d].append(100.0*match/tot)
    row=(os.path.basename(p),)+tuple(statistics.median(res[d]) if res[d] else 0 for d in (1,2,3))
    print("%-14s %7.1f%% %7.1f%% %7.1f%%"%row); allr.append(row)
print("MEDIAN OF MEDIANS hop1 %.1f%% hop2 %.1f%% hop3 %.1f%%"%tuple(statistics.median([r[i] for r in allr]) for i in (1,2,3)))

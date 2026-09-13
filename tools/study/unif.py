import sys, os, glob, statistics
from collections import Counter
sys.path.insert(0, os.path.join(os.getcwd(),'tools'))
import lev
rows=[]
for p in sorted(glob.glob('refs/extract/PS/*.LEV')):
    r=lev.Reader(p); lev.parse_sky(r); L=lev.parse_level_block(r); tex=L['texture']
    uni=0; tot=0; unicells=0; totcells=0; big=Counter()
    for si,s in enumerate(L['sectors']):
        for wi in range(s['firstWall'], s['lastWall']+1):
            w=L['walls'][wi]; n=w['normal']
            ax=[abs(n[0]),abs(n[1]),abs(n[2])]
            if not (ax[1]>=max(ax[0],ax[2]) and n[1]>0): continue
            if not (w['flags']&0x01): continue
            nc=w['tileLength']*w['tileHeight']; base=w['textures']
            tiles={tex[base+2*k+1] for k in range(nc)}
            tot+=1; totcells+=nc
            if len(tiles)==1:
                uni+=1; unicells+=nc
                big[(s['floorLevel'], next(iter(tiles)))]+=nc
    if tot:
        t1=big.most_common(1)[0][1] if big else 0
        rows.append((os.path.basename(p), tot, 100*uni/tot, 100*unicells/totcells, 100*t1/totcells))
print("%-14s %5s %8s %9s %10s"%("LEV","nFloor","uniW%","uniCell%","bestUni%"))
for r_ in rows: print("%-14s %5d %7.1f%% %8.1f%% %9.1f%%"%r_)
print("median uniW %.1f%%  uniCell %.1f%%  bestUni %.1f%%"%tuple(statistics.median([r_[i] for r_ in rows]) for i in (2,3,4)))

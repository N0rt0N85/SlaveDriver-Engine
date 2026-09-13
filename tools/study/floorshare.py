import sys, os, glob, json
sys.path.insert(0, os.path.join(os.getcwd(), 'tools'))
import lev
rows=[]
tot={'v':0,'f':0,'c':0,'vc':0,'fc':0,'cc':0}
for p in sorted(glob.glob('refs/extract/PS/*.LEV')):
    r=lev.Reader(p); lev.parse_sky(r); L=lev.parse_level_block(r)
    nv=nf=nc=0; cv=cf=cc=0
    for w in L['walls']:
        n=w['normal']
        ax=[abs(n[0]),abs(n[1]),abs(n[2])]
        vert = ax[1]>=max(ax[0],ax[2])
        if w['flags']&0x01:
            cells=w['tileLength']*w['tileHeight']
        else:
            cells=w['lastFace']-w['firstFace']+1
        if not vert: nv+=1; cv+=cells
        elif n[1]>0: nf+=1; cf+=cells
        else: nc+=1; cc+=cells
    rows.append((os.path.basename(p), L['header']['nmSectors'], nv,nf,nc, cv,cf,cc))
    tot['v']+=nv; tot['f']+=nf; tot['c']+=nc; tot['vc']+=cv; tot['fc']+=cf; tot['cc']+=cc
print("%-14s %5s %6s %6s %6s | %8s %8s %8s  %s"%("LEV","sect","wall","floor","ceil","cellW","cellF","cellC","F+C%"))
for n,s,nv,nf,nc,cv,cf,cc in rows:
    tt=cv+cf+cc
    print("%-14s %5d %6d %6d %6d | %8d %8d %8d  %5.1f%%"%(n,s,nv,nf,nc,cv,cf,cc,100.0*(cf+cc)/tt))
tt=tot['vc']+tot['fc']+tot['cc']
print("TOTAL walls v/f/c = %d/%d/%d ; cells %d/%d/%d ; floor+ceil share of cells = %.1f%% (floor %.1f%%, ceil %.1f%%)"%(
  tot['v'],tot['f'],tot['c'],tot['vc'],tot['fc'],tot['cc'],100.0*(tot['fc']+tot['cc'])/tt,100.0*tot['fc']/tt,100.0*tot['cc']/tt))

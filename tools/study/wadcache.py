import struct, statistics
from collections import Counter, defaultdict
p=r'C:\Users\pcico\Projects\Mimas\cd\data\DOOM1.WAD'
b=open(p,'rb').read()
magic,n,off=struct.unpack('<4sii',b[:12])
dirs=[]
for i in range(n):
    fo,sz,nm=struct.unpack('<ii8s',b[off+16*i:off+16*i+16])
    dirs.append((nm.rstrip(b'\0').decode('latin1'),fo,sz))
maps=[i for i,(nm,_,_) in enumerate(dirs) if len(nm)==4 and nm[0]=='E' and nm[2]=='M']
def S(x): return x.rstrip(b'\0').decode('latin1')
print("%-6s %7s %7s %7s %7s"%("MAP","hop0","hop1","hop2","hop3"))
rows=[]
for i in maps:
    d={}
    for j in range(i+1,min(i+12,len(dirs))):
        nm,fo,sz=dirs[j]
        if nm in ('SIDEDEFS','SECTORS','LINEDEFS'): d[nm]=(fo,sz)
    fo,sz=d['SIDEDEFS']; sides=[]
    for k in range(sz//30):
        r=b[fo+30*k:fo+30*k+30]
        sides.append((S(r[4:12]),S(r[12:20]),S(r[20:28]),struct.unpack('<h',r[28:30])[0]))
    fo,sz=d['SECTORS']; nsec=sz//26; secflat=[]
    for k in range(nsec):
        r=b[fo+26*k:fo+26*k+26]; secflat.append((S(r[4:12]),S(r[12:20])))
    fo,sz=d['LINEDEFS']; adj=defaultdict(set); sectex=defaultdict(set)
    for k in range(nsec): sectex[k]|= {t for t in secflat[k] if t and t!='-'}
    for k in range(sz//14):
        r=b[fo+14*k:fo+14*k+14]
        sr,sl=struct.unpack('<hh',r[10:14])
        for sd in (sr,sl):
            if sd<0 or sd>=len(sides): continue
            u,l,m,sec=sides[sd]
            for t in (u,l,m):
                if t and t!='-': sectex[sec].add(t)
        if sr>=0 and sl>=0 and sr<len(sides) and sl<len(sides):
            a,bb=sides[sr][3],sides[sl][3]
            adj[a].add(bb); adj[bb].add(a)
    res={0:[],1:[],2:[],3:[]}
    for s0 in range(nsec):
        seen={s0}; fr=[s0]
        res[0].append(len(sectex[s0]))
        for dd in (1,2,3):
            nf=[]
            for x in fr:
                for y in adj[x]:
                    if y not in seen: seen.add(y); nf.append(y)
            fr=nf
            u=set()
            for x in seen: u|=sectex[x]
            res[dd].append(len(u))
    row=(dirs[i][0],)+tuple(statistics.median(res[k]) for k in range(4))
    print("%-6s %7.0f %7.0f %7.0f %7.0f"%row); rows.append(row)
print("MEDIAN  %7.0f %7.0f %7.0f %7.0f   (VDP1 cache = 28 slots)"%tuple(statistics.median([r[k+1] for r in rows]) for k in range(4)))

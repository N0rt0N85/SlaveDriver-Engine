import struct,sys,collections
p=sys.argv[1]
b=open(p,'rb').read()
magic,n,off=struct.unpack('<4sii',b[:12])
dirs=[]
for i in range(n):
    fo,sz,nm=struct.unpack('<ii8s',b[off+16*i:off+16*i+16])
    dirs.append((nm.rstrip(b'\0').decode('latin1'),fo,sz))
print("WAD",p,"lumps",n,"bytes",len(b),magic)
import re
ismap=lambda nm: re.fullmatch(r'E\dM\d|MAP\d\d',nm) is not None
maps=[i for i,(nm,_,_) in enumerate(dirs) if ismap(nm)]
# SH-2 runtime struct sizes (Mimas r_defs.h / p_mobj.h)
SZ=dict(vertex=8,seg=14,ssec=8,node=28,sector=88,line=24,side=16,mobj=156)
if len(sys.argv)>2: 
    for kv in sys.argv[2].split(','):
        k,v=kv.split('='); SZ[k]=int(v)
MONST={3004:'POSS',9:'SPOS',3001:'TROO',3002:'SARG',58:'SARG',3005:'HEAD',3003:'BOSS',3006:'SKUL',65:'CPOS',69:'BOS2',3005:'HEAD',64:'VILE',66:'SKEL',67:'FATT',68:'BSPI',71:'PAIN',7:'SPID',16:'CYBR',84:'SSWV',88:'BBRN'}
print("%-6s %5s %5s %5s %4s %4s %5s %4s %5s | %6s %6s | %7s %7s %7s | %5s %5s"%("MAP","vert","line","side","sect","ssec","segs","node","thing","blkmap","reject","runtime","+mobj","total","alive","mons"))
rows=[]
for i in maps:
    d={}
    for j in range(i+1,min(i+12,len(dirs))):
        nm,fo,sz=dirs[j]
        if nm in ('THINGS','LINEDEFS','SIDEDEFS','VERTEXES','SEGS','SSECTORS','NODES','SECTORS','REJECT','BLOCKMAP'): d[nm]=(fo,sz)
        elif ismap(nm): break
    c=lambda k,e: d.get(k,(0,0))[1]//e
    nv,nl,nsd,nsec,nss,nsg,nnd,nth=c('VERTEXES',4),c('LINEDEFS',14),c('SIDEDEFS',30),c('SECTORS',26),c('SSECTORS',4),c('SEGS',12),c('NODES',28),c('THINGS',10)
    blk=d.get('BLOCKMAP',(0,0))[1]; rej=d.get('REJECT',(0,0))[1]
    # things: count skill-agnostic alive monsters (all flags) and total mobjs spawned in single player (skill 4: bit 2 'hard' (flag 4)), excluding deathmatch-only (flag 16) and player starts 1..4,11 and deathmatch starts
    fo,sz=d['THINGS']; mons=collections.Counter(); alive=0
    for k in range(sz//10):
        x,y,ang,typ,flg=struct.unpack('<hhhHH',b[fo+10*k:fo+10*k+10])
        if flg&16: continue
        if not (flg&4): continue  # skill 4/5 'hard' bit
        if typ in (1,2,3,4,11): continue
        alive+=1
        if typ in MONST: mons[MONST[typ]]+=1
    rt=nv*SZ['vertex']+nsg*SZ['seg']+nss*SZ['ssec']+nnd*SZ['node']+nsec*SZ['sector']+nl*SZ['line']+nsd*SZ['side']+blk+rej
    # sector->lines pointer table: 4 bytes per (line,side) reference
    rt+= (nl + sum(1 for _ in range(0)))*4*2  # approx 2 refs per line
    mob=alive*SZ['mobj']
    rows.append((dirs[i][0],nv,nl,nsd,nsec,nss,nsg,nnd,nth,blk,rej,rt,mob,rt+mob,alive,sum(mons.values()),mons))
    print("%-6s %5d %5d %5d %4d %4d %5d %4d %5d | %6d %6d | %7d %7d %7d | %5d %5d %s"%(rows[-1][:16]+(dict(mons),)))

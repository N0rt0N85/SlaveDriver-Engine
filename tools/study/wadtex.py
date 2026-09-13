import struct, sys, statistics
from collections import Counter
p=r'C:\Users\pcico\Projects\Mimas\cd\data\DOOM1.WAD'
b=open(p,'rb').read()
magic,n,off=struct.unpack('<4sii',b[:12])
dirs=[]
for i in range(n):
    fo,sz,nm=struct.unpack('<ii8s',b[off+16*i:off+16*i+16])
    dirs.append((nm.rstrip(b'\0').decode('latin1'),fo,sz))
maps=[i for i,(nm,_,_) in enumerate(dirs) if len(nm)==4 and nm[0]=='E' and nm[2]=='M']
print("%-6s %6s %6s %6s  %s"%("MAP","wallTx","flats","total","per-sector distinct(flat pair)"))
allt=[]
for i in maps:
    d={}
    for j in range(i+1,min(i+12,len(dirs))):
        nm,fo,sz=dirs[j]
        if nm in ('SIDEDEFS','SECTORS'): d[nm]=(fo,sz)
        elif len(nm)==4 and nm[0]=='E' and j>i: break
    fo,sz=d['SIDEDEFS']; tx=Counter()
    for k in range(sz//30):
        r=b[fo+30*k:fo+30*k+30]
        for s in (r[4:12],r[12:20],r[20:28]):
            s=s.rstrip(b'\0').decode('latin1')
            if s and s!='-': tx[s]+=1
    fo,sz=d['SECTORS']; fl=Counter()
    for k in range(sz//26):
        r=b[fo+26*k:fo+26*k+26]
        for s in (r[4:12],r[12:20]):
            fl[s.rstrip(b'\0').decode('latin1')]+=1
    print("%-6s %6d %6d %6d"%(dirs[i][0],len(tx),len(fl),len(tx)+len(fl)))
    allt.append(len(tx)+len(fl))
print("median total distinct %d, max %d"%(statistics.median(allt),max(allt)))

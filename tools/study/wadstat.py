import struct, sys
p=sys.argv[1] if len(sys.argv)>1 else r'C:\Users\pcico\Projects\Mimas\cd\data\DOOM1.WAD'
b=open(p,'rb').read()
magic,n,off=struct.unpack('<4sii',b[:12])
dirs=[]
for i in range(n):
    fo,sz,nm=struct.unpack('<ii8s',b[off+16*i:off+16*i+16])
    dirs.append((nm.rstrip(b'\0').decode('latin1'),fo,sz))
maps=[i for i,(nm,_,_) in enumerate(dirs) if len(nm)==4 and nm[0]=='E' and nm[2]=='M']
print("%-6s %6s %6s %6s %6s %6s %6s %7s"%("MAP","verts","lines","sides","sect","ssec","segs","nodes"))
tot=[]
for i in maps:
    d={}
    for j in range(i+1,min(i+12,len(dirs))):
        nm,fo,sz=dirs[j]
        if nm in ('THINGS','LINEDEFS','SIDEDEFS','VERTEXES','SEGS','SSECTORS','NODES','SECTORS','REJECT','BLOCKMAP'): d[nm]=sz
        elif len(nm)==4 and nm[0]=='E': break
    r=(dirs[i][0], d.get('VERTEXES',0)//4, d.get('LINEDEFS',0)//14, d.get('SIDEDEFS',0)//30,
       d.get('SECTORS',0)//26, d.get('SSECTORS',0)//4, d.get('SEGS',0)//12, d.get('NODES',0)//28)
    print("%-6s %6d %6d %6d %6d %6d %6d %7d"%r); tot.append(r)
import statistics
print("median ssec %d  segs %d  sect %d  lines %d"%tuple(statistics.median([t[i] for t in tot]) for i in (5,6,4,2)))
print("max    ssec %d  segs %d"%(max(t[5] for t in tot), max(t[6] for t in tot)))

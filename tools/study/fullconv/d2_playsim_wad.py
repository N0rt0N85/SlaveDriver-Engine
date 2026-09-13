import struct, sys, re
MAPL = ['THINGS','LINEDEFS','SIDEDEFS','VERTEXES','SEGS','SSECTORS','NODES','SECTORS','REJECT','BLOCKMAP']
def rd(p):
    b=open(p,'rb').read(); n,o=struct.unpack_from('<ii',b,4)
    d=[]
    for i in range(n):
        fo,sz,nm=struct.unpack_from('<ii8s',b,o+16*i); d.append((nm.rstrip(b'\0').decode('latin1'),fo,sz))
    return d
for path in sys.argv[1:]:
    d=rd(path); names=[x[0] for x in d]; sizes={}
    print("==",path,len(d),"lumps")
    maps=[]; i=0
    while i<len(d):
        if re.fullmatch(r'E\dM\d|MAP\d\d',d[i][0]):
            m=d[i][0]; parts={}
            j=i+1
            while j<len(d) and d[j][0] in MAPL: parts[d[j][0]]=d[j][2]; j+=1
            tot=sum(parts.values()); nosegs=tot-parts.get('SEGS',0)
            maps.append((m,tot,nosegs,parts)); i=j
        else: i+=1
    for m,tot,nosegs,p in maps:
        print("%-6s total %7d  sans SEGS %7d  (SEGS %6d NODES %6d BLOCKMAP %6d REJECT %5d THINGS %5d)"%(m,tot,nosegs,p.get('SEGS',0),p.get('NODES',0),p.get('BLOCKMAP',0),p.get('REJECT',0),p.get('THINGS',0)))
    tm=sum(x[1] for x in maps); tn=sum(x[2] for x in maps)
    print("cartes: %d, total %d, sans SEGS %d, max sans SEGS %d"%(len(maps),tm,tn,max(x[2] for x in maps)))
    def tot(pred): return sum(sz for nm,fo,sz in d if pred(nm))
    for lab,pred in [("TEXTURE1/2+PNAMES",lambda n:n in('TEXTURE1','TEXTURE2','PNAMES')),("PLAYPAL",lambda n:n=='PLAYPAL'),
                     ("2D M_/ST/WI/STCFN/AMMNUM/HELP/TITLEPIC/CREDIT/INTERPIC/VICTORY2/ENDPIC/BOSSBACK/PFUB",
                      lambda n:n.startswith(('M_','ST','WI','AMMNUM','HELP','TITLEPIC','CREDIT','INTERPIC','VICTORY2','ENDPIC','BOSSBACK','PFUB','END','BRDR')) and not n.startswith('STEP')),
                     ("DS*",lambda n:n.startswith('DS')),("D_*",lambda n:n.startswith('D_')),("DEMO",lambda n:n.startswith('DEMO')),("ENDOOM",lambda n:n=='ENDOOM')]:
        print("  %-70s %8d"%(lab,tot(pred)))
    # texture count
    if 'TEXTURE1' in names:
        b=open(path,'rb').read()
        for t in ('TEXTURE1','TEXTURE2'):
            if t in names:
                k=names.index(t); fo,sz=d[k][1],d[k][2]; nt=struct.unpack_from('<i',b,fo)[0]; print("  %s: %d textures"%(t,nt))
        k=names.index('PNAMES'); fo=d[k][1]; print("  PNAMES: %d patches"%struct.unpack_from('<i',b,fo)[0])

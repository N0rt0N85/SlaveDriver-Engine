import struct,sys,re,collections
p=sys.argv[1]
b=open(p,'rb').read()
magic,n,off=struct.unpack('<4sii',b[:12])
dirs=[]
for i in range(n):
    fo,sz,nm=struct.unpack('<ii8s',b[off+16*i:off+16*i+16])
    dirs.append((nm.rstrip(b'\0').decode('latin1'),fo,sz))
names=[d[0] for d in dirs]
def span(a,bn):
    try: s=names.index(a); e=names.index(bn); return set(range(s+1,e))
    except ValueError: return set()
S=span('S_START','S_END'); F=span('F_START','F_END'); P=span('P_START','P_END')
ismap=lambda nm: re.fullmatch(r'E\dM\d|MAP\d\d',nm) is not None
MAPL={'THINGS','LINEDEFS','SIDEDEFS','VERTEXES','SEGS','SSECTORS','NODES','SECTORS','REJECT','BLOCKMAP'}
cat=collections.Counter(); cnt=collections.Counter()
spr=collections.Counter(); sprn=collections.Counter()
snd=collections.Counter()
g2d=[]
for i,(nm,fo,sz) in enumerate(dirs):
    if i in S: c='sprite'; spr[nm[:4]]+=sz; sprn[nm[:4]]+=1
    elif i in F: c='flat'
    elif i in P: c='patch'
    elif ismap(nm) or nm in MAPL: c='map'
    elif nm.startswith('DS'): c='sfx'; snd[nm]=sz
    elif nm.startswith('DP'): c='pcspk'
    elif nm.startswith('D_'): c='music'
    elif nm in ('PLAYPAL','COLORMAP','TEXTURE1','TEXTURE2','PNAMES','GENMIDI','DMXGUS','ENDOOM') or nm.startswith('DEMO'): c='meta:'+nm
    elif sz==0: c='marker'
    else: c='2d'; g2d.append((nm,sz))
    cat[c]+=sz; cnt[c]+=1
for c,v in sorted(cat.items(), key=lambda kv:-kv[1]): print("%-16s %4d lumps %9d bytes"%(c,cnt[c],v))
print("--- sprites by prefix (bytes, lumps) ---")
for k,v in sorted(spr.items(), key=lambda kv:-kv[1]): print("  %s %7d %3d"%(k,v,sprn[k]))
print("--- 2D graphics lumps grouped ---")
g=collections.Counter(); gn=collections.Counter()
for nm,sz in g2d:
    k = 'STCFN font' if nm.startswith('STCFN') else 'ST* HUD' if nm.startswith('ST') else 'M_ menu' if nm.startswith('M_') else 'WI* intermission' if nm.startswith('WI') else 'AMMNUM' if nm.startswith('AMMNUM') else 'BRDR' if nm.startswith('BRDR') else nm
    g[k]+=sz; gn[k]+=1
for k,v in sorted(g.items(), key=lambda kv:-kv[1]): print("  %-18s %7d %3d"%(k,v,gn[k]))
print("--- sfx total %d bytes in %d lumps; largest: %s"%(sum(snd.values()),len(snd),sorted(snd.items(),key=lambda kv:-kv[1])[:5]))
# music: per lump
mus=[(nm,sz) for nm,fo,sz in dirs if nm.startswith('D_')]
print("--- music %d lumps, total %d, max %s, e1 set %d"%(len(mus),sum(s for _,s in mus),max(mus,key=lambda x:x[1]), sum(s for nm,s in mus if nm in ('D_E1M1','D_E1M2','D_E1M3','D_E1M4','D_E1M5','D_E1M6','D_E1M7','D_E1M8','D_E1M9','D_INTER','D_INTRO','D_VICTOR','D_INTROA'))))

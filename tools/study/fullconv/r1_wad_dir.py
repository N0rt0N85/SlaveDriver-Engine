import struct, sys, re, collections
p = sys.argv[1]
d = open(p,'rb').read()
ident, n, off = struct.unpack('<4sII', d[:12])
lumps=[]
for i in range(n):
    fp, sz, nm = struct.unpack('<II8s', d[off+16*i:off+16*i+16])
    lumps.append((nm.rstrip(b'\0').decode('ascii','replace'), fp, sz))
print("ident",ident,"n",n,"filesize",len(d))
cats = collections.OrderedDict([
 ('menus M_*', r'^M_'),
 ('font STCFN*', r'^STCFN'),
 ('statusbar STBAR/STARMS/STTNUM/STYSNUM/STGNUM/STKEYS/STTPRCNT/STTMINUS', r'^ST(BAR|ARMS|TNUM|YSNUM|GNUM|KEYS|TPRCNT|TMINUS)'),
 ('face STF*', r'^STF'),
 ('intermission WI* (non WIA)', r'^WI(?!A)'),
 ('intermission anims WIA*', r'^WIA'),
 ('fullscreen TITLEPIC/HELP1/HELP2/CREDIT/INTERPIC/VICTORY2/PFUB1/PFUB2/ENDPIC', r'^(TITLEPIC|HELP1|HELP2|HELP|CREDIT|INTERPIC|VICTORY2|PFUB1|PFUB2|ENDPIC|BOSSBACK)$'),
 ('automap AMMNUM*', r'^AMMNUM'),
 ('brdr BRDR*', r'^BRDR'),
 ('sounds DS*', r'^DS'),
 ('music D_*', r'^D_'),
 ('PLAYPAL/COLORMAP/ENDOOM/DEMO/GENMIDI/DMXGUS/TEXTURE/PNAMES', r'^(PLAYPAL|COLORMAP|ENDOOM|DEMO\d|GENMIDI|DMXGUS|TEXTURE\d|PNAMES)$'),
])
used=set()
for name,rx in cats.items():
    sel=[l for l in lumps if re.match(rx,l[0]) and l[0] not in used]
    for l in sel: used.add(l[0])
    tot=sum(l[2] for l in sel)
    print(f"{name}: count={len(sel)} bytes={tot}")
    if len(sel)<=40: print("   ", ' '.join(f"{l[0]}:{l[2]}" for l in sel))
# sounds detail: DMX header
print("--- DS* detail (fmt, rate, samples)")
rates=collections.Counter(); tot_samples=0
for nm,fp,sz in lumps:
    if nm.startswith('DS') and sz>=8:
        fmt, rate, ns = struct.unpack('<HHI', d[fp:fp+8])
        rates[(fmt,rate)]+=1; tot_samples+=ns
        print(f"  {nm} sz={sz} fmt={fmt} rate={rate} samples={ns}")
print("rates:",dict(rates),"total samples",tot_samples)
# map lumps
mapl=[l for l in lumps if re.match(r'^(THINGS|LINEDEFS|SIDEDEFS|VERTEXES|SEGS|SSECTORS|NODES|SECTORS|REJECT|BLOCKMAP)$',l[0])]
print("map lumps bytes", sum(l[2] for l in mapl), "count", len(mapl))
# markers
for m in ['S_START','S_END','SS_START','SS_END','F_START','F_END','F1_START','F1_END','P_START','P_END','P1_START','P1_END','E1M1','E1M9']:
    idx=[i for i,l in enumerate(lumps) if l[0]==m]
    print(m, idx)
def between(a,b):
    ia=[i for i,l in enumerate(lumps) if l[0]==a]; ib=[i for i,l in enumerate(lumps) if l[0]==b]
    if ia and ib:
        s=lumps[ia[0]+1:ib[0]]; return len(s), sum(l[2] for l in s)
    return None
print("sprites S_START..S_END", between('S_START','S_END'))
print("flats F_START..F_END", between('F_START','F_END'))
print("patches P_START..P_END", between('P_START','P_END'))
# everything else
rest=[l for l in lumps if l[0] not in used and not re.match(r'^(THINGS|LINEDEFS|SIDEDEFS|VERTEXES|SEGS|SSECTORS|NODES|SECTORS|REJECT|BLOCKMAP|E\dM\d)$',l[0])]
print("--- first 60 unclassified names:", ' '.join(l[0] for l in rest[:60]))

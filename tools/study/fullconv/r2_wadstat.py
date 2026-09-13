import struct,sys,collections
p=sys.argv[1]
d=open(p,'rb').read()
ident,n,off=struct.unpack('<4sII',d[:12])
lumps=[]
for i in range(n):
    fp,sz,name=struct.unpack('<II8s',d[off+16*i:off+16*i+16])
    lumps.append((name.rstrip(b'\0').decode('ascii','replace'),fp,sz))
names=[l[0] for l in lumps]
# sounds
ds=[l for l in lumps if l[0].startswith('DS')]
print('DS lumps:',len(ds),'total bytes',sum(l[2] for l in ds),'max',max(l[2] for l in ds) if ds else 0)
rates=collections.Counter()
for nm,fp,sz in ds:
    if sz>=8:
        fmt,rate,ns=struct.unpack('<HHI',d[fp:fp+8]); rates[rate]+=1
print('rates',rates)
# sprites between S_START/S_END
def patchdims(fp):
    w,h,lo,to=struct.unpack('<HHhh',d[fp:fp+8]); return w,h,lo,to
si=names.index('S_START'); se=names.index('S_END')
spr=lumps[si+1:se]
print('sprite lumps',len(spr))
tot=0; big=0; chunks64=0; chunks32=0; maxw=maxh=0
psnames=('PUN','PIS','SHT','CHG','MIS','PLS','BFG','SAW')
ps=[]; th=[]
for nm,fp,sz in spr:
    w,h,lo,to=patchdims(fp); tot+=w*h; maxw=max(maxw,w); maxh=max(maxh,h)
    c=((w+63)//64)*((h+63)//64); chunks64+=c
    (ps if nm[:3] in psnames else th).append((nm,w,h,c))
print('sum w*h all sprites',tot,'max w',maxw,'max h',maxh,'64x64 chunks all',chunks64)
print('psprite frames',len(ps),'sum w*h',sum(w*h for _,w,h,_ in ps),'chunks64',sum(c for *_,c in ps))
print('thing frames',len(th),'sum w*h',sum(w*h for _,w,h,_ in th),'chunks64',sum(c for *_,c in th))
# rotations: frames with rotation digit != 0
rot=collections.Counter()
for nm,w,h,c in th:
    rot[nm[5]]+=1
    if len(nm)>=8: rot[nm[7]]+=1
print('rotation digits',dict(rot))
# menus/hud/intermission patches: graphics between ? use names
ui=[l for l in lumps if l[0][:2] in ('M_','ST','WI','TI','CR','HE','IN','VI','CW','BR','AM') and not l[0].startswith('STEP') and l[2]>8]
print('UI-ish patches',len(ui),'bytes',sum(l[2] for l in ui))
for nm in ('TITLEPIC','HELP1','HELP2','CREDIT','VICTORY2','PFUB1','PFUB2','WIMAP0','STBAR','M_DOOM','INTERPIC','ENDPIC','SKY1','PLAYPAL','COLORMAP','DMXGUS','GENMIDI'):
    if nm in names:
        l=lumps[names.index(nm)]; print(nm,l[2],'bytes', patchdims(l[1]) if l[2]>8 and nm not in('PLAYPAL','COLORMAP','DMXGUS','GENMIDI') else '')
    else: print(nm,'ABSENT')
# flats & textures count
print('lump count',n,'wad size',len(d))

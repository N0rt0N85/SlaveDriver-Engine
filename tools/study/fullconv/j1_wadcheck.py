import struct, sys
p = r"C:/Users/pcico/Projects/Mimas/cd/data/DOOM1.WAD"
d = open(p,'rb').read()
n, ofs = struct.unpack_from('<ii', d, 4)
dirs = []
for i in range(n):
    fo, sz, nm = struct.unpack_from('<ii8s', d, ofs+16*i)
    dirs.append((nm.rstrip(b'\0').decode('latin1'), fo, sz))
idx = {}
for i,(nm,fo,sz) in enumerate(dirs): idx.setdefault(nm,i)
def lump(nm):
    _,fo,sz = dirs[idx[nm]]; return d[fo:fo+sz]
print("lumps", n, "size", len(d))
ds = [(nm,sz) for nm,fo,sz in dirs if nm.startswith('DS')]
print("DS* count", len(ds), "bytes", sum(s for _,s in ds), "max", max(ds,key=lambda t:t[1]))
# PLAYPAL blacks
pal = lump('PLAYPAL')[:768]
blacks = [i for i in range(256) if pal[3*i:3*i+3]==b'\0\0\0']
print("PLAYPAL0 black indices", blacks)
# STBAR index-0 pixels
def patch_pixels(b):
    w,h,lo,to = struct.unpack_from('<hhhh', b, 0)
    cols = struct.unpack_from('<%di'%w, b, 8)
    cnt0=0; tot=0
    for c in cols:
        pos=c
        while b[pos]!=255:
            top=b[pos]; ln=b[pos+1]; pos+=3
            for k in range(ln):
                if b[pos+k]==0: cnt0+=1
                tot+=1
            pos+=ln+1
    return w,h,cnt0,tot
print("STBAR w,h,idx0,total", patch_pixels(lump('STBAR')))
# things E1M1 / E1M6
def things(mapname):
    i = idx[mapname]
    for j in range(i+1, i+11):
        if dirs[j][0]=='THINGS':
            _,fo,sz=dirs[j]; b=d[fo:fo+sz]
            th=[struct.unpack_from('<hhHHH', b, k*10) for k in range(sz//10)]
            return th
    return []
for m in ('E1M1','E1M6'):
    th=things(m)
    allc=len(th)
    sp=[t for t in th if not (t[4]&16)]  # not multiplayer-only
    uv=[t for t in sp if t[4]&4]           # skill 4/5 bit
    # exclude player starts / deathmatch starts (types 1-4, 11) from mobj count
    starts={1,2,3,4,11}
    mob=[t for t in uv if t[3] not in starts]
    print(m, "THINGS all", allc, "no-MP", len(sp), "UV no-MP", len(uv), "UV no-MP no-starts", len(mob))
    # ssectors count
    i=idx[m]
    for j in range(i+1,i+11):
        if dirs[j][0]=='SSECTORS': print(m,"SSECTORS", dirs[j][2]//4)
        if dirs[j][0]=='SECTORS': print(m,"SECTORS", dirs[j][2]//26)
        if dirs[j][0]=='SIDEDEFS': print(m,"SIDEDEFS", dirs[j][2]//30)
# POSS family chunks
import math
def chunks(nm):
    b=lump(nm); w,h=struct.unpack_from('<hh',b,0); return math.ceil(w/64)*math.ceil(h/64), w, h
poss=[nm for nm,_,_ in dirs if nm.startswith('POSS')]
print("POSS lumps", len(poss), "chunks", sum(chunks(x)[0] for x in poss))
allspr=[]
inS=False
for nm,fo,sz in dirs:
    if nm=='S_START': inS=True; continue
    if nm=='S_END': inS=False
    if inS and sz>8: allspr.append(nm)
print("sprites", len(allspr), "chunks", sum(chunks(x)[0] for x in allspr), "max", max((chunks(x)[1:] for x in allspr)))
# weapon psprites
wp=[x for x in allspr if x[:4] in ('PUNG','PISG','PISF','SHTG','SHTF','CHGG','CHGF','MISG','MISF','SAWG','PLSG','PLSF','BFGG','BFGF')]
print("psprites", len(wp), "chunks", sum(chunks(x)[0] for x in wp), "bytes", sum(dirs[idx[x]][2] for x in wp))

import struct,sys
p=r"C:/Users/pcico/Projects/Mimas/cd/data/DOOM1.WAD"
d=open(p,'rb').read()
ident,n,off=struct.unpack('<4sII',d[:12])
lumps={}
order=[]
for i in range(n):
    fp,sz,nm=struct.unpack('<II8s',d[off+16*i:off+16*i+16])
    nm=nm.rstrip(b'\0').decode('latin1')
    lumps.setdefault(nm,(fp,sz)); order.append(nm)
pal=d[lumps['PLAYPAL'][0]:lumps['PLAYPAL'][0]+768*14]
p0=pal[:768]
blacks=[i for i in range(256) if p0[3*i:3*i+3]==b'\0\0\0']
print("PLAYPAL0 blacks:",blacks)
# tint means
for k in (1,8,9,12,13):
    pk=pal[768*k:768*k+768]
    dr=sum(pk[3*i]-p0[3*i] for i in range(256))/256
    dg=sum(pk[3*i+1]-p0[3*i+1] for i in range(256))/256
    db=sum(pk[3*i+2]-p0[3*i+2] for i in range(256))/256
    print("pal %d mean delta r%+.0f g%+.0f b%+.0f"%(k,dr,dg,db))
# STBAR index 0 pixels
def patch_px(nm):
    fp,sz=lumps[nm]; b=d[fp:fp+sz]
    w,h,lo,to=struct.unpack('<hhhh',b[:8])
    cols=struct.unpack('<%dI'%w,b[8:8+4*w])
    idx0=0; tot=0
    for c in cols:
        o=c
        while b[o]!=0xff:
            top=b[o]; ln=b[o+1]; o+=3
            px=b[o:o+ln]; o+=ln+1
            idx0+=px.count(0); tot+=ln
    return w,h,tot,idx0
print("STBAR",patch_px('STBAR'))
print("TITLEPIC",patch_px('TITLEPIC'))
print("M_DOOM",patch_px('M_DOOM'))
print("demos:",[ (x,lumps[x][1]) for x in order if x.startswith('DEMO')])
print("music:",[x for x in order if x.startswith('D_')])
print("sky:",[x for x in order if x.startswith('SKY')])
print("has HELP1/HELP2/CREDIT/VICTORY2/PFUB1:",[x in lumps for x in ('HELP1','HELP2','CREDIT','VICTORY2','PFUB1','ENDPIC')])

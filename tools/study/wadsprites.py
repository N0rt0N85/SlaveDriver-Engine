import struct
from collections import Counter
p=r'C:\Users\pcico\Projects\Mimas\cd\data\DOOM1.WAD'
b=open(p,'rb').read()
magic,n,off=struct.unpack('<4sii',b[:12])
d=[]
for i in range(n):
    fo,sz,nm=struct.unpack('<ii8s',b[off+16*i:off+16*i+16])
    d.append((nm.rstrip(b'\0').decode('latin1'),fo,sz))
try:
    s=[i for i,(nm,_,_) in enumerate(d) if nm=='S_START'][0]
    e=[i for i,(nm,_,_) in enumerate(d) if nm=='S_END'][0]
except IndexError:
    s,e=0,0
c=Counter(); big=0; tot=0; bytes_=0
w_h=[]
for i in range(s+1,e):
    nm,fo,sz=d[i]
    if sz<8: continue
    w,h=struct.unpack('<HH',b[fo:fo+4])
    tot+=1; bytes_+=sz
    cx=(w+63)//64; cy=(h+63)//64
    c[cx*cy]+=1
    w_h.append((w,h))
print("sprite lumps:",tot,"raw bytes:",bytes_)
print("chunks 64x64 needed per frame:",dict(sorted(c.items())))
print("max w,h:",max(x[0] for x in w_h),max(x[1] for x in w_h))
print("mean chunks per frame: %.2f"%(sum(k*v for k,v in c.items())/tot))

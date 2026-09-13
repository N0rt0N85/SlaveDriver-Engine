import struct, collections
W=r'C:\Users\pcico\Projects\Mimas\cd\data\DOOM1.WAD'
b=open(W,'rb').read()
_,n,off=struct.unpack('<4sii',b[:12])
d=[]
for i in range(n):
    fo,sz,nm=struct.unpack('<ii8s',b[off+16*i:off+16*i+16]); d.append((nm.rstrip(b'\0').decode('latin1'),fo,sz))
L={nm:(fo,sz) for nm,fo,sz in d}
fo,sz=L['PLAYPAL']; pal=b[fo:fo+768]
blacks=[i for i in range(256) if pal[3*i:3*i+3]==b'\0\0\0']
print('PLAYPAL0 noirs:',blacks)
# STBAR posts: count index 0 pixels
fo,sz=L['STBAR']; p=b[fo:fo+sz]
w,h,lo,to=struct.unpack('<hhhh',p[:8]); cols=struct.unpack('<%di'%w,p[8:8+4*w])
z=0;tot=0
for c in cols:
    q=c
    while p[q]!=255:
        top=p[q]; ln=p[q+1]; px=p[q+3:q+3+ln]; tot+=ln; z+=px.count(0); q+=ln+4
print('STBAR %dx%d pixels %d index0 %d'%(w,h,tot,z))
ds=[(nm,sz) for nm,fo,sz in d if nm.startswith('DS')]
print('DS* lumps',len(ds),'octets',sum(s for _,s in ds),'max',max(ds,key=lambda x:x[1]))
# LEV header
LEV=r'C:\Users\pcico\Projects\SlaveDriver-Engine\build\doom2ps\TOMB_e1m1.LEV'
lv=open(LEV,'rb').read()
o=512+4+4+512*256+320*4
size=struct.unpack('>i',lv[o:o+4])[0]; print('LEV total',len(lv),'sky bloc',o,'geom size',size)
hdr=lv[o+4:o+4+64]
print('header ints', struct.unpack('>16i',hdr[:64])[:16])

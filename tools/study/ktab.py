import sys, os, struct
sys.path.insert(0, os.path.join(os.getcwd(),'tools'))
import lev
r=lev.Reader('refs/extract/PS/TOMB.LEV'); s=lev.parse_sky(r)
b=r.b
tab=struct.unpack('>320i', b[s['table_off']:s['table_off']+1280])
print("first 12:", [hex(x&0xffffffff) for x in tab[:12]])
print("mid(150-170):", [hex(x&0xffffffff) for x in tab[150:170]])
print("last 8:", [hex(x&0xffffffff) for x in tab[-8:]])
import math
# compare against commented-out formula f=atan((x-160)/160)*128/0.785398 ; if |x-160|>1: d=(f/(x-160))*65536 & 0x007fffff
ok=0
for x in range(320):
    f=math.atan((x-160.0)/160.0)*128.0/0.785398
    if abs(x-160)>1:
        d=int((f/(x-160))*65536.0)&0x007fffff
    else:
        d=66754
    if d==(tab[x]&0xffffffff): ok+=1
print("matches commented formula:", ok, "/320")

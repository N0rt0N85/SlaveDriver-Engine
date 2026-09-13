# Decodes the STATUSTEXT overlay captures of 2026-09-12 (converted Duke E1L1, assert build).
# Overlay source of truth: SRUINS.C:2199-2227,2253
#   fps:%d %d          -> 60/framesElapsed , 60/(smoothVTime+1)
#   polys:%d           -> nmPolys+nmSlavePolys   (WALL/FLOOR/CEILING CELLS ONLY -- sprites not counted)
#   time:%d %d:%d      -> (lastCalc+lastLastCalc)>>1 , lastDraw , lastCalc+lastDraw
#   lastCalc = htimer at end of CPU work ; lastDraw = htimer-lastCalc after SPR_WaitDrawEnd
#   htimer = HBlank-IN count  => ONE UNIT = ONE SCANLINE
LINE_US = 63.556          # NTSC 320x240 non-interlace: 262.5 lines / 16.6833 ms
FIELD_MS = 16.6833
caps = [  # sector, polys, fps_inst, fps_gov, calc_avg, draw, total, used0, vswaps0
 (319, 190, 30, 30, 365, 136,  490, 14, 0),
 (317, 824, 12, 20, 751, 312, 1054, 16, 0),
 (394, 697, 20, 20, 699,  14,  712, 17, 0),
 ( 13, 662, 20, 20, 607,  13,  622, 14, 0),
 (100, 498, 15, 20, 685,  15,  931, 18, 1),
]
print("%-4s %6s %5s %8s %8s %8s %9s %7s"%("sect","polys","fps","calc ms","draw ms","work ms","fields ms","idle ms"))
pts=[]
for sect,polys,fi,fg,cavg,draw,tot,used,sw in caps:
    calc = tot - draw                      # CURRENT frame's lastCalc (cavg is a 2-frame average)
    cms, dms, tms = calc*LINE_US/1000, draw*LINE_US/1000, tot*LINE_US/1000
    fields = 60/fi
    fms = fields*FIELD_MS
    print("%-4d %6d %5d %8.1f %8.1f %8.1f %9.1f %7.1f"%(sect,polys,fi,cms,dms,tms,fms,fms-tms))
    pts.append((polys,cms,sw))
fit=[(p,c) for p,c,sw in pts if sw==0]
n=len(fit); mx=sum(p for p,_ in fit)/n; my=sum(c for _,c in fit)/n
sxy=sum((p-mx)*(c-my) for p,c in fit); sxx=sum((p-mx)**2 for p,_ in fit)
b=sxy/sxx; a=my-b*mx
print("\nfit on the %d swap-free captures:  calc(ms) = %.2f + %.4f * polys"%(n,a,b))
print("  => %.1f us per emitted cell, %.1f ms fixed"%(b*1000,a))
for p,c in fit: print("     polys %4d  calc %5.1f  model %5.1f  resid %+5.1f"%(p,c,a+b*p,c-(a+b*p)))
p5=[x for x in pts if x[2]==1][0]
print("     polys %4d  calc %5.1f  model %5.1f  resid %+5.1f   <- 1 vswap, NOT in the fit"%(p5[0],p5[1],a+b*p5[0],p5[1]-(a+b*p5[0])))
print("\nWhat one vblank step costs, at this model:")
for tgt,f in ((2,"30 fps"),(3,"20 fps"),(4,"15 fps")):
    print("  %s (%.1f ms) -> budget %4.0f cells"%(f,tgt*FIELD_MS,(tgt*FIELD_MS-a)/b))

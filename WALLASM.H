void rectTransform(Fixed32 wx,Fixed32 wy,Fixed32 wz,
		   int light,int h,int w,
		   Fixed32 px,Fixed32 py,Fixed32 pz,
		   Fixed32 hx,Fixed32 hy,Fixed32 hz,
		   void *output,
		   unsigned short (*lightFunc)(char vLightIndex,
					       MthXyz *pos),
		   int lightStepExtra,   /* GCC14: extra bytes after each light byte */
		   int lightRowExtra,    /* GCC14: extra bytes after each vertex row */
		   Fixed32 nearFloor);   /* GCC14: z floor of the projection, per wall */

void normTransform(sVertexType *firstVertex,MthMatrix *viewMatrix,
		   int nmVert,void *output,
		   unsigned short (*lightFunc)(char vLightIndex,
					       MthXyz *pos),
		   Fixed32 nearFloor);   /* GCC14: z floor of the projection, per wall */

/* GCC14: PROBE -- changes nothing.  VDP1 command vertices hold 11 signed bits, -1024..1023;
   beyond, "operation cannot be guaranteed" (HW_VDP1.md:184).  It runs on EVERY cell and nobody
   reads it (vdp1TakeMaxX/Y, WALLS.H, have no caller): at 0 it vanishes; set it to 1 only
   together with the overlay line that shows it. */
#define VDP1PROBE 0

static __inline__ XyInt *probeVDP1(XyInt *poly)
{
#if VDP1PROBE
 int i,a;
 for (i=0;i<4;i++)
    {a=poly[i].x; if (a<0) a=-a;
     if (a>vdp1MaxX) vdp1MaxX=a; if (a>vdp1MaxCoord) vdp1MaxCoord=a;
     a=poly[i].y; if (a<0) a=-a;
     if (a>vdp1MaxY) vdp1MaxY=a; if (a>vdp1MaxCoord) vdp1MaxCoord=a;
    }
#endif
 return poly;
}

#define VDP1DIAG 0      /* 1 = diagnostic disc: paints out-of-range cells */
#define VDP1LIM  1023
#define VCLIPONE 1024   /* cut fractions in 1/1024: (30000<<10) fits in an int */

/* GCC14: emit a wall cell with V-WINDOWING of the pattern when the quad leaves the VDP1's
   vertical range.  The pattern's V axis runs from edge (poly[0],poly[1]) to edge
   (poly[3],poly[2]), whatever pattern[]'s orientation.  Each vertical edge is cut where it enters
   the range and the INTERSECTION of the two intervals is kept: conservative, never out of range.
   Facing a wall the cut is EXACT (both edges at the same depth); at an angle it is approximate,
   losing a little texture on one edge.
   Known limits: (1) U cannot be windowed -- no stride register, a U sub-rectangle shears
   (HW_VDP1.md:160), so a horizontal overflow is not handled here; (2) the gouraud table is not
   re-interpolated on the cut quad. */
static __inline__ void EZ_distSprVClip(short charNm,XyInt *poly,
				       struct gourTable *g)
{int i,a,over=0,t0,t1;
 XyInt src[4];
 /* `over` looks at |y| ONLY: only V can be windowed (known limits above).  Do not add x --
    it would send x-overflowing quads to a vertical window that cannot fix them. */
 for (i=0;i<4;i++)
    {
#if VDP1PROBE
     a=poly[i].x; if (a<0) a=-a;
     if (a>vdp1MaxX) vdp1MaxX=a; if (a>vdp1MaxCoord) vdp1MaxCoord=a;
#endif
     a=poly[i].y; if (a<0) a=-a;
#if VDP1PROBE
     if (a>vdp1MaxY) vdp1MaxY=a; if (a>vdp1MaxCoord) vdp1MaxCoord=a;
#endif
     if (a>VDP1LIM) over=1;
    }
 if (!over)
    {EZ_specialDistSpr2(charNm,poly,g);
     return;
    }
 /* Replaces the old cx/cy maxima: counting the cells actually windowed says the same (do we
    leave the range, how often) for one increment on the RARE path, instead of six compares
    on EVERY cell. */
 vdp1NmClipped++;
#if VDP1DIAG
 /* DIAGNOSTIC DISC -- fixes nothing, SHOWS.  A cell with a vertex outside the VDP1 range
    is painted flat, clamped to the range, instead of textured:
       RED     X overflow only
       BLUE    Y overflow only
       MAGENTA both
    Axis, place and extent at a glance -- what no counter gives. */
 {int j,cx=0,cy=0;
  for (j=0;j<4;j++)
     {int a;
      a=poly[j].x; if (a<0) a=-a; if (a>VDP1LIM) cx=1;
      a=poly[j].y; if (a<0) a=-a; if (a>VDP1LIM) cy=1;
      if (poly[j].x> VDP1LIM) poly[j].x= VDP1LIM;
      if (poly[j].x<-VDP1LIM) poly[j].x=-VDP1LIM;
      if (poly[j].y> VDP1LIM) poly[j].y= VDP1LIM;
      if (poly[j].y<-VDP1LIM) poly[j].y=-VDP1LIM;
     }
  EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,
	     (cx&&cy)? RGB(31,0,31): (cx? RGB(31,0,0): RGB(0,0,31)),
	     poly,NULL);
  return;
 }
#endif
 t0=0; t1=VCLIPONE;
 for (i=0;i<2;i++)
    {int ya=poly[i].y,yb=poly[3-i].y,d=yb-ya,lo,hi,sp,sm;
     if (d==0)
	{if (ya>VDP1LIM || ya<-VDP1LIM)
	    {/* edge wholly out of range at constant height: nothing to window in V,
		it is a horizontal overflow.  Emit as is, as before. */
	     EZ_specialDistSpr2(charNm,poly,g);
	     return;
	    }
	 continue;
	}
     sp=(( VDP1LIM-ya)<<10)/d;
     sm=((-VDP1LIM-ya)<<10)/d;
     lo=sp<sm? sp: sm;
     hi=sp<sm? sm: sp;
     if (lo>t0) t0=lo;
     if (hi<t1) t1=hi;
    }
 if (t0<0) t0=0;
 if (t1>VCLIPONE) t1=VCLIPONE;
 if (t1-t0<=0)
    return;   /* nothing representable: the cell is over 3 screen heights away, off view */
 for (i=0;i<4;i++)
    src[i]=poly[i];
 for (i=0;i<2;i++)
    {int j=3-i;
     poly[i].x=src[i].x+(((src[j].x-src[i].x)*t0)>>10);
     poly[i].y=src[i].y+(((src[j].y-src[i].y)*t0)>>10);
     poly[j].x=src[i].x+(((src[j].x-src[i].x)*t1)>>10);
     poly[j].y=src[i].y+(((src[j].y-src[i].y)*t1)>>10);
    }
 EZ_specialDistSpr2V(charNm,t0,t1,poly,g);
}

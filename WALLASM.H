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

/* GCC14: PROBE (STATUSTEXT) -- what the VDP1 steps through for a cell, changes nothing.  A
   distorted sprite, like a polygon, is drawn as lines from edge A-D to edge B-C: as many lines
   as the longer of those two edges has pixels, each as long as the quad is wide there,
   anti-aliased (|dx|+|dy| pixels).  The clipping drops the WRITE of a pixel outside the window,
   not its step (HW_VDP1.md section 7).  Pre-clipping (Pclp = 0 on every cell) skips a line whose
   two ends lie beyond the same side of the window.
     vdp1Walk     every cell: lines x the wider of A-B and D-C
     vdp1Big      the cells whose count passes VDP1BIGWALK, a quarter of the 3D window ...
     vdp1BigWalk  ... re-counted on 16 of their lines with the pre-clip applied
     vdp1MaxX/Y   the largest |x| and |y| sent (vdp1Range holds them to VDP1LIM)
     vdp1RotWalk  the same walk if every wall were drawn from a pattern turned a quarter:
                  lines from edge A-B to edge D-C, vertical on a wall (a wall = its V edges
                  A-D and B-C vertical on screen), everything else counted as drawn
   Units of 64 pixels.  The window is the view's, not the sector box of RECTCLIP: a line only
   that box would pre-clip still counts. */
#ifdef STATUSTEXT
#define VDP1BIGWALK 16384
static __inline__ int vdp1Abs(int a)
{return a<0? -a: a;
}

/* the walk of the lines drawn from edge p0-p1 to edge q0-q1, p0-q0 the first line: as many lines
   as the longer edge has pixels, each |dx|+|dy| long; *big set when it passes VDP1BIGWALK and is
   re-counted on 16 of its lines with the pre-clip applied */
static int vdp1WalkEdges(XyInt *p0,XyInt *p1,XyInt *q0,XyInt *q1,int *big)
{int e,f,j,lines,len,walk;
 e=vdp1Abs(p1->x-p0->x); j=vdp1Abs(p1->y-p0->y); if (j>e) e=j;
 f=vdp1Abs(q1->x-q0->x); j=vdp1Abs(q1->y-q0->y); if (j>f) f=j;
 lines=(e>f? e: f)+1;
 e=vdp1Abs(q0->x-p0->x)+vdp1Abs(q0->y-p0->y);
 f=vdp1Abs(q1->x-p1->x)+vdp1Abs(q1->y-p1->y);
 len=(e>f? e: f)+1;
 if (lines>8192) lines=8192;          /* keeps lines x len under 2^27 */
 if (len>16384) len=16384;
 walk=lines*len;
 *big=walk>VDP1BIGWALK;
 if (*big)
    {int in=0;
     for (j=1;j<32;j+=2)
	{int px=p0->x+(((p1->x-p0->x)*j)>>5),py=p0->y+(((p1->y-p0->y)*j)>>5);
	 int qx=q0->x+(((q1->x-q0->x)*j)>>5),qy=q0->y+(((q1->y-q0->y)*j)>>5);
	 if ((px<viewXmin && qx<viewXmin) || (px>viewXmax && qx>viewXmax) ||
	     (py<viewYmin && qy<viewYmin) || (py>viewYmax && qy>viewYmax))
	    continue;                   /* pre-clipped */
	 in+=vdp1Abs(qx-px)+vdp1Abs(qy-py)+1;
	}
     if (in>16*16384) in=16*16384;
     walk=(in>>4)*lines;
    }
 return walk;
}

static void vdp1WalkProbe(XyInt *q)
{int j,big,walk;
 for (j=0;j<4;j++)
    {int a=vdp1Abs(q[j].x);
     if (a>vdp1MaxX) vdp1MaxX=a;
     a=vdp1Abs(q[j].y);
     if (a>vdp1MaxY) vdp1MaxY=a;
    }
 walk=vdp1WalkEdges(q,q+3,q+1,q+2,&big);        /* as drawn: from A-D to B-C */
 if (big)
    {vdp1Big++;
     vdp1BigWalk+=walk>>6;
    }
 vdp1Walk+=walk>>6;
 if (q[0].x==q[3].x && q[1].x==q[2].x)
    walk=vdp1WalkEdges(q,q+1,q+3,q+2,&big);     /* a wall, turned: from A-B to D-C */
 vdp1RotWalk+=walk>>6;
}
#define VDP1WALK(q) vdp1WalkProbe(q)
#else
#define VDP1WALK(q)
#endif

#define VDP1DIAG 0      /* 1 = diagnostic disc: paints out-of-range cells */
/* GCC14: the drawing plane is -1024..1023 AFTER the local origin is added (HW_VDP1.md:184-188),
   and that origin reaches (240,168) in split screen (SRUINS.C mpSetViewport): 1023 - 240, with a
   margin, keeps every view's command inside the plane.  The freeze of 2026-09-18 (vdp1Range) sat
   on a cell windowed to y 1022 -- 1134 on the plane in solo -- as well as at x 3017. */
#define VDP1LIM  767
#define VCLIPONE 1024   /* cut fractions in 1/1024: (30000<<10) fits in an int */

/* GCC14: past -1024..1023 the manual only says "operation cannot be guaranteed" (HW_VDP1.md:184).
   On console it HANGS: 2026-09-18, a wall cell with two corners at x 3017 held COPR on itself
   for over 4 s, the master waiting in SPR_WaitDrawEnd for ever (the freeze report, CRASH.C).
   Nothing past the range may leave for the VDP1.
   vdp1Slide moves corner p, past the range, along its edge towards q until it reaches the limit:
   the edge keeps its line, so what shows of it on screen stays where it was.
   vdp1Range slides along the U edges (A-B, D-C).  That keeps a wall exact -- its V edges are
   vertical on screen, so both ends of a V edge slide to the same x -- and squeezes the pattern
   in U, which cannot be windowed (no stride).  What is still past the range (an edge wholly past
   one side) is clamped where it lies, off screen.  Returns 0 when the quad cannot show: all four
   corners past one side of the range, or at or past one edge of the view (at most the view's
   last row or column, which the neighbouring cell draws too). */
static void __attribute__((noinline)) vdp1Slide(XyInt *p,const XyInt *q)
{int k,pc,qc,lim,t;
 for (k=0;k<2;k++)
    {pc=k? p->y: p->x;
     qc=k? q->y: q->x;
     if (pc>VDP1LIM)
	lim=VDP1LIM;
     else if (pc<-VDP1LIM)
	lim=-VDP1LIM;
     else
	continue;
     if (lim>0? qc>=lim: qc<=lim)
	continue;                       /* the whole edge is past this side */
     t=((lim-qc)<<12)/(pc-qc);          /* 0..4096 from q to p */
     p->x=q->x+(((p->x-q->x)*t)>>12);
     p->y=q->y+(((p->y-q->y)*t)>>12);
     if (k)
	p->y=lim;
     else
	p->x=lim;
    }
}

static int vdp1Range(XyInt *q)
{int i,xp=0,xm=0,yp=0,ym=0;
 for (i=0;i<4;i++)
    {xp+=q[i].x>VDP1LIM; xm+=q[i].x<-VDP1LIM;
     yp+=q[i].y>VDP1LIM; ym+=q[i].y<-VDP1LIM;
    }
 if (!(xp|xm|yp|ym))
    return 1;
 if (xp==4 || xm==4 || yp==4 || ym==4)
    return 0;
 vdp1Slide(q+0,q+1); vdp1Slide(q+1,q+0);
 vdp1Slide(q+3,q+2); vdp1Slide(q+2,q+3);
 xp=xm=yp=ym=0;
 for (i=0;i<4;i++)
    {if (q[i].x> VDP1LIM) q[i].x= VDP1LIM;
     if (q[i].x<-VDP1LIM) q[i].x=-VDP1LIM;
     if (q[i].y> VDP1LIM) q[i].y= VDP1LIM;
     if (q[i].y<-VDP1LIM) q[i].y=-VDP1LIM;
     xp+=q[i].x>=viewXmax; xm+=q[i].x<=viewXmin;
     yp+=q[i].y>=viewYmax; ym+=q[i].y<=viewYmin;
    }
 return !(xp==4 || xm==4 || yp==4 || ym==4);
}

/* GCC14: emit a wall cell with V-WINDOWING of the pattern when the quad leaves the VDP1's
   vertical range.  The pattern's V axis runs from edge (poly[0],poly[1]) to edge
   (poly[3],poly[2]), whatever pattern[]'s orientation.  Each vertical edge is cut where it enters
   the range and the INTERSECTION of the two intervals is kept: conservative, never out of range.
   Facing a wall the cut is EXACT (both edges at the same depth); at an angle it is approximate,
   losing a little texture on one edge.
   Known limits: (1) U cannot be windowed -- no stride register, a U sub-rectangle shears
   (HW_VDP1.md:160): a horizontal overflow is slid along U by vdp1Range, which squeezes the
   pattern; (2) the gouraud table is not re-interpolated on the cut quad. */
static __inline__ void EZ_distSprVClip(short charNm,XyInt *poly,
				       struct gourTable *g)
{int i,a,over=0,wide=0,t0,t1;
 XyInt src[4];
 /* `over` looks at |y| only: only V can be windowed (known limits above), and a wall's V edges
    are vertical, so its x never enters the window.  `wide` (an |x| past the range) goes to
    vdp1Range, after the window if there is one. */
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
     a=poly[i].x; if (a<0) a=-a;
     if (a>VDP1LIM) wide=1;
    }
 if (!(over|wide))
    {VDP1WALK(poly);
     EZ_specialDistSpr2(charNm,poly,g);
     return;
    }
 /* Replaces the old cx/cy maxima: counting the cells brought into range says the same (do we
    leave the range, how often) for one increment on the RARE path. */
 vdp1NmClipped++;
 if (!over)
    {if (!vdp1Range(poly))
	return;
     VDP1WALK(poly);
     EZ_specialDistSpr2(charNm,poly,g);
     return;
    }
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
	    {/* edge wholly out of range at constant height: nothing to window in V, it is
		an overflow along U -- slid there */
	     if (!vdp1Range(poly))
		return;
	     VDP1WALK(poly);
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
 if (wide && !vdp1Range(poly))
    return;
 VDP1WALK(poly);
 EZ_specialDistSpr2V(charNm,t0,t1,poly,g);
}

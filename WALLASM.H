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

/* GCC14: PROBE (WALKPROBE) -- what the VDP1 steps through for a cell, changes nothing.  A
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
   that box would pre-clip still counts.  Not on the test disc: it runs inside every cell's
   command, and SLAVECMDS and Master Draw would be timed with it. */
#ifdef WALKPROBE
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
 vdp1ClsWalk[vdp1Cls&3]+=walk>>6;               /* GCC14: and which CLASS carries it */
 if (q[0].x==q[3].x && q[1].x==q[2].x)
    walk=vdp1WalkEdges(q,q+1,q+3,q+2,&big);     /* a wall, turned: from A-B to D-C */
 vdp1RotWalk+=walk>>6;
}
#define VDP1WALK(q) vdp1WalkProbe(q)
/* GCC14: the engine's normal points into the leaf, so +Y is a FLOOR and -Y a ceiling (the
   converter emits (0,65536,0) and (0,-65536,0) for the flats, doom2ps).  Zero is a wall. */
#define WALLCLS(w)  (vdp1Cls=(w)->normal[1]>0? 1: (w)->normal[1]<0? 2: 0)
#else
#define VDP1WALK(q)
#define WALLCLS(w)
#endif

#define VDP1DIAG 0      /* 1 = diagnostic disc: paints out-of-range cells */
/* GCC14: the drawing plane is -1024..1023 AFTER the local origin is added (HW_VDP1.md:184-188),
   and that origin reaches (240,168) in split screen (SRUINS.C mpSetViewport): 1023 - 240, with a
   margin, keeps every view's command inside the plane.  The freeze of 2026-09-18 (vdp1Range) sat
   on a cell windowed to y 1022 -- 1134 on the plane in solo -- as well as at x 3017. */
/* VDP1LIM and vdp1VCut now live in WALLS.H: SRUINS.C needs them for the L+R+C chord.
   GCC14: VDP1LIM is where the HARDWARE stops -- past the drawing plane the console hangs, so
   vdp1Range clamps to it and nothing may leave for the VDP1 beyond it.  It was also serving as
   the threshold at which vdp1Fit cuts a quad in V, and that was a category error: the screen only
   shows |y| <= 112 (solo) or 56 (3/4p), so everything between the window and 767 was being walked
   by the VDP1 and then thrown away by the clip.  Console 2026-09-25, WALK=1 at a slow spot of
   E1M3: 1972k units = 2.02 Mpx stepped to show a 61440 px window -- 33 windows, 91 % of it in 14
   cells each stepping 2.1 windows.  The V cut now has its own bound, and it is live (L+R+C) so it
   can be judged in place against the same binary.  rho (WALLS.C) follows it: rho is what keeps the
   cut OUTSIDE the window, and a cut bound without it opens a hole at the top of near cells. */
#define VCLIPONE 1024   /* cut fractions in 1/1024: (30000<<10) fits in an int */
/* GCC14: and one PATTERN ROW of a wall tile, in the same fractions.  Every cell that reaches
   here draws a 64 x 64 tile (PIC.C classType[TILE16BPP], and both cell paths assert it), and
   EZ_specialDistSpr2V can only start and end the pattern on a whole row -- CMDSRCA is an address
   and CMDSIZE a size, with no stride (SPR.C).  A cut that is not on a row boundary therefore
   moves the texture against the geometry by (the cell's full height)/64 pixels, and that offset
   CRAWLS as the camera drifts the cut across a row: 47 px on a 3000 px cell.  That is the swim
   the console showed on 2026-09-25.  The cut is quantised to this grid, outward. */
#define VCLIPSTEP 16

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
   pattern; (2) the gouraud table is not re-interpolated on the cut quad.
   charNm < 0: the tile cache refused the cell (PIC.H mapWallPic), a polygon in `colour` takes its
   place, same outline and gouraud. */
/* GCC14: limit (2) above, now that the cut is the normal case for near cells.  vdp1Fit moved the
   corners along the V edges -- poly[0]->poly[3] and poly[1]->poly[2] -- so the gouraud corners
   must move the same way, or the shading of a cut cell is the shading of a quad that no longer
   exists.  While the bound was 767 the cut corners stayed far off screen and the error never
   reached the window; at 256 it does.  RGB 5:5:5, one lerp a channel, on a copy: the caller's
   table is built per cell but nothing promises it is not reused. */
static __inline__ void gourCut(struct gourTable *d,const struct gourTable *s,int t0,int t1)
{int i;
 for (i=0;i<2;i++)
    {int j=3-i,c;
     unsigned a=s->entry[i],b=s->entry[j],r0=0,r1=0;
     for (c=0;c<15;c+=5)
	{int va=(a>>c)&31,vb=(b>>c)&31;
	 r0|=(unsigned)(va+(((vb-va)*t0)>>10))<<c;
	 r1|=(unsigned)(va+(((vb-va)*t1)>>10))<<c;
	}
     d->entry[i]=(Uint16)r0;
     d->entry[j]=(Uint16)r1;
    }
}

static __inline__ void cellOut(short charNm,short colour,int t0,int t1,XyInt *poly,
			       struct gourTable *g)
{struct gourTable cut;
 if (g && (t0>0 || t1<VCLIPONE))
    {gourCut(&cut,g,t0,t1);
     g=&cut;
    }
 if (charNm<0)
    EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5|DRAW_GOURAU,colour,poly,g);
 else if (t0>0 || t1<VCLIPONE)
    EZ_specialDistSpr2V(charNm,t0,t1,poly,g);
 else
    EZ_specialDistSpr2(charNm,poly,g);
}

/* GCC14: the run of V where BOTH V edges lie past the bound c -- above it when `above`, below it
   otherwise -- kept only where it touches an end of [0,VCLIPONE).
   The VDP1 draws the cell as lines from edge poly[0]-poly[3] to edge poly[1]-poly[2]
   (vdp1WalkProbe above), and y is affine along a line, so a line whose TWO ends lie past one
   side of the view cannot show a pixel: that run is free.  A run in the MIDDLE is not taken --
   dropping it would split the cell in two, which one command cannot express -- so only the
   prefix and the suffix move the caller's [t0,t1).
   This is exact, and that is the whole point: what it removes is provably invisible, so no hole
   can open.  The old rule kept the INTERSECTION of the two edges' in-bound runs, which on a cell
   whose two edges sit at very different depths removes lines that DO show -- a band of ceiling
   or floor that simply is not drawn, and at worst an empty intersection and no cell at all.
   That was the holes of 2026-09-25. */
static void vdp1Hidden(const XyInt *poly,int above,int c,int *t0p,int *t1p)
{int i,lo=0,hi=VCLIPONE;
 for (i=0;i<2;i++)
    {int ya=poly[i].y,yb=poly[3-i].y;
     int pa=above? ya<c: ya>c,pb=above? yb<c: yb>c,r;
     if (pa==pb)
	{if (pa)
	    continue;                   /* the edge is past everywhere: it constrains nothing */
	 lo=hi;                         /* ... or nowhere: this side hides nothing */
	 break;
	}
     r=((c-ya)<<10)/(yb-ya);            /* where the edge crosses c; pa!=pb, so it does */
     if (pa)
	{if (r<hi) hi=r;}               /* past c BEFORE r */
     else
	{if (r>lo) lo=r;}               /* past c AFTER r */
    }
 if (lo<0) lo=0;
 if (hi>VCLIPONE) hi=VCLIPONE;
 if (lo>=hi)
    return;
 if (lo<=0 && hi>=VCLIPONE)
    {*t0p=VCLIPONE; *t1p=0; return;}    /* the whole cell is past this side */
 if (lo<=0)
    {if (hi>*t0p) *t0p=hi;}
 else if (hi>=VCLIPONE)
    {if (lo<*t1p) *t1p=lo;}
}

/* GCC14: a wall cell fitted to the VDP1's range, for the master's cells (EZ_distSprVClip) and the
   slave's (WALLS.C slaveCmdCell) alike.  Returns 0 when nothing of it can be drawn; else poly is
   in range and [*t0p,*t1p) is the V window of its pattern, in 1/1024 (0, VCLIPONE: all of it).
   `over` looks at |y| only: only V can be windowed (known limits above), and a wall's V edges
   are vertical, so its x never enters the window.  `wide` (an |x| past the range) goes to
   vdp1Range, after the window if there is one.  One copy for both: not inlined.
   vdp1VCut 0 is the exact rule (vdp1Hidden); a positive value is the old fixed bound, kept live
   on the L+R+C chord so the two can be judged in place on one binary. */
static int __attribute__((noinline)) vdp1Fit(XyInt *poly,int *t0p,int *t1p)
{int i,a,over=0,wide=0,tall=0,t0,t1;
 XyInt src[4];
#ifdef WALKPROBE
 vdp1FitN++;
 vdp1FitSaw=vdp1VCut;
#endif
 for (i=0;i<4;i++)
    {
#if VDP1PROBE
     a=poly[i].x; if (a<0) a=-a;
     if (a>vdp1MaxX) vdp1MaxX=a; if (a>vdp1MaxCoord) vdp1MaxCoord=a;
     a=poly[i].y; if (a<0) a=-a;
     if (a>vdp1MaxY) vdp1MaxY=a; if (a>vdp1MaxCoord) vdp1MaxCoord=a;
#endif
     if (vdp1VCut)
	{a=poly[i].y; if (a<0) a=-a;
	 if (a>vdp1VCut) over|=1;
	}
     else
	{if (poly[i].y<viewYmin) over|=1;   /* which SIDE overhangs: a side with no corner past */
	 if (poly[i].y>viewYmax) over|=2;   /* it hides nothing, and its two divides are waste */
	}
     a=poly[i].x; if (a<0) a=-a;
     if (a>VDP1LIM) wide=1;
    }
 *t0p=0;
 *t1p=VCLIPONE;
 if (!(over|wide))
    return 1;
 if (!over)
    return vdp1Range(poly);
#ifdef WALKPROBE
 vdp1FitOver++;
#endif
 t0=0; t1=VCLIPONE;
 if (!vdp1VCut)
    {if (over&1) vdp1Hidden(poly,1,viewYmin,&t0,&t1);
     if (over&2) vdp1Hidden(poly,0,viewYmax,&t0,&t1);
    }
 else
    {for (i=0;i<2;i++)
	{int ya=poly[i].y,yb=poly[3-i].y,d=yb-ya,lo,hi,sp,sm;
	 if (d==0)
	    {if (ya>VDP1LIM || ya<-VDP1LIM)
		return vdp1Range(poly);  /* edge wholly out of range at constant height: nothing
					    to window in V, it is an overflow along U -- slid there */
	     continue;
	    }
	 sp=(( vdp1VCut-ya)<<10)/d;
	 sm=((-vdp1VCut-ya)<<10)/d;
	 lo=sp<sm? sp: sm;
	 hi=sp<sm? sm: sp;
	 if (lo>t0) t0=lo;
	 if (hi<t1) t1=hi;
	}
     if (t0<0) t0=0;
     if (t1>VCLIPONE) t1=VCLIPONE;
    }
 if (t1-t0<=0)
    return 0;   /* nothing representable: the cell is over 3 screen heights away, off view */
 /* onto the pattern's row grid (VCLIPSTEP), OUTWARD and with a unit of slack: the texture can
    only be cut on a whole row, so geometry and texture have to agree there or the texture
    crawls.  Outward costs at most one row of band -- pixels that were drawn anyway before the
    cut existed -- and cannot uncover anything, which is why it is the only safe direction. */
 t0=(t0-1)&~(VCLIPSTEP-1);
 t1=(t1+VCLIPSTEP)&~(VCLIPSTEP-1);
 if (t0<0) t0=0;
 if (t1>VCLIPONE) t1=VCLIPONE;
 if (t0<=0 && t1>=VCLIPONE)
    return wide? vdp1Range(poly): 1;    /* the slack gave the whole pattern back */
#ifdef WALKPROBE
 vdp1FitCut++;
#endif
 for (i=0;i<4;i++)
    src[i]=poly[i];
 for (i=0;i<2;i++)
    {int j=3-i;
     poly[i].x=src[i].x+(((src[j].x-src[i].x)*t0)>>10);
     poly[i].y=src[i].y+(((src[j].y-src[i].y)*t0)>>10);
     poly[j].x=src[i].x+(((src[j].x-src[i].x)*t1)>>10);
     poly[j].y=src[i].y+(((src[j].y-src[i].y)*t1)>>10);
    }
 /* the exact rule cuts at the VIEW, not at the plane: the edge that binds can leave the other
    one far past VDP1LIM, and past the drawing plane the console HANGS.  vdp1Range clamps it. */
 for (i=0;i<4;i++)
    {a=poly[i].y; if (a<0) a=-a;
     if (a>VDP1LIM) tall=1;
    }
 if ((wide|tall) && !vdp1Range(poly))
    return 0;
 *t0p=t0;
 *t1p=t1;
 return 1;
}

static __inline__ void EZ_distSprVClip(short charNm,short colour,XyInt *poly,
				       struct gourTable *g)
{int t0,t1;
#if VDP1DIAG
 /* DIAGNOSTIC DISC -- fixes nothing, SHOWS.  A cell with a vertex outside the VDP1's vertical
    range is painted flat, clamped to the range, instead of textured:
       BLUE    Y overflow only
       MAGENTA both
    Axis, place and extent at a glance -- what no counter gives. */
 {int j,cx=0,cy=0;
  for (j=0;j<4;j++)
     if (poly[j].y>VDP1LIM || poly[j].y<-VDP1LIM)
	cy=1;
  if (cy)
     {for (j=0;j<4;j++)
	 {int a;
	  a=poly[j].x; if (a<0) a=-a; if (a>VDP1LIM) cx=1;
	  if (poly[j].x> VDP1LIM) poly[j].x= VDP1LIM;
	  if (poly[j].x<-VDP1LIM) poly[j].x=-VDP1LIM;
	  if (poly[j].y> VDP1LIM) poly[j].y= VDP1LIM;
	  if (poly[j].y<-VDP1LIM) poly[j].y=-VDP1LIM;
	 }
      EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,cx? RGB(31,0,31): RGB(0,0,31),poly,NULL);
      return;
     }
 }
#endif
 if (!vdp1Fit(poly,&t0,&t1))
    return;
 VDP1WALK(poly);
 cellOut(charNm,colour,t0,t1,poly,g);
}

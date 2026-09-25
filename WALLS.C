#include <machine.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_int.h>
#include <sega_mth.h>
#include <sega_sys.h>
#include <sega_dbg.h>
#include <sega_per.h>
#include <string.h>
#include <limits.h>

#include "level.h"
#include "sprite.h"
#include "walls.h"
#include "mplayer.h"
#include "util.h"
#include "spr.h"
#include "print.h"
#include "pic.h"
#include "sequence.h"
#include "sruins.h"
#include "profile.h"
#include "wallasm.h"
#include "gamestat.h"
#include "v_blank.h"
#include "dma.h"

#define WATER 1
#define WAVYWATER 0
#define RECTCLIP 1
#define ENABLEFARCLIP 0

#define TILENEARCLIP F(33)	/* GCC14: superseded -- the grid's near floor is passed per wall */
#define MIPDIST F(256)

short plaxBBymax,plaxBBxmax,
   plaxBBymin,plaxBBxmin;

static short slave_plaxBBymax,slave_plaxBBxmax,
   slave_plaxBBymin,slave_plaxBBxmin;

/* GCC14: the mip path is compiled in but gated by mipEnable (default 0 = the retail
   behaviour: retail shows none of its symptoms and no mip block was found in its
   MAIN.BIN).  Hold L+R+X in game to flip it (SRUINS.C).  Two of its three bugs are
   fixed below: (1) 1-tile dimensions halved to 0, so the wall was never emitted;
   (2) the texture list was walked sequentially on the halved grid, repeating the
   wall's top rows over the whole surface.  Still open when enabled: (3) rectTransform
   (wallasm_gnu.s) walks the per-vertex light list one byte per vertex of whatever grid
   it is given, so the halved grid reads a scrambled quarter of the light map -- fixing
   it needs per-vertex/per-row light strides in the asm; and by design each halved cell
   covers a 2x2 block of world tiles but shows only the (2h,2w) tile replicated four
   times (the mip pic is the half-res tile tiled 2x2, PIC.C map()), so any texture
   boundary inside a block bleeds a full tile row/column. */
#define MIPMAP 1

#if MIPMAP
short mipBase;
int mipEnable=0;
#endif

static int laserColor=0;

#ifndef NDEBUG
XyInt debugLines[200][2];
int nmDebugLines=0;

void addDebugLine(int x1,int y1,int x2,int y2)
{assert(nmDebugLines<200);
 debugLines[nmDebugLines][0].x=x1;
 debugLines[nmDebugLines][0].y=y1;
 debugLines[nmDebugLines][1].x=x2;
 debugLines[nmDebugLines][1].y=y2;
 nmDebugLines++;
}

void drawDebugLines(void)
{int i;
 for (i=0;i<nmDebugLines;i++)
    {EZ_line(COLOR_5|COMPO_REP,0xffff,debugLines[i],NULL);
    }
 nmDebugLines=0;
}
#endif

#define SDFLAG_NEEDTOPROCESS 1
#define SDFLAG_BBVALID       2
#define SDFLAG_CACHEVALID    4
#define SDFLAG_ADDEDTOTREE   8
#define SDFLAG_DISTANCEVALID 16

#define MAXFANIN 20
typedef
struct
{short xmin,xmax,ymin,ymax;
 Fixed32 distance;
 short ancestor[MAXFANIN];
 short flags;
 short spriteCommandStart; /* for slave rendered sectors only */
 char nmAncestors;
 char nmChildren;
 short spriteHead;         /* GP_GAME_DOOM: the sprites drawn after this leaf, index into
			      sprites[] (doom_spriteLeaves), -1 = none; was padding */
} SectorDrawRecord; /* size of this structure is 60 */

int nmPolys;

Sprite *autoTarget;
static int bestAutoAimRating;

void project_point(MthXyz *v,XyInt *p);

#if 0
void project_point(MthXyz *v,XyInt *p)
{unsigned int newZ;
 if (v->z<=F(1))
    newZ=F(1);
 else
    newZ=(unsigned int)v->z;
 {Fixed32 r;
  Set_Hardware_DivideFixed((10<<20),newZ);
  r=Get_Hardware_Divide();
  p->x = f(MTH_Mul(r,v->x));
  p->y = -f(MTH_Mul(r,v->y));
 }
#if 0
 Set_Hardware_Divide(v->y*(-focalDist>>4),newZ>>4);
 p->x = PROJECT(v->x,newZ);
 p->y = Get_Hardware_Divide();
/* p->y = -PROJECT(v->y,newZ); */
#endif
}
#endif

/* GCC14: the view window, one per view in split screen (SRUINS.C mpSetViewport).  Local
   coordinates; the local origin sits at (viewCx,viewCy) on screen.  Solo keeps the original
   320-wide window: -160..160 x CFG_YMIN..CFG_YMAX, origin (160, CFG_YCENTER), focal 160. */
int viewXmin=-160,viewXmax=160,viewYmin=CFG_YMIN,viewYmax=CFG_YMAX;
int viewCx=160,viewCy=CFG_YCENTER;
int focalDist=FOCALDIST;       /* read by the asm projection too (wallasm_gnu.s, 3 sites) */
int vdp1VCut;                  /* GCC14: 0 = the exact V cut (WALLS.H); L+R+C cycles it in game */
#define XMIN        viewXmin
#define YMIN        viewYmin   /* GP_GAME_DOOM: -112 (3D window = screen lines 0..191, SPEC_PLAYER 3.1) */
#define XMAX        viewXmax
#define YMAX        viewYmax   /* GP_GAME_DOOM: 80 */

/* GCC14: the viewer this image is built from, copied once per view.  The slave reads it while
   the master runs the game logic, which moves the camera and, in split screen, switches it to
   another player: reading `camera` there would cull view 0 against someone else's position. */
static Sprite *viewCamera;
static MthXyz viewPos;
static int viewSector;
static void setViewer(Sprite *c)
{viewCamera=c;
 viewPos=c->pos;
 viewSector=c->s;
}
Bool clip_visible( XyInt *poly,SectorDrawRecord *s);

#if 0
__asm__
    (".align 4\n"
     ".global _clip_visible\n"
     "_clip_visible:\n"
     "lds.l r8,macl\n"
     /* load mins and maxes */
     "mov.w @r5+,r8\n" /* xmin */
     "mov.w @r5+,r7\n" /* xmax */
     "mov #0,r0\n" /* r0 is test accum */
     "mov.w @r5+,r6\n" /* ymin */
     "mov #4,r3\n" /* r3 is loop counter */
     "mov.w @r5,r5\n" /* ymax */

     /* r2 is per loop accum */
  "LLOOOOP:\n"
     "mov.w @r4+,r1\n" /* load x value */
     "cmp/ge r8,r1\n"
     "rotcl r2\n"
     "cmp/ge r1,r7\n"

     "mov.w @r4+,r1\n" /* load y value */
     "rotcl r2\n"
     "cmp/ge r6,r1\n"
     "rotcl r2\n"
     "cmp/ge r1,r5\n"
     "rotcl r2\n"

     "dt r3\n"
     "bf.s LLOOOOP\n"
     "or r2,r0\n"

     "and #15,r0\n"
     "cmp/eq #15,r0\n"
     "movt r0\n"

     "rts\n"
     "sts.l macl,r8\n"

     );
#endif

#if 1
Bool clip_visible( XyInt *poly,SectorDrawRecord *s)
{
#if 0
 int p,accum;
 accum=0;
 for (p=0;p<4;p++)
    {if (poly[p].x>=s->xmin)
	accum|=1;
     if (poly[p].y>=s->ymin)
	accum|=2;
     if (poly[p].x<=s->xmax)
	accum|=4;
     if (poly[p].y<=s->ymax)
	accum|=8;
    }
 return (accum==0xf);
#else
/*top side clip*/
 if (poly[0].y<s->ymin && poly[1].y<s->ymin && poly[2].y<s->ymin &&
     poly[3].y<s->ymin)
    return( FALSE );
/*bottom side clip*/
 if (poly[0].y>s->ymax && poly[1].y>s->ymax && poly[2].y>s->ymax &&
     poly[3].y>s->ymax)
    return( FALSE );
 /*left side clip*/
 if (poly[0].x<s->xmin && poly[1].x<s->xmin && poly[2].x<s->xmin &&
     poly[3].x<s->xmin)
    return( FALSE );
 /*right side clip*/
 if (poly[0].x>s->xmax && poly[1].x>s->xmax && poly[2].x>s->xmax &&
     poly[3].x>s->xmax)
    return( FALSE );
 return( TRUE ); /*polygon is visible*/
#endif
}
#endif

void clipZTile(MthXyz *pointsIn,MthXyz *pointsOut,Fixed32 nearClip)
{int i;
 for (i=0;i<4;i++)
    {if (pointsIn[i].z<nearClip)
	{pointsOut[i].z=nearClip;
         pointsOut[i].x=pointsIn[i].x;
         pointsOut[i].y=pointsIn[i].y;
         continue;
	}
     else
	{pointsOut[i].x=pointsIn[i].x;
	 pointsOut[i].y=pointsIn[i].y;
	 pointsOut[i].z=pointsIn[i].z;
	}
    }
}


void clipZ(MthXyz *pointsIn,MthXyz *pointsOut,Fixed32 nearClip)
{int i,left,right;
 Fixed32 ratio;
 for (i=0;i<4;i++)
    {left=i+1;
     if (left>=4) left-=4;
     right=i-1;
     if (right<0) right+=4;
     if (pointsIn[i].z<nearClip)
	{pointsOut[i].z=nearClip;
         if (pointsIn[left].z>nearClip &&
	     pointsIn[right].z<=nearClip)
	    {ratio=MTH_Div(pointsIn[left].z-nearClip,
			   pointsIn[left].z-pointsIn[i].z);
	     assert(ratio>=0);
	     assert(ratio<=F(1));
	     pointsOut[i].x=pointsIn[left].x-
		MTH_Mul((pointsIn[left].x-pointsIn[i].x),ratio);
	     /* out is between i and left */
	     assert((pointsOut[i].x>=pointsIn[left].x &&
		     pointsOut[i].x<=pointsIn[i].x) ||
		    (pointsOut[i].x<=pointsIn[left].x &&
		     pointsOut[i].x>=pointsIn[i].x));
	     pointsOut[i].y=pointsIn[left].y-
		MTH_Mul((pointsIn[left].y-pointsIn[i].y),ratio);
	     assert((pointsOut[i].y>=pointsIn[left].y &&
		     pointsOut[i].y<=pointsIn[i].y) ||
		    (pointsOut[i].y<=pointsIn[left].y &&
		     pointsOut[i].y>=pointsIn[i].y));
	    }
	 else
	    {if (pointsIn[right].z>nearClip &&
		 pointsIn[left].z<=nearClip)
		{ratio=MTH_Div(pointsIn[right].z-nearClip,
			   pointsIn[right].z-pointsIn[i].z);
		 assert(ratio>=0);
		 assert(ratio<=F(1));
		 pointsOut[i].x=pointsIn[right].x-
		    MTH_Mul((pointsIn[right].x-pointsIn[i].x),ratio);
		 /* out is between i and left */
		 assert((pointsOut[i].x>=pointsIn[right].x &&
			 pointsOut[i].x<=pointsIn[i].x) ||
			(pointsOut[i].x<=pointsIn[right].x &&
			 pointsOut[i].x>=pointsIn[i].x));
		 pointsOut[i].y=pointsIn[right].y-
		    MTH_Mul((pointsIn[right].y-pointsIn[i].y),ratio);
		 assert((pointsOut[i].y>=pointsIn[right].y &&
			 pointsOut[i].y<=pointsIn[i].y) ||
			(pointsOut[i].y<=pointsIn[right].y &&
			 pointsOut[i].y>=pointsIn[i].y));
		}
	     else
		{pointsOut[i].x=pointsIn[i].x;
		 pointsOut[i].y=pointsIn[i].y;
		}
	    }
	}
     else
	{pointsOut[i].x=pointsIn[i].x;
	 pointsOut[i].y=pointsIn[i].y;
	 pointsOut[i].z=pointsIn[i].z;
	}
    }
}


#define goodPoint(p) (greater?((Fixed32 *)(p))[clipAxis]>clipLine:((Fixed32 *)(p))[clipAxis]<clipLine)

void clipZSub(int clipAxis,Fixed32 clipLine,int greater,
	      MthXyz *pointsIn,Fixed32 *shadeIn,int nmInQuads,
	      MthXyz *pointsOut,Fixed32 *shadeOut,int *nmOutQuads)
{int q,p,nmRing;
 int next;
 int i;
 MthXyz ring[10];
 Fixed32 shadeRing[10];
 int nmOutPoints;

 *nmOutQuads=0;
 nmOutPoints=0;
 for (q=0;q<nmInQuads;q++)
    {nmRing=0;
     for (p=0;p<4;p++)
	{/* consider the lines one at a time */
	 next=p+1;
	 if (next>=4) next-=4;

	 if ((i=goodPoint(pointsIn+p)))
	    {shadeRing[nmRing]=shadeIn[p];
	     ring[nmRing++]=pointsIn[p];
	     assert(nmRing<10);
	    }
	 if (i!=goodPoint(pointsIn+next))
	    /* add intersection point */
	    {Fixed32 inter[3];
	     Fixed32 *thisA,*nextA;
	     Fixed32 ratio;
	     thisA=(Fixed32 *)(pointsIn+p);
	     nextA=(Fixed32 *)(pointsIn+next);
	     inter[clipAxis]=clipLine;
	     ratio=MTH_Div(nextA[clipAxis]-clipLine,
			   nextA[clipAxis]-thisA[clipAxis]);
#if 0
	     if (ratio<0)
		{dPrint("ratio %d\n %d %d %d\n",ratio,
			nextA[clipAxis],clipLine,thisA[clipAxis]);
		 dPrint("%d",clipAxis);
		}
#endif
	     assert(ratio>=0); /* can crash here. Why? */
	     assert(ratio<=F(1));

	     for (i=0;i<3;i++)
		{if (i==clipAxis)
		    continue;
		 inter[i]=nextA[i]-MTH_Mul(nextA[i]-thisA[i],ratio);
		}
	     ring[nmRing].x=inter[0];
	     ring[nmRing].y=inter[1];
	     ring[nmRing].z=inter[2];
	     shadeRing[nmRing]=shadeIn[next]-
		MTH_Mul(shadeIn[next]-shadeIn[p],ratio);
	     nmRing++;
	     assert(nmRing<10);
	    }
	}
     /* now look at the points in the ring and translate them into quads */
     switch (nmRing)
	{case 0:
	    break;
	 case 3:
	    for (i=0;i<nmRing;i++)
	       {shadeOut[nmOutPoints]=shadeRing[i];
		pointsOut[nmOutPoints++]=ring[i];
	       }
	    shadeOut[nmOutPoints]=shadeRing[0];
	    pointsOut[nmOutPoints++]=ring[0];
	    (*nmOutQuads)++;
	    break;
	 case 4:
	    for (i=0;i<nmRing;i++)
	       {shadeOut[nmOutPoints]=shadeRing[i];
		pointsOut[nmOutPoints++]=ring[i];
	       }
	    (*nmOutQuads)++;
	    break;
	 case 5:
	    for (i=0;i<4;i++)
	       {shadeOut[nmOutPoints]=shadeRing[i];
		pointsOut[nmOutPoints++]=ring[i];
	       }
	    (*nmOutQuads)++;
	    assert(*nmOutQuads<12);
	    shadeOut[nmOutPoints]=shadeRing[3];
	    pointsOut[nmOutPoints++]=ring[3];
	    shadeOut[nmOutPoints]=shadeRing[4];
	    pointsOut[nmOutPoints++]=ring[4];
	    shadeOut[nmOutPoints]=shadeRing[0];
	    pointsOut[nmOutPoints++]=ring[0];
	    shadeOut[nmOutPoints]=shadeRing[0];
	    pointsOut[nmOutPoints++]=ring[0];
	    (*nmOutQuads)++;
	    break;
	 default:
#if 0
#ifndef NDEBUG
	    dPrint("nmRing=%d\n",nmRing);
	    for (p=0;p<4;p++)
	       {dPrint(" p(%d,%d,%d)\n",
		       pointsIn[p].x,
		       pointsIn[p].y,
		       pointsIn[p].z);
		dPrint("axis %d   line %d  greater %d\n",clipAxis,
		       clipLine,greater);
	       }
#endif
#endif
	    assert(0);
	   }
     pointsIn+=4;
     shadeIn+=4;
     assert(*nmOutQuads<12);
    }
}




#define WATERCOLOR (RGB(4,4,8))
#define GETWATERBRIGHT(x,z)  ((int)(waterBright[(((unsigned int)(x))>>22)&0xf][(((unsigned int)(z))>>22)&0xf]))
static char waterBright[16][16];
static int sawWater;
static int waterPos[16][16];
static int waterVel[16][16];
void initWater(void)
{int x,y,i;
 extern unsigned short randTable[];
 i=0;
 for (y=0;y<16;y++)
    for (x=0;x<16;x++)
       {waterPos[y][x]=randTable[i++]<<4;
	waterVel[y][x]=0;
       }
/* waterVel[0][1]=F(1000);
 waterVel[2][1]=F(1000);
 waterVel[1][1]=F(1000);
 waterVel[1][0]=F(1000);
 waterVel[1][2]=F(1000);

 waterVel[7][4]=F(-1000);
 waterVel[9][4]=F(-1000);
 waterVel[8][4]=F(-1000);
 waterVel[8][3]=F(-1000);
 waterVel[8][5]=F(-1000);*/
}

void stepWater(void)
{int x,y,b;
 laserColor+=2;
 if (laserColor>61)
    laserColor=0;
 if (camera->flags & SPRITEFLAG_UNDERWATER)
    sawWater=5;
 if (!sawWater)
    return;
 sawWater--;
 for (y=0;y<16;y++)
    for (x=0;x<16;x++)
       {waterPos[y][x]+=waterVel[y][x]>>8/*8*/;
	b=f(waterPos[y][x])+15;
	if (b<0) b=0;
	if (b>31) b=31;
	waterBright[y][x]=b;
       }
 for (y=0;y<16;y++)
    for (x=0;x<16;x++)
       {waterVel[y][x]+=
	   waterPos[(y+1)&0xf][x&0xf]+
	   waterPos[(y-1)&0xf][x&0xf]+
	   waterPos[y&0xf][(x+1)&0xf]+
	   waterPos[y&0xf][(x-1)&0xf]-(waterPos[y][x]<<2);
       }
}

#define LIGHT
/* GCC14: two light models, one per light (lMode).
   - subtractive (addLight): Lobotomy's, kept bit for bit for AI.C / AI2.C.
   - proportional (addLightEx, Doom): tint k 0..16 per channel, then radius and intensity 0..31
     per light.  s = (R^2 - d^2)/R^2 on 0..256, squared if CFG_LIGHTSMOOTH; adds
     s * k * intensity >> 12.
   BETTERLIGHT is gone: its #else left a dangling else that dropped the red channel. */
#define MAXNMLIGHTSOURCES 15
#define LIGHTRADIUS CFG_LIGHTRADIUS

static Sprite *lightSource[MAXNMLIGHTSOURCES];
int nmLights;                              /* GCC14: lights live in this image (WALLS.H, overlay) */
static int delayNmLights;
static MthXyz tLightPos[MAXNMLIGHTSOURCES];
/* GCC14: where each light stood when the view was drawn, in world units, and the leaf it stood
   in.  Made by drawWalls with tLightPos, before the slave is kicked: the slave reads these, never
   lightSource[]->pos, which runObjects moves while it draws. */
static int wLightPos[MAXNMLIGHTSOURCES][3];
static short lightLeaf[MAXNMLIGHTSOURCES];
/* GCC14: the leaves of the view being drawn, defined below: the light lists name a leaf */
extern SectorDrawRecord *sectorDraw;
static int lColor[MAXNMLIGHTSOURCES][3];   /* proportional: k * intensity */
static int delayColor[MAXNMLIGHTSOURCES][3];
/* GCC14: radius, radius^2 and (1<<24)/radius^2, set once per light: no divide per vertex. */
static int lRad[MAXNMLIGHTSOURCES],lRad2[MAXNMLIGHTSOURCES],lInv[MAXNMLIGHTSOURCES];
static int delayRad[MAXNMLIGHTSOURCES];
static char lMode[MAXNMLIGHTSOURCES];      /* 0 subtractive, 1 proportional */
static signed char lPrio[MAXNMLIGHTSOURCES];   /* GCC14: who gives way when the list is full */
static char lightColorChanged=0,lightsDeleted=0;
static char delayDeleteLight[MAXNMLIGHTSOURCES];
static void lightInit(void)
{int i;
 for (i=0;i<MAXNMLIGHTSOURCES;i++)
    {lightSource[i]=NULL;
     delayDeleteLight[i]=0;
    }
 nmLights=0;
 delayNmLights=0;
 lightColorChanged=0;
 lightsDeleted=0;
}

static void lightSetRadius(int i,int radius)
{lRad[i]=radius;
 lRad2[i]=radius*radius;
 lInv[i]=(1<<24)/lRad2[i];
}

/* GCC14: a full list used to refuse the new light, whatever it was -- the player's own muzzle
   flash behind fifteen fireballs.  Now the new light takes the slot of one that matters less: a
   light the camera's leaf never sees first, then the lowest priority, then the farthest from the
   camera; an equal priority gives way only when it is farther than the newcomer.  The slot is
   rewritten through the delayed values, as changeLightEx does: the image being drawn keeps its
   lights.  Nothing to take: refused, as before.  Priority 0 is what addLight and addLightEx ask. */
static int lightDist(Sprite *s)
{return camera? approxDist(s->pos.x-camera->pos.x,s->pos.y-camera->pos.y,s->pos.z-camera->pos.z): 0;
}

static int lightEvict(Sprite *s,int prio)
{int i,best=-1,bh=0,bp=0,bd=0,h,d,dn=lightDist(s);
 for (i=0;i<delayNmLights;i++)
    {if (delayDeleteLight[i] || lPrio[i]>prio)
	continue;
     h=camera? !level_maySee(lightSource[i]->s,camera->s): 0;
     d=lightDist(lightSource[i]);
     if (!h && lPrio[i]==prio && d<=dn)
	continue;
     if (best<0 || h>bh || (h==bh && (lPrio[i]<bp || (lPrio[i]==bp && d>bd))))
	{best=i; bh=h; bp=lPrio[i]; bd=d;}
    }
 return best;
}

static void lightPut(Sprite *s,int r,int g,int b,int radius,int mode,int prio)
{int i=delayNmLights;
 if (i>=MAXNMLIGHTSOURCES)
    {if (!CFG_LIGHT_EVICT)
	return;                 /* PowerSlave: a full list refuses the new light, as it always did */
     i=lightEvict(s,prio);
     if (i<0)
	return;
     /* the whole slot changes at once -- lMode chooses the branch lightApply takes and lRad2
	and lInv go with the radius, so a half-changed slot would be read by the slave */
     lColor[i][0]=delayColor[i][0]=r;
     lColor[i][1]=delayColor[i][1]=g;
     lColor[i][2]=delayColor[i][2]=b;
     lightSetRadius(i,radius);
     delayRad[i]=radius;
     lMode[i]=mode;
     lPrio[i]=prio;
     lightSource[i]=s;
     return;
    }
 lColor[i][0]=delayColor[i][0]=r;
 lColor[i][1]=delayColor[i][1]=g;
 lColor[i][2]=delayColor[i][2]=b;
 lightSetRadius(i,radius);
 delayRad[i]=radius;
 lMode[i]=mode;
 lPrio[i]=prio;
 lightSource[i]=s;
 delayNmLights++;
}

void addLight(Sprite *s,int r,int g,int b)
{lightPut(s,r,g,b,LIGHTRADIUS,0,0);
}

/* GCC14: the player's settings over what an effect asks (WALLS.H): the switch, the two scales
   and the tint offsets.  0 = nothing to light (intensity fell to 0, or the lights are off). */
static int lightTune(int *r,int *g,int *b,int *radius,int *peak)
{int k[3],i,p,d;
 if (!lightOn)
    return 0;
 k[0]=*r; k[1]=*g; k[2]=*b;
 for (i=0;i<3;i++)
    {d=k[i]+((i==0)? lightAddR: (i==1)? lightAddG: lightAddB);
     k[i]=(d<0)? 0: (d>16)? 16: d;
    }
 *r=k[0]; *g=k[1]; *b=k[2];
 p=(*peak)*lightPeakPct/100;
 *peak=(p>31)? 31: p;
 if (*radius>0)
    {d=(*radius)*lightRadPct/100;
     *radius=(d<16)? 16: (d>1024)? 1024: d;
    }
 return *peak>0;
}

/* GCC14: k 0..16 per channel, radius in world units, intensity 0..31 at the centre. */
void addLightPrio(Sprite *s,int r,int g,int b,int radius,int peak,int prio)
{assert(radius>=16 && radius<=1024);
 assert(peak>=0 && peak<=31);
 assert(r>=0 && r<=16 && g>=0 && g<=16 && b>=0 && b<=16);
 if (!lightTune(&r,&g,&b,&radius,&peak))
    return;                       /* the player's settings put this light out */
 lightPut(s,r*peak,g*peak,b*peak,radius,1,prio);
}

void addLightEx(Sprite *s,int r,int g,int b,int radius,int peak)
{addLightPrio(s,r,g,b,radius,peak,0);
}

/* GCC14: the live light of s, or -1.  Only [0, delayNmLights) is live: scanning the stale
   tail could match a recycled sprite and kill the next light placed at that index. */
static int lightFind(Sprite *s)
{int i;
 for (i=0;i<delayNmLights;i++)
    if (lightSource[i]==s && !delayDeleteLight[i])
       return i;
 return -1;
}

/* GCC14: values in the light's own model -- proportional: k 0..16 and intensity `peak`;
   subtractive: raw, peak ignored.  radius <= 0 keeps the radius. */
void changeLightEx(Sprite *s,int r,int g,int b,int radius,int peak)
{int i=lightFind(s);
 int m;
 if (i<0)
    return;
 assert(!lMode[i] || (peak>=0 && peak<=31));
 if (lMode[i])
    {lightTune(&r,&g,&b,&radius,&peak);   /* switched off gives intensity 0: an unlit light */
     m=peak;
    }
 else
    m=1;
 delayColor[i][0]=r*m;
 delayColor[i][1]=g*m;
 delayColor[i][2]=b*m;
 if (radius>0)
    {assert(radius>=16 && radius<=1024);
     delayRad[i]=radius;
    }
 lightColorChanged=1;
}

/* subtractive lights only (AI.C, AI2.C) */
void changeLightColor(Sprite *s,int r,int g,int b)
{changeLightEx(s,r,g,b,0,0);
}

void removeLight(Sprite *s)
{int i=lightFind(s);
 if (i<0)
    return;
 lightsDeleted=1;
 delayDeleteLight[i]=1;
}

/* GCC14: the list takes no duplicates -- removeLight drops the first one only. */
int hasLight(Sprite *s)
{return lightFind(s)>=0;
}


void updateLights(void)
{int i,j;
 nmLights=delayNmLights;

 if (lightColorChanged)
    {lightColorChanged=0;
     for (i=0;i<nmLights;i++)
	{lColor[i][0]=delayColor[i][0];
	 lColor[i][1]=delayColor[i][1];
	 lColor[i][2]=delayColor[i][2];
	 if (delayRad[i]!=lRad[i])
	    lightSetRadius(i,delayRad[i]);
	}
    }
 if (lightsDeleted)
    {for (i=0,j=0;j<nmLights;j++)
	{if (!delayDeleteLight[j])
	    {if (i!=j)
		{lightSource[i]=lightSource[j];
		 lColor[i][0]=lColor[j][0];
		 lColor[i][1]=lColor[j][1];
		 lColor[i][2]=lColor[j][2];
		 delayColor[i][0]=lColor[i][0];
		 delayColor[i][1]=lColor[i][1];
		 delayColor[i][2]=lColor[i][2];
		 lRad[i]=lRad[j];
		 lRad2[i]=lRad2[j];
		 lInv[i]=lInv[j];
		 delayRad[i]=lRad[i];
		 lMode[i]=lMode[j];
		 lPrio[i]=lPrio[j];
		}
	     i++;
	    }
	 else
	    delayDeleteLight[j]=0;
	}
     nmLights=i;
     delayNmLights=i;
     lightsDeleted=0;
    }
}

/* GCC14: adds light i at pos (view space) to r,g,b.  Shared by getLight, sgetLight and
   drawSprites; the box test runs before any multiply, and before any overflow.  Always inlined:
   out of line it was a call per vertex with r,g,b through the stack.  The products are >= 0
   (u > 0, sv <= 256), so the shifts are unsigned: signed ones by 8, 12 or 16 are libgcc calls. */
static inline __attribute__((always_inline))
void lightApplyD(int i,int dx,int dy,int dz,int *r,int *g,int *b)
{int rad=lRad[i];
 int u;
 if (dx>rad || dx<-rad || dy>rad || dy<-rad || dz>rad || dz<-rad)
    return;
 u=lRad2[i]-(dx*dx+dy*dy+dz*dz);
 if (u<=0)
    return;
 if (lMode[i])
    {unsigned sv=((unsigned)u*(unsigned)lInv[i])>>16;   /* 0..256 */
#if CFG_LIGHTSMOOTH
     sv=(sv*sv)>>8;                   /* soft edge */
#endif
     *r+=(sv*(unsigned)lColor[i][0])>>12;       /* <= intensity */
     *g+=(sv*(unsigned)lColor[i][1])>>12;
     *b+=(sv*(unsigned)lColor[i][2])>>12;
    }
 else
    {u=(unsigned)u>>CFG_LIGHTSHIFT;
     if (u-lColor[i][0]>0)
	*r+=u-lColor[i][0];
     if (u-lColor[i][1]>0)
	*g+=u-lColor[i][1];
     if (u-lColor[i][2]>0)
	*b+=u-lColor[i][2];
    }
}

/* The light at a point of the VIEW: what every wall vertex asks for. */
static inline __attribute__((always_inline))
void lightApply(int i,MthXyz *pos,int *r,int *g,int *b)
{lightApplyD(i,f(pos->x-tLightPos[i].x),f(pos->y-tLightPos[i].y),f(pos->z-tLightPos[i].z),r,g,b);
}

/* GCC14: and the same light at a point of the WORLD.  The view transform is a rotation and a
   translation, so the three distances above are the ones this one works out -- the answer is the
   same, to the unit the two roundings differ by, and it no longer belongs to one camera.  That is
   what lets a thing's light be found once per image instead of once per view (drawSprites). */
static inline __attribute__((always_inline))
void lightApplyW(int i,MthXyz *pos,int *r,int *g,int *b)
{lightApplyD(i,f(pos->x)-wLightPos[i][0],f(pos->y)-wLightPos[i][1],f(pos->z)-wLightPos[i][2],
	     r,g,b);
}


/* GCC14: the lights that reach a wall, as a list of their indexes.  A light is kept when the view
   may see its leaf, when it is within its radius of the box of the wall's vertices -- the corners
   of a grid wall bound its grid, a mesh wall is read vertex by vertex -- and on the visible side
   of the wall's plane, within its radius.  The plane alone marked every floor at the height of a
   fireball, across the map: half the view's vertices went through the lit path, and those walls
   lost the far and black LODs.  lightApply gives 0 to a vertex out of every light's reach, so the
   image does not change.  Master and slave each keep their own list. */
static int lightListFor(sWallType *wall,int sector,signed char *idx)
{int l,n,v,v0,v1,x0,y0,z0,x1,y1,z1,rad;
 Fixed32 dist;
 const sVertexType *p;
 if (wall->flags & WALLFLAG_PARALLELOGRAM)
    {v=0; v0=0; v1=3;}
 else
    {v=1; v0=wall->firstVertex; v1=wall->lastVertex;}
 p=level_vertex+(v? v0: wall->v[0]);
 x0=x1=p->x; y0=y1=p->y; z0=z1=p->z;
 for (l=v0+1;l<=v1;l++)
    {p=level_vertex+(v? l: wall->v[l]);
     if (p->x<x0) x0=p->x; else if (p->x>x1) x1=p->x;
     if (p->y<y0) y0=p->y; else if (p->y>y1) y1=p->y;
     if (p->z<z0) z0=p->z; else if (p->z>z1) z1=p->z;
    }
 p=level_vertex+wall->v[0];
 n=0;
 for (l=0;l<nmLights;l++)
    {int lx=wLightPos[l][0],ly=wLightPos[l][1],lz=wLightPos[l][2];
     rad=lRad[l]+2;                   /* +2: lightApply's units are floored in view space */
     if (lx+rad<x0 || lx-rad>x1 || ly+rad<y0 || ly-rad>y1 || lz+rad<z0 || lz-rad>z1)
	continue;
     /* the distance from the light to the wall's plane: only a light on the visible side (the
	backface test's dot product); the old test lit walls from up to a radius behind them */
     dist=(lx-p->x)*wall->normal[0]+(ly-p->y)*wall->normal[1]+(lz-p->z)*wall->normal[2];
     if (dist<=0 || dist>F(lRad[l]))
	continue;
     /* a light whose leaf never sees this one (the level's reject table) reaches it only
	through the walls between them: no light is occluded here, so it is not kept */
     if (!level_maySee(lightLeaf[l],sector))
	continue;
     idx[n++]=(signed char)l;
    }
 return n;
}

int nmWallLights;
static signed char wallLightIdx[MAXNMLIGHTSOURCES];
/* GCC14: the nukage's green is not tested here any more -- it is CUT INTO THE LIGHT BYTE of
   each vertex, bits 5-6, and the world's ramp has one band per level (UTIL.H).  The renderer's
   assembler indexes the ramp with that byte untouched, so a green wall costs nothing and keeps
   its LODs; only the lit path below has to take the byte apart. */
static void buildLightList(sWallType *wall,int sector)
{WALLCLS(wall);
 nmWallLights=nmLights? lightListFor(wall,sector,wallLightIdx): 0;
}

static int snmWallLights;
static signed char swallLightIdx[MAXNMLIGHTSOURCES];
static void sbuildLightList(sWallType *wall,int sector)
{WALLCLS(wall);
 snmWallLights=nmLights? lightListFor(wall,sector,swallLightIdx): 0;
}

#define NEARCLIP F(GP_NEAR_CLIP) /* MUST stay under the player radius: SPRITE.C:141 parks the eye exactly there */
#define SECTORBNDRYNEARCLIP F(-1)
#define FARCLIP F(1024)
#define FARCLIP2 10
/*#define FARCLIP F(256)
  #define FARCLIP2 8 */

static int wavyIndex=0;

unsigned short getLight(char vlight,
			MthXyz *pos)
{int r,g,b,i;
 if (wavyIndex)
    {vlight+=((*(((char *)waterBright)+wavyIndex))-20)>>1;
     if (vlight>31) vlight=31;
     if (vlight<0) vlight=0;
     wavyIndex=(wavyIndex+7)&0xff;
     if (!wavyIndex)
	wavyIndex++;
    }
 if (!nmWallLights)
    return worldGrey[(int)(unsigned char)vlight];
 /* GCC14: the byte carries the green band in bits 5-6.  The TINT goes on the vertex's own light
    and the lights are added AFTER it, with their own colour: a lamp over nukage must wash the
    green out, not be washed green by it. */
 r=g=b=((unsigned char)vlight)&31;
 {int k=((unsigned char)vlight)>>WORLDTINT_SH;
  if (k)
     {r=(r*(16-(((16-worldTint[0])*k)/(WORLDTINT_NM-1))))>>4;
      g=(g*(16-(((16-worldTint[1])*k)/(WORLDTINT_NM-1))))>>4;
      b=(b*(16-(((16-worldTint[2])*k)/(WORLDTINT_NM-1))))>>4;
      g+=((31-g)*k*WORLDTINT_GLOW)/((WORLDTINT_NM-1)*16);   /* the pool lights it (UTIL.H) */
     }
 }
 for (i=0;i<nmWallLights;i++)
    lightApply(wallLightIdx[i],pos,&r,&g,&b);
/* GCC14: THE CEILING IS TAKEN ON THE WHOLE COLOUR, NOT ON EACH CHANNEL.  What reaches the VDP1
   is a GOURAUD word -- an offset, 16 being the surface's own colour -- so a tint is carried by
   the DIFFERENCE between the three channels and by nothing else: the nukage's green is
   (13,16,11), three under the neutral in red and five in blue.  Clamping each channel on its own
   let a lamp push the highest to 31 and hold it there while the others caught up, the difference
   closed, and a green wall under a light came out WHITE (reported 2026-09-23).
   Taking the overflow off all three keeps every difference exactly, which is the hue, and costs
   what the three tests cost anyway.  A colour bright enough to drive a channel under zero has
   nothing left to say. */
 {int m=(r>g)? r: g;
  if (b>m)
     m=b;
  if (m>31)
     {m-=31;
      r-=m; if (r<0) r=0;
      g-=m; if (g<0) g=0;
      b-=m; if (b<0) b=0;
     }
 }
 return RGB(r,g,b);
}

static int sWavyIndex=0;
unsigned short sgetLight(char vlight,
			 MthXyz *pos)
{int r,g,b,i;
 if (sWavyIndex)
    {vlight+=((*(((char *)waterBright)+sWavyIndex))-20)>>1;
     if (vlight>31) vlight=31;
     if (vlight<0) vlight=0;
     sWavyIndex=(sWavyIndex+7)&0xff;
     if (!sWavyIndex)
	sWavyIndex++;
    }
 if (!snmWallLights)
    return worldGrey[(int)(unsigned char)vlight];
 r=g=b=((unsigned char)vlight)&31;
 {int k=((unsigned char)vlight)>>WORLDTINT_SH;   /* the green band: see getLight */
  if (k)
     {r=(r*(16-(((16-worldTint[0])*k)/(WORLDTINT_NM-1))))>>4;
      g=(g*(16-(((16-worldTint[1])*k)/(WORLDTINT_NM-1))))>>4;
      b=(b*(16-(((16-worldTint[2])*k)/(WORLDTINT_NM-1))))>>4;
      g+=((31-g)*k*WORLDTINT_GLOW)/((WORLDTINT_NM-1)*16);   /* the pool lights it (UTIL.H) */
     }
 }
 for (i=0;i<snmWallLights;i++)
    lightApply(swallLightIdx[i],pos,&r,&g,&b);
 {int m=(r>g)? r: g;              /* the whole colour, as getLight above */
  if (b>m)
     m=b;
  if (m>31)
     {m-=31;
      r-=m; if (r<0) r=0;
      g-=m; if (g<0) g=0;
      b-=m; if (b<0) b=0;
     }
 }
 return RGB(r,g,b);
}


#define MAXSUBS 12
void drawClippedFace(sFaceType *face,Fixed32 *shades,MthMatrix *view,
		     int wallStartVtx)
{int i,z;
 int buff1Size,buff2Size;
 MthXyz buff1[MAXSUBS*4];
 MthXyz buff2[MAXSUBS*4];
 Fixed32 shade1[MAXSUBS*4];
 Fixed32 shade2[MAXSUBS*4];
 XyInt poly[4];
 struct gourTable gtable;

 checkStack();

 for (i=0;i<4;i++)
    {getVertex(face->v[i]+wallStartVtx,buff1+i);
     MTH_CoordTrans(view,buff1+i,buff2+i);
    }
 for (i=0;i<4;i++)
    shade2[i]=F(shades[i]);
 clipZSub(2,F(5),1,
	  buff2,shade2,1,
	  buff1,shade1,&buff1Size);
 for (i=0;i<buff1Size*4;i++)
    {
#if 1
     XyInt xy;
     project_point(buff1+i,&xy);
     buff2[i].x=F(xy.x);
     buff2[i].y=F(xy.y);
#else
     buff2[i].x=F(PROJECT(buff1[i].x,buff1[i].z));
     buff2[i].y=-F(PROJECT(buff1[i].y,buff1[i].z));
#endif
     shade2[i]=shade1[i];
     if (buff2[i].x>F(2000))
	buff2[i].x=F(2000);
     if (buff2[i].x<F(-2000))
	buff2[i].x=F(-2000);
     if (buff2[i].y>F(2000))
	buff2[i].y=F(2000);
     if (buff2[i].y<F(-2000))
	buff2[i].y=F(-2000);
     buff2[i].z=0;
    }
 buff2Size=buff1Size;
 clipZSub(0,F(-160),1,
	  buff2,shade2,buff2Size,
	  buff1,shade1,&buff1Size);
 assert(buff1Size<MAXSUBS);

 clipZSub(0,F(160),0,
	  buff1,shade1,buff1Size,
	  buff2,shade2,&buff2Size);
 assert(buff2Size<MAXSUBS);

 clipZSub(1,F(-120),1,
	  buff2,shade2,buff2Size,
	  buff1,shade1,&buff1Size);
 assert(buff1Size<MAXSUBS);

 clipZSub(1,F(120),0,
	  buff1,shade1,buff1Size,
	  buff2,shade2,&buff2Size);
 assert(buff2Size<MAXSUBS);

 for (i=0;i<buff2Size;i++)
    {for (z=0;z<4;z++)
	{poly[z].x=f(buff2[z+i*4].x);
	 poly[z].y=f(buff2[z+i*4].y);
	 assert(poly[z].x>-161);
	 assert(poly[z].y>-121);
	 assert(poly[z].x<161);
	 assert(poly[z].y<121);
	 assert(f(shade2[z+i*4])>=0);
	 assert(f(shade2[z+i*4])<32);
	 gtable.entry[z]=worldGrey[f(shade2[z+i*4])];
	}
     EZ_polygon(DRAW_GOURAU|DRAW_MESH|ECD_DISABLE|SPD_DISABLE,
		WATERCOLOR,poly,&gtable);
    }
}

#if 0
void drawWater(sWallType *theWall,MthXyz *coords)
{int i,z,field;
 int buff1Size,buff2Size;
 MthXyz buff1[MAXSUBS*4];
 MthXyz buff2[MAXSUBS*4];
 Fixed32 shade1[MAXSUBS*4];
 Fixed32 shade2[MAXSUBS*4];
 XyInt poly[4];
 struct gourTable gtable;

 checkStack();
#if 0
 {static int wave=0;
  static Fixed32 pos[4]={F(10),F(7),F(4),-F(5)};
  static Fixed32 vel[4]={0,0,0,0};
  int i;
  for (i=0;i<4;i++)
     pos[i]+=vel[i]>>8;
  vel[0]+=-4*pos[0]+pos[1]+pos[3];
  vel[1]+=-4*pos[1]+pos[0]+pos[2];
  vel[2]+=-4*pos[2]+pos[1]+pos[3];
  vel[3]+=-4*pos[3]+pos[0]+pos[2];
  for (i=0;i<4;i++)
     shade2[i]=pos[i]+F(15);
 }
#endif

 if (theWall->normal[1]!=0)
    {/* water */
     WaveFace *f=level_waveFace+(int)theWall->object;
     for (i=0;i<4;i++)
	{shade2[i]=level_waveVert[f->connect[i]].pos+F(15);
	 if (shade2[i]<0)
	    shade2[i]=0;
	 if (shade2[i]>=F(31))
	    shade2[i]=F(31);
	 assert(f(shade2[i])>=0);
	 assert(f(shade2[i])<32);
	}
     sawWater=6;
     field=0;
    }
 else
    {/* force field */
     for (i=0;i<4;i++)
	shade2[i]=force[i];
     field=1;
    }

 clipZSub(2,F(5),1,
	  coords,shade2,1,
	  buff1,shade1,&buff1Size);

 for (i=0;i<buff1Size*4;i++)
    {buff2[i].x=F(PROJECT(buff1[i].x,buff1[i].z));
     buff2[i].y=-F(PROJECT(buff1[i].y,buff1[i].z));
     shade2[i]=shade1[i];
    }
 buff2Size=buff1Size;

 clipZSub(0,F(-160),1,
	  buff2,shade2,buff2Size,
	  buff1,shade1,&buff1Size);
 assert(buff1Size<MAXSUBS);

 clipZSub(0,F(160),0,
	  buff1,shade1,buff1Size,
	  buff2,shade2,&buff2Size);
 assert(buff2Size<MAXSUBS);

 clipZSub(1,F(-120),1,
	  buff2,shade2,buff2Size,
	  buff1,shade1,&buff1Size);
 assert(buff1Size<MAXSUBS);

 clipZSub(1,F(120),0,
	  buff1,shade1,buff1Size,
	  buff2,shade2,&buff2Size);
 assert(buff2Size<MAXSUBS);

 for (i=0;i<buff2Size;i++)
    {for (z=0;z<4;z++)
	{poly[z].x=f(buff2[z+i*4].x);
	 poly[z].y=f(buff2[z+i*4].y);
	 assert(poly[z].x>-161);
	 assert(poly[z].y>-121);
	 assert(poly[z].x<161);
	 assert(poly[z].y<121);
	 assert(f(shade2[z+i*4])>=0);
	 assert(f(shade2[z+i*4])<32);
	 gtable.entry[z]=worldGrey[f(shade2[z+i*4])];
	}
     EZ_polygon(DRAW_MESH|ECD_DISABLE|SPD_DISABLE,
		field?RGB(15,10,0):WATERCOLOR,poly,&gtable);
    }
}
#endif

#if 0
void drawPlax(sWallType *theWall,MthXyz *coords)
{int i,z;
 int buff1Size,buff2Size;
 MthXyz buff1[MAXSUBS*4];
 MthXyz buff2[MAXSUBS*4];
 Fixed32 shade1[MAXSUBS*4];
 Fixed32 shade2[MAXSUBS*4];
 XyInt poly[4];

 checkStack();

 clipZSub(2,F(5),1,
	  coords,shade2,1,
	  buff1,shade1,&buff1Size);

 for (i=0;i<buff1Size*4;i++)
    {buff2[i].x=F(PROJECT(buff1[i].x,buff1[i].z));
     buff2[i].y=-F(PROJECT(buff1[i].y,buff1[i].z));
     shade2[i]=shade1[i];
    }
 buff2Size=buff1Size;

 clipZSub(0,F(-160),1,
	  buff2,shade2,buff2Size,
	  buff1,shade1,&buff1Size);
 assert(buff1Size<MAXSUBS);

 clipZSub(0,F(160),0,
	  buff1,shade1,buff1Size,
	  buff2,shade2,&buff2Size);
 assert(buff2Size<MAXSUBS);

 clipZSub(1,F(-120),1,
	  buff2,shade2,buff2Size,
	  buff1,shade1,&buff1Size);
 assert(buff1Size<MAXSUBS);

 clipZSub(1,F(120),0,
	  buff1,shade1,buff1Size,
	  buff2,shade2,&buff2Size);
 assert(buff2Size<MAXSUBS);

 for (i=0;i<buff2Size;i++)
    {for (z=0;z<4;z++)
	{poly[z].x=f(buff2[z+i*4].x);
	 poly[z].y=f(buff2[z+i*4].y);
	 assert(poly[z].x>-161);
	 assert(poly[z].y>-121);
	 assert(poly[z].x<161);
	 assert(poly[z].y<121);
	}
     EZ_polygon(ECD_DISABLE|SPD_DISABLE,0x0000,poly,NULL);
    }
}
#endif

#define MAXVPERWALL 700
struct vCalc
{short x,y;
 unsigned short light;
 /* char clip;  clipped if (light&0x8000) */
};

static char pattern[][4]=
    {
     {0,1,2,3},
     {1,2,3,0},
     {2,3,0,1},
     {3,0,1,2},
     {0,3,2,1},
     {1,0,3,2},
     {2,1,0,3},
     {3,2,1,0}
    };

void EZ_specialDistSpr2(short charNm,XyInt *xy,struct gourTable *gTable);

#if MIPMAP
/* GCC14: screen-space midpoint of two projected grid corners, for the non-uniform
   mip-block fallback (RGB555 average keeps the channels separate; bit 15 is 0 on
   every corner beyond MIPDIST) */
#define MIPMID(d,a,b)  (d).x=(short)((((int)(a).x)+(b).x)>>1);  (d).y=(short)((((int)(a).y)+(b).y)>>1);  (d).light=(unsigned short)(((((a).light)&0x7bde)+(((b).light)&0x7bde))>>1)
#endif


/* GCC14: a cell that leaves the view.  The VDP1 lays a tile over a cell's four corners affinely:
   no perspective, and no window in U (WALLASM.H).  A corner off the view left at its true
   projection s_n shows the part of the tile still on screen z_e/z_n times too big -- z_e the
   depth where the cell's edge leaves the view, z_n the corner's.  Walking along the surface, that
   zoom grows (1 + z_f/z_n) times faster than it should (z_f the corner still in view): two depths
   shrink in it where one shrinks in the true zoom, z_e staying put -- the texture that "grows
   twice too fast" of the console report, never less than twice.  On the Doom floor the bottom of
   the view is 80 = FOCALDIST/2 below the centre, so it meets the floor at twice the eye's height,
   z = 82: a tile whose near edge is at the eye's depth, 41, shows x2.00, at 20 x4.1.
   The corner that makes the affine map EXACT at the corner in view (A) and where the edge leaves
   the view (e) is
      s* = s_A + (s_n - s_A) z_n/z_e,
   the projection of the stand-in  Q = N + A (z_e - z_n)/z_A,  at depth z_e -- which exists for a
   corner BEHIND the eye too (z_n <= 0), where s_n does not.  s* lies on the edge's own line, past
   e: the part of the edge in view does not move.
   s* is not taken as is.  On a wall it stands like an edge at depth z_e/(1 + k), k =
   (z_e - z_n)/z_A: for a corner behind the eye k passes 26 with a 256 cell, s* passes a short
   (project_point stores them) and, first, trips vdp1Fit, whose V window keeps the INTERSECTION
   of both V edges' intervals: a fitted edge that tall cuts the edge IN VIEW too, and a hole
   opens (simulated, hugging a wall of 128 cells: 250 px a frame against 100 before; of 256
   cells: 1450 against 150).  The corner goes to  s = s_e + lam (s* - s_e),  s_e the projection
   of e -- the old repair's place for a corner behind the plane -- with lam as large as keeps the
   cut out of the window: s stands at depth z_T = z_e/(1 + lam k), and the cut stays past the
   window's top and bottom while z_T >= rho z_A, rho = max(-ymin,ymax)/VDP1LIM:
      lam = (z_e - rho z_A) / (rho z_A k),  clamped to [0,1].
   No height in it: the same on every row of a wall, whose V edges stay vertical, and continuous,
   across the near plane too.  lam < 1 only on cells long against the distance, z_A (1 + k) >
   z_e/rho: there the tile shows squeezed rather than cut, as the old repair showed it; lam 0
   (z_e < rho z_A) is a cut no place avoids, s_e gives the smallest.  Last, a corner is held
   within FITLIMW of the middle, along the same line, for the shorts.
   Only a corner NEARER than its anchor is fitted: a further one shows its tile squeezed, not
   stretched, and pulling it out would make the VDP1 walk further off the screen.  Returns 1 and
   s, 0 when N is in view after all (the window's rounding), -1 for nothing to fit.  A may be off
   the view too (a cell wider than it): e is where the edge leaves the view towards N.
   Where a row leaves the view: the view's width, a wedge F.X >= xmin.Z and xmax.Z >= F.X, and
   the near plane -- a row that passes the eye inside the wedge, a lintel walked under, a riser
   stood over, leaves through it.  In 1/256 unit, so a point 8000 away still fits in 32 bits.
   The wedge is the window's: split screen (focal 126, 80 each side) is right too, where
   |x| = z was not. */
#define FITLIMW 16000   /* a fitted corner's |x|,|y|: a short, and room for vdp1Slide's <<12 */

static int viewSide(int side,const MthXyz *p)
{return side==0? focalDist*(p->x>>8)-viewXmin*(p->z>>8):
	side==1? viewXmax*(p->z>>8)-focalDist*(p->x>>8):
		 (p->z-NEARCLIP)>>8;
}

/* project_point in 32 bits (same divide, same rounding): a stand-in may pass a short */
static void projectWide(const MthXyz *p,int *sx,int *sy)
{Fixed32 q=MTH_Div(F(focalDist),p->z);
 *sx=(int)(((long long)p->x*q)>>32);
 *sy=-(int)(((long long)p->y*q)>>32);
}

/* lam no larger than keeps e + lam (q - e) within FITLIMW of the middle */
static Fixed32 capWide(Fixed32 lam,int e,int q)
{Fixed32 c;
 if (q>FITLIMW || q<-FITLIMW)
    {c=(q>0? FITLIMW: -FITLIMW)-e;
     c=((c<=0 && q>0) || (c>=0 && q<0))? 0: MTH_Div(c,q-e);
     if (c<lam)
	lam=c;
    }
 return lam;
}

static int fitCorner(const MthXyz *A,const MthXyz *N,XyInt *s)
{Fixed32 t0=0,t1=F(1),t,ze,k,lam,rz,num,den;
 int p,ga,gn,ex,ey,qx,qy;
 MthXyz E,Q;

 if (N->z>=A->z)
    return -1;
 for (p=0;p<3;p++)
    {ga=viewSide(p,A);
     gn=viewSide(p,N);
     if (ga<0 && gn<0)
	return -1;                      /* the edge is past this side end to end */
     if ((ga<0)!=(gn<0))
	{t=MTH_Div(ga,ga-gn);          /* where it crosses this side, 0..1 from A */
	 if (gn<0)
	    {if (t<t1) t1=t;}
	 else if (t>t0)
	    t0=t;
	}
    }
 if (t1>=F(1))
    return 0;
 if (t0>=t1)
    return -1;                          /* it passes by the view */
 ze=A->z+MTH_Mul(N->z-A->z,t1);
 if (ze<NEARCLIP)
    ze=NEARCLIP;                        /* the near plane itself, but for rounding */
 E.x=A->x+MTH_Mul(N->x-A->x,t1);
 E.y=A->y+MTH_Mul(N->y-A->y,t1);
 E.z=ze;
 k=MTH_Div(ze-N->z,A->z);
 Q.x=N->x+MTH_Mul(A->x,k);
 Q.y=N->y+MTH_Mul(A->y,k);
 Q.z=ze;
 projectWide(&E,&ex,&ey);
 projectWide(&Q,&qx,&qy);
 p=(viewYmax>-viewYmin)? viewYmax: -viewYmin;
 /* GCC14: rho exists to keep the OLD cut out of the window, so it follows that bound -- and under
    the exact cut (vdp1VCut 0) there is nothing to keep out: the cut is at the window by
    construction.  VDP1LIM there leaves fitCorner exactly as the engine shipped it. */
 rz=(A->z/(vdp1VCut? vdp1VCut: VDP1LIM))*p;
 num=ze-rz;
 den=MTH_Mul(rz,k);
 lam=(num<=0)? 0: (num>=den)? F(1): MTH_Div(num,den);
 lam=capWide(lam,ex,qx);
 lam=capWide(lam,ey,qy);
 s->x=ex+MTH_Mul(qx-ex,lam);
 s->y=ey+MTH_Mul(qy-ey,lam);
 return 1;
}

/* GCC14: a grid row is a world line, and on a wall its U runs along the screen's x (the V edges
   are vertical there): the view's width and the near plane decide where it leaves the view, not
   its top and bottom.  What of a row is in view is one run of points; the cell past each end of
   the run is fitted on its end point, and every point further out takes the fitted corner's
   place -- off the view, and never a point behind the eye with a projection the clamp invented
   for it (rectTransform cannot clip).  First the points behind the plane go onto their neighbour
   towards the row's far end: a place off the view for whatever the fit leaves, a row with
   nothing in view. */
#define ROWIN(v) (!((v).light&0x8000) && (v).x>=viewXmin && (v).x<=viewXmax)

static void rowPoint(const MthXyz *p0,const MthXyz *vW,int i,MthXyz *p)
{p->x=p0->x+vW->x*i;
 p->y=p0->y+vW->y*i;
 p->z=p0->z+vW->z*i;
}

/* the points of the row past its point a, going dir; 1 if a cell was fitted */
static int fitRowEnd(struct vCalc *row,int nmv,int a,int dir,MthXyz *p0,MthXyz *vW)
{int n,r;
 MthXyz A,N;
 XyInt s;

 for (;;)
    {n=a+dir;
     if (n<0 || n>=nmv)
	return 0;
     rowPoint(p0,vW,a,&A);
     rowPoint(p0,vW,n,&N);
     r=fitCorner(&A,&N,&s);
     if (r<0)
	return 0;
     if (r>0)
	break;
     a=n;                               /* in view, only rounded out of the window */
    }
 for (;n>=0 && n<nmv;n+=dir)
    {row[n].x=s.x;
     row[n].y=s.y;
    }
 return 1;
}

static void fitRow(struct vCalc *row,int nmv,MthXyz *p0,MthXyz *vW)
{int a,b,d;

 d=(vW->z>0)? -1: 1;                    /* from the row's far end to its near end */
 for (a=(vW->z>0)? nmv-2: 1;a>=0 && a<nmv;a+=d)
    if (row[a].light & 0x8000)
       {row[a].x=row[a-d].x;
	row[a].y=row[a-d].y;
       }
 for (a=0;a<nmv && !ROWIN(row[a]);a++)
    ;
 if (a<nmv)
    {for (b=nmv-1;!ROWIN(row[b]);b--)
	;
     fitRowEnd(row,nmv,a,-1,p0,vW);
     fitRowEnd(row,nmv,b,1,p0,vW);
     return;
    }
 /* nothing of the row in the window: one cell wider than the view -- its near corner fitted on
    its far one --, or nothing to show */
 for (a=0;a+1<nmv;a++)
    if (fitRowEnd(row,nmv,(vW->z>0)? a+1: a,d,p0,vW))
       return;
}

/* GCC14: a grid leaves the view's width, or crosses the near plane, iff one of its four corners
   does: a wall is flat and the view's width a wedge.  Four tests on the per-wall path, where the
   near plane alone took four compares. */
static int gridLeavesView(struct vCalc *vc,int width,int height)
{int last=height*(width+1);
 return !ROWIN(vc[0]) || !ROWIN(vc[width]) || !ROWIN(vc[last]) || !ROWIN(vc[last+width]);
}

#define FITGRID(vc)							\
 if (gridLeavesView((vc),width,height))					\
    {MthXyz p0=coords[0];						\
     int rh;								\
     for (rh=0;rh<=height;rh++)						\
	{fitRow((vc)+rh*(width+1),width+1,&p0,&vWidth);			\
	 p0.x+=vHeight.x; p0.y+=vHeight.y; p0.z+=vHeight.z;		\
	}								\
    }

/* --- GCC14: light LOD ------------------------------------------------------------------------
   A wall is split into a grid only to correct its texture perspective.  Once the fog has driven
   EVERY vertex light to zero there is nothing left to correct (gouraud -16 = black), so the wall
   becomes ONE quad: 4 projected vertices instead of (h+1)(w+1), one VDP1 command instead of h*w,
   no tile.  The test is exact, not a setting: the asm computes light = level_vertexLight[] -
   fogTable[z>>24] clamped at 0, which is zero everywhere iff the fog at the NEAREST corner
   already reaches the wall's STRONGEST static light.  No frame-time governor.
   A dynamic light vetoes it (nmWallLights is per wall): detail comes back where the light falls.
   Without fog (4096) it almost never fires. */
int lodEnable=1;         /* 0 = no fusion, 1 = fusion, 2 = fusion painted blue */
int lodFused,lodCells,lodFlat; /* fused walls, cells saved, cells flattened */
static int slave_lodFused,slave_lodCells,slave_lodFlat;
extern unsigned char fogTable[256];

/* GCC14: flat cells -- what fusion cannot take.  A wall fading into the fog reaches the fog's
   own colour far and stays lit near, never fusable whole, but its far cells reach it one by one.
   Exact and free: the asm stores the final gouraud word in vCalc[].light (worldGrey[light], bit 15
   flipped in front of the near plane, .Lrt_retFromLit), so a cell whose four corners all sit at
   worldGrey[0] has one flat colour and nothing else to show.  WHICH colour does not matter to
   the test: `fogFloorGour` is the floor of the ramp as the asm stores it (UTIL.C setFogColour),
   and it is 0 -- the original test, an OR against zero -- while the fog is black.
   Kept: the VDP1 command, painted in `fogFar`.  Dropped: the tile (mapPic, a cache slot), the
   gouraud table, the texture mapping. */
#define CELLATFOG(a,b,c,d) (!(((a)^fogFloorGour)|((b)^fogFloorGour)| \
					      ((c)^fogFloorGour)|((d)^fogFloorGour)))
#define CELLISBLACK(g) (lodEnable && \
			CELLATFOG((g).entry[0],(g).entry[1],(g).entry[2],(g).entry[3]))

/* Same test on a GRID cell, before the loop permutes the corners for the texture: the four
   vertices are at grid indices, not in pattern order. */
#define LODCELLDARK(V) (lodEnable && \
			CELLATFOG((V)[row1+w].light,(V)[row1+w+1].light, \
				  (V)[row2+w].light,(V)[row2+w+1].light))

/* GCC14: weld a run of black cells in a grid row.  Exact: a grid row is a world LINE (a
   parallelogram row along the width vector) and perspective maps lines to lines, so the
   projected vertices are collinear and the welded quad passes through the skipped ones -- no
   gap, no overlap.  The only vertex off that line, one rectTransform clamped to the near plane,
   carries the clip bit, so its light is not zero and it ends the run.
   w is ONE past the last black cell: the quad's right edge. */
#define LODRUNQUAD(V) \
   q[0].x=(V)[row1+runStart].x; q[0].y=(V)[row1+runStart].y; \
   q[1].x=(V)[row1+w].x;        q[1].y=(V)[row1+w].y; \
   q[2].x=(V)[row2+w].x;        q[2].y=(V)[row2+w].y; \
   q[3].x=(V)[row2+runStart].x; q[3].y=(V)[row2+runStart].y

/* --- GCC14: weld mesh strips -----------------------------------------------------------------
   That is where the image is: on E1M1, 437 mesh walls carry 3019 faces against 851 cells for
   565 grid walls, and mesh is 87-90 % of the cells on screen (floors and ceilings).
   Consecutive mesh faces form a STRIP 46 % of the time: (v3,v2) of one is (v0,v1) of the next
   (819 joins) or the reverse (585), so a run of black faces in a strip folds into one quad.
   Collinearity is NOT given here, so it is MEASURED on screen on the final quad: every skipped
   vertex must lie within a pixel of the replacing edge, or the whole strip is dropped and its
   faces go out one by one.  No holes -- checked, not assumed. */
static int faceIsBlack(int f,struct vCalc *V)
{return !(V[level_face[f].v[0]].light|V[level_face[f].v[1]].light|
	  V[level_face[f].v[2]].light|V[level_face[f].v[3]].light);
}

/* |cross product| <= max(|dx|,|dy|): within about 1.5 px of line (a,b), no sqrt, no divide. */
static int nearSegment(XyInt *a,XyInt *b,int px,int py)
{int dx=b->x-a->x,dy=b->y-a->y,m,n;
 int cr=dx*(py-a->y)-dy*(px-a->x);
 if (cr<0) cr=-cr;
 m=(dx<0)?-dx:dx;
 n=(dy<0)?-dy:dy;
 if (n>m) m=n;
 return cr<=m;
}

/* Extends a strip of black faces from f -- or, tile >= 0, of faces of that tile (far LOD) --, fills
   q[] with the welded quad and gq with its corners' gouraud, returns the strip's last index -- f
   itself if nothing welds. */
#define WELDS(n) (tile<0? faceIsBlack(n,V): level_face[n].tile==tile)
#define LODVXYG(Q,L,V,I) {(Q).x=(V)[I].x; (Q).y=(V)[I].y; (L)=(V)[I].light;}
static int weldFaceStrip(sWallType *wall,int f,struct vCalc *V,XyInt *q,struct gourTable *gq,
			 int tile)
{int g,dir,j;
 unsigned short *a,*c;
 if (f>=wall->lastFace)
    {g=f; dir=0;}
 else
    {a=level_face[f].v; c=level_face[f+1].v;
     if (a[3]==c[0] && a[2]==c[1])
	dir=1;                     /* strip runs v0 -> v3 */
     else if (a[0]==c[1] && a[3]==c[2])
	dir=2;                     /* ... the other way */
     else
	dir=0;
     g=f;
     while (dir && g<wall->lastFace)
	{a=level_face[g].v; c=level_face[g+1].v;
	 if (!WELDS(g+1))
	    break;
	 if (dir==1 ? !(a[3]==c[0] && a[2]==c[1])
		    : !(a[0]==c[1] && a[3]==c[2]))
	    break;
	 g++;
	}
    }
 if (g==f)
    dir=0;
 if (dir==1)
    {LODVXYG(q[0],gq->entry[0],V,level_face[f].v[0]);
     LODVXYG(q[1],gq->entry[1],V,level_face[f].v[1]);
     LODVXYG(q[2],gq->entry[2],V,level_face[g].v[2]);
     LODVXYG(q[3],gq->entry[3],V,level_face[g].v[3]);
     for (j=f;j<g;j++)   /* the skipped vertices are the shared edges */
	if (!nearSegment(q+0,q+3,V[level_face[j].v[3]].x,V[level_face[j].v[3]].y) ||
	    !nearSegment(q+1,q+2,V[level_face[j].v[2]].x,V[level_face[j].v[2]].y))
	   {dir=0; break;}
    }
 else
    if (dir==2)
       {LODVXYG(q[0],gq->entry[0],V,level_face[g].v[0]);
	LODVXYG(q[1],gq->entry[1],V,level_face[f].v[1]);
	LODVXYG(q[2],gq->entry[2],V,level_face[f].v[2]);
	LODVXYG(q[3],gq->entry[3],V,level_face[g].v[3]);
	for (j=f;j<g;j++)
	   if (!nearSegment(q+0,q+1,V[level_face[j].v[0]].x,V[level_face[j].v[0]].y) ||
	       !nearSegment(q+3,q+2,V[level_face[j].v[3]].x,V[level_face[j].v[3]].y))
	      {dir=0; break;}
       }
 if (!dir)
    {LODVXYG(q[0],gq->entry[0],V,level_face[f].v[0]);
     LODVXYG(q[1],gq->entry[1],V,level_face[f].v[1]);
     LODVXYG(q[2],gq->entry[2],V,level_face[f].v[2]);
     LODVXYG(q[3],gq->entry[3],V,level_face[f].v[3]);
     return f;
    }
 return g;
}

static int wallIsBlack(sWallType *w,MthXyz *coords,int nmLit,int wavy)
{int i,fog,n,base,maxl;
 if (!lodEnable || nmLit || wavy)
    return 0;
 fog=coords[0].z;              /* NEAREST corner, so the weakest fog */
 for (i=1;i<4;i++)
    if (coords[i].z<fog)
       fog=coords[i].z;
 if (fog<=0)
    return 0;
 fog>>=24;                     /* fogTable indexes 256-unit steps, like the asm */
 if (fog>255)
    fog=255;
 fog=fogTable[fog];
 if (fog<1)
    return 0;   /* no fog here: leave before scanning the near walls' lights */
 if (fog<31)    /* beyond this no static light survives: no need to read them */
    {base=w->firstLight;
     n=(w->tileHeight+1)*(w->tileLength+1);
     maxl=0;
     for (i=0;i<n;i++)
	if (level_vertexLight[base+i]>maxl)
	   maxl=level_vertexLight[base+i];
     if (maxl>fog)
	return 0;
    }
 return 1;
}

/* The wall's four corners, projected as rectTransform would (same divide by z, same y
   negation: wallasm_gnu.s _project_point, .Lrt_retFromLit).  Returns 0 if the quad is not
   visible in the sector. */
static int fuseWallPoly(MthXyz *coords,SectorDrawRecord *s,XyInt *q)
{int j;
 for (j=0;j<4;j++)
    project_point(coords+j,q+j);
 return clip_visible(q,s);
}

/* GCC14: far LOD.  A wall wholly beyond lodFar units (0 = off; SRUINS.C sets it per view) folds
   like a black one, but painted: its first tile's first texel (PIC.C picFirstColour) under its
   corners' own light.  A mesh wall welds its strips of one tile the same way.  Same vetoes as
   the light LOD: dynamic lights, water. */
int lodFar;
static int wallIsFar(MthXyz *coords,int nmLit,int wavy)
{int i;
 if (!lodEnable || !lodFar || nmLit || wavy)
    return 0;
 for (i=0;i<4;i++)
    if (coords[i].z<(lodFar<<16))
       return 0;
 return 1;
}

/* the gouraud of a folded wall's corners, grid vertices 0, W, H(W+1)+W and H(W+1), lit as the asm
   lights them: static light less the fog, clamped */
static void farCorners(sWallType *w,MthXyz *coords,struct gourTable *g)
{int c,l,z,W=w->tileLength,H=w->tileHeight,idx[4];
 idx[0]=0;
 idx[1]=W;
 idx[2]=H*(W+1)+W;
 idx[3]=H*(W+1);
 for (c=0;c<4;c++)
    {z=coords[c].z>>24;
     if (z>255)
	z=255;
     l=level_vertexLight[w->firstLight+idx[c]]-fogTable[z];
     if (l<0)
	l=0;
     if (l>32)
	l=32;
     g->entry[c]=worldGrey[l];
    }
}
#define LODFARTILE(p) (-16-(p))     /* a slave record painted flat, of pic p (drawSlaveWalls) */
/* LOD PAINTED gives each stage its own colour, to see at a glance which path a surface took:
      GREEN  a whole wall folded into one quad
         BLUE   a weld of black cells in a wall grid
         RED    a mesh face -- floor, ceiling or curved wall
         YELLOW the far LOD, folded wall or welded faces */
/* GCC14: a wall, or a run of cells, the fog has taken whole is painted in the fog's own far
   colour (UTIL.C fogFar) -- RGB(0,0,0), the original, while the fog is black. */
#define LODCOL_FUSE ((lodEnable>1)? RGB(0,24,0): fogFar)
#define LODCOL_RECT ((lodEnable>1)? RGB(0,0,24): fogFar)
#define LODCOL_MESH ((lodEnable>1)? RGB(24,0,0): fogFar)
#define LODCOL_FAR(p) ((lodEnable>1)? RGB(24,24,0): picFirstColour(p))   /* YELLOW: the far LOD */

/* GCC14: a mesh face -- a floor, a ceiling -- that leaves the view, fitted by fitCorner's law.  A
   face has no row: a corner off the view is fitted along its edge to its ONE neighbour in view,
   when its other neighbour is off the view too -- the edge between those two may move, the edge
   in view keeps its line.  In screen space, on the true projections: 1/z runs linearly along a
   projected edge, so with sg the part of edge A->N in view and r = z_n/z_A,
      z_n/z_e = r + sg (1 - r).
   vCalc holds no z: two dot products.
   CONTINUOUS, or it pops.  A corner is fitted or not by what its neighbours do, and it moves the
   texture of the whole face, the part in view included (the VDP1 draws lines from edge A-D to
   edge B-C): a fit that switches on or off between two images makes the floor jump -- simulated,
   13 to 20 texels of a 64 tile in 0.1 degree of turning, and flickering on the window's rounding.
   So the fit (1 - k of the way) eases to nothing at every switch, over FITFADE px or 8 units:
     - its neighbour in view reaching the window's edge (then there is none),
     - its neighbour off the view reaching the window (then both are in view),
     - the corner or its neighbour in view reaching the near plane (bit 15, below);
   and it is k = 1 already when the corner reaches the window or its neighbour's depth.
   Then each moved edge -- between a moved corner and its neighbour off the view, moved or not --
   must stay off the window, tested exactly (both past one side, or the window's four corners on
   one side of its line): if the whole fit does not keep it off, the largest share of it that does
   is kept, 1/32 near -- 0 when the edge came across the window already.  No face falls back.
   Left as they came: a corner behind the plane (bit 15), whose projection is the plane's -- in
   the face where it has two neighbours in view it could not follow, and the two faces would
   part; a corner with both neighbours in view, which cannot move without moving an edge in view;
   a corner further than its neighbour.
   On the floor the player stands on, the bottom of the view meets it at 82 and a tile is 64 deep:
   a corner next to one in view is 18 away at least, never behind the plane. */
typedef struct {sWallType *wall; MthMatrix *view; int f;} MeshFace;   /* NULL: a grid cell */
#define OUTCODE(p) (((p).x<viewXmin)|(((p).x>viewXmax)<<1)|		\
		    (((p).y<viewYmin)<<2)|(((p).y>viewYmax)<<3))
#define FITFADE 4       /* the fit eases out over 1<<FITFADE px */

static Fixed32 meshZ(const MeshFace *m,int v)
{MthXyz w;
 getVertex(m->wall->firstVertex+v,&w);
 return MTH_Product(m->view->val[2],(Fixed32 *)&w)+m->view->val[2][3];
}

/* how far p lies inside the window (< 0: outside), and outside it (< 0: inside), in px */
static int winIn(const XyInt *p)
{int d=p->x-viewXmin,e;
 e=viewXmax-p->x; if (e<d) d=e;
 e=p->y-viewYmin; if (e<d) d=e;
 e=viewYmax-p->y; if (e<d) d=e;
 return d;
}

static int winOut(const XyInt *p)
{int d=viewXmin-p->x,e;
 e=p->x-viewXmax; if (e>d) d=e;
 e=viewYmin-p->y; if (e>d) d=e;
 e=p->y-viewYmax; if (e>d) d=e;
 return d;
}

/* segment p-q misses the window: both past one side, or the window's corners all on one side of
   its line (with no side in common, what of the line crosses the window lies between p and q) */
static int winMissed(const XyInt *p,const XyInt *q)
{int i,cx,cy,s,sg=0;
 long long cr;
 if (OUTCODE(*p)&OUTCODE(*q))
    return 1;
 for (i=0;i<4;i++)
    {cx=(i==1 || i==2)? viewXmax: viewXmin;
     cy=(i>=2)? viewYmax: viewYmin;
     cr=(long long)(q->x-p->x)*(cy-p->y)-(long long)(q->y-p->y)*(cx-p->x);
     s=(cr>0)-(cr<0);
     if (!s || (sg && s!=sg))
	return 0;
     sg=s;
    }
 return 1;
}

/* d = a + t (b - a); d may be b */
static void lerpXy(XyInt *d,const XyInt *a,const XyInt *b,Fixed32 t)
{d->x=a->x+MTH_Mul(b->x-a->x,t);
 d->y=a->y+MTH_Mul(b->y-a->y,t);
}

static void __attribute__((noinline)) fitFace(const MeshFace *m,XyInt *poly,struct gourTable *g)
{int i,a,b,n,oc[4],nb[4];
 Fixed32 sg,t,r,k,w,za,zn,lo,hi;
 unsigned short *fv=level_face[m->f].v;
 XyInt q[4],u,v;

 for (i=0;i<4;i++)
    {oc[i]=OUTCODE(poly[i]);
     q[i]=poly[i];
     nb[i]=-1;
    }
 for (i=0;i<4;i++)
    {a=(i+1)&3;
     b=(i+3)&3;
     if (oc[a])
	{a=b;
	 b=(i+1)&3;
	}
     if (!oc[i] || oc[a] || !oc[b] || ((g->entry[i]|g->entry[a])&0x8000))
	continue;
     za=meshZ(m,fv[a]);
     zn=meshZ(m,fv[i]);
     if (zn>=za)
	continue;
     w=F(1);                            /* the ease, 0..1 */
     t=winIn(poly+a)<<(16-FITFADE); if (t<w) w=t;
     t=winOut(poly+b)<<(16-FITFADE); if (t<w) w=t;
     t=(zn-NEARCLIP)>>3; if (t<w) w=t;
     t=(za-NEARCLIP)>>3; if (t<w) w=t;
     if (w<=0)
	continue;
     sg=F(1);                           /* the part of A->N in view: to the first side it crosses */
     if (oc[i]&1)
	{t=MTH_Div(poly[a].x-viewXmin,poly[a].x-poly[i].x); if (t<sg) sg=t;}
     if (oc[i]&2)
	{t=MTH_Div(viewXmax-poly[a].x,poly[i].x-poly[a].x); if (t<sg) sg=t;}
     if (oc[i]&4)
	{t=MTH_Div(poly[a].y-viewYmin,poly[a].y-poly[i].y); if (t<sg) sg=t;}
     if (oc[i]&8)
	{t=MTH_Div(viewYmax-poly[a].y,poly[i].y-poly[a].y); if (t<sg) sg=t;}
     r=MTH_Div(zn,za);
     k=r+MTH_Mul(sg,F(1)-r);
     k=F(1)-MTH_Mul(w,F(1)-k);
     lerpXy(q+i,poly+a,poly+i,k);
     nb[i]=b;
    }
 /* each moved edge once: when both its ends moved, each names the other */
 for (i=0;i<4;i++)
    if ((b=nb[i])>=0 && !(nb[b]==i && b<i) && !winMissed(q+i,q+b))
       {lo=0;
	if (winMissed(poly+i,poly+b))
	   {hi=F(1);
	    for (n=0;n<5;n++)
	       {t=(lo+hi)>>1;
		lerpXy(&u,poly+i,q+i,t);
		lerpXy(&v,poly+b,q+b,t);
		if (winMissed(&u,&v))
		   lo=t;
		else
		   hi=t;
	       }
	   }
	lerpXy(q+i,poly+i,q+i,lo);
	lerpXy(q+b,poly+b,q+b,lo);
       }
 for (i=0;i<4;i++)
    poly[i]=q[i];
}

/* GCC14: a textured wall cell, through the wall tiles' rule (PIC.H): its tile, or a flat quad in
   the tile's first texel's colour when the cache refuses it.  Its size is the smaller side of its
   box: a far floor 2 px tall and 30 long would show 2 rows of its texture.  A mesh face (m) whose
   box leaves the view is fitted to it first (fitFace); its size stays the box it came with. */
static void __attribute__((noinline)) wallCell(int pic,XyInt *poly,struct gourTable *g,
					       const MeshFace *m)
{int i,x0,x1,y0,y1;
 x0=x1=poly[0].x;
 y0=y1=poly[0].y;
 for (i=1;i<4;i++)
    {if (poly[i].x<x0) x0=poly[i].x;
     if (poly[i].x>x1) x1=poly[i].x;
     if (poly[i].y<y0) y0=poly[i].y;
     if (poly[i].y>y1) y1=poly[i].y;
    }
 if (m && (x0<viewXmin || x1>viewXmax || y0<viewYmin || y1>viewYmax))
    fitFace(m,poly,g);
 x1-=x0;
 y1-=y0;
 i=mapWallPic(pic,x1<y1? x1: y1);
 EZ_distSprVClip(i,i<0? picFirstColour(pic): 0,poly,g);
}

void drawRectWall(sWallType *theWall,MthXyz *coords,
		  SectorDrawRecord *s)
{MthXyz vWidth,vHeight;
 XyInt poly[4];
 int w,h,clip;
 int runStart;   /* start of the current run of black cells, -1 = none */
 int light;
 int tex,row1,row2;
 register char *ppattern;
 int width=theWall->tileLength;
 int height=theWall->tileHeight;
 struct gourTable gtable;
 struct vCalc vCalc[MAXVPERWALL];
#if MIPMAP
 int tileBias;
#endif

 checkStack();
 assert(width*height<MAXVPERWALL);

#if MIPMAP
 if (mipEnable && /* GCC14: also drops the original `desiredWeapon &&` gate, a leftover
       test hack that kept mips off while holding weapon 0 (the sword) */
     width>=2 && height>=2 && /* GCC14: never halve a 1-tile dimension (bug 1) */
     coords[0].z>MIPDIST &&
     coords[1].z>MIPDIST &&
     coords[2].z>MIPDIST &&
     coords[3].z>MIPDIST)
    {width>>=1;
     height>>=1;
     tileBias=mipBase;
    }
 else
    tileBias=0;
#endif

 buildLightList(theWall,s-sectorDraw);

 if (wallIsBlack(theWall,coords,nmWallLights,wavyIndex))
    {XyInt q[4];
     if (fuseWallPoly(coords,s,q) && vdp1Range(q))
	{VDP1WALK(q);
	 EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,LODCOL_FUSE,q,NULL);
	 lodFused++;
	 lodCells+=theWall->tileHeight*theWall->tileLength-1;
	}
     return;
    }
 if (wallIsFar(coords,nmWallLights,wavyIndex))
    {XyInt q[4];
     struct gourTable g;
     if (fuseWallPoly(coords,s,q) && vdp1Range(q))
	{farCorners(theWall,coords,&g);
	 VDP1WALK(q);
	 EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5|DRAW_GOURAU,
		    LODCOL_FAR(level_texture[theWall->textures+1]),q,&g);
	 lodFused++;
	 lodCells+=theWall->tileHeight*theWall->tileLength-1;
	}
     return;
    }

 Set_Hardware_Divide(coords[1].x-coords[0].x,width);
 vWidth.y=(coords[1].y-coords[0].y)/width;
 vWidth.x=Get_Hardware_Divide();

 Set_Hardware_Divide(coords[1].z-coords[0].z,width);
 vHeight.x=(coords[2].x-coords[1].x)/height;
 vWidth.z=Get_Hardware_Divide();

 Set_Hardware_Divide(coords[2].y-coords[1].y,height);
 vHeight.z=(coords[2].z-coords[1].z)/height;
 vHeight.y=Get_Hardware_Divide();

 light=theWall->firstLight;

 rectTransform(vWidth.x,vWidth.y,vWidth.z,
	       light,height+1,width+1,
	       coords[0].x,coords[0].y,coords[0].z,
	       vHeight.x,vHeight.y,vHeight.z,
	       vCalc,
	       (nmWallLights||wavyIndex)?&getLight:NULL,
#if MIPMAP
	       tileBias?1:0,tileBias?2*(theWall->tileLength-width):0,
#else
	       0,0,
#endif
	       NEARCLIP);

 FITGRID(vCalc);

 tex=theWall->textures;
 row1=0;
 row2=width+1;
 pushProfile("2nd Half");
 for (h=0;h<height;h++)
    {runStart=-1;
     for (w=0;w<=width;w++)
	{if (w<width && LODCELLDARK(vCalc))
	    {if (runStart<0)
		runStart=w;
	     tex+=2;
	     continue;
	    }
	 if (runStart>=0)
	    {XyInt q[4];
	     LODRUNQUAD(vCalc);
	     if (clip_visible(q,s) && vdp1Range(q))
		{VDP1WALK(q);
		 EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,LODCOL_RECT,q,NULL);
		 nmPolys++;
		 lodFlat++;
		}
	     lodCells+=w-runStart-1;
	     runStart=-1;
	    }
	 if (w==width)
	    break;
	 clip=0x8000;
#if MIPMAP
	 if (tileBias)
	    {/* GCC14: a mip cell is ONE sprite whose pic replicates ONE half-res tile 2x2
		over a 2x2 world-tile block.  A block mixing (pattern,tile) pairs cannot be
		mipped: fall back to its 4 full-res tiles, with the missing grid points
		averaged in screen space.  Uniform blocks sample the (2h,2w) pair. */
	     int fullTL=theWall->tileLength;
	     int rr,cc,p00,p01,p10,p11;
	     rr=h*2; cc=w*2;
	     p00=theWall->textures+(rr*fullTL+cc)*2;
	     p01=(cc+1<fullTL)? p00+2: p00;
	     p10=(rr+1<theWall->tileHeight)? p00+fullTL*2: p00;
	     p11=p10+(p01-p00);
	     tex=p00;
	     if (level_texture[p00]!=level_texture[p01] ||
		 level_texture[p00]!=level_texture[p10] ||
		 level_texture[p00]!=level_texture[p11] ||
		 level_texture[p00+1]!=level_texture[p01+1] ||
		 level_texture[p00+1]!=level_texture[p10+1] ||
		 level_texture[p00+1]!=level_texture[p11+1])
		{struct vCalc pts[3][3];
		 int dh,dw;
		 int pr[2][2];
		 pr[0][0]=p00; pr[0][1]=p01; pr[1][0]=p10; pr[1][1]=p11;
		 pts[0][0]=vCalc[row1+w];
		 pts[0][2]=vCalc[row1+w+1];
		 pts[2][2]=vCalc[row2+w+1];
		 pts[2][0]=vCalc[row2+w];
		 clip&=pts[0][0].light&pts[0][2].light&
		       pts[2][2].light&pts[2][0].light;
		 poly[0].x=pts[0][0].x; poly[0].y=pts[0][0].y;
		 poly[1].x=pts[0][2].x; poly[1].y=pts[0][2].y;
		 poly[2].x=pts[2][2].x; poly[2].y=pts[2][2].y;
		 poly[3].x=pts[2][0].x; poly[3].y=pts[2][0].y;
		 if (clip || !clip_visible(probeVDP1(poly),s))
		    continue;
		 MIPMID(pts[0][1],pts[0][0],pts[0][2]);
		 MIPMID(pts[1][0],pts[0][0],pts[2][0]);
		 MIPMID(pts[1][2],pts[0][2],pts[2][2]);
		 MIPMID(pts[2][1],pts[2][0],pts[2][2]);
		 MIPMID(pts[1][1],pts[1][0],pts[1][2]);
		 for (dh=0;dh<2;dh++)
		    for (dw=0;dw<2;dw++)
		       {int t=pr[dh][dw];
			ppattern=pattern[(int)level_texture[t]];
			gtable.entry[(int)*ppattern]=pts[dh][dw].light;
			poly[(int)*ppattern].x=pts[dh][dw].x;
			poly[(int)*ppattern].y=pts[dh][dw].y;
			ppattern++;
			gtable.entry[(int)*ppattern]=pts[dh][dw+1].light;
			poly[(int)*ppattern].x=pts[dh][dw+1].x;
			poly[(int)*ppattern].y=pts[dh][dw+1].y;
			ppattern++;
			gtable.entry[(int)*ppattern]=pts[dh+1][dw+1].light;
			poly[(int)*ppattern].x=pts[dh+1][dw+1].x;
			poly[(int)*ppattern].y=pts[dh+1][dw+1].y;
			ppattern++;
			gtable.entry[(int)*ppattern]=pts[dh+1][dw].light;
			poly[(int)*ppattern].x=pts[dh+1][dw].x;
			poly[(int)*ppattern].y=pts[dh+1][dw].y;
			wallCell(level_texture[t+1],poly,&gtable,NULL);
			nmPolys++;
		       }
		 continue;
		}
	    }
#endif
	 assert(level_texture[tex]<8);
	 ppattern=pattern[(int)level_texture[tex++]];
	 gtable.entry[(int)*ppattern]=vCalc[row1+w].light;
	 clip&=vCalc[row1+w].light;
	 poly[(int)*ppattern].x=vCalc[row1+w].x;
	 poly[(int)*ppattern].y=vCalc[row1+w].y;
	 ppattern++;

	 gtable.entry[(int)*ppattern]=vCalc[row1+w+1].light;
	 clip&=vCalc[row1+w+1].light;
	 poly[(int)*ppattern].x=vCalc[row1+w+1].x;
	 poly[(int)*ppattern].y=vCalc[row1+w+1].y;
	 ppattern++;

	 gtable.entry[(int)*ppattern]=vCalc[row2+w+1].light;
	 clip&=vCalc[row2+w+1].light;
	 poly[(int)*ppattern].x=vCalc[row2+w+1].x;
	 poly[(int)*ppattern].y=vCalc[row2+w+1].y;
	 ppattern++;

	 gtable.entry[(int)*ppattern]=vCalc[row2+w].light;
	 clip&=vCalc[row2+w].light;
	 poly[(int)*ppattern].x=vCalc[row2+w].x;
	 poly[(int)*ppattern].y=vCalc[row2+w].y;

	 if (clip || !clip_visible(probeVDP1(poly),s))
	    {tex++;
	     continue;
	    }

	 assert(getPicClass(level_texture[tex])==TILE16BPP);
#if 0
	 EZ_distSpr(DIR_NOREV,
		    UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|
		    DRAW_GOURAU,
		    0,mapPic(level_texture[tex]),poly,&gtable);
#endif
	 wallCell(level_texture[tex]
#if MIPMAP
		  +tileBias
#endif
		  ,poly,&gtable,NULL);
	 nmPolys++;
	 tex++;
	}
     row1+=width+1;
     row2+=width+1;
    }
 popProfile();
}


void drawWall(sWallType *wall,MthMatrix *view,MthXyz *coords,SectorDrawRecord *s)
{int f,i,v,clip,far,black;
 XyInt poly[4];
 struct gourTable gtable;
 struct vCalc vCalc[MAXVPERWALL];
 MeshFace mf;
#ifndef NDEBUG
 int maxV;
#endif

 checkStack();
#ifdef LIGHT
 buildLightList(wall,s-sectorDraw);
#endif

 assert(wall->lastVertex-wall->firstVertex<MAXVPERWALL);
 v=0;
 assert(wall->lastVertex-wall->firstVertex<MAXVPERWALL);
 normTransform(level_vertex+wall->firstVertex,view,
	       wall->lastVertex-wall->firstVertex+1,vCalc,
	       (nmWallLights||wavyIndex)?&getLight:NULL,NEARCLIP);
 far=wallIsFar(coords,nmWallLights,wavyIndex);
 mf.wall=wall;
 mf.view=view;

#ifndef NDEBUG
 maxV=wall->lastVertex-wall->firstVertex+1;
 assert(maxV<MAXVPERWALL);
 assert(wall->firstFace>=0);
 assert(s->xmin>=-160 && s->ymin>=-120 &&
	s->xmax<=160 && s->ymax<=120);
#endif
 for (f=wall->firstFace;f<=wall->lastFace;f++)
    {clip=0x8000;
     for (i=0;i<4;i++)
	{v=level_face[f].v[i];
	 assert(v>=0);
	 assert(v<maxV);
	 gtable.entry[i]=vCalc[v].light;
	 clip&=vCalc[v].light;
	 poly[i].x=vCalc[v].x;
	 poly[i].y=vCalc[v].y;
	}
     if (clip || !clip_visible(probeVDP1(poly),s))
	continue;

     assert(getPicClass(level_face[f].tile)==TILE16BPP);
     if ((black=CELLISBLACK(gtable)) || far)
	{XyInt q[4];
	 struct gourTable gq;
	 int g=weldFaceStrip(wall,f,vCalc,q,&gq,black? -1: level_face[f].tile);
	 if (clip_visible(q,s) && vdp1Range(q))
	    {VDP1WALK(q);
	     if (black)
		EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,LODCOL_MESH,q,NULL);
	     else
		EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5|DRAW_GOURAU,
			   LODCOL_FAR(level_face[f].tile),q,&gq);
	     nmPolys++;
	     lodFlat++;
	    }
	 lodCells+=g-f;
	 f=g;
	 continue;
	}
#if 0
     EZ_distSpr(DIR_NOREV,
		UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU,
		0,mapPic(level_face[f].tile),poly,&gtable);
#endif
     mf.f=f;
     wallCell(level_face[f].tile,poly,&gtable,&mf);
     nmPolys++;
    }
}


void drawWaterSurface(sWallType *wall,MthMatrix *view,SectorDrawRecord *s)
{int f,i,v,clip,clip1;
 int light;
 MthXyz original;
 XyInt poly[4];
 MthXyz tformed;
 struct gourTable gtable;
 struct vCalc vCalc[MAXVPERWALL];
#ifndef NDEBUG
 int maxV;
#endif

 checkStack();
 sawWater=5;
 assert(!(wall->flags & WALLFLAG_PARALLELOGRAM));

 if (wall->firstFace<0)
    return;

 assert(wall->lastVertex-wall->firstVertex<MAXVPERWALL);
 v=0;
 light=wall->firstLight;
 assert(wall->lastVertex-wall->firstVertex<MAXVPERWALL);
 for (i=wall->firstVertex;i<=wall->lastVertex;i++)
    {getVertex(i,&original);
     {int wb=GETWATERBRIGHT(original.x,original.z);      /* GCC14: band 0 stops at 31 */
      if (wb>31) wb=31;
      vCalc[v].light=worldGrey[wb];
     }
#if WAVYWATER
     /* wavy water */
     original.y+=F(GETWATERBRIGHT(original.x,original.z)-16);
#endif
     MTH_CoordTrans(view,&original,&tformed);
     if (tformed.z>F(20))
	vCalc[v].light&=~0x8000;
     project_point(&tformed,poly);
     vCalc[v].x=poly[0].x;
     vCalc[v].y=poly[0].y;
     v++;
     assert(v<MAXVPERWALL);
    }
#ifndef NDEBUG
 maxV=v;
 assert(maxV<MAXVPERWALL);
 assert(wall->firstFace>=0);
 assert(s->xmin>=-160 && s->ymin>=-120 &&
	s->xmax<=160 && s->ymax<=120);
#endif
 for (f=wall->firstFace;f<=wall->lastFace;f++)
    {clip1=0;
     clip=0x8000;
     for (i=0;i<4;i++)
	{v=level_face[f].v[i];
	 assert(v>=0);
	 assert(v<maxV);
	 gtable.entry[i]=vCalc[v].light;
	 clip&=vCalc[v].light;
	 clip1|=vCalc[v].light;
	 poly[i].x=vCalc[v].x;
	 poly[i].y=vCalc[v].y;
	}

     if (clip1 & 0x8000)
	{/* do real polygon clipping */
	 Fixed32 shades[4];
	 for (i=0;i<4;i++)
	    {v=level_face[f].v[i];
	     shades[i]=vCalc[v].light&0x1f;
	    }
	 drawClippedFace(level_face+f,shades,view,
			 wall->firstVertex);
	 continue;
	}

     if (clip || !clip_visible(probeVDP1(poly),s) || !vdp1Range(poly))
	continue;

     assert(getPicClass(level_face[f].tile)==TILE16BPP);
     EZ_polygon(DRAW_MESH|ECDSPD_DISABLE|UCLPIN_ENABLE|COLOR_5|
		DRAW_GOURAU,
		WATERCOLOR,poly,&gtable);
     nmPolys++;
    }
}

struct doorwayCache
{short xmin,ymin,xmax,ymax;
 int distance;
 /* xmin=-32000 for walls that are totally rejected */
} doorwayCache[MAXNMWALLS];


/* --- GCC14: the slave writes its cells as VDP1 commands ---------------------------------------
   It used to write a record per cell (outline, gouraud, tile), and the master turned each into a
   command after the wait: 46 us a record on console, 10-26 ms an image (Slave Cmds).  The slave
   now writes the commands themselves, in slaveCmd[]: the outline fitted to the VDP1's range and
   its V window (vdp1Fit), and each gouraud table at the address where it will stay -- the top
   of the bank's gouraud area, below what the image's earlier blocks took (EZ_gourTop).
   What only the master may do is left in the commands for it (drawSlaveWalls):
   - the tile, the tile cache having one writer.  A textured cell carries its pic in `color`, its
     size on screen in `dummy`, its V window in charAddr/charSize (1/1024); mapWallPic turns them
     into the slot's address, or the cell into a flat polygon, as for the master's own cells;
   - the sprites of each slave sector, which the master draws meanwhile: a JUMP_CALL on the
     sector's last command -- each sector opens on its clip (RECTCLIP), so it has one;
   - water (PowerSlave): a skipped command, which the master turns into a call of the surface.
   Then the block goes into the list by DMA.  Its array doubles as the traversal's scratch
   (doorwayCache), free while the slave draws. */
#define MAXSLAVESECTORS 64      /* the servo gives the slave 51 sectors at most */
#define MAXSLAVEWATER 32
static struct cmdTable *slaveCmd=(struct cmdTable *)doorwayCache;
/* its gouraud tables, from the end: table k at MAXNMSLAVEPOLYS-1-k, grshAddr slaveGourTop-1-k */
static struct gourTable *slaveGour=
   (struct gourTable *)(((char *)doorwayCache)+MAXNMSLAVEPOLYS*sizeof(struct cmdTable));
#define SLAVECMD  ((struct cmdTable *)(((int)slaveCmd)|0x20000000))
#define SLAVEGOUR ((struct gourTable *)(((int)slaveGour)|0x20000000))
int nmSlavePolys;               /* commands in slaveCmd */
static int nmSlaveGour,nmSlaveSectors,nmSlaveWater;
static int slaveGourTop;        /* EZ_gourTop(), set by drawWalls before the kick */
static short slaveSectorEnd[MAXSLAVESECTORS];   /* each slave sector's last command */
static short slaveWaterCmd[MAXSLAVEWATER],slaveWaterWall[MAXSLAVEWATER],
	     slaveWaterSector[MAXSLAVEWATER];   /* its skipped command, wall, slave sector */

/*struct vCalc slave_vCalc[MAXVPERWALL]; */
static struct vCalc *slave_vCalc=(struct vCalc *)(((char *)doorwayCache)+
				 MAXNMSLAVEPOLYS*(sizeof(struct cmdTable)+sizeof(struct gourTable)));
typedef char slaveBlockFits[MAXNMSLAVEPOLYS*(sizeof(struct cmdTable)+sizeof(struct gourTable))+
			    MAXVPERWALL*sizeof(struct vCalc)<=sizeof(doorwayCache)? 1: -1];

/* the next command of the slave's block, `q` its outline, `g` its gouraud table.  Past the
   block's end it goes nowhere: the walls already check their room (MAXNMSLAVEPOLYS - 50) */
static struct cmdTable *slaveCmdNext(short control,short drawMode,short colour,XyInt *q,
				     struct gourTable *g)
{static struct cmdTable spill;
 struct cmdTable *c;
 int i;
 if (nmSlavePolys>=MAXNMSLAVEPOLYS)
    return &spill;
 c=SLAVECMD+nmSlavePolys++;
 c->control=control;
 c->link=0;
 c->drawMode=drawMode;
 c->color=colour;
 c->charAddr=0;
 c->charSize=0;
 if (q)
    {short *from=(short *)q,*to=(short *)&c->ax;
     for (i=8;i;i--)
	*(to++)=*(from++);
    }
 if (g)
    {SLAVEGOUR[MAXNMSLAVEPOLYS-1-nmSlaveGour]=*g;
     c->grshAddr=slaveGourTop-1-nmSlaveGour++;
    }
 else
    c->grshAddr=0;
 c->dummy=0;
 return c;
}

/* a flat quad -- a fused wall, a run of black cells, the far LOD -- once in the VDP1's range */
static void slaveCmdFlat(XyInt *q,short colour,struct gourTable *g)
{if (vdp1Range(q))
    slaveCmdNext(ZOOM_NOPOINT|DIR_NOREV|FUNC_POLYGON,
		 UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5|(g? DRAW_GOURAU: 0),colour,q,g);
}

/* a textured cell: wallCell and EZ_distSprVClip, all but the tile, which the master maps.  Its
   size is taken before the fit and the range cut the outline, as wallCell does. */
static void slaveCmdCell(int pic,XyInt *poly,struct gourTable *g,const MeshFace *m)
{int i,x0,x1,y0,y1,t0,t1;
 struct cmdTable *c;
 struct gourTable cut;
 x0=x1=poly[0].x;
 y0=y1=poly[0].y;
 for (i=1;i<4;i++)
    {if (poly[i].x<x0) x0=poly[i].x;
     if (poly[i].x>x1) x1=poly[i].x;
     if (poly[i].y<y0) y0=poly[i].y;
     if (poly[i].y>y1) y1=poly[i].y;
    }
 if (m && (x0<viewXmin || x1>viewXmax || y0<viewYmin || y1>viewYmax))
    fitFace(m,poly,g);
 x1-=x0;
 y1-=y0;
 if (!vdp1Fit(poly,&t0,&t1))
    return;
 /* GCC14: the master's cellOut moves the shading with the cut quad (WALLASM.H gourCut); this
    copy did not, so a cut cell drawn by the slave carried the shading of a quad that no longer
    exists.  It never showed while the cut sat far off screen; at the window it does. */
 if (g && (t0>0 || t1<VCLIPONE))
    {gourCut(&cut,g,t0,t1);
     g=&cut;
    }
 c=slaveCmdNext(ZOOM_NOPOINT|DIR_NOREV|FUNC_DISTORSP,
		UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU,pic,poly,g);
 c->charAddr=t0;
 c->charSize=t1;
 c->dummy=x1<y1? x1: y1;
}

/* a skipped command: the master may hang a call on it (sprites, water) */
static void slaveCmdSkip(void)
{slaveCmdNext(SKIP_NEXT,0,0,NULL,NULL);
}

void slave_drawWater(sWallType *theWall)
{if (nmSlaveWater<MAXSLAVEWATER && nmSlavePolys<MAXNMSLAVEPOLYS)
    {slaveWaterCmd[nmSlaveWater]=nmSlavePolys;
     slaveWaterWall[nmSlaveWater]=theWall-level_wall;
     slaveWaterSector[nmSlaveWater]=nmSlaveSectors;
     nmSlaveWater++;
     slaveCmdSkip();
    }
}

void slave_drawRectWall(sWallType *theWall,MthXyz *coords,
			SectorDrawRecord *s)
{MthXyz vWidth,vHeight;
 XyInt poly[4];
 int w,h,clip;
 int runStart;   /* start of the current run of black cells, -1 = none */
 int light;
 int tex,row1,row2;
 int width=theWall->tileLength; /* GCC14: not const, halved under #if MIPMAP */
 int height=theWall->tileHeight;
 struct gourTable gtable;
 char *ppattern;
#if MIPMAP
 int tileBias;
#endif

 if (height*width+nmSlavePolys+50>MAXNMSLAVEPOLYS)
    return;

#if MIPMAP
 if (mipEnable && /* GCC14: also drops the original `desiredWeapon &&` gate, a leftover
       test hack that kept mips off while holding weapon 0 (the sword) */
     width>=2 && height>=2 && /* GCC14: never halve a 1-tile dimension (bug 1) */
     coords[0].z>MIPDIST &&
     coords[1].z>MIPDIST &&
     coords[2].z>MIPDIST &&
     coords[3].z>MIPDIST)
    {width>>=1;
     height>>=1;
     tileBias=mipBase;
    }
 else
    tileBias=0;
#endif

 Set_Hardware_Divide(coords[1].x-coords[0].x,width);
 vWidth.y=(coords[1].y-coords[0].y)/width;
 vWidth.x=Get_Hardware_Divide();

 Set_Hardware_Divide(coords[1].z-coords[0].z,width);
 vHeight.x=(coords[2].x-coords[1].x)/height;
 vWidth.z=Get_Hardware_Divide();

 Set_Hardware_Divide(coords[2].y-coords[1].y,height);
 vHeight.z=(coords[2].z-coords[1].z)/height;
 vHeight.y=Get_Hardware_Divide();

#ifdef LIGHT
 sbuildLightList(theWall,s-sectorDraw);
#endif

 if (wallIsBlack(theWall,coords,snmWallLights,sWavyIndex))
    {XyInt q[4];
     if (fuseWallPoly(coords,s,q))
	{slaveCmdFlat(q,LODCOL_FUSE,NULL);
	 slave_lodFused++;
	 slave_lodCells+=theWall->tileHeight*theWall->tileLength-1;
	}
     return;
    }
 if (wallIsFar(coords,snmWallLights,sWavyIndex))
    {XyInt q[4];
     struct gourTable g;
     if (fuseWallPoly(coords,s,q))
	{farCorners(theWall,coords,&g);
	 slaveCmdFlat(q,LODCOL_FAR(level_texture[theWall->textures+1]),&g);
	 slave_lodFused++;
	 slave_lodCells+=theWall->tileHeight*theWall->tileLength-1;
	}
     return;
    }

 light=theWall->firstLight;

 rectTransform(vWidth.x,vWidth.y,vWidth.z,
	       light,height+1,width+1,
	       coords[0].x,coords[0].y,coords[0].z,
	       vHeight.x,vHeight.y,vHeight.z,
	       slave_vCalc,
	       (snmWallLights||sWavyIndex)?sgetLight:NULL,
#if MIPMAP
	       tileBias?1:0,tileBias?2*(theWall->tileLength-width):0,
#else
	       0,0,
#endif
	       NEARCLIP);

 FITGRID(slave_vCalc);

 tex=theWall->textures;
 row1=0;
 row2=width+1;
 for (h=0;h<height;h++)
    {runStart=-1;
     for (w=0;w<=width;w++)
	{if (w<width && LODCELLDARK(slave_vCalc))
	    {if (runStart<0)
		runStart=w;
	     tex+=2;
	     continue;
	    }
	 if (runStart>=0)
	    {XyInt q[4];
	     LODRUNQUAD(slave_vCalc);
	     if (clip_visible(q,s))
		{slaveCmdFlat(q,LODCOL_RECT,NULL);
		 slave_lodFlat++;
		}
	     slave_lodCells+=w-runStart-1;
	     runStart=-1;
	    }
	 if (w==width)
	    break;
	 clip=0x8000;
#if MIPMAP
	 if (tileBias)
	    {/* GCC14: a mip cell is ONE sprite whose pic replicates ONE half-res tile 2x2
		over a 2x2 world-tile block.  A block mixing (pattern,tile) pairs cannot be
		mipped: fall back to its 4 full-res tiles, with the missing grid points
		averaged in screen space.  Uniform blocks sample the (2h,2w) pair. */
	     int fullTL=theWall->tileLength;
	     int rr,cc,p00,p01,p10,p11;
	     rr=h*2; cc=w*2;
	     p00=theWall->textures+(rr*fullTL+cc)*2;
	     p01=(cc+1<fullTL)? p00+2: p00;
	     p10=(rr+1<theWall->tileHeight)? p00+fullTL*2: p00;
	     p11=p10+(p01-p00);
	     tex=p00;
	     if (level_texture[p00]!=level_texture[p01] ||
		 level_texture[p00]!=level_texture[p10] ||
		 level_texture[p00]!=level_texture[p11] ||
		 level_texture[p00+1]!=level_texture[p01+1] ||
		 level_texture[p00+1]!=level_texture[p10+1] ||
		 level_texture[p00+1]!=level_texture[p11+1])
		{struct vCalc pts[3][3];
		 int dh,dw;
		 int pr[2][2];
		 pr[0][0]=p00; pr[0][1]=p01; pr[1][0]=p10; pr[1][1]=p11;
		 pts[0][0]=slave_vCalc[row1+w];
		 pts[0][2]=slave_vCalc[row1+w+1];
		 pts[2][2]=slave_vCalc[row2+w+1];
		 pts[2][0]=slave_vCalc[row2+w];
		 clip&=pts[0][0].light&pts[0][2].light&
		       pts[2][2].light&pts[2][0].light;
		 poly[0].x=pts[0][0].x; poly[0].y=pts[0][0].y;
		 poly[1].x=pts[0][2].x; poly[1].y=pts[0][2].y;
		 poly[2].x=pts[2][2].x; poly[2].y=pts[2][2].y;
		 poly[3].x=pts[2][0].x; poly[3].y=pts[2][0].y;
		 if (clip || !clip_visible(probeVDP1(poly),s))
		    continue;
		 MIPMID(pts[0][1],pts[0][0],pts[0][2]);
		 MIPMID(pts[1][0],pts[0][0],pts[2][0]);
		 MIPMID(pts[1][2],pts[0][2],pts[2][2]);
		 MIPMID(pts[2][1],pts[2][0],pts[2][2]);
		 MIPMID(pts[1][1],pts[1][0],pts[1][2]);
		 for (dh=0;dh<2;dh++)
		    for (dw=0;dw<2;dw++)
		       {int t=pr[dh][dw];
			ppattern=pattern[(int)level_texture[t]];
			gtable.entry[(int)*ppattern]=pts[dh][dw].light;
			poly[(int)*ppattern].x=pts[dh][dw].x;
			poly[(int)*ppattern].y=pts[dh][dw].y;
			ppattern++;
			gtable.entry[(int)*ppattern]=pts[dh][dw+1].light;
			poly[(int)*ppattern].x=pts[dh][dw+1].x;
			poly[(int)*ppattern].y=pts[dh][dw+1].y;
			ppattern++;
			gtable.entry[(int)*ppattern]=pts[dh+1][dw+1].light;
			poly[(int)*ppattern].x=pts[dh+1][dw+1].x;
			poly[(int)*ppattern].y=pts[dh+1][dw+1].y;
			ppattern++;
			gtable.entry[(int)*ppattern]=pts[dh+1][dw].light;
			poly[(int)*ppattern].x=pts[dh+1][dw].x;
			poly[(int)*ppattern].y=pts[dh+1][dw].y;
			slaveCmdCell(level_texture[t+1],poly,&gtable,NULL);
		       }
		 continue;
		}
	    }
#endif

	 ppattern=pattern[(int)level_texture[tex++]];
	 gtable.entry[(int)*ppattern]=slave_vCalc[row1+w].light;
	 clip&=slave_vCalc[row1+w].light;
	 poly[(int)*ppattern].x=slave_vCalc[row1+w].x;
	 poly[(int)*ppattern].y=slave_vCalc[row1+w].y;
	 ppattern++;

	 gtable.entry[(int)*ppattern]=slave_vCalc[row1+w+1].light;
	 clip&=slave_vCalc[row1+w+1].light;
	 poly[(int)*ppattern].x=slave_vCalc[row1+w+1].x;
	 poly[(int)*ppattern].y=slave_vCalc[row1+w+1].y;
	 ppattern++;

	 gtable.entry[(int)*ppattern]=slave_vCalc[row2+w+1].light;
	 clip&=slave_vCalc[row2+w+1].light;
	 poly[(int)*ppattern].x=slave_vCalc[row2+w+1].x;
	 poly[(int)*ppattern].y=slave_vCalc[row2+w+1].y;
	 ppattern++;

	 gtable.entry[(int)*ppattern]=slave_vCalc[row2+w].light;
	 clip&=slave_vCalc[row2+w].light;
	 poly[(int)*ppattern].x=slave_vCalc[row2+w].x;
	 poly[(int)*ppattern].y=slave_vCalc[row2+w].y;

	 if (clip || !clip_visible(probeVDP1(poly),s))
	    {tex++;
	     continue;
	    }

	 slaveCmdCell(level_texture[tex]
#if MIPMAP
		      +tileBias
#endif
		      ,poly,&gtable,NULL);
	 tex++;
	}
     row1+=width+1;
     row2+=width+1;
    }
}

void slave_drawWall(sWallType *wall,MthMatrix *view,MthXyz *coords,SectorDrawRecord *s)
{int f,i,v,clip,far,black;
 XyInt poly[4];
 struct gourTable gtable;
 MeshFace mf;

 if (wall->lastFace-wall->firstFace+1+nmSlavePolys+50>MAXNMSLAVEPOLYS)
    return;

 sbuildLightList(wall,s-sectorDraw);

 assert(wall->lastVertex-wall->firstVertex<MAXVPERWALL);

 normTransform(level_vertex+wall->firstVertex,view,
	       wall->lastVertex-wall->firstVertex+1,slave_vCalc,
	       (snmWallLights||sWavyIndex)?&sgetLight:NULL,NEARCLIP);
 far=wallIsFar(coords,snmWallLights,sWavyIndex);
 mf.wall=wall;
 mf.view=view;

 for (f=wall->firstFace;f<=wall->lastFace;f++)
    {clip=0x8000;
     for (i=0;i<4;i++)
	{v=level_face[f].v[i];
	 gtable.entry[i]=slave_vCalc[v].light;
	 clip&=slave_vCalc[v].light;
	 poly[i].x=slave_vCalc[v].x;
	 poly[i].y=slave_vCalc[v].y;
	}
     if (clip || !clip_visible(probeVDP1(poly),s))
	continue;

     if ((black=CELLISBLACK(gtable)) || far)
	{XyInt q[4];
	 struct gourTable gq;
	 int g=weldFaceStrip(wall,f,slave_vCalc,q,&gq,black? -1: level_face[f].tile);
	 if (clip_visible(q,s))
	    {if (black)
		slaveCmdFlat(q,LODCOL_MESH,NULL);
	     else
		slaveCmdFlat(q,LODCOL_FAR(level_face[f].tile),&gq);
	     slave_lodFlat++;
	    }
	 slave_lodCells+=g-f;
	 f=g;
	 continue;
	}
     mf.f=f;
     slaveCmdCell(level_face[f].tile,poly,&gtable,&mf);
    }
}

/* GCC14: the traversal's output -- the visible sectors, their clip boxes and draw order.  The
   drawing reads the set of the view being drawn through these pointers; a traversal fills the
   set `tr` points at (TravSet, below).  Solo and view 0 use the static arrays. */
static SectorDrawRecord sectorDraw0[MAXNMSECTORS];
static SectorDrawRecord *updateList0[MAXNMSECTORS];
SectorDrawRecord *sectorDraw=sectorDraw0;
SectorDrawRecord **updateList=updateList0;
int updateListSize;
SectorDrawRecord *drawList[MAXNMSECTORS];       /* traversal scratch: one traversal at a time */
int drawListSize;

/* GCC14: split screen.  Views 1.. are traversed by the SLAVE, while the master draws view 0
   and runs the game logic: its share of view 0 drawn, it walks the views queued at the top of
   the image (wallsQueueView), each into its own set, allocated in low RAM the first time a
   level runs with several players (wallsSplitAlloc).  Every view is thus traversed from the
   cameras as the image began -- view 0's too (its traversal ran in the last image's tail).
   The logic defers every geometry move to Post (processDelayedMoves), after the slave has
   joined, so the walls the slave reads are frozen meanwhile.  Not frozen: the sprites and the
   light list; the sprite leaves and the light positions are made on the master, per view,
   when the view is drawn. */
typedef struct
{SectorDrawRecord *sd;
 SectorDrawRecord **ul;
 int ulSize;
 MthXyz pos;                    /* the viewer, as the traversal saw it */
 int sector;
 short xmin,ymin,xmax,ymax;     /* its window */
 MthMatrix view;
} TravSet;
static TravSet travSet[MPMAX]={{sectorDraw0,updateList0}};
static TravSet *tr=travSet;              /* the set a traversal fills */
static struct doorwayCache *trDC=doorwayCache;  /* its scratch.  The static array doubles as
					   the slave's commands (slaveCmd): a traversal run while
					   those wait for drawSlaveWalls uses splitDC. */
static struct doorwayCache *splitDC;
static int splitSets;                    /* views 1..splitSets have a set */
static int travQueued;                   /* views 1..travQueued queued this image */
static volatile int travDone;            /* the last one the slave finished (cache-through) */
static int travKicked;                   /* the queue went to the slave with view 0's draw */
#define TRAVDONE (*(volatile int *)((int)&travDone|0x20000000))

/* the viewer and window of a set, from the renderer's current ones */
static void travSetViewer(TravSet *t,Sprite *c)
{t->pos=c->pos;
 t->sector=c->s;
 t->xmin=viewXmin; t->ymin=viewYmin;
 t->xmax=viewXmax; t->ymax=viewYmax;
}

/* sets for views 1..MPMAX-1, sized for this level.  Low RAM (area 0), freed with the level
   (mem_init).  A set that does not fit leaves its view to the master's own traversal. */
void wallsSplitAlloc(void)
{int k;
 if (splitSets==MPMAX-1)
    return;
 if (!splitDC)
    splitDC=mem_nocheck_malloc(0,level_nmWalls*sizeof(struct doorwayCache));
 if (!splitDC)
    return;
 for (k=splitSets+1;k<MPMAX;k++)
    {travSet[k].sd=mem_nocheck_malloc(0,level_nmSectors*sizeof(SectorDrawRecord));
     if (!travSet[k].sd)
	break;
     travSet[k].ul=mem_nocheck_malloc(0,level_nmSectors*sizeof(SectorDrawRecord *));
     if (!travSet[k].ul)
	break;
     splitSets=k;
    }
}

/* views 1..wallsSplitSets() have a set; the master traverses the others (overlay MTRAV) */
int wallsSplitSets(void)
{return splitSets;
}

/* GCC14: the bytes wallsSplitAlloc would still take to give every view its set -- what a game
   must leave free when it spends the level's memory before a player joins (PSMULTI.C) */
int wallsSplitNeed(void)
{return (splitDC? 0: level_nmWalls*(int)sizeof(struct doorwayCache))+
	(MPMAX-1-splitSets)*level_nmSectors*(int)(sizeof(SectorDrawRecord)+sizeof(SectorDrawRecord *));
}

/* a new level: its memory is gone */
void wallsSplitReset(void)
{int k;
 splitDC=NULL;
 splitSets=0;
 travQueued=0;
 for (k=1;k<MPMAX;k++)
    travSet[k].sd=NULL,travSet[k].ul=NULL;
}

/* View k (1..) will be drawn from this matrix and this camera: queue its traversal.  Views
   must be queued in order, before drawWalls(0) kicks the slave. */
void wallsQueueView(int k,MthMatrix *view,Sprite *c)
{if (k==1)
    travQueued=0;               /* a new image's queue */
 if (k>splitSets || k!=travQueued+1)
    return;
 travSet[k].view=*view;
 travSetViewer(travSet+k,c);
 travQueued=k;
}

void drawSector(int sectorNm,MthMatrix *view,int slave)
{sSectorType *sec;
 sWallType *theWall;
 XyInt poly[4];
 MthXyz wallV[4];
 MthXyz tformed[4];
 MthXyz clipped[4];
 int w,i;

 sec = &(level_sector[sectorNm]);
 sec->flags|=SECFLAG_SEEN;

 assert(viewPos.x<F(16000) && viewPos.x>F(-16000) &&
	viewPos.y<F(16000) && viewPos.y>F(-16000));

 assert(sectorNm>=0 && sectorNm<level_nmSectors);
 /* draw walls */
 for (w=sec->firstWall;w<=sec->lastWall;w++)
    {theWall=&level_wall[w];
     /* do some validity checks on the wall */
     assert(abs(theWall->normal[0])<=F(1));
     assert(abs(theWall->normal[1])<=F(1));
     assert(abs(theWall->normal[2])<=F(1));

     if (theWall->flags & WALLFLAG_INVISIBLE)
	continue;
     /* back face clipping */
     getVertex(theWall->v[0],wallV+0);

     if ((f(viewPos.x-wallV[0].x))*theWall->normal[0]+
	 (f(viewPos.y-wallV[0].y))*theWall->normal[1]+
	 (f(viewPos.z-wallV[0].z))*theWall->normal[2]<0)
	continue;

     /* far plane clipping */
     getVertex(theWall->v[1],wallV+1);
     getVertex(theWall->v[2],wallV+2);
     getVertex(theWall->v[3],wallV+3);

     for (i=0;i<4;i++)
	MTH_CoordTrans(view,wallV+i,tformed+i);

#if ENABLEFARCLIP
     if (tformed[0].z>FARCLIP && tformed[1].z>FARCLIP &&
	 tformed[2].z>FARCLIP && tformed[3].z>FARCLIP)
	continue;
#endif

     /* near plane clipping */
     if (!(theWall->flags & WALLFLAG_WATERSURFACE))
	if (tformed[0].z<NEARCLIP &&
	    tformed[1].z<NEARCLIP &&
	    tformed[2].z<NEARCLIP &&
	    tformed[3].z<NEARCLIP)
	   continue;
     /* clip to near plane */
     clipZ(tformed,clipped,NEARCLIP);

     for (i=0;i<4;i++)
	project_point(clipped+i,poly+i);

     if (!clip_visible(probeVDP1(poly),sectorDraw+sectorNm))
	continue;

#if WATER
     if (theWall->flags & WALLFLAG_WATERSURFACE)
	{if (slave)
	    slave_drawWater(theWall);
	 else
	    drawWaterSurface(theWall,view,sectorDraw+sectorNm);
	 continue;
	}
#endif

     if (theWall->flags & WALLFLAG_PARALLAX)
	{if (slave)
	    {for (i=0;i<4;i++)
		{if (poly[i].x<slave_plaxBBxmin)
		    slave_plaxBBxmin=poly[i].x;
		 if (poly[i].y<slave_plaxBBymin)
		    slave_plaxBBymin=poly[i].y;
		 if (poly[i].x>slave_plaxBBxmax)
		    slave_plaxBBxmax=poly[i].x;
		 if (poly[i].y>slave_plaxBBymax)
		    slave_plaxBBymax=poly[i].y;
		}
	    }
	 else
	    {for (i=0;i<4;i++)
		{if (poly[i].x<plaxBBxmin)
		    plaxBBxmin=poly[i].x;
		 if (poly[i].y<plaxBBymin)
		    plaxBBymin=poly[i].y;
		 if (poly[i].x>plaxBBxmax)
		    plaxBBxmax=poly[i].x;
		 if (poly[i].y>plaxBBymax)
		    plaxBBymax=poly[i].y;
		}
	    }
	 continue;
	}

     /* we're going to draw the wall */
     if (slave)
	{if (level_sector[sectorNm].flags & SECFLAG_WATER)
	    sWavyIndex=(w & 0x1f)+1;
	 else
	    sWavyIndex=0;
	 if (theWall->flags & WALLFLAG_PARALLELOGRAM)
	    slave_drawRectWall(theWall,tformed,sectorDraw+sectorNm);
	 else
	    slave_drawWall(theWall,view,tformed,sectorDraw+sectorNm);
	}
     else
	{if (level_sector[sectorNm].flags & SECFLAG_WATER)
	    wavyIndex=(w & 0x1f)+1;
	 else
	    wavyIndex=0;
	 if (theWall->flags & WALLFLAG_PARALLELOGRAM)
	    drawRectWall(theWall,tformed,sectorDraw+sectorNm);
	 else
	    drawWall(theWall,view,tformed,sectorDraw+sectorNm);
	}

    }

}

/* --- GCC14: TRAVERSAL code.  From here to TRAVERSAL END it fills the set `tr` points at, with
   the viewer and window saved in it: in split screen the master draws another view meanwhile,
   with its own. */
#define sectorDraw     (tr->sd)
#define updateList     (tr->ul)
#define updateListSize (tr->ulSize)
#define doorwayCache   trDC
#define viewPos        (tr->pos)
#define viewSector     (tr->sector)
#undef XMIN
#undef YMIN
#undef XMAX
#undef YMAX
#define XMIN           (tr->xmin)
#define YMIN           (tr->ymin)
#define XMAX           (tr->xmax)
#define YMAX           (tr->ymax)

void findDoorways(int sectorNm,MthMatrix *view)
{sSectorType *sec;
 sWallType *theWall;
 XyInt poly[4];
 MthXyz wallV[4];
 MthXyz tformed[4];
 MthXyz clipped[4];
 Fixed32 wallDist;
 int w,i,polyGood;
 SectorDrawRecord *sr;
 SectorDrawRecord *next;
 int xmin,ymin,xmax,ymax,expanded;
 int cacheValid;

 sec = &(level_sector[sectorNm]);
 sr=sectorDraw+sectorNm;
 cacheValid=sr->flags & SDFLAG_CACHEVALID;
 sr->flags|=SDFLAG_CACHEVALID;

 for (w=sec->firstWall;w<=sec->lastWall;w++)
    {theWall=&level_wall[w];
     if (theWall->nextSector==-1)
	/* doorways are sorted to be first in the list */
	return;
     if (theWall->flags & WALLFLAG_BLOCKSSIGHT)
	continue;
     if (!cacheValid)
	{doorwayCache[w].xmin=-32000;

	 /* back face clipping */
	 getVertex(theWall->v[0],wallV+0);

	 wallDist=(f(viewPos.x-wallV[0].x))*theWall->normal[0]+
	          (f(viewPos.y-wallV[0].y))*theWall->normal[1]+
	          (f(viewPos.z-wallV[0].z))*theWall->normal[2];
	 if (sectorNm!=viewSector /* we may apear to be behind walls of the
				    sector we are in, but we really aren't */
	     && wallDist<=0) /* increase this number to increase
					protection from draw loops */
	    continue;
	 if (theWall->nextSector==viewSector)
	    /* we know that we can not see thru a wall into the sector the
	       viewCamera is in */
	    continue;


	 getVertex(theWall->v[1],wallV+1);
	 getVertex(theWall->v[2],wallV+2);
	 getVertex(theWall->v[3],wallV+3);

	 /* clip out doorways that have 0 height, so can't see under doors */
	 if (theWall->normal[1]==0 &&
	     wallV[0].y==wallV[3].y &&
	     wallV[0].y==wallV[2].y &&
	     wallV[0].y==wallV[1].y)
	    continue;

	 /* far plane clipping */
	 for (i=0;i<4;i++)
	    MTH_CoordTrans(view,wallV+i,tformed+i);

#if ENABLEFARCLIP
	 if (tformed[0].z>FARCLIP && tformed[1].z>FARCLIP &&
	     tformed[2].z>FARCLIP && tformed[3].z>FARCLIP)
	    continue;
#endif

	 /* near plane clipping */
	 if (tformed[0].z<SECTORBNDRYNEARCLIP &&
	     tformed[1].z<SECTORBNDRYNEARCLIP &&
	     tformed[2].z<SECTORBNDRYNEARCLIP &&
	     tformed[3].z<SECTORBNDRYNEARCLIP)
	    continue;

	 if (wallDist<F(48) &&
	     (tformed[0].z<NEARCLIP ||
	      tformed[1].z<NEARCLIP ||
	      tformed[2].z<NEARCLIP ||
	      tformed[3].z<NEARCLIP))
	    polyGood=0;
	 else
	    {
	     /* clip to near plane */
	     clipZ(tformed,clipped,NEARCLIP);
	     for (i=0;i<4;i++)
		project_point(clipped+i,poly+i);

	     polyGood=1;
	     if (!clip_visible(probeVDP1(poly),
			       sectorDraw+viewSector/*so cache will be good */))
		continue;
	    }
	 /* find bounding box for opening */
	 if (!polyGood)
	    {xmin=XMIN;
	     ymin=YMIN;
	     xmax=XMAX;
	     ymax=YMAX;
	    }
	 else
	    {
#if 0
	     if (theWall->flags & WALLFLAG_BLOCKED)
		{addDebugLine(poly[0].x,poly[0].y,poly[1].x,poly[1].y);
		 addDebugLine(poly[1].x,poly[1].y,poly[2].x,poly[2].y);
		 addDebugLine(poly[2].x,poly[2].y,poly[3].x,poly[3].y);
		 addDebugLine(poly[3].x,poly[3].y,poly[0].x,poly[0].y);
		}
#endif
	     xmin=1000;
	     ymin=1000;
	     xmax=-1000;
	     ymax=-1000;
	     for (i=0;i<4;i++)
		{if (poly[i].x<xmin)
		    xmin=poly[i].x;
		 if (poly[i].x>xmax)
		    xmax=poly[i].x;
		 if (poly[i].y<ymin)
		    ymin=poly[i].y;
		 if (poly[i].y>ymax)
		    ymax=poly[i].y;
		}
	    }
	 doorwayCache[w].xmin=xmin;
	 doorwayCache[w].ymin=ymin;
	 doorwayCache[w].xmax=xmax;
	 doorwayCache[w].ymax=ymax;

	 /* doorwayCache[w].distance=
	    (tformed[0].z+tformed[1].z+tformed[2].z+tformed[3].z)>>2; */
	 /* that distance approx wasn't good enough for some cases,
	    maybe this one will be better.  Maybe we could just use two of the
	    verticies instead of all 4 */
#if 0
	 {int cx=0,cy=0,cz=0;
	  for (i=0;i<4;i++)
	     {cx+=tformed[i].x;
	      cy+=tformed[i].y;
	      if (tformed[i].z>0)
		 cz+=tformed[i].z;
	     }
	  doorwayCache[w].distance=
	     approxDist(cx,cy,(cz<<2));
	 }
#else
	 /* that one still had some problems, try this one: */
	 {int minx=INT_MAX,xposCnt=0;
	  int miny=INT_MAX,yposCnt=0;
	  int minz=INT_MAX,zposCnt=0;
	  int j;
	  for (i=0;i<4;i++)
	     {if (tformed[i].x<0)
		 j=-tformed[i].x;
	      else
		 {xposCnt++;
		  j=tformed[i].x;
		 }
	      if (j<minx)
		 minx=j;

	      if (tformed[i].y<0)
		 j=-tformed[i].y;
	      else
		 {yposCnt++;
		  j=tformed[i].y;
		 }
	      if (j<miny)
		 miny=j;

	      if (tformed[i].z<0)
		 j=-tformed[i].z;
	      else
		 {zposCnt++;
		  j=tformed[i].z;
		 }
	      if (j<minz)
		 minz=j;
	     }
	  if (xposCnt!=0 && xposCnt!=4)
	     minx=0;
	  if (yposCnt!=0 && yposCnt!=4)
	     miny=0;
	  if (zposCnt!=0 && zposCnt!=4)
	     minz=0;
	  doorwayCache[w].distance=
	     approxDist(minx,miny,minz);
	 }
#endif
	}
     else
	{/* cache is valid */
	 xmin=doorwayCache[w].xmin;
	 if (xmin==-32000)
	    continue;
	 xmax=doorwayCache[w].xmax;
	 ymin=doorwayCache[w].ymin;
	 ymax=doorwayCache[w].ymax;
	}

     next=sectorDraw+theWall->nextSector;

     /* clip to our bounding box */
     if (xmin<sr->xmin)
	xmin=sr->xmin;
     if (ymin<sr->ymin)
	ymin=sr->ymin;
     if (xmax>sr->xmax)
	xmax=sr->xmax;
     if (ymax>sr->ymax)
	ymax=sr->ymax;
     if (xmax<=xmin || ymax<=ymin)
	continue;
     /* merge current bounding box with new bbox */
     if (!(next->flags & SDFLAG_BBVALID))
	{next->xmin=xmin; next->ymin=ymin;
	 next->xmax=xmax; next->ymax=ymax;
	 next->flags|=(SDFLAG_NEEDTOPROCESS|SDFLAG_BBVALID);
	 next->distance=
	    doorwayCache[w].distance;
	 updateList[updateListSize++]=next;
	}
     else
	{/* new code here */
	 if (doorwayCache[w].distance<next->distance)
	    next->distance=doorwayCache[w].distance;
	 /* end of new scary code */
	 expanded=0;
	 if (xmin<next->xmin)
	    {next->xmin=xmin;
	     expanded=1;
	    }
	 if (ymin<next->ymin)
	    {next->ymin=ymin;
	     expanded=1;
	    }
	 if (xmax>next->xmax)
	    {next->xmax=xmax;
	     expanded=1;
	    }
	 if (ymax>next->ymax)
	    {next->ymax=ymax;
	     expanded=1;
	    }
	 if (expanded)
	    next->flags|=SDFLAG_NEEDTOPROCESS;
	}
    }
}


#undef sectorDraw
#undef updateList
#undef updateListSize
#undef doorwayCache
#undef viewPos
#undef viewSector
#undef XMIN
#undef YMIN
#undef XMAX
#undef YMAX
#define XMIN        viewXmin
#define YMIN        viewYmin
#define XMAX        viewXmax
#define YMAX        viewYmax
/* --- TRAVERSAL END */

#define IPRA (Uint16 volatile *)0xfffffee2
#define IPRB (Uint16 volatile *)0xfffffe60
#define TIER (Uint8 volatile *)0xfffffe10
#define FTCSR (Uint8 volatile *)0xfffffe11
#define CACHECNTRL (Uint8 volatile *)0xfffffe92

volatile int slaveDrawStart;
       /* index in the update list at which to start drawing */
/* --- GCC14: traversal pipeline ---------------------------------------------------------
   Camera and level_vertex[] are frozen once the game loop ends (movePlayer and
   updatePushBlockPositions have written) and next frame's drawWalls reads them as is, so a
   traversal started there gives exactly the same result.
     0  off, original behaviour.
       1  MEASURE: the slave traverses in the tail, the master re-traverses and OVERWRITES it.
          Same image; pipeSpin (spins at the join) says whether the traversal fits in the tail.
          2  LEVER: the master skips its own traversal.
        Launched at the VERY END of the game loop, after the four returns and the menu, so no exit
        leaves the slave running while the level reloads.  Not while the earthquake is active: its
     jitter is drawn at the top of the next frame.  WALLPIPE is in WALLS.H (the loop needs it). */

/* GCC14: depth fog.  Both vertex transforms subtracted z>>24 (one level per 256 u, black at
   4096 -- beyond any line of sight, so no fog).  z>>24 now indexes this table (wallasm_gnu.s
   _fogTable), so slope and shape are tunable.  A deliberate departure from Doom, which never
   darkens a fully lit sector: the setting exists to judge it on screen. */
unsigned char fogTable[256];
int fogDist=4096;      /* distance at which a fully lit sector (16) reaches black */
const short fogLevels[4]={4096,2048,1024,512};
int fogCap=4096;
int lightOn=1,lightPeakPct=100,lightRadPct=100,lightAddR,lightAddG,lightAddB;

int fogLevel(void)
{int i;
 for (i=3;i>0 && fogLevels[i]!=fogCap;i--)
    ;
 return i;
}

/* GCC14: called once per view per image in split screen (SRUINS.C mpSetViewFog), so no divide
   per entry: one reciprocal, then entries until the table saturates at 31 -- 32 at most. */
void setFog(int dist)
{int i,v,step;
 if (dist<256)
    dist=256;
 fogDist=dist;
 step=(4096<<16)/dist;          /* i indexes 256-unit steps: v = i*4096/dist */
 for (i=0;i<256;i++)
    {v=(i*step)>>16;
     if (v>=31)
	break;
     fogTable[i]=v;
    }
 for (;i<256;i++)
    fogTable[i]=31;
}

volatile int slaveJob;         /* 0 = draw, 1 = traverse */
/* GCC14: where the slave is, for the freeze report (CRASH.C): 1 waiting for the signal,
   0x10|job the job read, 0x10000|i drawing update-list entry i, 0x20000|k traversing split view
   k.  Written through the cache-through alias, so the master reads it live. */
volatile int slaveStep;
#define SLAVESTEP (*(volatile int *)((int)&slaveStep|0x20000000))
/* GCC14: what the servo measures (drawWallsFinish), in hblank lines (V_BLANK.C htimer): the line
   the slave was kicked on, and the line it finished its share on -- the slave reads htimer and
   writes it through the cache-through alias */
static int kickLine;
static volatile int slaveDone;
#define SLAVEDONE (*(volatile int *)((int)&slaveDone|0x20000000))
#define HTIMER    (*(volatile int *)((int)&htimer|0x20000000))
/* GCC14: the probe of the slave's spare time (overlay row "slv:", STATUSTEXT only), in the same
   hblank lines as `time:`.  The slave has exactly two jobs -- its share of the wall draw, and the
   NEXT image's traversal, started in the tail.  Between the two it does nothing at all while the
   master runs Slave Cmds, Weapon, HUD, Overlay and waits for the VDP1.  Three numbers, because a
   playsim batch moved to the slave would have to fit in the first and leave the third alone:
     idle   from the line the slave finished its share on to the line the traversal was kicked on
     trav   what that traversal then cost the slave
     slack  from the end of the traversal to the master's join -- tail the slave still has free
   A negative slack means the master waited for the traversal (pipe: says the same in spins). */
static volatile int travDoneLine;
#define TRAVDONELINE (*(volatile int *)((int)&travDoneLine|0x20000000))
static volatile int sightDoneLine;
#define SIGHTDONELINE (*(volatile int *)((int)&sightDoneLine|0x20000000))
static int pipeKickLine;
static int slaveKicked;       /* drawWalls really kicked the slave: drawWallsFinish may wait */
int slaveIdle,slaveSight,slaveTrav,slaveSlack;
/* GCC14: htimer is zeroed at the top of the game loop (SRUINS.C), and this probe straddles that:
   the kick is at the end of an image, the slave finishes in the next one.  The first reading of
   it gave trav = -822 and slack = 0 on console (2026-09-23) -- the reset, not a measure.  SRUINS
   leaves the count the image reached here just before zeroing, so a stamp smaller than the kick's
   is simply one image further on. */
int lastFrameLines=1;
static MthMatrix pipeMatrix;   /* copie stable : viewTransform sera depile entre-temps */
static int pipeInFlight;
static int pipeDone;
int pipeSpin=-1;               /* spins at the join; -1 = nothing in flight */

void wallsTraverse(MthMatrix *view,int onSlave);

MthMatrix *slaveView;
void slaveDraw(void)
{int i,first;
 /* flush cache */
 *CACHECNTRL=0x10;
 *CACHECNTRL=0x01;
 nmSlavePolys=0;
 nmSlaveGour=0;
 nmSlaveSectors=0;
 nmSlaveWater=0;
 for (i=slaveDrawStart;i>=0;i--)
    {SLAVESTEP=0x10000|i;
     first=nmSlavePolys;
#if RECTCLIP
     {XyInt q[4];                /* the sector's clip box, EZ_userClip's A and C.  GCC14: this one
				    is written by hand, so it carries the 352 centring by hand too
				    -- EZ_userClip is what adds it everywhere else (SPR.C). */
      q[0].x=updateList[i]->xmin+viewCx+viewOrgOff; q[0].y=updateList[i]->ymin+viewCy;
      q[1].x=q[1].y=q[3].x=q[3].y=0;
      q[2].x=updateList[i]->xmax+viewCx+viewOrgOff; q[2].y=updateList[i]->ymax+viewCy;
      slaveCmdNext(FUNC_UCLIP,0,0,q,NULL);
     }
#endif
     drawSector(updateList[i]-sectorDraw,slaveView,1);
     /* the sector's last command carries the call of its sprites: one of its own, not a water
	command, which carries the call of its surface */
     if (nmSlavePolys==first ||
	 (nmSlaveWater && slaveWaterCmd[nmSlaveWater-1]==nmSlavePolys-1))
	slaveCmdSkip();
     if (nmSlaveSectors<MAXSLAVESECTORS)
	slaveSectorEnd[nmSlaveSectors++]=nmSlavePolys-1;
    }
}

/* water (PowerSlave): each surface, drawn by the master as a subroutine after the block, is
   called from the skipped command the slave left in its place; a skipped jump steps over them.
   The block's DMA has to be over before its commands are patched in VRAM. */
static void slaveWaterSurfaces(int first)
{int j,jump,start,n=EZ_getNextCmdNm()-first;
 struct cmdTable skip;
 memset(&skip,0,sizeof(skip));
 skip.control=SKIP_NEXT;
 jump=EZ_getNextCmdNm();
 EZ_cmd(&skip);
 while (dmaActive());
 for (j=0;j<nmSlaveWater && slaveWaterCmd[j]<n;j++)
    {start=EZ_getNextCmdNm();
     drawWaterSurface(level_wall+slaveWaterWall[j],slaveView,
		      updateList[slaveDrawStart-slaveWaterSector[j]]);
     if (EZ_getNextCmdNm()==start)
	continue;
     EZ_linkCommand(EZ_getNextCmdNm()-1,JUMP_RETURN,0);
     EZ_linkCommand(first+slaveWaterCmd[j],SKIP_CALL,start);
    }
 while (dmaActive());
 EZ_linkCommand(jump,SKIP_ASSIGN,EZ_getNextCmdNm());
}

/* GCC14: the slave's block into the list -- its gouraud tables to the top of the area, the
   tiles mapped, the sprites of its sectors called, then the commands by DMA */
void drawSlaveWalls(void)
{int i,j,s,first;
 struct cmdTable *c;
 /* flush cache */
 *CACHECNTRL=0x10;
 *CACHECNTRL=0x01;
 assert(nmSlavePolys<=MAXNMSLAVEPOLYS);
 assert(nmSlavePolys>=0);
 if (nmSlavePolys==0)
    return;
 EZ_appendGourTop(slaveGour+MAXNMSLAVEPOLYS-nmSlaveGour,nmSlaveGour);
 for (i=0,c=slaveCmd;i<nmSlavePolys;i++,c++)
    {if ((c->control&(CTRL_SKIP|CTRL_FUNC))==FUNC_DISTORSP)
	{int pic=c->color,ch;
	 assert(getPicClass(pic)==TILE16BPP);
	 ch=mapWallPic(pic,c->dummy);
	 if (ch<0)
	    {/* refused by the cache: flat in its tile's first texel, as wallCell paints it */
	     c->control=ZOOM_NOPOINT|DIR_NOREV|FUNC_POLYGON;
	     c->drawMode=UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5|DRAW_GOURAU;
	     c->color=picFirstColour(pic);
	     c->charAddr=0;
	     c->charSize=0;
	    }
	 else
	    {EZ_setCharWindow(c,ch,c->charAddr,c->charSize);
	     c->color=0;
	    }
	 c->dummy=0;
	}
#ifdef WALKPROBE
     /* GCC14: the slave's records carry no class -- they are replayed here, long after the wall
	they came from -- so they go to their own bucket rather than pollute the other three. */
     vdp1Cls=3;
     if (!(c->control&CTRL_SKIP) && (c->control&CTRL_FUNC)!=FUNC_UCLIP)
	VDP1WALK((XyInt *)&c->ax);
#endif
    }
 /* the sprites of each slave sector, drawn by the master meanwhile, after its walls */
 for (j=0,s=slaveDrawStart;j<nmSlaveSectors;j++,s--)
    if (updateList[s]->spriteCommandStart)
       {c=slaveCmd+slaveSectorEnd[j];
	c->control|=JUMP_CALL;
	c->link=updateList[s]->spriteCommandStart<<2;
       }
 first=EZ_appendCmds(slaveCmd,nmSlavePolys);
 if (nmSlaveWater)
    slaveWaterSurfaces(first);
}

void wallRenderSlaveMain(void)
{int job;
 set_imask(0xf);
 *IPRA=0x0000;
 *IPRB=0x0000;
 *TIER=0x01;
 *FTCSR=0x0;      /* GCC14: a kick left pending for the slave this one replaces is not a job */
 while (1)
    {/* wait for sync signal */
     SLAVESTEP=1;
     while (!(*FTCSR & 0x80)) ;
     /* sync */
     *FTCSR=0x0;
     /* Purge BEFORE reading slaveJob: the master just wrote it and the slave's cache
	may still hold the previous job (slaveDraw purges on entry too; harmless). */
     *CACHECNTRL=0x10;
     *CACHECNTRL=0x01;
     job=slaveJob;
     SLAVESTEP=0x10|job;
     if (job==1)
	{/* GCC14: the sight batch rides THIS kick -- it takes none of its own.  A kick that lands
	    while the slave is clearing its capture flag is lost and hangs both chips (the bounded
	    wait in wallsPipeJoin), so a third kick an image is the one thing not to add here. */
	 CFG_SIGHT_BATCH();
	 SIGHTDONELINE=HTIMER;  /* what the batch cost, before the traversal starts */
	 wallsTraverse(&pipeMatrix,1);
	 TRAVDONELINE=HTIMER;   /* the probe's `trav` and `slack`, wallsPipeJoin */
	}
     else
	{slaveDraw();
	 SLAVEDONE=HTIMER;      /* the servo's gap, drawWallsFinish */
	}
     *(Uint16 volatile *)0x21800000=0xffff;
     if (job==2)
	{/* split screen: its share of view 0 is signalled; now the views queued behind it.
	    Each one's end is published through the cache-through alias, the master spins on
	    it (wallsQueueJoin); the FRT signal stays the draw's alone. */
	 int k,n=travQueued;
	 trDC=splitDC;
	 for (k=1;k<=n;k++)
	    {SLAVESTEP=0x20000|k;
	     tr=travSet+k;
	     wallsTraverse(&travSet[k].view,2);
	     TRAVDONE=k;
	    }
	}
    }
}

/* GCC14: the blob under a thing and the spectre, both live: params/doom.cfg gives the first
   value and the pause's OPTIONS -> SHADOWS changes it with the level running.  The flags are
   kept beside the mode so the draw loop reads a word instead of branching per thing. */
int shadowMode=CFG_SHADOW_MODE;
int spectreMode=CFG_SPECTRE_MODE;
int shadowPct=GP_THING_SHADOW_SIZE;
int shadowSpan=(48*GP_THING_SHADOW_SIZE)/100;
CompoPlan shadowPlan,spectrePlan;

/* GCC14: a composition mode is a PLAN of one to three VDP1 passes over the same quad.  One pass
   is all the hardware offers per command -- the colour calculation has no strength parameter --
   so everything beyond a plain half comes from drawing again (VDP1 p.95: "To make the luminance
   one fourth, set the same command table in VRAM twice").  `jit` says a pass takes a random
   one-pixel offset: the mesh is a checkerboard on the FRAMEBUFFER, not on the sprite, so moving
   the quad by one pixel hands the pass the other half of its pixels. */
void compoPlanOf(CompoPlan *p,int mode)
{p->nm=1;
 p->jit=0;
 p->clip=0;
 p->f[0]=p->f[1]=p->f[2]=COMPO_REP;
 switch (mode)
    {case CFG_COMPO_MESH:   p->f[0]=DRAW_MESH; break;
     case CFG_COMPO_SHADOW: p->f[0]=COMPO_SHADOW; break;
     case CFG_COMPO_GRAIN:  p->f[0]=COMPO_SHADOW|DRAW_MESH; break;
     case CFG_COMPO_TRANS:  p->f[0]=COMPO_TRANS; break;
     case CFG_COMPO_TGRAIN: p->f[0]=COMPO_TRANS|DRAW_MESH; break;
     case CFG_COMPO_DARK:   p->nm=2;
			    p->f[0]=p->f[1]=COMPO_SHADOW; break;
     case CFG_COMPO_TBW:    p->nm=2;
			    p->f[0]=COMPO_SHADOW;
			    p->f[1]=DRAW_MESH; break;      /* replace, in its grey bank */
     case CFG_COMPO_FUZZ:   p->nm=3;
			    p->jit=6;           /* passes 1 and 2 are DISPLACED ... */
			    p->clip=6;          /* ... and CUT to a piece of the sprite */
			    p->f[0]=p->f[1]=p->f[2]=COMPO_SHADOW; break;
     default:               break;
    }
}

/* GCC14: the fuzz's own generator.  NOT the game's (getNextRand): the renderer must not consume
   a number the playsim counts on -- the two CPUs and the network read that one.
   FUZZ_SHIFT is how far a displaced pass moves.  One pixel, over the VDP1's mesh, only gave the
   other half of a CHECKERBOARD -- a grid, which is not what a fuzz looks like.  Several pixels,
   on the WHOLE silhouette, makes the three passes overlap in patches shaped like the monster
   itself: the part all three cover takes three shadows, the crescents one or two, and the whole
   thing is redrawn every image.  Doom's own fuzz is a displacement too (r_draw.c fuzzoffset). */
#define FUZZ_SHIFT 5
/* A cut pass is made of FUZZ_PIECES rectangles, each between a quarter and a half of the
   sprite in each axis and placed at random.  ONE big rectangle a pass left two flat halves --
   the thing looked like three sheets laid over one another, which is a shadow, not a fuzz. */
#define FUZZ_PIECES 3
#define FUZZ_MIN   2            /* the smallest piece: the sprite over this, in each axis */
#define FUZZ_SPAN  4            /* ... and at most the sprite over this, plus the minimum */
static unsigned int fuzzSeed=0x13579bdfu;
static int fuzzMod(int n)
{fuzzSeed=fuzzSeed*1103515245u+12345u;
 return n>0? (int)((fuzzSeed>>17)%(unsigned)n): 0;
}
static int fuzzNext(void)
{return fuzzMod(2*FUZZ_SHIFT+1)-FUZZ_SHIFT;
}

/* the plan, drawn: `pos` is the quad (pos[0] its corner, pos[1] its size), restored on the way
   out.  `tl` says pos[0] is the TOP-LEFT (ZOOM_TL) rather than the centre (ZOOM_MM), which is
   what a CUT pass needs to know to build its rectangle.
   A cut pass draws the WHOLE sprite through a user clip that keeps only a piece of it: three
   passes laid over one another darkened the monster evenly, which is a shadow, not a fuzz.  Cut
   -- and displaced -- they leave patches at one, two and three shadows, redrawn every image. */
static void compoDraw(const CompoPlan *p,int zoom,int md,int bk,int pic,XyInt *pos,
		      struct gourTable *g,int tl)
{int i,dx,dy,x0,y0,w,h,cw,ch;
 XyInt cl[2];
 if (tl)
    {x0=pos[0].x; y0=pos[0].y;}
 else
    {x0=pos[0].x-(pos[1].x>>1); y0=pos[0].y-(pos[1].y>>1);}
 w=pos[1].x; h=pos[1].y;
 for (i=0;i<p->nm;i++)
    {dx=dy=0;
     if (p->jit & (1<<i))
	{dx=fuzzNext();
	 dy=fuzzNext();
	 pos[0].x+=dx;
	 pos[0].y+=dy;
	}
     if (p->clip & (1<<i))
	{int k;
	 for (k=0;k<FUZZ_PIECES;k++)
	    {cw=(w/FUZZ_MIN)-fuzzMod(w/FUZZ_SPAN+1);   /* a size of its own, every piece */
	     ch=(h/FUZZ_MIN)-fuzzMod(h/FUZZ_SPAN+1);
	     if (cw<1) cw=1;
	     if (ch<1) ch=1;
	     cl[0].x=x0+fuzzMod(w-cw+1)+viewCx;
	     cl[0].y=y0+fuzzMod(h-ch+1)+viewCy;
	     cl[1].x=cl[0].x+cw;
	     cl[1].y=cl[0].y+ch;
	     if (cl[0].x<0) cl[0].x=0;
	     if (cl[0].y<0) cl[0].y=0;
	     if (cl[1].x>319) cl[1].x=319;   /* GCC14: the PICTURE -- EZ_userClip adds the 352
					        centring after this (SPR.C viewOrgOff) */
	     if (cl[1].y>239) cl[1].y=239;
	     if (cl[1].x<=cl[0].x || cl[1].y<=cl[0].y)
		continue;
	     EZ_userClip(cl);
	     EZ_scaleSpr(zoom,md|p->f[i],bk,pic,pos,g);
	    }
	}
     else
	EZ_scaleSpr(zoom,md|p->f[i],bk,pic,pos,g);
     pos[0].x-=dx;
     pos[0].y-=dy;
    }
 if (p->clip)                           /* the view's own clip back, for everything after */
    {cl[0].x=XMIN+viewCx; cl[0].y=YMIN+viewCy;
     cl[1].x=XMAX+viewCx; cl[1].y=YMAX+viewCy;
     EZ_userClip(cl);
    }
}

void setShadowPct(int pct)
{if (pct<10) pct=10;
 if (pct>200) pct=200;
 shadowPct=pct;
 shadowSpan=(48*pct)/100;
}

/* GCC14: doorwayCache for another slave program while no level runs (Doom's title fire) */
void *slaveScratch(int *size)
{if (size)
    *size=sizeof(doorwayCache);
 return doorwayCache;
}

void startSlave(void *slaveMain)
{volatile Uint8 *SMPC_SF=(Uint8 *)0x20100063;
 volatile Uint8 *SMPC_COM=(Uint8 *)0x2010001f;
 const Uint8 SMPC_SSHON=0x02;
 const Uint8 SMPC_SSHOFF=0x03;
 *TIER=0x01; /* disable master's frt interrupt */
 while ((*SMPC_SF & 0x01)==0x01) ;
 *SMPC_SF=1;
 *SMPC_COM=SMPC_SSHOFF;
 while ((*SMPC_SF & 0x01)==0x01) ;
 SYS_SETSINT(0x94,slaveMain);
 *SMPC_SF=1;
 *SMPC_COM=SMPC_SSHON;
 while ((*SMPC_SF & 0x01)==0x01) ;
}


/* --- GCC14: TRAVERSAL code.  From here to TRAVERSAL END it fills the set `tr` points at, with
   the viewer and window saved in it: in split screen the master draws another view meanwhile,
   with its own. */
#define sectorDraw     (tr->sd)
#define updateList     (tr->ul)
#define updateListSize (tr->ulSize)
#define doorwayCache   trDC
#define viewPos        (tr->pos)
#define viewSector     (tr->sector)
#undef XMIN
#undef YMIN
#undef XMAX
#undef YMAX
#define XMIN           (tr->xmin)
#define YMIN           (tr->ymin)
#define XMAX           (tr->xmax)
#define YMAX           (tr->ymax)

void buildTree(void)
{int update,s,w,adjoin;
 /* each wall of each sector in the update list needs to be marked if
    the adjoining sector is also in the draw list */
 for (update=0;update<updateListSize;update++)
    {s=updateList[update]-sectorDraw;
     for (w=level_sector[s].firstWall;w<=level_sector[s].lastWall;w++)
	{adjoin=level_wall[w].nextSector;
	 if (adjoin==-1)
	    break;
	 if (level_wall[w].flags & WALLFLAG_BLOCKSSIGHT)
	    continue;
	 if (!(sectorDraw[adjoin].flags & SDFLAG_BBVALID))
	    continue;
	 /* each wall is two sided, so we may only do the ancestor->child
	    relationships */

	 assert(sectorDraw[adjoin].flags & SDFLAG_CACHEVALID);
	 if (doorwayCache[w].xmin==-32000)
	    /* this means wall normal points away from eye */
	    continue;

	 /* here we know that this wall represents an arrow from s to
	    adjoin; s is adjoin's child */

	 assert(sectorDraw[adjoin].nmAncestors<MAXFANIN);
	 sectorDraw[adjoin].ancestor[(int)sectorDraw[adjoin].nmAncestors++]=s;
	 sectorDraw[s].nmChildren++;
	}
    }

 /* Two sectors with NO portal between them get no constraint at all above, so only the distance
    scalar orders them -- and that scalar is the distance to the nearest doorway they were reached
    through, not their depth.  On either side of a pillar both doorways are equidistant and the
    order is a coin toss: the floor shows through the pillar's wall.  The pair table
    (tools/ordre.py) names a wall whose plane decides, and the edge joins the SAME graph, so the
    sort composes it with the portal constraints -- unlike level_cutPlane, which reorders after
    the fact and undoes more than it repairs. */
 for (update=0;update<level_nmOrderPairs;update++)
    {int a=level_orderPair[update].a;
     int b=level_orderPair[update].b;
     int plane=level_orderPair[update].plane;
     int front,back;
     sWallType *cutWall;
     MthXyz testV,p;
     if (!(sectorDraw[a].flags & SDFLAG_BBVALID) ||
	 !(sectorDraw[b].flags & SDFLAG_BBVALID))
	continue;
     cutWall=level_wall+(((plane & 0x80)?level_sector[b].firstWall
				       :level_sector[a].firstWall)+(plane & 0x7f));
     getVertex(cutWall->v[0],&testV);
     p.x=viewPos.x-testV.x;
     p.y=viewPos.y-testV.y;
     p.z=viewPos.z-testV.z;
     /* the normal points INTO the wall's own sector, so the viewCamera on that side means that
	sector is the near one */
     if (MTH_Product((Fixed32 *)&p,(Fixed32 *)cutWall->normal)>0)
	front=(plane & 0x80)?b:a;
     else
	front=(plane & 0x80)?a:b;
     back=(front==a)?b:a;
     if (sectorDraw[back].nmAncestors>=MAXFANIN)
	continue;              /* ancestor[] is fixed at MAXFANIN: drop the edge, never smash it */
     sectorDraw[back].ancestor[(int)sectorDraw[back].nmAncestors++]=front;
     sectorDraw[front].nmChildren++;
    }
}


static void sortLeafList(SectorDrawRecord **leafList,int leafListSize)
{int i,j;
 sSectorType *s1,*s2;
 SectorDrawRecord *save;

 /* sort by distance */
 for (i=1;i<leafListSize;i++)
    {save=leafList[i];
     for (j=i;j>0;j--)
	if (leafList[j-1]->distance<save->distance)
	   leafList[j]=leafList[j-1];
	else
	   break;
     if (j!=i)
	leafList[j]=save;
    }

 /* sort by cut plane */
 for (i=0;i<leafListSize;i++)
    {s1=level_sector+(leafList[i]-sectorDraw);
     if (!(s1->flags & SECFLAG_CUTSORT))
	continue;
     /* look thru rest of list for sectors we are on wrong side of */
     for (j=leafListSize-1;j>i;j--)
	{s2=level_sector+(leafList[j]-sectorDraw);
	 if (!(s2->flags & SECFLAG_CUTSORT))
	    continue;
	 if (s1->cutChannel!=s2->cutChannel)
	    continue;
	 {/* sort via cut planes */
	  int plane=(*level_cutPlane)[s1->cutIndex][s2->cutIndex];
	  sWallType *cutWall;
	  MthXyz testV,p;
	  /* if (debugFlag)
	     dPrint(" cut sorting %d & %d\n",
		    s1-level_sector,s2-level_sector); */
	  assert(plane!=99);
	  if (plane&0x80)
	     cutWall=level_wall+(s2->firstWall+(plane&0x7f));
	  else
	     cutWall=level_wall+(s1->firstWall+(plane&0x7f));
	  /* if (debugFlag)
	     {dPrint(" cut wall %d (%d,%d,%d)\n",
		     cutWall-level_wall,
		     cutWall->normal[0],
		     cutWall->normal[1],
		     cutWall->normal[2]);
	     } */
	  getVertex(cutWall->v[0],&testV);
	  p.x=viewPos.x-testV.x;
	  p.y=viewPos.y-testV.y;
	  p.z=viewPos.z-testV.z;
	  if (((plane & 0x80)
	       && MTH_Product((Fixed32 *)&p,(Fixed32 *)cutWall->normal)<0)||
	      (!(plane & 0x80)
	       && MTH_Product((Fixed32 *)&p,(Fixed32 *)cutWall->normal)>0))
	     {/* move s1 so that it is after s2 */
	      int k;
	      save=leafList[i];
	      for (k=i;k<j;k++)
		 leafList[k]=leafList[k+1];
	      leafList[k]=save;
	      break;
	     }
	 }
	}
    }
/* if (debugFlag)
    {for (i=0;i<leafListSize;i++)
	dPrint("%d ",leafList[i]-sectorDraw);
     dPrint("\n");
    } */
}


int slaveSize=1;
/* GCC14: one split per view.  In split screen view 0 runs the game logic between drawWalls and
   drawWallsFinish, views 1.. run nothing: a single servo for all of them never settled, and the
   master waited for the slave (Slave Wait) in every view but the first. */
static int slaveSplit[MPMAX]={1,1,1,1},wallsView;
static signed char splitTrend[MPMAX];   /* the sign of each view's last correction (the servo) */
/* --- GCC14: TRAVERSAL half ----------------------------------------------------------
   Split from the DRAW half so it can run on the slave, in the tail of the PREVIOUS frame
   (WALLPIPE).  Writes only sectorDraw[], updateList[], drawList[], doorwayCache and
   tLightPos; never the VDP1, the tile cache, the overlay counters or plaxBB.
   `onSlave` disables the profiler, which the master owns. */
static SectorDrawRecord *leafList[MAXNMSECTORS];  /* 2.4 KB: off the stack, the slave's
						     is small */

void wallsTraverse(MthMatrix *view,int onSlave)
{int i;
 int done;
 int leafListSize;
 int leafToDraw;
 SectorDrawRecord *sdr;

 /* the light positions are made by drawWalls, on the master (see TravSet) */
 if (!onSlave) CFG_PROF_SUB("Find Visible");

 for (i=0;i<level_nmSectors;i++)
    sectorDraw[i].flags=0;
 assert(viewSector>=0 && viewSector<level_nmSectors);

 sectorDraw[viewSector].xmin=XMIN;
 sectorDraw[viewSector].ymin=YMIN;
 sectorDraw[viewSector].xmax=XMAX;
 sectorDraw[viewSector].ymax=YMAX;
 sectorDraw[viewSector].flags|=SDFLAG_BBVALID;
 /* No portal reaches the viewCamera's sector, so findDoorways never sets its distance:
    without this it kept an old frame's value (read by the leaf sort). */
 sectorDraw[viewSector].distance=0;
 updateListSize=1;
 updateList[0]=sectorDraw+viewSector;
 if (!onSlave) CFG_PROF_SUB("Find Doorways");
 findDoorways(viewSector,view);
 do
    {done=1;
     for (i=0;i<updateListSize;i++)
	{int s=updateList[i]-sectorDraw;
	 if (sectorDraw[s].flags & SDFLAG_NEEDTOPROCESS)
	    {sectorDraw[s].flags&=~SDFLAG_NEEDTOPROCESS;
	     findDoorways(s,view);
	     done=0;
	    }
	}
    }
 while (!done);
 if (!onSlave) CFG_PROF_SUB_END();

#if 0
 for (i=0;i<updateListSize;i++)
    {addDebugLine(updateList[i]->xmin,updateList[i]->ymin,
		  updateList[i]->xmax,updateList[i]->ymin);
     addDebugLine(updateList[i]->xmax,updateList[i]->ymin,
		  updateList[i]->xmax,updateList[i]->ymax);
     addDebugLine(updateList[i]->xmax,updateList[i]->ymax,
		  updateList[i]->xmin,updateList[i]->ymax);
     addDebugLine(updateList[i]->xmin,updateList[i]->ymax,
		  updateList[i]->xmin,updateList[i]->ymin);
    }
#endif

 /* build tree */
 for (i=0;i<updateListSize;i++)
    {updateList[i]->nmAncestors=0;
     updateList[i]->nmChildren=0;
    }
 buildTree();

#if 0
 /* I think I fixed this in findDoorways */

 /* if currentSector ended up with any ancestors (which can only
    happen when we are standing right on a sector boundry)
    then remove them */
 for (i=0;i<sectorDraw[viewSector].nmAncestors;i++)
    sectorDraw[sectorDraw[viewSector].ancestor[i]].nmChildren--;
 sectorDraw[viewSector].nmAncestors=0;
#endif

#if 0
#ifndef NDEBUG
 if (debugFlag)
    {for (i=0;i<updateListSize;i++)
	{dPrint("s:%d dist:%d\n",updateList[i]-sectorDraw,
		updateList[i]->distance);
	 dPrint(" #c=%d ",updateList[i]->nmChildren);
	 for (j=0;j<updateList[i]->nmAncestors;j++)
	    dPrint("%d ",updateList[i]->ancestor[j]);
	 dPrint("\n");
	}
     debugFlag=0;
    }
#endif
#endif

#ifndef NDEBUG
 if (updateListSize>1)
    for (i=0;i<updateListSize;i++)
       assert(updateList[i]->nmChildren>0 || updateList[i]->nmAncestors>0);
 for (i=0;i<updateListSize;i++)
    assert(updateList[i]==&sectorDraw[viewSector] ||
	   updateList[i]->nmAncestors>0);
#endif

 leafListSize=0;
 /* add leaves to leaf list */
 for (i=0;i<updateListSize;i++)
    if (updateList[i]->nmChildren==0)
       leafList[leafListSize++]=updateList[i];

 /* sort list */
 sortLeafList(leafList,leafListSize);

 drawListSize=0;
 /* itterate: choose leaf to draw, add this leaf to draw list and
    remove it from the leaf list, add all the newly created leaves to
    the leaf list */
 while (drawListSize<updateListSize)
    {SectorDrawRecord *breakLeaf=NULL;

     leafToDraw=0;

     if (leafListSize<=0)
	{/* there are still sectors we haven't drawn, we must have a
	    circular dependancy, try to break it */
	 /* this doesn't happen normally */
	 /* ... the freed sector is painted NEXT, so take the FARTHEST: it is the
	    one the others may legitimately paint over.  The number of children
	    ranks nothing.  Required by the pairs buildTree adds: they add edges,
	    edges make cycles, and breaking those by fan-in scrambles the list. */
	 Fixed32 farthest;
	 int i;
	 farthest=-1;
	 for (i=0;i<updateListSize;i++)
	    if (updateList[i]->nmChildren>0 &&
		updateList[i]->distance>farthest)
	       {farthest=updateList[i]->distance;
		breakLeaf=updateList[i];
	       }
	 assert(breakLeaf!=NULL);
	 /* ... remove all breakLeaf's children */
	 /* NOTE: this breaks the tree structure somewhat, as breakLeaf is
	    still referenced by ancestor pointers in other sectors. */
	 breakLeaf->nmChildren=0;
	 leafList[leafListSize++]=breakLeaf;
	 assert(leafListSize>0);
	}
     assert(leafListSize>0);

     /* add leafToDraw to drawList */
     drawList[drawListSize++]=leafList[leafToDraw];
     /* remove leafToDraw from leafList */
     for (i=leafToDraw+1;i<leafListSize;i++)
	leafList[i-1]=leafList[i];
     leafListSize--;
     /* pluck leafToDraw from tree and add new leaves to leaf list */
     for (i=0;i<drawList[drawListSize-1]->nmAncestors;i++)
	{sdr=&(sectorDraw[drawList[drawListSize-1]->ancestor[i]]);
	 if ((--sdr->nmChildren)==0)
	    /* add to leaf list */
	    leafList[leafListSize++]=sdr;
	}

     /* sort list */
     sortLeafList(leafList,leafListSize);
    }
 assert(leafListSize==0); /* crashes here for some damn reason */
 /* temp bandaid */
 updateListSize=drawListSize;
 for (i=0;i<updateListSize;i++)
    updateList[i]=drawList[updateListSize-i-1];

 if (onSlave!=2)                /* 2: split screen, during the logic -- see TravSet */
    CFG_SPRITE_LEAVES(view,&viewPos,onSlave!=0);  /* tr->pos: the camera drawWalls will signal from */
 if (!onSlave) CFG_PROF_SUB_END();
}


#undef sectorDraw
#undef updateList
#undef updateListSize
#undef doorwayCache
#undef viewPos
#undef viewSector
#undef XMIN
#undef YMIN
#undef XMAX
#undef YMAX
#define XMIN        viewXmin
#define YMIN        viewYmin
#define XMAX        viewXmax
#define YMAX        viewYmax
/* --- TRAVERSAL END */

/* Starts NEXT frame's traversal on the slave.  Call at the end of the game loop, with the
   view matrix as the next frame will rebuild it. */
void wallsPipeKick(MthMatrix *view)
{
#if WALLPIPE
 int k;
 char *d=(char *)&pipeMatrix,*s=(char *)view;
 if (pipeInFlight)
    return;                     /* already in flight: never relaunch without a join */
 for (k=0;k<(int)sizeof(MthMatrix);k++)
    d[k]=s[k];
 tr=travSet;                  /* view 0 of the next frame: the caller loaded player 0 */
 trDC=doorwayCache;
 travSetViewer(tr,camera);
 pipeDone=0;
 slaveJob=1;
 pipeInFlight=1;
 slaveIdle=(int)htimer-slaveDone;  /* it has done nothing since it finished its share of the draw */
 pipeKickLine=(int)htimer;
 CFG_SIGHT_SNAP();          /* the marines as they now stand: the slave reads no sprite */
 *(Uint16 volatile *)0x21000000=0xffff;
#endif
}

/* Join then DISCARD: call wherever the camera will still move before the next frame (menu,
   travel question) or the level will go (runLevel exits).  Never leave the game loop with a
   traversal in flight: the slave would read level_vertex[] while the master reloads it. */
void wallsPipeDiscard(void)
{
#if WALLPIPE
 wallsPipeJoin();
 pipeDone=0;
#endif
 wallsQueueJoin();
 travQueued=0;
}

/* Waits for the views queued behind view 0 (split screen).  Call before anything moves the
   geometry (Post), and before the level goes. */
void wallsQueueJoin(void)
{if (!travQueued || !travKicked)
    return;
 while (TRAVDONE<travQueued)
    ;
 travKicked=0;
 *CACHECNTRL=0x10;              /* the slave wrote the sets through normal addresses */
 *CACHECNTRL=0x01;
}

/* Joins the traversal started in the tail.  Call BEFORE drawWalls -- and before freeing the
   level, or the slave reads geometry reloaded under it. */
/* GCC14: kicks the SLAVE lost, and frames given up because of them.  The slave leaves its wait
   and clears its own capture flag one instruction later; a kick that lands in that window is
   gone, and both sides then wait for each other for ever -- a dead console, with the crash
   handler's dump saying `IN ROOT/WALLS/SLAVE WAIT` against `SLAVE JOB 0 STEP 1` (seen
   2026-09-23).  The wait below is bounded instead, and a lost kick costs an image. */
int pipeLost;

void wallsPipeJoin(void)
{
#if WALLPIPE
 int i=0,again=0;
 Uint32 t;
 if (!pipeInFlight)
    {pipeSpin=-1;
     return;
    }
 t=vtimer;
 while (!(*FTCSR & 0x80))
    {i++;
     if (vtimer-t<4)                    /* four fields is far past any traversal */
	   continue;
     t=vtimer;
     if (SLAVESTEP!=1)
	   continue;                       /* it IS working, just slowly: keep waiting */
     /* parked at its wait: it never started this job, so nothing of ours is being written */
     pipeLost++;
     if (++again<=2)
	   {*(Uint16 volatile *)0x21000000=0xffff;      /* wake it again */
	    continue;
	   }
     pipeInFlight=0;                    /* give the image up rather than the game */
     slaveJob=0;
     pipeSpin=-2;
#ifdef STATUSTEXT
     changeMessage("SLAVE KICK LOST");
#endif
     return;
    }
 *FTCSR=0x0;
 /* The slave wrote sectorDraw[], updateList[], drawList[], doorwayCache and tLightPos
    through NORMAL addresses, not the cache-through alias.  The master's cache still holds
    last frame's lines: without this purge it draws with stale clip boxes, order and list
    entries, and a face gets another face's tile. */
 *CACHECNTRL=0x10;
 *CACHECNTRL=0x01;
 pipeInFlight=0;
 slaveJob=0;
 pipeSpin=i;               /* 0 = the traversal fit entirely in the tail */
 {/* every stamp on the same line count as the kick's: one taken after the zeroing is an image
     further on (lastFrameLines) */
  int s=SIGHTDONELINE,t=TRAVDONELINE,now=(int)htimer;
  if (s<pipeKickLine) s+=lastFrameLines;
  if (t<pipeKickLine) t+=lastFrameLines;
  if (now<pipeKickLine) now+=lastFrameLines;
  slaveSight=s-pipeKickLine;             /* what the sight batch cost the slave */
  slaveTrav=t-s;                         /* ... and the traversal after it */
  slaveSlack=now-t;                      /* tail left over once both were done */
 }
#if WALLPIPE>=2
 pipeDone=1;
#endif
#endif
}


/* GCC14: the things' share of Master Draw in the L+R+Y tree -- one pointer, since pushProfile
   finds a child by its id's address */
static char thingsProf[]="Things";

/* GCC14: defined with drawSprites, which follows drawWalls -- the image's file of thing light */
static void thingFrameStart(void);

/* --- GCC14: DRAW half ----------------------------------------------------------------
   Everything that emits VDP1 commands or touches the tile cache stays here, on the
   master, its only writer. */
void drawWalls(int k,MthMatrix *view)
{int i,queued;
 XyInt parms[2];
 int lastWallCmd;
 checkStack();
 /* the last view's slave block may still be on its way to VRAM: its buffer is the traversal's
    scratch and the slave's next block (drawSlaveWalls) */
 while (dmaActive());
 /* split screen: view k>0 was traversed by the slave during view 0 -- drawn from the matrix
    and the viewer it was traversed with.  Otherwise the master traverses it here. */
 queued=(k>0 && k<=travQueued);
 if (queued)
    {wallsQueueJoin();
     view=&travSet[k].view;
     tr=travSet+k;
    }
 else
    {tr=(k>0 && k<=splitSets)? travSet+k: travSet;
     if (k>0 || !pipeDone)
	travSetViewer(tr,camera);
    }
 setViewer(camera);
 viewPos=tr->pos;
 viewSector=tr->sector;

 plaxBBxmin=160;
 plaxBBymin=120;
 plaxBBxmax=-160;
 plaxBBymax=-120;

 slave_plaxBBxmin=160;
 slave_plaxBBymin=120;
 slave_plaxBBxmax=-160;
 slave_plaxBBymax=-120;

 lodFused=0; lodCells=0; lodFlat=0;
 slave_lodFused=0; slave_lodCells=0; slave_lodFlat=0;

 slaveView=view;
 nmPolys=0;
#ifdef WALKPROBE
 if (k==0)
    {/* one count per image, every view: the last one is what the VDP1 is drawing now */
     vdp1PrevWalk=vdp1Walk; vdp1PrevMaxX=vdp1MaxX; vdp1PrevMaxY=vdp1MaxY;
     vdp1Walk=0; vdp1Big=0; vdp1BigWalk=0; vdp1MaxX=0; vdp1MaxY=0; vdp1RotWalk=0;
     vdp1ClsWalk[0]=vdp1ClsWalk[1]=vdp1ClsWalk[2]=vdp1ClsWalk[3]=0;
    }
#endif
 autoTarget=NULL;
 bestAutoAimRating=INT_MAX;

 if (queued)
    CFG_SPRITE_LEAVES(view,&camera->pos,0);  /* the sprites as they are now, on the master
					     (TravSet); SIGNAL_VIEW sees the camera as it is now,
					     not as the traversal saw it */
 else
    {trDC=doorwayCache;
#if WALLPIPE>=2
     if (k>0 || !pipeDone)  /* the slave did not do it in the tail: do it here */
#endif
	wallsTraverse(view,0);
    }
 if (k==0)
    {pipeDone=0;
     thingFrameStart();                 /* a new image: the things' light is worked out once */
    }
 /* draw from the set just made */
 sectorDraw=tr->sd;
 updateList=tr->ul;
 updateListSize=tr->ulSize;
 for (i=0;i<nmLights;i++)
    {MthXyz lp=lightSource[i]->pos;
     MTH_CoordTrans(view,&lp,tLightPos+i);
     wLightPos[i][0]=f(lp.x);
     wLightPos[i][1]=f(lp.y);
     wLightPos[i][2]=f(lp.z);
     lightLeaf[i]=(short)lightSource[i]->s;
    }

 slaveSize=slaveSplit[k];
 wallsView=k;
 if (slaveSize>updateListSize-1)
    slaveSize=updateListSize-1;
 slaveDrawStart=slaveSize;
 slaveGourTop=EZ_gourTop();
 /* start slave: its share, then -- view 0 in split screen -- the views queued */
 slaveJob=(k==0 && travQueued)? 2: 0;
 if (slaveJob==2)
    {TRAVDONE=0;
     travKicked=1;
    }
 kickLine=htimer;
 *(Uint16 volatile *)0x21000000=0xffff; slaveKicked=1; CFG_PROF("Master Draw");
 for (i=updateListSize-1;i>slaveDrawStart;i--)
    {parms[0].x=updateList[i]->xmin+viewCx;parms[0].y=updateList[i]->ymin+viewCy;
     parms[1].x=updateList[i]->xmax+viewCx;parms[1].y=updateList[i]->ymax+viewCy;
#if RECTCLIP
     EZ_userClip(parms);
#endif
     drawSector(updateList[i]-sectorDraw,view,0);
     CFG_PROF(thingsProf); drawSprites(&(viewPos),view,updateList[i]-sectorDraw); CFG_PROF_END();
    }

 lastWallCmd=EZ_getNextCmdNm()-1;
 /* draw sprites in slave rendered sectors */
 for (;i>=0;i--)
    {int last=EZ_getNextCmdNm();
     CFG_PROF(thingsProf); drawSprites(&(viewPos),view,updateList[i]-sectorDraw); CFG_PROF_END();
     if (EZ_getNextCmdNm()!=last)
	{EZ_linkCommand(EZ_getNextCmdNm()-1,JUMP_RETURN,0);
	 updateList[i]->spriteCommandStart=last;
	}
     else
	updateList[i]->spriteCommandStart=0;
    }
 EZ_linkCommand(lastWallCmd,JUMP_ASSIGN,EZ_getNextCmdNm()); CFG_PROF_END();
}


void drawWallsFinish(void)
{int i,arrive,dir;
 /* GCC14: the automap draws INSTEAD of the view (CFG_MAP_HIDES_VIEW, SRUINS.C), so drawWalls --
    and with it the slave's kick -- never ran.  This function waited for a signal nobody would
    ever send: pressing X in solo froze the game, master `IN ROOT/WALLS/SLAVE WAIT` against
    `SLAVE JOB 0 STEP 1`, the slave parked at its own wait (console 2026-09-23).  Nothing below
    means anything without that draw; the light list still has to age. */
 if (!slaveKicked)
    {updateLights();
     return;
    }
 slaveKicked=0;
 /* wait for slave to finish */
 arrive=htimer;
 i=0; CFG_PROF("Slave Wait");
 while (!(*FTCSR & 0x80))
    i++;
 /* sync */
 *FTCSR=0x0; CFG_PROF_END();
 /* GCC14: the servo moved the split one sector an image: after a turn the master waited 6-14 ms
    an image for the slave, image after image (split screen, console 2026-09-18).  A gap that
    keeps its sign from one image to the next is now closed by a share of itself: half of it is
    the work to move, and the slave's lines over its sectors price a sector.  That is the slave's
    average, and it holds the nearest, heaviest sectors: the step falls short of the boundary
    rather than past it.  A gap that changes sign -- view 0's logic, one tic or two -- keeps the
    one-sector step. */
 dir=(i>100)? -1: (i<100)? 1: 0;
 if (dir)
    {int step=1,busy=SLAVEDONE-kickLine,gap=SLAVEDONE-arrive;
     if (dir==splitTrend[wallsView] && busy>0)
	{step=(abs(gap)*(slaveSize+1))/(busy<<1);
	 if (step<1)
	    step=1;
	}
     splitTrend[wallsView]=dir;
     slaveSize+=dir*step;
     if (slaveSize<0)
	slaveSize=0;
     if (slaveSize>50)
	slaveSize=50;
    }
 slaveSplit[wallsView]=slaveSize;
 CFG_PROF("Slave Cmds"); drawSlaveWalls(); CFG_PROF_END();
#ifndef NDEBUG
 drawDebugLines();
#endif
 updateLights();
 /* The slave's share of the LOD counters.  Same rule as the plax box below: written through
    normal addresses, and drawSlaveWalls purged the master's cache before this point. */
 lodFused+=slave_lodFused;
 lodCells+=slave_lodCells;
 lodFlat+=slave_lodFlat;
 /* merge slave and master plax bbs */
 if (slave_plaxBBxmin<plaxBBxmin)
    plaxBBxmin=slave_plaxBBxmin;
 if (slave_plaxBBymin<plaxBBymin)
    plaxBBymin=slave_plaxBBymin;
 if (slave_plaxBBxmax>plaxBBxmax)
    plaxBBxmax=slave_plaxBBxmax;
 if (slave_plaxBBymax>plaxBBymax)
    plaxBBymax=slave_plaxBBymax;
 if (plaxBBxmin<XMIN)
    plaxBBxmin=XMIN;
 if (plaxBBymin<YMIN)
    plaxBBymin=YMIN;
 if (plaxBBxmax>XMAX)
    plaxBBxmax=XMAX;
 if (plaxBBymax>YMAX)
    plaxBBymax=YMAX;
}

#if 0
static short internalHead[MAXNMSECTORS];
static short internalList[MAXNMSECTORS];
static init=0;
void initInternalList(void)
{int s,w,i;
 int list,nm;
 char already[MAXNMSECTORS];
 list=0;
 for (s=0;s<level_nmSectors;s++)
    {internalHead[s]=list;
     nm=0;
     for (i=0;i<MAXNMSECTORS;i++)
	already[i]=0;
     for (w=level_sector[s].firstWall;
	  w<=level_sector[s].lastWall;
	  w++)
	if ((level_wall[w].flags & WALLFLAG_MOBILE) &&
	    level_wall[w].nextSector!=-1 &&
	    !already[level_wall[w].nextSector])
	   {internalList[list++]=level_wall[w].nextSector;
	    already[level_wall[w].nextSector]=1;
	    nm++;
	   }
     if (nm==0)
	internalHead[s]=-1;
     else
	{internalList[list++]=-1;
	}
     assert(list<MAXNMSECTORS);
    }
}
#endif

/* returns 1 if clipped out */
int frustumClip(Fixed32 *p1,Fixed32 *p2,int dx,int dy,int dz,int neg)
{Fixed32 t,denom;
 Fixed32 delX,delY,delZ;
 Fixed32 *badPoint;
 /* see if p2 is on wrong side of plane */
 delX=p2[dx]-p1[dx];
 delY=p2[dy]-p1[dy];
 badPoint=NULL;
 if (!neg)
    {if (p2[dx]>p2[dy])
	{badPoint=p2;
	}
     if (p1[dx]>p1[dy])
	{if (badPoint)
	    return 1;
	 badPoint=p1;
	}
     if (!badPoint)
	return 0;

     denom=delX-delY;
     if (abs(denom)<10)
	return 0;
     t=MTH_Div(p1[dy]-p1[dx],denom);
    }
 else
    {if (p2[dx]<-p2[dy])
	{badPoint=p2;
	}
     if (p1[dx]<-p1[dy])
	{if (badPoint)
	    return 1;
	 badPoint=p1;
	}
     if (!badPoint)
	return 0;
     denom=delX+delY;
     if (abs(denom)<10)
	return 0;
     t=MTH_Div(-p1[dy]-p1[dx],denom);
    }
 delZ=p2[dz]-p1[dz];
 badPoint[dx]=p1[dx]+MTH_Mul(t,delX);
 if (neg)
    badPoint[dy]=-badPoint[dx];
 else
    badPoint[dy]=badPoint[dx];
 badPoint[dz]=p1[dz]+MTH_Mul(t,delZ);
 return 0;
}

/* GCC14: a scaled sprite (ZOOM_TL: corner pos[0], size pos[1]) is held to the VDP1's range like
   a cell (WALLASM.H vdp1Range).  A chunk is at most ~1000 px wide at CFG_SPRITE_NEARCLIP, so one
   past the range lies off the view but for the edge of a very near one: clamped, the pattern
   squeezed.  A chunk wholly off the view is not sent at all. */
static int sprRect(XyInt *pos)
{int x0=pos[0].x,y0=pos[0].y,x1=x0+pos[1].x,y1=y0+pos[1].y;
 if (x0>viewXmax || x1<viewXmin || y0>viewYmax || y1<viewYmin)
    return 0;
 if (x0<-VDP1LIM) x0=-VDP1LIM;
 if (y0<-VDP1LIM) y0=-VDP1LIM;
 if (x1> VDP1LIM) x1= VDP1LIM;
 if (y1> VDP1LIM) y1= VDP1LIM;
 pos[0].x=x0; pos[0].y=y0;
 pos[1].x=x1-x0; pos[1].y=y1-y0;
 return 1;
}

/* GCC14: what a thing's light is made of does not depend on the camera -- the darkness of the leaf
   it stands in, and the dynamic lights that reach its feet.  In split screen the master worked both
   out again for every view that drew the thing, three times over in 4 players.  They are found once
   per IMAGE here and filed under the sprite, direct mapped: a collision simply works them out
   again, and the stamp empties the whole file when the image changes (drawWalls, view 0).  Solo has
   one view, so nothing is filed and nothing is read. */
#define DOOM_LIT_MEMO 64
typedef struct
{unsigned short stamp;
 short spr;
 short dark;                            /* leafDark, -1 = not worked out this image              */
 short best;                            /* strongest channel of the dynamic lights, -1 = idem    */
} DoomLitMemo;
static DoomLitMemo litMemo[DOOM_LIT_MEMO];
static unsigned short litStamp;

static void thingFrameStart(void)       /* drawWalls, before view 0: a new image */
{if (!++litStamp)
    litStamp=1;                         /* 0 is the empty file */
}

static DoomLitMemo *litMemoFor(Sprite *o)
{DoomLitMemo *m;
 if (mpPlayers<2)
    return NULL;                        /* one view: it would be written and never read */
 m=litMemo+((o-sprites)&(DOOM_LIT_MEMO-1));
 if (m->stamp!=litStamp || m->spr!=(short)(o-sprites))
    {m->stamp=litStamp;
     m->spr=(short)(o-sprites);
     m->dark=-1;
     m->best=-1;
    }
 return m;
}

/* The strongest channel the dynamic lights give this thing at its feet, measured in world space
   (lightApplyW) so every view of the image shares the one answer. */
static int thingLit(Sprite *o,MthXyz *feet)
{DoomLitMemo *m=litMemoFor(o);
 int li,lr,lg,lb,best=0;
 if (m && m->best>=0)
    return m->best;
 for (li=0;li<nmLights;li++)
    {if (!level_maySee(lightLeaf[li],o->s))
	continue;                       /* as the walls: it would only shine through a wall */
     lr=lg=lb=0;
     lightApplyW(li,feet,&lr,&lg,&lb);
     if (lr>best) best=lr;
     if (lg>best) best=lg;
     if (lb>best) best=lb;
    }
 if (m)
    m->best=(short)best;
 return best;
}

#if CFG_THING_LEAFLIGHT
/* GCC14: how much darker than full light (16) leaf s is, 0..16, in the walls' 5-bit units -- what
   its walls carry.  doom2ps gives every wall of a leaf its sector's light and setSectorBrightness
   (AICOMMON.C) rewrites them all alike, so any one of them says it (measured: one value per leaf
   on all 3 207 leaves of the E1M1-E1M9 disc of 2026-09-21).  Read from the end of the list, where
   doom2ps puts the floor and the ceiling: one wall read, two under a sky.  A portal or a sky
   carries no light.  Read each time rather than cached: the walls' own numbers, no state. */
static int leafDark(int s)
{int w,l;
 const sWallType *wall;
 assert(s>=0 && s<level_nmSectors);
 for (w=level_sector[s].lastWall;w>=level_sector[s].firstWall;w--)
    {wall=level_wall+w;
     if (wall->flags & (WALLFLAG_INVISIBLE|WALLFLAG_PARALLAX))
	continue;
     l=(wall->flags & WALLFLAG_PARALLELOGRAM)? level_vertexLight[wall->firstLight]:
					       level_vertex[wall->firstVertex].light;
     if (l>=16)
	return 0;
     if (l<=0)
	return 16;
     return 16-l;
    }
 return 0;
}

/* leafDark, kept for the image (litMemo): the leaf's walls do not move between two views. */
static int thingDark(Sprite *o)
{DoomLitMemo *m=litMemoFor(o);
 int d;
 if (m && m->dark>=0)
    return m->dark;
 d=leafDark(o->s);
 if (m)
    m->dark=(short)d;
 return d;
}
#endif

void drawSprites(MthXyz *playerPos,MthMatrix *view,int sector)
{Sprite *o;
 Sprite *drawList[100];
 int drawKey[100];              /* GCC14: their sort keys, made once */
 int nmDraw,draw;
 int chunk,light,x,y,i,j;
 int spriteFog=0,spriteBank=0;
 int flip;
 int frame,sqn;
 const short *sq;               /* GCC14: the sequence tables o->sequence reads (SEQUENCE.H) */
 const sFrameType *fr;
 const sChunkType *ch;
 Fixed32 width64,scale;
 MthXyz tformed,feetPos;
 XyInt pos[4];
 XyInt feetScreenPos;

#if 0
 if (!init)
    {initInternalList();
     init=1;
    }
#endif

 nmDraw=0;
 assert(sector>=0 && sector<level_nmSectors);

 assert(viewCamera->sequence==-1);
 for (o=CFG_SPR_FIRST(sector);nmDraw<100 && o;o=CFG_SPR_NEXT(o))
    if (o->sequence!=-1 && !(o->flags & SPRITEFLAG_INVISIBLE))
       {drawKey[nmDraw]=f(abs(o->pos.x-playerPos->x))+f(abs(o->pos.y-playerPos->y))+
			f(abs(o->pos.z-playerPos->z));   /* GCC14: the sort key, once per thing */
	drawList[nmDraw++]=o;
       }

#if 0
 if (internalHead[sector]!=-1)
    {assert(0);
     for (i=internalHead[sector];internalList[i]!=-1;i++)
	{for (o=sectorSpriteList[internalList[i]];o;o=o->next)
	    drawList[nmDraw++]=o;
	}
    }
#endif

 assert(nmDraw<100);

 if (nmDraw==0)
    return;

 /* sort -- GCC14: on the keys made above; each comparison rebuilt two of them, six absolute
    differences, at every step of the insertion */
 for (i=1;i<nmDraw;i++)
    for (j=i;j>0;j--)
       {Sprite *swap;
	int d;
	if (drawKey[j-1]<drawKey[j])
	   {swap=drawList[j];
	    drawList[j]=drawList[j-1];
	    drawList[j-1]=swap;
	    d=drawKey[j];
	    drawKey[j]=drawKey[j-1];
	    drawKey[j-1]=d;
	   }
	else
	   break;
       }

/* assert(nmDraw<=1);*/

#if RECTCLIP
 pos[0].x=XMIN+viewCx; pos[0].y=YMIN+viewCy;
 pos[1].x=XMAX+viewCx; pos[1].y=YMAX+viewCy;
 EZ_userClip(pos);
#endif

 for (draw=0;draw<nmDraw;draw++)
    {o=drawList[draw];
     assert(o->sequence>=0);
#if 0
     if (o->flags & OBJECTFLAG_PUSHBLOCK)
	{/* draw push block */
	 int w;
	 sPBType *pb=level_pushBlock+o->sequence;
	 sWallType *theWall;
	 for (w=pb->startWall;w<=pb->endWall;w++)
	    {theWall=level_wall+level_PBWall[w];
	     renderWall(theWall,view,sector);
	    }
	 continue;
	}
#endif
     if (o->flags & SPRITEFLAG_LINE)
	{/* draw line */
	 MthXyz p1,p2;
	 int width;
	 MTH_CoordTrans(view,&o->pos,&p1);
	 feetPos.x=o->angle;
	 feetPos.y=o->scale;
	 feetPos.z=o->frame;
	 MTH_CoordTrans(view,&feetPos,&p2);
	 /* p1 & p2 hold view space endpoints of line */
	 if (o->flags & SPRITEFLAG_THINLINE)
	    {if (frustumClip((Fixed32 *)&p1,(Fixed32 *)&p2,0,2,1,1) ||
		 frustumClip((Fixed32 *)&p1,(Fixed32 *)&p2,0,2,1,0) ||
		 frustumClip((Fixed32 *)&p1,(Fixed32 *)&p2,1,2,0,0) ||
		 frustumClip((Fixed32 *)&p1,(Fixed32 *)&p2,1,2,0,1))
		continue;
	     project_point(&p1,pos);
	     project_point(&p2,pos+1);

	     EZ_line(UCLPIN_ENABLE|COMPO_TRANS|ECDSPD_DISABLE|COLOR_5,
		     RGB(abs(laserColor-31),
			 15,
			 31-abs(laserColor-31))
		     /*o->color*/,
		     pos,NULL);
	     continue;
	    }
	 if (p1.z<F(32) || p2.z<F(32))
	    continue;
	 project_point(&p1,pos);
	 project_point(&p2,pos+1);
	 width=f(MTH_Div(2*F(focalDist),p2.z));
	 pos[2].x=pos[1].x;
	 pos[2].y=pos[1].y-width;
	 pos[3].x=pos[0].x;
	 pos[3].y=pos[0].y-width;
	 if (vdp1Range(pos))
	    EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,o->color,pos,NULL);
	 continue;
	}
     if (!CFG_VIEW_CULL && o->owner)
	signalObject(o->owner,SIGNAL_VIEW,0,0);   /* PowerSlave: its AI acts on this signal */
     feetPos.x=o->pos.x;
     feetPos.y=o->pos.y-o->radius;
     if (mpIsPlayer(o))            /* GCC14: a player's pos is its eye, which hovers (SPR_HOVER) */
	feetPos.y-=SPR_HOVER(o);
     feetPos.z=o->pos.z;
     MTH_CoordTrans(view,&feetPos,&tformed);
     if (tformed.z<CFG_SPRITE_NEARCLIP)
	continue;
     /* GCC14: and off the sides, where sprRect drops every chunk it would make (the test
	doom_spriteLeaves uses).  Before the view signal, the fog, the lights and the shadow:
	a thing in a drawn leaf but out of the frame paid all of them.  Its one-image flash is
	consumed here, as it was when the chunks were dropped one by one.  Only where the signal
	picks a rotation and nothing else (CFG_VIEW_CULL). */
     if (CFG_VIEW_CULL &&
	 (f(abs(tformed.x))>(f(tformed.z)<<2)+256 || f(abs(tformed.y))>(f(tformed.z)<<2)+256))
	{o->flags&=~SPRITEFLAG_FLASH;
	 continue;
	}
     if (CFG_VIEW_CULL && o->owner)
	signalObject(o->owner,SIGNAL_VIEW,0,0);
     /* if (tformed.z>FARCLIP)
	continue; */

     /* light=tformed.z>>((16+FARCLIP2)-3);
     if (light>NMOBJECTPALLETES-1) light=NMOBJECTPALLETES-1; */
     light=0;

     if (o->flags & SPRITEFLAG_FLASH)
	{light=NMOBJECTPALLETES;
	 o->flags&=~SPRITEFLAG_FLASH;
	}
     /* GCC14: depth fog on things.  Same table as the walls (setFog), softened by a quarter so a
	monster stays readable a little further than its surroundings. */
     {int d=tformed.z>>24;
      if (d>255) d=255;
      if (d<0) d=0;
      spriteFog=fogTable[d];
      spriteFog-=spriteFog>>2;
#if CFG_THING_LEAFLIGHT
      /* GCC14: and the light of the leaf it stands in (o->s, where its centre is: the leaf that
	 DRAWS it may be a later one, doom_spriteLeaves), on top of the distance's share, as a
	 wall takes both.  The darkest bank stays SPRITEFOGMAX, a third of the way down: in a room
	 gone black a thing is dim, not black -- enough, and the fog keeps its look.  The dynamic
	 lights below still lift it.  A FULLBRIGHT frame keeps the distance's share only, as Doom
	 gives it colormap 0. */
      if (!(o->flags & SPRITEFLAG_FULLBRIGHT))
	 spriteFog+=thingDark(o);
      if (spriteFog>SPRITEFOGMAX)
	 spriteFog=SPRITEFOGMAX;
#endif
      /* Doom's thing tiles are TILE8BPP (PIC_SLOTS), i.e. colour-bank mode, where gouraud shifts the
	 palette INDEX, not the RGB (HW measure, saturn-refs/knowledge/HW_VDP1.md:783): noise on
	 PLAYPAL.  A darkened bank is the only way, as Lobotomy planned (the commented line above). */
      /* GCC14: a dynamic light reduces the fog before the bank is chosen (strongest channel,
	 same maths as the walls).  Measured at the feet; bank 0 is the ceiling. */
      if (nmLights)
	 {spriteFog-=thingLit(o,&feetPos);
	  if (spriteFog<0)
	     spriteFog=0;
	 }
      spriteBank=(spriteFog*(nmObjectFogBanks-1))/SPRITEFOGMAX;
      if (spriteBank>nmObjectFogBanks-1)
	 spriteBank=nmObjectFogBanks-1;
      /* GCC14: in the heart of a nukage room a thing takes the green too (PIC.C buildTintBank,
	 SECFLAG_TINT_CORE).  No distance fog on it then -- the bank IS its colour. */
      if (level_sector[o->s].flags & SECFLAG_TINT_CORE)
	 spriteBank=tintBank;
      /* another player's body wears its own colours (MPLAYER.C mpSetBanks): one bank, no fog */
      if (mpPlayers>1)
	 {int k=mpIndexOfSprite(o);
	  if (k>=0 && mpBank[k])
	     spriteBank=mpBank[k];
	 }
      /* GCC14: the spectre wears its own bank -- grey, and darker than the fog ever goes
	 (PIC.C buildSpectreBank).  No distance fog on it: it is meant to read as a shape, and
	 the fog banks stop a third of the way down because a monster must stay readable. */
      if (o->flags & SPRITEFLAG_MESH)
	 {spriteBank=spectreBank;
	  spriteFog=SPRITEFOGMAX;              /* the 16bpp path shades by hand, below */
	 }
     }
     /* tformed is center of sprite */
     project_point(&tformed,&feetScreenPos);
     scale=o->scale;
     scale=MTH_Div(scale*focalDist,tformed.z);
     if (o->flags & SPRITEFLAG_NOSCALE)
	scale=65536;
     /* if (o->flags & SPRITEFLAG_32x32)
	width64=scale>>11;
	else */
     width64=scale>>10;

     if (o->owner && o->owner->class==CLASS_MONSTER &&
	 feetScreenPos.x<20 && feetScreenPos.x>-20)
	{/* object is an autoaiming candidate */
	 int rate;
	 rate=f(tformed.z)+abs(feetScreenPos.y<<4)+abs(feetScreenPos.x<<4);
	 if (rate<bestAutoAimRating)
	    {bestAutoAimRating=rate;
	     autoTarget=o;
	    }
	}
     /* draw shadow -- GCC14: not under a thing the tile cache's bar leaves out (PIC.H
	mapSpritePic): alone, it would mark a monster that is not drawn */
     /* GCC14: a see-through thing casts none.  A spectre is not a body, and the engine's blob
	under one read as a solid disc under something you can see through. */
     if (!(o->flags & (SPRITEFLAG_NOSHADOW|SPRITEFLAG_MESH)) && shadowMode!=CFG_COMPO_NONE &&
	 width64>=picSpriteLod && tformed.z<=CFG_SHADOW_DIST)
	{Fixed32 shadowHeight;
	 Fixed32 shadowWidth;
	 Fixed32 shadowScale;
	 XyInt shadowScreenPos;
	 MthXyz shadowPos;
	 int sh;
	 shadowPos.x=feetPos.x;
	 shadowPos.z=feetPos.z;
	 shadowPos.y=feetPos.y-findFloorDistance(o->s,&feetPos);
	 shadowScale=(F(128)-abs(shadowPos.y-feetPos.y))>>7;
	 if (shadowScale>0)
	    {MTH_CoordTrans(view,&shadowPos,&tformed);
	     if (tformed.z>F(32) && tformed.z<FARCLIP)
		{project_point(&tformed,&shadowScreenPos);
		 shadowScale=MTH_Mul(shadowScale,scale);
		 shadowWidth=shadowSpan*shadowScale;
		 shadowHeight=shadowSpan*shadowScale;

		 shadowHeight=MTH_Mul(shadowHeight,
				      MTH_Div(abs(shadowPos.y-playerPos->y),
					      tformed.z));
		 pos[0].x=shadowScreenPos.x;
		 pos[0].y=shadowScreenPos.y;
		 pos[1].x=f(shadowWidth);
		 pos[1].y=f(shadowHeight);
		 assert(getPicClass(0)!=TILEVDP);
		 /* GCC14: UCLPIN_ENABLE -- without it the VDP1 ignores the user clip, and in split
		    screen a shadow belonging to one view was painted across another.  The view test
		    drops it before the command exists: what the traversal kept is the thing, not
		    the ground under it.  Centred, so past VDP1LIM it is wholly off (<= 240 wide). */
		 if (pos[0].x+(pos[1].x>>1)>=viewXmin && pos[0].x-(pos[1].x>>1)<=viewXmax &&
		     pos[0].y+(pos[1].y>>1)>=viewYmin && pos[0].y-(pos[1].y>>1)<=viewYmax &&
		     abs(pos[0].x)+(pos[1].x>>1)<=VDP1LIM &&
		     abs(pos[0].y)+(pos[1].y>>1)<=VDP1LIM &&
		     (sh=mapSpritePic(0,PIC_ALWAYS))>=0)
		    compoDraw(&shadowPlan,ZOOM_MM,UCLPIN_ENABLE|COLOR_4,0,sh,pos,NULL,0);
		}
	    }
	}
     if (o->flags & SPRITEFLAG_FOOTCLIP)
	{if (feetScreenPos.y<sectorDraw[sector].ymax)
	    {pos[0].x=sectorDraw[sector].xmin+viewCx;
	     pos[0].y=sectorDraw[sector].ymin+viewCy;
	     pos[1].x=sectorDraw[sector].xmax+viewCx;
	     pos[1].y=feetScreenPos.y+viewCy;
	     EZ_userClip(pos);
	    }
	}

     /* draw sprite */
     /* GCC14: a sequence past the level's is the game's own (SEQUENCE.H extra_*) */
     sq=level_sequence; fr=level_frame; ch=level_chunk; sqn=o->sequence;
     if (sqn>=level_nmSequences)
	{sqn-=level_nmSequences; sq=extra_sequence; fr=extra_frame; ch=extra_chunk;}
     frame=o->frame+sq[sqn];
     for (chunk=fr[frame].chunkIndex;
	  chunk<fr[frame+1].chunkIndex;
	  chunk++)
	{x=ch[chunk].chunkx;
	 y=ch[chunk].chunky;
	 pos[0].x=feetScreenPos.x+f(scale*x);
	 pos[0].y=feetScreenPos.y+f(scale*y);
	 pos[1].x=width64;
	 pos[1].y=width64;
	 flip=0;
	 if (ch[chunk].flags & 1)
	    flip|=DIR_LRREV;
	 if (ch[chunk].flags & 2)
	    flip|=DIR_TBREV;
#if 0
	 {XyInt p[4];
	  int r=f(MTH_Mul(o->radius,scale));
	  p[0].x=feetScreenPos.x-r;
	  p[0].y=feetScreenPos.y-r;
	  p[1].x=feetScreenPos.x+r;
	  p[1].y=feetScreenPos.y-r;
	  p[2].x=feetScreenPos.x+r;
	  p[2].y=feetScreenPos.y+r;
	  p[3].x=feetScreenPos.x-r;
	  p[3].y=feetScreenPos.y+r;
	  EZ_polygon(ECDSPD_DISABLE|COMPO_REP|COLOR_5,0xffff,p,NULL);
	 }
#endif

	 assert(getPicClass(ch[chunk].tile!=TILEVDP));
	 /* GCC14: the tile is asked for once sprRect has kept the chunk: no slot for what is off the
	    view, and the tile cache's bar ranks only what is drawn (PIC.H mapSpritePic, -1 = left out) */
	 {int pic;

	  i=getPicClass(ch[chunk].tile);
	  if (i!=TILE8BPP && i!=TILESMALL8BPP)
	     {struct gourTable gtable;
	      if (i==TILESMALL16BPP)
		 {pos[1].x>>=1;
		  pos[1].y>>=1;
		 }
	      if (o->flags & SPRITEFLAG_COLORED)
		 gtable.entry[0]=o->color;
	      else
		 {if (light==NMOBJECTPALLETES)
		     gtable.entry[0]=RGB(31,31,31);
		 else
		    {int g=16-light*2-spriteFog;   /* softened fog, see above */
		     if (g<0) g=0;
		     gtable.entry[0]=worldGrey[g];
		    }
		 }
	      gtable.entry[1]=gtable.entry[0];
	      gtable.entry[2]=gtable.entry[0];
	      gtable.entry[3]=gtable.entry[0];
	      if (sprRect(pos) && (pic=mapSpritePic(ch[chunk].tile,width64))>=0)
		 {int md=UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU;
		  if (o->flags & SPRITEFLAG_MESH)
		     compoDraw(&spectrePlan,ZOOM_TL|flip,md,0,pic,pos,&gtable,1);
		  else
		     EZ_scaleSpr(ZOOM_TL|flip,md,0,pic,pos,&gtable);
		 }
	     }
	  else
	     {if (i==TILESMALL8BPP)
		 {pos[1].x>>=1;
		  pos[1].y>>=1;
		 }
	      if (sprRect(pos) && (pic=mapSpritePic(ch[chunk].tile,width64))>=0)
		 {int md=UCLPIN_ENABLE|COLOR_4|HSS_ENABLE|ECD_DISABLE;
		  int bk=(light? light: spriteBank)<<8;     /* light = muzzle flash */
		  if (o->flags & SPRITEFLAG_MESH)
		     compoDraw(&spectrePlan,ZOOM_TL|flip,md,bk,pic,pos,NULL,1);
		  else
		     EZ_scaleSpr(ZOOM_TL | flip,md,bk,pic,pos,NULL);
		 }
	     }
	 }

	}
     /* done drawing sprite */
     if (o->flags & SPRITEFLAG_FOOTCLIP)
	{/* pos[0].x=sectorDraw[sector].xmin+viewCx;
	    pos[0].y=sectorDraw[sector].ymin+120;
	    pos[1].x=sectorDraw[sector].xmax+viewCx;
	    pos[1].y=sectorDraw[sector].ymax+120; */
	 pos[0].x=XMIN+viewCx; pos[0].y=YMIN+viewCy;
	 pos[1].x=XMAX+viewCx; pos[1].y=YMAX+viewCy;
	 EZ_userClip(pos);
	}
    }
}


void initWallRenderer(void)
{compoPlanOf(&shadowPlan,shadowMode);   /* GCC14: the two live composition modes */
 compoPlanOf(&spectrePlan,spectreMode);
 setShadowPct(shadowPct);
 lightInit();
}

#ifdef GP_GAME_DOOM
/* Which leaf draws a sprite (CFG_SPRITE_LEAVES, CFG_SPR_FIRST/NEXT).  The painter draws a sprite
   right after the leaf holding its centre, and every leaf drawn later paints over it, floors as
   well as walls: there is no depth buffer.  A Doom map is cut into many small BSP leaves (E1M1:
   219), and the converter paints whole 64-unit squares of floor across the chords of a Doom
   sector (doom3d.sols_pleins): on the disc of 2026-09-21, 2 957 of the episode's 25 045 floor
   faces stick out of their leaf by more than 2 u, 32 u at the median and 84 at most.  The
   neighbour drawn next paints the very tile the monster stands on over its feet (console,
   2026-09-21).  Doom draws every floor before any sprite.
   The first promotion moved a sprite to the latest-drawn flush neighbour within one radius plus
   the spill, looking only at the portal it crossed: it jumped whole leaves, and the walls of the
   leaf it landed in went under the sprite -- things seen through walls on console.
   Here a sprite walks the leaves drawn after its own, in draw order, and passes one only when
   that leaf can paint NOTHING in front of it: its window misses the sprite's screen box (RECTCLIP
   clips the leaf to it), or none of its faces may hide the billboard -- the whole billboard is on
   the eye's side of the face's plane, or the face's screen box misses the sprite's.  The first
   leaf that fails stops the walk, and the sprite is drawn after the last leaf that passed.  A
   sprite moved past faces that cannot hide it is drawn over what is behind it, never through a
   wall; it is moved, never copied, so no sprite command is added (a leaf that gains its first
   sprite gains drawSprites' user clip, one that loses its last loses it).
   The billboard tested is the one drawn.  drawSprites sends SIGNAL_VIEW before it reads the
   frame, and a Doom actor picks its ROTATION there (DOOM_ACTOR.C doomSetSequence): from the
   camera as the image is drawn, and from the monster's angle after its last action (A_Chase and
   A_FaceTarget turn it after doom_setState chose the sequence).  The leaves are chosen before
   that, in the last image's tail: o->sequence is still the last image's rotation, and two
   rotations of one frame differ by up to 64 chunk pixels (DOOM1.WAD: SARG F 3/7, BOSS E 1/8; a
   mirrored A2A8 view moves the box by w - 2 lo).  doom_drawSeq gives the rotation SIGNAL_VIEW
   will give, from the same camera.  The shadow drawSprites lays under the feet is in the box
   too: it sticks out of the chunks on the left (5-14 chunk pixels: TROO, POSS, SARG, BAR1) and
   below the feet within 60-110 u.
   MEASURED on a model of this painter (findDoorways, buildTree, sortLeafList, RECTCLIP; the .LEV
   of the disc; 18 318 views at 96-512 u around the 926 monsters; billboard = the opaque box of
   the spawn frame): sprite samples covered by something BEHIND them 13 605 -> 4 718 (-65 %),
   samples shown over something NEARER 1 817 -> 1 837, all of it one view (E1M7: a step's top
   drawn before the monster's leaf, hidden until now by the next leaf's floor).  The first
   promotion gave 3 183 / 7 110.  The walk passes 2.1 leaves on average (p90 5, max 17) and
   projects 1.2 faces.  Not measured on console: the order of the leaves is the model's. */
#define DOOM_WALK     16                 /* leaves a sprite may pass (the model's walk: 17 at most) */
#define DOOM_LISTMAX  64                 /* drawSprites takes 100 sprites a leaf: a full list keeps
					    the rest where they are */
short doomDrawNext[MAXNMSPRITES];        /* the leaf lists (spriteHead): sprite index, -1 = end */

typedef struct
{MthMatrix *m;
 MthXyz x,y;                             /* the view's right and up axes, in world units */
 MthXyz *eye;                            /* the camera SIGNAL_VIEW will see (doom_drawSeq) */
 int cpu;                                /* which set of box memos is this walk's (0 master)     */
} DoomView;

typedef struct
{MthXyz feet;                            /* drawSprites' feetPos */
 Fixed32 lo,rw,up;                       /* the frame's chunks about the feet: left, right, above */
 int x0,y0,x1,y1;                        /* its screen box, view-local like the leaf windows */
 int dist;                               /* drawSprites' sort key */
} DoomSprBox;

/* reads the set the traversal just filled (tr): on the slave in the tail, on the master for
   a split-screen view (drawWalls) */
#define sectorDraw     (tr->sd)
#define updateList     (tr->ul)
#define updateListSize (tr->ulSize)

static int doomSprDist(Sprite *o)
{return f(abs(o->pos.x-tr->pos.x))+f(abs(o->pos.y-tr->pos.y))+f(abs(o->pos.z-tr->pos.z));
}

/* The billboard drawSprites will draw, as the box of the 64-unit chunks of the frame it will
   draw (the opaque patch is narrower: an imp is 41 wide in a 64 chunk -- the box is only ever
   too big), and of its shadow.  1 = boxed; 0 = nothing drawn; -1 = drawn, its box unknown -- a
   line, an unscaled or a foot-clipped sprite (FOOTCLIP clips to the window of the leaf that
   draws it), a frame doom_drawSeq cannot name, a shadow away from the feet (a flier's): it
   stays home, and a farther sprite does not pass the leaf that draws it (doomSprOver). */
static int doomSprBox(Sprite *o,DoomView *v,DoomSprBox *b)
{int c,seq,frame,x0=0,x1=0,y0=0,y1=0;
 Fixed32 dn,top,sh,fd;
 MthXyz t,p;
 XyInt a,z;
 if (o->sequence==-1 || (o->flags & SPRITEFLAG_INVISIBLE))
    return 0;                           /* drawSprites does not list it */
 if (o->flags & SPRITEFLAG_LINE)
    return -1;
 b->feet=o->pos;
 b->feet.y-=o->radius;
 if (mpIsPlayer(o))
    b->feet.y-=SPR_HOVER(o);
 MTH_CoordTrans(v->m,&b->feet,&t);
 if (t.z<CFG_SPRITE_NEARCLIP ||
     f(abs(t.x))>(f(t.z)<<2)+256 || f(abs(t.y))>(f(t.z)<<2)+256)
    return 0;                           /* not drawn, or far off the view (project_point is 16-bit) */
 if (o->flags & (SPRITEFLAG_NOSCALE|SPRITEFLAG_FOOTCLIP))
    return -1;
 seq=doom_drawSeq(o,v->eye);            /* the rotation SIGNAL_VIEW will pick, not the last one */
 if (seq<0)
    return -1;
 frame=o->frame+level_sequence[seq];
 for (c=level_frame[frame].chunkIndex;c<level_frame[frame+1].chunkIndex;c++)
    {if (level_chunk[c].chunkx<x0) x0=level_chunk[c].chunkx;
     if (level_chunk[c].chunkx+64>x1) x1=level_chunk[c].chunkx+64;
     if (level_chunk[c].chunky<y0) y0=level_chunk[c].chunky;
     if (level_chunk[c].chunky+64>y1) y1=level_chunk[c].chunky+64;
    }
 b->lo=-x0*o->scale;
 b->rw=x1*o->scale;
 b->up=-y0*o->scale;
 dn=y1*o->scale;
 top=b->up;
 if (!(o->flags & (SPRITEFLAG_NOSHADOW|SPRITEFLAG_MESH)) && shadowMode!=CFG_COMPO_NONE &&
     t.z<=CFG_SHADOW_DIST)              /* GCC14: the one drawSprites draws */
    {/* drawSprites' shadow: centred on the floor under the feet, at most 48 chunk pixels wide,
	and as tall as that width times dy/z, dy from the camera's pos to that floor.  Its width
	joins the billboard's (a row at the feet); its height only the screen box: flat on the
	floor, it crosses the plane of a face the billboard stands in front of only by its ends,
	as it crosses the walls of its own leaf. */
     fd=findFloorDistance(o->s,&b->feet);
     if (abs(fd)<F(128))
	{if (abs(fd)>F(1))
	    return -1;                  /* the shadow is not at the feet */
	 sh=(shadowSpan>>1)*o->scale;
	 if (b->lo<sh) b->lo=sh;
	 if (b->rw<sh) b->rw=sh;
	 sh=MTH_Mul(sh,MTH_Div(abs(b->feet.y-fd-tr->pos.y),t.z))+abs(fd);  /* + its centre's
					   offset: on the floor, up to a unit off the feet */
	 if (dn<sh) dn=sh;
	 if (top<sh) top=sh;
	}
    }
 p=t; p.x-=b->lo; p.y+=top;
 project_point(&p,&a);
 p=t; p.x+=b->rw; p.y-=dn;
 project_point(&p,&z);
 b->x0=a.x-1; b->y0=a.y-1;
 b->x1=z.x+1; b->y1=z.y+1;
 if (b->x1<tr->xmin || b->x0>tr->xmax || b->y1<tr->ymin || b->y0>tr->ymax)
    return 0;                           /* off the view: sprRect drops every chunk */
 b->dist=doomSprDist(o);
 return 1;
}

/* 1 when face w of leaf n may paint some of the sprite that is in front of it */
static int doomFaceHides(int w,SectorDrawRecord *n,DoomSprBox *b,DoomView *v)
{sWallType *wl=level_wall+w;
 MthXyz p,t;
 Fixed32 d,xn,yn;
 int i,code,all,x0,y0,x1,y1;
 long long fx,fy;
 if (wl->flags & (WALLFLAG_INVISIBLE|WALLFLAG_PARALLAX))
    return 0;                           /* drawSector draws no polygon for it */
 getVertex(wl->v[0],&p);
 if (f(tr->pos.x-p.x)*wl->normal[0]+f(tr->pos.y-p.y)*wl->normal[1]+
     f(tr->pos.z-p.z)*wl->normal[2]<0)
    return 0;                           /* back face: drawSector's own test */
 /* The billboard is feet + u.x + h.y, u in [-lo,rw], h in [0,up]: its rows under the feet (3-5 u
    of Doom's frames) are left to the floor, as they are on the floor of its own leaf.  Its point
    nearest the plane, measured towards the eye's side: */
 t.x=b->feet.x-p.x; t.y=b->feet.y-p.y; t.z=b->feet.z-p.z;
 d=MTH_Product((Fixed32 *)&t,(Fixed32 *)wl->normal);
 xn=MTH_Product((Fixed32 *)&v->x,(Fixed32 *)wl->normal);
 yn=MTH_Product((Fixed32 *)&v->y,(Fixed32 *)wl->normal);
 d+=(xn>0)? -MTH_Mul(b->lo,xn): MTH_Mul(b->rw,xn);
 if (yn<0)
    d+=MTH_Mul(b->up,yn);
 if (d>=(wl->normal[1]? -(F(1)>>1): F(1)))
    return 0;                           /* all of it on the eye's side (a unit to spare, the
					   model's): the face is behind it.  A floor under the feet
					   and a ceiling over the head pass whatever their outline
					   -- the spilled squares with them */
 /* the plane passes in front of part of it: does the face reach the sprite's box, inside the
    window n is clipped to?  Screen x = focal.x/z, y = -focal.y/z, compared as products. */
 x0=(b->x0>n->xmin)? b->x0: n->xmin;
 x1=(b->x1<n->xmax)? b->x1: n->xmax;
 y0=(b->y0>n->ymin)? b->y0: n->ymin;
 y1=(b->y1<n->ymax)? b->y1: n->ymax;
 all=15;
 for (i=0;i<4;i++)
    {getVertex(wl->v[i],&p);
     MTH_CoordTrans(v->m,&p,&t);
     if (t.z<NEARCLIP)
	return 1;                        /* the near-plane repair moves it: taken as everywhere */
     fx=(long long)focalDist*t.x;
     fy=-(long long)focalDist*t.y;
     code=0;
     if (fx<(long long)x0*t.z) code|=1;
     if (fx>(long long)x1*t.z) code|=2;
     if (fy<(long long)y0*t.z) code|=4;
     if (fy>(long long)y1*t.z) code|=8;
     all&=code;
    }
 return !all;                           /* all four corners past one edge: it misses the box */
}

/* 1 when leaf n, drawn after the sprite's own, paints nothing in front of it */
static int doomLeafPasses(SectorDrawRecord *n,DoomSprBox *b,DoomView *v)
{int w,s=n-sectorDraw;
 if (b->x1<n->xmin || b->x0>n->xmax || b->y1<n->ymin || b->y0>n->ymax)
    return 1;                           /* RECTCLIP: n paints nothing of the sprite's box */
 for (w=level_sector[s].firstWall;w<=level_sector[s].lastWall;w++)
    if (doomFaceHides(w,n,b,v))
       return 0;
 return 1;
}

/* GCC14: the box of a sprite, kept for the rest of this walk.  A sprite is asked for its box
   once per sprite it may hide, so a crowded leaf built the same box again and again -- a
   transform, a rotation, the floor under the feet and two projections each time.  Direct mapped
   on the sprite's index, one set per CPU (the slave walks the leaves in the tail, the master for
   a split view): a collision simply builds the box again.  The stamp empties the whole set at
   the start of each walk, so nothing survives a camera move. */
#define DOOM_MEMO 32
typedef struct
{unsigned int stamp;
 short spr;
 short x0,y0,x1,y1;
 signed char r;
} DoomBoxMemo;
static DoomBoxMemo boxMemo[2][DOOM_MEMO];
static unsigned int boxStamp[2];

static int doomSprBoxMemo(Sprite *u,DoomView *v,DoomSprBox *b)
{DoomBoxMemo *m=&boxMemo[v->cpu][(u-sprites)&(DOOM_MEMO-1)];
 int r;
 if (m->stamp==boxStamp[v->cpu] && m->spr==(short)(u-sprites))
    {b->x0=m->x0; b->y0=m->y0; b->x1=m->x1; b->y1=m->y1;
     return m->r;
    }
 r=doomSprBox(u,v,b);
 m->stamp=boxStamp[v->cpu];
 m->spr=(short)(u-sprites);
 m->r=(signed char)r;
 if (r>0)
    {m->x0=(short)b->x0; m->y0=(short)b->y0; m->x1=(short)b->x1; m->y1=(short)b->y1;}
 return r;
}

/* 1 when u, another sprite, is nearer than o (drawSprites' key) and overlaps it on screen --
   or may: a sprite drawn without a box is taken as over it */
static int doomSprOver(Sprite *u,Sprite *o,DoomSprBox *b,DoomView *v)
{DoomSprBox c;
 int r;
 if (u==o || doomSprDist(u)>=b->dist)
    return 0;
 r=doomSprBoxMemo(u,v,&c);
 if (r<0)
    return 1;
 return r && c.x0<=b->x1 && c.x1>=b->x0 && c.y0<=b->y1 && c.y1>=b->y0;
}

/* 1 when a sprite of home list h or of leaf list k is over o: o must not be drawn after the
   leaf that draws them.  *len: how far k was read (its length when 0 is returned). */
static int doomSprNearer(Sprite *h,int k,Sprite *o,DoomSprBox *b,DoomView *v,int *len)
{*len=0;
 for (;h;h=h->next)
    if (doomSprOver(h,o,b,v))
       return 1;
 for (;k>=0;k=doomDrawNext[k],(*len)++)
    if (doomSprOver(sprites+k,o,b,v))
       return 1;
 return 0;
}

void doom_spriteLeaves(MthMatrix *view,MthXyz *eye,int cpu)
{int i,j,t,k,len;
 Sprite *o,*home;
 SectorDrawRecord *a,*n;
 DoomSprBox b;
 DoomView v;
 MthXyz e,o0,ax[3];
 v.cpu=cpu&1;
 if (!++boxStamp[v.cpu])                /* 0 is the empty set */
    boxStamp[v.cpu]=1;
 /* the view's axes in world units, from the matrix as MTH_CoordTrans applies it */
 v.m=view;
 v.eye=eye;
 e.x=e.y=e.z=0;
 MTH_CoordTrans(view,&e,&o0);
 e.x=F(1); MTH_CoordTrans(view,&e,ax+0); e.x=0;
 e.y=F(1); MTH_CoordTrans(view,&e,ax+1); e.y=0;
 e.z=F(1); MTH_CoordTrans(view,&e,ax+2);
 v.x.x=ax[0].x-o0.x; v.x.y=ax[1].x-o0.x; v.x.z=ax[2].x-o0.x;
 v.y.x=ax[0].y-o0.y; v.y.y=ax[1].y-o0.y; v.y.z=ax[2].y-o0.y;
 for (i=0;i<updateListSize;i++)
    updateList[i]->spriteHead=-1;
 /* The leaves in draw order (updateList[updateListSize-1] first).  Of two sprites that overlap
    on screen, the nearer is drawn with the farther or after it, as drawSprites sorts them: a
    sprite stays home when a nearer one is over it there, and stops in the first leaf that draws
    one -- sectorSpriteList holds the sprites not yet placed, the leaf lists the ones placed. */
 for (i=updateListSize-1;i>=0;i--)
    {a=updateList[i];
     home=sectorSpriteList[a-sectorDraw];
     for (o=home;o;o=o->next)
	{t=i;
	 if (doomSprBox(o,&v,&b)>0 && !doomSprNearer(home,a->spriteHead,o,&b,&v,&len))
	    for (j=i-1;j>=0 && j>=i-DOOM_WALK;j--)
	       {n=updateList[j];
		if (!doomLeafPasses(n,&b,&v))
		   break;
		k=doomSprNearer(sectorSpriteList[n-sectorDraw],n->spriteHead,o,&b,&v,&len);
		if (len>=DOOM_LISTMAX)
		   break;
		t=j;
		if (k)
		   break;               /* a nearer sprite over it: drawn with it, drawSprites sorts */
	       }
	 k=o-sprites;
	 doomDrawNext[k]=updateList[t]->spriteHead;
	 updateList[t]->spriteHead=(short)k;
	}
    }
}
#undef sectorDraw
#undef updateList
#undef updateListSize

/* CFG_SPR_FIRST: the list drawWalls draws after leaf s (drawSprites) */
Sprite *doom_sprFirst(int s)
{return (sectorDraw[s].spriteHead<0)? NULL: sprites+sectorDraw[s].spriteHead;
}
#endif

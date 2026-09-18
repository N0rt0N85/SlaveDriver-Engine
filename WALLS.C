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
static int nmLights,delayNmLights;
static MthXyz tLightPos[MAXNMLIGHTSOURCES];
static int lColor[MAXNMLIGHTSOURCES][3];   /* proportional: k * intensity */
static int delayColor[MAXNMLIGHTSOURCES][3];
/* GCC14: radius, radius^2 and (1<<24)/radius^2, set once per light: no divide per vertex. */
static int lRad[MAXNMLIGHTSOURCES],lRad2[MAXNMLIGHTSOURCES],lInv[MAXNMLIGHTSOURCES];
static int delayRad[MAXNMLIGHTSOURCES];
static char lMode[MAXNMLIGHTSOURCES];      /* 0 subtractive, 1 proportional */
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

static void lightPut(Sprite *s,int r,int g,int b,int radius,int mode)
{int i=delayNmLights;
 if (i>=MAXNMLIGHTSOURCES)
    return;
 lColor[i][0]=delayColor[i][0]=r;
 lColor[i][1]=delayColor[i][1]=g;
 lColor[i][2]=delayColor[i][2]=b;
 lightSetRadius(i,radius);
 delayRad[i]=radius;
 lMode[i]=mode;
 lightSource[i]=s;
 delayNmLights++;
}

void addLight(Sprite *s,int r,int g,int b)
{lightPut(s,r,g,b,LIGHTRADIUS,0);
}

/* GCC14: k 0..16 per channel, radius in world units, intensity 0..31 at the centre. */
void addLightEx(Sprite *s,int r,int g,int b,int radius,int peak)
{assert(radius>=16 && radius<=1024);
 assert(peak>=0 && peak<=31);
 assert(r>=0 && r<=16 && g>=0 && g<=16 && b>=0 && b<=16);
 lightPut(s,r*peak,g*peak,b*peak,radius,1);
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
 m=lMode[i]? peak: 1;
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
   drawSprites; the box test runs before any multiply, and before any overflow. */
static inline void lightApply(int i,MthXyz *pos,int *r,int *g,int *b)
{int dx=f(pos->x-tLightPos[i].x);
 int dy=f(pos->y-tLightPos[i].y);
 int dz=f(pos->z-tLightPos[i].z);
 int rad=lRad[i];
 int u;
 if (dx>rad || dx<-rad || dy>rad || dy<-rad || dz>rad || dz<-rad)
    return;
 u=lRad2[i]-(dx*dx+dy*dy+dz*dz);
 if (u<=0)
    return;
 if (lMode[i])
    {int sv=(u*lInv[i])>>16;          /* 0..256 */
#if CFG_LIGHTSMOOTH
     sv=(sv*sv)>>8;                   /* soft edge */
#endif
     *r+=(sv*lColor[i][0])>>12;       /* <= intensity */
     *g+=(sv*lColor[i][1])>>12;
     *b+=(sv*lColor[i][2])>>12;
    }
 else
    {u>>=CFG_LIGHTSHIFT;
     if (u-lColor[i][0]>0)
	*r+=u-lColor[i][0];
     if (u-lColor[i][1]>0)
	*g+=u-lColor[i][1];
     if (u-lColor[i][2]>0)
	*b+=u-lColor[i][2];
    }
}


int nmWallLights;
static char wallLightP[MAXNMLIGHTSOURCES];
static void buildLightList(sWallType *wall)
{int l;
 Fixed32 dist;
 MthXyz wallP;
 getVertex(wall->v[0],&wallP);
 nmWallLights=0;
 for (l=0;l<nmLights;l++)
    {/* find distance from light to wall's plane */
     dist=(f(lightSource[l]->pos.x-wallP.x))*wall->normal[0]+
	  (f(lightSource[l]->pos.y-wallP.y))*wall->normal[1]+
	  (f(lightSource[l]->pos.z-wallP.z))*wall->normal[2];
     /* GCC14: only a light on the visible side of the plane (same dot product as the backface
	test); the old test lit walls from up to a radius behind them. */
     if (dist<=0 || dist>F(lRad[l]))
	wallLightP[l]=0;
     else
	{wallLightP[l]=1;
	 nmWallLights++;
	}
    }
}

static int snmWallLights;
static char swallLightP[MAXNMLIGHTSOURCES];
static void sbuildLightList(sWallType *wall)
{int l;
 Fixed32 dist;
 MthXyz wallP;
 getVertex(wall->v[0],&wallP);
 snmWallLights=0;
 for (l=0;l<nmLights;l++)
    {/* find distance from light to wall's plane */
     dist=(f(lightSource[l]->pos.x-wallP.x))*wall->normal[0]+
	  (f(lightSource[l]->pos.y-wallP.y))*wall->normal[1]+
	  (f(lightSource[l]->pos.z-wallP.z))*wall->normal[2];
     /* GCC14: see buildLightList */
     if (dist<=0 || dist>F(lRad[l]))
	swallLightP[l]=0;
     else
	{swallLightP[l]=1;
	 snmWallLights++;
	}
    }
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
    return greyTable[(int)vlight];
 r=g=b=vlight;
 for (i=0;i<nmLights;i++)
    if (wallLightP[i])
       lightApply(i,pos,&r,&g,&b);
 if (r>31) r=31;
 if (g>31) g=31;
 if (b>31) b=31;
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
    return greyTable[(int)vlight];
 r=g=b=vlight;
 for (i=0;i<nmLights;i++)
    if (swallLightP[i])
       lightApply(i,pos,&r,&g,&b);
 if (r>31) r=31;
 if (g>31) g=31;
 if (b>31) b=31;
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
	 gtable.entry[z]=greyTable[f(shade2[z+i*4])];
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
	 gtable.entry[z]=greyTable[f(shade2[z+i*4])];
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


/* GCC14: near-plane repair of one row of a wall grid.  rectTransform cannot clip, only clamp: a
   grid point nearer than the floor keeps its x,y and is divided by the floor, which for a point
   BEHIND the eye invents a projection -- as the wall's near end swings behind you its view x goes
   to zero and the corner drifts to the middle of the screen.  Every flagged point of the row
   (light bit 15) is moved to where the row crosses the cut, the further along U of
     (a) the near plane z = NEARCLIP, and
     (b) the lateral frustum plane |x| = z, where the wall genuinely leaves the screen.
   (b) is the only handle on the straddling tile's stretch: a tile's pattern cannot be windowed in
   U (WALLASM.H), so the whole tile is spread from that corner whatever we do, and the screen edge
   is the furthest out the corner can sit without leaving a hole.  Two divides per row, and only
   on a wall that has a corner nearer than the plane. */
static void repairNearRow(struct vCalc *row,int nmv,MthXyz *p0,MthXyz *vW)
{int i,flagged=0;
 Fixed32 u,ua,den,umax;
 MthXyz P;
 XyInt xy;

 for (i=0;i<nmv;i++)
    if (row[i].light & 0x8000)
       flagged++;
 if (!flagged || !vW->z)
    return;
 umax=F(nmv-1);
 if (flagged==nmv)
    /* no crossing in this row -- it is nearer than the plane from end to end.  Collapse it to
       its far end rather than divide for a crossing that is not there. */
    u=(vW->z>0)? umax: 0;
 else
    {/* a point on each side of the plane, so this crossing is inside [0,nmv-1] */
     u=MTH_Div(NEARCLIP-p0->z,vW->z);
     den=(p0->x<0)? vW->x+vW->z: vW->x-vW->z;
     if (den)
	{ua=(p0->x<0)? MTH_Div(-(p0->z+p0->x),den): MTH_Div(p0->z-p0->x,den);
	 if (ua>=0 && ua<=umax && ((vW->z>0)? (ua>u): (ua<u)))
	    u=ua;
	}
     if (u<0) u=0;
     if (u>umax) u=umax;
    }
 P.x=p0->x+MTH_Mul(vW->x,u);
 P.y=p0->y+MTH_Mul(vW->y,u);
 P.z=p0->z+MTH_Mul(vW->z,u);
 if (P.z<NEARCLIP)
    P.z=NEARCLIP;
 project_point(&P,&xy);
 for (i=0;i<nmv;i++)
    if (row[i].light & 0x8000)
       {row[i].x=xy.x;
	row[i].y=xy.y;
       }
}

/* GCC14: true when the wall reaches nearer than the plane, i.e. when its grid needs the repair
   above.  Four compares on the per-wall path, which already does back face, far and near. */
static int wallCrossesNear(MthXyz *coords)
{return coords[0].z<NEARCLIP || coords[1].z<NEARCLIP ||
	coords[2].z<NEARCLIP || coords[3].z<NEARCLIP;
}

#define REPAIRNEARGRID(vc)						\
 if (wallCrossesNear(coords))						\
    {MthXyz p0=coords[0];						\
     int rh;								\
     for (rh=0;rh<=height;rh++)						\
	{repairNearRow((vc)+rh*(width+1),width+1,&p0,&vWidth);		\
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
/* Why a mesh weld was refused: [0] the next black face was not in the strip (fan joint or new
   row); [1] the strip existed but a skipped vertex missed the replacing edge.  [0] calls for a
   wider adjacency, [1] for a tolerance. */
int lodWeldWhy[2];
static int slave_lodWeldWhy[2];
/* Cells from the MESH path (drawWall / slave_drawWall): non-parallelogram walls, which includes
   every Doom floor and ceiling.  Says whether welding mesh faces is worth building. */
int nmMeshPolys;
static int slave_nmMeshPolys;
extern unsigned char fogTable[256];

/* GCC14: flat cells -- what fusion cannot take.  A wall fading into the fog is black far and lit
   near, never fusable whole, but its far cells go black one by one.
   Exact and free: the asm stores the final gouraud word in vCalc[].light (greyTable[light], bit 15
   flipped in front of the near plane, .Lrt_retFromLit); greyTable[0] = 0x8000, so a valid
   all-black vertex is exactly 0.  The OR of the four is zero when the cell has nothing to show.
   Kept: the VDP1 command.  Dropped: the tile (mapPic, a cache slot), the gouraud table, the
   texture mapping. */
#define CELLISBLACK(g) (lodEnable && \
			!((g).entry[0]|(g).entry[1]|(g).entry[2]|(g).entry[3]))

/* Same test on a GRID cell, before the loop permutes the corners for the texture: the four
   vertices are at grid indices, not in pattern order. */
#define LODCELLDARK(V) (lodEnable && \
			!((V)[row1+w].light|(V)[row1+w+1].light| \
			  (V)[row2+w].light|(V)[row2+w+1].light))

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
#define LODVXY(Q,V,I) {(Q).x=(V)[I].x; (Q).y=(V)[I].y;}

/* Extends a strip of black faces from f, fills q[] with the welded quad, returns the strip's
   last index -- f itself if nothing welds. */
static int weldFaceStrip(sWallType *wall,int f,struct vCalc *V,XyInt *q,int *why)
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
	{dir=0;
	 if (faceIsBlack(f+1,V))
	    why[0]++;   /* next face was black, but not in the strip */
	}
     g=f;
     while (dir && g<wall->lastFace)
	{a=level_face[g].v; c=level_face[g+1].v;
	 if (!faceIsBlack(g+1,V))
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
    {LODVXY(q[0],V,level_face[f].v[0]);
     LODVXY(q[1],V,level_face[f].v[1]);
     LODVXY(q[2],V,level_face[g].v[2]);
     LODVXY(q[3],V,level_face[g].v[3]);
     for (j=f;j<g;j++)   /* the skipped vertices are the shared edges */
	if (!nearSegment(q+0,q+3,V[level_face[j].v[3]].x,V[level_face[j].v[3]].y) ||
	    !nearSegment(q+1,q+2,V[level_face[j].v[2]].x,V[level_face[j].v[2]].y))
	   {dir=0; why[1]++; break;}
    }
 else
    if (dir==2)
       {LODVXY(q[0],V,level_face[g].v[0]);
	LODVXY(q[1],V,level_face[f].v[1]);
	LODVXY(q[2],V,level_face[f].v[2]);
	LODVXY(q[3],V,level_face[g].v[3]);
	for (j=f;j<g;j++)
	   if (!nearSegment(q+0,q+1,V[level_face[j].v[0]].x,V[level_face[j].v[0]].y) ||
	       !nearSegment(q+3,q+2,V[level_face[j].v[3]].x,V[level_face[j].v[3]].y))
	      {dir=0; why[1]++; break;}
       }
 if (!dir)
    {LODVXY(q[0],V,level_face[f].v[0]);
     LODVXY(q[1],V,level_face[f].v[1]);
     LODVXY(q[2],V,level_face[f].v[2]);
     LODVXY(q[3],V,level_face[f].v[3]);
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
/* LOD PAINTED gives each stage its own colour, to see at a glance which path a surface took:
      GREEN  a whole wall folded into one quad
         BLUE   a weld of black cells in a wall grid
         RED    a mesh face -- floor, ceiling or curved wall */
#define LODCOL_FUSE ((lodEnable>1)? RGB(0,24,0): RGB(0,0,0))
#define LODCOL_RECT ((lodEnable>1)? RGB(0,0,24): RGB(0,0,0))
#define LODCOL_MESH ((lodEnable>1)? RGB(24,0,0): RGB(0,0,0))

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

 buildLightList(theWall);

 if (wallIsBlack(theWall,coords,nmWallLights,wavyIndex))
    {XyInt q[4];
     if (fuseWallPoly(coords,s,q))
	{EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,LODCOL_FUSE,q,NULL);
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

 REPAIRNEARGRID(vCalc);

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
	     if (clip_visible(q,s))
		{EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,LODCOL_RECT,q,NULL);
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
			EZ_distSprVClip(mapPic(level_texture[t+1]),poly,&gtable);
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
	 EZ_distSprVClip(mapPic(level_texture[tex]
#if MIPMAP
				   +tileBias
#endif
				   ),poly,&gtable);
	 nmPolys++;
	 tex++;
	}
     row1+=width+1;
     row2+=width+1;
    }
 popProfile();
}


void drawWall(sWallType *wall,MthMatrix *view,SectorDrawRecord *s)
{int f,i,v,clip;
 XyInt poly[4];
 struct gourTable gtable;
 struct vCalc vCalc[MAXVPERWALL];
#ifndef NDEBUG
 int maxV;
#endif

 checkStack();
#ifdef LIGHT
 buildLightList(wall);
#endif

 assert(wall->lastVertex-wall->firstVertex<MAXVPERWALL);
 v=0;
 assert(wall->lastVertex-wall->firstVertex<MAXVPERWALL);
 normTransform(level_vertex+wall->firstVertex,view,
	       wall->lastVertex-wall->firstVertex+1,vCalc,
	       (nmWallLights||wavyIndex)?&getLight:NULL,NEARCLIP);

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
     nmMeshPolys++;
     if (CELLISBLACK(gtable))
	{XyInt q[4];
	 int g=weldFaceStrip(wall,f,vCalc,q,lodWeldWhy);
	 if (clip_visible(q,s))
	    {EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,LODCOL_MESH,q,NULL);
	     nmPolys++;
	     lodFlat++;
	    }
	 lodCells+=g-f;
	 nmMeshPolys+=g-f;
	 f=g;
	 continue;
	}
#if 0
     EZ_distSpr(DIR_NOREV,
		UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU,
		0,mapPic(level_face[f].tile),poly,&gtable);
#endif
     EZ_distSprVClip(mapPic(level_face[f].tile),poly,&gtable);
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
     vCalc[v].light=greyTable[GETWATERBRIGHT(original.x,original.z)];
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

     if (clip || !clip_visible(probeVDP1(poly),s))
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

#define MAXNMSLAVEPOLYS 1300

/* if tile==-1 then gtable.entry[0]==sector that was just drawn */
struct slaveDrawResult
{XyInt poly[4];
 struct gourTable gtable;
 short tile,pad;
};
static struct slaveDrawResult *slaveResult=
   (struct slaveDrawResult *)doorwayCache;
int nmSlavePolys;

/*struct vCalc slave_vCalc[MAXVPERWALL]; */
static struct vCalc *slave_vCalc=(struct vCalc *)(((char *)doorwayCache)+MAXNMSLAVEPOLYS*sizeof(struct slaveDrawResult));

void slave_drawWater(sWallType *theWall)
{slaveResult[nmSlavePolys].tile=-2;
 slaveResult[nmSlavePolys].gtable.entry[0]=theWall-level_wall;
 nmSlavePolys++;
}

void slave_drawRectWall(sWallType *theWall,MthXyz *coords,
			SectorDrawRecord *s)
{MthXyz vWidth,vHeight;
 XyInt poly[4];
 int w,h,v,clip;
 int runStart;   /* start of the current run of black cells, -1 = none */
 int light;
 int tex,row1,row2;
 int width=theWall->tileLength; /* GCC14: not const, halved under #if MIPMAP */
 int height=theWall->tileHeight;
 struct gourTable gtable;
 char *ppattern;
 struct slaveDrawResult *cacheThruResult=
    (struct slaveDrawResult *)(((int)slaveResult)+0x20000000);
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
 sbuildLightList(theWall);
#endif

 if (wallIsBlack(theWall,coords,snmWallLights,sWavyIndex))
    {XyInt q[4];
     int j;
     if (fuseWallPoly(coords,s,q))
	{cacheThruResult[nmSlavePolys].tile=-5;
	 cacheThruResult[nmSlavePolys].gtable.entry[0]=LODCOL_FUSE;
	 for (j=0;j<4;j++)
	    cacheThruResult[nmSlavePolys].poly[j]=q[j];
	 nmSlavePolys++;
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

 REPAIRNEARGRID(slave_vCalc);

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
		{cacheThruResult[nmSlavePolys].gtable.entry[0]=LODCOL_RECT;
		 cacheThruResult[nmSlavePolys].tile=-5;
		 for (v=0;v<4;v++)
		    cacheThruResult[nmSlavePolys].poly[v]=q[v];
		 nmSlavePolys++;
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
			cacheThruResult[nmSlavePolys].gtable=gtable;
			for (v=0;v<4;v++)
			   cacheThruResult[nmSlavePolys].poly[v]=poly[v];
			cacheThruResult[nmSlavePolys].tile=level_texture[t+1];
			nmSlavePolys++;
			assert(nmSlavePolys<MAXNMSLAVEPOLYS);
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

	 cacheThruResult[nmSlavePolys].gtable=gtable;
	 for (v=0;v<4;v++)
	    cacheThruResult[nmSlavePolys].poly[v]=poly[v];
	 cacheThruResult[nmSlavePolys].tile=level_texture[tex]
#if MIPMAP
	    +tileBias
#endif
	    ;
	 nmSlavePolys++;
	 assert(nmSlavePolys<MAXNMSLAVEPOLYS);
	 tex++;
	}
     row1+=width+1;
     row2+=width+1;
    }
}

void slave_drawWall(sWallType *wall,MthMatrix *view,SectorDrawRecord *s)
{int f,i,v,clip;
 XyInt poly[4];
 struct gourTable gtable;
 struct slaveDrawResult *cacheThruResult=
    (struct slaveDrawResult *)(((int)slaveResult)+0x20000000);

 if (wall->lastFace-wall->firstFace+1+nmSlavePolys+50>MAXNMSLAVEPOLYS)
    return;

 sbuildLightList(wall);

 assert(wall->lastVertex-wall->firstVertex<MAXVPERWALL);

 normTransform(level_vertex+wall->firstVertex,view,
	       wall->lastVertex-wall->firstVertex+1,slave_vCalc,
	       (snmWallLights||sWavyIndex)?&sgetLight:NULL,NEARCLIP);

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

     slave_nmMeshPolys++;
     if (CELLISBLACK(gtable))
	{XyInt q[4];
	 int g=weldFaceStrip(wall,f,slave_vCalc,q,slave_lodWeldWhy);
	 if (clip_visible(q,s))
	    {cacheThruResult[nmSlavePolys].gtable.entry[0]=LODCOL_MESH;
	     cacheThruResult[nmSlavePolys].tile=-5;
	     for (i=0;i<4;i++)
		cacheThruResult[nmSlavePolys].poly[i]=q[i];
	     nmSlavePolys++;
	     slave_lodFlat++;
	    }
	 slave_lodCells+=g-f;
	 slave_nmMeshPolys+=g-f;
	 f=g;
	 continue;
	}
     cacheThruResult[nmSlavePolys].gtable=gtable;
     for (i=0;i<4;i++)
	cacheThruResult[nmSlavePolys].poly[i]=poly[i];
     cacheThruResult[nmSlavePolys].tile=level_face[f].tile;
     nmSlavePolys++;
     assert(nmSlavePolys<MAXNMSLAVEPOLYS);
    }
}

SectorDrawRecord sectorDraw[MAXNMSECTORS];
SectorDrawRecord *updateList[MAXNMSECTORS];
int updateListSize;
SectorDrawRecord *drawList[MAXNMSECTORS];
int drawListSize;

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
	    slave_drawWall(theWall,view,sectorDraw+sectorNm);
	}
     else
	{if (level_sector[sectorNm].flags & SECFLAG_WATER)
	    wavyIndex=(w & 0x1f)+1;
	 else
	    wavyIndex=0;
	 if (theWall->flags & WALLFLAG_PARALLELOGRAM)
	    drawRectWall(theWall,tformed,sectorDraw+sectorNm);
	 else
	    drawWall(theWall,view,sectorDraw+sectorNm);
	}

    }

}

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
static MthMatrix pipeMatrix;   /* copie stable : viewTransform sera depile entre-temps */
static int pipeInFlight;
static int pipeDone;
int pipeSpin=-1;               /* spins at the join; -1 = nothing in flight */

void wallsTraverse(MthMatrix *view,int onSlave);

MthMatrix *slaveView;
void slaveDraw(void)
{int i;
 /* flush cache */
 *CACHECNTRL=0x10;
 *CACHECNTRL=0x01;
 nmSlavePolys=0;
 for (i=slaveDrawStart;i>=0;i--)
    {drawSector(updateList[i]-sectorDraw,slaveView,1);
     /* mark end of sector */
     slaveResult[nmSlavePolys].tile=-1;
     slaveResult[nmSlavePolys].gtable.entry[0]=updateList[i]-sectorDraw;
     nmSlavePolys++;
    }
}

void EZ_specialDistSpr(struct slaveDrawResult *sdr,int charNm);
void drawSlaveWalls(void)
{int i;
 int s;
 XyInt parms[2];
 /* flush cache */
 *CACHECNTRL=0x10;
 *CACHECNTRL=0x01;
 s=slaveDrawStart;
 assert(nmSlavePolys<MAXNMSLAVEPOLYS);
 assert(nmSlavePolys>=0);
 if (nmSlavePolys==0)
    return;
#if RECTCLIP
 parms[0].x=updateList[s]->xmin+viewCx;parms[0].y=updateList[s]->ymin+viewCy;
 parms[1].x=updateList[s]->xmax+viewCx;parms[1].y=updateList[s]->ymax+viewCy;
 EZ_userClip(parms);
#endif

 for (i=0;i<nmSlavePolys;i++)
    {if (slaveResult[i].tile<0)
	{switch (slaveResult[i].tile)
	    {
	     case -5:
		{/* wall fused by the LOD: one quad, its colour carried in the gouraud
		    table for lack of another free field in the record */
		 EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,
			    slaveResult[i].gtable.entry[0],slaveResult[i].poly,NULL);
		 continue;
		}
#if 0
	     case -4:
		EZ_polygon(DRAW_GOURAU|DRAW_MESH|ECD_DISABLE|SPD_DISABLE,
			   WATERCOLOR,slaveResult[i].poly,
			   &slaveResult[i].gtable);
		continue;
#endif
#if 0
	     case -3:
		{/* its a plax wall */
		 sWallType *w;
		 MthXyz wallV;
		 int j;
		 MthXyz tformed[4];
		 w=level_wall+slaveResult[i].gtable.entry[0];
		 for (j=0;j<4;j++)
		    {getVertex(w->v[j],&wallV);
		     MTH_CoordTrans(slaveView,&wallV,tformed+j);
		    }
		 drawPlax(w,tformed);
		 continue;
		}
#endif
	     case -2:
		{/* its a water surface */
		 sWallType *w;
		 /* MthXyz wallV;
		    int j;
		    MthXyz tformed[4];*/
		 w=level_wall+slaveResult[i].gtable.entry[0];
		 /* for (j=0;j<4;j++)
		    {getVertex(w->v[j],&wallV);
		    MTH_CoordTrans(slaveView,&wallV,tformed+j);
		    }
		    drawWater(w,tformed); */
		 drawWaterSurface(w,slaveView,updateList[s]);
		 continue;
		}
	     case -1:
		{/* its an end of sector marker */
		 slaveDrawStart--;
		 /* drawSprites(&(viewPos),slaveView,
		    slaveResult[i].gtable.entry[0]); */
		 if (updateList[s]->spriteCommandStart)
		    EZ_linkCommand(EZ_getNextCmdNm()-1,JUMP_CALL,
				   updateList[s]->spriteCommandStart);

		 assert(updateList[s]-sectorDraw==
			slaveResult[i].gtable.entry[0]);
		 s--;
#if RECTCLIP
		 if (s>=0)
		    {parms[0].x=updateList[s]->xmin+viewCx;
		     parms[0].y=updateList[s]->ymin+viewCy;
		     parms[1].x=updateList[s]->xmax+viewCx;
		     parms[1].y=updateList[s]->ymax+viewCy;
		     EZ_userClip(parms);
		    }
#endif
		 continue;
		}
	       }
	 assert(0);
	}
     assert(getPicClass(slaveResult[i].tile)==TILE16BPP);
     EZ_distSprVClip(mapPic(slaveResult[i].tile),	/* GCC14: was EZ_specialDistSpr */
		     slaveResult[i].poly,&slaveResult[i].gtable);

#if 0
     EZ_distSpr(DIR_NOREV,
		UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU,
		0,mapPic(slaveResult[i].tile),
		slaveResult[i].poly,&slaveResult[i].gtable);
#endif
    }
 assert(s==-1);
}

void wallRenderSlaveMain(void)
{set_imask(0xf);
 *IPRA=0x0000;
 *IPRB=0x0000;
 *TIER=0x01;
 while (1)
    {/* wait for sync signal */
     while (!(*FTCSR & 0x80)) ;
     /* sync */
     *FTCSR=0x0;
     /* Purge BEFORE reading slaveJob: the master just wrote it and the slave's cache
	may still hold the previous job (slaveDraw purges on entry too; harmless). */
     *CACHECNTRL=0x10;
     *CACHECNTRL=0x01;
     if (slaveJob)
	wallsTraverse(&pipeMatrix,1);
     else
	slaveDraw();
     *(Uint16 volatile *)0x21800000=0xffff;
    }
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

 for (i=0;i<nmLights;i++)
    MTH_CoordTrans(view,&(lightSource[i]->pos),tLightPos+i);

 if (!onSlave) pushProfile("Find Visible");

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
 if (!onSlave) pushProfile("Find Doorways");
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
 if (!onSlave) popProfile();

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

 CFG_SPRITE_LEAVES();
 if (!onSlave) popProfile();
}


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
 setViewer(camera);           /* view 0 of the next frame: the caller loaded player 0 */
 pipeDone=0;
 slaveJob=1;
 pipeInFlight=1;
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
}

/* Joins the traversal started in the tail.  Call BEFORE drawWalls -- and before freeing the
   level, or the slave reads geometry reloaded under it. */
void wallsPipeJoin(void)
{
#if WALLPIPE
 int i=0;
 if (!pipeInFlight)
    {pipeSpin=-1;
     return;
    }
 while (!(*FTCSR & 0x80))
    i++;
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
#if WALLPIPE>=2
 pipeDone=1;
#endif
#endif
}


/* --- GCC14: DRAW half ----------------------------------------------------------------
   Everything that emits VDP1 commands or touches the tile cache stays here, on the
   master, its only writer. */
void drawWalls(MthMatrix *view)
{int i;
 XyInt parms[2];
 int lastWallCmd;
 checkStack();
 setViewer(camera);

 plaxBBxmin=160;
 plaxBBymin=120;
 plaxBBxmax=-160;
 plaxBBymax=-120;

 slave_plaxBBxmin=160;
 slave_plaxBBymin=120;
 slave_plaxBBxmax=-160;
 slave_plaxBBymax=-120;

 lodFused=0; lodCells=0; lodFlat=0; nmMeshPolys=0;
 lodWeldWhy[0]=0; lodWeldWhy[1]=0;
 slave_lodFused=0; slave_lodCells=0; slave_lodFlat=0; slave_nmMeshPolys=0;
 slave_lodWeldWhy[0]=0; slave_lodWeldWhy[1]=0;

 slaveView=view;
 nmPolys=0;
 vdp1NmClipped=0;
 autoTarget=NULL;
 bestAutoAimRating=INT_MAX;

#if WALLPIPE>=2
 if (!pipeDone)     /* the slave did not do it in the tail: do it here */
#endif
    wallsTraverse(view,0);
 pipeDone=0;

 if (slaveSize>updateListSize-1)
    slaveSize=updateListSize-1;
 slaveDrawStart=slaveSize;
 /* start slave */
 *(Uint16 volatile *)0x21000000=0xffff; CFG_PROF("Master Draw");
 for (i=updateListSize-1;i>slaveDrawStart;i--)
    {parms[0].x=updateList[i]->xmin+viewCx;parms[0].y=updateList[i]->ymin+viewCy;
     parms[1].x=updateList[i]->xmax+viewCx;parms[1].y=updateList[i]->ymax+viewCy;
#if RECTCLIP
     EZ_userClip(parms);
#endif
     drawSector(updateList[i]-sectorDraw,view,0);
     drawSprites(&(viewPos),view,updateList[i]-sectorDraw);
    }

 lastWallCmd=EZ_getNextCmdNm()-1;
 /* draw sprites in slave rendered sectors */
 for (;i>=0;i--)
    {int last=EZ_getNextCmdNm();
     drawSprites(&(viewPos),view,updateList[i]-sectorDraw);
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
{int i;
 /* wait for slave to finish */
 i=0; CFG_PROF("Slave Wait");
 while (!(*FTCSR & 0x80))
    i++;
 /* sync */
 *FTCSR=0x0; CFG_PROF_END();
 if (i>100 && slaveSize>0)
    slaveSize--;
 if (i<100 && slaveSize<50)
    slaveSize++;
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
 nmMeshPolys+=slave_nmMeshPolys;
 lodWeldWhy[0]+=slave_lodWeldWhy[0];
 lodWeldWhy[1]+=slave_lodWeldWhy[1];
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

void drawSprites(MthXyz *playerPos,MthMatrix *view,int sector)
{Sprite *o;
 Sprite *drawList[100];
 int nmDraw,draw;
 int chunk,light,x,y,i,j;
 int spriteFog=0,spriteBank=0;
 int flip;
 int frame;
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

 assert(viewSectorequence==-1);
 for (o=CFG_SPR_FIRST(sector);nmDraw<100 && o;o=CFG_SPR_NEXT(o))
    if (o->sequence!=-1 && !(o->flags & SPRITEFLAG_INVISIBLE))
       drawList[nmDraw++]=o;

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

 /* sort */
 for (i=1;i<nmDraw;i++)
    for (j=i;j>0;j--)
       {int d1,d2;
	Sprite *swap;
	d1=f(abs(drawList[j-1]->pos.x-playerPos->x))+
	   f(abs(drawList[j-1]->pos.y-playerPos->y))+
           f(abs(drawList[j-1]->pos.z-playerPos->z));
	d2=f(abs(drawList[j]->pos.x-playerPos->x))+
	   f(abs(drawList[j]->pos.y-playerPos->y))+
           f(abs(drawList[j]->pos.z-playerPos->z));
	if (d1<d2)
	   {swap=drawList[j];
	    drawList[j]=drawList[j-1];
	    drawList[j-1]=swap;
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
	 EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5,o->color,pos,NULL);
	 continue;
	}
     if (o->owner)
	signalObject(o->owner,SIGNAL_VIEW,0,0);
     feetPos.x=o->pos.x;
     feetPos.y=o->pos.y-o->radius;
     if (mpIsPlayer(o))            /* GCC14: a player's pos is its eye, which hovers (SPR_HOVER) */
	feetPos.y-=SPR_HOVER(o);
     feetPos.z=o->pos.z;
     MTH_CoordTrans(view,&feetPos,&tformed);
     if (tformed.z<CFG_SPRITE_NEARCLIP)
	continue;
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
      /* Doom's thing tiles are TILE8BPP (PIC_SLOTS), i.e. colour-bank mode, where gouraud shifts the
	 palette INDEX, not the RGB (HW measure, saturn-refs/knowledge/HW_VDP1.md:783): noise on
	 PLAYPAL.  A darkened bank is the only way, as Lobotomy planned (the commented line above). */
      /* GCC14: a dynamic light reduces the fog before the bank is chosen (strongest channel,
	 same maths as the walls).  Measured at the feet; bank 0 is the ceiling. */
      if (nmLights)
	 {int li,lr,lg,lb,best=0;
	  for (li=0;li<nmLights;li++)
	     {lr=lg=lb=0;
	      lightApply(li,&tformed,&lr,&lg,&lb);
	      if (lr>best) best=lr;
	      if (lg>best) best=lg;
	      if (lb>best) best=lb;
	     }
	  spriteFog-=best;
	  if (spriteFog<0)
	     spriteFog=0;
	 }
      spriteBank=(spriteFog*(NMOBJECTPALLETES-1))/SPRITEFOGMAX;
      if (spriteBank>NMOBJECTPALLETES-1)
	 spriteBank=NMOBJECTPALLETES-1;
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
     /* draw shadow */
     if (!(o->flags & SPRITEFLAG_NOSHADOW))
	{Fixed32 shadowHeight;
	 Fixed32 shadowWidth;
	 Fixed32 shadowScale;
	 XyInt shadowScreenPos;
	 MthXyz shadowPos;
	 shadowPos.x=feetPos.x;
	 shadowPos.z=feetPos.z;
	 shadowPos.y=feetPos.y-findFloorDistance(o->s,&feetPos);
	 shadowScale=(F(128)-abs(shadowPos.y-feetPos.y))>>7;
	 if (shadowScale>0)
	    {MTH_CoordTrans(view,&shadowPos,&tformed);
	     if (tformed.z>F(32) && tformed.z<FARCLIP)
		{project_point(&tformed,&shadowScreenPos);
		 shadowScale=MTH_Mul(shadowScale,scale);
		 shadowWidth=48*shadowScale;
		 shadowHeight=48*shadowScale;

		 shadowHeight=MTH_Mul(shadowHeight,
				      MTH_Div(abs(shadowPos.y-playerPos->y),
					      tformed.z));
		 pos[0].x=shadowScreenPos.x;
		 pos[0].y=shadowScreenPos.y;
		 pos[1].x=f(shadowWidth);
		 pos[1].y=f(shadowHeight);
		 assert(getPicClass(0)!=TILEVDP);
		 EZ_scaleSpr(ZOOM_MM,COLOR_4|COMPO_SHADOW,
			     0,mapPic(0),pos,NULL);
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
     frame=o->frame+level_sequence[o->sequence];
     for (chunk=level_frame[frame].chunkIndex;
	  chunk<level_frame[frame+1].chunkIndex;
	  chunk++)
	{x=level_chunk[chunk].chunkx;
	 y=level_chunk[chunk].chunky;
	 pos[0].x=feetScreenPos.x+f(scale*x);
	 pos[0].y=feetScreenPos.y+f(scale*y);
	 pos[1].x=width64;
	 pos[1].y=width64;
	 flip=0;
	 if (level_chunk[chunk].flags & 1)
	    flip|=DIR_LRREV;
	 if (level_chunk[chunk].flags & 2)
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

	 assert(getPicClass(level_chunk[chunk].tile!=TILEVDP));
	 {int pic=mapPic(level_chunk[chunk].tile);

	  i=getPicClass(level_chunk[chunk].tile);
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
		     gtable.entry[0]=greyTable[g];
		    }
		 }
	      gtable.entry[1]=gtable.entry[0];
	      gtable.entry[2]=gtable.entry[0];
	      gtable.entry[3]=gtable.entry[0];
	      EZ_scaleSpr(ZOOM_TL|flip,
			  UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|
			  DRAW_GOURAU,0,pic,pos,&gtable);
	     }
	  else
	     {if (i==TILESMALL8BPP)
		 {pos[1].x>>=1;
		  pos[1].y>>=1;
		 }
	      EZ_scaleSpr(ZOOM_TL | flip,
			  UCLPIN_ENABLE|COLOR_4|HSS_ENABLE|ECD_DISABLE,
			  (light? light: spriteBank)<<8,pic,pos,  /* light = muzzle flash */
			  NULL);
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
{lightInit();
}

#ifdef GP_GAME_DOOM
/* Which leaf draws a sprite (CFG_SPRITE_LEAVES, CFG_SPR_FIRST/NEXT).  The painter draws a sprite
   right after the leaf holding its centre, and every leaf drawn later paints over it.  A Doom map
   is cut into many small BSP leaves (E1M1: 237), so a monster standing near a leaf boundary has
   half its billboard over the neighbour; when that neighbour is drawn later, its FLOOR covers the
   monster up to the horizon -- the monster looks sunk or squat.  Doom draws every floor before
   any sprite, so a floor never hides one.  Here a sprite is drawn with the LATEST-drawn visible
   leaf it touches: its own, or a neighbour across a portal less than one radius away, its centre
   in front of the portal.  updateList[0] is drawn last (the master draws from the top, the slave
   the rest, WALLS.C:2422-2436).
   Only across a FLUSH portal: same floor and same ceiling on both sides, no step, no lintel --
   a BSP chord through one room.  Moving the sprite after a leaf also stops that leaf's WALLS
   from covering it; across a doorway or a window the jambs and the lintel belong to the
   neighbour and stand in front of the monster (seen on console: monster parts over a wall). */
static int doomFlatY(int s,int up)
{int f;
 for (f=level_sector[s].firstWall;f<=level_sector[s].lastWall;f++)
    if (up? level_wall[f].normal[1]>0: level_wall[f].normal[1]<0)
       return level_vertex[level_wall[f].v[0]].y;
 return 0x7fff;                                      /* no face: a sky ceiling */
}

/* portal w of leaf s into n: bottom = both floors, top = both ceilings */
static int doomFlush(int s,int n,int w)
{int ceil=doomFlatY(s,0),bot=level_vertex[level_wall[w].v[2]].y;
 return bot==doomFlatY(s,1) && bot==doomFlatY(n,1) && ceil==doomFlatY(n,0) &&
	(ceil==0x7fff || level_vertex[level_wall[w].v[1]].y==ceil);
}
/* The reach is one radius PLUS the spill: a leaf paints whole 64-unit squares of floor over its
   neighbour when both are the same Doom sector (doom3d.sols_pleins), so a monster up to a cell
   away from the chord can be covered too. */
#define DOOM_FLOOR_SPILL F(64)
#define DOOM_MAXDRAWSPRITES 450                      /* SPRITE.C:12 MAXNMSPRITES */
Sprite *doomDrawHead[MAXNMSECTORS];
Sprite *doomDrawNext[DOOM_MAXDRAWSPRITES];
static short doomDrawRank[MAXNMSECTORS];             /* updateList index + 1, 0 = not drawn */
static short doomRanked[MAXNMSECTORS];
static int doomNmRanked;

void doom_spriteLeaves(void)
{int i,s,t,n,w;
 Sprite *o;
 MthXyz p;
 Fixed32 d,c;
 for (i=0;i<doomNmRanked;i++)
    {doomDrawRank[doomRanked[i]]=0;
     doomDrawHead[doomRanked[i]]=NULL;
    }
 doomNmRanked=updateListSize;
 for (i=0;i<updateListSize;i++)
    {s=updateList[i]-sectorDraw;
     doomRanked[i]=(short)s;
     doomDrawRank[s]=(short)(i+1);
    }
 for (i=0;i<updateListSize;i++)
    {s=doomRanked[i];
     for (o=sectorSpriteList[s];o;o=o->next)
	{t=s;
	 for (w=level_sector[s].firstWall;w<=level_sector[s].lastWall;w++)
	    {n=level_wall[w].nextSector;
	     if (n==-1)
		break;                          /* portals come first (SPRITE.C:412) */
	     if (!doomDrawRank[n] || doomDrawRank[n]>=doomDrawRank[t])
		continue;
	     if (F(level_vertex[level_wall[w].v[2]].y)>=o->pos.y+o->radius ||
		 F(level_vertex[level_wall[w].v[1]].y)<=o->pos.y-o->radius)
		continue;                       /* portal above or below the body */
	     getVertex(level_wall[w].v[0],&p);
	     d=MTH_Mul(o->pos.x-p.x,level_wall[w].normal[0])+
	       MTH_Mul(o->pos.z-p.z,level_wall[w].normal[2]);
	     if (d>=o->radius+DOOM_FLOOR_SPILL)
		continue;
	     c=MTH_Mul(p.x-o->pos.x,level_wall[w].normal[2])+
	       MTH_Mul(o->pos.z-p.z,level_wall[w].normal[0]);
	     if (c<0 || c>F(level_wall[w].pixelLength))
		continue;                       /* beside the portal, not across it */
	     if (doomFlush(s,n,w))
			t=n;
	    }
	 assert(o-sprites>=0 && o-sprites<DOOM_MAXDRAWSPRITES);
	 doomDrawNext[o-sprites]=doomDrawHead[t];
	 doomDrawHead[t]=o;
	}
    }
}
#endif

/* HWFLOOR.C -- the dominant floor on VDP2 (GCC14).

   ONE plane of the image is elected per frame and handed to RBG0: every floor quad of every
   visible sector that carries it stops being drawn by the VDP1, and the rotation plane shows
   the flat instead.  The pattern is the sky's, WALLFLAG_PARALLAX (WALLS.C:2810): the painter
   skips the wall, keeps its screen box, and a VDP2 window opens the plane over exactly that
   box.  The difference is that the sky's walls are marked in the level and this election is
   made afresh every image.

   WHAT IS ELECTED, and why that and not something else (tools/study/soldom_secteur.py, nine
   maps of episode 1, the grid of standing positions):
     * the unit is the PLANE (floor height, tile), never a quad.  A Doom sector has one height
       and one flat, and a flat is one tile -- measured, no sector of the nine carries two --
       so electing the plane elects whole sectors, and with them every other sector at the same
       height with the same texture;
     * the plane taken is the LARGEST OF THE IMAGE, not the one under the player's feet: the
       foot plane rendered 3 to 5 points less on eight maps of nine;
     * EVERY sector of the couple joins, with no light term -- see hwFloorChoose for why the
       first version's tolerance on the mean vertex light was the defect, not the safeguard;
     * the election is LATCHED on the sector the player stands in, which is what Mimas found
       after dropping its own per-frame dominant pick: a margin only softens the flicker, a
       latch removes it.
   What it is worth, in cells the VDP1 no longer walks: median 22.5 % of the image (E1M1 22.5,
   E1M3 24.9, E1M8 70.0), 180 to 1561 cells, 4.3 to 37.5 ms at the marginal 24 us of
   tools/cout.py.

   THE PLANE ITSELF.  Screen dot (c,v) below the horizon v0 sees the floor at
   z = h.f/(v-v0) and x = (c-c0).h/(v-v0), so with k(v) = h/(v-v0) the world point is
       wx = k.(cos.c + f.sin - c0.cos) + camera.x
       wz = k.(-sin.c + f.cos + c0.sin) + camera.z
   which is the VDP2's own X = kx.(Xsp + dX.H) + Xp (ST-058 6.1) with A = cos, B = sin,
   D = -sin, E = cos, Xst = -c0, Yst = f, Mx/My = the camera, and every one of Px, Py, Pz, Cx,
   Cy, Cz zero so that Xp is Mx alone.  k is the coefficient table, one entry a line.
   RGB cells, not paletted: the CRAM has no bank left (PIC.C) and a flat is 8 KB either way.
   Characters in VRAM B0 and the page of names in B1, separate banks because the plane fetches
   a name and a character for the same dot. */
#include <stdlib.h>
#include <string.h>
#include <sega_scl.h>
#include <sega_mth.h>
#include "util.h"
#include "spr.h"
#include "sprite.h"
#include "slevel.h"
#include "level.h"
#include "walls.h"
#include "pic.h"
#include "hwfloor.h"

void vdp2CycleTable(void);      /* SRUINS.C */

extern SclRotreg *SclRotregBuff;
extern Sprite *camera;

#define HWFLOOR_MINAREA    2000     /* screen area under which a plane is not worth it */
#define HWFLOOR_MAXAREA    40000    /* one sector's box is capped at a third of the view */

/* VDP2 VRAM, offsets from SCL_VDP2_VRAM.  A0 stays the rotation's (coefficient table at 0, the
   parameter table at 0x500, as the sky had it); B0 and B1 become the plane's. */
#define KTAB_ADDR    0x00000        /* A0: one longword a line                                */
#define RPT_ADDR     0x00500        /* A0: the rotation parameter table                       */
#define BMP_ADDR     0x40000        /* B0: the plane, 512 x 256 at 8 bpp                       */
#define LINEWIN_ADDR 0x60000        /* B1: the line window, 2 words a raster line              */
#define VW(o)        ((volatile Uint16 *)(SCL_VDP2_VRAM+(o)))
#define VL(o)        ((volatile Uint32 *)(SCL_VDP2_VRAM+(o)))

int hwFloorOn;
int hwFloorCells;                   /* cells the election took out of this image (overlay)     */
int hwFloorPlane=-1;                /* the plane held, an index into plane[]                   */
unsigned char hwFloorSector[MAXNMSECTORS];
short hwFloorBBxmin,hwFloorBBymin,hwFloorBBxmax,hwFloorBBymax;

/* THE HOLE IS NOT A RECTANGLE.  One box over every dropped quad is far too generous the moment
   the floor is anything but a room: the plane then shows through doorways, over pits and above
   low walls, and it buried the sky.  The VDP2 gives exactly the right tool -- a LINE window,
   one [start, end] a raster line (SCL_SetLineWindow) -- which is the same idea as Mimas's
   per-column punch, turned a quarter to match the hardware.  Each dropped quad widens the span
   of the lines it covers; a line no quad reached stays shut and the sky comes through it. */
static short lineL[HWFLOOR_LINES],lineR[HWFLOOR_LINES];
static SclLineWindowTb lineWin[HWFLOOR_LINES];

static void hwFloorClear(void)
{int y;
 hwFloorBBxmin=160; hwFloorBBymin=120;
 hwFloorBBxmax=-160; hwFloorBBymax=-120;
 for (y=0;y<HWFLOOR_LINES;y++)
    {lineL[y]=0x3fff; lineR[y]=-1;}
}


/* one entry a plane of the level: a (height, tile) couple that at least one sector carries */
static struct {short y; short tile; short light; short first;} plane[HWFLOOR_MAXPLANES];
static short nmPlanes;
static short secPlane[MAXNMSECTORS]; /* -1 = the sector has no floor to give                   */
static short secCells[MAXNMSECTORS]; /* ... and how many quads it would have drawn             */
static short secVertex[MAXNMSECTORS];/* its floor's first vertex: re-read to catch a lift      */
static int tally[HWFLOOR_MAXPLANES];
static int held;                    /* cells the held plane earns in THIS image                */
static int loadedTile=-1;           /* the tile whose texels are in B0                         */
static int loadedLit=-1;            /* ... at that light level                                 */
static int floorHeight;             /* the elected plane's world height                        */
static int lastViewSector=-1;       /* the election is latched while the player stays in it    */

/* ---- the level's planes, once ------------------------------------------------------------ */

/* A floor wall's plane and its weight.  A grid wall (PARALLELOGRAM) carries its tiles in
   level_texture, two bytes a cell; a mesh wall carries one in each face.  They agree inside a
   sector -- measured on the nine maps -- so the first is the sector's. */
static void sectorPlane(int si)
{sSectorType *s=level_sector+si;
 int w,n=0,cells=0,lsum=0,lnm=0,y=0,tile=-1,vtx=0;
 for (w=s->firstWall;w<=s->lastWall;w++)
    {sWallType *wl=level_wall+w;
     int i,c;
     if (wl->normal[1]<=0 || (wl->flags & (WALLFLAG_PARALLAX|WALLFLAG_INVISIBLE)))
	continue;                                        /* not a floor, or nothing is drawn */
     if (wl->flags & WALLFLAG_PARALLELOGRAM)
	{c=wl->tileLength*wl->tileHeight;
	 if (!c)
	    continue;
	 if (tile<0)
	    {vtx=wl->v[0];
	     y=level_vertex[vtx].y;
	     tile=level_texture[wl->textures+1];
	    }
	 for (i=0;i<(wl->tileHeight+1)*(wl->tileLength+1);i++)
	    {lsum+=level_vertexLight[wl->firstLight+i]; lnm++;}
	}
     else if (wl->firstFace>=0)
	{c=wl->lastFace-wl->firstFace+1;
	 if (tile<0)
	    {vtx=wl->firstVertex+level_face[wl->firstFace].v[0];
	     y=level_vertex[vtx].y;
	     tile=level_face[wl->firstFace].tile;
	    }
	 for (i=wl->firstFace;i<=wl->lastFace;i++)
	    {int k;
	     for (k=0;k<4;k++)
		{lsum+=level_vertexLight[wl->firstLight+level_face[i].v[k]]; lnm++;}
	    }
	}
     else
	continue;
     cells+=c;
     n++;
    }
 secCells[si]=(short)cells;
 secPlane[si]=-1;
 secVertex[si]=(short)vtx;
 if (!n || tile<0 || !cells)
    return;
 for (w=0;w<nmPlanes;w++)
    if (plane[w].y==y && plane[w].tile==tile)
       {secPlane[si]=(short)w;
	return;
       }
 if (nmPlanes>=HWFLOOR_MAXPLANES)
    return;                                          /* more planes than the table holds: none */
 plane[nmPlanes].y=(short)y;
 plane[nmPlanes].tile=(short)tile;
 plane[nmPlanes].light=(short)(lnm? (lsum/lnm): 0);
 plane[nmPlanes].first=(short)si;
 secPlane[si]=nmPlanes++;
}

/* ---- the plane's VDP2 set-up ------------------------------------------------------------- */

/* A 512 x 256 bitmap of 8 bpp, repeated by OVER_0 -- the same shape the sky has had since the
   first day (PLAX.C initPlax), and the one Mimas ships after trying cells: its cell path reads
   a pattern name AND a character for every dot, which starved the rotation and snowed on real
   hardware, while the bitmap reads one.  The flat is 64 x 64, so it goes down 8 x 4 times.
   The palette is CRAM bank 6: banks 0..5 are the object's (UTIL.H NMOBJECTPALLETES) and 7 is
   the sky's, so 6 is the one nobody claims. */
#define FLOOR_BANK   6
static void loadFlat(int tile,int lit)
{const short *pal;
 const unsigned char *px=picTexels(tile,&pal);
 int i,y,x;
 if (!px || !pal)
    return;
 /* THE PLANE'S TONE.  RBG0 has no gouraud, so the light the VDP1 would have put on these
    quads per vertex has to go into the 256 entries instead -- the same trick the sky uses to
    fade (PLAX.C setPlaxFade), and the same one Mimas settled on after finding the palette-bank
    switch dead on hardware: bake the level into the texels, re-done only when the flat or the
    level changes.  `lit` is the plane's mean vertex light out of the 32 worldGrey steps. */
 for (i=0;i<256;i++)
    {unsigned int c=(unsigned int)(unsigned short)pal[i];
     unsigned int r=((c&31)*lit)>>5,
		  g=(((c>>5)&31)*lit)>>5,
		  b=(((c>>10)&31)*lit)>>5;
     POKE_W(SCL_COLRAM_ADDR+((256*FLOOR_BANK+i)<<1),(c&0x8000)|(b<<10)|(g<<5)|r);
    }
 /* one row of the flat, laid 8 times across, then the 64 rows four times down.  Two texels a
    write: the bitmap is bytes and VDP2 VRAM takes words. */
 for (y=0;y<64;y++)
    {volatile Uint16 *o=VW(BMP_ADDR)+(y<<8);
     const unsigned char *src=px+(y<<6);
     for (x=0;x<32;x++)
	{Uint16 t=(Uint16)(((Uint16)src[x<<1]<<8)|src[(x<<1)+1]);
	 o[x]=t; o[x+32]=t; o[x+64]=t; o[x+96]=t;
	 o[x+128]=t; o[x+160]=t; o[x+192]=t; o[x+224]=t;
	}
    }
 for (y=64;y<256;y++)
    {volatile Uint16 *o=VW(BMP_ADDR)+(y<<8);
     const volatile Uint16 *src=VW(BMP_ADDR)+((y&63)<<8);
     for (x=0;x<256;x++)
	o[x]=src[x];
    }
 loadedTile=tile;
 loadedLit=lit;
}

void hwFloorLevelStart(void)
{SclConfig scfg;
 SclVramConfig vcfg;
 int i;
 nmPlanes=0;
 hwFloorOn=0; hwFloorPlane=-1; held=0; loadedTile=-1; lastViewSector=-1;
 for (i=0;i<level_nmSectors;i++)
    sectorPlane(i);

 /* B0 leaves the loading screen (DOOM_TITLE.C) and becomes the plane's bitmap.  A1 keeps the
    sky, A0 the coefficient and parameter tables, B1 stays free. */
 SCL_InitVramConfigTb(&vcfg);
 vcfg.vramModeA=ON;
 vcfg.vramModeB=ON;
 vcfg.vramA0=SCL_RBG0_K;
 vcfg.vramA1=SCL_NON;
 vcfg.vramB0=SCL_RBG0_CHAR;
 vcfg.vramB1=SCL_NON;
 SCL_SetVramConfig(&vcfg);
 vdp2CycleTable();          /* it recomputed the access pattern: put setVDP2's back */

 SCL_InitRotateTable(SCL_VDP2_VRAM+RPT_ADDR,1,SCL_RBG0,SCL_NON);
 SCL_InitConfigTb(&scfg);
 scfg.dispenbl=ON;
 scfg.bmpsize=SCL_BMP_SIZE_512X256;
 scfg.coltype=SCL_COL_TYPE_256;
 scfg.datatype=SCL_BITMAP;
 scfg.mapover=SCL_OVER_0;                  /* the plane repeats: a floor has no edge */
 scfg.plate_addr[0]=BMP_ADDR;
 scfg.patnamecontrl=0;
 SCL_SetConfig(SCL_RBG0,&scfg);
 SCL_SET_R0CAOS(FLOOR_BANK);
 /* 2, not 1: the sky is NBG0 at 1 and CFG_SKY_FULLWINDOW opens its window over the WHOLE
    3D view, and equal priorities are broken in the normal screens' favour (NBG0 before
    RBG0) -- the floor would lose everywhere the sky is armed, which is everywhere
    outdoors.  A floor belongs in front of the sky anyway. */
 SCL_SetPriority(SCL_RBG0,2);

 Scl_r_reg.k_contrl=0x1;                   /* parameter A reads the coefficient table */
 Scl_r_reg.k_offset=0;
 /* THE TWO STEPS ARE NOT NAMED WHAT THEY ARE.  The rotation table holds KAst, then dKAst (the
    step per LINE), then dKAx (the step per DOT); SclRotreg calls them k_tab, k_delta.x and
    k_delta.y, so .x is the LINE step and .y the DOT step -- the opposite of what the names
    suggest, and PLAX.C's own author left "are these backwards??" beside them.
    Proof, not guesswork: the retail sky's table is 320 entries, symmetric about the 160th,
    0.80 at the edges and 1.02 in the middle -- an atan correction across the screen's WIDTH.
    320 entries for 320 COLUMNS, so the sky steps per dot, and PLAX sets .y.
    Copying the sky gave this floor a table read per COLUMN: every line re-read the same
    entries, Y depended on the column alone, and the plane came out as vertical stripes over
    the whole view whatever the values were (console, 2026-09-26 -- the fix to their SCALE
    changed nothing, which is what pointed here). */
 SclRotregBuff->k_tab=KTAB_ADDR;
 SclRotregBuff->k_delta.x=1<<16;           /* dKAst: one coefficient a LINE */
 SclRotregBuff->k_delta.y=0;               /* dKAx:  none per dot           */
 hwFloorOff();
}

void hwFloorOff(void)
{hwFloorOn=0;
 Scl_s_reg.dispenbl&=~0x0010;              /* R0ON */
 if (SclProcess==0)
    SclProcess=1;
}

/* ---- the election, once an image --------------------------------------------------------- */

void hwFloorBegin(void)
{if (nmPlanes)
    memset(tally,0,nmPlanes*sizeof(tally[0]));
 held=0;
 hwFloorCells=0;
}

/* A SECTOR IS SEEN.  Two things the first version got wrong, both visible on console.

   IT IS WEIGHED BY ITS AREA ON SCREEN, not by its count of cells.  Cells are cheap far away
   and dear under the eye, and the plane is worth having exactly where the floor is big: the
   near one.  Taking the near floor is also what lets the VDP1 degrade the far one -- fuse it,
   drop its tile, paint it flat -- without anyone seeing, which is the whole reason to want the
   near plane rather than the widest one.  The sector's screen box is the proxy; a near floor's
   box dwarfs a distant one's, so no extra term is needed to prefer it.

   AND ITS HEIGHT IS RE-READ, every image.  The plane table is built once at level start, but a
   lift's floor MOVES: its sector still claimed the plane's height after it had risen, so its
   quads were dropped and the plane was seen through the lift.  One vertex read says whether the
   sector is still on the plane it was born on. */
void hwFloorSee(int sectorNm,int w,int h)
{int p=secPlane[sectorNm],a;
 if (p<0)
    return;
 if (level_vertex[secVertex[sectorNm]].y!=plane[p].y)
    return;                                          /* it moved: a lift, a rising floor */
 a=(w+1)*(h+1);
 if (a>HWFLOOR_MAXAREA)
    a=HWFLOOR_MAXAREA;                               /* one huge box must not swamp the tally */
 tally[p]+=a;
}

/* THE LIGHT TERM IS GONE, and it has to be said why: it was the defect the owner saw.  A
   Doom sector's light reaches a .LEV as a value PER VERTEX, already carrying the distance
   shading, so the mean over one BSP leaf of a big floor differs from its neighbour's by more
   than any sane tolerance -- and the star of E1M8 came out in pieces, each leaf either in or
   out.  Mimas kept a light term (the plane's light BAND) and then measured that it almost
   never held a plane back; here it does nothing but tear the coverage.  The elected plane
   therefore takes EVERY sector of its (height, tile) couple, and wears one tone.

   The election is LATCHED ON THE VIEW SECTOR, not on a margin.  Mimas dropped its per-frame
   dominant pick for exactly the flicker a margin only softens: re-electing when the player
   changes sector is both cheaper and steadier. */
void hwFloorChoose(int viewSector)
{int p,best=-1,bestN=0,i;
 if (hwFloorPlane>=0 && viewSector==lastViewSector && tally[hwFloorPlane]>=HWFLOOR_MINAREA)
    {held=tally[hwFloorPlane];                   /* still in the same room: keep the plane */
     for (i=0;i<level_nmSectors;i++)             /* ... but a lift may have left it since */
	hwFloorSector[i]=(unsigned char)
	   (secPlane[i]==hwFloorPlane &&
	    level_vertex[secVertex[i]].y==plane[hwFloorPlane].y);
     hwFloorOn=1;
     hwFloorBBxmin=160; hwFloorBBymin=120;
     hwFloorBBxmax=-160; hwFloorBBymax=-120;
     return;
    }
 lastViewSector=viewSector;
 for (p=0;p<nmPlanes;p++)
    if (tally[p]>bestN)
       {bestN=tally[p]; best=p;}
 if (best<0 || bestN<HWFLOOR_MINAREA)
    {hwFloorOn=0;
     hwFloorPlane=-1;
     return;
    }
 hwFloorPlane=best;
 held=bestN;
 floorHeight=plane[best].y;
 hwFloorOn=1;
 for (i=0;i<level_nmSectors;i++)
    hwFloorSector[i]=(unsigned char)(secPlane[i]==best &&
				     level_vertex[secVertex[i]].y==plane[best].y);
 if (plane[best].tile!=loadedTile || plane[best].light!=loadedLit)
    loadFlat(plane[best].tile,plane[best].light>32? 32: plane[best].light);
 hwFloorClear();
}

/* a dropped quad, by its screen box: widen every line it covers */
void hwFloorQuad(int x0,int y0,int x1,int y1)
{int y;
 if (x0<hwFloorBBxmin) hwFloorBBxmin=(short)x0;
 if (y0<hwFloorBBymin) hwFloorBBymin=(short)y0;
 if (x1>hwFloorBBxmax) hwFloorBBxmax=(short)x1;
 if (y1>hwFloorBBymax) hwFloorBBymax=(short)y1;
 y0+=CFG_YCENTER;
 y1+=CFG_YCENTER;
 if (y0<0) y0=0;
 if (y1>=HWFLOOR_LINES) y1=HWFLOOR_LINES-1;
 x0+=160+viewOrgOff;
 x1+=160+viewOrgOff;
 if (x0<viewOrgOff) x0=viewOrgOff;
 if (x1>319+viewOrgOff) x1=319+viewOrgOff;
 if (x1<x0)
    return;
 for (y=y0;y<=y1;y++)
    {if (x0<lineL[y]) lineL[y]=(short)x0;
     if (x1>lineR[y]) lineR[y]=(short)x1;
    }
}

/* what the overlay's hw: row shows */
int hwFloorPlaneCount(void)
{return nmPlanes;
}

int hwFloorY(void)
{return hwFloorOn? floorHeight: 0;
}

/* ---- the plane, once an image ------------------------------------------------------------ */

/* k(v) = h / (v - v0), in the coefficient's 16 fractional bits, bit 31 marking the lines the
   plane must not touch -- everything at or above the horizon, and everything when the camera
   is level with the floor.  The table is walked from the top of the screen, so the index is
   the raster line and v0 is where the horizon falls in it. */
static void writeK(int h,int v0)
{int v;
 for (v=0;v<HWFLOOR_LINES;v++)
    {int d=v-v0;
     if (h<=0 || d<HWFLOOR_NEAR)
	VL(KTAB_ADDR)[v]=0x80000000;           /* transparent: no floor on this line */
     else
	{int k=(h<<16)/d;                      /* 16 fractional bits, as the retail sky's table
						  was built (PLAX.C, the commented generator:
						  f*65536 masked to 0x007fffff) */
	 if (k>0x007fffff)
	    k=0x007fffff;
	 VL(KTAB_ADDR)[v]=(Uint32)k;
	}
    }
}

void hwFloorFrame(Fixed32 yaw,int camx,int camy,int camz,int horizon)
{int h;
 if (!hwFloorOn)
    {hwFloorOff();
     return;
    }
 h=camy-floorHeight;
 if (h<=0 || hwFloorBBxmin>hwFloorBBxmax || hwFloorBBymin>hwFloorBBymax)
    {hwFloorOff();                                  /* under the plane, or nothing showed it */
     return;
    }
 /* k is h/(v-v0) and NOTHING else.  The focal is already in Yst below -- multiplying it in
    here too put the coefficient at 143 where the field holds 128, so every line clamped to
    the same value, Y stopped changing down the screen, and the plane came out as VERTICAL
    STRIPES over the whole view (console, 2026-09-26). */
 writeK(h,horizon);

 /* The signs come from the engine's OWN view matrix, not from a guess: SRUINS.C builds
    RotY(yaw) applied to (world - camera), so x_cam = cos.dx + sin.dz and z_cam = -sin.dx +
    cos.dz.  Inverting it, dx = cos.x_cam - sin.Z and dz = sin.x_cam + cos.Z, which puts
    -sin in B and +sin in D.  The first version had both the other way round, so the plane
    turned against the view and its two axes were swapped -- the scroll the owner saw. */
 {Fixed32 co=MTH_Cos(yaw),si=MTH_Sin(yaw);
  int c0=160+viewOrgOff;
  SclRotregBuff->matrix_a=co;
  SclRotregBuff->matrix_b=-si;
  SclRotregBuff->matrix_c=0;
  SclRotregBuff->matrix_d=si;
  SclRotregBuff->matrix_e=co;
  SclRotregBuff->matrix_f=0;
  SclRotregBuff->screenst.x=F(-c0);
  SclRotregBuff->screenst.y=F(HWFLOOR_FOCAL);
  SclRotregBuff->screenst.z=0;
  SclRotregBuff->screendlt.x=0;                     /* Xst and Yst hold for every line */
  SclRotregBuff->screendlt.y=0;
  SclRotregBuff->delta.x=F(1);                      /* one screen dot, one step of H */
  SclRotregBuff->delta.y=0;
  SclRotregBuff->viewp.x=0; SclRotregBuff->viewp.y=0; SclRotregBuff->viewp.z=0;
  SclRotregBuff->rotatecenter.x=0;
  SclRotregBuff->rotatecenter.y=0;
  SclRotregBuff->rotatecenter.z=0;
  SclRotregBuff->move.x=F(camx);                    /* Xp is Mx alone: P and C are zero */
  SclRotregBuff->move.y=F(camz);
  SclRotregBuff->zoom.x=F(1);
  SclRotregBuff->zoom.y=F(1);
 }
 {int y;
  for (y=0;y<HWFLOOR_LINES;y++)
     {if (lineL[y]>lineR[y] || y<horizon)
	 {lineWin[y].start=0x3ff;          /* start past end: this line shows no plane */
	  lineWin[y].end=0;
	 }
      else
	 {lineWin[y].start=(Uint16)lineL[y];
	  lineWin[y].end=(Uint16)lineR[y];
	 }
     }
  SCL_SetLineWindow(SCL_W1,0,SCL_RBG0,0xfffffff,SCL_VDP2_VRAM+LINEWIN_ADDR,0,
		    HWFLOOR_LINES,lineWin);
 }
 Scl_s_reg.dispenbl|=0x0010;                        /* R0ON */
 Scl_s_reg.dispenbl|=0x1000;                        /* R0TPON: the floor is opaque */
 if (SclProcess==0)
    SclProcess=1;
}

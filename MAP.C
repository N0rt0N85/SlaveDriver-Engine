#include <machine.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_int.h>
#include <sega_mth.h>
#include <sega_sys.h>
#include <sega_dbg.h>
#include <sega_per.h>

#include "util.h"
#include "spr.h"
#include "level.h"
#include "sprite.h"
#include "sruins.h"
#include "print.h"
#include "bigmap.h"
#include "gamestat.h"
#include "local.h"
#include "walls.h"     /* GCC14: viewXmin..viewCy -- the window of the view being drawn */
#include "mplayer.h"   /* ... and who is looking at it */
#include "map.h"

#define MINX (-160)
#define MAXX (160)
#define MINY (CFG_YMIN)   /* -110; GP_GAME_DOOM -112 (224-line frame, SPRITE.H) */
#define MAXY (CFG_YMAX)   /* 90;   GP_GAME_DOOM 80 */

char mapColor[MAXNMWALLS];

/* GCC14: THE WINDOW THE MAP IS DRAWN IN.  Solo keeps the four numbers above, to the pixel and
   for both games -- they are not the renderer's window (PowerSlave centres its view on line 120
   and its map on 110).  In SPLIT SCREEN each view draws ITS OWN map in ITS OWN half, and there
   the window IS the renderer's: viewXmin..viewYmax are what mpSetViewport just computed for this
   view (WALLS.H), so the map lands exactly where the view it replaces was. */
static short mapXmin=MINX,mapXmax=MAXX,mapYmin=MINY,mapYmax=MAXY;

static Fixed32 mapScale;
static int gotFullMap;

/* MPLAYER.H: each player zooms ITS OWN map.  mapOn is registered by the engine (SRUINS.C
   mpRegisterEngine), the scale belongs here. */
void mapMpRegister(void)
{MPREG(mapScale);
}

void mapScaleUp(void)
{mapScale=MTH_Mul(mapScale,60000);
 if (mapScale<2000)
    mapScale=2000;
}

void mapScaleDn(void)
{mapScale=MTH_Mul(mapScale,70000);
 if (mapScale>20000)
    mapScale=20000;
}

void revealMap(void)
{gotFullMap=1;
}

/* GCC14: whether revealMap has run on this level -- Doom's computer map is taken once
   (P_GivePower refuses pw_allmap to a player who has it) */
int mapRevealed(void)
{return gotFullMap;
}

void initMap(void)
{int s,w;
 MthXyz t;
 mapScale=F(1)/8;
 gotFullMap=0;
 for (s=0;s<level_nmSectors;s++)
    {for (w=level_sector[s].firstWall;w<=level_sector[s].lastWall;w++)
	{mapColor[w]=0;
	 if (level_wall[w].normal[1]!=0)
	    continue;
#ifdef GP_GAME_DOOM
	 /* AM_drawWalls (am_map.c) by wall kind (SPEC_PLAYER 3.7): 1 = one-sided line (REDS),
	    2 = floor step (BROWNS), 3 = ceiling step (YELLOWS), 0 = not drawn.  One-sided = no
	    nextSector; only the face that spans floor to ceiling is drawn, as below (a step face
	    under a portal would repaint that portal red).  The heights compared are the vertex
	    distances to each side's floor/ceiling (sSectorType has no ceiling level). */
	 if (level_wall[w].nextSector==-1)
	    {if (findCeilDistance(s,getVertex(level_wall[w].v[0],&t))<F(1) &&
		 findFloorDistance(s,getVertex(level_wall[w].v[3],&t))<F(1) &&
		 findCeilDistance(s,getVertex(level_wall[w].v[1],&t))<F(1) &&
		 findFloorDistance(s,getVertex(level_wall[w].v[2],&t))<F(1))
		mapColor[w]=1;
	     continue;
	    }
	 {int ns=level_wall[w].nextSector,k;
	  Fixed32 df=0,dc=0,d;
	  for (k=0;k<2;k++)
	     {d=abs(findFloorDistance(s,getVertex(level_wall[w].v[3-k],&t))-
		    findFloorDistance(ns,getVertex(level_wall[w].v[3-k],&t)));
	      if (d>df) df=d;
	      d=abs(findCeilDistance(s,getVertex(level_wall[w].v[k],&t))-
		    findCeilDistance(ns,getVertex(level_wall[w].v[k],&t)));
	      if (d>dc) dc=d;
	     }
	  if (df>F(1)/2)
	     mapColor[w]=2;
	  else if (dc>F(1)/2)
	     mapColor[w]=3;
	 }
	 continue;
#endif
	 if (/*level_wall[w].nextSector==-1*/
	     level_wall[w].flags & WALLFLAG_BLOCKED)
	    {/* only draw wall if goes from floor to ceiling */
	     if (findCeilDistance(s,getVertex(level_wall[w].v[0],&t))<F(1) &&
		 findFloorDistance(s,getVertex(level_wall[w].v[3],&t))<F(1) &&
		 findCeilDistance(s,getVertex(level_wall[w].v[1],&t))<F(1) &&
		 findFloorDistance(s,getVertex(level_wall[w].v[2],&t))<F(1))
		mapColor[w]=1;
	     continue;
	    }
	 else
	    {/* only draw wall if floor is different heights on both sides */
	     Fixed32 minDiff,f;
	     minDiff=
		abs(findFloorDistance(s,getVertex(level_wall[w].v[3],&t))-
		    findFloorDistance(level_wall[w].nextSector,
				      getVertex(level_wall[w].v[3],&t)));
	     f=abs(findFloorDistance(s,getVertex(level_wall[w].v[2],&t))-
		   findFloorDistance(level_wall[w].nextSector,
				     getVertex(level_wall[w].v[2],&t)));
	     if (f>minDiff)
		minDiff=f;

	     if (minDiff>F(1))
		{mapColor[w]=2;
		 if (minDiff<F(32))
		    mapColor[w]=3;
		}
	     continue;
	    }
	}
    }
}

#ifndef NDEBUG
#define MAXMARKS 5
static int markX[MAXMARKS],markY[MAXMARKS];
static int nmMarks=0;
void setMark(Fixed32 x,Fixed32 y)
{if (nmMarks==MAXMARKS)
    return;
 markX[nmMarks]=x;
 markY[nmMarks]=y;
 nmMarks++;
}
#else
void setMark(Fixed32 x,Fixed32 y)
{return;
}
#endif

static unsigned short colors[4]=
   {0,
    0x8000| 31<<10 | 31<<5 | 31,
    0x8000| 25<<10 | 25<<5 | 25,
    0x8000| 18<<10 | 18<<5 | 18};

#ifdef GP_GAME_DOOM
/* am_map.c colour indices (am_map.c:50-83): mapColor 1..3, then allmap grey and the arrow */
#define DOOM_AM_REDS      176           /* WALLCOLORS                                          */
#define DOOM_AM_BROWNS    64            /* FDWALLCOLORS                                        */
#define DOOM_AM_YELLOWS   231           /* CDWALLCOLORS                                        */
#define DOOM_AM_GRAYS3    (96+3)        /* GRAYS+3: lines revealed by the computer map          */
#define DOOM_AM_WHITE     209           /* YOURCOLORS                                          */

/* PLAYPAL entry -> RGB colour word, read from CRAM bank 0 (the object palette of the .LEV =
   PLAYPAL, copied there by loadPalletes, PIC.C:630-634): no palette data in the source */
static unsigned short doomMapColor(int index)
{return ((unsigned short *)SCL_COLRAM_ADDR)[index] | 0x8000;
}

/* GCC14: what the automap leaves of the command list for what is drawn after it (the arrow, the
   status bar, the message, the level's title: one command a glyph).  The view is not drawn
   under the map (SRUINS.C CFG_MAP_HIDES_VIEW), so the map has the list to itself, but a whole
   level zoomed out still has more walls than the list has commands (E1M6: 1 508 walls one-sided
   alone, the list 1 448): past this, the rest of the walls is left out. */
#define DOOM_AM_RESERVE 160

/* Doom automap walls: black view (AM_clearFB), then the classified walls of the seen leaves in
   their Doom colour; unseen leaves only with the computer map, in grey.  Same projection and
   clipping as the engine loop below. */
static void doomDrawMapWalls(int cx,int cy,MthXyz *north,MthXyz *east)
{int s,w,color;
 unsigned short pal[4];
 XyInt mapLine[4];
 MthXyz p1,p2,wallP;

 pal[0]=0;
 pal[1]=doomMapColor(DOOM_AM_REDS);
 pal[2]=doomMapColor(DOOM_AM_BROWNS);
 pal[3]=doomMapColor(DOOM_AM_YELLOWS);
 colors[1]=doomMapColor(mpPlayers>1? doom_playerMapColor(mpCur): DOOM_AM_WHITE);
                                              /* the player arrow drawn by drawMap: its OWN
						 colour in split, Doom's white when alone */

 mapLine[0].x=mapXmin; mapLine[0].y=mapYmin;
 mapLine[1].x=mapXmax; mapLine[1].y=mapYmin;
 mapLine[2].x=mapXmax; mapLine[2].y=mapYmax;
 mapLine[3].x=mapXmin; mapLine[3].y=mapYmax;
 EZ_polygon(ECD_DISABLE|SPD_DISABLE,RGB(0,0,0),mapLine,NULL);

 for (s=0;s<level_nmSectors;s++)
    {if (level_sector[s].flags & SECFLAG_NOMAP)
	continue;
     if (!(level_sector[s].flags & SECFLAG_SEEN) && !gotFullMap)
	continue;
     for (w=level_sector[s].firstWall;w<=level_sector[s].lastWall;w++)
	{if (!mapColor[w])
	    continue;
	 if (level_sector[s].flags & SECFLAG_SEEN)
	    color=pal[(int)mapColor[w]];
	 else
	    color=doomMapColor(DOOM_AM_GRAYS3);
	 getVertex(level_wall[w].v[0],&wallP);
	 p1.x=f(wallP.x-cx);
	 p1.y=f(wallP.z-cy);
	 getVertex(level_wall[w].v[1],&wallP);
	 p2.x=f(wallP.x-cx);
	 p2.y=f(wallP.z-cy);

	 mapLine[0].x=f(p1.x*north->x+p1.y*north->y);
	 mapLine[0].y=f(p1.x*east->x+p1.y*east->y);
	 mapLine[1].x=f(p2.x*north->x+p2.y*north->y);
	 mapLine[1].y=f(p2.x*east->x+p2.y*east->y);
	 if (mapLine[0].x<mapXmin && mapLine[1].x<mapXmin)
	    continue;
	 if (mapLine[0].x>mapXmax && mapLine[1].x>mapXmax)
	    continue;
	 if (mapLine[0].y<mapYmin && mapLine[1].y<mapYmin)
	    continue;
	 if (mapLine[0].y>mapYmax && mapLine[1].y>mapYmax)
	    continue;
	 /* GCC14: a wall shorter than a pixel is a dot the neighbouring walls already cover --
	    zoomed out, most of them are */
	 if (mapLine[0].x==mapLine[1].x && mapLine[0].y==mapLine[1].y)
	    continue;
	 if (EZ_getCmdRoom()<DOOM_AM_RESERVE)
	    return;
	 EZ_line(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5|COMPO_REP,color,mapLine,NULL);
	}
    }
}

/* GCC14: am_map.c player_arrow (:117-127), R = 8*PLAYERRADIUS/7 map units, in eighths of R:
   u along the player's facing, v across.  AM_drawPlayers turns it with the player and scales
   it with the map (AM_drawLineCharacter, scale 0); here it never gets smaller than
   DOOM_AM_ARROWMIN px, or the opening zoom would leave an arrow 5 px long. */
#define DOOM_AM_R        1198372        /* F(8*16)/7 = 18.29 u */
#define DOOM_AM_ARROWMIN 7
const signed char doomArrow[7][4]=       /* not static: PAUSE.OVL's MAP draws the same arrow */
   {{-7,0,8,0},{8,0,4,2},{8,0,4,-2},{-7,0,-9,2},{-7,0,-9,-2},{-5,0,-7,2},{-5,0,-7,-2}};

/* GCC14: THE OTHER PLAYERS, each in the colour its own body wears -- doom_playerMapColor gives
   the PLAYPAL index of the translation ramp mpSetBanks dressed it in, so the dot on the map and
   the marine in the view are the same colour by construction.  A CROSS, not a box: at the zoom
   the map opens on, two marines a few paces apart share a pixel and a cross still reads as two.
   Drawn after the walls and before the arrow, so the viewer's own arrow stays on top. */
#define DOOM_AM_MARK 3
static void doomDrawPlayers(int cx,int cy,MthXyz *north,MthXyz *east)
{int k;
 XyInt line[2];
 for (k=0;k<mpPlayers;k++)
    {int x,y,tx,ty,c;
     if (k==mpCur || !mpBody[k])
	continue;
     x=f(mpBody[k]->pos.x-cx);
     y=f(mpBody[k]->pos.z-cy);
     tx=f(x*north->x+y*north->y);
     ty=f(x*east->x+y*east->y);
     if (tx<mapXmin || tx>mapXmax || ty<mapYmin || ty>mapYmax)
	continue;
     if (EZ_getCmdRoom()<DOOM_AM_RESERVE)
	return;
     c=doomMapColor(doom_playerMapColor(k));
     line[0].x=tx-DOOM_AM_MARK; line[0].y=ty;
     line[1].x=tx+DOOM_AM_MARK; line[1].y=ty;
     EZ_line(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5|COMPO_REP,c,line,NULL);
     line[0].x=tx; line[0].y=ty-DOOM_AM_MARK;
     line[1].x=tx; line[1].y=ty+DOOM_AM_MARK;
     EZ_line(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5|COMPO_REP,c,line,NULL);
    }
}

/* Screen = north up (am_map.c draws the level unturned, following the player): the facing
   (-sin yaw, cos yaw) of DOOM_PLAYER.C doomMoveTic, Z = Doom's y (doom3d.py), goes to
   (-sin, -cos) on a screen whose y points down. */
static void doomDrawArrow(int yaw)
{int i;
 Fixed32 e,fx,fy,sx,sy;
 XyInt line[2];
 e=MTH_Mul(DOOM_AM_R,mapScale);
 if (e<F(DOOM_AM_ARROWMIN))
    e=F(DOOM_AM_ARROWMIN);
 e>>=3;                                  /* one eighth of R, in pixels */
 fx=-MTH_Sin(yaw); fy=-MTH_Cos(yaw);     /* along */
 sx=MTH_Cos(yaw);  sy=-MTH_Sin(yaw);     /* across */
 for (i=0;i<7;i++)
    {line[0].x=f(MTH_Mul(doomArrow[i][0]*e,fx)+MTH_Mul(doomArrow[i][1]*e,sx));
     line[0].y=f(MTH_Mul(doomArrow[i][0]*e,fy)+MTH_Mul(doomArrow[i][1]*e,sy));
     line[1].x=f(MTH_Mul(doomArrow[i][2]*e,fx)+MTH_Mul(doomArrow[i][3]*e,sx));
     line[1].y=f(MTH_Mul(doomArrow[i][2]*e,fy)+MTH_Mul(doomArrow[i][3]*e,sy));
     EZ_line(ECDSPD_DISABLE|COLOR_5|COMPO_REP,colors[1],line,NULL);
    }
}
#endif


#define ARROWR 5

void drawMap(int cx,int cy,int cz,int yaw,int currentSector)
{int w,i,s,color,transp;
 short currentHeight;
 XyInt mapLine[2];
 MthXyz north,east;
 MthXyz p1,p2,wallP;
#ifdef GP_GAME_DOOM
 north.x=mapScale;                      /* GCC14: north up, the arrow turns (doomDrawArrow) */
 north.y=0;
#else
 north.x=MTH_Mul(MTH_Cos(yaw),mapScale);
 north.y=MTH_Mul(MTH_Sin(yaw),mapScale);
#endif
 east.x=north.y;
 east.y=-north.x;

 if (mpPlayers==1)
    {mapXmin=MINX; mapXmax=MAXX; mapYmin=MINY; mapYmax=MAXY;}
 else
    {mapXmin=(short)viewXmin; mapXmax=(short)viewXmax;
     mapYmin=(short)viewYmin; mapYmax=(short)viewYmax;}
 {mapLine[0].x=viewCx+mapXmin; mapLine[0].y=viewCy+mapYmin;
  mapLine[1].x=viewCx+mapXmax; mapLine[1].y=viewCy+mapYmax;
  EZ_userClip(mapLine);
 }
#ifndef NDEBUG
 for (i=0;i<nmMarks;i++)
    {int x,y,tx,ty;
     XyInt mark[4];
     x=f(markX[i]-cx);
     y=f(markY[i]-cy);
     tx=f(x*north.x+y*north.y);
     ty=f(x*east.x+y*east.y);
     mark[0].x=tx-5; mark[0].y=ty-5;
     mark[1].x=tx+5; mark[1].y=ty-5;
     mark[2].x=tx+5; mark[2].y=ty+5;
     mark[3].x=tx-5; mark[3].y=ty+5;
     EZ_polygon(ECDSPD_DISABLE|COMPO_REP|COLOR_5,0x8ff0,mark,NULL);
    }
 nmMarks=0;
#endif

#ifdef GP_GAME_DOOM
 doomDrawMapWalls(cx,cy,&north,&east);
 doomDrawPlayers(cx,cy,&north,&east);
 (void)cz; (void)currentSector; (void)w; (void)s; (void)color; (void)transp; (void)currentHeight; (void)p1; (void)p2; (void)wallP;
#else
 currentHeight=level_sector[currentSector].floorLevel;
 for (s=0;s<level_nmSectors;s++)
    {transp=0;
     if (level_sector[s].flags & SECFLAG_NOMAP)
	continue;
     if (!(level_sector[s].flags & SECFLAG_SEEN))
	{if (!gotFullMap)
	    continue;
	 color=RGB(15,15,15);
	}
     else
	{color=(level_sector[s].floorLevel-currentHeight)>>4;
	 if (color<0)
	    {if (color<-31)
		{color=-31;
		 transp=1;
		}
	     color=RGB(31,31+color,31+color);
	    }
	 else
	    if (color>0)
	       {if (color>31)
		   {color=31;
		    transp=1;
		   }
		color=RGB(31-color,31-color,31);
	       }
	    else
	       color=RGB(31,31,31);
	}
     for (w=level_sector[s].firstWall;
	  w<=level_sector[s].lastWall;w++)
	{if (!mapColor[w])
	    {continue;
	    }
	 getVertex(level_wall[w].v[0],&wallP);
	 p1.x=f(wallP.x-cx);
	 p1.y=f(wallP.z-cy);
	 getVertex(level_wall[w].v[1],&wallP);
	 p2.x=f(wallP.x-cx);
	 p2.y=f(wallP.z-cy);

	 mapLine[0].x=f(p1.x*north.x+p1.y*north.y);
	 mapLine[0].y=f(p1.x*east.x+p1.y*east.y);
	 mapLine[1].x=f(p2.x*north.x+p2.y*north.y);
	 mapLine[1].y=f(p2.x*east.x+p2.y*east.y);
	 /* clip */
	 if (mapLine[0].x<mapXmin && mapLine[1].x<mapXmin)
	    continue;
	 if (mapLine[0].x>mapXmax && mapLine[1].x>mapXmax)
	    continue;
	 if (mapLine[0].y<mapYmin && mapLine[1].y<mapYmin)
	    continue;
	 if (mapLine[0].y>mapYmax && mapLine[1].y>mapYmax)
	    continue;
	 if (transp)
	    EZ_line(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5|COMPO_TRANS,
		    color,mapLine,NULL);
	 else
	    EZ_line(UCLPIN_ENABLE|ECDSPD_DISABLE|COLOR_5|COMPO_REP,
		    color,mapLine,NULL);
	}
    }
#endif

#ifdef GP_GAME_DOOM
 doomDrawArrow(yaw);
#else
 mapLine[0].x=0;
 mapLine[0].y=-ARROWR;
 mapLine[1].x=0;
 mapLine[1].y=+ARROWR;
 EZ_line(ECDSPD_DISABLE|COLOR_5|COMPO_REP,colors[1],mapLine,NULL);
 mapLine[0].x=-ARROWR;
 mapLine[0].y=0;
 mapLine[1].x=0;
 mapLine[1].y=-ARROWR;
 EZ_line(ECDSPD_DISABLE|COLOR_5|COMPO_REP, colors[1], mapLine, NULL);
 mapLine[0].x=ARROWR;
 mapLine[0].y=0;
 mapLine[1].x=0;
 mapLine[1].y=-ARROWR;
 EZ_line(ECDSPD_DISABLE|COLOR_5|COMPO_REP, colors[1], mapLine, NULL);
#endif
#if 0
#ifndef NDEBUG
 {int sec;
  Sprite *s;
  for (sec=0;sec<level_nmSectors;sec++)
     {for (s=sectorSpriteList[sec];s;s=s->next)
	 {int x,y,tx,ty;
	  int color;
	  XyInt mark[4];
	  x=f(s->pos.x-cx);
	  y=f(s->pos.z-cy);
	  tx=f(x*north.x+y*north.y);
	  ty=f(x*east.x+y*east.y);
	  mark[0].x=tx-2; mark[0].y=ty-2;
	  mark[1].x=tx+2; mark[1].y=ty-2;
	  mark[2].x=tx+2; mark[2].y=ty+2;
	  mark[3].x=tx-2; mark[3].y=ty+2;
	  if (sec==camera->s)
	     color=RGB(5,5,31);
	  else
	     color=RGB(15,31,15);
	  EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COMPO_REP|COLOR_5,
		     color,mark,NULL);
	 }
     }
 }
#endif
#endif
#ifndef JAPAN
 if (cheatsEnabled)
    {char buffer[160];
     sprintf(buffer,"x:%d y:%d  sector:%d",
	     (int)f(camera->pos.x),(int)f(camera->pos.z),camera->s);
     drawString(-90,-100,1,buffer);
    }
#endif
}

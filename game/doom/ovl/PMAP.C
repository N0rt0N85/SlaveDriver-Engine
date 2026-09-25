/* PMAP.C -- the pause's MAP browser (GCC14, PAUSE.OVL: never in MAIN).
 *
 * The player does not move; the MAP does.  The pad pans it, the triggers zoom it, Y fits the whole
 * level on screen, A puts it back on the player.  That is what the in-game automap on X cannot do,
 * and it is why this is a page of the pause rather than a mode of that map.
 *
 * Where it draws.  Not with the VDP1: the pause keeps the last game image frozen in the VDP1's
 * framebuffer (PAUSE.C), and a single VDP1 command would erase it.  The master writes PLAYPAL
 * indices straight into the NBG0 bitmap in VRAM B, one byte a pixel, CRAM bank 0 -- the layer the
 * level load already set up (SRUINS.C loadVDP2Sprites) and that nothing reads in solo play.  The
 * bitmap is 512x512 and VRAM B is two banks, so its two halves make a DOUBLE BUFFER: the browser
 * fills the hidden half while the chip displays the other, then moves NBG0's vertical scroll by
 * 256 rows inside a vertical blank.  A frame is never seen half drawn.  On the way out the map is
 * left showing the half that is NOT the menu's, so PAUSE.C can repaint its page unseen and only
 * then scroll back -- no flash of the map behind the menu.
 *
 * What it shows.  The walls the level's load already classified for the automap (MAP.C mapColor:
 * solid, floor step, ceiling step) in Doom's automap colours, for the leaves the renderer has
 * drawn at least once (SECFLAG_SEEN, set by drawSector) -- or every leaf, in grey, once the
 * computer area map has been picked up, as Doom does.  There is NO command cap here, unlike the
 * VDP1 automap, which runs out of list on E1M6 zoomed out and leaves walls off.
 *
 * The items are the pickups still on the floor: doomPickups is the level's own list and a pickup
 * taken leaves it (DOOM_GAME.C doom_item_func), so "still there" costs no flag and no test in
 * play.  "Seen" is the leaf's flag again -- the same granularity as the walls on the automap.
 *
 * Scale is an integer, pixels per PM_UNIT world units: every projection is then one multiply and
 * one shift, with no fixed-point division on the per-wall path. */
#include <machine.h>
#include <sega_scl.h>
#include <sega_mth.h>
#include <sega_per.h>

#include "util.h"
#include "level.h"
#include "sprite.h"
#include "sruins.h"
#include "walls.h"
#include "map.h"
#include "v_blank.h"
#include "mplayer.h"
#include "gamestat.h"
#include "ovl.h"
#include "doom.h"
#include "pmap.h"

#define B0          ((volatile unsigned char *)(SCL_VDP2_VRAM+0x40000))
#define VDP2R(o)    (*(volatile Uint16 *)(0x25f80000+(o)))
#define PM_W        320                 /* the screen; the bitmap's row pitch is 512 */
#define PM_VH       208                 /* the map's rows: 0..207, then the strip */
#define PM_ROWS     224
#define PM_CX       160
#define PM_CY       104
#define PM_STRIP_Y  208
#define PM_UNIT     1024                /* scale = pixels per PM_UNIT world units */
#define PM_SHIFT    10
#define PM_ZOOM_MAX 2048                /* 2 px a unit: a 64-u closet fills half the screen */
#define PM_ZOOM_OPEN 128                /* 0.125 px a unit: the automap's own opening zoom */
#define PM_PAN      3                   /* pixels a field, then PM_PAN_FAST once held */
#define PM_PAN_FAST 8
#define PM_PAN_HOLD 10
#define PM_ARROW    10                  /* the player arrow's half length, in pixels */
#define PM_PAD      (PER_DGT_U|PER_DGT_D|PER_DGT_L|PER_DGT_R)

/* am_map.c's colours as PLAYPAL indices written straight into the bitmap (MAP.C keeps the same
   list next to the automap; these are indices, not colour words, so no CRAM is read here) */
#define PM_BACK     247                 /* PLAYPAL (0,0,0), opaque under transparency ON */
#define PM_REDS     176
#define PM_BROWNS   64
#define PM_YELLOWS  231
#define PM_GRAYS3   99
#define PM_WHITE    209
#define PM_CROSS    96

enum {PMC_KEY,PMC_WEAPON,PMC_AMMO,PMC_HEALTH,PMC_ARMOUR,PMC_POWER,PMC_NM};
static const unsigned char pmMarkColour[PMC_NM]={231,216,160,112,196,251};

/* doom_sprite_t 55..93 covers every pickup.  One byte each: the class, or 255 for a sprite that
   is not a pickup (a barrel, a candle) -- those never reach doomPickups anyway. */
#define PM_SPR0 55                      /* SPR_ARM1 */
#define PM_SPR1 93                      /* SPR_SGN2 */
static const unsigned char pmSprClass[PM_SPR1-PM_SPR0+1]=
   {PMC_ARMOUR,PMC_ARMOUR,255,255,255,                 /* ARM1 ARM2 BAR1 BEXP FCAN */
    PMC_HEALTH,PMC_ARMOUR,                             /* BON1 BON2 */
    PMC_KEY,PMC_KEY,PMC_KEY,PMC_KEY,PMC_KEY,PMC_KEY,   /* BKEY RKEY YKEY BSKU RSKU YSKU */
    PMC_HEALTH,PMC_HEALTH,                             /* STIM MEDI */
    PMC_POWER,PMC_POWER,PMC_POWER,PMC_POWER,           /* SOUL PINV PSTR PINS */
    PMC_POWER,PMC_POWER,PMC_POWER,PMC_POWER,           /* MEGA SUIT PMAP PVIS */
    PMC_AMMO,PMC_AMMO,PMC_AMMO,PMC_AMMO,PMC_AMMO,      /* CLIP AMMO ROCK BROK CELL */
    PMC_AMMO,PMC_AMMO,PMC_AMMO,PMC_AMMO,               /* CELP SHEL SBOX BPAK */
    PMC_WEAPON,PMC_WEAPON,PMC_WEAPON,PMC_WEAPON,       /* BFUG MGUN CSAW LAUN */
    PMC_WEAPON,PMC_WEAPON,PMC_WEAPON};                 /* PLAS SHOT SGN2 */

#define PM_KEY_BLUE   200               /* a key keeps its own colour, as Doom's automap does */
#define PM_KEY_RED    176
#define PM_KEY_YELLOW 231

static int cx,cz;                       /* the map point at the middle of the screen, world units */
static int scale;                       /* pixels per PM_UNIT */
static int items;                       /* the marks are shown */
static int bx0,bz0,bx1,bz1;             /* the level's bounding box */
static int fitScale;                    /* the zoom that puts all of it on screen */

#define PMSX(x) (PM_CX+((((x)-cx)*scale)>>PM_SHIFT))
#define PMSY(z) (PM_CY-((((z)-cz)*scale)>>PM_SHIFT))

/* ------------------------------------------------------------------- the bitmap */
static volatile unsigned char *pmHalf(int half)
{return B0+(half? PAUSE_HALF_BYTES: 0);
}

static void pmClear(volatile unsigned char *h)
{int y,x;
 for (y=0;y<PM_ROWS;y++)
    {volatile Uint32 *d=(volatile Uint32 *)(h+(y<<9));
     for (x=0;x<512/4;x++)              /* GCC14: the whole row pitch, as PAUSE.C clearRows */
	d[x]=PM_BACK*0x01010101u;
    }
}

/* a clipped Bresenham line, one byte a pixel.  The four box rejections are the automap's; the
   rest is clipped per pixel, because at these lengths a full Cohen-Sutherland costs more than
   the pixels it would save. */
static void pmLine(volatile unsigned char *h,int x0,int y0,int x1,int y1,int c)
{int dx,dy,sx,sy,e,e2;
 if ((x0<0 && x1<0) || (x0>=PM_W && x1>=PM_W) || (y0<0 && y1<0) || (y0>=PM_VH && y1>=PM_VH))
    return;
 dx=x1-x0; if (dx<0) dx=-dx;
 dy=y1-y0; if (dy<0) dy=-dy;
 if (dx+dy>PM_W*8)                      /* zoomed right in, one end miles off screen: the span */
    return;                             /* would cost more steps than the screen has pixels */
 sx=(x0<x1)? 1: -1;
 sy=(y0<y1)? 1: -1;
 e=dx-dy;
 for (;;)
    {if (x0>=0 && x0<PM_W && y0>=0 && y0<PM_VH)
	h[(y0<<9)+x0]=(unsigned char)c;
     if (x0==x1 && y0==y1)
	return;
     e2=e<<1;
     if (e2>-dy)
	{e-=dy;
	 x0+=sx;
	}
     if (e2<dx)
	{e+=dx;
	 y0+=sy;
	}
    }
}

/* ------------------------------------------------------------------- the level */
static void pmBounds(void)
{int i,x,z,sw,sh;
 bx0=bz0=0x7fffff;
 bx1=bz1=-0x7fffff;
 for (i=0;i<level_nmVertex;i++)
    {x=level_vertex[i].x;
     z=level_vertex[i].z;
     if (x<bx0) bx0=x;
     if (x>bx1) bx1=x;
     if (z<bz0) bz0=z;
     if (z>bz1) bz1=z;
    }
 if (bx1<=bx0) bx1=bx0+1;
 if (bz1<=bz0) bz1=bz0+1;
 sw=(PM_W<<PM_SHIFT)/(bx1-bx0);
 sh=(PM_VH<<PM_SHIFT)/(bz1-bz0);
 fitScale=((sw<sh)? sw: sh)*4/5;        /* four fifths: a margin around the level */
 if (fitScale<1)
    fitScale=1;
}

static void pmWalls(volatile unsigned char *h)
{int s,w,c,full=mapRevealed();
 int x0,y0,x1,y1;
 const sSectorType *sec;
 for (s=0;s<level_nmSectors;s++)
    {sec=&level_sector[s];
     if (sec->flags & SECFLAG_NOMAP)
	continue;
     if (!(sec->flags & SECFLAG_SEEN) && !full)
	continue;
     for (w=sec->firstWall;w<=sec->lastWall;w++)
	{switch (mapColor[w])
	    {case 1:  c=PM_REDS; break;
	     case 2:  c=PM_BROWNS; break;
	     case 3:  c=PM_YELLOWS; break;
	     default: continue;
	    }
	 if (!(sec->flags & SECFLAG_SEEN))
	    c=PM_GRAYS3;                /* revealed by the computer map, never walked into */
	 x0=PMSX(level_vertex[level_wall[w].v[0]].x);
	 y0=PMSY(level_vertex[level_wall[w].v[0]].z);
	 x1=PMSX(level_vertex[level_wall[w].v[1]].x);
	 y1=PMSY(level_vertex[level_wall[w].v[1]].z);
	 if (x0==x1 && y0==y1)          /* shorter than a pixel: its neighbours cover it */
	    continue;
	 pmLine(h,x0,y0,x1,y1,c);
	}
    }
}

/* an item's mark, at a FIXED size, so it stays readable at the whole-level view */
static void pmMark(volatile unsigned char *h,int x,int y,int kind,int c)
{int i,j,r;
 if (x<3 || x>=PM_W-3 || y<3 || y>=PM_VH-3)
    return;
 switch (kind)
    {case PMC_KEY:                      /* a filled square, outlined so it reads over a wall */
	for (j=-2;j<=2;j++)
	   for (i=-2;i<=2;i++)
	      h[((y+j)<<9)+x+i]=(unsigned char)((i==-2||i==2||j==-2||j==2)? PM_BACK: c);
	break;
     case PMC_WEAPON:                   /* a plus, five by five */
	for (i=-2;i<=2;i++)
	   {h[(y<<9)+x+i]=(unsigned char)c;
	    h[((y+i)<<9)+x]=(unsigned char)c;
	   }
	break;
     case PMC_AMMO:                     /* two by two */
	for (j=0;j<2;j++)
	   for (i=0;i<2;i++)
	      h[((y+j)<<9)+x+i]=(unsigned char)c;
	break;
     case PMC_HEALTH:                   /* a small plus */
	for (i=-1;i<=1;i++)
	   {h[(y<<9)+x+i]=(unsigned char)c;
	    h[((y+i)<<9)+x]=(unsigned char)c;
	   }
	break;
     case PMC_ARMOUR:                   /* a hollow square */
	for (j=-1;j<=1;j++)
	   for (i=-1;i<=1;i++)
	      if (i || j)
		 h[((y+j)<<9)+x+i]=(unsigned char)c;
	break;
     default:                           /* a power-up: a diamond */
	for (j=-2;j<=2;j++)
	   {r=(j<0)? 2+j: 2-j;
	    for (i=-r;i<=r;i++)
	       h[((y+j)<<9)+x+i]=(unsigned char)c;
	   }
	break;
    }
}

static void pmItems(volatile unsigned char *h)
{DoomActor *a;
 int spr,kind,c,s,full=mapRevealed();
 for (a=(DoomActor *)doomPickups;a;a=(DoomActor *)a->pkNext)
    {if (!a->sprite)
	continue;
     s=a->sprite->s;
     if (s<0 || s>=level_nmSectors)
	continue;
     if (level_sector[s].flags & SECFLAG_NOMAP)
	continue;
     if (!(level_sector[s].flags & SECFLAG_SEEN) && !full)
	continue;
     spr=doomStates[a->state].sprite;
     if (spr<PM_SPR0 || spr>PM_SPR1)
	continue;
     kind=pmSprClass[spr-PM_SPR0];
     if (kind>=PMC_NM)
	continue;
     c=pmMarkColour[kind];
     if (kind==PMC_KEY)
	c=(spr==SPR_BKEY || spr==SPR_BSKU)? PM_KEY_BLUE:
	  (spr==SPR_RKEY || spr==SPR_RSKU)? PM_KEY_RED: PM_KEY_YELLOW;
     pmMark(h,PMSX(f(a->pkX)),PMSY(f(a->pkZ)),kind,c);
    }
}

/* the player where it really stands, turned the way it looks (MAP.C doomDrawArrow's shape, in
   eighths of its length: a0*PM_ARROW is eight times the pixels, hence the shift by 16+3) */
static void pmArrow(volatile unsigned char *h)
{int i,x,y,a0,a1,a2,a3;
 Fixed32 fx,fy,sx,sy;
 if (!camera)
    return;
 x=PMSX(f(camera->pos.x));
 y=PMSY(f(camera->pos.z));
 fx=-MTH_Sin(playerAngle.yaw); fy=-MTH_Cos(playerAngle.yaw);   /* along the facing */
 sx= MTH_Cos(playerAngle.yaw); sy=-MTH_Sin(playerAngle.yaw);   /* across it */
 for (i=0;i<7;i++)
    {a0=doomArrow[i][0]*PM_ARROW; a1=doomArrow[i][1]*PM_ARROW;
     a2=doomArrow[i][2]*PM_ARROW; a3=doomArrow[i][3]*PM_ARROW;
     pmLine(h,x+((a0*fx+a1*sx)>>19),y+((a0*fy+a1*sy)>>19),
	      x+((a2*fx+a3*sx)>>19),y+((a2*fy+a3*sy)>>19),PM_WHITE);
    }
}

/* the middle of the screen: panning needs a reference the player arrow no longer gives */
static void pmCross(volatile unsigned char *h)
{int i;
 for (i=2;i<=4;i++)
    {h[(PM_CY<<9)+PM_CX+i]=PM_CROSS;
     h[(PM_CY<<9)+PM_CX-i]=PM_CROSS;
     h[((PM_CY+i)<<9)+PM_CX]=PM_CROSS;
     h[((PM_CY-i)<<9)+PM_CX]=PM_CROSS;
    }
}

static void pmStrip(int half)
{int l=currentState.currentLevel;
 pause_half(half);
 if (l>=0 && l<DOOM_NMLEVELS)
    pause_text(4,PM_STRIP_Y,doomMapTitles[l],0,0);
 pause_text(4,PM_STRIP_Y+8,
	    items? "PAD MOVE  L R ZOOM  Y ALL  A YOU  X NO ITEMS  B BACK":
		   "PAD MOVE  L R ZOOM  Y ALL  A YOU  X ITEMS     B BACK",0,0);
 pause_half(0);
}

static void pmDraw(int half)
{volatile unsigned char *h=pmHalf(half);
 pmClear(h);
 pmWalls(h);
 if (items)
    pmItems(h);
 pmArrow(h);
 pmCross(h);
 pmStrip(half);
}

static void pmOnPlayer(void)
{if (camera)
    {cx=f(camera->pos.x);
     cz=f(camera->pos.z);
    }
 else
    {cx=(bx0+bx1)>>1;
     cz=(bz0+bz1)>>1;
    }
}

static void pmClamp(void)
{if (cx<bx0) cx=bx0;
 if (cx>bx1) cx=bx1;
 if (cz<bz0) cz=bz0;
 if (cz>bz1) cz=bz1;
}

/* ------------------------------------------------------------------- the browser */
int doom_pmapBrowse(char *freeBase,char *freeEnd)
{int back=1;                            /* the half being drawn into ... */
 int shown=0;                           /* ... and the one on screen: the menu's, to start with */
 int dirty=1,pending=0,hold=0,all=0,step,n,px,pz,r=0;
 Uint32 last=vtimer;
 Uint16 hit,held;
 (void)freeBase;
 (void)freeEnd;
 pmBounds();
 scale=PM_ZOOM_OPEN;
 if (scale<fitScale)
    scale=fitScale;
 if (scale>PM_ZOOM_MAX)
    scale=PM_ZOOM_MAX;
 items=1;
 pmOnPlayer();
 doom_playerSound(sfx_swtchn);
 for (;;)
    {if (dirty)
	{pmDraw(back);
	 pending=1;
	 dirty=0;
	}
     hit=pause_field();                 /* one field: the sound, the music and the veil go on */
     n=(int)(vtimer-last);
     last=vtimer;
     if (n<1) n=1;
     if (n>4) n=4;                      /* a slow redraw must not fling the map across */
     if (pending)
	{/* vtimer steps at the end of the vertical blank, so this write lands inside it */
	 VDP2R(PAUSE_SCYIN0)=(Uint16)(back? 256: 0);
	 shown=back;
	 back^=1;
	 pending=0;
	}
     held=pause_held();                 /* active low */
     if (hit&PER_DGT_S)
	{r=1;                           /* START: straight back into the game */
	 break;
	}
     if (hit&PER_DGT_B)
	{doom_playerSound(sfx_swtchn);
	 break;
	}
     if (hit&PER_DGT_X)
	{items=!items;
	 dirty=1;
	 doom_playerSound(sfx_pstop);
	}
     if (hit&PER_DGT_Y)
	{all=!all;
	 if (all)
	    {scale=fitScale;
	     cx=(bx0+bx1)>>1;
	     cz=(bz0+bz1)>>1;
	    }
	 else
	    {scale=PM_ZOOM_OPEN;
	     if (scale<fitScale) scale=fitScale;
	     pmOnPlayer();
	    }
	 dirty=1;
	 doom_playerSound(sfx_pstop);
	}
     if (hit&(PER_DGT_A|PER_DGT_C))
	{pmOnPlayer();
	 all=0;
	 dirty=1;
	 doom_playerSound(sfx_pstop);
	}
     /* zoom, held: about a seventeenth a field, so a doubling takes some eleven of them */
     if (!(held&PER_DGT_TR) && scale<PM_ZOOM_MAX)
	{scale+=(scale>>4)+1;
	 if (scale>PM_ZOOM_MAX) scale=PM_ZOOM_MAX;
	 dirty=1;
	 all=0;
	}
     else if (!(held&PER_DGT_TL) && scale>fitScale)
	{scale-=(scale>>4)+1;
	 if (scale<fitScale) scale=fitScale;
	 dirty=1;
	 all=0;
	}
     /* pan at a constant SCREEN speed, so the world step follows the zoom by itself */
     if ((held&PM_PAD)!=PM_PAD)
	hold++;
     else
	hold=0;
     step=((hold>PM_PAN_HOLD? PM_PAN_FAST: PM_PAN)*n)<<PM_SHIFT;
     step/=scale;
     if (step<1)
	step=1;
     px=pz=0;
     if (!(held&PER_DGT_L)) px=-step;
     if (!(held&PER_DGT_R)) px= step;
     if (!(held&PER_DGT_U)) pz= step;
     if (!(held&PER_DGT_D)) pz=-step;
     if (px || pz)
	{cx+=px;
	 cz+=pz;
	 pmClamp();
	 dirty=1;
	 all=0;
	}
    }
 /* Leave with the map on the half that is NOT the menu's, so PAUSE.C repaints half 0 unseen and
    scrolls back to it afterwards.  On a resume nobody looks: setRegs puts the game's own scroll
    back from SCL's buffers. */
 if (shown==0)
    {pmDraw(1);
     VDP2R(PAUSE_SCYIN0)=256;
    }
 return r;
}

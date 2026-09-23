/* DOOM_TITLE.C -- Doom's title screen: the DOOM logo over the PSX fire, Doom's letters, the skull.
 *
 * GCC14: the whole file.  The title of Doom on the Saturn (a port of PSX Doom): the M_DOOM logo on
 * black, the fire burning up from the bottom, the menu in Doom's red letters with a blinking skull
 * beside the chosen line.  The engine keeps its menus (INTRO.C, MENU.C); it reaches this file
 * through the CFG_TITLE_* and CFG_MENU_CURSOR hooks of SPRITE.H.  Everything shown comes from
 * DTITLE.DAT, which tools/doom2ps/wad2title.py cuts from the SHAREWARE WAD: M_DOOM, STCFN (the
 * menu font: x1, and x2 tall by x1.5 wide so the title's sub-screens fit in 320 px; each with a
 * space), M_SKULL1/2, PLAYPAL -- no retail art, no byte of MAIN.
 *
 * The VDP2 (320x224, CFG_SCL_LINES), VRAM B partitioned:
 *   NBG1 = the logo, 512x256 8 bpp bitmap in B0, zoomed x2 (M_DOOM 123x60 -> 246x120, screen lines
 *          12..131), CRAM bank 0 = PLAYPAL.
 *   NBG0 = the fire, 512x256 8 bpp bitmap in B1, 1:1, bitmap rows 8..223 = the band and its base,
 *          CRAM bank 1 = the PSX ramp's 37 colours, exact (N0CAOS 1).  Level 0 is pixel 0: it is
 *          transparent, and the logo shows between the flames.
 *   SP0 (the menu, VDP1) on top of both.
 * VRAM A is not used, so the layout is also the one a loading screen can keep (A is RBG0's then).
 *
 * The fire runs on the SLAVE SH-2: the master kicks it from the vblank (vblankUserHook, one 16-bit
 * write every FIRE_FIELDS fields) and does nothing else for it.  The slave computes one step into
 * its own buffer and writes the changed cells straight into the NBG0 bitmap; the VDP2 shows them.
 * The kick is the wall renderer's (WALLS.C): the master's write to 0x21000000 sets the slave's
 * FRT input-capture flag, and the slave spins on its own FTCSR -- on-chip, no bus traffic while it
 * waits.  The fire slave NEVER writes 0x21800000 (the master's flag: the wall renderer's answer).
 * Its buffers live in doorwayCache (WALLS.C slaveScratch), idle outside play, above the 8 KB that
 * MENU.C's loadOverBase borrows at the title: no pool byte, no BSS.
 *
 * The loading screen is the title going on (CFG_LOADING_SCREEN, SRUINS.C runLevel): the fire keeps
 * burning from the menu into the load, the same logo is over it, LOADING is stencilled black into
 * the white-hot base, and the flames GROW with the sectors read (FILE.C progressHook) until they
 * cover the logo.  The fire slave writes VDP2 VRAM, so DMA.C's dmaNoScu holds PIC.C's tile
 * uploads to a plain copy meanwhile: an SCU-DMA driving the B-bus while a CPU writes it hangs
 * (SCU restriction 08), which is what froze every loading screen.  STATIC.DAT's
 * first block is DTITLE.DAT's logo block, byte for byte.  VRAM A is RBG0's by then, B is the logo's
 * and the fire's, as at the title.  CRAM bank 1 does not survive the load (loadPalletes writes the
 * fog banks), bank 0 does (PLAYPAL, the object palette: the same colours but 0 and 255), so the
 * fire leaves the exact PSX ramp for its PLAYPAL match (P_load, N0CAOS 0) as the load begins.  The
 * master draws nothing: no bar, no SCL_DisplayFrame while the disc is read.  CFG_LOADING_END puts
 * the game's VDP2 back and runLevel's startSlave(wallRenderSlaveMain) replaces the fire slave.
 * From a level to the next the load starts black and the flames catch from the source line.
 *
 * The fire itself is Doom 32X Resurrection's (d32xr m_fire.c), itself PSX Doom's as Samuel
 * Villarreal and Fabien Sanglard rebuilt it, under this licence (m_fire.c's header, verbatim):
 *
 *   Victor Luchits, Samuel Villarreal and Fabien Sanglard
 *
 *   The MIT License (MIT)
 *
 *   Copyright (c) 2021 Victor Luchits, Derek John Evans, id Software and ZeniMax Media
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy
 *   of this software and associated documentation files (the "Software"), to deal
 *   in the Software without restriction, including without limitation the rights
 *   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *   copies of the Software, and to permit persons to whom the Software is
 *   furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in all
 *   copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *   SOFTWARE.
 *
 * What was ported: M_SpreadFire's rule (a cell drifts by rnd&3 - 1 and loses rnd&1 level per row),
 * the 256-entry random table walked cyclically, the white source line; the colour ramp is the full
 * PSX one (37 colours: d32xr keeps 26 and comments the 11 others out -- wad2title.py restores
 * them, in order).  What changed: the spread is written as a PULL (new(x,y) = old(x+r-1,y+1) - dec,
 * the same drift, no holes, done in place from the top down, 4 cells a long store); the decay
 * chance is a parameter (FIRE_TITLE_Q8; 128 = the original's r&1); rows above the highest flame
 * are skipped; only the cells that changed are written to VRAM; the slave SH-2 draws into a VDP2
 * bitmap instead of the 32X framebuffer. */
#include <machine.h>
#include <string.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_mth.h>
#include "util.h"
#include "spr.h"
#include "sprite.h"
#include "walls.h"
#include "print.h"
#include "file.h"
#include "v_blank.h"
#include "mpsky.h"
#include "sruins.h"
#include "dma.h"

#ifndef GP_FIRE_FIELDS
#define GP_FIRE_FIELDS 2                /* one fire step every 2 fields (30 Hz NTSC); 0 = frozen */
#endif
#ifndef GP_FIRE_TITLE_Q8
#define GP_FIRE_TITLE_Q8 128            /* the reference's own decay: one level every other row */
#endif
#ifndef GP_FIRE_LOAD_Q0
#define GP_FIRE_LOAD_Q0 220             /* a load: the decay at its first sector ...             */
#endif
#ifndef GP_FIRE_LOAD_Q1
#define GP_FIRE_LOAD_Q1 38              /* ... and at its last: 242 rows, MORE than the band     */
#endif
#ifndef GP_FIRE_LOAD_LEAD
#define GP_FIRE_LOAD_LEAD 32            /* x256 of the load the flames run ahead: they need as
					   many steps as rows to grow, and a load served from
					   the drive's cache would show nothing at all        */
#endif
#ifndef GP_FIRE_LOAD_FIELDS
#define GP_FIRE_LOAD_FIELDS 2           /* fields between two steps during a load; 0 = frozen     */
#endif

#define FIRE_W      320
/* The band holds the whole screen above its solid base, and its TOP ROW IS A PARAMETER (ctl.top):
   the title burns the bottom 126 rows of it, a load the lot.  The band is always higher than the
   flames are long, which is what makes the look: a flame dies out inside the band, so its last
   rows hold only the few tongues that got that far -- embers, scattering -- instead of a wall of
   fire cut off square.  The mean flame is FIRE_MAX*256/pQ8 rows (72 at the reference's decay,
   against the title's 126); a load walks pQ8 down until the flames are longer than the band and
   the logo is covered whole. */
#define FIRE_H      198                 /* rows of live fire: screen FIRE_Y0..FIRE_Y0+197 = 8..205 */
#define FIRE_SOLID  18                  /* solid white rows under it (m_fire.c solid_fire_height) */
#define FIRE_Y0     (224-FIRE_SOLID-FIRE_H)     /* the title's own offset: the band at 8..205     */
/* The screen row the band's first row lands on is a PARAMETER too (ctl.yOff): the title keeps its
   own 8, so its solid base is the reference's 18 rows; a load puts the band at 0, so its flames
   are CUT BY THE TOP OF THE SCREEN instead of stopping 8 rows short of it -- which is what says
   the fire is bigger than the screen.  A load's base is then 26 rows instead of 18. */
#define FIRE_TITLE  126                 /* the title burns this many of them, as the PSX does     */
#define FIRE_TOP0   (FIRE_H-FIRE_TITLE) /* ... so its top row is 72: screen 80, the logo's waist  */
#define FIRE_SRC    1                   /* the last row: the white source, never written          */
#define FIRE_MAX    36                  /* levels 0..36: the PSX ramp's 37 colours                */
#define FIRE_SLOTS  40                  /* the level -> pixel tables of the logo block            */
#define FIRE_DARK   247                 /* PLAYPAL's opaque black (PAUSE.C VEIL_INDEX): the
					   letters' outline.  Index 0 is TRANSPARENT here, so it
					   would show the logo through the outline           */
#define MASK_H      16                  /* "LOADING": STCFN x2, 16 rows ...                       */
#define MASK_Y0     (FIRE_H-FIRE_SRC-MASK_H)    /* ... fire rows 181..196, just over the source   */
#define LOGO_ROWS   112                 /* NBG1 bitmap rows on screen: 224 lines at x2            */
#define VRAM_B0     (SCL_VDP2_VRAM+0x40000)     /* NBG1: the logo */
#define VRAM_B1     (SCL_VDP2_VRAM+0x60000)     /* NBG0: the fire */

/* the logo block (wad2title.py; DTITLE.DAT's, and STATIC.DAT's first), big-endian like the SH-2 */
#define LB_LEVELS   4                   /* i16 37                                  */
#define LB_PLAYPAL  8                   /* u16[256], CRAM bank 0                   */
#define LB_RAMP     520                 /* u16[40], CRAM bank 1 (the title's fire) */
#define LB_PTITLE   600                 /* u8[40]: level -> bank-1 index (identity) */
#define LB_PLOAD    640                 /* u8[40]: level -> PLAYPAL index (a load)  */
#define LB_R        680                 /* u8[256]                                 */
#define LB_U        936                 /* u8[256]                                 */
#define LB_LOGO     1192                /* i16 w, h, x, y in the NBG1 bitmap       */
#define LB_MASK     1200                /* u8[16][40]: LOADING, the letters' BODY, 1 bit a cell */
#define LB_MASKG    1840                /* u8[16][40]: ... and every opaque cell of the glyph  */
#define LB_PIXELS   2480                /* u8[w*h], w a multiple of 4, no padding  */

/* the slave's on-chip registers, as WALLS.C has them (its own are file-local) */
#define IPRA (Uint16 volatile *)0xfffffee2
#define IPRB (Uint16 volatile *)0xfffffe60
#define TIER (Uint8 volatile *)0xfffffe10
#define FTCSR (Uint8 volatile *)0xfffffe11
#define CACHECNTRL (Uint8 volatile *)0xfffffe92

/* doorwayCache, as the fire uses it (the slave's cache is purged on every kick) */
#define LOOK_MAP     1                  /* VRAM gets P[level] (a load); else the level itself (the
					   title's bank-1 ramp, P the identity)                 */
#define LOOK_LETTERS 2                  /* LOADING stencilled over fire rows MASK_Y0.. (display only:
					   the flames above the letters burn on)                */
struct fireCtl                          /* read and written through the cache-through alias */
{int pQ8;                               /* master: chance x256 a cell loses a level in a row */
 int top;                               /* master: first row of the band that burns        */
 int yOff;                              /* master: the screen row that row `top` lands on  */
 int look;                              /* master: LOOK_*                                  */
 int shown;                             /* slave: the look VRAM holds (-1: none yet)       */
 int pad[11];
};
/* The fire's own tables sit AFTER its buffer, because the BASE of doorwayCache is borrowed by
   other code at the title -- MENU.C loadOverBase's picture temp, INTRO.C's sperm -- and the base
   is F's top rows, which the title never burns (its top is FIRE_TOP0).  Fpad is part of that free
   room: a cell of row 0 writes up to two cells before F, and only a load burns row 0. */
struct fireArea
{signed char Fpad[FIRE_W];              /* a cell of row 0 writes up to two cells before F */
 signed char F[FIRE_H][FIRE_W];         /* slave: levels 0..FIRE_MAX, the reference's layout */
 struct fireCtl ctl;
 unsigned char R[256],U[256];           /* master: R = the reference's rndtable (its low 2 bits) */
 unsigned char P[64];                   /* master: level -> pixel, read with LOOK_MAP      */
 unsigned char M[MASK_H][FIRE_W/8];     /* master: LOADING's BODY, one BIT a cell           */
 unsigned char MD[MASK_H][FIRE_W/8];    /* master: the whole glyph grown by a cell: the ink */
};
typedef char fireAreaFits[sizeof(struct fireArea)<=MAXNMWALLS*12? 1: -1];   /* doorwayCache */
/* what the title lends: everything below the first row it burns */
#define FIRE_FREE (FIRE_W+FIRE_TOP0*FIRE_W)
typedef char fireLendsEnough[FIRE_FREE>=8192? 1: -1];
#define FIRECTL(a) ((volatile struct fireCtl *)(((int)&(a)->ctl)|0x20000000))

extern unsigned char **doomFontList;    /* PRINT.C (CFG_FONTLIST_DEF) */
int fs_getFileSize(int fd);             /* FILE.C */

static unsigned char *titleBlock;       /* DTITLE.DAT's logo block, in the title's pool */
static unsigned char *titleFonts[5];    /* small, small, big: initFonts' list at the title */
static short skullChar=-1;              /* M_SKULL1's VDP1 char, M_SKULL2 the next; -1 = none */
static int fireField,fireEvery;
static int fireOn;                      /* the fire slave runs (title, or a load) */
static Uint16 savedCyc[8];              /* a load: setVDP2's cycle table ... */
static Uint8 savedN1Pri;                /* ... and NBG1's priority, given back at its end */

/* ------------------------------------------------------------------------ the slave's side */
static struct fireArea *fireArea(void)
{return (struct fireArea *)slaveScratch(NULL);
}

/* What VRAM shows of 4 cells w (a long of F): the level itself, or P[level]; then the letters'
   mask in their rows (mp: the mask's long, or NULL).  A macro: fireStep is built at O2 and the
   rest of the file at Os, and GCC does not inline across the two. */
/* GCC14: the word is painted with an OUTLINE, and the letters are the DARK half.  White letters
   with a black outline were still unreadable on the console, and the measurement says why: the
   word sits on fire rows 181..196, one to sixteen rows above the source, where the ramp is at its
   top -- and the top of the load's ramp is PLAYPAL 4, (31,31,31), the very white the letters were
   painted in.  Only the outline showed.  So it is the other way round now: the whole glyph, grown
   by a cell, takes the fire's white, and the letter's own body PLAYPAL's opaque black on top. */
#define FIREPIX(w,P,look,hl,mk,dark,hot)					   {if ((look)&LOOK_MAP)							       w=((unsigned int)(P)[w>>24]<<24)|((unsigned int)(P)[(w>>16)&255]<<16)|		 ((unsigned int)(P)[(w>>8)&255]<<8)|(P)[w&255];			    if (hl!=0xffffffffu)							       w=(w & (hl))|(~(hl) & (dark));					    if (mk!=0xffffffffu)							       w=(w & (mk))|(~(mk) & (hot));					   }
/* GCC14: the letters are PAINTED, not punched.  They were the mask's zeroes -- level 0, the
   transparent pixel -- so LOADING was a hole in the flames, and the flames' own dark gaps are
   holes too: at a glance there was no word there.  Now the cells the mask clears take the top of
   the ramp, the white the source rows burn at, so the word reads over the fire whatever it is
   doing underneath.  FIRE_HOT is that pixel, four cells at a time. */
#define FIRE_HOT(P) ((unsigned int)(P)[FIRE_MAX]*0x01010101u)
#define FIRE_INK    ((unsigned int)FIRE_DARK*0x01010101u)
#define FIRELETTER(a,look,y) (((look)&LOOK_LETTERS) && (y)>=MASK_Y0 && (y)<MASK_Y0+MASK_H)
#define FIREMASK(a,look,y) (FIRELETTER(a,look,y)? (a)->M[(y)-MASK_Y0]: NULL)
#define FIREHALO(a,look,y) (FIRELETTER(a,look,y)? (a)->MD[(y)-MASK_Y0]: NULL)
/* the mask is one bit a cell (1 = letter), the blit works four cells at a time: a nibble, the
   first cell in the top bit, to the long that keeps the fire where the letter is not */
static const unsigned int maskExp[16]=
   {0xffffffffu,0xffffff00u,0xffff00ffu,0xffff0000u,
    0xff00ffffu,0xff00ff00u,0xff0000ffu,0xff000000u,
    0x00ffffffu,0x00ffff00u,0x00ff00ffu,0x00ff0000u,
    0x0000ffffu,0x0000ff00u,0x000000ffu,0x00000000u};
#define MASKLONG(m,x) maskExp[((m)[(x)>>1]>>(((x)&1)? 0: 4))&15]

/* the pixel one cell of level l shows in `look`: the ramp's own index at the title (CRAM bank 1),
   PLAYPAL's through P during a load */
#define FIRE_PIX1(a,look,l) ((unsigned int)(((look)&LOOK_MAP)? (a)->P[l]: (l)))

/* VRAM = the band from its top row down.  The buffer is scattered into by the step, so there is
   no telling which longs changed: all of them go. */
static void fireBlit(struct fireArea *a,int look,int top,int yOff)
{int y,x;
 unsigned int hot=FIRE_HOT(a->P);
 for (y=top;y<FIRE_H;y++)
    {const unsigned int *f=(const unsigned int *)a->F[y];
     const unsigned char *m=FIREMASK(a,look,y);
     const unsigned char *d=FIREHALO(a,look,y);
     volatile unsigned int *v=(volatile unsigned int *)(VRAM_B1+((yOff+y)<<9));
     for (x=0;x<FIRE_W/4;x++)
	{unsigned int w=f[x];
	 FIREPIX(w,a->P,look,d? MASKLONG(d,x): 0xffffffffu,
		 m? MASKLONG(m,x): 0xffffffffu,hot,FIRE_INK);
	 v[x]=w;
	}
    }
}

/* what burns under the band: the reference's solid rows.  Written once, with the look. */
static void fireBase(struct fireArea *a,int look,int yOff)
{int y,x;
 unsigned int w=FIRE_PIX1(a,look,FIRE_MAX)*0x01010101u;
 for (y=yOff+FIRE_H;y<224;y++)
    {volatile unsigned int *v=(volatile unsigned int *)(VRAM_B1+(y<<9));
     for (x=0;x<FIRE_W/4;x++)
	 v[x]=w;
    }
}

/* the fire back to its start: every level 0, the source row white, in the buffer and in VRAM */
static void fireReset(struct fireArea *a,int look,int top,int yOff)
{int y,x;
 for (x=0;x<FIRE_W;x++)
    a->Fpad[x]=0;
 for (y=top;y<FIRE_H;y++)
    for (x=0;x<FIRE_W;x++)
       a->F[y][x]=(signed char)((y>=FIRE_H-FIRE_SRC)? FIRE_MAX: 0);
 fireBlit(a,look,top,yOff);
 fireBase(a,look,yOff);
}

/* the band's top row moved up (a load takes the rows the title lent out): what it gains is
   whatever the borrower left there, so it is cleared before it is ever shown */
static void fireOpen(struct fireArea *a,int from,int to)
{int y,x;
 for (x=0;x<FIRE_W;x++)
    a->Fpad[x]=0;
 for (y=from;y<to;y++)
    for (x=0;x<FIRE_W;x++)
       a->F[y][x]=0;
}

/* One step: M_SpreadFire of the reference (saturn-refs/d32xr/m_fire.c, MIT), cell by cell and in
   its own order -- a column at a time, from the source row upwards.  Two things make the look and
   both were lost in the first port: the cell SCATTERS (it writes into the row above, one column
   aside, so two cells can land on the same neighbour and leave the one beside it untouched --
   that is where the tongues and the gaps come from), and the random index WALKS ON at every
   single cell.  A fixed random field, however well made, draws the same comb image after image.
   dst can reach two cells before the buffer, which is what Fpad is for (the reference allocates
   one row before firePix for the same reason). */
static void __attribute__((optimize("O2"))) fireSpread(struct fireArea *a,int *rip,int q8,int top)
{int x,y,ri=*rip;
 signed char *F=a->F[0];
 const unsigned char *R=a->R;
 const unsigned char *U=a->U;
 for (x=0;x<FIRE_W;x++)
    {int src=(top+1)*FIRE_W+x;
     for (y=top+1;y<FIRE_H;y++,src+=FIRE_W)
	{int p=F[src],dst=src,nv=0;
	 if (p>0)
	    {ri=(ri+1)&255;
	     dst=src-(R[ri]&3)+1;
	     /* the reference loses a level on the odd draws, one row in two.  Here the chance is
	        q8/256 -- 128 IS the reference -- because a load grows its flames with it, and a
	        flame is FIRE_MAX*256/q8 rows long. */
	     nv=p-(U[ri]<q8);
	    }
	 F[dst-FIRE_W]=(signed char)nv;
	}
    }
 *rip=ri;
}

/* startSlave() entry.  Never writes 0x21800000: that is the wall renderer's answer to the master. */
static void fireSlaveMain(void)
{struct fireArea *a=fireArea();
 volatile struct fireCtl *c=FIRECTL(a);
 int rk=0,look,q8,top,yOff;
 set_imask(0xf);
 *IPRA=0x0000;
 *IPRB=0x0000;
 *TIER=0x01;
 *FTCSR=0x0;
 *CACHECNTRL=0x10;                      /* purge: the master wrote the tables */
 *CACHECNTRL=0x01;
 look=c->look;
 q8=c->pQ8;
 top=c->top;
 yOff=c->yOff;
 fireReset(a,look,top,yOff);
 c->shown=look;
 while (1)
    {while (!(*FTCSR & 0x80))           /* on-chip: no bus traffic while it waits */
	;
     *FTCSR=0x0;
     *CACHECNTRL=0x10;
     *CACHECNTRL=0x01;
     if (c->top<top || c->yOff!=yOff)   /* a load takes the rows the title lent out, and the
					   whole screen with them */
	{if (c->top<top)
	    fireOpen(a,c->top,top);
	 top=c->top;
	 yOff=c->yOff;
	 fireBlit(a,look,top,yOff);
	 fireBase(a,look,yOff);
	}
     if (c->look!=look)                 /* a load's colours and letters: P and M were written first */
	{look=c->look;
	 fireBlit(a,look,top,yOff);
	 fireBase(a,look,yOff);
	 c->shown=look;
	}
     q8=c->pQ8;                         /* the load grows the flames from one step to the next */
     fireSpread(a,&rk,q8,top);
     fireBlit(a,look,top,yOff);
    }
}

/* ------------------------------------------------------------------------ the master's side */
/* vblankUserHook (V_BLANK.C, every vblank-out): the whole of the master's share */
static void doom_fireKick(void)
{if (++fireField>=fireEvery)
    {fireField=0;
     *(Uint16 volatile *)0x21000000=0xffff;
    }
}

/* The two layers (2.2 of the design), the title's or a load's: NBG1 = the logo at B0, x2; NBG0 =
   the fire at B1, 1:1; pixel 0 transparent in both.  At the title the fire reads CRAM bank 1 (the
   exact PSX ramp) and the menu (SP0) is on top; in a load the fire reads bank 0 (PLAYPAL, which
   the load writes there again) and both layers sit over the VDP1 framebuffer, erased to opaque
   black.  Every register this depends on is set, not assumed: after a level they still hold its
   set-up (a split game's sky, the VDP2 sheet's window). */
static void setLayers(int loading)
{static Uint16 cycle[8]={0xeeee,0xeeee,   /* A0: CPU (A unused; a load's RBG0 ignores CYC) */
			 0xeeee,0xeeee,   /* A1                                  */
			 0x55ee,0xeeee,   /* B0: NBG1 bitmap, 2 reads (256 col.)  */
			 0x44ee,0xeeee};  /* B1: NBG0 bitmap                     */
 SclConfig scfg;
 SCL_SetCycleTable(cycle);
 SCL_InitConfigTb(&scfg);
 scfg.dispenbl=ON;
 scfg.bmpsize=SCL_BMP_SIZE_512X256;
 scfg.coltype=SCL_COL_TYPE_256;
 scfg.datatype=SCL_BITMAP;
 scfg.mapover=SCL_OVER_0;
 scfg.patnamecontrl=0;
 scfg.plate_addr[0]=VRAM_B1-SCL_VDP2_VRAM;
 SCL_SetConfig(SCL_NBG0,&scfg);
 scfg.plate_addr[0]=VRAM_B0-SCL_VDP2_VRAM;
 SCL_SetConfig(SCL_NBG1,&scfg);
 SCL_SET_N0CAOS(loading? 0: 1);         /* the fire: PLAYPAL in a load, the PSX ramp at the title */
 SCL_SET_N1CAOS(0);                     /* the logo: PLAYPAL in bank 0      */
 SCL_SET_N0CCEN(0);
 SCL_SET_N1CCEN(0);
 SCL_SetPriority(SCL_NBG0,loading? 6: 3);
 SCL_SetPriority(SCL_NBG1,loading? 5: 2);
 SCL_SetPriority(SCL_SP0,4);
 /* NBG0 and NBG1 on, both with transparency; the title shows nothing else, a load keeps RBG0's
    bit as the level's set-up leaves it (under the black framebuffer) */
 Scl_s_reg.dispenbl=(loading? Scl_s_reg.dispenbl&~0x0303: 0)|0x0003;
 Scl_w_reg.wincontrl[0]=0;              /* no window on either (dontDisplayVDP2Pic hid NBG0) */
 SCL_Open(SCL_NBG1); SCL_MoveTo(0,0,0); SCL_Scale(FIXED(2),FIXED(2)); SCL_Close();
 SCL_Open(SCL_NBG0); SCL_MoveTo(0,0,0); SCL_Scale(FIXED(1),FIXED(1)); SCL_Close();
 if (SclProcess==0)
    SclProcess=1;
}

/* CFG_TITLE_VDP2 (INTRO.C playIntro, display off): VRAM B partitioned, nothing on A, the layers */
void doom_titleVdp2(void)
{SclVramConfig vcfg;
 mpSkyOff();                            /* a split game's sky gives its registers back */
 SCL_InitVramConfigTb(&vcfg);
 vcfg.vramModeA=OFF;
 vcfg.vramModeB=ON;
 vcfg.vramA0=SCL_NON;
 vcfg.vramA1=SCL_NON;
 vcfg.vramB0=SCL_NON;
 vcfg.vramB1=SCL_NON;
 SCL_SetVramConfig(&vcfg);
 SCL_SetColRamMode(SCL_CRM15_2048);
 setLayers(0);
}

/* CFG_TITLE_FONTS (INTRO.C playIntro): DTITLE.DAT into the title's pool (runLevel's mem_init frees
   it), its two fonts as fonts 1 and 2, the two skulls after them.  -> the next free VDP1 char. */
int doom_titleFonts(void)
{int fd,size,i,w,h;
 unsigned char *p,**list;
 stopCD();                              /* as INTRO.PCS below: no data read under the music */
 fd=fs_open("+DTITLE.DAT");
 size=fs_getFileSize(fd);
 p=mem_malloc(1,size);
 fs_read(fd,(char *)p,size);
 fs_close(fd);
 assert(!memcmp(p,"DTT1",4));
 titleBlock=p+8;
 assert(!memcmp(titleBlock,"DLG1",4) && *(short *)(titleBlock+LB_LEVELS)==FIRE_MAX+1);
 p=titleBlock+*(int *)(p+4);
 titleFonts[0]=titleFonts[1]=p+4;
 p+=4+*(int *)p;
 titleFonts[2]=p+4;
 p+=4+*(int *)p;
 titleFonts[3]=titleFonts[4]=NULL;
 list=doomFontList;                     /* the HUD's, from the last level: given back below */
 doomFontList=titleFonts;
 i=initFonts(0,CFG_FONT_DOOMLIST|6);
 doomFontList=list;
 w=((int *)p)[0];
 h=((int *)p)[1];
 EZ_setChar(i,COLOR_5,w,h,p+8);
 EZ_setChar(i+1,COLOR_5,w,h,p+8+w*h*2);
 skullChar=i;
 return i+2;
}

/* The logo's rectangle r = w, h, x, y -- or none (w = h = 0) when the block is not DLG1 or the
   rectangle leaves the shown NBG1 rows.  Not an assert: it survives NDEBUG, so a DTITLE.DAT or
   STATIC.DAT from another build (D4 P15) shows a black logo instead of writing past VRAM B. */
static void logoRect(const unsigned char *b,int *r)
{int i;
 for (i=0;i<4;i++)
    r[i]=((const short *)(b+LB_LOGO))[i];
 if (memcmp(b,"DLG1",4) || *(const short *)(b+LB_LEVELS)!=FIRE_MAX+1 || ((r[0]|r[2])&3)
     || r[0]<0 || r[1]<0 || r[2]<0 || r[3]<0 || r[2]+r[0]>512 || r[3]+r[1]>LOGO_ROWS)
    r[0]=r[1]=0;
}

/* CFG_TITLE_PICTURE (INTRO.C playIntro, after the VRAM clear, display off): colours, logo, fire */
void doom_titlePicture(void)
{const unsigned char *b=titleBlock;
 const unsigned int *src;
 struct fireArea *a;
 volatile struct fireCtl *c;
 int i,j,w,h,x,y,r[4];
 for (i=0;i<256;i++)
    POKE_W(SCL_COLRAM_ADDR+(i<<1),((const Uint16 *)(b+LB_PLAYPAL))[i]);
 for (i=0;i<FIRE_SLOTS;i++)
    POKE_W(SCL_COLRAM_ADDR+512+(i<<1),((const Uint16 *)(b+LB_RAMP))[i]);
 logoRect(b,r);
 w=r[0];
 h=r[1];
 x=r[2];
 y=r[3];
 src=(const unsigned int *)(b+LB_PIXELS);
 for (j=0;j<h;j++)
    for (i=0;i<w;i+=4)
       POKE(VRAM_B0+((y+j)<<9)+x+i,*src++);
 a=fireArea();
 c=FIRECTL(a);
 memcpy(a->R,b+LB_R,256);
 memcpy(a->U,b+LB_U,256);
 c->pQ8=GP_FIRE_TITLE_Q8;
 c->top=FIRE_TOP0;                      /* the PSX band; the rows above are lent to the menu */
 c->yOff=FIRE_Y0;                       /* 8: the solid base is the reference's 18 rows */
 c->look=0;                             /* bank 1 holds the ramp as is, no letters */
 c->shown=-1;
 fireField=0;
 fireEvery=GP_FIRE_FIELDS;
 dmaNoScu=1;                            /* the slave writes VDP2 VRAM: no SCU-DMA on the B-bus */
 startSlave(fireSlaveMain);             /* resets the slave, whatever it ran */
 fireOn=1;
 vblankUserHook=fireEvery>0? doom_fireKick: NULL;
}

/* CFG_TITLE_END (SRUINS.C main, when playIntro returns): the menu's skull goes.  The fire burns on
   (the slave, the kicks) through the black hand-over into the load, where doom_loadingScreen takes
   it: nothing between the two touches doorwayCache or VRAM B. */
void doom_titleEnd(void)
{skullChar=-1;
}

/* CFG_MENU_CURSOR (MENU.C dlg_draw, the chosen wavy button at x,y): Doom's skull, flipping every
   8 tics (14 fields) as M_Drawer's.  0 = none drawn (no title loaded, or no room left of the
   line): the engine's wave marks the line instead. */
int doom_menuCursor(int x,int y)
{XyInt pos;
 if (skullChar<0 || x-32<-160)
    return 0;
 pos.x=x-32;
 pos.y=y-2;
 EZ_normSpr(DIR_NOREV,ECD_DISABLE|COLOR_5,0,skullChar+((vtimer/14)&1),&pos,NULL);
 return 1;
}

/* ------------------------------------------------------------------------ the loading screen */
/* FILE.C progressHook: once a sector read (~150 a second), a few cycles.  The flames GROW with
   the load -- the decay falls from FIRE_LOAD_Q0 to FIRE_LOAD_Q1, so a flame goes from about a
   third of the band to the whole of it and its tips scatter over the logo. */
static void doom_loadProgress(int read,int total)
{int t=(read<<8)/(total>0? total: 1)+GP_FIRE_LOAD_LEAD;
 if (t>256)
    t=256;
 FIRECTL(fireArea())->pQ8=GP_FIRE_LOAD_Q0-
    (((GP_FIRE_LOAD_Q0-GP_FIRE_LOAD_Q1)*t)>>8);
 /* GCC14: the logo BURNS AWAY.  The flames grow with the load -- the decay walks from
    FIRE_LOAD_Q0 down to FIRE_LOAD_Q1, so they go from a quarter of the band to longer than the
    screen -- but a fire is a random thing and the last cold cells let the word show through.  At
    the end of the load NBG1 goes out: whatever the fire is doing, DOOM is gone by 100 %. */
 if (t>=256)
    {Scl_s_reg.dispenbl&=~0x0002;
     if (SclProcess==0)
	SclProcess=1;
    }
}

/* GCC14: the letters' INK -- every opaque cell of the glyph, grown by one cell in the eight
   directions, built once by the master.  `g` is the DAT's second mask plane, not the first: a
   letter of STCFN is not a silhouette, its body is bright red and its outline -- the ring around
   it, the hole of the O, the counters of the A and the D -- a very dark red.  Painting the body
   alone left white blobs with no detail; painting the WHOLE glyph dark and the body white on top
   gives the letter its holes back, over a white-hot fire as well as over black. */
static void fireHalo(struct fireArea *a,const unsigned char *g)
{int y,x,dy,dx,b;
 memset(a->MD,0,sizeof(a->MD));
 for (y=0;y<MASK_H;y++)
    for (x=0;x<FIRE_W;x++)
       {if (!(g[y*(FIRE_W/8)+(x>>3)] & (0x80>>(x&7))))
	   continue;
	for (dy=-1;dy<=1;dy++)
	   for (dx=-1;dx<=1;dx++)
	      {b=x+dx;
	       if (y+dy<0 || y+dy>=MASK_H || b<0 || b>=FIRE_W)
		  continue;
	       a->MD[y+dy][b>>3]|=(unsigned char)(0x80>>(b&7));
	      }
       }
}

/* GCC14: a death load shows NOTHING -- no logo, no fire, not a word (SRUINS.H loadAfterDeath).
   The corpse has just faded out under the player's own eyes; anything else steps on it.  The
   band is written with the TRANSPARENT index, so what shows is the back screen: black. */
static void fireBlackOnly(struct fireArea *a)
{int y,x;
 (void)a;
 for (y=0;y<224;y++)
    {volatile unsigned int *v=(volatile unsigned int *)(VRAM_B1+(y<<9));
     for (x=0;x<FIRE_W/4;x++)
	 v[x]=0;
    }
}

/* CFG_LOADING_SCREEN (SRUINS.C runLevel, STATIC.DAT's first block, display off): the title's
   picture again, over the fire -- still burning if the title just handed over, lit from its
   source line if a level did.  No pool byte: the block's head goes to the menu's corner of
   doorwayCache (no menu runs in a load), the logo straight from the disc into VRAM B0. */
void doom_loadingScreen(int fd)
{struct fireArea *a=fireArea();
 volatile struct fireCtl *c=FIRECTL(a);
 unsigned char *b=(unsigned char *)a->Fpad;      /* the rows the title lends: free until c->top */
 int i,j,w,h,x,y,look,r[4];
 Uint32 t;
 progressHook=doom_loadProgress;        /* first: FILE.C draws no bar while it is set */
 fs_read(fd,(char *)b,LB_PIXELS);
 assert(!memcmp(b,"DLG1",4) && *(short *)(b+LB_LEVELS)==FIRE_MAX+1);
 for (i=0;i<8;i++)                      /* setVDP2's, given back by doom_loadingEnd */
    savedCyc[i]=Scl_s_reg.vramcyc[i];
 savedN1Pri=SCL_GetPriority(SCL_NBG1);
 setLayers(1);
 logoRect(b,r);
 w=r[0];
 h=r[1];
 x=r[2];
 y=r[3];
 if (loadAfterDeath)                    /* no logo over a death: NBG1 out of the way */
    Scl_s_reg.dispenbl&=~0x0002;
 if (loadAfterDeath || !fireOn || !w)   /* B0 after a level (a split sky), or no logo wanted */
    for (i=0;i<LOGO_ROWS*512;i+=4)      /* what NBG1 shows: 112 rows, x2 */
       POKE(VRAM_B0+i,0);
 if (!fireOn)                           /* NBG0 above the fire (fireReset does the rest) */
    for (i=0;i<FIRE_Y0*512;i+=4)
       POKE(VRAM_B1+i,0);
 for (i=0;i<256;i++)
    POKE_W(SCL_COLRAM_ADDR+(i<<1),((const Uint16 *)(b+LB_PLAYPAL))[i]);
 for (j=0;j<h;j++)
    fs_read(fd,(char *)VRAM_B0+((y+j)<<9)+x,w);
 /* the fire's tables: R and U are the title's own bytes, so the running slave may read them
    while they are rewritten; P and M it reads only once the look below asks for them */
 memcpy(a->P,b+LB_PLOAD,FIRE_SLOTS);
 memcpy(a->R,b+LB_R,256);
 memcpy(a->U,b+LB_U,256);
 memcpy(a->M,b+LB_MASK,MASK_H*(FIRE_W/8));      /* the body; the ink from the second plane */
 fireHalo(a,b+LB_MASKG);
 if (loadAfterDeath)                    /* black, and nothing else: no logo, no fire, no word */
    {if (fireOn)                        /* the title's fire, if it was still burning */
	{vblankUserHook=NULL;
	 fireEvery=0;
	 startSlave(wallRenderSlaveMain);
	 fireOn=0;
	}
     fireBlackOnly(a);
     dmaNoScu=0;                        /* nothing writes VDP2 VRAM: the DMA may drive B again */
     SCL_DisplayFrame();
     SCL_DisplayFrame();
     return;
    }
 look=LOOK_MAP|LOOK_LETTERS;
 c->pQ8=GP_FIRE_LOAD_Q0;
 c->look=look;
 c->top=0;                              /* b is read out: the whole band burns from here */
 c->yOff=0;                             /* ... and it starts at the very top of the screen */
 fireField=0;
 fireEvery=GP_FIRE_LOAD_FIELDS;
 dmaNoScu=1;                            /* the slave writes VDP2 VRAM: no SCU-DMA on the B-bus */
 if (!fireOn)
    {c->shown=-1;
     startSlave(fireSlaveMain);         /* resets the slave: the wall renderer's, idle */
     fireOn=1;
    }
 vblankUserHook=fireEvery>0? doom_fireKick: NULL;
 *(Uint16 volatile *)0x21000000=0xffff; /* one step now: the repaint in the load's look */
 SCL_DisplayFrame();                    /* the registers reach the chip (frame mode 3) */
 SCL_DisplayFrame();
 t=vtimer;                              /* the first image shows the load's colours and letters */
 while (c->shown!=look && vtimer-t<30)
    ;
}

/* CFG_LOADING_END (SRUINS.C runLevel, before startSlave(wallRenderSlaveMain), which stops the fire
   wherever it is): the VDP2 as the game's load leaves it -- NBG1 off, the game's cycle table and
   priority, NBG0 the hidden weapon sheet (VRAM B keeps logo and fire bytes nobody shows; a split
   sky rewrites it) -- and one frame, so the layers go now rather than freeze through the rest of
   runLevel's set-up.  The framebuffer is still erased to opaque black. */
void doom_loadingEnd(void)
{loadAfterDeath=0;                      /* the next load is an ordinary one again */
 vblankUserHook=NULL;
 progressHook=NULL;
 fireOn=0;
 dmaNoScu=0;                            /* the slave stops writing VDP2: the DMA may drive B again */
 Scl_s_reg.dispenbl&=~0x0002;
 SCL_Open(SCL_NBG1); SCL_Scale(FIXED(1),FIXED(1)); SCL_Close();
 SCL_SetCycleTable(savedCyc);
 SCL_SetPriority(SCL_NBG1,savedN1Pri);
 vdp2SheetConfig();
 SCL_DisplayFrame();
}

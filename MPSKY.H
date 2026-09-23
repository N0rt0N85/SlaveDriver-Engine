/* MPSKY.H -- the split-screen sky, an engine service (GCC14).  See MPSKY.C. */
#ifndef __MPSKY_H
#define __MPSKY_H

#include <sega_mth.h>

extern int mpSkyOn;             /* the split sky owns RBG0, NBG0, NBG1 and both windows */
void mpSkyRepaintB0(void);      /* the mist's cells and the cloud names, after something used B0 */

void mpSkyPlayers(int players); /* 2-4: build the sky (once a level) or lay it for this count */
void mpSkyOff(void);            /* level load: the solo set-up takes the VDP2 back */
void mpSkyShow(int on);         /* enablePlax's split-screen half */
/* once an image: fog = the densest view's fog distance, offA = colour offset A (the damage
   flash, the fades), ticks = vblanks since the last image */
void mpSkyFrame(int fog,const int *offA,int ticks);
void mpSkyMoon(int view,MthMatrix *m);  /* before view's walls: its moon, under everything */
void mpSkyViewDone(int view);           /* after view's walls: its sky box, for the next sun */

/* Colour offset A's layers.  In split screen the sky's layers take offset B, which follows A
   plus the sky's own light (lightning): A must stop re-selecting them every tic. */
#define SKY_OFFSET_A (mpSkyOn? SCL_SP0: SCL_SP0|SCL_NBG0|SCL_RBG0)

/* GCC14: what the split sky keeps in VRAM B0 from one image to the next (offsets from
   SCL_VDP2_VRAM), for a layer that borrows B0 while the game is frozen (Doom's pause saves these
   and gives them back): the mist's cells, the clouds' names, the rotation's parameter table.  The
   rest of B0 is free.  MPSKY.C checks them against its own layout. */
#define MPSKY_B0_HAZE      0x40000      /* .. MPSKY_B0_HAZE_END: blank + 7x64 cells of 128 bytes */
#define MPSKY_B0_HAZE_END  (0x40000+(1+7*64)*128)
#define MPSKY_B0_CLOUD     0x50000      /* .. MPSKY_B0_CLOUD_END: 64x64 two-word names */
#define MPSKY_B0_CLOUD_END (0x50000+64*64*4)
#define MPSKY_B0_RPT       0x5F000      /* RBG0's parameter tables, A and B */

#endif

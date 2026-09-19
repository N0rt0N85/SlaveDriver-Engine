/* MPSKY.H -- the split-screen sky, an engine service (GCC14).  See MPSKY.C. */
#ifndef __MPSKY_H
#define __MPSKY_H

#include <sega_mth.h>

extern int mpSkyOn;             /* the split sky owns RBG0, NBG0, NBG1 and both windows */

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

#endif

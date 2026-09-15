#ifndef __INCLUDEDpich
#define __INCLUDEDpich

#include "picset.h"

enum Class {TILE16BPP,TILE8BPP,TILEVDP,TILESMALL16BPP,TILESMALL8BPP,
#ifdef JAPAN
	       TILEJCHAR,
#endif
	       NMCLASSES};

int initPicSystem(int _picNmBase,int *classSizes);

int addPic(enum Class bpp,void *data,void *pal,int lock);
   /* if a pic is locked, its memory may be free'd after this call */

int mapPic(int picNm);

void pic_nextFrame(int *swaps,int *used);

void loadTiles(int fd);
int loadWeaponTiles(int fd);

int getPicClass(int p);
void displayVDP2Pic(int picNm,int xo,int yo);
void updateVDP2Pic(void);
void dontDisplayVDP2Pic(void);
void delay_dontDisplayVDP2Pic(void);
void resetPics(void);

void setDrawModeBit(int class,int bit,int onOff);

void markAnimTiles(void);
void advanceWallAnimations(void);

int loadPicSetAsPics(int fd,int class);

int createMippedPics(void);

/* Which slot a miss evicts (PIC.C map, expanded there only).  PowerSlave: the least recently
   used by FRAME.  A Doom view can need more wall tiles than there are slots; the victim is then a
   tile this very frame already drew with, and every cell drawn with it shows the new texture.
   Doom ranks the current frame's tiles by the ORDER of their last use: the painter goes far to
   near (WALLS.C:2422), so the earliest is the farthest -- the smallest wrong patch.  Tiles of
   older frames still go first (negative age).  The clock wraps after ~2^31 mapPic calls. */
#ifdef GP_GAME_DOOM
#define CFG_PIC_DECL        static int doomPicSeq[MAXNMPICS],doomPicClock;
#define CFG_PIC_NONE        0x7fffffff
#define CFG_PIC_AGE(q)      ((q)->lastUse<frameCount? (q)->lastUse-frameCount: doomPicSeq[(q)-pics])
#define CFG_PIC_SEQ(p)      doomPicSeq[(p)-pics]=++doomPicClock&0x7fffffff;
#else
#define CFG_PIC_DECL
#define CFG_PIC_NONE        frameCount+1
#define CFG_PIC_AGE(q)      (q)->lastUse
#define CFG_PIC_SEQ(p)
#endif

#endif

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
void pic_flush(void);   /* GCC14: after SPR_WaitDrawEnd, before the image is shown (wall rule below) */

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

/* GCC14: CRAM banks of the object palette (UTIL.H NMOBJECTPALLETES) */
extern int nmObjectFogBanks;                    /* banks 0..n-1 carry the things' fog */
void buildObjectFogBanks(int n);                /* rebuild 1..n-1 from bank 0 */
void buildRemappedBank(int bank,const unsigned char *remap);   /* bank = bank 0 through remap[] */

/* GCC14: which slot a miss takes (PIC.C map).  lastUse is a clock, one tick per use: the least
   recently used goes first, within an image too -- the painter goes far to near, so an image's
   earliest tile is its farthest.
   The wall tiles' rule, on when gameparams PIC_LOD_PX > 0 (0 = PowerSlave's cache as shipped).  A
   wall cell asks for its tile with its size on screen, the larger side of its box in pixels, and
   is painted flat in picMeanColour(), under its own gouraud, when mapWallPic answers -1:
   - a wall never takes a slot the image being built uses, and when it takes one the image on
     screen uses, the texels only go in at pic_flush, once the VDP1 has finished that image.  The
     VDP1 draws the last image while the master builds the next: a far tile loaded early in the
     next one used to land in a slot the near walls, drawn last, still read;
   - a cell smaller than picLodNow pixels does not bring its tile in;
   - picLodNow is PIC_LOD_PX, raised when more tiles compete than there are slots less PICSPARE:
     each image ranks its tiles by their largest cell, the first ones keep a slot through the next
     image, and a newcomer must beat the last one kept.  The biggest on screen are the nearest.
   Wall tiles are indexed by a byte (level_texture, sFaceType.tile): the rule covers the pics
   below WALLPICS; the mip pics go through mapPic. */
#define WALLPICS 256
int mapWallPic(int picNm,int size);
int picMeanColour(int picNm);
extern int picLodNow,picLastSmall,picLastFull;  /* the image just closed: the bar, and the cells
						   painted flat under it / for want of a slot */

#endif

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
int vdp2PicCount(void);         /* GCC14: 0 = VRAM B0-B1 unused (MPSKY.C) */
/* GCC14: the weapon cut into VDP1 tiles for split screen -- see PIC.C.  Once it is on, nothing
   reads the VDP2 sheet any more and B0-B1 is the split sky's (MPSKY.C skyAllowed). */
int picWeaponSprites(void);     /* cut it: 1 = done, 0 = no room, leave the gun off */
int picVdp2Sub(int picNm,int cx,int cy);   /* a sub-tile, 128 x 128 frame pixels; -1 = none */
extern int weaponSpritesOn;
/* what the last cut did, for the STATUSTEXT overlay ("W:" in SRUINS.C) */
extern int weaponSpriteTry,weaponSpriteTiles,weaponSpriteKb,weaponSpriteDrawn,
	   weaponSpriteShown;
void dontDisplayVDP2Pic(void);
void delay_dontDisplayVDP2Pic(void);
void resetPics(void);
/* GCC14: the tile-upload buffer, lent while the world is not drawn (MENU.C) */
void *picScratch(int *size);
/* GCC14: how many more pics the table takes (a game adding tiles of its own: PSMULTI.C) */
int picRoom(void);
int picAddSpriteRle(void *rle);  /* ... and one: a 64x64 8 bpp RLE tile of the sprites' class */

void setDrawModeBit(int class,int bit,int onOff);

void markAnimTiles(void);
void advanceWallAnimations(void);

int loadPicSetAsPics(int fd,int class);

int createMippedPics(void);

/* GCC14: CRAM banks of the object palette (UTIL.H NMOBJECTPALLETES) */
extern int nmObjectFogBanks;                    /* banks 0..n-1 carry the things' fog */
extern int spectreBank;
extern int tintBank;                    /* GCC14: SECFLAG_TINT_CORE (PIC.C) */
void buildTintBank(int bank);           /* < 0 = the bank already chosen (UTIL.C setWorldTint) */                         /* bank n: grey and dark, SPRITEFLAG_MESH */
void buildObjectFogBanks(int n);                /* rebuild 1..n-1 from bank 0, and n for the spectre */
void buildRemappedBank(int bank,const unsigned char *remap);   /* bank = bank 0 through remap[] */
void buildTintedBank(int bank,int tr,int tg,int tb);            /* bank = bank 0 under a colour filter, 0..31 a channel */

/* GCC14: which slot a miss takes (PIC.C map).  lastUse is a clock, one tick per use: the least
   recently used goes first, within an image too -- the painter goes far to near, so an image's
   earliest tile is its farthest.
   The wall tiles' rule, on when gameparams PIC_LOD_PX > 0 (0 = PowerSlave's cache as shipped).  A
   wall cell asks for its tile with its size on screen, the SMALLER side of its box in pixels (a far
   floor 2 px tall and 30 long shows 2 rows of its texture), and is painted flat in
   picFirstColour(), under its own gouraud, when mapWallPic answers -1:
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
int picFirstColour(int picNm);  /* its first texel's colour: the flat cells and the far LOD */
extern int picLodNow,picLastSmall,picLastFull;  /* the image just closed: the bar, and the cells
						   painted flat under it / for want of a slot */

/* GCC14: the same slot rule for the things' tiles, TILE8BPP: the things, their shadow and the gun
   share 31 slots (params/doom.cfg PIC_SLOTS).  A command holds its slot's ADDRESS (SPR.C
   setCharPara) and the VDP1 reads the texels when it draws it; mapPic's plain LRU gave a slot the
   list already used to the next tile asked for, and uploaded at once.  In split screen every view
   asks its own rotation of each monster, then its gun: past 31 tiles an image, the next views'
   things took the slots of a gun already emitted -- the gun gone from its view, small copies of
   monsters and players in its place (a thing is one 64 x 64 tile, drawn at the gun's 32 px).
   Under the rule (PIC_LOD_PX > 0) every TILE8BPP tile goes through mapSpritePic, never mapPic:
   - no slot the image being built uses is taken, and one the image on screen uses is filled at
     pic_flush -- the walls' rule, above;
   - -1, not drawn: a tile smaller on screen than picSpriteLod pixels -- unless this image
     already holds it, which costs no slot -- or one that finds every slot taken by this image.
     The bar is 0 while an image refuses nothing: below 31 tiles an image, every thing is drawn
     as before.  Once an image refuses, its tiles -- held or refused, each ONCE at its largest --
     are ranked by size and the slots less PICSPARE largest set the bar: the farthest things go,
     the gun and the shadow (PIC_ALWAYS) never do;
   - the bar is one image late, and a gun that fires asks for up to 4 tiles more from one image
     to the next (DOOM1.WAD: pistol 1 -> 4 + its flash, shotgun 2 -> 4 -> 6): past the slots, the
     LAST requests of the image are the ones refused, and the last is a gun.  So in split
     screen every gun takes its tiles once the tics have set it, before the things of views 1..
     (SEQUENCE.C reserveWeaponTiles, SRUINS.C after Motion): what is refused then is a thing. */
#define PIC_ALWAYS 127
int mapSpritePic(int picNm,int size);
extern int picSpriteLod,picLastSpriteOut;       /* the bar, px; tiles refused in the image just
						   closed */

#endif

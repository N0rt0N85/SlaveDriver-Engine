#ifndef __INCLUDEDspriteh
#define __INCLUDEDspriteh

#include "util.h"

#include "object.h"

typedef struct{
 Fixed32 yaw;
 Fixed32 pitch;
 Fixed32 roll;
} Orient;


struct __sprite;

#define SPRITEFLAG_NOSPRCOLLISION 0x001
#define SPRITEFLAG_NOHITSCAN      0x002
#define SPRITEFLAG_NOSHADOW       0x004
#define SPRITEFLAG_IMATERIAL (SPRITEFLAG_NOSPRCOLLISION|SPRITEFLAG_NOHITSCAN|SPRITEFLAG_NOSHADOW)

#define SPRITEFLAG_FLASH       0x008
#define SPRITEFLAG_UNDERWATER  0x010
#define SPRITEFLAG_IMMOBILE    0x020
#define SPRITEFLAG_32x32       0x040
#define SPRITEFLAG_INVISIBLE   0x080

#define SPRITEFLAG_BBLOCKED    0x100
#define SPRITEFLAG_BWATER      0x200
#define SPRITEFLAG_BWATERBNDRY 0x400
#define SPRITEFLAG_BCLIFF      0x800
#define SPRITEFLAG_BSHORT      0x1000

#define SPRITEFLAG_ONSLIPPERYSLOPE 0x2000
#define SPRITEFLAG_BOUNCY          0x4000
#define SPRITEFLAG_NOSCALE         0x8000
#define SPRITEFLAG_FOOTCLIP        0x10000
#define SPRITEFLAG_LINE            0x20000
#define SPRITEFLAG_THINLINE        0x40000
#define SPRITEFLAG_COLORED         0x80000

typedef
struct __sprite
{MthXyz pos;
 MthXyz vel;
 Fixed32 radius,radius2;
 int angle,scale,frame;
     /* for lines, these are position of other point of line */
 struct __sprite *next;
 Fixed32 friction,gravity;
 Object *owner; /* object that this sprite belongs to */

 int flags;
 short s;
 short sequence; /* this is pushblock number if this thing is a push block */
 short floorSector; /* sector number of floor that we are in contact with
		       or -1 if not in contact with a floor */
 unsigned short color; /* gourau shading table for colored sprites */
} Sprite;

extern Sprite sprites[];

void freeSprite(Sprite *o);
void moveSprites(void);
void initSpriteSystem(void);
Sprite *newSprite(int sector,Fixed32 radius,Fixed32 friction,Fixed32 gravity,
		  int sequence,int flags,Object *owner);

int bumpWall(sWallType *wall,Sprite *o,int sector);

void frameAdvance(void);

void updateSavePositions(void);

extern Sprite *sectorSpriteList[MAXNMSECTORS];

void movePushBlock(int block,int dx,int dy,int dz);


/* functions to support ai routines */
void moveCamera(void);

/* in order of priority */
#define COLLIDE_SPRITE (0x8<<16)
#define COLLIDE_WALL   (0x4<<16)
#define COLLIDE_FLOOR  (0x2<<16)
#define COLLIDE_CEILING (0x1<<16)

int moveSprite(Sprite *sprite);
void moveSpriteTo(Sprite *sprite,int newSector,MthXyz *newPos);
int spriteAdvanceFrame(Sprite *sprite);
void spriteMakeSound(Sprite *sprite,int sNm);

int findSectorContaining(MthXyz *pos,int guessSector);

#define spriteMakeSound(s,sndNm) posMakeSound((int)(s),&(s)->pos,(sndNm))

void updatePushBlockPositions(void);

void shiftSprites(void);
void spriteRandomizeFrame(Sprite *o);

void suckSpriteParams(Sprite *s);

/* Build-time game parameters: gameparams.cfg -> $(BUILD)/gameparams.h (see the Makefile).
   Every hook below is a macro written ON the line it replaces in the .C files.  That is not
   cosmetic: assert() bakes __LINE__ into the binary (UTIL.H:82), so adding or removing a line
   -- even inside a #ifdef that is switched off -- would change every assert that follows.
   With the shipped defaults these all expand to the engine's original expressions, and the
   build is byte for byte the one you get without any of this. */
#include "gameparams.h"

#ifdef GP_PLAYER_MODEL_CYLINDER
#include "plrcyl.h"
#else
#define SPR_FOOT(o)        ((o)->radius)             /* centre -> floor contact (SPRITE.C:642)  */
#define SPR_HOVER(o)       F(GP_PLAYER_EYE_HOVER)    /* camera hover (SPRITE.C:644)             */
#define SPR_HEAD(o)        ((o)->radius)             /* centre -> ceiling (SPRITE.C:703,708)    */
#define SPR_SQUISH(o,c,fl) (((c)+(fl))>>1)           /* squeezed (SPRITE.C:706)                 */
#define SPR_STEP(o)        F(GP_PLAYER_STEP)         /* STEPHEIGHT (SPRITE.C:597)               */
#define SPR_YLO(o)         ((o)->pos.y)              /* body interval vs wall edges (SPRITE.C:200,205) */
#define SPR_YHI(o)         ((o)->pos.y)
#define SPR_YOUT(o,d)      (d)
#define SPR_SUPPORT(o,w)   0                         /* steep-slope plane offset (SPRITE.C:133) */
#define SPR_DY(m,s)        ((m)->pos.y-(s)->pos.y)   /* sprite/sprite (SPRITE.C:499)            */
#define CFG_PLAYER_INIT(s)                           /* AI.C:46                                 */
#define CFG_POSTURE(in,p)
#endif
#define PLAYER_RADIUS      F(GP_PLAYER_RADIUS)       /* AI.C:46                                 */

#ifdef GP_CROUCH_BUTTON_WEPDN
#define CFG_WEPDN          0                         /* that slot crouches now (SRUINS.C:987)   */
#define WEP_NEXT(m,d)      cylWeaponNext((m),(d))    /* so one button cycles (WEAPON.C:977)     */
#else
#define CFG_WEPDN          1
#define WEP_NEXT(m,d)      bitScanForward((m),(d))
#endif

/* L and R are strafe and run (V_BLANK.H:53-54) and get held together constantly in normal
   play, so "lr" fires by accident; "lr_x" adds a third button and is the safe one.
   Either way the toggle flips the engine's own cheatsEnabled (BUP.C:78-85). */
#ifdef GP_DEBUG_TOGGLE_LR_X
#define CFG_DEBUG(in,p)    if(!((in)&(PER_DGT_TL|PER_DGT_TR))&&((p)&PER_DGT_X)) cheatsEnabled=!cheatsEnabled;
#endif
#ifdef GP_DEBUG_TOGGLE_LR
#define CFG_DEBUG(in,p)    if(!((in)&PER_DGT_TR)&&((p)&PER_DGT_TL)) cheatsEnabled=!cheatsEnabled;
#endif
#ifndef CFG_DEBUG
#define CFG_DEBUG(in,p)
#endif
#define CFG_FRAME(in,p)    do{CFG_DEBUG(in,p) CFG_POSTURE(in,p);}while(0)

#endif

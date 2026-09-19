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
extern char noClipCheat;                        /* GCC14: test toggle, camera ignores walls */

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

/* GAME = doom (params/doom.cfg): the Doom runtime in game/doom/ (DOOM_ABI, SPEC_RUNTIME section
   9).  Same rule as above: every macro sits ON the line it replaces, the #else branch is the
   engine's original expression.  The tic clock: monsterMoveCounter accumulates vtimer (60 Hz
   vblanks) and the loop consumes CFG_TIC_UNIT per tic -- x7 / 12 gives 60*7/12 = 35 Hz exactly,
   CFG_TIC_CAP = 4 tics per frame either way (SRUINS.C:2139-2143, 2196, 2302). */
#ifdef GP_GAME_DOOM
int  game_placeObject(int ot);                       /* DOOM_GAME.C (OBJECT.C:212 hook)         */
void doom_playerTic(void);                           /* DOOM_PLAYER.C, 35 Hz                    */
char *doom_levelName(int lNm);                       /* DOOM_GAME.C, doomLevelNames[] bounded   */
#define CFG_TIC_ADD(v)       ((v)*7)                 /* SRUINS.C:2302                           */
#define CFG_TIC_UNIT         12                      /* SRUINS.C:2142, 2196                     */
#define CFG_TIC_CAP          48                      /* SRUINS.C:2139-2140                      */
#define CFG_PLACE(t)         game_placeObject(t)     /* OBJECT.C:212: 1 = consumed by game/doom */
/* OBJECT.C:213: game_placeObject places every Doom object, so the engine's switch is dead code for
   Doom -- `if (0)` lets the compiler drop it, and the PowerSlave constructors it alone reaches
   leave the link (the Doom build links with --gc-sections, Makefile) */
#define CFG_ENGINE_PLACE     if (0)
#define CFG_PLAYER_TIC()     doom_playerTic()        /* SRUINS.C:2143, before runObjects()      */
#define CFG_DOOR_FIT         GP_PLAYER_FIT_HEIGHT    /* AI.C:4304 SHORTOPENING threshold (56)   */
#define CFG_SPRITE_NEARCLIP  F(10)                   /* WALLS.C:2701                            */
#define CFG_LIFT_RESET(o)    if ((o)->channel!=-1) signalAllObjects(SIGNAL_SWITCHRESET,(o)->channel,0)  /* AI.C:4667 */
#define CFG_LIFT_STOP_SND    3                       /* AI.C:4674: ST_PUSHBLOCK+3 = PSTOP       */
void doom_pbBlockBits(int pb);                       /* DOOM_GAME.C                             */
#define CFG_LIFT_MOVED(o)    doom_pbBlockBits((o)->pbNum)   /* AI.C:4670: lift portals follow the floor */
void doom_spriteLeaves(void);                        /* WALLS.C (end): the leaf that draws each sprite */
extern Sprite *doomDrawHead[];
extern Sprite *doomDrawNext[];
#define CFG_SPRITE_LEAVES()  doom_spriteLeaves()     /* WALLS.C:2415, once the draw order is known */
#define CFG_SPR_FIRST(s)     doomDrawHead[s]         /* WALLS.C:2593 drawSprites                */
#define CFG_SPR_NEXT(o)      doomDrawNext[(o)-sprites]
#define CFG_LEVEL_NAME(l)    doom_levelName(l)       /* SRUINS.C:2510 (BIGMAP.C levelGraph)     */
#define CFG_PLAY_INTRO()                             /* INITMAIN.C:617: no LOGOS.PCS/OPEN.MOV   */
/* player / weapons (SPEC_PLAYER sections 1-2, 5; DOOM_PLAYER.C, DOOM_WEAPON.C) */
void doom_playerInit(void);                          /* every level, after initWeapon()        */
void doom_playerFrame(unsigned short input,unsigned short pushed);   /* 60 Hz, movePlayer      */
void doom_playerDamage(int damage,Object *source);   /* replaces playerHurt                    */
void doom_weaponNext(int dir);                       /* +1 / -1: next / previous owned weapon   */
void doom_weaponDraw(int nmFrames);                  /* the pinned psprite (advanceWeaponSequence) */
#define CFG_CONTROL(in,ch,p)   doom_playerFrame(in,p)           /* SRUINS.C:960                  */
#define CFG_DEATH_SFX          11                                /* SRUINS.C:967: PLDETH (1 = SHOTGN in the Doom static list) */
#define CFG_WEAPON_UP(m)       doom_weaponNext(1)                /* SRUINS.C:983                  */
#define CFG_WEAPON_DN(m)       doom_weaponNext(-1)               /* SRUINS.C:988                  */
#define CFG_BOWLDOTS()                                           /* SRUINS.C:984, 989: no bowls   */
#define CFG_FALL_MIN           0x7fffffff                        /* SRUINS.C:238: no fall damage  */
#define CFG_LEVEL_PLAYER_INIT() doom_playerInit()                /* SRUINS.C:2010                 */
#define CFG_RUN_WEAPON(n,i,b)  if (!mapOn) doom_weaponDraw(n)    /* SRUINS.C:2207; no gun over the automap */
#define CFG_PLAYER_HURT(hp,src) doom_playerDamage(hp,(Object *)(src))   /* AI.C:34 player_func  */
#define CFG_WOVERLAY_FROM      0x7fffffff                        /* SEQUENCE.C:252: no palette swap at wseq 50 */
#define CFG_WFLASH_OVERLAY     0                                 /* SEQUENCE.C:311: flash = object palette 0   */
#define CFG_ENGINE_FIRE        0                                 /* SRUINS.C:977: no fireWeapon() on an empty queue -- the Doom psprite machine fires (A_WeaponReady) */
void doom_useRefused(void);                          /* DOOM_PLAYER.C: sfx_noway                */
#define CFG_DEATH_PITCH        0                                 /* SRUINS.C:895: dead view stays level (Doom has no pitch) */
#define CFG_USE_REFUSED        else doom_useRefused();           /* SRUINS.C:853: use on a plain wall => sfx_noway (p_map.c PTR_UseTraverse) */
/* AI2.C:535: Doom uses the whole line (P_UseSpecialLine), not a 40 u disc around the orifice --
   the disc (L1-like approxDist) left 5-84 % of an episode-1 switch wall pressable */
#define CFG_SWITCH_AIM         0
extern int doomViewBob;                              /* DOOM_PLAYER.C: P_CalcHeight's bob       */
#define CFG_VIEW_BOB           doomViewBob                       /* SRUINS.C:2117: the view rides the walk bob */
#define CFG_PROF(id)           pushProfile(id)                   /* SRUINS.C:2196-2245: the frame after the walls, in the L+R+Y tree */
#define CFG_PROF_END()         popProfile()
/* the collision's three sub-nodes (SPRITE.C bumpSectorBoundries, bumpWalls, collideSprite)
   leave the Doom tree: 6 of its 24 lines, given to MOTION's parts, and a push per sector
   penetrated.  So do the traversal's two (WALLS.C wallsTraverse): the slave traverses the
   views, and pipe:-1 says when the master had to traverse view 0 */
#define CFG_PROF_SUB(id)
#define CFG_PROF_SUB_END()
extern int updateListSize,slaveSize,nmSlavePolys;    /* WALLS.C: sectors drawn, the slave's share   */
#define CFG_STATUS_SECTOR()    drawStringf(-158,-80,1,"sector:%d vis:%d sl:%d/%d",camera->s,updateListSize,slaveSize+1,nmSlavePolys)   /* SRUINS.C:2220 */
#define CFG_LIGHTRADIUS        181                               /* subtractive lights only (addLight); Doom: params LIGHT_* */
#define CFG_LIGHTSHIFT         10                                /* subtractive lights only: 181^2>>10 = 31 */
#define CFG_LIGHTSMOOTH        1                                 /* WALLS.C lightApply: s^2 falloff, soft edge */
/* SPRITE.C:658: a thing below its floor stands on it at once, as in Doom (P_ZMovement: z =
   floorz).  The engine rises 1/8 of the depth a tic, and against Doom's gravity of 1 u a tic --
   exactly the value where neither snap test above it fires -- that held every awake monster 7 u
   under its floor (d = 7/8 (d+1)).  The camera keeps the 1/8: Doom eases the view up a step the
   same way (deltaviewheight). */
#define CFG_FLOOR_RISE(o,d)    ((o)==camera? (d)>>3: (d))
#else
#define CFG_TIC_ADD(v)       (v)
#define CFG_TIC_UNIT         2
#define CFG_TIC_CAP          8
#define CFG_PLACE(t)         0
#define CFG_ENGINE_PLACE
#define CFG_PLAYER_TIC()
#define CFG_DOOR_FIT         80
#define CFG_SPRITE_NEARCLIP  F(32)
#define CFG_LIFT_RESET(o)
#define CFG_LIFT_STOP_SND    1
#define CFG_LIFT_MOVED(o)
#define CFG_SPRITE_LEAVES()
#define CFG_SPR_FIRST(s)     sectorSpriteList[s]
#define CFG_SPR_NEXT(o)      (o)->next
#define CFG_LIGHTRADIUS      256
#define CFG_LIGHTSHIFT       11
#define CFG_LIGHTSMOOTH      1
#define CFG_FLOOR_RISE(o,d)  ((d)>>3)
#define CFG_LEVEL_NAME(l)    getLevelName(l)
#define CFG_PLAY_INTRO()     playIntro()
#define CFG_CONTROL(in,ch,p)   controlInput(in,ch)
#define CFG_DEATH_SFX          1
#define CFG_WEAPON_UP(m)       weaponUp(m)
#define CFG_WEAPON_DN(m)       weaponDown(m)
#define CFG_BOWLDOTS()         redrawBowlDots()
#define CFG_FALL_MIN           F(1)
#define CFG_LEVEL_PLAYER_INIT()
#define CFG_RUN_WEAPON(n,i,b)  runWeapon(n,i,b)
#define CFG_PLAYER_HURT(hp,src) playerHurt(hp)
#define CFG_WOVERLAY_FROM      50
#define CFG_WFLASH_OVERLAY     0x4000
#define CFG_ENGINE_FIRE        1
#define CFG_DEATH_PITCH        F(80)
#define CFG_USE_REFUSED
#define CFG_SWITCH_AIM         1
#define CFG_VIEW_BOB           0
#define CFG_PROF(id)
#define CFG_PROF_END()
#define CFG_PROF_SUB(id)       pushProfile(id)
#define CFG_PROF_SUB_END()     popProfile()
#define CFG_STATUS_SECTOR()    drawStringf(-158,-80,1,"sector:%d",camera->s)
#endif

/* HUD / screen frame (SPEC_PLAYER section 3, owner: DOOM_HUD.C).  Doom runs a 320x224 frame: 3D
   window = screen lines 0..191 (YMIN -112 .. YMAX 80 around the line-112 centre), the STBAR char at
   lines 192..223 (y_e = y_doom - 88), weapon clip 192, DOOM_PSP_Y0 = 0 (DOOM.H).  Chars: 0 STBAR,
   1-3 keys, 4 face (doom_hudInit, replacing stat_bar + 3 compasses), fonts from 5.  Fonts: PRINT.o
   is linked into INIT, MAIN and KEYGEN, so the PowerSlave list stays the default one and the Doom
   list (STCFN 1, STTNUM 2, STYSNUM 3; set by doom_hudInit) is taken only when the initFonts mask
   carries CFG_FONT_DOOMLIST -- runLevel's call -- the menus/title/INIT keep brianFont/bigFont. */
#ifdef GP_GAME_DOOM
void doom_hudInit(void);                             /* DOOM_HUD.C: chars 0-4 + doomFontList    */
void doom_drawStatBar(void);                         /* DOOM_HUD.C                              */
void doom_drawMessage(void);                         /* DOOM_HUD.C                              */
extern int mapOn;                                    /* SRUINS.C:85                             */
#define CFG_FONT_DOOMLIST      0x10                              /* initFonts mask bit: Doom list */
#define CFG_NMFONTS            4                                 /* PRINT.C:14 MAXNMFONTS         */
#define CFG_FONTLIST_DEF       static unsigned char *peFontList[]={brianFont,brianFont,bigFont,NULL,NULL}; unsigned char **doomFontList; static unsigned char **fontList=peFontList;   /* PRINT.C:17 */
#define CFG_FONT_PICK(m)       fontList=(((m)&CFG_FONT_DOOMLIST)&&doomFontList)?doomFontList:peFontList;   /* PRINT.C:55 */
#define CFG_FONT_BASE          5                                 /* SRUINS.C:1902: after chars 0-4 */
#define CFG_FONT_MASK          (CFG_FONT_DOOMLIST|0xE)           /* fonts 1-3 of the Doom list    */
#define CFG_TV_SIZE            SPR_TV_320X224                    /* SRUINS.C:1878, 2433           */
#define CFG_SCL_LINES          SCL_224LINE                       /* SRUINS.C:2399, 2487           */
#define CFG_YCENTER            112                               /* SRUINS.C:2127, 2316-2317; WALLS.C; SEQUENCE.C:297; MAP.C */
#define CFG_YMIN               -112                              /* WALLS.C:129, MAP.C:22         */
#define CFG_YMAX               80                                /* WALLS.C:131, MAP.C:23         */
#define CFG_WCLIP_BOTTOM       192                               /* SEQUENCE.C:258                */
#define CFG_HUD_CHARS(x)       doom_hudInit()                    /* SRUINS.C:1890-1897            */
#define CFG_REDRAW_STATBAR()                                     /* SRUINS.C:1906: done by doom_hudInit */
#define CFG_BOWLDOTS_GUARD     return;                           /* SRUINS.C:1171: never poke bowls into STBAR */
#define CFG_DRAW_MESSAGE()     doom_drawMessage()                /* SRUINS.C:2211 (after drawMessage) */
#define CFG_DRAW_STATBAR(f)    doom_drawStatBar()                /* SRUINS.C:2212                 */
void doom_mpRegister(void);                          /* DOOM_PLAYER.C: the Doom player, per player (MPLAYER.H) */
void doom_mpNewPlayer(void);
void doom_drawSplitHud(int view,int nmViews);         /* DOOM_HUD.C */
#define CFG_MP_REGISTER()      doom_mpRegister()                 /* SRUINS.C mpRegisterEngine: the Doom player per player (MPLAYER.H) */
#define CFG_MP_NEWPLAYER()     doom_mpNewPlayer()                /* SRUINS.C mpBuild/mpRespawn: nothing carried, G_PlayerReborn */
#define CFG_DRAW_SPLITHUD(k,n) doom_drawSplitHud(k,n)           /* SRUINS.C view loop, 2-4 players: the compact HUD of view k */
short doom_playerBodySeq(Sprite *body,Sprite *viewer,int health);
#define CFG_MP_BODYSEQ(b,v,h) doom_playerBodySeq(b,v,h)       /* SRUINS.C mpShowBodies: the frame of a player's body seen from a view */
const unsigned char *doom_playerTranslation(int k);
#define CFG_MP_TRANSLATION(k) doom_playerTranslation(k)          /* MPLAYER.C mpSetBanks: player k's palette remap, NULL = none */
/* GCC14: game modes (MPLAYER.H mpMode; the rules in game/doom/DOOM_MODES.C) */
const char *doom_levelLabel(int l);
int  doom_bossLevel(int l);
void doom_mpLevelStart(void);
void doom_mpRespawned(int k);
void doom_levelEnd(int action);
#define CFG_MP_MENU            1                                 /* INTRO.C: the title's MULTIPLAYER button */
#define CFG_MENU_PITCH         24                                /* INTRO.C: four title buttons fit the screen */
#define CFG_MP_NMLEVELS        9                                 /* MPLAYER.C mpMenu: DOOM_NMLEVELS */
#define CFG_MP_LEVELLABEL(l)   doom_levelLabel(l)
#define CFG_MP_BOSSLEVEL(l)    doom_bossLevel(l)                 /* a level with a boss: the boss battle's */
#define CFG_MP_MONSTERS_NAME   "DEMONS"
#define CFG_MP_MONSTER_ROLE    "DEMON"
const char *doom_skillName(int s);
extern unsigned char doomSectorDamage[];
#define CFG_MP_NMSKILLS        4                                 /* Doom's but NIGHTMARE */
#define CFG_MP_SKILLDEFAULT    2                                 /* HURT ME PLENTY, Doom's own */
#define CFG_MP_SKILLNAME(s)    doom_skillName(s)
#define CFG_MP_SPOTSAFE(s)     (!doomSectorDamage[s])            /* no spawn on nukage or E1M8's last room */
#define CFG_MP_LEVELSTART()    doom_mpLevelStart()               /* SRUINS.C runLevel, after mpLevelBuild */
#define CFG_MP_RESPAWNED(k)    doom_mpRespawned(k)               /* SRUINS.C mpRespawn, player k loaded */
#define CFG_LEVEL_END(a)       doom_levelEnd(a)                  /* SRUINS.C runLevel, before an exit returns */
#define CFG_LEVEL_END_SHOWN(a) ((a)>=200 || (a)==2)              /* ... a level's exit, or the episode's end */
#define CFG_DRAW_AIRMETER(f)                                     /* SRUINS.C:2214: no air meter   */
#define CFG_AIR_PICS(x)                                          /* SRUINS.C:1978-1979: nor its two pics -- meter_bubble/back, 16 KB, leave the link */
/* PowerSlave paths a Doom level never takes, compiled out so their code leaves the link: water
   (doom2ps sets no SECFLAG_WATER), the doll power (a PowerSlave save in the same backup file could
   switch it on under Doom), the route plotter only PowerSlave's monsters read, the camel's map
   screen and travel question (hitCamel is only set by camel_func). */
#define CFG_WATER              0                                 /* SRUINS.C:916                  */
#define CFG_DOLLPOWER          0                                 /* SRUINS.C:966                  */
#define CFG_ROUTE_INIT()       ((void)0)                         /* SRUINS.C:1987                 */
#define CFG_CAMEL              0                                 /* SRUINS.C:2439                 */
#define CFG_RUN_MAP(l)         (l)                               /* SRUINS.C:2656                 */
#else
#define CFG_NMFONTS            3
#define CFG_FONTLIST_DEF       static unsigned char *fontList[]={brianFont,brianFont,bigFont,NULL};
#define CFG_FONT_PICK(m)
#define CFG_FONT_BASE          4
#define CFG_FONT_MASK          3
#define CFG_TV_SIZE            SPR_TV_320X240
#define CFG_SCL_LINES          SCL_240LINE
#define CFG_YCENTER            120
#define CFG_YMIN               -110
#define CFG_YMAX               90
#define CFG_WCLIP_BOTTOM       210
#define CFG_HUD_CHARS(x)       x
#define CFG_REDRAW_STATBAR()   redrawStatBar()
#define CFG_BOWLDOTS_GUARD
#define CFG_DRAW_MESSAGE()
#define CFG_DRAW_STATBAR(f)    drawStatBar(f)
#define CFG_MP_REGISTER()                                        /* the engine's player is all PowerSlave has */
#define CFG_MP_NEWPLAYER()
#define CFG_DRAW_SPLITHUD(k,n)
#define CFG_MP_BODYSEQ(b,v,h) (-1)                              /* PowerSlave has no body to show */
#define CFG_MP_TRANSLATION(k) ((const unsigned char *)0)
#define CFG_MP_MENU            0                                 /* PowerSlave has no multiplayer game */
#define CFG_MENU_PITCH         30
#define CFG_MP_NMLEVELS        1
#define CFG_MP_LEVELLABEL(l)   ""
#define CFG_MP_BOSSLEVEL(l)    0
#define CFG_MP_MONSTERS_NAME   "MONSTERS"
#define CFG_MP_MONSTER_ROLE    "MONSTER"
#define CFG_MP_NMSKILLS        1
#define CFG_MP_SKILLDEFAULT    0
#define CFG_MP_SKILLNAME(s)    ""
#define CFG_MP_SPOTSAFE(s)     1
#define CFG_MP_LEVELSTART()
#define CFG_MP_RESPAWNED(k)
#define CFG_LEVEL_END(a)
#define CFG_LEVEL_END_SHOWN(a) 0
#define CFG_DRAW_AIRMETER(f)   drawAirMeter(f)
#define CFG_AIR_PICS(x)        x
#define CFG_WATER              1
#define CFG_DOLLPOWER          1
#define CFG_ROUTE_INIT()       initRoutePlotter()
#define CFG_CAMEL              1
#define CFG_RUN_MAP(l)         runMap(l)
#endif

/* VDP1 VRAM slots per pic class, in PIC.H order (TILE16BPP walls, TILE8BPP sprites, TILEVDP,
   TILESMALL16BPP, TILESMALL8BPP) -- gameparams key PIC_SLOTS, PowerSlave's split by default. */
#ifndef GP_PIC_SLOTS
#define GP_PIC_SLOTS 28,31,1,10,12
#endif
/* Far LOD, in world units (WALLS.C wallIsFar) -- gameparams key FAR_LOD, 0 = off. */
#ifndef GP_FAR_LOD
#define GP_FAR_LOD 0
#endif

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

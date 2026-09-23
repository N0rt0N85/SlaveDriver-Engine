/* DOOM.H -- the ONE header of the Doom runtime (game/doom/, GAME = doom in params/doom.cfg).
 *
 * Contract: docs/doom/DOOM_ABI.md (sections 1, 4, 6, 8, 9); runtime: docs/doom/SPEC_RUNTIME.md
 * (sections 2, 6, 10); player/weapons/HUD/sound: docs/doom/SPEC_PLAYER_WEAPONS_HUD_SOUND.md
 * (sections 1.2, 2.2, 2.5, 3, 4.1, 4.3).  Types: DoomActor (SpriteObject prefix), DoomPlayer,
 * DoomAction (union {mobj, psp}); every public signature of the game/doom sources.
 *
 * Units: 1 Doom unit = 1 Saturn unit; angles = degrees << 16 (F(45)); Fixed32 16.16; big-endian
 * SH-2; C gnu89.  The generated tables (DOOM_TABLES.H / .C, info2tables.py) are GPL-2+ Doom data;
 * the engine hooks reach this code only through the CFG_* macros of SPRITE.H.
 */
#ifndef __INCLUDEDdoomh
#define __INCLUDEDdoomh

#include "util.h"
#include "sprite.h"
#include "object.h"
#include "doom_tables.h"

/* --- levels (contract section 9) ------------------------------------------------------------ */
#define DOOM_NMLEVELS 9                 /* size of doomLevelNames[]: the shareware episode (section 9) */

/* --- object types that are not mobjs (contract section 1) ----------------------------------- */
#define OT_DOOM_EXIT        176         /* params: channel (900)                                  */
#define OT_DOOM_SECRETEXIT  177         /* params: channel (901)                                  */
#define OT_DOOM_LIGHT       178         /* sector light special -- not before J3                  */
#define OT_DOOM_DAMAGE      179         /* params: sectorNm, hp (SPEC_RUNTIME section 6)          */
#define OT_DOOM_SECRETWALL  180         /* params: wall -- a DOORWALL of an ML_SECRET line: monsters never press it */
#define DOOM_MAXSECRETWALLS 64
#define DOOM_EXIT_CHANNEL        900
#define DOOM_SECRETEXIT_CHANNEL  901

/* --- DoomActor.mflags (SPEC_RUNTIME section 2) ---------------------------------------------- */
#define DF_AMBUSH       0x01            /* THINGS flag bit 3 at spawn                             */
#define DF_JUSTHIT      0x02
#define DF_JUSTATTACKED 0x04
#define DF_CORPSE       0x08
#define DF_MISSILE      0x10
#define DF_NOBLOOD      0x20
#define DF_SHOOTABLE    0x40
#define DF_DROPPED      0x80
#define DF_IDLE         0x100           /* already handed to objectIdleList (doom_setState)     */
#define DF_SOLID        0x200           /* MF_SOLID: stops missiles (PIT_CheckThing); A_Fall clears it */
#define DF_STEP         0x400           /* A_Chase's step: the velocity of the next tic only        */
#define DF_DOORASKED    0x800           /* doom_nearDoor asked where it stands (DOOM_ACTOR.C)       */
#define DF_NEARDOOR     0x1000          /* ... and its answer: at rest by a door, it collides       */
#define DF_PICKUP       0x2000          /* in doomPickups: the players test its reach (doom_pickupTic) */
#define DF_HALF         0x4000          /* far and unseen: A_Chase every other state, twice the step */
#define DF_HALFSTATE    0x8000          /* ... and the state it is in was lengthened for that */
#define DI_NODIR        8               /* movedir: no direction (p_enemy.c dirtype_t)            */

/* --- weapons (doomdef.h weapontype_t; index into doomWeaponInfo[]) -------------------------- */
typedef enum
{wp_fist=0,wp_pistol=1,wp_shotgun=2,wp_chaingun=3,wp_missile=4,
 wp_plasma=5,wp_bfg=6,wp_chainsaw=7,wp_supershotgun=8} doom_weapontype_t;
#define DOOM_WP_NOCHANGE (-1)           /* DoomPlayer.pendingWeapon                               */
#define DOOM_NUMAMMO 4                  /* am_clip, am_shell, am_cell, am_misl                    */
#define DOOM_PS_WEAPON 0                /* psprite slots (p_pspr.h psprnum_t)                     */
#define DOOM_PS_FLASH  1
#define DOOM_NUMPSPRITES 2

/* --- the generic Doom object (SPEC_RUNTIME section 2): SpriteObject prefix (OBJECT.H:62-69),
       then the mobj_t subset the E1M1 verbs need.  Must fit in an Object (144 bytes, asserted
       in DOOM_GAME.C).  Lives in objectRunList; sprite->owner points back here. ---------------- */
typedef struct
{short type,class;                      /* OT (contract section 1), CLASS_MONSTER/PROJECTILE/SPRITE */
 struct __object *next,*prev;
 messHandler func;                      /* game_actor_func / doom_item_func                        */
 struct __sprite *sprite;
 unsigned short *sequenceMap;           /* unused (sequence computed by doom_seq), kept for prefix */
 short state;                           /* state = doom_statenum_t                                 */
 short lookSkip;                        /* A_Look's parity when far and asleep (GP_MONSTER_FAR_LOOK) */

 short mt;                              /* doom_mobjtype_t                                         */
 short tics;                            /* -1 = forever (terminal states)                          */
 short health,reactiontime,threshold,movecount;
 unsigned char movedir,dirCur,nDir;     /* A_Chase (section 3.2): current try index, count        */
 char dirTry[8];                        /* doom_newChaseDir candidates, Doom order                 */
 unsigned char chStage,chD1,chD2,chOld; /* lazy candidate stages: the list grows (and draws P_Random) only when the previous candidates failed */
 unsigned char chFlags,flashTics;       /* chFlags bit 0: |dz| > |dx| when the list was started    */
					/* flashTics: tics left on the muzzle light (DOOM_ACTOR.C)  */
 unsigned short mflags;                 /* DF_*                                                    */
 Object *target;                        /* monster: target; missile: shooter -- cleared on DESTROYED */
 int collide;                           /* last moveSprite result, consumed by A_Chase             */
 short lastlook;
 unsigned short restEpoch;              /* pushBlockEpoch of its last collision at rest by a door  */
 struct __object *pkNext,*pkPrev;       /* a pickup: the list doom_pickupTic walks (DF_PICKUP)     */
 Fixed32 pkX,pkZ;                       /* ... where it lies: a pickup never moves sideways        */
} DoomActor;

/* --- exit switch target (contract section 6): SIGNAL_SWITCH(channel) -> playerHitTeleport ----- */
typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short channel;                         /* DOOM_EXIT_CHANNEL / DOOM_SECRETEXIT_CHANNEL             */
} DoomExitObject;

/* --- the player (SPEC_PLAYER section 1.2): ONE typedef, owned by DOOM_PLAYER.C.  Health itself
       stays currentState.health (read by the stat bar and the death branch); `health` here is a
       mirror kept by doom_playerDamage. ------------------------------------------------------- */
typedef struct
{Fixed32 momx,momz;                     /* u/tic, 16.16 (Doom momx/momy; engine z = Doom y)       */
 int turnHeld;                          /* tics of continuous turning (g_game.c SLOWTURNTICS 6)   */
 int health,armorPoints,armorType;
 int ammo[DOOM_NUMAMMO],maxAmmo[DOOM_NUMAMMO];   /* am_clip, am_shell, am_cell, am_misl         */
 unsigned char weaponOwned;             /* bit wp_*; = WEAPONINV(currentState.inventory)          */
 unsigned char keys;                    /* bits it_bluecard.. (p_inter.c:421-467)                 */
 signed char readyWeapon,pendingWeapon; /* pendingWeapon = DOOM_WP_NOCHANGE                       */
 int backpack;                          /* maxAmmo x2 once (p_inter.c:589-600)                    */
 int refire,attackDown,damageCount,bonusCount;
 short pspState[DOOM_NUMPSPRITES],pspTics[DOOM_NUMPSPRITES];   /* ps_weapon, ps_flash            */
 Fixed32 bob,pspSx,pspSy;
 unsigned short input,pushed;           /* last 60 Hz input; pushed = edges OR-ed since the last tic */
 int fire;                              /* BT_ATTACK of the current tic (held, or pushed between tics) */
 int extralight;                        /* A_Light0/1/2 (colour offset, plan section 2.5)         */
 int muzzleTics;                        /* tics left on the muzzle flash (DOOM_WEAPON.C)          */
 Object *attacker;                      /* last damager (P_DeathThink turns towards it)           */
 int lastFloor;                         /* camera->floorSector at the previous frame (sfx_oof)    */
 Fixed32 lastVelY;                      /* camera->vel.y at the previous frame                    */
} DoomPlayer;
#define DOOM_PSP_Y0 0                   /* contract section 5: 224-line frame, 3D window = screen lines 0..191 (SPRITE.H CFG_YMIN) */
extern DoomPlayer doomPlayer;

/* --- verbs: ONE table for actor and weapon verbs (contract section 4); the member is chosen by
       doomStates[].flags & DOOM_SF_PSPRITE (states 1..89 = .psp, the rest = .mobj).  Built in
       DOOM_VERBS.C from the X-macro of DOOM_ACTIONS.H. ---------------------------------------- */
typedef union
{void (*mobj)(DoomActor *this);
 void (*psp)(DoomPlayer *p,int ps);
} DoomAction;
extern const DoomAction doomActions[DOOM_NUMACTIONS];   /* [0] = {NULL}                          */

/* ============================================================================================ */
/* DOOM_GAME.C -- placement hook, level names, exit, items, damage sectors (SPEC_RUNTIME 6, 9)   */
/* ============================================================================================ */
void        doom_init(void);                   /* size guards + tables sanity; once per boot     */
int         game_placeObject(int ot);          /* OBJECT.C:212 hook: 1 = consumed (params read)  */
void        doom_item_func(Object *this,int message,int param1,int param2);   /* leaves doomPickups when destroyed */
void        doom_pickupTic(void);              /* 35 Hz from doom_playerTic: the player picks up what it touches */
extern Object *doomPickups;                    /* every pickup of the level (DF_PICKUP, DOOM_ACTOR.C) */
void        exit_func(Object *this,int message,int param1,int param2);        /* OT_DOOM_EXIT   */
void        doomLight_func(Object *this,int message,int param1,int param2);   /* OT_DOOM_LIGHT  */
char       *doom_levelName(int lNm);           /* CFG_LEVEL_NAME: doomLevelNames[lNm], bounded   */
extern unsigned char doomSectorDamage[MAXNMSECTORS];   /* OT_DOOM_DAMAGE: hp per 32 tics, 0 = none */
void        doom_weaponMpRegister(void);        /* GCC14: MPLAYER.H, per-player state of DOOM_WEAPON.C */
void        doom_hudMpRegister(void);           /* GCC14: ... of DOOM_HUD.C */
void        doom_sectorDamageTic(void);        /* 35 Hz from doom_playerTic: leveltime++, nukage */
void        doom_wlineTic(void);               /* 35 Hz from doom_playerTic: walk-over lines crossed (OT_DOOM_WLINE) */
extern int  doomLevelTime;                     /* THE 35 Hz level clock (Doom leveltime)         */
extern const char *doomLevelNames[DOOM_NMLEVELS];      /* "+E1M1.LEV", ... (8.3, root)           */
int         doom_wallIsSecret(int w);          /* OT_DOOM_SECRETWALL: 1 = monsters do not press it */
int         doom_monsterUseDoor(int w);        /* doomBlocked: 1 = the door wall answers (P_UseSpecialLine) */
int         doom_nearDoor(Sprite *s);          /* a door's sector under it, or within its radius */
void        doom_bossDeath(DoomActor *mo);     /* A_BossDeath: the level's boss rule (E1M8 tag 666) */

/* ============================================================================================ */
/* DOOM_ACTOR.C -- the generic object (SPEC_RUNTIME sections 2, 4, 5, 7)                        */
/* ============================================================================================ */
void        game_actor_func(Object *this,int message,int param1,int param2);
DoomActor  *doom_spawn(int mt,int sector,MthXyz *pos,int angle,int thingFlags);   /* NULL = no slot */
void        doom_setState(DoomActor *this,int state);                          /* P_SetMobjState */
#define DOOM_BASETHRESHOLD 100         /* p_local.h:58 -- tics a hurt monster keeps its target */
short       doom_seq(int sprite,int frame,int view);                           /* contract section 2 formula */
void        doom_damage(Sprite *target,Object *inflictor,Object *source,int damage);
void        doom_damageActor(DoomActor *this,int damage,Object *source);       /* P_DamageMobj   */
void        doom_radiusAttack(DoomActor *spot,Object *source,int damage);      /* A_Explode      */
void        doom_noiseAlert(Object *emitter,int sector);                       /* P_RecursiveSound */
extern Object *doomSoundTarget[MAXNMSECTORS];  /* per sector, last noise emitter (A_Look)       */
Object     *doom_spawnMissile(DoomActor *src,Object *dest,int mt);
Object     *doom_spawnPlayerMissile(int mt,int angle,int pitch);   /* P_SpawnPlayerMissile: from the camera, sprite-convention angle */
void        doom_spawnPuff(MthXyz *pos,int sector,int melee);
void        doom_spawnBlood(MthXyz *pos,int sector,int damage);
int         doom_lineAttack(DoomActor *src,int yaw,int damage);   /* returns the hitScan code    */
void        doom_missileLightsReset(void);     /* every level: forget the missiles carrying a light */
void        doom_sightShareReset(void);        /* every level: empty the shared sight answers */
void        doom_actorLevelInit(void);         /* level start: sound targets, nmSpawnFail       */
Sprite     *doom_targetSprite(Object *t);      /* player -> camera, DoomActor -> sprite, else NULL */
int         doom_targetAlive(Object *t);       /* player: currentState.health; actor: health && DF_SHOOTABLE */
Fixed32     doom_approxDist2(Fixed32 dx,Fixed32 dz);   /* P_AproxDistance (2D)                  */
extern int  nmSpawnFail;                       /* doom_spawn returned NULL (overlay counter)    */

/* ============================================================================================ */
/* DOOM_VERBS.C -- A_* (actor AND weapon verbs, SPEC_RUNTIME 3 / SPEC_PLAYER 2.2) + helpers     */
/* ============================================================================================ */
int         doom_lookForPlayer(DoomActor *this,int allaround);
int         doom_meleeRange(DoomActor *this);
int         doom_missileRange(DoomActor *this);
void        doom_newChaseDir(DoomActor *this);

/* ============================================================================================ */
/* DOOM_PLAYER.C -- integrator, health/armour, pickups (SPEC_PLAYER 1.2, 1.3, 4.3)              */
/* ============================================================================================ */
void        doom_playerInit(void);             /* SRUINS.C:2010, after initWeapon(); every level */
void        doom_playerFinishLevel(void);      /* G_PlayerFinishLevel: the next level keeps the arsenal */
void        doom_playerGodOff(void);           /* special 11 (E1M8's end): cheats &= ~CF_GODMODE */
void        doom_playerSawPull(void);          /* A_Saw hit: MF_JUSTATTACKED, the next tic pulls forward */
void        doom_playerTic(void);              /* 35 Hz, CFG_PLAYER_TIC, BEFORE runObjects()     */
void        doom_playerFrame(unsigned short input,unsigned short pushed);   /* 60 Hz, before moveCamera() */
void        doom_playerDamage(int damage,Object *source);   /* replaces playerHurt; ONLY armour calc */
void        doom_playerThrust(Sprite *inflictor,int damage); /* P_DamageMobj thrust on the player    */
int         doom_playerGetObject(int mt,int dropped);       /* 1 = consumed (effects only)      */
void        doom_weaponNext(int dir);          /* +1 / -1: SRUINS.C:983/988 (CFG_WEAPON_UP/DN)   */
void        doom_useRefused(void);             /* CFG_USE_REFUSED, SRUINS.C:853: sfx_noway       */

/* ============================================================================================ */
/* DOOM_WEAPON.C -- psprite state machine, hitscan/autoaim (SPEC_PLAYER 2.2, 2.5)               */
/* ============================================================================================ */
void        doom_psprTic(void);                /* P_MovePsprites, called by doom_playerTic       */
void        doom_setPsprite(int ps,int state); /* P_SetPsprite: loops while tics == 0            */
void        doom_fireWeapon(void);             /* P_FireWeapon                                   */
int         doom_checkAmmo(void);              /* P_CheckAmmo                                    */
void        doom_bringUpWeapon(void);          /* P_BringUpWeapon                                */
int         doom_aimSlope(Fixed32 *outPitch);  /* P_BulletSlope: autoTarget -> pitch; 0 if none  */
void        doom_muzzleFlash(void);            /* dynamic light on camera, two tics             */
void        doom_muzzleTic(void);              /* its fade and removal, from doom_playerTic     */
void        doom_gunShot(int accurate,int damage,int range);   /* P_GunShot + P_LineAttack       */
Sprite     *doom_playerLineAttack(int yaw,int pitch,Fixed32 range,int damage,int melee);   /* P_LineAttack from the camera: the sprite hit (linetarget) or NULL */
void        doom_weaponDraw(int nmFrames);     /* SRUINS.C:2207 (CFG_RUN_WEAPON): draw the pin  */

/* ============================================================================================ */
/* DOOM_HUD.C -- status bar, face, HU message (SPEC_PLAYER 3)                                   */
/* ============================================================================================ */
void        doom_hudInit(void);                /* CFG_HUD_CHARS (SRUINS.C:1890): chars 0-4, fonts */
void        doom_drawStatBar(void);            /* replaces drawStatBar (SRUINS.C:1206-1338)      */
void        doom_setMessage(const char *msg);  /* replaces changeMessage                         */
void        doom_drawMessage(void);            /* replaces drawMessage                           */

/* ============================================================================================ */
/* DOOM_MODES.C -- the multiplayer modes (MPLAYER.H mpMode): roles, frags, limits, the score    */
/* ============================================================================================ */
/* The loaded player's monster, when it wears one (doomRoleMt[mpCur] != 0).  Per player. */
typedef struct
{short maxHealth;                       /* what the split HUD's percent is of                   */
 short cool;                            /* tics before the next attack                          */
 short atk;                             /* tics into the attack's frames, -1 = none              */
 short hurt;                            /* tics of the pain frame                               */
 short dash;                            /* the lost soul's charge, tics left                    */
 short melee;                           /* the attack running is a blow (its frames)            */
 short dieTic;                          /* doomLevelTime of the death (the death frames)        */
 short alt;                             /* the attack chosen: 0 its missile, 1 its blow (Z)      */
} DoomRole;
extern DoomRole doomRole;
extern short doomRoleMt[];                     /* [MPMAX] the monster each player wears, 0 = marine */
void        doom_modesMpRegister(void);
int         doom_isMonsterPlayer(int k);
int         doom_roleSpeed(void);              /* percent of the marine's thrust                 */
void        doom_roleTic(void);                /* 35 Hz instead of the psprites                  */
void        doom_roleHop(void);                /* DEMONS: previous weapon (Y) changes monster    */
void        doom_roleNextAttack(void);         /* next weapon (Z): the monster's other attack     */
void        doom_rolePain(void);
short       doom_roleBodySeq(int k,Sprite *body,Sprite *viewer,int health);
void        doom_playerKilled(Object *source); /* the loaded player died: frags, the crown        */
void        doom_deathLook(void);             /* dead: the view turns towards the killer          */
void        doom_modesTic(void);               /* 35 Hz, once: limits, the crown                 */
int         doom_modePlace(int mt,int sector,MthXyz *pos,int angle,int flags);   /* 1 = not placed in this mode or skill */
const char *doom_skillName(int s);
void        doom_modesLevelReset(void);        /* a level's placement begins                     */
int         doom_levelOpenChannel(int i);      /* BOSS BATTLE: the channels that open the arena, -1 = end */
void        doom_mpLevelStart(void);
void        doom_mpRespawned(int k);
int         doom_mpRespawnHold(int k);         /* CFG_MP_RESPAWN_HOLD: 1 = stays where it fell */
int         doom_chainAt(int st,int t);        /* the state t tics into the chain starting at st */
void        doom_shootLine(Fixed32 hx,Fixed32 hz);  /* P_ShootSpecialLine: a line a bullet opens */
void        doom_mpJoined(int k);
void        doom_levelEnd(int action);
/* DOOM_HORDE.C -- MP_HORDE, the survival mode */
void        doom_hordeLevelReset(void);        /* a level's placement begins: forget the families */
void        doom_hordeSaw(int mt);             /* ... this level ships that family               */
void        doom_hordeLevelStart(void);        /* every player built: the first wave is on       */
void        doom_hordeTic(void);               /* 35 Hz, once: the waves                         */
void        doom_hordeKilled(DoomActor *this); /* doomKill: what the corpse leaves behind        */
int         doom_hordeWave(void);              /* the wave reached -- the mode's score           */
/* DOOM_GAME.C, for the modes */
int         doom_exiting(void);                /* the level is over                              */
void        doom_endRound(void);               /* a fighting game's limit: the next round        */
const char *doom_levelLabel(int l);            /* "E1M1"                                         */
int         doom_bossLevel(int l);             /* 1 = a level with a boss                        */
int         doom_levelBossMt(void);            /* this level's boss type, -1 = none               */
int         doom_bossCount(int l);             /* the bosses level l holds                        */
const char *doom_bossName(int l);              /* what they are, "" = none                        */
/* DOOM_WEAPON.C: what doom_aimSlope aimed at (a monster, or a player not on our side) */
extern Sprite *doomAimed;

/* ============================================================================================ */
/* DOOM_SOUND.C -- sfx -> sound index (contract section 3; SPEC_PLAYER 4.1) -- ONE definition  */
/* ============================================================================================ */
int         doom_sfxIndex(int sfx);            /* static: staticSoundMap[ST_JOHN]+n; else objectSoundMap[sfx]; <0 absent */
void        doom_sound(Sprite *s,int sfx);     /* spriteMakeSound(s,idx); s == NULL => playSound(0,idx) */
void        doom_playerSound(int sfx);         /* playSound((int)player, idx)                    */
void        doom_posSound(Object *src,MthXyz *pos,int sfx);   /* posMakeSound(src, pos, idx)     */

#endif /* __INCLUDEDdoomh */

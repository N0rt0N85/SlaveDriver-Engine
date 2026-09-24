/* MPLAYER.H -- local multiplayer, an engine service (GCC14).  Up to MPMAX players on one
   console, each with its own view of the same world.  Game-agnostic by construction.

   The engine was written for one player, and its player is a set of globals (camera,
   playerAngle, currentState.health, the game's own player struct...) read at a few hundred
   sites.  Rather than rewrite those sites, every module that owns per-player globals
   REGISTERS them, and mpSwitch(k) stores the loaded player's copies and loads player k's.
   Code that runs AS a player -- input, movement, weapon, damage, HUD -- then keeps reading
   its usual globals, unchanged and correct.  Only code that must see ALL players at once
   (monster targeting, pickups, "is this sprite a player?") uses the accessors below, which
   answer whatever player is loaded.

   Solo never switches: mpCur stays 0, nothing is ever copied, and every registered global
   is exactly the global it always was. */
#ifndef __INCLUDEDmplayerh
#define __INCLUDEDmplayerh

#define MPMAX 4

extern int mpPlayers;      /* players in the current level, 1..MPMAX                      */
extern int mpArmed;        /* count chosen for the NEXT game (the title's NEW GAME / MULTIPLAYER screens) */
extern int mpCur;          /* the player whose copies the registered globals hold          */

/* A global that belongs to ONE player.  Idempotent (same address = same block), and made
   once, before the first player is built: a block registered later would have no copy in
   the slots already filled. */
void mpRegister(void *addr,int size);
#define MPREG(v) mpRegister(&(v),sizeof(v))
extern int mpRegisterLost;              /* blocks refused, no room left: must stay 0 */

void mpSwitch(int k);                   /* store mpCur's copies, load k's                  */
int  mpBegin(int k);                    /* mpSwitch(k), returning who was loaded            */
#define mpEnd(prev) mpSwitch(prev)
void mpStoreAs(int k);                  /* copy what is loaded into slot k (a new player's template) */
void mpPeek(int k,void *addr,int size,void *out);   /* player k's copy of a registered global */
void mpPoke(int k,void *addr,int size,const void *in);   /* ... and writing it, without loading k */
int  mpPeekInt(int k,int *addr);

/* Bodies: valid whatever mpCur is.  Set by the engine when a player is built. */
extern struct __sprite *mpBody[MPMAX];
extern struct __object *mpObj[MPMAX];
int  mpIndexOfSprite(struct __sprite *s);   /* -1 = not a player's body */
int  mpIndexOfObject(struct __object *o);
#define mpIsPlayer(s) (mpIndexOfSprite(s)>=0)

/* Colours: player k>0's body is drawn from CRAM bank mpBank[k] (0 = its normal fogged banks),
   bank 0 seen through the game's remap (CFG_MP_TRANSLATION, SPRITE.H).  The banks come from the
   top of the object palette's fog and from the sky's, which split screen does not show.
   mpSetBanks() after every change of mpPlayers and after a level's palettes are loaded. */
extern unsigned char mpBank[MPMAX];
void mpSetBanks(void);

/* Pads: player k reads its own 60 Hz queue (V_BLANK.C fills them) from its own pad; a player
   whose pad is absent stands still. */
extern volatile unsigned short inputQP[MPMAX][16];
extern volatile unsigned short lastInputSampleP[MPMAX];
extern int mpPadsPresent;               /* pads found at the last poll */

/* Rules of the game, chosen on the title's multiplayer screen (mpMenu) and frozen for the whole
   game: nothing in a level writes them but the boss battle's mpRole.  The engine knows sides,
   spawn spots and the score; what a role IS (a monster the player drives, the boss) belongs to
   the game (CFG_MP_* hooks). */
/* MP_HORDE (game/doom/DOOM_HORDE.C): the survival mode.  Appended, so the values above keep
   theirs; it is the one mode the multiplayer screen lets a single player start. */
enum {MP_COOP,MP_DM,MP_TEAM,MP_MONSTERS,MP_BOSS,MP_HORDE,MP_NMMODES};
extern int mpMode;
extern unsigned char mpTeam[MPMAX];     /* MP_TEAM: 0 or 1 */
extern unsigned char mpRole[MPMAX];     /* MP_MONSTERS: 1 = drives a monster, 0 = the normal player;
					   MP_BOSS: 1 = a boss -- the one role a level writes: the
					   crown moves it, a newcomer may take it */
extern int mpFragLimit,mpTimeLimit;     /* 0 = none; the time in minutes */
extern int mpStartLevel;                /* the level a new game starts on */
#define mpCompetitive() (mpMode==MP_DM || mpMode==MP_TEAM || mpMode==MP_BOSS)
int  mpAllies(int a,int b);             /* same side: killing an ally costs a frag */
#define mpSpares(a,b) ((mpMode==MP_TEAM || mpMode==MP_MONSTERS || mpMode==MP_BOSS) && (a)!=(b) && mpAllies(a,b))
                                        /* no damage between them (co-op keeps Doom's friendly fire) */
int  mpSide(int k);                     /* the side that scores k's frags: k itself, or its team */

/* The score of the level.  kills/items/secrets out of mpTotal[0..2] (counted by the game when it
   places the level), frags as Doom counts them: +1 an enemy, -1 an ally or oneself. */
typedef struct {short kills,items,secrets,frags,deaths;} MpStat;
extern MpStat mpStat[MPMAX];
extern short mpTotal[3];
void mpScoreDeath(int victim,int killer);   /* killer -1 = the world, -2 = a monster */

/* Spawn spots of the level (competitive modes): the game adds the places its monsters would
   have stood; a player is (re)born on the one farthest from the others. */
void mpSpotAdd(int sector,MthXyz *feet,int yaw);
int  mpSpotFar(int k,int *sector,MthXyz *feet,int *yaw);   /* 0 = no spot; never on a floor the game calls unsafe (CFG_MP_SPOTSAFE) while a safe one is left */
/* The spots themselves, for a game that chooses by its own rule instead of mpSpotFar's respawn
   one (game/doom/DOOM_HORDE.C picks the one nearest the marines ON FOOT). */
int  mpSpotCount(void);
int  mpSpotGet(int i,int *sector,MthXyz *feet,int *yaw);   /* 0 = no such spot */
void mpLevelReset(void);                /* runLevel, before the level's objects are placed */

int  mpMenu(void);                      /* the title's multiplayer screen: 1 = start that game */
int  mpNewGameMenu(void);               /* the title's NEW GAME screen (skill, players): 1 = start */
extern int mpSkill;                     /* the game's difficulty, 0..CFG_MP_NMSKILLS-1 */
void mpSoloRules(void);                 /* the title's own new game: co-op rules, first level */
void mpIntermission(const char *title,int seconds,int tics);   /* in a level: the score until a button */
#endif

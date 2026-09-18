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
extern int mpArmed;        /* count chosen for the NEXT game (START on pad 2, at the menu) */
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
int  mpPeekInt(int k,int *addr);

/* Bodies: valid whatever mpCur is.  Set by the engine when a player is built. */
extern struct __sprite *mpBody[MPMAX];
extern struct __object *mpObj[MPMAX];
int  mpIndexOfSprite(struct __sprite *s);   /* -1 = not a player's body */
int  mpIndexOfObject(struct __object *o);
#define mpIsPlayer(s) (mpIndexOfSprite(s)>=0)

/* Pads: player k reads its own 60 Hz queue (V_BLANK.C fills them).  A pad that is absent
   mirrors another -- player 3 plays pad 1, player 4 pad 2 -- so 3 and 4 players can be tried
   on an emulator that offers two pads. */
extern volatile unsigned short inputQP[MPMAX][16];
extern volatile unsigned short lastInputSampleP[MPMAX];
extern int mpPadsPresent;               /* pads found at the last poll */
void mpMenuFrame(void);                 /* the title menu's player count line (MENU.C) */
#endif

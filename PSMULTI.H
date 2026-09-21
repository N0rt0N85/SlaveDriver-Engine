/* PSMULTI.H -- PowerSlave's side of local multiplayer (GCC14).  The engine owns the service
   (MPLAYER.H, MPRULES.C); this is what the CFG_MP_* hooks of SPRITE.H call on the PowerSlave
   runtime, the way game/doom/DOOM_MODES.C answers them on Doom's. */
#ifndef __INCLUDEDpsmultih
#define __INCLUDEDpsmultih

struct __sprite;

#define PS_NMLEVELS 23                  /* the maps the disc holds (psMpLevel[]) */

int         ps_mpLevel(int i);          /* MP map i -> the BIGMAP.C level number */
const char *ps_levelLabel(int i);
int         ps_startLevel(void);        /* SRUINS.C: the multiplayer map, or PowerSlave's map screen */

void  ps_mpRegister(void);
void  ps_mpNewPlayer(void);
void  ps_levelPlaced(void);             /* after placeObjects: fog colour, skin, spawn spots */
void  ps_mpLevelStart(void);            /* after mpLevelBuild: the roles take their monster */
void  ps_mpRespawned(int k);
int   ps_mpRespawnHold(int k);
void  ps_mpJoined(int k);
void  ps_levelEnd(int action);
void  ps_playerTic(void);
void  ps_playerHurt(int hpLost,struct __object *source);

short ps_playerBodySeq(struct __sprite *body,struct __sprite *viewer,int health);
int   ps_playerTint(int k);
void  ps_drawSplitHud(int view,int nmViews);

/* the roles: a player wearing one of the level's monsters (MP_MONSTERS, MP_BOSS).  The table
   of what a worn monster is -- health, touch, missile, frames -- is psRoles[] in PSMULTI.C. */
int   ps_roleNoWeapon(void);
void  ps_roleFire(void);
int   ps_isMonsterPlayer(int k);
int   ps_bossLevel(int l);
int   ps_bossCount(int l);
const char *ps_bossName(int l);

#endif

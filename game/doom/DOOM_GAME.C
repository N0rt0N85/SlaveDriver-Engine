/* DOOM_GAME.C -- placement hook, level names, exit switch, damage sectors, item collision.
 * SPEC_RUNTIME sections 6 and 9; contract sections 1, 6, 9.
 *
 * game_placeObject runs for EVERY level object before the engine switch (OBJECT.C:212): Doom
 * mobjs (6 shorts) go to doom_spawn, the exit and the damage sectors are built here, engine
 * types return 0.  doom_item_func is the pickup collision; doom_sectorDamageTic is the 35 Hz
 * level clock plus the nukage damage. */
#include "util.h"
#include "level.h"
#include "sprite.h"
#include "object.h"
#include "ai.h"
#include "sruins.h"
#include "gamestat.h"
#include "doom.h"

/* compile-time guards (C89): the DoomActor must fit the Object pool slot, and the generated
   tables must have the layout the contract fixes (section 4) */
typedef char doomActorFitsObject_[(sizeof(DoomActor)<sizeof(Object))?1:-1];
typedef char doomExitFitsObject_[(sizeof(DoomExitObject)<=sizeof(Object))?1:-1];
typedef char doomStateIs8_[(sizeof(DoomState)==8)?1:-1];
typedef char doomMobjInfoIs44_[(sizeof(DoomMobjInfo)==44)?1:-1];

unsigned char doomSectorDamage[MAXNMSECTORS];
int doomLevelTime;

/* contract section 9: 8.3 names at the disc root ('+'), bounded by DOOM_NMLEVELS; the last one
   exits to the intro (exit_func).  make_e1m1.py checks these against cd_doom/. */
const char *doomLevelNames[DOOM_NMLEVELS]=
{"+E1M1.LEV",
};

static int doomInitDone;
static int doomPlaceIdx;       /* objects seen by game_placeObject in this placeObjects() */
static short doomSecretWalls[DOOM_MAXSECRETWALLS];   /* OT_DOOM_SECRETWALL of this level */
static int doomNmSecretWalls;

/* doomBlocked (DOOM_VERBS.C): a door wall of an ML_SECRET line is not pressed by monsters */
int doom_wallIsSecret(int w)
{int i;
 for (i=0;i<doomNmSecretWalls;i++)
    if (doomSecretWalls[i]==w)
       return 1;
 return 0;
}

void doom_init(void)
{assert(sizeof(DoomState)==8);
 assert(sizeof(DoomMobjInfo)==44);
 assert(sizeof(DoomActor)<sizeof(Object));
 assert(doomStates[S_POSS_RUN1].tics==4);
 assert(doomStates[S_PISTOL1].flags & DOOM_SF_PSPRITE);
 assert(doomMtToOt[MT_PLAYER]==OT_PLAYER);
 assert(doomOtToMt[OT_PLAYER]==MT_PLAYER);
 assert(doomOtToMt[OT_DOOM_EXIT]==-1 && doomOtToMt[OT_DOOM_DAMAGE]==-1);
 assert(doomOtToMt[OT_DOOM_SECRETWALL]==-1);
 assert(doomMobjInfo[MT_TROOPSHOT].speed==10);
 /* the pad as Mimas lays it out (dg_saturn.cxx pad_map: A fire, B use, C run held, L/R strafe)
    so the two are played with the same hands.  controllerConfig maps an action slot to a button
    (UTIL.C:515-518): the JUMP slot is Doom's run (doomMoveTic), PUSH is use -- B and C swapped,
    the rest as PowerSlave (Z / Y next / previous weapon).  Once per boot: the options menu can
    still change it afterwards. */
 controllerConfig[ACTION_JUMP]=2;
 controllerConfig[ACTION_PUSH]=1;
 doomInitDone=1;
}

char *doom_levelName(int lNm)
{assert(lNm>=0 && lNm<DOOM_NMLEVELS);
 return (char *)doomLevelNames[lNm];
}

/* first object of a level: Doom's P_SetupLevel state (M_ClearRandom, sound targets, damage
   sectors, level clock).  placeObjects calls the hook exactly once per level object, in
   order, so the count wraps at level_nmObjects. */
static void doomLevelStart(void)
{int s;
 M_ClearRandom();
 doom_actorLevelInit();
 for (s=0;s<MAXNMSECTORS;s++)
    doomSectorDamage[s]=0;
 doomLevelTime=0;
 doomNmSecretWalls=0;
}

/* OBJECT.C:212 (CFG_PLACE): called for every level object BEFORE the engine switch.  Return 0
   for an engine type (the switch runs), 1 once the params have been consumed here. */
int game_placeObject(int ot)
{int mt;
 assert(ot>=0 && ot<OT_NMTYPES);
 if (!doomInitDone)
    doom_init();
 if (doomPlaceIdx==0)
    doomLevelStart();
 doomPlaceIdx++;
 if (doomPlaceIdx>=level_nmObjects)
    doomPlaceIdx=0;

 if (ot==OT_PLAYER)
    {/* P_SpawnPlayer -> P_SpawnMobj draws lastlook: the player is THINGS entry 0 of E1M1 and
	things2objects puts it first, so this draw lands where Doom's does */
     (void)P_Random();
     return 0;                                  /* constructPlayer, OBJECT.C:200-206 */
    }
 switch (ot)
    {case OT_DOOM_EXIT:
     case OT_DOOM_SECRETEXIT:
	{int channel=suckShort();
	 DoomExitObject *o=(DoomExitObject *)getFreeObject(exit_func,ot,CLASS_SECTOR);
	 if (o)
	    {moveObject((Object *)o,objectIdleList);   /* SIGNAL_SWITCH reaches both lists */
	     o->channel=(short)channel;
	    }
	 return 1;
	}
     case OT_DOOM_DAMAGE:
	{int sectorNm=suckShort();
	 int hp=suckShort();
	 assert(sectorNm>=0 && sectorNm<level_nmSectors);
	 assert(hp>=0 && hp<256);
	 doomSectorDamage[sectorNm]=(unsigned char)hp;
	 return 1;
	}
     case OT_DOOM_SECRETWALL:
	{int w=suckShort();
	 assert(w>=0 && w<level_nmWalls);
	 assert(level_wall[w].flags & WALLFLAG_DOORWALL);
	 assert(doomNmSecretWalls<DOOM_MAXSECRETWALLS);
	 doomSecretWalls[doomNmSecretWalls++]=(short)w;
	 return 1;
	}
     case OT_DOOM_LIGHT:
	return 0;                                /* not before J3: params undefined (engine asserts) */
     default:
	break;
    }
 mt=doomOtToMt[ot];
 if (mt<0)
    return 0;                                   /* engine type */
 {int sector,x,y,z,angle,flags;
  MthXyz pos;
  DoomActor *a;
  sector=suckShort();
  x=suckShort();
  y=suckShort();                                /* floorLevel: shiftSprites adds the radius */
  z=suckShort();
  angle=suckShort();
  flags=suckShort();
  assert(sector>=0 && sector<level_nmSectors);
  pos.x=F(x);
  pos.y=F(y);
  pos.z=F(z);
  a=doom_spawn(mt,sector,&pos,angle*5760,flags);   /* OBJECT.C:194: 360/4096 degree steps */
  /* P_SpawnMapThing: `if (mobj->tics > 0) mobj->tics = 1 + (P_Random () % mobj->tics)` --
     placed things start out of phase (after the lastlook draw of doom_spawn, as Doom) */
  if (a && a->type!=OT_DEAD && a->tics>0)
     a->tics=(short)(1+P_Random()%a->tics);
 }
 return 1;
}

/* Pickup reach, Doom's own test (SPEC_PLAYER 4.3): P_TryMove -> PIT_CheckThing (p_map.c) -- only
   a MOVING player touches, inside the box of thing radius + player radius, whatever the heights --
   then P_TouchSpecialThing (p_inter.c): delta = special->z - toucher->z outside -8 .. 56 (player
   height) is out of reach.  The engine's spheres could not say it: the camera ball is centred on
   the eye (41 above the feet), a pickup's is 8 u at the floor, they never met -- nothing could be
   picked up.  game_actor_func runs the animation tics (no collideSprite for an IMMOBILE sprite);
   doom_playerGetObject(mt, dropped) does the effects; 1 => delayKill. */
void doom_item_func(Object *_this,int message,int param1,int param2)
{DoomActor *this=(DoomActor *)_this;
 Sprite *s;
 Fixed32 reach,delta;
 game_actor_func(_this,message,param1,param2);
 if (message!=SIGNAL_MOVE || this->type==OT_DEAD || !camera || currentState.health<=0)
    return;
 if (!camera->vel.x && !camera->vel.z)
    return;
 s=this->sprite;
 reach=F(doomMobjInfo[this->mt].radius+GP_PLAYER_RADIUS);
 if (abs(s->pos.x-camera->pos.x)>=reach || abs(s->pos.z-camera->pos.z)>=reach)
    return;
 delta=(s->pos.y-s->radius)-(camera->pos.y-F(GP_PLAYER_RADIUS+GP_PLAYER_EYE_HOVER));
 if (delta>F(56) || delta<F(-8))
    return;
 if (doom_playerGetObject(this->mt,(this->mflags & DF_DROPPED)?1:0))
    delayKill(_this);
}

/* Exit switch (contract section 6 / 9): SIGNAL_SWITCH(channel) from the OT_SW1 press.
   next < DOOM_NMLEVELS => runLevel returns 200+next and main() loads doomLevelNames[next];
   last level => playerHitTeleport(2-200): action 2 = quit -> intro.  Never -1 (camel branch). */
void exit_func(Object *_this,int message,int param1,int param2)
{DoomExitObject *this=(DoomExitObject *)_this;
 (void)param2;
 if (message!=SIGNAL_SWITCH || param1!=this->channel)
    return;
 {int next=currentState.currentLevel+1;
  if (next<DOOM_NMLEVELS)
     playerHitTeleport(next);
  else
     playerHitTeleport(2-200);
 }
}

/* P_PlayerInSpecialSector (p_spec.c, special 7 and kin): once per tic from doom_playerTic;
   THE level clock (Doom leveltime) ticks here.  hp every 32 tics while the player stands on
   the floor of a damage sector (Doom: mo->z == floorheight). */
void doom_sectorDamageTic(void)
{doomLevelTime++;
 if (!camera)
    return;
 assert(camera->s>=0 && camera->s<level_nmSectors);
 if (!doomSectorDamage[camera->s])
    return;
 if (camera->floorSector==-1)
    return;                                     /* not on the floor */
 if (!(doomLevelTime & 0x1f))
    doom_playerDamage(doomSectorDamage[camera->s],NULL);
}

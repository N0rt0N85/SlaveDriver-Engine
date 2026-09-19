/* DOOM_GAME.C -- placement hook, level names, exit switch, damage sectors, item collision,
 * line teleporters, doors, moving floors and the boss death.
 * SPEC_RUNTIME sections 6 and 9; contract sections 1, 6, 9.
 *
 * game_placeObject places EVERY level object (OBJECT.C:212; the engine's switch is compiled out
 * of the Doom build): Doom mobjs (6 shorts) go to doom_spawn, the exit, the damage sectors, the
 * teleporters, the doors and the moving floors are built here, and the few engine objects doom2ps
 * emits (switches, W triggers, lifts) are built as the engine switch built them.  doom_item_func
 * is the pickup collision; doom_sectorDamageTic is the 35 Hz level clock plus the nukage damage. */
#include <string.h>
#include "util.h"
#include "level.h"
#include "sprite.h"
#include "object.h"
#include "ai.h"
#include "aicommon.h"
#include "sruins.h"
#include "sound.h"
#include "gamestat.h"
#include "doom.h"
#include "mplayer.h"

/* Objects that exist only in this file (doom2ps doom_specials.OT_DOOM_TELEPORT / OT_DOOM_FLOOR,
   params big-endian shorts in that order):
     OT_DOOM_TELEPORT  trigger leaf, arrival leaf, x, z, angle (Doom's, as the player start's:
                       the sprite convention, turned into a camera yaw here), arrival fog x, z,
                       flags (DOOM_TELE_ONCE)
     OT_DOOM_FLOOR     push block, throw (> 0 rises, < 0 lowers), channel, speed (1/8 u per
                       tic), donor face (-1 = keep the flat), damage afterwards (hp per 32 tics,
                       -1 = unchanged)
     OT_DOOM_DOOR      push block, channel (-1 = no tag), open height, press kind, key, tag kind
     OT_DOOM_LIFT      push block, throw (< 0), channel, speed (1/8 u per tic), wait (tics)
     OT_DOOM_WLINE     x1, z1, x2, z2 (the linedef), channel, flags (DOOM_WLINE_ONCE)
   and OT_DOOM_DAMAGE's hp carries DOOM_DAMAGE_EXIT for special 11 (E1M8's last room). */
#define OT_DOOM_TELEPORT 181
#define OT_DOOM_FLOOR    182
#define OT_DOOM_DOOR     183
#define OT_DOOM_LIFT     184
#define OT_DOOM_WLINE    185
#define DOOM_TELE_ONCE   1
#define DOOM_WLINE_ONCE  1
#define DOOM_WLINE_GUN   2       /* P_ShootSpecialLine: fired by a BULLET, never by crossing */
#define DOOM_DAMAGE_EXIT 0x100

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short sectorNm,destSector;
 short x,z,angle,fogX,fogZ,flags;
} DoomTeleportObject;

typedef struct
{short type,class;                      /* PushBlockObject prefix (OBJECT.H:53-61): pbObject_*, */
 struct __object *next,*prev;           /* pushBlockMakeSound and registerPBObject read it      */
 messHandler func;
 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;
 short throw,channel,speed,donorFace,damage,pad;
} DoomFloorObject;

typedef struct
{short type,class;                      /* PushBlockObject prefix, as DoomFloorObject          */
 struct __object *next,*prev;
 messHandler func;
 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;
 short channel,height;                  /* tag (-1 = none); open height above the file's state */
 unsigned char manual,key,tagged,kind;  /* DOOM_DOOR_* of a press / of the tag, the one running */
} DoomDoorObject;

typedef struct
{short type,class;                      /* PushBlockObject prefix, as DoomFloorObject          */
 struct __object *next,*prev;
 messHandler func;
 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;
 short throw,channel,speed,wait;
} DoomLiftObject;

typedef struct __doomWLine
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __doomWLine *wnext;             /* the level's walk-over lines (doomWLines)            */
 short x1,z1,x2,z2,channel,flags;
} DoomWLineObject;

/* compile-time guards (C89): the DoomActor must fit the Object pool slot, and the generated
   tables must have the layout the contract fixes (section 4) */
typedef char doomActorFitsObject_[(sizeof(DoomActor)<sizeof(Object))?1:-1];
typedef char doomExitFitsObject_[(sizeof(DoomExitObject)<=sizeof(Object))?1:-1];
typedef char doomTeleportFitsObject_[(sizeof(DoomTeleportObject)<=sizeof(Object))?1:-1];
typedef char doomFloorFitsObject_[(sizeof(DoomFloorObject)<=sizeof(Object))?1:-1];
typedef char doomDoorFitsObject_[(sizeof(DoomDoorObject)<=sizeof(Object))?1:-1];
typedef char doomLiftFitsObject_[(sizeof(DoomLiftObject)<=sizeof(Object))?1:-1];
typedef char doomWLineFitsObject_[(sizeof(DoomWLineObject)<=sizeof(Object))?1:-1];
typedef char doomStateIs8_[(sizeof(DoomState)==8)?1:-1];
typedef char doomMobjInfoIs44_[(sizeof(DoomMobjInfo)==44)?1:-1];

unsigned char doomSectorDamage[MAXNMSECTORS];
int doomLevelTime;
static unsigned char doomSectorExit[(MAXNMSECTORS+7)>>3];   /* special 11: exit at health <= 10 */
static unsigned char doomDoorSector[(MAXNMSECTORS+7)>>3];   /* a door's own sectors: doom_nearDoor */
static int doomExiting;                                    /* the level is over: exit asked once */

/* Floor height of sector s, read from its flat as bumpFloor does (the vertices move with a lift) */
static int doomFloorY(int s)
{int f;
 for (f=level_sector[s].firstWall;f<=level_sector[s].lastWall;f++)
    if (level_wall[f].normal[1]>0)
       return level_vertex[level_wall[f].v[0]].y;
 return -0x7fff;
}

/* The sector owning wall w: sectors hold contiguous, ascending wall ranges */
static int doomWallSector(int w)
{int s,lo=0,hi=level_nmSectors-1;
 while (lo<hi)
    {s=(lo+hi+1)>>1;
     if (level_sector[s].firstWall<=w) lo=s; else hi=s-1;
    }
 assert(level_sector[lo].firstWall<=w && w<=level_sector[lo].lastWall);
 return lo;
}

/* A moving lift's portals (AI.C:4670 CFG_LIFT_MOVED, every frame it moves).  doom2ps sets, for
   the level as loaded, SHORTOPENING (opening < 56, or a step up of more than 24: player and
   monsters carry BSHORT, missiles do not) and CLIFFBNDRY (a drop of more than 24: monsters only,
   P_TryMove's dropoff); nothing in the engine touches them when an elevator moves --
   setDoorBlockBits is doors only -- so a lift that came down left an invisible wall (E1M1: the
   shotgun room behind sector 59, 8 u open while it is up).  Re-read here from the vertices. */
static void doomPortalBits(int w,int s)
{int bot,own,next;
 if (level_wall[w].nextSector==-1 || level_wall[w].normal[1]!=0)
    return;
 own=doomFloorY(s);
 next=doomFloorY(level_wall[w].nextSector);
 bot=level_vertex[level_wall[w].v[2]].y;
 if (level_vertex[level_wall[w].v[1]].y-bot<CFG_DOOR_FIT || bot-own>GP_PLAYER_STEP)
    level_wall[w].flags|=WALLFLAG_SHORTOPENING;
 else
    level_wall[w].flags&=~WALLFLAG_SHORTOPENING;
 if (own-next>GP_PLAYER_STEP)
    level_wall[w].flags|=WALLFLAG_CLIFFBNDRY;
 else
    level_wall[w].flags&=~WALLFLAG_CLIFFBNDRY;
}

/* GCC14: not only the walls that move -- a portal whose edge stays put (the lift's side facing a
   higher floor: E1M8's lift to the stimpacks) changes too when the floor beside it moves, from
   both sides.  Those kept the flags of the level as loaded: an invisible wall at the top of the
   lift (seen on console 09-19; 1 to 34 such portals on each E1 level). */
void doom_pbBlockBits(int pb)
{short sec[16];
 int i,j,w,s,n=0,f;
 for (i=level_pushBlock[pb].startWall;i<=level_pushBlock[pb].endWall;i++)
    {w=level_PBWall[i];
     s=doomWallSector(w);
     for (j=0;j<n && sec[j]!=s;j++)
	;
     if (j==n && n<16)
	sec[n++]=(short)s;
    }
 for (j=0;j<n;j++)
    for (w=level_sector[sec[j]].firstWall;w<=level_sector[sec[j]].lastWall;w++)
       {doomPortalBits(w,sec[j]);
	s=level_wall[w].nextSector;
	if (s!=-1 && level_wall[w].normal[1]==0)
	   for (f=level_sector[s].firstWall;f<=level_sector[s].lastWall;f++)
	      if (level_wall[f].nextSector==sec[j])
		 doomPortalBits(f,s);
       }
}

/* A floor that is shut when the level loads (floor = ceiling in the WAD: E1M8's tag 666 wall)
   leaves only a 1 u slit, which the renderer would traverse into everything behind it
   (WALLS.C:2138): doom2ps marks those portals BLOCKSSIGHT, the flag of PowerSlave's explodable
   walls (AICOMMON.C:486), and they see again as soon as the floor moves. */
static void doomPbOpenSight(int pb)
{int i,w;
 for (i=level_pushBlock[pb].startWall;i<=level_pushBlock[pb].endWall;i++)
    {w=level_PBWall[i];
     if (level_wall[w].nextSector!=-1)
	level_wall[w].flags&=~WALLFLAG_BLOCKSSIGHT;
    }
}

/* --- line teleporters (EV_Teleport, p_telept.c) ----------------------------------------------- */

/* MT_TFOG standing on `feet` (a dynamic spawn takes pos as its sphere centre: feet + radius,
   radius = height/2 in doom_spawn) and sfx_telept from it -- unpositioned if no slot was left */
static void doomTeleFog(int sector,MthXyz *feet)
{MthXyz pos=*feet;
 DoomActor *f;
 pos.y+=F(doomMobjInfo[MT_TFOG].height/2);
 f=doom_spawn(MT_TFOG,sector,&pos,0,0);
 doom_sound((f && f->type!=OT_DEAD)?f->sprite:NULL,sfx_telept);
}

/* One object per leaf on the BACK side of a teleport line: Doom teleports only a thing that
   crosses the line from its front (`if (side == 1) return`), which lands it in the back sector,
   and the engine tells a sector object when the camera enters its leaf (SIGNAL_ENTER,
   SPRITE.C:731).  moveSpriteTo sends nothing, so the arrival never fires a teleporter by itself:
   walking off the arrival pad and back on does, as in Doom.  Monsters do not teleport (the
   engine signals the camera only).  Not done: the 18-tic freeze of the player (reactiontime) --
   the momentum is zeroed, the controls stay live. */
static void doomTeleport_func(Object *_this,int message,int param1,int param2)
{DoomTeleportObject *this=(DoomTeleportObject *)_this;
 MthXyz feet,pos;
 int floorY;
 (void)param1; (void)param2;
 if (message!=SIGNAL_ENTER || !camera || currentState.health<=0)
    return;
 feet=camera->pos;
 feet.y-=F(GP_PLAYER_RADIUS+GP_PLAYER_EYE_HOVER);
 doomTeleFog(camera->s,&feet);
 floorY=doomFloorY(this->destSector);
 pos.x=F(this->x);
 pos.z=F(this->z);
 pos.y=F(floorY+GP_PLAYER_RADIUS+GP_PLAYER_EYE_HOVER);   /* standing: the eye hovers (SPRITE.C:644) */
 moveSpriteTo(camera,this->destSector,&pos);
 camera->vel.x=0;
 camera->vel.y=0;
 camera->vel.z=0;
 camera->floorSector=this->destSector;    /* on that floor now: collideSprite keeps it (SPRITE.C:649) */
 doomPlayer.momx=0;
 doomPlayer.momz=0;
 /* the player start's conversion: short * 5760 (OBJECT.C:194) is a sprite angle, the camera
    yaw is 90 degrees less (AI.C:51-52) */
 playerAngle.yaw=normalizeAngle(normalizeAngle(((int)this->angle)*5760)-F(90));
 camera->angle=playerAngle.yaw;
 feet.x=F(this->fogX);
 feet.z=F(this->fogZ);
 feet.y=F(floorY);
 doomTeleFog(findSectorContaining(&feet,this->destSector),&feet);
 if (this->flags & DOOM_TELE_ONCE)
    level_sector[this->sectorNm].object=NULL;
}

/* --- moving floors (EV_DoFloor, EV_DoPlat raise*AndChange; p_floor.c, p_plats.c) ------------- */

enum {DOOM_FLOOR_WAIT,DOOM_FLOOR_MOVE,DOOM_FLOOR_DONE};

/* a Doom sfx from a push block's centre (pushBlockMakeSound, OBJECT.C:547); absent = silent */
static void doomPbSound(Object *o,int sfx)
{int idx=doom_sfxIndex(sfx);
 if (idx>=0)
    pushBlockMakeSound((PushBlockObject *)o,idx);
}

/* The ...AndChange variants, at the start as Doom does them: the floor takes the flat of the
   trigger line's front sector (a face of it, donorFace, copied tile for tile -- loadLevel has
   already added the tileBase to both) and its damage special becomes `damage`. */
static void doomFloorChange(DoomFloorObject *this)
{int i,w,f;
 for (i=level_pushBlock[this->pbNum].startWall;i<=level_pushBlock[this->pbNum].endWall;i++)
    {w=level_PBWall[i];
     if (level_wall[w].normal[1]<=0)
	continue;                                /* the floors of the push block: its own sector's */
     if (this->donorFace>=0 && level_wall[w].firstFace>=0)
	for (f=level_wall[w].firstFace;f<=level_wall[w].lastFace;f++)
	   level_face[f].tile=level_face[this->donorFace].tile;
     if (this->damage>=0)
	doomSectorDamage[doomWallSector(w)]=(unsigned char)this->damage;
    }
}

/* A RISING floor (throw > 0): doom2ps emits it at its DESTINATION (a lift at the top, whose
   geometry the E1M1 lifts proved) and this object lowers it by `throw` when the level is placed:
   the level starts as the WAD draws it.  A LOWERING floor (throw < 0) is emitted as the WAD draws
   it and goes down by -throw (the engine elevator it used to be went at 5 u per tic with
   PowerSlave's sound).  SIGNAL_SWITCH(channel) -- a switch, a walk-over line, or the boss death
   on tag 666 -- moves it once at `speed`/8 u per tic (FLOORSPEED 1, the plats' PLATSPEED/2,
   turbo 4), stnmov every 8 tics and pstop at the end (T_MoveFloor, T_PlatRaise).  It does not
   stop for what stands in its way: the engine carries the sprites with it. */
static void doomFloor_func(Object *_this,int message,int param1,int param2)
{DoomFloorObject *this=(DoomFloorObject *)_this;
 Fixed32 step;
 (void)param2;
 switch (message)
    {case SIGNAL_SWITCH:
	if (this->state!=DOOM_FLOOR_WAIT || param1!=this->channel)
	   break;
	this->state=DOOM_FLOOR_MOVE;
	if (this->throw>0)
	   doomFloorChange(this);                /* the raise ...AndChange: at the start */
	doomPbOpenSight(this->pbNum);
	doomPbSound(_this,sfx_stnmov);
	delay_moveObject((Object *)this,objectRunList);
	break;
     case SIGNAL_MOVE:
	if (this->state!=DOOM_FLOOR_MOVE)
	   break;
	pushBlockAdjustSound((PushBlockObject *)this);
	step=((Fixed32)this->speed)<<13;
	pbObject_move((PushBlockObject *)this,(this->throw>0)?step:-step);
	if ((this->throw>0)?(this->offset>=0):(this->offset<=F(this->throw)))
	   {pbObject_moveTo((PushBlockObject *)this,(this->throw>0)?0:this->throw);
	    this->state=DOOM_FLOOR_DONE;
	    stopAllSound((int)this);
	    doomPbSound(_this,sfx_pstop);
	    delay_moveObject((Object *)this,objectIdleList);
	   }
	else if (!(doomLevelTime&7))
	   doomPbSound(_this,sfx_stnmov);
	doom_pbBlockBits(this->pbNum);
	break;
    }
}

/* --- lifts (EV_DoPlat downWaitUpStay / blazeDWUS, T_PlatRaise; p_plats.c) --------------------- */

enum {DOOM_LIFT_IDLE,DOOM_LIFT_DOWN,DOOM_LIFT_WAIT,DOOM_LIFT_UP};

/* The lift doom2ps emits at the top, as the WAD draws it.  SIGNAL_SWITCH(channel) starts it only
   when it is idle -- EV_DoPlat skips a sector that has specialdata -- then down at `speed`/8 u
   per tic (PLATSPEED*4, blaze *8), `wait` tics at the bottom (PLATWAIT, 3 s), back up, and it is
   idle again.  The engine elevator it replaces (elevator_func, AI.C:4598) went at 5 u per frame,
   waited 150 frames, and left again at ONCE on any signal while it waited: stepping off it at
   the bottom sent it up empty (E1M2's lift 180, tag 10, console 2026-09-18).  It does not
   reverse on a thing under it going up: the engine carries the sprites with it. */
static void doomLift_func(Object *_this,int message,int param1,int param2)
{DoomLiftObject *this=(DoomLiftObject *)_this;
 Fixed32 step;
 (void)param2;
 switch (message)
    {case SIGNAL_SWITCH:
	if (this->state!=DOOM_LIFT_IDLE || param1!=this->channel)
	   break;
	this->state=DOOM_LIFT_DOWN;
	doomPbSound(_this,sfx_pstart);
	delay_moveObject((Object *)this,objectRunList);
	break;
     case SIGNAL_MOVE:
	step=((Fixed32)this->speed)<<13;
	switch (this->state)
	   {case DOOM_LIFT_DOWN:
	       pushBlockAdjustSound((PushBlockObject *)this);
	       pbObject_move((PushBlockObject *)this,-step);
	       if (this->offset<=F(this->throw))
		  {pbObject_moveTo((PushBlockObject *)this,this->throw);
		   this->state=DOOM_LIFT_WAIT;
		   this->waitCounter=this->wait;
		   stopAllSound((int)this);
		   doomPbSound(_this,sfx_pstop);
		  }
	       break;
	    case DOOM_LIFT_WAIT:
	       if (--this->waitCounter<=0)
		  {this->state=DOOM_LIFT_UP;
		   doomPbSound(_this,sfx_pstart);
		  }
	       break;
	    case DOOM_LIFT_UP:
	       pushBlockAdjustSound((PushBlockObject *)this);
	       pbObject_move((PushBlockObject *)this,step);
	       if (this->offset>=0)
		  {pbObject_moveTo((PushBlockObject *)this,0);
		   this->state=DOOM_LIFT_IDLE;
		   stopAllSound((int)this);
		   doomPbSound(_this,sfx_pstop);
		   delay_moveObject((Object *)this,objectIdleList);
		   /* the switches of this channel come back up, as the doors do it (a repeatable
		      one can be pressed again) */
		   signalAllObjects(SIGNAL_SWITCHRESET,this->channel,0);
		  }
	       break;
	   }
	doom_pbBlockBits(this->pbNum);
	break;
    }
}

/* --- walk-over lines (P_CrossSpecialLine, p_spec.c) ------------------------------------------- */

/* A W line fires when a player's centre crosses it, from either side, as P_TryMove calls
   P_CrossSpecialLine: SIGNAL_SWITCH(channel), once (W1: `line->special = 0`) or at every
   crossing (WR -- the receivers ignore it while they are on their way).  The engine's trigger,
   OT_SECTORSWITCH, fires on ENTERING A LEAF: the whole leaf on either side of the line, and
   again from a leaf the player stands in when its channel is reset -- E1M2's lift was called
   before the player reached it, and sent back up empty as the player stepped off.  Monsters do not cross them
   (Doom lets them on 4, 10 and 88). */
#define DOOM_SHOOTLINE_TOL  (8*16)           /* how far off the line a hit may land, 1/16 u */
static DoomWLineObject *doomWLines;          /* this level's, in placing order */
static Fixed32 doomWPrevX[MPMAX],doomWPrevZ[MPMAX];
static char doomWPrevOk[MPMAX];              /* 0: no previous position (level start, death) */

static void doomWLine_func(Object *_this,int message,int param1,int param2)
{(void)_this;(void)message;(void)param1;(void)param2;   /* idle: doom_wlineTic tests it */
}

/* segment (px,pz)-(cx,cz) against the line, in 1/16 u: the centre changes side
   (P_PointOnLineSide) where the line is, within a player radius past its ends (the thing's box
   touches it, PIT_CheckLine) */
static int doomWLineCrossed(DoomWLineObject *w,int px,int pz,int cx,int cz)
{long long ex=w->x2-w->x1,ez=w->z2-w->z1,ax=w->x1*16,az=w->z1*16;
 long long sp=ex*(pz-az)-ez*(px-ax),sc=ex*(cz-az)-ez*(cx-ax);
 long long l2=ex*ex+ez*ez,t,r2;
 if ((sp>0)==(sc>0) || l2==0)
    return 0;
 /* where along it, in u: projection of the centre x |AB|, against a radius x |AB| past an end */
 t=ex*((cx>>4)-w->x1)+ez*((cz>>4)-w->z1);
 r2=(long long)GP_PLAYER_RADIUS*GP_PLAYER_RADIUS*l2;
 if (t<0)
    return t*t<=r2;
 if (t>l2)
    return (t-l2)*(t-l2)<=r2;
 return 1;
}

/* P_ShootSpecialLine (p_spec.c): a line a BULLET opens -- Doom's "GR open door" (46), the only
   one of its kind in episode 1 (E1M2's chainsaw closet).  doom_playerLineAttack calls this with
   the point its hitscan struck a wall at; the line is the Doom linedef the wall was built from,
   so the point sits on it to within rounding.  Distances in 1/16 u, like doomWLineCrossed. */
void doom_shootLine(Fixed32 hx,Fixed32 hz)
{DoomWLineObject *w;
 int x=hx>>12,z=hz>>12;
 for (w=doomWLines;w;w=w->wnext)
    {long long ex,ez,ax,az,s,l2,t;
     if (w->channel==-1 || !(w->flags & DOOM_WLINE_GUN))
	continue;
     ex=w->x2-w->x1; ez=w->z2-w->z1; ax=w->x1*16; az=w->z1*16;
     l2=ex*ex+ez*ez;
     if (!l2)
	continue;
     s=ex*(z-az)-ez*(x-ax);                  /* perpendicular distance x |AB| */
     if (s*s>(long long)DOOM_SHOOTLINE_TOL*DOOM_SHOOTLINE_TOL*l2)
	continue;
     t=ex*((x>>4)-w->x1)+ez*((z>>4)-w->z1);  /* where along it, x |AB| */
     if (t<0 || t>l2)
	continue;
     {int channel=w->channel;
      if (w->flags & DOOM_WLINE_ONCE)
	 w->channel=-1;
      signalAllObjects(SIGNAL_SWITCH,channel,0);
     }
    }
}

/* doom_playerTic, per player (mpCur): the move since this player's last tic */
void doom_wlineTic(void)
{DoomWLineObject *w;
 int k=mpCur,px,pz,cx,cz;
 if (currentState.health<=0)
    {doomWPrevOk[k]=0;
     return;
    }
 cx=camera->pos.x>>12;
 cz=camera->pos.z>>12;
 px=doomWPrevX[k]>>12;
 pz=doomWPrevZ[k]>>12;
 /* a teleport, a respawn: a jump, not a walk (P_TeleportMove crosses nothing) */
 if (doomWPrevOk[k] && abs(cx-px)+abs(cz-pz)<64*16)
    for (w=doomWLines;w;w=w->wnext)
       if (w->channel!=-1 && !(w->flags & DOOM_WLINE_GUN) &&
	   doomWLineCrossed(w,px,pz,cx,cz))
	  {int channel=w->channel;
	   if (w->flags & DOOM_WLINE_ONCE)
	      w->channel=-1;
	   signalAllObjects(SIGNAL_SWITCH,channel,0);
	  }
 doomWPrevX[k]=camera->pos.x;
 doomWPrevZ[k]=camera->pos.z;
 doomWPrevOk[k]=1;
}

/* --- doors (p_doors.c) ------------------------------------------------------------------------ */

/* vldoor_e kinds, as doom2ps doom_specials writes them (PORTE_*) */
enum {DOOM_DOOR_NONE,DOOM_DOOR_NORMAL,DOOM_DOOR_OPEN,DOOM_DOOR_BLAZERAISE,DOOM_DOOR_BLAZEOPEN};
enum {DOOM_DOOR_CLOSED,DOOM_DOOR_UP,DOOM_DOOR_WAIT,DOOM_DOOR_DOWN,DOOM_DOOR_OPENED};
#define DOOM_VDOORSPEED 2                 /* u per tic; blaze x4 */
#define DOOM_VDOORWAIT  150               /* tics at the top */

/* d_englsh.h PD_BLUEK, PD_YELLOWK, PD_REDK: key 1, 2, 3 */
static const char *doomDoorKeyMsg[3]=
{"You need a blue key to open this door",
 "You need a yellow key to open this door",
 "You need a red key to open this door"};

static int doomDoorBlaze(int kind)
{return kind==DOOM_DOOR_BLAZERAISE || kind==DOOM_DOOR_BLAZEOPEN;
}

/* a door that is on its way, or waiting at the top: Doom's sector->specialdata */
static int doomDoorBusy(DoomDoorObject *this)
{return this->state==DOOM_DOOR_UP || this->state==DOOM_DOOR_WAIT || this->state==DOOM_DOOR_DOWN;
}

static void doomDoorStart(DoomDoorObject *this,int kind)
{this->kind=(unsigned char)kind;
 if (this->offset<F(this->height))       /* EV_DoDoor: no sound for a door already up there */
    doomPbSound((Object *)this,doomDoorBlaze(kind)?sfx_bdopn:sfx_doropn);
 this->state=DOOM_DOOR_UP;
 delay_moveObject((Object *)this,objectRunList);
}

/* A door is ONE object for what Doom does with two entry points: EV_VerticalDoor (a press on a
   manual line: its kind, its key) and EV_DoDoor (the tag: SIGNAL_SWITCH(channel) from a switch
   or a walk-over line, its kind).  The engine door (door_func, AI.C:4313) only knew one -- a
   channel switched the press off -- and always closed again after 128 tics: E1M2's door to the
   outside, 1500 u from its switch, was shut before the player could reach it.
   The file holds it closed (a 1 u slit), `height` above that is Doom's top (lowest surrounding
   ceiling - 4).  T_VerticalDoor: normal and blazeRaise wait 150 tics and close, open and
   blazeOpen stay; a press on a raise door that is moving reverses it (a monster never closes
   one); the tag does nothing to a door on its way (sec->specialdata); the ceiling coming down on
   the player sends it back up.  A monster (doomBlocked passes no hit point) only opens a manual
   door of kind normal without a key: P_UseSpecialLine lets it use line special 1, nothing else. */
static void doomDoor_func(Object *_this,int message,int param1,int param2)
{DoomDoorObject *this=(DoomDoorObject *)_this;
 int speed;
 (void)param2;
 switch (message)
    {case SIGNAL_PRESS:
	{int byPlayer=(param1!=0);              /* push(): the hit point; doomBlocked: 0 */
	 if (this->manual==DOOM_DOOR_NONE)
	    break;
	 if (!byPlayer && (this->manual!=DOOM_DOOR_NORMAL || this->key))
	    break;
	 if (this->key &&
	     !(doomPlayer.keys & ((1<<(this->key-1))|(1<<(this->key+2)))))
	    {doom_setMessage(doomDoorKeyMsg[this->key-1]);
	     doom_playerSound(sfx_oof);
	     break;
	    }
	 if (doomDoorBusy(this))
	    {if (this->manual!=DOOM_DOOR_NORMAL && this->manual!=DOOM_DOOR_BLAZERAISE)
		break;                           /* the open kinds cleared their line */
	     if (this->state==DOOM_DOOR_DOWN)
		this->state=DOOM_DOOR_UP;          /* go back up */
	     else if (byPlayer)
		this->state=DOOM_DOOR_DOWN;        /* start going down immediately */
	     break;
	    }
	 doomDoorStart(this,this->manual);
	 if (this->manual==DOOM_DOOR_OPEN || this->manual==DOOM_DOOR_BLAZEOPEN)
	    this->manual=DOOM_DOOR_NONE;           /* EV_VerticalDoor: line->special = 0 */
	 break;
	}
     case SIGNAL_SWITCH:
	if (param1!=this->channel || this->tagged==DOOM_DOOR_NONE || doomDoorBusy(this))
	   break;
	doomDoorStart(this,this->tagged);
	break;
     case SIGNAL_CEILCONTACT:
	if (this->state==DOOM_DOOR_DOWN)
	   {this->state=DOOM_DOOR_UP;
	    doomPbSound(_this,doomDoorBlaze(this->kind)?sfx_bdopn:sfx_doropn);
	   }
	break;
     case SIGNAL_MOVE:
	speed=doomDoorBlaze(this->kind)?4*DOOM_VDOORSPEED:DOOM_VDOORSPEED;
	switch (this->state)
	   {case DOOM_DOOR_UP:
	       pushBlockAdjustSound((PushBlockObject *)this);
	       pbObject_move((PushBlockObject *)this,F(speed));
	       if (this->offset>=F(this->height))
		  {pbObject_moveTo((PushBlockObject *)this,this->height);
		   if (this->kind==DOOM_DOOR_NORMAL || this->kind==DOOM_DOOR_BLAZERAISE)
		      {this->state=DOOM_DOOR_WAIT;
		       this->waitCounter=DOOM_VDOORWAIT;
		      }
		   else
		      {this->state=DOOM_DOOR_OPENED;
		       delay_moveObject((Object *)this,objectIdleList);
		      }
		  }
	       break;
	    case DOOM_DOOR_WAIT:
	       if (--this->waitCounter<=0)
		  {this->state=DOOM_DOOR_DOWN;
		   doomPbSound(_this,doomDoorBlaze(this->kind)?sfx_bdcls:sfx_dorcls);
		  }
	       break;
	    case DOOM_DOOR_DOWN:
	       pushBlockAdjustSound((PushBlockObject *)this);
	       pbObject_move((PushBlockObject *)this,-F(speed));
	       if (this->offset<=0)
		  {pbObject_moveTo((PushBlockObject *)this,0);
		   this->state=DOOM_DOOR_CLOSED;
		   if (this->kind==DOOM_DOOR_BLAZERAISE)
		      doomPbSound(_this,sfx_bdcls);
		   delay_moveObject((Object *)this,objectIdleList);
		   /* the engine's convention (door_func): the switches of this channel come back
		      up once the door is shut, so a repeatable one can be pressed again */
		   if (this->channel!=-1)
		      signalAllObjects(SIGNAL_SWITCHRESET,this->channel,0);
		  }
	       break;
	   }
	doom_pbBlockBits(this->pbNum);
	break;
    }
}

/* doomBlocked (DOOM_VERBS.C): a monster walked into door wall w.  1 = the door answers (it opens,
   or turns back up), so the monster keeps pushing; 0 = blocked, pick another direction -- a
   locked, secret or switch-only door, as P_Move sees P_UseSpecialLine fail. */
int doom_monsterUseDoor(int w)
{DoomDoorObject *d;
 assert(w>=0 && w<level_nmWalls);
 d=(DoomDoorObject *)level_wall[w].object;
 if (!d || !(level_wall[w].flags & WALLFLAG_DOORWALL) || doom_wallIsSecret(w))
    return 0;
 if (d->type!=OT_DOOM_DOOR)
    {signalObject((Object *)d,SIGNAL_PRESS,0,0);   /* an engine door: as before */
     return 1;
    }
 if (d->manual!=DOOM_DOOR_NORMAL || d->key)
    return 0;
 signalObject((Object *)d,SIGNAL_PRESS,0,0);
 return 1;
}

/* A door's own sectors, those whose ceiling its push block moves */
static void doomMarkDoor(int pb)
{int i,w,s;
 for (i=level_pushBlock[pb].startWall;i<=level_pushBlock[pb].endWall;i++)
    {w=level_PBWall[i];
     if (level_wall[w].normal[1]>=0)
	continue;
     s=doomWallSector(w);
     doomDoorSector[s>>3]|=(unsigned char)(1<<(s&7));
    }
}

/* GCC14: a thing standing still skips its collision (DOOM_ACTOR.C), except here: in a door's
   sector, or across a portal into one less than its radius away -- the ceiling that comes down
   meets it.  Portals come first in a sector's walls (SPRITE.C:412); their normal points into
   the sector. */
int doom_nearDoor(Sprite *s)
{int w,n;
 MthXyz p;
 assert(s);
 if (doomDoorSector[s->s>>3] & (1<<(s->s&7)))
    return 1;
 for (w=level_sector[s->s].firstWall;w<=level_sector[s->s].lastWall;w++)
    {n=level_wall[w].nextSector;
     if (n==-1)
	break;
     if (!(doomDoorSector[n>>3] & (1<<(n&7))))
	continue;
     getVertex(level_wall[w].v[0],&p);
     if (MTH_Mul(s->pos.x-p.x,level_wall[w].normal[0])+
	 MTH_Mul(s->pos.z-p.z,level_wall[w].normal[2])<s->radius)
	return 1;
    }
 return 0;
}

/* contract section 9: 8.3 names at the disc root ('+'), bounded by DOOM_NMLEVELS.  make_e1m1.py
   checks these against cd_doom/.  Where each level leads is Doom's own order (g_game.c
   G_DoCompleted): the secret exit of E1M3 goes to E1M9, E1M9 comes back to E1M4, and -1 -- after
   E1M8 -- ends the episode (exit_func). */
const char *doomLevelNames[DOOM_NMLEVELS]=
{"+E1M1.LEV","+E1M2.LEV","+E1M3.LEV","+E1M4.LEV","+E1M5.LEV","+E1M6.LEV","+E1M7.LEV",
 "+E1M8.LEV","+E1M9.LEV",
};
static const signed char doomLevelNext[DOOM_NMLEVELS]  ={1,2,3,4,5,6,7,-1,3};
static const signed char doomLevelSecret[DOOM_NMLEVELS]={1,2,8,4,5,6,7,-1,3};

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
 assert(doomOtToMt[OT_DOOM_TELEPORT]==-1 && doomOtToMt[OT_DOOM_FLOOR]==-1);
 assert(doomOtToMt[OT_DOOM_LIFT]==-1 && doomOtToMt[OT_DOOM_WLINE]==-1);
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
 for (s=0;s<(int)sizeof(doomSectorExit);s++)
    doomSectorExit[s]=0;
 for (s=0;s<(int)sizeof(doomDoorSector);s++)
    doomDoorSector[s]=0;
 doomLevelTime=0;
 doomNmSecretWalls=0;
 doomExiting=0;
 doomWLines=NULL;
 for (s=0;s<MPMAX;s++)
    doomWPrevOk[s]=0;
}

/* OBJECT.C:212 (CFG_PLACE): called for every level object; returns 1 once its params have been
   consumed here -- always, since the engine's switch is not compiled for Doom. */
int game_placeObject(int ot)
{int mt;
 assert(ot>=0 && ot<OT_NMTYPES);
 if (!doomInitDone)
    doom_init();
 if (doomPlaceIdx==0)
    {doomLevelStart();
     doom_modesLevelReset();                    /* GCC14: DOOM_MODES.C, before the first thing */
    }
 doomPlaceIdx++;
 if (doomPlaceIdx>=level_nmObjects)
    doomPlaceIdx=0;

 if (ot==OT_PLAYER)
    {int i;
     /* P_SpawnPlayer -> P_SpawnMobj draws lastlook: the player is THINGS entry 0 of E1M1 and
	things2objects puts it first, so this draw lands where Doom's does.  constructPlayer ran
	before the loop (OBJECT.C:200-206): its 5 params are skipped, as the engine case did. */
     (void)P_Random();
     for (i=0;i<5;i++)
	suckShort();
     return 1;
    }
 /* Every object of a Doom level is placed here: the engine's switch is compiled out of the Doom
    build (CFG_ENGINE_PLACE, OBJECT.C:213), which drops PowerSlave's constructors and everything
    only they reach from the link.  The engine objects doom2ps emits are built as that switch
    built them (OBJECT.C:214-218, 255-273). */
 switch (ot)
    {case OT_SECTORSWITCH:
	constructSectorSwitch();
	return 1;
     case OT_SW1 ... OT_SW4:
	constructSwitch(ot);
	return 1;
     case OT_NORMALELEVATOR:
     case OT_STUCKDOWNELEVATOR:
     case OT_STARTSDOWNELEVATOR:
	{int pb=suckShort();
	 int lower=suckShort();
	 int upper=suckShort();
	 constructElevator(pb,ot,lower,upper);
	 return 1;
	}
     case OT_NORMALDOOR:
     case OT_STUCKUPDOOR:
	{int pb=suckShort();
	 constructDoor(ot,pb);
	 doomMarkDoor(pb);
	 return 1;
	}
     case OT_DOOM_EXIT:
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
	 assert(hp>=0 && hp<(256|DOOM_DAMAGE_EXIT));
	 doomSectorDamage[sectorNm]=(unsigned char)(hp&0xff);
	 if (hp & DOOM_DAMAGE_EXIT)
	    doomSectorExit[sectorNm>>3]|=(unsigned char)(1<<(sectorNm&7));
	 return 1;
	}
     case OT_DOOM_DOOR:
	{DoomDoorObject *o=(DoomDoorObject *)getFreeObject(doomDoor_func,ot,CLASS_PUSHBLOCK);
	 int pb=suckShort();
	 assert(o);                              /* as every push block constructor (AI.C:4388) */
	 moveObject((Object *)o,objectIdleList);
	 registerPBObject(pb,(Object *)o);
	 o->pbNum=(short)pb;
	 doomMarkDoor(pb);
	 o->state=DOOM_DOOR_CLOSED;
	 o->counter=0;
	 o->waitCounter=0;
	 o->offset=0;
	 o->channel=suckShort();
	 o->height=suckShort();
	 o->manual=(unsigned char)suckShort();
	 o->key=(unsigned char)suckShort();
	 o->tagged=(unsigned char)suckShort();
	 o->kind=DOOM_DOOR_NONE;
	 assert(o->height>0 && o->manual<=DOOM_DOOR_BLAZEOPEN && o->key<=3 &&
		o->tagged<=DOOM_DOOR_BLAZEOPEN && (o->manual || o->tagged));
	 assert((o->channel!=-1)==(o->tagged!=DOOM_DOOR_NONE));
	 return 1;                               /* closed: doom2ps set DOORWALL|SHORTOPENING */
	}
     case OT_DOOM_SECRETWALL:
	{int w=suckShort();
	 assert(w>=0 && w<level_nmWalls);
	 assert(level_wall[w].flags & WALLFLAG_DOORWALL);
	 assert(doomNmSecretWalls<DOOM_MAXSECRETWALLS);
	 doomSecretWalls[doomNmSecretWalls++]=(short)w;
	 return 1;
	}
     case OT_DOOM_TELEPORT:
	{DoomTeleportObject *o;
	 int s,dest,x,z,angle,fogX,fogZ,flags;
	 s=suckShort();
	 dest=suckShort();
	 x=suckShort();
	 z=suckShort();
	 angle=suckShort();
	 fogX=suckShort();
	 fogZ=suckShort();
	 flags=suckShort();
	 assert(s>=0 && s<level_nmSectors && dest>=0 && dest<level_nmSectors);
	 assert(!level_sector[s].object);        /* one sector object per leaf (SPRITE.C:731) */
	 o=(DoomTeleportObject *)getFreeObject(doomTeleport_func,ot,CLASS_SECTOR);
	 if (o)
	    {moveObject((Object *)o,objectIdleList);   /* SIGNAL_ENTER is sent to the object itself */
	     o->sectorNm=(short)s;
	     o->destSector=(short)dest;
	     o->x=(short)x;
	     o->z=(short)z;
	     o->angle=(short)angle;
	     o->fogX=(short)fogX;
	     o->fogZ=(short)fogZ;
	     o->flags=(short)flags;
	     level_sector[s].object=o;
	    }
	 return 1;
	}
     case OT_DOOM_FLOOR:
	{DoomFloorObject *o=(DoomFloorObject *)getFreeObject(doomFloor_func,ot,CLASS_PUSHBLOCK);
	 int pb=suckShort();
	 assert(o);                              /* as every push block constructor (AI.C:4388) */
	 moveObject((Object *)o,objectIdleList);
	 registerPBObject(pb,(Object *)o);
	 o->pbNum=(short)pb;
	 o->state=DOOM_FLOOR_WAIT;
	 o->counter=0;
	 o->waitCounter=0;
	 o->offset=0;
	 o->throw=suckShort();
	 o->channel=suckShort();
	 o->speed=suckShort();
	 o->donorFace=suckShort();
	 o->damage=suckShort();
	 o->pad=0;
	 assert(o->throw!=0 && o->speed>0);
	 /* a rising floor goes down to the WAD's floor now, not at the end of the first frame:
	    nothing stands on it yet (every sprite's floorSector is still -1, SPRITE.C:87), so only
	    the vertices move.  A lowering one already is where the WAD draws it. */
	 if (o->throw>0)
	    {pbObject_moveTo((PushBlockObject *)o,-o->throw);
	     updatePushBlockPositions();
	     doom_pbBlockBits(pb);               /* GCC14: the portals of the floor as it now is */
	    }
	 return 1;
	}
     case OT_DOOM_LIFT:
	{DoomLiftObject *o=(DoomLiftObject *)getFreeObject(doomLift_func,ot,CLASS_PUSHBLOCK);
	 int pb=suckShort();
	 assert(o);                              /* as every push block constructor (AI.C:4388) */
	 moveObject((Object *)o,objectIdleList);
	 registerPBObject(pb,(Object *)o);
	 o->pbNum=(short)pb;
	 o->state=DOOM_LIFT_IDLE;
	 o->counter=0;
	 o->waitCounter=0;
	 o->offset=0;
	 o->throw=suckShort();
	 o->channel=suckShort();
	 o->speed=suckShort();
	 o->wait=suckShort();
	 assert(o->throw<0 && o->speed>0 && o->wait>0 && o->channel!=-1);
	 return 1;
	}
     case OT_DOOM_WLINE:
	{DoomWLineObject *o=(DoomWLineObject *)getFreeObject(doomWLine_func,ot,CLASS_SECTOR);
	 DoomWLineObject **tail;
	 assert(o);
	 moveObject((Object *)o,objectIdleList);
	 o->x1=suckShort();
	 o->z1=suckShort();
	 o->x2=suckShort();
	 o->z2=suckShort();
	 o->channel=suckShort();
	 o->flags=suckShort();
	 assert(o->channel!=-1);
	 o->wnext=NULL;
	 for (tail=&doomWLines;*tail;tail=&(*tail)->wnext)
	    ;
	 *tail=o;                                /* placing order: Doom fires them in line order */
	 return 1;
	}
     case OT_DOLL1 ... OT_DOLL23:
	/* Switch appearances 5..27.  An OT_SW type is ONE wall appearance -- constructSwitch looks
	   for the OFF tile of its sequence on the walls of its sector (AI2.C:606-633) -- and the
	   engine has four (OBJECT.C:217); a Doom level shows more.  Doom has no dolls, so their 23
	   slots carry the rest: same params, same object (doom_specials.OT_SWITCH_TYPES). */
	constructSwitch(ot);
	return 1;
     default:
	break;
    }
 mt=doomOtToMt[ot];
 assert(mt>=0);                                 /* a type doom2ps does not emit (OT_DOOM_LIGHT...) */
 if (mt<0)
    {dPrint("unknown object type %d\n",ot);
     return 1;
    }
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
  if (doom_modePlace(mt,sector,&pos,angle*5760,flags))   /* GCC14: not in this mode, or this skill (DOOM_MODES.C) */
     return 1;
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
 Sprite *s,*c;
 Fixed32 reach,delta;
 int k,prev,got;
 game_actor_func(_this,message,param1,param2);
 if (message!=SIGNAL_MOVE || this->type==OT_DEAD || !camera)
    return;
 s=this->sprite;
 reach=F(doomMobjInfo[this->mt].radius+GP_PLAYER_RADIUS);
 /* every living player in turn, the first to touch takes it -- with ITS state loaded, so the
    ammo goes in the right pocket (MPLAYER.H).  Solo: player 1, as before. */
 for (k=0;k<mpPlayers;k++)
    {c=mpBody[k];               /* the body's own fields first: they cost nothing */
     if (!c->vel.x && !c->vel.z)
	continue;
     if (abs(s->pos.x-c->pos.x)>=reach || abs(s->pos.z-c->pos.z)>=reach)
	continue;
     delta=(s->pos.y-s->radius)-(c->pos.y-F(GP_PLAYER_RADIUS+GP_PLAYER_EYE_HOVER));
     if (delta>F(56) || delta<F(-8))
	continue;
     if (mpPeekInt(k,&currentState.health)<=0 || doom_isMonsterPlayer(k))
	continue;                               /* GCC14: a monster picks nothing up */
     prev=mpBegin(k);
     got=doom_playerGetObject(this->mt,(this->mflags & DF_DROPPED)?1:0);
     mpEnd(prev);
     if (got==1)                                 /* 2: given, and left for the others */
	{if (doomMobjInfo[this->mt].flags & MF_COUNTITEM)
	    mpStat[k].items++;
	 delayKill(_this);
	}
     if (got)
	return;
    }
}

/* Exit switch (contract section 6 / 9): SIGNAL_SWITCH(channel) from the exit switch press.
   next >= 0 => runLevel returns 200+next and main() loads doomLevelNames[next] -- doomLevelNext,
   or doomLevelSecret for an OT_DOOM_SECRETEXIT; -1 => playerHitTeleport(2-200): action 2 = quit
   -> intro.  Never -1 itself (camel branch). */
static void doomExitLevel(int secret)
{int lNm=currentState.currentLevel;
 int next;
 assert(lNm>=0 && lNm<DOOM_NMLEVELS);
 if (mpCompetitive())
    {doom_endRound();                           /* GCC14: a fighting game's exit is the round's */
     return;
    }
 if (doomExiting)
    return;
 doomExiting=1;
 next=secret?doomLevelSecret[lNm]:doomLevelNext[lNm];
 if (next>=0)
    {doom_playerFinishLevel();                  /* the next level keeps the arsenal */
     playerHitTeleport(next);
    }
 else
    playerHitTeleport(2-200);
}

void exit_func(Object *_this,int message,int param1,int param2)
{DoomExitObject *this=(DoomExitObject *)_this;
 (void)param2;
 if (message!=SIGNAL_SWITCH || param1!=this->channel)
    return;
 if (doom_isMonsterPlayer(mpCur))
    return;                                     /* GCC14: a monster's player never ends the level */
 doomExitLevel(this->type==OT_DOOM_SECRETEXIT);
}

/* P_PlayerInSpecialSector (p_spec.c, special 7 and kin): once per tic from doom_playerTic;
   THE level clock (Doom leveltime) ticks here.  hp every 32 tics while the player stands on
   the floor of a damage sector (Doom: mo->z == floorheight).  Special 11 (E1M8's last room, which
   the teleporter behind the Barons leads to) hurts by 20 and ends the level once the health is
   10 or less -- E1M8 ends the episode (doomLevelNext -1). */
void doom_sectorDamageTic(void)
{int s;
 if (mpCur==0) doomLevelTime++;   /* doom_playerTic runs per player: one clock (MPLAYER.H) */
 if (!camera)
    return;
 s=camera->s;
 assert(s>=0 && s<level_nmSectors);
 if (!doomSectorDamage[s])
    return;
 if (camera->floorSector==-1)
    return;                                     /* not on the floor */
 if (doomSectorExit[s>>3] & (1<<(s&7)))
    doom_playerGodOff();                        /* case 11: cheats &= ~CF_GODMODE */
 if (!(doomLevelTime & 0x1f))
    doom_playerDamage(doomSectorDamage[s],NULL);
 if ((doomSectorExit[s>>3] & (1<<(s&7))) && currentState.health<=10 && !mpCompetitive() &&
     !doom_isMonsterPlayer(mpCur))
    doomExitLevel(0);
}

/* A_BossDeath (p_enemy.c): on a boss level, when the last boss of its kind dies and the player
   is alive, the tag no line targets (doom2ps doom_specials.BOSS_TAGS) moves -- E1M8: both
   MT_BRUISER dead lowers tag 666 to its lowest neighbour (lowerFloorToLowest), the wall that
   hides the teleporter to the last room.  -1 = no boss rule on that level. */
static const short doomBossMt[DOOM_NMLEVELS]={-1,-1,-1,-1,-1,-1,-1,MT_BRUISER,-1};
#define DOOM_BOSS_TAG 666

void doom_bossDeath(DoomActor *mo)
{Object *o;
 int l,lNm=currentState.currentLevel;
 assert(mo);
 if (lNm<0 || lNm>=DOOM_NMLEVELS || doomBossMt[lNm]!=mo->mt)
    return;
 if (currentState.health<=0)
    return;                                     /* make sure there is a player alive for victory */
 for (l=0;l<2;l++)
    for (o=l?objectIdleList:objectRunList;o;o=o->next)
       if (o!=(Object *)mo && o->func==game_actor_func && ((DoomActor *)o)->mt==mo->mt &&
	   ((DoomActor *)o)->health>0)
	  return;                               /* other boss not dead */
 signalAllObjects(SIGNAL_SWITCH,DOOM_BOSS_TAG,0);
}

/* --- GCC14: for the game modes (DOOM_MODES.C, MPLAYER.H) ------------------------------------ */

int doom_exiting(void)
{return doomExiting;
}

/* A fighting game's round is over (its limit, or the exit): the next level -- the same one for
   the boss battle, the first after the last -- and nobody carries anything into it */
void doom_endRound(void)
{int lNm=currentState.currentLevel,next;
 assert(lNm>=0 && lNm<DOOM_NMLEVELS);
 if (doomExiting)
    return;
 doomExiting=1;
 next=(mpMode==MP_BOSS)? lNm: doomLevelNext[lNm];
 if (next<0)
    next=0;
 playerHitTeleport(next);
}

const char *doom_levelLabel(int l)
{static char label[5];
 if (l<0 || l>=DOOM_NMLEVELS)
    return "";
 memcpy(label,doomLevelNames[l]+1,4);           /* "+E1M1.LEV" -> "E1M1" */
 label[4]=0;
 return label;
}

int doom_bossLevel(int l)
{return l>=0 && l<DOOM_NMLEVELS && doomBossMt[l]>=0;
}

/* BOSS BATTLE's screen, before the level is loaded: how many bosses the level holds (E1M8: its
   two Barons, at every skill -- things 3003, flags 7) and what they are */
int doom_bossCount(int l)
{static const unsigned char count[DOOM_NMLEVELS]={0,0,0,0,0,0,0,2,0};
 return (l>=0 && l<DOOM_NMLEVELS)? count[l]: 0;
}

const char *doom_bossName(int l)
{switch ((l>=0 && l<DOOM_NMLEVELS)? doomBossMt[l]: -1)
    {case MT_BRUISER: return "BARON OF HELL";
     default:         return "";
    }
}

/* BOSS BATTLE: what opens a boss level's arena from the start -- E1M8: the block in front of the
   start (tag 1) and the Barons' two closets (tag 5).  The boss floor (666) stays up: behind it,
   the teleporter to the last room, which hurts and ends the level. */
int doom_levelOpenChannel(int i)
{static const short e1m8[]={1,5,-1};
 if (currentState.currentLevel!=7)
    return -1;
 return e1m8[(i<2)? i: 2];
}

/* the mobj type whose death opens this level's way out, -1 = none */
int doom_levelBossMt(void)
{int l=currentState.currentLevel;
 return (l>=0 && l<DOOM_NMLEVELS)? doomBossMt[l]: -1;
}

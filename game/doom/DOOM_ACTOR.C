/* DOOM_ACTOR.C -- the generic Doom object: spawn, states, rotations, damage, projectiles.
 * SPEC_RUNTIME sections 2, 4, 5, 7; contract sections 1, 2, 4.
 *
 * One handler, game_actor_func, drives every mobj of the level (monsters, barrels, decor,
 * missiles, puffs, blood); pickups share it through doom_item_func (DOOM_GAME.C).  States and
 * tics come from doomStates[] (35 Hz, counted by SIGNAL_MOVE), the verb of a state from
 * doomActions[] (DOOM_VERBS.C), the drawn sequence from the contract section 2 formula.
 * Randomness: P_Random() over rndtable[] (DOOM_TABLES.C), consumed in Doom's order wherever
 * the engine mechanics allow (deviations are noted at the call). */
#include "util.h"
#include "level.h"
#include "sprite.h"
#include "walls.h"
#include "object.h"
#include "ai.h"
#include "aicommon.h"
#include "sruins.h"
#include "sequence.h"
#include "hitscan.h"
#include "gamestat.h"
#include "doom.h"
#include "mplayer.h"
#include "doom_lights.h"

/* The missile carrying the light of its kind, or NULL (doomLightMissile): one entry per kind of
   missile that lights.  Declared here because game_actor_func clears an entry when its carrier
   dies. */
#define DOOM_NMLIT 5
static Sprite *doomLitMissile[DOOM_NMLIT];
static void doomMissileLightGone(Sprite *s);

#define DOOM_MISSILERANGE F(2048)      /* p_local.h:55 */

int nmSpawnFail;
Object *doomSoundTarget[MAXNMSECTORS];
/* GCC14: every pickup of the level, linked through the actors (pkNext/pkPrev).  A pickup ran
   SIGNAL_MOVE every tic to test the players' reach, forever, drops included: 70 to 230 visits a
   tic that almost always found nobody moving.  The player now walks this list once a tic
   (DOOM_GAME.C doom_pickupTic), and a pickup whose state never changes leaves objectRunList. */
Object *doomPickups;

static void doomMissileHit(DoomActor *this,int collide);
static int doomMoveMissile(DoomActor *this);

/* --- helpers shared with DOOM_VERBS.C ------------------------------------------------------ */

/* The usual target is the engine PlayerObject (AI.C:39-58), which is not a DoomActor: its
   sprite is the camera and its life is currentState.health (SPEC_RUNTIME section 3.2). */
Sprite *doom_targetSprite(Object *t)
{int k;
 if (!t)
    return NULL;
 if ((k=mpIndexOfObject(t))>=0)       /* any player, whoever is loaded (MPLAYER.H) */
    return mpBody[k];
 if (t->func==game_actor_func || t->func==doom_item_func)
    return ((DoomActor *)t)->sprite;
 return NULL;
}

int doom_targetAlive(Object *t)
{int k;
 if (!t)
    return 0;
 if ((k=mpIndexOfObject(t))>=0)
    return mpPeekInt(k,&currentState.health)>0;
 if (t->func==player_func)
    return 0;                           /* a player who left: parked, not a target */
 if (t->func==game_actor_func)
    {DoomActor *a=(DoomActor *)t;
     return a->health>0 && (a->mflags & DF_SHOOTABLE);
    }
 return 0;
}

/* P_AproxDistance (p_maputl.c): dx+dy-min/2, the 2D version (approxDist is 3D) */
Fixed32 doom_approxDist2(Fixed32 dx,Fixed32 dz)
{dx=abs(dx);
 dz=abs(dz);
 if (dx<dz)
    return dx+dz-(dx>>1);
 return dx+dz-(dz>>1);
}

void doom_actorLevelInit(void)
{int s;
 for (s=0;s<MAXNMSECTORS;s++)
    doomSoundTarget[s]=NULL;
 nmSpawnFail=0;
 doomPickups=NULL;
}

/* --- sequences (contract section 2) --------------------------------------------------------- */

/* Identical to the Python side:
     if (map[spr] == -2) return -2            (guard BEFORE the 0x8000 test: -2 = 0xFFFE)
     stride = (map[spr] & 0x8000) ? 8 : 1
     seq = (map[spr] & 0x7fff) + frame*stride + (stride == 8 ? view : 0)
   view = getFacingAngle() 0..7 = Doom rotation view+1; frame = states[].frame & 0x7fff. */
short doom_seq(int sprite,int frame,int view)
{int m,stride;
 assert(sprite>=0 && sprite<NUMSPRITES);
 assert(frame>=0 && frame<0x8000);
 assert(view>=0 && view<8);
 m=level_sequenceMap[sprite];
 if (m==-2)
    return -2;
 stride=(m & 0x8000)?8:1;
 return (short)((m & 0x7fff)+frame*stride+(stride==8?view:0));
}

/* The rotation (0..7) of `from` seen from the point `at`: AICOMMON.C getFacingAngle, from a
   position.  doomSetSequence (drawSprites' SIGNAL_VIEW) and doom_drawSeq (WALLS.C's leaf walk,
   before that signal) share it, so the frame the walk tests is the frame drawn.
   GCC14: the direction of the eye in the thing's own frame, by two rotations instead of the
   arc tangent the angle needed (getAngle walks a table, and this ran two to four times per
   thing per image).  The eight sectors are the same, 46 degrees wide about the front and 44
   about the back (tan 23 and tan 68 as 16.16 fractions), so the same rotation comes out except
   within a hair of a boundary.  Past THING_ROT4_DIST four of them are kept: the front, the two
   sides and the back, the frame of the nearest quarter turn. */
#define DOOM_TAN23 27819                /* tan 23 deg * 65536 */
#define DOOM_TAN68 162213               /* tan 68 */
#define DOOM_TAN22 26478                /* tan 22 (the 158 deg boundary, from the back) */
#define DOOM_TAN67 154394               /* tan 67 (the 113 deg boundary, from the back) */
static int doomSeqTurns(int sprite)     /* eight views to pick from?  (-2: an absent family) */
{int m=level_sequenceMap[sprite];
 return m!=-2 && (m & 0x8000);
}

static int doomFacing(Sprite *from,MthXyz *at)
{Fixed32 wx=at->x-from->pos.x,wz=at->z-from->pos.z,c,s,x,y,ay;
 int lod,back;
 lod=(GP_THING_ROT4_DIST>0 && doom_approxDist2(wx,wz)>F(GP_THING_ROT4_DIST));
 while (wx>F(2048) || wx<F(-2048) || wz>F(2048) || wz<F(-2048))
    {wx>>=1;                            /* the products stay inside 32 bits */
     wz>>=1;
    }
 c=MTH_Cos(from->angle);
 s=MTH_Sin(from->angle);
 x=MTH_Mul(wx,c)+MTH_Mul(wz,s);         /* the eye, turned into the thing's frame */
 y=MTH_Mul(wz,c)-MTH_Mul(wx,s);
 ay=(y<0)? -y: y;
 back=(x<0);
 if (back)
    x=-x;
 if (lod)
    {if (ay<=x)
	return back? 4: 0;
     return (y<0)? 6: 2;
    }
 if (!back)
    {if (ay<MTH_Mul(x,DOOM_TAN23))
	return 0;
     if (ay<MTH_Mul(x,DOOM_TAN68))
	return (y<0)? 7: 1;
    }
 else
    {if (ay<MTH_Mul(x,DOOM_TAN22))
	return 4;
     if (ay<MTH_Mul(x,DOOM_TAN67))
	return (y<0)? 5: 3;
    }
 return (y<0)? 6: 2;
}

/* sprite->sequence for the current state and the view of `at`; an absent family (-2) leaves
   the sprite undrawn (-1: WALLS.C:2594 skips it) instead of reading outside the block.
   at = NULL: the rotation is not chosen here.  A state change happens up to four times a second
   per monster and the rotation it picked was thrown away by the next SIGNAL_VIEW, which picks it
   for the camera that draws (and, in split screen, for each view in turn).  A family drawn from
   one side only (stride 1 in level_sequenceMap) never needs it at all. */
static void doomSetSequence(DoomActor *this,MthXyz *at)
{const DoomState *st=&doomStates[this->state];
 int view=(at && doomSeqTurns(st->sprite))? doomFacing(this->sprite,at): 0;
 int seq=doom_seq(st->sprite,st->frame,view);
 if (seq<0)
    seq=-1;
 assert(seq<level_nmSequences);
 this->sprite->sequence=(short)seq;
 /* GCC14: FF_FULLBRIGHT -- the frame lights itself, the leaf's light does not reach it (WALLS.C
    drawSprites).  Here, so the flag follows the frame drawn: SIGNAL_VIEW runs this just before */
 if (st->flags & DOOM_SF_FULLBRIGHT)
    this->sprite->flags|=SPRITEFLAG_FULLBRIGHT;
 else
    this->sprite->flags&=~SPRITEFLAG_FULLBRIGHT;
}

/* The sequence drawSprites will draw for o: its SIGNAL_VIEW runs doomSetSequence, which picks
   the rotation from where the camera stands (eye) and from the actor's angle as its last action
   left it (A_Chase, A_FaceTarget turn it after doom_setState chose the sequence).  WALLS.C
   doom_spriteLeaves picks each sprite's leaf BEFORE that signal -- on the slave, in the last
   image's tail -- when o->sequence may still be the last image's rotation, up to 64 chunk pixels
   narrower on a side.  The same choice, nothing written: the slave may call it.  o->sequence for
   a sprite whose owner does not choose it there (a player body: SRUINS.C mpShowBodies). */
short doom_drawSeq(Sprite *o,MthXyz *eye)
{Object *ow=o->owner;
 const DoomState *st;
 int seq;
 if (!ow || (ow->func!=game_actor_func && ow->func!=doom_item_func) ||
     ((DoomActor *)ow)->sprite!=o)
    return o->sequence;
 st=&doomStates[((DoomActor *)ow)->state];
 seq=doom_seq(st->sprite,st->frame,
	      doomSeqTurns(st->sprite)? doomFacing(o,eye): 0);
 return (short)((seq<0 || seq>=level_nmSequences)? -1: seq);
}

/* --- states (P_SetMobjState, p_mobj.c:49-72) ------------------------------------------------ */

/* terminal states without motion leave objectRunList: decor, corpses and pickups (SPEC_RUNTIME
   section 2) -- a pickup's reach is the player's to test (doom_pickupTic) */
static void doomIdleIfTerminal(DoomActor *this)
{if (this->tics==-1 && (this->func==game_actor_func || (this->mflags & DF_PICKUP)) &&
     !(this->mflags & DF_IDLE) &&
     ((this->mflags & DF_CORPSE) || !(this->mflags & (DF_SHOOTABLE|DF_MISSILE))))
    {this->mflags|=DF_IDLE;
     delay_moveObject((Object *)this,objectIdleList);
    }
}

/* P_SpawnMobj's state: `mobj->state = st; mobj->tics = st->tics` WITHOUT the verb ("action
   routines can not be called yet", p_mobj.c) -- a monster's first A_Look is its first tic. */
static void doomSetSpawnState(DoomActor *this,int state)
{const DoomState *st;
 if (state==S_NULL)
    {doom_setState(this,S_NULL);                /* MT_TELEPORTMAN and kin: nothing to show */
     return;
    }
 assert(state>0 && state<NUMSTATES);
 st=&doomStates[state];
 assert(!(st->flags & DOOM_SF_PSPRITE));
 this->state=(short)state;
 this->tics=st->tics;
 this->sprite->frame=0;
 doomSetSequence(this,NULL);
 doomIdleIfTerminal(this);
}

void doom_setState(DoomActor *this,int state)
{const DoomState *st;
 assert(this);
 assert(this->sprite);
 do
    {if (state==S_NULL)
	{this->state=0;
	 this->tics=-1;
	 delayKill((Object *)this);
	 return;
	}
     assert(state>0 && state<NUMSTATES);
     st=&doomStates[state];
     assert(!(st->flags & DOOM_SF_PSPRITE));    /* weapon states never reach an actor */
     this->state=(short)state;
     this->tics=st->tics;
     this->mflags&=~DF_HALFSTATE;               /* its own length again (A_Chase may lengthen it) */
     this->sprite->frame=0;
     doomSetSequence(this,NULL);
     /* Doom actors have no momentum: a state that does not walk (attack, pain, look) stands
	still.  A_Chase sets the velocity again right after this, missiles keep theirs. */
     if (!(this->mflags & DF_MISSILE))
	{this->sprite->vel.x=0;
	 this->sprite->vel.z=0;
	}
     if (st->action)
	{assert(st->action<DOOM_NUMACTIONS);
	 if (doomActions[st->action].mobj)
	    doomActions[st->action].mobj(this);
	 if (this->type==OT_DEAD)
	    return;                             /* the verb killed us */
	}
     state=st->nextstate;
    }
 while (!this->tics);
 doomIdleIfTerminal(this);
}

/* GCC14: at rest, is it by a door?  Asked once where it stopped, not every tic: the answer only
   depends on where it stands, and the doors' sectors are marked once, at the level start.
   By a door it collides again only when the geometry has moved since its last collision there
   (pushBlockEpoch: the push blocks move once an image, after the tics): the same collision
   against a door standing still gives the same answer.  It used to collide every tic. */
static int doomRestNearDoor(DoomActor *this)
{if (!(this->mflags & DF_DOORASKED))
    {this->mflags|=DF_DOORASKED;
     if (doom_nearDoor(this->sprite))
	this->mflags|=DF_NEARDOOR;
     else
	this->mflags&=~DF_NEARDOOR;
     this->restEpoch=(unsigned short)(pushBlockEpoch-1);
    }
 if (!(this->mflags & DF_NEARDOOR) || this->restEpoch==(unsigned short)pushBlockEpoch)
    return 0;
 this->restEpoch=(unsigned short)pushBlockEpoch;
 return 1;
}

/* --- the handler ---------------------------------------------------------------------------- */

void game_actor_func(Object *_this,int message,int param1,int param2)
{DoomActor *this=(DoomActor *)_this;
 switch (message)
    {case SIGNAL_MOVE:
	assert(this->sprite);
	if (this->mflags & DF_CORPSE)
	   this->collide=0;
	else if (this->mflags & DF_MISSILE)
	   {this->collide=doomMoveMissile(this);
	    doomMissileHit(this,this->collide);
	   }
	else if (this->sprite->flags & SPRITEFLAG_IMMOBILE)
	   this->collide=0;      /* pickups and solid decor never move: moveSprite would only run
				    collideSprite for nothing, every tic, for each of them -- the
				    movers find them, a pickup tests its own reach (doom_item_func) */
	else if (this->sprite->floorSector!=-1 && !this->sprite->vel.x && !this->sprite->vel.y &&
		 !this->sprite->vel.z && !doomRestNearDoor(this))
	   {if (!this->target)
	       this->collide=0;
	   }                     /* at rest on its floor, asleep or not: the same collision again
				    every tic was 7-8 ms a frame on E1M1 asleep (RUNOBJECTS >
				    COLLIDESPRITE), up to 19 awake.  A lift carries it (floorSector,
				    updatePushBlockPositions), A_Chase's step gives it a velocity.
				    Awake, it keeps its last step's answer for doomBlocked.  By a
				    door it keeps colliding: the ceiling coming down meets it */
	else
	   {Fixed32 x=this->sprite->pos.x,z=this->sprite->pos.z;
	    int s=this->sprite->s;
	    this->collide=moveSprite(this->sprite);
	    if (this->sprite->pos.x!=x || this->sprite->pos.z!=z || this->sprite->s!=s)
	       this->mflags&=~DF_DOORASKED;   /* it moved: ask again where it stops */
	    if (this->mflags & DF_STEP)
	       {this->sprite->vel.x=0;  /* the step is taken: still until the next A_Chase */
		this->sprite->vel.z=0;
		this->mflags&=~DF_STEP;
	       }
	   }
	/* Muzzle flash and explosion fade, then go out -- before doom_setState, which can move the
	   actor to S_NULL and free the sprite under the light.  Only on a tic an image was drawn
	   for: a flash between two images is a flash nobody sees (DOOM_LIGHTS.C). */
	if (this->flashTics && doom_lightStep())
	   {this->flashTics--;
	    if (this->sprite)
	       {if (!this->flashTics)
		   removeLight(this->sprite);
		else if (this->mt==MT_ROCKET || this->mt==MT_BARREL)   /* A_Explode's two users */
		   doom_lightFade(this->sprite,DLF_EXPLODE,this->flashTics);
		else
		   doom_lightFade(this->sprite,DLF_MUZZLE_MONSTER,this->flashTics);
	       }
	   }
	if (this->tics>0)
	   {if (--this->tics==0)
	       doom_setState(this,doomStates[this->state].nextstate);
	   }
	break;
     case SIGNAL_VIEW:
	if (this->sprite)
	   doomSetSequence(this,camera? &camera->pos: (MthXyz *)0);
	break;
     case SIGNAL_HURT:
	doom_damageActor(this,param1,(Object *)param2);
	break;
     case SIGNAL_OBJECTDESTROYED:
	if ((Object *)param1==_this)
	   {if (this->sprite)
	       {doomMissileLightGone(this->sprite);  /* a carrier dies: the next one takes the light */
		removeLight(this->sprite);   /* no-op without a light; else the list keeps a freed sprite */
		freeSprite(this->sprite);
	       }
	    this->sprite=NULL;
	   }
	else if ((Object *)param1==this->target)
	   this->target=NULL;
	break;
     default:
	break;
    }
}

/* --- spawn (P_SpawnMobj, SPEC_RUNTIME section 2) -------------------------------------------- */

/* Sphere radius of a solid, non-shootable thing (COLU, ELEC, CBRA...).  Doom blocks by the 2D
   distance r + 16 whatever the heights; the engine collides spheres, the player's being centred
   on the eye (41 above the feet, radius 16: params/doom.cfg).  A sphere of radius R resting on
   the floor stops that eye ball at the 2D distance D = r + 16 when (16+R)^2 - (41-R)^2 = D^2,
   i.e. R = (D^2/57 + 25)/2 (COLU r 16 -> R 21; its height/2 would be 8, which the eye ball
   never reaches).  Monsters (sphere 28 at feet + 28) are then stopped ~13 u farther than Doom. */
static int doomSolidRadius(int r)
{int d=r+16;
 return (d*d/(16+41)+(41-16)+1)/2;
}

/* angle = engine angle (degrees << 16); pos taken as is (placeObjects adds the radius through
   shiftSprites, dynamic callers set pos.y themselves).  NULL when a pool is empty. */
DoomActor *doom_spawn(int mt,int sector,MthXyz *pos,int angle,int thingFlags)
{const DoomMobjInfo *info;
 DoomActor *this;
 messHandler func;
 int class,sflags,gravity,radius,lastlook;
 assert(mt>=0 && mt<NUMMOBJTYPES);
 assert(pos);
 assert(sector>=0 && sector<level_nmSectors);
 info=&doomMobjInfo[mt];
 /* P_SpawnMobj draws `lastlook = P_Random() % MAXPLAYERS` for every mobj (placed or spawned):
    drawn first, even when a pool turns out empty below, so the RNG index follows Doom's */
 lastlook=P_Random()%4;
 func=game_actor_func;
 if (info->flags & MF_SHOOTABLE)
    {class=CLASS_MONSTER;               /* monsters AND barrels: autoTarget, OBJECTDESTROYED */
     /* BSHORT (beyond the spec's BWATERBNDRY|BCLIFF): a closed door portal carries
	WALLFLAG_SHORTOPENING and only blocks sprites that carry the matching bit
	(bumpSectorBoundries SPRITE.C:414); without it a monster would penetrate the 1 u slit
	of a closed door.  With it the door wall is the COLLIDE_WALL of the tic, and A_Chase
	presses it (doomBlocked, DOOM_VERBS.C). */
     sflags=SPRITEFLAG_BWATERBNDRY|SPRITEFLAG_BCLIFF|SPRITEFLAG_BSHORT;
    }
 else if (info->flags & MF_MISSILE)
    {class=CLASS_PROJECTILE;
     sflags=SPRITEFLAG_IMATERIAL;
    }
 else if (info->flags & MF_SPECIAL)
    {class=CLASS_SPRITE;                /* pickups: collision only, doom_item_func */
     sflags=SPRITEFLAG_IMATERIAL|SPRITEFLAG_IMMOBILE;
     func=doom_item_func;
    }
 else if (info->flags & MF_SOLID)
    {class=CLASS_SPRITE;                /* solid decor: blocks walkers and missiles, bullets pass
					   (PIT_CheckThing; P_LineAttack shoots only SHOOTABLE) */
     sflags=SPRITEFLAG_NOHITSCAN|SPRITEFLAG_NOSHADOW|SPRITEFLAG_IMMOBILE;
    }
 else if (doomStates[info->spawnstate].tics==-1)
    {class=CLASS_SPRITE;                /* decor: never moves */
     sflags=SPRITEFLAG_IMATERIAL|SPRITEFLAG_IMMOBILE;
    }
 else
    {class=CLASS_SPRITE;                /* puff, blood, fog: one-shots with a velocity -- up and
					   down only, as P_ZMovement moves them (SPRITEFLAG_ZONLY) */
     sflags=SPRITEFLAG_IMATERIAL|SPRITEFLAG_ZONLY;
    }
 /* MF_SHADOW, the spectre: Doom draws it through its fuzz column map, which this engine has no
    equivalent of -- the VDP1's mesh is the hardware's own see-through, a screen checkerboard. */
 if (info->flags & MF_SHADOW)
    sflags|=SPRITEFLAG_MESH;
 /* P_ZMovement: momz -= GRAVITY = FRACUNIT per tic (SIGNAL_MOVE is one 35 Hz tic) */
 gravity=(info->flags & MF_NOGRAVITY)?0:F(1);
 /* one sphere: height/2 covers the Doom height (SPEC_RUNTIME section 2); a missile keeps its
    Doom radius (TROOPSHOT 6, height 8: the horizontal hit is what matters, section 5); solid
    decor gets the radius that blocks the player at Doom's distance (doomSolidRadius) */
 if (info->flags & MF_MISSILE)
    radius=info->radius;
 else if ((info->flags & MF_SOLID) && !(info->flags & (MF_SHOOTABLE|MF_SPECIAL)))
    radius=doomSolidRadius(info->radius);
 else
    radius=info->height/2;

 this=(DoomActor *)getFreeObject(func,doomMtToOt[mt],class);
 if (!this)
    {nmSpawnFail++;
     return NULL;
    }
 moveObject((Object *)this,objectRunList);   /* getFreeObject leaves it in the free list */
 this->sprite=newSprite(sector,F(radius),F(1),gravity,-1,sflags,(Object *)this);
 if (!this->sprite)
    {this->type=OT_DEAD;
     this->class=CLASS_DEAD;
     moveObject((Object *)this,objectFreeList);
     nmSpawnFail++;
     return NULL;
    }
 this->sprite->pos=*pos;
 this->sprite->angle=normalizeAngle(angle);
 this->sprite->scale=65536;                  /* 1 texel per unit (contract section 2) */
 this->sequenceMap=NULL;
 this->state=0;
 this->lookSkip=0;
 this->mt=(short)mt;
 this->tics=0;
 this->health=info->spawnhealth;
 this->reactiontime=info->reactiontime;
 this->threshold=0;
 this->movecount=0;
 this->movedir=DI_NODIR;
 this->dirCur=0;
 this->nDir=0;
 this->mflags=0;
 if (thingFlags & 8)
    this->mflags|=DF_AMBUSH;
 if (info->flags & MF_SHOOTABLE)
    this->mflags|=DF_SHOOTABLE;
 if (info->flags & MF_MISSILE)
    this->mflags|=DF_MISSILE;
 if (info->flags & MF_NOBLOOD)
    this->mflags|=DF_NOBLOOD;
 if (info->flags & MF_SOLID)
    this->mflags|=DF_SOLID;
 this->target=NULL;
 this->collide=0;
 this->lastlook=(short)lastlook;
 this->restEpoch=0;
 this->chStage=2;
 this->chD1=DI_NODIR;
 this->chD2=DI_NODIR;
 this->chOld=DI_NODIR;
 this->chFlags=0;
 this->flashTics=0;
 this->pkNext=this->pkPrev=NULL;
 if (func==doom_item_func)
    {this->mflags|=DF_PICKUP;           /* before the spawn state: it may idle at once */
     this->pkX=pos->x;
     this->pkZ=pos->z;
     this->pkNext=doomPickups;
     if (doomPickups)
	((DoomActor *)doomPickups)->pkPrev=(Object *)this;
     doomPickups=(Object *)this;
    }
 doomSetSpawnState(this,info->spawnstate);
 return this;
}

/* --- damage (SPEC_RUNTIME section 4) -------------------------------------------------------- */

/* target == camera => doom_playerDamage(damage, source) (armour maths live there only);
   DoomActor => SIGNAL_HURT(damage, source): param2 is the SOURCE (a missile passes its
   shooter, p_inter.c:781), not the inflictor. */
void doom_damage(Sprite *target,Object *inflictor,Object *source,int damage)
{int k;
 assert(target);
 if ((k=mpIndexOfSprite(target))>=0)
    {/* P_DamageMobj pushes the player away from the inflictor before the armour maths.  The hit
	player's state is loaded for the damage, whoever was loaded when it came (MPLAYER.H). */
     int prev;
     Sprite *is;
     if (mpSpares(mpIndexOfObject(source),k))
	return;                                 /* GCC14: team play spares an ally (MPLAYER.H) */
     prev=mpBegin(k);
     is=doom_targetSprite(inflictor);
     if (is && is!=camera)
	doom_playerThrust(is,damage);
     doom_playerDamage(damage,source);
     mpEnd(prev);
    }
 else if (target->owner && target->owner->func==game_actor_func)
    signalObject(target->owner,SIGNAL_HURT,damage,(int)source);
}

/* P_KillMobj (p_inter.c:668-730): corpse flags, death/xdeath state, random tic shortening,
   drops (CLIP, SHOTGUN, CHAINGUN) at floor + item radius (SPEC_RUNTIME section 6) */
static void doomKill(DoomActor *this,Object *source)
{const DoomMobjInfo *info=&doomMobjInfo[this->mt];
 int item,k;
 if ((k=mpIndexOfObject(source))>=0 && (info->flags & MF_COUNTKILL))
    mpStat[k].kills++;                          /* GCC14: the level's score (MPLAYER.H) */
 this->mflags&=~DF_SHOOTABLE;
 this->mflags|=DF_CORPSE;
 /* Doom: a dying thing is no longer MF_SHOOTABLE -- the autoaim (PTR_AimTraverse) and the
    bullets (PTR_ShootTraverse) pass it from THIS tic on, while it stays MF_SOLID until A_Fall.
    Engine: the autoaim elects CLASS_MONSTER sprites (WALLS.C:2724-2732) and hitScan hits any
    sprite without NOHITSCAN (HITSCAN.C:33-34) -- both dropped here, not 2 states later in
    A_Fall (10 tics of a dying POSS stealing the aim from the live one behind it); the body
    keeps blocking (NOSPRCOLLISION comes with A_Fall's IMATERIAL). */
 this->class=CLASS_SPRITE;
 this->sprite->flags|=SPRITEFLAG_NOHITSCAN;
 if (this->health< -info->spawnhealth && info->xdeathstate)
    doom_setState(this,info->xdeathstate);
 else
    doom_setState(this,info->deathstate);
 if (this->type==OT_DEAD)
    return;                                     /* no death state: already S_NULL */
 this->tics-=P_Random()&3;
 if (this->tics<1)
    this->tics=1;

 doom_hordeKilled(this);                        /* HORDE: its own drop, before Doom's own */

 switch (this->mt)
    {case MT_WOLFSS:
     case MT_POSSESSED:
	item=MT_CLIP;
	break;
     case MT_SHOTGUY:
	item=MT_SHOTGUN;
	break;
     case MT_CHAINGUY:
	item=MT_CHAINGUN;
	break;
     default:
	return;
    }
 {MthXyz pos=this->sprite->pos;
  DoomActor *drop;
  pos.y=(pos.y-findFloorDistance(this->sprite->s,&pos))+F(doomMobjInfo[item].height/2);
  drop=doom_spawn(item,this->sprite->s,&pos,0,0);
  if (drop)
     drop->mflags|=DF_DROPPED;
 }
}

/* P_DamageMobj (p_inter.c:792-926) without the thrust */
void doom_damageActor(DoomActor *this,int damage,Object *source)
{const DoomMobjInfo *info;
 assert(this);
 info=&doomMobjInfo[this->mt];
 if (!(this->mflags & DF_SHOOTABLE))
    return;
 if (this->health<=0)
    return;
 this->health-=damage;
 if (this->health<=0)
    {doomKill(this,source);
     return;
    }
 if (P_Random()<info->painchance && info->painstate)
    {this->mflags|=DF_JUSTHIT;                 /* fight back! */
     doom_setState(this,info->painstate);
    }
 this->reactiontime=0;                         /* awake now */
 if (this->mflags & DF_HALFSTATE)              /* hit: it thinks at the full rate again, and the
						  state it is in gets its own length back */
    {this->tics=(short)((this->tics+1)>>1);
     this->mflags&=~DF_HALFSTATE;
    }
 this->mflags&=~DF_HALF;
 if (!this->threshold && source && source!=(Object *)this)
    {/* if not intent on another target, chase after this one */
     this->target=source;
     this->threshold=DOOM_BASETHRESHOLD;
     if (this->state==info->spawnstate && info->seestate)
	doom_setState(this,info->seestate);
    }
}

/* P_RadiusAttack / PIT_RadiusAttack (p_map.c:1240-1300) over the live sprites: distance =
   max(|dx|,|dz|) - radius in units, damage - distance when in range and in sight.
   GCC14: over the leaves near the spot, as Doom reads the blockmap box spot +- (damage +
   MAXRADIUS) -- not every sprite of every leaf.  A thing it can hurt is within damage + its radius
   on both axes, and in sight: the line between them crosses only portals, each of whose planes is
   nearer the spot than the thing.  So the leaves reached from the spot's through portals whose
   plane lies within DOOM_BLAST_REACH hold every candidate, and the same tests follow.  A flood
   that outgrows its queue falls back to every leaf. */
#define DOOM_BLAST_MAXR    128           /* >= the widest thing's radius of the IWADs (MT_SPIDER) */
#define DOOM_BLAST_LEAVES  64
void doom_radiusAttack(DoomActor *spot,Object *source,int damage)
{int dist,n,i,k,w,ns;
 short leaf[DOOM_BLAST_LEAVES];
 Sprite *spr;
 Fixed32 dx,dz,d,reach;
 MthXyz p;
 assert(spot);
 assert(spot->sprite);
 /* sqrt(2) (the box's corner) < 3/2 */
 reach=F((damage+DOOM_BLAST_MAXR)+((damage+DOOM_BLAST_MAXR)>>1));
 n=0;
 leaf[n++]=spot->sprite->s;
 for (i=0;i<n && n>0;i++)
    for (w=level_sector[leaf[i]].firstWall;w<=level_sector[leaf[i]].lastWall;w++)
       {ns=level_wall[w].nextSector;
	if (ns<0)
	   break;                               /* doom2ps puts a leaf's portals first */
	/* the portal's own box first: its plane runs across the map, and a leaf whose portal
	   lies on the same line as a wall beside the spot would be flooded from any distance */
	{int vv,x0,x1,z0,z1,q=f(reach);
	 const sVertexType *pv=level_vertex+level_wall[w].v[0];
	 x0=x1=pv->x; z0=z1=pv->z;
	 for (vv=1;vv<4;vv++)
	    {pv=level_vertex+level_wall[w].v[vv];
	     if (pv->x<x0) x0=pv->x; else if (pv->x>x1) x1=pv->x;
	     if (pv->z<z0) z0=pv->z; else if (pv->z>z1) z1=pv->z;
	    }
	 if (f(spot->sprite->pos.x)+q<x0 || f(spot->sprite->pos.x)-q>x1 ||
	     f(spot->sprite->pos.z)+q<z0 || f(spot->sprite->pos.z)-q>z1)
	    continue;
	}
	getVertex(level_wall[w].v[0],&p);
	d=(f(spot->sprite->pos.x-p.x))*level_wall[w].normal[0]+
	  (f(spot->sprite->pos.y-p.y))*level_wall[w].normal[1]+
	  (f(spot->sprite->pos.z-p.z))*level_wall[w].normal[2];
	if (abs(d)>reach)
	   continue;
	for (k=0;k<n && leaf[k]!=ns;k++)
	   ;
	if (k<n)
	   continue;
	if (n==DOOM_BLAST_LEAVES)
	   {n=-1;                               /* too far a flood: every leaf, as before */
	    break;
	   }
	leaf[n++]=(short)ns;
       }
 for (i=0;i<((n<0)? level_nmSectors: n);i++)
    for (spr=sectorSpriteList[(n<0)? i: leaf[i]];spr;spr=spr->next)
       {if (spr==spot->sprite || !spr->owner)
	   continue;
	if (!doom_targetAlive(spr->owner))
	   continue;
	if (doom_targetSprite(spr->owner)!=spr)
	   continue;
	dx=abs(spr->pos.x-spot->sprite->pos.x);
	dz=abs(spr->pos.z-spot->sprite->pos.z);
	d=(dx>dz)?dx:dz;
	/* thing->radius of Doom: the camera's is the player's (16), an actor's sphere is
	   height/2 -- take info->radius (POSS 20, barrel 10) */
	dist=f(d-((mpIsPlayer(spr))?spr->radius:
		  F(doomMobjInfo[((DoomActor *)spr->owner)->mt].radius)));
	if (dist<0)
	   dist=0;
	if (dist>=damage)
	   continue;                            /* out of range */
	if (canSee(spr,spot->sprite))
	   doom_damage(spr,(Object *)spot,source,damage-dist);
       }
}

/* --- noise alert (P_RecursiveSound, p_enemy.c:101-160) -------------------------------------- */

/* Breadth-first flood over the portals from `sector`; a door portal whose blocking bits are
   set (closed: setDoorBlockBits AI.C:4294-4309) stops the sound.  No ML_SOUNDBLOCK here.
   GCC14: a shot flooded every wall of every leaf the sound reached, each time the player fired
   (E1M6: 4 394 walls).  A leaf's portals come first (doom2ps' invariant), so its walls stop
   being read at the first solid one.  And the flood is not redone when it would write what the
   last one wrote: the same emitter, from a leaf that flood reached, no other flood since, and no
   portal's blocking bits changed (portalBitsEpoch) -- a chaingun fired from one room. */
void doom_noiseAlert(Object *emitter,int sector)
{static short queue[MAXNMSECTORS];
 static unsigned short soundValid[MAXNMSECTORS];
 static unsigned short validcount;
 static Object *lastEmitter;
 static int lastEpoch;
 int head,tail,s,w,ns;
 assert(sector>=0 && sector<level_nmSectors);
 if (emitter==lastEmitter && lastEpoch==portalBitsEpoch && validcount &&
     soundValid[sector]==validcount && doomSoundTarget[sector]==emitter)
    return;
 lastEmitter=emitter;
 lastEpoch=portalBitsEpoch;
 validcount++;
 if (!validcount)
    {for (s=0;s<MAXNMSECTORS;s++)
	soundValid[s]=0;
     validcount=1;
    }
 head=0;
 tail=0;
 queue[tail++]=(short)sector;
 soundValid[sector]=validcount;
 while (head<tail)
    {s=queue[head++];
     doomSoundTarget[s]=emitter;
     for (w=level_sector[s].firstWall;w<=level_sector[s].lastWall;w++)
	{ns=level_wall[w].nextSector;
	 if (ns<0)
	    break;                              /* the portals come first */
	 if ((level_wall[w].flags & WALLFLAG_DOORWALL) &&
	     (level_wall[w].flags & WALLFLAG_BLOCKBITS))
	    continue;                           /* closed door */
	 assert(ns<level_nmSectors);
	 if (soundValid[ns]==validcount)
	    continue;                           /* already flooded */
	 soundValid[ns]=validcount;
	 assert(tail<MAXNMSECTORS);
	 queue[tail++]=(short)ns;
	}
    }
}

/* --- projectiles (SPEC_RUNTIME section 5) --------------------------------------------------- */

/* P_ExplodeMissile (p_mobj.c:85-98).  GCC14: the flight light goes out at the impact, before the
   death state: its frames are FULLBRIGHT and light themselves, and the light kept a whole pool
   burning for the 18 tics of an imp's explosion.  A rocket's A_Explode (the death state's verb)
   lights its own explosion.  A lit plasma bolt hands the stream's light to the next one. */
static void doomExplodeMissile(DoomActor *this)
{const DoomMobjInfo *info=&doomMobjInfo[this->mt];
 this->sprite->vel.x=0;
 this->sprite->vel.y=0;
 this->sprite->vel.z=0;
 removeLight(this->sprite);
 doomMissileLightGone(this->sprite);
 this->sprite->flags|=SPRITEFLAG_ZONLY;         /* it stays where it hit, touching nothing */
 doom_setState(this,info->deathstate);
 if (this->type==OT_DEAD)
    return;
 this->tics-=P_Random()&3;
 if (this->tics<1)
    this->tics=1;
 this->mflags&=~DF_MISSILE;
 if (info->deathsound)
    doom_sound(this->sprite,info->deathsound);
}

/* moveSprite of a missile with its shooter made transparent for the call (PIT_CheckThing:
   `tmthing->target == thing` => no interaction at all).  Spawned at the shooter's centre
   (28 + 6 u of overlap) the missile would otherwise be pushed 30 u out of the shooter's
   sphere by collideSpriteSprite (SPRITE.C:506-517), mostly upwards, and lose the velocity
   component along the push; the player's rocket would do the same against the camera. */
static int doomMoveMissile(DoomActor *this)
{Sprite *shooter=doom_targetSprite(this->target);
 int saved=0,collide;
 assert(this->sprite);
 if (shooter)
    {saved=shooter->flags;
     shooter->flags|=SPRITEFLAG_NOSPRCOLLISION;
    }
 collide=moveSprite(this->sprite);
 if (shooter)
    shooter->flags=saved;
 return collide;
}

/* PIT_CheckThing for a missile (p_map.c:310-360): the shooter is passed through; the same
   species as the shooter (player excepted) explodes the missile without damage; anything
   else takes (P_Random()%8+1)*damage.  Walls, floors and ceilings explode it. */
static void doomMissileHit(DoomActor *this,int collide)
{const DoomMobjInfo *info=&doomMobjInfo[this->mt];
 if (!collide)
    return;
 if (collide & COLLIDE_SPRITE)
    {Sprite *spr=&sprites[collide&0xffff];
     Object *o=spr->owner;
     int damage;
     if (o && o==this->target)
	goto walls;                             /* shooter */
     if (o && !mpIsPlayer(spr) && (o->func==game_actor_func || o->func==doom_item_func))
	{DoomActor *hit=(DoomActor *)o;
	 if (this->target && this->target->func==game_actor_func &&
	     ((DoomActor *)this->target)->mt==hit->mt)
	    {doomExplodeMissile(this);          /* same species: explode, no damage */
	     return;
	    }
	 if (!(hit->mflags & DF_SHOOTABLE))
	    {if (hit->mflags & DF_SOLID)
		{doomExplodeMissile(this);      /* `return !(thing->flags & MF_SOLID)`: solid decor,
						   dying body before A_Fall */
		 return;
		}
	     goto walls;                        /* corpse, puff: not solid */
	    }
	}
     else if (!mpIsPlayer(spr))
	goto walls;                             /* engine sprite: ignore */
     damage=(P_Random()%8+1)*info->damage;
     doom_damage(spr,(Object *)this,this->target,damage);
     doomExplodeMissile(this);
     return;
    }
 walls:
 if (collide & (COLLIDE_WALL|COLLIDE_FLOOR|COLLIDE_CEILING))
    doomExplodeMissile(this);
}

/* --- dynamic lights (what each effect asks for, and the tuner: DOOM_LIGHTS.C) ---------------- */

/* Every level: the old level's sprites are freed, so are the carriers. */
void doom_missileLightsReset(void)
{int i;
 for (i=0;i<DOOM_NMLIT;i++)
    doomLitMissile[i]=NULL;
}

/* A carrier died: the next missile of its kind lights again. */
static void doomMissileLightGone(Sprite *s)
{int i;
 for (i=0;i<DOOM_NMLIT;i++)
    if (doomLitMissile[i]==s)
       doomLitMissile[i]=NULL;
}

/* GCC14: one light for a volley.  Two missiles of the same kind flying closer to each other than
   GP_LIGHT_VOLLEY_DIST are inside one another's pool (reach 160 u), so the second is spawned with
   no light of its own -- what the engine already did for the plasma stream, whose bolts fly 75 u
   apart.  Plasma keeps its own unconditional rule: ONE bolt lit, the oldest alive, whatever the
   distance -- with a distance test the third bolt of a stream would light again.
   The carrier is forgotten when it dies, so the next one fired carries the light. */
static int doomLitKind(int mt)
{switch (mt)
    {case MT_TROOPSHOT:   return 0;
     case MT_HEADSHOT:    return 1;
     case MT_BRUISERSHOT: return 2;
     case MT_ROCKET:      return 3;
     case MT_PLASMA:      return 4;
    }
 return -1;
}

static void doomLightMissile(DoomActor *th,int mt)
{int k;
 Sprite *c;
 assert(th);
 if (!th->sprite)
    return;
 k=doomLitKind(mt);
 if (k<0)
    return;
 c=doomLitMissile[k];
 if (c)
    {if (mt==MT_PLASMA)
	return;                          /* the stream's one bolt is already lit */
     if (GP_LIGHT_VOLLEY_DIST &&
	 doom_approxDist2(c->pos.x-th->sprite->pos.x,c->pos.z-th->sprite->pos.z)<
	 F(GP_LIGHT_VOLLEY_DIST))
	return;                          /* already standing in the carrier's pool */
    }
 doomLitMissile[k]=th->sprite;
 switch (mt)
    {case MT_TROOPSHOT:
	doom_lightAdd(th->sprite,DLF_IMP);
	break;
     case MT_HEADSHOT:
	doom_lightAdd(th->sprite,DLF_CACO);
	break;
     case MT_BRUISERSHOT:
	doom_lightAdd(th->sprite,DLF_BARON);
	break;
     case MT_ROCKET:
	doom_lightAdd(th->sprite,DLF_ROCKET);
	break;
     case MT_PLASMA:
	doom_lightAdd(th->sprite,DLF_PLASMA);
	break;
    }
}

/* P_SpawnMissile + P_CheckMissileSpawn (p_mobj.c:1121-1160): spawned 32 u above the feet,
   speed u/tic (contract section 4) towards dest, vertical rate from the flight time */
Object *doom_spawnMissile(DoomActor *src,Object *dest,int mt)
{const DoomMobjInfo *info;
 DoomActor *th;
 Sprite *ds;
 MthXyz pos,vel;
 int an,dist,collide;
 assert(src);
 assert(src->sprite);
 assert(mt>=0 && mt<NUMMOBJTYPES);
 ds=doom_targetSprite(dest);
 if (!ds)
    return NULL;
 info=&doomMobjInfo[mt];
 pos=src->sprite->pos;
 pos.y+=F(32)-src->sprite->radius;
 an=getAngle(ds->pos.x-src->sprite->pos.x,ds->pos.z-src->sprite->pos.z);
 th=doom_spawn(mt,src->sprite->s,&pos,an,0);
 if (!th)
    return NULL;
 if (info->seesound)
    doom_sound(th->sprite,info->seesound);
 th->target=(Object *)src;                     /* where it came from */
 doomLightMissile(th,mt);
 th->sprite->vel.x=MTH_Mul(F(info->speed),MTH_Cos(an));
 th->sprite->vel.z=MTH_Mul(F(info->speed),MTH_Sin(an));
 dist=f(doom_approxDist2(ds->pos.x-src->sprite->pos.x,ds->pos.z-src->sprite->pos.z));
 dist=dist/info->speed;
 if (dist<1)
    dist=1;
 {/* momz = (dest->z - source->z)/dist: feet to feet (the camera is the eye, 41 above) */
  Fixed32 dfeet=(mpIsPlayer(ds))?ds->pos.y-F(41):ds->pos.y-ds->radius;
  th->sprite->vel.y=(dfeet-(src->sprite->pos.y-src->sprite->radius))/dist;
 }

 /* P_CheckMissileSpawn: shorten the first state, move half a tic, explode on contact */
 th->tics-=P_Random()&3;
 if (th->tics<1)
    th->tics=1;
 vel=th->sprite->vel;
 th->sprite->vel.x=vel.x>>1;
 th->sprite->vel.y=vel.y>>1;
 th->sprite->vel.z=vel.z>>1;
 collide=doomMoveMissile(th);
 th->sprite->vel=vel;
 th->collide=collide;
 doomMissileHit(th,collide);
 return (Object *)th;
}

/* P_SpawnPlayerMissile (p_mobj.c:1167-1200) for the rocket launcher (SPEC_PLAYER section 2.5):
   spawned 32 u above the feet (camera->pos.y is the eye, 41 above the feet), speed u/tic along
   the sprite-convention angle (yaw + 90) and the autoaim pitch, shooter = the player object
   (doomMissileHit lets it through, the victims target the player).  Same P_CheckMissileSpawn. */
Object *doom_spawnPlayerMissile(int mt,int angle,int pitch)
{const DoomMobjInfo *info;
 DoomActor *th;
 MthXyz pos,vel;
 Fixed32 cp;
 int collide;
 assert(mt>=0 && mt<NUMMOBJTYPES);
 assert(camera);
 info=&doomMobjInfo[mt];
 pos=camera->pos;
 pos.y-=F(41-32);
 th=doom_spawn(mt,camera->s,&pos,angle,0);
 if (!th)
    return NULL;
 if (info->seesound)
    doom_sound(th->sprite,info->seesound);
 th->target=(Object *)player;
 doomLightMissile(th,mt);
 cp=MTH_Mul(F(info->speed),MTH_Cos(pitch));
 th->sprite->vel.x=MTH_Mul(cp,MTH_Cos(angle));
 th->sprite->vel.z=MTH_Mul(cp,MTH_Sin(angle));
 th->sprite->vel.y=MTH_Mul(F(info->speed),MTH_Sin(pitch));
 th->tics-=P_Random()&3;
 if (th->tics<1)
    th->tics=1;
 vel=th->sprite->vel;
 th->sprite->vel.x=vel.x>>1;
 th->sprite->vel.y=vel.y>>1;
 th->sprite->vel.z=vel.z>>1;
 collide=doomMoveMissile(th);
 th->sprite->vel=vel;
 th->collide=collide;
 doomMissileHit(th,collide);
 return (Object *)th;
}

/* P_SpawnPuff (p_mobj.c:1022-1036): MT_PUFF, random height, rises 1 u/tic, S_PUFF3 for
   a punch.  The ONE implementation, also used by the player hitscan (SPEC_PLAYER 2.5). */
void doom_spawnPuff(MthXyz *pos,int sector,int melee)
{MthXyz p;
 DoomActor *th;
 assert(pos);
 p=*pos;
 p.y+=(P_Random()-P_Random())<<10;
 th=doom_spawn(MT_PUFF,sector,&p,0,0);
 if (!th)
    return;
 th->sprite->vel.y=F(1);
 th->tics-=P_Random()&3;
 if (th->tics<1)
    th->tics=1;
 if (melee)
    doom_setState(th,S_PUFF3);                 /* punches do not spark on the wall */
}

/* P_SpawnBlood (p_mobj.c:1049-1066): MT_BLOOD, rises 2 u/tic under gravity, smaller
   splashes for smaller damage */
void doom_spawnBlood(MthXyz *pos,int sector,int damage)
{MthXyz p;
 DoomActor *th;
 assert(pos);
 p=*pos;
 p.y+=(P_Random()-P_Random())<<10;
 th=doom_spawn(MT_BLOOD,sector,&p,0,0);
 if (!th)
    return;
 th->sprite->vel.y=F(2);
 th->tics-=P_Random()&3;
 if (th->tics<1)
    th->tics=1;
 if (damage<=12 && damage>=9)
    doom_setState(th,S_BLOOD2);
 else if (damage<9)
    doom_setState(th,S_BLOOD3);
}

/* --- monster hitscan (SPEC_RUNTIME section 3.3) --------------------------------------------- */

/* Eye at feet + height/2 + 8 (p_map.c:1116), yaw with the spread already applied, pitch
   towards the centre of the target sphere (P_AimLineAttack), range MISSILERANGE.  On a
   sprite: puff or blood first, then the damage (PTR_ShootTraverse order, p_map.c:1083-1089);
   on a wall: puff pulled back 4 u along the ray.  Returns the hitScan code (0 = nothing). */
int doom_lineAttack(DoomActor *src,int yaw,int damage)
{const DoomMobjInfo *info;
 MthXyz eye,ray,hit;
 Sprite *ts;
 Fixed32 cp;
 int hitSec,code,pitch;
 assert(src);
 assert(src->sprite);
 /* Muzzle flash on the shooter -- an addition: Doom only marks firing frames fullbright.
    The flashTics guard avoids a second light when a monster fires on consecutive tics. */
 if (!src->flashTics)
    doom_lightAdd(src->sprite,DLF_MUZZLE_MONSTER);
 else
    doom_lightChange(src->sprite,DLF_MUZZLE_MONSTER);     /* back to full after the half tic */
 src->flashTics=doomLightFx[DLF_MUZZLE_MONSTER].tics;
 info=&doomMobjInfo[src->mt];
 eye=src->sprite->pos;
 eye.y=eye.y-src->sprite->radius+F(info->height/2+8);
 pitch=0;
 ts=doom_targetSprite(src->target);
 if (ts)
    {int du=dist(ts->pos.x-eye.x,0,ts->pos.z-eye.z);   /* fixSqrt(n,0): integer units */
     if (du>32767)
	du=32767;
     if (du>0)
	pitch=getAngle(F(du),ts->pos.y-eye.y);
    }
 cp=MTH_Cos(pitch);
 ray.x=MTH_Mul(cp,MTH_Cos(yaw));
 ray.y=MTH_Sin(pitch);
 ray.z=MTH_Mul(cp,MTH_Sin(yaw));
 code=hitScan(src->sprite,&ray,&eye,src->sprite->s,&hit,&hitSec);
 if (!code)
    return 0;
 if (approxDist(hit.x-eye.x,hit.y-eye.y,hit.z-eye.z)>DOOM_MISSILERANGE)
    return 0;
 if (code & COLLIDE_SPRITE)
    {Sprite *spr=&sprites[code&0xffff];
     if (mpIsPlayer(spr))
	{doom_spawnBlood(&hit,hitSec,damage);
	 doom_damage(spr,(Object *)src,(Object *)src,damage);
	}
     else if (spr->owner && spr->owner->func==game_actor_func)
	{if (((DoomActor *)spr->owner)->mflags & DF_NOBLOOD)
	    doom_spawnPuff(&hit,hitSec,0);
	 else
	    doom_spawnBlood(&hit,hitSec,damage);
	 doom_damage(spr,(Object *)src,(Object *)src,damage);
	}
     else
	doom_spawnPuff(&hit,hitSec,0);
    }
 else
    {MthXyz p;
     p.x=hit.x-(ray.x<<2);
     p.y=hit.y-(ray.y<<2);
     p.z=hit.z-(ray.z<<2);
     doom_spawnPuff(&p,hitSec,0);
    }
 return code;
}

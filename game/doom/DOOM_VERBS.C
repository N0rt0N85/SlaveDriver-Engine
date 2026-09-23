/* DOOM_VERBS.C -- the A_* verbs (actor AND weapon, one table) and the A_Chase helpers.
 * SPEC_RUNTIME section 3 (actors), SPEC_PLAYER section 2.2 (psprites); contract section 4.
 *
 * doomActions[] is instantiated from the X-macro of DOOM_ACTIONS.H (generated with the tables):
 * index = doom_action_t, [0] = NULL; M(n) = actor verb -> .mobj, P(n) = weapon verb -> .psp.  The
 * member is chosen at run time by doomStates[].flags & DOOM_SF_PSPRITE, never here.
 *
 * The episode 1 actor verbs are real (A_Look, A_Chase, A_FaceTarget, A_PosAttack,
 * A_SPosAttack, A_TroopAttack, A_SargAttack, A_BruisAttack, A_BossDeath, A_Pain, A_Scream,
 * A_XScream, A_Fall, A_Explode; A_HeadAttack too, the same shape); the weapon verbs (P) live in
 * DOOM_WEAPON.C next to the psprite machine; the actor verbs the shareware never reaches and the
 * shareware-less weapon verbs stay empty bodies (STUB_ lists below). */
#include "util.h"
#include "level.h"
#include "sprite.h"
#include "walls.h"
#include "object.h"
#include "ai.h"
#include "sruins.h"
#include "hitscan.h"
#include "gamestat.h"
#include "doom.h"
#include "mplayer.h"
#include "profile.h"
#include "doom_lights.h"
#include "doom_actions.h"

DOOM_ACTIONS_PROTOTYPES

/* p_enemy.c dirtype_t: 45 degree steps, east first, counter-clockwise (engine z = Doom y,
   vel = (cos, sin)(movedir * 45 degrees), same sense: doom3d.py:1006-1011) */
#define DI_EAST      0
#define DI_NORTHEAST 1
#define DI_NORTH     2
#define DI_NORTHWEST 3
#define DI_WEST      4
#define DI_SOUTHWEST 5
#define DI_SOUTH     6
#define DI_SOUTHEAST 7

static const unsigned char doomOpposite[9]=
{DI_WEST,DI_SOUTHWEST,DI_SOUTH,DI_SOUTHEAST,DI_EAST,DI_NORTHEAST,DI_NORTH,DI_NORTHWEST,DI_NODIR};
static const unsigned char doomDiags[4]=
{DI_NORTHWEST,DI_NORTHEAST,DI_SOUTHWEST,DI_SOUTHEAST};

/* --- stub bodies: weapon verbs outside the shareware target, actor verbs E1M1 never reaches -- */
#define STUB_M(n) void n(DoomActor *this) {assert(this);}
#define STUB_P(n) void n(DoomPlayer *p,int ps) {assert(p); assert(ps>=0 && ps<DOOM_NUMPSPRITES);}
STUB_P(A_FireShotgun2)
STUB_P(A_OpenShotgun2)
STUB_P(A_LoadShotgun2)
STUB_P(A_CloseShotgun2)
STUB_P(A_BFGsound)
STUB_P(A_FireBFG)
STUB_M(A_BFGSpray)
STUB_M(A_PlayerScream)
STUB_M(A_VileChase)
STUB_M(A_VileStart)
STUB_M(A_VileTarget)
STUB_M(A_VileAttack)
STUB_M(A_StartFire)
STUB_M(A_Fire)
STUB_M(A_FireCrackle)
STUB_M(A_Tracer)
STUB_M(A_SkelWhoosh)
STUB_M(A_SkelFist)
STUB_M(A_SkelMissile)
STUB_M(A_FatRaise)
STUB_M(A_FatAttack1)
STUB_M(A_FatAttack2)
STUB_M(A_FatAttack3)
STUB_M(A_CPosAttack)
STUB_M(A_CPosRefire)
STUB_M(A_SkullAttack)
STUB_M(A_Metal)
STUB_M(A_SpidRefire)
STUB_M(A_BabyMetal)
STUB_M(A_BspiAttack)
STUB_M(A_Hoof)
STUB_M(A_CyberAttack)
STUB_M(A_PainAttack)
STUB_M(A_PainDie)
STUB_M(A_KeenDie)
STUB_M(A_BrainPain)
STUB_M(A_BrainScream)
STUB_M(A_BrainDie)
STUB_M(A_BrainAwake)
STUB_M(A_BrainSpit)
STUB_M(A_SpawnSound)
STUB_M(A_SpawnFly)
STUB_M(A_BrainExplode)
#undef STUB_M
#undef STUB_P

/* --- the table ------------------------------------------------------------------------------ */
#define M(n) { n },
#define P(n) { .psp = n },
const DoomAction doomActions[DOOM_NUMACTIONS]=
{{ 0 },
 DOOM_ACTIONS_LIST(M, P)
};
#undef M
#undef P

/* --- A_Chase helpers (SPEC_RUNTIME sections 3, 3.2) ----------------------------------------- */

/* GCC14: one line of sight, shared by the leaf.  canSee walks the ray leaf by leaf and tests every
   wall of each one twice, so what it costs is WHICH leaves the ray crosses -- and that is the same
   corridor for every monster of one leaf looking at one marine.  The answer is filed under (leaf of
   the looker, which marine, leaf of the marine) and kept GP_MONSTER_SIGHT_SHARE tics: the look
   phases are drawn at random when the level is placed (DOOM_GAME.C), so two monsters of a leaf
   almost never ask on the same tic and a file kept for less than a look period would never be read.
   Direct mapped -- a collision simply traces again.  Only a marine is filed: monster-to-monster
   sight (the noise target) is rare and its second leaf would not fit the key. */
#if GP_MONSTER_SIGHT_SHARE
/* GCC14: 32 leaves PER MARINE.  The key holds which marine, so the distinct keys are as many as
   there are players, and a table that did not grow with them thrashed exactly where sight costs
   most: on console, E1M3 in 2 players spends 8.8-12.5 ms a frame in Sight against 0.3-8.6 solo
   (captures 2026-09-23).  Four slots per leaf bucket, one per marine: no player ever takes a
   slot from another. */
#define DOOM_SIGHT_MEMO (32*MPMAX)
#define DOOM_SIGHT_SLOT(leaf,k) (((((leaf)<<2)+(k)))&(DOOM_SIGHT_MEMO-1))
typedef struct
{short look;                            /* leaf the looker stands in                            */
 short seen;                            /* leaf the marine stands in                            */
 unsigned short tic;                    /* doomLevelTime when it was traced                     */
 unsigned short qstamp;                 /* image this pair was last queued for the batch on     */
 signed char k;                         /* which marine, -1 = free                              */
 signed char ans;                       /* what canSee answered                                 */
} DoomSightMemo;
static DoomSightMemo sightMemo[DOOM_SIGHT_MEMO];

/* GCC14: THE SLAVE'S SIGHT BATCH.  Every pair the tic had to trace is written down here with the
   point it traced from; in the tail of the image the slave answers them all again against the
   marines as they now stand, and fills the file, so the next tic reads instead of tracing.
   Where it runs: at the head of the traversal the slave already does in the tail (WALLS.C
   wallRenderSlaveMain, job 1).  It takes NO kick of its own -- a kick that lands while the slave
   is clearing its capture flag is lost and hangs both chips (wallsPipeJoin, seen on console
   2026-09-23), so the one thing not to do here is add a third one an image.  And by then Weapon,
   the HUD and the overlay have run: nothing of the master's spawns or frees sprites any more.
   It still reads no sprite at all -- only the snapshot below and the level's geometry -- because
   the master owns the sprites and this must never depend on that. */
#define DOOM_SIGHT_JOBS 48
typedef struct
{MthXyz pos;                            /* where the monster that asked stood                   */
 short leaf,k;
} DoomSightJob;
static DoomSightJob sightJob[DOOM_SIGHT_JOBS];
static int nmSightJobs;
static MthXyz sightEye[MPMAX];          /* the marines, copied at the kick: the slave reads this */
static short sightEyeLeaf[MPMAX];
static int nmSightEyes;
static unsigned short sightBatchTic;
static unsigned short sightQueueStamp=1;   /* bumped once an image: one job per pair, not per ask */
int nmSightBatch;                       /* what the last batch answered (overlay) */
#endif

void doom_sightShareReset(void)
{
#if GP_MONSTER_SIGHT_SHARE
 int i;
 for (i=0;i<DOOM_SIGHT_MEMO;i++)
    {sightMemo[i].k=-1;
     sightMemo[i].qstamp=0;
    }
 nmSightJobs=0;
 nmSightEyes=0;
 nmSightBatch=0;
#endif
}

/* The master, in the tail (CFG_SIGHT_SNAP): freeze what the slave will need.  Called where the
   traversal is kicked -- the tic, Post, the weapon and the drawing are all behind us. */
void doom_sightSnap(void)
{
#if GP_MONSTER_SIGHT_SHARE
 int k;
 nmSightEyes=0;
 for (k=0;k<mpPlayers && k<MPMAX;k++)
    {Sprite *p=mpBody[k];
     if (!p)
	break;
     sightEye[k]=p->pos;
     sightEyeLeaf[k]=(short)p->s;
     nmSightEyes++;
    }
 sightBatchTic=(unsigned short)doomLevelTime;
 if (!++sightQueueStamp)                /* 0 means "never queued" in a fresh table */
    sightQueueStamp=1;
#endif
}

/* The slave (CFG_SIGHT_BATCH), at the head of its traversal. */
void doom_sightBatch(void)
{
#if GP_MONSTER_SIGHT_SHARE
 int i,n=0;
 for (i=0;i<nmSightJobs;i++)
    {int leaf=sightJob[i].leaf,k=sightJob[i].k;
     DoomSightMemo *m;
     if (k<0 || k>=nmSightEyes)
	continue;                       /* that marine left between the tic and here */
     if (leaf<0 || leaf>=level_nmSectors)
	continue;                       /* this runs on the SLAVE: never index on a torn read */
     m=sightMemo+DOOM_SIGHT_SLOT(leaf,k);
     m->look=(short)leaf;
     m->seen=sightEyeLeaf[k];
     m->tic=sightBatchTic;
     m->k=(signed char)k;
     m->ans=(signed char)canSeePos(&sightJob[i].pos,leaf,sightEye+k,sightEyeLeaf[k]);
     n++;
    }
 nmSightJobs=0;                         /* the next tic writes its own list */
 nmSightBatch=n;
#endif
}

/* GCC14: canSee, timed as Sight in the L+R+Y tree (under Run Objects) */
static int doomSee(Sprite *a,Sprite *b)
{int r;
#if GP_MONSTER_SIGHT_SHARE
 DoomSightMemo *m=NULL;
 int k=mpIndexOfSprite(b);
 int hit=0;
 if (k>=0)
    {m=sightMemo+DOOM_SIGHT_SLOT(a->s,k);
     hit=(m->k==(signed char)k && m->look==(short)a->s && m->seen==(short)b->s &&
	  (unsigned short)((unsigned short)doomLevelTime-m->tic)<=
	  (unsigned short)GP_MONSTER_SIGHT_SHARE);  /* both 16 bits: the subtraction wraps as it must */
    }
 if (!hit)
#endif
    {CFG_PROF("Sight");
     r=canSee(a,b);
     CFG_PROF_END();
    }
#if GP_MONSTER_SIGHT_SHARE
 else
    r=m->ans;
 if (m)
    {if (!hit)
	{m->look=(short)a->s;
	 m->seen=(short)b->s;
	 m->tic=(unsigned short)doomLevelTime;
	 m->k=(signed char)k;
	 m->ans=(signed char)r;
	}
     /* Ask the slave for this pair again in the tail, whether we traced it or read it: a list
	built from the MISSES alone starves itself -- everything the batch answered is then a hit,
	nothing is asked for again, and the image after that misses all over again.  Once per pair
	per image (qstamp), so one leaf full of monsters queues one job. */
     if (m->qstamp!=sightQueueStamp && nmSightJobs<DOOM_SIGHT_JOBS)
	{m->qstamp=sightQueueStamp;
	 sightJob[nmSightJobs].pos=a->pos;
	 sightJob[nmSightJobs].leaf=(short)a->s;
	 sightJob[nmSightJobs].k=(short)k;
	 nmSightJobs++;
	}
    }
#endif
 return r;
}

/* P_LookForPlayers (p_enemy.c:495-560), one player: alive, in sight, and in front unless
   allaround (behind = |angle| > 90 degrees and farther than MELEERANGE) */
int doom_lookForPlayer(DoomActor *this,int allaround)
{Sprite *s,*p;
 int an,i,k;
 assert(this);
 s=this->sprite;
 /* P_LookForPlayers: the players in turn from lastlook, two at most per call (MPLAYER.H: they
    are read whoever is loaded).  Solo is the one-player case of the same loop. */
 for (i=0;i<mpPlayers && i<2;i++)
    {k=((this->lastlook&3)+i)%mpPlayers;
     p=mpBody[k];
     if (mpPeekInt(k,&currentState.health)<=0)
	continue;                               /* dead */
     if (doom_isMonsterPlayer(k))
	continue;                               /* GCC14: one of theirs -- until it hurts them */
     /* GCC14: the facing test before the line of sight, not after: both must pass and neither
	draws P_Random or writes anything, so the answer is Doom's, and the cheap one goes
	first -- a third of the sleepers' traces were of a player behind their back */
     if (!allaround)
	{an=normalizeAngle(getAngle(p->pos.x-s->pos.x,p->pos.z-s->pos.z)-s->angle);
	 if (an>F(90) || an<F(-90))
	    {if (doom_approxDist2(p->pos.x-s->pos.x,p->pos.z-s->pos.z)>F(64))
		continue;                       /* behind back */
	    }
	}
     /* GCC14: a sleeper far from this marine traces every other look.  The sector's noise target is
	read before this (A_Look), so a shot still wakes it on the spot; what this costs is up to one
	look period -- 0.29 s -- before it notices a marine in silence, and d32xr looks every 5 tics
	at 15 Hz, which is slower than that already.  allaround is the awake caller: never delayed. */
#if GP_MONSTER_FAR_LOOK
     if (!allaround &&
	 doom_approxDist2(p->pos.x-s->pos.x,p->pos.z-s->pos.z)>F(GP_MONSTER_FAR_LOOK))
	{this->lookSkip^=1;
	 if (this->lookSkip)
	    continue;                           /* its turn to skip the trace */
	}
#endif
     if (!doomSee(s,p))
	continue;                               /* out of sight */
     this->lastlook=(short)k;
     this->target=mpObj[k];
     return 1;
    }
 this->lastlook=(short)(((this->lastlook&3)+i)%mpPlayers);
 return 0;
}

/* P_CheckMeleeRange (p_enemy.c:170-186): closer than MELEERANGE-20 + the target radius */
int doom_meleeRange(DoomActor *this)
{Sprite *ts;
 assert(this);
 ts=doom_targetSprite(this->target);
 if (!ts)
    return 0;
 /* Doom radius of the target: the camera's is the Doom player's (16); an actor's sphere is
    height/2 (DOOM_ACTOR.C doom_spawn), not its Doom radius */
 if (doom_approxDist2(ts->pos.x-this->sprite->pos.x,ts->pos.z-this->sprite->pos.z)>=
     F(64-20)+((mpIsPlayer(ts))?ts->radius:F(doomMobjInfo[((DoomActor *)this->target)->mt].radius)))
    return 0;
 if (!doomSee(this->sprite,ts))
    return 0;
 return 1;
}

/* P_CheckMissileRange (p_enemy.c:188-256).  GCC14: a monster still waiting (reactiontime) and
   not just hit answers 0 whatever the line of sight says, and the trace writes nothing: it is
   not traced -- id's own "OPTIMIZE" note on that line.  A shot that wakes thirty monsters used
   to trace thirty lines for nothing on their first A_Chase. */
int doom_missileRange(DoomActor *this)
{const DoomMobjInfo *info;
 Sprite *ts;
 int dist;
 assert(this);
 info=&doomMobjInfo[this->mt];
 ts=doom_targetSprite(this->target);
 if (!ts)
    return 0;
 if (this->reactiontime && !(this->mflags & DF_JUSTHIT))
    return 0;
 if (!doomSee(this->sprite,ts))
    return 0;
 if (this->mflags & DF_JUSTHIT)
    {this->mflags&=~DF_JUSTHIT;                 /* the target just hit us: fight back */
     return 1;
    }
 if (this->reactiontime)
    return 0;                                   /* do not attack yet */
 dist=f(doom_approxDist2(this->sprite->pos.x-ts->pos.x,this->sprite->pos.z-ts->pos.z))-64;
 if (!info->meleestate)
    dist-=128;                                  /* no melee attack, so fire more */
 if (this->mt==MT_VILE && dist>14*64)
    return 0;
 if (this->mt==MT_UNDEAD)
    {if (dist<196)
	return 0;
     dist>>=1;
    }
 if (this->mt==MT_CYBORG || this->mt==MT_SPIDER || this->mt==MT_SKULL)
    dist>>=1;
 if (dist>200)
    dist=200;
 if (this->mt==MT_CYBORG && dist>160)
    dist=160;
 if (P_Random()<dist)
    return 0;
 return 1;
}

static void doomAddDir(DoomActor *this,int d)
{int i;
 if (d==DI_NODIR)
    return;
 for (i=0;i<this->nDir;i++)
    if (this->dirTry[i]==d)
       return;                                  /* Doom would probe it twice: same answer */
 assert(this->nDir<8);
 this->dirTry[this->nDir++]=(char)d;
}

#define DOOM_CH_DOMINANT_Z 1           /* chFlags: |dz| > |dx| when the list was started          */

/* Next stage of the candidate list, drawn only when every earlier candidate has failed -- the
   draws of P_NewChaseDir (p_enemy.c:395-440) are reached only past a failed probe:
     stage 0 -> 1: axis swap (P_Random() > 200), then dominant axis, minor axis, old direction;
     stage 1 -> 2: sweep sense (P_Random() & 1), then the sweep of the 8 and the turnaround.
   Skips a stage that adds nothing (Doom falls through it too).  Returns 1 with dirCur on the
   first new candidate, 0 when the list is exhausted. */
static int doomChaseStage(DoomActor *this)
{int d1,d2,tdir,turnaround,n0;
 assert(this->chOld<=DI_NODIR);
 turnaround=doomOpposite[(int)this->chOld];
 n0=this->nDir;
 while (this->chStage<2 && this->nDir==n0)
    {if (this->chStage==0)
	{/* try other directions */
	 d1=this->chD1;
	 d2=this->chD2;
	 if (P_Random()>200 || (this->chFlags & DOOM_CH_DOMINANT_Z))
	    {tdir=d1;
	     d1=d2;
	     d2=tdir;
	    }
	 if (d1==turnaround)
	    d1=DI_NODIR;
	 if (d2==turnaround)
	    d2=DI_NODIR;
	 doomAddDir(this,d1);
	 doomAddDir(this,d2);
	 /* there is no direct path to the player, so pick another direction */
	 if (this->chOld!=DI_NODIR)
	    doomAddDir(this,this->chOld);
	}
     else
	{/* randomly determine direction of search */
	 if (P_Random()&1)
	    {for (tdir=DI_EAST;tdir<=DI_SOUTHEAST;tdir++)
		if (tdir!=turnaround)
		   doomAddDir(this,tdir);
	    }
	 else
	    {for (tdir=DI_SOUTHEAST;tdir>=DI_EAST;tdir--)
		if (tdir!=turnaround)
		   doomAddDir(this,tdir);
	    }
	 if (turnaround!=DI_NODIR)
	    doomAddDir(this,turnaround);
	}
     this->chStage++;
    }
 if (this->nDir==n0)
    return 0;
 this->dirCur=(unsigned char)n0;
 return 1;
}

/* P_NewChaseDir (p_enemy.c:353-487) without the P_TryWalk probes: the candidates come in
   Doom's order (diagonal; dominant axis, minor axis, old direction; sweep of the 8 in a random
   sense, turnaround) and A_Chase tries one per blocked tic.  P_Random: the axis swap and the
   sweep sense are drawn lazily by doomChaseStage, as Doom draws them only past failed probes;
   ONE movecount per call (Doom: P_TryWalk on the probe that succeeds).  Deviation: that
   movecount is drawn here, before the stage draws of the candidates tried on later tics. */
void doom_newChaseDir(DoomActor *this)
{Sprite *ts;
 Fixed32 dx,dz;
 int d1,d2,olddir,turnaround;
 assert(this);
 this->nDir=0;
 this->dirCur=0;
 this->chStage=0;
 this->chFlags=0;
 ts=doom_targetSprite(this->target);
 if (!ts)
    {this->movedir=DI_NODIR;
     this->chStage=2;
     return;
    }
 olddir=this->movedir;
 assert(olddir<=DI_NODIR);
 turnaround=doomOpposite[olddir];
 dx=ts->pos.x-this->sprite->pos.x;
 dz=ts->pos.z-this->sprite->pos.z;
 if (dx>F(10))
    d1=DI_EAST;
 else if (dx<F(-10))
    d1=DI_WEST;
 else
    d1=DI_NODIR;
 if (dz<F(-10))
    d2=DI_SOUTH;
 else if (dz>F(10))
    d2=DI_NORTH;
 else
    d2=DI_NODIR;
 this->chD1=(unsigned char)d1;
 this->chD2=(unsigned char)d2;
 this->chOld=(unsigned char)olddir;
 if (abs(dz)>abs(dx))
    this->chFlags|=DOOM_CH_DOMINANT_Z;

 /* try direct route */
 if (d1!=DI_NODIR && d2!=DI_NODIR)
    {int diag=doomDiags[((dz<0)<<1)+(dx>0)];
     if (diag!=turnaround)
	doomAddDir(this,diag);
    }
 if (!this->nDir && !doomChaseStage(this))
    {this->movedir=DI_NODIR;                    /* can not move */
     return;
    }
 this->movedir=this->dirTry[(int)this->dirCur];
 this->movecount=P_Random()&15;                 /* P_TryWalk */
}

/* a blocked tic: next candidate of the list, the next stage of it, or a fresh list when it is
   exhausted (Doom: movedir = DI_NODIR, then a new P_NewChaseDir at the next A_Chase) */
static void doomNextDir(DoomActor *this)
{if (this->movedir!=DI_NODIR)
    {if (this->dirCur+1<this->nDir)
	{this->dirCur++;
	 this->movedir=this->dirTry[(int)this->dirCur];
	 return;
	}
     if (doomChaseStage(this))
	{this->movedir=this->dirTry[(int)this->dirCur];
	 return;
	}
    }
 doom_newChaseDir(this);
}

/* P_Move's answer for the last tic (SPEC_RUNTIME section 3.2 step 7): blocked by a wall or
   by a sprite (the target included: P_TryMove fails against it too, so Doom re-picks a
   direction every call while adjacent); a door wall in the way is pressed (P_UseSpecialLine)
   and does not count as blocked -- except the wall of an ML_SECRET line, which a monster
   never uses (P_UseSpecialLine: `if (!thing->player && line->flags & ML_SECRET) return false`). */
static int doomBlocked(DoomActor *this)
{int collide=this->collide;
 if (this->movedir==DI_NODIR)
    return 1;
 if (collide & COLLIDE_SPRITE)
    return 1;
 if (collide & COLLIDE_WALL)
    {int w=collide&0xffff;
     assert(w>=0 && w<level_nmWalls);
     if (doom_monsterUseDoor(w))
	return 0;                               /* the door is opening: keep pushing */
     return 1;                                  /* locked, secret or switch-only: P_Move fails */
    }
 return 0;
}

/* --- the E1M1 actor verbs (SPEC_RUNTIME section 3) ----------------------------------------- */

/* A_Look (p_enemy.c:591-657): the sector's noise target, else a player in front */
void A_Look(DoomActor *this)
{const DoomMobjInfo *info;
 Object *targ;
 int sound;
 assert(this);
 info=&doomMobjInfo[this->mt];
 this->threshold=0;                             /* any shot will wake up */
 targ=doomSoundTarget[this->sprite->s];
 if (targ && doom_targetAlive(targ))
    {this->target=targ;
     if (this->mflags & DF_AMBUSH)
	{if (doomSee(this->sprite,doom_targetSprite(targ)))
	    goto seeyou;
	}
     else
	goto seeyou;
    }
 if (!doom_lookForPlayer(this,0))
    return;
 seeyou:
 if (info->seesound)
    {switch (info->seesound)
	{case sfx_posit1:
	 case sfx_posit2:
	 case sfx_posit3:
	    sound=sfx_posit1+P_Random()%3;
	    break;
	 case sfx_bgsit1:
	 case sfx_bgsit2:
	    sound=sfx_bgsit1+P_Random()%2;
	    break;
	 default:
	    sound=info->seesound;
	    break;
	}
     if (this->mt==MT_SPIDER || this->mt==MT_CYBORG)
	doom_sound(NULL,sound);                 /* full volume */
     else
	doom_sound(this->sprite,sound);
    }
 doom_setState(this,info->seestate);
}

/* GCC14: is this monster one the level can run at half the rate?  It chases a marine, the reject
   table says its leaf never sees any marine's -- so no attack of its can reach one, they all ask
   for a line of sight, and nothing of it is drawn -- and it is far from every marine.  What it
   then does between two thoughts is walk, and the step below is doubled to match. */
static int doomChaseHalf(DoomActor *this)
{Sprite *p,*ts;
 int k;
 /* not DF_JUSTHIT: only doom_missileRange clears it, and a monster this test keeps can never
    reach it -- a melee-only monster would carry it for the rest of its life */
 if (!GP_MONSTER_FAR_THINK || (this->mflags & DF_JUSTATTACKED))
    return 0;
 ts=doom_targetSprite(this->target);
 if (!ts || !mpIsPlayer(ts))
    return 0;
 for (k=0;k<mpPlayers;k++)
    {p=mpBody[k];
     if (!p)
	continue;
     if (level_maySee(this->sprite->s,p->s))
	return 0;
     if (doom_approxDist2(p->pos.x-this->sprite->pos.x,p->pos.z-this->sprite->pos.z)<
	 F(GP_MONSTER_FAR_THINK))
	return 0;
    }
 return 1;
}

/* A_Chase (p_enemy.c:659-767).  P_Move's whole step (speed along movedir) is the velocity of the
   next tic only (DF_STEP, DOOM_ACTOR.C): Doom moves a monster once per A_Chase, every 2 to 4
   tics, and so collides it once.
   GCC14: a monster that is far and cannot be seen (doomChaseHalf) holds its run state twice as
   long, walks twice as far in its step, and counts down twice as fast -- the same ground speed,
   the same turns, the same timers, half the thoughts: half its A_Chase calls, its steps, its
   collisions and its sight traces. */
void A_Chase(DoomActor *this)
{const DoomMobjInfo *info;
 const DoomState *st;
 Sprite *s;
 int a,delta,step=(this->mflags & DF_HALF)?2:1;
 assert(this);
 info=&doomMobjInfo[this->mt];
 s=this->sprite;
 if (this->reactiontime)
    {this->reactiontime-=step;
     if (this->reactiontime<0)
	this->reactiontime=0;
    }
 /* modify target threshold */
 if (this->threshold)
    {if (!doom_targetAlive(this->target))
	this->threshold=0;
     else
	{this->threshold-=step;
	 if (this->threshold<0)
	    this->threshold=0;
	}
    }
 /* turn towards movement direction if not there yet: angle &= 7<<29, +-45 degrees */
 if (this->movedir<8)
    {a=s->angle;
     while (a<0)
	a+=F(360);
     while (a>=F(360))
	a-=F(360);
     a=(a/F(45))*F(45);
     delta=a-this->movedir*F(45);
     while (delta<0)
	delta+=F(360);
     while (delta>=F(360))
	delta-=F(360);
     if (delta>=F(180))
	delta-=F(360);
     if (delta>0)
	a-=F(45);
     else if (delta<0)
	a+=F(45);
     s->angle=normalizeAngle(a);
    }
 if (!doom_targetAlive(this->target))
    {/* look for a new target */
     if (doom_lookForPlayer(this,1))
	return;                                 /* got a new target */
     doom_setState(this,info->spawnstate);
     return;
    }
 /* do not attack twice in a row */
 if (this->mflags & DF_JUSTATTACKED)
    {this->mflags&=~DF_JUSTATTACKED;
     doom_newChaseDir(this);
     goto setvel;
    }
 /* check for melee attack */
 if (info->meleestate && doom_meleeRange(this))
    {if (info->attacksound)
	doom_sound(s,info->attacksound);
     doom_setState(this,info->meleestate);
     return;
    }
 /* check for missile attack */
 if (info->missilestate)
    {if (this->movecount)
	goto nomissile;
     if (!doom_missileRange(this))
	goto nomissile;
     doom_setState(this,info->missilestate);
     this->mflags|=DF_JUSTATTACKED;
     return;
    }
 nomissile:
 /* chase towards player */
 this->movecount-=step;
 if (this->movecount<0)
    doom_newChaseDir(this);
 else if (doomBlocked(this))
    doomNextDir(this);
 /* make active sound */
 if (info->activesound && P_Random()<3*step)
    doom_sound(s,info->activesound);
 setvel:
 /* the state it is in now (doom_newChaseDir may have changed none of it, a missile attack
    returned above): its own length, doubled with the step when it is far and unseen */
 if (doomChaseHalf(this))
    {this->mflags|=DF_HALF;
     if (this->tics>0)
	{this->tics*=2;
	 this->mflags|=DF_HALFSTATE;   /* so a hit can give this state its own length back */
	}
     step=2;
    }
 else
    {this->mflags&=~DF_HALF;
     step=1;
    }
 st=&doomStates[this->state];
 if (this->movedir<8 && info->speed && st->tics>0)
    {Fixed32 v=F(info->speed*step);
     this->mflags|=DF_STEP;
     int an=normalizeAngle(this->movedir*F(45));   /* SBL MTH_Sin/Cos: |x| >= 180 reads as 0 */
     s->vel.x=MTH_Mul(v,MTH_Cos(an));
     s->vel.z=MTH_Mul(v,MTH_Sin(an));
    }
 else
    {s->vel.x=0;
     s->vel.z=0;
     this->mflags&=~DF_STEP;
    }
}

/* A_FaceTarget (p_enemy.c:769-787) = PlotCourseToObject */
void A_FaceTarget(DoomActor *this)
{Sprite *ts;
 assert(this);
 ts=doom_targetSprite(this->target);
 if (!ts)
    return;
 this->mflags&=~DF_AMBUSH;
 this->sprite->angle=getAngle(ts->pos.x-this->sprite->pos.x,ts->pos.z-this->sprite->pos.z);
}

/* A_PosAttack (p_enemy.c:789-806): one pistol shot, spread (P_Random()-P_Random())<<20 on
   2^32 = +-255 steps of 360/4096 degrees = 5760 engine units each */
void A_PosAttack(DoomActor *this)
{int angle,damage;
 assert(this);
 if (!this->target)
    return;
 A_FaceTarget(this);
 angle=this->sprite->angle;
 doom_sound(this->sprite,sfx_pistol);
 angle+=(P_Random()-P_Random())*5760;
 damage=((P_Random()%5)+1)*3;
 doom_lineAttack(this,normalizeAngle(angle),damage);
}

/* A_SPosAttack (p_enemy.c:808-830): three pellets around the same base angle */
void A_SPosAttack(DoomActor *this)
{int i,angle,bangle,damage;
 assert(this);
 if (!this->target)
    return;
 doom_sound(this->sprite,sfx_shotgn);
 A_FaceTarget(this);
 bangle=this->sprite->angle;
 for (i=0;i<3;i++)
    {angle=bangle+(P_Random()-P_Random())*5760;
     damage=((P_Random()%5)+1)*3;
     doom_lineAttack(this,normalizeAngle(angle),damage);
    }
}

/* A_TroopAttack (p_enemy.c:900-913): claw in melee range, else a fireball */
void A_TroopAttack(DoomActor *this)
{int damage;
 assert(this);
 if (!this->target)
    return;
 A_FaceTarget(this);
 if (doom_meleeRange(this))
    {Sprite *ts=doom_targetSprite(this->target);
     doom_sound(this->sprite,sfx_claw);
     damage=(P_Random()%8+1)*3;
     if (ts)
	doom_damage(ts,(Object *)this,(Object *)this,damage);
     return;
    }
 doom_spawnMissile(this,this->target,MT_TROOPSHOT);
}

/* A_SargAttack (p_enemy.c:922-935): the demon's (and the spectre's) bite, melee only -- its
   sfx_sgtatk is the attacksound A_Chase plays on entering the melee state */
void A_SargAttack(DoomActor *this)
{Sprite *ts;
 assert(this);
 if (!this->target)
    return;
 A_FaceTarget(this);
 if (doom_meleeRange(this))
    {int damage=((P_Random()%10)+1)*4;
     ts=doom_targetSprite(this->target);
     if (ts)
	doom_damage(ts,(Object *)this,(Object *)this,damage);
    }
}

/* A_HeadAttack (p_enemy.c:937-954): the cacodemon bites in melee range, else spits */
void A_HeadAttack(DoomActor *this)
{assert(this);
 if (!this->target)
    return;
 A_FaceTarget(this);
 if (doom_meleeRange(this))
    {Sprite *ts=doom_targetSprite(this->target);
     int damage=(P_Random()%6+1)*10;
     if (ts)
	doom_damage(ts,(Object *)this,(Object *)this,damage);
     return;
    }
 doom_spawnMissile(this,this->target,MT_HEADSHOT);
}

/* A_BruisAttack (p_enemy.c:966-983): the Baron's claw in melee range, else a green ball -- no
   A_FaceTarget here, the states before it turn him */
void A_BruisAttack(DoomActor *this)
{assert(this);
 if (!this->target)
    return;
 if (doom_meleeRange(this))
    {Sprite *ts=doom_targetSprite(this->target);
     int damage=(P_Random()%8+1)*10;
     doom_sound(this->sprite,sfx_claw);
     if (ts)
	doom_damage(ts,(Object *)this,(Object *)this,damage);
     return;
    }
 doom_spawnMissile(this,this->target,MT_BRUISERSHOT);
}

/* A_BossDeath (p_enemy.c:1666-1760): the level's boss rule lives with the level (DOOM_GAME.C) */
void A_BossDeath(DoomActor *this)
{assert(this);
 doom_bossDeath(this);
}

/* A_Scream (p_enemy.c:1541-1570): random variant of the death sound */
void A_Scream(DoomActor *this)
{int sound;
 assert(this);
 switch (doomMobjInfo[this->mt].deathsound)
    {case 0:
	return;
     case sfx_podth1:
     case sfx_podth2:
     case sfx_podth3:
	sound=sfx_podth1+P_Random()%3;
	break;
     case sfx_bgdth1:
     case sfx_bgdth2:
	sound=sfx_bgdth1+P_Random()%2;
	break;
     default:
	sound=doomMobjInfo[this->mt].deathsound;
	break;
    }
 if (this->mt==MT_SPIDER || this->mt==MT_CYBORG)
    doom_sound(NULL,sound);                     /* full volume */
 else
    doom_sound(this->sprite,sound);
}

void A_XScream(DoomActor *this)
{assert(this);
 doom_sound(this->sprite,sfx_slop);
}

void A_Pain(DoomActor *this)
{assert(this);
 if (doomMobjInfo[this->mt].painsound)
    doom_sound(this->sprite,doomMobjInfo[this->mt].painsound);
}

/* A_Fall (p_enemy.c:1591-1594): the corpse can be walked over and shot through
   (IMATERIAL), is no autoaim candidate (CLASS_SPRITE, WALLS.C:2724), lies on the floor with a
   quarter of its radius (P_KillMobj height >>= 2, p_inter.c:681) */
void A_Fall(DoomActor *this)
{Sprite *s;
 int d;
 assert(this);
 s=this->sprite;
 s->flags|=SPRITEFLAG_IMATERIAL;
 this->class=CLASS_SPRITE;
 this->mflags&=~DF_SOLID;                       /* p_enemy.c A_Fall: flags &= ~MF_SOLID */
 d=findFloorDistance(s->s,&s->pos);
 s->pos.y-=d;                                   /* centre on the floor */
 s->radius>>=2;
 s->radius2=MTH_Mul(s->radius,s->radius);
 s->pos.y+=s->radius;                           /* feet (pos.y - radius) on the floor */
}

/* A_Explode (p_enemy.c:1604-1607) */
void A_Explode(DoomActor *this)
{assert(this);
 doom_explosionLight(this);                     /* an addition: Doom has no dynamic light */
 doom_radiusAttack(this,this->target,128);
}

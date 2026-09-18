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

/* GCC14: canSee, timed as Sight in the L+R+Y tree (under Run Objects) */
static int doomSee(Sprite *a,Sprite *b)
{int r;
 CFG_PROF("Sight");
 r=canSee(a,b);
 CFG_PROF_END();
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
     if (!doomSee(s,p))
	continue;                               /* out of sight */
     if (!allaround)
	{an=normalizeAngle(getAngle(p->pos.x-s->pos.x,p->pos.z-s->pos.z)-s->angle);
	 if (an>F(90) || an<F(-90))
	    {if (doom_approxDist2(p->pos.x-s->pos.x,p->pos.z-s->pos.z)>F(64))
		continue;                       /* behind back */
	    }
	}
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

/* P_CheckMissileRange (p_enemy.c:188-256) */
int doom_missileRange(DoomActor *this)
{const DoomMobjInfo *info;
 Sprite *ts;
 int dist;
 assert(this);
 info=&doomMobjInfo[this->mt];
 ts=doom_targetSprite(this->target);
 if (!ts)
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

/* A_Chase (p_enemy.c:659-767) in velocity: vel = speed/tics (u per tic) along movedir */
void A_Chase(DoomActor *this)
{const DoomMobjInfo *info;
 const DoomState *st;
 Sprite *s;
 int a,delta;
 assert(this);
 info=&doomMobjInfo[this->mt];
 s=this->sprite;
 if (this->reactiontime)
    this->reactiontime--;
 /* modify target threshold */
 if (this->threshold)
    {if (!doom_targetAlive(this->target))
	this->threshold=0;
     else
	this->threshold--;
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
 if (--this->movecount<0)
    doom_newChaseDir(this);
 else if (doomBlocked(this))
    doomNextDir(this);
 /* make active sound */
 if (info->activesound && P_Random()<3)
    doom_sound(s,info->activesound);
 setvel:
 st=&doomStates[this->state];
 if (this->movedir<8 && info->speed && st->tics>0)
    {Fixed32 v=F(info->speed)/st->tics;
     int an=normalizeAngle(this->movedir*F(45));   /* SBL MTH_Sin/Cos: |x| >= 180 reads as 0 */
     s->vel.x=MTH_Mul(v,MTH_Cos(an));
     s->vel.z=MTH_Mul(v,MTH_Sin(an));
    }
 else
    {s->vel.x=0;
     s->vel.z=0;
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

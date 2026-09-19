/* DOOM_MODES.C -- the multiplayer modes (MPLAYER.H mpMode) on the Doom runtime: the monster a
 * player drives (DEMONS: one of the level's, taken over; BOSS BATTLE: the Baron), its attack and
 * its body as the others see it; frags, the frag and time limits, the boss's crown; the level's
 * placement per mode and the end-of-level score.  The engine keeps the sides, the spawn spots
 * and the table (MPLAYER.C); this file says what a role is.
 *
 * A role is not a possessed DoomActor.  The player stays the engine's camera -- its movement,
 * collision, input and per-player swap untouched -- and wears the monster: its frames, its
 * health, a share of its speed, its attack on the fire button.  Taking a monster over moves the
 * camera to where it stood and removes it.
 *
 * Doom references: p_enemy.c A_PosAttack, A_SPosAttack, A_TroopAttack, A_SargAttack,
 * A_HeadAttack, A_BruisAttack, A_SkullAttack (damage, missile, sounds); info.c (attack states,
 * whose tics make the refire time); p_inter.c P_KillMobj (frags), P_TouchSpecialThing (netgame:
 * weapons and keys stay); p_mobj.c P_SpawnMapThing (MF_NOTDMATCH, no monsters in deathmatch). */
#include <string.h>
#include <stdio.h>
#include "util.h"
#include <sega_per.h>
#include "level.h"
#include "sprite.h"
#include "object.h"
#include "ai.h"
#include "sruins.h"
#include "gamestat.h"
#include "sound.h"
#include "doom.h"
#include "sequence.h"
#include "aicommon.h"
#include "hitscan.h"
#include "mplayer.h"

DoomRole doomRole;
short doomRoleMt[MPMAX];               /* the monster each player wears, 0 = the marine: read
					  across players (targeting, bodies), so not in doomRole */

#define DOOM_SPREAD_UNIT    1440       /* the player's own spread, P_GunShot (DOOM_WEAPON.C) */
#define DOOM_MELEE          F(64)      /* MELEERANGE */
#define DOOM_SKULLSPEED     F(20)
#define DOOM_SKULLDASH      12         /* tics of a lost soul's charge */
#define DOOM_BOSS_HP        1000       /* the Baron's, + 700 per marine past the first */
#define DOOM_BOSS_HP_ADD    700
#define DOOM_CROWN_TICS     70         /* 2 s between the boss's death and the next boss */
#define DOOM_VEL_SCALE      38229      /* DOOM_PLAYER.C: u/tic -> u/frame */

/* A monster a player can wear.  melee: (P_Random()%mod+1)*mul, 0 = none; missile -1 = none;
   bullets: 3*(P_Random()%5+1) each (the hitscan monsters); hitSfx: on a melee hit (the imp's and
   the baron's claw), the missile has its own seesound, the bullets play it at the shot; cool =
   the attack states' tics (info.c); speed = percent of the marine's thrust (a monster walks
   8-10 u a step, a running marine 16: the share keeps the chase fair). */
typedef struct
{short mt,missile;
 unsigned char meleeMod,meleeMul,bullets,hitSfx,cool,speed;
} DoomRoleInfo;

static const DoomRoleInfo doomRoles[]=
{{MT_POSSESSED,-1,             0, 0,1,sfx_pistol,26, 70},
 {MT_SHOTGUY,  -1,             0, 0,3,sfx_shotgn,30, 70},
 {MT_TROOP,    MT_TROOPSHOT,   8, 3,0,sfx_claw,  22, 75},
 {MT_SERGEANT, -1,            10, 4,0,0,         24,100},
 {MT_SHADOWS,  -1,            10, 4,0,0,         24,100},
 {MT_HEAD,     MT_HEADSHOT,    6,10,0,0,         15, 60},
 {MT_BRUISER,  MT_BRUISERSHOT, 8,10,0,sfx_claw,  24, 80},
 {MT_SKULL,    -1,             8, 3,0,0,         30,100},
};
#define DOOM_NMROLES ((int)(sizeof(doomRoles)/sizeof(doomRoles[0])))

static int doomCrownTics,doomCrownHeir;
/* BOSS BATTLE: where the level's bosses stand (doom_modePlace), the boss comes in there */
#define DOOM_MAXBOSSSPOTS 4
static struct {MthXyz feet; short sector,yaw;} doomBossSpot[DOOM_MAXBOSSSPOTS];
static int doomNmBossSpots;

void doom_modesLevelReset(void)
{doomNmBossSpots=0;
}

/* Doom's menu, without the characters bigFont lacks (' , !) -- and without NIGHTMARE */
const char *doom_skillName(int s)
{static const char *const name[4]={"TOO YOUNG TO DIE","NOT TOO ROUGH","HURT ME PLENTY","ULTRA-VIOLENCE"};
 return (s>=0 && s<4)? name[s]: "";
}

static const DoomRoleInfo *doomRoleInfo(int mt)
{int i;
 for (i=0;i<DOOM_NMROLES;i++)
    if (doomRoles[i].mt==mt)
       return &doomRoles[i];
 return NULL;
}

void doom_modesMpRegister(void)
{MPREG(doomRole);
}

int doom_isMonsterPlayer(int k)
{return k>=0 && k<MPMAX && doomRoleMt[k]!=0;
}

int doom_roleSpeed(void)
{const DoomRoleInfo *ri=doomRoleInfo(doomRoleMt[mpCur]);
 return ri? ri->speed: 100;
}

/* The loaded player becomes `mt` with `health` (0 = the marine back) */
static void doomBecome(int mt,int health)
{doomRoleMt[mpCur]=(short)mt;
 memset(&doomRole,0,sizeof(doomRole));
 doomRole.atk=-1;
 if (!mt)
    return;
 doomRole.maxHealth=(short)health;
 currentState.health=health;
 doomPlayer.health=health;
 doomPlayer.armorPoints=0;
 doomPlayer.armorType=0;
 doomPlayer.damageCount=0;
 doomPlayer.bonusCount=0;
 doomPlayer.pendingWeapon=DOOM_WP_NOCHANGE;
}

/* DEMONS: the loaded player takes over one of the level's monsters, of a kind it can wear -- the
   nearest at full health, or else the one with the most health left (the nearest of those).
   Never the level's boss (its death opens the way out), never one standing on a floor that hurts
   (E1M8's last room, which also ends the level), never `skip` (the one just left).  1 = done. */
static int doomTakeOver(DoomActor *skip)
{Object *o;
 DoomActor *a,*pick=NULL;
 MthXyz pos;
 Fixed32 d,bestD=0;
 int l,full,bestFull=-1,bestHp=0;
 for (l=0;l<2;l++)
    for (o=l?objectIdleList:objectRunList;o;o=o->next)
       {if (o->func!=game_actor_func || o==(Object *)skip)
	   continue;
	a=(DoomActor *)o;
	if (!a->sprite || !(a->mflags & DF_SHOOTABLE) || a->health<=0 ||
	    (a->sprite->flags & SPRITEFLAG_INVISIBLE) || !doomRoleInfo(a->mt) ||
	    a->mt==doom_levelBossMt() || doomSectorDamage[a->sprite->s])
	   continue;
	full=(a->health>=doomMobjInfo[a->mt].spawnhealth);
	d=doom_approxDist2(a->sprite->pos.x-camera->pos.x,a->sprite->pos.z-camera->pos.z);
	if (pick && (full<bestFull || (!full && !bestFull && a->health<bestHp) ||
		     (full==bestFull && (full || a->health==bestHp) && d>=bestD)))
	   continue;
	pick=a;
	bestFull=full;
	bestHp=a->health;
	bestD=d;
       }
 if (!pick)
    return 0;
 doomBecome(pick->mt,pick->health);
 doomRole.maxHealth=doomMobjInfo[pick->mt].spawnhealth;
 /* where it stood: its feet, the eye at the player's height above them; its facing (the
    sprite convention is the player's yaw + 90) */
 pos=pick->sprite->pos;
 pos.y+=F(GP_PLAYER_RADIUS+GP_PLAYER_EYE_HOVER)-pick->sprite->radius;
 moveSpriteTo(camera,pick->sprite->s,&pos);
 camera->vel.x=camera->vel.y=camera->vel.z=0;
 doomPlayer.momx=doomPlayer.momz=0;
 playerAngle.yaw=normalizeAngle(pick->sprite->angle-F(90));
 camera->angle=playerAngle.yaw;
 /* gone at once for everything that could meet it this tic, freed with the others */
 pick->sprite->flags|=SPRITEFLAG_INVISIBLE|SPRITEFLAG_NOSPRCOLLISION|SPRITEFLAG_NOHITSCAN;
 pick->mflags&=~DF_SHOOTABLE;
 pick->health=0;
 delayKill((Object *)pick);
 doom_setMessage("YOU TAKE OVER A MONSTER");
 return 1;
}

/* The weapon buttons of a player wearing a monster (doom_weaponNext): the monster it leaves goes
   back to its own mind where it stands, with the health it has, and the player takes over the
   next one by the same rule -- never straight back into the one it left. */
void doom_roleHop(void)
{int mt=doomRoleMt[mpCur],hp=currentState.health;
 DoomActor *left;
 MthXyz pos;
 if (mpMode!=MP_MONSTERS || !mt || hp<=0)
    return;
 pos=camera->pos;
 pos.y+=F(doomMobjInfo[mt].height/2)-F(GP_PLAYER_RADIUS+GP_PLAYER_EYE_HOVER);   /* feet + radius */
 left=doom_spawn(mt,camera->s,&pos,normalizeAngle(playerAngle.yaw+F(90)),0);
 if (!left || left->type==OT_DEAD)
    return;                              /* no room for it: stay */
 left->sprite->flags|=SPRITEFLAG_NOSPRCOLLISION;   /* inside the player: kept apart one tic */
 if (!doomTakeOver(left))
    {delayKill((Object *)left);          /* nobody else to take: stay */
     return;
    }
 left->health=(short)hp;
 left->sprite->flags&=~SPRITEFLAG_NOSPRCOLLISION;
}

static void doomBossMorph(void)
{int marines=mpPlayers-1;
 doomBecome(MT_BRUISER,DOOM_BOSS_HP+DOOM_BOSS_HP_ADD*(marines>1? marines-1: 0));
 doom_setMessage("YOU ARE THE BOSS");
}

/* The first boss of a round comes in where one of the level's bosses stands, in its arena */
static void doomBossPlace(void)
{MthXyz pos;
 int i;
 if (!doomNmBossSpots)
    return;
 i=getNextRand()%doomNmBossSpots;
 pos=doomBossSpot[i].feet;
 pos.y+=F(GP_PLAYER_RADIUS+GP_PLAYER_EYE_HOVER);
 moveSpriteTo(camera,doomBossSpot[i].sector,&pos);
 camera->vel.x=camera->vel.y=camera->vel.z=0;
 doomPlayer.momx=doomPlayer.momz=0;
 playerAngle.yaw=((int)doomBossSpot[i].yaw)<<16;
 camera->angle=playerAngle.yaw;
}

/* every player's message but one's */
static void doomTellOthers(int but,const char *msg)
{int k,prev=mpCur;
 for (k=0;k<mpPlayers;k++)
    if (k!=but)
       {mpSwitch(k);
	doom_setMessage(msg);
       }
 mpSwitch(prev);
}

/* --- level start, respawn, placement -------------------------------------------------------- */

/* SRUINS.C runLevel, every player built (CFG_MP_LEVELSTART): the roles of the mode */
void doom_mpLevelStart(void)
{int k,prev=mpCur;
 char msg[32];
 doomCrownTics=0;
 for (k=0;k<MPMAX;k++)
    doomRoleMt[k]=0;
 for (k=0;k<mpPlayers;k++)
    {mpSwitch(k);
     doomBecome(0,0);
     if (mpMode==MP_MONSTERS && mpRole[k] && !doomTakeOver(NULL))
	doom_setMessage("NO MONSTER TO TAKE OVER");
    }
 if (mpMode==MP_BOSS && mpPlayers>1)
    {for (k=0;doom_levelOpenChannel(k)>=0;k++)
	signalAllObjects(SIGNAL_SWITCH,doom_levelOpenChannel(k),0);   /* the arena, open */
     k=getNextRand()%mpPlayers;
     mpSwitch(k);
     doomBossMorph();
     doomBossPlace();
     sprintf(msg,"PLAYER %d IS THE BOSS",k+1);
     doomTellOthers(k,msg);
    }
 mpSwitch(prev);
}

/* SRUINS.C mpRespawn (CFG_MP_RESPAWNED), player k loaded, the marine's kit given: a demon takes
   another monster (none left: it stays down, and fire asks again); a fallen boss comes back as
   a marine -- the crown went to its killer */
void doom_mpRespawned(int k)
{doomBecome(0,0);
 if (mpMode==MP_MONSTERS && mpRole[k] && !doomTakeOver(NULL))
    {currentState.health=0;
     doomPlayer.health=0;
     doom_setMessage("NO MONSTER LEFT TO TAKE OVER");
    }
}

/* DOOM_GAME.C game_placeObject, before a mobj is spawned: 1 = this mode does not place it.  A
   fighting game has no monsters -- their places become the spawn spots -- and no MF_NOTDMATCH
   thing (the keys).  The lost soul is no COUNTKILL: named, as Doom's nomonsters does.  The
   score's totals count what is placed. */
int doom_modePlace(int mt,int sector,MthXyz *pos,int angle,int thingFlags)
{int flags=doomMobjInfo[mt].flags;
 static const unsigned char skillBit[4]={1,1,2,4};    /* P_SpawnMapThing: easy and baby share */
 if (!(thingFlags & skillBit[(mpSkill>=0 && mpSkill<4)? mpSkill: 2]))
    return 1;                            /* not at this skill: doom2ps keeps ULTRA-VIOLENCE's things */
 if (mt==doom_levelBossMt() && doomNmBossSpots<DOOM_MAXBOSSSPOTS)
    {doomBossSpot[doomNmBossSpots].feet=*pos;
     doomBossSpot[doomNmBossSpots].sector=(short)sector;
     doomBossSpot[doomNmBossSpots].yaw=(short)(normalizeAngle(angle-F(90))>>16);
     doomNmBossSpots++;
    }
 if (mpCompetitive())
    {if ((flags & MF_COUNTKILL) || mt==MT_SKULL)
	{mpSpotAdd(sector,pos,angle-F(90));
	 return 1;
	}
     if (flags & MF_NOTDMATCH)
	return 1;
    }
 if (flags & MF_COUNTKILL)
    mpTotal[0]++;
 if (flags & MF_COUNTITEM)
    mpTotal[1]++;
 return 0;
}

/* --- the attack ----------------------------------------------------------------------------- */

static void doomRoleAttack(const DoomRoleInfo *ri)
{const DoomMobjInfo *info=&doomMobjInfo[ri->mt];
 Fixed32 pitch,d=0x7fffffff;
 Sprite *t;
 int yaw=playerAngle.yaw,i,damage;
 doom_aimSlope(&pitch);
 t=doomAimed;
 if (t)
    d=doom_approxDist2(t->pos.x-camera->pos.x,t->pos.z-camera->pos.z);
 doomRole.melee=0;
 if (ri->mt==MT_SKULL)
    {/* A_SkullAttack: a charge at SKULLSPEED; the hit is looked for along the way */
     doomRole.dash=DOOM_SKULLDASH;
     doomRole.melee=1;
     if (info->attacksound)
	doom_sound(camera,info->attacksound);
     return;
    }
 if (ri->meleeMod && (ri->missile<0 || (t && d<=DOOM_MELEE+t->radius)))
    {/* in reach, or a monster that only bites: its attacksound (A_Chase plays it on the way
	into the melee state), then the blow */
     doomRole.melee=1;
     if (info->attacksound)
	doom_sound(camera,info->attacksound);
     damage=(P_Random()%ri->meleeMod+1)*ri->meleeMul;
     if (doom_playerLineAttack(normalizeAngle(yaw),pitch,DOOM_MELEE+F(20),damage,1) && ri->hitSfx)
	doom_sound(camera,ri->hitSfx);
     return;
    }
 if (ri->missile>=0)
    {doom_spawnPlayerMissile(ri->missile,normalizeAngle(yaw+F(90)),pitch);
     return;
    }
 doom_sound(camera,ri->hitSfx);
 for (i=0;i<ri->bullets;i++)
    {damage=((P_Random()%5)+1)*3;
     doom_playerLineAttack(normalizeAngle(yaw+(P_Random()-P_Random())*DOOM_SPREAD_UNIT),pitch,
			   F(2048),damage,0);
    }
}

/* 35 Hz, doom_playerTic, the loaded player wearing a monster (instead of the psprites) */
void doom_roleTic(void)
{const DoomRoleInfo *ri=doomRoleInfo(doomRoleMt[mpCur]);
 if (!ri)
    return;
 if (doomRole.hurt)
    doomRole.hurt--;
 if (doomRole.atk>=0 && ++doomRole.atk>ri->cool)
    doomRole.atk=-1;
 if (doomRole.dash)
    {/* the lost soul flies straight at SKULLSPEED until it meets something (or runs out) */
     int an=playerAngle.yaw;
     doomRole.dash--;
     doomPlayer.momx=MTH_Mul(DOOM_SKULLSPEED,-MTH_Sin(an));
     doomPlayer.momz=MTH_Mul(DOOM_SKULLSPEED,MTH_Cos(an));
     camera->vel.x=MTH_Mul(doomPlayer.momx,DOOM_VEL_SCALE);
     camera->vel.z=MTH_Mul(doomPlayer.momz,DOOM_VEL_SCALE);
     if (doom_playerLineAttack(an,0,F(40),(P_Random()%ri->meleeMod+1)*ri->meleeMul,1))
	{doomRole.dash=0;
	 doomPlayer.momx=doomPlayer.momz=0;
	 camera->vel.x=camera->vel.z=0;
	}
    }
 if (doomRole.cool)
    {doomRole.cool--;
     return;
    }
 if (!doomPlayer.fire)
    return;
 doomRoleAttack(ri);
 doomRole.cool=ri->cool;
 doomRole.atk=0;
}

/* doom_playerDamage, the loaded player wearing a monster and still alive: its pain */
void doom_rolePain(void)
{const DoomMobjInfo *info=&doomMobjInfo[doomRoleMt[mpCur]];
 if (P_Random()<info->painchance)
    {doomRole.hurt=6;
     if (info->painsound)
	doom_sound(camera,info->painsound);
    }
}

/* --- the body the others see ---------------------------------------------------------------- */

/* the state `t` tics into the chain that starts at `st` (0 = S_NULL: nothing to draw) */
static int doomChainAt(int st,int t)
{int n,tics;
 for (n=0;n<32 && st>0;n++)
    {tics=doomStates[st].tics;
     if (tics<0 || t<tics)
	break;
     t-=tics;
     st=doomStates[st].nextstate;
    }
 return st;
}

/* doom_playerBodySeq for a player k wearing a monster: its death, its attack, its pain, its run,
   its stand -- the same frames an AI monster would show */
short doom_roleBodySeq(int k,Sprite *body,Sprite *viewer,int health)
{const DoomMobjInfo *info=&doomMobjInfo[doomRoleMt[k]];
 DoomRole r;
 int st,frame,view,saved,seq;
 mpPeek(k,&doomRole,sizeof(r),&r);
 if (health<=0)
    st=doomChainAt(info->deathstate,(doomLevelTime-r.dieTic)&0x7fff);
 else if (r.atk>=0)
    st=doomChainAt((r.melee && info->meleestate)? info->meleestate:
		   (info->missilestate? info->missilestate: info->meleestate),r.atk);
 else if (r.hurt && info->painstate)
    st=info->painstate;
 else if (body->vel.x || body->vel.z)
    st=doomChainAt(info->seestate,doomLevelTime&63);
 else
    st=info->spawnstate;
 if (st<=0)
    return -1;
 frame=doomStates[st].frame&0x7fff;
 saved=body->angle;
 body->angle=normalizeAngle(body->angle+F(90));
 view=getFacingAngle(body,viewer);
 body->angle=saved;
 seq=doom_seq(doomStates[st].sprite,frame,view);
 if (seq<0 || seq>=level_nmSequences || level_sequence[seq+1]==level_sequence[seq])
    return -1;
 return (short)seq;
}

/* --- deaths, frags, limits, the crown ------------------------------------------------------- */

/* doom_playerDamage, the loaded player just died.  source: a player, a monster, or NULL (the
   world: nukage, a fall) -- Doom's P_KillMobj, which takes a frag for a suicide and none for a
   monster's kill. */
void doom_playerKilled(Object *source)
{int victim=mpCur,killer,mt=doomRoleMt[victim];
 char msg[32];
 killer=mpIndexOfObject(source);
 if (source && killer<0)
    killer=-2;                          /* a monster */
 mpScoreDeath(victim,killer);
 if (mt)
    {doomRole.dieTic=(short)doomLevelTime;
     doomRole.atk=-1;
     doomRole.dash=0;
     if (doomMobjInfo[mt].deathsound)
	doom_sound(camera,doomMobjInfo[mt].deathsound);
     if (killer>=0 && killer!=victim && !doomRoleMt[killer])
	mpStat[killer].kills++;         /* a marine killed a monster: it counts as one */
    }
 if (mpPlayers>1 && killer>=0 && killer!=victim)
    {int prev=mpBegin(killer);
     sprintf(msg,"YOU FRAGGED PLAYER %d",victim+1);
     doom_setMessage(msg);
     mpEnd(prev);
     sprintf(msg,"PLAYER %d FRAGGED YOU",killer+1);
     doom_setMessage(msg);
    }
 if (mpMode==MP_BOSS && mt)
    {doomCrownHeir=(killer>=0 && killer!=victim)? killer: -1;
     doomCrownTics=DOOM_CROWN_TICS;
    }
}

/* the side with the most frags, and that count (MPLAYER.H mpSide) */
static int doomBestFrags(void)
{int side[MPMAX+2],k,best=-0x7fff;
 for (k=0;k<MPMAX+2;k++)
    side[k]=0;
 for (k=0;k<mpPlayers;k++)
    side[mpSide(k)]+=mpStat[k].frags;
 for (k=0;k<MPMAX+2;k++)
    if (side[k]>best)
       best=side[k];
 return best;
}

/* 35 Hz, once (doom_playerTic of player 1): the round's limits, the crown */
void doom_modesTic(void)
{if (mpPlayers<2 || doom_exiting())
    return;
 if (mpCompetitive() &&
     ((mpTimeLimit && doomLevelTime>=mpTimeLimit*60*35) ||
      (mpFragLimit && doomBestFrags()>=mpFragLimit)))
    {doom_endRound();
     return;
    }
 if (doomCrownTics && !--doomCrownTics)
    {int k,heir=doomCrownHeir,prev=mpCur,n=0;
     char msg[32];
     if (heir<0 || mpPeekInt(heir,&currentState.health)<=0)
	for (heir=-1,k=0;k<mpPlayers;k++)  /* no killer standing: a living player at random */
	   if (!doomRoleMt[k] && mpPeekInt(k,&currentState.health)>0 && !(getNextRand()%(++n)))
	      heir=k;
     if (heir<0)
	{doomCrownTics=1;               /* everybody down: the next tic asks again */
	 return;
	}
     mpSwitch(heir);
     doomBossMorph();
     mpSwitch(prev);
     sprintf(msg,"PLAYER %d IS THE BOSS",heir+1);
     doomTellOthers(heir,msg);
    }
}

/* --- the end of the level ------------------------------------------------------------------- */

/* SRUINS.C runLevel (CFG_LEVEL_END): an exit to a level, or the episode's end, shows the score */
void doom_levelEnd(int action)
{char title[24];
 if (action<200 && action!=2)
    return;
 sprintf(title,"%s FINISHED",doom_levelLabel(currentState.currentLevel));
 mpIntermission(title,doomLevelTime/35,0);
}

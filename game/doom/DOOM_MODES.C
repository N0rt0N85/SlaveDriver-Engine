/* DOOM_MODES.C -- the multiplayer modes (MPLAYER.H mpMode) on the Doom runtime: the monster a
 * player drives (DEMONS: one of the level's, taken over; BOSS BATTLE: the level's boss, as many
 * as it holds), its attacks and its body as the others see it; frags, the frag and time limits,
 * the boss's crown; the level's placement per mode and the end-of-level score.  The engine keeps
 * the sides, the spawn spots and the table (MPLAYER.C); this file says what a role is.
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
#define DOOM_BOSS_HP_ADD    700        /* a lone boss: + 700 per marine past the first */
#define DOOM_CROWN_TICS     70         /* 2 s between a boss's death and the next boss */
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

/* BOSS BATTLE: a fallen boss (mpRole still 1) hands its crown on after DOOM_CROWN_TICS */
static short doomCrownTics[MPMAX];
static signed char doomCrownHeir[MPMAX];
/* BOSS BATTLE: where the level's bosses stand (doom_modePlace), the boss comes in there */
#define DOOM_MAXBOSSSPOTS 4
static struct {MthXyz feet; short sector,yaw;} doomBossSpot[DOOM_MAXBOSSSPOTS];
static int doomNmBossSpots;

void doom_modesLevelReset(void)
{doomNmBossSpots=0;
 doom_hordeLevelReset();                /* HORDE: the families of the LAST level are not this one's */
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

/* DEMONS: the monster the loaded player would take over -- the nearest at full health, or else
   the one with the most health left (the nearest of those).  Never the level's boss (its death
   opens the way out), never one standing on a floor that hurts (E1M8's last room, which also ends
   the level), never `skip` (the one just left).  NULL = the level has none left. */
static DoomActor *doomPickMonster(DoomActor *skip)
{Object *o;
 DoomActor *a,*pick=NULL;
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
 return pick;
}

/* Takes it over: the camera moves to where it stood and the monster goes.  1 = done. */
static int doomTakeOver(DoomActor *skip)
{DoomActor *pick=doomPickMonster(skip);
 MthXyz pos;
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

/* The previous-weapon button (Y) of a player wearing a monster (doom_weaponNext): the monster it
   leaves goes back to its own mind where it stands, with the health it has, and the player takes
   over the next one by the same rule -- never straight back into the one it left. */
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

/* The next-weapon button (Z) of a player wearing a monster with two attacks -- the imp, the
   cacodemon, the baron: a blow and a missile.  Doom picks by range (A_Chase: melee in reach,
   else the missile); a player picks, and keeps it until the button again. */
void doom_roleNextAttack(void)
{const DoomRoleInfo *ri=doomRoleInfo(doomRoleMt[mpCur]);
 if (!ri || !ri->meleeMod || ri->missile<0 || currentState.health<=0)
    return;
 doomRole.alt^=1;
 doom_setMessage(!doomRole.alt? "FIREBALL": ri->hitSfx==sfx_claw? "CLAW": "BITE");
}

/* BOSS BATTLE: the players who are a boss, or are about to be one again (a crown on its way) */
static int doomBosses(void)
{int k,n=0;
 for (k=0;k<mpPlayers;k++)
    n+=(mpRole[k]!=0);
 return n;
}

/* the most bosses at once: as many as the level holds, one at least */
static int doomBossCap(void)
{int n=doom_bossCount(currentState.currentLevel);
 return (n>1)? n: 1;
}

/* The loaded player becomes the level's boss (mpRole already set).  A lone boss grows with the
   marines it faces; several bosses keep the monster's own health. */
static void doomBossMorph(void)
{int mt=doom_levelBossMt(),bosses=doomBosses(),marines=mpPlayers-bosses,hp;
 if (!doomRoleInfo(mt))
    mt=MT_BRUISER;
 hp=doomMobjInfo[mt].spawnhealth;
 if (bosses<=1 && marines>1)
    hp+=DOOM_BOSS_HP_ADD*(marines-1);
 doomBecome(mt,hp);
 doom_setMessage(bosses>1? "YOU ARE A BOSS": "YOU ARE THE BOSS");
}

/* A boss comes in where one of the level's bosses stands, in its arena: the stand farthest from
   the other bosses (the first: one at random) */
static void doomBossPlace(void)
{MthXyz pos;
 int n,i,j,start,best=0;
 Fixed32 d,nearest,far=-1;
 if (!doomNmBossSpots)
    return;
 start=getNextRand()%doomNmBossSpots;
 for (n=0;n<doomNmBossSpots;n++)
    {i=(start+n)%doomNmBossSpots;
     nearest=0x7fffffff;
     for (j=0;j<mpPlayers;j++)
	if (j!=mpCur && mpRole[j] && mpBody[j])
	   {d=abs(mpBody[j]->pos.x-doomBossSpot[i].feet.x)+abs(mpBody[j]->pos.z-doomBossSpot[i].feet.z);
	    if (d<nearest)
	       nearest=d;
	   }
     if (nearest>far)
	{far=nearest;
	 best=i;
	}
    }
 i=best;
 pos=doomBossSpot[i].feet;
 pos.y+=F(GP_PLAYER_RADIUS+GP_PLAYER_EYE_HOVER);
 moveSpriteTo(camera,doomBossSpot[i].sector,&pos);
 camera->vel.x=camera->vel.y=camera->vel.z=0;
 doomPlayer.momx=doomPlayer.momz=0;
 playerAngle.yaw=((int)doomBossSpot[i].yaw)<<16;
 camera->angle=playerAngle.yaw;
}

/* the message of every player who is no boss */
static void doomTellMarines(const char *msg)
{int k,prev=mpCur;
 for (k=0;k<mpPlayers;k++)
    if (!mpRole[k])
       {mpSwitch(k);
	doom_setMessage(msg);
       }
 mpSwitch(prev);
}

/* --- level start, respawn, placement -------------------------------------------------------- */

/* "PLAYER 2 IS THE BOSS", "PLAYERS 2 AND 4 ARE THE BOSSES" */
static void doomNameBosses(char *msg)
{int k,i,n=0,last=0;
 for (k=0;k<mpPlayers;k++)
    if (mpRole[k])
       {n++;
	last=k;
       }
 strcpy(msg,(n>1)? "PLAYERS": "PLAYER");
 for (k=0,i=0;k<mpPlayers;k++)
    if (mpRole[k])
       sprintf(msg+strlen(msg),"%s %d",!i++? "": (k==last)? " AND": ",",k+1);
 strcat(msg,(n>1)? " ARE THE BOSSES": " IS THE BOSS");
}

/* SRUINS.C runLevel, every player built (CFG_MP_LEVELSTART): the roles of the mode */
void doom_mpLevelStart(void)
{int k,n,prev=mpCur;
 char msg[40];
 for (k=0;k<MPMAX;k++)
    doomCrownTics[k]=0;                  /* a crown on its way stays with its boss: mpRole */
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
     /* the bosses chosen on the title's screen (MPRULES.C), a crown's moves since; nobody: one
	at random; never more than the level holds, never nobody left to fight them */
     for (k=0,n=0;k<mpPlayers;k++)
	if (mpRole[k] && (++n>doomBossCap() || n==mpPlayers))
	   mpRole[k]=0;
     if (!doomBosses())
	mpRole[getNextRand()%mpPlayers]=1;
     for (k=0;k<mpPlayers;k++)
	if (mpRole[k])
	   {mpSwitch(k);
	    doomBossMorph();
	    doomBossPlace();
	   }
     doomNameBosses(msg);
     doomTellMarines(msg);
    }
 mpSwitch(prev);
 doom_hordeLevelStart();
}

/* SRUINS.C mpRespawn (CFG_MP_RESPAWNED), player k loaded, the marine's kit given: a demon takes
   another monster (none left: it stays down, and fire asks again); a fallen boss comes back as
   a marine -- its crown goes to its killer */
/* CFG_MP_RESPAWN_HOLD (SRUINS.C mpRespawn), BEFORE the player is moved: 1 = leave it where it
   fell.  A demon with no monster left to take over stays dead on the corpse -- respawning it
   would stand its body at a spawn spot with nothing to wear. */
int doom_mpRespawnHold(int k)
{if (mpMode==MP_HORDE)
    return 1;                           /* survival: one life a player, and no way back */
 if (mpMode!=MP_MONSTERS || !mpRole[k] || doomPickMonster(NULL))
    return 0;
 doom_setMessage("NO MONSTER LEFT TO TAKE OVER");
 return 1;
}

void doom_mpRespawned(int k)
{doomBecome(0,0);
 if (mpMode==MP_MONSTERS && mpRole[k] && !doomTakeOver(NULL))
    {currentState.health=0;
     doomPlayer.health=0;
     doom_setMessage("NO MONSTER LEFT TO TAKE OVER");
    }
}

/* SRUINS.C mpPollStart (CFG_MP_JOINED), a newcomer built as a marine: in the boss battle it is
   a boss if two marines are in already and the level holds one more boss than are played */
void doom_mpJoined(int k)
{int j,marines=0,prev=mpCur;
 char msg[40];
 if (mpMode!=MP_BOSS)
    return;
 for (j=0;j<mpPlayers;j++)
    if (j!=k && !mpRole[j])
       marines++;
 if (marines<2 || doomBosses()>=doomBossCap())
    return;
 mpRole[k]=1;
 mpSwitch(k);
 doomBossMorph();
 doomBossPlace();
 mpSwitch(prev);
 sprintf(msg,"PLAYER %d IS A BOSS",k+1);
 doomTellMarines(msg);
}

/* DOOM_GAME.C game_placeObject, before a mobj is spawned: 1 = this mode does not place it.  A
   fighting game has no monsters -- their places become the spawn spots -- and no MF_NOTDMATCH
   thing (the keys).  The lost soul is no COUNTKILL: named, as Doom's nomonsters does.  The
   score's totals count what is placed. */
int doom_modePlace(int mt,int sector,MthXyz *pos,int angle,int thingFlags)
{int flags=doomMobjInfo[mt].flags;
 static const unsigned char skillBit[4]={1,1,2,4};    /* P_SpawnMapThing: easy and baby share */
 /* HORDE, before the skill filter and on its own: EVERY monster thing is a spawn spot and a
    family, whatever skill it was placed for.  The .LEV carries ULTRA-VIOLENCE's things and the
    skill only sieves them here, so the sprites of the sieved ones ARE in RAM -- and the mode
    would otherwise lose most of its map on the easy skills.  What the skill changes in a horde
    is the pressure (how many come, how fast), not where they come from (DOOM_HORDE.C). */
 if (mpMode==MP_HORDE)
    {doom_hordeSaw(mt);
     if ((flags & MF_COUNTKILL) || mt==MT_SKULL)
	{mpSpotAdd(sector,pos,angle-F(90));
	 return 1;
	}
    }
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
 Fixed32 pitch;
 int yaw=playerAngle.yaw,i,damage;
 doom_aimSlope(&pitch);
 doomRole.melee=0;
 if (ri->mt==MT_SKULL)
    {/* A_SkullAttack: a charge at SKULLSPEED; the hit is looked for along the way */
     doomRole.dash=DOOM_SKULLDASH;
     doomRole.melee=1;
     if (info->attacksound)
	doom_sound(camera,info->attacksound);
     return;
    }
 if (ri->meleeMod && (ri->missile<0 || doomRole.alt))
    {/* the blow chosen (doom_roleNextAttack), or a monster that only bites: its attacksound
	(A_Chase plays it on the way into the melee state), then the blow */
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
int doom_chainAt(int st,int t)
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
    st=doom_chainAt(info->deathstate,(doomLevelTime-r.dieTic)&0x7fff);
 else if (r.atk>=0)
    st=doom_chainAt((r.melee && info->meleestate)? info->meleestate:
		    (info->missilestate? info->missilestate: info->meleestate),r.atk);
 else if (r.hurt && info->painstate)
    st=info->painstate;
 else if (body->vel.x || body->vel.z)
    st=doom_chainAt(info->seestate,doomLevelTime&63);
 else
    st=info->spawnstate;
 if (st<=0)
    return -1;
 /* GCC14: FF_FULLBRIGHT, as on an AI monster's frame (DOOM_ACTOR.C doomSetSequence): a worn
    lost soul, a worn shotgun guy's muzzle flash keep their light in a dark leaf (WALLS.C
    drawSprites).  doom_playerBodySeq cleared the flag before it called this. */
 if (doomStates[st].flags & DOOM_SF_FULLBRIGHT)
    body->flags|=SPRITEFLAG_FULLBRIGHT;
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
    {doomCrownHeir[victim]=(signed char)((killer>=0 && killer!=victim && !mpRole[killer])? killer: -1);
     doomCrownTics[victim]=DOOM_CROWN_TICS;
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

/* The crown of the fallen boss v: to its killer, if it is a marine still standing; else to a
   living marine at random -- v itself (up again) only when nobody else is */
static void doomCrown(int v)
{int k,pass,heir=doomCrownHeir[v],prev=mpCur,n=0;
 char msg[40];
 if (heir<0 || mpRole[heir] || mpPeekInt(heir,&currentState.health)<=0)
    for (heir=-1,pass=0;pass<2 && heir<0;pass++)
       for (k=0;k<mpPlayers;k++)
	  if ((pass || k!=v) && (k==v || !mpRole[k]) && !doomRoleMt[k] &&
	      mpPeekInt(k,&currentState.health)>0 && !(getNextRand()%(++n)))
	     heir=k;
 if (heir<0)
    {doomCrownTics[v]=1;                /* everybody down: the next tic asks again */
     return;
    }
 mpRole[v]=0;
 mpRole[heir]=1;
 mpSwitch(heir);
 doomBossMorph();
 mpSwitch(prev);
 sprintf(msg,"PLAYER %d IS %s BOSS",heir+1,(doomBosses()>1)? "A": "THE");
 doomTellMarines(msg);
}

/* 35 Hz, once (doom_playerTic of player 1): the round's limits, the crowns */
void doom_modesTic(void)
{int k;
 doom_hordeTic();                       /* HORDE runs with one player too: before the guard */
 if (mpPlayers<2 || doom_exiting())
    return;
 if (mpCompetitive() &&
     ((mpTimeLimit && doomLevelTime>=mpTimeLimit*60*35) ||
      (mpFragLimit && doomBestFrags()>=mpFragLimit)))
    {doom_endRound();
     return;
    }
 for (k=0;k<mpPlayers;k++)
    if (doomCrownTics[k] && !--doomCrownTics[k])
       doomCrown(k);
}

/* --- the end of the level ------------------------------------------------------------------- */

/* SRUINS.C runLevel (CFG_LEVEL_END): an exit to a level, or the episode's end, shows the score */
void doom_levelEnd(int action)
{char title[24];
 if (action<200 && action!=2)
    return;
 if (mpMode==MP_HORDE)
    sprintf(title,"%d WAVES SURVIVED",doom_hordeWave());
 else
    sprintf(title,"%s FINISHED",doom_levelLabel(currentState.currentLevel));
 mpIntermission(title,doomLevelTime/35,0);
}

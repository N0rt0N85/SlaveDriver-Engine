/* DOOM_PLAYER.C -- the Doom player: 35 Hz integrator, health/armour/death, pickups, selection.
 * SPEC_PLAYER sections 1.2, 1.3, 2.6, 4.3; SPEC_RUNTIME section 1 (the single clock).
 *
 * Doom state (momx/momz, yaw, psprites, counters) advances at 35 Hz in doom_playerTic, called
 * by the tic loop (CFG_PLAYER_TIC, SRUINS.C:2143) BEFORE runObjects() -- P_Ticker's order.  The
 * position advances at 60 Hz in the engine's movePlayer: doom_playerFrame (CFG_CONTROL,
 * SRUINS.C:960) stores the input and writes camera->vel = mom * 35/60 before moveCamera(); the
 * tic reads mom back from camera->vel, so a wall that zeroed a component is seen on the Doom
 * side (P_SlideMove's effect).  internal_moveSprite adds vel once per frame (SPRITE.C:459-467,
 * non-PAL build) and doFriction is inert with friction = F(1).
 *
 * Doom references [doom]: g_game.c:158-160 (forwardmove/sidemove/angleturn), :362-391 (turnheld,
 * strafe), p_user.c:52-60 (P_Thrust), :81-88 (bob), :141-160 (P_MovePlayer), :182-217
 * (P_DeathThink), p_mobj.c:106-107, 132-140, 216-219 (FRICTION, MAXMOVE, STOPSPEED),
 * p_inter.c:60-260 (give*), :335-662 (P_TouchSpecialThing), :781-926 (P_DamageMobj player). */
#include "util.h"
#include <sega_per.h>               /* PER_DGT_* (after util.h: sega_xpt.h types) */
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
#include "mplayer.h"

DoomPlayer doomPlayer;
int doomViewBob;                        /* P_CalcHeight's bob, added to the view (CFG_VIEW_BOB)    */

/* --- constants ------------------------------------------------------------------------------ */
#define DOOM_VEL_SCALE   38229          /* 35/60: camera->vel (u/frame) = mom (u/tic) * this     */
#define DOOM_VEL_INV     112347         /* 60/35: mom = vel * this (MTH_Mul, no division)         */
#define DOOM_GRAVITY_FR  22301          /* 1 u/tic^2 * (35/60)^2, u/frame^2                       */
#define DOOM_MAXMOVE     F(30)          /* p_mobj.c MAXMOVE, per axis                             */
#define DOOM_STOPSPEED   0x1000
#define DOOM_FRICTION    0xE800         /* 0.90625                                                */
#define DOOM_SLOWTURNTICS 6
#define DOOM_MAXBOB      F(16)          /* MAXBOB 0x100000                                        */
#define DOOM_MAXHEALTH   100
#define DOOM_BONUSADD    6
#define DOOM_ANG5        F(5)
#define DOOM_OOF_VEL     (-8*DOOM_VEL_SCALE)   /* momz < -8 u/tic (p_mobj.c:324) in u/frame       */

/* g_game.c:158-160: forwardmove/sidemove are 16.16 * 2048 in P_Thrust (u/tic); angleturn is in
   1<<16 of a 2^32 turn = 360/65536 degrees = 360 engine units (degrees << 16 / 65536) */
static const int doomForwardMove[2]={0x19,0x32};
static const int doomSideMove[2]={0x18,0x28};
static const int doomAngleTurn[3]={640,1280,320};
#define DOOM_TURN_UNIT   360

/* p_inter.c:52-53 */
static const int doomMaxAmmo[DOOM_NUMAMMO]={200,50,300,50};
static const int doomClipAmmo[DOOM_NUMAMMO]={10,4,20,1};

/* G_NextWeapon order (chocolate g_game.c weapon_order_table) */
static const signed char doomWeaponOrder[9]=
{wp_fist,wp_chainsaw,wp_pistol,wp_shotgun,wp_supershotgun,wp_chaingun,wp_missile,wp_plasma,wp_bfg};

/* d_englsh.h GOT* (SPEC_PLAYER section 3.6 puts them in DOOM_GAME.C; they live next to the one
   function that uses them) */
static const char *GOTARMOR="Picked up the armor.";
static const char *GOTMEGA="Picked up the MegaArmor!";
static const char *GOTHTHBONUS="Picked up a health bonus.";
static const char *GOTARMBONUS="Picked up an armor bonus.";
static const char *GOTSTIM="Picked up a stimpack.";
static const char *GOTMEDINEED="Picked up a medikit that you REALLY need!";
static const char *GOTMEDIKIT="Picked up a medikit.";
static const char *GOTSUPER="Supercharge!";
static const char *GOTBLUECARD="Picked up a blue keycard.";
static const char *GOTYELWCARD="Picked up a yellow keycard.";
static const char *GOTREDCARD="Picked up a red keycard.";
static const char *GOTBLUESKUL="Picked up a blue skull key.";
static const char *GOTYELWSKUL="Picked up a yellow skull key.";
static const char *GOTREDSKULL="Picked up a red skull key.";
static const char *GOTCLIP="Picked up a clip.";
static const char *GOTCLIPBOX="Picked up a box of bullets.";
static const char *GOTROCKET="Picked up a rocket.";
static const char *GOTROCKBOX="Picked up a box of rockets.";
static const char *GOTCELL="Picked up an energy cell.";
static const char *GOTCELLBOX="Picked up an energy cell pack.";
static const char *GOTSHELLS="Picked up 4 shotgun shells.";
static const char *GOTSHELLBOX="Picked up a box of shotgun shells.";
static const char *GOTBACKPACK="Picked up a backpack full of ammo!";
static const char *GOTBFG9000="You got the BFG9000!  Oh, yes.";
static const char *GOTCHAINGUN="You got the chaingun!";
static const char *GOTCHAINSAW="A chainsaw!  Find some meat!";
static const char *GOTLAUNCHER="You got the rocket launcher!";
static const char *GOTPLASMA="You got the plasma gun!";
static const char *GOTSHOTGUN="You got the shotgun!";
static const char *GOTSHOTGUN2="You got the super shotgun!";

/* --- init ----------------------------------------------------------------------------------- */

/* Set by an exit (doom_playerFinishLevel), read once by the next doom_playerInit. */
static char doomCarry;
/* A_Saw hit its target: the next tic's move is the saw's pull (doomMoveTic) */
static char doomSawPull;

void doom_playerSawPull(void)
{doomSawPull=1;
}

/* G_PlayerFinishLevel (g_game.c), from DOOM_GAME.C's exit towards a next level: keys and powers
   stay behind, the rest of the arsenal goes on. */
void doom_playerFinishLevel(void)
{int k,prev=mpCur;
 /* co-op: every player carries its arsenal, except one lying dead -- Doom reborns it with the
    pistol kit at the next level (G_DoReborn).  Solo cannot exit dead: unchanged. */
 for (k=0;k<mpPlayers;k++)
    {mpSwitch(k);
     doomCarry=(currentState.health>0);
     doomPlayer.keys=0;
    }
 mpSwitch(prev);
}

/* GCC14: local multiplayer (MPLAYER.H).  The Doom player, every copy of it per player: its
   struct, the view bob, and the three flags of this file; the weapon and HUD files register
   theirs. */
void doom_mpRegister(void)
{MPREG(doomPlayer); MPREG(doomViewBob);
 MPREG(doomCarry); MPREG(doomSawPull);
 doom_weaponMpRegister();
 doom_hudMpRegister();
}

/* a player with nothing to carry: the next doom_playerInit gives it Doom's starting kit */
void doom_mpNewPlayer(void)
{doomCarry=0;
}

/* Every level (SRUINS.C:2010 after initWeapon(), CFG_LEVEL_PLAYER_INIT): P_SpawnPlayer +
   P_SetupPsprites, and G_PlayerReborn unless the level before was left by an exit.  A new game
   and a restart after death start as Doom's single player does -- pistol, 50 bullets, 100 health,
   no armour; an exit keeps health, armour, weapons, ammo and backpack (seen on console 09-18:
   every level started with the pistol).  Health itself is currentState.health (the stat bar and
   the death branch read it): the engine's new game sets 200 (nmBowls*200, SRUINS.C:2531) and a
   restart puts back the value the level started with (main, `levStart`), so a reborn player is
   set to 100 here. */
void doom_playerInit(void)
{int i;
 assert(camera);
 if (!doomCarry)
    {currentState.health=DOOM_MAXHEALTH;       /* G_PlayerReborn */
     doomPlayer.armorPoints=0;
     doomPlayer.armorType=0;
     for (i=0;i<DOOM_NUMAMMO;i++)
	{doomPlayer.ammo[i]=0;
	 doomPlayer.maxAmmo[i]=doomMaxAmmo[i];
	}
     doomPlayer.ammo[am_clip]=50;
     doomPlayer.weaponOwned=(1<<wp_fist)|(1<<wp_pistol);
     doomPlayer.readyWeapon=wp_pistol;
     doomPlayer.backpack=0;
    }
 doomCarry=0;
 doomSawPull=0;
 doomPlayer.momx=doomPlayer.momz=0;
 doomPlayer.turnHeld=0;
 doomPlayer.health=currentState.health;
 doomPlayer.keys=0;
 doomPlayer.pendingWeapon=doomPlayer.readyWeapon;   /* P_SetupPsprites: pendingweapon = readyweapon */
 doomPlayer.refire=0;
 doomPlayer.attackDown=0;
 doomPlayer.muzzleTics=0;              /* lightInit() already emptied the engine list */
 doom_missileLightsReset();
 doomPlayer.damageCount=0;
 doomPlayer.bonusCount=0;
 for (i=0;i<DOOM_NUMPSPRITES;i++)
    {doomPlayer.pspState[i]=0;
     doomPlayer.pspTics[i]=0;
    }
 doomPlayer.bob=0;
 doomViewBob=0;
 doomPlayer.pspSx=F(1);
 doomPlayer.pspSy=F(32);              /* WEAPONTOP */
 doomPlayer.input=0xffff;             /* IMASK is active low: nothing pressed */
 doomPlayer.pushed=0;
 doomPlayer.fire=0;
 doomPlayer.extralight=0;
 doomPlayer.attacker=NULL;
 doomPlayer.lastFloor=camera->floorSector;
 doomPlayer.lastVelY=0;
 /* the engine's player physics are switched off: no friction (SPRITE.C:481), Doom gravity */
 camera->friction=F(1);
 camera->gravity=DOOM_GRAVITY_FR;
 camera->vel.x=0;
 camera->vel.z=0;
 playerAngle.pitch=0;
 playerAngle.roll=0;
 doom_bringUpWeapon();
}

/* --- 60 Hz ---------------------------------------------------------------------------------- */

/* CFG_CONTROL (SRUINS.C:960), only while the player is alive and not stunned: remember the input
   (edges are OR-ed until the tic consumes them: a press shorter than a tic still fires), lock the
   view, landing sound, and camera->vel = mom * 35/60 for the moveCamera() that follows.  yavel and
   xavel are never written under GP_GAME_DOOM, so the engine's angle update (SRUINS.C:1026-1037)
   and weaponPlayerMove (:1043) are inert. */
void doom_playerFrame(unsigned short input,unsigned short pushed)
{assert(camera);
 doomPlayer.input=input;
 doomPlayer.pushed|=pushed;
 playerAngle.pitch=0;
 playerAngle.roll=0;
 /* P_ZMovement p_mobj.c:324: hit the floor faster than 8 u/tic => sfx_oof */
 if (camera->floorSector!=-1 && doomPlayer.lastFloor==-1 && doomPlayer.lastVelY<DOOM_OOF_VEL)
    doom_playerSound(sfx_oof);
 doomPlayer.lastFloor=camera->floorSector;
 doomPlayer.lastVelY=camera->vel.y;
 camera->friction=F(1);
 /* No clipping: no floor under the camera, so gravity is off. */
 camera->gravity=noClipCheat?0:DOOM_GRAVITY_FR;
 if (noClipCheat)
    camera->vel.y=0;
 camera->vel.x=MTH_Mul(doomPlayer.momx,DOOM_VEL_SCALE);
 camera->vel.z=MTH_Mul(doomPlayer.momz,DOOM_VEL_SCALE);
}

/* --- 35 Hz ---------------------------------------------------------------------------------- */

/* P_Thrust (p_user.c:52-60) in the engine frame: forward = (-sin yaw, cos yaw), right =
   (cos yaw, sin yaw) -- the same vectors as controlInput SRUINS.C:509-518 / 544-556 */
static void doomThrust(int forward,Fixed32 move)
{if (forward)
    {doomPlayer.momx+=MTH_Mul(move,-MTH_Sin(playerAngle.yaw));
     doomPlayer.momz+=MTH_Mul(move,MTH_Cos(playerAngle.yaw));
    }
 else
    {doomPlayer.momx+=MTH_Mul(move,MTH_Cos(playerAngle.yaw));
     doomPlayer.momz+=MTH_Mul(move,MTH_Sin(playerAngle.yaw));
    }
}

/* G_BuildTiccmd + P_MovePlayer + P_XYMovement (player part) for one tic */
static void doomMoveTic(void)
{unsigned short input=doomPlayer.input;
 int run,tspeed,turning,forward,side,onground;
 Fixed32 turn;
 /* read back what the engine's collisions left of the last tic's velocity */
 doomPlayer.momx=MTH_Mul(camera->vel.x,DOOM_VEL_INV);
 doomPlayer.momz=MTH_Mul(camera->vel.z,DOOM_VEL_INV);
 onground=(camera->floorSector!=-1);

 /* run = the JUMP slot, held (C, doom_init: Mimas's KEY_RSHIFT button).  It used to be R, which
    is also strafe right, so running straight ahead was impossible: Doom's walking speed and
    turn rate only (8.3 u/tic, 3.5 degrees/tic). */
 run=!(input & IMASK(ACTION_JUMP));
 turning=0;
 forward=0;
 side=0;
 if (!(input & PER_DGT_R))
    turning-=1;
 if (!(input & PER_DGT_L))
    turning+=1;
 if (!(input & PER_DGT_U))
    forward+=doomForwardMove[run];
 if (!(input & PER_DGT_D))
    forward-=doomForwardMove[run];
 if (!(input & IMASK(ACTION_RUN)))
    side+=doomSideMove[run];
 if (!(input & IMASK(ACTION_STRAFE)))
    side-=doomSideMove[run];
 if (forward>doomForwardMove[1])
    forward=doomForwardMove[1];
 if (forward<-doomForwardMove[1])
    forward=-doomForwardMove[1];
 if (side>doomForwardMove[1])
    side=doomForwardMove[1];
 if (side<-doomForwardMove[1])
    side=-doomForwardMove[1];
 /* P_PlayerThink: MF_JUSTATTACKED, set by the chainsaw on a hit -- this tic no turn, no strafe,
    forwardmove 0xc800/512 = 100 (twice the run): the saw pulls the player into what it cuts */
 if (doomSawPull)
    {turning=0;
     forward=100;
     side=0;
     doomSawPull=0;
    }

 /* turning without inertia: 3.5 degrees per tic, 7 running, 1.75 the first 6 tics */
 if (turning)
    doomPlayer.turnHeld++;
 else
    doomPlayer.turnHeld=0;
 if (doomPlayer.turnHeld<DOOM_SLOWTURNTICS)
    tspeed=2;
 else
    tspeed=run;
 turn=turning*doomAngleTurn[tspeed]*DOOM_TURN_UNIT;
 playerAngle.yaw=normalizeAngle(playerAngle.yaw+turn);

 /* P_XYMovement tail for the PREVIOUS move: stop below STOPSPEED without a command, else
    friction; none of it airborne (p_mobj.c:200-219) */
 if (onground)
    {if (doomPlayer.momx>-DOOM_STOPSPEED && doomPlayer.momx<DOOM_STOPSPEED &&
	 doomPlayer.momz>-DOOM_STOPSPEED && doomPlayer.momz<DOOM_STOPSPEED &&
	 forward==0 && side==0)
	{doomPlayer.momx=0;
	 doomPlayer.momz=0;
	}
     else
	{doomPlayer.momx=MTH_Mul(doomPlayer.momx,DOOM_FRICTION);
	 doomPlayer.momz=MTH_Mul(doomPlayer.momz,DOOM_FRICTION);
	}
    }
 /* P_MovePlayer: thrust only on the ground */
 if (forward && onground)
    doomThrust(1,forward*2048);
 if (side && onground)
    doomThrust(0,side*2048);
 /* P_XYMovement head: MAXMOVE per axis */
 if (doomPlayer.momx>DOOM_MAXMOVE)
    doomPlayer.momx=DOOM_MAXMOVE;
 else if (doomPlayer.momx<-DOOM_MAXMOVE)
    doomPlayer.momx=-DOOM_MAXMOVE;
 if (doomPlayer.momz>DOOM_MAXMOVE)
    doomPlayer.momz=DOOM_MAXMOVE;
 else if (doomPlayer.momz<-DOOM_MAXMOVE)
    doomPlayer.momz=-DOOM_MAXMOVE;
 camera->vel.x=MTH_Mul(doomPlayer.momx,DOOM_VEL_SCALE);
 camera->vel.z=MTH_Mul(doomPlayer.momz,DOOM_VEL_SCALE);

 /* P_CalcHeight p_user.c:81-88: the weapon bob amplitude */
 doomPlayer.bob=(MTH_Mul(doomPlayer.momx,doomPlayer.momx)+
		 MTH_Mul(doomPlayer.momz,doomPlayer.momz))>>2;
 if (doomPlayer.bob>DOOM_MAXBOB)
    doomPlayer.bob=DOOM_MAXBOB;
 /* P_CalcHeight: the view rides bob/2 * sin(FINEANGLES/20 * leveltime), a 20-tic period, up to
    +-8 u; none airborne.  Without it walking reads as skating. */
 if (onground)
    doomViewBob=-MTH_Mul(doomPlayer.bob>>1,
			 MTH_Sin(normalizeAngle(F((doomLevelTime*18)%360))));
 else
    doomViewBob=0;
}

/* P_DeathThink p_user.c:182-217: turn towards the killer 5 degrees per tic, fade the flash once
   looking at it.  The camera drop is the engine's own (stepPlayerHeight, SRUINS.C:214-234). */
static void doomDeathTic(void)
{Object *a=doomPlayer.attacker;
 Sprite *as=NULL;
 if (a && a!=(Object *)player && a->type!=OT_DEAD && a->func==game_actor_func)
    as=((DoomActor *)a)->sprite;
 if (as)
    {int angle=normalizeAngle(getAngle(as->pos.x-camera->pos.x,as->pos.z-camera->pos.z)-F(90));
     int delta=normalizeAngle(angle-playerAngle.yaw);
     if (delta<DOOM_ANG5 && delta>-DOOM_ANG5)
	{playerAngle.yaw=angle;
	 if (doomPlayer.damageCount)
	    doomPlayer.damageCount--;
	}
     else if (delta>0)
	playerAngle.yaw=normalizeAngle(playerAngle.yaw+DOOM_ANG5);
     else
	playerAngle.yaw=normalizeAngle(playerAngle.yaw-DOOM_ANG5);
    }
 else if (doomPlayer.damageCount)
    doomPlayer.damageCount--;
 doomViewBob=0;
 /* the death hop (SRUINS.C:970-972) would slide forever with friction F(1) */
 camera->vel.x=MTH_Mul(camera->vel.x,DOOM_FRICTION);
 camera->vel.z=MTH_Mul(camera->vel.z,DOOM_FRICTION);
}

/* Doom's palette flash as a colour offset (SRUINS.C colorOffset, stepped by stepColorOffset
   after this tic): red = damagecount palette (cnt+7)>>3 of 8 levels (st_stuff.c:ST_doPaletteStuff),
   yellow = bonuscount (cnt+7)>>3 of 4, plus extralight.  Not while dead: the death branch drives
   colorCenter to -255 and runLevel waits for colorOffset to get there (SRUINS.C:2321). */
static void doomFlashTic(void)
{int red,bonus,r,g,b;
 red=(doomPlayer.damageCount+7)>>3;
 if (red>8)
    red=8;
 bonus=(doomPlayer.bonusCount+7)>>3;
 if (bonus>4)
    bonus=4;
 if (red)
    bonus=0;                                    /* ST_doPaletteStuff: the bonus only when no red */
 r=red*8+bonus*12+doomPlayer.extralight*12;
 g=-red*8+bonus*12+doomPlayer.extralight*12;
 b=-red*8+doomPlayer.extralight*12;
 if (r>63) r=63;
 if (r<-63) r=-63;
 if (g>63) g=63;
 if (g<-63) g=-63;
 if (b>63) b=63;
 if (b<-63) b=-63;
 /* nothing to show: leave colorOffset to stepColorOffset, which walks it to colorCenter (0) --
    rewriting 0 every tic would cut the level-start fade from black (SRUINS.C:1973, 1988) */
 if (r || g || b)
    changeColorOffset(r,g,b,3);
}

/* CFG_USE_REFUSED (SRUINS.C:853, push()): the use ray met a solid wall within reach that drives
   no object -- PTR_UseTraverse (p_map.c) says sfx_noway on a non-special line with no opening */
void doom_useRefused(void)
{if (currentState.health>0)
    doom_playerSound(sfx_noway);
}

/* --- test toggles (not in Doom) -----------------------------------------------------------------
   Same idiom as the engine chords (SRUINS.C): hold the chord, one flip per press, and a
   face-button alternative for analog-trigger pads.  Every L+R letter is taken by the engine,
   so these use the d-pad:
      L+R+UP     (A+B+X)   every implemented weapon, full ammo
      L+R+DOWN   (A+C+X)   invulnerability
      L+R+LEFT   (B+C+X)   no clipping
   L and R together cancel out; pad bits are active low. */
#define DOOM_CHEAT_WEAPONS ((1<<wp_fist)|(1<<wp_pistol)|(1<<wp_shotgun)|\
			    (1<<wp_chaingun)|(1<<wp_missile)|(1<<wp_plasma))
static char doomCheatGod;

/* True once per press of `chord` or `alt`; `held` latches until release. */
static int doomChord(unsigned short input,unsigned short chord,unsigned short alt,char *held)
{if (((~input)&chord)==chord || ((~input)&alt)==alt)
    {if (*held)
	return 0;
     *held=1;
     return 1;
    }
 *held=0;
 return 0;
}

static void doomCheatTic(void)
{static char weaponsHeld,godHeld,clipHeld;
 unsigned short input=doomPlayer.input;
 int i;
 if (doomChord(input,PER_DGT_TL|PER_DGT_TR|PER_DGT_U,PER_DGT_A|PER_DGT_B|PER_DGT_X,&weaponsHeld))
    {doomPlayer.weaponOwned=DOOM_CHEAT_WEAPONS;
     for (i=0;i<DOOM_NUMAMMO;i++)
	doomPlayer.ammo[i]=doomPlayer.maxAmmo[i];
     doom_setMessage("VERY HAPPY AMMO ADDED");
    }
 if (doomChord(input,PER_DGT_TL|PER_DGT_TR|PER_DGT_D,PER_DGT_A|PER_DGT_C|PER_DGT_X,&godHeld))
    {doomCheatGod=!doomCheatGod;
     doom_setMessage(doomCheatGod?"DEGREELESSNESS MODE ON":"DEGREELESSNESS MODE OFF");
    }
 if (doomChord(input,PER_DGT_TL|PER_DGT_TR|PER_DGT_L,PER_DGT_B|PER_DGT_C|PER_DGT_X,&clipHeld))
    {noClipCheat=!noClipCheat;
     doom_setMessage(noClipCheat?"NO CLIPPING MODE ON":"NO CLIPPING MODE OFF");
    }
}

/* P_PlayerInSpecialSector case 11 (E1M8's last room): `cheats &= ~CF_GODMODE`, or the level
   that ends at 10 health would never end */
void doom_playerGodOff(void)
{doomCheatGod=0;
}

/* P_PlayerThink for one 35 Hz tic (CFG_PLAYER_TIC, before runObjects()): level clock + nukage,
   death, movement, sector damage, fire button, psprites, counters, flash. */
void doom_playerTic(void)
{doom_sectorDamageTic();                       /* doomLevelTime++, OT_DOOM_DAMAGE */
 if (!camera)
    return;
 doomPlayer.fire=!(doomPlayer.input & IMASK(ACTION_FIRE)) ||
    (doomPlayer.pushed & IMASK(ACTION_FIRE));
 doomPlayer.pushed=0;
 doomCheatTic();
 doom_muzzleTic();                             /* before the psprites: the flash lives two tics */
 if (currentState.health<=0)
    {doomDeathTic();
     doom_psprTic();                            /* P_DeathThink calls P_MovePsprites: A_Lower */
     return;
    }
 doomMoveTic();
 doom_psprTic();
 if (doomPlayer.damageCount)
    doomPlayer.damageCount--;
 if (doomPlayer.bonusCount)
    doomPlayer.bonusCount--;
 doomFlashTic();
}

/* --- damage (P_DamageMobj player branch, p_inter.c:781-926) --------------------------------- */

/* The ONLY armour calculation: saved = damage/3 (type 1) or /2 (type 2), bounded by armorPoints
   (type reset); health -= damage (clamped at 0); damageCount += damage (cap 100); pain sound on
   (almost) every hit: painchance 255 => P_Random() < 255, one draw as Doom.  The death itself is
   the engine's branch (SRUINS.C:962-975, health <= 0: CFG_DEATH_SFX = PLDETH, camera drop) which
   runs once (switchPlayerMotion(0) takes movePlayer out of that branch the next frame). */
void doom_playerDamage(int damage,Object *source)
{int saved;
 if (damage<=0 || currentState.health<=0)
    return;
 if (doomCheatGod)
    return;
 if (doomPlayer.armorType)
    {saved=(doomPlayer.armorType==1)?damage/3:damage/2;
     if (doomPlayer.armorPoints<=saved)
	{saved=doomPlayer.armorPoints;
	 doomPlayer.armorType=0;
	}
     doomPlayer.armorPoints-=saved;
     damage-=saved;
    }
 currentState.health-=damage;
 if (currentState.health<0)
    currentState.health=0;
 doomPlayer.health=currentState.health;
 doomPlayer.attacker=source;
 doomPlayer.damageCount+=damage;
 if (doomPlayer.damageCount>100)
    doomPlayer.damageCount=100;
 if (currentState.health<=0)
    {/* P_KillMobj: P_DropWeapon lowers the gun at once (p_pspr.c:P_DropWeapon), even in the
	middle of an attack; the death itself is the engine branch (sound, camera drop).  The
	`tics -= P_Random()&3` of the death state is drawn for the RNG order only. */
     doom_setPsprite(DOOM_PS_WEAPON,doomWeaponInfo[(int)doomPlayer.readyWeapon].downstate);
     (void)P_Random();
     return;
    }
 if (P_Random()<255)
    doom_playerSound(sfx_plpain);               /* S_PLAY_PAIN -> A_Pain */
}

/* P_DamageMobj thrust (p_inter.c) on the player, called by doom_damage BEFORE the armour maths:
   mass 100 => damage/8 u/tic away from the inflictor (engine frame: getAngle(dx,dz), vel =
   (cos,sin), as doom_spawnMissile); the knock-down variant (damage < 40 and lethal, inflictor's
   feet more than 64 u below the player's, P_Random()&1) turns it round and x4.  Written into
   momx/momz AND camera->vel (DOOM_VEL_SCALE): a second tic of the same frame reads camera->vel
   back (doomMoveTic) and would otherwise lose it. */
void doom_playerThrust(Sprite *inflictor,int damage)
{int an;
 Fixed32 thrust,tx,tz;
 assert(inflictor);
 if (!camera || currentState.health<=0 || damage<=0)
    return;
 an=getAngle(camera->pos.x-inflictor->pos.x,camera->pos.z-inflictor->pos.z);
 thrust=F(damage)>>3;
 if (damage<40 && damage>currentState.health &&
     (camera->pos.y-F(41))-(inflictor->pos.y-inflictor->radius)>F(64) && (P_Random()&1))
    {an=normalizeAngle(an+F(180));
     thrust<<=2;
    }
 tx=MTH_Mul(thrust,MTH_Cos(an));
 tz=MTH_Mul(thrust,MTH_Sin(an));
 doomPlayer.momx+=tx;
 doomPlayer.momz+=tz;
 camera->vel.x+=MTH_Mul(tx,DOOM_VEL_SCALE);
 camera->vel.z+=MTH_Mul(tz,DOOM_VEL_SCALE);
}

/* --- selection (SPEC_PLAYER section 2.6) ---------------------------------------------------- */

/* G_NextWeapon (chocolate) selectable rule: owned, in this game mode, with ammo for one shot;
   the fist is skipped when the chainsaw is owned (no berserk) */
static int doomWeaponSelectable(int w)
{int ammo;
 if (!(doomPlayer.weaponOwned & (1<<w)))
    return 0;
 if (w==wp_plasma || w==wp_bfg || w==wp_supershotgun)
    return 0;                                   /* shareware target */
 if (w==wp_fist && (doomPlayer.weaponOwned & (1<<wp_chainsaw)))
    return 0;
 ammo=doomWeaponInfo[w].ammo;
 if (ammo!=am_noammo && doomPlayer.ammo[ammo]<1)
    return 0;
 return 1;
}

/* CFG_WEAPON_UP / CFG_WEAPON_DN (SRUINS.C:983/988, 60 Hz edge) and the jump button: the next /
   previous selectable weapon in G_NextWeapon's order from the pending (else ready) one; then
   P_PlayerThink's BT_CHANGE rule (p_user.c:276-306): owned and different => pending */
void doom_weaponNext(int dir)
{int start,i,idx,w;
 if (currentState.health<=0)
    return;
 start=(doomPlayer.pendingWeapon==DOOM_WP_NOCHANGE)?doomPlayer.readyWeapon:doomPlayer.pendingWeapon;
 w=start;
 idx=0;
 for (i=0;i<9;i++)
    if (doomWeaponOrder[i]==start)
       idx=i;
 for (i=0;i<9;i++)
    {idx=(idx+dir+9)%9;
     w=doomWeaponOrder[idx];
     if (doomWeaponSelectable(w))
	break;
    }
 if (i==9)
    return;
 if (w==wp_fist && (doomPlayer.weaponOwned & (1<<wp_chainsaw)))
    w=wp_chainsaw;
 if ((doomPlayer.weaponOwned & (1<<w)) && w!=doomPlayer.readyWeapon)
    doomPlayer.pendingWeapon=(signed char)w;
}

/* --- pickups (P_TouchSpecialThing, p_inter.c:335-662) --------------------------------------- */

/* P_GiveAmmo :60-150: num = clip loads (0 = half a clip); refused at max; a weapon switch only
   when we were down to zero */
static int doomGiveAmmo(int ammo,int num)
{int oldammo;
 if (ammo==am_noammo)
    return 0;
 assert(ammo>=0 && ammo<DOOM_NUMAMMO);
 if (doomPlayer.ammo[ammo]==doomPlayer.maxAmmo[ammo])
    return 0;
 if (num)
    num*=doomClipAmmo[ammo];
 else
    num=doomClipAmmo[ammo]/2;
 oldammo=doomPlayer.ammo[ammo];
 doomPlayer.ammo[ammo]+=num;
 if (doomPlayer.ammo[ammo]>doomPlayer.maxAmmo[ammo])
    doomPlayer.ammo[ammo]=doomPlayer.maxAmmo[ammo];
 if (oldammo)
    return 1;
 switch (ammo)
    {case am_clip:
	if (doomPlayer.readyWeapon==wp_fist)
	   {if (doomPlayer.weaponOwned & (1<<wp_chaingun))
	       doomPlayer.pendingWeapon=wp_chaingun;
	    else
	       doomPlayer.pendingWeapon=wp_pistol;
	   }
	break;
     case am_shell:
	if (doomPlayer.readyWeapon==wp_fist || doomPlayer.readyWeapon==wp_pistol)
	   {if (doomPlayer.weaponOwned & (1<<wp_shotgun))
	       doomPlayer.pendingWeapon=wp_shotgun;
	   }
	break;
     case am_cell:
	if (doomPlayer.readyWeapon==wp_fist || doomPlayer.readyWeapon==wp_pistol)
	   {if (doomPlayer.weaponOwned & (1<<wp_plasma))
	       doomPlayer.pendingWeapon=wp_plasma;
	   }
	break;
     case am_misl:
	if (doomPlayer.readyWeapon==wp_fist)
	   {if (doomPlayer.weaponOwned & (1<<wp_missile))
	       doomPlayer.pendingWeapon=wp_missile;
	   }
	break;
     default:
	break;
    }
 return 1;
}

/* P_GiveWeapon :162-215 (single player): one clip dropped, two found; new => pending */
static int doomGiveWeapon(int weapon,int dropped)
{int gaveammo,gaveweapon;
 assert(weapon>=0 && weapon<NUMWEAPONS);
 if (doomWeaponInfo[weapon].ammo!=am_noammo)
    gaveammo=doomGiveAmmo(doomWeaponInfo[weapon].ammo,dropped?1:2);
 else
    gaveammo=0;
 if (doomPlayer.weaponOwned & (1<<weapon))
    gaveweapon=0;
 else
    {gaveweapon=1;
     doomPlayer.weaponOwned|=(unsigned char)(1<<weapon);
     doomPlayer.pendingWeapon=(signed char)weapon;
    }
 return gaveweapon || gaveammo;
}

/* P_GiveBody :225-241 */
static int doomGiveBody(int num)
{if (currentState.health>=DOOM_MAXHEALTH)
    return 0;
 currentState.health+=num;
 if (currentState.health>DOOM_MAXHEALTH)
    currentState.health=DOOM_MAXHEALTH;
 doomPlayer.health=currentState.health;
 return 1;
}

/* P_GiveArmor :248-260 */
static int doomGiveArmor(int armortype)
{int hits=armortype*100;
 if (doomPlayer.armorPoints>=hits)
    return 0;
 doomPlayer.armorType=armortype;
 doomPlayer.armorPoints=hits;
 return 1;
}

/* P_GiveCard :266-276: 1 when the card was new (message), the item is consumed either way */
static int doomGiveCard(int card)
{if (doomPlayer.keys & (1<<card))
    return 0;
 doomPlayer.bonusCount=DOOM_BONUSADD;
 doomPlayer.keys|=(unsigned char)(1<<card);
 return 1;
}

/* Sole caller: doom_item_func (SPEC_RUNTIME section 6) on a camera collision with a live player.
   Effects only: inventory, sound, message, bonusCount.  1 = consumed (delayKill by the caller),
   0 = refused (the item stays).  Identified by the spawn sprite like Doom. */
int doom_playerGetObject(int mt,int dropped)
{int spr,sound;
 const char *msg;
 assert(mt>=0 && mt<NUMMOBJTYPES);
 if (currentState.health<=0)
    return 0;
 spr=doomStates[doomMobjInfo[mt].spawnstate].sprite;
 sound=sfx_itemup;
 msg=NULL;
 switch (spr)
    {case SPR_ARM1:
	if (!doomGiveArmor(1))
	   return 0;
	msg=GOTARMOR;
	break;
     case SPR_ARM2:
	if (!doomGiveArmor(2))
	   return 0;
	msg=GOTMEGA;
	break;
     case SPR_BON1:
	currentState.health++;                  /* can go over 100% */
	if (currentState.health>200)
	   currentState.health=200;
	doomPlayer.health=currentState.health;
	msg=GOTHTHBONUS;
	break;
     case SPR_BON2:
	doomPlayer.armorPoints++;
	if (doomPlayer.armorPoints>200)
	   doomPlayer.armorPoints=200;
	if (!doomPlayer.armorType)
	   doomPlayer.armorType=1;
	msg=GOTARMBONUS;
	break;
     case SPR_SOUL:
	currentState.health+=100;
	if (currentState.health>200)
	   currentState.health=200;
	doomPlayer.health=currentState.health;
	msg=GOTSUPER;
	sound=sfx_getpow;
	break;
     case SPR_BKEY:
	if (doomGiveCard(0))
	   msg=GOTBLUECARD;
	break;
     case SPR_YKEY:
	if (doomGiveCard(1))
	   msg=GOTYELWCARD;
	break;
     case SPR_RKEY:
	if (doomGiveCard(2))
	   msg=GOTREDCARD;
	break;
     case SPR_BSKU:
	if (doomGiveCard(3))
	   msg=GOTBLUESKUL;
	break;
     case SPR_YSKU:
	if (doomGiveCard(4))
	   msg=GOTYELWSKUL;
	break;
     case SPR_RSKU:
	if (doomGiveCard(5))
	   msg=GOTREDSKULL;
	break;
     case SPR_STIM:
	if (!doomGiveBody(10))
	   return 0;
	msg=GOTSTIM;
	break;
     case SPR_MEDI:
	if (!doomGiveBody(25))
	   return 0;
	if (currentState.health<25)             /* vanilla: tested AFTER the gift */
	   msg=GOTMEDINEED;
	else
	   msg=GOTMEDIKIT;
	break;
     case SPR_CLIP:
	if (!doomGiveAmmo(am_clip,dropped?0:1))
	   return 0;
	msg=GOTCLIP;
	break;
     case SPR_AMMO:
	if (!doomGiveAmmo(am_clip,5))
	   return 0;
	msg=GOTCLIPBOX;
	break;
     case SPR_ROCK:
	if (!doomGiveAmmo(am_misl,1))
	   return 0;
	msg=GOTROCKET;
	break;
     case SPR_BROK:
	if (!doomGiveAmmo(am_misl,5))
	   return 0;
	msg=GOTROCKBOX;
	break;
     case SPR_CELL:
	if (!doomGiveAmmo(am_cell,1))
	   return 0;
	msg=GOTCELL;
	break;
     case SPR_CELP:
	if (!doomGiveAmmo(am_cell,5))
	   return 0;
	msg=GOTCELLBOX;
	break;
     case SPR_SHEL:
	if (!doomGiveAmmo(am_shell,1))
	   return 0;
	msg=GOTSHELLS;
	break;
     case SPR_SBOX:
	if (!doomGiveAmmo(am_shell,5))
	   return 0;
	msg=GOTSHELLBOX;
	break;
     case SPR_BPAK:
	{int i;
	 if (!doomPlayer.backpack)
	    {for (i=0;i<DOOM_NUMAMMO;i++)
		doomPlayer.maxAmmo[i]*=2;
	     doomPlayer.backpack=1;
	    }
	 for (i=0;i<DOOM_NUMAMMO;i++)
	    doomGiveAmmo(i,1);
	 msg=GOTBACKPACK;
	}
	break;
     case SPR_BFUG:
	if (!doomGiveWeapon(wp_bfg,0))
	   return 0;
	msg=GOTBFG9000;
	sound=sfx_wpnup;
	break;
     case SPR_MGUN:
	if (!doomGiveWeapon(wp_chaingun,dropped))
	   return 0;
	msg=GOTCHAINGUN;
	sound=sfx_wpnup;
	break;
     case SPR_CSAW:
	if (!doomGiveWeapon(wp_chainsaw,0))
	   return 0;
	msg=GOTCHAINSAW;
	sound=sfx_wpnup;
	break;
     case SPR_LAUN:
	if (!doomGiveWeapon(wp_missile,0))
	   return 0;
	msg=GOTLAUNCHER;
	sound=sfx_wpnup;
	break;
     case SPR_PLAS:
	if (!doomGiveWeapon(wp_plasma,0))
	   return 0;
	msg=GOTPLASMA;
	sound=sfx_wpnup;
	break;
     case SPR_SHOT:
	if (!doomGiveWeapon(wp_shotgun,dropped))
	   return 0;
	msg=GOTSHOTGUN;
	sound=sfx_wpnup;
	break;
     case SPR_SGN2:
	if (!doomGiveWeapon(wp_supershotgun,dropped))
	   return 0;
	msg=GOTSHOTGUN2;
	sound=sfx_wpnup;
	break;
     default:
	return 0;                               /* powers (PINV PSTR PINS SUIT PMAP PVIS), MEGA: TODO, refused */
    }
 if (msg)
    doom_setMessage(msg);
 doomPlayer.bonusCount+=DOOM_BONUSADD;
 doom_sound(NULL,sound);
 return 1;
}

/* GCC14: a player's body as another player sees it (MPLAYER.H, SRUINS.C mpShowBodies).  Doom's
   S_PLAY frames: standing, the four of S_PLAY_RUN while it moves (4 tics each), the corpse of
   S_PLAY_DIE7 once dead.  getFacingAngle works in the sprite convention, and a player's body
   carries the camera yaw, 90 degrees less (AI.C constructPlayer).  -1 = not drawn, which is
   also the answer when the level has no PLAY frames at all (-2 from doom_seq). */
short doom_playerBodySeq(Sprite *body,Sprite *viewer,int health)
{int frame,view,saved,seq;
 const DoomState *st;
 body->scale=65536;                    /* 1 texel per unit, as every Doom thing */
 body->flags|=SPRITEFLAG_NOSHADOW;
 if (health<=0)
    st=&doomStates[S_PLAY_DIE7];
 else if (body->vel.x || body->vel.z)
    st=&doomStates[S_PLAY_RUN1+((doomLevelTime>>2)&3)];
 else
    st=&doomStates[S_PLAY];
 frame=st->frame&0x7fff;
 saved=body->angle;
 body->angle=normalizeAngle(body->angle+F(90));
 view=getFacingAngle(body,viewer);
 body->angle=saved;
 seq=doom_seq(st->sprite,frame,view);
 if (seq<0 || seq>=level_nmSequences)
    return -1;
 return (short)seq;
}

/* DOOM_WEAPON.C -- psprite state machine, weapon verbs, hitscan/autoaim, the display pin.
 * SPEC_PLAYER sections 2.2-2.5; contract section 5 (wseq(state) = state - 1).
 *
 * The machine is P_SetPsprite / P_MovePsprites over doomStates[] (35 Hz, doom_playerTic); the
 * verbs are the .psp members of the shared doomActions[] table (DOOM_VERBS.C, prototypes from
 * DOOM_ACTIONS.H), defined here next to what they drive.  Display: after each tic the weapon
 * state is PINNED (setWeaponSequence, SEQUENCE.C) at (pspSx, DOOM_PSP_Y0 + pspSy) and the flash
 * state added with addWeaponSequence; doom_weaponDraw (CFG_RUN_WEAPON, SRUINS.C:2207) then
 * calls advanceWeaponSequence once per rendered frame.
 *
 * Doom references [doom] p_pspr.c: P_SetPsprite :50-115, P_BringUpWeapon :129-145, P_CheckAmmo
 * :152-230, P_FireWeapon :236-247, A_WeaponReady :273-326, A_ReFire :334-352, A_Lower/A_Raise
 * :376-432, A_GunFlash :440-446, A_Punch :456-483, P_BulletSlope :610-632, P_GunShot :635-650,
 * A_FirePistol :657-673, A_FireShotgun :681-698, A_FireCGun :767-789, A_Light* :795-810,
 * P_MovePsprites :860-880; p_map.c P_LineAttack / PTR_ShootTraverse :983-1140. */
#include "util.h"
#include "level.h"
#include "sprite.h"
#include "object.h"
#include "ai.h"
#include "sruins.h"
#include "sequence.h"
#include "hitscan.h"
#include "walls.h"
#include "gamestat.h"
#include "doom.h"
#include "mplayer.h"
#include "doom_lights.h"
#include "doom_actions.h"

DOOM_ACTIONS_PROTOTYPES

#define DOOM_WEAPONTOP     F(32)
#define DOOM_WEAPONBOTTOM  F(128)
#define DOOM_LOWERSPEED    F(6)
#define DOOM_RAISESPEED    F(6)
#define DOOM_MELEERANGE    F(64)
#define DOOM_MISSILERANGE  F(2048)
#define DOOM_SHOOTZ        F(41-36)     /* camera->pos.y is the eye (41 above the feet); Doom shoots from height/2 + 8 = 36 */
#define DOOM_SPREAD_UNIT   1440         /* (P_Random()-P_Random())<<18 on 2^32 = 360/16384 degrees = 1440 engine units */
#define DOOM_FINE_UNIT     2880         /* one of FINEANGLES = 8192 steps = 360/8192 degrees */

static Fixed32 doomBulletPitch;         /* bulletslope of P_BulletSlope, as an engine pitch */

/* GCC14: MPLAYER.H -- the autoaim slope is the shooter's */
void doom_weaponMpRegister(void)
{MPREG(doomBulletPitch);
}

/* --- P_SetPsprite (p_pspr.c:50-115) --------------------------------------------------------- */

/* Loops while the state has tics == 0; S_NULL switches the slot off; the verb is the .psp member
   of the shared table (states 1..89 only).  misc1/misc2 (DeHackEd offsets) do not exist here. */
void doom_setPsprite(int ps,int state)
{const DoomState *st;
 assert(ps>=0 && ps<DOOM_NUMPSPRITES);
 do
    {if (!state)
	{doomPlayer.pspState[ps]=0;        /* S_NULL: psprite off */
	 doomPlayer.pspTics[ps]=-1;
	 break;
	}
     assert(state>0 && state<NUMSTATES);
     st=&doomStates[state];
     assert(st->flags & DOOM_SF_PSPRITE);
     doomPlayer.pspState[ps]=(short)state;
     doomPlayer.pspTics[ps]=st->tics;
     if (st->action)
	{assert(st->action<DOOM_NUMACTIONS);
	 if (doomActions[st->action].psp)
	    doomActions[st->action].psp(&doomPlayer,ps);
	 if (!doomPlayer.pspState[ps])
	    break;                          /* the verb switched the slot off */
	}
     state=doomStates[doomPlayer.pspState[ps]].nextstate;
    }
 while (!doomPlayer.pspTics[ps]);
}

/* --- the display pin (SPEC_PLAYER section 2.3, contract section 5) -------------------------- */

/* (cx, cy) = (f(sx), Y0 + f(sy)): advanceWeaponSequence draws chunk c at screen
   (cx + chunkx, cy + chunky) with chunkx = -leftoffset + 64c, chunky = -topoffset + 64r, which
   is Doom's (sx - lo, sy - to) in the 3D window whose first line is Y0 (SEQUENCE.C:296-297). */
static void doomPinWeapon(void)
{if (doomPlayer.pspState[DOOM_PS_WEAPON])
    setWeaponSequence(doomPlayer.pspState[DOOM_PS_WEAPON]-1,
		      f(doomPlayer.pspSx),DOOM_PSP_Y0+f(doomPlayer.pspSy));
 else
    setWeaponSequence(-1,0,0);
 if (doomPlayer.pspState[DOOM_PS_FLASH] && doomPlayer.pspState[DOOM_PS_WEAPON])
    addWeaponSequence(doomPlayer.pspState[DOOM_PS_FLASH]-1);
 else
    addWeaponSequence(-1);
}

/* P_MovePsprites (p_pspr.c:860-880): one tic for both slots; the flash follows the weapon
   (shared pspSx/pspSy); then the pin for the frames until the next tic. */
void doom_psprTic(void)
{int i;
 for (i=0;i<DOOM_NUMPSPRITES;i++)
    {if (doomPlayer.pspState[i] && doomPlayer.pspTics[i]!=-1)
	{if (!--doomPlayer.pspTics[i])
	    doom_setPsprite(i,doomStates[doomPlayer.pspState[i]].nextstate);
	}
    }
 doomPinWeapon();
}

/* CFG_RUN_WEAPON (SRUINS.C:2207): the engine's runWeapon replaced by the draw of the pinned
   entry, once per rendered frame (runWeapon called it once per elapsed 60 Hz frame to clock the
   PowerSlave sequences; ours have one frame per state, clocked by the tic). */
void doom_weaponDraw(int nmFrames)
{(void)nmFrames;
 advanceWeaponSequence(0,0,0);
}

/* --- P_BringUpWeapon, P_CheckAmmo, P_FireWeapon --------------------------------------------- */

void doom_bringUpWeapon(void)
{int newstate;
 if (doomPlayer.pendingWeapon==DOOM_WP_NOCHANGE)
    doomPlayer.pendingWeapon=doomPlayer.readyWeapon;
 assert(doomPlayer.pendingWeapon>=0 && doomPlayer.pendingWeapon<NUMWEAPONS);
 if (doomPlayer.pendingWeapon==wp_chainsaw)
    doom_playerSound(sfx_sawup);
 newstate=doomWeaponInfo[(int)doomPlayer.pendingWeapon].upstate;
 doomPlayer.pendingWeapon=DOOM_WP_NOCHANGE;
 doomPlayer.pspSy=DOOM_WEAPONBOTTOM;
 doom_setPsprite(DOOM_PS_WEAPON,newstate);
}

/* 1 if the ready weapon has ammo for one shot; else picks the next one in Doom's preference
   order (shareware: no plasma/BFG/SSG) and lowers the current one */
int doom_checkAmmo(void)
{int ammo,count;
 assert(doomPlayer.readyWeapon>=0 && doomPlayer.readyWeapon<NUMWEAPONS);
 ammo=doomWeaponInfo[(int)doomPlayer.readyWeapon].ammo;
 if (doomPlayer.readyWeapon==wp_bfg)
    count=40;
 else if (doomPlayer.readyWeapon==wp_supershotgun)
    count=2;
 else
    count=1;
 if (ammo==am_noammo || doomPlayer.ammo[ammo]>=count)
    return 1;
 do
    {if ((doomPlayer.weaponOwned & (1<<wp_chaingun)) && doomPlayer.ammo[am_clip])
	doomPlayer.pendingWeapon=wp_chaingun;
     else if ((doomPlayer.weaponOwned & (1<<wp_shotgun)) && doomPlayer.ammo[am_shell])
	doomPlayer.pendingWeapon=wp_shotgun;
     else if (doomPlayer.ammo[am_clip])
	doomPlayer.pendingWeapon=wp_pistol;
     else if (doomPlayer.weaponOwned & (1<<wp_chainsaw))
	doomPlayer.pendingWeapon=wp_chainsaw;
     else if ((doomPlayer.weaponOwned & (1<<wp_missile)) && doomPlayer.ammo[am_misl])
	doomPlayer.pendingWeapon=wp_missile;
     else
	doomPlayer.pendingWeapon=wp_fist;
    }
 while (doomPlayer.pendingWeapon==DOOM_WP_NOCHANGE);
 doom_setPsprite(DOOM_PS_WEAPON,doomWeaponInfo[(int)doomPlayer.readyWeapon].downstate);
 return 0;
}

/* P_CheckAmmo, S_PLAY_ATK1 (no player mobj here), atkstate, P_NoiseAlert from the camera's sector */
void doom_fireWeapon(void)
{if (!doom_checkAmmo())
    return;
 doom_setPsprite(DOOM_PS_WEAPON,doomWeaponInfo[(int)doomPlayer.readyWeapon].atkstate);
 assert(camera->s>=0 && camera->s<level_nmSectors);
 doom_noiseAlert((Object *)player,camera->s);
}

static void doomDecreaseAmmo(int amount)
{int ammo=doomWeaponInfo[(int)doomPlayer.readyWeapon].ammo;
 if (ammo==am_noammo)
    return;
 doomPlayer.ammo[ammo]-=amount;
 if (doomPlayer.ammo[ammo]<0)
    doomPlayer.ammo[ammo]=0;
}

/* --- player muzzle flash ------------------------------------------------------------------------
   A light on `camera`, so it lights exactly the walls the backface test keeps.  An addition:
   Doom's A_Light1/2 is a screen tint.  Tuned apart (LIGHT_MUZZLE_PLAYER) because it sits on the
   view; a shot landing on a half-faded flash relights it instead of adding a second light. */
void doom_muzzleFlash(void)
{assert(camera);
 if (!doomPlayer.muzzleTics)
    addLightEx(camera,GP_LIGHT_MUZZLE_PLAYER);
 else
    changeLightEx(camera,GP_LIGHT_MUZZLE_PLAYER);
 doomPlayer.muzzleTics=GP_LIGHT_MUZZLE_TICS;
}

/* Called by doom_playerTic before the psprites: full on tic N, half on N+1, off at N+2. */
void doom_muzzleTic(void)
{if (!doomPlayer.muzzleTics || !camera)
    return;
 if (!--doomPlayer.muzzleTics)
    removeLight(camera);
 else
    doom_lightFade(camera,GP_LIGHT_MUZZLE_PLAYER,doomPlayer.muzzleTics,GP_LIGHT_MUZZLE_TICS);
}

/* --- hitscan and autoaim (SPEC_PLAYER section 2.5) ------------------------------------------ */

/* P_BulletSlope: the renderer's autoTarget (CLASS_MONSTER whose feet fall within +-20 px of the
   screen centre, WALLS.C:2724-2733 -- a live monster or a barrel; corpses are CLASS_SPRITE) gives
   the pitch from the shot origin to the centre of its sphere; 0 otherwise.  1 = a target. */
int doom_aimSlope(Fixed32 *outPitch)
{Sprite *t=autoTarget;
 int du;
 assert(outPitch);
 *outPitch=0;
 if (!t || !t->owner || !doom_targetAlive(t->owner))
    return 0;
 du=dist(t->pos.x-camera->pos.x,0,t->pos.z-camera->pos.z);   /* integer units (UTIL.C) */
 if (du<=0)
    return 0;
 if (du>32767)
    du=32767;
 *outPitch=getAngle(F(du),t->pos.y-(camera->pos.y-DOOM_SHOOTZ));
 return 1;
}

/* P_LineAttack from the camera: origin = eye - 5 u (feet + 36), yaw in the player convention
   (forward = (-sin, cos)), pitch towards the aim.  On a shootable Doom sprite: blood (or a puff
   when it does not bleed) then the damage, PTR_ShootTraverse order p_map.c:1083-1089 -- the
   damage is the player's, source = the player object; on anything else within range a puff,
   pulled back 4 u along the ray on a wall (:1029-1040).  Returns the sprite hit (linetarget). */
Sprite *doom_playerLineAttack(int yaw,int pitch,Fixed32 range,int damage,int melee)
{MthXyz eye,ray,hit;
 Fixed32 cp,d;
 int hitSec,code;
 assert(camera);
 eye=camera->pos;
 eye.y-=DOOM_SHOOTZ;
 cp=MTH_Cos(pitch);
 ray.x=MTH_Mul(cp,-MTH_Sin(yaw));
 ray.y=MTH_Sin(pitch);
 ray.z=MTH_Mul(cp,MTH_Cos(yaw));
 code=hitScan(camera,&ray,&eye,camera->s,&hit,&hitSec);
 if (!code)
    return NULL;
 d=approxDist(hit.x-eye.x,hit.y-eye.y,hit.z-eye.z);
 if (code & COLLIDE_SPRITE)
    {Sprite *spr=&sprites[code&0xffff];
     Object *o=spr->owner;
     /* Doom crosses a thing at its centre diagonal (PIT_AddThingIntercepts), so the reach is
	measured to the CENTRE: hit point on the sphere + radius.  "+radius" on the range let the
	fist (64) land at 64 + 2*28 = 120 u from a POSS. */
     if (d+spr->radius>range)
	return NULL;
     if (o && o->func==game_actor_func && (((DoomActor *)o)->mflags & DF_SHOOTABLE))
	{if (((DoomActor *)o)->mflags & DF_NOBLOOD)
	    doom_spawnPuff(&hit,hitSec,melee);
	 else
	    doom_spawnBlood(&hit,hitSec,damage);
	 doom_damage(spr,(Object *)player,(Object *)player,damage);
	 return spr;
	}
     if (mpIsPlayer(spr) && o && doom_targetAlive(o))
	{/* GCC14: friendly fire, as Doom's co-op: a player is MF_SHOOTABLE and bleeds */
	 doom_spawnBlood(&hit,hitSec,damage);
	 doom_damage(spr,(Object *)player,(Object *)player,damage);
	 return spr;
	}
     doom_spawnPuff(&hit,hitSec,melee);
     return NULL;
    }
 if (d>range)
    return NULL;
 if (code & COLLIDE_WALL)
    {MthXyz p;
     p.x=hit.x-(ray.x<<2);
     p.y=hit.y-(ray.y<<2);
     p.z=hit.z-(ray.z<<2);
     doom_spawnPuff(&p,hitSec,melee);
    }
 else
    doom_spawnPuff(&hit,hitSec,melee);         /* floor / ceiling */
 return NULL;
}

/* P_GunShot: damage first, then the spread when not accurate (P_Random order), then the attack
   along doomBulletPitch (P_BulletSlope called by the verb) */
void doom_gunShot(int accurate,int damage,int range)
{int yaw;
 (void)damage;
 damage=5*(P_Random()%3+1);
 yaw=playerAngle.yaw;
 if (!accurate)
    yaw+=(P_Random()-P_Random())*DOOM_SPREAD_UNIT;
 doom_playerLineAttack(normalizeAngle(yaw),doomBulletPitch,F(range),damage,0);
}

/* --- weapon verbs: void A_x(DoomPlayer *p, int ps) (SPEC_PLAYER section 2.2) ---------------- */

/* A_WeaponReady :273-326: pending change or death => downstate; fire => doom_fireWeapon (the
   missile launcher and the BFG do not autofire: attackDown); else bob (section 2.4:
   angle = 128 * leveltime of FINEANGLES = leveltime * 5.625 degrees, period 64 tics) */
void A_WeaponReady(DoomPlayer *p,int ps)
{int an;
 assert(p && ps==DOOM_PS_WEAPON);
 if (p->readyWeapon==wp_chainsaw && p->pspState[ps]==S_SAW)
    doom_playerSound(sfx_sawidl);
 if (p->pendingWeapon!=DOOM_WP_NOCHANGE || currentState.health<=0)
    {doom_setPsprite(ps,doomWeaponInfo[(int)p->readyWeapon].downstate);
     return;
    }
 if (p->fire)
    {if (!p->attackDown || (p->readyWeapon!=wp_missile && p->readyWeapon!=wp_bfg))
	{p->attackDown=1;
	 doom_fireWeapon();
	 return;
	}
    }
 else
    p->attackDown=0;
 an=normalizeAngle(((doomLevelTime*128)&8191)*DOOM_FINE_UNIT);   /* SBL MTH_Cos: |x| >= 180 reads as 0 */
 p->pspSx=F(1)+MTH_Mul(p->bob,MTH_Cos(an));
 an=((doomLevelTime*128)&4095)*DOOM_FINE_UNIT;
 p->pspSy=DOOM_WEAPONTOP+MTH_Mul(p->bob,MTH_Sin(an));
}

/* A_ReFire :334-352 */
void A_ReFire(DoomPlayer *p,int ps)
{assert(p && ps==DOOM_PS_WEAPON);
 if (p->fire && p->pendingWeapon==DOOM_WP_NOCHANGE && currentState.health>0)
    {p->refire++;
     doom_fireWeapon();
    }
 else
    {p->refire=0;
     doom_checkAmmo();
    }
}

void A_CheckReload(DoomPlayer *p,int ps)
{assert(p && ps==DOOM_PS_WEAPON);
 doom_checkAmmo();
}

/* A_Lower :376-405: 6 u/tic down to 128, then the pending weapon comes up (or the slot goes off
   when dead) */
void A_Lower(DoomPlayer *p,int ps)
{assert(p && ps==DOOM_PS_WEAPON);
 p->pspSy+=DOOM_LOWERSPEED;
 if (p->pspSy<DOOM_WEAPONBOTTOM)
    return;
 if (currentState.health<=0)
    {p->pspSy=DOOM_WEAPONBOTTOM;
     doom_setPsprite(ps,S_NULL);
     return;
    }
 p->readyWeapon=p->pendingWeapon;
 doom_bringUpWeapon();
}

/* A_Raise :412-432 */
void A_Raise(DoomPlayer *p,int ps)
{assert(p && ps==DOOM_PS_WEAPON);
 p->pspSy-=DOOM_RAISESPEED;
 if (p->pspSy>DOOM_WEAPONTOP)
    return;
 p->pspSy=DOOM_WEAPONTOP;
 doom_setPsprite(ps,doomWeaponInfo[(int)p->readyWeapon].readystate);
}

/* A_GunFlash :440-446 */
void A_GunFlash(DoomPlayer *p,int ps)
{assert(p);
 (void)ps;
 doom_setPsprite(DOOM_PS_FLASH,doomWeaponInfo[(int)p->readyWeapon].flashstate);
}

/* A_Punch :456-483: damage (P_Random()%10+1)*2, spread, MELEERANGE; on a hit: sfx_punch and turn
   to face the target (player convention: sprite angle - 90) */
void A_Punch(DoomPlayer *p,int ps)
{int damage,yaw;
 Fixed32 pitch;
 Sprite *t;
 assert(p);
 (void)ps;
 damage=(P_Random()%10+1)<<1;
 yaw=playerAngle.yaw+(P_Random()-P_Random())*DOOM_SPREAD_UNIT;
 doom_aimSlope(&pitch);
 t=doom_playerLineAttack(normalizeAngle(yaw),pitch,DOOM_MELEERANGE,damage,1);
 if (t)
    {doom_playerSound(sfx_punch);
     playerAngle.yaw=normalizeAngle(getAngle(t->pos.x-camera->pos.x,t->pos.z-camera->pos.z)-F(90));
    }
}

/* A_Saw (p_pspr.c:490-534): damage 2*(P_Random()%10+1), spread, MELEERANGE + 1 (so the puff
   does not skip the flash).  No target: sfx_sawful.  A target: sfx_sawhit, the view locks on it
   -- snapped to within ANG90/21 when it is farther than ANG90/20, else turned by ANG90/20 --
   and the next tic pulls the player forward (MF_JUSTATTACKED, doom_playerSawPull).  Angles in
   the player convention (sprite angle - 90), which turns the same way as Doom's. */
#define DOOM_ANG90_20 (F(9)/2)            /* ANG90/20 = 4.5 degrees  */
#define DOOM_ANG90_21 (F(90)/21)          /* ANG90/21 = 4.29 degrees */
void A_Saw(DoomPlayer *p,int ps)
{int damage,yaw,an,delta;
 Fixed32 pitch;
 Sprite *t;
 assert(p);
 (void)ps;
 damage=2*(P_Random()%10+1);
 yaw=playerAngle.yaw+(P_Random()-P_Random())*DOOM_SPREAD_UNIT;
 doom_aimSlope(&pitch);
 t=doom_playerLineAttack(normalizeAngle(yaw),pitch,DOOM_MELEERANGE+F(1),damage,1);
 if (!t)
    {doom_playerSound(sfx_sawful);
     return;
    }
 doom_playerSound(sfx_sawhit);
 an=normalizeAngle(getAngle(t->pos.x-camera->pos.x,t->pos.z-camera->pos.z)-F(90));
 delta=normalizeAngle(an-playerAngle.yaw);
 if (delta<0)
    playerAngle.yaw=normalizeAngle((delta<-DOOM_ANG90_20)?an+DOOM_ANG90_21:
				   playerAngle.yaw-DOOM_ANG90_20);
 else
    playerAngle.yaw=normalizeAngle((delta>DOOM_ANG90_20)?an-DOOM_ANG90_21:
				   playerAngle.yaw+DOOM_ANG90_20);
 doom_playerSawPull();
}

/* A_FirePistol :657-673 */
void A_FirePistol(DoomPlayer *p,int ps)
{assert(p);
 (void)ps;
 doom_playerSound(sfx_pistol);
 doomDecreaseAmmo(1);
 doom_setPsprite(DOOM_PS_FLASH,doomWeaponInfo[(int)p->readyWeapon].flashstate);
 doom_muzzleFlash();
 doom_aimSlope(&doomBulletPitch);
 doom_gunShot(!p->refire,0,2048);
}

/* A_FireShotgun :681-698: 7 pellets, all spread */
void A_FireShotgun(DoomPlayer *p,int ps)
{int i;
 assert(p);
 (void)ps;
 doom_playerSound(sfx_shotgn);
 doomDecreaseAmmo(1);
 doom_setPsprite(DOOM_PS_FLASH,doomWeaponInfo[(int)p->readyWeapon].flashstate);
 doom_muzzleFlash();
 doom_aimSlope(&doomBulletPitch);
 for (i=0;i<7;i++)
    doom_gunShot(0,0,2048);
}

/* A_FireCGun :767-789: one bullet per state, the flash frame follows the barrel (S_CHAIN1 = atkstate) */
void A_FireCGun(DoomPlayer *p,int ps)
{int ammo;
 assert(p);
 doom_playerSound(sfx_pistol);
 ammo=doomWeaponInfo[(int)p->readyWeapon].ammo;
 if (ammo!=am_noammo && !p->ammo[ammo])
    return;
 doomDecreaseAmmo(1);
 doom_setPsprite(DOOM_PS_FLASH,doomWeaponInfo[(int)p->readyWeapon].flashstate+
		 p->pspState[ps]-doomWeaponInfo[wp_chaingun].atkstate);
 doom_muzzleFlash();
 doom_aimSlope(&doomBulletPitch);
 doom_gunShot(!p->refire,0,2048);
}

/* A_FireMissile :745-753: P_SpawnPlayerMissile(MT_ROCKET) along the autoaim */
void A_FireMissile(DoomPlayer *p,int ps)
{Fixed32 pitch;
 assert(p);
 (void)ps;
 doomDecreaseAmmo(1);
 doom_muzzleFlash();
 doom_aimSlope(&pitch);
 doom_spawnPlayerMissile(MT_ROCKET,normalizeAngle(playerAngle.yaw+F(90)),pitch);
}

/* A_FirePlasma (p_pspr.c:721-731): one bolt, flash frame picked by P_Random()&1, no sound of
   its own (the missile's seesound).  Not in the shareware; implemented so the stream's light
   can be reached. */
void A_FirePlasma(DoomPlayer *p,int ps)
{Fixed32 pitch;
 assert(p);
 (void)ps;
 doomDecreaseAmmo(1);
 doom_setPsprite(DOOM_PS_FLASH,doomWeaponInfo[(int)p->readyWeapon].flashstate+(P_Random()&1));
 doom_muzzleFlash();
 doom_aimSlope(&pitch);
 doom_spawnPlayerMissile(MT_PLASMA,normalizeAngle(playerAngle.yaw+F(90)),pitch);
}

/* A_Light0/1/2 :795-810: extralight, applied by the flash of the tic (DOOM_PLAYER.C) */
void A_Light0(DoomPlayer *p,int ps)
{assert(p);
 (void)ps;
 p->extralight=0;
}

void A_Light1(DoomPlayer *p,int ps)
{assert(p);
 (void)ps;
 p->extralight=1;
}

void A_Light2(DoomPlayer *p,int ps)
{assert(p);
 (void)ps;
 p->extralight=2;
}

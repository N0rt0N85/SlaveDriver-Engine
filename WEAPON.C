#include <machine.h>

#include <libsn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_int.h>
#include <sega_mth.h>
#include <sega_sys.h>
#include <sega_dbg.h>
#include <sega_per.h>
#include <sega_cdc.h>
#include <sega_gfs.h>
#include <sega_snd.h>

#include "weapon.h"
#include "util.h"
#include "spr.h"
#include "sound.h"
#include "sequence.h"
#include "sprite.h"
#include "object.h"
#include "ai.h"
#include "sruins.h"
#include "walls.h"
#include "v_blank.h"
#include "hitscan.h"
#include "pic.h"
#include "gamestat.h"
#include "level.h"
#include "mplayer.h"

static int const weaponMap[]=
{
 0,  /* sword */
 13, /* pistol */
 17, /* m60 */
 30, /* gernade! */
 35, /* flamer */
 40, /* cobra */
 50, /* ring */
 44, /* ravolt */
 0
};

#define WBASEX 160
#define WBASEY 130

static XyInt const weaponCenter[]=
    {{WBASEX,WBASEY+100},
     {WBASEX-12,WBASEY+110},
     {WBASEX,WBASEY+10}, /* m60 */
     {WBASEX+2,WBASEY+70}, /* grenade! */
     {WBASEX+2,WBASEY+10}, /* flamer */
     {WBASEX,WBASEY+80},
     {WBASEX,WBASEY+90},
     {WBASEX,WBASEY+80},
    };

#ifndef JAPAN
int const weaponMaxAmmo[]=
  {
   0,
   60,
   30, /* m60 */
   10, /* gernade */
   200, /* flamer */
   12, /* cobra */
   100, /* red balls-o-death */
   4 /* ravolt */
  };
#else
int const weaponMaxAmmo[]=
  {
   0,
   70,
   40, /* m60 */
   15, /* gernade */
   200, /* flamer */
   12, /* cobra */
   100, /* red balls-o-death */
   4 /* ravolt */
  };
#endif

static int weaponPos[2],weaponVel[2];
int currentWeapon=0;
static int grenadeHoldTime;
static int swordForeSwing=3;
static int recoverInProgress=0;

/* static int ray1Dist=0,ray2Dist=0;
static int ray1Spark,ray2Spark; */
static int weaponSwitchTimer;

static short ringDepletion=0;
static short ringCharging=0;

void initWeapon(void)
{weaponPos[0]=0; weaponPos[1]=0;
 weaponVel[0]=0; weaponVel[1]=0;
 initWeaponQ();
 currentWeapon=WP_NMWEAPONS;
 ringDepletion=0;
 ringCharging=0;
}

void weaponForce(int fx,int fy)
{weaponVel[0]+=fx; weaponVel[1]+=fy;
}

void weaponSetVel(int vx,int vy)
{weaponVel[0]=vx; weaponVel[1]=vy;
}

static void moveWeapon(void)
{weaponPos[0]+=weaponVel[0]; weaponPos[1]+=weaponVel[1];
 weaponForce(-weaponPos[0]>>3,-weaponPos[1]>>3);
 weaponVel[0]=weaponVel[0]-(weaponVel[0]>>2);
 weaponVel[1]=weaponVel[1]-(weaponVel[1]>>2);
}

static int weaponState=1;
void switchWeapons(int on)
{weaponState=on;
}

/* GCC14: the engine weapon's state belongs to a player (MPLAYER.H) */
void weaponMpRegister(void)
{MPREG(weaponPos); MPREG(weaponVel); MPREG(currentWeapon);
 MPREG(grenadeHoldTime); MPREG(swordForeSwing); MPREG(recoverInProgress);
 MPREG(weaponSwitchTimer); MPREG(ringDepletion); MPREG(ringCharging);
 MPREG(weaponState);
}

static int weaponOK(void)
{if (!weaponState)
    return 0;
 if ((level_sector[camera->s].flags & SECFLAG_WATER) &&
     currentState.desiredWeapon!=WP_SWORD &&
     currentState.desiredWeapon!=WP_GRENADE &&
     currentState.desiredWeapon!=WP_COBRA)
    return 0;
 return 1;
}

void fireWeapon(void)
{if (currentWeapon>=WP_NMWEAPONS)
    return;
 if (weaponMaxAmmo[currentWeapon] && !currentState.weaponAmmo[currentWeapon])
    {int w;
     if (currentWeapon!=currentState.desiredWeapon)
	return;
     /* out of ammo for current weapon, switch to next lower weapon that
	has ammo */
     do
	{w=currentState.desiredWeapon;
	 weaponDown(WEAPONINV(currentState.inventory));
	}
     while (w!=currentState.desiredWeapon &&
	    !currentState.weaponAmmo[(int)currentState.desiredWeapon]);
     redrawBowlDots();
     return;
    }
 switch (currentWeapon)
    {case WP_SWORD:
	if (swordForeSwing==3)
	   queueWeaponSequence(weaponMap[WP_SWORD]+3,
			       weaponCenter[WP_SWORD].x,
			       weaponCenter[WP_SWORD].y);
	if (!swordForeSwing)
	   {queueWeaponSequence(weaponMap[WP_SWORD]+5,
				weaponCenter[WP_SWORD].x,
				weaponCenter[WP_SWORD].y);
	   }
	else
	   {queueWeaponSequence(weaponMap[WP_SWORD]+9,
				weaponCenter[WP_SWORD].x,
				weaponCenter[WP_SWORD].y);
	   }
	break;
     case WP_PISTOL:
	queueWeaponSequence(weaponMap[WP_PISTOL]+3,
			    weaponCenter[WP_PISTOL].x,
			    weaponCenter[WP_PISTOL].y);
	queueWeaponSequence(weaponMap[WP_PISTOL]+1,
			    weaponCenter[WP_PISTOL].x,
			    weaponCenter[WP_PISTOL].y);
	break;
     case WP_M60:
	queueWeaponSequence(weaponMap[WP_M60]+2,
			    weaponCenter[WP_M60].x,
			    weaponCenter[WP_M60].y);
	if (currentState.weaponAmmo[WP_M60]>2)
	   addWeaponSequence(weaponMap[WP_M60]+4);
	if (currentState.weaponAmmo[WP_M60]==2)
	   addWeaponSequence(weaponMap[WP_M60]+5);
	if (currentState.weaponAmmo[WP_M60]==1)
	   addWeaponSequence(weaponMap[WP_M60]+6);

	queueWeaponSequence(weaponMap[WP_M60]+1,
			    weaponCenter[WP_M60].x,
			    weaponCenter[WP_M60].y);
	if (currentState.weaponAmmo[WP_M60]>2)
	   addWeaponSequence(weaponMap[WP_M60]+7);
	if (currentState.weaponAmmo[WP_M60]==2)
	   addWeaponSequence(weaponMap[WP_M60]+8);

	break;
     case WP_COBRA:
	queueWeaponSequence(weaponMap[WP_COBRA]+2,
			    weaponCenter[WP_COBRA].x,
			    weaponCenter[WP_COBRA].y);
	queueWeaponSequence(weaponMap[WP_COBRA]+1,
			    weaponCenter[WP_COBRA].x,
			    weaponCenter[WP_COBRA].y);
	break;
     case WP_RAVOLT:
	if (getCurrentWeaponSequence()==weaponMap[WP_RAVOLT]+3)
	   return;
	queueWeaponSequence(weaponMap[WP_RAVOLT]+2,
			    weaponCenter[WP_RAVOLT].x,
			    weaponCenter[WP_RAVOLT].y);
	queueWeaponSequence(weaponMap[WP_RAVOLT]+3,
			    weaponCenter[WP_RAVOLT].x,
			    weaponCenter[WP_RAVOLT].y);
	grenadeHoldTime=0;
	return;
     case WP_FLAMER:
	if (getCurrentWeaponSequence()==weaponMap[WP_FLAMER]+4)
	   return;
	stopAllSound(0);
	playStaticSound(ST_FLAMER,0);
	queueWeaponSequence(weaponMap[WP_FLAMER]+4,
			    weaponCenter[WP_FLAMER].x,
			    weaponCenter[WP_FLAMER].y);
	break;
     case WP_GRENADE:
	if (getCurrentWeaponSequence()==-1)
	   return;
	queueWeaponSequence(weaponMap[WP_GRENADE]+1,
			    weaponCenter[WP_GRENADE].x,
			    weaponCenter[WP_GRENADE].y);
	queueWeaponSequence(-1,
			    weaponCenter[WP_GRENADE].x,
			    weaponCenter[WP_GRENADE].y);
	grenadeHoldTime=0;
	return;
     case WP_RING:
	if (getCurrentWeaponSequence()==weaponMap[WP_RING]+2)
	   return;
	if (ringCharging)
	   return;
	/* beamWidth=20;
	beamColor=-26;
	ray1Spark=0;
	ray2Spark=0; */
	queueWeaponSequence(weaponMap[WP_RING]+2,
			    weaponCenter[WP_RING].x,
			    weaponCenter[WP_RING].y);
	break;
     case WP_NMWEAPONS:
	assert(0);
	break;
       }
 if (weaponMaxAmmo[currentWeapon])
    currentState.weaponAmmo[currentWeapon]--;
}

static void getRay(Fixed32 yaw,Fixed32 pitch,MthXyz *outRay)
{Fixed32 c;
 outRay->x=-MTH_Sin(yaw); outRay->z=MTH_Cos(yaw);
 c=MTH_Cos(pitch);
 outRay->x=MTH_Mul(outRay->x,c);
 outRay->z=MTH_Mul(outRay->z,c);
 outRay->y=MTH_Sin(pitch);
}

static void getAutoAimRay(MthXyz *outRay,Fixed32 dYaw,Fixed32 dPitch,
			  int jitter)
{Fixed32 dist;
 if (!autoTarget/* || playerAngle.pitch!=0*/)
    {getRay(normalizeAngle(playerAngle.yaw+dYaw),
	    normalizeAngle(playerAngle.pitch+dPitch),
	    outRay);
     return;
    }

#if 0
 dist=fixSqrt(f(camera->pos.x-autoTarget->pos.x)*
	      f(camera->pos.x-autoTarget->pos.x)+
	      f(camera->pos.z-autoTarget->pos.z)*
	      f(camera->pos.z-autoTarget->pos.z),0);
 assert(dist>=0);
 pitch=getAngle(F(dist),autoTarget->pos.y-camera->pos.y);
 getRay(normalizeAngle(playerAngle.yaw+dYaw),
	normalizeAngle(pitch+dPitch),
	outRay);
#endif
 outRay->x=autoTarget->pos.x-camera->pos.x;
 outRay->y=autoTarget->pos.y-camera->pos.y;
 outRay->z=autoTarget->pos.z-camera->pos.z;
 if (jitter)
    {outRay->x=MTH_Mul(outRay->x,F(1)+(getNextRand()&0x3fff)-8048);
     outRay->y=MTH_Mul(outRay->y,F(1)+(getNextRand()&0x3fff)-8048);
     outRay->z=MTH_Mul(outRay->z,F(1)+(getNextRand()&0x3fff)-8048);
    }
 dist=fixSqrt(f(camera->pos.x-autoTarget->pos.x)*
	      f(camera->pos.x-autoTarget->pos.x)+
	      f(camera->pos.y-autoTarget->pos.y)*
	      f(camera->pos.y-autoTarget->pos.y)+
	      f(camera->pos.z-autoTarget->pos.z)*
	      f(camera->pos.z-autoTarget->pos.z),0);

 outRay->x=outRay->x/dist;
 outRay->x=CLAMP(outRay->x,F(-1),F(1));
 outRay->y=outRay->y/dist;
 outRay->y=CLAMP(outRay->y,F(-1),F(1));
 outRay->z=outRay->z/dist;
 outRay->z=CLAMP(outRay->z,F(-1),F(1));
}

static void weaponFire(void)
{switch (currentWeapon)
    {case WP_RING:
	{MthXyz orfice;
	 MthXyz ray;
	 static int angleOffset[]={F(0),F(0),F(20),F(-20),F(-20),F(20)};
	 static int pattern=0;
	 static int fire=0;
	 static int hand=0;

	 /*ringDepletion+=8;*/
	 if (ringDepletion>140)
	    {ringCharging=1;
	     if (weaponSequenceQEmpty())
		queueWeaponSequence(weaponMap[WP_RING]+1,
				    weaponCenter[WP_RING].x,
				    weaponCenter[WP_RING].y);
	     return;
	    }

	 fire^=1;
	 if (!fire)
	    return;
	 hand^=1;

	 if (currentState.weaponAmmo[WP_RING] &&
	     !(lastInputSample & IMASK(ACTION_FIRE)))
	    currentState.weaponAmmo[WP_RING]--;
	 else
	    {if (weaponSequenceQEmpty())
		queueWeaponSequence(weaponMap[WP_RING]+1,
				    weaponCenter[WP_RING].x,
				    weaponCenter[WP_RING].y);
	     return;
	    }

	 getRay(normalizeAngle(playerAngle.yaw-(hand?F(40):F(-40))),
		normalizeAngle(playerAngle.pitch-F(20)),
		&ray);
	 orfice.x=camera->pos.x+(ray.x<<5)+(ray.x<<4);
	 orfice.y=camera->pos.y+(ray.y<<5)+(ray.y<<4);
	 orfice.z=camera->pos.z+(ray.z<<5)+(ray.z<<4);

	 getRay(normalizeAngle(playerAngle.yaw+angleOffset[pattern++]),
		playerAngle.pitch,&ray);
	 if (pattern>=6)
	    pattern=0;
	 if (weaponPowerUpCounter)
	    {ray.x<<=4; ray.z<<=4;}
	 else
	    {ray.x<<=3; ray.z<<=3;}
	 ray.y=F(12);
	 constructRingo(camera->s,
			&orfice,&ray,(SpriteObject *)player);
	 if (hand)
	    playStaticSound(ST_RING,0);
	 return;
	}
     case WP_RAVOLT:
	{MthXyz orfice,ray;
	 Object *o;
#define MAXNMVOLTTARGETS 20
	 Object *targetList[MAXNMVOLTTARGETS];
	 int targetRating[MAXNMVOLTTARGETS];
	 int nmTargets,i,j,onIdleList;
	 MonsterObject *m;
	 int dist,angle,rating,sec;

	 currentState.weaponAmmo[WP_RAVOLT]--;
	 changeColorOffset(255,255,255,30);
	 stopAllSound(1234);
	 queueWeaponSequence(weaponMap[WP_RAVOLT]+0,
			     weaponCenter[WP_RAVOLT].x,
			     weaponCenter[WP_RAVOLT].y);
	 queueWeaponSequence(weaponMap[WP_RAVOLT]+1,
			     weaponCenter[WP_RAVOLT].x,
			     weaponCenter[WP_RAVOLT].y);


	 getRay(playerAngle.yaw-F(45),playerAngle.pitch,&ray);
	 orfice=camera->pos;
	 orfice.x+=(ray.x<<5)+(ray.x<<4);
	 orfice.y+=(ray.y<<5)+(ray.y<<4);
	 orfice.z+=(ray.z<<5)+(ray.z<<4);
	 sec=findSectorContaining(&orfice,camera->s);
	 nmTargets=0;
	 onIdleList=0;
	 for (o=objectRunList;;o=o->next)
	    {if (!o)
		{if (onIdleList)
		    break;
		 onIdleList=1;
		 o=objectIdleList;
		 continue;
		}
	     if (o->class!=CLASS_MONSTER || o==(Object *)player)
		continue;
	     m=(MonsterObject *)o;
	     dist=approxDist(m->sprite->pos.x-camera->pos.x,
			     m->sprite->pos.y-camera->pos.y,
			     m->sprite->pos.z-camera->pos.z);
	     if (dist>F(2000))
		continue;
	     angle=abs(normalizeAngle(getAngle(m->sprite->pos.x-
					       camera->pos.x,
					       m->sprite->pos.z-
					       camera->pos.z)-
				      camera->angle-F(90)));
	     if (angle>F(60))
		continue;
	     rating=(dist>>2)+angle;

	     if (rating>F(700))
		continue;
	     if (!canSee(camera,m->sprite))
		continue;
	     if (nmTargets>=MAXNMVOLTTARGETS)
		break;
	     targetList[nmTargets]=o;
	     targetRating[nmTargets]=rating;
	     nmTargets++;
	    }
	 /* sort targets in order of rating */
	 for (i=1;i<nmTargets;i++)
	    {int saveRating=targetRating[i];
	     Object *saveMonster=targetList[i];
	     for (j=i;j>0;j--)
		{if (targetRating[j-1]>saveRating)
		    {targetRating[j]=targetRating[j-1];
		     targetList[j]=targetList[j-1];
		    }
		 else
		    break;
		}
	     if (j!=i)
		{targetRating[j]=saveRating;
		 targetList[j]=saveMonster;
		}
	    }
	 /* launch zaps at targets */
	 grenadeHoldTime=(grenadeHoldTime>>6)+1;
	 if (nmTargets>0)
	    {j=0;
	     for (i=0;i<grenadeHoldTime;i++)
		{constructZap(sec,&orfice,(SpriteObject *)player,
			      (MonsterObject *)targetList[j],0);
		 j++;
		 if (j>=nmTargets)
		    j=0;
		}
	    }
	 return;
	}
     case WP_COBRA:
	{MthXyz orfice,ray;
	 getRay(playerAngle.yaw,playerAngle.pitch,&ray);
	 orfice=camera->pos;
	 orfice.x+=(ray.x<<5)+(ray.x<<4);
	 orfice.y+=(ray.y<<5)+(ray.y<<4);
	 orfice.z+=(ray.z<<5)+(ray.z<<4);

	 constructCobra(findSectorContaining(&orfice,camera->s),
			orfice.x,orfice.y,orfice.z,
			normalizeAngle(playerAngle.yaw+F(90)),ray.y<<4,
			(SpriteObject *)player,OT_COBRA,0);
	 return;
	}
     case WP_FLAMER:
	{MthXyz p;
	 Fixed32 pitch,yaw;
	 MthXyz orfice,ray;
	 int heading;

	 if (!currentState.weaponAmmo[WP_FLAMER])
	    {fireWeapon();
	     break;
	    }
	 playStaticSound(ST_FLAMER,2);

	 if (!(lastInputSample & IMASK(ACTION_FIRE)))
	    currentState.weaponAmmo[WP_FLAMER]--;
	 else
	    {clearWeaponQ();
	     queueWeaponSequence(weaponMap[WP_FLAMER]+2,
				 weaponCenter[WP_FLAMER].x,
				 weaponCenter[WP_FLAMER].y);
	     stopAllSound(0);
	     playStaticSound(ST_FLAMER,1);
	     dPrint("SND!\n");
	    }

	 pitch=normalizeAngle(playerAngle.pitch-F(20));
	 yaw=playerAngle.yaw-(weaponPos[0]>>2);
	 getRay(yaw,pitch,&orfice);
	 p=camera->pos;
	 p.x+=(orfice.x<<5)+(orfice.x<<4);
	 p.y+=(orfice.y<<5)+(orfice.y<<4);
	 p.z+=(orfice.z<<5)+(orfice.z<<4);

	 heading=normalizeAngle(playerAngle.yaw+F(90));
#if 0
	 getAutoAimRay(&ray,0,0,0);
	 {/* if autoaim ray is lots different from straight ahead ray, then
	     use straight ahead ray instead */
	  MthXyz straight;
	  getRay(playerAngle.yaw,playerAngle.pitch,&straight);
	  if (MTH_Product((Fixed32 *)&straight,(Fixed32 *)&ray)<50000)
	     ray=straight;
	 }
#else
	 getRay(playerAngle.yaw,playerAngle.pitch,&ray);
#endif
	 ray.x<<=4; ray.y<<=4; ray.z<<=4;
	 constructFlameball(findSectorContaining(&p,camera->s),&p,&ray,
			    (SpriteObject *)player,heading,
			    !!(MTH_GetRand()&0x800));
	 return;
	}
     case WP_GRENADE:
	{MthXyz p;
	 MthXyz orfice,ray;
	 int heading;
	 int vel;
	 currentState.weaponAmmo[WP_GRENADE]--;
	 getRay(playerAngle.yaw,normalizeAngle(playerAngle.pitch-F(10)),
		&orfice);
	 p=camera->pos;
	 p.x+=(orfice.x<<5)+(orfice.x<<4);
	 p.y+=(orfice.y<<5)+(orfice.y<<4);
	 p.z+=(orfice.z<<5)+(orfice.z<<4);
	 heading=normalizeAngle(playerAngle.yaw+F(90));
	 vel=(grenadeHoldTime<<14)+F(20);
	 if (vel>F(50)) vel=F(50);

	 getRay(playerAngle.yaw,playerAngle.pitch,&ray);
	 ray.x=MTH_Mul(ray.x,vel);
	 ray.y=MTH_Mul(ray.y,vel);
	 ray.z=MTH_Mul(ray.z,vel);
	 ray.y+=F(8);
	 constructGrenade(findSectorContaining(&p,camera->s),
			  &p,&ray,
			  (SpriteObject *)player);
	 return;
	}
       }

 {MthXyz ray;
  MthXyz hitPos;
  int hitSector,hscan;
  /* weapons that do need hit scan */
  if (currentWeapon==WP_M60)
     {getAutoAimRay(&ray,
		    (MTH_GetRand()&0x03ffff)-F(2),
		    (MTH_GetRand()&0x03ffff)-F(2),1);
     }
  else
     getAutoAimRay(&ray,0,0,0);
  hscan=hitScan(camera,&ray,&(camera->pos),camera->s,
		&hitPos,&hitSector);

  switch (currentWeapon)
     {case WP_PISTOL:
	 {if (hscan & COLLIDE_SPRITE)
	     {hurtSprite(sprites+(hscan&0xffff),(Object *)player,15);
	     }
	  if (hscan & COLLIDE_WALL)
	     {constructKapow(hitSector,hitPos.x,hitPos.y,hitPos.z);
	      /*level_wall[hscan&0xffff].flags|=WALLFLAG_INVISIBLE; */
	     }
	  break;
	 }
      case WP_M60:
	 {if (hscan & COLLIDE_SPRITE)
	     {hurtSprite(sprites+(hscan&0xffff),(Object *)player,20);
	     }
	  if (hscan & COLLIDE_WALL)
	     constructKapow(hitSector,hitPos.x,hitPos.y,hitPos.z);
	  break;
	 }
      case WP_SWORD:
	 {int range=F(80);
	  if (hscan & COLLIDE_SPRITE)
	     range+=sprites[hscan&0xffff].radius;
	  recoverInProgress=1;
	  if (!hscan ||
	      approxDist(camera->pos.x-hitPos.x,
			 camera->pos.y-hitPos.y,
			 camera->pos.z-hitPos.z)>range)
	     {/* swing and a miss! */
	      queueWeaponSequence(weaponMap[WP_SWORD]+(swordForeSwing?10:6),
				  weaponCenter[WP_SWORD].x,
				  weaponCenter[WP_SWORD].y);
	      queueWeaponSequence(-1,0,0);
	      return;
	     }
	  if (hscan & COLLIDE_SPRITE)
	     {queueWeaponSequence(weaponMap[WP_SWORD]+(swordForeSwing?11:7),
				  weaponCenter[WP_SWORD].x,
				  weaponCenter[WP_SWORD].y);
	      queueWeaponSequence(-1,0,0);
	      hurtSprite(sprites+(hscan&0xffff),(Object *)player,20);
	     }
	  else
	     {queueWeaponSequence(weaponMap[WP_SWORD]+(swordForeSwing?12:8),
				  weaponCenter[WP_SWORD].x,
				  weaponCenter[WP_SWORD].y);
	      queueWeaponSequence(-1,0,0);
	     }
	  break;
	 }
      case WP_NMWEAPONS:
	 assert(0);
	 break;
	}
 }
}

static void weaponOut(void)
{switch (currentWeapon)
    {case WP_SWORD:
	queueWeaponSequence(weaponMap[WP_SWORD]+2,
			    weaponCenter[WP_SWORD].x,
			    weaponCenter[WP_SWORD].y);
	break;
     case WP_PISTOL:
	queueWeaponSequence(weaponMap[WP_PISTOL]+2,
			    weaponCenter[WP_PISTOL].x,
			    weaponCenter[WP_PISTOL].y);
	break;
     case WP_M60:
	queueWeaponSequence(weaponMap[WP_M60]+3,
			    weaponCenter[WP_M60].x,
			    weaponCenter[WP_M60].y);
	if (currentState.weaponAmmo[WP_M60]>1)
	   addWeaponSequence(weaponMap[WP_M60]+9);
	if (currentState.weaponAmmo[WP_M60]==1)
	   addWeaponSequence(weaponMap[WP_M60]+10);
	break;
     case WP_FLAMER:
	queueWeaponSequence(weaponMap[WP_FLAMER]+1,
			    weaponCenter[WP_FLAMER].x,
			    weaponCenter[WP_FLAMER].y);
	stopAllSound(0);
	playStaticSound(ST_FLAMER,1);
	break;
     case WP_GRENADE:
	queueWeaponSequence(weaponMap[WP_GRENADE]+1,
			    weaponCenter[WP_GRENADE].x,
			    weaponCenter[WP_GRENADE].y);
	break;
     case WP_COBRA:
	queueWeaponSequence(weaponMap[WP_COBRA]+3,
			    weaponCenter[WP_COBRA].x,
			    weaponCenter[WP_COBRA].y);
	break;
     case WP_RAVOLT:
	queueWeaponSequence(weaponMap[WP_RAVOLT]+5,
			    weaponCenter[WP_RAVOLT].x,
			    weaponCenter[WP_RAVOLT].y);
	break;
     case WP_RING:
	queueWeaponSequence(weaponMap[WP_RING]+3,
			    weaponCenter[WP_RING].x,
			    weaponCenter[WP_RING].y);
	break;
     case WP_NMWEAPONS:
	assert(0);
	break;
       }
}

static void weaponIn(void)
{switch (currentWeapon)
    {case WP_SWORD:
	queueWeaponSequence(weaponMap[WP_SWORD]+0,
			    weaponCenter[WP_SWORD].x,
			    weaponCenter[WP_SWORD].y);
	queueWeaponSequence(weaponMap[WP_SWORD]+1,
			    weaponCenter[WP_SWORD].x,
			    weaponCenter[WP_SWORD].y);
	break;
     case WP_PISTOL:
	queueWeaponSequence(weaponMap[WP_PISTOL]+0,
			    weaponCenter[WP_PISTOL].x,
			    weaponCenter[WP_PISTOL].y);
	queueWeaponSequence(weaponMap[WP_PISTOL]+1,
			    weaponCenter[WP_PISTOL].x,
			    weaponCenter[WP_PISTOL].y);
	break;
     case WP_M60:
	if (currentState.weaponAmmo[WP_M60]>1)
	   queueWeaponSequence(weaponMap[WP_M60]+11,
			       weaponCenter[WP_M60].x,
			       weaponCenter[WP_M60].y);
	if (currentState.weaponAmmo[WP_M60]==1)
	   queueWeaponSequence(weaponMap[WP_M60]+12,
			       weaponCenter[WP_M60].x,
			       weaponCenter[WP_M60].y);
	if (currentState.weaponAmmo[WP_M60]==0)
	   queueWeaponSequence(weaponMap[WP_M60]+0,
			       weaponCenter[WP_M60].x,
			       weaponCenter[WP_M60].y);
	queueWeaponSequence(weaponMap[WP_M60]+1,
			    weaponCenter[WP_M60].x,
			    weaponCenter[WP_M60].y);
	if (currentState.weaponAmmo[WP_M60]>1)
	   addWeaponSequence(weaponMap[WP_M60]+7);
	if (currentState.weaponAmmo[WP_M60]==1)
	   addWeaponSequence(weaponMap[WP_M60]+8);
	break;
     case WP_FLAMER:
	playStaticSound(ST_FLAMER,0);
	queueWeaponSequence(weaponMap[WP_FLAMER]+0,
			    weaponCenter[WP_FLAMER].x,
			    weaponCenter[WP_FLAMER].y);
	queueWeaponSequence(weaponMap[WP_FLAMER]+2,
			    weaponCenter[WP_FLAMER].x,
			    weaponCenter[WP_FLAMER].y);
	break;
     case WP_GRENADE:
	queueWeaponSequence(weaponMap[WP_GRENADE]+0,
			    weaponCenter[WP_GRENADE].x,
			    weaponCenter[WP_GRENADE].y);
	queueWeaponSequence(weaponMap[WP_GRENADE]+2,
			    weaponCenter[WP_GRENADE].x,
			    weaponCenter[WP_GRENADE].y);
	break;
     case WP_COBRA:
	queueWeaponSequence(weaponMap[WP_COBRA]+0,
			    weaponCenter[WP_COBRA].x,
			    weaponCenter[WP_COBRA].y);
	queueWeaponSequence(weaponMap[WP_COBRA]+1,
			    weaponCenter[WP_COBRA].x,
			    weaponCenter[WP_COBRA].y);
	break;
     case WP_RAVOLT:
	queueWeaponSequence(weaponMap[WP_RAVOLT]+0,
			    weaponCenter[WP_RAVOLT].x,
			    weaponCenter[WP_RAVOLT].y);
	queueWeaponSequence(weaponMap[WP_RAVOLT]+1,
			    weaponCenter[WP_RAVOLT].x,
			    weaponCenter[WP_RAVOLT].y);
	break;
     case WP_RING:
	queueWeaponSequence(weaponMap[WP_RING]+0,
			    weaponCenter[WP_RING].x,
			    weaponCenter[WP_RING].y);
	queueWeaponSequence(weaponMap[WP_RING]+1,
			    weaponCenter[WP_RING].x,
			    weaponCenter[WP_RING].y);
	break;
     case WP_NMWEAPONS:
	assert(0);
	break;
       }
}

static int yavel=0;
void weaponPlayerMove(int _yavel)
{yavel=_yavel;
 if (currentWeapon==WP_FLAMER)
    {translateFlames((camera->vel.x>>1)+(camera->vel.x>>2),
		     (camera->vel.y>>1)+(camera->vel.y>>2),
		     (camera->vel.z>>1)+(camera->vel.z>>2));
     rotateFlames((yavel>>1)+(yavel>>2),camera->pos.x,camera->pos.z);
    }
}

void runWeapon(int nmFrames,int invisible,int boost)
{int i,frame;
 int flags;
 int yoffs;

/* static int counter=0;
 if (counter++>8)
    {counter=0;
     nmFrames=1;
    }
 else
    return; */


 if (nmFrames==0)
    return;
 assert(nmFrames>0 && nmFrames<100);

 if (boost && (currentWeapon!=WP_FLAMER && currentWeapon!=WP_RING))
    nmFrames*=2;
 for (frame=0;frame<nmFrames;frame++)
    {moveWeapon();
     yoffs=0;
     if (currentWeapon==WP_GRENADE && currentState.weaponAmmo[WP_GRENADE]==0)
	yoffs=65;

     flags=advanceWeaponSequence(f(weaponPos[0]+(1<<15)),
				 f(weaponPos[1]+(1<<15))+yoffs,
				 (invisible||boost)&&currentWeapon==WP_M60);

     if (weaponSequenceQEmpty() &&
	 (currentState.desiredWeapon!=currentWeapon || !weaponOK()))
	{if (currentWeapon<WP_NMWEAPONS)
	    weaponOut();
	 queueWeaponSequence(-2,0,0);
	}

     assert(weaponSwitchTimer>=0);

     if (getCurrentWeaponSequence()==-2 && (weaponSwitchTimer++>15))
	{weaponSwitchTimer=0;
	 currentWeapon=currentState.desiredWeapon;
	 if (weaponOK())
	    weaponIn();
	}

     if (ringDepletion>0)
	{ringDepletion--;
	 if (!ringDepletion)
	    ringCharging=0;
	}

#if 0
     if (currentWeapon==WP_RING &&
	 getCurrentWeaponSequence()==weaponMap[WP_RING]+1 &&
	 getWeaponSequenceQSize()==0)
	{if (ringCharging)
	    setWeaponFrame(0);
	 else
	    setWeaponFrame(2);
	}
#endif

     if (currentWeapon==WP_SWORD &&
	 getCurrentWeaponSequence()==-1)
	{if (!(lastInputSample & IMASK(ACTION_FIRE)))
	    {swordForeSwing=!swordForeSwing;
	     recoverInProgress=0;
	     fireWeapon();
	    }
	else
	   {queueWeaponSequence(weaponMap[WP_SWORD]+4,
				weaponCenter[WP_SWORD].x,
				weaponCenter[WP_SWORD].y);
	    queueWeaponSequence(weaponMap[WP_SWORD]+1,
				weaponCenter[WP_SWORD].x,
				weaponCenter[WP_SWORD].y);
	    swordForeSwing=3;
	    recoverInProgress=0;
	   }
	}

     if (currentWeapon==WP_RAVOLT &&
	 getCurrentWeaponSequence()==weaponMap[WP_RAVOLT]+3 &&
	 getWeaponSequenceQSize()==0)
	if (lastInputSample & IMASK(ACTION_FIRE))
	   {queueWeaponSequence(weaponMap[WP_RAVOLT]+4,
				weaponCenter[WP_RAVOLT].x,
				weaponCenter[WP_RAVOLT].y);
	   }
	else
	   {int snd,grunch;
	    static struct soundSlotRegister *afterTouch;
	    struct soundSlotRegister ssr;
	    initSoundRegs(level_staticSoundMap[ST_MANACLE],32,0,&ssr);
	    if (grenadeHoldTime>(5<<6)+10)
	       grenadeHoldTime=(5<<6)+10;
	    snd=256+(grenadeHoldTime<<4);
	    grunch=(snd&0x3ff)|((snd&0x3c00)<<1);
	    ssr.reg[8]=grunch;
	    if ((grenadeHoldTime&0x3f)==0)
	       {int step=grenadeHoldTime>>6;
		if (step<1/*5*/)
		   {stopAllSound(1234);
		    ssr.reg[4]=(ssr.reg[4]&(~0x1f))|0x8; /* lower attack */
		    afterTouch=playSoundMegaE(1234,&ssr);
		    playSound(1234,level_staticSoundMap[ST_MANACLE]+1);
		   }
	       }
	    else
	       afterTouch->reg[8]=grunch;
	    grenadeHoldTime+=2;
	   }


     if (currentWeapon==WP_GRENADE &&
	 getCurrentWeaponSequence()==-1)
	if (lastInputSample & IMASK(ACTION_FIRE))
	   {queueWeaponSequence(weaponMap[WP_GRENADE]+4,
				weaponCenter[WP_GRENADE].x,
				weaponCenter[WP_GRENADE].y-17);
	    if (currentState.weaponAmmo[WP_GRENADE])
	       {queueWeaponSequence(weaponMap[WP_GRENADE]+0,
				    weaponCenter[WP_GRENADE].x,
				    weaponCenter[WP_GRENADE].y);
		queueWeaponSequence(weaponMap[WP_GRENADE]+2,
				    weaponCenter[WP_GRENADE].x,
				    weaponCenter[WP_GRENADE].y);
	       }
	    else
	       queueWeaponSequence(-2,0,0);
	   }
	else
	   grenadeHoldTime++;

     if (flags)
	weaponFire();
    }
 if (currentWeapon==WP_FLAMER)
    if (getCurrentWeaponSequence()==weaponMap[WP_FLAMER]+2 && !invisible)
       {static int flameFrame=0;
	static int flameClock=0;
	const int flameSeq=weaponMap[WP_FLAMER]+3;
	XyInt pos[4];
	sChunkType *c;
	flameClock-=nmFrames;
	while (flameClock<0)
	   {flameClock+=2;
	    flameFrame++;
	    if (flameFrame+level_wSequence[flameSeq]>=
		level_wSequence[flameSeq+1])
	       flameFrame=0;
	   }
	c=level_wChunk+
	   level_wFrame[flameFrame+level_wSequence[flameSeq]].chunkIndex;
	{int angle=yavel<<3;
	 int x,y;
	 pos[0].x=0; pos[0].y=0;
	 pos[1].x=64; pos[1].y=0;
	 pos[2].x=64; pos[2].y=64;
	 pos[3].x=0; pos[3].y=64;
	 for (i=0;i<4;i++)
	    {x=pos[i].x-c->chunkx-5; y=pos[i].y-(c->chunky+40);
	     pos[i].x=MTH_Mul(MTH_Cos(angle),x)-MTH_Mul(MTH_Sin(angle),y)-
		2;
	     pos[i].y=MTH_Mul(MTH_Cos(angle),y)+MTH_Mul(MTH_Sin(angle),x)+
		50+f(weaponPos[1]);
	    }
	}
	assert(getPicClass(c->tile)==TILE8BPP);
	EZ_distSpr(0,UCLPIN_ENABLE|COLOR_4|HSS_ENABLE|ECD_DISABLE,
		   0,mapPic(c->tile),pos,NULL);
       }
}

int getCurrentWeapon(void)
{return currentWeapon;
}


void setCurrentWeapon(int i)
{if (i<0) return;
 if (i>=WP_NMWEAPONS) return;
 assert(i>=0);
 assert(i<WP_NMWEAPONS);
 currentState.desiredWeapon=i;
}

void weaponUp(int weaponMask)
{int newD;
 if (level_sector[camera->s].flags & SECFLAG_WATER)
    weaponMask&=UNDERWATERWEAPONS;
 newD=WEP_NEXT(weaponMask,currentState.desiredWeapon);
 if (newD!=-1)
   {currentState.desiredWeapon=newD;
    weaponSwitchTimer=0;
   }
}

void weaponDown(int weaponMask)
{int newD;
 if (level_sector[camera->s].flags & SECFLAG_WATER)
    weaponMask&=UNDERWATERWEAPONS;
 newD=bitScanBackwards(weaponMask,currentState.desiredWeapon);
 if (newD!=-1)
    {currentState.desiredWeapon=newD;
     weaponSwitchTimer=0;
    }
}

void weaponChangeAmmo(int weapon,int deltaAmmo)
{int flag;
 flag=0;
 if (weapon==WP_M60 && currentWeapon==WP_M60 &&
     currentState.weaponAmmo[WP_M60]<2)
    {weaponOut();
     flag=1;
    }
 currentState.weaponAmmo[weapon]+=deltaAmmo;
 if (flag)
    weaponIn();
}

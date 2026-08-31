/* stuff to do:

     make movie text background be dark blue w/border
     sync movie text
     mummy improvments (shootable snakes, charge)

     rewrite rest of render loop in lisp

     problem: water surface walls can be near clipped out when still visible,
     and never get to the water specific routines

     fix triangle problem

     control changes- run & strafe configuration (w/ always run)
     secret push door
     light fish
 */


#include <machine.h>

#include <libsn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
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

#include "util.h"
#include "spr.h"
#include "file.h"
#include "v_blank.h"
#include "level.h"
#include "art.h"
#include "print.h"
#include "sprite.h"
#include "map.h"
#include "pic.h"
#include "walls.h"
#include "sequence.h"
#include "sound.h"
#include "object.h"
#include "ai.h"
#include "hitscan.h"
#include "route.h"
#include "menu.h"
#include "bup.h"
#include "megainit.h"
#include "weapon.h"
#include "dma.h"
#include "local.h"
#include "bigmap.h"
#include "profile.h"
#include "gamestat.h"
#include "intro.h"
#include "mov.h"
#include "plax.h"
#include "airbub.c"
#include "initmain.h"
#include "aicommon.h"

#ifdef JAPAN
#undef STATUSTEXT
#endif

extern int end;

SaveState currentState;

PlayerObject *player;
Sprite *camera;

Orient playerAngle;

int mapOn;
int quitRequest;

int debugFlag=0;

Fixed32 xavel = 0, yavel = 0;

#define MAXTVELOCITY 130000
#define MAXRTVELOCITY 200000
#define VELOCITY 48
#define RUNVELOCITY (VELOCITY*2)
#define TURNVELOCITY 10000
#define RTURNVELOCITY 12000
#define RTURNFRICTION 15000
#define TURNFRICTION 65000
#define SANDALJUMPVEL (60<<13)
#define NORMALJUMPVEL (39<<13)

/* game status variables */
static int nmFullBowls=0;
int keyMask=0;

static int playerIsDead;
static int playerMotionEnable;
static int hitCamel,hitPyramid,hitTeleport;
static int stunCounter=0;
#define INVISIBLEDOSE (30*30)
int invisibleCounter=0;
#define WEAPONPOWERDOSE (30*30)
int weaponPowerUpCounter=0;

static int deathTimer;

void redrawBowlDots(void);

static int colorOffset[3]={0,0,0};
static int colorCenter[3]={0,0,0};
static int colorStepRate=3;

int getKeyMask(void)
{return keyMask;
}

void stepColorOffset(void)
{int i;
 for (i=0;i<3;i++)
    {if (colorOffset[i]-colorStepRate>colorCenter[i])
	colorOffset[i]-=colorStepRate;
     else
	if (colorOffset[i]+colorStepRate<colorCenter[i])
	   colorOffset[i]+=colorStepRate;
	else
	   {colorOffset[i]=colorCenter[i];
	    colorStepRate=3;
	   }
    }
 SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0|SCL_RBG0,
		  colorOffset[0],colorOffset[1],colorOffset[2]);
}

void changeColorOffset(int r,int g,int b,int rate)
{colorOffset[0]=r;
 colorOffset[1]=g;
 colorOffset[2]=b;
 colorStepRate=rate;
}

void addColorOffset(int r,int g,int b)
{colorOffset[0]+=r;
 colorOffset[1]+=g;
 colorOffset[2]+=b;
}

static int ouchTime=0;
void playerHurt(int hpLost)
{int i;
 if (hpLost<=0)
    return;
 if (playerIsDead)
    return;
#ifdef JAPAN
 hpLost=(hpLost*3)>>2;
#endif
 currentState.health-=hpLost;
 colorOffset[0]=63;
 colorOffset[1]=-63;
 colorOffset[2]=-63;
 if (ouchTime<=0)
    {i=getNextRand()&0x1;
     playStaticSound(ST_JOHN,3+i);
     ouchTime=60;
    }
}

static int ltHurtAmount=0;
static int ltHurtTime=0;
void playerLongHurt(int lava)
{if (lava)
    {if (ltHurtTime==0)
	playSound(69,level_staticSoundMap[ST_JOHN]+7);
     if (currentState.inventory & INV_ANKLET)
	ltHurtAmount=2;
     else
	ltHurtAmount=20;
    }
 else
    {if (currentState.inventory & INV_ANKLET)
	ltHurtAmount=0;
     else
	{if (ltHurtTime==0)
	    playSound(69,level_staticSoundMap[ST_JOHN]+7);
	 ltHurtAmount=20;
	}
    }
 ltHurtTime=30;
}

static void greenFlash(void)
{colorOffset[1]+=32;
}

void switchPlayerMotion(int state)
{playerMotionEnable=state;
}

int playerHeightOffset=0,playerHeightVel=0;

static void stepPlayerHeight(void)
{if (playerIsDead)
    {if (playerHeightOffset>F(31) &&
	 abs(playerHeightVel)<1<<15)
	return;
     playerHeightOffset+=playerHeightVel;
     playerHeightVel+=1<<14;
     if (playerHeightOffset>F(32))
	{playerHeightOffset=F(32);
	 playerHeightVel=-playerHeightVel>>2;
	}
     return;
    }
 playerHeightOffset+=playerHeightVel;
 playerHeightVel-=playerHeightOffset>>3;
 weaponForce(0,playerHeightOffset>>4);
 playerHeightVel=MTH_Mul(playerHeightVel,65536*0.6);
 if (abs(playerHeightVel)<1<<13 &&
     abs(playerHeightOffset)<1<<13)
    {playerHeightVel=0;
     playerHeightOffset=0;
    }
}

void playerDYChange(Fixed32 dvel)
{int ouch;
 if (dvel<F(1))
    return;
 if (dvel>F(15))
    {ouch=currentState.nmBowls*
	f(MTH_Mul(dvel-F(12),dvel-F(12)));
     playerHurt(ouch);
    }
 playerHeightVel+=dvel;
/* weaponForce(0,dvel>>1);*/
}

void underWaterControl(unsigned short input)
{Fixed32 dir = 0, vel, tvel;
 Fixed32 maxTVel,walking;
 MthXyz ray;
 static int swimTime=0;
 int run,turningLeft=0,turningRight=0;
 MthXyz force;
 force.x=0; force.y=0; force.z=0;
 dir=playerAngle.yaw;
 walking=0;
 vel = VELOCITY;
 tvel=TURNVELOCITY;
 maxTVel=MAXTVELOCITY;
 run=0;
 vel=vel>>1;

 camera->gravity=0;

 if (analogControlerPresent)
    {xavel=(15*xavel+(analogY*1700))>>4;

     if (analogY<3 && xavel>0)
	{xavel-=RTURNFRICTION;
	 if (xavel<0)
	    xavel=0;
	}

     if (analogY>-3 && xavel<0)
	{xavel+=RTURNFRICTION;
	 if (xavel>0)
	    xavel=0;
	}
     if( xavel < -maxTVel) xavel = -maxTVel;
     if( xavel > maxTVel) xavel = maxTVel;
    }
 else
    {if ((input & PER_DGT_D) == 0)
	{xavel += TURNVELOCITY;
	 if (xavel>maxTVel) xavel=maxTVel;
	}
    else
       if (xavel>0)
	  {if (xavel>=TURNFRICTION)
	      xavel-=TURNFRICTION;
	  else
	     xavel=0;
	  }
     if ((input & PER_DGT_U) == 0)
	{xavel -= TURNVELOCITY;
	 if (xavel<-maxTVel) xavel=-maxTVel;
	}
     else
	{if (xavel<0)
	    {if (xavel<=TURNFRICTION)
		xavel+=TURNFRICTION;
	    else
	       xavel=0;
	    }
	}
    }

 if (analogIndexButtonsPresent && IMASK(ACTION_STRAFE)==PER_DGT_TL)
    {force.x-=(MTH_Cos(dir)*analogTL)>>4;
     force.z-=(MTH_Sin(dir)*analogTL)>>4;
    }
 else
    if (!(input & IMASK(ACTION_STRAFE)))
       {force.x-=(MTH_Cos(dir)*60)>>2;
	force.z-=(MTH_Sin(dir)*60)>>2;
       }
 if (analogIndexButtonsPresent && IMASK(ACTION_RUN)==PER_DGT_TR)
    {force.x+=(MTH_Cos(dir)*analogTR)>>4;
     force.z+=(MTH_Sin(dir)*analogTR)>>4;
    }
 else
    if (!(input & IMASK(ACTION_RUN)))
       {force.x+=(MTH_Cos(dir)*60)>>2;
	force.z+=(MTH_Sin(dir)*60)>>2;
       }

 if (analogControlerPresent)
    {yavel=(15*yavel+(-analogX*1700))>>4;

     if (analogX>-3 && yavel>0)
	{yavel-=RTURNFRICTION;
	 if (yavel<0)
	    yavel=0;
	}

     if (analogX<3 && yavel<0)
	{yavel+=RTURNFRICTION;
	 if (yavel>0)
	    yavel=0;
	}


     if( yavel < -maxTVel) yavel = -maxTVel;
     if( yavel > maxTVel) yavel = maxTVel;
    }
 else
    {if ((input & PER_DGT_R) == 0)
	{yavel -= tvel;
	 if( yavel < -maxTVel) yavel = -maxTVel;
	 turningRight=1;
	}

     if (!turningRight)
	if (yavel<0)
	   {if (yavel<=-RTURNFRICTION)
	       yavel+=RTURNFRICTION;
	   else
	      yavel=0;
	   }

     if ((input & PER_DGT_L) == 0)
	{yavel += tvel;
	 if( yavel > maxTVel) yavel = maxTVel;
	 turningLeft=1;
	}

     if (!turningLeft)
	{if (yavel>0)
	    if (yavel>=RTURNFRICTION)
	       yavel-=RTURNFRICTION;
	    else
	       yavel=0;
	}
    }

 if ((input & IMASK(ACTION_JUMP)) == 0)
    {ray.x=-MTH_Sin(playerAngle.yaw); ray.y=0; ray.z=MTH_Cos(playerAngle.yaw);
     ray.x=MTH_Mul(ray.x,MTH_Cos(playerAngle.pitch));
     ray.z=MTH_Mul(ray.z,MTH_Cos(playerAngle.pitch));
     ray.y=MTH_Sin(playerAngle.pitch);
     force.x=ray.x<<4;
     force.y=ray.y<<4;
     force.z=ray.z<<4;

     swimTime++;
#if 1
     if (swimTime>30)
	{force.x=ray.x<<2;
	 force.y=ray.y<<2;
	 force.z=ray.z<<2;
	}
#endif
     if (swimTime>50)
	swimTime=0;
    }
 else
    swimTime=0;

 if (force.x || force.y || force.z)
    {camera->vel.x=(31*camera->vel.x+force.x)>>5;
     camera->vel.y=(31*camera->vel.y+force.y)>>5;
     camera->vel.z=(31*camera->vel.z+force.z)>>5;
    }

 if (camera->vel.y<-160<<15)
    camera->vel.y=-160<<15;

 playerAngle.roll=-((Fixed32)yavel)<<2;

 if (!(level_sector[camera->s].flags & SECFLAG_WATER))
    {int d;
     playerAngle.roll=0;
     d=findFloorDistance(camera->s,&camera->pos);
     if (d<F(14))
	camera->pos.y+=F(1);
     if (camera->vel.y>0)
	camera->vel.y=0;

     if (playerAngle.pitch>0)
	{if (xavel>0)
	    xavel=0;
	 playerAngle.pitch-=F(4);
	 if (playerAngle.pitch<0)
	    playerAngle.pitch=0;
	}
    }

}

void controlInput(unsigned short input,unsigned short changeInput)
{Fixed32 dir = 0, vel, tvel;
 Fixed32 maxTVel,walking;
 static int walkTime=0,hoverTime=0;
 static int shawlActive=0;
 static int dirChange=0;
 int turningLeft=0,turningRight=0,turningUp=0,turningDown=0;
 MthXyz force;

 force.x=0; force.y=0; force.z=0;
 dir=playerAngle.yaw;
 walking=0;
 vel=RUNVELOCITY;
 tvel=RTURNVELOCITY;
 maxTVel=MAXRTVELOCITY;

 camera->gravity=GRAVITY;

 if (dirChange==3)
    {if (abs(playerAngle.pitch)<F(1))
	{playerAngle.pitch=0;
	 dirChange=0;
	}
     else
	{playerAngle.pitch-=playerAngle.pitch>>3;
	 if (playerAngle.pitch<0)
	    playerAngle.pitch+=1<<14;
	 else
	    playerAngle.pitch-=1<<14;
	}
    }
 if ((input & IMASK(ACTION_FREELOC)) == 0)
    {if (!dirChange || dirChange==3)
	dirChange=1;
     if (analogControlerPresent)
	{if (analogY<-3)
	    {xavel+=analogY<<6;
	     if( xavel < -maxTVel) xavel = -maxTVel;
	     dirChange=2;
	     turningDown=1;
	    }
	 if (analogY>3)
	    {xavel+=analogY<<6;
	     if( xavel > maxTVel) xavel = maxTVel;
	     dirChange=2;
	     turningUp=1;
	    }
	}
     else
	{if ((input & PER_DGT_D) == 0)
	    {xavel += TURNVELOCITY;
	     if( xavel > maxTVel) xavel = maxTVel;
	     dirChange=2;
	     turningUp=1;
	    }
	 if ((input & PER_DGT_U) == 0)
	    {xavel -= TURNVELOCITY;
	     if( xavel < -maxTVel) xavel = -maxTVel;
	     dirChange=2;
	     turningDown=1;
	    }
	}
    }
 else
    {if (dirChange && dirChange!=3)
	{if (dirChange==1)
	   dirChange=3;
	 else
	   dirChange=0;
	}
     if (analogControlerPresent)
	{force.x+=(MTH_Sin(dir)*analogY*3)>>4;
	 force.z-=(MTH_Cos(dir)*analogY*3)>>4;
	 if (abs(analogY)>64)
	    walking=1;
	}
     else
	{if ((input & PER_DGT_U) == 0)
	    {force.x += (MTH_Sin( dir ) * -vel)>>2;
	     force.z += (MTH_Cos( dir ) * vel)>>2;
	     walking=1;
	    }
	 if ((input & PER_DGT_D) == 0)
	    {force.x += (MTH_Sin( dir ) * vel)>>2;
	     force.z += (MTH_Cos( dir ) * -vel)>>2;
	     walking=1;
	    }
	}
    }

 if (!turningUp)
    {if (xavel>0)
	{if (xavel>=TURNFRICTION)
	    xavel-=TURNFRICTION;
	else
	   xavel=0;
	}
    }
 if (!turningDown)
    {if (xavel<0)
	{if (xavel<=TURNFRICTION)
	    xavel+=TURNFRICTION;
	else
	   xavel=0;
	}
    }

 if (analogIndexButtonsPresent && IMASK(ACTION_STRAFE)==PER_DGT_TL)
    {force.x-=(MTH_Cos(dir)*analogTL)>>4;
     force.z-=(MTH_Sin(dir)*analogTL)>>4;
    }
 else
    if (!(input & IMASK(ACTION_STRAFE)))
       {force.x-=(MTH_Cos(dir)*60)>>2;
	force.z-=(MTH_Sin(dir)*60)>>2;
       }
 if (analogIndexButtonsPresent && IMASK(ACTION_RUN)==PER_DGT_TR)
    {force.x+=(MTH_Cos(dir)*analogTR)>>4;
     force.z+=(MTH_Sin(dir)*analogTR)>>4;
    }
 else
    if (!(input & IMASK(ACTION_RUN)))
       {force.x+=(MTH_Cos(dir)*60)>>2;
	force.z+=(MTH_Sin(dir)*60)>>2;
       }

 if (analogControlerPresent)
    {yavel=(15*yavel+(-analogX*1700))>>4;

     if (analogX>-3 && yavel>0)
	{yavel-=RTURNFRICTION;
	 if (yavel<0)
	    yavel=0;
	}

     if (analogX<3 && yavel<0)
	{yavel+=RTURNFRICTION;
	 if (yavel>0)
	    yavel=0;
	}


     if( yavel < -maxTVel) yavel = -maxTVel;
     if( yavel > maxTVel) yavel = maxTVel;
    }
 else
    {if ((input & PER_DGT_R) == 0)
	{yavel -= tvel;
	 weaponForce(-1<<15,0);
	 if( yavel < -maxTVel) yavel = -maxTVel;
	 turningRight=1;
	}

     if (!turningRight)
	if (yavel<0)
	   {if (yavel<=-RTURNFRICTION)
	       yavel+=RTURNFRICTION;
	   else
	      yavel=0;
	   }

     if ((input & PER_DGT_L) == 0)
	{yavel += tvel;
	 weaponForce(1<<15,0);
	 if( yavel > maxTVel) yavel = maxTVel;
	 turningLeft=1;
	}

     if (!turningLeft)
	{if (yavel>0)
	    if (yavel>=RTURNFRICTION)
	       yavel-=RTURNFRICTION;
	    else
	       yavel=0;
	}
    }

 if (((input & IMASK(ACTION_JUMP)) == 0) &&
     !(camera->flags & SPRITEFLAG_ONSLIPPERYSLOPE))
    {camera->vel.y+=3<<12;
     if (changeInput & IMASK(ACTION_JUMP))
	if (camera->floorSector!=-1)
	   {if (currentState.inventory & INV_SANDALS)
	       camera->vel.y=SANDALJUMPVEL;
	    else
	       camera->vel.y=NORMALJUMPVEL;
	    playStaticSound(ST_JOHN,0);
	    weaponForce(0,1<<17);
	   }
	else
	   {if (currentState.inventory & (INV_SHAWL|INV_FEATHER))
	       {shawlActive=1;
		hoverTime=0;
	       }
	   }
    }
 else
    shawlActive=0;

 if (!(camera->flags & SPRITEFLAG_ONSLIPPERYSLOPE))
    if (force.x || force.z)
       {camera->vel.x=(15*camera->vel.x+force.x)>>4;
	camera->vel.z=(15*camera->vel.z+force.z)>>4;
       }

 if (shawlActive)
    {if (currentState.inventory & INV_FEATHER)
	{if (camera->vel.y<camera->gravity)
	    {playerHeightVel+=camera->gravity-camera->vel.y;
	     camera->vel.y=camera->gravity;
	     hoverTime+=3;
	     if (hoverTime>360)
		hoverTime=0;
	     playerHeightVel+=MTH_Sin(F(hoverTime-180))>>2;
	    }
	}
     else
	if (camera->vel.y<-4<<15)
	   camera->vel.y=-4<<15;
    }
 else
    if (camera->vel.y<F(-80))
       {camera->vel.y=F(-80);
       }

 if (walking)
    {walkTime++;
     if (walkTime>20)
	{weaponForce(0,1<<14);
	 playerHeightVel-=1<<13;
	}
     if (walkTime>40)
	walkTime=0;
    }
 else
    walkTime=0;

 playerAngle.roll=-((Fixed32)yavel);
}

void dollPowerControlInput(unsigned short input,unsigned short changeInput)
{Fixed32 dir = 0, vel, tvel;
 Fixed32 maxTVel,walking;
 MthXyz ray;
 int turningLeft=0,turningRight=0,turningUp=0,turningDown=0;
 static int hoverTime;
 MthXyz force;

 force.x=0; force.y=0; force.z=0;
 dir=playerAngle.yaw;
 walking=0;
 vel=RUNVELOCITY;
 tvel=RTURNVELOCITY;
 maxTVel=MAXRTVELOCITY;

 camera->gravity=0;
  if (analogControlerPresent)
    {xavel=(15*xavel+(analogY*1700))>>4;

     if (analogY<3 && xavel>0)
	{xavel-=RTURNFRICTION;
	 if (xavel<0)
	    xavel=0;
	}

     if (analogY>-3 && xavel<0)
	{xavel+=RTURNFRICTION;
	 if (xavel>0)
	    xavel=0;
	}

     if( xavel < -maxTVel) xavel = -maxTVel;
     if( xavel > maxTVel) xavel = maxTVel;
    }
 else
    {if ((input & PER_DGT_D) == 0)
	{xavel += TURNVELOCITY;
	 if( xavel > maxTVel) xavel = maxTVel;
	 turningUp=1;
	}
     if ((input & PER_DGT_U) == 0)
	{xavel -= TURNVELOCITY;
	 if( xavel < -maxTVel) xavel = -maxTVel;
	 turningDown=1;
	}
     if (!turningUp)
	{if (xavel>0)
	    {if (xavel>=TURNFRICTION)
		xavel-=TURNFRICTION;
	    else
	       xavel=0;
	    }
	}
     if (!turningDown)
	{if (xavel<0)
	    {if (xavel<=TURNFRICTION)
		xavel+=TURNFRICTION;
	    else
	       xavel=0;
	    }
	}
    }

 if (analogIndexButtonsPresent && IMASK(ACTION_STRAFE)==PER_DGT_TL)
    {force.x-=(MTH_Cos(dir)*analogTL)>>4;
     force.z-=(MTH_Sin(dir)*analogTL)>>4;
    }
 else
    if (!(input & IMASK(ACTION_STRAFE)))
       {force.x-=(MTH_Cos(dir)*60)>>2;
	force.z-=(MTH_Sin(dir)*60)>>2;
       }
 if (analogIndexButtonsPresent && IMASK(ACTION_RUN)==PER_DGT_TR)
    {force.x+=(MTH_Cos(dir)*analogTR)>>4;
     force.z+=(MTH_Sin(dir)*analogTR)>>4;
    }
 else
    if (!(input & IMASK(ACTION_RUN)))
       {force.x+=(MTH_Cos(dir)*60)>>2;
	force.z+=(MTH_Sin(dir)*60)>>2;
       }

  if (analogControlerPresent)
    {yavel=(15*yavel+(-analogX*1700))>>4;

     if (analogX>-3 && yavel>0)
	{yavel-=RTURNFRICTION;
	 if (yavel<0)
	    yavel=0;
	}

     if (analogX<3 && yavel<0)
	{yavel+=RTURNFRICTION;
	 if (yavel>0)
	    yavel=0;
	}


     if( yavel < -maxTVel) yavel = -maxTVel;
     if( yavel > maxTVel) yavel = maxTVel;
    }
 else
    {if ((input & PER_DGT_R) == 0)
	{yavel -= tvel;
	 weaponForce(-1<<15,0);
	 if( yavel < -maxTVel) yavel = -maxTVel;
	 turningRight=1;
	}

     if (!turningRight)
	if (yavel<0)
	   {if (yavel<=-RTURNFRICTION)
	       yavel+=RTURNFRICTION;
	   else
	      yavel=0;
	   }

     if ((input & PER_DGT_L) == 0)
	{yavel += tvel;
	 weaponForce(1<<15,0);
	 if( yavel > maxTVel) yavel = maxTVel;
	 turningLeft=1;
	}

     if (!turningLeft)
	{if (yavel>0)
	    if (yavel>=RTURNFRICTION)
	       yavel-=RTURNFRICTION;
	    else
	       yavel=0;
	}
    }

 if (!(input & IMASK(ACTION_JUMP)))
    {ray.x=-MTH_Sin(playerAngle.yaw); ray.y=0; ray.z=MTH_Cos(playerAngle.yaw);
     ray.x=MTH_Mul(ray.x,MTH_Cos(playerAngle.pitch));
     ray.z=MTH_Mul(ray.z,MTH_Cos(playerAngle.pitch));
     ray.y=MTH_Sin(playerAngle.pitch);
     force.x=ray.x<<4;
     force.y=ray.y<<4;
     force.z=ray.z<<4;
    }

 if (force.x || force.z || force.y)
    {camera->vel.x=(15*camera->vel.x+force.x)>>4;
     camera->vel.y=(15*camera->vel.y+force.y)>>4;
     camera->vel.z=(15*camera->vel.z+force.z)>>4;
    }

 playerAngle.roll=-((Fixed32)yavel)<<2;
 hoverTime+=3;
 if (hoverTime>360)
    hoverTime=0;
 playerHeightVel+=MTH_Sin(F(hoverTime-180))>>2;
}


static void push(void)
{int hscan;
 MthXyz pos,ray,collidePos;
 Fixed32 yaw,pitch;
 int sector;

 pos=camera->pos;
 yaw=playerAngle.yaw;
 pitch=playerAngle.pitch;
 ray.x=-MTH_Sin(yaw); ray.y=0; ray.z=MTH_Cos(yaw);
 ray.x=MTH_Mul(ray.x,MTH_Cos(pitch));
 ray.z=MTH_Mul(ray.z,MTH_Cos(pitch));
 ray.y=MTH_Sin(pitch);
 hscan=hitScan(camera,&ray,&pos,camera->s,&collidePos,&sector);

 if ((hscan & COLLIDE_WALL) &&
     approxDist(camera->pos.x-collidePos.x,
		camera->pos.y-collidePos.y,
		camera->pos.z-collidePos.z)<F(120))
    {/* we hit a wall */
     dPrint("hit wall %d\n",hscan&0xffff);
     if (level_wall[hscan & 0xffff].object)
	{signalObject((Object *)(level_wall[hscan & 0xffff].object),
		      SIGNAL_PRESS,(int)(&collidePos),0);
	}
    }
}

void movePlayer(int inputEnd,int nmFrames)
{unsigned short input;
 unsigned short changeInput=0;
 unsigned short pushed;
 static unsigned short lastInput=0;
 static char wasUnderWater=0;
 int inputPos,i;
 inputPos=inputEnd-nmFrames;
 if (inputPos<0) inputPos+=INPUTQSIZE;
 while (inputPos!=inputEnd)
    {/* control input */
     input=inputQ[inputPos];

     if (mapOn && !(input & IMASK(ACTION_PUSH)))
	{if (!(input & PER_DGT_L))
	    {mapScaleUp();
	     input|=PER_DGT_L;
	    }
	 if (!(input & PER_DGT_R))
	    {mapScaleDn();
	     input|=PER_DGT_R;
	    }
	}

     changeInput=lastInput ^ input;
     pushed=changeInput&~input;

     if (!playerMotionEnable || stunCounter)
	{if (stunCounter)
	    stunCounter--;
	 if (playerIsDead)
	    {playerIsDead++;
	     i=15-((playerIsDead-120)>>2);
	     if (i<0) i=0;
	     if (i>15)i=15;
	     setMasterVolume(i);
	     currentState.health=0;
	     weaponSetVel(0,F(4));
	     if (playerAngle.pitch<F(80))
		{xavel+=1<<12;
		 playerAngle.pitch += xavel;
		 if (playerAngle.pitch>F(90)) playerAngle.pitch=F(90);
		 if (playerAngle.pitch<F(-90)) playerAngle.pitch=F(-90);
		}
	     else
		xavel=0;
	    }
	}
     else
	{if (camera->flags & SPRITEFLAG_UNDERWATER)
	    {if (currentState.gameFlags & GAMEFLAG_DOLLPOWERMODE)
		dollPowerControlInput(input,changeInput);
	     else
		underWaterControl(input);
	     wasUnderWater=5;
	     if ((level_sector[camera->s].flags & SECFLAG_WATER) &&
		 (getNextRand())<3000)
		{/* decide which sector to put bubble in */
		 int s=camera->s;
		 int i,j,k;
		 /* start in the player's sector and random walk away */
		 for (i=0;i<10;i++)
		    {k=level_sector[s].firstWall;
		     for (j=getNextRand()&0xf;j>=0;j--)
			{k++;
			 if (k>level_sector[s].lastWall ||
			     level_wall[k].nextSector==-1)
			    k=level_sector[s].firstWall;
			}
		     if (level_sector[level_wall[k].nextSector].flags &
			 SECFLAG_WATER)
			s=level_wall[k].nextSector;
		     assert(s>=0);
		    }
		 /* now s=sector to put bubble in */
		 {MthXyz pos,v;
		  pos.x=F(level_sector[s].center[0]);
		  pos.y=F(level_sector[s].center[1]);
		  pos.z=F(level_sector[s].center[2]);
		  k=level_sector[s].firstWall;
		  for (j=getNextRand()&0xf;j>=0;j--)
		     {k++;
		      if (k>level_sector[s].lastWall)
			 k=level_sector[s].firstWall;
		     }
		  getVertex(level_wall[k].v[0],&v);
		  pos.x=(pos.x+v.x)>>1;
		  pos.z=(pos.z+v.z)>>1;
		  pos.y-=findFloorDistance(s,&pos);
		  constructBubble(s,&pos,findCeilDistance(s,&pos));
		 }
		}
	    }
	else
	   {if (wasUnderWater)
	       {wasUnderWater--;
		input&=~PER_DGT_U;
		camera->vel.y+=(int)(0.45*65536);
	       }
	    if (currentState.gameFlags & GAMEFLAG_DOLLPOWERMODE)
	       dollPowerControlInput(input,changeInput);
	    else
	       controlInput(input,changeInput);
	   }
	 if (currentState.health<=0)
	    {playerIsDead=1;
	     colorCenter[0]=-255;
	     colorCenter[1]=-255;
	     colorCenter[2]=-255;

	     playStaticSound(ST_JOHN,1);
	     switchPlayerMotion(0);
	     xavel=0;
	     yavel=0;
	     camera->vel.y+=F(6);
	     camera->vel.x+=MTH_Sin(playerAngle.yaw)*5;
	     camera->vel.z-=MTH_Cos(playerAngle.yaw)*5;
	     deathTimer=0;
	    }

	 if (!(input & IMASK(ACTION_FIRE)))
	    {if (weaponSequenceQEmpty())
		fireWeapon();
	    }

	 if (pushed&IMASK(ACTION_WEPUP))
	    {weaponUp(WEAPONINV(currentState.inventory));
	     redrawBowlDots();
	    }

	 if (pushed&IMASK(ACTION_WEPDN))
	    {weaponDown(WEAPONINV(currentState.inventory));
	     redrawBowlDots();
	    }

	 if (pushed&IMASK(ACTION_PUSH))
	    {push();
	     debugFlag=!debugFlag;
	     dumpProfileData();
#if 0
	     {int sec;
	      Sprite *s;
	      Sprite *bestSprite;
	      Fixed32 minDist,d;
	      minDist=INT_MAX;
	      for (sec=0;sec<level_nmSectors;sec++)
		 {for (s=sectorSpriteList[sec];s;s=s->next)
		     {if (s==camera)
			 continue;
		      d=approxDist(s->pos.x-camera->pos.x,
				   s->pos.y-camera->pos.y,
				   s->pos.z-camera->pos.z);
		      if (d<minDist)
			 {minDist=d;
			  bestSprite=s;
			 }

		     }
		 }
	      dPrint("best sprite type=%d sector=%d\n",
		     bestSprite->owner->type,bestSprite->s);
	     }
#endif
	    }

	 if (cheatsEnabled)
	    if (!(input&(PER_DGT_A|PER_DGT_C)))
	       camera->vel.y=F(3);

	 /* player angle update */
	 playerAngle.yaw += yavel;
	 playerAngle.pitch += xavel;
	 if( playerAngle.yaw > F( 180 )) playerAngle.yaw -= F( 360 );
	 if( playerAngle.yaw < F( -180 )) playerAngle.yaw += F( 360 );
	 if( playerAngle.pitch > F(90)) playerAngle.pitch = F( 90 );
	 if( playerAngle.pitch < F( -90 )) playerAngle.pitch = F( -90 );
	}

     /* player position update */
     {int flags=camera->flags;
      if (currentState.gameFlags & GAMEFLAG_DOLLPOWERMODE)
	 camera->flags|=SPRITEFLAG_UNDERWATER;
      moveCamera();
      if (currentState.gameFlags & GAMEFLAG_DOLLPOWERMODE)
	 camera->flags=flags;
     }
     weaponPlayerMove(yavel);
     lastInput=input;
     inputPos++;
     if (inputPos==INPUTQSIZE)
	inputPos=0;
    }
}

void setVDP2(void)
{static Uint16 cycle[]=
   {0xeeee, 0xeeee,
    0xeeee, 0xeeee,
    0x44ee, 0xeeee,
    0x44ee, 0xeeee};

 SclVramConfig vcfg;
 SCL_InitVramConfigTb(&vcfg);
 vcfg.vramModeA=ON;
 vcfg.vramModeB=ON;
 vcfg.vramA0=SCL_RBG0_K;
 vcfg.vramA1=SCL_RBG0_CHAR;

 SCL_SetVramConfig(&vcfg);

 SCL_SetColRamMode(SCL_CRM15_2048);

 SCL_SetPriority(SCL_SP0|SCL_SP1|SCL_SP2|SCL_SP3|SCL_SP4|
		 SCL_SP5|SCL_SP6|SCL_SP7, 7);
 SCL_SetPriority(SCL_NBG0,6);
 SCL_SetPriority(SCL_RBG1,7);
 SCL_SetPriority(SCL_SP0,4);

 SCL_SetSpriteMode(SCL_TYPE1,SCL_MIX,SCL_SP_WINDOW);
 SCL_SetCycleTable(cycle);
}


void loadVDP2Sprites(int fd)
{unsigned char *vram;
 SclConfig scfg;
 vram=(unsigned char *)SCL_VDP2_VRAM;
 fs_read(fd,vram+1024*256,1024*256);
 /* setup VDP2Sprite screen */
 SCL_InitConfigTb(&scfg);
 scfg.dispenbl=ON;
 scfg.bmpsize=SCL_BMP_SIZE_512X512;
 scfg.coltype=SCL_COL_TYPE_256;
 scfg.datatype= SCL_BITMAP;
 scfg.mapover=SCL_OVER_0;
 scfg.plate_addr[0]=1024*256;
 scfg.patnamecontrl=0;
 SCL_SetConfig(SCL_NBG0, &scfg);
 dontDisplayVDP2Pic();
}

void loadLoadingScreen(int fd)
{unsigned char *data;
 int xsize,ysize,y,x;
 unsigned short colorRam[256];
 fs_read(fd,(char *)&colorRam,512);
 fs_read(fd,(char *)&xsize,4);
 fs_read(fd,(char *)&ysize,4);
 assert(xsize==320);
 assert(ysize==240);
 data=mem_malloc(1,320*240);
 fs_read(fd,(char *)data,320*240);
 EZ_setErase(0,0x0000);
 for (y=0;y<240;y++)
    for (x=0;x<320;x++)
       POKE_W(FBUF_ADDR+y*1024+x*2,
	      colorRam[data[y*320+x]]);
 SCL_DisplayFrame();
 for (y=0;y<240;y++)
    for (x=0;x<320;x++)
       POKE_W(FBUF_ADDR+y*1024+x*2,
	      colorRam[data[y*320+x]]);
 SCL_DisplayFrame();
 mem_free(data);
}


static void plotBowl(unsigned char *pos)
{int y,x;
 unsigned char c;
 for (y=0;y<*(((int *)stat_bowl)+1);y++)
    {for (x=0;x<*(((int *)stat_bowl));x++)
	{c=stat_bowl[y*(*(int *)stat_bowl)+x+8];
	 if (c!=0)
	    *(pos+x)=c;
	}
     pos+=320;
    }
}

static void plotDot(unsigned char *pos,int blue)
{int y,x;
 unsigned char *art;
 unsigned char c;
 if (blue)
    art=stat_bluedot;
 else
    art=stat_reddot;
 for (y=0;y<*(((int *)art)+1);y++)
    {for (x=0;x<*(((int *)art));x++)
	{c=art[y*(*(int *)art)+x+8];
	 if (c!=0)
	    *(pos+x)=c;
	}
     pos+=320;
    }
}

static void plotNoDot(unsigned char *pos)
{int y,x;
 unsigned char c;
 for (y=0;y<*(((int *)stat_bluedot)+1);y++)
    {for (x=0;x<*(((int *)stat_bluedot));x++)
	{c=stat_bluedot[y*(*(int *)stat_bluedot)+x+8];
	 if (c!=0)
	    *(pos+x)=96; /* black in ruins pallete */
	}
     pos+=320;
    }
}

void redrawBowlDots(void)
{unsigned char *barPic,*pos;
 int i,w;
 barPic=(unsigned char *)((EZ_charNoToVram(0)<<3)+0x5c00000);
 for (i=0;i<currentState.nmBowls-1;i++)
    {pos=barPic+196+320*29+(10*i);
     plotBowl(pos);
    }
 for (i=0;i<nmFullBowls;i++)
    {pos=barPic+200+320*31+(10*i);
     plotDot(pos,0);
    }
 for (;i<currentState.nmBowls-1;i++)
    {pos=barPic+200+320*31+(10*i);
     plotNoDot(pos);
    }

 w=WEAPONINV(currentState.inventory);
 for (i=0;i<8;i++)
    {if (!((w>>i)&1))
	continue;
     pos=barPic+56+320*29+(10*i);
     plotBowl(pos);
     if (currentState.desiredWeapon==i)
	plotDot(barPic+59+320*31+(10*i),1);
     else
	plotNoDot(barPic+59+320*31+(10*i));
    }
}

void redrawStatBar(void)
{EZ_setChar(0,COLOR_4,*(int *)stat_bar,*(int *)(stat_bar+4),
	    (Uint8 *)stat_bar+8);
 redrawBowlDots();
}

static int sparkle=0;
static int healthMeterPos;
void drawStatBar(int time)
{int weapon;
 static XyInt statusbar = {-160, 112 - 40};
 XyInt compass = {-14,93};
 int hmp;
 int healthDiff;

 if (abs(healthMeterPos-F(currentState.health))<F(1))
    {healthMeterPos=F(currentState.health);
     healthDiff=0;
    }
 else
    {healthDiff=-((healthMeterPos-F(currentState.health))>>3);
     healthMeterPos+=healthDiff;
    }

 hmp=f(healthMeterPos);
 /* draw blood */
 if ((hmp-1)/200!=nmFullBowls)
    {nmFullBowls=(hmp-1)/200;
     redrawBowlDots();
    }

 /* draw erase bar */
 {XyInt ammoBar[4];
  ammoBar[0].x=310; ammoBar[0].y=95;
  ammoBar[1].x=310; ammoBar[1].y=99;
  ammoBar[2].x=-105; ammoBar[2].y=99;
  ammoBar[3].x=-105; ammoBar[3].y=95;
  EZ_polygon(ECD_DISABLE|SPD_DISABLE,RGB(0,0,0),ammoBar,NULL);
 }

 weapon=currentState.desiredWeapon;
 /* draw ammo indicator */
 if (weaponMaxAmmo[weapon] && currentState.weaponAmmo[weapon])
    {XyInt ammoBar[4];
     int ammoPos;
     int i;
     ammoPos=(87*currentState.weaponAmmo[weapon])/
	weaponMaxAmmo[weapon];

     ammoBar[0].x=-105; ammoBar[0].y=95;
     ammoBar[1].x=-105; ammoBar[1].y=99;
     ammoBar[2].x=-105+ammoPos; ammoBar[2].y=99;
     ammoBar[3].x=-105+ammoPos; ammoBar[3].y=95;
     EZ_polygon(ECD_DISABLE|SPD_DISABLE,RGB(0,0,31),ammoBar,NULL);
     if (weaponMaxAmmo[weapon]<=20)
	{for (i=1;i<currentState.weaponAmmo[weapon];i++)
	    {ammoBar[0].x=-105+(87*i)/weaponMaxAmmo[weapon];
	     ammoBar[0].y=95;
	     ammoBar[1].x=ammoBar[0].x;
	     ammoBar[1].y=99;
	     EZ_line(ECD_DISABLE|SPD_DISABLE,RGB(0,0,11),ammoBar,NULL);
	     ammoBar[0].x++;
	     ammoBar[1].x++;
	     EZ_line(ECD_DISABLE|SPD_DISABLE,RGB(10,10,31),ammoBar,NULL);
	    }
	}
    }

 /* draw health indicator */
 if (hmp>0)
    {int pos=((hmp%200)*87)/200;
     XyInt rect[4];
     if (pos==0 && hmp>100)
	pos=(199*87)/200;
     rect[0].x=35; rect[0].y=95;
     rect[1].x=35; rect[1].y=99;
     rect[2].x=35+pos; rect[2].y=99;
     rect[3].x=35+pos; rect[3].y=95;
     {char r,g,b;
      r=17; g=3; b=2;
      if (healthDiff<0)
	 {r-=(healthDiff>>16);
	  if (r>31) r=31;
	  g-=(healthDiff>>16);
	  if (g>31) g=31;
	  b-=(healthDiff>>16);
	  if (b>31) b=31;
	 }
      EZ_polygon(ECD_DISABLE|SPD_DISABLE,RGB(r,g,b),rect,NULL);
     }
     if (sparkle)
	{unsigned int fadeReg=1;
	 char r=17,g=3,b=2;
	 int x,y,i;
	 for (i=0;i<512;i++)
	    {if (i>sparkle+128)
		break;
	     if (fadeReg & 1)
		fadeReg=(fadeReg>>1) ^ (0x0110);
	     else
		fadeReg=fadeReg>>1;
	     if (i<sparkle)
		continue;
	     x=fadeReg & 0x7f;
	     if (!(i&7))
		{r++;
		 if (r>31) r=31;
		 g++;
		 b++;
		}
	     if (x>pos)
		continue;
	     y=fadeReg>>7;
	     x+=35; y+=95;
	     rect[0].x=x; rect[0].y=y;
	     rect[1].x=x; rect[1].y=y;
	     EZ_line(ECD_DISABLE|SPD_DISABLE,RGB(r,g,b),rect,NULL);
	    }
	 sparkle+=16;
	 if (sparkle>512)
	    sparkle=0;
	}
    }

 EZ_normSpr(DIR_NOREV,COLOR_4,0x4000,0,&statusbar,NULL);

 /* draw compass */
 {int angle,flip;
  angle=camera->angle;
  flip=0;
  if (angle<0)
     {flip|=DIR_LRREV;
      angle=-angle;
      compass.x-=5;
     }
  if (angle>F(90))
     {flip|=DIR_TBREV;
      angle=F(180)-angle;
     }
  if (angle<F(23))
     EZ_normSpr(flip,COLOR_4,0x4000,1,&compass,NULL);
  else
     if (angle<F(23+45))
	EZ_normSpr(flip,COLOR_4,0x4000,2,&compass,NULL);
     else
	EZ_normSpr(flip,COLOR_4,0x4000,3,&compass,NULL);
 }


}


static char *currentMessage=NULL;
static int messageAge,messageXPos;

#ifdef JAPAN
#define MESSAGEFONT 3
#else
#define MESSAGEFONT 1
#endif

void changeMessage(char *message)
{currentMessage=message;
 messageAge=0;
 messageXPos=-getStringWidth(MESSAGEFONT,message)/2;
}

static void drawMessage(int nmFrames)
{int t,b;
 int s;
 if (!currentMessage)
    return;

 s=(MTH_Sin(normalizeAngle(F(messageAge)<<4))>>13);
 t=15+s;
 b=15-s;

#ifndef JAPAN
 drawStringGouro(messageXPos,-100,MESSAGEFONT,RGB(t,t,t),RGB(b,b,b),
		 currentMessage);
#else
 {char buffer[80];
  int bpos;
  XyInt p;
  char *c;
  p.y=-100;
  bpos=0;
  for (c=currentMessage;;c++)
     {buffer[bpos++]=*c;
      if (*c=='\n' || *c==0)
	 {buffer[bpos-1]=0;
	  p.x=-(getStringWidth(3,buffer))/2;
	  drawStringGouro(p.x,p.y,3,RGB(t,t,t),RGB(b,b,b),
			  buffer);
	  p.y+=getFontHeight(3)-1;
	  bpos=0;
	 }
      if (!*c)
	 break;
     }
 }
#endif
 messageAge+=nmFrames;
 if (messageAge>60*5)
    currentMessage=NULL;
}


extern int slaveSize;
extern int lastHitWall;

void playerGetCamel(int toLevel)
{hitCamel=toLevel+100;
}

void playerHitTeleport(int toLevel)
{hitTeleport=toLevel+200;
}

void playerGotEntombedWithRamses(void)
{hitTeleport=5;
}

static Fixed32 airStatus=0;
static int drownStatus=0;
static int meterPos=0;
static Fixed32 dialPos=0;
static enum {METERUP,METERDOWN,TRANSITION} meterState=METERUP;
static int airBase;
void drawAirMeter(int frames)
{int i;
 static int transitionTimer=0;
 Fixed32 angle=0;
 static Fixed32 dialVel=0;
 static int underCount=0;
 static airFrom,airTo;
 XyInt pos[4];

 switch (meterState)
    {case METERUP:
	if (camera->flags & SPRITEFLAG_UNDERWATER)
	   meterState=METERDOWN;
	if (meterPos==0)
	   return;
	meterPos-=F(frames);
	if (meterPos<0)
	   meterPos=0;
	break;
     case METERDOWN:
	if (meterPos<F(50))
	   {meterPos+=F(frames);
	    if (meterPos>F(50))
	       meterPos=F(50);
	   }
	else
	   if (!(camera->flags & SPRITEFLAG_UNDERWATER))
	      {meterState=TRANSITION;
	       transitionTimer=0;
	      }
	break;
     case TRANSITION:
	transitionTimer+=frames;
	if (transitionTimer>120)
	   meterState=METERUP;
	if (camera->flags & SPRITEFLAG_UNDERWATER)
	   meterState=METERDOWN;
	break;
       }

 if (level_sector[camera->s].flags & SECFLAG_WATER)
    {for (i=0;i<frames;i++)
	{underCount++;
	 if (underCount==220)
	    {airFrom=airStatus;
	     airStatus+=F(27);
	     if (airStatus>F(270) || !(currentState.inventory & INV_MASK))
		{playerHurt(30);
		 airStatus=F(270);
#if 0
		 if (currentState.health<100 && !playerIsDead)
		    {int j;
		     for (j=0;j<3;j++)
			colorCenter[j]=-128+currentState.health;
		    }
#endif
		 underCount=drownStatus;
		 drownStatus=180;
		 if (drownStatus>180)
		    drownStatus=180;
		}
	     else
		{playSoundE(0,level_staticSoundMap[ST_JOHN]+5,32,0);
		 drownStatus=0;
		}
	    }
	 airTo=airStatus;

	 if (underCount==240)
	    {underCount=0;
	     dialPos=airStatus;
	    }
	}
    }
 else
    {if (underCount>0)
	{if (airStatus>F(90))
	    playStaticSound(ST_JOHN,6);
	 airStatus=0;
	 underCount=-1;
	 dialVel=0;
	}
    }

 if (underCount>220 && underCount<240)
    dialPos=evalHermite(F(underCount-220)/20,airFrom,airTo,0,0);

 if (underCount<0)
    {for (i=0;i<frames;i++)
	{dialPos+=dialVel;
	 dialVel+=(airStatus-dialPos)>>9;
	 dialVel-=dialVel>>5;
	 if (dialPos<0)
	    {dialPos=0;
	     dialVel=-dialVel>>4;
	    }
	}
    }

 if (!(currentState.inventory & INV_MASK))
    return;
 angle=F(90)-dialPos;
 if (angle<F(-180))
    angle+=F(360);

 EZ_localCoord(320/2,f(meterPos)-50);

#if 1
 {pos[0].x=90;
  pos[0].y=15;
  EZ_normSpr(0,DRAW_MESH|COLOR_5|ECD_DISABLE,0,mapPic(airBase+1),pos,NULL);
  pos[0].x+=31;
  pos[0].y+=31;
  pos[1].x=64-(dialPos/276480);
  pos[1].y=pos[1].x;
  EZ_scaleSpr(ZOOM_MM,DRAW_MESH|COLOR_5|ECD_DISABLE,0,mapPic(airBase),pos,
	      NULL);
 }
#else
 pos[0].x=-67;
 pos[0].y=-130+120;
 EZ_normSpr(0,UCLPIN_ENABLE|COLOR_5|ECD_DISABLE,0,4,pos,NULL);
 {int c=0;
  unsigned short color = RGB(31,5,5);
  MthXyz north,east;
  north.x=MTH_Cos(angle);
  north.y=MTH_Sin(angle);
  east.x=-north.y;
  east.y=north.x;

  for (i=0;i<nmDialLines;i++)
     {pos[0].x=
	 f(MTH_Mul(dialLines[c],north.x)+MTH_Mul(dialLines[c+1],north.y));
      pos[0].y=
	 f(MTH_Mul(dialLines[c],east.x)+MTH_Mul(dialLines[c+1],east.y));
      pos[1].x=
	 f(MTH_Mul(dialLines[c+2],north.x)+MTH_Mul(dialLines[c+3],north.y));
      pos[1].y=
	 f(MTH_Mul(dialLines[c+2],east.x)+MTH_Mul(dialLines[c+3],east.y));
      c+=4;
      EZ_line(COMPO_REP|ECD_DISABLE|SPD_DISABLE,color,pos,NULL);
      if (i==13)
	 color=RGB(31,31,15);
      if (i==30)
	 color=RGB(31,31,31);

     }
 }
#endif
 EZ_localCoord(320/2,240/2);
}

static int delayed_fade=0;
static int delayed_fadeButton=0;
static int delayed_fadeSel=0;

int playerGetObject(int objectType)
{switch (objectType)
    {case OT_DOLL1 ... OT_DOLL23:
	currentState.dolls|=1<<(objectType-OT_DOLL1);
	delayed_fade=1; delayed_fadeButton=2;
	delayed_fadeSel=6;
	break;
     case OT_INVISIBLEBALL:
	invisibleCounter=INVISIBLEDOSE;
	changeMessage(getText(LB_ITEMMESSAGE,20));
	playStaticSound(ST_ITEM,4);
	break;
     case OT_WEAPONPOWERBALL:
	weaponPowerUpCounter=WEAPONPOWERDOSE;
	changeMessage(getText(LB_ITEMMESSAGE,21));
	playStaticSound(ST_ITEM,3);
	break;
     case OT_EYEBALL:
	revealMap();
	changeMessage(getText(LB_ITEMMESSAGE,22));
	playStaticSound(ST_ITEM,3);
	break;
     case OT_COMM_BATTERY ... OT_COMM_TOP:
	currentState.inventory|=0x10000<<(objectType-OT_COMM_BATTERY);
	delayed_fade=1;	delayed_fadeButton=3;
	delayed_fadeSel=objectType-OT_COMM_BATTERY;
	break;
     case OT_CHOPPER:
	hitTeleport=4;
	break;
     case OT_RAMMUMMY:
	currentState.inventory|=INV_MUMMY;
	hitTeleport=6;
	break;
     case OT_PYRAMID:
	hitPyramid=1;
	currentState.levFlags[(int)currentState.currentLevel]|=
	   LEVFLAG_GOTPYRAMID;
	break;
     case OT_M60:
	playStaticSound(ST_ITEM,3);
	currentState.inventory|=INV_M60;
	setCurrentWeapon(WP_M60);
	redrawBowlDots();
	changeMessage(getText(LB_ITEMMESSAGE,5));
	break;
     case OT_COBRASTAFF:
	playStaticSound(ST_ITEM,3);
	currentState.inventory|=INV_COBRA;
	setCurrentWeapon(WP_COBRA);
	redrawBowlDots();
	changeMessage(getText(LB_ITEMMESSAGE,8));
	break;
     case OT_FLAMER:
	playStaticSound(ST_ITEM,3);
	currentState.inventory|=INV_FLAMER;
	setCurrentWeapon(WP_FLAMER);
	redrawBowlDots();
	changeMessage(getText(LB_ITEMMESSAGE,6));
	break;
     case OT_GRENADEAMMO:
	playStaticSound(ST_ITEM,3);
	currentState.inventory|=INV_GRENADE;
	setCurrentWeapon(WP_GRENADE);
	redrawBowlDots();
	changeMessage(getText(LB_ITEMMESSAGE,7));
	break;
     case OT_MANACLE:
	playStaticSound(ST_ITEM,3);
	currentState.inventory|=INV_MANACLE;
	setCurrentWeapon(WP_RAVOLT);
	redrawBowlDots();
	changeMessage(getText(LB_ITEMMESSAGE,9));
	break;
     case OT_PISTOL:
	playStaticSound(ST_ITEM,3);
	currentState.inventory|=INV_PISTOL;
	setCurrentWeapon(WP_PISTOL);
	redrawBowlDots();
	changeMessage(getText(LB_ITEMMESSAGE,4));
	break;
     case OT_RING:
	playStaticSound(ST_ITEM,3);
	currentState.inventory|=INV_RING;
	setCurrentWeapon(WP_RING);
	redrawBowlDots();
	changeMessage(getText(LB_ITEMMESSAGE,10));
	break;
     case OT_BUGKEY:
	keyMask|=1;
	playStaticSound(ST_ITEM,0);
	changeMessage(getText(LB_ITEMMESSAGE,0));
	break;
     case OT_TIMEKEY:
	keyMask|=2;
	playStaticSound(ST_ITEM,0);
	changeMessage(getText(LB_ITEMMESSAGE,1));
	break;
     case OT_XKEY:
	keyMask|=4;
	playStaticSound(ST_ITEM,0);
	changeMessage(getText(LB_ITEMMESSAGE,2));
	break;
     case OT_PLANTKEY:
	keyMask|=8;
	playStaticSound(ST_ITEM,0);
	changeMessage(getText(LB_ITEMMESSAGE,3));
	break;
     case OT_CAPE:
	currentState.inventory|=INV_SHAWL;
	currentState.gameFlags|=GAMEFLAG_GOTSHAWL;
	delayed_fade=1;	delayed_fadeButton=2; delayed_fadeSel=2;
	break;
     case OT_FEATHER:
	currentState.inventory|=INV_FEATHER;
	currentState.gameFlags|=GAMEFLAG_GOTFEATHER;
	delayed_fade=1;	delayed_fadeButton=2; delayed_fadeSel=5;
	break;
     case OT_MASK:
	currentState.inventory|=INV_MASK;
	currentState.gameFlags|=GAMEFLAG_GOTMASK;
	delayed_fade=1;	delayed_fadeButton=2; delayed_fadeSel=1;
	break;
     case OT_SANDALS:
	currentState.inventory|=INV_SANDALS;
	currentState.gameFlags|=GAMEFLAG_GOTSANDALS;
	delayed_fade=1;	delayed_fadeButton=2; delayed_fadeSel=0;
	break;
     case OT_ANKLETS:
	currentState.inventory|=INV_ANKLET;
	currentState.gameFlags|=GAMEFLAG_GOTANKLET;
	delayed_fade=1;	delayed_fadeButton=2; delayed_fadeSel=3;
	break;
     case OT_SCEPTER:
	currentState.inventory|=INV_SCEPTER;
	currentState.gameFlags|=GAMEFLAG_GOTSCEPTER;
	delayed_fade=1;	delayed_fadeButton=2; delayed_fadeSel=4;
	break;
     case OT_BLOODBOWL:
	playStaticSound(ST_ITEM,4);
	currentState.nmBowls++;
	currentState.levFlags[(int)currentState.currentLevel]|=
	   LEVFLAG_GOTVESSEL;
	changeMessage(getText(LB_ITEMMESSAGE,15));
	redrawBowlDots();
	break;
     case OT_HEALTHBALL:
     case OT_HEALTHORB:
     case OT_HEALTHSPHERE:
	if (currentState.health==currentState.nmBowls*200 &&
	    airStatus==0)
	   return 0;
	sparkle=1;
	if (objectType==OT_HEALTHBALL)
	   {currentState.health+=20;
	    airStatus-=F(20);
	    changeMessage(getText(LB_ITEMMESSAGE,13));
	    playStaticSound(ST_ITEM,2);
	   }
	if (objectType==OT_HEALTHORB)
	   {currentState.health+=50;
	    airStatus-=F(50);
	    changeMessage(getText(LB_ITEMMESSAGE,13));
	    playStaticSound(ST_ITEM,2);
	   }
	if (objectType==OT_HEALTHSPHERE)
	   {currentState.health=currentState.nmBowls*200;
	    airStatus=0;
	    changeMessage(getText(LB_ITEMMESSAGE,14));
	    playStaticSound(ST_ITEM,4);
	   }
	if (airStatus<0)
	   airStatus=0;
	dialPos=airStatus;
	if (currentState.health>currentState.nmBowls*200)
	   currentState.health=currentState.nmBowls*200;
	break;
     case OT_AMMOBALL:
     case OT_AMMOORB:
	{int diff;
	 int weapon=currentState.desiredWeapon;
	 if (currentState.weaponAmmo[weapon]==weaponMaxAmmo[weapon])
	    return 0;
	 diff=(weaponMaxAmmo[weapon]*(objectType==OT_AMMOBALL?15:30))/100;
	 if (diff<1) diff=1;
	 weaponChangeAmmo(weapon,diff);
	 changeMessage(getText(LB_ITEMMESSAGE,11));
	 playStaticSound(ST_ITEM,1);
	 if (currentState.weaponAmmo[weapon]>weaponMaxAmmo[weapon])
	    currentState.weaponAmmo[weapon]=weaponMaxAmmo[weapon];
	 break;
	}
     case OT_AMMOSPHERE:
#if 1
	{int i;
	 for (i=0;i<WP_NMWEAPONS;i++)
	    if (currentState.weaponAmmo[i]<weaponMaxAmmo[i])
	       break;
	 if (i==WP_NMWEAPONS)
	    return 0;
	 for (i=0;i<WP_NMWEAPONS;i++)
	    currentState.weaponAmmo[i]=weaponMaxAmmo[i];
	}
#else
	if (currentState.weaponAmmo[currentWeapon]==
	    weaponMaxAmmo[currentWeapon])
	   return 0;
	weaponChangeAmmo(currentWeapon,weaponMaxAmmo[currentWeapon]-
			 currentState.weaponAmmo[currentWeapon]);
#endif
	changeMessage(getText(LB_ITEMMESSAGE,12));
	playStaticSound(ST_ITEM,3);
	break;
       }
 greenFlash();
 return 1;
}

void rotateRectangle(int width,int height,int cx,int cy,
		     Fixed32 angle,
		     XyInt *result)
{MthXyz north,east;
 int x,y;
 north.x=MTH_Cos(angle);
 north.y=MTH_Sin(angle);
 east.x=-north.y;
 east.y=north.x;

 x=-cx; y=-cy;
 result[0].x=f(x*north.x+y*north.y);
 result[0].y=f(x*east.x+y*east.y);

 x+=width;
 result[1].x=f(x*north.x+y*north.y);
 result[1].y=f(x*east.x+y*east.y);

 y+=height;
 result[2].x=f(x*north.x+y*north.y);
 result[2].y=f(x*east.x+y*east.y);

 x-=width;
 result[3].x=f(x*north.x+y*north.y);
 result[3].y=f(x*east.x+y*east.y);

}



static int earthQuake;
int getEarthQuake(void)
{return earthQuake;
}

void setEarthQuake(int richter)
{if (richter>earthQuake)
    earthQuake=richter;
}

void stunPlayer(int ticks)
{stunCounter=ticks;
 playStaticSound(ST_JOHN,3);
 colorOffset[2]=128;
}

int runLevel(char *filename,int levelNm)
{XyInt parms;
 XyInt noUserClip[2]={{0,0},{320-1,240-1}};
 int i,monsterMoveCounter;
 int nmWeaponTiles,nmStaticSounds;
 int lastDraw=0,lastCalc=0;
 int framesElapsed,inputEnd;
 unsigned int smoothVTime;
 int vspeedSwitchCount;
 int lastLastCalc=0;
 int lastYaw,lastPitch,mmcSave;

 MthMatrixTbl viewTransform;
 MthMatrix matstack[4];

 dPrint("vroom!\n");
 nmFullBowls=0;
 healthMeterPos=0;
 stopCD();
 initSound();

 quitRequest=0;
 mem_init();
 /* do hardware initialization */
 dPrint("A!\n");
 plaxOff();
 dPrint("A1!\n");
 setVDP2();
 dPrint("B!\n");
 displayEnable(0);

 SPR_SetTvMode(SPR_TV_NORMAL,SPR_TV_320X240,OFF);
 EZ_initSprSystem(1448,4,1224,
		  240,0x8000);
 dPrint("C!\n");
 SCL_SetFrameInterval(0xfffe);

 for (i=0;i<3;i++)
    {EZ_openCommand();
     EZ_sysClip();
     EZ_closeCommand();
     SCL_DisplayFrame();
    }
 EZ_setChar(0,COLOR_4,*(int *)stat_bar,*(int *)(stat_bar+4),
	    (Uint8 *)stat_bar+8);
 EZ_setChar(1,COLOR_4,*(int *)stat_compass0,*(int *)(stat_compass0+4),
	    (Uint8 *)stat_compass0+8);
 EZ_setChar(2,COLOR_4,*(int *)stat_compass1,*(int *)(stat_compass1+4),
	    (Uint8 *)stat_compass1+8);
 EZ_setChar(3,COLOR_4,*(int *)stat_compass2,*(int *)(stat_compass2+4),
	    (Uint8 *)stat_compass2+8);

#ifdef JAPAN
 initPicSystem(4,((int []){28,30,1,10,12,30,-1}));
#else
 i=initFonts(4,3);
 initPicSystem(i,((int []){28,31,1,10,12,-1}));
#endif
 dPrint("ert!\n");
 redrawStatBar();

 MTH_InitialMatrix(&viewTransform,4,matstack);
 MTH_ClearMatrix(&viewTransform);

 initObjects();
 initFlames();

 SCL_SetWindow(SCL_W1,0,SCL_RBG0,0xfffffff,0,0,0,0);

 SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0|SCL_RBG0,0,0,0);
 fs_startProgress(1);
 fs_addToProgress("+STATIC.DAT");
 fs_addToProgress(filename);
 /* load static data */
 {int fd;
  fd=fs_open("+STATIC.DAT");
  assert(fd>=0);
  dPrint("blat!\n");
  loadLoadingScreen(fd);
  dPrint("frop!\n");
  displayEnable(1);
  loadVDP2Sprites(fd);
  nmStaticSounds=loadStaticSounds(fd);
  nmWeaponTiles=loadWeaponTiles(fd);
  loadWeaponSequences(fd);
  fs_close(fd);
 }
 /* load level file */
 debugPrint("Loaded static\n");
 {int fd;
  fd=fs_open(filename);
  assert(fd>=0);
  initPlax(fd);
  loadLevel(fd,nmWeaponTiles);
  loadDynamicSounds(fd);
  loadTiles(fd);
  loadSequences(fd,nmWeaponTiles,nmStaticSounds);
  fs_close(fd);
 }
 fs_closeProgress();

#ifdef JAPAN
 loadJapanFontPics();
#endif

#if 0
 {/* mirror level */
  int i;
  for (i=0;i<level_nmVertex;i++)
     level_vertex[i].x=-level_vertex[i].x;
  for (i=0;i<level_nmWalls;i++)
     level_wall[i].normal[0]=-level_wall[i].normal[0];
  for (i=0;i<level_nmSectors;i++)
     level_sector[i].center[0]=-level_sector[i].center[0];
 }
#endif

 startSlave(wallRenderSlaveMain);
 delay(1);

 /* air meter stuff */
 airBase=addPic(TILE16BPP,meter_bubble+8,NULL,0);
 addPic(TILE16BPP,meter_back+8,NULL,0);


 SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0|SCL_RBG0,-255,-255,-255);
 colorOffset[0]=-255; colorOffset[1]=-255; colorOffset[2]=-255;
 EZ_setErase(240,0x0000);

 debugPrint("Loaded dynamic\n");
 initRoutePlotter();
 initWallRenderer();
 initMap();
 markAnimTiles();
 mapOn=0;
 assert(level_nmSectors<=MAXNMSECTORS);
 assert(level_nmWalls<=MAXNMWALLS);
 initSpriteSystem();


 switchPlayerMotion(1);
 colorCenter[0]=0;
 colorCenter[1]=0;
 colorCenter[2]=0;
 player=NULL;
 playerAngle.pitch=0;
 playerAngle.yaw=F(0);
 placeObjects();
 if (!player)
    player=constructPlayer(0,0);
 assert(player);
 camera=player->sprite;
#if 0
 {MthXyz pos;
#define S 0
  pos.x=F(level_sector[S].center[0]);
  pos.y=F(level_sector[S].center[1]);
  pos.z=F(level_sector[S].center[2]);
  moveSpriteTo(camera,S,&pos);
 }
#endif

 monsterMoveCounter=0;
 initWeapon();
 playerIsDead=0;
 hitCamel=0;
 hitPyramid=0;
 hitTeleport=0;
 initInput();
 playCDTrackForLevel(levelNm);

 initWater();
#if 0
 makeDial();
#endif
 debugPrint("Start Loop\n");
 initProfiler();
 keyMask=0;
 earthQuake=0;
 vspeedSwitchCount=0;
 airStatus=0;
 dialPos=0;
 meterPos=0;
 meterState=METERUP;
 drownStatus=0;
 framesElapsed=0;
 inputEnd=0;
 lastYaw=playerAngle.yaw;
 lastPitch=playerAngle.pitch;
 ltHurtTime=0;
 delayed_fade=0;
 switchWeapons(1);
 retryPlaxPal();
 vtimer=0;
 smoothVTime=1;

 SCL_SET_N0CCEN(1);
 SCL_SetColMixRate(SCL_NBG0,0);
 invisibleCounter=0;

 if (currentState.gameFlags & GAMEFLAG_DOLLPOWERMODE)
    weaponPowerUpCounter=10000;
 else
    weaponPowerUpCounter=0;
 currentMessage=NULL;

 if (currentState.gameFlags & GAMEFLAG_KILENTRYCHEATENABLED)
    {int i;
     currentState.gameFlags&=~GAMEFLAG_KILENTRYCHEATENABLED;
     for (i=0;i<6;i++)
	signalAllObjects(SIGNAL_SWITCH,i+11000,0);
    }
#ifndef NDEBUG
 colorOffset[0]=0; colorOffset[1]=0; colorOffset[2]=0;
#endif

 /* GCC14: mipBase=createMippedPics() removed -- MIPMAP baked off (WALLS.C) */

 while(1)
    {htimer=0;
     /* ok */
     if (framesElapsed>8)
	framesElapsed=8;
     /* move paralax sky */
     MTH_PushMatrix(&viewTransform);
     MTH_RotateMatrixZ(&viewTransform, playerAngle.roll );
     MTH_RotateMatrixX(&viewTransform, playerAngle.pitch );
     MTH_RotateMatrixY(&viewTransform, playerAngle.yaw );
     if (earthQuake)
	{camera->pos.x+=(MTH_GetRand()%(earthQuake<<15))-(earthQuake<<14);
	 camera->pos.y+=(MTH_GetRand()%(earthQuake<<15))-(earthQuake<<14);
	 camera->pos.z+=(MTH_GetRand()%(earthQuake<<15))-(earthQuake<<14);
	 earthQuake--;
	}
     MTH_MoveMatrix(&viewTransform,
		    -camera->pos.x,
		    -camera->pos.y+playerHeightOffset,
		    -camera->pos.z);
     /* ok */
     EZ_openCommand();
     /* ok */
     parms.x = 320 - 1;
     parms.y = 240 - 1;
     EZ_sysClip();
     EZ_userClip(noUserClip);

     EZ_localCoord(320/2,240/2);
     pushProfile("Walls");
     /* ok */
     drawWalls(viewTransform.current);
     /* nok */
     popProfile();

     EZ_userClip(noUserClip);

     pushProfile("Motion");
     movePlayer(inputEnd,framesElapsed);
     camera->angle=playerAngle.yaw;
     if (monsterMoveCounter>8)
	monsterMoveCounter=8;
     mmcSave=monsterMoveCounter;
     for (;monsterMoveCounter>1;monsterMoveCounter-=2)
	{if (ltHurtTime>0)
	    {ltHurtTime--;
	     playerHurt(ltHurtAmount);
	     if (!ltHurtTime)
		stopAllSound(69);
	    }
	 pushProfile("Run Objects");
	 runObjects();
	 popProfile();
	 stepColorOffset();
	 stepPlayerHeight();
	 ouchTime--;
	 if (weaponPowerUpCounter &&
	     !(currentState.gameFlags & GAMEFLAG_DOLLPOWERMODE))
	    {weaponPowerUpCounter--;
	     if (weaponPowerUpCounter<60 && !(weaponPowerUpCounter & 0xf))
		playStaticSound(ST_ITEM,5);
	     if (weaponPowerUpCounter&0x2)
		SCL_SetColOffset(SCL_OFFSET_B,SCL_NBG0,
				 255,60,60);
	     else
		SCL_SetColOffset(SCL_OFFSET_B,SCL_NBG0,
				 0,0,0);
	    }
	 if (invisibleCounter)
	    {int rev;
	     invisibleCounter--;
	     if (invisibleCounter<60 && !(invisibleCounter & 0xf))
		playStaticSound(ST_ITEM,5);
	     rev=INVISIBLEDOSE-invisibleCounter;
	     if (rev>16 && rev<16+20)
		{int c=rev-16;
		 SCL_SetColMixRate(SCL_NBG0,c);
		}
	     if (rev<32)
		{int rg,b,o;
		 o=16-abs(rev-16);
		 rg=o<<4;
		 if (rg>255) rg=255;
		 b=o<<5;
		 if (b>255) b=255;
		 SCL_SetColOffset(SCL_OFFSET_B,SCL_NBG0,
				  rg,rg,b);
		}
	     if (invisibleCounter<20)
		SCL_SetColMixRate(SCL_NBG0,invisibleCounter);
	    }
	}
     popProfile();
     pushProfile("Walls");
     drawWallsFinish();
     popProfile();

     for (;mmcSave>1;mmcSave-=2)
	{advanceWallAnimations();
	 stepWater();
	}
     updatePushBlockPositions();
     processDelayedMoves();

     if (mapOn)
	drawMap(camera->pos.x,camera->pos.z,camera->pos.y,playerAngle.yaw,
		camera->s);

     runWeapon(framesElapsed,invisibleCounter,weaponPowerUpCounter);

     MTH_PopMatrix(&viewTransform);

     drawMessage(framesElapsed);
     drawStatBar(framesElapsed);

     drawAirMeter(framesElapsed);


#ifdef STATUSTEXT
     drawStringf(-158,-60,1,"fps:%d %d",60/framesElapsed,60/(smoothVTime+1));

     drawStringf(-158,-80,1,"sector:%d",camera->s);

#ifndef NDEBUG
     drawStringf(-158,-100,1,"extra:%d",extraStuff);

     for (i=0;i<16;i++)
	{if (errorQ[i])
	    {drawStringf(15,-50+10*i,1,"%x",errorQ[i]);
	     drawStringf(90,-50+10*i,1,"%x",prQ[i]);
	    }
	}
#endif

     drawStringf(-158,-70,1,"polys:%d",nmPolys+nmSlavePolys);

     drawStringf(-158,-50,1,"time:%d %d:%d",
		 (lastCalc+lastLastCalc)>>1,lastDraw,
		 lastCalc+lastDraw);

     drawStringf(-158,-40,1,"mem:%dk+%dk=%dk",mem_coreleft(0)>>10,
		 mem_coreleft(1)>>10,(mem_coreleft(0)+mem_coreleft(1))>>10);

#endif

     lastLastCalc=lastCalc;
     lastCalc=htimer;
     sound_nextFrame();
     {int nmSwaps[NMCLASSES];
      int used[NMCLASSES];
      pic_nextFrame(nmSwaps,used);
#ifndef NDEBUG
#ifdef STATUSTEXT
      drawString(25,-80,1,"vswaps:");
      for (i=0;i<NMCLASSES;i++)
         drawStringf(80+17*i,-80,1,"%d",nmSwaps[i]);
      drawString(25,-70,1,"used:");
      for (i=0;i<NMCLASSES;i++)
         drawStringf(80+17*i,-70,1,"%d",used[i]);
#endif
#endif
     }

     if (hitPyramid || hitTeleport)
	{EZ_closeCommand();
	 SPR_WaitDrawEnd();
	 SCL_DisplayFrame();
	 return hitTeleport?hitTeleport:3;
	}
     /* used to be here */
     EZ_closeCommand();
     SPR_WaitDrawEnd();
     lastDraw=htimer-lastCalc;

     DISABLE;
     if (vtimer<smoothVTime)
	{vspeedSwitchCount++;
	 if (vspeedSwitchCount>10)
	    {smoothVTime=vtimer;
	     if (smoothVTime<1)
		smoothVTime=1;
	     vspeedSwitchCount=0;
	    }
	}
     else
	vspeedSwitchCount=0;
     ENABLE;
     while (vtimer<smoothVTime) ;

     /* sometimes a vtimer switch can occur in here */
     SCL_DisplayFrame();

     DISABLE;
     if (vtimer-1>smoothVTime)
	{smoothVTime=vtimer-1;
	 if (smoothVTime>2)
	    smoothVTime=2;
	 vspeedSwitchCount=0;
	}
     monsterMoveCounter+=vtimer;
     framesElapsed=vtimer;
     inputEnd=inputQHead;
     vtimer=0;
     ENABLE;

     movePlax(lastYaw,lastPitch);
     if (currentState.currentLevel==18)
	{plaxBBxmin=-160;
	 plaxBBymin=-110;
	 plaxBBxmax=160;
	 plaxBBymax=90;
	}
     SCL_SetWindow(SCL_W1,0,SCL_RBG0,0xfffffff,
		   plaxBBxmin+160,plaxBBymin+120,
		   plaxBBxmax+160,plaxBBymax+120);
     updateVDP2Pic();
     lastYaw=playerAngle.yaw; lastPitch=playerAngle.pitch;

     if (playerIsDead && colorOffset[0]==-255)
	return 1;

     enablePlax(1);
     if (hitCamel)
	{enablePlax(0);
	 if (runTravelQuestion(getText(LB_LEVELNAMES,hitCamel-100)))
	    return hitCamel;
	 stunCounter=10;
	 hitCamel=0;
	 vtimer=smoothVTime;
	}
     if (playerMotionEnable &&
	 (!(lastInputSample & PER_DGT_S) || !controlerPresent || delayed_fade))
	{enablePlax(0);
	 runInventory(currentState.inventory,keyMask,&mapOn,
		      delayed_fade,delayed_fadeButton,delayed_fadeSel);
	 delayed_fade=0;
	 vtimer=smoothVTime;
	}

     if (quitRequest)
	return 2;
#ifdef PSYQ
     pollhost();
#endif
    }
}

#if 0
static void fadeSegaLogo(void)
{int i,pos;
 int r,g,b;
 POKE_W(SCL_VDP2_VRAM+0x180110,0x7f);
 POKE_W(SCL_VDP2_VRAM+0x180112,0x00);
 pos=0;
 for (i=0;i<32;i++)
    {pos+=F(1)/32;
     r=f(evalHermite(pos,0,F(-255),0,0));
     g=f(evalHermite(pos,0,F(-255),0,0));
     b=f(evalHermite(pos,0,F(-255),0,0));

     while (!(PEEK_W(SCL_VDP2_VRAM+0x180004) & 8)) ;
     POKE_W(SCL_VDP2_VRAM+0x180114,r & 0x1ff);
     POKE_W(SCL_VDP2_VRAM+0x180116,g & 0x1ff);
     POKE_W(SCL_VDP2_VRAM+0x180118,b & 0x1ff);
     while ((PEEK_W(SCL_VDP2_VRAM+0x180004) & 8)) ;
    }
}
#endif

void main(void)
{char *levelFile;
 int level;
 enable_stereo=1;
 enable_music=1;
 abcResetEnable=1;

 POKE_W(SCL_VDP2_VRAM+0x180112,0x00);
 POKE_W(SCL_VDP2_VRAM+0x180114,(-255) & 0x1ff);
 POKE_W(SCL_VDP2_VRAM+0x180116,(-255) & 0x1ff);
 POKE_W(SCL_VDP2_VRAM+0x180118,(-255) & 0x1ff);

 megaInit();
 dPrint("Start...\n");
 fs_init();
 set_imask(0);

 dPrint("Acquiring system info...");
 /* aquire system info */
 {PerGetSys *sys_data;
  sys_data=(PerGetSys *)waitSystemData(PadWorkArea); /* GCC14: was PER_LInit + bare PER_GET_SYS poll, see UTIL.C */
  systemMemory=sys_data->sm;
 }
 dPrint("done.\n");

 SCL_Vdp2Init();
 SCL_SetDisplayMode(SCL_NON_INTER,SCL_240LINE,SCL_NORMAL_A);
 SPR_SetEraseData(RGB(0,0,0),0,0,319,239);
 displayEnable(0);
 setVDP2();
 SetVblank();

 mem_init();
 dPrint("loading initial...");
 /* do initial load */
 {int fd=fs_open(
#ifndef JAPAN
		 "+INITLOAD.DAT"
#else
		 "+JINITLOD.DAT"
#endif
		 );
  dlg_init(fd); /* warning, locks memory */
#ifndef JAPAN
  loadLocalText(fd); /* locks memory */
#else
  loadJapanFontData(fd);
  mem_lock();
  loadLocalText(fd);
#endif
  fs_close(fd);
 }
 dPrint("done\n");
 initDMA();

/* testEZ();*/

 EZ_initSprSystem(1540,8,1524,
		  240,0x8000);
 SCL_SetFrameInterval(0xfffe);
 SPR_SetTvMode(SPR_TV_NORMAL,SPR_TV_320X240,OFF);

#ifdef JAPAN
 {int i;
  i=initFonts(1,7);
  initPicSystem(i,((int []){0,0,0,0,0,70,-1}));
  loadJapanFontPics();
 }
#else
 initFonts(1,7);
#endif

 EZ_clearScreen();
 POKE_W(SCL_VDP2_VRAM+0x180114,0); /* reset color offsets */
 POKE_W(SCL_VDP2_VRAM+0x180116,0);
 POKE_W(SCL_VDP2_VRAM+0x180118,0);

 dPrint("Initialized\n");
 displayEnable(1);

#ifdef FLASH
 {int level=0;
  char *levelFile;
  do
     {bup_initCurrentGame();
      currentState.inventory|=INV_SWORD|INV_PISTOL|INV_M60|INV_COBRA|
	 INV_SANDALS|INV_MASK;
      currentState.currentLevel=level;
      levelFile=getLevelName(level);
     }
  while (runLevel(levelFile,level)==1);
  fadePos=0; fadeEnd=-256; fadeDir=-4;
  while (fadeDir) ;
  SYS_EXECDMP();
  return;
 }
#endif

 bup_initialProc();

 intro:
#ifndef TESTCODE
 abcResetEnable=1;
 playIntro();
#else
 bup_initCurrentGame();
 currentState.inventory=0x00ffff;
#endif
 abcResetEnable=0;
 dPrint("1\n");
 SCL_Vdp2Init();
 dPrint("2\n");
 displayEnable(0);
 SCL_SetDisplayMode(SCL_NON_INTER,SCL_240LINE,SCL_NORMAL_A);
 setVDP2();
 dPrint("3\n");

#ifndef TESTCODE
 if ((currentState.gameFlags & GAMEFLAG_JUSTTELEPORTED) &&
     !(currentState.inventory & INV_MUMMY))
    level=3;
 else
    level=runMap(currentState.currentLevel);
 currentState.inventory&=~INV_MUMMY;
#else
 level=22;
#endif

 while (1)
    {int action;
     SaveState levStart=currentState;
     if (level==-1)
	{/* map was aborted with abc-start */
	 goto intro;
	}
     currentState.currentLevel=level;
     levelFile=getLevelName(level);

     action=runLevel(levelFile,level);

     {extern int SclRotateTableAddress;
      SclRotateTableAddress=0;
     }
     stopAllLoopedSounds();
     dontDisplayVDP2Pic();
     EZ_clearScreen();
     SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0|SCL_RBG0,0,0,0);

     switch (action)
	{case 100 ... 199: /* camel */
	    currentState.gameFlags|=GAMEFLAG_TALKEDTORAMSES;
	    currentState.levFlags[hitCamel-100]|=LEVFLAG_CANENTER;
	    if (currentState.health<200)
	       currentState.health=200;
	    currentState.currentLevel=hitCamel-100;
	    mem_init();
	    bup_saveGame();
	    level=runMap(currentState.currentLevel);
	    break;
	 case 200 ... 399: /* teleporter */
	    mem_init();
	    teleportEffect();
	    level=action-200;
	    if (level==13 /* kilentry */)
	       currentState.inventory|=
		  INV_SANDALS|INV_MASK|INV_SHAWL|INV_ANKLET|
		     INV_SCEPTER|INV_FEATHER;
	    break;
	 case 1: /* restart level */
	    currentState=levStart;
	    break;
	 case 2: /* quit */
	    goto intro;
	    break;
	 case 3: /* warp to tomb */
	    currentState.health=currentState.nmBowls*200;
	    {int i;
	     for (i=0;i<WP_NMWEAPONS;i++)
		currentState.weaponAmmo[i]=weaponMaxAmmo[i];
	    }
	    dontDisplayVDP2Pic();
	    mem_init();
	    teleportEffect();
	    level=3;
	    currentState.currentLevel=level;
	    bup_saveGame();
	    break;
	 case 4: /* good end */
	    POKE(0x02ffffc,GOODEND);
	    if (currentState.dolls==ALLDOLLS)
	       POKE(0x02ffffc,SUPERGOODEND);
	    link("0");
	    break;
	 case 5: /* bad end */
	    POKE(0x02ffffc,BADEND);
	    link("0");
	    break;
	 case 6: /* got mummy */
	    dontDisplayVDP2Pic();
	    mem_init();
	    teleportEffect();
	    level=30;
	    currentState.currentLevel=3;
	    currentState.inventory|=
	       INV_SANDALS|INV_MASK|INV_SHAWL|INV_ANKLET|
		  INV_SCEPTER|INV_FEATHER;
	    currentState.gameFlags&=~GAMEFLAG_JUSTTELEPORTED;
	    bup_saveGame();
	    currentState.inventory&=
	       ~(INV_SANDALS|INV_MASK|INV_SHAWL|INV_ANKLET|
		 INV_SCEPTER|INV_FEATHER);
	    currentState.gameFlags|=GAMEFLAG_JUSTTELEPORTED;
	    break;
	   }
    }
}

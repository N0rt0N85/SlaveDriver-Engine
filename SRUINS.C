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
#include "mplayer.h"
#include "profile.h"
#include "crash.h"
#include "gamestat.h"
#include "intro.h"
#include "mov.h"
#include "plax.h"
#include "mpsky.h"
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
/* GCC14: the death screen holds.  0 = the view is still on the corpse and nothing fades; then
   the player presses fire or use and this counts the tics of the fade that follows, which is
   what ends the level (colorOffset reaching -255, below).  It MUST be reset wherever
   playerIsDead is, or the level ends on its first image for ever after the first death. */
static int deathFade;
/* GCC14: 1 = the load that follows is the one after a death.  The game's loading screen shows
   neither its logo nor its fire then -- dying is not the moment for the title (DOOM_TITLE.C);
   doom_loadingEnd clears it. */
int loadAfterDeath;
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
 SCL_SetColOffset(SCL_OFFSET_A,SKY_OFFSET_A,
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

/* GCC14: the sky follows the fog.  At infinity the fog is at its maximum, so the sky should be
   black; its palette is only lowered enough not to punch a hole.  4096 (fog off) leaves it
   intact, 512 lowers it to a third. */
static int skyFadeFor(int fog)
{int f=4+(12*fog)/4096;
 if (f>16) f=16;
 if (f<0) f=0;
 return f;
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
 if (mpPlayers==1)              /* GCC14: the VDP2 colour offset is the whole screen's -- in
				   split screen one player's wound would flash all four views */
    {colorOffset[0]=63;
     colorOffset[1]=-63;
     colorOffset[2]=-63;
    }
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
 if (dvel<CFG_FALL_MIN)
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
	} CFG_USE_REFUSED
    }
}

/* GCC14: movePlayer's edge state, per player (registered in mpRegisterEngine) */
static unsigned short lastInput=0;
static char wasUnderWater=0;
static char mvRespawn;          /* a dead player asked to come back (multiplayer only) */

void movePlayer(int inputEnd,int nmFrames)
{unsigned short input;
 unsigned short changeInput=0;
 unsigned short pushed;
 int inputPos,i;
 inputPos=inputEnd-nmFrames;
 if (inputPos<0) inputPos+=INPUTQSIZE;
 while (inputPos!=inputEnd)
    {/* control input: player 1 keeps the engine's queue, the others read their own pad */
     input=mpCur? inputQP[mpCur][inputPos]: inputQ[inputPos];

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
	     if (mpPlayers>1)
		{if (playerIsDead>60 &&
		     (pushed & (IMASK(ACTION_FIRE)|IMASK(ACTION_PUSH))))
		    mvRespawn=1;        /* Doom co-op: use or fire brings you back */
		}
	     /* GCC14: CFG_DEATH_HOLD -- the view STAYS on the corpse, at full brightness and full
		volume, as Doom's does.  Fire or use asks to go on; the screen and the sound fade
		out only from there, and the level ends when the fade is done.  PowerSlave has its
		own death, which fades on its own: CFG_DEATH_HOLD is 0 there and the count below is
		the original one, tics since the death. */
	     else if (CFG_DEATH_HOLD && !deathFade)
		{if (playerIsDead>60 &&
		     (pushed & (IMASK(ACTION_FIRE)|IMASK(ACTION_PUSH))))
		    {deathFade=1;
		     colorCenter[0]=-255;
		     colorCenter[1]=-255;
		     colorCenter[2]=-255;
		    }
		}
	     else
		{i=15-((CFG_DEATH_HOLD? deathFade++: playerIsDead-120)>>2);
		 if (i<0) i=0;
		 if (i>15)i=15;
		 setMasterVolume(i);    /* the master volume is everybody's */
		}
	     currentState.health=0;
	     weaponSetVel(0,F(4));
	     CFG_DEATH_LOOK();          /* GCC14: the view swings towards the killer */
	     if (playerAngle.pitch<CFG_DEATH_PITCH)
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
	{if (CFG_WATER && (camera->flags & SPRITEFLAG_UNDERWATER))
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
	    if (CFG_DOLLPOWER && (currentState.gameFlags & GAMEFLAG_DOLLPOWERMODE))
	       dollPowerControlInput(input,changeInput);
	    else
	       CFG_CONTROL(input,changeInput,pushed);
	   }
	 if (currentState.health<=0)
	    {playerIsDead=1;
	     deathFade=0;               /* CFG_DEATH_HOLD: the fade waits for fire (above) */
	     if (!CFG_DEATH_HOLD || mpPlayers>1)  /* the VDP2 colour offset fades every view: split
					   screen cannot fade one player out, so it fades at once */
		{colorCenter[0]=-255;
		 colorCenter[1]=-255;
		 colorCenter[2]=-255;
		}

	     playStaticSound(ST_JOHN,CFG_DEATH_SFX);
	     switchPlayerMotion(0);
	     xavel=0;
	     yavel=0;
	     camera->vel.y+=F(6);
	     camera->vel.x+=MTH_Sin(playerAngle.yaw)*5;
	     camera->vel.z-=MTH_Cos(playerAngle.yaw)*5;
	     deathTimer=0;
	    }

	 if (CFG_ENGINE_FIRE && !(input & IMASK(ACTION_FIRE)))
	    {if (weaponSequenceQEmpty())
		fireWeapon();
	    }

	 if (pushed&IMASK(ACTION_WEPUP))
	    {CFG_WEAPON_UP(WEAPONINV(currentState.inventory));
	     CFG_BOWLDOTS();
	    }

	 if (CFG_WEPDN && (pushed&IMASK(ACTION_WEPDN)))
	    {CFG_WEAPON_DN(WEAPONINV(currentState.inventory));
	     CFG_BOWLDOTS();
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
      CFG_FRAME(input,pushed); moveCamera();
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


/* GCC14: NBG0 as the VDP2 weapon sheet (512x512 at VRAM B, hidden until displayVDP2Pic), apart
   from reading it: a game with no sheet still leaves NBG0 so after its loading screen (Doom) */
void vdp2SheetConfig(void)
{SclConfig scfg;
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

void loadVDP2Sprites(int fd)   /* GCC14: the read, then vdp2SheetConfig (split for Doom) */
{fs_read(fd,(char *)SCL_VDP2_VRAM+1024*256,1024*256);
 vdp2SheetConfig();
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
 CFG_BOWLDOTS_GUARD barPic=(unsigned char *)((EZ_charNoToVram(0)<<3)+0x5c00000);
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

/* --- local multiplayer: the engine's side (MPLAYER.H) ------------------------------------------
   The engine's player is the set of globals below; each player gets its own copy, swapped by
   mpSwitch.  The game registers its own player through CFG_MP_REGISTER. */
static void mpRegisterEngine(void)
{MPREG(player); MPREG(camera); MPREG(playerAngle);
 MPREG(xavel); MPREG(yavel);
 MPREG(keyMask); MPREG(playerIsDead); MPREG(playerMotionEnable); MPREG(stunCounter);
 MPREG(invisibleCounter); MPREG(weaponPowerUpCounter); MPREG(deathTimer);
 MPREG(ouchTime); MPREG(ltHurtAmount); MPREG(ltHurtTime);
 MPREG(playerHeightOffset); MPREG(playerHeightVel);
 MPREG(lastInput); MPREG(wasUnderWater); MPREG(mvRespawn);
 MPREG(currentState.health); MPREG(currentState.inventory);
 MPREG(currentState.weaponAmmo); MPREG(currentState.desiredWeapon);
 MPREG(autoTarget);             /* autoTFormedPos is declared in WALLS.H but defined nowhere: unused */
 MPREG(currentMessage); MPREG(messageAge); MPREG(messageXPos);
 sequenceMpRegister();          /* SEQUENCE.C: the weapon's animation queue */
 weaponMpRegister();            /* WEAPON.C: its position, velocity, current weapon */
 CFG_MP_REGISTER();             /* the game's own player */
}

/* The views.  Solo: the original window, 320 x 192 over the 32-line status bar, focal 160
   (90 deg).  Split screen has NO status bar, so the views take the lines the bar had
   (CFG_SPLIT_H, SPRITE.H): 2 players are two halves side by side, 3-4 players quadrants over a
   16-line band each, and the 4th quadrant stays black in 3p.  Split uses focal 126, 65 degrees
   over 160 pixels -- the focal and the WIDTH set the field of view, the height only says how
   much of the vertical cone a view shows, so a taller view adds sky and floor and stretches
   nothing.  The horizon sits where solo puts it, 7/12 of the way down. */
typedef struct {short x0,y0,w,h,cy;} MpView;
static void mpViewGeometry(int k,MpView *v)
{if (mpPlayers==1)
    {v->x0=0; v->y0=0; v->w=320; v->h=192; v->cy=CFG_YCENTER;}
 else if (mpPlayers==2)
    {/* the horizon stays on the line solo puts it on: a taller view adds FLOOR below it */
     v->x0=160*k; v->y0=0; v->w=160; v->h=CFG_SPLIT_H; v->cy=CFG_YCENTER;}
 else
    /* 3-4 players are left alone: the split sky's bands are cut to these very lines
       (MPSKY.C bandRect, VAULT_ROWS), so moving a quadrant moves it off its sky. */
    {v->x0=160*(k&1); v->y0=112*(k>>1); v->w=160; v->h=96; v->cy=56;}
}

/* Window, focal and local origin of view k.  emit=0 sets the renderer's globals only (the tail
   kick traverses view 0 of the next image with them); emit=1 also puts the VDP1 local origin
   and user clip on the view, for what is drawn next. */
static void mpSetViewport(int k,int emit)
{MpView v;
 mpViewGeometry(k,&v);
 viewCx=v.x0+v.w/2;
 viewCy=v.y0+v.cy;
 viewXmin=-v.w/2;
 viewXmax=v.w/2;
 viewYmin=-v.cy;
 viewYmax=v.h-v.cy;
 focalDist=(mpPlayers==1)? FOCALDIST: 126;
 if (emit)
    {XyInt r[2];
     r[0].x=v.x0; r[0].y=v.y0;
     r[1].x=v.x0+v.w-1; r[1].y=v.y0+v.h-1;
     EZ_localCoord(viewCx,viewCy);
     EZ_userClip(r);
    }
}

static MthXyz mpSpawnPos[MPMAX];
static int mpSpawnSector[MPMAX];
static Orient mpSpawnAngle[MPMAX];
static char mpBuilt[MPMAX];     /* slot k holds a player whose state goes on to the next level */
static char mpInLevel[MPMAX];   /* ... and whose body exists in THIS level */

/* Player k's body next to player 1's.  The converter keeps only Doom's start 1, so the others
   step a few paces away from it through the engine's own collision -- never through a wall.
   The other bodies are made transparent for the move, as for a missile leaving its shooter. */
static void mpPlaceNear(Sprite *s,int k)
{static const signed char dir[MPMAX][2]={{0,0},{1,0},{-1,0},{0,-1}};
 Sprite *ref=mpBody[0];
 int saved[MPMAX],j,step;
 moveSpriteTo(s,ref->s,&ref->pos);
 for (j=0;j<mpPlayers;j++)
    if (mpBody[j] && mpInLevel[j])
       {saved[j]=mpBody[j]->flags;
	mpBody[j]->flags|=SPRITEFLAG_NOSPRCOLLISION;
       }
 for (step=0;step<4;step++)
    {s->vel.x=F(12)*dir[k][0];
     s->vel.y=0;
     s->vel.z=F(12)*dir[k][1];
     moveSprite(s);
    }
 s->vel.x=s->vel.y=s->vel.z=0;
 for (j=0;j<mpPlayers;j++)
    if (mpBody[j] && mpInLevel[j])
       mpBody[j]->flags=saved[j];
 /* GCC14: those moves went through collideSprite, which opens its profile node where it is
    called -- here, between two images, at the root of the tree, where it would stay at 0.0.
    The tree times images: it starts over */
 initProfiler();
}

/* GCC14: a fighting game (MPLAYER.H mpCompetitive) puts a player on the spawn spot farthest from
   the others, standing on its floor, eye at its height; 0 = the level gave no spot */
static int mpPlaceFar(Sprite *s,int k)
{MthXyz pos;
 int sector,yaw;
 if (!mpCompetitive() || !mpSpotFar(k,&sector,&pos,&yaw))
    return 0;
 pos.y+=F(GP_PLAYER_RADIUS+GP_PLAYER_EYE_HOVER);
 moveSpriteTo(s,sector,&pos);
 s->vel.x=s->vel.y=s->vel.z=0;
 playerAngle.yaw=yaw;
 s->angle=yaw;
 return 1;
}

/* The per-player part of the level start, the same calls in the same order as for player 1. */
static void mpPlayerLevelInit(void)
{autoTarget=NULL;               /* a carried slot holds last level's pointer */
 playerIsDead=0; deathFade=0; mvRespawn=0; deathTimer=0; stunCounter=0;
 xavel=0; yavel=0;
 playerHeightOffset=0; playerHeightVel=0;
 ouchTime=0; ltHurtTime=0;
 switchPlayerMotion(1);
 initWeapon(); CFG_LEVEL_PLAYER_INIT();
 switchWeapons(1);
}

/* Build player k in this level: a body, a place, and either the state it carried from the
   level before or a fresh one copied from player 1's and reset by the game's own init. */
static void mpBuild(int k)
{int prev=mpCur;
 assert(k>0 && k<MPMAX);
 if (mpInLevel[k])
    {/* already has a body here (it left and came back): revive it where player 1 stands */
     mpSwitch(k);
     camera->flags&=~(SPRITEFLAG_INVISIBLE|SPRITEFLAG_NOSPRCOLLISION);
     mpPlaceNear(camera,k);
     mpSwitch(prev);
     return;
    }
 if (!mpBuilt[k])
    mpStoreAs(k);               /* template: player 1's copies */
 mpSwitch(k);
 if (!mpBuilt[k])
    CFG_MP_NEWPLAYER();         /* nothing to carry: the game's init gives the starting kit */
 player=constructPlayer(mpBody[0]->s,0);
 /* GCC14: no body -- the level's own objects filled the pool (OBJECT.C objectsRefused).  This
    player is not in the level, and mpLevelBuild stops there rather than draw a view with
    nothing behind it. */
 if (!player)
    {mpSwitch(prev);
     return;
    }
 camera=player->sprite;
 mpBody[k]=camera;
 mpObj[k]=(Object *)player;
 mpInLevel[k]=1;
 mpPeek(0,&playerAngle,sizeof(playerAngle),&playerAngle);
 camera->angle=playerAngle.yaw;
 if (!mpPlaceFar(camera,k))
    mpPlaceNear(camera,k);
 mpSpawnPos[k]=camera->pos;
 mpSpawnSector[k]=camera->s;
 mpSpawnAngle[k]=playerAngle;
 mpPlayerLevelInit();
 mpBuilt[k]=1;
 mpSwitch(prev);
}

/* Players 2..mpPlayers at level start, once player 1 is fully set up. */
static void mpLevelBuild(void)
{int k;
 mpRegisterEngine();
 assert(mpCur==0);
 mpBody[0]=camera;
 mpObj[0]=(Object *)player;
 mpInLevel[0]=1;
 mpBuilt[0]=1;
 mpSpawnPos[0]=camera->pos;
 mpSpawnSector[0]=camera->s;
 mpSpawnAngle[0]=playerAngle;
 for (k=1;k<MPMAX;k++)
    mpInLevel[k]=0;
 /* GCC14: a player the level had no room for ends the count -- fewer players is better than a
    view with no body behind it */
 for (k=1;k<mpPlayers;k++)
    {mpBuild(k);
     if (!mpInLevel[k])
	{mpPlayers=k;
	 break;
	}
    }
 mpSetBanks();                  /* loadPalletes and initPlax just rebuilt banks 1..7 */
 mpSkyPlayers(mpPlayers);       /* split screen's sky (MPSKY.C), from the sky initPlax loaded */
 wallsSplitReset();             /* the level's low RAM was reset with it */
 if (mpPlayers>1)
    wallsSplitAlloc();
}

/* A new game: the count armed at the menu, and no player carries anything into it. */
static void mpNewGame(void)
{int k;
 mpPlayers=mpArmed;
 for (k=0;k<MPMAX;k++)
    mpBuilt[k]=0;
}

/* A dead player back at its spawn point with the starting kit (Doom co-op: G_DoReborn). */
static void mpRespawn(int k)
{/* GCC14: the game can keep this one where it fell -- a demon with no monster left to take over
    stays dead on the corpse, instead of standing its body at a spawn spot.  Fire asks again. */
 if (CFG_MP_RESPAWN_HOLD(k))
    {mvRespawn=0;
     return;
    }
 removeLight(camera);           /* died firing: doom_playerInit zeroes muzzleTics, not the light */
 playerAngle=mpSpawnAngle[k];
 if (!mpPlaceFar(camera,k))     /* GCC14: a fighting game respawns away from the others */
    {moveSpriteTo(camera,mpSpawnSector[k],&mpSpawnPos[k]);
     camera->vel.x=camera->vel.y=camera->vel.z=0;
     camera->angle=playerAngle.yaw;
    }
 CFG_MP_NEWPLAYER();
 mpPlayerLevelInit();
 CFG_MP_RESPAWNED(k);           /* GCC14: the game's roles (a monster to take over, the boss) */
}

/* The loaded player's view matrix, pushed on viewTransform (the caller pops it) -- as the view
   loop builds it, less the earthquake's jitter, which is view 0's alone. */
static void mpPushViewMatrix(MthMatrixTbl *vt)
{MTH_PushMatrix(vt);
 MTH_RotateMatrixZ(vt, playerAngle.roll );
 MTH_RotateMatrixX(vt, playerAngle.pitch );
 MTH_RotateMatrixY(vt, playerAngle.yaw );
 MTH_MoveMatrix(vt,
		-camera->pos.x,
		-camera->pos.y+playerHeightOffset+CFG_VIEW_BOB,
		-camera->pos.z);
}

/* The others' bodies as view `viewer` sees them, its own hidden.  The game picks the frame
   (CFG_MP_BODYSEQ); -1 = not drawn.  Solo: the camera keeps its -1, as it always had. */
static void mpShowBodies(int viewer)
{int k;
 if (mpPlayers==1)
    return;
 for (k=0;k<mpPlayers;k++)
    mpBody[k]->sequence=(k==viewer)? -1: CFG_MP_BODYSEQ(mpBody[k],mpBody[viewer],mpPeekInt(k,&currentState.health));
}


/* START on pad 2, in a level: one more player, and from 4 back to 1.  The traversal started in
   the last image's tail was made for the old view 0, whose window changes with the count. */
static void mpPollStart(void)
{static char held[MPMAX]={1,1,1,1};
 int j,k,down,n;
 CFG_PAUSE_POLL(-1);                /* GCC14: nobody -- a request the loop did not take this image
				       (motion off, the level ending) is dropped, not kept for later */
 for (j=1;j<MPMAX;j++)
    {down=!(lastInputSampleP[j] & PER_DGT_S);
     if (down && !held[j] && j<mpPlayers)
	CFG_PAUSE_POLL(j);            /* GCC14: a player already in pauses the game (SPRITE.H) */
     if (down && !held[j] && j==mpPlayers && j<mpPadsPresent)
	{wallsPipeDiscard();
	 mpSwitch(0);
	 if (mpMode==MP_TEAM)           /* the smaller team */
	    {for (k=0,n=0;k<mpPlayers;k++)
		n+=mpTeam[k]? 1: -1;
	     mpTeam[j]=(n<0);
	    }
	 mpRole[j]=0;                   /* a newcomer is the normal player -- unless the game says */
	 mpPlayers++;
	 mpBuild(j);
	 if (!mpInLevel[j])             /* GCC14: the level had no object left for it (mpBuild) */
	    {mpPlayers--;
	     changeMessage("NO ROOM IN THIS LEVEL");
	     held[j]=(char)down;
	     continue;
	    }
	 CFG_MP_JOINED(j);              /* GCC14: its role (a boss, when two marines are in already) */
	 wallsSplitAlloc();
	 mpArmed=mpPlayers;
	 mpSetBanks();
	 mpSkyPlayers(mpPlayers);
	 mpSetViewport(0,0);
	 {static char *msg[MPMAX]={"","PLAYER 2 JOINS","PLAYER 3 JOINS","PLAYER 4 JOINS"};
	  changeMessage(msg[j]);
	 }
	}
     held[j]=(char)down;
    }
}

/* --- adaptive LOD in split screen -------------------------------------------------------------
   The currency is the cell: what the VDP1 list holds -- 1448 commands an image, the tail dropped
   silently past that (SPR.C flushCmdBuffer) -- and what the two CPUs pay per image.  ONE budget
   for all the views, shared out max-min: a view that needs less than an equal share keeps only
   what it uses, and what it leaves goes to the others.  Each view then steers its OWN fog
   distance, continuously between 4096 and 512, so that its cell count tracks its share: the fog
   IS the LOD, since the light LOD folds and welds whatever the fog has blacked out.

   The budget IS the hardware ceiling -- the command list, less what the image spends on things,
   weapons, HUD and overlay.  The frame rate does not steer it: on the console the split image is
   master-bound (traversal, sprites, logic), not cell-bound, and fog driven by the fps darkened
   every view to 512 for a few percent (HW, 4p: 7 fps either way).  So the fog closes in only
   where the list would overflow -- or the slave's cell records (MAXNMSLAVEPOLYS), past which it
   drops whole walls.
   One rule for every view, solo included (gameparams SOLO_FOG = budget; toggle keeps PowerSlave's
   solo, the L+R+Z fog alone): the budget steers the view's fog, no fog while it holds, the L+R+Z
   toggle's fog is a ceiling, and the far LOD (FAR_LOD, WALLS.C wallIsFar) sits at FAR_LOD, or at
   the fog when the fog is nearer. */
#define MPFOGMIN 512
#define MPFOGMAX 4096
#define MPQUADLIM (MAXNMSLAVEPOLYS-50-(MAXNMSLAVEPOLYS>>4))
static int mpView;              /* the view being drawn */
static int mpFog[MPMAX]={MPFOGMAX,MPFOGMAX,MPFOGMAX,MPFOGMAX};
static int mpCells[MPMAX],mpShare[MPMAX],mpQuads[MPMAX];
static int mpBudget;
#ifdef STATUSTEXT
static int loadT0,loadFields;   /* GCC14: runLevel's start, the fields its load took (L:) */
#endif

static void mpSetViewFog(int k)
{int f=mpFog[k];
#ifndef GP_SOLO_FOG_BUDGET
 if (mpPlayers==1)
    f=MPFOGMAX;                 /* solo's fog is the toggle's alone */
#endif
 if (f>fogCap)                  /* the player's fog (options, L+R+Z): a ceiling on every view's */
    f=fogCap;
 if (f!=fogDist)
    {setFog(f);
     if (mpPlayers==1)          /* split screen has no sky: it lends the sky's bank to a player */
	setPlaxFade(skyFadeFor(f));
    }
 lodFar=GP_FAR_LOD? (GP_FAR_LOD<f? GP_FAR_LOD: f): 0;
}

static void mpViewDone(int k)
{mpCells[k]=nmPolys+nmSlavePolys;
 mpQuads[k]=nmSlavePolys;
}

/* once per image, after the VDP1 is done */
static void mpBalance(void)
{int k,n,left,changed,eq,cells=0;
 int want[MPMAX],done[MPMAX];
#ifndef GP_SOLO_FOG_BUDGET
 if (mpPlayers==1)
    return;
#endif
 for (k=0;k<mpPlayers;k++)
    cells+=mpCells[k];
 mpBudget=EZ_cmdsCap()-(EZ_cmdsUsed()-cells)-32;
 if (mpBudget<48*mpPlayers) mpBudget=48*mpPlayers;
 /* max-min: a view drawn without fog and under its share wants what it used, plus an eighth
    to turn round in; a fogged view wants all it can get */
 for (k=0;k<mpPlayers;k++)
    {done[k]=0;
     want[k]=(mpFog[k]>=MPFOGMAX && mpCells[k]<=mpShare[k])? mpCells[k]+(mpCells[k]>>3): 0x7fffffff;
    }
 left=mpBudget;
 n=mpPlayers;
 do {changed=0;
     eq=left/n;
     for (k=0;k<mpPlayers;k++)
	if (!done[k] && want[k]<=eq)
	   {mpShare[k]=want[k]; left-=want[k]; n--; done[k]=1; changed=1;}
    } while (changed && n>0);
 for (k=0;k<mpPlayers;k++)
    if (!done[k])
       mpShare[k]=left/n;
 /* each view closes in on its share: fog in by 1/16 an image, out by 1/32 */
 for (k=0;k<mpPlayers;k++)
    {if (mpCells[k]>mpShare[k] || mpQuads[k]>MPQUADLIM)
	mpFog[k]-=mpFog[k]>>4;
     else if (mpCells[k]<mpShare[k]-(mpShare[k]>>3) && mpQuads[k]<MPQUADLIM-(MPQUADLIM>>3))
	mpFog[k]+=(mpFog[k]>>5)+1;
     if (mpFog[k]<MPFOGMIN) mpFog[k]=MPFOGMIN;
     if (mpFog[k]>MPFOGMAX) mpFog[k]=MPFOGMAX;
    }
}

int runLevel(char *filename,int levelNm)
{XyInt noUserClip[2]={{0,0},{320-1,240-1}};
 int i,monsterMoveCounter,musicMark;
 int nmWeaponTiles,nmStaticSounds;
 int lastDraw=0,lastCalc=0;
 int framesElapsed,inputEnd;
 unsigned int smoothVTime;
 int vspeedSwitchCount;
 int lastLastCalc=0;
 int lastYaw,lastPitch,mmcSave=0;

 MthMatrixTbl viewTransform;
 MthMatrix matstack[4];

 dPrint("vroom!\n");
#ifdef STATUSTEXT
 loadT0=vtimer;                 /* GCC14: the load's length, for the overlay (L:) */
#endif
 nmFullBowls=0;
 healthMeterPos=0;
 musicMark=cdMark();            /* GCC14: the same track picks up there after the load */
 stopCD();
 initSound();

 quitRequest=0;
 mem_init();
 /* do hardware initialization */
 dPrint("A!\n");
 plaxOff();
 mpSkyOff();                    /* the solo set-up below takes the VDP2 back */
 dPrint("A1!\n");
 setVDP2();
 dPrint("B!\n");
 displayEnable(0);

 SPR_SetTvMode(SPR_TV_NORMAL,CFG_TV_SIZE,OFF);
 EZ_initSprSystem(1448,4,1224,
		  240,0x8000);
 dPrint("C!\n");
 SCL_SetFrameInterval(0xfffe);

 /* Three empty frames: in frame mode 3 each change erases the other buffer, so after them both
    are black -- and they are also what makes the VDP1 finish the list the last level left it.
    A death load tried skipping them, to keep the corpse on screen under the loading fire, and
    froze on the first draw of the loading screen (CFG_DEATH_WAIT, SPRITE.H). */
 for (i=0;i<3;i++)
    {EZ_openCommand();
     EZ_sysClip();
     EZ_closeCommand();
     SCL_DisplayFrame();
    }
 CFG_HUD_CHARS(EZ_setChar(0,COLOR_4,*(int *)stat_bar,*(int *)(stat_bar+4),
	    (Uint8 *)stat_bar+8);
 EZ_setChar(1,COLOR_4,*(int *)stat_compass0,*(int *)(stat_compass0+4),
	    (Uint8 *)stat_compass0+8);
 EZ_setChar(2,COLOR_4,*(int *)stat_compass1,*(int *)(stat_compass1+4),
	    (Uint8 *)stat_compass1+8);
 EZ_setChar(3,COLOR_4,*(int *)stat_compass2,*(int *)(stat_compass2+4),
	    (Uint8 *)stat_compass2+8));

#ifdef JAPAN
 initPicSystem(4,((int []){28,30,1,10,12,30,-1}));
#else
 i=initFonts(CFG_FONT_BASE,CFG_FONT_MASK);
 initPicSystem(i,((int []){GP_PIC_SLOTS,-1}));
#endif
 dPrint("ert!\n");
 CFG_REDRAW_STATBAR();

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
  CFG_LOADING_SCREEN(fd);      /* GCC14: a game's loading screen (SPRITE.H) */
  dPrint("frop!\n");
  displayEnable(1);
  CFG_VDP2_SHEET(fd);          /* GCC14: PowerSlave's weapon sheet; Doom has none */
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
#ifdef STATUSTEXT
 loadFields=vtimer-loadT0;            /* GCC14: the L: probe */
#endif

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

 CFG_LOADING_END();            /* GCC14: the game's VDP2 back from its loading screen */
 startSlave(wallRenderSlaveMain);
 delay(1);

 /* air meter stuff */
 CFG_AIR_PICS(airBase=addPic(TILE16BPP,meter_bubble+8,NULL,0));
 CFG_AIR_PICS(addPic(TILE16BPP,meter_back+8,NULL,0));


 SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0|SCL_RBG0,-255,-255,-255);
 colorOffset[0]=-255; colorOffset[1]=-255; colorOffset[2]=-255;
 EZ_setErase(240,0x0000);

 debugPrint("Loaded dynamic\n");
 CFG_ROUTE_INIT();
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
 for (i=0;i<MPMAX;i++)             /* last level's bodies are gone: nobody is a player until */
    {mpBody[i]=NULL;               /* mpLevelBuild says so (a stale pointer could name a     */
     mpObj[i]=NULL;                /* monster of this level)                                  */
    }
 playerAngle.pitch=0;
 playerAngle.yaw=F(0);
 mpLevelReset();                   /* GCC14: the score and the spawn spots the placement adds */
 placeObjects();
 CFG_LEVEL_PLACED();               /* GCC14: the level stands, no player is built yet -- where a
				      game turns its monsters into spawn spots (SPRITE.H) */
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
 initWeapon(); CFG_LEVEL_PLAYER_INIT();
 playerIsDead=0;
 deathFade=0;                   /* GCC14: with playerIsDead, or the next death ends at once */
 hitCamel=0;
 hitPyramid=0;
 hitTeleport=0;
 initInput();
 playCDTrackForLevelFrom(levelNm,musicMark);

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

 /* GCC14: split screen -- cut the VDP2 weapon sheet into VDP1 tiles, after every other tile so
    the level's own indices (LEVEL.C tileBase) do not move.  Armed at the menu, or simply a
    second pad plugged in: a player may still join in the middle of a level. */
 if (mpArmed>1 || mpPadsPresent>1)
    picWeaponSprites();
 mipBase=createMippedPics();
 setFog((fogDist<fogCap)? fogDist: fogCap);   /* fills the table; fogDist survives from one level
						  to the next, under the options' ceiling */
 setPlaxFade(skyFadeFor(fogDist));  /* initPlax restored the original palette on load */
 mpLevelBuild();                    /* players 2..: player 1 is fully set up by now */
 CFG_MP_LEVELSTART();              /* GCC14: the game's roles, every player built */
 crashInstall();                    /* the vectors again: a level load may have re-registered */

 while(1)
    {htimer=0;
     crashBeat();                   /* freeze report: armed while the loop turns (CRASH.H) */
     mpPollStart();                 /* START on the next pad: that player joins, once */
     soundNmEars=(mpPlayers>1)? mpPlayers: 0;   /* GCC14: every player hears (SOUND.C) */
     for (i=0;i<soundNmEars;i++)
	soundEar[i]=mpBody[i];
     {/* GCC14: hold L+R+X together -- or X+Y+Z, for pads whose triggers report only
	 analog values -- to flip runtime mipmapping (mipEnable, WALLS.C) */
      static char mipChord=0;
      if (((((~lastInputSample)&(PER_DGT_TL|PER_DGT_TR|PER_DGT_X)))==
	   (PER_DGT_TL|PER_DGT_TR|PER_DGT_X)) ||
	  ((((~lastInputSample)&(PER_DGT_X|PER_DGT_Y|PER_DGT_Z)))==
	   (PER_DGT_X|PER_DGT_Y|PER_DGT_Z)))
	 {if (!mipChord)
	     {mipEnable=!mipEnable;
	      changeMessage(mipEnable? "MIPMAPPING ON": "MIPMAPPING OFF");
	      mipChord=1;
	     }
	 }
      else
	 mipChord=0;
     }
     {/* GCC14: hold L+R+B -- or A+B+Z -- to cycle the light LOD: off, fuse, fuse PAINTED blue
	  (shows which walls go; used to tune the fog until the switch is invisible). */
      static char lodChord=0;
      if (((((~lastInputSample)&(PER_DGT_TL|PER_DGT_TR|PER_DGT_B)))==
	   (PER_DGT_TL|PER_DGT_TR|PER_DGT_B)) ||
	  ((((~lastInputSample)&(PER_DGT_A|PER_DGT_B|PER_DGT_Z)))==
	   (PER_DGT_A|PER_DGT_B|PER_DGT_Z)))
	 {if (!lodChord)
	     {static char *lodName[3]={"LOD OFF","LOD ON","LOD PAINTED"};
	      lodEnable=(lodEnable+1)%3;
	      changeMessage(lodName[lodEnable]);
	      lodChord=1;
	     }
	 }
      else
	 lodChord=0;
     }
     {/* GCC14: hold L+R+Z -- or A+C+Z, for pads whose triggers are analog only -- to cycle the
	  depth fog: original, then black at 2048, 1024 and 512 units.  A deliberate departure
	  from Doom, which never darkens a fully lit sector: the point is to judge it on screen.
	  It is a ceiling: the budget may bring a view's fog nearer (mpSetViewFog). */
      static char fogChord=0;
      if (((((~lastInputSample)&(PER_DGT_TL|PER_DGT_TR|PER_DGT_Z)))==
	   (PER_DGT_TL|PER_DGT_TR|PER_DGT_Z)) ||
	  ((((~lastInputSample)&(PER_DGT_A|PER_DGT_C|PER_DGT_Z)))==
	   (PER_DGT_A|PER_DGT_C|PER_DGT_Z)))
	 {if (!fogChord)
	     {static char *fogName[4]={"FOG OFF","FOG LOW (2048)","FOG MEDIUM (1024)","FOG HIGH (512)"};
	      int fogIndex=(fogLevel()+1)&3;   /* the options' level, one step on */
	      fogCap=fogLevels[fogIndex];
	      setFog(fogCap);
	      if (mpPlayers==1)   /* split screen lends bank 7 to a player (MPLAYER.C) */
	         setPlaxFade(skyFadeFor(fogCap));
	      changeMessage(fogName[fogIndex]);
	      fogChord=1;
	     }
	 }
      else
	 fogChord=0;
     }
#ifdef STATUSTEXT
     {/* hold L+R+A -- or X+Y+A -- to PAINT THE BACKGROUND: the VDP1 erase stops being
	 transparent (0x0000) and becomes magenta, so every pixel the world does not draw
	 shows up magenta instead of the VDP2 sky behind it.  That is the only way to tell a
	 hole (nothing emitted there) from a cell painted flat in its tile's first texel,
	 which a plain sky imitates exactly.  Everything really open to the sky goes magenta
	 too -- that is the point, not a fault. */
      static char holeChord=0,holePaint=0;
      if (((((~lastInputSample)&(PER_DGT_TL|PER_DGT_TR|PER_DGT_A)))==
	   (PER_DGT_TL|PER_DGT_TR|PER_DGT_A)) ||
	  ((((~lastInputSample)&(PER_DGT_X|PER_DGT_Y|PER_DGT_A)))==
	   (PER_DGT_X|PER_DGT_Y|PER_DGT_A)))
	 {if (!holeChord)
	     {holePaint=!holePaint;
	      EZ_setErase(240,holePaint? RGB(31,0,31): 0x0000);
	      changeMessage(holePaint? "BACKGROUND PAINTED": "BACKGROUND CLEAR");
	      holeChord=1;
	     }
	 }
      else
	 holeChord=0;
     }
     {/* hold L+R+Y -- or A+B+C, for pads whose triggers report only analog values --
	 to show the per-frame profile tree (PROFILE.C) */
      static char profChord=0;
      if (((((~lastInputSample)&(PER_DGT_TL|PER_DGT_TR|PER_DGT_Y)))==
	   (PER_DGT_TL|PER_DGT_TR|PER_DGT_Y)) ||
	  ((((~lastInputSample)&(PER_DGT_A|PER_DGT_B|PER_DGT_C)))==
	   (PER_DGT_A|PER_DGT_B|PER_DGT_C)))
	 {if (!profChord)
	     {profileShow=!profileShow;
	      changeMessage(profileShow? "PROFILE ON": "PROFILE OFF");
	      profChord=1;
	     }
	 }
      else
	 profChord=0;
     }
#endif
     /* ok */
     if (framesElapsed>8)
	framesElapsed=8;
     /* GCC14: one pass per player (MPLAYER.H).  Solo is a single pass that emits exactly what
	the loop always emitted.  The game logic still runs inside view 0's draw window, between
	the master's walls and drawWallsFinish, while the slave finishes its share -- for every
	player at once; views 1.. are then drawn from the world as this image left it. */
     EZ_openCommand();
     EZ_sysClip();
     /* split screen: views 1.. are traversed by the slave while view 0 is drawn and the logic
	runs (WALLS.C TravSet), from the cameras as they are now -- as view 0's was, in the last
	image's tail */
     if (mpPlayers>1)
	{int k;
	 for (k=1;k<mpPlayers;k++)
	    {mpSwitch(k);
	     mpSetViewport(k,0);
	     mpPushViewMatrix(&viewTransform);
	     wallsQueueView(k,viewTransform.current,camera);
	     MTH_PopMatrix(&viewTransform);
	    }
	 mpSwitch(0);
	}
     CFG_MAP_TOGGLE();              /* GCC14: before anything of the image is drawn */
     for (mpView=0;mpView<mpPlayers;mpView++)
	{mpSwitch(mpView);
	 mpSetViewport(mpView,mpPlayers>1);
	 MTH_PushMatrix(&viewTransform);
	 MTH_RotateMatrixZ(&viewTransform, playerAngle.roll );
	 MTH_RotateMatrixX(&viewTransform, playerAngle.pitch );
	 MTH_RotateMatrixY(&viewTransform, playerAngle.yaw );
	 if (earthQuake && mpView==0)
	    {camera->pos.x+=(MTH_GetRand()%(earthQuake<<15))-(earthQuake<<14);
	     camera->pos.y+=(MTH_GetRand()%(earthQuake<<15))-(earthQuake<<14);
	     camera->pos.z+=(MTH_GetRand()%(earthQuake<<15))-(earthQuake<<14);
	     earthQuake--;
	    }
	 MTH_MoveMatrix(&viewTransform,
			-camera->pos.x,
			-camera->pos.y+playerHeightOffset+CFG_VIEW_BOB,
			-camera->pos.z);
	 if (mpPlayers==1)
	    {EZ_userClip(noUserClip);
	     EZ_localCoord(320/2,CFG_YCENTER);
	    }
	 pushProfile("Walls");
#if WALLPIPE
	 /* collect the traversal started in last frame's tail, BEFORE the
	    master touches sectorDraw[] -- it was view 0's */
	 if (mpView==0)
	    wallsPipeJoin();
#endif
	 mpSetViewFog(mpView);
	 mpShowBodies(mpView);
	 if (mpPlayers>1)
	    mpSkyMoon(mpView,viewTransform.current);
	 if (!(CFG_MAP_HIDES_VIEW && mapOn && mpPlayers==1))   /* GCC14: drawMap draws instead */
	    drawWalls(mpView,viewTransform.current);
	 popProfile();

	 if (mpPlayers==1)
	    EZ_userClip(noUserClip);
	 else
	    mpSetViewport(mpView,1);    /* drawWalls moved the user clip about: back on the view */

	 if (mpView==0)
	    {int k;
	     pushProfile("Motion");
	     CFG_PROF("Move Player"); for (k=0;k<mpPlayers;k++)
		{mpSwitch(k);
		 movePlayer(inputEnd,framesElapsed);
		 camera->angle=playerAngle.yaw;
		 if (mvRespawn)
		    mpRespawn(k);
		}
	     CFG_PROF_END(); if (monsterMoveCounter>CFG_TIC_CAP)
		monsterMoveCounter=CFG_TIC_CAP;
	     mmcSave=monsterMoveCounter;
	     for (;monsterMoveCounter>CFG_TIC_UNIT-1;monsterMoveCounter-=CFG_TIC_UNIT)
		{CFG_PROF("Player Tic"); for (k=0;k<mpPlayers;k++)
		    {mpSwitch(k);
		     CFG_PLAYER_TIC(); if (ltHurtTime>0)
			{ltHurtTime--;
			 playerHurt(ltHurtAmount);
			 if (!ltHurtTime)
			    stopAllSound(69);
			}
		    }
		 CFG_PROF_END(); mpSwitch(0);
		 pushProfile("Run Objects");
		 runObjects();
		 popProfile();
		 if (mpPlayers>1)       /* the VDP2 colour offset is every view's: kept neutral */
		    changeColorOffset(0,0,0,3);
		 stepColorOffset();
		 CFG_PROF("Post Tic"); for (k=0;k<mpPlayers;k++)
		    {mpSwitch(k);
		     stepPlayerHeight();
		     ouchTime--;
		     if (weaponPowerUpCounter &&
			 !(currentState.gameFlags & GAMEFLAG_DOLLPOWERMODE))
			{weaponPowerUpCounter--;
			 if (weaponPowerUpCounter<60 && !(weaponPowerUpCounter & 0xf))
			    playStaticSound(ST_ITEM,5);
			 if (mpPlayers==1)
			    {if (weaponPowerUpCounter&0x2)
				SCL_SetColOffset(SCL_OFFSET_B,SCL_NBG0,
						 255,60,60);
			     else
				SCL_SetColOffset(SCL_OFFSET_B,SCL_NBG0,
						 0,0,0);
			    }
			}
		     if (invisibleCounter)
			{int rev;
			 invisibleCounter--;
			 if (invisibleCounter<60 && !(invisibleCounter & 0xf))
			    playStaticSound(ST_ITEM,5);
			 rev=INVISIBLEDOSE-invisibleCounter;
			 if (mpPlayers==1)
			    {if (rev>16 && rev<16+20)
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
		    } CFG_PROF_END();
		}
#ifdef GP_GAME_DOOM
	     /* GCC14: the tics have set every gun: each takes its tiles now, before the things of
		views 1.. could take the last slots (DOOM_WEAPON.C doom_weaponReserve, PIC.H) */
	     if (mpPlayers>1)
		for (k=0;k<mpPlayers;k++)
		   doom_weaponReserve(k);
#endif
	     mpSwitch(mpView);
	     popProfile();
	    }
	 pushProfile("Walls");
	 drawWallsFinish();
	 popProfile();
	 mpViewDone(mpView);            /* the slave's cells are only counted once it has joined */
	 mpSkyViewDone(mpView);

	 if (mpView==0)
	    {wallsQueueJoin();          /* the slave reads the walls Post is about to move */
	     for (;mmcSave>CFG_TIC_UNIT-1;mmcSave-=CFG_TIC_UNIT)
		{advanceWallAnimations();
		 stepWater();
		}
	     updatePushBlockPositions();
	     processDelayedMoves();
	    }

	 if (mapOn && mpPlayers==1)
	    drawMap(camera->pos.x,camera->pos.z,camera->pos.y,playerAngle.yaw,
		    camera->s);

	 CFG_PROF("Weapon"); CFG_RUN_WEAPON(framesElapsed,invisibleCounter,weaponPowerUpCounter); CFG_PROF_END();

	 MTH_PopMatrix(&viewTransform);

	 CFG_PROF("HUD");
	 if (mpPlayers==1)
	    {drawMessage(framesElapsed); CFG_DRAW_MESSAGE();
	     CFG_DRAW_STATBAR(framesElapsed);
	    }
	 else
	    CFG_DRAW_SPLITHUD(mpView,mpPlayers);
	 CFG_PROF_END();

	 if (mpPlayers==1)
	    CFG_DRAW_AIRMETER(framesElapsed);   /* GCC14: it is drawn on the solo frame's own HUD */
	}
     /* the rest of the image is player 1's, drawn over the whole screen: the toggles' message,
	the overlay, the kick of view 0's next traversal */
     mpSwitch(0);
     if (mpPlayers>1)
	{mpSetViewport(0,0);
	 EZ_localCoord(320/2,CFG_YCENTER);
	 EZ_userClip(noUserClip);
	 drawMessage(framesElapsed);
	}


#ifdef STATUSTEXT
     /* LEGEND  fps : frames per second
		 lod : fused walls / cells the fusion avoided / cells emitted
		       FLAT.  The first two are no longer in polys; the third still
		       is -- it keeps its VDP1 command and loses only its texture.
		       Black under the fog, or painted beyond the far LOD (FAR_LOD;
		       YELLOW under L+R+B's LOD PAINTED).
		 th  : the things' tiles (PIC.H mapSpritePic) -- requests left out in the
		       last image / the bar in force, px.  0/0 = every thing drawn.  Else the
		       31 sprite slots overflowed: the things smaller on screen than the
		       bar are not drawn, nor their shadow.  The guns take their tiles
		       before views 1..: a gun is refused only when view 0's things alone
		       filled every slot.  Before, the slots were given over under the list.
	 The fps line used nine of the ~40 readable columns, and -50 to -30
	 are taken (time, mem, then the profile tree): the LOD fits here. */
     CFG_PROF("Overlay"); drawStringf(-158,-60,1,"fps:%d lod:%d/%d/%d th:%d/%d",
				      60/framesElapsed,lodFused,lodCells,lodFlat,
				      picLastSpriteOut,picSpriteLod);

     /* LEGEND  sector : the leaf the camera stands in (SRUINS.C camera->s)
		 vis  : sectors the traversal kept -- the update list (WALLS.C updateListSize)
		 sl   : of them, the slave's share / the cells it emitted (nmSlavePolys, also
			on the polys line).  PowerSlave's build shows the leaf alone.
	 GCC14: row -100 and solo only.  It used to sit on -80 and wrote over obj:, both every
	 image.  -100 is the row the shipping build leaves free, and the three that borrow it
	 win over this one: the split screen's c: line, the walk probe (WALK=1), the ASSERT
	 build's extra:.  Before moving any line, check the row -- SRUINS.C draws nine. */
#if defined(NDEBUG) && !defined(WALKPROBE)
     if (mpPlayers==1)
	CFG_STATUS_SECTOR();
#endif

     if (mpPlayers>1)
	{/* LEGEND  B : the split-screen cell budget -- the 1448-command list less what this
		     image spent outside the cells (things, guns, HUD, this overlay)
		 f : each view's fog distance, 512..4096 (4096 = no fog)
		 c : each view's cells in the last image / its share of B.  A view drawn
		     without fog and under its share gives the rest to the others.
		 MTRAV : views the master traverses itself -- their set did not fit in the
		     level's memory (WALLS.C wallsSplitAlloc).  Shown only then.
		 lt : dynamic lights live, of the 15 slots (WALLS.C MAXNMLIGHTSOURCES)
		 mz : one digit per player, 1 = it holds a light right now.  A muzzle flash
		     is a light on the firing player: the digit blinks at every shot.  It
		     stays 0 when the list was full at that moment -- the players are served
		     in order, so 2, 3, 4 are the ones that lose the slot.
		 Only the views in play are listed. */
	 static const char *fmtF[MPMAX+1]={"","","B:%d f:%d %d","B:%d f:%d %d %d",
					   "B:%d f:%d %d %d %d"};
	 static const char *fmtC[MPMAX+1]={"","","c:%d/%d %d/%d","c:%d/%d %d/%d %d/%d",
					   "c:%d/%d %d/%d %d/%d %d/%d"};
	 drawStringf(-158,-110,1,fmtF[mpPlayers],mpBudget,mpFog[0],mpFog[1],mpFog[2],mpFog[3]);
	 if (mpRegisterLost)       /* a player's global had no copy: players would share it */
	    drawStringf(60,-100,1,"MPLOST:%d",mpRegisterLost);
	 if (wallsSplitSets()<mpPlayers-1)
	    drawStringf(60,-110,1,"MTRAV:%d",mpPlayers-1-wallsSplitSets());
	 drawStringf(-158,-100,1,fmtC[mpPlayers],mpCells[0],mpShare[0],
		     mpCells[1],mpShare[1],mpCells[2],mpShare[2],mpCells[3],mpShare[3]);
	 {int k;                       /* the lights, and who holds one: the flash of each player */
	  char mz[MPMAX+1];
	  for (k=0;k<mpPlayers;k++)
	     mz[k]=(mpBody[k] && hasLight(mpBody[k]))? '1': '0';
	  mz[mpPlayers]=0;
	  drawStringf(40,-70,1,"lt:%d mz:%s",nmLights,mz);
	 }
	}

#ifndef NDEBUG
     drawStringf(-158,-100,1,"extra:%d",extraStuff);

     for (i=0;i<16;i++)
	{if (errorQ[i])
	    {drawStringf(15,-50+10*i,1,"%x",errorQ[i]);
	     drawStringf(90,-50+10*i,1,"%x",prQ[i]);
	    }
	}
#endif

     /* LEGEND  polys : cells emitted (walls+floors+ceilings, sprites excluded), total /
			 slave's share.  The second divides SLAVECMDS of the tree to give
			 the cost of one record.  (lod is on the fps line, -60)
		 pipe  : spins at the join of the traversal started in the tail
			 of the previous frame.  0 = it fit entirely in the tail;
			 -1 = nothing was in flight, the master traversed (earthquake,
			 or WALLPIPE at 0). */
     drawStringf(-158,-70,1,"polys:%d/%d pipe:%d",nmPolys+nmSlavePolys,nmSlavePolys,pipeSpin);

     /* LEGEND  obj : objects of the pool in use / its size (OBJECT.C MAXOBJECTS).  Everything
	       the level places takes one, and so does every shot, puff and drop of blood.
	 spr : the same for the sprite pool (SPRITE.C MAXNMSPRITES).
	 lost: objects then sprites the pools REFUSED since the level started.  Anything
	       but 0 means the level is bigger than the engine holds: things are
	       missing, and before the refusal was counted it corrupted memory. */
     drawStringf(-158,-80,1,"obj:%d/%d spr:%d/%d lost:%d/%d",
		 MAXOBJECTS-3-objectsFree(),MAXOBJECTS-3,
		 MAXNMSPRITES-spritesFree(),MAXNMSPRITES,
		 objectsRefused,spritesRefused);

     drawStringf(-158,-50,1,"time:%d %d:%d",(lastCalc+lastLastCalc)>>1,lastDraw,
		 lastCalc+lastDraw);

     drawStringf(-158,-40,1,"mem:%dk+%dk=%dk",mem_coreleft(0)>>10,
		 mem_coreleft(1)>>10,(mem_coreleft(0)+mem_coreleft(1))>>10);

#ifdef WALKPROBE
     /* LEGEND  walk : what the VDP1 steps through for the cells, in thousands of pixels:
			 lines x width of every cell, the pixels outside the window
			 included -- the clipping drops the write, not the step.  A cell
			 over a quarter of the 3D window is re-counted with the pre-clip
			 (a line wholly past one edge is skipped).  WALLASM.H vdp1WalkProbe.
		 big  : those cells / their part of walk.  Things are not counted.
		 rot  : walk if the walls were drawn from patterns turned a quarter --
			vertical lines -- the rest as drawn (WALLASM.H vdp1WalkProbe)
	 Solo only: split screen has its c: line on this row.  The ASSERT build has extra: at
	 its left end, so the line moves right there.  Only on a disc built with the probe
	 (make WALK=1). */
     if (mpPlayers==1)
#ifdef NDEBUG
	drawStringf(-158,-100,1,"walk:%dk big:%d/%dk rot:%dk",vdp1Walk>>4,vdp1Big,
		    vdp1BigWalk>>4,vdp1RotWalk>>4);
#else
	drawStringf(-60,-100,1,"walk:%dk big:%d/%dk rot:%dk",vdp1Walk>>4,vdp1Big,
		    vdp1BigWalk>>4,vdp1RotWalk>>4);
#endif
#endif

     if (profileShow)
	drawProfileData(-158,-28);
     CFG_PROF_END();
#endif

     lastLastCalc=lastCalc;
     lastCalc=htimer;
     sound_nextFrame();
     {int nmSwaps[NMCLASSES];
      int used[NMCLASSES];
      pic_nextFrame(nmSwaps,used);
#ifdef STATUSTEXT
      /* LEGEND  tile: wall-class tiles used in THIS frame, against the
		       slots allocated (params/doom.cfg: PIC_SLOTS=32,31,1,0,0)
		  sw:  slots given to another tile in the frame, ~2.5 ms of master
		       each on hardware (palette expansion + DMA, PIC.C upload).  Under
		       PIC_LOD_PX a wall never takes a slot this image uses, and one the
		       image on screen uses gets its texels once the VDP1 is done with
		       it (Tile Flush in the tree): sw no longer means a wrong texture.
		  flat: cells painted flat in their tile's first texel: too small /
		       every slot taken by this image.  A large cell painted flat looks
		       like a hole onto a plain sky -- L+R+A tells them apart.
		  B:   the cell budget, the VDP1 list less what the image spends
		       outside the cells.  Past it, or past the slave's records,
		       the fog comes in (SOLO_FOG = budget, mpBalance).  Solo only:
		       split screen carries its own B: on that line.
		  L:   the fields the last level load took, from runLevel's start to
		       the last byte read (60 a second): the loading screen's cost
		       shows here (Doom: FIRE_LOAD_FIELDS 2 against 0, console only). */
      /* fog : distance in units at which a fully lit sector reaches black.  4096 is
	 the original setting, beyond any line of sight -- L+R+Z cycles it. */
      drawStringf(-158,-90,1,"tile:%d sw:%d flat:%d/%d fog:%d",used[0],nmSwaps[0],
		  picLastSmall,picLastFull,fogDist);
      if (mpPlayers==1)
	 drawStringf(-158,-110,1,"B:%d L:%d",mpBudget,loadFields);
      /* LEGEND  W : the VDP2 weapon sheet cut into VDP1 tiles (PIC.C picWeaponSprites) --
		     <times asked>,<tiles produced>,<LWRAM free in KB when asked>,<sub-tiles
		     the gun put down in the last image>.  The split sky waits on the cut
		     (MPSKY.C skyAllowed), so 0 tiles means no sky either.
		 S : the split sky is built and owns RBG0, NBG0 and NBG1.
		 last: the colour the light ramp bottoms out at, r.g.b in 0..15 (UTIL.C
		     setFogColour) -- 0.0.0 is the black fog the engine shipped with.
	 Its own row -- every other line here can run the width of the screen -- and that row
	 is PowerSlave's top one (240 lines, CFG_YCENTER 120); on Doom's 224 it falls off the
	 top, which is where this probe is not needed. */
      drawStringf(-158,-118,1,"W:%d,%d,%d,%d S:%d last:%d.%d.%d P:%d,%d",
		  weaponSpriteTry,weaponSpriteTiles,weaponSpriteKb,weaponSpriteShown,
		  mpSkyOn,fogColour[0],fogColour[1],fogColour[2],
		  menuStreamPics,menuStreamW);
#endif
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
	 pic_flush();
	 SCL_DisplayFrame();
	 crashDisarm();
	 wallsPipeDiscard();
	 if (CFG_LEVEL_END_SHOWN(hitTeleport))
	    {stopAllLoopedSounds();     /* GCC14: the game's end-of-level screen, the level still */
	     enablePlax(0);             /* loaded -- over a black screen, lifts and sky gone     */
	     dontDisplayVDP2Pic();
	     CFG_LEVEL_END(hitTeleport);
	    }
	 return hitTeleport?hitTeleport:3;
	}
     /* used to be here */
     EZ_closeCommand();
     SPR_WaitDrawEnd();
     lastDraw=htimer-lastCalc;
     /* GCC14: the wall tiles this image took from the last one go in now the VDP1 is done with it
	(PIC.H) -- before the display, and before the gap's kick */
     CFG_PROF("Tile Flush"); pic_flush(); CFG_PROF_END();
     mpBalance();

#if WALLPIPE
     /* GCC14: THE GAP.  VDP1 drawing is done and the master only waits for VBlank:
	at 30 fps it computed 222 lines out of 525, ~300 are idle.  Camera and geometry
	are frozen since Motion and Post, and next frame's drawWalls reads them as is,
	so a traversal started here is identical to the bit.  After this point only the
	menu, the travel question and the two exits can move the camera: each calls
	wallsPipeDiscard().  Not while the earthquake is active -- its jitter is drawn
	at the top of the next frame. */
     if (!earthQuake)
	{MTH_PushMatrix(&viewTransform);
	 MTH_RotateMatrixZ(&viewTransform, playerAngle.roll );
	 MTH_RotateMatrixX(&viewTransform, playerAngle.pitch );
	 MTH_RotateMatrixY(&viewTransform, playerAngle.yaw );
	 MTH_MoveMatrix(&viewTransform,
			-camera->pos.x,
			-camera->pos.y+playerHeightOffset+CFG_VIEW_BOB,
			-camera->pos.z);
#ifdef GP_GAME_DOOM
	 /* the bodies as view 0 will draw them (mpShowBodies, drawWalls' own call gives the same):
	    the slave picks their leaves now (WALLS.C doom_spriteLeaves), and the last view left
	    them turned to its viewer, its own body hidden */
	 if (mpPlayers>1)
	    mpShowBodies(0);
#endif
	 wallsPipeKick(viewTransform.current);
	 MTH_PopMatrix(&viewTransform);
	}
#endif

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
     monsterMoveCounter+=CFG_TIC_ADD(vtimer);
     framesElapsed=vtimer;
     inputEnd=inputQHead;
     vtimer=0;
     ENABLE;

     if (mpSkyOn)
	{int k,f=MPFOGMAX;             /* the split sky takes the densest view's fog */
	 for (k=0;k<mpPlayers;k++)
	    if (mpFog[k]<f)
	       f=mpFog[k];
	 mpSkyFrame(f<fogCap? f: fogCap,colorOffset,framesElapsed);
	}
     else
	{movePlax(lastYaw,lastPitch);
	 if (currentState.currentLevel==18)
	    {plaxBBxmin=-160;
	     plaxBBymin=-110;
	     plaxBBxmax=160;
	     plaxBBymax=90;
	    }
	 /* GCC14: CFG_SKY_FULLWINDOW -- the sky's window over the WHOLE 3D view, whenever any sky
	    wall was seen at all.  The window used to be the BOX of the sky walls, and on Doom that
	    box does not cover everything that shows sky: the sky came out cut along a line that
	    moved with the view (proved on screen, 2026-09-23).  Outside the window RBG0 shows
	    nothing and the back screen is black, hence the band.  An empty box still closes the
	    window, or RBG0 would show through every crack of a level with no sky in it. */
	 else if (CFG_SKY_FULLWINDOW && plaxBBxmin<=plaxBBxmax && plaxBBymin<=plaxBBymax)
	    {plaxBBxmin=-160;
	     plaxBBymin=CFG_YMIN;
	     plaxBBxmax=160;
	     plaxBBymax=CFG_YMAX;
	    }
	 SCL_SetWindow(SCL_W1,0,SCL_RBG0,0xfffffff,
		       plaxBBxmin+160,plaxBBymin+CFG_YCENTER,
		       plaxBBxmax+160,plaxBBymax+CFG_YCENTER);
	 updateVDP2Pic();
	}
     lastYaw=playerAngle.yaw; lastPitch=playerAngle.pitch;

     if (playerIsDead && colorOffset[0]==-255)
	{wallsPipeDiscard();
	 crashDisarm();
	 return 1;
	}

     enablePlax(mpPlayers==1 || mpSkyOn);   /* split screen: the sky of no view (MPSKY.C) */
     if (CFG_CAMEL && hitCamel)
	{/* the travel question hands control elsewhere, or later: what the
	    slave is traversing will be worthless */
	 wallsPipeDiscard();
	 enablePlax(0);
	 crashDisarm();              /* a question waits for the player: the next beat re-arms */
	 if (runTravelQuestion(getText(LB_LEVELNAMES,hitCamel-100)))
	    return hitCamel;
	 stunCounter=10;
	 hitCamel=0;
	 vtimer=smoothVTime;
	}
     if (playerMotionEnable &&
	 (!(lastInputSample & PER_DGT_S) || !controlerPresent || delayed_fade || CFG_PAUSE_ASKED()))
	{/* the menu can move the camera */
	 wallsPipeDiscard();
	 CFG_PAUSE_PREP();
	 crashDisarm();              /* the menu waits for the player: the next beat re-arms */
	 /* GCC14: PowerSlave's inventory, or the game's own pause (SPRITE.H) */
	 CFG_PAUSE_MENU(currentState.inventory,keyMask,&mapOn,
			delayed_fade,delayed_fadeButton,delayed_fadeSel);
	 delayed_fade=0;
	 vtimer=smoothVTime;
	}

     if (quitRequest)
	{wallsPipeDiscard();
	 crashDisarm();
	 return CFG_QUIT_ACTION(quitRequest);
	}
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
 BOOT_PROBE2(0x7c1f);	/* GCC14: magenta = MAIN.BIN reached, see docs/PORTING_NOTES.md */
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
 SCL_SetDisplayMode(SCL_NON_INTER,CFG_SCL_LINES,SCL_NORMAL_A);
 SPR_SetEraseData(RGB(0,0,0),0,0,319,239);
 displayEnable(0);
 setVDP2();
 SetVblank();
 crashInstall();        /* GCC14: freeze and crash report (CRASH.C), after the VBlank handlers */

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
  dlg_init(fd); /* GCC14: no longer keeps the set -- it tables the sizes (MENU.C) */
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
 SPR_SetTvMode(SPR_TV_NORMAL,CFG_TV_SIZE,OFF);

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
 BOOT_PROBE2(0x001f);	/* GCC14: red = backup RAM read, about to enter the title screen */

 intro:
#ifndef TESTCODE
 abcResetEnable=1;
 playIntro();
 CFG_TITLE_END();               /* GCC14: a game's title lets go (Doom: its fire burns on into the load) */
 mpNewGame();                   /* the players armed at the menu, none carrying anything */
#else
 bup_initCurrentGame();
 currentState.inventory=0x00ffff;
#endif
 abcResetEnable=0;
 dPrint("1\n");
 SCL_Vdp2Init();
 dPrint("2\n");
 displayEnable(0);
 SCL_SetDisplayMode(SCL_NON_INTER,CFG_SCL_LINES,SCL_NORMAL_A);
 setVDP2();
 dPrint("3\n");

#ifndef TESTCODE
 /* GCC14: where a game begins when the title hands over (CFG_START_LEVEL, SPRITE.H).  Doom goes
    straight to the map its menu chose; PowerSlave keeps its map screen, unless a multiplayer
    game was armed (PSMULTI.C ps_startLevel). */
 level=CFG_START_LEVEL();
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
     levelFile=CFG_LEVEL_NAME(level);

     action=runLevel(levelFile,level);
     soundNmEars=0;                 /* GCC14: the title and the map hear as one player again */

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
	    level=CFG_RUN_MAP(currentState.currentLevel);
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
	 case 1: /* restart level: the load shows neither logo nor fire (DOOM_TITLE.C) */
	    loadAfterDeath=1;
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

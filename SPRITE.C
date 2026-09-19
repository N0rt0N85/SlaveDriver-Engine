#include <machine.h>
#include "sega_mth.h"
#include "level.h"
#include "sprite.h"
#include "util.h"
#include "sequence.h"
#include "sruins.h"
#include "profile.h"
#include "sound.h"
#include "ai.h"
#include "mplayer.h"

extern Sprite *camera;

Sprite sprites[MAXNMSPRITES];

/* GCC14: sprites the pool could not give, plus every sector index refused below.  Kept in NDEBUG:
   sectorSpriteList[sector] with a sector out of range is a write tens of kilobytes past the
   array, and the asserts that used to catch it are compiled out of the disc builds. */
int spritesRefused;

Sprite *freeList;

int spritesFree(void)
{Sprite *o;
 int n=0;
 for (o=freeList;o;o=o->next)
    n++;
 return n;
}

Sprite *sectorSpriteList[MAXNMSECTORS];

Fixed32 closestSectorBoundry;
int newSector;

void initSpriteSystem(void)
{int i;
 /* initialize free list */
 freeList=sprites;
 for (i=0;i<MAXNMSPRITES-1;i++)
    sprites[i].next=&(sprites[i+1]);
 sprites[i].next=NULL;

 for (i=0;i<MAXNMSECTORS;i++)
    sectorSpriteList[i]=NULL;

 {int s,w;
  MthXyz v;
  for (w=0;w<level_nmWalls;w++)
     {if (level_wall[w].normal[1]>=((int)(0.75*65536.0)) ||
	  level_wall[w].normal[1]<0)
	 level_wall[w].flags|=WALLFLAG_COLLIDEASFLOOR;
     }
  for (s=0;s<level_nmSectors;s++)
     for (w=level_sector[s].firstWall;w<=level_sector[s].lastWall;w++)
	if (level_wall[w].normal[1]<0 &&
	    level_wall[w].flags & WALLFLAG_COLLIDEASFLOOR)
	   {/* check to see if wall touches floor at any point */
	    if (level_wall[w].normal[1]==F(-1))
	       /* prevent door cielings from being included */
	       continue;

	    for (i=0;i<4;i++)
	       {getVertex(level_wall[w].v[i],&v);
		if (abs(findFloorDistance(s,&v))<F(1))
		   level_wall[w].flags&=~WALLFLAG_COLLIDEASFLOOR;
	       }
	   }
 }
}

void shiftSprites(void)
{int s;
 Sprite *o;
 for (s=0;s<MAXNMSECTORS;s++)
    for (o=sectorSpriteList[s];o;o=o->next)
       o->pos.y+=o->radius;
}

Sprite *newSprite(int sector,Fixed32 radius,Fixed32 friction,Fixed32 gravity,
		  int sequence,int flags,Object *owner)
{Sprite *o;
 /* assert(freeList); */
 assert(sector>=0);
 assert(sector<level_nmSectors);
 if (sector<0 || sector>=level_nmSectors)
    {spritesRefused++;          /* GCC14: kept in NDEBUG, see spritesRefused */
     return NULL;
    }
 if (!freeList)
    {spritesRefused++;
     return NULL;
    }
 o=freeList;
 freeList=freeList->next;
 o->vel.x=0; o->vel.y=0; o->vel.z=0;
 o->radius=radius; o->radius2=MTH_Mul(o->radius,o->radius);
 o->s=sector;
 o->angle=0;
 o->owner=owner;
 o->friction=friction;
 o->gravity=gravity;
 o->sequence=sequence;
 o->scale=48000;
 o->floorSector=-1;
 o->frame=0;
 o->next=sectorSpriteList[sector];
 sectorSpriteList[sector]=o;
 o->pos.x=0; o->pos.y=0; o->pos.z=0;
 o->flags=flags|SPRITEFLAG_BBLOCKED;
 if (level_sector[sector].flags & SECFLAG_WATER)
    o->flags|=SPRITEFLAG_UNDERWATER;

 o->pos.x=F(level_sector[sector].center[0]);
 o->pos.y=F(level_sector[sector].center[1]);
 o->pos.z=F(level_sector[sector].center[2]);

 return o;
}

void freeSprite(Sprite *o)
{Sprite *prev;
 prev=sectorSpriteList[o->s];
 if (prev==o)
    sectorSpriteList[o->s]=o->next;
 else
    {for (;prev->next!=o;prev=prev->next)
	{assert(prev->next);
	}
     assert(prev->next==o);
     prev->next=o->next;
    }
 o->next=freeList;
 freeList=o;
}

static sWallType *behindWall;
int bumpWall(sWallType *wall,Sprite *o,int sector)
{/* find signed distance from player to wall */
 Fixed32 planeDist,crossDist,yDist;
 Fixed32 pushDistance,velPush;
 MthXyz wallP;
 MthXyz collidePoint;

 getVertex(wall->v[0],&wallP);

#if 1
 planeDist=
    (f(o->pos.x-wallP.x))*wall->normal[0]+
    (f(o->pos.y-wallP.y))*wall->normal[1]+
    (f(o->pos.z-wallP.z))*wall->normal[2]-SPR_SUPPORT(o,wall);
#else
 planeDist=
    MTH_Mul(o->pos.x-wallP.x,wall->normal[0])+
    MTH_Mul(o->pos.y-wallP.y,wall->normal[1])+
    MTH_Mul(o->pos.z-wallP.z,wall->normal[2]);
#endif

 if (planeDist-o->radius>0/* || planeDist<-o->radius*/)
    {return 0;
    }

 if (wall->nextSector!=-1 && planeDist>o->radius-F(1))
    /* hack to avoid elevators acting like steps */
    return 0;

#if 0
 if (level_vertex[wall->v[0]].y<level_vertex[wall->v[3]].y)
    /* ignore the inside-out walls created when doors and elevators move */
    /* not sure if this is nescessary */
    /* caused problems with some sloped floors that were steep enough to
       be considered walls */
    return 0;
#endif

 if (planeDist<F(15) && wall->nextSector!=-1 &&
     (level_sector[wall->nextSector].flags & SECFLAG_WATER))
    o->flags|=SPRITEFLAG_UNDERWATER;

 if (wall->normal[1]==0)
    {/* its a wall */
     int edge=0;
     Fixed32 crossCoord;
     Fixed32 realDist;
     Fixed32 len;
     Fixed32 wallTop;
     Fixed32 wallBottom;

     if (planeDist<F(-1) && wall->nextSector==-1)
	behindWall=wall;

     crossCoord=
	(-f(o->pos.x-wallP.x))*wall->normal[2]+
	   (f(o->pos.z-wallP.z))*wall->normal[0];
     if (crossCoord>=F(wall->pixelLength))
	{crossDist=crossCoord-F(wall->pixelLength);
         wallTop=F(level_vertex[wall->v[1]].y);
         wallBottom=F(level_vertex[wall->v[2]].y);
         edge=1;
	}
     else
	if (crossCoord<0)
	   {crossDist=crossCoord;
            wallTop=F(level_vertex[wall->v[0]].y);
            wallBottom=F(level_vertex[wall->v[3]].y);
	    edge=1;
	   }
	else
           {crossDist=0;
            wallTop=F(level_vertex[wall->v[0]].y)+
	       (level_vertex[wall->v[1]].y-
		level_vertex[wall->v[0]].y)*(crossCoord
					     /wall->pixelLength);
            wallBottom=F(level_vertex[wall->v[3]].y)+
	       (level_vertex[wall->v[2]].y-level_vertex[wall->v[3]].y)*
		  (crossCoord/wall->pixelLength);
           }
     if (SPR_YLO(o)>wallTop)
        {yDist=SPR_YOUT(o,wallTop-o->pos.y);
	 edge=1;
	}
     else
        {if (SPR_YHI(o)<wallBottom)
            {yDist=SPR_YOUT(o,wallBottom-o->pos.y);
	     edge=1;
	    }
	else
	   yDist=0;
	}

     if (edge)
	{realDist=f(planeDist)*f(planeDist)+f(crossDist)*f(crossDist)+
	    f(yDist)*f(yDist);
	 assert(realDist>=0);
         if (wall->nextSector!=-1 && planeDist<0 && sector==o->s &&
	     !((wall->flags & o->flags) & WALLFLAG_BLOCKBITS))
	    {if (closestSectorBoundry<F(-2000))
		{closestSectorBoundry=F(-2000);
		 newSector=wall->nextSector;
		}
	    }
	 if (realDist>f(o->radius2))
	    return 0;
	 /* otherwise we were bumped by the edge */
	 if (!((wall->flags & o->flags) & WALLFLAG_BLOCKBITS))
	    return 1;

         /* ... find the point we collided with */
	 collidePoint.x=o->pos.x+f(crossDist)*wall->normal[2]-
	    f(planeDist)*wall->normal[0];
	 collidePoint.z=o->pos.z-f(crossDist)*wall->normal[0]-
	    f(planeDist)*wall->normal[2];
	 collidePoint.y=o->pos.y+yDist;
	 /* ... find the vector from the point to us */

	 /* collidePoint <- collideVector */
	 collidePoint.x=o->pos.x-collidePoint.x;
	 collidePoint.y=o->pos.y-collidePoint.y;
	 collidePoint.z=o->pos.z-collidePoint.z;

	 /* ... normalize the vector */
	 len=f(collidePoint.x)*collidePoint.x+
	     f(collidePoint.y)*collidePoint.y+
	     f(collidePoint.z)*collidePoint.z;
	 len=fixSqrt(len,16);
	 collidePoint.x/=f(len);
	 collidePoint.y/=f(len);
	 collidePoint.z/=f(len);

         pushDistance=o->radius-len;
         o->pos.x+=MTH_Mul(collidePoint.x,pushDistance);
         o->pos.y+=MTH_Mul(collidePoint.y,pushDistance);
         o->pos.z+=MTH_Mul(collidePoint.z,pushDistance);

	 /* cancel velocity in direction of normal */
	 pushDistance=MTH_Product((Fixed32 *)&(o->vel),
				  (Fixed32 *)&collidePoint);
	 if (pushDistance>0)
	    return 0;
	 if (o->flags & SPRITEFLAG_BOUNCY)
	    velPush=velPush<<1;
	 o->vel.x-=MTH_Mul(collidePoint.x,pushDistance);
	 o->vel.y-=MTH_Mul(collidePoint.y,pushDistance);
	 o->vel.z-=MTH_Mul(collidePoint.z,pushDistance);
	 return 1;
	}
    }

 /* we collided */
 assert(closestSectorBoundry<=0);
 if (!((wall->flags & o->flags) & WALLFLAG_BLOCKBITS))
    {if (/* wall->nextSector!=o->s &&  turned out not to be nescessary */
	 /*newSector==sector &&*//* kind of a hack, to prevent water problem */
	 /* caused problems under bridges for unknown reason */
	 planeDist<0 &&
	 planeDist>closestSectorBoundry)
	{newSector=wall->nextSector;
	 closestSectorBoundry=planeDist;
	}
     /* if (!(wall->flags & WALLFLAG_DOORWALL) ||
	 level_vertex[wall->v[1]].y-level_vertex[wall->v[2]].y>40*2)	 */
	/* this is to prevent sneeking under doors when they are part-way
	   open */
	return 1;
    }

 velPush=MTH_Product((Fixed32 *)&(o->vel),(Fixed32 *)wall->normal);
 /*if (velPush>0)
   return 0;*/
 /* this was bad for when sprites pushed you thru the walls */
 pushDistance=o->radius-planeDist;
 o->pos.x+=MTH_Mul(wall->normal[0],pushDistance);
 o->pos.y+=MTH_Mul(wall->normal[1],pushDistance);
 o->pos.z+=MTH_Mul(wall->normal[2],pushDistance);

 if (velPush<0)
    {/* cancel velocity in direction of normal */
     if (o->flags & SPRITEFLAG_BOUNCY)
	velPush=velPush<<1;
     o->vel.x-=MTH_Mul(wall->normal[0],velPush);
     o->vel.y-=MTH_Mul(wall->normal[1],velPush);
     o->vel.z-=MTH_Mul(wall->normal[2],velPush);
     if (o==camera)
	{if (wall->flags & WALLFLAG_LAVA)
	    {playerLongHurt(1);
	    }
	 if (wall->flags & WALLFLAG_SWAMP)
	    {playerLongHurt(0);
	    }
	}
    }
#if 0
 if (!((wall->flags & o->flags) & WALLFLAG_BLOCKBITS))
    /* more stuff to fix problem with the fix to prevent sneeking under doors*/
   return 0;
#endif
 return 1;
}

short wallCollideNm;

sWallType *bestFloor,*bestCeil;
int bestFloorHeight;
int bestCeilingHeight;
int ourSectorFloorHeight;
int floorValid;
int ourSectorCeilingHeight;
int ceilValid;
int floorSector;
MthXyz *floorNormal; /* or null if best floor is not a slope */

static int STEPHEIGHT;

void bumpFloor(sWallType *floor,Sprite *o,int sector)
{Fixed32 floorHeight;
 if (floor->normal[1]==F(1) ||
     floor->normal[1]==F(-1))
    floorHeight=F(level_vertex[floor->v[0]].y);
 else
    {Fixed32 planeDist;
     MthXyz wallP;
     getVertex(floor->v[0],&wallP);
     planeDist=
	(f(o->pos.x-wallP.x))*floor->normal[0]+
	(f(o->pos.z-wallP.z))*floor->normal[2];
     floorHeight=wallP.y-MTH_Div(planeDist,floor->normal[1]);
    }

 if (floor->normal[1]>0)
    {/* floor */
     if (floorValid==2)
	return;
     if (!floorValid)
	{ourSectorFloorHeight=floorHeight; /* we collide with our sector's
					      floor first */
	 bestFloorHeight=floorHeight;
	 floorSector=sector;
	 bestFloor=floor;
	 if (floor->normal[1]!=F(1)) /* if our sector's floor is a slope then
					collide only with it */
	    {floorValid=2;
	     floorNormal=(MthXyz *)floor->normal;
	    }
	 else
	    floorValid=1;
	}
     if (floorHeight>ourSectorFloorHeight+STEPHEIGHT)
	return;
     if (floorHeight>o->pos.y+STEPHEIGHT)
	return;
     if (floor->normal[1]!=F(1))
	return;
     if (floorHeight<=bestFloorHeight)
	return;
     bestFloorHeight=floorHeight;
     bestFloor=floor;
     floorSector=sector;
    }
 else
    {/* ceiling */
     if (ceilValid==2)
	return;
     if (!ceilValid)
	{ourSectorCeilingHeight=floorHeight;
	 bestCeilingHeight=floorHeight;
	 bestCeil=floor;
	 if (floor->normal[1]!=F(-1))
	    ceilValid=2;
	 else
	    ceilValid=1;
	}
     if (floorHeight<ourSectorCeilingHeight-STEPHEIGHT)
	return;
     if (floor->normal[1]!=F(-1))
	return;
     if (floorHeight>=bestCeilingHeight)
	return;
     bestCeilingHeight=floorHeight;
     bestCeil=floor;
    }
}

/* if a sector is in the penetrate list then it means we are penetrating
   that sector.  */
void bumpSectorBoundries(int s,Sprite *o,short *penetrate,int *nmPenetrate)
{sSectorType *sec=level_sector+s;
 int w,i;
 CFG_PROF_SUB("SectorBndry");
 for (w=sec->firstWall;w<=sec->lastWall;w++)
    {if (level_wall[w].nextSector==-1)
	break;
     if ((level_wall[w].flags & o->flags) & WALLFLAG_BLOCKBITS)
	continue;
     if (!bumpWall(level_wall+w,o,s))
	continue;
     /* see if sector on other side is already in list */
     for (i=0;i<*nmPenetrate;i++)
	if (penetrate[i]==level_wall[w].nextSector)
	   break;
     if (i<*nmPenetrate)
	continue;
     /* otherwise add to penetrate list */
     penetrate[(*nmPenetrate)++]=level_wall[w].nextSector;
    }
 CFG_PROF_SUB_END();
}

void bumpWalls(int s,Sprite *o)
{sSectorType *sec=level_sector+s;
 int w;
 CFG_PROF_SUB("Walls");
 for (w=sec->firstWall;w<=sec->lastWall;w++)
    {if (!((level_wall[w].flags & o->flags) & WALLFLAG_BLOCKBITS))
	continue;
     /*     if (abs(level_wall[w].normal[1])<((int)(0.75*65536.0)))*/
     if (level_wall[w].flags & WALLFLAG_COLLIDEASFLOOR)
	bumpFloor(&level_wall[w],o,s);
     else
	if (bumpWall(&level_wall[w],o,s))
	   wallCollideNm=w;
#if 0
     if (level_wall[w].normal[1]<((int)(0.75*65536.0)) &&
	 level_wall[w].normal[1]>=0) /* cause all ceilings to collide as
					non-walls */
	{
	 if (bumpWall(&level_wall[w],o,s))
	    wallCollideNm=w;
	}
     else
	bumpFloor(&level_wall[w],o,s);
#endif
    }
 CFG_PROF_SUB_END();
}

#ifndef PAL
void internal_moveSprite(Sprite *o)
{if (!(o->flags & SPRITEFLAG_UNDERWATER))
    o->vel.y-=o->gravity;
 else
    o->vel.y-=o->gravity>>1;
 o->pos.x+=o->vel.x;
 o->pos.z+=o->vel.z;
 o->pos.y+=o->vel.y;
}
#else
void internal_moveSprite(Sprite *o)
{if (!(o->flags & SPRITEFLAG_UNDERWATER))
    o->vel.y-=MTH_Mul(o->gravity,72643);
 else
    o->vel.y-=MTH_Mul(o->gravity>>1,72643);
 o->pos.x+=MTH_Mul(o->vel.x,78643);
 o->pos.z+=MTH_Mul(o->vel.z,78643);
 o->pos.y+=MTH_Mul(o->vel.y,78643);
}
#endif

inline void doFriction(Sprite *o)
{if (o->friction!=F(1) && !(o->flags & SPRITEFLAG_ONSLIPPERYSLOPE))
    {o->vel.x=MTH_Mul(o->vel.x,o->friction);
     if (o->flags & SPRITEFLAG_UNDERWATER)
	o->vel.y=MTH_Mul(o->vel.y,o->friction);
     o->vel.z=MTH_Mul(o->vel.z,o->friction);
    }
}

short spriteCollideNm;
void collideSpriteSprite(Sprite *mobile,Sprite *stat)
{Fixed32 distance2,pushDistance;
 Fixed32 len;
 MthXyz dp;
 if (stat->flags & SPRITEFLAG_NOSPRCOLLISION)
    return;
 dp.x=mobile->pos.x-stat->pos.x;
 if (abs(dp.x)>mobile->radius+stat->radius)
    return;
 dp.y=SPR_DY(mobile,stat);
 if (abs(dp.y)>mobile->radius+stat->radius)
    return;
 dp.z=mobile->pos.z-stat->pos.z;
 if (abs(dp.z)>mobile->radius+stat->radius)
    return;
 distance2=dp.x*f(dp.x)+dp.y*f(dp.y)+dp.z*f(dp.z);
 /* assert(distance2>=0); */
 if (distance2<1000)
    return;
 if (distance2>(mobile->radius+stat->radius)*f(mobile->radius+stat->radius))
    return;

 len=fixSqrt(distance2,16);
 dp.x/=f(len);
 dp.y/=f(len);
 dp.z/=f(len);

 spriteCollideNm=stat-sprites;

 if (mobile->flags & SPRITEFLAG_IMMOBILE)
    return;

 pushDistance=mobile->radius+stat->radius-len+F(1);
 mobile->pos.x+=MTH_Mul(dp.x,pushDistance);
 mobile->pos.y+=MTH_Mul(dp.y,pushDistance);
 mobile->pos.z+=MTH_Mul(dp.z,pushDistance);

#if 1
	 /* cancel velocity in direction of normal */
 pushDistance=MTH_Product((Fixed32 *)&(mobile->vel),
			  (Fixed32 *)&dp);
 if (pushDistance>0)
    return;
 mobile->vel.x-=MTH_Mul(dp.x,pushDistance);
 mobile->vel.y-=MTH_Mul(dp.y,pushDistance);
 mobile->vel.z-=MTH_Mul(dp.z,pushDistance);
#endif

}


void splash(Sprite *o)
{int s,i,vol,pan;
 MthXyz vel,pos;
 if (o->radius<=F(1))
    return;
 if (o==camera && o->vel.y<F(-7))
    {/* make bubbles */
     pos=o->pos;
     pos.y-=o->radius+F(32);
     s=findSectorContaining(&pos,o->s);
     for (i=0;i<40;i++)
	{vel.x=(MTH_GetRand()&0x7ffff)-F(4);
	 vel.z=(MTH_GetRand()&0x7ffff)-F(4);
	 vel.y=o->vel.y-(MTH_GetRand()&0x3ffff);
	 constructOneBubble(s,&pos,&vel);
	}
    }
 constructOneShot(o->s,o->pos.x,o->pos.y,o->pos.z,OT_SPLASH,65000,0,0);

 posGetSoundParams(&(o->pos),&vol,&pan);
 if (f(o->radius)<48)
    vol+=48-f(o->radius);
 if (vol>255)
    return;
 playSoundE(0,level_staticSoundMap[ST_JOHN]+2,vol,pan);
}


int collideSprite(Sprite *o)
{int i;
 int wasUnderWater;
 int retVal;
 short sectorPenetrate[25];
 int nmPenetrate;
 int floorCollideNm,ceilCollideNm;

 pushProfile("Collide Sprite");
 closestSectorBoundry=F(-3000);
 newSector=o->s;
 floorValid=0;
 ceilValid=0;
 floorNormal=NULL;
 bestFloor=NULL;
 bestCeil=NULL;
 wallCollideNm=-1;
 floorCollideNm=-1;
 ceilCollideNm=-1;
 sectorPenetrate[0]=o->s;
 nmPenetrate=1;

 wasUnderWater=o->flags & SPRITEFLAG_UNDERWATER;
 if (wasUnderWater)
    {o->flags&=~(SPRITEFLAG_UNDERWATER);
     STEPHEIGHT=F(1);
    }
 else
    STEPHEIGHT=SPR_STEP(o);

 behindWall=NULL;
 /* bump the walls in our sector to prevent spurious sector transitions */
 if (!(o->flags & SPRITEFLAG_IMMOBILE))
    bumpWalls(o->s,o);

 /* figure out which sectors we penetrate */
 for (i=0;i<nmPenetrate;i++)
    /* nmPenetrate changes inside this loop */
    {bumpSectorBoundries(sectorPenetrate[i],o,sectorPenetrate,&nmPenetrate);
     assert(nmPenetrate<25);
    }

 if (newSector==o->s && behindWall && wallCollideNm==-1)
    wallCollideNm=behindWall-level_wall;

 CFG_PROF_SUB("Sprite");
 /* check for collision with other sprites */
 spriteCollideNm=-1;
 for (i=0;i<nmPenetrate;i++)
    {/* check for collision with sprites in that sector */
     Sprite *s;
     for (s=sectorSpriteList[sectorPenetrate[i]];s;s=s->next)
	{if (s==o)
	    continue;
	 collideSpriteSprite(o,s);
	}
    }
 CFG_PROF_SUB_END();

 if (o->flags & SPRITEFLAG_IMMOBILE)
    goto skipWallCollision;

 /* collide with the walls in that sector */

 /* if we aren't colliding with any sprites then we don't have to collide
    with our sector again */
 i=0;
 if (spriteCollideNm==-1)
    i++;
 for (;i<nmPenetrate;i++)
    bumpWalls(sectorPenetrate[i],o);

 if (floorValid)
    {int floorDistance=SPR_FOOT(o)+bestFloorHeight-o->pos.y;
     if (o==camera)
	floorDistance+=SPR_HOVER(o);
     if (floorDistance>0 ||
	 /* if we were on the floor last round, and our yvel is <=0 and
	    the best floor is the same as the one we were on last round
	    then put us on the floor.  (prevents bumping down slopes) */
	 (floorValid==2 && floorSector==o->floorSector && o->vel.y<=0))
	{if (o->vel.y<F(-1)  /* if we hit the floor really fast */
	     || floorDistance<F(1) /* or we're not far below
				      the floor */
	     || bestFloor->normal[1]<60000 /* or we're on a steep slope */
	     || spriteCollideNm!=-1 /* or we collided with a sprite */)
	    o->pos.y+=floorDistance;
	 else
	    o->pos.y+=CFG_FLOOR_RISE(o,floorDistance);
	 floorCollideNm=bestFloor-level_wall;
	 assert(floorCollideNm>=0 && floorCollideNm<level_nmWalls);
	 if (0/*floorNormal*/)
	    {/* is a slippery floor */
	     Fixed32 velPush=
		MTH_Product((Fixed32 *)(&o->vel),(Fixed32 *)floorNormal);
	     if (velPush<0)
		{o->vel.x-=MTH_Mul(floorNormal->x,velPush);
	     o->vel.y-=MTH_Mul(floorNormal->y,velPush);
		 o->vel.z-=MTH_Mul(floorNormal->z,velPush);
		 if (o==camera)
		    playerDYChange(abs(velPush));
		}
	     o->flags|=SPRITEFLAG_ONSLIPPERYSLOPE;
	    }
	 else
	    {if (o->vel.y<0)
		{if (o==camera)
		    {playerDYChange(abs(o->vel.y));
		     if (bestFloor->flags & WALLFLAG_LAVA)
			{playerLongHurt(1);
			}
		     if (bestFloor->flags & WALLFLAG_SWAMP)
			{playerLongHurt(0);
			}
		    }
		 if (o->flags & SPRITEFLAG_BOUNCY)
		    o->vel.y=-o->vel.y;
		 else
		    o->vel.y=0;
		 if (o==camera && bestFloor->object &&
		     !(bestFloor->flags & WALLFLAG_WATERSURFACE))
		    signalObject((Object *)bestFloor->object,
				 SIGNAL_FLOORCONTACT,0,0);
		}
	     o->flags&=~SPRITEFLAG_ONSLIPPERYSLOPE;
	    }
	 o->floorSector=floorSector;
	}
     else
	o->floorSector=-1;
    }
 else
    o->floorSector=-1;

 if (ceilValid && o->pos.y>bestCeilingHeight-SPR_HEAD(o))
    {if (floorCollideNm!=-1)
	/* we're being squished between ceiling and floor */
	o->pos.y=SPR_SQUISH(o,bestCeilingHeight,bestFloorHeight);
     else
	o->pos.y=bestCeilingHeight-SPR_HEAD(o);
     ceilCollideNm=bestCeil-level_wall;
     if (o->vel.y>0)
	o->vel.y=0;
     if (o==camera && bestCeil->object)
	signalObject((Object *)bestCeil->object,SIGNAL_CEILCONTACT,0,0);
    }

 /* check if we are in a new sector */
 if (newSector!=o->s)
    {Sprite *prev=NULL,*t;
     /* remove o from current sector list */
     for (t=sectorSpriteList[o->s];o!=t;prev=t,t=t->next)
	{assert(t);
	}
     if (!prev)
	sectorSpriteList[o->s]=o->next;
     else
	prev->next=o->next;
     /* insert o into new sector list */
     o->next=sectorSpriteList[newSector];
     sectorSpriteList[newSector]=o;
     o->s=newSector;
     if (level_sector[newSector].object)
	{/* GCC14: any player entering, with ITS state loaded (MPLAYER.H) -- solo: the camera */
	 int k=(o==camera)? mpCur: mpIndexOfSprite(o);
	 if (k>=0)
	    {int prev=mpBegin(k);
	     signalObject((Object *)level_sector[newSector].object,
			  SIGNAL_ENTER,0,0);
	     mpEnd(prev);
	    }
	}
    }

 if (level_sector[o->s].flags & SECFLAG_WATER)
    o->flags|=SPRITEFLAG_UNDERWATER;

 if (!wasUnderWater && (o->flags & SPRITEFLAG_UNDERWATER))
    splash(o);

 skipWallCollision:
 retVal=0;

 if (floorCollideNm!=-1)
    {retVal=((retVal|COLLIDE_FLOOR)&0x0ffff0000)|floorCollideNm;
     assert(floorCollideNm<30000);
     assert(floorCollideNm>=0);
    }

 if (ceilCollideNm!=-1)
    retVal=((retVal|COLLIDE_CEILING)&0x0ffff0000)|ceilCollideNm;

 if (wallCollideNm!=-1)
    retVal=((retVal|COLLIDE_WALL)&0x0ffff0000)|wallCollideNm;

 if (spriteCollideNm!=-1)
    retVal=((retVal|COLLIDE_SPRITE)&0x0ffff0000)|spriteCollideNm;


#ifndef NDEBUG
 assert(spriteCollideNm==-1 ||
	(spriteCollideNm>=0 && spriteCollideNm<MAXNMSPRITES));

 if (retVal & COLLIDE_SPRITE)
    {assert(spriteCollideNm!=-1);
     assert((retVal & 0xffff)<500);
    }
#endif

 popProfile();
 return retVal;
}

int spriteAdvanceFrame(Sprite *o)
{int flags,frame;
 assert(o);
 assert(o->sequence!=-2);
 if (o->sequence<0)
    return 0;
 frame=level_sequence[o->sequence]+o->frame;
 if (level_frame[frame].sound!=-1)
    spriteMakeSound(o,level_frame[frame].sound);
 flags=level_frame[frame].flags;
 o->frame++;
 if (frame+1>=level_sequence[o->sequence+1])
    {o->frame=0;
     flags|=FRAMEFLAG_ENDOFSEQUENCE;
    }
 return flags;
}

void spriteRandomizeFrame(Sprite *o)
{int nmFrames;
 assert(o);
 nmFrames=level_sequence[o->sequence+1]-level_sequence[o->sequence];
 assert(nmFrames);
 if (nmFrames==1)
    {o->frame=0;
     return;
    }
 o->frame=getNextRand()%nmFrames;
}

static int pbMoved=0;
#if 0
void updatePushBlockPositions(void)
{int block,v,vt,fs;
 sPBType *pb;
 Sprite *o;
 if (!pbMoved)
    return;
 pbMoved=0;
 for (block=0;block<level_nmPushBlocks;block++)
    {pb=level_pushBlock+block;
     if (!pb->dx && !pb->dy && !pb->dz)
	continue;
     for (v=level_pushBlock[block].startVertex;
	  v<=level_pushBlock[block].endVertex;v++)
	{for (vt=level_PBVert[v].vStart;
	      vt<level_PBVert[v].vNm+level_PBVert[v].vStart;
	      vt++)
	    {assert(vt>=0 && vt<level_nmVertex);
	     level_vertex[vt].x+=pb->dx;
	     level_vertex[vt].y+=pb->dy;
	     level_vertex[vt].z+=pb->dz;
	    }
	}
     fs=level_pushBlock[block].floorSector;
     if (fs!=-1)
	{/* look thru sprites in floorSector, moving ones that are on
	    that floor */
	 for (o=sectorSpriteList[fs];
	      o;o=o->next)
	    {if (o->floorSector==fs && o!=camera)
		{o->pos.x+=F(pb->dx);
		 o->pos.y+=F(pb->dy);
		 o->pos.z+=F(pb->dz);
		}
	    }
	 /* do extra check for player */
	 if (camera->floorSector==fs)
	    {camera->pos.x+=F(pb->dx);
	     camera->pos.y+=F(pb->dy);
	     camera->pos.z+=F(pb->dz);
	    }
	}
     pb->dx=0; pb->dy=0; pb->dz=0;
    }
}
#else
void updatePushBlockPositions(void)
{int block,v,vt,fs;
 sPBType *pb;
 Sprite *o;

 if (!pbMoved)
    return;
 pbMoved=0;
 for (block=0;block<level_nmPushBlocks;block++)
    {pb=level_pushBlock+block;
     if (!pb->dy)
	continue;
     for (v=level_pushBlock[block].startVertex;
	  v<=level_pushBlock[block].endVertex;v++)
	{for (vt=level_PBVert[v].vStart;
	      vt<level_PBVert[v].vNm+level_PBVert[v].vStart;
	      vt++)
	    {assert(vt>=0 && vt<level_nmVertex);
	     level_vertex[vt].y+=pb->dy;
	    }
	}
     fs=level_pushBlock[block].floorSector;
     if (fs!=-1)
	{/* GCC14: EVERY leaf of the block, not just the one the field names.  A Doom sector
	   becomes several leaves and floorSector holds a single one (doom3d push_blocks: the
	   largest), so anything standing on one of the others stayed in mid-air -- E1M2's
	   chainsaw on its donut floor, whose four leaves carry four floor walls.  Walking the
	   block's FLOOR walls visits each leaf exactly once: a leaf has one floor. */
	 int k;
	 for (k=level_pushBlock[block].startWall;
	      k<=level_pushBlock[block].endWall;k++)
	    {int w=level_PBWall[k],s2;
	     if (level_wall[w].normal[1]<=0)
		continue;
	     s2=findWallsSector(w);
	     if (s2<0 || s2>=level_nmSectors)
		continue;
	     for (o=sectorSpriteList[s2];
		  o;o=o->next)
		{if (o->floorSector==s2 && o!=camera)
		    {o->pos.y+=F(pb->dy);
		    }
		}
	     /* do extra check for player */
	     if (camera->floorSector==s2)
		{camera->pos.y+=F(pb->dy);
		}
	    }
	}
     pb->dy=0;
    }
}
#endif

void movePushBlock(int block,int dx,int dy,int dz)
{assert(block>=0);
 assert(block<level_nmPushBlocks);
 pbMoved=1;
 level_pushBlock[block].dx+=dx;
 level_pushBlock[block].dy+=dy;
 level_pushBlock[block].dz+=dz;
}

char noClipCheat=0;     /* GCC14: test toggle, set by DOOM_PLAYER.C */

void moveCamera(void)
{doFriction(camera);
 internal_moveSprite(camera);
 if (noClipCheat)
    {/* GCC14: no collision; the sector follows portals only, so crossing a solid wall keeps
	the old sector (the room is drawn from outside, harmless).  floorSector stays valid so the
	player can still move; gravity is cut by the game. */
     int s=findSectorContaining(&camera->pos,camera->s);
     if (s!=camera->s)
	moveSpriteTo(camera,s,&camera->pos);
     camera->floorSector=camera->s;
     return;
    }
 collideSprite(camera);
}

int moveSprite(Sprite *sprite)
{if (!(sprite->flags & SPRITEFLAG_IMMOBILE))
    {doFriction(sprite);
     internal_moveSprite(sprite);
    }
 return collideSprite(sprite);
}

void moveSpriteTo(Sprite *o,int newSector,MthXyz *newPos)
{Sprite *t,*prev;
 assert(o);
 if (!o || newSector<0 || newSector>=level_nmSectors)
    {spritesRefused++;          /* GCC14: kept in NDEBUG, see spritesRefused */
     return;
    }
 prev=NULL;
 if (o->s!=newSector)
    {/* remove o from current sector list */
     assert(newSector<level_nmSectors);
     assert(newSector>=0);
     assert(sectorSpriteList[o->s]);
     for (t=sectorSpriteList[o->s];o!=t;prev=t,t=t->next)
	{assert(t);
	}
     if (!prev)
	sectorSpriteList[o->s]=o->next;
     else
	prev->next=o->next;
     /* insert o into new sector list */
     o->next=sectorSpriteList[newSector];
     sectorSpriteList[newSector]=o;
     o->s=newSector;
    }
 o->pos=*newPos;
}

int pointInSectorP(MthXyz *pos,int sector)
{int w;
 sSectorType *sec=level_sector+sector;
 sWallType *wall;
 MthXyz wallP;
 int planeDist;
 for (w=sec->firstWall;w<=sec->lastWall;w++)
    {wall=level_wall+w;
     getVertex(wall->v[0],&wallP);
     planeDist=
	(f(pos->x-wallP.x))*wall->normal[0]+
	(f(pos->y-wallP.y))*wall->normal[1]+
	(f(pos->z-wallP.z))*wall->normal[2];
     if (planeDist<F(-1)>>2)
	return 0;
    }
 return 1;
}

int findSectorContaining(MthXyz *pos,int guessSector)
{int w,i,s;
 sSectorType *sec;
 short penetrate[MAXNMSECTORS];
 int nmPenetrate;

 checkStack();
 penetrate[0]=guessSector;
 nmPenetrate=1;
 for (s=0;s<nmPenetrate;s++)
    /* nmPenetrate changes inside this loop */
    {if (pointInSectorP(pos,penetrate[s]))
	return penetrate[s];
     if (s>10)
	{/* assert(0); Yuill was here*/
	 return guessSector;
	}
     sec=level_sector+penetrate[s];
     for (w=sec->firstWall;w<=sec->lastWall;w++)
	{if (level_wall[w].nextSector!=-1)
	    {/* see if sector on other side is already in list */
	     for (i=0;i<nmPenetrate;i++)
		if (penetrate[i]==level_wall[w].nextSector)
		   break;
	     if (i<nmPenetrate)
		continue;
	     /* otherwise add to penetrate list */
	     penetrate[nmPenetrate++]=level_wall[w].nextSector;
	    }
	}
    }
 dPrint("Doh! %d\n",nmPenetrate);
 return guessSector;
}

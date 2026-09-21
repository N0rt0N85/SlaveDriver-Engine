#include <sega_mth.h>
#include <sega_scl.h>
#include <limits.h>

#include "slevel.h"
#include "level.h"
#include "sprite.h"
#include "ai.h"
#include "util.h"
#include "sequence.h"
#include "hitscan.h"
#include "sound.h"
#include "sruins.h"
#include "route.h"
#include "walls.h"
#include "pic.h"
#include "file.h"
#include "gamestat.h"
#include "bigmap.h"
#include "weapon.h"
#include "local.h"

#include "aicommon.h"
#include "mplayer.h"

static int kilmaatPuzzleNumber=0;

/********************************\
 *          PLAYER STUFF        *
\********************************/

void player_func(Object *o,int message,int param1,int param2)
{switch (message)
    {case SIGNAL_HURT:
	{/* GCC14: the player hit, whoever is loaded (MPLAYER.H) -- solo: player 1 */
	 int k=mpIndexOfObject(o);
	 int prev=mpBegin(k>=0? k: mpCur);
	 CFG_PLAYER_HURT(param1,param2);
	 mpEnd(prev);
	}
	break;
       }
}

unsigned short playerSeqList[]={-1};
/* GCC14: NULL when the level's objects filled the pool, or its sprites did -- the caller decides
   what to do without a body (SRUINS.C mpBuild drops that player).  A refused player still reads
   its params: the stream is positional (OBJECT.C objectPPos). */
PlayerObject *constructPlayer(int sector,int suckParams)
{PlayerObject *this=(PlayerObject *)getFreeObject(player_func,OT_PLAYER,
						  CLASS_MONSTER);
 Sprite *s;
 assert(sizeof(*this)<sizeof(Object));
 if (!this)
    {if (suckParams)
	suckSpriteParams(NULL);            /* its 4 remaining params: the caller took the sector */
     return NULL;
    }
 moveObject((Object *)this,objectIdleList);
 dPrint("player in sector %d\n",sector);
 s=newSprite(sector,PLAYER_RADIUS,0.90*65536.0,GRAVITY,
		     -1,SPRITEFLAG_BSHORT,
		     (Object *)this);
 if (!s)
    {this->type=OT_DEAD;                    /* give the object back: the level has no sprite left */
     moveObject((Object *)this,objectFreeList);
     if (suckParams)
	suckSpriteParams(NULL);            /* its 4 remaining params: the caller took the sector */
     return NULL;
    }
 this->sprite=s; CFG_PLAYER_INIT(this->sprite);
 if (suckParams)
    {suckSpriteParams(this->sprite);
     this->sprite->angle=normalizeAngle(this->sprite->angle-F(90));
     playerAngle.yaw=this->sprite->angle;
    }
 this->health=700;
 this->sequenceMap=playerSeqList;
 this->state=0;
 return this;
}


/********************************\
 *          GENPROJ STUFF       *
\********************************/
void genproj_func(Object *_this,int msg,int param1,int param2)
{GenericProjectileObject *this=(GenericProjectileObject *)_this;
 int collide,fflags;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    {if (this->light)
		removeLight(this->sprite);
	     freeSprite(this->sprite);
	    }
	 break;
	}
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	fflags=spriteAdvanceFrame(this->sprite);
	collide=moveSprite(this->sprite);
	/*if (this->armTime>0)
	  {this->armTime--;
	  break;
	  }*/
	if (!collide)
	   break;
	if ((collide & COLLIDE_SPRITE) &&
	    sprites[collide&0xffff].owner!=this->owner)
	   {assert(sprites[collide&0xffff].owner->class!=CLASS_DEAD);
	    signalObject(sprites[collide&0xffff].owner,
			 SIGNAL_HURT,this->damage,
			 (int)this);
	   }
	if (!(collide & COLLIDE_SPRITE) &&
	    this->type==OT_MAGBALL &&
	    (collide & (COLLIDE_WALL|COLLIDE_FLOOR|COLLIDE_CEILING)) &&
	    (level_wall[collide&0xffff].flags & WALLFLAG_EXPLODABLE))
	   explodeAllMaskedWallsInSector(findWallsSector(collide & 0xffff));
	if (collide & (COLLIDE_WALL|COLLIDE_FLOOR|COLLIDE_CEILING|
		       COLLIDE_SPRITE))
	   {if (this->light)
	       removeLight(this->sprite);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_POOF,F(1),this->explosionColor,0);
	    delayKill(_this);
	   }
	break;
       }
}

unsigned short genprojTurnSequenceMap[]={HB|0};
unsigned short genprojFlatSequenceMap[]={0};

Object *constructGenproj(int sector,MthXyz *pos,MthXyz *vel,
			 SpriteObject *owner,int heading,
			 int flat,int type,int damage,
			 int explosionColor,
			 char r,char g,char b,
			 int armTime)
{GenericProjectileObject *this=(GenericProjectileObject *)
    getFreeObject(genproj_func,type,CLASS_PROJECTILE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 assert(level_sequenceMap[type]>=0);
 assert(level_sequenceMap[type]<1000);
 this->sprite=newSprite(sector,F(8)/*F(16)*/,F(1),0,
			level_sequenceMap[type],
			SPRITEFLAG_IMATERIAL,(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos=*pos;
 this->sprite->vel=*vel;
 this->sprite->angle=heading;
 if (flat)
    this->sequenceMap=genprojFlatSequenceMap;
 else
    this->sequenceMap=genprojTurnSequenceMap;
 setState((SpriteObject *)this,0);
 this->owner=(Object *)owner;
 this->damage=damage;
 this->explosionColor=explosionColor;
 if (r || g || b)
    {addLight(this->sprite,16-r,16-g,16-b);
     this->light=1;
    }
 else
    this->light=0;
 this->armTime=armTime;
 return (Object *)this;
}

/********************************\
 *           BIT STUFF          *
\********************************/
static int nmBits=0;
void bit_func(Object *_this,int msg,int param1,int param2)
{BitObject *this=(BitObject *)_this;
 int collide;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    {freeSprite(this->sprite);
	     nmBits--;
	    }
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	collide=moveSprite(this->sprite);
	if (collide & COLLIDE_FLOOR)
	   {this->sprite->vel.x=0;
	    this->sprite->vel.y=0;
	    this->sprite->vel.z=0;
	    this->sprite->flags|=SPRITEFLAG_IMMOBILE;
	   }

	this->age++;
	if (this->age>90)
	   this->sprite->scale=32000-((this->age-90)<<11);
	if (this->age>90+15)
	   delayKill(_this);
	break;
       }
}

Object *constructBit(int sector,MthXyz *pos,int sequence,int onFloor)
{BitObject *this;
 if (nmBits>20)
    return NULL;
 this=(BitObject *)getFreeObject(bit_func,OT_BIT,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 level_sequenceMap[OT_BIT]=0; /* yak! */
 this->sprite=newSprite(sector,F(16),F(1),GRAVITY<<1,
			0,SPRITEFLAG_IMATERIAL,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->scale=32000;
 this->sprite->pos=*pos;
 this->sprite->vel.x=(MTH_GetRand()&0x7ffff)-F(4);
 this->sprite->vel.z=(MTH_GetRand()&0x7ffff)-F(4);
 if (onFloor)
    this->sprite->vel.y=F(8)+(MTH_GetRand()&0x3ffff);
 else
    this->sprite->vel.y=(MTH_GetRand()&0x0fffff);
 this->sequence=sequence;
 this->sequenceMap=&this->sequence;
 this->age=getNextRand()&0x1f;
 setState((SpriteObject *)this,0);
 nmBits++;
 return (Object *)this;
}

void makeExplosion(MonsterObject *this,int bitSeqStart,int nmBits)
{int seq,i;
 seq=level_sequenceMap[this->type]+bitSeqStart;
 for (i=0;i<nmBits;i++)
    constructBit(this->sprite->s,&this->sprite->pos,seq++,
		 this->sprite->floorSector!=-1);
}


/********************************\
 *        BOUNCYBIT STUFF       *
\********************************/
static int nmBouncyBits=0;

void bouncyBit_func(Object *_this,int msg,int param1,int param2)
{BitObject *this=(BitObject *)_this;
 int collide;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    {freeSprite(this->sprite);
	     nmBouncyBits--;
	    }
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	collide=moveSprite(this->sprite);
	if (collide & COLLIDE_FLOOR)
	   {if (this->age<2)
	       {if (this->soft)
		   posMakeSound((int)this,&this->sprite->pos,
				level_objectSoundMap[OT_CONTAIN10]+
				(getNextRand()%3));
	        else
		   posMakeSound((int)this,&this->sprite->pos,
				level_staticSoundMap[ST_BLOWPOT]+
				(getNextRand()%3));
	       }
	    this->sprite->vel.x>>=1;
	    this->sprite->vel.y>>=1;
	    this->sprite->vel.z>>=1;
	    this->age++;
	    if (this->age>5)
	       delayKill(_this);
	   }
	break;
       }
}

Object *constructBouncyBit(int sector,MthXyz *pos,int sequence,int frame,
			   int softType)
{BitObject *this;
 if (nmBouncyBits>10)
    return NULL;
 this=(BitObject *)
    getFreeObject(bouncyBit_func,OT_BOUNCYBIT,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 level_sequenceMap[OT_BOUNCYBIT]=0; /* yak! */
 this->sprite=newSprite(sector,F(16),F(1),GRAVITY<<1,
			0,SPRITEFLAG_IMATERIAL|SPRITEFLAG_BOUNCY,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->scale=32000;
 this->sprite->pos=*pos;
 this->sprite->vel.x=(MTH_GetRand()&0xfffff)-F(8);
 this->sprite->vel.z=(MTH_GetRand()&0xfffff)-F(8);
 this->sprite->vel.y=F(8)+(MTH_GetRand()&0x7ffff);
 this->sequence=sequence;
 this->sequenceMap=&this->sequence;
 this->age=0;
 this->soft=softType;
 setState((SpriteObject *)this,0);
 this->sprite->frame=frame;
 nmBouncyBits++;
 return (Object *)this;
}

/********************************\
 *        HARDBIT STUFF         *
\********************************/

void hardBit_func(Object *_this,int msg,int param1,int param2)
{HardBitObject *this=(HardBitObject *)_this;
 int collide;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	if (this->stuck)
	   break;
	collide=moveSprite(this->sprite);
	if (collide & COLLIDE_FLOOR)
	   {this->sprite->vel.x=0;
	    this->sprite->vel.y=0;
	    this->sprite->vel.z=0;
	    this->stuck=1;
	    this->sprite->flags|=SPRITEFLAG_IMMOBILE;
	    delay_moveObject((Object *)this,objectIdleList);
	   }
	break;
       }
}

Object *constructHardBit(int sector,MthXyz *pos,int sequence,int onFloor,
			 int frame)
{HardBitObject *this=(HardBitObject *)
    getFreeObject(hardBit_func,OT_HARDBIT,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 level_sequenceMap[OT_HARDBIT]=0; /* yak! */
 this->sprite=newSprite(sector,F(16),F(1),GRAVITY<<1,
			0,SPRITEFLAG_IMATERIAL,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->scale=32000;
 this->sprite->pos=*pos;
 this->sprite->vel.x=(MTH_GetRand()&0x7ffff)-F(4);
 this->sprite->vel.z=(MTH_GetRand()&0x7ffff)-F(4);
 if (onFloor)
    this->sprite->vel.y=F(8)+(MTH_GetRand()&0x3ffff);
 else
    this->sprite->vel.y=(MTH_GetRand()&0x0fffff);

 this->sequence=sequence;
 this->sequenceMap=&this->sequence;
 this->stuck=0;
 setState((SpriteObject *)this,0);
 this->sprite->frame=frame;
 return (Object *)this;
}

/********************************\
 *           ZORCH STUFF        *
\********************************/

void zorch_func(Object *_this,int msg,int param1,int param2)
{ZorchObject *this=(ZorchObject *)_this;
 int fflags;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	this->age++;
	if (this->age<30)
	   break;
	if (this->age==30)
	   this->sprite->flags&=~SPRITEFLAG_INVISIBLE;

	fflags=spriteAdvanceFrame(this->sprite);
	if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
	   {if (this->type==OT_BLUEZORCH)
	       constructThing(this->sprite->s,
			      this->sprite->pos.x,
			      this->sprite->pos.y,
			      this->sprite->pos.z,
			      this->big?OT_AMMOORB:OT_AMMOBALL);
	    if (this->type==OT_REDZORCH)
	       constructThing(this->sprite->s,
			      this->sprite->pos.x,
			      this->sprite->pos.y,
			      this->sprite->pos.z,
			      this->big?OT_HEALTHORB:OT_HEALTHBALL);
	    delayKill(_this);
	   }
	break;
       }
}

unsigned short zorchSequenceMap[]={0};

Object *constructZorch(int sector,int x,int y,int z,int zorchType)
{ZorchObject *this=(ZorchObject *)
    getFreeObject(zorch_func,zorchType,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_INVISIBLE|SPRITEFLAG_IMATERIAL|
			SPRITEFLAG_IMMOBILE,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos.x=x;
 this->sprite->pos.y=y;
 this->sprite->pos.z=z;
 this->big=(getNextRand()&0x40);
 if (this->big)
    this->sprite->scale=48000;
 else
    this->sprite->scale=28000;
 this->sequenceMap=zorchSequenceMap;
 this->age=0;
 setState((SpriteObject *)this,0);
 return (Object *)this;
}



/********************************\
 *          SPIDER STUFF        *
\********************************/

enum {AI_SPIDER_IDLE,AI_SPIDER_WALK,AI_SPIDER_BITE,AI_SPIDER_JUMPUP,
	 AI_SPIDER_JUMPDOWN,AI_SPIDER_HIT,AI_SPIDER_NMSEQ};

unsigned short spiderSeqMap[]={HB|0, HB|0, HB|0, HB|8,
				  HB|0, 16};

void spider_func(Object *_this,int message,int param1,int param2)
{SpiderObject *this=(SpiderObject *)_this;
 int collide=0,fflags=0;
 switch (message)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeZorch((SpriteObject *)this);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     this->sprite->floorSector==-1?
			         OT_AIRGUTS:OT_LANDGUTS,
			     48000,0,0);
	    spriteObject_makeSound((SpriteObject *)this,1);
	    makeExplosion((MonsterObject *)this,17,4);
	    makeExplosion((MonsterObject *)this,17,4);
	    delayKill(_this);
	   }
	else
	   spriteObject_makeSound((SpriteObject *)this,0);
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_SPIDER_IDLE)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (!this->enemy)
	       setState((SpriteObject *)this,AI_SPIDER_IDLE);
	   }
	switch (this->state)
	   {case AI_SPIDER_IDLE:
	       if ((this->aiSlot&0x0f)==(aicount&0x0f))
		  {if (!this->enemy)
		      this->enemy=findPlayer(this->sprite,F(900));
		   if (this->enemy)
		      {setState((SpriteObject *)this,AI_SPIDER_WALK);
		       break;
		      }
		  }
	       break;
	    case AI_SPIDER_JUMPUP:
	       if (this->sprite->vel.y<=0)
		  setState((SpriteObject *)this,AI_SPIDER_JUMPDOWN);
	       break;
	    case AI_SPIDER_JUMPDOWN:
	       if (this->sprite->floorSector!=-1)
		  setState((SpriteObject *)this,AI_SPIDER_WALK);
	       break;
	    case AI_SPIDER_WALK:
	       if (collide & COLLIDE_SPRITE)
		  if (sprites[collide&0xffff].owner==(Object *)this->enemy)
		     {/* bite them! */
		      setState((SpriteObject *)this,AI_SPIDER_BITE);
		      this->sprite->vel.x=0;
		      this->sprite->vel.z=0;
		     }
	       if (collide & COLLIDE_WALL)
		  {this->sprite->angle=
		      normalizeAngle(this->sprite->angle+/*F(180)+*/
				     randomAngle(8));
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<2;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<2;
		  }
	       if (AISLOT(0x0f))
		  {switch (decideWhatToDo((MonsterObject *)this,0,0))
		      {case DO_GOIDLE:
			  setState((SpriteObject *)this,AI_SPIDER_IDLE);
			  this->enemy=NULL;
			  break;
		       case DO_CHARGE:
			  PlotCourseToObject((SpriteObject *)this,
					     (SpriteObject *)this->enemy);
			  break;
		       case DO_RANDOMWANDER:
			  if (AISLOT(0x1f))
			     this->sprite->angle=((short)getNextRand())*360;
			  break;
			 }
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<2;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<2;
		   if (getNextRand()&1)
		      {this->sprite->vel.y = F(16);
		       spriteObject_makeSound((SpriteObject *)this,2);
		       setState((SpriteObject *)this,AI_SPIDER_JUMPUP);
		      }
		  }
	       break;
	    case AI_SPIDER_BITE:
	       if (this->sprite->frame==2)
		  {spriteObject_makeSound((SpriteObject *)this,3);
		   assert(this->enemy);
		   assert(this->enemy->class!=CLASS_DEAD);
		   signalObject((Object *)this->enemy,SIGNAL_HURT,10,
				(int)this);
		   setState((SpriteObject *)this,AI_SPIDER_WALK);
		  }
	       break;
	      }
	break;
       }
}

Object *constructSpider(int sector,int suckParams,MthXyz *pos,MthXyz *vel)
{SpiderObject *this=(SpiderObject *)getFreeObject(spider_func,OT_SPIDER,
						  CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(16),F(1),GRAVITY<<2,
			0,SPRITEFLAG_BWATERBNDRY,
			(Object *)this);
 if (suckParams)
    suckSpriteParams(this->sprite);
 else
    this->sprite->pos=*pos;
 this->sprite->scale=40000;
 this->sequenceMap=spiderSeqMap;
 this->enemy=NULL;
 setState((SpriteObject *)this,AI_SPIDER_IDLE);
 if (vel)
    this->sprite->vel=*vel;
 if (!suckParams)
    {setState((SpriteObject *)this,AI_SPIDER_JUMPUP);
     this->enemy=player;
    }
 this->health=20;
 this->aiSlot=nextAiSlot++;
 return (Object *)this;
}


/********************************\
 *           FISH STUFF         *
\********************************/

enum {AI_FISH_IDLE,AI_FISH_WALK,AI_FISH_BITE,AI_FISH_SEEK};

unsigned short fishSeqMap[]={HB|8, HB|0, HB|19};

void fish_func(Object *_this,int message,int param1,int param2)
{FishObject *this=(FishObject *)_this;
 int collide=0,fflags=0;
 switch (message)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeZorch((SpriteObject *)this);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_AIRGUTS,48000,0,0);
	    spriteObject_makeSound((SpriteObject *)this,1);
	    makeExplosion(this,16,3);
	    makeExplosion(this,16,3);
	    delayKill(_this);
	   }
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_FISH_IDLE)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (!this->enemy)
	       setState((SpriteObject *)this,AI_FISH_IDLE);
	   }
	if (this->state==AI_FISH_WALK && this->routePos==-1 &&
	    (this->aiSlot&0x07)==(aicount&0x07))
	   if (this->enemy)
	      {if (this->sprite->pos.y<this->enemy->sprite->pos.y-F(10))
		  this->sprite->vel.y=F(1);
	       else
		  if (this->sprite->pos.y>this->enemy->sprite->pos.y+F(10))
		     this->sprite->vel.y=-F(1);
		  else
		     this->sprite->vel.y=0;
	      }
	   else
	      this->sprite->vel.y=0;
	switch (this->state)
	   {case (AI_FISH_IDLE) :
	       if ((this->aiSlot&0x0f)==(aicount&0x0f))
		  {this->sprite->vel.x=0;
		   this->sprite->vel.y=0;
		   if (!this->enemy)
		      this->enemy=findPlayer(this->sprite,F(900));
		   if (this->enemy)
		      {spriteObject_makeSound((SpriteObject *)this,0);
		       setState((SpriteObject *)this,AI_FISH_WALK);
		       break;
		      }
		  }
	       break;
	    case AI_FISH_WALK:
	       if (collide & COLLIDE_SPRITE)
		  if (sprites[collide&0xffff].owner==(Object *)this->enemy)
		     {/* bite them! */
		      setState((SpriteObject *)this,AI_FISH_BITE);
		      this->sprite->vel.x=0;
		      this->sprite->vel.y=0;
		      this->sprite->vel.z=0;
		      break;
		     }
	       if (AISLOT(0x0f))
		  {switch (decideWhatToDo((MonsterObject *)this,1,1))
		      {case DO_GOIDLE:
			  setState((SpriteObject *)this,AI_FISH_IDLE);
			  dPrint("Fish idle!\n");
			  this->enemy=NULL;
			  break;
		       case DO_FOLLOWROUTE:
			  this->sprite->vel.x=0;
			  this->sprite->vel.z=0;
			  setState((SpriteObject *)this,AI_FISH_SEEK);
			  return;
		       case DO_CHARGE:
			  PlotCourseToObject((SpriteObject *)this,
					     (SpriteObject *)this->enemy);
			  break;
		       case DO_RANDOMWANDER:
			  if (AISLOT(0x1f))
			     this->sprite->angle=((short)getNextRand())*360;
			  break;
			 }
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<2;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<2;
		  }
	       break;
	    case AI_FISH_BITE:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {if (spriteDistApprox(this->sprite,
					this->enemy->sprite)<F(150))
		      {PlotCourseToObject((SpriteObject *)this,
					  (SpriteObject *)this->enemy);
		       assert(this->enemy->class!=CLASS_DEAD);
		       signalObject((Object *)this->enemy,SIGNAL_HURT,20,
				    (int)this);
		       setState((SpriteObject *)this,AI_FISH_WALK);
		      }
		   else
		      setState((SpriteObject *)this,AI_FISH_WALK);
		  }
	       break;
	    case AI_FISH_SEEK:
	       if ((this->aiSlot&0x0f)==(aicount&0x0f) &&
		   monster_seekEnemy((MonsterObject *)this,1,2))
		  {dPrint("Fish tracking\n");
		   setState((SpriteObject *)this,AI_FISH_WALK);
		   this->sprite->vel.x=0;
		   this->sprite->vel.y=0;
		   this->sprite->vel.z=0;
		  }
	       break;
	      }
	break;
       }
}

Object *constructFish(int sector)
{FishObject *this=(FishObject *)getFreeObject(fish_func,OT_FISH,CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_BWATER,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sprite->scale=40000;
 this->sequenceMap=fishSeqMap;
 setState((SpriteObject *)this,AI_FISH_IDLE);
 this->health=40;
 this->routePos=-1;
 this->enemy=NULL;
 this->aiSlot=nextAiSlot++;
 return (Object *)this;
}


/********************************\
 *          COBRA  STUFF        *
\********************************/

enum {AI_COBRA_SEEK,AI_COBRA_HOME};

unsigned short cobraSeqMap[]={HB|0,HB|0};

void cobra_func(Object *_this,int message,int param1,int param2)
{CobraObject *this=(CobraObject *)_this;
 int collide,fflags,i;
 static int lightP[]={F(5),F(25),-F(1),0,
		      F(0),F(25),0,0,
		      F(5),F(25),-F(1),0};
 static int mlightP[]={F(0),F(25),0,0,
		       F(5),F(25),-F(1),0,
		       F(5),F(25),-F(1),0};
 int c,s,wave;
 switch (message)
    {case SIGNAL_HURT:
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==(Object *)this->enemy)
	    this->enemy=NULL;
	 if (killed==_this)
	    {if (this->glow)
		removeLight(this->sprite);
	     freeSprite(this->sprite);
	     for (i=0;i<NMCOBRABALLS;i++)
		freeSprite(this->balls[i]);
	    }
	 break;
	}
     case SIGNAL_MOVE:
	this->age++;
	for (i=NMCOBRABALLS-1;i;i--)
	   {moveSpriteTo(this->balls[i],
			 this->balls[i-1]->s,
			 &this->balls[i-1]->pos);
	    this->balls[i]->flags=
	       this->balls[i-1]->flags;
	   }
	moveSpriteTo(this->balls[0],
		     this->sprite->s,
		     &this->sprite->pos);

	collide=moveSprite(this->sprite);
	fflags=spriteAdvanceFrame(this->sprite);
	/* do motion */
	c=MTH_Cos(this->sprite->angle);
	s=MTH_Sin(this->sprite->angle);
	this->sprite->vel.x=c<<4;
	this->sprite->vel.z=s<<4;

	wave=MTH_Cos(this->wave);
	this->wave+=F(20);
	if (this->wave>F(180))
	   this->wave-=F(360);
	this->sprite->vel.x+=MTH_Mul(s,wave)<<2;
	this->sprite->vel.z+=MTH_Mul(-c,wave)<<2;

	switch (this->state)
	   {case AI_COBRA_HOME:
	       if (!this->enemy ||
		   (this->enemy->sprite->flags & SPRITEFLAG_INVISIBLE))
		  {setState((SpriteObject *)this,AI_COBRA_SEEK);
		   break;
		  }
	       spriteHome((SpriteObject *)this,(SpriteObject *)this->enemy,
			  F(10),this->type==OT_MUMBALL?F(4):F(8));
	       if (this->sprite->pos.y<this->enemy->sprite->pos.y-F(20))
		  this->sprite->vel.y=F(4);
	       else
		  if (this->sprite->pos.y>this->enemy->sprite->pos.y+F(20))
		     this->sprite->vel.y=-F(4);
		  else
		     this->sprite->vel.y=0;
	       break;
	    case AI_COBRA_SEEK:
	       {if (this->age<10)
		   break;
		if ((this->aiSlot&0x3)!=(aicount&0x3))
		   break;
		/* look for enemy */
		{Object *o;
		 Object *list;
		 MonsterObject *m;
		 MonsterObject *best;
		 int dist,angle,rating,bestRating;
		 best=NULL;
		 bestRating=INT_MAX;
		 for (list=o=objectRunList;;o=o->next)
		    {if (!o)
			if (list==objectRunList)
			   {list=objectIdleList;
			    o=list;
			    continue;
			   }
			else
			   break;
		     if (o->class!=CLASS_MONSTER || o==this->owner)
			continue;
		     m=(MonsterObject *)o;
		     if (m->sprite->flags & SPRITEFLAG_INVISIBLE)
			continue;
		     dist=approxDist(m->sprite->pos.x-this->sprite->pos.x,
				     m->sprite->pos.y-this->sprite->pos.y,
				     m->sprite->pos.z-this->sprite->pos.z);
		     if (dist>F(1000))
			continue;
		     angle=abs(normalizeAngle(getAngle(m->sprite->pos.x-
						       this->sprite->pos.x,
						       m->sprite->pos.z-
						       this->sprite->pos.z)-
					      this->sprite->angle));
		     if (angle>F(90))
			continue;
		     rating=(dist>>2)+angle;

		     if (m->type>=OT_CONTAIN1 && m->type<=OT_CONTAIN17)
			continue; /* rating+=F(800); */
		     if (m->type==OT_BOOMPOT1 || m->type==OT_BOOMPOT2)
			continue; /* rating+=F(600); */
		     if (rating>bestRating)
			continue;
		     if (!canSee(this->sprite,m->sprite))
			continue;
		     bestRating=rating;
		     best=m;
		    }
		 if (best)
		    {this->enemy=best;
		     setState((SpriteObject *)this,AI_COBRA_HOME);
		    }
		 break;
		}
	       }
	      }
	if (collide & (COLLIDE_SPRITE|COLLIDE_WALL))
	   {if (!(collide & COLLIDE_WALL) && (collide & COLLIDE_SPRITE) &&
		(sprites[collide&0xffff].owner==this->owner))
	       break;
	    for (i=0;i<3;i++)
	       constructOneShot(this->sprite->s,
				this->sprite->pos.x+(((int)MTH_GetRand())>>10),
				this->sprite->pos.y+(((int)MTH_GetRand())>>10)
				         +F(10),
				this->sprite->pos.z+(((int)MTH_GetRand())>>10),
				OT_POOF,F(1),
				this->type==OT_MUMBALL?RGB(20,5,5):RGB(5,20,5),
				i*3);
	    constructLight(this->sprite->s,
			   this->sprite->pos.x,
			   this->sprite->pos.y+F(30),
			   this->sprite->pos.z,
			   this->type==OT_MUMBALL?mlightP:lightP,F(1)/16);
	    if (this->type==OT_MUMBALL)
	       radialDamage((Object *)this,&this->sprite->pos,this->sprite->s,
			    70,F(200));
	    else
	       radialDamage((Object *)this,&this->sprite->pos,this->sprite->s,
			    100,F(300));
	    delayKill(_this);
	   }
	break;
       }
}

Object *constructCobra(int sector,int x,int y,int z,int heading,Fixed32 yvel,
		       SpriteObject *owner,int type,int glow)
{int i;
 CobraObject *this=(CobraObject *)getFreeObject(cobra_func,type,
						CLASS_PROJECTILE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;

 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_IMATERIAL,(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos.x=x;
 this->sprite->pos.y=y;
 this->sprite->pos.z=z;
 this->sprite->scale=40000;
 this->sequenceMap=cobraSeqMap;
 this->sprite->angle=heading;
 this->sprite->vel.y=yvel;
 setState((SpriteObject *)this,AI_COBRA_SEEK);

 for (i=0;i<NMCOBRABALLS;i++)
    {this->balls[i]=newSprite(sector,F(16),F(1),0,
			      level_sequenceMap[type]+8,
			      SPRITEFLAG_INVISIBLE|SPRITEFLAG_IMMOBILE|
			      SPRITEFLAG_IMATERIAL,
			      NULL);
     if (!this->balls[i])
	{for (i--;i>=0;i--)
	    freeSprite(this->balls[i]);
	 freeSprite(this->sprite);
	 moveObject((Object *)this,objectFreeList);
	 return NULL;
	}
     this->balls[i]->pos.x=x;
     this->balls[i]->pos.y=y;
     this->balls[i]->pos.z=z;
     this->balls[i]->scale=40000-i*5000;
    }
 this->balls[0]->flags&=~SPRITEFLAG_INVISIBLE;
 this->wave=0;
 this->owner=(Object *)owner;
 this->age=0;
 this->enemy=NULL;
 this->aiSlot=nextAiSlot++;
 this->glow=glow;
 if (this->glow)
    addLight(this->sprite,16,31,31);
 return (Object *)this;
}

#if 0
/********************************\
 *        BADCOBRA  STUFF       *
\********************************/

enum {AI_BADCOBRA_HOME};

unsigned short badCobraSeqMap[]={HB|0};

void badCobra_func(Object *_this,int message,int param1,int param2)
{BadCobraObject *this=(BadCobraObject *)_this;
 int collide,fflags,i;
 static int lightP[]={F(5),F(25),-F(1),0,
		      F(0),F(25),0,0,
		      F(5),F(25),-F(1),0};
 int c,s,wave;
 switch (message)
    {case SIGNAL_HURT:
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==(Object *)this->enemy)
	    this->enemy=NULL;
	 if (killed==_this)
	    {freeSprite(this->sprite);
	     for (i=0;i<NMBADCOBRABALLS;i++)
		freeSprite(this->balls[i]);
	    }
	 break;
	}
     case SIGNAL_MOVE:
	this->age++;
	for (i=NMBADCOBRABALLS-1;i;i--)
	   {moveSpriteTo(this->balls[i],
			 this->balls[i-1]->s,
			 &this->balls[i-1]->pos);
	    this->balls[i]->flags=
	       this->balls[i-1]->flags;
	   }
	moveSpriteTo(this->balls[0],
		     this->sprite->s,
		     &this->sprite->pos);

	collide=moveSprite(this->sprite);
	fflags=spriteAdvanceFrame(this->sprite);
	/* do motion */
	c=MTH_Cos(this->sprite->angle);
	s=MTH_Sin(this->sprite->angle);
	this->sprite->vel.x=c<<4;
	this->sprite->vel.z=s<<4;

	wave=MTH_Cos(this->wave);
	this->wave+=F(20);
	if (this->wave>F(180))
	   this->wave-=F(360);
	this->sprite->vel.x+=MTH_Mul(s,wave)<<2;
	this->sprite->vel.z+=MTH_Mul(-c,wave)<<2;

	switch (this->state)
	   {case AI_BADCOBRA_HOME:
	       if (!this->enemy)
		  {delayKill(_this);
		   break;
		  }
	       spriteHome((SpriteObject *)this,(SpriteObject *)this->enemy,
			  F(10),F(8));
	       if (this->sprite->pos.y<this->enemy->sprite->pos.y+
		   this->enemy->sprite->radius-F(20))
		  this->sprite->vel.y=F(4);
	       else
		  if (this->sprite->pos.y>this->enemy->sprite->pos.y+
		      this->enemy->sprite->radius+F(20))
		     this->sprite->vel.y=-F(4);
		  else
		     this->sprite->vel.y=0;
	       break;
	      }
	if (collide & (COLLIDE_SPRITE|COLLIDE_WALL))
	   {if (!(collide & COLLIDE_WALL) && (collide & COLLIDE_SPRITE) &&
		(sprites[collide&0xffff].owner==this->owner))
	       break;
	    for (i=0;i<3;i++)
	       constructOneShot(this->sprite->s,
				this->sprite->pos.x+(((int)MTH_GetRand())>>10),
				this->sprite->pos.y+(((int)MTH_GetRand())>>10)
				         +F(10),
				this->sprite->pos.z+(((int)MTH_GetRand())>>10),
				OT_POOF,F(1),
				this->type==OT_MUMBALL?RGB(20,5,5):RGB(5,20,5),
				i*3);
	    constructLight(this->sprite->s,
			   this->sprite->pos.x,
			   this->sprite->pos.y+F(30),
			   this->sprite->pos.z,
			   lightP,F(1)/16);
	    radialDamage((Object *)this,&this->sprite->pos,this->sprite->s,
			 50,F(200));
	    delayKill(_this);
	   }
	break;
       }
}

Object *constructBadCobra(int sector,int x,int y,int z,int heading,
			  Fixed32 yvel,SpriteObject *owner)
{int i;
 BadCobraObject *this=(BadCobraObject *)getFreeObject(badCobra_func,type,
						      CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;

 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_IMATERIAL,(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos.x=x;
 this->sprite->pos.y=y;
 this->sprite->pos.z=z;
 this->sprite->scale=40000;
 this->sequenceMap=badCobraSeqMap;
 this->sprite->angle=heading;
 this->sprite->vel.y=yvel;
 setState((SpriteObject *)this,AI_BADCOBRA_HOME);

 for (i=0;i<NMBADCOBRABALLS;i++)
    {this->balls[i]=newSprite(sector,F(16),F(1),0,
			      level_sequenceMap[type]+8,
			      SPRITEFLAG_INVISIBLE|SPRITEFLAG_IMMOBILE|
			      SPRITEFLAG_IMATERIAL,
			      NULL);
     if (!this->balls[i])
	{for (i--;i>=0;i--)
	    freeSprite(this->balls[i]);
	 freeSprite(this->sprite);
	 moveObject((Object *)this,objectFreeList);
	 return NULL;
	}
     this->balls[i]->pos.x=x;
     this->balls[i]->pos.y=y;
     this->balls[i]->pos.z=z;
     this->balls[i]->scale=40000-i*5000;
    }
 this->balls[0]->flags&=~SPRITEFLAG_INVISIBLE;
 this->wave=0;
 this->owner=(Object *)owner;
 this->age=0;
 this->enemy=NULL;
 this->aiSlot=nextAiSlot++;
 return (Object *)this;
}
#endif

#if 0
/********************************\
 *        GLOWWORM  STUFF       *
\********************************/

enum {AI_GLOWWORM_SLEEP,AI_GLOWWORM_SEEK,AI_GLOWWORM_RETREAT};

unsigned short glowWormSeqMap[]={HB|0,HB|0};

void glowWorm_func(Object *_this,int message,int param1,int param2)
{GlowWormObject *this=(GlowWormObject *)_this;
 int collide,fflags,i;
 static int lightP[]={F(5),F(25),-F(1),0,
		      F(0),F(25),0,0,
		      F(5),F(25),-F(1),0};
 int c,s,wave;
 switch (message)
    {case SIGNAL_HURT:
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==(Object *)this->enemy)
	    this->enemy=NULL;
	 if (killed==_this)
	    {freeSprite(this->sprite);
	     for (i=0;i<NMGLOWWORMBALLS;i++)
		freeSprite(this->balls[i]);
	    }
	 break;
	}
     case SIGNAL_MOVE:
	this->age++;
	for (i=NMGLOWWORMBALLS-1;i;i--)
	   {moveSpriteTo(this->balls[i],
			 this->balls[i-1]->s,
			 &this->balls[i-1]->pos);
	    this->balls[i]->flags=
	       this->balls[i-1]->flags;
	   }
	moveSpriteTo(this->balls[0],
		     this->sprite->s,
		     &this->sprite->pos);

	collide=moveSprite(this->sprite);
	fflags=spriteAdvanceFrame(this->sprite);
	/* do motion */
	c=MTH_Cos(this->sprite->angle);
	s=MTH_Sin(this->sprite->angle);
	this->sprite->vel.x=c<<4;
	this->sprite->vel.z=s<<4;

	wave=MTH_Cos(this->wave);
	this->wave+=F(20);
	if (this->wave>F(180))
	   this->wave-=F(360);
	this->sprite->vel.x+=MTH_Mul(s,wave)<<2;
	this->sprite->vel.z+=MTH_Mul(-c,wave)<<2;

	switch (this->state)
	   {case AI_GLOWWORM_HOME:
	       if (!this->enemy)
		  {setState((SpriteObject *)this,AI_GLOWWORM_SEEK);
		   break;
		  }
	       {spriteHome((SpriteObject *)this,(SpriteObject *)this->enemy,
			   F(10),F(5));
		if (this->sprite->pos.y<this->enemy->sprite->pos.y+
		    this->enemy->sprite->radius-F(20))
		   this->sprite->vel.y=F(4);
		else
		   if (this->sprite->pos.y>this->enemy->sprite->pos.y+
		       this->enemy->sprite->radius+F(20))
		      this->sprite->vel.y=-F(4);
		   else
		      this->sprite->vel.y=0;
		break;
	       }
	    case AI_GLOWWORM_SEEK:
	       {if (this->age<10)
		   break;
		if ((this->aiSlot&0x3)!=(aicount&0x3))
		   break;
		/* look for enemy */
		{Object *o;
		 Object *list;
		 MonsterObject *m;
		 MonsterObject *best;
		 int rating,bestRating;
		 best=NULL;
		 bestRating=F(10000);
		 for (list=o=objectRunList;;o=o->next)
		    {if (!o)
			if (list==objectRunList)
			   {list=objectIdleList;
			    o=list;
			    continue;
			   }
			else
			   break;
		     if (o->class!=CLASS_MONSTER || o==this->owner)
			continue;
		     m=(MonsterObject *)o;
		     rating=approxDist(m->sprite->pos.x-this->sprite->pos.x,
				       m->sprite->pos.y-this->sprite->pos.y,
				       m->sprite->pos.z-this->sprite->pos.z);
		     if (rating>F(1000))
			continue;
		     rating=(rating>>2)+
			abs(normalizeAngle(getAngle(m->sprite->pos.x-
						    this->sprite->pos.x,
						    m->sprite->pos.z-
						    this->sprite->pos.z)-
					   this->sprite->angle));
		     if (rating>bestRating)
			continue;
		     if (!canSee(this->sprite,m->sprite))
			continue;
		     rating=bestRating;
		     best=m;
		    }
		 if (best)
		    {this->enemy=best;
		     setState((SpriteObject *)this,AI_GLOWWORM_HOME);
		    }
		 break;
		}
	       }
	      }
	if (collide & (COLLIDE_SPRITE|COLLIDE_WALL))
	   {if ((collide & COLLIDE_SPRITE) &&
		(sprites[collide&0xffff].owner==this->owner))
	       break;
	    for (i=0;i<3;i++)
	       constructOneShot(this->sprite->s,
				this->sprite->pos.x+(((int)MTH_GetRand())>>10),
				this->sprite->pos.y+(((int)MTH_GetRand())>>10)
				         +F(10),
				this->sprite->pos.z+(((int)MTH_GetRand())>>10),
				OT_POOF,F(1),RGB(5,20,5),i*3);
	    constructLight(this->sprite->s,
			   this->sprite->pos.x,
			   this->sprite->pos.y+F(30),
			   this->sprite->pos.z,
			   lightP,F(1)/16);
	    if (this->type==OT_MUMBALL)
	       radialDamage((Object *)this,&this->sprite->pos,this->sprite->s,
			    50,F(200));
	    else
	       radialDamage((Object *)this,&this->sprite->pos,this->sprite->s,
			    100,F(300));
	    delayKill(_this);
	   }
	break;
       }
}

Object *constructGlowWorm(int sector)
{int i;
 GlowWormObject *this=(GlowWormObject *)getFreeObject(glowWorm_func,
						      OT_GLOWWORM,
						      CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;

 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_IMATERIAL,(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos.x=x;
 this->sprite->pos.y=y;
 this->sprite->pos.z=z;
 this->sprite->scale=40000;
 this->sequenceMap=glowWormSeqMap;
 this->sprite->angle=heading;
 this->sprite->vel.y=yvel;
 setState((SpriteObject *)this,AI_GLOWWORM_SEEK);

 for (i=0;i<NMGLOWWORMBALLS;i++)
    {this->balls[i]=newSprite(sector,F(16),F(1),0,
			      level_sequenceMap[type]+8,
			      SPRITEFLAG_INVISIBLE|SPRITEFLAG_IMMOBILE|
			      SPRITEFLAG_IMATERIAL,
			      NULL);
     if (!this->balls[i])
	{for (i--;i>=0;i--)
	    freeSprite(this->balls[i]);
	 freeSprite(this->sprite);
	 moveObject((Object *)this,objectFreeList);
	 return NULL;
	}
     this->balls[i]->pos.x=x;
     this->balls[i]->pos.y=y;
     this->balls[i]->pos.z=z;
     this->balls[i]->scale=40000-i*5000;
    }
 this->balls[0]->flags&=~SPRITEFLAG_INVISIBLE;
 this->wave=0;
 this->owner=(Object *)owner;
 this->age=0;
 this->enemy=NULL;
 this->aiSlot=nextAiSlot++;
 return (Object *)this;
}
#endif


/********************************\
 *           ZAP  STUFF         *
\********************************/

void zap_func(Object *_this,int message,int param1,int param2)
{ZapObject *this=(ZapObject *)_this;
 int i,collide,loop;
 switch (message)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==(Object *)this->enemy)
	    this->enemy=NULL;
	 if (killed==_this)
	    {freeSprite(this->sprite);
	     for (i=0;i<NMZAPBALLS;i++)
		freeSprite(this->balls[i]);
	    }
	 break;
	}
     case SIGNAL_MOVE:
	if (!this->enemy || this->age++>120)
	   {delayKill((Object *)this);
	    break;
	   }
	assert(this->enemy->class==CLASS_MONSTER);
	for (loop=0;loop<2;loop++)
	   {for (i=0;i<NMZAPBALLS;i++)
	       {this->balls[i]->angle=this->balls[i]->pos.x;
		this->balls[i]->scale=this->balls[i]->pos.y;
		this->balls[i]->frame=this->balls[i]->pos.z;
	       }
	    this->sprite->angle=this->sprite->pos.x;
	    this->sprite->scale=this->sprite->pos.y;
	    this->sprite->frame=this->sprite->pos.z;
	    for (i=NMZAPBALLS-1;i;i--)
	       {moveSpriteTo(this->balls[i],
			     this->balls[i-1]->s,
			     &this->balls[i-1]->pos);
		this->balls[i]->flags=
		   this->balls[i-1]->flags;
	       }
	    moveSpriteTo(this->balls[0],
			 this->sprite->s,
			 &this->sprite->pos);
	    {Fixed32 tDist;
	     MthXyz del;
	     del.x=this->enemy->sprite->pos.x-this->sprite->pos.x;
	     del.y=this->enemy->sprite->pos.y-this->sprite->pos.y;
	     del.z=this->enemy->sprite->pos.z-this->sprite->pos.z;
	     tDist=approxDist(del.x,del.y,del.z);
	     tDist/=16; /* distance to move */
	     del.x=MTH_Div(del.x,tDist)+(((short)getNextRand())<<4);
	     del.y=MTH_Div(del.y,tDist)+(((short)getNextRand())<<4);
	     del.z=MTH_Div(del.z,tDist)+(((short)getNextRand())<<4);
	     /*del is now a length 64 vector pointing in the right direction*/
	     this->sprite->vel=del;
	    }
	    collide=moveSprite(this->sprite);
	    if ((collide & COLLIDE_SPRITE) &&
		sprites[collide&0xffff].owner!=this->owner)
	       {if (this->red)
		   {int i;
		    MthXyz vel;
		    for (i=0;i<5;i++)
		       {vel.x=(MTH_GetRand()&0xfffff)-F(8);
			vel.z=(MTH_GetRand()&0xfffff)-F(8);
			vel.y=F(10)+(MTH_GetRand()&0x1ffff);
			constructRingo(this->sprite->s,&this->sprite->pos,
				       &vel,(SpriteObject *)player);
		       }
		   }
		assert(sprites[collide&0xffff].owner->class!=CLASS_DEAD);
		signalObject(sprites[collide&0xffff].owner,
			     SIGNAL_HURT,100,
			     (int)this);
		delayKill((Object *)this);
		return;
	       }
	   }
       }
}

unsigned short zapSeqMap[]={0};
Object *constructZap(int sector,MthXyz *pos,SpriteObject *owner,
		     MonsterObject *enemy,int red)
{int i;
 ZapObject *this=(ZapObject *)getFreeObject(zap_func,OT_LIGHTNING,
					    CLASS_PROJECTILE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 assert(enemy);
 assert(enemy->class==CLASS_MONSTER);
 if (!this)
    return NULL;
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(16),F(1),0,level_sequenceMap[OT_HEALTHBALL],
			SPRITEFLAG_IMATERIAL|SPRITEFLAG_LINE,(Object *)this);
 if (!this->sprite)
    return NULL;
 this->sprite->pos=*pos;
 this->sprite->angle=pos->x;
 this->sprite->scale=pos->y;
 this->sprite->frame=pos->z;
 this->sprite->color=RGB(31,31,31);
 for (i=0;i<NMZAPBALLS;i++)
    {this->balls[i]=newSprite(sector,F(16),F(1),0,
			      level_sequenceMap[OT_HEALTHBALL],
			      SPRITEFLAG_LINE|
			      SPRITEFLAG_INVISIBLE|SPRITEFLAG_IMMOBILE|
			      SPRITEFLAG_IMATERIAL,
			      NULL);
     assert(this->balls[i]);
     this->balls[i]->pos=*pos;
     this->balls[i]->angle=pos->x;
     this->balls[i]->scale=pos->y;
     this->balls[i]->frame=pos->z;
     this->balls[i]->color=
	red?RGB(31-(i<<2),16-i,16-i):
	   RGB(31-(i<<2),31-(i<<2),31-(i<<2));
    }
 this->balls[0]->flags&=~SPRITEFLAG_INVISIBLE;
 this->owner=(Object *)owner;
 this->enemy=enemy;
 assert(this->enemy->class==CLASS_MONSTER);
 this->aiSlot=nextAiSlot++;
 this->red=red;
 this->age=0;
 this->sequenceMap=zapSeqMap;
 return (Object *)this;
}


/********************************\
 *          HAWK STUFF          *
\********************************/
enum {AI_HAWK_SLEEP,AI_HAWK_GLIDE,AI_HAWK_DIVE,AI_HAWK_CHASE,
	 AI_HAWK_RETREAT,AI_HAWK_NMSTATES};

unsigned short hawkSeqMap[]={HB|8,HB|8,HB|8,HB|0,
				HB|16};

void hawk_func(Object *_this,int message,int param1,int param2)
{HawkObject *this=(HawkObject *)_this;
 int collide=0,fflags=0,i;
 switch (message)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeZorch((SpriteObject *)this);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_AIRGUTS,48000,0,0);
	    for (i=0;i<10;i++)
	       constructBit(this->sprite->s,
			    &this->sprite->pos,
			    level_sequenceMap[OT_HAWK]+24,0);
	    delayKill(_this);
	   }
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	if (this->state==AI_HAWK_SLEEP)
	   setState((SpriteObject *)this,AI_HAWK_GLIDE);
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_HAWK_SLEEP)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (this->state!=AI_HAWK_GLIDE && !this->enemy)
	       setState((SpriteObject *)this,AI_HAWK_GLIDE);
	   }
	if ((aicount&0x3f)==(this->aiSlot&0x3f) &&
	    (this->state==AI_HAWK_GLIDE))
	   {if (!this->enemy)
	       this->enemy=findPlayer(this->sprite,F(1800));
	    if (this->enemy)
	       {if ((level_sector[this->enemy->sprite->s].flags&SECFLAG_WATER)
		    || (this->enemy==player && invisibleCounter))
		   {this->enemy=NULL;
		    break;
		   }
		setState((SpriteObject *)this,AI_HAWK_DIVE);
		spriteObject_makeSound((SpriteObject *)this,0);
		this->diveTimer=0;
		PlotCourseToObject((SpriteObject *)this,
				   (SpriteObject *)this->enemy);
		this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<2;
		this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<2;
		break;
	       }
	   }
	switch (this->state)
	   {case AI_HAWK_DIVE:
	       if (this->diveTimer++>90)
		  setState((SpriteObject *)this,AI_HAWK_CHASE);

	    case AI_HAWK_CHASE:
	       if ((aicount&0x07)==(this->aiSlot&0x07))
		  {if (level_sector[this->enemy->sprite->s].flags &
		       SECFLAG_WATER)
		      setState((SpriteObject *)this,AI_HAWK_SLEEP);
		   this->jinkTimer+=F(2);
		   if (this->jinkTimer>F(10))
		      this->jinkTimer-=F(20);
		   PlotCourseToObject((SpriteObject *)this,
				      (SpriteObject *)this->enemy);
		   this->sprite->angle=normalizeAngle(this->sprite->angle+
						      this->jinkTimer);
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<4;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<4;
		  }
	       this->sprite->vel.y=
		  (F(10)+this->enemy->sprite->pos.y-this->sprite->pos.y)>>4;
	       if ((collide & COLLIDE_SPRITE) &&
		   sprites[collide&0xffff].owner==(Object *)this->enemy)
		  {assert(this->enemy->class!=CLASS_DEAD);
		   signalObject((Object *)this->enemy,SIGNAL_HURT,20,
				(int)this);
		   this->sprite->angle=
		      normalizeAngle(this->sprite->angle+F(180));
		   setState((SpriteObject *)this,AI_HAWK_RETREAT);
		   this->diveTimer=0;
		  }
	       break;
	    case AI_HAWK_GLIDE:
	       if ((aicount&0x07)==(this->aiSlot&0x07))
		  {this->sprite->angle=
		      normalizeAngle(this->sprite->angle+F(12));
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<3;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<3;
		  }
	       break;
	    case AI_HAWK_RETREAT:
	       if ((aicount&0x03)==(this->aiSlot&0x03))
		  {this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<3;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<3;
		   this->sprite->vel.y=6<<15;
		  }
	       if (this->diveTimer++>90)
		  setState((SpriteObject *)this,AI_HAWK_GLIDE);
	       break;
	      }
	break;
       }
}

Object *constructHawk(int sector)
{HawkObject *this=(HawkObject *)getFreeObject(hawk_func,OT_HAWK,CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(24),65536*0.95,0,
			0,SPRITEFLAG_BWATER,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sprite->scale=65000;
 this->sequenceMap=hawkSeqMap;
 setState((SpriteObject *)this,AI_HAWK_SLEEP);
 this->health=10;
 this->enemy=NULL;
 this->routePos=-1;
 this->aiSlot=nextAiSlot++;
 this->diveTimer=0;
 this->jinkTimer=0;
 return (Object *)this;
}


/********************************\
 *          WASP STUFF          *
\********************************/

enum {AI_WASP_IDLE,AI_WASP_HOVER,AI_WASP_DART,AI_WASP_BITE,AI_WASP_SEEK,
	 AI_WASP_NMSTATES};

unsigned short waspSeqMap[]={HB|0,HB|0,HB|0,HB|9,HB|0};

void wasp_func(Object *_this,int message,int param1,int param2)
{WaspObject *this=(WaspObject *)_this;
 int collide=0,fflags=0;
 switch (message)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeZorch((SpriteObject *)this);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_AIRGUTS,48000,0,0);
	    spriteObject_makeSound((SpriteObject *)this,0);
	    makeExplosion((MonsterObject *)this,17,5);
	    makeExplosion((MonsterObject *)this,17,5);
	    delayKill(_this);
	   }
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_WASP_IDLE)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (!this->enemy)
	       setState((SpriteObject *)this,AI_WASP_IDLE);
	   }
	if (this->enemy && this->state!=AI_WASP_SEEK &&
	    (this->aiSlot&0x07)==(aicount&0x07))
	   if (this->sprite->pos.y<this->enemy->sprite->pos.y)
	      this->sprite->vel.y+=(1<<16)-(this->sprite->vel.y>>4);
	   else
	      this->sprite->vel.y+=(-1<<16)-(this->sprite->vel.y>>4);

	switch (this->state)
	   {case AI_WASP_IDLE:
	       if (AISLOT(0x1f))
		  {if (!this->enemy)
		      this->enemy=findPlayer(this->sprite,F(1800));
		   if (this->enemy)
		      {setState((SpriteObject *)this,AI_WASP_HOVER);
		       this->hoverCounter=3;
		      }
		  }
	       break;
	    case AI_WASP_HOVER:
	       this->hoverCounter--;
	       if (this->hoverCounter>0)
		  break;
	       this->hoverCounter=10;
	       assert(this->enemy);
	       switch (decideWhatToDo((MonsterObject *)this,1,1))
		  {case DO_GOIDLE:
		      setState((SpriteObject *)this,AI_WASP_IDLE);
		      this->enemy=NULL;
		      return;
		   case DO_FOLLOWROUTE:
		      this->sprite->vel.x=0;
		      this->sprite->vel.z=0;
		      setState((SpriteObject *)this,AI_WASP_SEEK);
		      return;
		   case DO_CHARGE:
		      PlotCourseToObject((SpriteObject *)this,
					 (SpriteObject *)this->enemy);
		      this->sprite->angle=
			 normalizeAngle(this->sprite->angle+randomAngle(5));
		      break;
		   case DO_RANDOMWANDER:
		      this->sprite->angle=((short)getNextRand())*360;
		      break;
		     }
	       setState((SpriteObject *)this,AI_WASP_DART);
	       this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<4;
	       this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<4;
	       this->dartCounter=20;
	       break;
	    case AI_WASP_DART:
	       if (collide & COLLIDE_SPRITE)
		  if (sprites[collide&0xffff].owner==(Object *)this->enemy)
		     {/* bite them! */
		      setState((SpriteObject *)this,AI_WASP_BITE);
		      PlotCourseToObject((SpriteObject *)this,
					 (SpriteObject *)this->enemy);
		      this->sprite->vel.x=0;
		      this->sprite->vel.z=0;
		     }
	       if (collide & COLLIDE_WALL)
		  {this->sprite->angle=
		      normalizeAngle(this->sprite->angle+/*F(180)+*/
				     randomAngle(8));
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<2;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<2;
		  }
	       this->dartCounter--;
	       if (this->dartCounter>0)
		  break;
	       setState((SpriteObject *)this,AI_WASP_HOVER);
	       this->sprite->vel.x=0;
	       this->sprite->vel.z=0;
	       this->hoverCounter=getNextRand()&0x1f;
	       break;
	    case AI_WASP_BITE:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {if (this->enemy)
		      {assert(this->enemy->class!=CLASS_DEAD);
		       signalObject((Object *)this->enemy,SIGNAL_HURT,20,
				    (int)this);
		       this->sprite->vel.x=-MTH_Cos(this->sprite->angle)<<4;
		       this->sprite->vel.z=-MTH_Sin(this->sprite->angle)<<4;
		       setState((SpriteObject *)this,AI_WASP_DART);
		       this->dartCounter=6;
		      }
		  else
		      setState((SpriteObject *)this,AI_WASP_HOVER);
		  }
	       break;
	    case AI_WASP_SEEK:
	       if ((this->aiSlot&0x0f)==(aicount&0x0f) &&
		   monster_seekEnemy((MonsterObject *)this,1,4))
		  {setState((SpriteObject *)this,AI_WASP_HOVER);
		   this->sprite->vel.x=0;
		   this->sprite->vel.y=0;
		   this->sprite->vel.z=0;
		  }
	       break;
	      }
	break;
       }
}

Object *constructWasp(int sector)
{WaspObject *this=(WaspObject *)getFreeObject(wasp_func,OT_WASP,CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(24),65536*0.95,0,
			0,SPRITEFLAG_BWATER,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sprite->scale=50000;
 this->sequenceMap=waspSeqMap;
 setState((SpriteObject *)this,AI_WASP_IDLE);
 this->health=60;
 this->enemy=NULL;
 this->routePos=-1;
 this->aiSlot=nextAiSlot++;
 this->hoverCounter=10;
 return (Object *)this;
}


/********************************\
 *          ANUBALL STUFF       *
\********************************/
void anuball_func(Object *_this,int msg,int param1,int param2)
{ProjectileObject *this=(ProjectileObject *)_this;
 int collide,fflags;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    {removeLight(this->sprite);
	     freeSprite(this->sprite);
	    }
	 break;
	}
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	fflags=spriteAdvanceFrame(this->sprite);
	if (!this->state)
	   {collide=moveSprite(this->sprite);
	    if (collide & COLLIDE_SPRITE &&
		sprites[collide&0xffff].owner!=this->owner)
	       {assert(sprites[collide&0xffff].owner->class!=CLASS_DEAD);
		signalObject(sprites[collide&0xffff].owner,
			     SIGNAL_HURT,20,
			     (int)this);
		setState((SpriteObject *)this,1);
		changeLightColor(this->sprite,5,5,0);
	       }
	    if (collide & (COLLIDE_WALL|COLLIDE_FLOOR|COLLIDE_CEILING))
	       setState((SpriteObject *)this,1);
	   }
	else
	   {changeLightColor(this->sprite,
			     5+this->sprite->frame*2,
			     5+this->sprite->frame*2,
			     0+this->sprite->frame*2);
	    if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
	       delayKill(_this);
	   }
	break;
       }
}



unsigned short anuballSequenceMap[]={HB|0,8};

Object *constructAnuball(int sector,MthXyz *pos,MthXyz *vel,
			 SpriteObject *owner,int heading)
{ProjectileObject *this=(ProjectileObject *)
    getFreeObject(anuball_func,OT_ANUBALL,CLASS_PROJECTILE);
 assert(sizeof(*this)<sizeof(Object));
 if (!this)
    return NULL;
 assert(this);
 assert(level_sequenceMap[OT_ANUBALL]>=0);
 assert(level_sequenceMap[OT_ANUBALL]<1000);
 this->sprite=newSprite(sector,F(16),F(1),0,
			level_sequenceMap[OT_ANUBALL],
			SPRITEFLAG_IMATERIAL,(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos=*pos;
 this->sprite->vel=*vel;
 this->sprite->angle=heading;
 this->sequenceMap=anuballSequenceMap;
 setState((SpriteObject *)this,0);
 this->owner=(Object *)owner;
 addLight(this->sprite,16,16,0);
 return (Object *)this;
}


/********************************\
 *           RINGO STUFF        *
\********************************/
void ringo_func(Object *_this,int msg,int param1,int param2)
{ProjectileObject *this=(ProjectileObject *)_this;
 int collide,fflags;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	fflags=spriteAdvanceFrame(this->sprite);
	collide=moveSprite(this->sprite);
	/* if (collide & (COLLIDE_WALL|COLLIDE_FLOOR|COLLIDE_CEILING))
	   {this->age++;
	   if (this->age>6)
	   delayKill(_this);
	   } */
	if (collide & COLLIDE_SPRITE &&
	    sprites[collide&0xffff].owner!=this->owner)
	   {int hurtAmount;
	    assert(sprites[collide&0xffff].owner->class!=CLASS_DEAD);
	    if (weaponPowerUpCounter && this->owner==(Object *)player)
	       hurtAmount=40;
	    else
	       hurtAmount=20;
	    signalObject(sprites[collide&0xffff].owner,
			 SIGNAL_HURT,hurtAmount,
			 (int)this);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_POOF,F(1),RGB(31,8,8),0);
	    delayKill(_this);
	    break;
	   }
	this->age++;
	if (this->age>=60)
	   {constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_POOF,F(1),RGB(31,8,8),0);
	    delayKill(_this);
	    break;
	   }
	break;
       }
}

unsigned short ringoSequenceMap[]={0};

Object *constructRingo(int sector,MthXyz *pos,MthXyz *vel,
		       SpriteObject *owner)
{ProjectileObject *this=(ProjectileObject *)
    getFreeObject(ringo_func,OT_RINGO,CLASS_PROJECTILE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 assert(level_sequenceMap[OT_RINGO]>=0);
 assert(level_sequenceMap[OT_RINGO]<1000);
 this->sprite=newSprite(sector,F(16),F(1),GRAVITY<<2,
			level_sequenceMap[OT_RINGO],
			SPRITEFLAG_BOUNCY|SPRITEFLAG_IMATERIAL,(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos=*pos;
 this->sprite->vel=*vel;
#if SPEEDOPTIMIZE
 this->sprite->maxSpeed=10;
#endif
 if (weaponPowerUpCounter && owner==(SpriteObject *)player)
    this->sprite->scale=80000;
 this->sequenceMap=ringoSequenceMap;
 setState((SpriteObject *)this,0);
 this->owner=(Object *)owner;
 this->age=getNextRand()&0xf;
 return (Object *)this;
}


/********************************\
 *        FLAMEBALL STUFF       *
\********************************/
#define MAXFLAMEBALLAGE 50
static ProjectileObject *flameList[MAXFLAMEBALLAGE];
static int flameHead;
void initFlames(void)
{int i;
 for (i=0;i<MAXFLAMEBALLAGE;i++)
    flameList[i]=NULL;
 flameHead=0;
}

void translateFlames(int dx,int dy,int dz)
{int i;
 for (i=0;i<MAXFLAMEBALLAGE;i++)
    if (flameList[i] && flameList[i]->state<2)
       {flameList[i]->sprite->pos.x+=dx;
	flameList[i]->sprite->pos.y+=dy;
	flameList[i]->sprite->pos.z+=dz;
       }
}

void rotateFlames(int angle,int cx,int cz)
{int i,x,z;
 int C,S;
 for (i=0;i<MAXFLAMEBALLAGE;i++)
    if (flameList[i] && flameList[i]->state<2)
       {C=MTH_Cos(angle);
	S=MTH_Sin(angle);

	x=flameList[i]->sprite->pos.x-cx;
	z=flameList[i]->sprite->pos.z-cz;

	flameList[i]->sprite->pos.x=
	   MTH_Mul(C,x)-MTH_Mul(S,z)+cx;
	flameList[i]->sprite->pos.z=
	   MTH_Mul(C,z)+MTH_Mul(S,x)+cz;

	x=flameList[i]->sprite->vel.x;
	z=flameList[i]->sprite->vel.z;
	flameList[i]->sprite->vel.x=
	   MTH_Mul(C,x)-MTH_Mul(S,z);
	flameList[i]->sprite->vel.z=
	   MTH_Mul(C,z)+MTH_Mul(S,x);

       }
}


#define FLAMELEN 30
static unsigned short flameColor[FLAMELEN];
static int flameInit=0;

static void initFlameColor(void)
{const char colors[3][3]=
    {{15,15,31},
     {31,31,0},
     {15,0,0}};
 const int slideLen[]={5,20,0};
 int i,c,count;
 int r,g,b;
 Fixed32 frac1,frac2;

 flameInit=1;
 count=0;
 for (c=0;slideLen[c];c++)
    {for (i=0;i<slideLen[c];i++)
	{frac1=MTH_Div(i,slideLen[c]);
	 frac2=F(1)-frac1;
	 r=MTH_Mul(colors[c][0],frac2)+MTH_Mul(colors[c+1][0],frac1);
	 g=MTH_Mul(colors[c][1],frac2)+MTH_Mul(colors[c+1][1],frac1);
	 b=MTH_Mul(colors[c][2],frac2)+MTH_Mul(colors[c+1][2],frac1);
	 flameColor[count++]=RGB(r,g,b);
	}
    }
}

void flameball_func(Object *_this,int msg,int param1,int param2)
{ProjectileObject *this=(ProjectileObject *)_this;
 int collide,i;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    {freeSprite(this->sprite);
	     for (i=0;i<MAXFLAMEBALLAGE;i++)
		if (flameList[i]==this)
		   break;
	     assert(i<MAXFLAMEBALLAGE-1);
	     flameList[i]=NULL;
	    }
	 break;
	}
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	this->sprite->color=flameColor[this->age];
	break;
     case SIGNAL_MOVE:
	this->age++;
	if (this->age==2)
	   this->sprite->flags&=~SPRITEFLAG_INVISIBLE;
	if (this->age>=2)
	   this->sprite->scale+=5000-((this->age-2)*100);
	if (this->age==7)
	   {setState((SpriteObject *)this,1);
	    this->sprite->frame=aicount&1;
	   }
	collide=moveSprite(this->sprite);

	if ((collide & COLLIDE_SPRITE) &&
	    sprites[collide&0xffff].owner!=this->owner)
	   {assert((collide&0xffff)<500);
	    assert(sprites[collide&0xffff].owner);
	    this->sprite->pos.x+=(MTH_GetRand()&0xfffff)-F(8);
	    this->sprite->pos.y+=(MTH_GetRand()&0xfffff)-F(8);
	    this->sprite->pos.z+=(MTH_GetRand()&0xfffff)-F(8);
	    assert(sprites[collide&0xffff].owner->class!=CLASS_DEAD);
	    signalObject(sprites[collide&0xffff].owner,
			 SIGNAL_HURT,weaponPowerUpCounter?8:4,
			 (int)this);
	    this->sprite->flags&=~SPRITEFLAG_INVISIBLE;
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_FLAMEWALL,40000,0,0);
	    delayKill(_this);
	    break;
	   }
	if (collide & (COLLIDE_WALL|COLLIDE_FLOOR|COLLIDE_CEILING))
	   {if (level_wall[collide&0xffff].flags & WALLFLAG_PARALLAX)
	       {delayKill(_this);
		break;
	       }
	    this->sprite->flags&=~SPRITEFLAG_INVISIBLE;
	    this->sprite->pos.x+=(MTH_GetRand()&0xfffff)-F(8);
	    this->sprite->pos.y+=(MTH_GetRand()&0xfffff)-F(8);
	    this->sprite->pos.z+=(MTH_GetRand()&0xfffff)-F(8);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y+F(16),
			     this->sprite->pos.z,
			     OT_FLAMEWALL,40000,0,0);
	    delayKill(_this);
	    break;
	   }
	if (this->age>20 /* 10 */)
	   delayKill(_this);
	break;
       }
}

unsigned short flameballSequenceMap[]={0,1};

Object *constructFlameball(int sector,MthXyz *pos,MthXyz *vel,
			   SpriteObject *owner,int heading,int frame)
{ProjectileObject *this=(ProjectileObject *)
    getFreeObject(flameball_func,OT_FLAMEBALL,CLASS_PROJECTILE);
 int i;
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 assert(level_sequenceMap[OT_FLAMEBALL]>=0);
 assert(level_sequenceMap[OT_FLAMEBALL]<1000);
 if (!flameInit)
    initFlameColor();
 this->sprite=newSprite(sector,F(4),F(1),0,
			level_sequenceMap[OT_FLAMEBALL],
			SPRITEFLAG_COLORED|SPRITEFLAG_IMATERIAL|
			SPRITEFLAG_INVISIBLE,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->color=flameColor[0];
 this->sprite->scale=10000;
 this->sprite->pos=*pos;
 this->sprite->vel=*vel;
 this->sprite->angle=heading;
 this->sequenceMap=flameballSequenceMap;
 this->age=0;
 setState((SpriteObject *)this,0);
 this->owner=(Object *)owner;

 for (i=0;i<MAXFLAMEBALLAGE;i++)
    if (!flameList[i])
       break;
 assert(i<MAXFLAMEBALLAGE-1);
 flameList[i]=this;

 return (Object *)this;
}

/********************************\
 *         GRENADE STUFF        *
\********************************/

void spriteObject_makeGrenadeExplosion(SpriteObject *this)
{if (this->sprite->flags & SPRITEFLAG_UNDERWATER)
    {constructOneShot(this->sprite->s,
		      this->sprite->pos.x,
		      this->sprite->pos.y+F(30),
		      this->sprite->pos.z,
		      OT_GRENBUBB,F(1),0,0);
     {int vol,pan,oct;
      struct soundSlotRegister ssr;

      posGetSoundParams(&this->sprite->pos,&vol,&pan);
      initSoundRegs(level_objectSoundMap[OT_GRENBUBB],vol,pan,
		    &ssr);
      oct=(ssr.reg[8]-0x0800)&0xf800;
      ssr.reg[8]=(ssr.reg[8]&~(0xf800))|oct;
      ssr.reg[8]|=0x100;
      playSoundMegaE((int)this,&ssr);
     }
    }
else
   constructOneShot(this->sprite->s,
		    this->sprite->pos.x,
		    this->sprite->pos.y+F(30),
		    this->sprite->pos.z,
		    OT_GRENPOW,F(2),0,0);
}



void grenade_func(Object *_this,int msg,int param1,int param2)
{ProjectileObject *this=(ProjectileObject *)_this;
 int collide,fflags;
 static int lightP[]={0,F(25),0,0,
		      F(5),F(25),0,0,
		      F(25),F(25),0,0};
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	fflags=spriteAdvanceFrame(this->sprite);
	collide=moveSprite(this->sprite);
	if (collide)
	   {if (collide & COLLIDE_SPRITE)
	       {if (sprites[collide&0xffff].owner==this->owner &&
		    !(collide & COLLIDE_WALL))
		   break;
	       }
	    else
	       {if ((collide & (COLLIDE_WALL|COLLIDE_FLOOR|COLLIDE_CEILING)) &&
		    (level_wall[collide&0xffff].flags & WALLFLAG_PARALLAX))
		   {delayKill(_this);
		    break;
		   }
		/* check for collisions with exploding walls */
		if ((collide & (COLLIDE_WALL|COLLIDE_FLOOR|COLLIDE_CEILING)) &&
		    (level_wall[collide&0xffff].flags & WALLFLAG_EXPLODABLE))
		   {explodeAllMaskedWallsInSector(findWallsSector(collide&
								  0xffff));
		   }
	       }
	    spriteObject_makeGrenadeExplosion((SpriteObject *)this);
	    constructLight(this->sprite->s,
			   this->sprite->pos.x,
			   this->sprite->pos.y+F(30),
			   this->sprite->pos.z,
			   lightP,F(1)/16);
	    radialDamage((Object *)this,&this->sprite->pos,this->sprite->s,
			 180,F(300));
	    delayKill(_this);
	   }
	break;
       }
}

unsigned short grenadeSequenceMap[]={0,1};

Object *constructGrenade(int sector,MthXyz *pos,MthXyz *vel,
			 SpriteObject *owner)
{ProjectileObject *this=(ProjectileObject *)
    getFreeObject(grenade_func,OT_GRENADE,CLASS_PROJECTILE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 assert(level_sequenceMap[OT_GRENADE]>=0);
 assert(level_sequenceMap[OT_GRENADE]<1000);
 this->sprite=newSprite(sector,F(4),F(1),GRAVITY<<2,
			level_sequenceMap[OT_GRENADE],
			SPRITEFLAG_IMATERIAL,(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->scale=15000;
 this->sprite->pos=*pos;
 this->sprite->vel=*vel;
 this->sequenceMap=grenadeSequenceMap;
 setState((SpriteObject *)this,0);
 this->owner=(Object *)owner;
 return (Object *)this;
}

/********************************\
 *       MAGMANTIS STUFF        *
\********************************/

enum  {AI_MAGMANTIS_IDLE,AI_MAGMANTIS_LURK,AI_MAGMANTIS_TAIL,
	  AI_MAGMANTIS_UP,AI_MAGMANTIS_FIRE,
	  AI_MAGMANTIS_DOWN,AI_MAGMANTIS_SPLASH,
	  AI_MAGMANTIS_NMSEQ};

unsigned short magmantisSeqMap[]={0,0,0,
				  HB|1,HB|9,
				  HB|25,33};

void magmantis_func(Object *_this,int message,int param1,int param2)
{MagmantisObject *this=(MagmantisObject *)_this;
 int collide;
 int fflags;
 collide=0;
 fflags=0;
 switch (message)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeSound((SpriteObject *)this,0);
	    {int i;
	     for (i=0;i<20;i++)
		{MthXyz vel;
		 vel.x=(MTH_GetRand()&0xfffff)-F(8);
		 vel.z=(MTH_GetRand()&0xfffff)-F(8);
		 vel.y=F(20)+(MTH_GetRand()&0x3ffff);
		 constructRingo(this->sprite->s,&this->sprite->pos,
				&vel,(SpriteObject *)this);
		}
	     for (i=0;i<5;i++)
		{MthXyz pos;
		 pos=this->sprite->pos;
		 pos.x+=((short)getNextRand())<<5;
		 pos.z+=((short)getNextRand())<<5;
		 pos.y+=(((short)getNextRand())<<8)+F(40);
		 constructOneShot(this->sprite->s,
				  pos.x,pos.y,pos.z,
				  OT_GRENPOW,F(2),0,0);
		}
	    }
	    delayKill(_this);
	   }
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_MAGMANTIS_IDLE)
	   {collide=moveSprite(this->sprite);
	    if (!(this->sprite->flags & SPRITEFLAG_INVISIBLE))
	       fflags=spriteAdvanceFrame(this->sprite);
	   }
	switch (this->state)
	   {case AI_MAGMANTIS_IDLE:
	       if ((this->aiSlot&0x1f)==(aicount&0x1f))
		  {this->sprite->vel.x=0;
		   this->sprite->vel.z=0;
		   if (!this->enemy)
		      this->enemy=findPlayer(this->sprite,F(800));
		   if (this->enemy)
		      {setState((SpriteObject *)this,AI_MAGMANTIS_LURK);
		       PlotCourseToObject((SpriteObject *)this,
					  (SpriteObject *)this->enemy);
		       this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<2;
		       this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<2;
		      }
		  }
	       break;
	    case AI_MAGMANTIS_LURK:
	       if ((this->aiSlot&0x1f)==(aicount&0x1f))
		  {if (spriteDistApprox(this->sprite,
					this->enemy->sprite)>F(1600))
		      {setState((SpriteObject *)this,AI_MAGMANTIS_IDLE);
		       this->enemy=NULL;
		       return;
		      }
		   PlotCourseToObject((SpriteObject *)this,
				      (SpriteObject *)this->enemy);
		   if (this->rarCount>0)
		      {this->rarCount--;
		       this->sprite->vel.x=0;
		       this->sprite->vel.z=0;
		       this->sprite->flags&=
			  ~(SPRITEFLAG_INVISIBLE|SPRITEFLAG_IMATERIAL);
		       this->sprite->flags|=SPRITEFLAG_NOSHADOW;
		       setState((SpriteObject *)this,AI_MAGMANTIS_UP);
		       this->tailInhibit=2;
		       break;
		      }
		   this->tailInhibit--;
		   if (this->tailInhibit<=0)
		      {this->tailInhibit=4;
		       this->sprite->vel.x=0;
		       this->sprite->vel.z=0;
		       this->sprite->flags&=
			  ~(SPRITEFLAG_INVISIBLE|SPRITEFLAG_IMATERIAL);
		       this->sprite->flags|=SPRITEFLAG_NOSHADOW;
		       setState((SpriteObject *)this,AI_MAGMANTIS_TAIL);
		       break;
		      }
		   this->angry--;
		   if (this->angry<=0)
		      {this->angry=2;
		       this->rarCount=(getNextRand()&7)+1;
		      }
		   this->sprite->angle=
		      normalizeAngle(this->sprite->angle+randomAngle(7));
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<2;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<2;
		  }
	       break;
	    case AI_MAGMANTIS_SPLASH:
	    case AI_MAGMANTIS_TAIL:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {setState((SpriteObject *)this,AI_MAGMANTIS_LURK);
		   this->sprite->flags|=
		      SPRITEFLAG_INVISIBLE|SPRITEFLAG_IMATERIAL;
		   PlotCourseToObject((SpriteObject *)this,
				      (SpriteObject *)this->enemy);
		   this->sprite->angle=
		      normalizeAngle(this->sprite->angle+randomAngle(7));
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<2;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<2;
		  }
	       break;
	    case AI_MAGMANTIS_UP:
	       this->sprite->vel.x=0;
	       this->sprite->vel.y=0;
	       this->sprite->vel.z=0;

	       spriteHome((SpriteObject *)this,
			  (SpriteObject *)this->enemy,
			  F(10),F(2));
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {setState((SpriteObject *)this,AI_MAGMANTIS_FIRE);
		  }
	       break;
	    case AI_MAGMANTIS_FIRE:
	       this->sprite->vel.x=0;
	       this->sprite->vel.y=0;
	       this->sprite->vel.z=0;
	       spriteHome((SpriteObject *)this,
			  (SpriteObject *)this->enemy,
			  F(10),F(2));
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {if (this->enemy && canSee(this->sprite,this->enemy->sprite))
		      {MthXyz ballPos;
		       MthXyz ballVel;
		       initProjectile(&this->sprite->pos,
				      &this->enemy->sprite->pos,
				      &ballPos,&ballVel,F(90),4);
		       constructGenproj(this->sprite->s,&ballPos,&ballVel,
					(SpriteObject *)this,
					getAngle(ballVel.x,ballVel.z),
					1,OT_MAGBALL,50,RGB(15,5,5),
					0,0,0,0);
		      }
		   if (this->rarCount-->0)
		      setState((SpriteObject *)this,AI_MAGMANTIS_FIRE);
		   else
		      setState((SpriteObject *)this,AI_MAGMANTIS_DOWN);
		  }
	       break;
	    case AI_MAGMANTIS_DOWN:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_MAGMANTIS_SPLASH);
	       break;
	      }
	break;
       }
}

Object *constructMagmantis(int sector)
{MagmantisObject *this=(MagmantisObject *)getFreeObject(magmantis_func,
							OT_MAGMANTIS,
							CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(32),F(1),GRAVITY<<1,
			0,SPRITEFLAG_BWATERBNDRY|SPRITEFLAG_IMATERIAL|
			SPRITEFLAG_INVISIBLE|SPRITEFLAG_FOOTCLIP|
			SPRITEFLAG_NOSHADOW,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sprite->scale=80000;
 this->sequenceMap=magmantisSeqMap;
 setState((SpriteObject *)this,AI_MAGMANTIS_IDLE);
 this->health=400;
 this->enemy=NULL;
 this->routePos=-1;
 this->aiSlot=nextAiSlot++;
 this->tailInhibit=0;
 this->rarCount=0;
 this->angry=5;
 return (Object *)this;
}

/********************************\
 *          ANUBIS STUFF        *
\********************************/

enum  {AI_ANUBIS_IDLE=STATE_IDLE,
	  AI_ANUBIS_WALK=STATE_WALK,
	  AI_ANUBIS_CLAW=STATE_SRA,
	  AI_ANUBIS_THROW=STATE_LRA,
	  AI_ANUBIS_HIT=STATE_HIT,
	  AI_ANUBIS_SEEK=STATE_SEEK,
	  AI_ANUBIS_NMSEQ};

unsigned short anubisSeqMap[]={HB|0, HB|8, HB|16, HB|24,
				  HB|32, HB|8};

void anubis_func(Object *_this,int message,int param1,int param2)
{AnubisObject *this=(AnubisObject *)_this;
 int collide;
 int fflags;
 collide=0;
 fflags=0;
 switch (message)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeZorch((SpriteObject *)this);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_LANDGUTS,48000,0,0);
	    makeExplosion((MonsterObject *)this,40,4);
	    makeExplosion((MonsterObject *)this,40,4);
	    spriteObject_makeSound((SpriteObject *)this,1);
	    delayKill(_this);
	   }
	else
	   if (!this->stunCounter)
	      {setState((SpriteObject *)this,AI_ANUBIS_HIT);
	       this->stunCounter=20;
	      }
	this->sprite->vel.x=0;
	this->sprite->vel.z=0;
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_ANUBIS_IDLE)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (!this->enemy)
	       setState((SpriteObject *)this,AI_ANUBIS_IDLE);
	   }
	if (this->stunCounter)
	   {this->stunCounter--;
	    /*break;*/
	   }
	switch (this->state)
	   {case AI_ANUBIS_HIT:
	       if (fflags && FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_ANUBIS_WALK);
	       break;
	    case AI_ANUBIS_IDLE:
	       normalMonster_idle((MonsterObject *)this,2,0);
	       break;
	    case AI_ANUBIS_CLAW:
	       this->sprite->vel.x=0;
	       this->sprite->vel.z=0;
	       if (this->sprite->frame==5 || this->sprite->frame==18)
		  {if (spriteDistApprox(this->sprite,
					this->enemy->sprite)<F(150))
		      {PlotCourseToObject((SpriteObject *)this,
					  (SpriteObject *)this->enemy);
		       assert(this->enemy->class!=CLASS_DEAD);
		       signalObject((Object *)this->enemy,SIGNAL_HURT,20,
				    (int)this);
		      }
		   else
		      {setState((SpriteObject *)this,AI_ANUBIS_WALK);
		      }
		  }
	       break;
	    case AI_ANUBIS_THROW:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_ANUBIS_WALK);
	       if (this->sprite->frame==9)
		  {if (canSee(this->sprite,this->enemy->sprite))
		      {MthXyz ballPos;
		       MthXyz ballVel;
		       initProjectile(&this->sprite->pos,
				      &this->enemy->sprite->pos,
				      &ballPos,&ballVel,F(46),4);
		       constructAnuball(this->sprite->s,&ballPos,&ballVel,
					(SpriteObject *)this,
					getAngle(ballVel.x,ballVel.z));
		      }
		  }
	       break;
	    case AI_ANUBIS_WALK:
	       normalMonster_walking((MonsterObject *)this,
				     collide,fflags,2);
	       break;
	    case AI_ANUBIS_SEEK:
	       if ((this->aiSlot&0x0f)==(aicount&0x0f) &&
		   monster_seekEnemy((MonsterObject *)this,0,2))
		  setState((SpriteObject *)this,AI_ANUBIS_WALK);
	       break;
	      }
	break;
       }
}

Object *constructAnubis(int sector)
{AnubisObject *this=(AnubisObject *)getFreeObject(anubis_func,OT_ANUBIS,
						  CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(32),F(1),GRAVITY<<1,
			0,SPRITEFLAG_BWATERBNDRY|SPRITEFLAG_BCLIFF,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sequenceMap=anubisSeqMap;
 setState((SpriteObject *)this,AI_ANUBIS_IDLE);
 this->health=100;
 this->enemy=NULL;
 this->routePos=-1;
 this->aiSlot=nextAiSlot++;
 this->stunCounter=0;
 return (Object *)this;
}

/********************************\
 *          SELKIS STUFF        *
\********************************/

enum  {AI_SELKIS_IDLE=STATE_IDLE,AI_SELKIS_WALK=STATE_WALK,
	  AI_SELKIS_CLAW=STATE_SRA,AI_SELKIS_THROW=STATE_LRA,
	  AI_SELKIS_HIT=STATE_HIT,AI_SELKIS_SEEK=STATE_SEEK,

	  AI_SELKIS_SPARK1,AI_SELKIS_SPARK2,
	  AI_SELKIS_DIEING,AI_SELKIS_DEAD,
	  AI_SELKIS_ICU,AI_SELKIS_CHARGE,
	  AI_SELKIS_NMSEQ};

unsigned short selkisSeqMap[]={HB|0, HB|8,
				  HB|25, HB|16,
				  HB|33, HB|8,

				  42,43,
				  44,45,
				  41,HB|8,
				 };

void selkis_func(Object *_this,int message,int param1,int param2)
{SelkisObject *this=(SelkisObject *)_this;
 int collide;
 int fflags;
 collide=0;
 fflags=0;
 assert(AI_SELKIS_IDLE==0 &&
	AI_SELKIS_WALK==1 &&
	AI_SELKIS_CLAW==2 &&
	AI_SELKIS_THROW==3 &&
	AI_SELKIS_HIT==4);
 switch (message)
    {case SIGNAL_HURT:
	if (this->state==AI_SELKIS_DIEING || this->state==AI_SELKIS_DEAD)
	   break;
	this->health-=param1;
	this->sprite->flags|=SPRITEFLAG_FLASH;
	if (this->health<=0)
	   {setState((SpriteObject *)this,AI_SELKIS_DIEING);
	    this->sparkTimer=0;
	    currentState.gameFlags|=GAMEFLAG_KILLEDSELKIS;
	   }
	else
	   if (!this->stunCounter && this->state==AI_SELKIS_WALK)
	      {setState((SpriteObject *)this,AI_SELKIS_HIT);
	       this->stunCounter=30*4;
	      }
	this->sprite->vel.x=0;
	this->sprite->vel.z=0;
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_SELKIS_IDLE)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (!this->enemy)
	       setState((SpriteObject *)this,AI_SELKIS_IDLE);
	   }
	if (this->stunCounter)
	   this->stunCounter--;
	switch (this->state)
	   {case AI_SELKIS_HIT:
	       if (fflags && FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_SELKIS_WALK);
	       break;
	    case AI_SELKIS_ICU:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_SELKIS_WALK);
	       break;
	    case AI_SELKIS_IDLE:
	       normalMonster_idle((MonsterObject *)this,2,-1);
	       if (this->enemy)
		  {setState((SpriteObject *)this,AI_SELKIS_ICU);
		   this->sprite->vel.x=0;
		   this->sprite->vel.z=0;
		  }
	       break;
	    case AI_SELKIS_CLAW:
	       this->sprite->vel.x=0;
	       this->sprite->vel.z=0;
	       if (this->sprite->frame==5 || this->sprite->frame==18)
		  {if (spriteDistApprox(this->sprite,
					this->enemy->sprite)<F(150))
		      {PlotCourseToObject((SpriteObject *)this,
					  (SpriteObject *)this->enemy);
		       assert(this->enemy->class!=CLASS_DEAD);
		       signalObject((Object *)this->enemy,SIGNAL_HURT,20,
				    (int)this);
		      }
		   else
		      {setState((SpriteObject *)this,AI_SELKIS_WALK);
		      }
		  }
	       break;
	    case AI_SELKIS_THROW:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_SELKIS_WALK);
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {if (canSee(this->sprite,this->enemy->sprite))
		      {MthXyz ballPos;
		       MthXyz ballVel;
		       initProjectile(&this->sprite->pos,
				      &this->enemy->sprite->pos,
				      &ballPos,&ballVel,F(46),4);
		       /* constructAnuball(this->sprite->s,&ballPos,&ballVel,
					(SpriteObject *)this,
					getAngle(ballVel.x,ballVel.z)); */
		       constructCobra(this->sprite->s,ballPos.x,ballPos.y,
				      ballPos.z,getAngle(ballVel.x,ballVel.z),
				      0,(SpriteObject *)this,OT_MUMBALL,1);
		      }
		  }
	       break;
	    case AI_SELKIS_WALK:
	       if ((this->aiSlot&0x1f)==(aicount&0x1f))
		  {this->sparkTimer=0;
		   if (this->health<500 && getNextRand()<10000)
		      {setState((SpriteObject *)this,AI_SELKIS_SPARK1);
		       this->sprite->vel.x=0; this->sprite->vel.z=0;
		       break;
		      }
		   if (this->health<250 && getNextRand()<20000)
		      {setState((SpriteObject *)this,AI_SELKIS_SPARK2);
		       this->sprite->vel.x=0; this->sprite->vel.z=0;
		       break;
		      }
		  }
	       normalMonster_walking((MonsterObject *)this,
				     collide,fflags,3);
	       if (this->state==AI_SELKIS_IDLE)
		  {this->state=AI_SELKIS_WALK;
		   this->enemy=(MonsterObject *)player;
		   break;
		  }
	       if (this->health<1000 && this->state==AI_SELKIS_THROW)
		  {PlotCourseToObject((SpriteObject *)this,
				      (SpriteObject *)this->enemy);
		   spriteObject_makeSound((SpriteObject *)this,1);
		   this->chargeCounter=0;
		   setState((SpriteObject *)this,AI_SELKIS_CHARGE);
		  }
	       break;
	    case AI_SELKIS_CHARGE:
	       spriteAdvanceFrame(this->sprite);
	       if ((collide & COLLIDE_SPRITE) &&
		   sprites[collide&0xffff].owner==(Object *)this->enemy)
		  {/* bite them! */
		   setState((SpriteObject *)this,AI_SELKIS_CLAW);
		   this->sprite->vel.x=0;
		   this->sprite->vel.z=0;
		  }
	       if (this->chargeCounter++>60)
		  {setState((SpriteObject *)this,AI_SELKIS_WALK);
		   break;
		  }
	       spriteHome((SpriteObject *)this,(SpriteObject *)this->enemy,
			  F(20),F(2));
	       this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<5;
	       this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<5;
	       break;
	    case AI_SELKIS_SPARK1:
	       if (this->sparkTimer++>30)
		  setState((SpriteObject *)this,AI_SELKIS_WALK);
	       break;
	    case AI_SELKIS_SPARK2:
	       if (this->sparkTimer++>60)
		  setState((SpriteObject *)this,AI_SELKIS_WALK);
	       break;
	    case AI_SELKIS_DIEING:
	       if (AISLOT(0x7))
		  {{MthXyz pos;
		    pos=this->sprite->pos;
		    pos.x+=((short)getNextRand())<<5;
		    pos.z+=((short)getNextRand())<<5;
		    pos.y+=(((short)getNextRand())<<6)+F(60);
		    constructOneShot(this->sprite->s,
				     pos.x,pos.y,pos.z,
				     OT_GRENPOW,F(2),0,0);
		   }
		   {MthXyz vel;
		    vel.x=(MTH_GetRand()&0xfffff)-F(8);
		    vel.z=(MTH_GetRand()&0xfffff)-F(8);
		    vel.y=F(12)+(MTH_GetRand()&0x3ffff);
		    constructSpider(this->sprite->s,0,&this->sprite->pos,
				    &vel);
		   }
		  }
	       if (this->sparkTimer++>120)
		  {setState((SpriteObject *)this,AI_SELKIS_DEAD);
		   spriteObject_makeSound((SpriteObject *)this,0);
		   signalAllObjects(SIGNAL_SWITCH,1010,0);
		  }
	       break;
	    case AI_SELKIS_DEAD:
	       this->sprite->vel.x=0;
	       this->sprite->vel.y=0;
	       this->sprite->vel.z=0;
	       break;
	    case AI_SELKIS_SEEK:
	       if ((this->aiSlot&0x0f)==(aicount&0x0f) &&
		   monster_seekEnemy((MonsterObject *)this,0,3))
		  setState((SpriteObject *)this,AI_SELKIS_WALK);
	       break;
	      }
	break;
       }
}

Object *constructSelkis(int sector)
{SelkisObject *this;
 if (currentState.gameFlags & GAMEFLAG_KILLEDSELKIS)
    {signalAllObjects(SIGNAL_SWITCH,1010,0);
     /* this only works if the monsters come after the push blocks
	in the object list */
     suckSpriteParams(NULL);
     return NULL;
    }

 this=(SelkisObject *)getFreeObject(selkis_func,OT_SELKIS,
				    CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(32),F(1),GRAVITY<<1,
			0,SPRITEFLAG_BWATERBNDRY,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sprite->scale=65536*1.2;
 this->sequenceMap=selkisSeqMap;
 setState((SpriteObject *)this,AI_SELKIS_IDLE);
 this->health=2000;
 this->enemy=NULL;
 this->routePos=-1;
 this->aiSlot=nextAiSlot++;
 this->stunCounter=0;
 this->sparkTimer=0;
 return (Object *)this;
}

/********************************\
 *           SET STUFF          *
\********************************/

enum  {AI_SET_IDLE=STATE_IDLE,AI_SET_WALK=STATE_WALK,AI_SET_CLAW=STATE_SRA,
	  AI_SET_THROW=STATE_LRA,AI_SET_HIT=STATE_HIT,AI_SET_SEEK=STATE_SEEK,
	  AI_SET_DYING,AI_SET_DEAD,AI_SET_JUMP1,AI_SET_JUMP2,AI_SET_JUMP3,
	  AI_SET_NMSEQ};

unsigned short setSeqMap[]={HB|0, HB|0, HB|8,
			    HB|40, HB|48, HB|0 ,
			    56,57,HB|16,HB|24,HB|32};

void set_func(Object *_this,int message,int param1,int param2)
{SetObject *this=(SetObject *)_this;
 int collide;
 int fflags;
 collide=0;
 fflags=0;
 switch (message)
    {case SIGNAL_HURT:
	if (this->state==AI_SET_DYING || this->state==AI_SET_DEAD)
	   break;
	this->health-=param1;
	this->sprite->flags|=SPRITEFLAG_FLASH;
	if (this->health<=0)
	   {setState((SpriteObject *)this,AI_SET_DYING);
	    currentState.gameFlags|=GAMEFLAG_KILLEDSET;
	    this->sprite->vel.x=0;
	    this->sprite->vel.z=0;
	   }
	else
	   if (!this->stunCounter && this->state==AI_SET_WALK)
	      {setState((SpriteObject *)this,AI_SET_HIT);
	       this->stunCounter=60;
	       this->sprite->vel.x=0;
	       this->sprite->vel.z=0;
	      }
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_SET_IDLE)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (!this->enemy)
	       setState((SpriteObject *)this,AI_SET_IDLE);
	   }
	if (this->stunCounter)
	   this->stunCounter--;
	switch (this->state)
	   {case AI_SET_HIT:
	       if (fflags && FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_SET_WALK);
	       break;
	    case AI_SET_IDLE:
	       normalMonster_idle((MonsterObject *)this,3,-1);
	       break;
	    case AI_SET_CLAW:
	       this->sprite->vel.x=0;
	       this->sprite->vel.z=0;
	       if (this->sprite->frame==5 || this->sprite->frame==18)
		  {if (spriteDistApprox(this->sprite,
					this->enemy->sprite)<F(150))
		      {PlotCourseToObject((SpriteObject *)this,
					  (SpriteObject *)this->enemy);
		       assert(this->enemy->class!=CLASS_DEAD);
		       signalObject((Object *)this->enemy,SIGNAL_HURT,40,
				    (int)this);
		      }
		   else
		      {setState((SpriteObject *)this,AI_SET_WALK);
		      }
		  }
	       break;
	    case AI_SET_THROW:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_SET_WALK);
	       if (this->sprite->frame==9)
		  {if (canSee(this->sprite,this->enemy->sprite))
		      {MthXyz ballPos;
		       MthXyz ballVel;
		       initProjectile(&this->sprite->pos,
				      &this->enemy->sprite->pos,
				      &ballPos,&ballVel,F(46),4);
		       constructGenproj(this->sprite->s,&ballPos,&ballVel,
					(SpriteObject *)this,
					getAngle(ballVel.x,ballVel.z),
					0,OT_SETBALL,30,RGB(15,15,15),
					0,0,0,0);
		      }
		  }
	       break;
	    case AI_SET_WALK:
	       if ((this->aiSlot&0x7f)==(aicount&0x7f) &&
		   spriteDistApprox(this->sprite,this->enemy->sprite)>F(300))
		  {PlotCourseToObject((SpriteObject *)this,
				      (SpriteObject *)this->enemy);
		   this->sprite->angle=
		      normalizeAngle(this->sprite->angle+randomAngle(6));
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<3;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<3;

		   setState((SpriteObject *)this,AI_SET_JUMP1);
		   break;
		  }
	       normalMonster_walking((MonsterObject *)this,
				     collide,fflags,3);
	       break;
	    case AI_SET_JUMP1:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {setState((SpriteObject *)this,AI_SET_JUMP2);
		   this->sprite->vel.y=F(30);
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<4;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<4;
		  }
	       break;
	    case AI_SET_JUMP2:
	       if (this->sprite->floorSector!=-1)
		  {setState((SpriteObject *)this,AI_SET_JUMP3);
		   this->sprite->vel.x=0;
		   this->sprite->vel.z=0;
		   setEarthQuake(30);
		   if (camera->floorSector!=-1)
		      stunPlayer(120);
		  }
	       break;
	    case AI_SET_JUMP3:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_SET_WALK);
	       break;
	    case AI_SET_SEEK:
	       if ((this->aiSlot&0x0f)==(aicount&0x0f) &&
		   monster_seekEnemy((MonsterObject *)this,0,2))
		  setState((SpriteObject *)this,AI_SET_WALK);
	       break;
	    case AI_SET_DYING:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {setState((SpriteObject *)this,AI_SET_DEAD);
		   signalAllObjects(SIGNAL_SWITCH,1010,0);
		  }
	       break;
	    case AI_SET_DEAD:
	       this->sprite->vel.x=0;
	       this->sprite->vel.y=0;
	       this->sprite->vel.z=0;
	       break;
	      }
	break;
       }
}

Object *constructSet(int sector)
{SetObject *this;
 if (currentState.gameFlags & GAMEFLAG_KILLEDSET)
    {signalAllObjects(SIGNAL_SWITCH,1010,0);
     /* this only works if the monsters come after the push blocks
	in the object list */
     suckSpriteParams(NULL);
     return NULL;
    }
 this=(SetObject *)getFreeObject(set_func,OT_SET,
				 CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(64),F(1),GRAVITY<<2,
			0,SPRITEFLAG_BWATERBNDRY,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sprite->scale=80000;
 this->sequenceMap=setSeqMap;
 setState((SpriteObject *)this,AI_SET_IDLE);
 this->health=1300;
 this->enemy=NULL;
 this->routePos=-1;
 this->aiSlot=nextAiSlot++;
 this->stunCounter=0;
 return (Object *)this;
}

/********************************\
 *          SENTRY BALL         *
\********************************/

void sball_func(Object *_this,int msg,int param1,int param2)
{ProjectileObject *this=(ProjectileObject *)_this;
 int collide;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	collide=moveSprite(this->sprite);
	if (collide)
	   {if (collide & COLLIDE_SPRITE)
	       {if (sprites[collide&0xffff].owner==this->owner)
		   break;
		assert(sprites[collide&0xffff].owner->class!=CLASS_DEAD);
		signalObject(sprites[collide&0xffff].owner,
			     SIGNAL_HURT,30,
			     (int)this);
	       }
	    delayKill(_this);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_POOF,F(1),RGB(20,20,0),0);
	   }
	break;
       }
}

unsigned short sballSequenceMap[]={24};

Object *constructSball(int sector,MthXyz *pos,MthXyz *vel,
		       SpriteObject *owner,int heading,int seq,
		       int scale)
{ProjectileObject *this=(ProjectileObject *)
    getFreeObject(sball_func,OT_SBALL,CLASS_PROJECTILE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 sballSequenceMap[0]=seq;
 assert(level_sequenceMap[OT_SBALL]>=0);
 assert(level_sequenceMap[OT_SBALL]<1000);
 moveObject((Object *)this,objectRunList);
 this->sprite=newSprite(sector,F(4),F(1),0,
			level_sequenceMap[OT_SBALL],
			SPRITEFLAG_IMATERIAL,(Object *)this);
 this->sprite->scale=scale;
 this->sprite->pos=*pos;
 this->sprite->vel=*vel;
 this->sprite->angle=heading;
 this->sequenceMap=sballSequenceMap;
 setState((SpriteObject *)this,0);
 this->owner=(Object *)owner;
 return (Object *)this;
}


/********************************\
 *          SENTRY STUFF        *
\********************************/

enum  {AI_SENTRY_IDLE=STATE_IDLE,
	  AI_SENTRY_WALK=STATE_WALK,
	  AI_SENTRY_CLAW=STATE_SRA,
	  AI_SENTRY_THROW=STATE_LRA,
	  AI_SENTRY_HIT=STATE_HIT,
	  AI_SENTRY_SEEK=STATE_SEEK,
	  AI_SENTRY_FIRE,
	  AI_SENTRY_SPRINT,
	  AI_SENTRY_NMSEQ};

unsigned short sentrySeqMap[]={HB|0, HB|0, HB|0, HB|0,
				  HB|8, HB|0,HB|16,HB|0};

void sentry_func(Object *_this,int message,int param1,int param2)
{SentryObject *this=(SentryObject *)_this;
 int collide;
 int fflags;
 collide=0;
 fflags=0;
 switch (message)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeZorch((SpriteObject *)this);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_LANDGUTS,48000,0,0);
	    makeExplosion((MonsterObject *)this,25,5);
	    makeExplosion((MonsterObject *)this,25,5);
	    spriteObject_makeSound((SpriteObject *)this,1);
	    delayKill(_this);
	   }
	else
	   if (!this->stunCounter && this->state!=AI_SENTRY_THROW)
	      {setState((SpriteObject *)this,AI_SENTRY_HIT);
	       this->stunCounter=40;
	      }
	this->sprite->vel.x=0;
	this->sprite->vel.z=0;
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_SENTRY_IDLE)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (!this->enemy)
	       setState((SpriteObject *)this,AI_SENTRY_IDLE);
	   }
	if (this->stunCounter)
	   this->stunCounter--;
	switch (this->state)
	   {case AI_SENTRY_HIT:
	       if (fflags && FRAMEFLAG_ENDOFSEQUENCE)
		  {setState((SpriteObject *)this,AI_SENTRY_WALK);
		   this->decideNow=1;
		  }
	       break;
	    case AI_SENTRY_IDLE:
	       normalMonster_idle((MonsterObject *)this,2,0);
	       break;
	    case AI_SENTRY_CLAW:
	    case AI_SENTRY_THROW:
	       this->sprite->vel.x=0;
	       this->sprite->vel.z=0;
	       spriteHome((SpriteObject *)this,(SpriteObject *)this->enemy,
			  F(20),F(18));
	       if (this->aimCounter++>15)
		  setState((SpriteObject *)this,AI_SENTRY_FIRE);
	       break;
	    case AI_SENTRY_FIRE:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {setState((SpriteObject *)this,AI_SENTRY_SPRINT);
		   this->sprite->angle=((short)getNextRand())*360;
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<4;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<4;
		   this->sprintCounter=20;
		   break;
		  }
	       if (this->sprite->frame==9)
		  {if (canSee(this->sprite,this->enemy->sprite))
		      {/* constructZap(this->sprite->s,&this->sprite->pos,
				    (SpriteObject *)this,
				    this->enemy,0); */
#if 1
		       MthXyz ballPos;
		       MthXyz ballVel;
		       initProjectile(&this->sprite->pos,
				      &this->enemy->sprite->pos,
				      &ballPos,&ballVel,F(46),4);
		       constructSball(this->sprite->s,&ballPos,&ballVel,
				      (SpriteObject *)this,
				      getAngle(ballVel.x,ballVel.z),24,
				      15000);
#endif
		      }
		  }
	       break;
	    case AI_SENTRY_SPRINT:
	       if (this->sprintCounter--<=0 ||
		   (collide & (COLLIDE_WALL|COLLIDE_SPRITE)))
		  {setState((SpriteObject *)this,AI_SENTRY_WALK);
		   this->decideNow=1;
		  }
	       break;
	    case AI_SENTRY_WALK:
	       if (AISLOT(0xf) || this->decideNow)
		  {int choice=decideWhatToDo((MonsterObject *)this,1,0);
		   this->decideNow=0;
		   switch (choice)
		      {case DO_RANDOMWANDER:
			  if (AISLOT(0xf))
			     {this->sprite->angle=
				 ((short)getNextRand())*360;
			      this->sprite->vel.x=
				 MTH_Cos(this->sprite->angle)<<2;
			      this->sprite->vel.z=
				 MTH_Sin(this->sprite->angle)<<2;
			     }
			  break;
		       case DO_FOLLOWROUTE:
			  this->sprite->vel.x=0;
			  this->sprite->vel.z=0;
			  setState((SpriteObject *)this,STATE_SEEK);
			  return;
		       case DO_GOIDLE:
			  setState((SpriteObject *)this,STATE_IDLE);
			  this->enemy=NULL;
			  break;
		       case DO_CHARGE:
			  this->aimCounter=0;
			  setState((SpriteObject *)this,STATE_LRA);
			  this->sprite->vel.x=0;
			  this->sprite->vel.z=0;
			  break;
			 }
		  }
	       break;
	    case AI_SENTRY_SEEK:
	       if ((this->aiSlot&0x0f)==(aicount&0x0f) &&
		   monster_seekEnemy((MonsterObject *)this,0,2))
		  setState((SpriteObject *)this,AI_SENTRY_WALK);
	       break;
	      }
	break;
       }
}

Object *constructSentry(int sector)
{SentryObject *this=(SentryObject *)getFreeObject(sentry_func,OT_SENTRY,
						  CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(32),F(1),GRAVITY<<1,
			0,SPRITEFLAG_BWATERBNDRY|SPRITEFLAG_BCLIFF,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sequenceMap=sentrySeqMap;
 setState((SpriteObject *)this,AI_SENTRY_IDLE);
 this->health=180;
 this->enemy=NULL;
 this->routePos=-1;
 this->aiSlot=nextAiSlot++;
 this->stunCounter=0;
 this->sprintCounter=0;
 this->decideNow=0;
 return (Object *)this;
}


/********************************\
 *          MUMMY STUFF        *
\********************************/
enum  {AI_MUMMY_IDLE=STATE_IDLE,
	  AI_MUMMY_WALK=STATE_WALK,
	  AI_MUMMY_CLAW=STATE_SRA,
	  AI_MUMMY_THROW=STATE_LRA,
	  AI_MUMMY_STUNNED=STATE_HIT,
	  AI_MUMMY_SEEK=STATE_SEEK,
	  AI_MUMMY_NMSEQ};

unsigned short mummySeqMap[]={HB|8, HB|0, HB|20, HB|28,
				 HB|8,HB|0};

void mummy_func(Object *_this,int message,int param1,int param2)
{MummyObject *this=(MummyObject *)_this;
 int collide;
 int fflags;
 collide=0;
 fflags=0;
 switch (message)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeZorch((SpriteObject *)this);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_LANDGUTS,48000,0,0);
	    spriteObject_makeSound((SpriteObject *)this,0);
	    makeExplosion((MonsterObject *)this,16,4);
	    makeExplosion((MonsterObject *)this,16,4);
	    delayKill(_this);
	   }
	else
	   if (this->state!=AI_MUMMY_STUNNED)
	      {setState((SpriteObject *)this,AI_MUMMY_STUNNED);
	       this->stunCounter=20;
	      }
	this->sprite->vel.x=0;
	this->sprite->vel.z=0;
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_MUMMY_IDLE)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (!this->enemy)
	       setState((SpriteObject *)this,AI_MUMMY_IDLE);
	   }
	switch (this->state)
	   {case AI_MUMMY_IDLE:
	       normalMonster_idle((MonsterObject *)this,2,-1);
	       break;
	    case AI_MUMMY_STUNNED:
	       if (this->stunCounter>0)
		  this->stunCounter--;
	       else
		  setState((SpriteObject *)this,AI_MUMMY_WALK);
	       break;
	    case AI_MUMMY_CLAW:
	       this->sprite->vel.x=0;
	       this->sprite->vel.z=0;
	       if (this->sprite->frame==5 || this->sprite->frame==18)
		  {if (spriteDistApprox(this->sprite,
					this->enemy->sprite)<F(150))
		      {PlotCourseToObject((SpriteObject *)this,
					  (SpriteObject *)this->enemy);
		       assert(this->enemy->class!=CLASS_DEAD);
		       signalObject((Object *)this->enemy,SIGNAL_HURT,20,
				    (int)this);
		      }
		   else
		      setState((SpriteObject *)this,AI_MUMMY_WALK);
		  }
	       break;
	    case AI_MUMMY_THROW:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_MUMMY_IDLE);
	       if (fflags & FRAMEFLAG_FIRE)
		  {if (canSee(this->sprite,this->enemy->sprite))
		      {MthXyz ballPos;
		       MthXyz ballVel;
		       initProjectile(&this->sprite->pos,
				      &this->enemy->sprite->pos,
				      &ballPos,&ballVel,F(46),4);
		       constructCobra(this->sprite->s,ballPos.x,ballPos.y,
				      ballPos.z,getAngle(ballVel.x,ballVel.z),
				      0,(SpriteObject *)this,OT_MUMBALL,0);
		      }
		  }
	       break;
	    case AI_MUMMY_WALK:
	       normalMonster_walking((MonsterObject*)this,
				     collide,fflags,2);
	       break;
	    case AI_MUMMY_SEEK:
	       if ((this->aiSlot&0x0f)==(aicount&0x0f) &&
		   monster_seekEnemy((MonsterObject *)this,0,2))
		  setState((SpriteObject *)this,AI_MUMMY_WALK);
	       break;
	      }
	break;
       }
}

Object *constructMummy(int sector)
{MummyObject *this=(MummyObject *)getFreeObject(mummy_func,OT_MUMMY,
						CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(32),F(1),GRAVITY<<1,
			0,SPRITEFLAG_BWATERBNDRY|SPRITEFLAG_BCLIFF,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sequenceMap=mummySeqMap;
 setState((SpriteObject *)this,AI_MUMMY_IDLE);
 this->health=130;
 this->enemy=NULL;
 this->routePos=-1;
 this->aiSlot=nextAiSlot++;
 this->stunCounter=0;
 return (Object *)this;
}


/********************************\
 *          BASTET STUFF        *
\********************************/
enum  {AI_BASTET_IDLE,AI_BASTET_WALK,AI_BASTET_CLAW,
	  AI_BASTET_STUNNED,AI_BASTET_SEEK,
	  AI_BASTET_PORTDOWN,AI_BASTET_PORTING,AI_BASTET_PORTUP,
	  AI_BASTET_NMSEQ};

unsigned short bastetSeqMap[]={31, HB|0, HB|10,
				  HB|18,HB|0,
				  8,31,9};

void bastet_func(Object *_this,int message,int param1,int param2)
{BastetObject *this=(BastetObject *)_this;
 int collide;
 int fflags;
 collide=0;
 fflags=0;
 switch (message)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeZorch((SpriteObject *)this);
	    if (this->hasLight)
	       {removeLight(this->sprite);
		this->hasLight=0;
	       }
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_LANDGUTS,48000,0,0);
	    spriteObject_makeSound((SpriteObject *)this,1);
	    makeExplosion((MonsterObject *)this,26,4);
	    delayKill(_this);
	   }
	else
	   if (this->state!=AI_BASTET_STUNNED &&
	       this->state!=AI_BASTET_PORTDOWN &&
	       this->state!=AI_BASTET_PORTING &&
	       this->state!=AI_BASTET_PORTUP)
	      {setState((SpriteObject *)this,AI_BASTET_STUNNED);
	       this->stunCounter=20;
	      }
	this->sprite->vel.x=0;
	this->sprite->vel.z=0;
	break;
     case SIGNAL_OBJECTDESTROYED:
	if (this->hasLight)
	   {removeLight(this->sprite);
	    this->hasLight=0;
	   }
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	if (this->state!=AI_BASTET_IDLE)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (!this->enemy)
	       setState((SpriteObject *)this,AI_BASTET_IDLE);
	   }
	switch (this->state)
	   {case AI_BASTET_IDLE:
	       normalMonster_idle((MonsterObject *)this,2,0);
	       break;
	    case AI_BASTET_STUNNED:
	       if (this->stunCounter>0)
		  this->stunCounter--;
	       else
		  setState((SpriteObject *)this,AI_BASTET_WALK);
	       break;
	    case AI_BASTET_CLAW:
	       this->sprite->vel.x=0;
	       this->sprite->vel.z=0;
	       if (this->sprite->frame==5 || this->sprite->frame==18)
		  {if (spriteDistApprox(this->sprite,
					this->enemy->sprite)<F(150))
		      {PlotCourseToObject((SpriteObject *)this,
					  (SpriteObject *)this->enemy);
		       assert(this->enemy->class!=CLASS_DEAD);
		       signalObject((Object *)this->enemy,SIGNAL_HURT,10,
				    (int)this);
		      }
		   else
		      setState((SpriteObject *)this,AI_BASTET_WALK);
		  }
	       break;
	    case AI_BASTET_PORTDOWN:
	       if (this->sprite->frame==23)
		  {if (!this->hasLight)
		      {addLight(this->sprite,0,0,0);
		       this->hasLight=1;
		      }
		  }
	       if (this->sprite->frame>23)
		  changeLightColor(this->sprite,
				   this->sprite->frame-23,
				   this->sprite->frame-23,
				   this->sprite->frame-23);
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {if (this->hasLight)
		      {removeLight(this->sprite);
		       this->hasLight=0;
		      }
		   setState((SpriteObject *)this,AI_BASTET_PORTING);
		   this->sprite->flags|=SPRITEFLAG_INVISIBLE;
		   this->portTimer=0;
		  }
	       break;
	    case AI_BASTET_PORTING:
	       if ((this->aiSlot&0x0f)==(aicount&0x0f))
		  {PlotCourseToObject((SpriteObject *)this,
				      (SpriteObject *)this->enemy);
		   /* if we get here, we know we can see the player */
		   this->sprite->angle=
		      normalizeAngle(this->sprite->angle+randomAngle(3));
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<4;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<4;
		  }
	       this->portTimer++;
	       if (this->portTimer>60)
		  {if (!this->hasLight)
		      {addLight(this->sprite,0,0,0);
		       this->hasLight=1;
		      }
		   setState((SpriteObject *)this,AI_BASTET_PORTUP);
		   this->sprite->flags&=~SPRITEFLAG_INVISIBLE;
		   this->sprite->vel.x=0;
		   this->sprite->vel.z=0;
		  }
	       break;
	    case AI_BASTET_PORTUP:
	       changeLightColor(this->sprite,
				this->sprite->frame,
				this->sprite->frame,
				this->sprite->frame);
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {setState((SpriteObject *)this,AI_BASTET_WALK);
		   this->sprite->flags&=~(SPRITEFLAG_FOOTCLIP|
					  SPRITEFLAG_IMATERIAL);
		   if (this->hasLight)
		      {removeLight(this->sprite);
		       this->hasLight=0;
		      }
		  }
	       break;
	    case AI_BASTET_WALK:

	       /* shouldn't really need this */
	       this->sprite->flags&=~(SPRITEFLAG_FOOTCLIP|
				      SPRITEFLAG_IMATERIAL);



	       if ((collide & COLLIDE_SPRITE) &&
		   sprites[collide&0xffff].owner==(Object *)this->enemy)
		  {/* bite them! */
		   setState((SpriteObject *)this,AI_BASTET_CLAW);
		   this->sprite->vel.x=0;
		   this->sprite->vel.z=0;
		  }
	       if (collide & COLLIDE_WALL)
		  {this->sprite->angle=
		      normalizeAngle(this->sprite->angle+/*F(180)+*/
				     randomAngle(8));
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<2;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<2;
		  }
	       if (AISLOT(0xf))
		  switch (decideWhatToDo((MonsterObject *)this,1,0))
		     {case DO_GOIDLE:
			 setState((SpriteObject *)this,AI_BASTET_IDLE);
			 this->enemy=NULL;
			 break;
		      case DO_CHARGE:
			 if (AISLOT(0x3f))
			    {if (getNextRand()&0x80)
				{setState((SpriteObject *)this,
					  AI_BASTET_PORTDOWN);
				 this->sprite->vel.x=0;
				 this->sprite->vel.z=0;
				 this->sprite->flags|=SPRITEFLAG_FOOTCLIP|
				    SPRITEFLAG_IMATERIAL;
				 break;
				}
			    }
			 PlotCourseToObject((SpriteObject *)this,
					    (SpriteObject *)this->enemy);
			 this->sprite->angle=
			    normalizeAngle(this->sprite->angle+
					   randomAngle(7));
			 this->sprite->vel.x=
			    MTH_Cos(this->sprite->angle)<<4;
			 this->sprite->vel.z=
			    MTH_Sin(this->sprite->angle)<<4;
			 break;
		      case DO_RANDOMWANDER:
			 if (AISLOT(0x1f))
			    this->sprite->angle=
			       ((short)getNextRand())*360;
			 this->sprite->vel.x=
			    MTH_Cos(this->sprite->angle)<<3;
			 this->sprite->vel.z=
			    MTH_Sin(this->sprite->angle)<<3;
			 break;
		      case DO_FOLLOWROUTE:
			 this->sprite->vel.x=0;
			 this->sprite->vel.z=0;
			 setState((SpriteObject *)this,AI_BASTET_SEEK);
			 break;
			}
	       break;
	    case AI_BASTET_SEEK:
	       if ((this->aiSlot&0x0f)==(aicount&0x0f) &&
		   monster_seekEnemy((MonsterObject *)this,0,2))
		  setState((SpriteObject *)this,AI_BASTET_WALK);
	       break;
	      }
	break;
       }
}


Object *constructBastet(int sector)
{BastetObject *this=(BastetObject *)getFreeObject(bastet_func,OT_BASTET,
						  CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(32),F(1),GRAVITY<<1,
			0,SPRITEFLAG_BWATERBNDRY,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sequenceMap=bastetSeqMap;
 setState((SpriteObject *)this,AI_BASTET_IDLE);
 this->health=150;
 this->enemy=NULL;
 this->routePos=-1;
 this->aiSlot=nextAiSlot++;
 this->stunCounter=0;
 this->hasLight=0;
 return (Object *)this;
}


/********************************\
 *          KAPOW   STUFF       *
\********************************/

unsigned short kapowSequenceMap[]={0};

void kapow_func(Object *_this,int msg,int param1,int param2)
{SpriteObject *this=(SpriteObject *)_this;
 int fflags;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	fflags=spriteAdvanceFrame(this->sprite);
	if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
	   delayKill(_this);
	break;
       }
}

Object *constructKapow(int sector,int x,int y,int z)
{SpriteObject *this=(SpriteObject *)
    getFreeObject(kapow_func,OT_KAPOW,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_IMATERIAL|SPRITEFLAG_IMMOBILE,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos.x=x;
 this->sprite->pos.y=y;
 this->sprite->pos.z=z;
 this->sequenceMap=kapowSequenceMap;
 setState(this,0);
 return (Object *)this;
}


/********************************\
 *          THING STUFF         *
\********************************/

#define BEATLEN 12
static int beatScale[]=
  {255,230,190,180,190,200,210,220,230,240,250,256};

unsigned short thingSequenceMap[]={0};

void thing_func(Object *_this,int msg,int param1,int param2)
{ThingObject *this=(ThingObject *)_this;
 int collide;
 int fflags;
 int s,beep;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    {if (this->light)
		removeLight(this->sprite);
	     freeSprite(this->sprite);
	    }
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	collide=moveSprite(this->sprite);
	if (this->flags & THINGFLAG_ANIMATE)
	   fflags=spriteAdvanceFrame(this->sprite);
	this->sin+=F(8);
	beep=0;
	if (this->sin>F(180))
	   {this->sin-=F(360);
	    if (this->flags & THINGFLAG_NOISY)
	       {spriteObject_makeSound((SpriteObject *)this,0);
		beep=1;
	       }
	   }
	s=MTH_Sin(this->sin);
	if (this->flags & THINGFLAG_WAVE)
	   this->sprite->pos.y=this->baseY+6*s;
	if (this->flags & THINGFLAG_PULSE)
	   {int c=(s>>13)+16;
	    this->sprite->color=RGB(c,c,c);
	   }

	if (this->flags & THINGFLAG_THROB)
	   {int c;
	    this->frame++;
	    if (this->frame>=32)
	       this->frame=0;
	    if (this->frame<BEATLEN)
	       {this->sprite->scale=
		   ((48000*beatScale[this->frame])>>8);
		c=15+((256-beatScale[this->frame])>>3);
		this->sprite->color=RGB(c,c,c);
	       }
	   }
	if (this->flags & THINGFLAG_BLUEMYSTICAL)
	   {int d=approxDist(this->sprite->pos.x-camera->pos.x,
			     this->sprite->pos.y-camera->pos.y,
			     this->sprite->pos.z-camera->pos.z);
	    if (this->light==1)
	       removeLight(this->sprite);
	    this->light--;
	    if (this->light<0)
	       this->light=0;

	    if (d<F(712) && beep)
	       {if (!this->light)
		   addLight(this->sprite,31,31,31);
		this->light=22;
	       }
	    if (this->light)
	       {d=this->light;
		changeLightColor(this->sprite,31-d,31-d,31-d);
		if (d<15)
		   d=15;
		this->sprite->color=greyTable[d];
	       }
	   }
	if (this->flags & THINGFLAG_MYSTICAL)
	   {int d=approxDist(this->sprite->pos.x-camera->pos.x,
			     this->sprite->pos.y-camera->pos.y,
			     this->sprite->pos.z-camera->pos.z);
	    if (d<F(512))
	       {if (!this->light)
		   addLight(this->sprite,31,31,31);
		this->light++;
		if (this->light>31)
		   this->light=31;
	       }
	    else
	       {if (this->light==1)
		   removeLight(this->sprite);
		this->light--;
		if (this->light<0)
		   this->light=0;
	       }
	    if (this->light)
	       {d=this->light;
		changeLightColor(this->sprite,31-d,31-d,31-d);
		if (d<15)
		   d=15;
		this->sprite->color=greyTable[d];
	       }
	   }
	if (collide & COLLIDE_SPRITE)
	   {/* GCC14: whoever walked into it, not whoever the globals hold (MPLAYER.H): a pickup
	       fills the inventory of the player that touched it */
	    int who=mpIndexOfSprite(&(sprites[collide&0xffff]));
	    if (who>=0)
	       {int prev=mpBegin(who);
		if (playerGetObject(this->type))
		   {switch (this->type)
		       {case OT_PYRAMID:
			case OT_RAMMUMMY:
			   currentState.gameFlags|=GAMEFLAG_JUSTTELEPORTED;
			   currentState.gameFlags&=~GAMEFLAG_TALKEDTORAMSES;
			   break;
			case OT_SANDALS ... OT_FEATHER:
			   signalAllObjects(SIGNAL_SWITCH,
					    this->type-OT_SANDALS+1000,0);
			   break;
			  }
		    delayKill(_this);
		   }
		mpEnd(prev);
	       }
	   }
	break;
       }
}


Object *constructSpecialArtifact(int sector,int x,int y,int z,int thingType)
{ThingObject *this;
 this=(ThingObject *)
    getFreeObject(thing_func,thingType,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_IMATERIAL|SPRITEFLAG_IMMOBILE,
			(Object *)this);
 moveObject((Object *)this,objectRunList);
 this->sequenceMap=thingSequenceMap;
 this->sprite->pos.x=x;
 this->sprite->pos.y=y;
 this->sprite->pos.z=z;
 this->sin=0;
 this->baseY=this->sprite->pos.y;
 this->frame=0;
 this->light=0;
 this->sprite->color=RGB(15,15,15);
 setState((SpriteObject *)this,0);
 this->flags=THINGFLAG_WAVE;
 this->sprite->flags|=SPRITEFLAG_COLORED;
 return (Object *)this;
}

Object *constructThing(int sector,int x,int y,int z,int thingType)
{ThingObject *this;
 /* decide if we should not place this thing */
 if (thingType>=OT_DOLL1 && thingType<=OT_DOLL23)
    if (currentState.dolls & (1<<(thingType-OT_DOLL1)))
       return NULL;
 if (thingType>=OT_PISTOL && thingType<=OT_MANACLE)
    if (currentState.inventory & (INV_PISTOL<<(thingType-OT_PISTOL)))
       return NULL;
 if (thingType>=OT_SANDALS && thingType<=OT_FEATHER)
    if (currentState.gameFlags & (GAMEFLAG_GOTSANDALS<<(thingType-OT_SANDALS)))
       {signalAllObjects(SIGNAL_SWITCH,
			 thingType-OT_SANDALS+1000,0);
	/* this only works if the monsters come after the push blocks
	   in the object list */
	return NULL;
       }
#if 0
 if (thingType==OT_PYRAMID)
    if (currentState.levFlags[(int)currentState.currentLevel] &
	LEVFLAG_GOTPYRAMID)
       return NULL;
#endif

 if (thingType==OT_BLOODBOWL)
    if (currentState.levFlags[(int)currentState.currentLevel] &
	LEVFLAG_GOTVESSEL)
       return NULL;
 if (thingType>=OT_COMM_BATTERY && thingType<=OT_COMM_TOP)
    if (currentState.inventory & (0x10000<<(thingType-OT_COMM_BATTERY)))
       return NULL;

 this=(ThingObject *)
    getFreeObject(thing_func,thingType,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 {int rad;
  rad=F(16);
  if (thingType==OT_CHOPPER || thingType==OT_RAMMUMMY)
     rad=F(64);
  this->sprite=newSprite(sector,rad,F(1),0,
			 0,SPRITEFLAG_IMATERIAL|SPRITEFLAG_IMMOBILE,
			 (Object *)this);
 }
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 if (thingType==OT_AMMOBALL || thingType==OT_HEALTHBALL)
    this->sprite->scale=20000;
 if (thingType==OT_AMMOORB || thingType==OT_HEALTHORB)
    this->sprite->scale=48000;
 if (thingType==OT_AMMOSPHERE || thingType==OT_HEALTHSPHERE)
    this->sprite->scale=45000;
 if (thingType>=OT_BUGKEY && thingType<=OT_PLANTKEY)
    this->sprite->scale=65000;
 if (thingType==OT_CHOPPER)
    this->sprite->scale=F(1);
 if (thingType==OT_RAMMUMMY)
    this->sprite->scale=23000;
 this->sequenceMap=thingSequenceMap;
 this->sprite->pos.x=x;
 this->sprite->pos.y=y;
 this->sprite->pos.z=z;
 this->sin=0;
 this->baseY=this->sprite->pos.y;
 this->frame=0;
 this->light=0;
 this->sprite->color=RGB(15,15,15);
 setState((SpriteObject *)this,0);

 this->flags=THINGFLAG_WAVE;
 if (thingType==OT_AMMOORB ||
     thingType==OT_HEALTHSPHERE || thingType==OT_AMMOSPHERE)
    {this->flags|=THINGFLAG_PULSE;
     this->sprite->flags|=SPRITEFLAG_COLORED;
    }
 if ((this->type>=OT_BUGKEY && this->type<=OT_PLANTKEY) ||
     this->type==OT_AMMOSPHERE || this->type==OT_HEALTHSPHERE ||
     this->type==OT_BLOODBOWL || this->type==OT_CHOPPER ||
     this->type==OT_PYRAMID)
    this->flags|=THINGFLAG_ANIMATE;
 if (this->type==OT_HEALTHORB)
    {this->flags|=THINGFLAG_THROB;
     this->sprite->flags|=SPRITEFLAG_COLORED;
    }
 if ((thingType>=OT_SANDALS && thingType<=OT_FEATHER) ||
     thingType==OT_RAMMUMMY)
    {this->flags|=THINGFLAG_MYSTICAL;
     this->sprite->flags|=SPRITEFLAG_COLORED;
    }
 if (thingType>=OT_COMM_BATTERY && thingType<=OT_COMM_TOP)
    {this->flags|=THINGFLAG_BLUEMYSTICAL|THINGFLAG_NOISY;
     this->sprite->flags|=SPRITEFLAG_COLORED;
    }
 return (Object *)this;
}

/********************************\
 *          TORCH STUFF         *
\********************************/

unsigned short torchSequenceMap[]={0,0};

void torch_func(Object *_this,int msg,int param1,int param2)
{TorchObject *this=(TorchObject *)_this;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	if (this->state==0)
	   {this->state=1;
	    delay_moveObject(_this,objectRunList);
	   }
	this->seeCounter=5;
	break;
     case SIGNAL_MOVE:
	spriteAdvanceFrame(this->sprite);
	if (this->seeCounter--<=0)
	   {delay_moveObject(_this,objectIdleList);
	    this->state=0;
	   }
	break;
       }
}

Object *constructTorch(int sector,int torchType)
{TorchObject *this;
 this=(TorchObject *)
    getFreeObject(torch_func,torchType,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_IMATERIAL|SPRITEFLAG_IMMOBILE,
			(Object *)this);
 suckSpriteParams(this->sprite);
 moveObject((Object *)this,objectIdleList);
 this->sequenceMap=torchSequenceMap;
 this->seeCounter=0;
 setState((SpriteObject *)this,0);
 spriteRandomizeFrame(this->sprite);
 return (Object *)this;
}

/********************************\
 *           BLOB STUFF         *
\********************************/

enum  {AI_BLOB_IDLE,AI_BLOB_RILED,AI_BLOB_EXPLODE};
unsigned short blobSequenceMap[]={0,1,2};

void blob_func(Object *_this,int msg,int param1,int param2)
{BlobObject *this=(BlobObject *)_this;
 int fflags;
 switch (msg)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeZorch((SpriteObject *)this);
	    spriteObject_makeGrenadeExplosion((SpriteObject *)this);
	    radialDamage((Object *)this,&this->sprite->pos,
			 this->sprite->s,
			 100,F(300));
	    delayKill(_this);
	   }
     case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	fflags=spriteAdvanceFrame(this->sprite);
	switch (this->state)
	   {case AI_BLOB_IDLE:
	       if (AISLOT(0xf) &&
		   spriteDistApprox(this->sprite,player->sprite)<F(200))
		  {this->anger=1;
		   setState((SpriteObject *)this,AI_BLOB_RILED);
		   spriteObject_makeSound((SpriteObject *)this,0);
		  }
	       break;
	    case AI_BLOB_RILED:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_BLOB_EXPLODE);
	       break;
	    case AI_BLOB_EXPLODE:
	       this->anger++;
	       if (this->anger==50)
		  {spriteObject_makeZorch((SpriteObject *)this);
		   spriteObject_makeGrenadeExplosion((SpriteObject *)this);
		   radialDamage((Object *)this,&this->sprite->pos,
				this->sprite->s,
				100,F(200));
		  }
	       break;
	      }
	break;
       }
}

Object *constructBlob(int sector)
{BlobObject *this;
 this=(BlobObject *)
    getFreeObject(blob_func,OT_BLOB,CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);
 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_IMMOBILE,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sequenceMap=blobSequenceMap;
 this->anger=0;
 this->health=60;
 this->enemy=NULL;
 this->aiSlot=nextAiSlot++;
 setState((SpriteObject *)this,0);
 return (Object *)this;
}

/********************************\
 *           LIGHT STUFF        *
\********************************/
unsigned short lightSequenceMap[]={0};

void light_func(Object *_this,int msg,int param1,int param2)
{LightObject *this=(LightObject *)_this;
 int r,g,b;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    {removeLight(this->sprite);
	     freeSprite(this->sprite);
	    }
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	r=f(evalHermite(this->age,
			this->parameters[0],
			this->parameters[1],
			this->parameters[2],
			this->parameters[3]));
	g=f(evalHermite(this->age,
			this->parameters[4],
			this->parameters[5],
			this->parameters[6],
			this->parameters[7]));
	b=f(evalHermite(this->age,
			this->parameters[8],
			this->parameters[9],
			this->parameters[10],
			this->parameters[11]));
	changeLightColor(this->sprite,r,g,b);
	this->age+=this->ageInc;
	if (this->age>F(1))
	   {delayKill(_this);
	   }
	break;
       }
}

/* parameters are start level, end level, start slope, end slope for
   each of red, green and blue */
Object *constructLight(int sector,int x,int y,int z,
		       int *parameters,Fixed32 ageInc)
{LightObject *this=(LightObject *)
    getFreeObject(light_func,OT_LIGHT,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_INVISIBLE|SPRITEFLAG_IMATERIAL|
			SPRITEFLAG_IMMOBILE,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos.x=x;
 this->sprite->pos.y=y;
 this->sprite->pos.z=z;
 this->sequenceMap=lightSequenceMap;
 this->parameters=parameters;
 this->ageInc=ageInc;
 addLight(this->sprite,f(parameters[0]),f(parameters[4]),f(parameters[8]));
 this->age=0;
 return (Object *)this;
}


/********************************\
 *          ONESHOT STUFF       *
\********************************/

unsigned short oneShotSequenceMap[]={0};

void oneshot_func(Object *_this,int msg,int param1,int param2)
{OneShotObject *this=(OneShotObject *)_this;
 int fflags;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	if (this->waitTime)
	   {this->waitTime--;
	    if (this->waitTime)
	       break;
	    this->sprite->flags&=~SPRITEFLAG_INVISIBLE;
	   }
	fflags=spriteAdvanceFrame(this->sprite);
	if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
	   delayKill(_this);
	break;
       }
}

Object *constructOneShot(int sector,int x,int y,int z,int oneShotType,
			 int scale,unsigned short colored,int waitTime)
{OneShotObject *this=(OneShotObject *)
    getFreeObject(oneshot_func,oneShotType,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_INVISIBLE|SPRITEFLAG_IMATERIAL|
			  SPRITEFLAG_IMMOBILE,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 if (colored)
    {this->sprite->color=colored;
     this->sprite->flags|=SPRITEFLAG_COLORED;
    }
 this->sprite->pos.x=x;
 this->sprite->pos.y=y;
 this->sprite->pos.z=z;
 this->sprite->scale=scale;
 this->sequenceMap=oneShotSequenceMap;
 this->waitTime=waitTime+1;
 assert(waitTime>=0);
 setState((SpriteObject *)this,0);
 return (Object *)this;
}


/********************************\
 *          RACLOUD STUFF       *
\********************************/

unsigned short cloudSequenceMap[]={1};

void cloud_func(Object *_this,int msg,int param1,int param2)
{CloudObject *this=(CloudObject *)_this;
 int fflags;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==(Object *)this->target)
	    this->target=NULL;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	fflags=spriteAdvanceFrame(this->sprite);
	if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
	   {if (this->target)
	       {assert(this->target->class!=CLASS_DEAD);
		signalObject((Object *)this->target,SIGNAL_HURT,this->damage,
			     (int)this);
	       }
	    delayKill(_this);
	   }
	break;
       }
}

Object *constructCloud(MonsterObject *target,int damage)
{CloudObject *this=(CloudObject *)
    getFreeObject(cloud_func,OT_RACLOUD,CLASS_PROJECTILE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 this->sprite=newSprite(target->sprite->s,F(16),F(1),0,
			0,SPRITEFLAG_IMATERIAL|SPRITEFLAG_IMMOBILE,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos.x=target->sprite->pos.x;
 this->sprite->pos.y=target->sprite->pos.y+F(64);
 this->sprite->pos.z=target->sprite->pos.z;

 this->sequenceMap=cloudSequenceMap;
 this->target=target;
 this->owner=(Object *)player;
 this->damage=damage;
 setState((SpriteObject *)this,0);
 return (Object *)this;
}


/********************************\
 *          DOOR STUFF          *
\********************************/
static void setDoorBlockBits(PushBlockObject *this)
{int i,w;
 for (i=level_pushBlock[this->pbNum].startWall;
      i<=level_pushBlock[this->pbNum].endWall;
      i++)
    {w=level_PBWall[i];
     assert(level_wall[w].object==this);
     if (!(level_wall[w].flags & WALLFLAG_DOORWALL))
	continue;
     if (level_vertex[level_wall[w].v[1]].y-
	 level_vertex[level_wall[w].v[2]].y<CFG_DOOR_FIT)
	level_wall[w].flags|=WALLFLAG_SHORTOPENING;
     else
	level_wall[w].flags&=~WALLFLAG_SHORTOPENING;
    }
}

enum  {AI_DOOR_IDLE,AI_DOOR_UP,AI_DOOR_WAIT,AI_DOOR_DOWN};
#define DOORSPEED 2
void door_func(Object *_this,int msg,int param1,int param2)
{DoorObject *this=(DoorObject *)_this;
 switch (msg)
    {case SIGNAL_SWITCH:
     case SIGNAL_PRESS:
     case SIGNAL_CEILCONTACT:
	if (this->state==AI_DOOR_WAIT)
	   break;
	if (msg==SIGNAL_PRESS &&
	    (this->type<OT_BUGDOOR || this->type>OT_PLANTDOOR))
	   if (this->channel!=-1)
	      break;
	if (msg==SIGNAL_SWITCH)
	   if (param1!=this->channel)
	      break;
	if (this->type>=OT_BUGDOOR &&
	    this->type<=OT_PLANTDOOR)
	   if (!((getKeyMask()>>(this->type-OT_BUGDOOR))&1))
	      {changeMessage(getText(LB_ITEMMESSAGE,16+this->type-OT_BUGDOOR));
	       break;
	      }

	this->state=AI_DOOR_UP;
	pushBlockMakeSound((PushBlockObject *)this,
			   level_staticSoundMap[ST_PUSHBLOCK]);
	delay_moveObject((Object *)this,objectRunList);
	break;
     case SIGNAL_MOVE:
	switch (this->state)
	   {case AI_DOOR_UP:
	       if (this->offset<F(this->doorHeight))
		  {pushBlockAdjustSound((PushBlockObject *)this);
		   pbObject_move((PushBlockObject *)this,F(DOORSPEED));
		  }
	       else
		  {stopAllSound((int)this);
		   this->state=AI_DOOR_WAIT;
		   this->waitCounter=0;
		   if (this->type==OT_STUCKUPDOOR /* ||
		       (this->type>=OT_BUGDOOR && this->type<=OT_PLANTDOOR &&
			this->channel==1)*/)
		      delay_moveObject((Object *)this,objectIdleList);
		  }
	       setDoorBlockBits((PushBlockObject *)this);
	       break;
	    case AI_DOOR_WAIT:
	       if (this->waitCounter++>128)
		  {this->state=AI_DOOR_DOWN;
		   pushBlockMakeSound((PushBlockObject *)this,
				      level_staticSoundMap[ST_PUSHBLOCK]);
		  }
	       break;
	    case AI_DOOR_DOWN:
	       pushBlockAdjustSound((PushBlockObject *)this);
	       pbObject_move((PushBlockObject *)this,-F(DOORSPEED));
	       if (this->offset<=0)
		  {pbObject_moveTo((PushBlockObject *)this,0);
		   this->state=AI_DOOR_IDLE;
		   stopAllSound((int)this);
		   pushBlockMakeSound((PushBlockObject *)this,
				      level_staticSoundMap[ST_PUSHBLOCK]+1);
		   delay_moveObject((Object *)this,objectIdleList);
		   if (this->channel!=-1)
		      signalAllObjects(SIGNAL_SWITCHRESET,this->channel,0);
		  }
	       setDoorBlockBits((PushBlockObject *)this);
	       break;
	      }
       }
}

Object *constructDoor(int type,int pb)
{DoorObject *this=(DoorObject *)
    getFreeObject(door_func,type,CLASS_PUSHBLOCK);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 registerPBObject(pb,(Object *)this);
 this->pbNum=pb;
 this->state=AI_DOOR_IDLE;
 this->waitCounter=0;
 this->channel=suckShort();
 this->offset=0;
 this->doorHeight=suckShort();
 setDoorBlockBits((PushBlockObject *)this);
 return (Object *)this;
}

/********************************\
 *        DOWNDOOR STUFF        *
\********************************/

enum  {AI_DOWNDOOR_IDLE,AI_DOWNDOOR_DOWN,AI_DOWNDOOR_STUCK};

void downDoor_func(Object *_this,int msg,int param1,int param2)
{DoorObject *this=(DoorObject *)_this;
 switch (msg)
    {case SIGNAL_SWITCH:
	if (this->state!=AI_DOWNDOOR_IDLE)
	   break;
	if (param1!=this->channel)
	   break;
	this->state=AI_DOWNDOOR_DOWN;
	pushBlockMakeSound((PushBlockObject *)this,
			   level_staticSoundMap[ST_PUSHBLOCK]);
	delay_moveObject((Object *)this,objectRunList);
	this->state=AI_DOWNDOOR_DOWN;
	break;
     case SIGNAL_MOVE:
	pushBlockAdjustSound((PushBlockObject *)this);
	pbObject_move((PushBlockObject *)this,-F(DOORSPEED));
	if (this->offset<=0)
	   {pbObject_moveTo((PushBlockObject *)this,0);
	    this->state=AI_DOWNDOOR_STUCK;
	    stopAllSound((int)this);
	    pushBlockMakeSound((PushBlockObject *)this,
			       level_staticSoundMap[ST_PUSHBLOCK]+1);
	    delay_moveObject((Object *)this,objectIdleList);
	   }
	break;
       }
}

Object *constructDownDoor(int pb)
{DoorObject *this=(DoorObject *)
    getFreeObject(downDoor_func,OT_STUCKDOWNDOOR,CLASS_PUSHBLOCK);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 registerPBObject(pb,(Object *)this);
 this->pbNum=pb;
 this->state=AI_DOWNDOOR_IDLE;
 this->waitCounter=0;
 this->channel=suckShort();
 this->offset=0;
 pbObject_moveTo((PushBlockObject *)this,suckShort());
 return (Object *)this;
}


/********************************\
 *         BOBBLOCK STUFF       *
\********************************/

void bobBlock_func(Object *_this,int msg,int param1,int param2)
{BobBlockObject *this=(BobBlockObject *)_this;
 switch (msg)
    {case SIGNAL_MOVE:
	this->counter=normalizeAngle(this->counter+F(3));
	pbObject_move((PushBlockObject *)this,
		      MTH_Sin(this->counter)<<(2-this->speedDiv));
	break;
       }
}

Object *constructBobBlock(int pb)
{int phase,i;
 BobBlockObject *this=(BobBlockObject *)
    getFreeObject(bobBlock_func,OT_BOBBINGBLOCK,CLASS_PUSHBLOCK);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);
 registerPBObject(pb,(Object *)this);
 this->speedDiv=suckShort();
 assert(this->speedDiv<=2);
 phase=suckShort();
 this->pbNum=pb;
 this->counter=0;
 this->waitCounter=0;
 for (i=0;i<F(phase);i+=F(3))
    {this->counter=normalizeAngle(this->counter+F(3));
     pbObject_move((PushBlockObject *)this,
		   MTH_Sin(this->counter)<<(2-this->speedDiv));
    }
 return (Object *)this;
}

/********************************\
 *        SINKBLOCK STUFF       *
\********************************/

enum {AI_SINKBLOCK_SLEEP,AI_SINKBLOCK_FALL,AI_SINKBLOCK_RESET};

void sinkBlock_func(Object *_this,int msg,int param1,int param2)
{SinkBlockObject *this=(SinkBlockObject *)_this;
 switch (msg)
    {case SIGNAL_FLOORCONTACT:
	if (this->counter==0)
	   {this->state=AI_SINKBLOCK_FALL;
	    delay_moveObject((Object *)this,objectRunList);
	    this->counter=1;
	    this->vel=0;
	   }
	break;
     case SIGNAL_SWITCH:
	if (param1!=this->channel)
	   break;
	this->state=AI_SINKBLOCK_RESET;
	delay_moveObject((Object *)this,objectRunList);
	break;
     case SIGNAL_MOVE:
	switch (this->state)
	   {case AI_SINKBLOCK_FALL:
	       this->counter++;
	       if (this->counter==30)
		  pushBlockMakeSound((PushBlockObject *)this,
				     level_staticSoundMap[ST_PUSHBLOCK]+1);
	       if (this->counter>=30)
		  {this->vel-=(GRAVITY<<1);
		   pbObject_move((PushBlockObject *)this,this->vel);
		   if (f(this->offset)<-this->distanceToFall)
		      {pbObject_moveTo((PushBlockObject *)this,
				       -this->distanceToFall);
		       if (camera->floorSector==
			   level_pushBlock[this->pbNum].floorSector)
			  playerDYChange(-this->vel>>1);
		       setEarthQuake(25);
		       pushBlockMakeSound((PushBlockObject *)this,
					level_objectSoundMap[OT_SINKINGBLOCK]);
		       delay_moveObject((Object *)this,objectIdleList);
		       if (this->switchChannel!=0)
			  signalAllObjects(SIGNAL_SWITCH,
					   this->switchChannel,0);
		      }
		  }
	       break;
	    case AI_SINKBLOCK_RESET:
	       {int dist;
		dist=-this->offset>>5;
		if (dist>F(10))
		   dist=F(10);
		if (dist<F(1))
		   dist=F(1);
		pbObject_move((PushBlockObject *)this,dist);
		if (this->offset>=0)
		   {pbObject_moveTo((PushBlockObject *)this,0);
		    this->counter=0;
		    delay_moveObject((Object *)this,objectIdleList);
		    if (this->channel!=-1)
		       signalAllObjects(SIGNAL_SWITCHRESET,this->channel,0);
		   }
		break;
	       }
	      }
	break;
       }
}

Object *constructSinkBlock(int pb)
{SinkBlockObject *this=(SinkBlockObject *)
    getFreeObject(sinkBlock_func,OT_SINKINGBLOCK,CLASS_PUSHBLOCK);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 registerPBObject(pb,(Object *)this);
 this->pbNum=pb;
 this->counter=0;
 this->waitCounter=0;
 this->vel=0;
 this->distanceToFall=suckShort();
 this->channel=suckShort();
 this->switchChannel=suckShort();
 this->state=AI_SINKBLOCK_SLEEP;
 this->offset=0;
 return (Object *)this;
}



/********************************\
 *        ELEVATOR STUFF        *
\********************************/

enum  {AI_ELEVATOR_SLEEP,
	  AI_ELEVATOR_WAIT,
	  AI_ELEVATOR_GO};

void elevator_func(Object *_this,int msg,int param1,int param2)
{ElevatorObject *this=(ElevatorObject *)_this;
 switch (msg)
    {case SIGNAL_PRESS:
     case SIGNAL_SWITCH:
	if (msg==SIGNAL_PRESS)
	   if (this->channel!=-1)
	      break;
	if (msg==SIGNAL_SWITCH)
	   if (param1!=this->channel)
	      break;
	if (msg==SIGNAL_PRESS)
	   this->direction=-1;
	this->stepEnable=20;
	if (this->state==AI_ELEVATOR_SLEEP)
	   {this->state=AI_ELEVATOR_WAIT;
	    delay_moveObject((Object *)this,objectRunList);
	   }
	break;
     case SIGNAL_FLOORCONTACT:
	if (this->channel!=-1)
	   break;
	if (this->state==AI_ELEVATOR_GO)
	   break;
	if (this->state!=AI_ELEVATOR_SLEEP && this->stepEnable<0)
	   {this->stepEnable=-5;
	    break;
	   }
	this->stepEnable++;
	if (this->state==AI_ELEVATOR_SLEEP)
	   {this->counter=0;
	    this->state=AI_ELEVATOR_WAIT;
	    delay_moveObject((Object *)this,objectRunList);
	   }
	break;
     case SIGNAL_MOVE:
	switch (this->state)
	   {case AI_ELEVATOR_WAIT:
	       {int goIdle=0;
		int gogogo=0;
		if (this->stepEnable<0)
		   this->stepEnable++;
		this->counter++;
		if (this->counter>30*5)
		   {if ((this->direction==1) ^
			(this->type==OT_STARTSDOWNELEVATOR))
		       gogogo=1;
		    else
		       goIdle=1;
		   }
		if (this->stepEnable>=15)
		   gogogo=1;
		if (gogogo)
		   {this->stepEnable=-5;
		    this->state=AI_ELEVATOR_GO;
		    pushBlockMakeSound((PushBlockObject *)this,
				       level_staticSoundMap[ST_PUSHBLOCK]+2);
		    break;
		   }
		if (goIdle)
		   {this->state=AI_ELEVATOR_SLEEP;
		    delay_moveObject((Object *)this,objectIdleList);
		    break;
		   }
		break;
	       }
	    case AI_ELEVATOR_GO:
	       {int wait=0;
		pushBlockAdjustSound((PushBlockObject *)this);
		pbObject_move((PushBlockObject *)this,this->direction*F(5));
		if (this->direction==-1 && this->offset<=-F(this->throw))
		   {pbObject_moveTo((PushBlockObject *)this,-this->throw);
		    if (this->type==OT_STUCKDOWNELEVATOR)
		       delay_moveObject((Object *)this,objectIdleList);
		    wait=1;
		   }
		if (this->direction==1 && this->offset>=0)
		   {pbObject_moveTo((PushBlockObject *)this,0); CFG_LIFT_RESET(this);
		    wait=1;
		   }
		CFG_LIFT_MOVED(this); if (wait)
		   {this->state=AI_ELEVATOR_WAIT;
		    stopAllSound((int)this);
		    pushBlockMakeSound((PushBlockObject *)this,
				       level_staticSoundMap[ST_PUSHBLOCK]+CFG_LIFT_STOP_SND);
		    this->counter=0;
		    this->stepEnable=-5;
		    this->direction=-1*this->direction;
		   }
		break;
	       }
	      }
       }
}


Object *constructElevator(int pb,int type,short lowerLevel,short upperLevel)
{ElevatorObject *this=(ElevatorObject *)
    getFreeObject(elevator_func,type,CLASS_PUSHBLOCK);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 registerPBObject(pb,(Object *)this);
 this->pbNum=pb;
 this->state=AI_ELEVATOR_SLEEP;
 this->counter=0;
 this->throw=upperLevel-lowerLevel;
 this->stepEnable=0;
 this->channel=suckShort();
 dPrint("elevator with channel %d\n",this->channel);
 this->offset=0;
 this->direction=-1;
 if (this->type==OT_STARTSDOWNELEVATOR)
    {pbObject_moveTo((PushBlockObject *)this,-this->throw);
     this->direction=1;
    }
 return (Object *)this;
}


/********************************\
 *     UPDOWNELEVATOR STUFF     *
\********************************/

enum  {AI_UPDOWNELEVATOR_SLEEP,
	  AI_UPDOWNELEVATOR_GO};

void upDownElevator_func(Object *_this,int msg,int param1,int param2)
{UpDownElevatorObject *this=(UpDownElevatorObject *)_this;
 switch (msg)
    {case SIGNAL_SWITCH:
	if (param1==this->channel)
	   {this->direction=1;
	    if (this->state==AI_UPDOWNELEVATOR_SLEEP)
	       {this->state=AI_UPDOWNELEVATOR_GO;
		pushBlockMakeSound((PushBlockObject *)this,
				   level_staticSoundMap[ST_PUSHBLOCK]+2);
		delay_moveObject((Object *)this,objectRunList);
	       }
	   }
	if (param1==this->channel+10000)
	   {this->direction=-1;
	    if (this->state==AI_UPDOWNELEVATOR_SLEEP)
	       {this->state=AI_UPDOWNELEVATOR_GO;
		pushBlockMakeSound((PushBlockObject *)this,
				   level_staticSoundMap[ST_PUSHBLOCK]+2);
		delay_moveObject((Object *)this,objectRunList);
	       }
	   }
	break;
     case SIGNAL_MOVE:
	switch (this->state)
	   {case AI_UPDOWNELEVATOR_GO:
	       {int wait=0;
		pushBlockAdjustSound((PushBlockObject *)this);
		pbObject_move((PushBlockObject *)this,this->direction*F(5));
		if (this->direction==-1 && this->offset<=-F(this->throw))
		   {pbObject_moveTo((PushBlockObject *)this,-this->throw);
		    wait=1;
		   }
		if (this->direction==1 && this->offset>=0)
		   {pbObject_moveTo((PushBlockObject *)this,0);
		    wait=1;
		   }
		if (wait)
		   {this->state=AI_UPDOWNELEVATOR_SLEEP;
		    stopAllSound((int)this);
		    pushBlockMakeSound((PushBlockObject *)this,
				       level_staticSoundMap[ST_PUSHBLOCK]+1);
		    this->counter=0;
		    delay_moveObject((Object *)this,objectIdleList);
		   }
		break;
	       }
	      }
       }
}


Object *constructUpDownElevator(int pb,int type,
				short lowerLevel,short upperLevel)
{UpDownElevatorObject *this=(UpDownElevatorObject *)
    getFreeObject(upDownElevator_func,type,CLASS_PUSHBLOCK);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 registerPBObject(pb,(Object *)this);
 this->pbNum=pb;
 this->state=AI_UPDOWNELEVATOR_SLEEP;
 this->counter=0;
 this->throw=upperLevel-lowerLevel;
 this->stepEnable=0;
 this->channel=suckShort();
 dPrint("upDownElevator with channel %d\n",this->channel);
 this->offset=0;
 this->direction=-1;
 return (Object *)this;
}


/********************************\
 *      FLOORSWITCH STUFF       *
\********************************/

enum  {AI_FLOORSWITCH_SLEEP,
	  AI_FLOORSWITCH_GODOWN,
	  AI_FLOORSWITCH_DOWN,
	  AI_FLOORSWITCH_GOUP};

void floorSwitch_func(Object *_this,int msg,int param1,int param2)
{FloorSwitchObject *this=(FloorSwitchObject *)_this;
 switch (msg)
    {case SIGNAL_FLOORCONTACT:
	if (this->state==AI_FLOORSWITCH_SLEEP)
	   {this->state=AI_FLOORSWITCH_GODOWN;
	    this->counter=0;
	    delay_moveObject((Object *)this,objectRunList);
	   }
	break;
     case SIGNAL_SWITCHRESET:
	if (param1==this->channel &&
	    this->state==AI_FLOORSWITCH_DOWN)
	   {pushBlockMakeSound((PushBlockObject *)this,
			       level_objectSoundMap[OT_FLOORSWITCH]);
	    this->state=AI_FLOORSWITCH_GOUP;
	    this->counter=0;
	    delay_moveObject((Object *)this,objectRunList);
	   }
	break;
     case SIGNAL_MOVE:
	switch (this->state)
	   {case AI_FLOORSWITCH_GODOWN:
	       movePushBlock(this->pbNum,0,
			     (this->lowerLevel-this->upperLevel)>>1,0);
	       this->counter++;
	       if (this->counter==2)
		  {delay_moveObject((Object *)this,objectIdleList);
		   pushBlockMakeSound((PushBlockObject *)this,
				      level_objectSoundMap[OT_FLOORSWITCH]);
		   this->state=AI_FLOORSWITCH_DOWN;
		   signalAllObjects(SIGNAL_SWITCH,this->channel,0);
		   this->counter=0;
		  }
	       break;
	    case AI_FLOORSWITCH_GOUP:
	       movePushBlock(this->pbNum,0,
			     -((this->lowerLevel-this->upperLevel)>>1),0);
	       this->counter++;
	       if (this->counter==2)
		  {delay_moveObject((Object *)this,objectIdleList);
		   this->state=AI_FLOORSWITCH_SLEEP;
		  }
	      }
       }
}


Object *constructFloorSwitch(int pb)
{FloorSwitchObject *this=(FloorSwitchObject *)
    getFreeObject(floorSwitch_func,OT_FLOORSWITCH,CLASS_PUSHBLOCK);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 registerPBObject(pb,(Object *)this);
 this->pbNum=pb;
 this->state=AI_FLOORSWITCH_SLEEP;
 this->counter=0;
 this->lowerLevel=suckShort();
 this->upperLevel=suckShort();
 this->channel=suckShort();
 return (Object *)this;
}


/********************************\
 *       FORCEFIELD STUFF       *
\********************************/
enum  {AI_FFIELD_ON,AI_FFIELD_OFF};

void ffield_func(Object *_this,int msg,int param1,int param2)
{FFieldObject *this=(FFieldObject *)_this;
 switch (msg)
    {case SIGNAL_MOVE:
	switch (this->state)
	   {case AI_FFIELD_ON:
	       if (AISLOT(0x0f))
		  {sWallType *w=level_wall+this->wallNm;
		   if (!(currentState.inventory & INV_SCEPTER))
		      break;
		   if (approxDist(camera->pos.x-this->center.x,
				  camera->pos.y-this->center.y,
				  camera->pos.z-this->center.z)<F(200))
		      {this->state=AI_FFIELD_OFF;
		       {int vol,pan;
			posGetSoundParams(&this->center,&vol,&pan);
			playSoundE((int)this,
				   level_objectSoundMap[OT_FORCEFIELD],
				   vol,pan);
		       }
		       w->flags|=WALLFLAG_INVISIBLE;
		       w->flags&=~WALLFLAG_BLOCKED;
		      }
		  }
	       break;
	    case AI_FFIELD_OFF:
	       if (AISLOT(0x0f))
		  {sWallType *w=level_wall+this->wallNm;
		   if (approxDist(camera->pos.x-this->center.x,
				  camera->pos.y-this->center.y,
				  camera->pos.z-this->center.z)>F(300))
		      {this->state=AI_FFIELD_ON;
		       {int vol,pan;
			posGetSoundParams(&this->center,&vol,&pan);
			playSoundE((int)this,
				   level_objectSoundMap[OT_FORCEFIELD]+1,
				   vol,pan);
		       }
		       w->flags&=~WALLFLAG_INVISIBLE;
		       w->flags|=WALLFLAG_BLOCKED;
		      }
		  }
	       break;
	      }
       }
}

Object *constructForceField(int wallNm)
{MthXyz center,v;
 int i;
 FFieldObject *this=(FFieldObject *)
    getFreeObject(ffield_func,OT_FORCEFIELD,CLASS_WALL);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);
 this->aiSlot=nextAiSlot++;
 this->wallNm=wallNm;
 this->state=AI_FFIELD_ON;
 center.x=0; center.y=0; center.z=0;
 for (i=0;i<4;i++)
    {getVertex(level_wall[wallNm].v[i],&v);
     center.x+=v.x;
     center.y+=v.y;
     center.z+=v.z;
    }
 center.x>>=2; center.y>>=2; center.z>>=2;
 this->center=center;

 return (Object *)this;
}



/******************\
 * BUBBLES STUFF  *
\******************/

unsigned short bubbleSequenceMap[]={0,1,2};

void bubble_func(Object *_this,int msg,int param1,int param2)
{BubbleObject *this=(BubbleObject *)_this;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	spriteAdvanceFrame(this->sprite);
	this->sprite->pos.y+=F(2);
	this->distLeft-=F(2);
	if (this->distLeft<=0)
	   delayKill(_this);
	break;
       }
}

Object *constructBubble(int sector,MthXyz *pos,int distToCiel)
{/* static int type=0; */
 BubbleObject *this=(BubbleObject *)
    getFreeObject(bubble_func,OT_BUBBLES,CLASS_SPRITE);

 level_sequenceMap[OT_BUBBLES]=level_sequenceMap[OT_1BUBBLE];

 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 this->sprite=newSprite(sector,F(16),F(1),0,
			0,SPRITEFLAG_IMATERIAL|SPRITEFLAG_IMMOBILE,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 this->sprite->scale=30000+(unsigned short)getNextRand();

 moveObject((Object *)this,objectRunList);
 this->sprite->pos=*pos;
 this->sequenceMap=bubbleSequenceMap;

#if 0
 setState((SpriteObject *)this,type++);
 if (type>2)
    type=0;
#endif
 setState((SpriteObject *)this,0);

 this->distLeft=abs(distToCiel);
 return (Object *)this;
}

/******************\
 * DROPLET STUFF  *
\******************/

unsigned short oneBubbleSequenceMap[]={0};

void oneBubble_func(Object *_this,int msg,int param1,int param2)
{OneBubbleObject *this=(OneBubbleObject *)_this;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	this->sprite->pos.x+=this->sprite->vel.x;
	this->sprite->pos.y+=this->sprite->vel.y;
	this->sprite->pos.z+=this->sprite->vel.z;
	this->sprite->vel.y+=GRAVITY<<1;
	this->sprite->scale-=600;
	if (this->sprite->scale<0)
	   {this->sprite->scale=1;
	    delayKill(_this);
	   }
	break;
       }
}

Object *constructOneBubble(int sector,MthXyz *pos,MthXyz *vel)
{OneBubbleObject *this=(OneBubbleObject *)
    getFreeObject(oneBubble_func,OT_1BUBBLE,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 if (!this)
    return NULL;
 this->sprite=newSprite(sector,F(1),F(1),-GRAVITY<<1,
			0,SPRITEFLAG_IMATERIAL,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos=*pos;
 this->sprite->vel=*vel;
 this->sprite->scale=64000;
 this->sequenceMap=oneBubbleSequenceMap;

 setState((SpriteObject *)this,0);
 return (Object *)this;
}



/******************\
 *   CAMEL STUFF  *
\******************/

unsigned short camelSequenceMap[]={0};

void camel_func(Object *_this,int msg,int param1,int param2)
{CamelObject *this=(CamelObject *)_this;
 int collide;
 int fflags;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	if (this->disableTime)
	   {if (!(--this->disableTime))
	       this->sprite->flags|=SPRITEFLAG_IMATERIAL;
	    break;
	   }
	collide=moveSprite(this->sprite);
	fflags=spriteAdvanceFrame(this->sprite);
	if (collide & COLLIDE_SPRITE)
	   {/* GCC14: any player may hail the camel; the travel it asks for is the game's */
	    if (mpIsPlayer(&(sprites[collide&0xffff])))
	       {playSound((int)this,level_objectSoundMap[OT_CAMEL]);
		currentState.gameFlags&=~GAMEFLAG_FIRSTLEVEL;
		kilmaatPuzzleNumber=0;
		playerGetCamel(this->toLevel);
		this->disableTime=60;
		this->sprite->flags&=~SPRITEFLAG_IMATERIAL;
	       }
	   }
	break;
       }
}

Object *constructCamel(int sector)
{short a,t;
 CamelObject *this=(CamelObject *)
    getFreeObject(camel_func,OT_CAMEL,CLASS_SPRITE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);
 this->sprite=newSprite(sector,F(48),F(1),0,
			0,SPRITEFLAG_IMATERIAL|SPRITEFLAG_IMMOBILE,
			(Object *)this);
 this->sprite->pos.x=F(suckShort()); /* mirror <<<<<< */
 this->sprite->pos.y=F(suckShort());
 this->sprite->pos.z=F(suckShort());
 a=suckShort();

 t=-1;
 if (a==0)
    t=1;
 if (a==1024)
    t=0;
 if (a==2048)
    t=3;
 if (a==2048+1024)
    t=2;
 assert(t!=-1);
 this->toLevel=getMapLink(currentState.currentLevel,t);
 /*assert(this->toLevel>=0);*/
 if (this->toLevel<0)
    this->toLevel=currentState.currentLevel;
 dPrint("from %d dir %d (%d)\n",currentState.currentLevel,t,a);
 dPrint("camel to level %d\n",this->toLevel);

 this->sequenceMap=camelSequenceMap;
 this->disableTime=0;
 setState((SpriteObject *)this,0);
 return (Object *)this;
}


/********************************\
 *           QEGG STUFF         *
\********************************/
static int nmQeggs=0;

enum {AI_QEGG_INCUBATE,AI_QEGG_HATCH,AI_QEGG_ATTACK,AI_QEGG_BITE};

unsigned short qeggSeqMap[]={17,16,HB|0,HB|8};

void qegg_func(Object *_this,int message,int param1,int param2)
{QeggObject *this=(QeggObject *)_this;
 int collide=0,fflags=0;
 switch (message)
    {case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {spriteObject_makeZorch((SpriteObject *)this);
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y,
			     this->sprite->pos.z,
			     OT_AIRGUTS,48000,0,0);
	    delayKill(_this);
	   }
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_OBJECTDESTROYED:
	if (((Object *)param1)==(Object *)this)
	   {freeSprite(this->sprite);
	    nmQeggs--;
	   }
	break;
     case SIGNAL_MOVE:
	collide=moveSprite(this->sprite);
	fflags=spriteAdvanceFrame(this->sprite);
	if (this->state==AI_QEGG_ATTACK && AISLOT(0x7))
	   if (this->sprite->pos.y<this->enemy->sprite->pos.y-F(10))
	      this->sprite->vel.y=F(1);
	   else
	      if (this->sprite->pos.y>this->enemy->sprite->pos.y+F(10))
		 this->sprite->vel.y=-F(1);
	      else
		 this->sprite->vel.y=0;
	switch (this->state)
	   {case AI_QEGG_INCUBATE:
	       this->age++;
	       if (this->age>60)
		  {setState((SpriteObject *)this,AI_QEGG_HATCH);
		   this->sprite->gravity=0;
		   this->sprite->flags&=~SPRITEFLAG_BOUNCY;
		  }
	       if (collide & COLLIDE_FLOOR)
		  {this->sprite->vel.x>>=1;
		   this->sprite->vel.y>>=1;
		   this->sprite->vel.z>>=1;
		  }
	       break;
	    case AI_QEGG_HATCH:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_QEGG_ATTACK);
	       break;
	    case AI_QEGG_ATTACK:
	       if (collide & COLLIDE_SPRITE)
		  if (sprites[collide&0xffff].owner==(Object *)this->enemy)
		     {/* bite them! */
		      setState((SpriteObject *)this,AI_QEGG_BITE);
		      this->sprite->vel.x=0;
		      this->sprite->vel.y=0;
		      this->sprite->vel.z=0;
		      break;
		     }
	       if (AISLOT(0x0f))
		  {PlotCourseToObject((SpriteObject *)this,
				      (SpriteObject *)this->enemy);
		   this->sprite->angle=
		      normalizeAngle(this->sprite->angle+randomAngle(6));
		   this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<3;
		   this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<3;
		  }
	       break;
	    case AI_QEGG_BITE:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {if (spriteDistApprox(this->sprite,
					this->enemy->sprite)<F(150))
		      {PlotCourseToObject((SpriteObject *)this,
					  (SpriteObject *)this->enemy);
		       assert(this->enemy->class!=CLASS_DEAD);
		       spriteObject_makeSound((SpriteObject *)this,0);
		       signalObject((Object *)this->enemy,SIGNAL_HURT,10,
				    (int)this);
		      }
		   setState((SpriteObject *)this,AI_QEGG_ATTACK);
		  }
	       break;
	      }
	break;
       }
}

Object *constructQEgg(int sector,MthXyz *pos,MthXyz *vel)
{QeggObject *this=(QeggObject *)getFreeObject(qegg_func,
					      OT_QUEENEGG,CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(16),F(1),GRAVITY<<1,
			0,SPRITEFLAG_BWATER|SPRITEFLAG_BOUNCY,
			(Object *)this);
 this->sprite->pos=*pos;
 this->sprite->vel=*vel;
 this->sprite->scale=40000;
 this->sequenceMap=qeggSeqMap;
 setState((SpriteObject *)this,AI_QEGG_INCUBATE);
 this->health=40;
 this->routePos=-1;
 this->enemy=player;
 this->age=0;
 this->aiSlot=nextAiSlot++;
 nmQeggs++;
 return (Object *)this;
}

/********************************\
 *          QUEEN STUFF        *
\********************************/

enum  {AI_QUEEN_IDLE,
	  AI_QUEEN_WALK,
	  AI_QUEEN_SHOOTTAIL,
	  AI_QUEEN_LOSETAIL,
	  AI_QUEEN_WALKNOTAIL,

	  AI_QUEEN_SHOOTMOUTH,
	  AI_QUEEN_SRA,
	  AI_QUEEN_HURTBLOOD,
	  AI_QUEEN_DEAD,

	  AI_QUEEN_PUSH,

	  AI_QUEEN_NMSEQ};

unsigned short queenSeqMap[]=
{
 HB|0,
 HB|0,
 HB|18,
 HB|45,
 HB|9,

 HB|27,
 HB|36,
 HB|54,
 55,
 HB|0,
};

#ifndef JAPAN
#define LOSETAIL 2000
#else
#define LOSETAIL 1500
#endif
void queen_func(Object *_this,int message,int param1,int param2)
{QueenObject *this=(QueenObject *)_this;
 int collide,i;
 static int lastSound=0;
 static int alternate=0;
 int fflags;
 collide=0;
 fflags=0;

 switch (message)
    {case SIGNAL_HURT:
	this->enemy=(MonsterObject *)player;
	if (this->state>=AI_QUEEN_HURTBLOOD &&
	    this->state<=AI_QUEEN_DEAD)
	   break;
	if (this->health>LOSETAIL &&
	    this->health-param1<=LOSETAIL)
	   {setState((SpriteObject *)this,AI_QUEEN_LOSETAIL);
	    this->sprite->vel.x=0;
	    this->sprite->vel.z=0;
	    {int x,z,angle;
	     angle=normalizeAngle(this->sprite->angle+F(180));
	     x=this->sprite->pos.x+(MTH_Cos(angle)<<4);
	     z=this->sprite->pos.z+(MTH_Sin(angle)<<4);
	     constructOneShot(this->sprite->s,
			      x,
			      this->sprite->pos.y+F(30),
			      z,
			      OT_GRENPOW,F(2),0,0);
	    }
	   }
	if (aicount-lastSound>80)
	   {playSound((int)this,
		      level_objectSoundMap[OT_QUEEN]+(alternate?3:4));
	    lastSound=aicount;
	    alternate=!alternate;
	   }
	this->health-=param1;
	this->sprite->flags|=SPRITEFLAG_FLASH;
	if (this->health<=0)
	   {setState((SpriteObject *)this,AI_QUEEN_HURTBLOOD);
	    this->sprite->vel.x=0;
	    this->sprite->vel.z=0;
	   }
	break;
     case SIGNAL_OBJECTDESTROYED:
	monsterObject_signalDestroyed((MonsterObject *)this,param1);
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_MOVE:
	if (this->state==AI_QUEEN_DEAD)
	   break;
	if (this->state!=AI_QUEEN_IDLE)
	   {collide=moveSprite(this->sprite);
	    fflags=spriteAdvanceFrame(this->sprite);
	    if (!this->enemy)
	       setState((SpriteObject *)this,AI_QUEEN_IDLE);
	   }
	switch (this->state)
	   {case AI_QUEEN_IDLE:
	       normalMonster_idle((MonsterObject *)this,2,0);
	       break;
	    case AI_QUEEN_HURTBLOOD:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {setState((SpriteObject *)this,AI_QUEEN_DEAD);
		   this->sprite->flags|=SPRITEFLAG_IMATERIAL;
		   constructOneShot(this->sprite->s,
				    this->sprite->pos.x,
				    this->sprite->pos.y,
				    this->sprite->pos.z,
				    OT_LANDGUTS,
				    48000,0,0);
		   for (i=0;i<2;i++)
		      {constructHardBit(this->sprite->s,&this->sprite->pos,
					level_sequenceMap[OT_QUEEN]+55,0,0);
		       constructHardBit(this->sprite->s,&this->sprite->pos,
					level_sequenceMap[OT_QUEEN]+58,0,0);
		       constructHardBit(this->sprite->s,&this->sprite->pos,
					level_sequenceMap[OT_QUEEN]+59,0,0);
		      }
		   constructQhead(this->sprite->s,
				  &this->sprite->pos);
		   delayKill(_this);
		  }
	       break;
	    case AI_QUEEN_LOSETAIL:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_QUEEN_WALKNOTAIL);
	       break;
	    case AI_QUEEN_SHOOTTAIL:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {if (this->fireCount--<=0)
		      setState((SpriteObject *)this,AI_QUEEN_WALK);
		  }
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {if (canSee(this->sprite,this->enemy->sprite))
		      {MthXyz ballPos;
		       MthXyz ballVel;
		       initProjectile(&this->sprite->pos,
				      &this->enemy->sprite->pos,
				      &ballPos,&ballVel,F(60),4);
		       constructSball(this->sprite->s,&ballPos,&ballVel,
				      (SpriteObject *)this,
				      getAngle(ballVel.x,ballVel.z),99,
				      45000);
		      }
		  }
	       break;
	    case AI_QUEEN_SHOOTMOUTH:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {MthXyz pos,vel;
		   vel.x=MTH_Cos(this->sprite->angle)<<2;
		   vel.z=MTH_Sin(this->sprite->angle)<<2;
		   vel.y=0;
		   pos=this->sprite->pos;
		   pos.x+=vel.x<<5;
		   pos.y+=-F(16);
		   pos.z+=vel.z<<5;
		   constructQEgg(this->sprite->s,&pos,&vel);
		  }
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  setState((SpriteObject *)this,AI_QUEEN_WALKNOTAIL);
	       break;
	    case AI_QUEEN_PUSH:
	       spriteAdvanceFrame(this->sprite);
	       if ((collide & COLLIDE_SPRITE) &&
		   sprites[collide&0xffff].owner==(Object *)this->enemy)
		  {/* push them! */
		   this->enemy->sprite->vel.x+=MTH_Cos(this->sprite->angle)<<4;
		   this->enemy->sprite->vel.z+=MTH_Sin(this->sprite->angle)<<4;
		   this->sprite->vel.x=0;
		   this->sprite->vel.y=0;
		   playSound((int)this,level_objectSoundMap[OT_QUEEN]+2);
		   setState((SpriteObject *)this,AI_QUEEN_WALK);
		   break;
		  }
	       if (this->chargeCounter++>60)
		  {setState((SpriteObject *)this,AI_QUEEN_WALK);
		   this->multiCharge=0;
		   break;
		  }
	       spriteHome((SpriteObject *)this,(SpriteObject *)this->enemy,
			  F(20),F(2));
	       this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<4;
	       this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<4;
	       break;
	    case AI_QUEEN_WALK:
	       if (this->multiCharge || (AISLOT(0x3f) && getNextRand()<60000))
		  {this->chargeCounter=0;
		   if (!this->multiCharge)
		      this->multiCharge=3;
		   else
		      this->multiCharge--;
		   PlotCourseToObject((SpriteObject *)this,
				      (SpriteObject *)this->enemy);
		   spriteObject_makeSound((SpriteObject *)this,1);
		   setState((SpriteObject *)this,AI_QUEEN_PUSH);
		   break;
		  }
	    case AI_QUEEN_WALKNOTAIL:
	       if (AISLOT(0x0f))
		  {PlotCourseToObject((SpriteObject *)this,
				      (SpriteObject *)this->enemy);
		   if ((getNextRand()&0xff)<190)
		      {this->sprite->angle=
			  normalizeAngle(this->sprite->angle+randomAngle(6));
		       this->sprite->vel.x=MTH_Cos(this->sprite->angle)<<2;
		       this->sprite->vel.z=MTH_Sin(this->sprite->angle)<<2;
		       break;
		      }
		   switch (this->state)
		      {case AI_QUEEN_WALK:
			  /* shoot player */
			  setState((SpriteObject *)this,AI_QUEEN_SHOOTTAIL);
			  this->fireCount=4;
			  this->sprite->vel.x=0;
			  this->sprite->vel.z=0;
			  break;
		       case AI_QUEEN_WALKNOTAIL:
			  /* shoot player */
			  if (nmQeggs>10)
			     break;
			  setState((SpriteObject *)this,AI_QUEEN_SHOOTMOUTH);
			  this->sprite->vel.x=0;
			  this->sprite->vel.z=0;
			  break;
			 }
		  }
	       break;
	      }
	break;
       }
}

Object *constructQueen(int sector)
{QueenObject *this=(QueenObject *)getFreeObject(queen_func,OT_QUEEN,
						  CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);

 moveObject((Object *)this,objectRunList);

 this->sprite=newSprite(sector,F(64),F(1),GRAVITY<<1,
			0,SPRITEFLAG_BWATERBNDRY|SPRITEFLAG_BCLIFF,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sprite->scale=65536*1.3;
 this->sequenceMap=queenSeqMap;
 setState((SpriteObject *)this,AI_QUEEN_IDLE);
#ifndef JAPAN
 this->health=5000;
#else
 this->health=3000;
#endif
 this->enemy=NULL;
 this->routePos=-1;
 this->aiSlot=nextAiSlot++;
 this->multiCharge=0;
 return (Object *)this;
}

/********************************\
 *          QHEAD  STUFF        *
\********************************/

enum {AI_QHEAD_WAIT,AI_QHEAD_RUN,AI_QHEAD_ATTACK};

unsigned short qheadSeqMap[]={HB|65,HB|65,HB|65};

void qhead_func(Object *_this,int message,int param1,int param2)
{QheadObject *this=(QheadObject *)_this;
 int collide,fflags,i;
 int c,s,wave;
 switch (message)
    {case SIGNAL_HURT:
	if (this->state==AI_QHEAD_WAIT)
	   break;
	this->sprite->flags|=SPRITEFLAG_FLASH;
	for (i=0;i<this->nmBalls;i++)
	   if (this->balls[i])
	      this->balls[i]->flags|=SPRITEFLAG_FLASH;
	this->health-=param1;
	this->nmBalls=this->health/80;
	if (this->nmBalls<0) this->nmBalls=0;
	if (this->nmBalls>NMQHEADBALLS) this->nmBalls=NMQHEADBALLS;
	if (this->health<=0)
	   {for (i=0;i<20;i++)
	       constructOneShot(this->sprite->s,
				this->sprite->pos.x+(getNextRand()<<6)-F(32),
				this->sprite->pos.y+(getNextRand()<<5)+F(16),
				this->sprite->pos.z+(getNextRand()<<6)-F(32),
				OT_GRENPOW,F(2),0,i*5);
	    signalAllObjects(SIGNAL_SWITCH,1010,0);
	    delayKill(_this);
	   }
	break;
     case SIGNAL_VIEW:
	setSequence((SpriteObject *)this);
	break;
     case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    {freeSprite(this->sprite);
	     for (i=0;i<NMQHEADBALLS;i++)
		if (this->balls[i])
		   freeSprite(this->balls[i]);
	    }
	 break;
	}
     case SIGNAL_MOVE:
	if (this->state==AI_QHEAD_WAIT)
	   {this->waitTime++;
	    if (moveSprite(this->sprite)&COLLIDE_FLOOR)
	       {this->sprite->vel.x=0;
		this->sprite->vel.z=0;
	       }
	    if (this->waitTime>170)
	       {setState((SpriteObject *)this,AI_QHEAD_ATTACK);
		this->sprite->gravity=0;
		this->balls[0]->flags&=~SPRITEFLAG_INVISIBLE;
		moveSpriteTo(this->balls[0],
			     this->sprite->s,
			     &this->sprite->pos);
		this->sprite->flags&=~SPRITEFLAG_BCLIFF;
	       }
	    else
	       break;
	   }
	for (i=NMQHEADBALLS-1;i>=this->nmBalls;i--)
	   {if (!this->balls[i])
	       continue;
	    collide=moveSprite(this->balls[i]);
	    if (collide & COLLIDE_FLOOR)
	       {constructOneShot(this->balls[i]->s,
				 this->balls[i]->pos.x,
				 this->balls[i]->pos.y+F(30),
				 this->balls[i]->pos.z,
				 OT_GRENPOW,F(2),0,0);
		freeSprite(this->balls[i]);
		this->balls[i]=NULL;
	       }
	   }
	for (i=this->nmBalls-1;i>0;i--)
	   {moveSpriteTo(this->balls[i],
			 this->balls[i-1]->s,
			 &this->balls[i-1]->pos);
	    this->balls[i]->flags=
	       this->balls[i-1]->flags;
	   }
	if (this->balls[0])
	   moveSpriteTo(this->balls[0],
			this->sprite->s,
			&this->sprite->pos);

	collide=moveSprite(this->sprite);
	fflags=spriteAdvanceFrame(this->sprite);
	/* do motion */
	c=MTH_Cos(this->sprite->angle);
	s=MTH_Sin(this->sprite->angle);
	this->sprite->vel.x=c<<4;
	this->sprite->vel.z=s<<4;

	wave=MTH_Cos(this->wave);
	this->wave+=F(20);
	if (this->wave>F(180))
	   this->wave-=F(360);
	this->sprite->vel.x+=MTH_Mul(s,wave)<<2;
	this->sprite->vel.z+=MTH_Mul(-c,wave)<<2;

	switch (this->state)
	   {case AI_QHEAD_ATTACK:
	       spriteHome((SpriteObject *)this,(SpriteObject *)player,
			  F(20),F(15));
	       if (this->sprite->pos.y<player->sprite->pos.y-F(3))
		  this->sprite->vel.y=F(2);
	       else
		  if (this->sprite->pos.y>player->sprite->pos.y+F(3))
		     this->sprite->vel.y=-F(2);
		  else
		     this->sprite->vel.y=0;
	       if ((collide & COLLIDE_SPRITE) &&
		   (sprites[collide&0xffff].owner==(Object *)player))
		  {signalObject((Object *)player,SIGNAL_HURT,10,
				(int)this);
		   PlotCourseToObject((SpriteObject *)this,
				      (SpriteObject *)player);
		   this->sprite->angle+=F(180-32)+F(getNextRand()&0x3f);
		   setState((SpriteObject *)this,AI_QHEAD_RUN);
		   this->sprite->vel.y=0;
		   this->waitTime=60;
		  }
	       break;
	    case AI_QHEAD_RUN:
	       if (collide & COLLIDE_WALL)
		  this->sprite->angle=
		     normalizeAngle(this->sprite->angle+
				    F(getNextRand()<<6)-F(32));
	       this->waitTime--;
	       if (this->waitTime<=0)
		  setState((SpriteObject *)this,AI_QHEAD_ATTACK);
	       break;
	      }
	break;
       }
}

Object *constructQhead(int sector,MthXyz *pos)
{int i;
 QheadObject *this=(QheadObject *)getFreeObject(qhead_func,OT_QHEAD,
						CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;

 this->sprite=newSprite(sector,F(16),F(1),GRAVITY<<1,
			0,SPRITEFLAG_BCLIFF,(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->pos=*pos;
 this->sprite->scale=65536*1.1;
 this->sprite->vel.y=F(10);
 this->sprite->vel.x=F(6);
 this->sprite->vel.z=F(2);
 this->sequenceMap=qheadSeqMap;
 setState((SpriteObject *)this,AI_QHEAD_WAIT);

 for (i=0;i<NMQHEADBALLS;i++)
    {this->balls[i]=newSprite(sector,F(16),F(1),0,
			      level_sequenceMap[OT_QUEEN]+73,
			      SPRITEFLAG_INVISIBLE|SPRITEFLAG_NOSPRCOLLISION,
			      (Object *)this);
     this->balls[i]->pos=*pos;
     this->balls[i]->scale=60000-i*3000;
     /* prepare for falling off snake */
     this->balls[i]->vel.x=(MTH_GetRand()&0x7ffff)-F(4);
     this->balls[i]->vel.y=(MTH_GetRand()&0x3fffff);
     this->balls[i]->vel.z=(MTH_GetRand()&0x7ffff)-F(4);
     this->balls[i]->gravity=GRAVITY<<2;
    }
 this->wave=0;
 this->waitTime=0;
 this->enemy=NULL;
 this->aiSlot=nextAiSlot++;
 this->health=400;
 this->nmBalls=NMQHEADBALLS;
 return (Object *)this;
}

/********************************\
 *        TELEPORT STUFF        *
\********************************/
enum  {AI_TELEPORT_WAIT,AI_TELEPORT_TURNON,AI_TELEPORT_TURNOFF,
	  AI_TELEPORT_ON};

void teleport_func(Object *_this,int msg,int param1,int param2)
{TeleportObject *this=(TeleportObject *)_this;
 switch (msg)
    {case SIGNAL_FLOORCONTACT:
	break;
     case SIGNAL_MOVE:
	if (camera->s!=this->sectorNm)
	   this->enable=1;
	if (!this->enable)
	   break;
	switch (this->state)
	   {case AI_TELEPORT_WAIT:
	       if (AISLOT(0x1f))
		  {if (approxDist(camera->pos.x-F(this->pSector->center[0]),
				  camera->pos.y-F(this->pSector->center[1]),
				  camera->pos.z-F(this->pSector->center[2])
				  )<F(600))
		      this->state=AI_TELEPORT_TURNON;
		  }
	       break;
	    case AI_TELEPORT_TURNON:
	       if (AISLOT(1))
		  {setSectorBrightness(this->sectorNm,this->powerLevel++);
		   if (this->powerLevel>=20)
		      this->state=AI_TELEPORT_ON;
		  }
	       break;
	    case AI_TELEPORT_ON:
	       if (camera->floorSector==this->sectorNm)
		  {if (this->endOfPuzzle)
		      kilmaatPuzzleNumber=currentState.currentLevel;
		   else
		      kilmaatPuzzleNumber=currentState.currentLevel+1000;
		   playerHitTeleport(this->toLevel);
		  }
	       setSectorBrightness(this->sectorNm,this->powerLevel+
				   (MTH_Sin(this->angle)>>14));
	       this->angle=normalizeAngle(this->angle+F(9));
	       if (this->angle==0 || this->angle==F(180))
		  {if (approxDist(camera->pos.x-F(this->pSector->center[0]),
				  camera->pos.y-F(this->pSector->center[1]),
				  camera->pos.z-F(this->pSector->center[2])
				  )>F(650))
		      {this->angle=0;
		       this->state=AI_TELEPORT_TURNOFF;
		      }
		  }
	       break;
	    case AI_TELEPORT_TURNOFF:
	       if (AISLOT(1))
		  {setSectorBrightness(this->sectorNm,this->powerLevel--);
		   if (this->powerLevel<=0)
		      this->state=AI_TELEPORT_WAIT;
		  }
	       break;
	      }
       }
}

Object *constructTeleporter(void)
{TeleportObject *this=(TeleportObject *)
    getFreeObject(teleport_func,OT_TELEPSECTOR,CLASS_SECTOR);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);
 this->aiSlot=nextAiSlot++;
 this->sectorNm=suckShort();
 this->endOfPuzzle=suckShort();
 this->toLevel=suckShort();
 this->state=AI_TELEPORT_WAIT;
 this->powerLevel=0;
 this->pSector=level_sector+this->sectorNm;
 this->angle=0;
 this->countDown=-1;
 this->enable=0;
 markSectorFloor(this->sectorNm,(Object *)this);
 setSectorBrightness(this->sectorNm,0);
 return (Object *)this;
}

#if 0
/********************************\
 *     TELEPORTRETURN STUFF     *
\********************************/
enum  {AI_TELEPORTRETURN_WAIT,AI_TELEPORTRETURN_TAKEOBJECT,
	 AI_TELEPORTRETURN_SPRINGDOOR};

void teleportReturn_func(Object *_this,int msg,int param1,int param2)
{TeleportReturnObject *this=(TeleportReturnObject *)_this;
 switch (msg)
    {case SIGNAL_MOVE:
	switch (this->state)
	   {case AI_TELEPORTRETURN_TAKEOBJECT:
	       this->age++;
	       if (this->age==-16)
		  {changeColorOffset(255,255,255,7);
		   addLight(this->artifact->sprite,0,0,0);
		   this->artifact->sprite->flags&=~SPRITEFLAG_INVISIBLE;
		  }
	       if (this->age<0)
		  break;
	       {int a=this->age;
		changeLightColor(this->artifact->sprite,a,a,a);
		a=31-(a>>1);
		this->artifact->sprite->color=RGB(a,a,a);
	       }
	       if (this->age==32)
		  {delay_moveObject((Object *)this,objectIdleList);
		   this->state=AI_TELEPORTRETURN_WAIT;
		   switchPlayerMotion(1);
		   removeLight(this->artifact->sprite);
		   /* switch door blockers */
		   signalAllObjects(SIGNAL_SWITCH,this->artifactNm+11000,0);
		   break;
		  }
	       break;
	    case AI_TELEPORTRETURN_SPRINGDOOR:
	       delay_moveObject((Object *)this,objectIdleList);
	       this->state=AI_TELEPORTRETURN_WAIT;
	       signalAllObjects(SIGNAL_SWITCH,this->artifactNm+11000,0);
	       break;
	      }
	break;
       }
}

Object *constructTeleportReturn(void)
{int playerSec,fromLevel,artifactPlace,artifact;
 TeleportReturnObject *this=(TeleportReturnObject *)
    getFreeObject(teleportReturn_func,OT_TELEPRETURN,CLASS_SECTOR);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 playerSec=suckShort();
 fromLevel=suckShort();
 artifactPlace=suckShort();
 artifact=suckShort();
 this->artifactNm=artifact;
 this->aiSlot=nextAiSlot++;
 this->state=AI_TELEPORTRETURN_WAIT;
 this->sectorNm=artifactPlace;
 this->artifact=NULL;
 this->age=-128;
/* kilmaatPuzzleNumber=fromLevel; */
 if (kilmaatPuzzleNumber==fromLevel || kilmaatPuzzleNumber==fromLevel+1000)
    {MthXyz pos;
     assert(camera);
     /* move player to position */
     pos.x=F(level_sector[playerSec].center[0]);
     pos.y=F(level_sector[playerSec].center[1]);
     pos.z=F(level_sector[playerSec].center[2]);
     pos.y+=SPR_FOOT(camera)-findFloorDistance(playerSec,&pos);
     moveSpriteTo(camera,playerSec,&pos);
     /* point player @ place where artifact will appear */
     camera->angle=getAngle(pos.x-F(level_sector[artifactPlace].center[0]),
			    pos.z-F(level_sector[artifactPlace].center[2]));
     camera->angle=normalizeAngle(camera->angle+F(90));
     playerAngle.yaw=camera->angle;
     if (kilmaatPuzzleNumber==fromLevel &&
	 (currentState.inventory & (INV_SANDALS<<artifact)))
	{currentState.inventory&=~(INV_SANDALS<<artifact);
	 switchPlayerMotion(0);
	 this->state=AI_TELEPORTRETURN_TAKEOBJECT;
	 moveObject((Object *)this,objectRunList);
	}
     kilmaatPuzzleNumber=0;
    }
 if (!(currentState.inventory & (INV_SANDALS<<artifact)))
    {/* player does not have artifact, so it must be on the pad */
     MthXyz pos;
     pos.x=F(level_sector[artifactPlace].center[0]);
     pos.y=F(level_sector[artifactPlace].center[1]);
     pos.z=F(level_sector[artifactPlace].center[2]);
     pos.y+=F(32)-findFloorDistance(artifactPlace,&pos);
     this->artifact=(SpriteObject *)
	constructSpecialArtifact(artifactPlace,
				 pos.x,pos.y,pos.z,
				 OT_SANDALS+artifact);
     /* ... move artifact closer to ground */
     assert(this->artifact);
     if (this->state!=AI_TELEPORTRETURN_TAKEOBJECT)
	{this->state=AI_TELEPORTRETURN_SPRINGDOOR;
	 moveObject((Object *)this,objectRunList);
	}
    }
 if (this->state==AI_TELEPORTRETURN_TAKEOBJECT)
    {assert(this->artifact);
     this->artifact->sprite->flags|=SPRITEFLAG_INVISIBLE|SPRITEFLAG_COLORED;
     this->artifact->sprite->color=RGB(31,31,31);
    }
 return (Object *)this;
}

#else

/********************************\
 *     TELEPORTRETURN STUFF     *
\********************************/
enum  {AI_TELEPORTRETURN_WAIT,AI_TELEPORTRETURN_TAKEOBJECT,
	 AI_TELEPORTRETURN_SPRINGDOOR,AI_TELEPORTRETURN_ALIGNPLAYER};

void teleportReturn_func(Object *_this,int msg,int param1,int param2)
{TeleportReturnObject *this=(TeleportReturnObject *)_this;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 dPrint("somethin killed!\n");
	 if (killed==(Object *)this->artifact)
	    {this->artifact=NULL;
	     dPrint("ours killed!");
	    }
	 break;
	}
     case SIGNAL_FLOORCONTACT:
	if (this->artifact)
	   break;
	if (this->state!=AI_TELEPORTRETURN_WAIT)
	   break;
	delay_moveObject((Object *)this,objectRunList);
	this->state=AI_TELEPORTRETURN_ALIGNPLAYER;
	switchPlayerMotion(0);
	this->age=0;
	break;
     case SIGNAL_MOVE:
	switch (this->state)
	   {case AI_TELEPORTRETURN_ALIGNPLAYER:
	       camera->vel.x+=(F(level_sector[this->playerSec].center[0])-
			       camera->pos.x)>>5;
	       camera->vel.z+=(F(level_sector[this->playerSec].center[2])-
			       camera->pos.z)>>5;
	       camera->vel.x-=camera->vel.x>>3;
	       camera->vel.z-=camera->vel.z>>3;
	       playerAngle.yaw=
		  normalizeAngle(playerAngle.yaw+
				 ((this->playerAngle-playerAngle.yaw)>>3));
	       playerAngle.pitch-=playerAngle.pitch>>3;
	       playerAngle.roll=0;
	       if (this->age++>=30)
		  {this->state=AI_TELEPORTRETURN_TAKEOBJECT;
		   this->age=-32;
		   currentState.inventory&=~(INV_SANDALS<<this->artifactNm);

		   this->artifact=(SpriteObject *)
		      constructSpecialArtifact(this->artifactPlace,
					       this->apos.x,this->apos.y,
					       this->apos.z,
					       OT_SANDALS+this->artifactNm);
		   assert(this->artifact);
		   this->artifact->sprite->flags|=
		      SPRITEFLAG_INVISIBLE|SPRITEFLAG_COLORED;
		   this->artifact->sprite->color=RGB(31,31,31);
		  }
	       break;
	    case AI_TELEPORTRETURN_TAKEOBJECT:
	       this->age++;
	       if (this->age==-16)
		  {changeColorOffset(255,255,255,7);
		   addLight(this->artifact->sprite,0,0,0);
		   this->artifact->sprite->flags&=~SPRITEFLAG_INVISIBLE;
		  }
	       if (this->age<0)
		  break;
	       {int a=this->age;
		changeLightColor(this->artifact->sprite,a,a,a);
		a=31-(a>>1);
		this->artifact->sprite->color=RGB(a,a,a);
	       }
	       if (this->age==32)
		  {delay_moveObject((Object *)this,objectIdleList);
		   this->state=AI_TELEPORTRETURN_WAIT;
		   switchPlayerMotion(1);
		   removeLight(this->artifact->sprite);
		   /* switch door blockers */
		   signalAllObjects(SIGNAL_SWITCH,this->artifactNm+11000,0);
		   break;
		  }
	       break;
	    case AI_TELEPORTRETURN_SPRINGDOOR:
	       delay_moveObject((Object *)this,objectIdleList);
	       this->state=AI_TELEPORTRETURN_WAIT;
	       signalAllObjects(SIGNAL_SWITCH,this->artifactNm+11000,0);
	       break;
	      }
	break;
       }
}

Object *constructTeleportReturn(void)
{int fromLevel,artifactPlace,artifact,w;
 MthXyz pos;
 TeleportReturnObject *this=(TeleportReturnObject *)
    getFreeObject(teleportReturn_func,OT_TELEPRETURN,CLASS_SECTOR);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 this->playerSec=suckShort();
 for (w=level_sector[this->playerSec].firstWall;
      w<=level_sector[this->playerSec].lastWall;w++)
    if (level_wall[w].normal[1]>0)
       level_wall[w].object=this;
 fromLevel=suckShort();
 artifactPlace=suckShort();
 this->artifactPlace=artifactPlace;
 artifact=suckShort();
 this->artifactNm=artifact;
 this->aiSlot=nextAiSlot++;
 this->state=AI_TELEPORTRETURN_WAIT;
 this->sectorNm=artifactPlace;
 pos.x=F(level_sector[artifactPlace].center[0]);
 pos.y=F(level_sector[artifactPlace].center[1]);
 pos.z=F(level_sector[artifactPlace].center[2]);
 pos.y+=F(32)-findFloorDistance(artifactPlace,&pos);
 this->apos=pos;
 this->artifact=NULL;
 this->age=-128;
 this->playerAngle=getAngle(pos.x-F(level_sector[this->playerSec].center[0]),
			    pos.z-F(level_sector[this->playerSec].center[2]));
 this->playerAngle=normalizeAngle(this->playerAngle-F(90));
 dPrint("playerAngle=%d\n",this->playerAngle);
 if (!(currentState.inventory & (INV_SANDALS<<artifact)))
    {/* player does not have artifact, so it must be on the pad */
     this->artifact=(SpriteObject *)
	constructSpecialArtifact(artifactPlace,
				 pos.x,pos.y,pos.z,
				 OT_SANDALS+artifact);
     assert(this->artifact);
     this->state=AI_TELEPORTRETURN_SPRINGDOOR;
     moveObject((Object *)this,objectRunList);
    }
 return (Object *)this;
}
#endif

/********************************\
 *         SHOOTER STUFF        *
\********************************/
enum {AI_SHOOTER_ON,AI_SHOOTER_OFF};

static void laserSetVisib(ShooterObject *this)
{int i;
 for (i=0;i<this->nmLines;i++)
    {if (this->state==AI_SHOOTER_ON)
	this->beam[i]->flags&=~SPRITEFLAG_INVISIBLE;
     else
	this->beam[i]->flags|=SPRITEFLAG_INVISIBLE;
    }
}

void shooter_func(Object *_this,int msg,int param1,int param2)
{ShooterObject *this=(ShooterObject *)_this;
 switch (msg)
    {case SIGNAL_SWITCH:
	if (param1!=this->channel &&
	    (this->switchType!=2 || this->channel+1!=param1))
	   break;
	dPrint("switch channel %d (sw=%d)\n",param1,this->switchType);
	this->counter=300;
	switch (this->switchType)
	   {case 0:
	       /* switched off */
	       this->state=AI_SHOOTER_OFF;
	       break;
	    case 1:
	       /* switched on */
	       this->state=AI_SHOOTER_ON;
	       break;
	    case 2:
	       /* toggled */
	       if (this->channel+1==param1)
		  {this->state=AI_SHOOTER_OFF;
		   signalAllObjects(SIGNAL_SWITCHRESET,this->channel,0);
		  }
	       else
		  {this->state=AI_SHOOTER_ON;
		   signalAllObjects(SIGNAL_SWITCHRESET,this->channel+1,0);
		  }
	       break;
	      }
	if (this->type==OT_SHOOTER3)
	   laserSetVisib(this);
	break;
     case SIGNAL_MOVE:
	if (this->counter)
	   {this->counter--;
	    if (!this->counter)
	       {switch (this->switchType)
		   {case 0:
		       this->state=AI_SHOOTER_ON;
		       signalAllObjects(SIGNAL_SWITCHRESET,this->channel,0);
		       break;
		    case 1:
		       this->state=AI_SHOOTER_OFF;
		       signalAllObjects(SIGNAL_SWITCHRESET,this->channel,0);
		       break;
		      }
		if (this->type==OT_SHOOTER3)
		   laserSetVisib(this);
	       }
	   }
	switch (this->state)
	   {case AI_SHOOTER_OFF:
	       break;
	    case AI_SHOOTER_ON:
	       switch (this->type)
		  {case OT_SHOOTER1:
		   case OT_SHOOTER2:
		      if (AISLOT(0x3f))
			 {MthXyz vel,pos;
			  pos=this->orficePos;
			  pos.x+=this->normal.x<<4;
			  pos.y+=this->normal.y<<4;
			  pos.z+=this->normal.z<<4;

			  if (this->type==OT_SHOOTER1)
			     {pos.y+=F(8);
			      vel.x=this->normal.x<<4;
			      vel.y=this->normal.y<<4;
			      vel.z=this->normal.z<<4;
			      constructGenproj(this->sectorNm,&pos,
					       &vel,NULL,getAngle(vel.x,vel.z),
					       1,OT_MAGBALL,100,
					       RGB(10,10,10),0,0,0,5);
			      {int vol,pan;
			       posGetSoundParams(&pos,&vol,&pan);
			       playSoundE((int)this,
					  level_objectSoundMap[OT_SHOOTER1],
					  vol,pan);
			      }
			     }
			  else
			     {vel.x=this->normal.x<<3;
			      vel.y=(this->normal.y<<3)+F(8);
			      vel.z=this->normal.z<<3;

			      constructBoingRock(this->sectorNm,
						 &this->orficePos,
						 &vel);
			     }
			 }
		      break;
		   case OT_SHOOTER3:
		      /* if (AISLOT(0x3)) */
		      if (level_sector[camera->s].flags & SECFLAG_LASERSECTOR)
			 {int i,collide,outSec;
			  MthXyz outPos;
			  for (i=0;i<this->nmLines;i++)
			     if (camera->s==this->beam[i]->s)
				break;
			  if (i<this->nmLines)
			     {collide=hitScan(NULL,&this->normal,
					      &this->orficePos,
					      this->sectorNm,
					      &outPos,&outSec);
			      if (collide & COLLIDE_SPRITE)
				 {signalObject(sprites[collide&0xffff].owner,
					       SIGNAL_HURT,1000,
					       (int)this);
				 }
			     }
			 }
		      break;
		     }
	       break;
	      }
	break;
       }
}

Object *constructShooter(int type)
{ShooterObject *this=(ShooterObject *)
    getFreeObject(shooter_func,type,CLASS_WALL);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);
 this->sectorNm=suckShort();
 this->wallNm=suckShort();
 this->switchType=suckShort();
 this->channel=suckShort();
 this->orficePos.x=F(suckShort()); /* mirror <<<<<<<< */
 this->orficePos.y=F(suckShort());
 this->orficePos.z=F(suckShort());
 this->normal.x=suckInt();
 this->normal.y=suckInt();
 this->normal.z=suckInt();
 if (this->switchType==0 || this->switchType==-1)
    this->state=AI_SHOOTER_ON;
 else
    this->state=AI_SHOOTER_OFF;
 if (this->switchType==3)
    {this->state=AI_SHOOTER_ON;
     this->switchType=2;
    }
 this->aiSlot=nextAiSlot++;
 this->pulse=0;
 this->counter=0;
 this->nmLines=0;
 if (type==OT_SHOOTER3)
    {MthXyz hitPos;
     MthXyz lastPos;
     int s,lastSec;
     int collide;
     int nmLines;
     lastPos=this->orficePos;
     lastSec=this->sectorNm;
     nmLines=0;
     while (1)
	{assert(nmLines<NMSHOOTERBEAMS);
	 level_sector[lastSec].flags|=SECFLAG_LASERSECTOR;
	 this->beam[nmLines]=newSprite(lastSec,F(1),F(1),0,
				       level_sequenceMap[OT_HEALTHBALL],
				       SPRITEFLAG_IMATERIAL|SPRITEFLAG_LINE|
				       SPRITEFLAG_THINLINE,
				       (Object *)this);
	 assert(this->beam[nmLines]);
	 this->beam[nmLines]->color=RGB(15,15,31);
	 this->beam[nmLines]->pos=lastPos;
	 collide=singleSectorWallHitScan(&this->normal,&lastPos,lastSec,
					 &hitPos,&s);
	 if (!collide)
	    break;
	 lastPos=hitPos;
	 lastSec=s;
	 this->beam[nmLines]->angle=hitPos.x;
	 this->beam[nmLines]->scale=hitPos.y;
	 this->beam[nmLines]->frame=hitPos.z;
	 nmLines++;
	 if (collide!=2)
	    break;
	}
     this->nmLines=nmLines;
    }
 laserSetVisib(this);
 return (Object *)this;
}

/********************************\
 *        BOINGROCK STUFF       *
\********************************/
unsigned short rockSeqList[]={0};

void boingRock_func(Object *_this,int msg,int param1,int param2)
{BoingRockObject *this=(BoingRockObject *)_this;
 int collide;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_VIEW:
	break;
     case SIGNAL_MOVE:
	collide=moveSprite(this->sprite);
	if (collide & COLLIDE_SPRITE)
	   {signalObject(sprites[collide&0xffff].owner,
			 SIGNAL_HURT,50,
			 (int)this);
	   }
	else
	   {if ((collide & (COLLIDE_WALL|COLLIDE_FLOOR|COLLIDE_CEILING)) &&
		(level_wall[collide&0xffff].flags & WALLFLAG_EXPLODABLE))
	      explodeAllMaskedWallsInSector(findWallsSector(collide & 0xffff));
	   }
	if (collide & (COLLIDE_FLOOR|COLLIDE_WALL|COLLIDE_CEILING))
	   {int vol,pan;
	    this->age++;
	    spriteAdvanceFrame(this->sprite);
	    posGetSoundParams(&this->sprite->pos,&vol,&pan);
	    vol+=20;
	    playSoundE((int)this,level_objectSoundMap[OT_SHOOTER2],vol,pan);
	   }
	if ((collide & COLLIDE_SPRITE) || this->age>10)
	   {int i;
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y+F(20),
			     this->sprite->pos.z,
			     OT_POOF,F(1),RGB(28,5,5),0);
	    for (i=0;i<3;i++)
	       constructBouncyBit(this->sprite->s,&this->sprite->pos,
				  this->sprite->sequence+1,0,0);
	    delayKill(_this);
	   }
	break;
       }
}

Object *constructBoingRock(int sector,MthXyz *pos,MthXyz *vel)
{BoingRockObject *this;
 this=(BoingRockObject *)
    getFreeObject(boingRock_func,OT_BOINGROCK,CLASS_PROJECTILE);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (!this)
    return NULL;
 this->sprite=newSprite(sector,F(16),F(1),GRAVITY<<2,
			0,SPRITEFLAG_IMATERIAL|SPRITEFLAG_BOUNCY,
			(Object *)this);
 if (!this->sprite)
    return NULL;
 moveObject((Object *)this,objectRunList);
 this->sprite->scale=F(1);
 this->sprite->pos=*pos;
 this->sprite->vel=*vel;
 this->owner=NULL;
 this->sequenceMap=rockSeqList;
 this->age=0;
 setState((SpriteObject *)this,0);
 return (Object *)this;
}



void resetLimitCounters(void)
{nmBouncyBits=0;
 nmBits=0;
}

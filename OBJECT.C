#include "level.h"
#include "sprite.h"
#include "object.h"
#include "util.h"
#include "ai.h"
#include "sruins.h"
#include "sound.h"

Object objects[MAXOBJECTS];

/* GCC14: what the pool could not give.  A level that asks for more objects than MAXOBJECTS used
   to be a silent memory fault: getFreeObject returned NULL, and moveObject read o->prev from
   address 8 -- the boot ROM -- and stored through what it read, anywhere in memory.  Refused and
   counted now; the overlay shows obj: and lost: (SRUINS.C). */
int objectsRefused;

/* the pool's free entries; walked, so only for the overlay */
int objectsFree(void)
{Object *o;
 int n=0;
 for (o=objectFreeList->next;o;o=o->next)
    n++;
 return n;
}

Object *objectRunList; /* these lists have head nodes */
Object *objectIdleList;
Object *objectFreeList;

#define MAXNMMOVES 256          /* GCC14: 100 -- a level's placement alone can queue more */
static struct {Object *o,*toList;} objMove[MAXNMMOVES];
static int nmMoves;
int nmMovesRefused;             /* GCC14: moves the queue could not take (OBJECT.H) */
void delay_moveObject(Object *o,Object *toList)
{if (!o)                        /* GCC14: a refused object (getFreeObject) reaches no list */
    return;
 assert(nmMoves<MAXNMMOVES);
 if (nmMoves>=MAXNMMOVES)
    {/* GCC14: the assert is compiled out of the disc builds, and this wrote past the array --
	over the statics behind it.  A dropped move leaves an object in the list it is in. */
     nmMovesRefused++;
     return;
    }
 objMove[nmMoves].o=o;
 objMove[nmMoves].toList=toList;
 nmMoves++;
}

void moveObject(Object *o,Object *toList)
{if (!o)                        /* GCC14: NEVER dereference a refused object -- o->prev would be
    return;                        read from the boot ROM and stored through */
 assert(o!=objectRunList);
 assert(o!=objectIdleList);
 assert(o!=objectFreeList);
 assert(toList==objectRunList ||
	toList==objectFreeList ||
	toList==objectIdleList);

 assert(o->prev);
 o->prev->next=o->next;
 if (o->next)
    o->next->prev=o->prev;

 o->next=toList->next;
 if (o->next)
    o->next->prev=o;
 toList->next=o;
 o->prev=toList;
}

void processDelayedMoves(void)
{int i;
 for (i=0;i<nmMoves;i++)
    moveObject(objMove[i].o,objMove[i].toList);
 nmMoves=0;
}

void initObjects(void)
{int i;
 resetLimitCounters();
 objectRunList=objects+0;
 objectIdleList=objects+1;
 objectFreeList=objects+2;

 objectRunList->next=NULL;
 objectIdleList->next=NULL;
 objectRunList->prev=NULL;
 objectIdleList->prev=NULL;

 for (i=3;i<MAXOBJECTS-1;i++)
    {objects[i].type=OT_DEAD;
     objects[i].next=&(objects[i+1]);
     objects[i].prev=&(objects[i-1]);
    }
 objectFreeList->prev=NULL;
 objectFreeList->next=objects+3;
 objects[i].prev=&(objects[i-1]);
 objects[i].next=NULL;
 objects[i].type=OT_DEAD;
 nmMoves=0;
}

Object *getFreeObject(messHandler handler,int type,int class)
{if (!objectFreeList->next)
    {objectsRefused++;          /* GCC14: the level wants more than MAXOBJECTS (overlay lost:) */
     return NULL;
    }
 assert(objectFreeList->next);
 /* assert(objectFreeList->next->type==OT_DEAD); */
 objectFreeList->next->type=type;
 objectFreeList->next->class=class;
 objectFreeList->next->func=handler;
 return objectFreeList->next;
}


void signalObject(Object *object,int message,int param1,int param2)
{
#ifndef NDEBUG
 checkStack();
 assert(object);
 assert(object!=objectRunList);
 assert(object!=objectIdleList);
 assert(object!=objectFreeList);
#if 1
 assert(object->type!=OT_DEAD); /* here */
 assert(object->type<OT_NMTYPES);
 assert(object->type>=0);
#endif
#endif
 object->func(object,message,param1,param2);
}

void signalList(Object *head,int message,int param1,int param2)
{Object *o;
 for (o=head->next;o;o=o->next)
    {assert(o->type>=0);
     o->func(o,message,param1,param2);
    }
}

void null_func(Object *_this,int message,int param1,int param2)
{return;
}

void delayKill(Object *o)
{int oldClass;
 int oldType;
 checkStack();
 assert(o->class!=CLASS_DEAD);
 assert(o->type!=OT_DEAD);
 signalObject(o,SIGNAL_OBJECTDESTROYED,(int)o,0);
 oldType=o->type;
 o->type=OT_DEAD;
 o->func=null_func;
 oldClass=o->class;
 o->class=CLASS_DEAD;
 delay_moveObject(o,objectFreeList);
 if (oldClass==CLASS_MONSTER ||
     (oldType>=OT_SANDALS && oldType<=OT_FEATHER)) /* just an optimization */
    {signalList(objectRunList,SIGNAL_OBJECTDESTROYED,(int)o,0);
     signalList(objectIdleList,SIGNAL_OBJECTDESTROYED,(int)o,0);
    }
}

void registerPBObject(int pbNm,Object *me)
{int w;
/* dPrint("registering pb object %d (ot=%d)\n",pbNm,me->type); */
 assert(pbNm>=0);
 assert(pbNm<level_nmPushBlocks);
 for (w=level_pushBlock[pbNm].startWall;
      w<=level_pushBlock[pbNm].endWall;
      w++)
    {assert(level_PBWall[w]>=0);
     assert(level_PBWall[w]<level_nmWalls);
     level_wall[level_PBWall[w]].object=me;
     /* dPrint("  has wall %d\n",level_PBWall[w]); */
    }
}


static int objectPPos;

#if 0
static char suckChar(void)
{return level_objectParams[objectPPos++];
}
#endif

short suckShort(void)
{short ret;
 ret=(level_objectParams[objectPPos]<<8)|
     (level_objectParams[objectPPos+1]);
 objectPPos+=2;
 return ret;
}

int suckInt(void)
{int ret;
 ret=(level_objectParams[objectPPos]<<24)|
     (level_objectParams[objectPPos+1]<<16)|
     (level_objectParams[objectPPos+2]<<8)|
     (level_objectParams[objectPPos+3]<<0);
 objectPPos+=4;
 return ret;
}

/* GCC14: the next nShorts params, read IN PLACE: a pointer into level_objectParams, which stays
   resident for the whole level (LEVEL.C).  For variable-length records whose owner reads and
   writes its own table (the Doom sector effects and lamps) instead of copying it.  The block is
   2-aligned: every param before it is a short (asserted), and mem_malloc returns 4-aligned
   memory.  Shorts are big-endian on disc, the SH-2's own order. */
short *suckParams(int nShorts)
{short *ret;
 assert(!(objectPPos&1) && nShorts>=0);
 assert(objectPPos+2*nShorts<=level_nmObjectParams);
 ret=(short *)(level_objectParams+objectPPos);
 objectPPos+=2*nShorts;
 return ret;
}

void suckSpriteParams(Sprite *s)
{if (!s)
    {suckShort();
     suckShort();
     suckShort();
     suckShort();
     return;
    }
 s->pos.x=F(suckShort()); /* mirror <<<< */
 s->pos.y=F(suckShort());
 s->pos.z=F(suckShort());
 s->angle=normalizeAngle(((int)suckShort())*5760);
}

void placeObjects(void)
{int o,p1,p2,p3,p4,i;

 for (o=0;o<level_nmObjects;o++)
    {if (level_object[o].type==OT_PLAYER)
	{objectPPos=level_object[o].firstParam;
	 player=constructPlayer(suckShort(),1);
	 camera=player->sprite;
	 break;
	}
    }
 objectPPos=0;

 for (o=0;o<level_nmObjects;o++)
    {assert(level_object[o].type<OT_NMTYPES);
     assert(level_object[o].firstParam==objectPPos); if (CFG_PLACE(level_object[o].type)) continue;
     CFG_ENGINE_PLACE switch (level_object[o].type)
	{case OT_SECTORSWITCH:
	    constructSectorSwitch();
	    break;
         case OT_SW1 ... OT_SW4:
	    constructSwitch(level_object[o].type);
	    break;
         case OT_SHOOTER1:
	 case OT_SHOOTER2:
	 case OT_SHOOTER3:
	    constructShooter(level_object[o].type);
	    break;
         case OT_TELEPSECTOR:
	    constructTeleporter();
	    break;
	 case OT_TELEPRETURN:
	    constructTeleportReturn();
	    break;
         case OT_PLAYER:
	    for (i=0;i<5;i++)
	       suckShort();
	    break;
	 case OT_BLOB:
	    constructBlob(suckShort());
	    break;
	 case OT_QUEEN:
	    constructQueen(suckShort());
	    break;
         case OT_RAMSESTRIGGER:
	    p1=suckShort();
	    p2=suckShort();
	    constructRamsesTrigger(p1,p2);
	    break;
	 case OT_CAMEL:
	    constructCamel(suckShort());
	    break;
	 case OT_FORCEFIELD:
	    constructForceField(suckShort());
	    break;
	 case OT_FLOORSWITCH:
	    constructFloorSwitch(suckShort());
	    break;
         case OT_NORMALELEVATOR:
	 case OT_STUCKDOWNELEVATOR:
	 case OT_STARTSDOWNELEVATOR:
	    p1=suckShort();
	    p2=suckShort();
	    p3=suckShort();
	    constructElevator(p1,level_object[o].type,p2,p3);
	    break;
	 case OT_DOWNTHENUPELEVATOR:
	    p1=suckShort();
	    p2=suckShort();
	    p3=suckShort();
	    constructUpDownElevator(p1,level_object[o].type,p2,p3);
	    break;
         case OT_NORMALDOOR:
	 case OT_BUGDOOR ... OT_PLANTDOOR:
	 case OT_STUCKUPDOOR:
	    constructDoor(level_object[o].type,suckShort());
	    break;
	 case OT_STUCKDOWNDOOR:
	    constructDownDoor(suckShort());
	    break;
	 case OT_RAMSESLID:
	    constructRamsesLid(suckShort());
	    break;
	 case OT_EARTHQUAKEBLOCK:
	    constructEarthQuakeBlock(suckShort());
	    break;
	 case OT_SINKINGBLOCK:
	    constructSinkBlock(suckShort());
	    break;
	 case OT_BOBBINGBLOCK:
	    constructBobBlock(suckShort());
	    break;
         case OT_SPIDER:
	    constructSpider(suckShort(),1,NULL,NULL);
	    break;
	 case OT_ANUBIS:
	    constructAnubis(suckShort());
	    break;
	 case OT_WASP:
	    constructWasp(suckShort());
	    break;
	 case OT_FISH:
	    constructFish(suckShort());
	    break;
	 case OT_HAWK:
	    constructHawk(suckShort());
	    break;
	 case OT_MUMMY:
	    constructMummy(suckShort());
	    break;
	 case OT_BASTET:
	    constructBastet(suckShort());
	    break;
	 case OT_SENTRY:
	    constructSentry(suckShort());
	    break;
	 case OT_MAGMANTIS:
	    constructMagmantis(suckShort());
	    break;
	 case OT_SET:
	    constructSet(suckShort());
	    break;
	 case OT_SELKIS:
	    constructSelkis(suckShort());
	    break;
	 case OT_TORCH1 ... OT_TORCH38:
	    constructTorch(suckShort(),level_object[o].type);
	    break;
	 case OT_CONTAIN1 ... OT_CONTAIN17:
	 case OT_BOOMPOT1:
	 case OT_BOOMPOT2:
	    constructBlowPot(suckShort(),level_object[o].type);
	    break;
	 case OT_RAMSIDEMUMMY:
	    suckShort();
	    suckShort();
	    suckShort();
	    suckShort();
	    suckShort();
	    break;
	 case OT_BUGKEY:
	 case OT_TIMEKEY:
	 case OT_XKEY:
	 case OT_PLANTKEY:
	 case OT_M60:
	 case OT_COBRASTAFF:
	 case OT_FLAMER:
	 case OT_GRENADEAMMO:
	 case OT_MANACLE:
	 case OT_PISTOL:
	 case OT_HEALTHBALL:
	 case OT_AMMOBALL:
	 case OT_HEALTHORB:
	 case OT_AMMOORB:
	 case OT_HEALTHSPHERE:
	 case OT_AMMOSPHERE:
	 case OT_CAPE:
	 case OT_FEATHER:
	 case OT_MASK:
	 case OT_SANDALS:
	 case OT_SCEPTER:
	 case OT_PYRAMID:
	 case OT_RING:
	 case OT_ANKLETS:
	 case OT_BLOODBOWL:
	 case OT_TRANSMITTER:
	 case OT_CHOPPER:
	 case OT_RAMMUMMY:
	 case OT_COMM_BATTERY ... OT_COMM_TOP:
	 case OT_DOLL1 ... OT_DOLL23:
	    p1=suckShort();
	    p2=suckShort();
	    p3=suckShort();
	    p4=suckShort();
	    constructThing(p1,F(p2),F(p3),F(p4),
			   level_object[o].type);
	    suckShort(); /* discard angle */
	    break;
	 default:
	    dPrint("unknown object type %d\n",level_object[o].type);
	    assert(0);
	    break;
	   }
    }

 shiftSprites();
 /* GCC14: the level's objects asked for their list as they were built (a corpse, a decor, a Doom
    pickup: delay_moveObject from the spawn state), and nothing was walking a list here -- the
    queue is emptied now instead of holding a whole level's worth until the first image. */
 processDelayedMoves();


#if 0
 {int i;
  /* put objects on the ground */
  for (i=0;i<MAXOBJECTS;i++)
     {if (objects[i].type!=OT_DEAD)
	 if ((objects[i].type!=OT_BLOB && objects[i].type!=OT_HAWK &&
	      objects[i].class==CLASS_MONSTER) ||
	     objects[i].type==OT_CAMEL)
	    {Sprite *s=((SpriteObject *)&(objects[i]))->sprite;
	     int d=findFloorDistance(s->s,&(s->pos));
	     s->pos.y+=s->radius-d;
	    }
     }
 }
#endif

 /* put the map in a random pot */
 {int potCount=0;
  int c;
  Object *o;
  for (o=objectIdleList;o;o=o->next)
     {if (o->type>=OT_CONTAIN1 && o->type<=OT_CONTAIN17)
	 potCount++;
     }
  if (potCount>0)
     {c=getNextRand()%potCount;
      for (o=objectIdleList;o;o=o->next)
	 {if (o->type>=OT_CONTAIN1 && o->type<=OT_CONTAIN17)
	     {potCount--;
	      if (!potCount)
		 break;
	     }
	 }
      assert(o);
      ((BlowPotObject *)o)->iAmMapHolder=1;
     }
 }
}

void hurtSprite(Sprite *sprite,Object *hurter,int damage)
{assert(sprite->owner->class!=CLASS_DEAD);
 assert(sprite->owner->type!=OT_DEAD);
 signalObject(sprite->owner,SIGNAL_HURT,damage,(int)hurter);
}

#define MAXPERSECTOR 20
void radialDamage(Object *this,MthXyz *center,int sector,int damage,
		  Fixed32 radius)
{char processed[MAXNMSECTORS]; /* 0=not, 1=need to be, 2=has been */
 Fixed32 dist;
 MthXyz normal;
 Sprite *sp;
 SpriteObject *list[MAXPERSECTOR];
 int nmToHurt;
 int w,i,s,dam,push;
 int done;
 for (i=0;i<MAXNMSECTORS;i++)
    processed[i]=0;
 processed[sector]=1;
 do
    {done=1;
     for (s=0;s<MAXNMSECTORS;s++)
	if (processed[s]==1)
	   {/* hurt all the sprites in the sector */
	    nmToHurt=0;
	    for (sp=sectorSpriteList[s];sp;sp=sp->next)
	       {if (!sp->owner)
		   continue;
		if (sp->owner->class!=CLASS_MONSTER)
		   continue;
		if (nmToHurt>=MAXPERSECTOR)
		   continue;
		list[nmToHurt++]=(SpriteObject *)sp->owner;
	       }
	    for (nmToHurt--;nmToHurt>=0;nmToHurt--)
	       {if (list[nmToHurt]->class==CLASS_DEAD)
		   continue;
		sp=list[nmToHurt]->sprite;
		if (!sp)
		   continue;
		dist=approxDist(center->x-sp->pos.x,
				center->y-sp->pos.y,
				center->z-sp->pos.z);
		if (dist>radius)
		   continue;
		dam=MTH_Mul(damage,F(1)-MTH_Div(dist,radius));
		{int pdam=dam;
		 if (pdam>50) pdam=50;
		 push=MTH_Div((pdam<<14),dist);
		 normal.x=MTH_Mul(-center->x+sp->pos.x,push);
		 normal.y=MTH_Mul(-center->y+sp->pos.y,push);
		 normal.z=MTH_Mul(-center->z+sp->pos.z,push);
		 sp->vel.x+=normal.x;
		 sp->vel.y+=normal.y;
		 sp->vel.z+=normal.z;
		}
		signalObject((Object *)list[nmToHurt],SIGNAL_HURT,
			     dam,(int)this);
	       }
	    /* add close adjacent sectors */
	    for (w=level_sector[s].firstWall;w<=level_sector[s].lastWall;w++)
	       {if (level_wall[w].nextSector==-1)
		   break;
		getVertex(level_wall[w].v[0],&normal);
		normal.x=center->x-normal.x;
		normal.y=center->y-normal.y;
		normal.z=center->z-normal.z;
		dist=MTH_Product((Fixed32 *)level_wall[w].normal,
				 (Fixed32 *)&normal);
		if (dist<radius)
		   if (processed[level_wall[w].nextSector]==0)
		      {processed[level_wall[w].nextSector]=1;
		       done=0;
		      }
	       }
	    processed[s]=2;
	   }
    }
 while (!done);
}

#if 0
void radialDamage(Object *this,MthXyz *center,int damage,Fixed32 radius)
{int nmList,dist,dam,push;
 MthXyz normal;
 Object *o,*list[MAXOBJECTS];
 Sprite *s;

 checkStack();
 nmList=0;
 for (o=objectRunList->next;o;o=o->next)
    if (o->class==CLASS_MONSTER)
       list[nmList++]=o;
 for (o=objectIdleList->next;o;o=o->next)
    if (o->class==CLASS_MONSTER)
       list[nmList++]=o;
 for (nmList--;nmList>=0;nmList--)
    {s=((MonsterObject *)list[nmList])->sprite;
     if (s->owner->class==CLASS_DEAD)
	continue;
     dist=approxDist(center->x-s->pos.x,
		     center->y-s->pos.y,
		     center->z-s->pos.z);
     if (dist>radius)
	continue;
     dam=MTH_Mul(damage,F(1)-MTH_Div(dist,radius));
     push=MTH_Div((dam<<14),dist);
     normal.x=MTH_Mul(-center->x+s->pos.x,push);
     normal.y=MTH_Mul(-center->y+s->pos.y,push);
     normal.z=MTH_Mul(-center->z+s->pos.z,push);
     s->vel.x+=normal.x;
     s->vel.y+=normal.y;
     s->vel.z+=normal.z;
     /* these asserts can fail if an object blows up from radial damage
	and kills something that is still in an earlier explosion's list */
     assert(s->owner->class!=CLASS_DEAD);
     assert(s->owner->type!=OT_DEAD);
     signalObject(s->owner,SIGNAL_HURT,dam,(int)this);
    }
}
#endif

void pushBlockMakeSound(PushBlockObject *source,int sndNm)
{MthXyz pos;
 int s=source->pbNum;
 pos.x=F(level_sector[level_pushBlock[s].enclosingSector].center[0]);
 pos.y=F(level_sector[level_pushBlock[s].enclosingSector].center[1]);
 pos.z=F(level_sector[level_pushBlock[s].enclosingSector].center[2]);
 posMakeSound((int)(source),&pos,(sndNm));
}

void pushBlockAdjustSound(PushBlockObject *source)
{MthXyz pos;
 int s=source->pbNum;
 pos.x=F(level_sector[level_pushBlock[s].enclosingSector].center[0]);
 pos.y=F(level_sector[level_pushBlock[s].enclosingSector].center[1]);
 pos.z=F(level_sector[level_pushBlock[s].enclosingSector].center[2]);
 posAdjustSound((int)(source),&pos);
}

#ifndef __INCLUDEDobjecth
#define __INCLUDEDobjecth

#include "sprite.h"

#define SIGNAL_MOVE 0
#define SIGNAL_ENTER 1 /* sent to sector objects when player enters */
#define SIGNAL_PRESS 2
#define SIGNAL_CEILCONTACT 3 /* only sent for contact with player */
#define SIGNAL_FLOORCONTACT 4 /* only sent for contact with player */
#define SIGNAL_VIEW 5
#define SIGNAL_HURT 10 /* params: hp of damage, Object * to damager */
#define SIGNAL_OBJECTDESTROYED 11 /* params: Object * of destroyed object */
#define SIGNAL_SWITCH 12 /* params: channel */
#define SIGNAL_SWITCHRESET 13 /* params: channel */

#define CLASS_SPRITE 1
#define CLASS_SECTOR 2
#define CLASS_PUSHBLOCK 3
#define CLASS_PROJECTILE 4
#define CLASS_MONSTER 5
#define CLASS_WALL 6
#define CLASS_DEAD 100

struct __object;

typedef void (*messHandler)(struct __object *this,int message,
                            int param1,int param2);

typedef struct __object
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 char pad[128];
} Object;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;

 short wallNm,state,aiSlot;
} WallObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;

 short sectorNm,state,aiSlot;
} SectorObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;

 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;
} PushBlockObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;

 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;
} SpriteObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;

 Object *owner;
 int age;
} ProjectileObject;

typedef struct __monster
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;

 short health,aiSlot;
 struct __monster *enemy;
 short route[ROUTESIZE+1];
 short routePos;
} MonsterObject;

extern Object *objectRunList; /* these lists have head nodes */
extern Object *objectIdleList;
extern Object *objectFreeList;
#define MAXOBJECTS 544          /* GCC14: the pool, in OBJECT.H so the overlay can show it */
extern int objectsRefused;      /* GCC14: objects the pool could not give (overlay lost:) */
int objectsFree(void);          /* GCC14: entries left in it -- walked, for the overlay only */

void initObjects(void);

void delayKill(Object *o);
void processKills(void);

Object *getFreeObject(messHandler handler,int type,int class);
void signalObject(Object *object,int message,int param1,int param2);
void hurtSprite(struct __sprite *sprite,Object *hurter,int damage);
void signalList(Object *head,int message,int param1,int param2);
void delayKill(Object *o);
void processDelayedMoves(void);
void delay_moveObject(Object *o,Object *toList);
void moveObject(Object *o,Object *toList);
void placeObjects(void);

void radialDamage(Object *this,MthXyz *center,int sector,int damage,
		  Fixed32 radius);

void registerPBObject(int pbNm,Object *me);

void pushBlockMakeSound(PushBlockObject *source,int sndNm);
void pushBlockAdjustSound(PushBlockObject *source);

short suckShort(void);
int suckInt(void);
short *suckParams(int nShorts);        /* GCC14: n shorts read in place (see OBJECT.C) */

#endif

#ifndef __INCLUDEDaih
#define __INCLUDEDaih
/* Sequence definitions */

#define MAX_VISUAL_RANGE 4000

typedef MonsterObject SpiderObject;
typedef MonsterObject PlayerObject;
typedef MonsterObject FishObject;

typedef SpriteObject OneBubbleObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;

 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,seeCounter;
} TorchObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;

 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,toLevel,disableTime;
} CamelObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;

 short speedDiv;
} BobBlockObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;

 int vel,distanceToFall,fallAccum;
 int channel,switchChannel;
} SinkBlockObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;

 int vel;
} EarthQuakeBlockObject;


typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;

 int distanceToRise,vel;
} RamsesLidObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;

 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad;
 int distLeft;
} BubbleObject;

typedef struct
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
 int anger;
} BlobObject;

typedef struct
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
 int age;
} QeggObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short sectorNm,state,aiSlot;

 int timer,disabled;
 struct __sprite *ramses;
 int baseY;
 int homeSector;
 int fd,soundRingBase,soundRingHead;
#ifdef JAPAN
 int soundRingBase2;
#endif
 int nmFrames,framePos;
 char *frames;
 char sndSlot,lastSndPos;
} RamsesTriggerObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short sectorNm,state,aiSlot;

 int toLevel,powerLevel,angle,countDown,enable,endOfPuzzle;
 sSectorType *pSector;
} TeleportObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short sectorNm,state,aiSlot;

 SpriteObject *artifact;
 int age,artifactNm,playerSec,playerAngle,artifactPlace;
 MthXyz apos;
} TeleportReturnObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short wallNm,state,aiSlot;

 MthXyz center;
} FFieldObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;

 Object *owner;
 int age;
 short damage;
 unsigned short explosionColor;
 char light;
 int armTime;
} GenericProjectileObject;

typedef struct
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

 int jinkTimer;
 short diveTimer;
} HawkObject;

typedef struct
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

 int boomTimer,iAmMapHolder;
} BlowPotObject;

typedef struct
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

 short hoverCounter,dartCounter;
} WaspObject;

#define NMCOBRABALLS 5
typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;
 Object *owner;
 int age;

 Sprite *balls[NMCOBRABALLS];
 int wave;
 MonsterObject *enemy;
 int aiSlot,glow;
} CobraObject;

#define NMQHEADBALLS 5
typedef struct
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

 Sprite *balls[NMQHEADBALLS];
 int wave,waitTime,nmBalls;
} QheadObject;

#define NMZAPBALLS 5
typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;
 Object *owner;
 int age;

 Sprite *balls[NMZAPBALLS];
 int wave;
 MonsterObject *enemy;
 int aiSlot,red;
 MthXyz hitPos;
} ZapObject;


typedef struct
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

 short stunCounter;
} AnubisObject;

typedef struct
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

 int tailInhibit;
 short rarCount,angry;
} MagmantisObject;

typedef AnubisObject MummyObject;
typedef AnubisObject SetObject;


typedef struct
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

 short stunCounter;
 short aimCounter,sprintCounter,decideNow;
} SentryObject;

typedef struct
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

 short stunCounter;
 short chargeCounter;
 int sparkTimer;
} SelkisObject;

typedef struct
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

 short fireCount;
 short chargeCounter;
 short multiCharge;
} QueenObject;


typedef struct
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

 short stunCounter;
 short portTimer;
 short hasLight;
} BastetObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;

 unsigned short sequence;
 int age,soft;
} BitObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;

 unsigned short sequence;
 int stuck;
} HardBitObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;

 int age,big;
} ZorchObject;

#define THINGFLAG_WAVE 1
#define THINGFLAG_PULSE 2
#define THINGFLAG_ANIMATE 4
#define THINGFLAG_THROB 8
#define THINGFLAG_MYSTICAL 16
#define THINGFLAG_BLUEMYSTICAL 32
#define THINGFLAG_NOISY 64
typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;

 int baseY,sin,frame;
 int flags,light;
} ThingObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;

 int *parameters;
 int ageInc;
 int age;
} LightObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;

 int waitTime;
} OneShotObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 int wallNm;
 short aiSlot,state;

 int firstTile,lastTile,speed;
} AnimWallObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;
 Object *owner;
 int age;

 MonsterObject *target;
 int damage;
} CloudObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;

 short throw;
 short stepEnable;
 short channel;
 short direction;
} ElevatorObject;

typedef ElevatorObject UpDownElevatorObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;

 short channel;
 short doorHeight;
} DoorObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short pbNum,state;
 int counter,waitCounter;
 Fixed32 offset;

 int channel,upperLevel,lowerLevel;
} FloorSwitchObject;

#define NMSHOOTERBEAMS 16
typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short wallNm,state,aiSlot;

 short sectorNm;
 char nmLines;
 int pulse;
 MthXyz normal;
 MthXyz orficePos;
 Sprite *beam[NMSHOOTERBEAMS];
 short switchType,channel,counter;
} ShooterObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short wallNm,state,aiSlot;

 char *tilePos;
 short frame,sequence;
 short channel,sectorNm;
 MthXyz orficePos;
} SwitchObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 short sectorNm,state,aiSlot;

 int channel;
} SectorSwitchObject;

typedef struct
{short type,class;
 struct __object *next,*prev;
 messHandler func;
 struct __sprite *sprite;
 unsigned short *sequenceMap;
 short state,pad1;

 Object *owner;
 int age;
} BoingRockObject;

void item_func(Object *this, int message, int param1, int param2);
void spider_func(Object *this, int message, int param1, int param2);
void hawk_func(Object *this, int message, int param1, int param2);
void anubis_func(Object *this, int message, int param1, int param2);
void torch_func(Object *this, int message, int param1, int param2);
void player_func(Object *this, int message, int param1, int param2);

Object *constructSpider(int sector,int suckParams,MthXyz *pos,MthXyz *vel);
/* GCC14: the monsters' projectiles, declared because a player may now throw them too -- a role
   wears a monster and fires what it fires (PSMULTI.C ps_roleFire) */
Object *constructGenproj(int sector,MthXyz *pos,MthXyz *vel,
			 SpriteObject *owner,int heading,
			 int flat,int type,int damage,
			 int explosionColor,
			 char r,char g,char b,
			 int armTime);
Object *constructAnuball(int sector,MthXyz *pos,MthXyz *vel,
			 SpriteObject *owner,int heading);
Object *constructSball(int sector,MthXyz *pos,MthXyz *vel,
		       SpriteObject *owner,int heading,int seq,
		       int scale);
PlayerObject *constructPlayer(int sector,int suckParams);
Object *constructAnubis(int sector);
Object *constructSelkis(int sector);
Object *constructSet(int sector);
Object *constructSentry(int sector);
Object *constructMummy(int sector);
Object *constructKapow(int sector,int x,int y,int z);
Object *constructWasp(int sector);
Object *constructFish(int sector);
Object *constructThing(int sector,int x,int y,int z,int thingType);
Object *constructOneShot(int sector,int x,int y,int z,int oneShotType,
			 int scale,unsigned short colored,int waitTime);
Object *constructZorch(int sector,int x,int y,int z,int zorchType);
Object *constructFlameball(int sector,MthXyz *pos,MthXyz *vel,
			   SpriteObject *owner,int heading,int frame);
Object *constructGrenade(int sector,MthXyz *pos,MthXyz *vel,
			 SpriteObject *owner);
Object *constructLight(int sector,int x,int y,int z,
		       int *parameters,Fixed32 ageInc);
Object *constructCobra(int sector,int x,int y,int z,int heading,Fixed32 yvel,
		       SpriteObject *owner,int type,int glow);
Object *constructRingo(int sector,MthXyz *pos,MthXyz *vel,
		       SpriteObject *owner);
Object *constructDoor(int type,int pb);
Object *constructBobBlock(int pb);
Object *constructSinkBlock(int pb);

Object *constructCloud(MonsterObject *target,int damage);

Object *constructHawk(int sector);

Object *constructElevator(int pb,int type,short lowerLevel,short upperLevel);
Object *constructUpDownElevator(int pb,int type,short lowerLevel,
				short upperLevel);
Object *constructFloorSwitch(int pb);

Object *constructBastet(int sector);

Object *constructForceField(int wallNm);

Object *constructRamsesTrigger(int triggerSec,int homeSector);

Object *constructCamel(int sector);

Object *constructMagmantis(int sector);
Object *constructBubble(int sector,MthXyz *pos,int distToCiel);
Object *constructOneBubble(int sector,MthXyz *pos,MthXyz *vel);
Object *constructBlob(int sector);
Object *constructZap(int sector,MthXyz *pos,SpriteObject *owner,
		     MonsterObject *enemy,int red);
Object *constructQueen(int sector);
Object *constructQhead(int sector,MthXyz *pos);

Object *constructTorch(int sector,int type);
Object *constructBlowPot(int sector,int type);

Object *constructTeleportReturn(void);
Object *constructTeleporter(void);

Object *constructShooter(int type);
Object *constructRamsesLid(int pb);
Object *constructSwitch(int type);

void runObjects(void);

void rotateFlames(int angle,int cx,int cz);
void translateFlames(int dx,int dy,int dz);
void initFlames(void);

Object *constructDownDoor(int pb);
Object *constructSectorSwitch(void);
Object *constructBouncyBit(int sector,MthXyz *pos,int sequence,int frame,
			   int soft);

Object *constructHardBit(int sector,MthXyz *pos,int sequence,int onFloor,
			 int frame);

Object *constructBoingRock(int sector,MthXyz *pos,MthXyz *vel);
Object *constructEarthQuakeBlock(int pb);

void resetLimitCounters(void);

#endif

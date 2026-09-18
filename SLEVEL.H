
#ifndef __INCLUDEDslevelh
#define __INCLUDEDslevelh

/* Ordering pair: two sectors with NO portal between them -- the two sides of a pillar -- which
   buildTree therefore leaves entirely to the distance scalar.  `plane` names the wall whose plane
   decides, same encoding as level_cutPlane: an offset from firstWall of `a`, or of `b` when bit
   0x80 is set.  The block is OPTIONAL and sits after cutPlane; the header has no field left, so
   its presence is read off the level block size.  Retail levels have none. */
typedef
struct
{short a,b;
 unsigned char plane;
 unsigned char pad;
} sOrderPair;

/* GCC14: the reject table -- which sectors can never see which: Doom's REJECT, which P_CheckSight
   reads before it traces a line.  Sectors with the same row share a class (sSectorType
   rejectClass).  The table is symmetric -- the converter keeps a pair only when both ways
   reject it -- so it holds the triangle a <= b of the n classes: bit a*n-a*(a-1)/2+b-a, low bit
   first, set = a and b never see each other.  OPTIONAL, after the ordering pairs, which are then
   there even when there are none: int n, then (n*(n+1)/2+7)/8 bytes.  Retail levels have none. */

struct sLevelHeader
{int nmSectors;
 int nmWalls;
 int nmVerticies;
 int nmFaces;
 int nmTextureIndexes;
 int nmLightValues;
 int nmObjects;
 int nmObjectParams;
 int nmPushBlocks;
 int nmPBWalls;
 int nmPBVert;
 int nmWaveVert;
 int nmWaveFace;
 int nmCutSectors;
};

enum ObjectType
{
 OT_PISTOL,OT_M60,OT_GRENADEAMMO,OT_FLAMER,OT_COBRASTAFF,OT_RING,OT_MANACLE,
 OT_SANDALS,OT_MASK,OT_CAPE,OT_ANKLETS,OT_SCEPTER,OT_FEATHER,

 OT_PLAYER,OT_SPIDER,OT_ANUBIS,OT_MUMMY,OT_BASTET,OT_ANUBALL,
 OT_KAPOW, OT_BIT,OT_BOUNCYBIT,OT_WASP,OT_FISH,OT_BUGKEY,OT_TIMEKEY,
 OT_XKEY,OT_PLANTKEY,OT_HEALTHBALL,OT_AMMOBALL,OT_LANDGUTS,
 OT_AIRGUTS,OT_BLUEZORCH,OT_REDZORCH,OT_FLAMEBALL,OT_FLAMEWALL,OT_GRENPOW,
 OT_HAWK,
 OT_LIGHT,OT_POOF,OT_RINGO,OT_COBRA,OT_GRENADE,
 OT_HEALTHORB,OT_AMMOORB,OT_HEALTHSPHERE, OT_AMMOSPHERE,
 OT_PYRAMID,OT_NORMALDOOR,OT_NORMALELEVATOR,OT_BOBBINGBLOCK,OT_SINKINGBLOCK,
 OT_EARTHQUAKEBLOCK,
 OT_BUGDOOR,OT_TIMEDOOR,OT_XDOOR,OT_PLANTDOOR,
 OT_STUCKUPDOOR,OT_STUCKDOWNDOOR,
 OT_FLOORSWITCH,OT_RAMSESLID,OT_STUCKDOWNELEVATOR,OT_STARTSDOWNELEVATOR,
 OT_DOWNTHENUPELEVATOR,
 OT_RACLOUD,OT_FORCEFIELD,OT_RAMSESTRIGGER,OT_CAMEL,
 OT_BLOODBOWL,OT_SENTRY,OT_SBALL,OT_MAGMANTIS,OT_SET,OT_SELKIS,
 OT_SETBALL,OT_MUMBALL,OT_MAGBALL,OT_BUBBLES,OT_1BUBBLE,OT_SPLASH,
 OT_BLOB,OT_GRENBUBB,OT_LIGHTNING,OT_QUEEN,OT_QUEENEGG,OT_HARDBIT,OT_QHEAD,
 OT_TRANSMITTER,OT_BOOMPOT1,OT_BOOMPOT2,OT_GLOWWORM,OT_SECTORSWITCH,
 OT_INVISIBLEBALL,OT_WEAPONPOWERBALL,OT_EYEBALL,OT_BRICK,OT_BOINGROCK,
 OT_CHOPPER,OT_RAMMUMMY,OT_RAMSIDEMUMMY,

 OT_COMM_BATTERY,OT_COMM_BOTTOM,OT_COMM_DISH,OT_COMM_HEAD,
 OT_COMM_KEYBOARD,OT_COMM_MOUSE,OT_COMM_SCREEN,OT_COMM_TOP,

 OT_TORCH1,OT_TORCH2,OT_TORCH3,OT_TORCH4,OT_TORCH5,OT_TORCH6,OT_TORCH7,
 OT_TORCH8,OT_TORCH9,OT_TORCH10,OT_TORCH11,OT_TORCH12,OT_TORCH13,
 OT_TORCH14,OT_TORCH15,OT_TORCH16,OT_TORCH17,OT_TORCH18,OT_TORCH19,
 OT_TORCH20,OT_TORCH21,OT_TORCH22,OT_TORCH23,OT_TORCH24,OT_TORCH25,
 OT_TORCH26,OT_TORCH27,OT_TORCH28,OT_TORCH29,OT_TORCH30,OT_TORCH31,
 OT_TORCH32,OT_TORCH33,OT_TORCH34,OT_TORCH35,OT_TORCH36,OT_TORCH37,
 OT_TORCH38,

 OT_CONTAIN1,OT_CONTAIN2,OT_CONTAIN3,OT_CONTAIN4,OT_CONTAIN5,OT_CONTAIN6,
 OT_CONTAIN7,OT_CONTAIN8,OT_CONTAIN9,OT_CONTAIN10,OT_CONTAIN11,
 OT_CONTAIN12,OT_CONTAIN13,OT_CONTAIN14,OT_CONTAIN15,OT_CONTAIN16,OT_CONTAIN17,

 OT_TELEPSECTOR,OT_TELEPRETURN,OT_SHOOTER1,OT_SHOOTER2,OT_SHOOTER3,
 OT_SW1,OT_SW2,OT_SW3,OT_SW4,

 OT_ANIM_CHAOS1,OT_ANIM_CHAOS2,OT_ANIM_CHAOS3,
 OT_ANIM_LAVA1,OT_ANIM_LAVA2,OT_ANIM_LAVA3,
 OT_ANIM_LAVAFALL,OT_ANIM_LAVAPO1,OT_ANIM_LAVAPO2,OT_ANIM_LAVAFAL1,
 OT_ANIM_TELEP1,OT_ANIM_TELEP2,OT_ANIM_TELEP3,OT_ANIM_TELEP4,OT_ANIM_TELEP5,
 OT_ANIM_LAVAHEAD,OT_ANIM_FORCEFIELD,OT_ANIM_WSAND,OT_ANIM_WBRICK,
 OT_ANIM_SWAMP,OT_ANM1,OT_ANM2,OT_ANM3,OT_ANM4,OT_ANM5,OT_ANM6,OT_ANM7,OT_ANM8,
 OT_ANM9,OT_ANM10,OT_ANM11,OT_ANM12,

 OT_DOLL1,OT_DOLL2,OT_DOLL3,OT_DOLL4,OT_DOLL5,
 OT_DOLL6,OT_DOLL7,OT_DOLL8,OT_DOLL9,OT_DOLL10,
 OT_DOLL11,OT_DOLL12,OT_DOLL13,OT_DOLL14,OT_DOLL15,
 OT_DOLL16,OT_DOLL17,OT_DOLL18,OT_DOLL19,OT_DOLL20,
 OT_DOLL21,OT_DOLL22,OT_DOLL23,

 OT_NMTYPES,OT_DEAD=8196,
};

typedef struct
{int pos,vel;
 short connect[4]; /* -1 means no connection */
} WaveVert;

typedef struct
{short connect[4];
} WaveFace;

typedef struct
{short type;
 short firstParam;
} sObjectType;

typedef struct
{short enclosingSector;
 short startWall,endWall;  /* indexes into the pbwall array */
 short startVertex,endVertex; /* indexes into the pbvertex array */
 short floorSector; /* a sector's floor that moves with the block,
		       or -1 if none */
 short dx,dy,dz;
} sPBType;

#define PBVFLAG_XLOCK 1
#define PBVFLAG_YLOCK 2
#define PBVFLAG_ZLOCK 4
typedef struct
{unsigned short vStart;
 unsigned char vNm;
 char flags;
} sPBVertex;

typedef struct
{short x,y,z;
 char light,pad;
} sVertexType;

typedef struct
{unsigned short v[4];
 unsigned char tile;
 char pad;
} sFaceType;

#define TILESIZE 64

#define WALLFLAG_PARALLELOGRAM    0x01
#define WALLFLAG_INVISIBLE        0x02
#define WALLFLAG_SLIPPERY         0x04
#define WALLFLAG_BLOCKSSIGHT      0x08
#define WALLFLAG_NOTSTEPABLE      0x10
#define WALLFLAG_DOORWALL         0x20
#define WALLFLAG_PARALLAX         0x40
#define WALLFLAG_LAVA             0x80

/* blocking bits */
#define WALLFLAG_BLOCKBITS      0x1f00
#define WALLFLAG_BLOCKED         0x100
#define WALLFLAG_WATERSURFACE    0x200
#define WALLFLAG_WATERBNDRY      0x400
#define WALLFLAG_CLIFFBNDRY      0x800
#define WALLFLAG_SHORTOPENING   0x1000

#define WALLFLAG_SWAMP          0x2000
#define WALLFLAG_COLLIDEASFLOOR 0x4000

#define WALLFLAG_EXPLODABLE     0x8000

typedef struct
{/* plane equation */
 int normal[3];
 int d;
 void *object;

 short flags;
/* if RECTANGLE then */
 unsigned short textures; /* index into texture array */
/* else */
 short firstFace,lastFace;
 unsigned short firstVertex,lastVertex;
/* endif */
 unsigned short v[4];
 short nextSector; /* or -1 */
 unsigned short firstLight;
 short pixelLength;
 unsigned char tileLength,tileHeight;
          /* length is number of tiles along v0 -> v1,
	     height is number along v1->v2 */
} sWallType;

#define MAXCUTSECTORS 128

#define SECFLAG_WATER 1
#define SECFLAG_CUTSORT 2
#define SECFLAG_SEEN 4
#define SECFLAG_EXPLODINGWALLS 8
#define SECFLAG_LASERSECTOR 16
#define SECFLAG_NOMAP 32

typedef struct
{void *object;
 short center[3];
 short floorLevel; /* avg floor level if sloped */
 short firstWall,lastWall;
 short light; /*lighting level (-16 to 16)*/
 short flags;
 char cutIndex,cutChannel;
 short rejectClass;     /* GCC14: its row and column in the reject table (LEVEL.C); was pad, 0 in retail */
} sSectorType;

#define TILEFLAG_VDP2     0x01
#define TILEFLAG_64x64    0x02
#define TILEFLAG_32x32    0x04
#define TILEFLAG_8BPP     0x08
#define TILEFLAG_16BPP    0x10
#define TILEFLAG_PALLETE  0x20
#define TILEFLAG_RLE      0x40
struct seqHeader
{int nmSequences;
 int nmFrames;
 int nmChunks;
};

/* sequence data */
/*short *sequenceList; declared elsewhere */
   /*index into the frame array for start of each sequence*/
/* there is always an extra entry on the end which points to the element past
   the end of the frameArray */

#define FRAMEFLAG_ENDOFSEQUENCE 0x4000
#define FRAMEFLAG_FIRE 0x80

typedef struct
{short chunkIndex; /* index into the chunk array for each frame */
 short flags;
 short sound;
 char pad[2];
} sFrameType;

typedef struct
{short chunkx,chunky;
 short tile;
 char flags;
 char pad;
} sChunkType;

#endif

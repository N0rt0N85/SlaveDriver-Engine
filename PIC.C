#include <machine.h>

#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_int.h>
#include <sega_mth.h>
#include <sega_sys.h>
#include <sega_dbg.h>
#include <sega_per.h>
#include <sega_dma.h>
#include <string.h>
#include "gameparams.h"
#include "pic.h"
#include "util.h"
#include "spr.h"
#include "file.h"
#include "slevel.h"
#include "sequence.h"

#include "art.h"
#include "dma.h"

#define COMPRESS16BPP 1
#define MIPMAP 1	/* GCC14: compiled in, gated at runtime by mipEnable -- see WALLS.C */

/* note: mipmaping is incompatible with locked tiles */

#ifndef JAPAN
#if MIPMAP
#define MAXNMPICS 1600
#else
#define MAXNMPICS 800
#endif
#else
#define MAXNMPICS 1000
#endif

/* GCC14: the use clock (PIC.H), and its value when the image being built and the image the VDP1
   is drawing started */
static int picClock,picThis,picPrev;

#define MAXNMVDP2PICS 50
struct _vdp2PicData
{short x,y;
 short w,h;
} vdp2PicData[MAXNMVDP2PICS];
static int nmVDP2Pics;

#define PICFLAG_LOCKED 1
#define PICFLAG_RLE 2
#ifdef JAPAN
#define PICFLAG_RLE2 4
#endif
#if MIPMAP
#define PICFLAG_MIP 8
#endif
/* GCC14: the pic is a 64x64 WINDOW on the packed weapon source, not a buffer of its own
   (picWeaponSprites below); it is cut into rleBuffer at upload, the way an RLE tile is
   expanded there.  `flags` is a byte and every other bit is taken, so this takes the one the
   Japanese build spends on RLE2 -- and is simply off there, which costs that build the
   split-screen gun and, with it, the split sky (MPSKY.C skyAllowed). */
#ifdef JAPAN
#define PICFLAG_WSUB 0
#else
#define PICFLAG_WSUB 4
#endif
#define PICFLAG_ANIM 0xf0
typedef struct
{void *data; /* data = NULL if not in use */
#if COMPRESS16BPP
 void *pallete;
#endif
 int lastUse;
 short charNm; /* or -1 if not mapped */
 char class;
 unsigned char flags;
} Pic;

typedef struct
{/* static */
 int colorMode,drawWord,width,height,nmSlots,dataSize;
 Pic **slots;
 /* dynamic */
 int picNmBase;
 char nmSwaps;
} ClassType;

static Pic *__TILE8BPPSPACE[60+1],*__TILE16BPPSPACE[48+1];
static Pic *__VDPSPACE[2],*__TILESMALL16BPPSPACE[40+1],
   *__TILESMALL8BPPSPACE[20];

#ifdef JAPAN
static Pic *__JFONTSPACE[80];
#endif

ClassType classType[NMCLASSES]=
{{COLOR_5,UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU,64,64,0,
     64*64*2,__TILE16BPPSPACE},
 {COLOR_4,UCLPIN_ENABLE|COLOR_4|HSS_ENABLE|ECD_DISABLE,64,64,0,
     64*64,__TILE8BPPSPACE},
 {0,0,0,0,1,sizeof(struct _vdp2PicData),__VDPSPACE},
 {COLOR_5,UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU,32,32,0,
     32*32*2,__TILESMALL16BPPSPACE},
 {COLOR_4,UCLPIN_ENABLE|COLOR_4|HSS_ENABLE|ECD_DISABLE,32,32,0,
     32*32,__TILESMALL8BPPSPACE}
#ifdef JAPAN
 ,
 {COLOR_1,UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE,24,18,0,
     24*18/2,__JFONTSPACE}
#endif
};

Pic pics[MAXNMPICS];
static int nmPics;
static unsigned short *palletes;

/* GCC14: the wall tiles' rule (PIC.H) */
#ifndef GP_PIC_LOD_PX
#define GP_PIC_LOD_PX 0
#endif
#define PICSPARE 4              /* slots the ranking leaves to newcomers */
static const int picLodPx=GP_PIC_LOD_PX;
int picLodNow,picLastSmall,picLastFull;
static int picSmall,picFull;
static unsigned char picSize[WALLPICS],picKeep[WALLPICS/8];
static Pic *picPending[64];     /* slots a wall or a thing took from the image on screen: pic_flush
				   fills them.  One per slot at most, 32 + 31 */
static int picNmPending;
/* GCC14: the things' tiles under the same rule (PIC.H mapSpritePic) */
static unsigned char sprSize[60];               /* per TILE8BPP slot (__TILE8BPPSPACE's bound): its
						   tile's largest size in this image */
static unsigned char sprRef[60];                /* ... and the largest size refused for the tile it
						   holds while this image did not use it, 0 = none */
static unsigned char sprOut[PIC_ALWAYS+1];      /* the image's tiles by size, for the ranking: the
						   refused out of a slot go in as they are refused,
						   once each (lastUse marks them) */
static int sprNmOut;
int picSpriteLod,picLastSpriteOut;


#define MAXNMANIMSETS 15
  /* these are chunk indexes */
static int animTileStart[MAXNMANIMSETS];
static int animTileEnd[MAXNMANIMSETS];
static int nmAnimTileSets;
static int animTileChunk[MAXNMANIMSETS];

void advanceWallAnimations(void)
{int i;
 static int flip;
 flip=!flip;
 if (flip)
    return;
 for (i=0;i<nmAnimTileSets;i++)
    {if ((++animTileChunk[i])>=animTileEnd[i])
	animTileChunk[i]=animTileStart[i];
    }
}

void markAnimTiles(void)
{static int animObjectList[]=
    {
     OT_ANIM_CHAOS1,OT_ANIM_CHAOS2,OT_ANIM_CHAOS3,
     OT_ANIM_LAVA1,OT_ANIM_LAVA2,OT_ANIM_LAVA3,
     OT_ANIM_LAVAFALL,OT_ANIM_LAVAPO1,OT_ANIM_LAVAPO2,
     OT_ANIM_TELEP1,OT_ANIM_TELEP2,OT_ANIM_TELEP3,OT_ANIM_TELEP4,
     OT_ANIM_TELEP5,OT_ANIM_LAVAHEAD,OT_ANIM_FORCEFIELD,OT_ANIM_WSAND,
     OT_ANIM_WBRICK,OT_ANIM_SWAMP,OT_ANM1,OT_ANM2,OT_ANM3,OT_ANM4,OT_ANM5,
     OT_ANM6,OT_ANM7,OT_ANM8,OT_ANM9,OT_ANM10,OT_ANM11,OT_ANM12,
     0
    };
 int i,seq,frame,tile;
 nmAnimTileSets=0;
 for (i=0;animObjectList[i];i++)
    {if (level_sequenceMap[animObjectList[i]]<0)
	continue;
     assert(nmAnimTileSets<MAXNMANIMSETS);
     seq=level_sequenceMap[animObjectList[i]];
     animTileStart[nmAnimTileSets]=level_frame[level_sequence[seq]].chunkIndex;
     animTileEnd[nmAnimTileSets]=level_frame[level_sequence[seq+1]].chunkIndex;

     for (frame=level_sequence[seq];frame<level_sequence[seq+1];frame++)
	{tile=level_chunk[level_frame[frame].chunkIndex].tile;
	 pics[tile].flags=(pics[tile].flags&0xf)|
	    ((nmAnimTileSets+1)<<4);
	}
     nmAnimTileSets++;
    }
 for (i=0;i<MAXNMANIMSETS;i++)
    animTileChunk[i]=animTileStart[i];
}

void setDrawModeBit(int class,int bit,int onOff)
{if (onOff)
    classType[class].drawWord|=bit;
 else
    classType[class].drawWord&=~bit;
}

/* GCC14: an image closes: rank its wall tiles by their largest cell, a tile already in a slot
   counting a quarter more so that it only leaves for a clearly bigger one.  The first
   slots-PICSPARE keep a slot through the next image; the bar is the size a newcomer must reach. */
static void wallRank(void)
{unsigned short hist[256];
 int t,s,n,keep;
 memset(hist,0,sizeof(hist));
 for (t=0;t<WALLPICS;t++)
    if ((s=picSize[t]))
       {if (pics[t].charNm!=-1)
	   s+=s>>2;
	if (s>255)
	   s=255;
	picSize[t]=s;
	hist[s]++;
       }
 keep=classType[TILE16BPP].nmSlots-PICSPARE;
 for (s=255,n=0;s>0 && n+hist[s]<=keep;s--)
    n+=hist[s];
 picLodNow=(s+1>picLodPx)? s+1: picLodPx;
 for (t=0;t<WALLPICS;t++)
    if (picSize[t]>=picLodNow)
       picKeep[t>>3]|=1<<(t&7);
    else
       picKeep[t>>3]&=~(1<<(t&7));
 memset(picSize,0,sizeof(picSize));
}

/* GCC14: the things' bar (PIC.H).  0 while the image just closed refused nothing.  Else its tiles,
   in a slot or refused, are ranked by size and the slots less PICSPARE largest set it.  PICSPARE
   for the walls' reason: a new frame or a monster turning must find a slot no image still reads.
   A model on E1M6's tiles, 2 and 4 players, 8-30 monsters seen by every view, guns IDLE: the last
   view's gun found every slot taken in up to 26 % of the images at 0 spare, 4 % at 2, none at 4.
   Guns that FIRE (up to 4 tiles more at once, the bar one image late): still 1 to 2.5 % at 4 --
   hence the guns' own reservation (PIC.H, SEQUENCE.C reserveWeaponTiles). */
static void spriteRank(void)
{ClassType *c=classType+TILE8BPP;
 int s,n,keep,v;
 picLastSpriteOut=sprNmOut;
 if (!sprNmOut)
    {picSpriteLod=0;
     return;
    }
 /* a slot counts once: at its tile's largest size if this image used it, else at the largest
    size refused for it (a far pack of one monster is ONE tile, not one per monster) */
 for (s=0;s<c->nmSlots;s++)
    {v=(c->slots[s] && c->slots[s]->lastUse>=picThis)? sprSize[s]: sprRef[s];
     if (v && sprOut[v]<255)
	sprOut[v]++;
     sprRef[s]=0;
    }
 keep=c->nmSlots-PICSPARE;
 for (s=PIC_ALWAYS,n=0;s>0 && n+sprOut[s]<=keep;s--)
    n+=sprOut[s];
 picSpriteLod=s? s+1: 0;
 if (picSpriteLod>PIC_ALWAYS)
    picSpriteLod=PIC_ALWAYS;    /* the guns and the shadow alone overflow: they still pass */
 memset(sprOut,0,sizeof(sprOut));
 sprNmOut=0;
}

void pic_nextFrame(int *swaps,int *used)
{
 weaponSpriteShown=weaponSpriteDrawn;   /* the sub-tiles the gun put down in the image
				  that just ended -- the overlay prints it below */
 weaponSpriteDrawn=0;
 /* Measurement discs are NDEBUG builds: without this, the only two counters that say
    whether the tile cache overflows exist only in the build nobody measures. */
#if !defined(NDEBUG) || defined(STATUSTEXT)
 int i,j;
 if (swaps)
    for (i=0;i<NMCLASSES;i++)
       {swaps[i]=classType[i].nmSwaps;
	classType[i].nmSwaps=0;
       }
 if (used)
    for (i=0;i<NMCLASSES;i++)
       {used[i]=0;
	for (j=0;j<classType[i].nmSlots;j++)
	   if (classType[i].slots[j] &&
	       classType[i].slots[j]->lastUse>=picThis)
	      used[i]++;
       }
#endif
 if (picLodPx)
    {wallRank();
     spriteRank();
    }
 picLastSmall=picSmall;
 picLastFull=picFull;
 picSmall=picFull=0;
 picPrev=picThis;
 picThis=picClock+1;
}

int getPicClass(int p)
{return pics[p].class;
}

unsigned char *getPicData(int p)
{return pics[p].data;
}

static unsigned char *rleBuffer;

/* returns new pic num */
int initPicSystem(int _picNmBase,int *classSizes)
{int i;
 enum Class c;
 nmPics=0;
 picNmPending=0;
 picClock=0;
 picThis=picPrev=1;
 memset(picSize,0,sizeof(picSize));
 memset(picKeep,0,sizeof(picKeep));
 picLodNow=picLodPx;
 picSpriteLod=0;
 sprNmOut=0;
 memset(sprOut,0,sizeof(sprOut));
 memset(sprRef,0,sizeof(sprRef));
 palletes=NULL;
 nmVDP2Pics=0;
 /* GCC14: the pic table is being rebuilt, so every sub-tile index the weapon cut recorded is
    stale -- it has to be cut again for this level (picWeaponSprites). */
 weaponSpritesOn=0;
 weaponSpriteTiles=0;
 weaponSpriteTry=0;
 rleBuffer=(unsigned char *)0x6001000; /*mem_malloc(1,4096);*/
 for (c=0;c<NMCLASSES;c++)
    {classType[c].nmSlots=*(classSizes++);
     classType[c].picNmBase=_picNmBase;
     classType[c].nmSwaps=0;
     for (i=0;i<classType[c].nmSlots;i++)
	{classType[c].slots[i]=NULL;
	 if (classType[c].width==0)
	    continue;
	 EZ_setChar(_picNmBase++,
		    classType[c].colorMode,
		    classType[c].width,
		    classType[c].height,NULL);
	 assert(EZ_charNoToVram(_picNmBase-1));
	}
     classType[c].slots[i]=NULL; /* set sentinel */
    }
 assert(*classSizes==-1);
 for (i=0;i<MAXNMPICS;i++)
    {pics[i].data=NULL;
     pics[i].charNm=-1;
    }
 return _picNmBase;
}

void resetPics(void)
{int c,i;
 picNmPending=0;                /* its pics are unmapped below */
 for (i=0;i<MAXNMPICS;i++)
    if (!(pics[i].flags & PICFLAG_LOCKED))
       pics[i].charNm=-1;
 for (c=0;c<NMCLASSES;c++)
    {for (i=0;i<classType[c].nmSlots;i++)
	if (classType[c].slots[i] &&
	    !(classType[c].slots[i]->flags & PICFLAG_LOCKED))
	   classType[c].slots[i]=NULL;
    }
}

/* GCC14: what map() hands the DMA.  dmaMemCpy returns before the transfer ends, so the tile must
   not sit in map()'s frame: the next calls' frames overwrote its last row while the DMA was still
   reading it.  Static, and every writer of it or of rleBuffer first waits for the DMA. */
static unsigned short picBuff[1024*4];
/* GCC14: the tile buffer, lent out.  The menus decompress one picture at a time through it
   (MENU.C loadOverPic) instead of keeping the whole set in low RAM; the world is not being
   drawn while a menu is open, so nothing else wants it then. */
void *picScratch(int *size)
{while (dmaActive())            /* the last image's uploads may still be reading it */
    ;
 if (size)
    *size=sizeof(picBuff);
 return picBuff;
}

static unsigned char *unRle(Pic *p)
{register int outSize;
 register int i;
 register unsigned char *inPos;
 int nmPixels;
 while (dmaActive())
    ;
 outSize=0;
 inPos=p->data;
 if (p->class==TILESMALL8BPP)
    nmPixels=32*32;
 else
    nmPixels=64*64;
 while (outSize<nmPixels)
    {/* decode blank space */
     i=*(inPos++);
     for (;i;i--)
	rleBuffer[outSize++]=0;
     /* decode not blank space */
     i=*(inPos++);
     for (;i;i--)
	rleBuffer[outSize++]=*(inPos++);
    }
 assert(outSize==nmPixels);
 return rleBuffer;
}

static void upload(Pic *p);
static unsigned char *wpnCut(const void *d);  /* the gun: below, with picWeaponSprites */

/* GCC14: safe (a wall): never a slot this image uses -- 0 then -- and one the image on screen
   uses is filled at pic_flush */
static int map(Pic *p,int safe)
{int oldest,oldTime,index,defer=0;
 Pic **array,**s;
 checkStack();
 validPtr(p);
 assert(p->class!=TILEVDP);
 assert(p->charNm==-1);
 assert(p->class>=0 && p->class<=NMCLASSES);
 /* find the oldest slot in the apropriate array */
 oldest=-1;
 oldTime=0x7fffffff;
 array=classType[(int)p->class].slots;

#ifndef NDEBUG
 for (s=array;*s;s++)
    assert(*s!=p);
#endif
 for (s=array;*s;s++)
    if (!((*s)->flags & PICFLAG_LOCKED) && (*s)->lastUse<oldTime)
       {oldTime=(*s)->lastUse;
	oldest=s-array;
       }
 if (s-array<classType[(int)p->class].nmSlots)
    {/* we found an empty slot. */
     index=s-array;
    }
 else
    {/* otherwise we have to knock one out */
     if (safe && oldTime>=picThis)
	return 0;
     defer=safe && oldTime>=picPrev;
     assert(oldest>=0);
     index=oldest;
     classType[(int)p->class].slots[oldest]->charNm=-1;
#if !defined(NDEBUG) || defined(STATUSTEXT)
     classType[(int)p->class].nmSwaps++;
#endif
    }

 classType[(int)p->class].slots[index]=p;
 p->charNm=index+classType[(int)p->class].picNmBase;
 if (defer)
    picPending[picNmPending++]=p;
 else
    upload(p);
 return 1;
}

/* GCC14: the tile's texels into its slot */
static void upload(Pic *p)
{unsigned char *srcData;
#if COMPRESS16BPP && MIPMAP
 unsigned short pbuff[1024*4];   /* a mip pic's full-size tile, halved into picBuff */
#endif
 if (p->flags & PICFLAG_WSUB)
    srcData=wpnCut(p->data);    /* the gun: a window on the packed sheet (picWeaponSprites) */
 else if (p->flags & PICFLAG_RLE)
    srcData=unRle(p);
 else
    srcData=p->data;
#ifdef JAPAN
 if (p->flags & PICFLAG_RLE2)
    {register int outSize;
     register int i;
     register unsigned char *inPos;
     int nmPixels;
     outSize=0;
     inPos=p->data;
     nmPixels=24*18;
     for (i=0;i<nmPixels>>1;i++)
	rleBuffer[i]=0;
     while (outSize<nmPixels)
	{/* decode blank space */
	 i=*(inPos++);
	 for (;i;i--)
	    {if (outSize & 1)
		rleBuffer[(outSize>>1)]|=0x01;
	     else
		rleBuffer[(outSize>>1)]|=0x10;
	     outSize++;
	    }
	 /* decode not blank space */
	 outSize+=*(inPos++);
	}
     assert(outSize==nmPixels);
     srcData=rleBuffer;
    }
#endif

#if COMPRESS16BPP
 if (p->pallete)
    {register int i;
     unsigned short *out=picBuff;
#if MIPMAP
     if (p->flags & PICFLAG_MIP)
	out=pbuff;
#endif
     while (dmaActive())
	;
     for (i=0;i<classType[(int)p->class].dataSize>>1;i++)
	out[i]=((unsigned short *)p->pallete)[(int)srcData[i]];
     srcData=(unsigned char *)out;
    }
#endif


#if MIPMAP
 if (p->flags & PICFLAG_MIP)
    {/* convert the image in srcData to a mip down */
     int x,y;
     int p,src;
     unsigned short *ssrc=(unsigned short *)srcData;
     while (dmaActive())
	;
     for (y=0,p=0,src=0;
	  y<32;
	  y++,p+=32,src+=64)
	for (x=0;
	     x<32;
	     x++,p++,src+=2)
	   {unsigned short temp1=ssrc[src];
	    unsigned short temp2=ssrc[src+1];
	    unsigned short temp3=ssrc[src+64];
	    unsigned short temp4=ssrc[src+64+1];
	    unsigned short avg=
   (((temp1&0x001f)+(temp2&0x001f)+(temp3&0x001f)+(temp4&0x001f))>>2)|
   ((((temp1&0x03e0)+(temp2&0x03e0)+(temp3&0x03e0)+(temp4&0x03e0))>>2)&0x03e0)|
   ((((temp1&0x7c00)+(temp2&0x7c00)+(temp3&0x7c00)+(temp4&0x7c00))>>2)&0x7c00)|
	    0x8000;

	    picBuff[p]=avg;
	    picBuff[p+32]=avg;
	    picBuff[p+32*64]=avg;
	    picBuff[p+32*64+32]=avg;
	   }
     srcData=(char *)picBuff;
    }
#endif

 {unsigned char *pos=
     (unsigned char *)
	((EZ_charNoToVram(p->charNm)<<3)+
	 0x25c00000);

  /* DMA_ScuMemCopy(pos,srcData,classType[(int)p->class].dataSize); */

  dmaMemCpy(srcData,pos,classType[(int)p->class].dataSize);

/*  qmemcpy(pos,srcData,classType[(int)p->class].dataSize); */
 }
}

/* GCC14: the uploads map() deferred, once the VDP1 has finished the image on screen
   (SPR_WaitDrawEnd) and before the next one is shown */
void pic_flush(void)
{int i;
 if (!picNmPending)
    return;
 for (i=0;i<picNmPending;i++)
    upload(picPending[i]);
 picNmPending=0;
 while (dmaActive())
    ;
}


/* if a pic is locked, its memory may be free'd after this call */
int addPic(enum Class class,void *data,void *pallete,int flags)
{int p;
 p=nmPics++;
 assert(p<MAXNMPICS);
#if COMPRESS16BPP
 pics[p].pallete=pallete;
#endif
 pics[p].data=data;
 pics[p].lastUse=0;
 pics[p].class=class;
 pics[p].charNm=-1;
 pics[p].flags=flags;
 if (flags & PICFLAG_LOCKED)
    map(pics+p,0);
 return p;
}

#if MIPMAP
int createMippedPics(void)
{int p,firstMip;
 firstMip=nmPics;
 for (p=0;p<firstMip;p++)
    {pics[nmPics]=pics[p];
     pics[nmPics].flags|=PICFLAG_MIP;
     nmPics++;
     assert(nmPics<MAXNMPICS);
    }
 return firstMip;
}
#endif

int mapPic(int picNm)
{Pic *p=pics+picNm;
 assert(picNm>=0);
 assert(picNm<nmPics);
 if (p->flags & PICFLAG_ANIM)
    {picNm=level_chunk[animTileChunk[(p->flags>>4)-1]].tile;
     p=pics+picNm;
    }
 assert((p->flags & PICFLAG_LOCKED) || p->data);
 assert(p->class != TILEVDP);
 if (p->charNm==-1)
    map(p,0);
 p->lastUse=++picClock;
 assert(p->charNm>=0);
 return p->charNm;
}

/* GCC14: the wall tiles' rule, PIC.H */
int mapWallPic(int picNm,int size)
{Pic *p;
 if (!picLodPx || (unsigned int)picNm>=WALLPICS)
    return mapPic(picNm);
 if (size>255)
    size=255;
 if (size>picSize[picNm])
    picSize[picNm]=size;
 if (size<picLodNow && !(picKeep[picNm>>3] & (1<<(picNm&7))))
    {picSmall++;
     return -1;
    }
 p=pics+picNm;
 if (p->flags & PICFLAG_ANIM)
    p=pics+level_chunk[animTileChunk[(p->flags>>4)-1]].tile;
 if (p->charNm==-1 && !map(p,1))
    {int k=(pics[picNm].flags>>4)-1,c;
     p=NULL;
     if (k>=0)          /* an animation shows a frame it still holds rather than none */
	for (c=animTileStart[k];c<animTileEnd[k] && !p;c++)
	   if (pics[level_chunk[c].tile].charNm!=-1)
	      p=pics+level_chunk[c].tile;
     if (!p)
	{picFull++;
	 return -1;
	}
    }
 p->lastUse=++picClock;
 return p->charNm;
}

/* GCC14: the things' tiles, PIC.H.  A refused tile out of a slot is counted once per image: its
   lastUse, which nothing reads while it holds no slot, marks it.  One in a slot keeps its lastUse
   -- its slot's age, which the LRU reads -- and its slot remembers the largest size refused
   (sprRef, counted once by spriteRank).  A tile this image already holds is drawn whatever its
   size: its slot is spent either way. */
int mapSpritePic(int picNm,int size)
{Pic *p=pics+picNm;
 int k;
 if (!picLodPx || p->class!=TILE8BPP)
    return mapPic(picNm);
 if (p->flags & PICFLAG_ANIM)
    p=pics+level_chunk[animTileChunk[(p->flags>>4)-1]].tile;
 if (size>PIC_ALWAYS)
    size=PIC_ALWAYS;
 if (p->charNm==-1)
    {if (size<picSpriteLod || !map(p,1))
	{sprNmOut++;
	 if (p->lastUse<picThis && sprOut[size]<255)
	    {p->lastUse=picThis;
	     sprOut[size]++;
	    }
	 return -1;
	}
     sprSize[p->charNm-classType[TILE8BPP].picNmBase]=size;
    }
 else
    {k=p->charNm-classType[TILE8BPP].picNmBase;
     if (size<picSpriteLod && p->lastUse<picThis)
	{sprNmOut++;
	 if (size>sprRef[k])
	    sprRef[k]=size;
	 return -1;
	}
     if (p->lastUse<picThis || size>sprSize[k])
	sprSize[k]=size;
    }
 p->lastUse=++picClock;
 return p->charNm;
}

/* GCC14: the colour a wall cell painted flat takes: its tile's first texel, nothing computed.  An
   RLE tile opens on its count of blank texels, then of drawn ones. */
int picFirstColour(int picNm)
{Pic *p=pics+picNm;
 unsigned char *d=p->data;
 int i;
 if (!p->pallete)
    return ((unsigned short *)d)[0]|0x8000;
 i=(p->flags & PICFLAG_RLE)? (d[0]? 0: d[2]): d[0];
 return ((unsigned short *)p->pallete)[i]|0x8000;
}

static int vxmin,vymin,vxmax,vymax,vx,vy;

/* GCC14: VDP2 pictures the level loaded; 0 = VRAM B0-B1 hold nothing (MPSKY.C) */
int vdp2PicCount(void)
{return nmVDP2Pics;
}

void updateVDP2Pic(void)
{SCL_Open(SCL_NBG0);
 SCL_MoveTo(vx<<16,vy<<16,0);
 SCL_Close();
 SCL_SetWindow(SCL_W0,0,SCL_NBG0,0xfffffff,vxmin,vymin,
	       vxmax,vymax);
}

void displayVDP2Pic(int picNm,int xo,int yo)
{int xmin,ymin,xmax,ymax;
 struct _vdp2PicData *pd=(struct _vdp2PicData *)pics[picNm].data;
 vx=pd->x-xo;
 vy=pd->y-yo;
#if 0
 SCL_Open(SCL_NBG0);
 SCL_MoveTo((pd->x-xo)<<16,(pd->y-yo)<<16,0);
 SCL_Close();
#endif
 xmin=xo; ymin=yo;
 if (xmin<0) xmin=0;
 if (ymin<0) ymin=0;
 xmax=xo+pd->w-1;
 ymax=yo+pd->h-1;
 if (xmax>320) xmax=320;
 if (ymax>210)
    ymax=210;

/* xmin=0; ymin=0; xmax=320; ymax=240; */
 vxmin=xmin; vymin=ymin; vxmax=xmax; vymax=ymax;
#if 0
 SCL_SetWindow(SCL_W0,0,SCL_NBG0,0xfffffff,xmin,ymin,
	       xmax,ymax);
#endif
}

/* GCC14: the weapon as VDP1 sprites, for split screen -------------------------------------------
   PowerSlave's weapon is not a sprite: loadVDP2Sprites (SRUINS.C) reads a 512x512 8 bpp BITMAP
   into VDP2 VRAM B0/B1 and NBG0 shows it, scrolled to the frame wanted (displayVDP2Pic above).
   A background plane has ONE scroll, so in split screen the gun can only be in one place -- it
   sits across the views -- and it holds the 256 KB the split sky wants (MPSKY.C).
   Doom has no such thing: its gun is 64x64 VDP1 tiles like everything else, and SEQUENCE.C
   already knows how to lay those out in a view (advanceWeaponSequence, the vw != 320 branch).
   So the sheet is CUT into the same kind of tile, at HALF resolution -- which is exactly the
   scale a 160-pixel view draws the gun at, so a tile goes down 1:1 and the VDP1 never scales.
   Measured on the retail STATIC.DAT: 18 pictures, 27 tiles, 108 KB of LWRAM, and at most THREE
   tiles for one weapon frame (the widest gun is 340 px = 3 half-tiles across).
   Built once a level, after every other tile: the level's own indices are based on the weapon
   tile count (LEVEL.C loadLevel tileBase), so these have to come last. */
int weaponSpritesOn;                    /* the cut succeeded: the sheet is free (MPSKY.C) */
/* GCC14: what the cut did, for the overlay (SRUINS.C "W:").  try = times it was asked this
   run, tiles = sub-tiles it produced, kb = LWRAM free when it was asked. */
int weaponSpriteTry,weaponSpriteTiles,weaponSpriteKb,weaponSpriteDrawn,weaponSpriteShown;
static short vdp2Sub[MAXNMVDP2PICS];    /* first sub-tile of a VDP2 picture, -1 = not cut */
static unsigned char vdp2Cols[MAXNMVDP2PICS],vdp2Rows[MAXNMVDP2PICS];

/* The packed source: every picture of the sheet, halved, laid end to end.  Measured on the
   retail STATIC.DAT: 47 KB, where cutting the same pictures into aligned 64x64 tiles up front
   cost 108 KB -- 60 KB of that was the alignment's own waste, and PowerSlave has no 60 KB to
   waste (a heavy level leaves ten). */
static unsigned char *wpnPack;
static int   wpnOff[MAXNMVDP2PICS];     /* a picture's first byte in wpnPack */
static short wpnW[MAXNMVDP2PICS];       /* ... and its size once halved */
static short wpnH[MAXNMVDP2PICS];
#define WPNMAXSUB 64
static struct {short pic; unsigned char cx,cy;} wpnSub[WPNMAXSUB];

/* the 64x64 window `d` of the packed source, built where an RLE tile is expanded (rleBuffer is
   4096 bytes and a sub-tile is 64x64: the same buffer, and the same rule -- wait for the DMA
   that may still be reading it) */
static unsigned char *wpnCut(const void *d)
{const struct {short pic; unsigned char cx,cy;} *sub=d;
 int i=sub->pic,x0=sub->cx<<6,y0=sub->cy<<6,x,y;
 const unsigned char *src=wpnPack+wpnOff[i];
 int w=wpnW[i],h=wpnH[i];
 while (dmaActive())
    ;
 for (y=0;y<64;y++)
    {int sy=y0+y;
     for (x=0;x<64;x++)
	{int sx=x0+x;
	 rleBuffer[(y<<6)+x]=(sx<w && sy<h)? src[sy*w+sx]: 0;
	}
    }
 return rleBuffer;
}

int picWeaponSprites(void)
{const unsigned char *sheet=(const unsigned char *)(SCL_VDP2_VRAM+1024*256);
 int i,cx,cy,x,y,c,r,first,bytes=0,nsub=0;
 weaponSpriteTry++;
 weaponSpriteKb=mem_coreleft(0)>>10;
 if (!PICFLAG_WSUB)             /* no bit to mark a window with: see above */
    return 0;
 if (weaponSpritesOn)
    return 1;
 for (i=0;i<MAXNMVDP2PICS;i++)
    vdp2Sub[i]=-1;
 /* Count first, then ONE block for the lot.  mem_nocheck_malloc keeps a stack of eight
    records an area (UTIL.C STACKSIZE): a call per tile would wrap that ring three times
    over and lose every record the level load left underneath. */
 for (i=0;i<nmVDP2Pics;i++)
    {wpnW[i]=(short)((vdp2PicData[i].w+1)>>1);
     wpnH[i]=(short)((vdp2PicData[i].h+1)>>1);
     wpnOff[i]=bytes;
     bytes+=wpnW[i]*wpnH[i];
    }
 if (bytes<=0)
    {weaponSpritesOn=1;         /* no sheet to cut: B0-B1 is free either way */
     return 1;
    }
 /* GCC14: nocheck -- LWRAM also holds the level's tiles, and a level with no room for the
    gun must lose the gun, not the level */
 wpnPack=(unsigned char *)mem_nocheck_malloc(0,bytes);
 if (!wpnPack)
    return 0;
 for (i=0;i<nmVDP2Pics;i++)
    {int sx0=vdp2PicData[i].x,sy0=vdp2PicData[i].y;
     unsigned char *dst=wpnPack+wpnOff[i];
     int w=wpnW[i],h=wpnH[i];
     for (y=0;y<h;y++)
	for (x=0;x<w;x++)
	   dst[y*w+x]=sheet[((sy0+(y<<1))<<9)+sx0+(x<<1)];
     c=(w+63)>>6;
     r=(h+63)>>6;
     first=-1;
     for (cy=0;cy<r;cy++)
	for (cx=0;cx<c;cx++)
	   {int pn;
	    if (nsub>=WPNMAXSUB)         /* more windows than the table holds: stop clean */
	       {c=cx+1; r=cy+1; break;}
	    wpnSub[nsub].pic=(short)i;
	    wpnSub[nsub].cx=(unsigned char)cx;
	    wpnSub[nsub].cy=(unsigned char)cy;
	    pn=addPic(TILE8BPP,wpnSub+nsub,NULL,PICFLAG_WSUB);
	    if (first<0)
	       first=pn;
	    nsub++;
	   }
     vdp2Sub[i]=(short)first;
     vdp2Cols[i]=(unsigned char)c;
     vdp2Rows[i]=(unsigned char)r;
    }
 weaponSpriteTiles=nsub;
 weaponSpritesOn=1;
 return 1;
}

/* the sub-tile of VDP2 picture `picNm` at column cx, row cy -- -1 = the picture was not cut.
   A sub-tile covers 128 x 128 pixels of the 320-wide frame the gun is laid out in. */
int picVdp2Sub(int picNm,int cx,int cy)
{int i;
 if (!weaponSpritesOn || getPicClass(picNm)!=TILEVDP)
    return -1;
 i=(struct _vdp2PicData *)pics[picNm].data-vdp2PicData;
 if (i<0 || i>=nmVDP2Pics || vdp2Sub[i]<0)
    return -1;
 if (cx<0 || cy<0 || cx>=vdp2Cols[i] || cy>=vdp2Rows[i])
    return -1;
 return vdp2Sub[i]+cy*vdp2Cols[i]+cx;
}

void delay_dontDisplayVDP2Pic(void)
{vxmin=0; vymin=0; vxmax=0; vymax=0;
}

void dontDisplayVDP2Pic(void)
{SCL_SetWindow(SCL_W0,0,SCL_NBG0,0xfffffff,0,0,
	       0,0);
 vxmin=0; vymin=0; vxmax=0; vymax=0;
}

static void load16BPPTile(int fd,int lock)
{short width,height,palNm;
#if !COMPRESS16BPP
 int i;
#endif
 unsigned char *buffer;
 unsigned char *b;
 short *pal;
 width=64;
 height=64;
#if !COMPRESS16BPP
 buffer=(unsigned char *)mem_malloc(1,2*width*height);
#endif
 b=(unsigned char *)mem_malloc(1,width*height);
 assert(b);
 fs_read(fd,(char *)&palNm,2);
 assert(palletes);
 pal=palletes+256*palNm+1;
 fs_read(fd,b,width*height);
#if COMPRESS16BPP
 buffer=b;
#else
 assert(buffer);
 for (i=0;i<width*height;i++)
    {buffer[i*2]=pal[b[i]] >>8;
     buffer[i*2+1]=pal[b[i]] & 0xff;
    }
 mem_free(b);
#endif
 addPic(TILE16BPP,buffer,pal,lock?PICFLAG_LOCKED:0);
 if (lock)
    mem_free(buffer);
}

static void loadSmall16BPPTile(int fd,int lock)
{short width,height,palNm;
#if !COMPRESS16BPP
 int i;
#endif
 unsigned char *buffer;
 unsigned char *b;
 short *pal;
 width=32;
 height=32;
#if !COMPRESS16BPP
 buffer=(unsigned char *)mem_malloc(1,2*width*height);
#endif
 b=(unsigned char *)mem_malloc(1,width*height);
 assert(b);
 fs_read(fd,(char *)&palNm,2);
 assert(palletes);
 pal=palletes+256*palNm+1;
 fs_read(fd,b,width*height);
#if COMPRESS16BPP
 buffer=b;
#else
 assert(buffer);
 for (i=0;i<width*height;i++)
    {buffer[i*2]=pal[b[i]] >>8;
     buffer[i*2+1]=pal[b[i]] & 0xff;
    }
 mem_free(b);
#endif
 addPic(TILESMALL16BPP,buffer,pal,lock?PICFLAG_LOCKED:0);
 if (lock)
    mem_free(buffer);
}

static void load8BPPRLETile(int fd,int lock)
{unsigned char *buffer;
 short size,palNm;
 fs_read(fd,(char *)&palNm,2);
 fs_read(fd,(char *)&size,2);
 assert(size);
 buffer=(char *)mem_malloc(0,size);
 assert(buffer);
 fs_read(fd,buffer,size);
 addPic(TILE8BPP,buffer,NULL,(lock?PICFLAG_LOCKED:0)|PICFLAG_RLE);
 if (lock)
    mem_free(buffer);
}

static void loadSmall8BPPRLETile(int fd,int lock)
{unsigned char *buffer;
 short size,palNm;
 fs_read(fd,(char *)&palNm,2);
 fs_read(fd,(char *)&size,2);
 assert(size);
 buffer=(char *)mem_malloc(0,size);
 assert(buffer);
 fs_read(fd,buffer,size);
 addPic(TILESMALL8BPP,buffer,NULL,(lock?PICFLAG_LOCKED:0)|PICFLAG_RLE);
 if (lock)
    mem_free(buffer);
}

static void load16BPPRLETile(fd,lock)
{unsigned char *buffer;
 short size,palNm;
 short *pal;
 fs_read(fd,(char *)&palNm,2);
 fs_read(fd,(char *)&size,2);
 assert(size);
 buffer=(char *)mem_malloc(0,size);
 fs_read(fd,buffer,size);
 pal=palletes+256*palNm+1;
 addPic(TILE16BPP,buffer,pal,(lock?PICFLAG_LOCKED:0)|PICFLAG_RLE);
 if (lock)
    mem_free(buffer);
}

/* GCC14: the darkened banks 1..n-1, built from bank 0.  The original step was r-=i, at most
   -4/31 over five banks: enough to mark distance, not to follow a fog going to black.  Same
   mechanism spread over SPRITEFOGMAX (UTIL.H), and SUBTRACTIVE like the walls' gouraud, so
   things keep the same range as the scenery at the same distance.  n < NMOBJECTPALLETES frees
   the top banks for something else (split screen: the players' colours, MPLAYER.C);
   drawSprites reads nmObjectFogBanks. */
int nmObjectFogBanks=NMOBJECTPALLETES;
/* GCC14: the see-through things' bank (SPRITEFLAG_MESH, the spectre).  The fog banks bottom out
   at SPRITEFOGMAX below bank 0 -- about a third -- because a monster must stay readable; a
   spectre must do the opposite.  It gets a bank of its own, GREY (the luminance of each PLAYPAL
   entry, ITU-R 601 weights) and darkened well past the fog floor, so the mesh shows a shape
   rather than a demon you can see past.  Grey is only possible in a bank of its own: the VDP1's
   gouraud shifts the palette INDEX on a colour-bank sprite, not the colour, and PLAYPAL is not
   ordered by luminance (measured on hardware, ../saturn-refs/knowledge HW_VDP1.md).
   It costs one fog step: buildObjectFogBanks builds 0..n-1 and takes n for this. */
#define SPECTREGREY 13         /* 5-bit units taken off the grey; the fog floor is SPRITEFOGMAX */
int spectreBank=NMOBJECTPALLETES-1;

static void buildSpectreBank(int bank)
{unsigned short *colorRam=(unsigned short *)SCL_COLRAM_ADDR;
 int c,r,g,b,y;
 assert(bank>0 && bank<NMOBJECTPALLETES);
 spectreBank=bank;
 for (c=0;c<256;c++)
    {unsigned short v=colorRam[c];
     r=v & 0x1f;
     g=(v>>5) & 0x1f;
     b=(v>>10) & 0x1f;
     y=((r*77+g*151+b*28)>>8)-SPECTREGREY;
     if (y<0) y=0;
     colorRam[bank*256+c]=RGB(y,y,y);
    }
}

/* GCC14: one channel of a fog bank at fog step `sub` (UTIL.H setFogColour).  The walls carry the
   fog's colour in their gouraud ramp; a thing has no gouraud of its own -- its bank IS the ramp,
   baked -- so it is darkened as it always was and then lifted towards the fog's colour by the
   same step, and a monster in the haze goes the way its surroundings go.  A black fog lifts by
   nothing: the original banks, byte for byte. */
static int fogBankChan(int v,int sub,int c)
{v-=sub;
 if (v<0) v=0;
 v+=(c*sub)/16;
 if (v>31) v=31;
 return v;
}

void buildObjectFogBanks(int n)
{unsigned short *colorRam=(unsigned short *)SCL_COLRAM_ADDR;
 int i,c,r,g,b,sub;
 assert(n>=3 && n<=NMOBJECTPALLETES);
 n--;                           /* the top one goes to the spectre, below */
 nmObjectFogBanks=n;
 for (i=1;i<n;i++)
    {sub=(SPRITEFOGMAX*i)/(n-1);
     for (c=0;c<256;c++)
	{unsigned short v=colorRam[c];
	 r=fogBankChan(v & 0x1f,sub,fogColour[0]);
	 g=fogBankChan((v>>5) & 0x1f,sub,fogColour[1]);
	 b=fogBankChan((v>>10) & 0x1f,sub,fogColour[2]);
	 colorRam[i*256+c]=RGB(r,g,b);
	}
    }
 buildSpectreBank(n);
}

/* GCC14: bank `bank` = bank 0 under a colour filter, for a game whose palette carries no ramp
   to remap.  Doom reads the green marine ramp as another of PLAYPAL's (buildRemappedBank);
   PowerSlave's object palette is a different one in every level, so a fixed index table would
   paint a different thing each time.  A filter needs no table: tint is 0..31 a channel, 31 =
   keep, and the shading of each pixel survives -- only its hue moves.  MPLAYER.C mpSetBanks. */
void buildTintedBank(int bank,int tr,int tg,int tb)
{unsigned short *colorRam=(unsigned short *)SCL_COLRAM_ADDR;
 int c;
 assert(bank>0 && bank<8 && bank!=NMOBJECTPALLETES);
 for (c=0;c<256;c++)
    {unsigned short v=colorRam[c];
     int r=((v & 0x1f)*tr)/31;
     int g=(((v>>5) & 0x1f)*tg)/31;
     int b=(((v>>10) & 0x1f)*tb)/31;
     colorRam[bank*256+c]=RGB(r,g,b);
    }
}

/* GCC14: bank `bank` = bank 0 seen through an index remap (Doom's player translations: the
   green ramp read as another ramp).  The sprite's pixels are unchanged; only its bank is. */
void buildRemappedBank(int bank,const unsigned char *remap)
{unsigned short *colorRam=(unsigned short *)SCL_COLRAM_ADDR;
 unsigned short tmp[256];
 int c;
 assert(bank>0 && bank<8 && bank!=NMOBJECTPALLETES);
 for (c=0;c<256;c++)
    tmp[c]=colorRam[remap[c]];
 for (c=0;c<256;c++)
    colorRam[bank*256+c]=tmp[c];
}

void loadPalletes(int fd)
{int size,i,j;
 unsigned short *colorRam=(unsigned short *)SCL_COLRAM_ADDR;
 fs_read(fd,(char *)&size,4);
 assert(size>0 && size<1024*1024);
 palletes=(unsigned short *)
    mem_malloc(
#if COMPRESS16BPP
	       1
#else
	       0
#endif
	       ,size);
 assert(palletes);
 fs_read(fd,(char *)palletes,size);
 /* ... load the object pallete into c-ram */
 {unsigned short *objectPal;
  unsigned short tempSpace[256];
  int c;
  objectPal=palletes+256*(*palletes)+1;
  objectPal[255]=0xffff;

  for (i=0;i<256;i++)
     colorRam[i]=objectPal[i];
  /* SCL_SetColRam(0,0,256,objectPal); */

  buildObjectFogBanks(NMOBJECTPALLETES);
  /* make flash pallete */
  for (c=0;c<256;c++)
     tempSpace[c]=0xffff;
  tempSpace[0]=0x8000;

  for (j=0;j<256;j++)
     colorRam[NMOBJECTPALLETES*256+j]=tempSpace[j];
  /* SCL_SetColRam(0,NMOBJECTPALLETES*256,256,tempSpace); */

 }
}

/* return # of tiles loaded */
int loadTileSet(int fd,int lock)
{int nmTiles,i;
 short flags;

 fs_read(fd,(char *)&nmTiles,4);
 for (i=0;i<nmTiles;i++)
    {fs_read(fd,(char *)&flags,2);
     switch (flags)
	{case (TILEFLAG_64x64|TILEFLAG_16BPP|TILEFLAG_PALLETE):
	    load16BPPTile(fd,lock);
	    break;
	 case (TILEFLAG_VDP2):
	    assert(nmVDP2Pics<MAXNMVDP2PICS);
	    fs_read(fd,(char *)(vdp2PicData+nmVDP2Pics),8);
	    addPic(TILEVDP,vdp2PicData+nmVDP2Pics,NULL,0);
	    nmVDP2Pics++;
	    break;
	 case (TILEFLAG_64x64|TILEFLAG_8BPP|TILEFLAG_RLE|TILEFLAG_PALLETE):
	    load8BPPRLETile(fd,lock);
	    break;
	 case (TILEFLAG_32x32|TILEFLAG_8BPP|TILEFLAG_RLE|TILEFLAG_PALLETE):
	    loadSmall8BPPRLETile(fd,lock);
	    break;
	 case (TILEFLAG_32x32|TILEFLAG_16BPP|TILEFLAG_PALLETE):
	    loadSmall16BPPTile(fd,lock);
	    break;
	 case (TILEFLAG_64x64|TILEFLAG_16BPP|TILEFLAG_PALLETE|TILEFLAG_RLE):
	    load16BPPRLETile(fd,lock);
	    break;
	 default:
	    assert(0);
	    break;
	   }
    }
 return nmTiles;
}

/* returns # of weapon tiles loaded */
int loadWeaponTiles(int fd)
{return loadTileSet(fd,0);
}

void loadTiles(int fd)
{loadPalletes(fd);
 loadTileSet(fd,0);
}

#define PICWIDTH(d) (*((int *)(d)))
#define PICHEIGHT(d) (*(((int *)(d))+1))
#define PICDATA(d) (((unsigned char *)d)+8)

int loadPicSetAsPics(int fd,int class)
{unsigned int *datas[50];
 unsigned short *palletes[50];
 int nmSetPics,i,x,y,c,picBase;
 picBase=nmPics;
 nmSetPics=loadPicSet(fd,palletes,datas,50);

 for (i=0;i<nmSetPics;i++)
    {switch (class)
	{case TILESMALL16BPP:
	    {unsigned short buffer[32*32];
	     memset(buffer,0,32*32*2);
	     c=0;
	     assert(!(((int)datas[i])&0x3));
	     assert(!(((int)palletes[i])&0x1));
	     for (y=0;y<PICHEIGHT(datas[i]);y++)
		for (x=0;x<PICWIDTH(datas[i]);x++)
		   buffer[y*32+x]=palletes[i][PICDATA(datas[i])[c++]];
	     addPic(TILESMALL16BPP,buffer,NULL,PICFLAG_LOCKED);
	     break;
	    }
	 case TILE16BPP:
	    /* this does something totaly different from the SMALL16BPP case.
	       sorry. */
	    addPic(TILE16BPP,PICDATA(datas[i]),palletes[i],0);
	    break;
	   }
    }
 return picBase;
}

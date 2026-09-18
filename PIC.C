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
static unsigned short picMean[WALLPICS];   /* 0 = not taken yet */
static Pic *picPending[48];     /* slots a wall took from the image on screen: pic_flush fills them */
static int picNmPending;


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

void pic_nextFrame(int *swaps,int *used)
{
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
    wallRank();
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
 memset(picMean,0,sizeof(picMean));
 picLodNow=picLodPx;
 palletes=NULL;
 nmVDP2Pics=0;
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
 if (p->flags & PICFLAG_RLE)
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

/* GCC14: a wall tile's mean colour, for its cells painted flat: 64 texels on an 8x8 grid, taken
   once per level */
int picMeanColour(int picNm)
{Pic *p=pics+picNm;
 unsigned char *src;
 int i,t,c,r=0,g=0,b=0;
 assert((unsigned int)picNm<WALLPICS);
 if (picMean[picNm])
    return picMean[picNm];
 src=(p->flags & PICFLAG_RLE)? unRle(p): p->data;
 for (i=0;i<64;i++)
    {t=((i>>3)*8+4)*64+(i&7)*8+4;
     c=p->pallete? ((unsigned short *)p->pallete)[src[t]]: ((unsigned short *)src)[t];
     r+=c&0x1f;
     g+=(c>>5)&0x1f;
     b+=(c>>10)&0x1f;
    }
 return picMean[picNm]=0x8000|(r>>6)|((g>>6)<<5)|((b>>6)<<10);
}

static int vxmin,vymin,vxmax,vymax,vx,vy;

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
void buildObjectFogBanks(int n)
{unsigned short *colorRam=(unsigned short *)SCL_COLRAM_ADDR;
 int i,c,r,g,b,sub;
 assert(n>=2 && n<=NMOBJECTPALLETES);
 nmObjectFogBanks=n;
 for (i=1;i<n;i++)
    {sub=(SPRITEFOGMAX*i)/(n-1);
     for (c=0;c<256;c++)
	{unsigned short v=colorRam[c];
	 r=(v & 0x1f)-sub;
	 g=((v>>5) & 0x1f)-sub;
	 b=((v>>10) & 0x1f)-sub;
	 if (r<0) r=0;
	 if (g<0) g=0;
	 if (b<0) b=0;
	 colorRam[i*256+c]=RGB(r,g,b);
	}
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

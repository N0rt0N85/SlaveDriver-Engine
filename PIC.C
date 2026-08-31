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

static int frameCount;

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

#if MIPMAP
char mippedSlot[MAXNMPICS];
#endif


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

void pic_nextFrame(int *swaps,int *used)
{
#ifndef NDEBUG
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
	       classType[i].slots[j]->lastUse==frameCount)
	      used[i]++;
       }
#endif
 frameCount++;
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
 frameCount=0;
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

#if MIPMAP
unsigned short mipbuff[1024*4];
#endif

static void map(Pic *p)
{int oldest,oldTime,index;
 Pic **array,**s;
 unsigned char *srcData;
#if COMPRESS16BPP
 unsigned short pbuff[1024*4];
#endif
 checkStack();
 validPtr(p);
 assert(p->class!=TILEVDP);
 assert(p->charNm==-1);
 assert(p->class>=0 && p->class<=NMCLASSES);
 /* find the oldest slot in the apropriate array */
 oldest=-1;
 oldTime=frameCount+1;
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
     assert(oldest>=0);
     index=oldest;
     classType[(int)p->class].slots[oldest]->charNm=-1;
#ifndef NDEBUG
     classType[(int)p->class].nmSwaps++;
#endif
    }

 classType[(int)p->class].slots[index]=p;
 p->charNm=index+classType[(int)p->class].picNmBase;

 if (p->flags & PICFLAG_RLE)
    {register int outSize;
     register int i;
     register unsigned char *inPos;
     int nmPixels;
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
     srcData=rleBuffer;
    }
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
     for (i=0;i<classType[(int)p->class].dataSize>>1;i++)
	pbuff[i]=((unsigned short *)p->pallete)[(int)srcData[i]];
     srcData=(unsigned char *)pbuff;
    }
#endif


#if MIPMAP
 if (p->flags & PICFLAG_MIP)
    {/* convert the image in srcData to a mip down */
     int x,y;
     int p,src;
     unsigned short *ssrc=(unsigned short *)srcData;
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

	    mipbuff[p]=avg;
	    mipbuff[p+32]=avg;
	    mipbuff[p+32*64]=avg;
	    mipbuff[p+32*64+32]=avg;
	   }
     srcData=(char *)mipbuff;
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
    map(pics+p);
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
    map(p);
 p->lastUse=frameCount;
 assert(p->charNm>=0);
 return p->charNm;
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

  for (i=1;i<NMOBJECTPALLETES;i++)
     {int r,g,b;
      for (c=0;c<256;c++)
	 {r=objectPal[c] & 0x1f;
	  g=(objectPal[c]>>5) & 0x1f;
	  b=(objectPal[c]>>10) & 0x1f;
	  r-=i;
	  if (r<0)
	     r=0;
	  g-=i;
	  if (g<0)
	     g=0;
	  b-=i;
	  if (b<0)
	     b=0;
	  tempSpace[c]=RGB(r,g,b);
	 }
      for (j=0;j<256;j++)
	 colorRam[i*256+j]=tempSpace[j];
      /* SCL_SetColRam(0,i*256,256,tempSpace); */
     }
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

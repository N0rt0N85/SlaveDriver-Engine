#include <machine.h>

#include <libsn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sega_spr.h"
#include "sega_scl.h"
#include "sega_int.h"
#include "sega_mth.h"
#include "sega_sys.h"
#include "sega_dbg.h"
#include "sega_per.h"
#include "sega_cdc.h"
#include "sega_snd.h"
#include "sega_gfs.h"

#include "picset.h"
#include "file.h"
#include "util.h"
#include "print.h"
#include "v_blank.h"
#include "sound.h"
#include "spr.h"
#include "mov.h"
#include "megainit.h"
#include "initmain.h"
#include "local.h"

/* GCC14: boot probe (make BOOTPROBE=1): paints the screen with one back-screen colour after
   each step of main() to show how far INIT gets.  magenta = main(), red = fadeSegaLogo,
   green = megaInit, blue = fs_init, yellow = mem_init, white = PER_GET_SYS answered. */
#ifdef BOOTPROBE
#define BOOT_PROBE(c) do { POKE_W(SCL_VDP2_VRAM+0x180000,0x8000); POKE_W(SCL_VDP2_VRAM+0x180020,0); \
    POKE_W(SCL_VDP2_VRAM+0x180110,0); POKE_W(SCL_VDP2_VRAM+0x1800ac,0); POKE_W(SCL_VDP2_VRAM+0x1800ae,0); \
    POKE_W(SCL_VDP2_VRAM,(c)); } while (0)
#else
#define BOOT_PROBE(c) ((void)0)
#endif
#ifdef JAPAN
#include "pic.h"
#endif

#include "sprite.h"
Sprite *camera;

#ifdef JAPAN
int level_frame,level_sequenceMap,level_sequence,level_chunk;
#endif


#define MAXNMPICS 20
static unsigned short *picPals[MAXNMPICS];
static unsigned int *picDatas[MAXNMPICS];

static void setupVDP2(void)
{static Uint16 cycle[]=
    {0x44ee, 0xeeee,
     0x44ee, 0xeeee,
     0x55ee, 0xeeee,
     0x55ee, 0xeeee};
 SclConfig scfg;
 SclVramConfig vcfg;
 SCL_InitVramConfigTb(&vcfg);
 vcfg.vramModeA=OFF;
 vcfg.vramModeB=OFF;
 vcfg.vramA0=SCL_NON;
 vcfg.vramB0=SCL_NON;
 SCL_SetVramConfig(&vcfg);
 SCL_SetCycleTable(cycle);

 /* setup VDP2Sprite screen */
 SCL_InitConfigTb(&scfg);
 scfg.dispenbl=ON;
 scfg.bmpsize=SCL_BMP_SIZE_512X256;
 scfg.coltype=SCL_COL_TYPE_256;
 scfg.datatype=SCL_BITMAP;
 scfg.mapover=SCL_OVER_0;
 scfg.plate_addr[0]=0;
 scfg.patnamecontrl=0;
 SCL_SetConfig(SCL_NBG0, &scfg);
 scfg.plate_addr[0]=256*1024;
 SCL_SetConfig(SCL_NBG1, &scfg);
 SCL_SET_N0CAOS(0);
 SCL_SET_N1CAOS(1);
 SCL_SetPriority(SCL_NBG0,2);
 SCL_SetPriority(SCL_NBG1,1);
 SCL_SET_N0CCEN(1);
}

static void loadVDPPic(int picNm,int bankNm)
{int i,width,height,x,y,yoffs;

 for (i=0;i<256;i++)
    POKE_W(SCL_COLRAM_ADDR+(bankNm?512:0)+(i<<1),picPals[picNm][i]);


 width=picDatas[picNm][0];
 height=picDatas[picNm][1];
 yoffs=(240-height)>>1;

 SCL_SetWindow(bankNm?SCL_W0:SCL_W1,
	       0,
	       bankNm?(SCL_NBG1|SCL_NBG2|SCL_NBG3):
	              (SCL_NBG0|SCL_NBG2|SCL_NBG3),
	       0xfffffff,0,yoffs,
	       320,yoffs+height-1);

 for (y=0;y<yoffs;y++)
    for (x=0;x<(width>>2);x++)
       POKE(SCL_VDP2_VRAM+(y<<9)+(x<<2)+(bankNm?1024*256:0),0);
 i=2;
 for (y=0;y<height;y++)
    for (x=0;x<(width>>2);x++)
       POKE(SCL_VDP2_VRAM+((y+yoffs)<<9)+(x<<2)+(bankNm?1024*256:0),
	    picDatas[picNm][i++]);
 for (y=height+yoffs;y<240;y++)
    for (x=0;x<(width>>2);x++)
       POKE(SCL_VDP2_VRAM+(y<<9)+(x<<2)+(bankNm?1024*256:0),0);
}

#if 0
static void xFadeUp(void)
{int f;
 for (f=0;f<32;f++)
    {SCL_SetColMixRate(SCL_NBG0,f);
     SCL_DisplayFrame();
    }
}

static void xFadeDown(void)
{int f;
 for (f=31;f>=0;f--)
    {SCL_SetColMixRate(SCL_NBG0,f);
     SCL_DisplayFrame();
    }
}
#endif

static void fadeDown(void)
{int f;
 for (f=0;f>=-255;f-=4)
    {SCL_SetColOffset(SCL_OFFSET_A,SCL_NBG1|SCL_NBG0,f,f,f);
     SCL_DisplayFrame();
    }
 SCL_SetColOffset(SCL_OFFSET_A,SCL_NBG1|SCL_NBG0,-255,-255,-255);
}

static void fadeUp(void)
{int f;
 for (f=-255;f<0;f+=4)
    {SCL_SetColOffset(SCL_OFFSET_A,SCL_NBG1|SCL_NBG0,f,f,f);
     SCL_DisplayFrame();
    }
 SCL_SetColOffset(SCL_OFFSET_A,SCL_NBG1|SCL_NBG0,0,0,0);
}

/* #define CANCELKEYS (PER_DGT_A|PER_DGT_B|PER_DGT_C|PER_DGT_X|PER_DGT_Y|PER_DGT_Z|PER_DGT_S) */
#define CANCELKEYS (PER_DGT_A)

static int wait(int nmFrames)
{int i;
 for (i=0;i<nmFrames;i++)
    {SCL_DisplayFrame();
     if ((inputAccum & CANCELKEYS)!=CANCELKEYS)
	return 1;
    }
 return 0;
}

#if 0
static int waitForFAD(int fad)
{while (getCurrentFAD()<fad)
    if ((inputAccum & CANCELKEYS)!=CANCELKEYS)
       return 1;
 return 0;
}
#endif

void playIntro(void)
{int i;
 displayEnable(0);
 mem_init();
 initSound();
 {
#ifndef JAPAN
  int fd=fs_open("+LOGOS.PCS");
#else
  int fd=fs_open("+JLOGOS.PCS");
#endif
  loadPicSet(fd,picPals,picDatas,MAXNMPICS);
#ifndef PAL
  loadSound(fd);
#endif
  fs_close(fd);
 }
 for (i=0;i<512*1024;i+=4)
    POKE(SCL_VDP2_VRAM+i,0);
 setupVDP2();
 EZ_initSprSystem(500,8,500,
		  240,0x0000);
 i=initFonts(0,7);
 SCL_SetDisplayMode(SCL_NON_INTER,SCL_240LINE,SCL_NORMAL_A);
 SCL_SetFrameInterval(1);
 SPR_SetTvMode(SPR_TV_NORMAL,SPR_TV_320X240,OFF);

 Scl_s_reg.dispenbl|=0xf00;
 if (SclProcess==0)
    SclProcess=1;

 SCL_Open(SCL_NBG0); SCL_MoveTo(0,0,0); SCL_Close();
 SCL_Open(SCL_NBG1); SCL_MoveTo(0,0,0); SCL_Close();

 inputAccum=0xffff;

 SCL_SetColMixRate(SCL_NBG0,0);
 SCL_SetColOffset(SCL_OFFSET_A,SCL_NBG1|SCL_NBG0,-255,-255,-255);
 SCL_DisplayFrame(); SCL_DisplayFrame();
 displayEnable(1);

#ifdef PAL
 loadVDPPic(3,0);

 /* POKE_W(SCL_VDP2_VRAM+0x180020,0x0f03);*/ /* transparency and enables */
 fadeUp();
 if (wait(210))
    goto skipIntro;
 fadeDown();
#endif
#ifdef PAL
 loadVDPPic(1,0);
#else
 loadVDPPic(0,0);
#endif
 fadeUp();
 if (wait(210))
    goto skipIntro;
 fadeDown();
 loadVDPPic(1,0);
 fadeUp();
#ifndef PAL
 playSound(0,0);
#endif
 if (wait(210))
    goto skipIntro;
 fadeDown();
 displayEnable(0);
 playMovie("OPEN.MOV",1);
 skipIntro:
}

static void fadeSegaLogo(void)
{int i,pos;
 int r,g,b;
 POKE_W(SCL_VDP2_VRAM+0x180112,0x00); /* enable line scroll for nbg[01] */
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

static void setVDP2(void)
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

#define MOSAIC 2
#define XD (320/MOSAIC)
#define YD (240/MOSAIC)
#define MAXNMSTRINGS 120


static void stepWaterVel(int *pos,int *vel,int nmRows)
{int *ppos,*pvel,y,x;
 for (y=0;y<nmRows;y++)
    {ppos=pos+y*XD+1;
     pvel=vel+y*XD+1;

     for (x=1;x<XD-1;x++)
	{*pvel+=*(ppos+XD)+*(ppos-XD)+*(ppos+1)+*(ppos-1)-
	    (*ppos<<2);
	 pvel++;
	 ppos++;
	}
    }
}

static void stepWaterPos(int *ppos,int *pvel,int nmCells)
{int y;
 for (y=nmCells;y;y--)
    {*ppos+=(*pvel)>>3;
     *pvel-=(*pvel)>>6;
     ppos++; pvel++;
    }
}

static void drawWater(int *pvel,int vposStart,int nmRows)
{int vpos,y,x;
 vpos=vposStart;
 for (y=0;y<nmRows;y++)
    {for (x=0;x<XD;x++)
	{POKE_B(vpos,(((*pvel)+(1<<14))>>16)+126);
	 pvel++;
	 vpos+=MOSAIC;
	}
     vpos+=512*MOSAIC-(XD*MOSAIC);
    }
}

void credits(int specialMessage)
{int (*pos)[YD][XD];
 int (*vel)[YD][XD];
 int x,y,vpos,i,textYPos;
 int screen=0;
 int dropCount;
 int fading=0;
#ifdef PAL
 static char creditSectionLen[]={3,2,5,2,2,11,2,2,6,8,2,10,7,-1};
#else
 static char creditSectionLen[]={3,2,2,2,2,8,3,2,3,3,11,4,2,2,6,2,7,
				    1,2,2,2,2,2,8,3,3,4,7,1,7,-1};
#endif
#ifdef JAPAN
 int fontChange=100000;
#endif
 XyInt stringPos[MAXNMSTRINGS];
 int nmStrings;
 displayEnable(0);
 SCL_DisplayFrame();
 SCL_DisplayFrame();

 checkStack();
 mem_init();

 EZ_initSprSystem(1000,8,1000,240,0x0000);
#ifdef JAPAN
 {int i;
  i=initFonts(0,7);
  initPicSystem(i,((int []){0,0,0,0,0,70,-1}));
 }
 loadJapanFontPics();
#else
 initFonts(0,7);
#endif

#ifdef PAL
 if (!specialMessage)
    creditSectionLen[12]=-1;
#else
 if (!specialMessage)
    creditSectionLen[29]=-1;
#endif

 {int section,ypos;
  nmStrings=0;
  ypos=0;
  for (section=0;creditSectionLen[section]!=-1;section++)
     {if (specialMessage && creditSectionLen[section+1]==-1)
	 {ypos+=120;
#ifdef JAPAN
	  fontChange=nmStrings;
#endif
	 }
      for (i=0;i<creditSectionLen[section];i++)
	 {assert(nmStrings<MAXNMSTRINGS);
	  stringPos[nmStrings].y=ypos;
	  stringPos[nmStrings].x=
	     -getStringWidth(1,getText(LB_CREDITS,nmStrings))/2;
#ifdef JAPAN
	  if (nmStrings>=fontChange)
	     {stringPos[nmStrings].x=
		 -getStringWidth(3,getText(LB_CREDITS,nmStrings))/2;
	      ypos+=11;
	     }
#endif
	  nmStrings++;
	  ypos+=13;
	 }
      ypos+=13;
     }
 }
 textYPos=-120; /* y coordinate that is in the middle */
 pos=mem_malloc(1,XD*YD*4);
 vel=mem_malloc(1,XD*YD*4);

 {static Uint16 cycle[]=
     {0x44ee, 0xeeee,
	 0x44ee, 0xeeee,
	 0x55ee, 0xeeee,
	 0x55ee, 0xeeee};
  SclConfig scfg;
  SclVramConfig vcfg;
  SCL_InitVramConfigTb(&vcfg);
  vcfg.vramModeA=OFF;
  vcfg.vramModeB=OFF;
  vcfg.vramA0=SCL_NON;
  vcfg.vramB0=SCL_NON;
  SCL_SetVramConfig(&vcfg);
  SCL_SetCycleTable(cycle);
  /* setup VDP2Sprite screen */
  SCL_InitConfigTb(&scfg);
  scfg.dispenbl=ON;
  scfg.bmpsize=SCL_BMP_SIZE_512X256;
  scfg.coltype=SCL_COL_TYPE_256;
  scfg.datatype=SCL_BITMAP;
  scfg.mapover=SCL_OVER_0;
  scfg.plate_addr[0]=0;
  scfg.patnamecontrl=0;
  SCL_SetConfig(SCL_NBG0, &scfg);
  SCL_SET_N0CAOS(0);
  scfg.plate_addr[0]=256*1024;
  SCL_SetConfig(SCL_NBG1, &scfg);
  SCL_SET_N1CAOS(0);
 }
 SCL_SetMosaic(SCL_NBG0|SCL_NBG1,MOSAIC,MOSAIC);
 SCL_SetPriority(SCL_NBG0,1);
 SCL_SetPriority(SCL_NBG1,1);
 SCL_SetPriority(SCL_SP0,4);

 for (x=0;x<256;x++)
    POKE_W(SCL_COLRAM_ADDR+(x<<1),RGB(0,0,0));
 for (x=128-15;x<128+15;x++)
    {POKE_W(SCL_COLRAM_ADDR+(x<<1),RGB(0,0,x-(128-15)));
    }
 for (x=128+15;x<128+15+31;x++)
    POKE_W(SCL_COLRAM_ADDR+(x<<1),RGB(x-128-15,x-128-15,31));


 SCL_SetFrameInterval(0xfffe);
 /* initialize */
 for (y=0;y<YD;y++)
    for (x=0;x<XD;x++)
       {(*pos)[y][x]=0;
	(*vel)[y][x]=0;
       }
 dropCount=10;
 vtimer=0;

 drawWater((int *)vel,SCL_VDP2_VRAM,YD);

 while (1)
    {/* step water */
     if (dropCount--<=0)
	{dropCount=getNextRand()&0xf;
	 x=(getNextRand()%(XD-4))+2;
	 y=(getNextRand()%(YD-4))+2;
	 (*vel)[y][x]=F(25);
	 (*vel)[y+1][x]=F(10);
	 (*vel)[y-1][x]=F(10);
	 (*vel)[y][x+1]=F(10);
	 (*vel)[y][x-1]=F(10);
	}
     /* ... update water vel */
     stepWaterVel(((int *)pos)+XD,
		  ((int *)vel)+XD,
		  YD-2);
     /* ... update water pos */
     stepWaterPos((int *)pos,(int *)vel,YD*XD);
     /* draw water */
     vpos=SCL_VDP2_VRAM;
     if (screen==1)
	vpos+=1024*256;
     drawWater((int *)vel,vpos,YD);
     if (screen)
	{Scl_s_reg.dispenbl&=~1;
	 Scl_s_reg.dispenbl|=2;
	}
     else
	{Scl_s_reg.dispenbl&=~2;
	 Scl_s_reg.dispenbl|=1;
	}
     if (SclProcess==0)
	SclProcess=1;
     screen=!screen;
     /* draw credit text */
     EZ_openCommand();
     EZ_sysClip();
     EZ_localCoord(160,120);
     for (i=0;i<nmStrings;i++)
	{if (stringPos[i].y>textYPos-130 &&
	     stringPos[i].y<textYPos+130)
	    {
#ifdef JAPAN
	     if (i>=fontChange)
		drawString(stringPos[i].x,stringPos[i].y-textYPos,3,
			   getText(LB_CREDITS,i));
	     else
#endif
	     drawString(stringPos[i].x,stringPos[i].y-textYPos,1,
			getText(LB_CREDITS,i));
	    }
	}
     if (specialMessage && stringPos[nmStrings-1].y-textYPos<
#ifndef JAPAN
	 35
#else
	 55
#endif
	 )
	{if ((lastInputSample & CANCELKEYS)!=CANCELKEYS)
	    if (!fading)
	       fading=1;
	}
     else
	textYPos++;

     vtimer=0;
     EZ_closeCommand();
     SPR_WaitDrawEnd();
     SCL_DisplayFrame();
     if (fading)
	{fading+=4;
	 if (fading>255)
	    return;
	 SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG1|SCL_NBG0,
			  -fading,-fading,-fading);
	 setMasterVolume(15-(fading>>4));
	}

     if (!fading && stringPos[nmStrings-1].y-textYPos<-130)
	fading=1;
     displayEnable(1);
#ifdef JAPAN
     pic_nextFrame(NULL,NULL);
#endif
    }
}

void main(void)
{int token;
 BOOT_PROBE(0x7c1f); /* GCC14: magenta = main() reached */
 fadeSegaLogo();
 BOOT_PROBE(0x001f); /* GCC14: red */
 megaInit();
 BOOT_PROBE(0x03e0); /* GCC14: green */
 fs_init();
 BOOT_PROBE(0x7c00); /* GCC14: blue */
 mem_init();
 BOOT_PROBE(0x03ff); /* GCC14: yellow */
 set_imask(0);
 /* aquire system info */
 {PerGetSys *sys_data;
  sys_data=(PerGetSys *)waitSystemData(PadWorkArea); /* GCC14: was PER_LInit + bare PER_GET_SYS poll, see UTIL.C */
  systemMemory=sys_data->sm;
 }
 BOOT_PROBE(0x7fff); /* GCC14: white */
 dPrint("Here we are!\n");
 SCL_Vdp2Init();
 SCL_SetDisplayMode(SCL_NON_INTER,SCL_240LINE,SCL_NORMAL_A);
 SPR_SetEraseData(RGB(0,0,0),0,0,319,239);
 EZ_initSprSystem(1540,8,1524,
		  240,0x8000);
 initFonts(1,7);

 setVDP2();

 displayEnable(1);
 SetVblank();
 abcResetEnable=1;
 token=PEEK(0x02ffffc);
 if (token==GOODEND || token==BADEND || token==SUPERGOODEND)
    {
#ifndef JAPAN
     int fd=fs_open("+INITLOAD.DAT");
     skipPicSet(fd);
     loadLocalText(fd); /* locks memory */
     fs_close(fd);
#else
     int fd=fs_open("+JINITLOD.DAT");
     skipPicSet(fd);
     loadJapanFontData(fd);
     mem_lock();
     loadLocalText(fd); /* locks memory */
     fs_close(fd);
#endif
     initSound();
     if (token==GOODEND || token==SUPERGOODEND)
	playMovie("GOOD.MOV",0);
     else
	playMovie("BAD.MOV",0);
     playCDTrack(endMusic,1);
     credits(token==SUPERGOODEND);
     SCL_SetMosaic(SCL_NBG0|SCL_NBG1,1,1);
    }

 playIntro();

 displayEnable(0);
 SCL_DisplayFrame();

 POKE(0x02ffffc,0);
 link("+MAIN.BIN");
}

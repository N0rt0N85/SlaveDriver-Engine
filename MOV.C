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

#include "pic.h"
#include "file.h"
#include "util.h"
#include "spr.h"
#include "print.h"
#include "v_blank.h"
#include "local.h"
#include "sound.h"
#include "dma.h"

#define ENDCODE -1
#define USEDMA 0

#define MAXNMBUFFERS 2000
static int qPos,qHead,qTail,nmBuffers;
static unsigned char **buffer;

static int sndRingPos,BPS,waveChannels;
static int ourPrtn;

static GfsHn cdFile; /* only one cd file may be open at a time */

static Sint32 bsize;


static int maybeReadBuffer(void)
{int nextHead;
 GFS_NwExecOne(cdFile);
 assert(qHead<nmBuffers);
 nextHead=qHead+1;
 if (nextHead==nmBuffers)
    nextHead=0;
 if (nextHead==qTail)
    return 0;

 CDC_GetSctNum(ourPrtn,&bsize);
 if (bsize>0)
    {/* suck down another buffer full */
     GFS_Fread(cdFile,1,buffer[qHead],2048);
     qHead=nextHead;
     return 1;
    }
 return 0;
}

static unsigned char deQByte(void)
{unsigned char r=*(buffer[qTail]+qPos);
 assert(qPos<2048);
 assert(qTail<nmBuffers);
 qPos++;
 if (qPos>=2048)
    {qTail++;
     if (qTail==nmBuffers)
	qTail=0;
     qPos=0;
    }
 return r;
}

static unsigned short deQShort(void)
{int r;
 r=deQByte()<<8;
 r+=deQByte();
 return r;
}

#define deQCount deQInt

static int deQInt(void)
{int r;
 r=deQByte()<<24;
 r|=deQByte()<<16;
 r|=deQByte()<<8;
 r|=deQByte();
 return r;
}

static void deQCopy(void *dest,int size)
{int s;
 assert(qPos<2048);
 while (size)
    {s=2048-qPos;
     if (s>size)
	{
	 /* assert(!(((int)dest) & 3));
	    assert(!(qPos & 3)); */
#if USEDMA
	 dmaMemCpy(buffer[qTail]+qPos,dest,size);
#else
	 qmemcpy(dest,buffer[qTail]+qPos,size);
#endif
	 qPos+=size;
	 break;
	}
     /* assert(!(((int)dest) & 3));
	assert(!(qPos & 3)); */
#if USEDMA
     dmaMemCpy(buffer[qTail]+qPos,dest,s);
#else
     qmemcpy(dest,buffer[qTail]+qPos,s);
#endif
     dest+=s;
     size-=s;
     assert(qTail<nmBuffers);
     qTail++;
     if (qTail==nmBuffers)
	qTail=0;
     qPos=0;
    }
}

#define SNDBASE 0x05a00000

static void deQSound(void)
{int amount;
 int s;
 int saveAmount,saveSndRingPos;
 amount=deQInt();
 assert(amount>=0);
 assert(amount<40000);
 saveAmount=amount;
 saveSndRingPos=sndRingPos;

 while (amount)
    {s=65536-sndRingPos;
     if (s>amount)
	{if (BPS==16)
	    deQCopy((void *)(SNDBASE+(sndRingPos<<1)),amount<<1);
	 else
	    deQCopy((void *)(SNDBASE+(sndRingPos)),amount);
	 sndRingPos+=amount;
	 break;
	}
     if (BPS==16)
	deQCopy((void *)(SNDBASE+(sndRingPos<<1)),s<<1);
     else
	deQCopy((void *)(SNDBASE+(sndRingPos)),s);
     sndRingPos=0;
     amount-=s;
    }
 if (waveChannels==1)
    return;
 amount=saveAmount;
 sndRingPos=saveSndRingPos;
 while (amount)
    {s=65536-sndRingPos;
     if (s>amount)
	{if (BPS==16)
	    deQCopy((void *)(1024*128+SNDBASE+(sndRingPos<<1)),amount<<1);
	 else
	    deQCopy((void *)(1024*128+SNDBASE+(sndRingPos)),amount);
	 sndRingPos+=amount;
	 break;
	}
     if (BPS==16)
	deQCopy((void *)(1024*128+SNDBASE+(sndRingPos<<1)),s<<1);
     else
	deQCopy((void *)(1024*128+SNDBASE+(sndRingPos)),s);
     sndRingPos=0;
     amount-=s;
    }
}

#define FRAMESKIP 3
#define NMLINES 6

void playMovie(char *fileName,int canSkip)
{unsigned char *booffer[MAXNMBUFFERS];
 short nmFrames,frame;
 int waveRate;
 char *text;
 char *line[NMLINES];
 int *textData;
 short linePos[NMLINES],nmChars[NMLINES];
 int i,size,screenPos,charNm,vdp1Vram,textOffset=0,textDone=0;

 checkStack();

 Scl_s_reg.dispenbl=0x000;
 if (SclProcess==0)
    SclProcess=1;

 buffer=booffer; /* naf */
 mem_init();
 initSound();

 textData=mem_malloc(1,1024*6);
 /* allocate buffers */
 nmBuffers=0;
/* for (i=0;i<512*1024;i+=2048)
    buffer[nmBuffers++]=(char *)(SCL_VDP2_VRAM+i);*/

 do
    buffer[nmBuffers++]=mem_nocheck_malloc(!(nmBuffers&1),2048);
 while (buffer[nmBuffers-1]);
 nmBuffers--;

 dPrint("Movie allocated %d buffers.\n",nmBuffers);
 qHead=0; qTail=0; qPos=0;

 {int id=GFS_NameToId(fileName);
  assert(id>=0);
  cdFile=GFS_Open(id);
  assert(cdFile);
  GFS_SetTmode(cdFile,GFS_TMODE_CPU);
  GFS_NwCdRead(cdFile,1500*2048); /* start prefetch */
  GFS_SetTransPara(cdFile,10);
 }

 for (i=0;i<150/*nmBuffers-1*/;i++)
    GFS_Fread(cdFile,1,buffer[i],2048);

 qHead=i;
 qPos=0;
 qTail=0;

 ourPrtn=CDC_CdGetLastBuf((Sint32 *)&ourPrtn);

 /* read language file */
 {int size;
  size=deQInt();
  if (size)
     {deQCopy(textData,1024*6-4);
      text=(char *)
	 (((int)textData)+textData[textData[getLanguageNumber()]>>2]);
      dPrint("%d %d offset=%d\n",
	     getLanguageNumber(),
	     textData[getLanguageNumber()],
	     textData[textData[getLanguageNumber()]>>2]);
     }
  else
     text=NULL;
  if (getLanguageNumber()==0)
     text=NULL;
 }
 for (i=0;i<NMLINES;i++)
    {line[i]=text;
     linePos[i]=0;
     nmChars[i]=0;
    }

 EZ_initSprSystem(600,8,600,0,0x8000);
 SPR_SetEraseData(0x8000,0,0,319,239);
 charNm=initFonts(0,7);
 EZ_setChar(charNm,COLOR_5,320,240,NULL);
 vdp1Vram=(EZ_charNoToVram(charNm)<<3)+0x25c00000;

 SCL_SetDisplayMode(SCL_NON_INTER,SCL_240LINE,SYS_GETSYSCK? SCL_NORMAL_B: SCL_NORMAL_A);  /* GCC14: keep the clock */
 SCL_SetFrameInterval(FRAMESKIP);
 SPR_SetTvMode(SPR_TV_NORMAL,SPR_TV_320X240,OFF);
 SPR_SetEraseData(0x0000,0,0,319,239);

 SCL_DisplayFrame();

 for (i=0;i<320*240*2;i+=2)
    POKE_W(vdp1Vram+i,RGB(0,0,0));
 /* POKE_W(SCL_VDP2_VRAM+0x180020,0x0f03); *//* transparency and enables */

 /* read movie header */
 for (i=0;i<6;i++)
    deQByte();
 nmFrames=deQShort();
 dPrint("Movie has %d frames\n",nmFrames);

 BPS=deQShort();
 waveChannels=deQShort();
 waveRate=deQShort();
 deQShort(); /* padding to keep dword alignment */

 sndRingPos=0;
 deQSound();
 if (BPS)
    {/* start sound */
     int base=SNDBASE+0x100000;
     POKE_W(base+2,0); /* start addr */
     POKE_W(base+6,0xffff); /* end addr */
     POKE_W(base+0x10,waveRate);
     if (waveChannels==2)
	POKE_W(base+0x16,(7<<13)|(0x1f<<8)); /* vol and pan */
     else
	POKE_W(base+0x16,(7<<13)|(0<<8)); /* vol and pan */
     POKE_W(base+4,0); /* loop start */

     if (waveChannels==2)
	{int base=SNDBASE+0x100000+0x40;
	 if (BPS==16)
	    POKE_W(base,0x800|(1<<5)|0x02);
	 else
	    POKE_W(base,0x800|(1<<5)|0x12);
	 POKE_W(base+2,0x0000); /* start addr */
	 POKE_W(base+6,0xffff); /* end addr */
	 POKE_W(base+0x10,waveRate);
	 POKE_W(base+0x16,(7<<13)|(0xf<<8)); /* vol and pan */
	 POKE_W(base+4,0); /* loop start */
	}

     if (BPS==16)
	POKE_W(base,0x1800|(1<<5)); /* kon, ex and loop */
     else
	POKE_W(base,0x1800|(1<<5)|0x10); /* kon, ex and loop */
    }

/* #define ALLKEYS  (PER_DGT_A|PER_DGT_B|PER_DGT_C|PER_DGT_X|PER_DGT_Y|PER_DGT_Z|PER_DGT_S|PER_DGT_TL|PER_DGT_TR) */
#define ALLKEYS (PER_DGT_A)

 for (frame=0;frame<nmFrames;frame++)
    {/* decode next frame in buffer */
     if (frame<=60)
	SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0|SCL_RBG0,
			 4*(frame-60),4*(frame-60),4*(frame-60));
     vtimer=0;
     screenPos=0;
     if (canSkip && (lastInputSample & ALLKEYS)!=ALLKEYS)
	{break;
	 /* while (maybeReadBuffer());
	    SCL_DisplayFrame();
	    continue; */
	}
     while (1)
	{/* decode unchanged area */
	 size=deQCount();
	 if (size==ENDCODE)
	    break;
	 assert(size<80000);
	 assert(size>=0);
	 screenPos+=size;
	 /* decode changed area */
	 size=deQCount();
	 if (size==ENDCODE)
	    break;
	 assert(size<80000);
	 assert(size>=0);
	 if (size>0)
	    {deQCopy((void *)(vdp1Vram+(screenPos<<1)),size<<1);
	     screenPos+=size;
	    }
	}
     EZ_openCommand();
     EZ_localCoord(0,0);
     EZ_sysClip();
     {XyInt pos[1];
      pos->x=0;
      pos->y=0;
      EZ_normSpr(0,ECDSPD_DISABLE|COLOR_5,0,charNm,pos,NULL);
     }
#ifndef NDEBUG
     {XyInt parms[4];
      int fullBuffers;
      fullBuffers=qHead-qTail;
      if (fullBuffers<0)
	 fullBuffers+=nmBuffers;
      parms[0].x=0; parms[0].y=220;
      parms[1].x=0; parms[1].y=230;
      parms[2].x=fullBuffers>>2; parms[2].y=230;
      parms[3].x=fullBuffers>>2; parms[3].y=220;
      EZ_polygon(ECD_DISABLE|SPD_DISABLE,RGB(0,0,31),
		 parms,NULL);
     }
#endif
     /* draw subtitle */
     if (text)
	{int y;
	 XyInt pos[4];
	 pos[0].x=0; pos[0].y=170;
	 pos[1].x=319; pos[1].y=239;
	 EZ_userClip(pos);

	 pos[0].x=15; pos[0].y=165;
	 pos[1].x=304; pos[1].y=165;
	 pos[2].x=304; pos[2].y=229;
	 pos[3].x=15; pos[3].y=229;
	 EZ_polygon(COMPO_TRANS|ECDSPD_DISABLE,RGB(0,0,0),pos,NULL);

	 y=160+textOffset;
	 for (i=0;i<NMLINES;i++)
	    {if (nmChars[i]>0)
		drawStringN(linePos[i],y,1,line[i],nmChars[i]);
	     y+=10;
	    }

	 /* advance subtitle */
	 if (!(frame&0x3))
	    textOffset--;
	 if (textOffset<0)
	    {int width;
	     char *c;
	     textOffset=9;
	     /* ... move all lines up one */
	     for (i=0;i<NMLINES-1;i++)
		{line[i]=line[i+1];
		 linePos[i]=linePos[i+1];
		 nmChars[i]=nmChars[i+1];
		}
	     /* ... add new line on end */
	     if (textDone)
		{line[NMLINES-1]=NULL;
		 linePos[NMLINES-1]=0;
		 nmChars[NMLINES-1]=0;
		}
	     else
		{c=text;
		 /* skip initial white space */
		 while (*c==' ')
		    c++;
		 line[NMLINES-1]=c;
		 width=0;
		 for (;*c;c++)
		    {if (*c=='\n')
			break;
		     width+=getCharWidth(1,*c)+1;
		     if (width>280)
			break;
		    }
		 if (!*c)
		    {textDone=1;
		    }
		 if (*c!='\n')
		    while (*c!=0 && *c!=' ')
		       {width-=getCharWidth(1,*c)+1;
			c--;
		       }
		 text=c+1;
		 if (*c=='\n')
		    c--;
		 linePos[NMLINES-1]=160-(width>>1);
		 nmChars[NMLINES-1]=c-line[NMLINES-1];
		}
	    }
	}
     EZ_closeCommand();
     EZ_executeCommand();
     deQSound();
#if 1
     PEEK_W(SCL_VDP2_VRAM+0x180002);
     while ((vtimer<FRAMESKIP-1 ||
	     (0x3ff & PEEK_W(SCL_VDP2_VRAM+0x18000a))<150) &&
	    maybeReadBuffer())
	PEEK_W(SCL_VDP2_VRAM+0x180002);
#else
     while (maybeReadBuffer());
#endif
     SPR_WaitDrawEnd();
     EZ_clearCommand();
     SCL_DisplayFrame();
     displayEnable(1);
    }
 POKE_W(SNDBASE+0x100000+0x00,0x0000|(1<<5));
 POKE_W(SNDBASE+0x100000+0x40,0x1000|(1<<5)|0x02);
 GFS_NwStop(cdFile); /* stop prefetch */
 GFS_Close(cdFile);
}

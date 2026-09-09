#include <machine.h>
#include <sega_xpt.h>
#include <sega_int.h>
#include <sega_per.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_cdc.h>
#include <sega_sys.h>
#include "v_blank.h"
#include "util.h"

#define INPUTQSIZE 16 /* dont change this */
volatile unsigned short inputQ[INPUTQSIZE]; /* GCC14: header declares it volatile */
volatile unsigned short lastInputSample; /* GCC14: header declares it volatile */
volatile unsigned short inputAccum; /* GCC14: header declares it volatile */
volatile char controlerPresent;
volatile char analogControlerPresent;
volatile short analogX;
volatile short analogY;
volatile char analogIndexButtonsPresent;
volatile short analogTR;
volatile short analogTL;
volatile char inputQHead,inputQTail;

volatile int fadeDir,fadePos,fadeEnd;
volatile int abcResetEnable=0; /* GCC14: header declares it volatile */
int abcResetDisable=0;

/*
volatile Uint16	PadData1  = 0x0000;
volatile Uint16	PadData1E = 0x0000;
volatile Uint16	PadData2  = 0x0000;
volatile Uint16	PadData2E = 0x0000;
*/
Uint8 *Pad;
PerMulInfo *Mul;
volatile Uint32 vtimer = 0, htimer = 0, secs = 0;

Uint8  PadWorkArea[4*(PER_SIZE_NCON_15*6+69)];

void UsrHblankIn(void)
{htimer++;
}

void processInput(void)
{unsigned short accum;
 int i,pos,size,id;
 accum=0xffff;
 PER_LGetPer((void **)&Pad,&Mul);

 if (!Pad)
    return;

 pos=0;
 controlerPresent=0;
 analogControlerPresent=0;
 analogIndexButtonsPresent=0;
 for (i=0;i<Mul[0].con;i++)
    {id=Pad[pos++];
     size=Pad[pos++];
     if (id==PER_ID_DGT ||
	 (id==PER_ID_ANL && size!=3 /* ignore steering wheel */))
	{accum&=(Pad[pos]<<8)|(Pad[pos+1]);
	 controlerPresent=1;
	 if (id==PER_ID_ANL)
	    {analogControlerPresent=1;
	     analogX=Pad[pos+2]-128;
	     analogY=Pad[pos+3]-128;
	     if (size==6)
		{analogIndexButtonsPresent=1;
		 analogTR=Pad[pos+4];
		 analogTL=Pad[pos+5];
		}
	    }
	 break;
	}
     pos+=15;
    }
 if (!(accum & (PER_DGT_A|PER_DGT_B|PER_DGT_C|PER_DGT_S)))
    {if (abcResetEnable && !abcResetDisable)
	SYS_EXECDMP();
     abcResetDisable=1;
    }
 else
    abcResetDisable=0;
 lastInputSample=accum;
 inputAccum&=accum;
 /* add input sample */
 inputQ[(int)inputQHead]=accum;
 inputQHead=(inputQHead+1)&0xf;
}

volatile char enableDoorReset=1;

void UsrVblankStart(void)
{SCL_VblankStart();
 if (enableDoorReset && (CDC_GetHirqReq() & CDC_HIRQ_DCHG))
    SYS_EXECDMP();
 if (fadeDir)
    {fadePos+=fadeDir;
     if (fadeDir>0 && fadePos>fadeEnd)
	{fadePos=fadeEnd;
	 fadeDir=0;
	}
     if (fadeDir<0 && fadePos<fadeEnd)
	{fadePos=fadeEnd;
	 fadeDir=0;
	}
#if 0
     POKE_W(SCL_VDP2_VRAM+0x180110,0x7f);/*enable color offset for all planes*/
     POKE_W(SCL_VDP2_VRAM+0x180112,0x00);/*use offset reg 0 for all planes*/
#endif

     POKE_W(SCL_VDP2_VRAM+0x180114,fadePos & 0x1ff);
     POKE_W(SCL_VDP2_VRAM+0x180116,fadePos & 0x1ff);
     POKE_W(SCL_VDP2_VRAM+0x180118,fadePos & 0x1ff);
    }
}

void initInput(void)
{int o=get_imask();
 set_imask(0xff);
 inputQHead=0;
 inputAccum=0xffff;
 lastInputSample=0xffff;
 set_imask(o);
}

#ifdef BOOTPROBE
#ifndef PROBESET
#define PROBESET 31
#endif
volatile unsigned short bootStage=0;

/* GCC14: the boot/hang probe, see util.h.  Called from the checkpoint itself (it has to work
   before SetVblank) and from every vblank-out, which is the last VDP2 write of the field --
   SCL_ScrollShow runs at vblank-in. */
void bootProbePaint(void)
{unsigned short c;
 if (!bootStage)
    return;
 c=bootStage & 0x7fff;
 if (vtimer & ((bootStage & 0x8000)? 8: 32))
    c=(c>>1)&0x3def;			/* half brightness: throb, MAIN.BIN faster than INIT.BIN */
 /* PROBESET selects which of these writes happen, to find out which one a boot depends on;
    the default writes them all.  Constant mask, so the disabled ones fold away. */
 if (PROBESET & 1)
    POKE_W(SCL_VDP2_VRAM+0x180000,0x8000);	/* display on                */
 if (PROBESET & 2)
    POKE_W(SCL_VDP2_VRAM+0x180020,0);	/* every background off      */
 if (PROBESET & 4)
    POKE_W(SCL_VDP2_VRAM+0x180110,0);	/* no colour offset          */
 if (PROBESET & 8)
    {POKE_W(SCL_VDP2_VRAM+0x1800ac,0);	/* back screen table address */
     POKE_W(SCL_VDP2_VRAM+0x1800ae,0);	/*    ... at VDP2 VRAM 0     */
    }
 if (PROBESET & 16)
    POKE_W(SCL_VDP2_VRAM,c);
}
#endif

void UsrVblankEnd(void)
{SCL_VblankEnd();
 vtimer++;
 processInput();
#ifdef BOOTPROBE
 bootProbePaint();
#endif
}


volatile Sint32	perFlag;

void CheckVblankEnd(void)
{perFlag = 0;
}

#ifndef NDEBUG

int errorQ[16]={0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
int prQ[16]={0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0};
int errorQSize=0;

#pragma interrupt
void userBreakBlam(void)
{int dummy[1];
 __asm__ volatile ("sts pr,%0\n" /* GCC14: GNU as rejects "sts.l pr,Rn"; PR->register is "sts" */
		   : "=r" (dummy[0])); /* GCC14: plain lvalue as output operand */
 errorQ[errorQSize]=dummy[8]; /* GCC14: the exception PC; GCC 14's prologue pushes 28 bytes
    of registers before dummy, so the hardware-pushed PC sits at @(32,r15) = dummy[8] */
 prQ[errorQSize]=dummy[0];
 errorQSize=(errorQSize+1)&0xf;

 fadePos=255;
 fadeDir=-10;
 fadeEnd=0;

 return;
#if 0
 int i,x,y;
 for (y=0;y<256;y++)
    for (x=0;x<512;x++)
       {POKE_W(SCL_VDP2_VRAM+(y<<10)+(x<<1),RGB(x&0xf,x&0xf,x&0xf));
       }
 POKE_W(SCL_VDP2_VRAM+0x180110,0x7f);/*enable color offset for all planes*/
 POKE_W(SCL_VDP2_VRAM+0x180112,0x00);/*use offset reg 0 for all planes*/
 POKE_W(SCL_VDP2_VRAM+0x180114,0); /* zero the color offset */
 POKE_W(SCL_VDP2_VRAM+0x180116,0);
 POKE_W(SCL_VDP2_VRAM+0x180118,0);
 POKE_W(SCL_VDP2_VRAM+0x180020,1); /* turn nbg0 on */
 POKE_W(SCL_VDP2_VRAM+0x180028,0x32); /* set 16 bit color & on bitmap */
 POKE_W(SCL_VDP2_VRAM+0x180040,0x0);
 POKE_W(SCL_VDP2_VRAM+0x180042,0x0);
 POKE_W(SCL_VDP2_VRAM+0x18003c,0x0);
 POKE_W(SCL_VDP2_VRAM+0x18003e,0x0);

 POKE_W(SCL_VDP2_VRAM+0x18003e,0x0);

 for (i=0;i<8;i++)
    POKE(SCL_VDP2_VRAM+(i<<4)+0x180010,0x4444);
 while (1);
 while (1)
    for (i=-255;i<255;i++)
       {POKE_W(SCL_VDP2_VRAM+0x180114,i & 0x1ff);
	POKE_W(SCL_VDP2_VRAM+0x180116,i & 0x1ff);
	POKE_W(SCL_VDP2_VRAM+0x180118,i & 0x1ff);
       }
#endif
}
#endif

void SetVblank(void)
{/* V_Blank Out */
 fadeDir=0;
 fadePos=0;
 fadeEnd=0;

 INT_ChgMsk(INT_MSK_NULL,INT_MSK_VBLK_IN | INT_MSK_VBLK_OUT);
 INT_SetScuFunc(INT_SCU_VBLK_OUT,CheckVblankEnd);
 INT_ChgMsk(INT_MSK_VBLK_OUT,INT_MSK_NULL);

 perFlag = 1;
 while(perFlag);

 PER_LInit(PER_KD_PERTIM,6,PER_SIZE_NCON_15,PadWorkArea,0);

 /* V-Blank */
 INT_ChgMsk(INT_MSK_NULL,INT_MSK_VBLK_IN|INT_MSK_VBLK_OUT|INT_MSK_HBLK_IN);
#ifdef STATUSTEXT
 INT_SetScuFunc( INT_SCU_HBLK_IN, UsrHblankIn );
#endif
 INT_SetScuFunc( INT_SCU_VBLK_IN, UsrVblankStart );
 INT_SetScuFunc( INT_SCU_VBLK_OUT, UsrVblankEnd );
#ifdef STATUSTEXT
 INT_ChgMsk(INT_MSK_VBLK_IN|INT_MSK_VBLK_OUT|INT_MSK_HBLK_IN,INT_MSK_NULL);
#else
 INT_ChgMsk(INT_MSK_VBLK_IN|INT_MSK_VBLK_OUT,INT_MSK_NULL);
#endif

#if 1
#ifndef NDEBUG
 /* set user break interrupt */
/* asm("trapa #34");*/
 POKE_W(0xffffff48,0); /* break bus cycle register */
/* INT_SetScuFunc(12,userBreakBlam); */
 POKE(0x06000000+0x30,userBreakBlam);
#if 1
 POKE(0xffffff40,0x00000000); /* break address */
 POKE(0xffffff44,0x00000fff); /* break address mask */
 POKE_W(0xffffff48,0x06c); /* break bus cycle register */
#endif
#endif
#endif
}

/* CRASH.C -- the freeze and crash report, an engine service (GCC14).

   A console that stops on a still image says nothing about where it stopped.  Two ways into a
   report that names the instruction:
   - an exception on the master CPU -- illegal instruction, illegal slot, CPU or DMA address
     error: its vector (crash_gnu.S) records the cause, PC, SR, PR and SP, and returns into
     crashEntry instead of the faulting code;
   - a hang: the game loop beats once per image (crashBeat, CRASH.H); the VBlank-OUT interrupt
     counts the fields since, and past CRASHHANGFIELDS sends the code it interrupted to
     crashEntry through the same return, so PC is where the loop was stuck.  crashVbOut
     (crash_gnu.S) sits on the vector in front of the BIOS handler and records where the
     hardware pushed that PC.
   crashEntry restarts from the top of the master's stack (UTIL.C mystack: every frame on it is
   abandoned, so no RAM is taken from the levels) and never returns: it rebuilds a VDP1 list
   and prints the report -- where in the frame (the profile path), the camera, the slave's
   step, and the VDP1 as the hang left it (crashReadVdp1).  PC and PR are looked up in the
   MAIN.map of the same build (the BUILD line).
   A, B, C and START together still reset.
   Not covered: a hang with the interrupts masked (no VBlank reaches it), and the slave's own
   exceptions -- the master then waits for it, so the report shows that wait, and SLAVE says
   where the slave was. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "sega_spr.h"
#include "sega_scl.h"
#include "sega_sys.h"
#include "util.h"
#include "spr.h"
#include "print.h"
#include "profile.h"
#include "file.h"
#include "sprite.h"
#include "crash.h"

extern Sprite *camera;
extern Orient playerAngle;
extern volatile int slaveJob,slaveStep;
extern int vdp1PrevWalk,vdp1PrevMaxX,vdp1PrevMaxY;  /* WALLS.H, WALKPROBE builds */

#define CRASHHANGFIELDS 240     /* 4 s: the slowest image the loop draws is 8 fields */

int crashCause;                 /* 0 a hang, else the exception vector */
unsigned int crashPC,crashSR,crashPR,crashSP;   /* crashPR: exceptions only */
unsigned int crashEntryPR;      /* PR on arrival in crashEntry: the stuck code's, for a hang */
unsigned int *crashFrame;       /* crashVbOut: this interrupt's hardware frame, PC then SR */
void *crashVbOutChain;          /* the handler crashVbOut hands over to */

void crashVbOut(void);
void crashEntry(void);
void crashExc4(void);
void crashExc6(void);
void crashExc9(void);
void crashExc10(void);

static const unsigned char crashVector[4]={4,6,9,10};
static void (*const crashStub[4])(void)={crashExc4,crashExc6,crashExc9,crashExc10};
static void *crashOld[4];
static int crashIn;

/* VBlank-OUT (V_BLANK.C vblankOutHook), inside the interrupt crashVbOut framed */
static void crashCheck(void)
{unsigned int *fr=crashFrame;
 if (!crashArmed || !fr)
    return;
 if (++crashFields<CRASHHANGFIELDS)
    return;
 if (fr[1] & 0xf0)
    return;                     /* it interrupted another interrupt: wait for the loop's code */
 crashArmed=0;
 crashCause=0;
 crashPC=fr[0];
 crashSR=fr[1];
 crashSP=(unsigned int)(fr+2);
 fr[0]=(unsigned int)crashEntry; /* the handler's RTE lands there ... */
 fr[1]&=~0xf0;                   /* ... with the interrupts open: the report needs the VBlank */
}

/* Gives the vectors back: before another program replaces this one (FILE.C link) */
static void crashUninstall(void)
{int i;
 if (!crashIn)
    return;
 crashArmed=0;
 vblankOutHook=NULL;
 if (SYS_GETSINT(0x41)==(void *)crashVbOut)
    SYS_SETSINT(0x41,crashVbOutChain);
 for (i=0;i<4;i++)
    SYS_SETSINT(crashVector[i],crashOld[i]);
 crashIn=0;
 linkHook=NULL;
}

void crashInstall(void)
{void *v;
 int i;
 v=SYS_GETSINT(0x41);           /* INT_SCU_VBLK_OUT: the BIOS handler that calls UsrVblankEnd */
 if (v!=(void *)crashVbOut)
    {crashVbOutChain=v;
     SYS_SETSINT(0x41,(void *)crashVbOut);
    }
 if (!crashIn)
    for (i=0;i<4;i++)
       {crashOld[i]=SYS_GETSINT(crashVector[i]);
	SYS_SETSINT(crashVector[i],(void *)crashStub[i]);
       }
 crashIn=1;
 vblankOutHook=crashCheck;
 linkHook=crashUninstall;
}

/* The VDP1 as the hang left it, read before crashScreen rebuilds the list.  A master stuck in
   SPR_WaitDrawEnd means the VDP1 never reached the end of the list it is drawing (EDSR CEF):
   COPR, sampled over six fields, tells a command that does not end (still) from a loop in the
   list (moving); the command at the first sample, the root and end commands (VRAM 0 and 32)
   and both lists' spans (SPR.C EZ_listInfo) tell which.  Command numbers throughout: VRAM
   address / 32, so COPR and LOPR (address / 8) and CMDLINK (address / 8) are shown >> 2. */
#define VDP1REG(o) (*(volatile unsigned short *)(0x25d00000+(o)))
#define VDP1CMD(n) ((volatile unsigned short *)(0x25c00000+((n)<<5)))
static unsigned short crashEdsr,crashLopr,crashCopr[6],crashCmd[16],crashRoot[2],crashEnd;
static unsigned int crashDsta;
static int crashLists[5];

static void crashReadVdp1(void)
{int i,t;
 unsigned int v;
 crashEdsr=VDP1REG(0x10);
 crashLopr=VDP1REG(0x12);
 crashDsta=*(volatile unsigned int *)0x25fe007c;    /* SCU DMA status */
 for (i=0;i<6;i++)
    {crashCopr[i]=VDP1REG(0x14);
     v=vtimer;
     for (t=0;vtimer==v && t<4000000;t++)
	;
    }
 for (i=0;i<16;i++)
    crashCmd[i]=VDP1CMD(crashCopr[0]>>2)[i];
 crashRoot[0]=VDP1CMD(0)[0];
 crashRoot[1]=VDP1CMD(0)[1];
 crashEnd=VDP1CMD(1)[0];
 EZ_listInfo(crashLists);
}

static const char *crashName(int c)
{switch (c)
    {case 0:  return "FREEZE";
     case 4:  return "ILLEGAL INSTRUCTION";
     case 6:  return "ILLEGAL SLOT";
     case 9:  return "CPU ADDRESS ERROR";
     case 10: return "DMA ADDRESS ERROR";
    }
 return "EXCEPTION";
}

/* One line in the menu font (FONT1.H), upper case to read on a photo.  That font has no '>'
   and no '+': the profile path's separator is printed as '/'. */
static int crashY;
static void crashLine(const char *fmt,...)
{char buf[96];
 int i;
 va_list ap;
 va_start(ap,fmt);
 vsprintf(buf,fmt,ap);
 va_end(ap);
 for (i=0;buf[i];i++)
    if (buf[i]>='a' && buf[i]<='z')
       buf[i]-='a'-'A';
    else if (buf[i]=='>')
       buf[i]='/';
 drawString(12,crashY,1,(unsigned char *)buf);
 crashY+=11;
}

void crashScreen(void)
{char path[128];
 int i,n,sector=-1,x=0,y=0,z=0;
 crashArmed=0;
 vblankOutHook=NULL;
 crashReadVdp1();
 profilePath(path,sizeof(path));
 if (camera)
    {sector=camera->s;
     x=f(camera->pos.x); y=f(camera->pos.y); z=f(camera->pos.z);
    }
 displayEnable(1);
 EZ_initSprSystem(1000,8,1000,240,RGB(0,0,0));
 initFonts(0,7);
 SCL_SetFrameInterval(1);
 SPR_SetEraseData(0x8000,0,0,319,239);
 for (;;)
    {SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0,0,0,0);
     EZ_openCommand();
     EZ_sysClip();
     EZ_localCoord(0,0);
     crashY=12;
     crashLine("%s (%d)",crashName(crashCause),crashCause);
     crashY+=3;
     crashLine("PC %08X  PR %08X",crashPC,crashCause? crashPR: crashEntryPR);
     crashLine("SR %08X  SP %08X",crashSR,crashSP);
     crashY+=3;
     n=strlen(path);
     for (i=0;i<n || !i;i+=36)
	crashLine("%s%.36s",i? "   ": "IN ",path+i);
     crashY+=3;
     crashLine("SECTOR %d  AT %d %d %d",sector,x,y,z);
     crashLine("YAW %d  PITCH %d",playerAngle.yaw>>16,playerAngle.pitch>>16);
     crashLine("SLAVE JOB %d STEP %X",slaveJob,
	       *(volatile int *)((int)&slaveStep|0x20000000));
     crashY+=3;
     crashLine("EDSR %X  LOPR %X  DMA %X",crashEdsr,crashLopr>>2,crashDsta);
     crashLine("COPR %X %X %X %X %X %X",crashCopr[0]>>2,crashCopr[1]>>2,
	       crashCopr[2]>>2,crashCopr[3]>>2,crashCopr[4]>>2,crashCopr[5]>>2);
     crashLine("LIST %X-%X %X-%X OPEN %d",crashLists[1],crashLists[2],
	       crashLists[3],crashLists[4],crashLists[0]);
     crashLine("ROOT %04X %X  END %04X",crashRoot[0],crashRoot[1]>>2,crashEnd);
     crashLine("CMD %04X %X %04X %04X %04X %04X",crashCmd[0],crashCmd[1]>>2,
	       crashCmd[2],crashCmd[3],crashCmd[4],crashCmd[5]);
     crashLine("A %d %d  B %d %d",(short)crashCmd[6],(short)crashCmd[7],
	       (short)crashCmd[8],(short)crashCmd[9]);
     crashLine("C %d %d  D %d %d",(short)crashCmd[10],(short)crashCmd[11],
	       (short)crashCmd[12],(short)crashCmd[13]);
#ifdef WALKPROBE
     crashLine("WALK %dK  MAX X %d Y %d",vdp1PrevWalk>>4,vdp1PrevMaxX,vdp1PrevMaxY);
#endif
     crashY+=3;
     crashLine("BUILD %s %s",__DATE__,__TIME__);
     crashLine("A B C START TOGETHER RESETS");
     EZ_closeCommand();
     SCL_DisplayFrame();
    }
}

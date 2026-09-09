#include<libsn.h>
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<sega_mem.h>
#include "sega_spr.h"
#include "sega_scl.h"
#include "sega_mth.h"
#include "print.h"
#include "sega_per.h"
#include "util.h"
#include "spr.h"


#include "level.h"
#include "v_blank.h"

#ifndef NDEBUG
int extraStuff;
#endif

char enable_stereo;
char enable_music;
char cheatsEnabled;

unsigned int systemMemory;

#define STICKSIZE 5096
int mystack[STICKSIZE];
void *_stackinit=&mystack[STICKSIZE];

MthXyz *getVertex(int vindex,MthXyz *out)
{out->x=F(level_vertex[vindex].x);
 out->y=F(level_vertex[vindex].y);
 out->z=F(level_vertex[vindex].z);
 return out;
}

int findFloorDistance(int s,MthXyz *p)
{int w;
 sWallType *floor;
 for (w=level_sector[s].firstWall;w<=level_sector[s].lastWall;w++)
    {if (level_wall[w].normal[1]<=0)
	continue;
     break;
    }

 if (w>level_sector[s].lastWall)
    return 0;
 assert(w<=level_sector[s].lastWall);

 floor=level_wall+w;
 if (floor->normal[1]==F(1))
    {assert(floor->normal[0]==0 && floor->normal[2]==0);
     return p->y-F(level_vertex[floor->v[0]].y);
    }
 {Fixed32 planeDist;
  MthXyz wallP;
  getVertex(floor->v[0],&wallP);
  planeDist=
     (f(p->x-wallP.x))*floor->normal[0]+
	(f(p->z-wallP.z))*floor->normal[2];
  return p->y-(wallP.y-MTH_Div(planeDist,floor->normal[1]));
 }
}

int findCeilDistance(int s,MthXyz *p)
{int w;
 sWallType *floor;
 for (w=level_sector[s].firstWall;w<=level_sector[s].lastWall;w++)
    {if (level_wall[w].normal[1]>=0)
	continue;
     break;
    }
 assert(w<=level_sector[s].lastWall);
 floor=level_wall+w;
 if (floor->normal[1]==F(-1))
    {assert(floor->normal[0]==0 && floor->normal[2]==0);
     return p->y-F(level_vertex[floor->v[0]].y);
    }
 {Fixed32 planeDist;
  MthXyz wallP;
  getVertex(floor->v[0],&wallP);
  planeDist=
     (f(p->x-wallP.x))*floor->normal[0]+
	(f(p->z-wallP.z))*floor->normal[2];
  return p->y-(wallP.y-MTH_Div(planeDist,floor->normal[1]));
 }
}

#define SQRTTABLESIZE 1024
#define SQRTTABLEBITS 10 /* this must be even */
#define SQRTTABLEMASK 0x3ff
#include "sqrttab.h"

int getAngle(int dx,int dy)
{while (dx>F(1) || dx<F(-1))
    {dx>>=1;
     dy>>=1;
    }
 while (dy>F(1) || dy<F(-1))
    {dx>>=1;
     dy>>=1;
    }
 return MTH_Atan(dy,dx);
}

unsigned short greyTable[33]=
{0x8000,
 0x8000|(1<<10)|(1<<5)|1,
 0x8000|(2<<10)|(2<<5)|2,
 0x8000|(3<<10)|(3<<5)|3,
 0x8000|(4<<10)|(4<<5)|4,
 0x8000|(5<<10)|(5<<5)|5,
 0x8000|(6<<10)|(6<<5)|6,
 0x8000|(7<<10)|(7<<5)|7,
 0x8000|(8<<10)|(8<<5)|8,
 0x8000|(9<<10)|(9<<5)|9,
 0x8000|(10<<10)|(10<<5)|10,
 0x8000|(11<<10)|(11<<5)|11,
 0x8000|(12<<10)|(12<<5)|12,
 0x8000|(13<<10)|(13<<5)|13,
 0x8000|(14<<10)|(14<<5)|14,
 0x8000|(15<<10)|(15<<5)|15,
 0x8000|(16<<10)|(16<<5)|16,
 0x8000|(17<<10)|(17<<5)|17,
 0x8000|(18<<10)|(18<<5)|18,
 0x8000|(19<<10)|(19<<5)|19,
 0x8000|(20<<10)|(20<<5)|20,
 0x8000|(21<<10)|(21<<5)|21,
 0x8000|(22<<10)|(22<<5)|22,
 0x8000|(23<<10)|(23<<5)|23,
 0x8000|(24<<10)|(24<<5)|24,
 0x8000|(25<<10)|(25<<5)|25,
 0x8000|(26<<10)|(26<<5)|26,
 0x8000|(27<<10)|(27<<5)|27,
 0x8000|(28<<10)|(28<<5)|28,
 0x8000|(29<<10)|(29<<5)|29,
 0x8000|(30<<10)|(30<<5)|30,
 0x8000|(31<<10)|(31<<5)|31,
 0x8000|(31<<10)|(31<<5)|31
 };

#if 0
void setGreyTableBalance(int r,int g,int b) /* 0-31 */
{int i;
 for (i=0;i<32;i++)
    {greyTable[i]=
	0x8000|
	(((b*i)/32)<<10)|
	(((g*i)/32)<<5)|
	(((r*i)/32));
    }
}
#endif

Fixed32 dist(Fixed32 dx,Fixed32 dy,Fixed32 dz)
{int d;
 d=f(dx)*f(dx)+
    f(dy)*f(dy)+
       f(dz)*f(dz);
 return fixSqrt(d,0);
}

int approxDist(int dx,int dy,int dz)
{int min;
 dx=abs(dx);
 dy=abs(dy);
 dz=abs(dz);
 if (dx<dy)
    min=dx;
 else
    min=dy;
 if (dz<min)
    min=dz;
 return dx+dy+dz-(min>>1);
}

#define NMPARTS 5

void assertFail(char *file, int line)
{Uint16  i, sw;
 MthXyz pos[NMPARTS],vel[NMPARTS];
 char *text[NMPARTS]={NULL,NULL,"Write","This","Down"};

 text[1]=file;
 displayEnable(1);
 /** BEGIN ***************************************************************/

 EZ_initSprSystem(1000,8,1000,240,RGB(0,0,0));
 initFonts(0,7);
 SCL_SetFrameInterval(1);
 SPR_SetEraseData(0x8000,0,0,319,239);
 for (i=0;i<NMPARTS;i++)
    {pos[i].x=(MTH_GetRand()&0x000fffff)+(140<<16);
     pos[i].y=(MTH_GetRand()&0x000fffff)+(100<<16);
     vel[i].x=(MTH_GetRand()&0x0007ffff)-0x3ffff;
     vel[i].y=(MTH_GetRand()&0x0007ffff)-0x3ffff;
    }

 i  = 0;
 sw = 0;
 for(;;)
    {SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0,0,0,0);

     EZ_openCommand();

     EZ_sysClip();
     EZ_localCoord(0,0);

     for (i=0;i<NMPARTS;i++)
	{if (pos[i].x<0 && vel[i].x<0) vel[i].x=-vel[i].x;
	 if (pos[i].y<0 && vel[i].y<0) vel[i].y=-vel[i].y;
	 if (pos[i].x>(300<<16) && vel[i].x>0) vel[i].x=(vel[i].x>>3)-vel[i].x;
	 if (pos[i].y>(200<<16) && vel[i].y>0) vel[i].y=(vel[i].y>>3)-vel[i].y;
	 pos[i].x+=vel[i].x;
	 pos[i].y+=vel[i].y;
	 vel[i].y+=0x1000;
	 if (i)
	    drawString(pos[i].x>>16,pos[i].y>>16,1,text[i]);
	 else
	    drawStringf(pos[i].x>>16,pos[i].y>>16,1,"%d",line);
	}

     EZ_closeCommand();
     SCL_DisplayFrame();
     pollhost();
    }
}


void message(char *message)
{Uint16  i;
 int data;
 static int sw=0;
 /** BEGIN ***************************************************************/

 SCL_SetFrameInterval(1);
 SPR_SetEraseData(0x8000,0,0,319,239);

 i  = 0;
 for(;;)
    {EZ_openCommand();

     EZ_sysClip();
     EZ_localCoord(0,0);

     drawString(10,100,0,message);

     EZ_closeCommand();
     SCL_DisplayFrame();
     pollhost();

     data = lastInputSample;
     if ((sw && !(data & PER_DGT_A)) ||
         (!sw && !(data & PER_DGT_B)))
        {sw=!sw;
         return;
        }
    }
}


char *catFixed(char *buffer,int n,int frac)
{char buff[80];
 int bit,div,i;
 int accum;
 if (n<0)
    {strcat(buffer,"-");
     n=-n;
    }
 sprintf(buff,"%d",n>>frac);
 strcat(buffer,buff);
 accum=0;
 for (bit=frac-1,div=2; bit>=0 ; bit--,div+=div)
    {if ((n>>bit)&1)
	accum+=1000000000/div;
    }
 strcat(buffer,".");
 sprintf(buff,"%d",accum);
 /* print leading zeros */
 for (i=9-strlen(buff);i>0;i--)
    strcat(buffer,"0");
 /* print rest of decimal part */
 strcat(buffer,buff);
 return buffer;
}



/* frac must be even! */
int fixSqrt(int n,int frac)
{int shiftCount=0;
 int e;
 int result;
 assert(!(frac & 1));
 assert(!(n & 0x80000000));
 while (n & (0xffffffff-SQRTTABLEMASK))
    {n=n>>2;
     shiftCount++;
    }
 e=2*shiftCount+SQRTTABLEBITS-frac-2;
 result=sqrtTable[n]>>(31-frac-e/2);
 return result;
}

Fixed32 fixMul(Fixed32 a,Fixed32 b)
{Fixed32 c;
 __asm__ volatile ("dmuls.l %1,%2\n sts mach,r11\n sts macl,%0\n xtrct r11,%0"
                   : "=r" ((Fixed32)c)
		   : "r" ((Fixed32)a), "r" ((Fixed32)b)
                   : "mach","macl","r11");
 return c;
}

Fixed32 evalHermite(Fixed32 t,Fixed32 p1,Fixed32 p2,Fixed32 d1,Fixed32 d2)
{Fixed32 t2=MTH_Mul(t,t);
 Fixed32 t3=MTH_Mul(t2,t);

 return (MTH_Mul(2*t3-3*t2+F(1),p1)+
	 MTH_Mul(-2*t3+3*t2,p2)+
	 MTH_Mul(t3-2*t2+t,d1)+
	 MTH_Mul(t3-t2,d2));
}

Fixed32 evalHermiteD(Fixed32 t,Fixed32 p1,Fixed32 p2,Fixed32 d1,Fixed32 d2)
{Fixed32 t2=MTH_Mul(t,t);

 return (MTH_Mul(6*t2-6*t,p1)+
	 MTH_Mul(-6*t2+6*t,p2)+
	 MTH_Mul(3*t2-4*t+F(1),d1)+
	 MTH_Mul(3*t2-2*t,d2));
}


/*static int low;*/
extern int end;

#define NMAREAS 2
#define STACKSIZE 8 /* must be power of 2 */
static int memStack[NMAREAS][STACKSIZE];
static int stackPos[NMAREAS],areaEnd[NMAREAS];

static int mem1Start=0x200000;
static int mem2Start=(int)&end;

void mem_init(void)
{memStack[0][0]=mem1Start;
 memStack[1][0]=mem2Start;
 stackPos[0]=0; stackPos[1]=0; /* GCC14: stackPos[2]=0 removed -- out of bounds (NMAREAS
    is 2); GCC 14's static layout made it clobber memStack[0][0] */
 areaEnd[0]=0x0300000;
 areaEnd[1]=0x6100000;
}

void mem_lock(void)
{mem1Start=memStack[0][stackPos[0]];
 mem2Start=memStack[1][stackPos[1]];
}

int mem_coreleft(int a1)
{return areaEnd[a1]-memStack[a1][stackPos[a1]];
}

void *mem_nocheck_malloc(int area,int size)
{int a1=area;
 int retAddr;
 assert(area>=0);
 assert(area<NMAREAS);
 size=(size+3)&(~3);
 do
    {/* see if we can allocate in this area */
     assert(memStack[a1][stackPos[a1]]<areaEnd[a1]);
     if (areaEnd[a1]-memStack[a1][stackPos[a1]]>size)
	{/* can allocate here */
	 retAddr=memStack[a1][stackPos[a1]];
	 stackPos[a1]=(stackPos[a1]+1)&(STACKSIZE-1);
	 memStack[a1][stackPos[a1]]=retAddr+size;
	 assert(!(retAddr & 3));
	 return (void *)retAddr;
	}
     a1++;
     if (a1>=NMAREAS)
        a1=0;
    }
 while (a1!=area);
 return NULL;
}

void *mem_malloc(int area,int size)
{void *r=mem_nocheck_malloc(area,size);
 assert(r);
 return r;
}

void mem_free(void *p)
{int a,s;
 for (a=0;a<NMAREAS;a++)
    {s=(stackPos[a]-1)&(STACKSIZE-1);
     if (memStack[a][s]!=(int)p)
	continue;
     stackPos[a]=s;
     return;
    }
 assert(0);
}

#ifdef PSYQ
void debugPrint(char *message)
{PCwrite(-10,message,strlen(message));
}
#endif

void resetDisable(void)
{volatile Uint8 *SMPC_SF=(Uint8 *)0x20100063;
 volatile Uint8 *SMPC_COM=(Uint8 *)0x2010001f;
 const Uint8 SMPC_RESDIS=0x1a;
 while ((*SMPC_SF & 0x01)==0x01) ;
 *SMPC_SF=1;
 *SMPC_COM=SMPC_RESDIS;
 while ((*SMPC_SF & 0x01)==0x01) ;
}

void resetEnable(void)
{volatile Uint8 *SMPC_SF=(Uint8 *)0x20100063;
 volatile Uint8 *SMPC_COM=(Uint8 *)0x2010001f;
 const Uint8 SMPC_RESENA=0x19;
 while ((*SMPC_SF & 0x01)==0x01) ;
 *SMPC_SF=1;
 *SMPC_COM=SMPC_RESENA;
 while ((*SMPC_SF & 0x01)==0x01) ;
}

int normalizeAngle(int angle)
{while (angle>F(180))
    angle-=F(360);
 while (angle<F(-180))
    angle+=F(360);
 return angle;
}

#ifndef NDEBUG
void _checkStack(char *file,int line)
{int c;
 __asm__ volatile ("mov r15,%0\n" /* GCC14: GNU as rejects "mov.l Rm,Rn" (register move is "mov"); cast dropped from the output lvalue */
		   : "=r" (c));
 if (c<((int)mystack)+0x100)
    assertFail(file,line);
}
#endif


/* GCC14: replaces the bare `PER_LInit(); while (!(sys=PER_GET_SYS()));` of INITMAIN.C /
   SRUINS.C.  Per the SBL manual, GoIntBack() returns WITHOUT issuing the INTBACK when the
   SMPC SF flag is busy and nothing in a bare poll re-issues it: wait vblank by vblank and
   re-arm every 4 frames (BOOTPROBE colours: cyan = SF busy at re-arm, orange = no answer). */
void *waitSystemData(Uint8 *padWork)
{PerGetSys *sys_data;
 int frames=0;
 PER_LInit(PER_KD_SYS,6,PER_SIZE_DGT,padWork,0);
 while (!(sys_data=PER_GET_SYS()))
    {/* GCC14: pace one frame per attempt -- but only the vblank flag can do that while the
	display is ON.  With TVMD's DISP bit clear the whole display interval is blanking
	(VDP2 manual ST-058-R2, "it is in the blank condition during the display interval
	when this bit is 0"), so TVSTAT's VBLANK bit stays 1 and the second loop never ends.
	MAIN.BIN reaches here with the display off, INIT.BIN having disabled it before link:
	pace on a plain count in that case. */
     if (PEEK_W(SCL_VDP2_VRAM+0x180000) & 0x8000)
	{while (!(PEEK_W(SCL_VDP2_VRAM+0x180004) & 8)) ;   /* wait for vblank */
	 while ((PEEK_W(SCL_VDP2_VRAM+0x180004) & 8)) ;
	}
     else
	{volatile int d;
	 for (d=0;d<20000;d++) ;
	}
     if ((++frames & 3)==0)
	{
#ifdef BOOTPROBE
	 {Uint16 c=((*((volatile Uint8 *)0x20100063)) & 1)? 0x7fe0 /* cyan: SF busy */ : 0x01ff /* orange */;
	  POKE_W(SCL_VDP2_VRAM+0x180000,0x8000); POKE_W(SCL_VDP2_VRAM+0x180020,0);
	  POKE_W(SCL_VDP2_VRAM+0x180110,0); POKE_W(SCL_VDP2_VRAM+0x1800ac,0);
	  POKE_W(SCL_VDP2_VRAM+0x1800ae,0); POKE_W(SCL_VDP2_VRAM,c);
	 }
#endif
	 PER_LInit(PER_KD_SYS,6,PER_SIZE_DGT,padWork,0);
	}
    }
 return sys_data;
}

int bitScanForward(unsigned int i,int start)
{start++;
 i=i>>start;
 while (i)
    {if (i & 1)
	return start;
     i=i>>1;
     start++;
    }
 return -1;
}

int bitScanBackwards(unsigned int i,int start)
{i=i<<(32-start);
 start--;
 while (i)
    {if (i & 0x80000000)
	return start;
     i=i<<1;
     start--;
    }
 return -1;
}

unsigned short buttonMasks[8]={PER_DGT_A,PER_DGT_B,PER_DGT_C,
				  PER_DGT_X,PER_DGT_Y,PER_DGT_Z,
				  PER_DGT_TL,PER_DGT_TR};
char controllerConfig[8]={0,1,2,3,4,5,6,7};


#include "rndtab.h"
static int nextRand=0;
unsigned short getNextRand(void)
{if (nextRand>=RANDTABLESIZE)
    nextRand=0;
 return randTable[nextRand++];
}


void getDateTime(int *year,int *month,int *day,int *hour,int *min)
{Uint8 *time;
 time=PER_GET_TIM();
 *year=(Uint8)((Uint16)(time[6]>>4)*1000 +
	       (Uint16)(time[6]&0x0f)*100+
	       (Uint16)(time[5]>>4)*10+
	       (Uint16)(time[5]&0x0f)-1980);
 *month=time[4]&0x0f;
 *day=(time[3]>>4)*10+(time[3]&0x0f);
 *hour=(time[2]>>4)*10+(time[2]&0x0f);
 *min=(time[1]>>4)*10+(time[1]&0x0f);
}

int findWallsSector(int wallNm)
{int s;
 for (s=0;s<level_nmSectors && wallNm>level_sector[s].lastWall;s++) ;
 assert(s<level_nmSectors);
 return s;
}


void displayEnable(int state)
{if (state)
    Scl_s_reg.tvmode|=0x8000;
 else
    Scl_s_reg.tvmode&=0x7fff;
 POKE_W(SCL_VDP2_VRAM+0x180000,Scl_s_reg.tvmode);
 if (SclProcess==0)
    SclProcess=1;
}

int findSectorHeight(int s)
{MthXyz pos;
 int a,b;
 pos.x=0; pos.y=0; pos.z=0;
 a=findCeilDistance(s,&pos);
 b=findFloorDistance(s,&pos);
 return f(b-a);
}

void delay(int frames)
{int v=vtimer+frames+1;
 while (vtimer<v);
}

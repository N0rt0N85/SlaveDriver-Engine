#ifndef __INCLUDEDutilh
#define __INCLUDEDutilh

#include <sega_mth.h>
#include <sega_spr.h>
#include "slevel.h"

typedef unsigned char BYTE;

#ifdef PAL
#define FRAMESPERSEC 50
#else
#define FRAMESPERSEC 60
#endif

#ifndef NDEBUG
extern int extraStuff;
#endif


#define MAXNMSECTORS 600
#define MAXNMWALLS 5500
#define ROUTESIZE 8
#define NMOBJECTPALLETES 5

#define DISABLE INT_ChgMsk(INT_MSK_NULL,INT_MSK_DMA0|INT_MSK_SPR)
#define ENABLE INT_ChgMsk(INT_MSK_DMA0|INT_MSK_SPR,INT_MSK_NULL)

void *qmemcpy(void *dst, const void *src, unsigned int len);
#ifndef NDEBUG
#define qmemcpy(d,s,l) \
   {assert(((int)d)>0x10000); \
    assert(((int)s)>0x10000); \
    qmemcpy(d,s,l);}
#endif

extern void _builtin_set_imask(int);
extern int _builtin_get_imask(void);
extern int sprintf(char *, const char *, ...);

extern unsigned int systemMemory;

extern char cheatsEnabled;
extern char enable_stereo;
extern char enable_music;

int bitScanBackwards(unsigned int i,int start);
int bitScanForward(unsigned int i,int start);

Fixed32 evalHermite(Fixed32 t,Fixed32 p1,Fixed32 p2,Fixed32 d1,Fixed32 d2);
Fixed32 evalHermiteD(Fixed32 t,Fixed32 p1,Fixed32 p2,Fixed32 d1,Fixed32 d2);

int approxDist(int dx,int dy,int dz);
Fixed32 dist(Fixed32 dx,Fixed32 dy,Fixed32 dz);
int getAngle(int dx,int dy);
int normalizeAngle(int angle);
int fixSqrt(int n,int frac);

MthXyz *getVertex(int vindex,MthXyz *out);
void setGreyTableBalance(int r,int g,int b); /* 0-31 */
extern unsigned short greyTable[];

int findCeilDistance(int s,MthXyz *p);
int findFloorDistance(int s,MthXyz *p);

char *catFixed(char *buffer,int fixed,int frac);
void message(char *message);

/* GCC14: declared for every build -- the definition in UTIL.C is not conditional, and an
   NDEBUG build was calling it through an implicit declaration */
void *waitSystemData(Uint8 *padWork); /* PER_KD_SYS wait with INTBACK re-arm; returns PerGetSys* */

/* GCC14: the sources call these without including the standard headers (the SN compiler
   declared them implicitly); same signatures as newlib, so a mismatch would not compile */
int abs(int j);
char *strcpy(char *dest,const char *src);

#ifndef NDEBUG

void assertFail(char *file,int line) __attribute__((noreturn)); /* GCC14 */

#define assert(x) do if (!(x)) assertFail(__FILE__,__LINE__); while(0)
#define validPtr(x) do if (!((((int)x>=0x200000 && ((int)x)<=0x2fffff)) || (((int)x)>=0x6004000 && ((int)x)<=0x60fffff))) assertFail(__FILE__,__LINE__); while(0)

#else

#define assert(x)
#define validPtr(x)

#endif

#define F(a)  ((a) << 16)
#define f(a)  ((a) >> 16)

#define MTH_Mul(a,b) fixMul(a,b)
extern inline Fixed32 fixMul(Fixed32 a,Fixed32 b)
{Fixed32 c;
 __asm__ volatile ("dmuls.l %1,%2\n"
		   "sts mach,r11\n"
		   "sts macl,%0\n"
		   "xtrct r11,%0"
                   : "=r" ((Fixed32)c)
		   : "r" ((Fixed32)a), "r" ((Fixed32)b)
                   : "mach","macl","r11");
 return c;
}

extern Fixed32 getStackPointer(void);

#ifndef NDEBUG
extern void _checkStack(char *file,int line);
#define checkStack() _checkStack(__FILE__,__LINE__)
#else
#define checkStack()
#endif


#if 0
#define MTH_Product(a,b) fixProduct(a,b)
extern inline Fixed32 fixProduct(Fixed32 *a,Fixed32 *b)
{Fixed32 c;
 __asm__ volatile ("clrmac\n"
		   "mac.l @%1+,@%2+\n"
		   "mac.l @%1+,@%2+\n"
		   "mac.l @%1+,@%2+\n"
		   "sts mach,r11\n"
		   "sts macl,%0\n"
		   "xtrct r11,%0"
                   : "=r" ((Fixed32)c), "=r" (a), "=r" (b)
		   : "1" (a), "2" (b)
                   : "mach","macl","r11");
 return c;
}
#endif



void mem_init(void);
void *mem_nocheck_malloc(int area,int size);
void *mem_malloc(int area,int size);
void mem_free(void *);
int mem_coreleft(int area);
void mem_lock(void);

#ifdef PSYQ
void debugPrint(char *message);
#define dPrint(format,args...) \
   {char buff[80]; \
    sprintf(buff,format, ## args); \
    debugPrint(buff);}
#else
#define debugPrint(x)
#define dPrint(x,args...)
#endif

#ifndef NDEBUG
extern int debugFlag;
extern int extraStuff;
#endif


#define RGB(r,g,b) (0x8000|((b)<<10)|((g)<<5)|(r))
#define CLAMP(x,min,max) ((x)<(min)?(min):(x)>(max)?(max):(x))

void resetEnable(void);
void resetDisable(void);

#define POKE_B(adr,data) (*((volatile Uint8 *)(adr))=((Uint8)(data)))
#define POKE_W(adr,data) (*((volatile Uint16 *)(adr))=((Uint16)(data)))

/* GCC14: boot/hang probe (make BOOTPROBE=1).  A checkpoint latches a colour that fills the
   screen through the back screen: display on, every background off, colour offset off, back
   screen table at VDP2 VRAM 0.  The paint has to be REPEATED from vblank-out (bootProbePaint,
   v_blank.c): the game holds the display disabled across the whole logo and title sequence,
   and SCL_ScrollShow restores its own copy of the VDP2 registers at every vblank-in, so a
   colour poked once from the main path is gone within a frame.  Painted from vblank-out it
   wins the field, and keeps winning after the main path has stopped.  The colour throbs
   while the vblank interrupt still runs; steady means the CPU itself stopped.  BOOT_PROBE
   marks INIT.BIN (slow throb) and BOOT_PROBE2 marks MAIN.BIN (fast throb), so the two
   programs can reuse one set of colours.  0 turns the probe off.  Needs sega_scl.h. */
#ifdef BOOTPROBE
extern volatile unsigned short bootStage;
void bootProbePaint(void);
#define BOOT_PROBE(c)  do { bootStage=(c); bootProbePaint(); } while (0)
#define BOOT_PROBE2(c) do { bootStage=(c)? 0x8000|(c): 0; bootProbePaint(); } while (0)
#else
#define BOOT_PROBE(c) ((void)0)
#define BOOT_PROBE2(c) ((void)0)
#endif
#define POKE(adr,data) (*((volatile Uint32 *)(adr))=((Uint32)(data)))
#define PEEK(adr) (*((volatile Uint32 *)(adr)))
#define PEEK_W(adr) (*((volatile Uint16 *)(adr)))
#define PEEK_B(adr) (*((volatile Uint8 *)(adr)))



/* from the GCC FAQ */

/*
** void Set_Hardware_Divide(int, int);
**
** Set the dividend and divisor of the hardware divide unit.
** The divider requires 37 clocks to calculate a result,
** so you want to execute some other code before retrieving the result.
*/

#define Set_Hardware_Divide(x,y) \
        ({ \
                int *div_unit = (int *)0xffffff00; \
                int dividend = x, divisor = y;     \
                __asm__ volatile ("mov.l %1,@%2; mov.l %0,@(4,%2);" \
                        : /* no output */                          \
                        : "r" (dividend), "r" (divisor), "r" (div_unit)); \
        });

#define Set_Hardware_DivideFixed(x,y) \
        ({ \
                int *div_unit = (int *)0xffffff00; \
                int dividend = x, divisor = y;     \
                __asm__ volatile ("mov.l %1,@%2; mov.l %0,@(0x10,%2); mov #0,r1; mov.l r1,@(0x14,%2);" /* GCC14: immediate move is `mov` (SN accepted `mov.l #0,r1`, same encoding 0xe100) */ \
                        : /* no output */                          \
                        : "r" (dividend), "r" (divisor), "r" (div_unit)\
		        : "r1"); \
        });

/*
** int Get_Hardware_Divide(void)
**
** Retrieves division result from the hardware divide unit.
** If less than 37 clocks have elapsed the CPU will be halted
** until the result is ready.
*/

#define Get_Hardware_Divide() \
        ({                                         \
                int *div_unit = (int *)0xffffff00; \
                int __result;                      \
                __asm__ volatile ("mov.l @(0x1c,%1),%0" \
                        : "=r" (__result)               \
                        : "r" (div_unit));              \
                __result;                               \
        });

extern unsigned short buttonMasks[];
enum {ACTION_FIRE,ACTION_JUMP,ACTION_PUSH,ACTION_FREELOC,ACTION_WEPDN,
	 ACTION_WEPUP,ACTION_STRAFE,ACTION_RUN};
extern char controllerConfig[];
#define IMASK(action) (buttonMasks[(int)controllerConfig[(int)(action)]])


unsigned short getNextRand(void);

#define BREAK asm("trapa #34")

void getDateTime(int *year,int *month,int *day,int *hour,int *min);

int findWallsSector(int wallNm);

void displayEnable(int state);

int findSectorHeight(int s);

void delay(int frames);

#endif

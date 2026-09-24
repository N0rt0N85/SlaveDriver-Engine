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
/* GCC14: CRAM banks of the object palette.  CRAM holds 2048 colours (INITMAIN.C:286), EIGHT
   banks of 256: 0..NMOBJECTPALLETES-1 for the object, lightest to darkest, NMOBJECTPALLETES
   for the muzzle flash (PIC.C:660, AI2.C:247), bank 7 for the sky (PLAX.C,
   SCL_SET_R0CAOS(7)).  At 6 the CRAM is full: there is no other bank to take. */
#define NMOBJECTPALLETES 6
/* Darkening of the darkest bank, in 5-bit units (the fog takes at most 16 from a wall).
   12 = three quarters, so a monster stays readable a little further than its surroundings.
   PIC.C:loadPalletes builds the banks, WALLS.C:drawSprites picks one; both read this. */
#define SPRITEFOGMAX 12

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
/* GCC14: the WORLD's light ramp.  greyTable stays the neutral one -- the menus, the messages,
   a thing's own glow -- and the geometry reads this one (walls, floors, water and things:
   WALLS.C, and _worldGrey in wallasm_gnu.s), so that the fog can have a COLOUR of its own. */
extern unsigned short worldGrey[];
/* GCC14: THE RAMP HAS FOUR BANDS OF 32.  A vertex's light byte carries its light in bits 0-4 and
   its GREEN level in bits 5-6 (WORLDTINT_NM levels), so `worldGrey[light]` reads the right band
   without anyone testing anything -- the renderer's assembler indexes this table with the byte as
   it stands (wallasm_gnu.s .Lrt_grey), and gouraud smooths the levels between two vertices.  The
   converter bakes the level from the distance to the pool of nukage (doom3d.cuire_vert_nukage).
   Band 0 IS the old ramp, byte for byte, so nothing that carries no green changes.
   Entries 17..31 of a band are its FLOOR: the assembler subtracts the fog from the byte before it
   indexes, so a vertex the fog swallows falls into the top of the band below, and must come out
   dark.  Band 0 keeps the bright greyTable there instead -- that is where PowerSlave's water
   reads, and PowerSlave has no green. */
#define WORLDTINT_NM 4                 /* bands: 0 = no tint, 3 = the most */
#define WORLDTINT_SH 5                 /* the band's bit position in a light byte */
/* The tint itself, k per channel over 16 (16 = that channel untouched).  It is the LEVEL's, not
   the game's: the converter reads it off the special floor the map is built around -- green for
   nukage, red for blood, orange for lava -- and the pools object hands it over at load
   (setWorldTint).  The default is the nukage green, which is what an untouched level gets. */
/* GCC14: THE TINT IS A LIGHT, NOT A FILTER.  Holding red and blue down (worldTint) gives the hue,
   but on its own it is a MULTIPLY: it shifts a bright wall by six levels and a dark one by two, so
   the green showed most exactly where the room was already bright, and a dark room near the pool
   barely changed -- the opposite of a pool that glows.  The bands therefore also LIFT green toward
   full by a share of what the wall has left, which is large when the wall is dark and nearly
   nothing when it is bright.  Out of sixteen, at the top band. */
#define WORLDTINT_GLOW 3
extern unsigned char worldTint[3];
void setWorldTint(int r,int g,int b);   /* rebuilds bands 1.. of worldGrey, and the things' bank */
/* how much RED and BLUE the LAST band keeps, in sixteenths; the green is the light itself.
   NOT a params/ value: util.h is read before gameparams.h, so a GP_ default written here would
   silently win over the file. */
#define WORLDTINT_R 13
#define WORLDTINT_B 11
/* GCC14: the colour the fog fades TO, 0..31 a channel, in gouraud units (16 = the surface's own
   colour).  (0,0,0) is the original fog -- black -- and rebuilds greyTable byte for byte.
   worldGrey[] then runs from that colour at no light to neutral at 16, so a surface does not go
   dark with distance, it goes SAND (or azure under water: PSMULTI.C picks per level).
   The light LOD is not in the way: it never dropped a fogged cell, it welds a run of them into
   one flat quad and PAINTS it (WALLS.C LODCOL_RECT/MESH), so the quad simply takes `fogFar`, the
   same colour the ramp bottoms out at.  What the LOD tests is the RAMP'S FLOOR, not zero --
   `fogFloorGour` is that floor as the wall asm stores it (bit 15 cleared for a valid vertex),
   which IS zero when the fog is black. */
void setFogColour(int r,int g,int b);
extern unsigned char fogColour[3];
extern unsigned short fogFar;           /* what a fully fogged surface is painted (absolute RGB) */
extern unsigned short fogFloorGour;     /* worldGrey[0] as a vertex word: the LOD's test value */

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

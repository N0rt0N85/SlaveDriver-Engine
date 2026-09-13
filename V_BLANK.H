#ifndef __INCLUDEDv_blankh
#define __INCLUDEDv_blankh

#include <sega_per.h>

void SetVblank(void);

#define INPUTQSIZE 16 /* dont change this */
extern volatile unsigned short inputQ[INPUTQSIZE];
extern volatile char inputQHead;
/* interrupt only changes QHead, main thread only changes
   QTail */
extern volatile char controlerPresent;
extern volatile unsigned short lastInputSample;
extern volatile unsigned short inputAccum;
extern volatile int fadeDir,fadePos,fadeEnd;
extern volatile int abcResetEnable;
extern Uint8 PadWorkArea[];
extern volatile char analogControlerPresent;
extern volatile short analogX;
extern volatile short analogY;
extern volatile char analogIndexButtonsPresent;
extern volatile short analogTR;
extern volatile short analogTL;
extern volatile char enableDoorReset;
void initInput(void);

extern volatile Uint32 vtimer, htimer;

#ifndef NDEBUG
extern int errorQ[];
extern int prQ[];
#endif

#if 0
#define PAD_U   PER_DGT_U
#define PAD_D	PER_DGT_D
#define PAD_R	PER_DGT_R
#define PAD_L	PER_DGT_L
#define PAD_A	PER_DGT_A
#define PAD_B	PER_DGT_B
#define PAD_C	PER_DGT_C
#define PAD_S	PER_DGT_S
#define PAD_X	PER_DGT_X
#define PAD_Y	PER_DGT_Y
#define PAD_Z	PER_DGT_Z
#define PAD_RB	PER_DGT_TR
#define PAD_LB	PER_DGT_TL

/*-----------keys----------------*/
#define     KJUMP           PER_DGT_B
#define     KFIRE           PER_DGT_A
#define     KRUN            PER_DGT_TR
#define     KSTRAFE         PER_DGT_TL
#define     KPUSH           PER_DGT_C
#define     KPITCH          PER_DGT_X
#define     KWEAPDOWN       PER_DGT_Y
#define     KWEAPUP         PER_DGT_Z
#define     KFLYUP          PER_DGT_A
#define     KUP             PER_DGT_U
#define     KDOWN           PER_DGT_D
#define     KLEFT           PER_DGT_L
#define     KRIGHT          PER_DGT_R
#endif


/* UBC watchpoint on the low 4 KB: ubcTrap captures the faulting PC before any
   prologue, userBreakBlam logs it into errorQ[]/prQ[] (see wallasm_gnu.s). */
extern int ubcFaultPC;
void ubcTrap(void);

#endif

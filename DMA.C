#include <machine.h>

#include <sega_spr.h>
#include <sega_xpt.h>
#include <sega_int.h>
#include <sega_per.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_cdc.h>
#include <sega_sys.h>

#define DISABLEDMA 0

#include "util.h"
#include "dma.h"
/* dma flow:


       dma is idle
       dma requests are queued and dma buffers are used
       sprite draw end interrupt -- begin dma transfer.
       dma requests are queued
       dma end interrupt -- dma buffers are freed, next dma transfer is
           started if any.
       dma requests are queued, if no dma is executing then
          dma is started.
       dma end interrupt -- next transfer is initiated
       "wait for dma end" is called.

 */

#define DMAQSIZE 16 /* must be power of 2 */

typedef struct
{int from,to,size;
} QType;
QType dmaQ[DMAQSIZE];
static volatile int qHead,qTail;


void initDMA(void)
{qHead=0; qTail=0;
}

void qDMA(void *from,void *to,int size)
{DISABLE;
 dmaQ[qHead].from=(int)from;
 dmaQ[qHead].to=(int)to;
 dmaQ[qHead].size=size;
 qHead=(qHead+1)&(DMAQSIZE-1);
 assert(qHead!=qTail);
 ENABLE;
}

#define BASE 0x25fe0000

/* GCC14: no SCU-DMA while the other CPU writes the B-bus.  Doom's title and loading screens run
   the fire on the slave, which writes the VDP2 bitmap straight, and PIC.C uploads its tiles to
   VDP1 VRAM -- the same bus.  The manual forbids the pair (SCU restriction 08: while the SCU-DMA
   drives the A- and B-buses, CPU access to them is on standby) and it hangs: the loading fire
   showed one image and stopped.  While this is set the same transfers are plain copies, which
   cost about the same to VDP VRAM (Mimas, hardware: DMA 10,3 ms against 10,0 ms for the CPU). */
int dmaNoScu;

void startNextDma(void)
{if (qTail==qHead)
    return;
 if (
#if DISABLEDMA
     1 ||
#endif
     dmaNoScu ||
     dmaQ[qTail].from<0x06000000 ||
     dmaQ[qTail].from>0x06100000)
    {qmemcpy((void *)dmaQ[qTail].to,(void *)dmaQ[qTail].from,
	     dmaQ[qTail].size);
    }
 else
    {POKE(BASE+0,dmaQ[qTail].from);
     POKE(BASE+4,dmaQ[qTail].to);
     POKE(BASE+8,dmaQ[qTail].size);
     POKE(BASE+0xc,0x101);
     POKE(BASE+0x14,7); /* start factor */
     POKE(BASE+0x10,0x100); /* enable */
     POKE(BASE+0x10,0x101); /* enable and start */
    }
 qTail=(qTail+1)&(DMAQSIZE-1);
}

void dmaMemCpy(void *from,void *to,int size)
{if (!size)
    return;
 while (dmaActive());
 if (
#if DISABLEDMA
     1 ||
#endif
     dmaNoScu ||
     from<(void *)0x06000000 || from>(void *)0x06100000 ||
     (to>=(void *)0x00200000 && to<=(void *)0x00300000))
    {qmemcpy((void *)to,from,size);
    }
 else
    {POKE(BASE+0,from);
     POKE(BASE+4,to);
     POKE(BASE+8,size);
     POKE(BASE+0xc,0x101);
     POKE(BASE+0x14,7); /* start factor */
     POKE(BASE+0x10,0x100); /* enable */
     POKE(BASE+0x10,0x101); /* enable and start */
    }
}

int dmaActive(void)
{
#if DISABLEDMA
 return 0;
#endif
 return PEEK(BASE+0x7c)&0x120;
}

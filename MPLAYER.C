/* MPLAYER.C -- local multiplayer, an engine service (GCC14).  See MPLAYER.H for the model:
   modules register their per-player globals, mpSwitch swaps them. */
#include <string.h>
#include "util.h"
#include "sprite.h"
#include "object.h"
#include "mplayer.h"
#include "print.h"
#include "pic.h"
#include "plax.h"
#include <sega_per.h>

int mpPlayers=1;
int mpArmed=1;
int mpCur=0;
Sprite *mpBody[MPMAX];
Object *mpObj[MPMAX];

/* The registry.  Sizes measured at the first build: the engine's player (SRUINS.C) and the
   Doom player (game/doom) come to a few hundred bytes; MPSTORE leaves room for another game's
   player without being a pool anyone could fill by accident -- mpRegister asserts. */
#define MPMAXBLOCKS 64
#define MPSTORE     1024
static struct {void *addr; short size,offs;} mpBlock[MPMAXBLOCKS];
static int mpNmBlocks,mpUsed;
static char mpStore[MPMAX][MPSTORE];
int mpRegisterLost;             /* blocks refused for lack of room: must read 0 (STATUSTEXT) */

/* The two limits are checked in NDEBUG too: a block past MPSTORE would be written into the
   next player's slot at every switch, silently.  Refused and counted instead. */
void mpRegister(void *addr,int size)
{int i;
 for (i=0;i<mpNmBlocks;i++)
    if (mpBlock[i].addr==addr)
       return;
 if (mpNmBlocks>=MPMAXBLOCKS || mpUsed+size>MPSTORE)
    {mpRegisterLost++;
     return;
    }
 mpBlock[mpNmBlocks].addr=addr;
 mpBlock[mpNmBlocks].size=(short)size;
 mpBlock[mpNmBlocks].offs=(short)mpUsed;
 mpUsed+=(size+3)&~3;
 mpNmBlocks++;
}

static void mpCopyOut(int k)
{int i;
 for (i=0;i<mpNmBlocks;i++)
    memcpy(mpStore[k]+mpBlock[i].offs,mpBlock[i].addr,mpBlock[i].size);
}

static void mpCopyIn(int k)
{int i;
 for (i=0;i<mpNmBlocks;i++)
    memcpy(mpBlock[i].addr,mpStore[k]+mpBlock[i].offs,mpBlock[i].size);
}

void mpSwitch(int k)
{assert(k>=0 && k<MPMAX);
 if (k==mpCur)
    return;
 mpCopyOut(mpCur);
 mpCopyIn(k);
 mpCur=k;
}

int mpBegin(int k)
{int prev=mpCur;
 mpSwitch(k);
 return prev;
}

void mpStoreAs(int k)
{assert(k>=0 && k<MPMAX);
 mpCopyOut(k);
}

/* Where player k's copy of addr lives (NULL = not registered).  Pickups, targeting and sight ask
   for the same few globals per thing, per player, per tic: the last block found is tried first,
   so the walk over the registry is paid once per new address, not per call. */
static char *mpCopyOf(int k,void *addr,int size)
{static int last;
 int i;
 char *a=(char *)addr;
 if (last<mpNmBlocks && a>=(char *)mpBlock[last].addr &&
     a+size<=(char *)mpBlock[last].addr+mpBlock[last].size)
    return mpStore[k]+mpBlock[last].offs+(a-(char *)mpBlock[last].addr);
 for (i=0;i<mpNmBlocks;i++)
    if (a>=(char *)mpBlock[i].addr && a+size<=(char *)mpBlock[i].addr+mpBlock[i].size)
       {last=i;
	return mpStore[k]+mpBlock[i].offs+(a-(char *)mpBlock[i].addr);
       }
 return NULL;
}

void mpPeek(int k,void *addr,int size,void *out)
{char *c;
 if (k==mpCur)
    {memcpy(out,addr,size);
     return;
    }
 c=mpCopyOf(k,addr,size);
 assert(c);                     /* not a registered global: there is no per-player copy */
 memcpy(out,c? c: (char *)addr,size);
}

int mpPeekInt(int k,int *addr)
{int *c;
 if (k==mpCur)
    return *addr;
 c=(int *)mpCopyOf(k,addr,sizeof(int));
 assert(c);
 return c? *c: *addr;
}

int mpIndexOfSprite(Sprite *s)
{int k;
 if (!s)
    return -1;
 for (k=0;k<mpPlayers;k++)
    if (mpBody[k]==s)
       return k;
 return -1;
}

int mpIndexOfObject(Object *o)
{int k;
 if (!o)
    return -1;
 for (k=0;k<mpPlayers;k++)
    if (mpObj[k]==o)
       return k;
 return -1;
}

/* The CRAM holds eight banks of 256 (UTIL.H): the things' fog 0..5, the muzzle flash 6, the sky 7.
   Split screen shows no sky, so player 2 takes bank 7 and the fog keeps its six steps; players 3
   and 4 each take the darkest fog bank left, the fog spreading the same range over fewer steps.
   Back to solo: six fog banks and the sky's palette again. */
unsigned char mpBank[MPMAX];

void mpSetBanks(void)
{static const unsigned char take[MPMAX]={0,7,5,4};
 static char tookSky;           /* PowerSlave loads weapon palettes into bank 7 (SEQUENCE.C):
				   only give back what was taken */
 int k,extra=0;
 for (k=0;k<MPMAX;k++)
    mpBank[k]=0;
 for (k=1;k<mpPlayers;k++)
    if (CFG_MP_TRANSLATION(k))
       extra=k;
 buildObjectFogBanks(extra>=3? 4: extra==2? 5: NMOBJECTPALLETES);
 for (k=1;k<=extra;k++)
    {buildRemappedBank(take[k],CFG_MP_TRANSLATION(k));
     mpBank[k]=take[k];
    }
 if (!extra && tookSky)
    retryPlaxPal();             /* bank 7 is the sky's again */
 tookSky=(extra>0);
}

/* The count for the next game, at the title menu: START on pad 2 cycles 1-2-3-4, and the line
   says so.  Once per menu frame, inside the open command list (MENU.C dlg_run).  In a level the
   same press adds a player (SRUINS.C mpPollStart), which also keeps mpArmed current. */
void mpMenuFrame(void)
{static char held=1;
 int down=!(lastInputSampleP[1] & PER_DGT_S);
 if (down && !held && mpPadsPresent>=2)
    mpArmed=mpArmed%MPMAX+1;
 held=(char)down;
 if (mpPadsPresent>=2 || mpArmed>1)
    drawStringf(-150,100,1,"%d PLAYER%s - START ON PAD 2",mpArmed,(mpArmed>1)?"S":"");
}

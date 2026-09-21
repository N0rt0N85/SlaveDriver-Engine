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
#define MPMAXBLOCKS 80
#define MPSTORE     1024
static struct {void *addr; short size,offs;} mpBlock[MPMAXBLOCKS];
static int mpNmBlocks,mpUsed;
static char mpStore[MPMAX][MPSTORE] __attribute__((aligned(4)));
int mpRegisterLost;             /* blocks refused for lack of room: must read 0 (STATUSTEXT) */

/* GCC14: the registry is walked twice per swap, and its TURNS cost as much as its bytes -- a swap
   moves at most 1 KB each way and still takes 0.34-0.48 ms on console.  Globals declared side by
   side land side by side, so a block is kept in address order and merged with a neighbour it
   touches exactly: the same bytes move in far fewer turns, and a run of ints stays on mpCopy's
   word path.  Offsets are rebuilt at every insertion, which is why registration must be over
   before a slot holds anything (MPLAYER.H) -- mpLive says it is not. */
static int mpLive;              /* a player's state is in the store: the registry must not move */

/* block j swallows block j+1 when it ends exactly where that one starts */
static void mpMerge(int j)
{int k;
 if (j+1>=mpNmBlocks ||
     (char *)mpBlock[j].addr+mpBlock[j].size!=(char *)mpBlock[j+1].addr ||
     mpBlock[j].size+mpBlock[j+1].size>32767)
    return;
 mpBlock[j].size+=mpBlock[j+1].size;
 mpNmBlocks--;
 for (k=j+1;k<mpNmBlocks;k++)
    mpBlock[k]=mpBlock[k+1];
}

/* The two limits are checked in NDEBUG too: a block past MPSTORE would be written into the
   next player's slot at every switch, silently.  Refused and counted instead. */
void mpRegister(void *addr,int size)
{char *a=(char *)addr;
 int i,j;
 for (i=0;i<mpNmBlocks;i++)          /* already covered, merged into a neighbour or not */
    if (a>=(char *)mpBlock[i].addr &&
	a+size<=(char *)mpBlock[i].addr+mpBlock[i].size)
       return;
 if (mpNmBlocks>=MPMAXBLOCKS || mpUsed+size>MPSTORE)
    {mpRegisterLost++;
     return;
    }
 assert(!mpLive);                    /* a block registered now would have no copy in the slots */
 if (mpLive)                         /* too late to move an offset: keep the appended shape */
    {mpBlock[mpNmBlocks].addr=addr;
     mpBlock[mpNmBlocks].size=(short)size;
     mpBlock[mpNmBlocks].offs=(short)mpUsed;
     mpUsed+=(size+3)&~3;
     mpNmBlocks++;
     return;
    }
 for (i=0;i<mpNmBlocks && (char *)mpBlock[i].addr<a;i++)
    ;
 for (j=mpNmBlocks;j>i;j--)
    mpBlock[j]=mpBlock[j-1];
 mpBlock[i].addr=addr;
 mpBlock[i].size=(short)size;
 mpNmBlocks++;
 mpMerge(i);                         /* with the block after, then with the one before */
 if (i>0)
    mpMerge(i-1);
 mpUsed=0;
 for (i=0;i<mpNmBlocks;i++)
    {mpBlock[i].offs=(short)mpUsed;
     mpUsed+=(mpBlock[i].size+3)&~3;
    }
}

/* GCC14: most blocks are one int or a few.  A memcpy call for each made a swap cost 0.34-0.48 ms
   on console (POSTTIC, where the swaps are nearly all the work), ~37 swaps an image in 4p:
   aligned words are copied four at a time, only the rest goes through memcpy. */
static __inline__ void mpCopy(void *dst,const void *src,int size)
{if (!(((int)dst|(int)src|size)&3))
    {int *d=(int *)dst;
     const int *s=(const int *)src;
     for (size>>=2;size>=4;size-=4)
	{d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=s[3];
	 d+=4; s+=4;
	}
     for (;size>0;size--)
	*d++=*s++;
    }
 else if (size==2 && !(((int)dst|(int)src)&1))
    *(short *)dst=*(const short *)src;
 else if (size==1)
    *(char *)dst=*(const char *)src;
 else
    memcpy(dst,src,size);
}

static void mpCopyOut(int k)
{int i;
 mpLive=1;                      /* from here a slot holds a player: no more registering */
 for (i=0;i<mpNmBlocks;i++)
    mpCopy(mpStore[k]+mpBlock[i].offs,mpBlock[i].addr,mpBlock[i].size);
}

static void mpCopyIn(int k)
{int i;
 for (i=0;i<mpNmBlocks;i++)
    mpCopy(mpBlock[i].addr,mpStore[k]+mpBlock[i].offs,mpBlock[i].size);
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
    {mpCopy(out,addr,size);
     return;
    }
 c=mpCopyOf(k,addr,size);
 assert(c);                     /* not a registered global: there is no per-player copy */
 mpCopy(out,c? c: (char *)addr,size);
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

/* GCC14: does player k wear a colour of its own?  A game answers with an index remap (Doom's
   translation tables) or with a colour filter (PowerSlave, whose object palette changes every
   level: CFG_MP_TINT, 0 = none, else RGB()).  A player that wears neither keeps bank 0 and its
   fog banks -- which is what player 1 does in every mode but team play. */
static int mpWearsColour(int k)
{return CFG_MP_TRANSLATION(k)!=0 || CFG_MP_TINT(k)!=0;
}

void mpSetBanks(void)
{static const unsigned char take[MPMAX-1]=CFG_MP_BANKS;
 static char tookSky;           /* PowerSlave loads weapon palettes into bank 7 (SEQUENCE.C):
				   only give back what was taken */
 int k,extra=0,tint;
 for (k=0;k<MPMAX;k++)
    mpBank[k]=0;
 /* GCC14: any player may wear a colour (player 1 too, on a team's): the banks go to those that
    do, in order, three at most */
 for (k=0;k<mpPlayers;k++)
    if (mpWearsColour(k) && extra<MPMAX-1)
       extra++;
 buildObjectFogBanks(CFG_MP_FOGBANKS(extra));
 for (k=0,extra=0;k<mpPlayers && extra<MPMAX-1;k++)
    if (mpWearsColour(k))
       {if (CFG_MP_TRANSLATION(k))
	   buildRemappedBank(take[extra],CFG_MP_TRANSLATION(k));
	else
	   {tint=CFG_MP_TINT(k);
	    buildTintedBank(take[extra],tint & 0x1f,(tint>>5) & 0x1f,(tint>>10) & 0x1f);
	   }
	mpBank[k]=take[extra++];
       }
 if (!extra && tookSky)
    retryPlaxPal();             /* bank 7 is the sky's again */
 tookSky=(extra>0 && take[0]==7);
}

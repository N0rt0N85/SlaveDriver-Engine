/* OVL.C -- overlays: code kept on the disc and run from idle memory.
 *
 * GCC14: the whole file.  A program here is one flat image (saturn.ld); every byte of it is paid
 * for as long as it runs, and a Doom level's wall tiles get only what MAIN leaves (the converter
 * reads MAIN's _end).  Code that runs only in a menu does not need to be there: it can wait on the
 * disc.  An overlay is such code, linked against MAIN's symbols (saturn_ovl.ld, tools/ovlpack.py),
 * read on demand into memory nothing else uses at that moment, relocated, run, and forgotten.
 *
 * Where it runs:
 *   OVL_SCRATCH  doorwayCache (WALLS.C slaveScratch): the renderer's scratch for ONE image, idle
 *                once wallsPipeDiscard has joined the slave -- as long as nothing renders until
 *                the overlay returns.  The overlay gets the rest of it as free memory.
 *   OVL_POOL     the top of the level pool (area 1, else 0), given back on return: at the title,
 *                where doorwayCache is the fire's and the pool holds little.  It is exactly the
 *                image and its BSS, so the free area an entry is handed there is empty: an entry
 *                that needs working memory takes it from the pool and gives it back before it
 *                returns (the pool is a stack: what ovl_run frees must be the top again).
 * The file carries the build id of the MAIN it was linked against; any other is refused, so a
 * disc that pairs the wrong files leaves the feature out instead of jumping into garbage.
 * The overlay may call anything global in MAIN.  It never tears the level down itself: it
 * returns an action and the game acts on it (the next load reuses the same memory). */
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_mth.h>
#include "util.h"
#include "spr.h"
#include "sprite.h"
#include "file.h"
#include "walls.h"
#include "v_blank.h"
#include "sruins.h"
#include "ovl.h"

int fs_getFileSize(int fd);             /* FILE.C */

typedef struct
{unsigned int magic;                    /* 'OVL1' */
 unsigned int buildId;                  /* == ovlBuildId of the MAIN it was linked against */
 unsigned int linkBase;                 /* the address it was linked at */
 unsigned int imageBytes;               /* entry table, text, rodata, data: a multiple of 4 */
 unsigned int bssBytes;
 unsigned int nRelocs;                  /* u16 word offsets after the image */
 unsigned int nEntries;
 unsigned int pad;
} OvlHeader;

#if defined(STATUSTEXT) && !defined(NDEBUG)
/* a callback left pointing into the overlay would jump into the next image's slave commands
   (an overlay bug: checked in the debug build, whose MAIN bytes do not cost the tested disc) */
#define OVL_HOOKCHECK(h,t) if ((unsigned int)(h)-(unsigned int)img<(unsigned int)room) \
			      {h=(t)0; changeMessage("OVERLAY LEFT A HOOK");}
#endif

int ovl_run(char *file,int where,int entry,int arg)
{OvlHeader h;
 int fd,mark,room=0,r;
 unsigned int rel,need,d,i;
 unsigned char *img=NULL;
 if (!fs_exists(file))
    return OVL_MISSING;
 mark=cdMark();                         /* the read takes the drive off the music */
 fd=fs_open(file);
 fs_read(fd,(char *)&h,sizeof(h));
 rel=(h.nRelocs*2+3)&~3u;
 need=h.imageBytes+(rel>h.bssBytes? rel: h.bssBytes);
 r=OVL_STALE;
 if (h.magic==0x4f564c31 && h.buildId==ovlBuildId && (unsigned int)entry<h.nEntries &&
     (unsigned int)fs_getFileSize(fd)==sizeof(h)+h.imageBytes+rel)
    {r=OVL_NOROOM;
     if (where==OVL_SCRATCH)
	img=slaveScratch(&room);
     else if ((img=mem_nocheck_malloc(1,need+15))!=NULL)
	room=need+15;
     if (img && need+15<=(unsigned int)room)    /* +15: the free area starts 16-aligned */
	{fs_read(fd,(char *)img,h.imageBytes+rel);
	 r=0;
	}
    }
 fs_close(fd);
 cdResume(mark);
 if (!r)
    {const unsigned short *reloc=(const unsigned short *)(img+h.imageBytes);
     d=(unsigned int)img-h.linkBase;
     for (i=0;i<h.nRelocs;i++)
	*(unsigned int *)(img+(reloc[i]<<2))+=d;
     for (i=0;i<h.bssBytes;i+=4)        /* over the relocation table: it is spent */
	*(unsigned int *)(img+h.imageBytes+i)=0;
     *(volatile unsigned char *)0xfffffe92=0x10;   /* purge: the code was written as data */
     *(volatile unsigned char *)0xfffffe92=0x01;
     r=((OvlEntry *)img)[entry](arg,(char *)(((int)img+need+15)&~15),(char *)img+room);
#ifdef OVL_HOOKCHECK
     OVL_HOOKCHECK(vblankUserHook,void (*)(void));
     OVL_HOOKCHECK(progressHook,void (*)(int,int));
     OVL_HOOKCHECK(linkHook,void (*)(void));
#endif
     if (where==OVL_POOL)
	mem_free(img);
    }
 return r;
}

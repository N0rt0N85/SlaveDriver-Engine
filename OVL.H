/* OVL.H -- overlays: code kept on the disc and run from idle memory (GCC14; see OVL.C). */
#ifndef __INCLUDEDovlh
#define __INCLUDEDovlh

#define OVL_SCRATCH 0           /* doorwayCache: only while nothing renders and the slave is idle */
#define OVL_POOL    1           /* the top of the level pool, given back on return (the title) */

#define OVL_MISSING (-1)        /* ovl_run: the file is not on the disc ... */
#define OVL_STALE   (-2)        /* ... it was linked against another MAIN ... */
#define OVL_NOROOM  (-3)        /* ... or it does not fit where it was asked to run */

/* An entry: arg as given to ovl_run, and the memory the overlay may use while it runs,
   [freeBase, freeEnd) -- what is left of the buffer after its image and BSS, 16-aligned. */
typedef int (*OvlEntry)(int arg,char *freeBase,char *freeEnd);

/* file = "+NAME.OVL".  -> the entry's result, or OVL_MISSING / OVL_STALE / OVL_NOROOM.  The
   overlay must return before the level goes (the next load reuses the buffer): it hands the
   game an action instead of tearing anything down itself. */
int ovl_run(char *file,int where,int entry,int arg);

extern const unsigned int ovlBuildId;   /* generated (tools/ovlpack.py --buildid) */

#endif

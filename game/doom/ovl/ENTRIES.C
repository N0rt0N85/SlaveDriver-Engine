/* ENTRIES.C -- PAUSE.OVL's entry table (GCC14): the image's first bytes (saturn_ovl.ld KEEPs
 * .rodata.ovlEntries first), called by OVL.C ovl_run through word k.  Its own file: it is the one
 * object allowed to name the title-only code (TUNER_TITLE.C, which draws), so the pause's code
 * cannot reach that by mistake (tools/ovlpack.py --roots --title). */
#include "ovl.h"
#include "doom_ovl.h"

int pause_main(int k,char *freeBase,char *freeEnd);     /* PAUSE.C */
int title_lights(int arg,char *freeBase,char *freeEnd); /* TUNER_TITLE.C */

const OvlEntry ovlEntries[]={pause_main,         /* PAUSE_MENU */
			    title_lights};      /* PAUSE_LIGHTS */

typedef char ovlEntryOrder[PAUSE_MENU==0 && PAUSE_LIGHTS==1? 1: -1];

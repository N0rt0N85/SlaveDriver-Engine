#ifndef __INCLUDEDcrashh
#define __INCLUDEDcrashh

/* CRASH.C -- the freeze and crash report, an engine service (GCC14).  The model is at the top
   of CRASH.C.  crashArmed and crashFields live in V_BLANK.C, which the VBlank-OUT interrupt
   runs in every program. */
#include "v_blank.h"

void crashInstall(void);        /* after SetVblank, and at every level: idempotent */

/* The game loop: crashBeat once per image while it runs, crashDisarm before anything that may
   legitimately hold it for seconds -- a menu, a question, the level's exits. */
#define crashBeat()   (crashFields=0,crashArmed=1)
#define crashDisarm() (crashArmed=0)

#endif

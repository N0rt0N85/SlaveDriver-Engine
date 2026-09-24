/* DOOM_PAUSE.C -- the pause's resident half: START hands over to PAUSE.OVL (GCC14).
 *
 * The pause itself (the frozen, darkened view, Doom's letters, the skull, the pages) lives in
 * PAUSE.OVL (game/doom/ovl/), read into doorwayCache when START is pressed (OVL.C): it costs
 * MAIN nothing but these few bytes and the loader.  SRUINS.C's main loop calls doom_pause through
 * CFG_PAUSE_MENU once the slave is joined; the action it returns goes to quitRequest.
 * The title's OPTIONS -> LIGHTS runs the same file's light tuner (doom_titleLights). */
#include "util.h"
#include "sprite.h"
#include "ovl.h"
#include "v_blank.h"
#include "mplayer.h"
#include "doom.h"
#include "doom_ovl.h"

char doomPauseWho;

int doom_pause(void)
{/* GCC14: only a STALE overlay is refused for good -- that one cannot get better without a new
    disc, and retrying would read the CD at every START.  A file that did not answer (a read that
    failed, a drive still seeking) is retried the next time: it used to latch the same way, and
    one unlucky read left the pause dead for the rest of the session. */
 static signed char err;
 static char msg[]="PAUSE.OVL ERROR 0"; /* 1 not on the disc, 2 another build's, 3 no room */
 int k=doomPauseWho? doomPauseWho-1: 0,r=err;
 if (!r)
    r=ovl_run(PAUSE_FILE,OVL_SCRATCH,PAUSE_MENU,k);
 if (r>=0)
    return r;
 if (r==OVL_STALE)
    err=(signed char)r;
 msg[sizeof(msg)-2]=(char)('0'-r);
 doom_setMessage(msg);                  /* Start does nothing: the message says why ... */
 while (!(lastInputSampleP[k] & PER_DGT_S))
    ;                                   /* ... once, not again every image while it is held */
 return PAUSE_RESUME;
}

/* The title's OPTIONS -> LIGHTS (INTRO.C optionMenu, CFG_LIGHT_MENU): the light tuner's screen,
   from PAUSE.OVL at the top of the level pool -- doorwayCache is the fire's at the title.  A
   refusal (no file, another build's) leaves the options menu as it is. */
void doom_titleLights(void)
{ovl_run(PAUSE_FILE,OVL_POOL,PAUSE_LIGHTS,0);
}

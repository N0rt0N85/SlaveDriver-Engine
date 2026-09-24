/* DOOM_OVL.H -- Doom's overlays (GCC14): PAUSE.OVL, its entries and the actions it returns.
 *
 * PAUSE.OVL = the sources in game/doom/ovl/, linked against MAIN (OVL.C, tools/ovlpack.py,
 * the Makefile).  Its entry table is ovlEntries[] in ovl/ENTRIES.C, in this order. */
#ifndef __INCLUDEDdoomovlh
#define __INCLUDEDdoomovlh

#define PAUSE_FILE     "+PAUSE.OVL"
#define PAUSE_MENU     0                /* entry: the pause, arg = the pausing player */
#define PAUSE_LIGHTS   1                /* entry: the title's LIGHTS screen (OVL_POOL), arg 0 */

/* what the pause returns: SRUINS.C main loop, quitRequest (CFG_PAUSE_MENU, CFG_QUIT_ACTION) */
#define PAUSE_RESUME   0
#define PAUSE_QUIT     1                /* -> runLevel returns 2: the title */
#define PAUSE_RESTART  7                /* -> SRUINS.C main case 7: CFG_START_LEVEL over again,
                                           with the arsenal it began with (doom_playerRestart) */

int  doom_pause(void);                  /* DOOM_PAUSE.C, resident */
void doom_titleLights(void);            /* ... the title's OPTIONS -> LIGHTS (CFG_LIGHT_MENU) */
extern char doomPauseWho;               /* 1 + the joined player whose START asked, 0 = pad 1 */

#endif

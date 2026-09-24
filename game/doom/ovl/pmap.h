/* PMAP.H -- the pause's MAP browser and what PAUSE.C lends it (GCC14, PAUSE.OVL).
 *
 * The browser draws the level into the OTHER half of the NBG0 bitmap and flips a single scroll
 * register, so a frame is never seen half-drawn; the menu's own half is left as it is and comes
 * back without a flash.  Both halves are one VDP2 bank each (VRAM B0 and B1), which is why the
 * chip can read one while the CPU fills the other. */
#ifndef __INCLUDEDpmaph
#define __INCLUDEDpmaph

#define PAUSE_HALF_BYTES 0x20000u       /* 256 rows of 512 bytes: B0 -> B1 */
#define PAUSE_SCYIN0     0x74           /* the NBG0 vertical scroll, poked to 0 or 256 */

/* PAUSE.C lends these (it owns the font, the veil and the pad) */
void   pause_half(int h);               /* 0 = B0 (the menu's), 1 = B1 (the map's) */
void   pause_text(int x,int y,const char *t,int big,int dim);
Uint16 pause_field(void);               /* one field: sound, music, veil; -> buttons pressed */
Uint16 pause_held(void);                /* the buttons held (active low) */

/* PMAP.C: the browser.  free..end is what the overlay loader left unused; the browser needs none
   of it today, and takes it only so a future lever (a clip box per half) has somewhere to go.
   -> 0 = back to the pause menu, 1 = resume the game (START). */
int doom_pmapBrowse(char *freeBase,char *freeEnd);

#endif

#ifndef __INCLUDEDmaph
#define __INCLUDEDmaph

void initMap(void);
void mapMpRegister(void);       /* GCC14: MPLAYER.H -- each player zooms its own map */
void drawMap(int cx,int cy,int cz,int yaw,int currentSector);
void setMark(Fixed32 x,Fixed32 y);
void mapScaleUp(void);
void mapScaleDn(void);
void revealMap(void);
int mapRevealed(void);          /* GCC14: revealMap has run on this level */

#ifdef GP_GAME_DOOM
/* GCC14: what the pause's MAP browser (PAUSE.OVL) shares with the automap -- the classification
   the level's load already computed, and the arrow's shape.  The am_map.c colour indices live
   next to their user in MAP.C; the browser writes PLAYPAL indices straight into a VDP2 bitmap. */
extern char mapColor[];                  /* MAP.C initMap: 0 none, 1 solid, 2 floor step, 3 ceiling */
extern const signed char doomArrow[7][4];
#endif

#endif

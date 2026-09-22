#ifndef __INCLUDEDmaph
#define __INCLUDEDmaph

void initMap(void);
void drawMap(int cx,int cy,int cz,int yaw,int currentSector);
void setMark(Fixed32 x,Fixed32 y);
void mapScaleUp(void);
void mapScaleDn(void);
void revealMap(void);
int mapRevealed(void);          /* GCC14: revealMap has run on this level */

#endif

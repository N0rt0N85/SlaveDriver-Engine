#ifndef __INCLUDEDwallsh
#define __INCLUDEDwallsh

#define FOCALDIST   160
#define PROJECT(x,z) ( ((x)*(FOCALDIST>>4))/(int)((z)>>4) )

void drawWalls(MthMatrix *view);
void drawWallsFinish(void);
void wallRenderSlaveMain(void);
void startSlave(void *slaveMain);
void drawSprites(MthXyz *playerPos,MthMatrix *view,int sector);
void initWallRenderer(void);
void removeLight(Sprite *s);
void addLight(Sprite *s,int r,int g,int b);
void changeLightColor(Sprite *s,int r,int g,int b);

void initWater(void);
void stepWater(void);

extern int nmPolys,nmSlavePolys;
extern Sprite *autoTarget;
extern MthXyz autoTFormedPos;

extern short plaxBBymax,plaxBBxmax,plaxBBymin,plaxBBxmin,mipBase;
extern int mipEnable; /* GCC14: runtime mip toggle, see WALLS.C */
#endif

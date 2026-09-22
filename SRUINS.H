#ifndef __INCLUDEDsruinsh
#define __INCLUDEDsruinsh

#include "sprite.h"

#ifdef __INCLUDEDaih
extern PlayerObject *player;
#endif

extern Sprite *camera;
extern int quitRequest;
extern int invisibleCounter;
extern int weaponPowerUpCounter;

void playerHurt(int hpLost);
void playerLongHurt(int lava);
void playerDYChange(Fixed32 dvel);
/* returns 1 if object accepted, 0 otherwise */
int playerGetObject(int objectType);
void playerGetCamel(int toLevel);
void playerHitTeleport(int toLevel);
void switchPlayerMotion(int state);
void setEarthQuake(int richter);
void stunPlayer(int ticks);
void changeColorOffset(int r,int g,int b,int rate);
void addColorOffset(int r,int g,int b);
void redrawBowlDots(void);
int getKeyMask(void);
void changeMessage(char *message);
void playerGotEntombedWithRamses(void);
int getEarthQuake(void);
void vdp2SheetConfig(void);     /* GCC14: NBG0 as the (hidden) VDP2 weapon sheet, no read */

extern Orient playerAngle;
#endif

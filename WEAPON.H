#ifndef __INCLUDEDweaponh
#define __INCLUDEDweaponh

extern int currentWeapon;
extern int const weaponMaxAmmo[];

void weaponForce(int fx,int fy);
void weaponSetVel(int vx,int vy);
void initWeapon(void);
void fireWeapon(void);
void weaponPlayerMove(int yavel);

void setCurrentWeapon(int i);
void weaponUp(int weaponMask);
void weaponDown(int weaponMask);

void runWeapon(int nmFrames,int invisible,int boost);

void weaponChangeAmmo(int weapon,int deltaAmmo);
void switchWeapons(int on);
void weaponMpRegister(void);     /* GCC14: MPLAYER.H */
#endif

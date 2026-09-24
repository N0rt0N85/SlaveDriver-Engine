#ifndef __INCLUDEDprofileh
#define __INCLUDEDprofileh

#ifndef NPROFILE

void initProfiler(void);
void popProfile(void);
void pushProfile(char *id);
void dumpProfileData(void);
void drawProfileData(int x,int y);
extern int profileShow;
char *profilePath(char *buf,int size);  /* GCC14: "root>Walls>...", for CRASH.C */
unsigned short getTimer(void);          /* GCC14: the FRT at phi/32 -- the 352 clock probe */

#else

#define initProfiler()
#define popProfile()
#define pushProfile(x)
#define dumpProfileData()
#define drawProfileData(x,y)
#define profilePath(buf,size) ((buf)[0]=0,(buf))

#endif

#endif

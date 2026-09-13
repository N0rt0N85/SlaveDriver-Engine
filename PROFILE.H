#ifndef __INCLUDEDprofileh
#define __INCLUDEDprofileh

#ifndef NPROFILE

void initProfiler(void);
void popProfile(void);
void pushProfile(char *id);
void dumpProfileData(void);
void drawProfileData(int x,int y);
extern int profileShow;

#else

#define initProfiler()
#define popProfile()
#define pushProfile(x)
#define dumpProfileData()
#define drawProfileData(x,y)

#endif

#endif

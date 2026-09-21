#ifndef __INCLUDEDfileh
#define __INCLUDEDfileh

void fs_startProgress(int swirly);
void fs_addToProgress(char *filename);
void fs_closeProgress(void);

void fs_init(void);
int fs_open(char *filename);
int fs_exists(char *filename);  /* GCC14: 1 = on the disc (fs_open asserts it is) */
void fs_read(int fd, char *buf,int n);
void fs_close(int fd);
void fs_getStatus(int *stat,int *ndata);
void fs_execOne(void);

void playWholeCD(void);
void playCDTrack(int track,int repeat);
void stopCD(void);
int cdMark(void);               /* GCC14: resume the music after a data read */
int cdResume(int mark);
void playCDTrackFrom(int track,int mark);
void cdTic(void);

int getTrackStartFAD(int track);
int getCurrentFAD(void);
int getCurrentStatus(void);

void link(char *filename);
extern void (*linkHook)(void);   /* GCC14: runs first in link() */

#endif

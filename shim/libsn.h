/* libsn.h stub: SN Systems PC file server API is only referenced under #ifdef PSYQ */
#ifndef LIBSN_H
#define LIBSN_H
int PCinit(void); int PCopen(char*,int,int); int PCread(int,char*,int); int PCwrite(int,char*,int); int PClseek(int,int,int); int PCclose(int);
void pollhost(void);	/* SN host poll; UTIL.C assertFail calls it in its wait loop */
#endif

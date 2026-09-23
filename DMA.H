#ifndef __INCLUDEDdmah
#define __INCLUDEDdmah

extern int dmaNoScu;                   /* GCC14: 1 = plain copies, the B-bus is shared */
int dmaActive(void);
void startNextDma(void);
void qDMA(void *from,void *to,int size);
void initDMA(void);
void dmaMemCpy(void *from,void *to,int size);

#endif

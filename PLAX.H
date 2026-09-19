void initPlax(int fd);
void movePlax(Fixed32 yaw,Fixed32 pitch);
void enablePlax(int setting);
void retryPlaxPal(void);
void plaxOff(void);
const unsigned short *plaxPalette(void);  /* GCC14: as loaded (MPSKY.C) */
void setPlaxFade(int f);  /* 0..16 scale applied to the sky palette; 16 = original */

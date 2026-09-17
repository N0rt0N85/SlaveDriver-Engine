void initPlax(int fd);
void movePlax(Fixed32 yaw,Fixed32 pitch);
void enablePlax(int setting);
void retryPlaxPal(void);
void plaxOff(void);
void setPlaxFade(int f);  /* 0..16 : echelle appliquee a la palette du ciel ; 16 = d'origine */
int getPlaxFade(void);

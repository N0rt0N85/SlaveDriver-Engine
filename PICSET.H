#ifndef __INCLUDEDpicseth
#define __INCLUDEDpicseth

unsigned char *loadPic(int fd,int *width,int *height);
int loadPicSet(int fd,unsigned short **palletes,
	       unsigned int **datas,int maxNmPics);
void skipPicSet(int fd);

/* GCC14: le meme picset lu en flux, sans le garder en memoire (voir PICSET.C) */
int picSetBegin(int fd);
int picSetNext(int fd,int *left,int *w,int *h,
	       unsigned char *rle,int rleMax,unsigned short *pal);

#endif

#include <machine.h>

#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_int.h>
#include <sega_mth.h>
#include <sega_sys.h>
#include <sega_dbg.h>
#include <sega_per.h>
#include <sega_dma.h>
#include <string.h>

#include "util.h"
#include "file.h"
#include "picset.h"


unsigned char *loadPic(int fd,int *width,int *height)
{short w,h;
 short flags;
 unsigned char *data;
 fs_read(fd,(char *)&w,2);
 fs_read(fd,(char *)&h,2);
 fs_read(fd,(char *)&flags,2);
 *width=w;
 *height=h;
 data=mem_malloc(0,*width**height);
 assert(data);
 fs_read(fd,data,*width**height);
 return data;
}

void skipPicSet(int fd)
{int size;
 char *data;
 fs_read(fd,(char *)&size,4);
 data=mem_malloc(0,size);
 fs_read(fd,data,size);
 mem_free(data);
}

int loadPicSet(int fd,unsigned short **palletes,
	       unsigned int **datas,int maxNmPics)
{char *data;
 char *d;
 char *lastPallete;
 int chunkSize;
 int size;
 int pic,w,h;
 fs_read(fd,(char *)&size,4);
 data=mem_malloc(0,size);
 fs_read(fd,data,size);
 pic=0;
 d=data;
 lastPallete=NULL;
 while (d<data+size)
    {assert(!(((int)d) & 3));
     datas[pic]=((unsigned int *)d)+1;
     chunkSize=*(int *)d;
     w=*(((int *)d)+1);
     h=*(((int *)d)+2);
     d+=chunkSize+12;
     if ((*(int *)d) & 1)
	{d+=4;
	 palletes[pic]=(unsigned short *)d;
	 lastPallete=d;
	 d+=256*2;
	}
     else
	{d+=4;
	 assert(lastPallete);
	 palletes[pic]=(unsigned short *)lastPallete;
	}
     pic++;
     assert(pic<maxNmPics);
    }
 return pic;
}

/* GCC14: le picset des menus EN FLUX ---------------------------------------------------------
   loadPicSet ci-dessus garde les 57 Ko du bloc en aire 0, verrouilles pour toute la partie
   (MENU.C dlg_init, puis mem_lock).  Sur PowerSlave ces 57 Ko sont le sixieme de ce qui reste
   a un niveau lourd : KARNAK n'a que 11 Ko de libre une fois charge, et le split n'a donc ni
   de quoi decouper l'arme ni de quoi se payer ses propres jeux de parcours (WALLS.C
   wallsSplitAlloc).  On ne garde plus que la TAILLE de chaque image, plus la premiere -- celle
   dont dlg_addBase refait le fond de chaque dialogue.  Les autres sont relues du disque au
   moment ou l'inventaire s'ouvre, une a la fois, dans un tampon d'emprunt.

   Le systeme de fichiers ne sait pas se deplacer (FILE.H n'a pas de fs_seek), donc tout se lit
   SEQUENTIELLEMENT, du debut, exactement comme loadPicSet le faisait deja.

   Disposition d'une image, telle que loadPicSet la parcourt :
      [taille:4][largeur:4][hauteur:4][taille octets de RLE][fanion:4][palette:512 si fanion&1]
   Une image sans palette propre reprend la derniere lue. */

/* ouvre la passe : lit la taille du bloc et rend le nombre d'octets qu'il contient */
int picSetBegin(int fd)
{int size;
 fs_read(fd,(char *)&size,4);
 return size;
}

/* lit l'image suivante.  `rle` recoit ses octets compresses (rleMax doit tenir la plus grosse :
   7560 sur le disque du commerce), `pal` la palette en vigueur -- inchangee si l'image reprend
   celle d'avant.  Rend la taille compressee, ou -1 si le bloc est fini. */
int picSetNext(int fd,int *left,int *w,int *h,
	       unsigned char *rle,int rleMax,unsigned short *pal)
{int chunk,flag;
 if (*left<=0)
    return -1;
 fs_read(fd,(char *)&chunk,4);
 fs_read(fd,(char *)w,4);
 fs_read(fd,(char *)h,4);
 assert(chunk>0 && chunk<=rleMax);
 fs_read(fd,(char *)rle,chunk);
 fs_read(fd,(char *)&flag,4);
 *left-=chunk+16;
 if (flag & 1)
    {fs_read(fd,(char *)pal,512);
     *left-=512;
    }
 return chunk;
}

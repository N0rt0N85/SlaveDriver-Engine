#include <machine.h>
#include <libsn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_int.h>
#include <sega_mth.h>
#include <sega_sys.h>
#include <sega_dbg.h>
#include <sega_per.h>
#include <sega_cdc.h>
#include <sega_gfs.h>
#include <sega_snd.h>

#include "v_blank.h"
#include "file.h"
#include "util.h"
#include "spr.h"
#include "sprite.h"
#include "pic.h"
#include "plax.h"
#include "mpsky.h"

#define PLAXPERSCREEN 128

/* GCC14: the sky's display and no-transparency bits in BGON, for whichever screen carries it
   (SPRITE.H CFG_SKY_SCREEN): N0ON/N0TPON or R0ON/R0TPON. */
#if CFG_SKY_SCREEN==SCL_NBG0
#define SKY_ON     0x0001
#define SKY_TP     0x0100
#else
#define SKY_ON     0x0010
#define SKY_TP     0x1000
#endif

/* The block on the disc is 512 x 256 either way and goes into VRAM A1 either way -- 128 KB, the
   space the sky has always had.  What changes is what its axes MEAN.  For RBG0 a row is an
   azimuth and a column a height, because the 90-degree matrix below reads it transposed.  For
   NBG0 there is no matrix, so the converter writes it screen-wise: a column is an azimuth (512
   dots = two copies of the 256-texel quarter, and the bitmap wraps) and a row is a height (256
   = two copies of a 128-texel sky, so the vertical wrap is seamless too -- and repeating every
   128 is what Doom itself does, R_DrawColumn masking the line by 127).
   It cannot go to B0-B1, free though they are: Doom's loading screen is THERE (DOOM_TITLE.C
   VRAM_B0/VRAM_B1) and it is on screen while this runs. */
#define SKY_W        512                /* dots across the bitmap                              */
#define SKY_H        256                /* dots down                                           */
#define SKY_MIDROW   112                /* the row the view's centre line reads at pitch 0     */
void movePlax(Fixed32 yaw,Fixed32 pitch)
{int x,y;
 extern SclRotreg *SclRotregBuff;
 x=-(yaw*PLAXPERSCREEN)/F(45);
 while (x<0) x+=256;
 while (x>256) x-=256;

 y=-(pitch*PLAXPERSCREEN)/F(45)-100;
/* y=-(pitch*PLAXPERSCREEN)/F(45)-40; */

#if 0
 /* poke x offsets */
 POKE(SCL_VDP2_VRAM_A0+0x500,F(x));
 POKE_W(SCL_VDP2_VRAM_A0+0x500+0x34,160+x);

 /* poke y offsets */
 POKE(SCL_VDP2_VRAM_A0+0x500+4,F(y));
 POKE_W(SCL_VDP2_VRAM_A0+0x500+0x36,120+y);
#endif

/* GCC14: the sky's screen-column -> texture mapping is anchored on the picture's middle column,
   which the 352 centring moved by viewOrgOff (SPR.C).  Its WINDOW already follows it
   (SRUINS.C SCL_SetWindow), so without this the sky sits 16 dots out of register with the walls.
   It goes on screenst, not on viewp: under this 90-degree matrix viewp.x feeds the texture's Y
   as well, and the sky would slide vertically too. */
#if CFG_SKY_SCREEN==SCL_NBG0
 /* GCC14: a scroll, not a matrix.  Screen dot (c,r) reads bitmap dot (x+c, y+r), so putting
    SKY_MIDROW on the view's centre line is one subtraction.  `y` still carries the pitch term
    the RBG0 path used, less its own -100 offset. */
 SCL_Open(SCL_NBG0);
 SCL_MoveTo((x-viewOrgOff)<<16,(SKY_MIDROW-CFG_YCENTER+y+100)<<16,0);
 SCL_Close();
#else
 SclRotregBuff->screenst.x=F(x-viewOrgOff);
 SclRotregBuff->viewp.x=160+x;
 SclRotregBuff->screenst.y=F(y);
 SclRotregBuff->viewp.y=20;
#endif

 if (SclProcess==0)
    SclProcess=1;

}

void enablePlax(int setting)
{if (mpSkyOn)                  /* GCC14: split screen's sky (MPSKY.C) */
    {mpSkyShow(setting);
     return;
    }
 if (setting)
    Scl_s_reg.dispenbl|=SKY_ON;
 else
    Scl_s_reg.dispenbl&=~SKY_ON;
}

static unsigned short plaxPal[256];
static int plaxFade=16;   /* 16 = palette as stored on the disc */


/* GCC14: DARKEN THE SKY.  The plax is an 8 bpp bitmap on RBG0 with its OWN CRAM bank (bank 7,
   SCL_SET_R0CAOS(7) below): rewriting those 256 entries touches no other plane, not the VDP1,
   nor colour offset A (shared by damage and the death screen).  Cost: 256 CRAM writes when
   the setting changes, none per frame.  A true depth fog would turn the whole sky black; it
   is only lowered enough to sit in the same range as the fogged scenery. */
void setPlaxFade(int f)
{int i;
 if (f<0) f=0;
 if (f>16) f=16;
 plaxFade=f;
 for (i=0;i<256;i++)
    {unsigned int c=plaxPal[i];
     unsigned int r=((c&31)*f)>>4,
		  g=(((c>>5)&31)*f)>>4,
		  b=(((c>>10)&31)*f)>>4;
     POKE_W(SCL_COLRAM_ADDR+((256*7+i)<<1),(c&0x8000)|(b<<10)|(g<<5)|r);
    }
}

void retryPlaxPal(void)
{setPlaxFade(plaxFade);
 /* SCL_SetColRam(0,256*7,256,plaxPal); */
}

/* GCC14: the sky's palette as loaded, for the split-screen sky (MPSKY.C) */
const unsigned short *plaxPalette(void)
{return plaxPal;
}

void plaxOff(void)
{Scl_s_reg.dispenbl&=~SKY_ON;
 if (SclProcess==0)
    SclProcess=1;
/* SclConfig scfg;
   SCL_InitConfigTb(&scfg);
   scfg.dispenbl=OFF;
   SCL_SetConfig(SCL_RBG0, &scfg); */
}

void initPlax(int fd)
{SclConfig scfg;
 int x,i;
 fs_read(fd,(char *)(plaxPal),256*2);

 for (i=0;i<256;i++)
    POKE_W(SCL_COLRAM_ADDR+((256*7+i)<<1),plaxPal[i]);
 /* SCL_SetColRam(0,256*7,256,plaxPal); */

 fs_read(fd,(char *)&x,4);
 assert(x==SKY_W);
 fs_read(fd,(char *)&x,4);
 assert(x==SKY_H);
 fs_read(fd,(char *)SCL_VDP2_VRAM_A1,SKY_W*SKY_H);

#if CFG_SKY_SCREEN==SCL_NBG0
 SCL_InitConfigTb(&scfg);
 scfg.dispenbl=ON;
 scfg.bmpsize=SCL_BMP_SIZE_512X256;
 scfg.coltype=SCL_COL_TYPE_256;
 scfg.datatype=SCL_BITMAP;
 scfg.mapover=SCL_OVER_0;
 scfg.plate_addr[0]=128*1024;                  /* VRAM A1 */
 scfg.patnamecontrl=0;
 SCL_SetConfig(SCL_NBG0, &scfg);

 SCL_SET_N0CAOS(7);
 SCL_SetPriority(SCL_NBG0,1);   /* behind everything: setVDP2 puts it at 6 for the gun sheet */
 Scl_s_reg.dispenbl|=SKY_TP;    /* turn off transparency for plax */
 if (SclProcess==0)
    SclProcess=1;

 fs_read(fd,(char *)SCL_VDP2_VRAM_A0,320*4);   /* the K table: read past it, RBG0 is free now */
#else

 SCL_InitRotateTable(SCL_VDP2_VRAM_A0+0x500,1,SCL_RBG0,SCL_NON);

 SCL_InitConfigTb(&scfg);
 scfg.dispenbl=ON;
 scfg.bmpsize=SCL_BMP_SIZE_512X256;
 scfg.coltype=SCL_COL_TYPE_256;
 scfg.datatype=SCL_BITMAP;
 scfg.mapover=SCL_OVER_0;
 scfg.plate_addr[0]=128*1024;
 scfg.patnamecontrl=0;
 SCL_SetConfig(SCL_RBG0, &scfg);

 SCL_SET_R0CAOS(7);
 Scl_r_reg.k_contrl=0x1;
 Scl_r_reg.k_offset=0;
 Scl_s_reg.dispenbl|=SKY_TP; /* turn off transparency for plax */
 if (SclProcess==0)
    SclProcess=1;

 fs_read(fd,(char *)SCL_VDP2_VRAM_A0,320*4);
#endif

#if 0
 {int x,d;
  double f;
  for (x=0;x<320;x++)
     {f=atan((x-160.0)/160.0)*((double)PLAXPERSCREEN)/0.785398;
      if (abs(x-160)>1)
	 {f=f/(x-160);
	  d=f*65536.0;
	  d=d&0x007fffff;
	 }
      else
	 d=66754/*83200*/ /*0x400*/;
      POKE(SCL_VDP2_VRAM_A0+x*4,d);
     }
 }
#endif

#if CFG_SKY_SCREEN!=SCL_NBG0
 {extern SclRotreg *SclRotregBuff;
  SclRotregBuff->k_tab=0;
  SclRotregBuff->k_delta.y=1<<16;
  SclRotregBuff->k_delta.x=0; /* are these backwards?? */
  SclRotregBuff->matrix_a=0;
  SclRotregBuff->matrix_b=F(-1);
  SclRotregBuff->matrix_c=0;
  SclRotregBuff->matrix_d=F(1);
  SclRotregBuff->matrix_e=0;
  SclRotregBuff->matrix_f=0;
 }
#endif
 if (SclProcess==0)
    SclProcess=1;

#if 0

  POKE(SCL_VDP2_VRAM_A0+0x500+0x54,0); /* coeff start address */
  POKE(SCL_VDP2_VRAM_A0+0x500+0x58,0 /*(160)<<16*/); /* line increment */
  POKE(SCL_VDP2_VRAM_A0+0x500+0x5c,(1)<<16); /* dot increment */


  {static int rotMat[6]={0,-1,0,
			    1, 0,0};
   for (i=0;i<6;i++)
      POKE(SCL_VDP2_VRAM_A0+0x500+0x1c+i*4,F(rotMat[i]));
  }
#endif

}

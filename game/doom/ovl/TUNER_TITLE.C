/* TUNER_TITLE.C -- the title's OPTIONS -> LIGHTS screen (PAUSE.OVL, entry PAUSE_LIGHTS).
 *
 * GCC14: the whole file, moved out of MAIN (it was DOOM_LIGHTS.C doom_lightMenu).  It runs at the
 * title only, from the top of the level pool (OVL_POOL: doorwayCache is the fire's there), through
 * DOOM_PAUSE.C doom_titleLights.  Unlike the pause, it draws with the VDP1 and presents its
 * images, as every title screen does: so it is the one object of the overlay the deny list does
 * not cover, and nothing but the entry table may reach it (Makefile OVL_TITLE, ovlpack.py). */
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_mth.h>
#include <sega_per.h>
#include "util.h"
#include "spr.h"
#include "sprite.h"
#include "print.h"
#include "sound.h"
#include "v_blank.h"
#include "ovl.h"
#include "tuner.h"

/* The same loop as the multiplayer screen (MPRULES.C mpGameMenu): every line is a value the
   d-pad turns, so dlg_run -- which answers a press -- is no use here. */
int title_lights(int arg,char *freeBase,char *freeEnd)
{static Fixed32 wave;
 int data,last,edge,r,d,y,color;
 char text[40];
 (void)arg;
 (void)freeBase;
 (void)freeEnd;
 fadeEnd=-150;                  /* the title picture dims behind the lines, as for a submenu */
 fadeDir=-5;
 SCL_SetFrameInterval(0xfffe);
 data=lastInputSample;
 while (1)
    {EZ_openCommand();
     EZ_sysClip();
     EZ_localCoord(320/2,240/2);
     drawString(-getStringWidth(2,(unsigned char *)"LIGHTS")/2,-110,2,(unsigned char *)"LIGHTS");
     color=MTH_Sin(wave)>>12;
     wave+=F(8);
     if (wave>F(180))
	wave-=F(360);
     for (r=0,y=-80;r<LR_NM;r++)
	{if (!lightRowShown(r))
	    continue;
	 lightLine(r,text);
	 if (r==LR_RESET)
	    y+=6;
	 if (r==lightRow)
	    drawStringGouro(-getStringWidth(2,(unsigned char *)text)/2,y,2,
			    greyTable[16+color],greyTable[16-color],(unsigned char *)text);
	 else
	    drawString(-getStringWidth(2,(unsigned char *)text)/2,y,2,(unsigned char *)text);
	 y+=18;
	}
     SPR_WaitDrawEnd();
     EZ_closeCommand();
     sound_nextFrame();
     SCL_DisplayFrame();

     last=data;
     data=lastInputSample;
     edge=(last^data)&~data;    /* pad bits are active low: this frame's presses */
     if (edge & (PER_DGT_U|PER_DGT_D))
	{lightMove((edge & PER_DGT_U)? -1: 1);
	 playSound(0,0);
	}
     d=0;
     if (edge & PER_DGT_L)
	d=-1;
     if (edge & PER_DGT_R)
	d=1;
     if ((edge & PER_DGT_B) || ((edge & (PER_DGT_A|PER_DGT_C|PER_DGT_S)) && lightRow==LR_BACK))
	break;
     if (edge & (PER_DGT_A|PER_DGT_C))
	d=1;
     if (!d || lightRow==LR_BACK)
	continue;
     playSound(0,1);
     lightAdjust(lightRow,d);
    }
 playSound(0,1);
 fadeEnd=0;
 fadeDir=5;
 return 0;
}

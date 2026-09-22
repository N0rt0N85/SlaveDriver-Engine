/* TUNER.C -- the light tuner's rows (PAUSE.OVL).
 *
 * GCC14: the whole file, moved out of MAIN (it was DOOM_LIGHTS.C's): the tuner runs only in a
 * menu, so it waits on the disc.  It shows one effect at a time, or ALL, which edits the player's
 * settings instead (WALLS.C lightOn, lightPeakPct, lightRadPct, lightAddR/G/B).  Two ways in: the
 * pause's OPTIONS -> LIGHTS (PAUSE.C) and the title's OPTIONS -> LIGHTS (TUNER_TITLE.C).  What
 * they change is MAIN's (DOOM_LIGHTS.C doomLightFx): it lasts until the machine is switched off. */
#include <string.h>
#include <stdio.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_mth.h>
#include "util.h"
#include "spr.h"
#include "sprite.h"
#include "object.h"
#include "walls.h"
#include "doom.h"
#include "doom_lights.h"
#include "tuner.h"

/* what params/doom.cfg built: RESET (only here, so MAIN does not keep a second copy) */
static const DoomLightFx lightDef[DLF_NM]=
   {{GP_LIGHT_IMP,0},
    {GP_LIGHT_CACO,0},
    {GP_LIGHT_BARON,0},
    {GP_LIGHT_ROCKET,0},
    {GP_LIGHT_PLASMA,0},
    {GP_LIGHT_EXPLODE,GP_LIGHT_EXPLODE_TICS},
    {GP_LIGHT_MUZZLE_PLAYER,GP_LIGHT_MUZZLE_TICS},
    {GP_LIGHT_MUZZLE_MONSTER,GP_LIGHT_MUZZLE_TICS}};

static const char *const lightFxName[DLF_NM+1]=
   {"ALL","IMP FIREBALL","CACODEMON SHOT","BARON SHOT","ROCKET","PLASMA BOLT","EXPLOSION",
    "MY MUZZLE FLASH","MONSTER MUZZLE"};
static int lightSel=0;                  /* 0 = ALL (the player's settings), else the effect + 1 */
int lightRow=LR_EFFECT;                 /* defined, not tentative: a COMMON would look for MAIN's */

static int lightClamp(int v,int lo,int hi)
{return (v<lo)? lo: (v>hi)? hi: v;
}

/* the line's own effect, or NULL on ALL */
static DoomLightFx *lightFxOf(void)
{return lightSel? &doomLightFx[lightSel-1]: (DoomLightFx *)0;
}

int lightRowShown(int r)
{if (r==LR_TICS)
    {DoomLightFx *f=lightFxOf();
     return f && f->tics>0;             /* a missile's light lasts as long as the missile */
    }
 return 1;
}

/* bigFont (the title) has capitals, digits, spaces and '-' only: no '%' anywhere here */
void lightLine(int r,char *text)
{DoomLightFx *f=lightFxOf();
 switch (r)
    {case LR_EFFECT: sprintf(text,"EFFECT  %s",lightFxName[lightSel]); break;
     case LR_ON:     sprintf(text,"LIGHTS  %s",lightOn? "ON": "OFF"); break;
     case LR_INT:
	if (f) sprintf(text,"INTENSITY  %d",f->peak);
	else   sprintf(text,"INTENSITY  %d PCT",lightPeakPct);
	break;
     case LR_RAD:
	if (f) sprintf(text,"RADIUS  %d",f->radius);      /* world units, half the diameter */
	else   sprintf(text,"RADIUS  %d PCT",lightRadPct);
	break;
     case LR_RED:    sprintf(text,"RED  %d",f? f->r: lightAddR); break;
     case LR_GREEN:  sprintf(text,"GREEN  %d",f? f->g: lightAddG); break;
     case LR_BLUE:   sprintf(text,"BLUE  %d",f? f->b: lightAddB); break;
     case LR_TICS:   sprintf(text,"DURATION  %d TICS",f? f->tics: 0); break;
     case LR_RESET:  strcpy(text,"RESET"); break;
     default:        strcpy(text,"BACK"); break;
    }
}

/* d = -1 / +1 on the selected line.  RESET puts back what params/doom.cfg built. */
void lightAdjust(int r,int d)
{DoomLightFx *f=lightFxOf();
 int k;
 switch (r)
    {case LR_EFFECT: lightSel=(lightSel+d+DLF_NM+1)%(DLF_NM+1); break;
     case LR_ON:     lightOn=!lightOn; break;
     case LR_INT:
	if (f) f->peak=lightClamp(f->peak+d,0,31);
	else   lightPeakPct=lightClamp(lightPeakPct+d*10,0,400);
	break;
     case LR_RAD:
	if (f) f->radius=lightClamp(f->radius+d*16,16,1024);
	else   lightRadPct=lightClamp(lightRadPct+d*10,10,400);
	break;
     case LR_RED:
     case LR_GREEN:
     case LR_BLUE:
	k=r-LR_RED;
	if (f)
	   {short *c=(k==0)? &f->r: (k==1)? &f->g: &f->b;
	    *c=lightClamp(*c+d,0,16);
	   }
	else
	   {int *c=(k==0)? &lightAddR: (k==1)? &lightAddG: &lightAddB;
	    *c=lightClamp(*c+d,-16,16);
	   }
	break;
     case LR_TICS:
	if (f) f->tics=lightClamp(f->tics+d,1,35);
	break;
     case LR_RESET:
	if (f)
	   *f=lightDef[lightSel-1];
	else
	   {lightPeakPct=lightRadPct=100;
	    lightAddR=lightAddG=lightAddB=0;
	    lightOn=1;
	    for (k=0;k<DLF_NM;k++)
	       doomLightFx[k]=lightDef[k];
	   }
	break;
    }
}

void lightMove(int d)
{do
    lightRow=(lightRow+d+LR_NM)%LR_NM;
 while (!lightRowShown(lightRow));
}

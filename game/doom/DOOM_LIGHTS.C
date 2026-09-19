/* DOOM_LIGHTS.C -- the dynamic lights of the Doom port: what each effect asks for, and the tuner
 * that edits it.
 *
 * params/doom.cfg gives a build its starting values (GP_LIGHT_*); doomLightFx holds them from
 * then on, so a light can be judged and changed with the level running.  An effect asks for a
 * tint (k 0..16 a channel), a radius and an intensity; WALLS.C then applies the player's own
 * settings -- the switch, the two scales, the tint offsets -- and places the light.
 *
 * The tuner shows one effect at a time, or ALL, which edits those settings instead.  Two ways in,
 * one state: the title's OPTIONS -> LIGHTS (doom_lightMenu, its own screen), and in the level
 * (L+R+RIGHT, or C+X+Y), where the tuner eats the d-pad -- the buttons stay the player's, so the
 * gun still fires under it.  Nothing is saved: the numbers on screen are what params/doom.cfg
 * should be given once they are right. */
#include <string.h>
#include <stdio.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_mth.h>
#include <sega_per.h>
#include "util.h"
#include "spr.h"
#include "sprite.h"
#include "object.h"
#include "walls.h"
#include "mplayer.h"
#include "print.h"
#include "sound.h"
#include "v_blank.h"
#include "doom.h"
#include "doom_lights.h"

#define DOOM_FONT_MSG 1                 /* STCFN, as DOOM_HUD.C loads it (FONT_MSG) */

DoomLightFx doomLightFx[DLF_NM]=
   {{GP_LIGHT_IMP,0},              /* a missile's light lives as long as the missile does */
    {GP_LIGHT_CACO,0},
    {GP_LIGHT_BARON,0},
    {GP_LIGHT_ROCKET,0},
    {GP_LIGHT_PLASMA,0},
    {GP_LIGHT_EXPLODE,GP_LIGHT_EXPLODE_TICS},
    {GP_LIGHT_MUZZLE_PLAYER,GP_LIGHT_MUZZLE_TICS},
    {GP_LIGHT_MUZZLE_MONSTER,GP_LIGHT_MUZZLE_TICS}};

static const DoomLightFx doomLightDef[DLF_NM]=
   {{GP_LIGHT_IMP,0},
    {GP_LIGHT_CACO,0},
    {GP_LIGHT_BARON,0},
    {GP_LIGHT_ROCKET,0},
    {GP_LIGHT_PLASMA,0},
    {GP_LIGHT_EXPLODE,GP_LIGHT_EXPLODE_TICS},
    {GP_LIGHT_MUZZLE_PLAYER,GP_LIGHT_MUZZLE_TICS},
    {GP_LIGHT_MUZZLE_MONSTER,GP_LIGHT_MUZZLE_TICS}};

/* --- what the effects ask the engine --------------------------------------------------------- */

void doom_lightAdd(Sprite *s,int fx)
{const DoomLightFx *f=&doomLightFx[fx];
 assert(s && fx>=0 && fx<DLF_NM);
 addLightEx(s,f->r,f->g,f->b,f->radius,f->peak);
}

void doom_lightChange(Sprite *s,int fx)
{const DoomLightFx *f=&doomLightFx[fx];
 assert(s && fx>=0 && fx<DLF_NM);
 changeLightEx(s,f->r,f->g,f->b,f->radius,f->peak);
}

/* Scales the intensity to left/tics; tint and radius unchanged.  The tuner can shorten a flash
   under one that is running, so left is held to the duration. */
void doom_lightFade(Sprite *s,int fx,int left)
{const DoomLightFx *f=&doomLightFx[fx];
 int total=f->tics;
 assert(s && fx>=0 && fx<DLF_NM);
 if (total<1)
    total=1;
 if (left>total)
    left=total;
 changeLightEx(s,f->r,f->g,f->b,0,f->peak*left/total);
}

/* A_Explode.  A rocket already carries its flight light and the list takes no duplicates, so
   that light is brightened and widened instead.  Fading and removal go through flashTics. */
void doom_explosionLight(DoomActor *this)
{assert(this);
 if (!this->sprite)
    return;
 if (hasLight(this->sprite))
    doom_lightChange(this->sprite,DLF_EXPLODE);
 else
    doom_lightAdd(this->sprite,DLF_EXPLODE);
 this->flashTics=doomLightFx[DLF_EXPLODE].tics;
}

/* --- the tuner's rows ------------------------------------------------------------------------ */

enum {LR_EFFECT,LR_ON,LR_INT,LR_RAD,LR_RED,LR_GREEN,LR_BLUE,LR_TICS,LR_RESET,LR_BACK,LR_NM};
static const char *const lightFxName[DLF_NM+1]=
   {"ALL","IMP FIREBALL","CACODEMON SHOT","BARON SHOT","ROCKET","PLASMA BOLT","EXPLOSION",
    "MY MUZZLE FLASH","MONSTER MUZZLE"};
static int lightSel;                    /* 0 = ALL (the player's settings), else the effect + 1 */
static int lightRow;

static int lightClamp(int v,int lo,int hi)
{return (v<lo)? lo: (v>hi)? hi: v;
}

/* the line's own effect, or NULL on ALL */
static DoomLightFx *lightFxOf(void)
{return lightSel? &doomLightFx[lightSel-1]: (DoomLightFx *)0;
}

static int lightRowShown(int r)
{if (r==LR_TICS)
    {DoomLightFx *f=lightFxOf();
     return f && f->tics>0;             /* a missile's light lasts as long as the missile */
    }
 return 1;
}

/* bigFont (the title) has capitals, digits, spaces and '-' only: no '%' anywhere here */
static void lightLine(int r,int inLevel,char *text)
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
     default:        strcpy(text,inLevel? "CLOSE": "BACK"); break;
    }
}

/* d = -1 / +1 on the selected line.  RESET puts back what params/doom.cfg built. */
static void lightAdjust(int r,int d)
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
	   *f=doomLightDef[lightSel-1];
	else
	   {lightPeakPct=lightRadPct=100;
	    lightAddR=lightAddG=lightAddB=0;
	    lightOn=1;
	    for (k=0;k<DLF_NM;k++)
	       doomLightFx[k]=doomLightDef[k];
	   }
	break;
    }
}

static void lightMove(int d)
{do
    lightRow=(lightRow+d+LR_NM)%LR_NM;
 while (!lightRowShown(lightRow));
}

/* --- the title's screen (INTRO.C optionMenu, LIGHTS) ----------------------------------------- */

/* The same loop as the multiplayer screen (MPRULES.C mpGameMenu): every line is a value the
   d-pad turns, so dlg_run -- which answers a press -- is no use here. */
void doom_lightMenu(void)
{static Fixed32 wave;
 int data,last,edge,r,d,y,color;
 char text[40];
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
	 lightLine(r,0,text);
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
}

/* --- the tuner in the level ------------------------------------------------------------------ */

static char lightTunerOn,lightChordHeld;
static unsigned short lightLast=0xffff;
static short lightHold;                 /* frames a value key has been held: the repeat */

#define LIGHT_CHORD  (PER_DGT_TL|PER_DGT_TR|PER_DGT_R)
#define LIGHT_ALT    (PER_DGT_C|PER_DGT_X|PER_DGT_Y)

/* 60 Hz, player 1 in a solo game (DOOM_PLAYER.C doom_playerFrame).  1 = the d-pad and the
   shoulders are the tuner's this frame; the buttons stay the player's, so the effects can be
   fired while they are tuned. */
int doom_lightTuner(unsigned short input)
{unsigned short last=lightLast;
 int edge,d=0;
 if (mpPlayers>1 || mpCur!=0)
    return 0;
 lightLast=input;
 if (((~input)&LIGHT_CHORD)==LIGHT_CHORD || ((~input)&LIGHT_ALT)==LIGHT_ALT)
    {if (!lightChordHeld)
	{lightChordHeld=1;
	 lightTunerOn=!lightTunerOn;
	 lightHold=0;
	 if (lightTunerOn && !lightRowShown(lightRow))
	    lightMove(1);
	}
     return lightTunerOn;
    }
 lightChordHeld=0;
 if (!lightTunerOn)
    return 0;
 edge=(last^input)&~input;
 if (edge & (PER_DGT_U|PER_DGT_D))
    lightMove((edge & PER_DGT_U)? -1: 1);
 /* a press steps once; held down, it repeats after half a second */
 if (!(input & PER_DGT_L))
    d=-1;
 else if (!(input & PER_DGT_R))
    d=1;
 if (!d)
    lightHold=0;
 else if (edge & (PER_DGT_L|PER_DGT_R))
    lightHold=1;
 else if (++lightHold<30 || (lightHold&3))
    d=0;
 if (d)
    lightAdjust(lightRow,d);
 return 1;
}

/* STCFN has no ' ' glyph and drawString adds width+1: the same walk as doom_drawMessage */
static void lightText(int x,int y,const char *s)
{int c,w;
 for (;*s;s++)
    {c=(unsigned char)*s;
     if (c>='a' && c<='z')
	c-='a'-'A';
     w=(c!=' ')? getCharWidth(DOOM_FONT_MSG,(unsigned char)c): 0;
     if (w>0)
	{drawChar(x,y,DOOM_FONT_MSG,(unsigned char)c);
	 x+=w;
	}
     else
	x+=4;
    }
}

/* With the HUD of a solo game (DOOM_HUD.C doom_drawMessage), under the pickup message: the
   lines, the selected one marked, and what the pad does. */
void doom_lightTunerDraw(void)
{char text[40];
 int r,y=-CFG_YCENTER+14;
 if (!lightTunerOn || mpPlayers>1)      /* solo's local origin: the lines are placed on it */
    return;
 for (r=0;r<LR_NM;r++)
    {if (!lightRowShown(r) || r==LR_BACK)
	continue;
     lightLine(r,1,text);
     if (r==lightRow)
	lightText(-152,y,">");
     lightText(-144,y,text);
     y+=10;
    }
 lightText(-152,y+4,"L+R+RIGHT CLOSES");
}

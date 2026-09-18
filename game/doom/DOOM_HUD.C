/* DOOM_HUD.C -- status bar (STBAR + STARMS char, numbers, arms, keys, face) and the HU message.
 * SPEC_PLAYER section 3; art = build/doom/doom_art.h generated from the WAD by
 * tools/doom2ps/wad2hud.py (Makefile rule; an array-less stub with DOOM_ART_STUB until then, in
 * which case nothing is drawn and PRINT.C keeps the PowerSlave fonts).
 *
 * Frame: 320x224 (SPRITE.H CFG_TV_SIZE); local origin (160, 112).  Status-bar coordinates follow
 * st_stuff.c with x_e = x_doom - 160, y_e = y_doom - 88 (Doom bar at y 168 -> screen line 192).
 * Chars (EZ_setChar, allocated in this order by doom_hudInit at SRUINS.C:1890, before the fonts
 * of initFonts(CFG_FONT_BASE = 5)): 0 STBAR 320x32, 1-3 keys 8x8, 4 face 24x32 (re-uploaded when
 * the face index changes).  Fonts (PRINT.C, Doom list): 1 STCFN, 2 STTNUM, 3 STYSNUM + STGNUM.
 *
 * VDP1 VRAM ledger (EZ_initSprSystem(1448,4,1224): chars from 112 448): STBAR 10 240 + keys 3 x 64
 * + face 768 + fonts 2 176 + 1 472 + 640 (32-byte rounded, measured on doom_art.h) = 15 488, against
 * 22 464 for stat_bar 13 440 + compasses 1 920 + brianFont 7 104; + tiles 389 120 (PIC_SLOTS
 * {32,31,1,0,0}, params/doom.cfg) = 517 056 / 524 288: 7 232 bytes free -- no 8 bpp slot given up.
 */
#include <sega_spr.h>
#include <sega_scl.h>
#include "util.h"
#include "spr.h"
#include "print.h"
#include "sprite.h"
#include "object.h"
#include "ai.h"
#include "sruins.h"
#include "gamestat.h"
#include "doom.h"
#include "mplayer.h"
#define DOOM_ART_DEFINE                 /* doom_stbar, doom_faces, doom_keys: MAIN only          */
#define DOOM_ART_DEFINE_FONTS           /* the fonts too: PRINT.o only sees them through doomFontList */
#include "doom_art.h"

#define DOOM_MSGLEN      80
#define DOOM_MSGTIMEOUT  (4*35)         /* HU_MSGTIMEOUT (hu_stuff.c), tics                      */

#define HUD_X(x)         ((x)-160)      /* st_stuff.c x -> local x                               */
#define HUD_Y(y)         ((y)-88)       /* st_stuff.c y -> local y (bar 168..199 -> 80..111)      */
#define HUD_CWORD        0x4000         /* colour word of stat_bar (SRUINS.C:1322): CRAM bank 0 (= PLAYPAL,
					   PIC.C:630-634) + sprite priority register S2 (R9 section 2) */
#define CH_STBAR         0
#define CH_KEY0          1
#define CH_FACE          4
#define FONT_MSG         1              /* STCFN                                                 */
#define FONT_TNUM        2              /* STTNUM + '%'                                          */
#define FONT_SNUM        3              /* STYSNUM, STGNUM at '0'..'9' | 0x80                    */
#define TNUM_PITCH       14             /* STlib_drawNum: width of the '0' patch                  */
#define SNUM_PITCH       4
#define ST_TURNCOUNT     35             /* st_stuff.c, tics                                       */
#define ST_EVILGRINCOUNT 70
#define ST_STRAIGHTFACECOUNT 17

extern unsigned char **doomFontList;    /* PRINT.C (CFG_FONTLIST_DEF): list used by initFonts(.., CFG_FONT_DOOMLIST|..) */

enum {FACE_STRAIGHT,FACE_HURT,FACE_EVIL,FACE_DEAD};

static char doomMessage[DOOM_MSGLEN];
static int doomMessageOn,doomMessageUntil;      /* doomLevelTime deadline                        */
static int hudFirst;                            /* first bar of the level: take the references   */
static int faceKind,faceUntil,faceNextLook,faceStraight,faceUploaded;
static unsigned char faceOldOwned;
static int faceOldDamage;

#ifndef DOOM_ART_STUB
static unsigned char *doomFonts[]=
   {doom_font_stcfn,doom_font_stcfn,doom_font_sttnum,doom_font_stysnum,NULL};

/* ST_Y 168 row of ammo counters, am_clip/shell/cell/misl (ST_AMMO0Y..ST_AMMO3Y) */
static const short ammoY[DOOM_NUMAMMO]={173,179,191,185};
#endif

/* SRUINS.C:1890 (CFG_HUD_CHARS), every level, right after EZ_initSprSystem: the chars in slot
   order, then the Doom font list for initFonts(CFG_FONT_BASE, CFG_FONT_MASK) two lines below. */
void doom_hudInit(void)
{
#ifndef DOOM_ART_STUB
 int k;
 assert(*(int *)doom_stbar==DOOM_STBAR_W && *(int *)(doom_stbar+4)==DOOM_STBAR_H);
 EZ_setChar(CH_STBAR,COLOR_4,DOOM_STBAR_W,DOOM_STBAR_H,doom_stbar+8);
 for (k=0;k<DOOM_NMKEYS;k++)
    EZ_setChar(CH_KEY0+k,COLOR_4,DOOM_KEY_W,DOOM_KEY_H,doom_keys[k]);
 EZ_setChar(CH_FACE,COLOR_4,DOOM_FACE_W,DOOM_FACE_H,doom_faces[DOOM_FACE_ST(0,0)]);
 assert(EZ_charNoToVram(CH_FACE));
 faceUploaded=DOOM_FACE_ST(0,0);
 doomFontList=doomFonts;
#endif
 doomMessageOn=0;
 hudFirst=1;
}

#ifndef DOOM_ART_STUB
/* ST_calcPainOffset (st_stuff.c:684-700): 5 steps of ~20 hp */
static int doomPainOffset(void)
{int h=currentState.health;
 if (h>100) h=100;
 if (h<0) h=0;
 return ((100-h)*5)/101;
}

/* ST_updateFaceWidget reduced (SPEC_PLAYER 3.5; st_stuff.c:707-875): dead > evil grin on a new
   weapon (70 tics, priority 8) > hurt (STFKILL, 35 tics, priorities 6-7: damagecount rose) >
   rampage (STFKILL while the fire button has been held 70 tics, priority 5: ST_RAMPAGEDELAY) >
   straight, look index M_Random()%3 every 17 tics.  Called per frame, timed on the 35 Hz
   doomLevelTime.  Returns the doom_faces[] index. */
#define DOOM_RAMPAGEDELAY 70
static int doomFaceIndex(void)
{int now=doomLevelTime,pain;
 static int rampFrom=-1;                        /* doomLevelTime when attackDown rose; -1 = up */
 if (doomPlayer.attackDown)
    {if (rampFrom<0)
	rampFrom=now;
    }
 else
    rampFrom=-1;
 if (hudFirst)
    {rampFrom=-1;
     faceOldOwned=doomPlayer.weaponOwned;
     faceOldDamage=doomPlayer.damageCount;
     faceKind=FACE_STRAIGHT;
     faceUntil=0;
     faceNextLook=now;
     faceStraight=0;
     hudFirst=0;
    }
 if (faceKind!=FACE_STRAIGHT && faceKind!=FACE_DEAD && now>=faceUntil)
    faceKind=FACE_STRAIGHT;
 if (currentState.health<=0)
    faceKind=FACE_DEAD;
 else
    {if (faceKind==FACE_DEAD)
	faceKind=FACE_STRAIGHT;
     if (doomPlayer.weaponOwned & ~faceOldOwned)
	{faceKind=FACE_EVIL;
	 faceUntil=now+ST_EVILGRINCOUNT;
	}
     else if (doomPlayer.damageCount>faceOldDamage && faceKind!=FACE_EVIL)
	{faceKind=FACE_HURT;
	 faceUntil=now+ST_TURNCOUNT;
	}
    }
 faceOldOwned=doomPlayer.weaponOwned;
 faceOldDamage=doomPlayer.damageCount;

 pain=doomPainOffset();
 switch (faceKind)
    {case FACE_DEAD:
	return DOOM_FACE_DEAD;
     case FACE_EVIL:
	return DOOM_FACE_EVL(pain);
     case FACE_HURT:
	return DOOM_FACE_KILL(pain);
     default:
	if (rampFrom>=0 && now-rampFrom>=DOOM_RAMPAGEDELAY)
	   return DOOM_FACE_KILL(pain);
	if (now>=faceNextLook)
	   {faceStraight=M_Random()%3;
	    faceNextLook=now+ST_STRAIGHTFACECOUNT;
	   }
	return DOOM_FACE_ST(pain,faceStraight);
    }
}

/* STlib_drawNum (st_lib.c): right edge xr, fixed pitch, at most `digits` digits, 0 drawn as '0';
   no clamp (Doom neither).  Negative values do not occur (health/armour/ammo are clamped at 0). */
static void doomHudNum(int xr,int y,int font,int pitch,int num,int digits)
{if (num<0)
    num=0;
 if (!num)
    {drawChar(xr-pitch,y,font,'0');
     return;
    }
 while (num && digits--)
    {xr-=pitch;
     drawChar(xr,y,font,(unsigned char)('0'+num%10));
     num/=10;
    }
}

/* STlib_updatePercent: '%' at x, the number right-aligned on it */
static void doomHudPercent(int xr,int y,int num)
{drawChar(xr,y,FONT_TNUM,'%');
 doomHudNum(xr,y,FONT_TNUM,TNUM_PITCH,num,3);
}
#endif

/* SRUINS.C:2212 (CFG_DRAW_STATBAR), every rendered frame, after the weapon: ST_Drawer's widgets
   (st_stuff.c:ST_createWidgets) at their Doom positions. */
void doom_drawStatBar(void)
{
#ifndef DOOM_ART_STUB
 XyInt pos;
 int i,a,face;

 face=doomFaceIndex();
 assert(face>=0 && face<DOOM_NMFACES);
 if (face!=faceUploaded)
    {EZ_setChar(CH_FACE,COLOR_4,DOOM_FACE_W,DOOM_FACE_H,doom_faces[face]);
     faceUploaded=face;
    }

 pos.x=HUD_X(0); pos.y=HUD_Y(168);
 EZ_normSpr(DIR_NOREV,COLOR_4,HUD_CWORD,CH_STBAR,&pos,NULL);

 /* w_ready: ammo of the ready weapon (ST_AMMOX 44, ST_AMMOY 171), nothing for am_noammo */
 assert(doomPlayer.readyWeapon>=0 && doomPlayer.readyWeapon<NUMWEAPONS);
 a=doomWeaponInfo[(int)doomPlayer.readyWeapon].ammo;
 if (a>=0 && a<DOOM_NUMAMMO)
    doomHudNum(HUD_X(44),HUD_Y(171),FONT_TNUM,TNUM_PITCH,doomPlayer.ammo[a],3);

 doomHudPercent(HUD_X(90),HUD_Y(171),currentState.health);     /* ST_HEALTHX/Y */

 /* w_arms: weapons 2..7 (pistol..BFG), yellow if owned, else the grey STGNUM (ST_ARMSX 111,
    ST_ARMSY 172, spacing 12 x 10) */
 for (i=0;i<6;i++)
    drawChar(HUD_X(111+12*(i%3)),HUD_Y(172+10*(i/3)),FONT_SNUM,
	     (doomPlayer.weaponOwned & (1<<(i+1)))?
	     (unsigned char)('2'+i):(unsigned char)DOOM_FONT_GRAY_DIGIT('2'+i));

 pos.x=HUD_X(DOOM_FACE_ORG_X); pos.y=HUD_Y(DOOM_FACE_ORG_Y);    /* ST_FACESX/Y + patch offsets */
 EZ_normSpr(DIR_NOREV,COLOR_4,HUD_CWORD,CH_FACE,&pos,NULL);

 doomHudPercent(HUD_X(221),HUD_Y(171),doomPlayer.armorPoints);  /* ST_ARMORX/Y */

 /* w_keyboxes (ST_KEY0X 239, ST_KEY0Y 171 + 10 i): card or skull of each colour; the skulls
    (STKEYS3-5) are not in doom_art.h, they show the card */
 for (i=0;i<DOOM_NMKEYS;i++)
    if (doomPlayer.keys & ((1<<i)|(1<<(i+3))))
       {pos.x=HUD_X(239); pos.y=HUD_Y(171+10*i);
	EZ_normSpr(DIR_NOREV,COLOR_4,HUD_CWORD,CH_KEY0+i,&pos,NULL);
       }

 /* w_ammo / w_maxammo: ST_AMMO0X 288, ST_MAXAMMO0X 314, width 3 */
 for (i=0;i<DOOM_NUMAMMO;i++)
    {doomHudNum(HUD_X(288),HUD_Y(ammoY[i]),FONT_SNUM,SNUM_PITCH,doomPlayer.ammo[i],3);
     doomHudNum(HUD_X(314),HUD_Y(ammoY[i]),FONT_SNUM,SNUM_PITCH,doomPlayer.maxAmmo[i],3);
    }
#endif
}

/* changeMessage replacement (SPEC_PLAYER section 3.6): GOT* texts, HU_MSGTIMEOUT tics */
void doom_setMessage(const char *msg)
{int i;
 assert(msg);
 for (i=0;i<DOOM_MSGLEN-1 && msg[i];i++)
    doomMessage[i]=msg[i];
 doomMessage[i]=0;
 doomMessageOn=1;
 doomMessageUntil=doomLevelTime+DOOM_MSGTIMEOUT;
}

/* SRUINS.C:2211 (CFG_DRAW_MESSAGE): HUlib_drawTextLine at (HU_MSGX 0, HU_MSGY 0) = screen (0, 0):
   upper-cased, a glyph advances by its width, a space or an absent glyph by 4, stop at the right
   edge.  drawString is not used: STCFN has no ' ' glyph and drawString adds width+1. */
void doom_drawMessage(void)
{int x,c,w;
 const char *s;
 if (!doomMessageOn)
    return;
 if (doomLevelTime>=doomMessageUntil)
    {doomMessageOn=0;
     return;
    }
#ifndef DOOM_ART_STUB
 x=-160;
 for (s=doomMessage;*s;s++)
    {c=(unsigned char)*s;
     if (c>='a' && c<='z')
	c-='a'-'A';
     w=(c!=' ')?getCharWidth(FONT_MSG,(unsigned char)c):0;
     if (w>0)
	{if (x+w>160)
	    break;
	 drawChar(x,-CFG_YCENTER,FONT_MSG,(unsigned char)c);
	 x+=w;
	}
     else
	{x+=4;
	 if (x>=160)
	    break;
	}
    }
#else
 (void)x; (void)c; (void)w; (void)s;
#endif
}

/* GCC14: local multiplayer (MPLAYER.H).  The HUD's per-player state -- the message and the face
   animation.  faceUploaded stays global: it says which face sits in VDP1 memory. */
void doom_hudMpRegister(void)
{MPREG(doomMessage); MPREG(doomMessageOn); MPREG(doomMessageUntil);
 MPREG(hudFirst);
 MPREG(faceKind); MPREG(faceUntil); MPREG(faceNextLook); MPREG(faceStraight);
 MPREG(faceOldOwned); MPREG(faceOldDamage);
}

/* The compact HUD of one split-screen view, cut from the real STBAR: three slices of it side by
   side -- AMMO (bar x 0-47), HEALTH (48-103), ARMOR (178-233) -- make exactly the 160 pixels of
   a view.  2 players: all 32 lines, under each half.  3-4 players: a 16-line band under each
   quadrant, rows 3-18 of the bar, which is where the big numbers are.  Each slice is the same
   STBAR char drawn under its own local origin and user clip, so the numbers are placed by the
   same st_stuff.c coordinates as solo.  No face (one VDP1 char for everyone, re-uploaded at each
   change), no arms, no ammo table; the keys sit in the view's bottom-right corner.  Doom flashes
   damage and pickups through the palette -- one for the whole screen -- so here a pickup or a
   hit washes the view alone, with a half-transparent quad. */
void doom_drawSplitHud(int view,int nmViews)
{
#ifndef DOOM_ART_STUB
 static const short src[3]={0,48,178},wid[3]={48,56,56};
 XyInt clip[2],pos,quad[4];
 int x0,y0,vh,yb,row0,rows,i,a,dx,n;
 if (nmViews==2)
    {x0=160*view; y0=0; vh=192; yb=192; row0=0; rows=32;}
 else
    {x0=160*(view&1); y0=112*(view>>1); vh=96; yb=y0+96; row0=3; rows=16;}

 /* the flash, over the view it belongs to (drawn before the HUD so the band stays readable) */
 if (doomPlayer.damageCount>=8 || doomPlayer.bonusCount>0)
    {EZ_localCoord(0,0);
     clip[0].x=x0; clip[0].y=y0; clip[1].x=x0+159; clip[1].y=y0+vh-1;
     EZ_userClip(clip);
     quad[0].x=x0;     quad[0].y=y0;
     quad[1].x=x0+159; quad[1].y=y0;
     quad[2].x=x0+159; quad[2].y=y0+vh-1;
     quad[3].x=x0;     quad[3].y=y0+vh-1;
     EZ_polygon(UCLPIN_ENABLE|ECDSPD_DISABLE|COMPO_TRANS|COLOR_5,
		(doomPlayer.damageCount>=8)? RGB(31,0,0): RGB(31,24,0),quad,NULL);
    }

 for (i=0,dx=0;i<3;dx+=wid[i],i++)
    {EZ_localCoord(x0+dx-src[i]+160,yb-row0-80);
     clip[0].x=x0+dx; clip[0].y=yb;
     clip[1].x=x0+dx+wid[i]-1; clip[1].y=yb+rows-1;
     EZ_userClip(clip);
     pos.x=HUD_X(0); pos.y=HUD_Y(168);
     EZ_normSpr(DIR_NOREV,UCLPIN_ENABLE|COLOR_4,HUD_CWORD,CH_STBAR,&pos,NULL);
     if (i==0)
	{assert(doomPlayer.readyWeapon>=0 && doomPlayer.readyWeapon<NUMWEAPONS);
	 a=doomWeaponInfo[(int)doomPlayer.readyWeapon].ammo;
	 if (a>=0 && a<DOOM_NUMAMMO)
	    doomHudNum(HUD_X(44),HUD_Y(171),FONT_TNUM,TNUM_PITCH,doomPlayer.ammo[a],3);
	}
     else if (i==1)
	doomHudPercent(HUD_X(90),HUD_Y(171),currentState.health);
     else
	doomHudPercent(HUD_X(221),HUD_Y(171),doomPlayer.armorPoints);
    }

 EZ_localCoord(0,0);
 clip[0].x=x0; clip[0].y=y0; clip[1].x=x0+159; clip[1].y=yb+rows-1;
 EZ_userClip(clip);
 for (i=0,n=0;i<DOOM_NMKEYS;i++)
    if (doomPlayer.keys & ((1<<i)|(1<<(i+3))))
       {pos.x=x0+160-2-(DOOM_KEY_W+2)*(++n);
	pos.y=yb-DOOM_KEY_H-2;
	EZ_normSpr(DIR_NOREV,COLOR_4,HUD_CWORD,CH_KEY0+i,&pos,NULL);
       }

 /* the pickup message at the view's top-left: Doom's (0,0) is the view's corner */
 EZ_localCoord(x0+160,y0+CFG_YCENTER);
 clip[0].x=x0; clip[0].y=y0; clip[1].x=x0+159; clip[1].y=y0+vh-1;
 EZ_userClip(clip);
 doom_drawMessage();
#else
 (void)view; (void)nmViews;
#endif
}

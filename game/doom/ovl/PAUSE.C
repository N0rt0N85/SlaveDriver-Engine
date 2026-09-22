/* PAUSE.C -- Doom's pause: START in a level (PAUSE.OVL, an overlay: OVL.C, tools/ovlpack.py).
 *
 * GCC14: the whole file.  Compiled into PAUSE.OVL, never into MAIN: it is read from the disc into
 * doorwayCache when START is pressed (DOOM_PAUSE.C doom_pause), runs, and returns an action.
 *
 * The frozen view.  The frame mode is manual (SCL_SetFrameInterval(0xfffe)): the VDP1 erases and
 * swaps, and SCL copies its register buffers to the VDP2, only when SCL_DisplayFrame asks.  This
 * file never calls it (ovlpack.py refuses the link if it does), so the image the game last
 * presented stays on screen, HUD and all, with the VDP1 idle and the slave parked.
 *
 * The VDP2 over it -- set by writing the few registers below straight to the chip at the vblank,
 * never through SCL's buffers, which already hold the NEXT image's sky; given back from those
 * buffers on the way out, so the chip shows exactly what the game would have shown next:
 *   NBG1 = the veil: an 8 bpp bitmap zoomed x16 whose 20x14 visible pixels (VRAM B0 rows 224..237,
 *          columns 0..19) are PLAYPAL 247, black; colour calculation on, ratio GP_PAUSE_VEIL: the
 *          view under it keeps (GP_PAUSE_VEIL+1)/32 of its light -- a multiply, not an offset, so
 *          dark rooms stay readable.  It fades in over 6 fields.
 *   NBG0 = the menu: a 512x512 8 bpp bitmap at VRAM B0, rows 0..223 shown (PAUSE_ROWS), drawn by
 *          the CPU in PLAYPAL indices (CRAM bank 0), 0 = transparent, on top and never mixed.
 * The letters are the HUD's STCFN (resident, doom_art.h) drawn in the title's big shape: every
 * row twice, every even column twice (x2 tall, x1.5 wide -- wad2title.py's big font, pixel for
 * pixel).  The skull (M_SKULL1/2) is pause_art.h's, generated from the WAD.
 * Solo: VRAM B0 is free in play.  Split screen: the split sky keeps its mist and cloud names in
 * B0 (MPSKY.H), so the bytes the menu covers are saved to the free memory the loader hands over
 * and written back before the sky's registers return.
 *
 * Pad: the pausing player's (any joined player in split screen).  UP/DOWN move (DSPSTOP), A or C
 * chooses (DSPISTOL), B goes back a page (DSSWTCHN) or closes the first, START closes from any page
 * (DSSWTCHX); the pause opened with DSSWTCHN.  It returns once the buttons are released, so the
 * START that closes it does not open it again.
 *
 * OPTIONS acts at once, on the values the title's OPTIONS edits: CONTROLS (the action a button
 * does, controllerConfig), SOUND stereo/mono, MUSIC on/off (the CD), FOG (fogCap: the game sets it
 * on the next image), LIGHTS (the light tuner, TUNER.C).  The view stays frozen: what the lights
 * and the fog change shows when the game resumes. */
#include <sega_scl.h>
#include "util.h"
#include "sprite.h"
#include "sound.h"
#include "v_blank.h"
#include "mplayer.h"
#include "gamestat.h"
#include "mpsky.h"
#include "walls.h"
#include "file.h"
#include "ovl.h"
#include "doom.h"
#include "doom_art.h"
#include "doom_ovl.h"
#include "pause_art.h"
#include "tuner.h"

#ifndef GP_PAUSE_VEIL
#define GP_PAUSE_VEIL 16                /* params/doom.cfg PAUSE_VEIL */
#endif

#define VDP2R(o)    (*(volatile Uint16 *)(0x25f80000+(o)))
#define TVSTAT      0x04                /* bit 3: in the vertical blank */
#define B0          ((volatile unsigned char *)(SCL_VDP2_VRAM+0x40000))
#define CRAM0       ((volatile Uint16 *)SCL_COLRAM_ADDR)
#define PAUSE_ROWS  224                 /* NBG0 rows the pause owns (the MAP clears these only) */
#define PAUSE_COLS  320
#define VEIL_Y      224                 /* the veil's pixels: B0 rows 224..237, columns 0..19 */
#define VEIL_W      20
#define VEIL_H      14
#define VEIL_INDEX  247                 /* PLAYPAL (0,0,0), opaque */
#define VEIL_FIELDS 6
#define BLINK       14                  /* fields a skull frame shows: Doom's 8 tics */
#define ITEM_X      88                  /* Doom's main menu is at 97, the skull 32 to its left */
#define ITEM_Y0     46
#define ITEM_PITCH  18
#define TITLE_Y     18
#define HELP_Y      204
#define BIG_H       (2*(*(const short *)doom_font_stcfn))   /* a big letter's rows: 16 */
#define CTL_X       56                  /* CONTROLS: the actions, their buttons at CTL_BX */
#define CTL_BX      264
#define CTL_Y0      38
#define LIT_X       40                  /* LIGHTS: "EFFECT  MY MUZZLE FLASH" is 275 wide */
#define LIT_Y0      36
#define LIT_PITCH   16                  /* ten rows end at 196, above the two small lines */
#define LIT_NOTE_Y  199
#define LIT_HELP_Y  209
#define BUTTONS     (PER_DGT_S|PER_DGT_A|PER_DGT_B|PER_DGT_C)

typedef char sclLayout[sizeof(SclSysreg)==0x28 && sizeof(SclDataset)==0x48 &&
		       sizeof(SclNorscl)==0x40 && sizeof(SclWinscl)==0x20? 1: -1];

/* The registers the pause writes (offsets from 0x25f80000), and nothing else: the frozen VDP1
   image and RBG0's sky stay exactly as they are on screen. */
static const Uint16 regList[]=
{0x18,0x1a,                             /* CYCB0L/U */
 0x20,0x28,0x2c,0x3c,                   /* BGON, CHCTLA, BMPNA, MPOFN */
 0x70,0x72,0x74,0x76,0x78,0x7a,0x7c,0x7e,   /* NBG0 scroll and zoom */
 0x80,0x82,0x84,0x86,0x88,0x8a,0x8c,0x8e,   /* NBG1 */
 0x98,0x9a,0xd0,                        /* ZMCTL, SCRCTL, WCTLA */
 0xe4,0xe8,0xea,0xec,0xee,              /* CRAOFA, LNCLEN, SFPRMD, CCCTL, SFCCMD */
 0xf8,0x108,0x110,0x112};               /* PRINA, CCRNA, CLOFEN, CLOFSL */
#define NREGS (sizeof(regList)/sizeof(regList[0]))

static const char *const items[]={"RESUME","MAP","STATS","WEAPONS","OPTIONS","SAVE / LOAD",
				  "RESTART LEVEL","QUIT TO TITLE"};
#define NITEMS 8
#define ITEM_RESUME 0
#define ITEM_OPTIONS 4
#define ITEM_QUIT   7
#define LIVE ((1<<ITEM_RESUME)|(1<<ITEM_OPTIONS)|(1<<ITEM_QUIT))   /* the others: grey, later */

enum {OPT_CONTROLS,OPT_SOUND,OPT_MUSIC,OPT_FOG,OPT_LIGHTS,OPT_BACK,OPT_NM};
#define CTL_BACK    8                   /* CONTROLS: the eight actions, then BACK */
/* Doom's words for the engine's action slots (UTIL.H ACTION_*, as DOOM_PLAYER.C and SRUINS.C read
   them in a Doom game: the JUMP slot runs, PUSH uses, the free-look slot does nothing) */
static const char *const actName[8]={"FIRE","RUN","USE","NOTHING","PREVIOUS WEAPON",
				     "NEXT WEAPON","STRAFE LEFT","STRAFE RIGHT"};
static const char *const buttonName[8]={"A","B","C","X","Y","Z","L","R"};   /* buttonMasks[] */

static int pad;                         /* the pausing player */
static Uint16 held;                     /* its buttons at the last field (0 = pressed) */
static Uint32 lastField;
static int field;                       /* fields since the pause opened */
static int sel;                         /* the chosen line */
static int pgX,pgN;                     /* the page shown: its lines' left edge and count, */
static unsigned int pgLive;             /* ... the lines the skull may stop on, */
static short lineY[LR_NM];              /* ... and each line's top */
static int skullShown;                  /* the skull frame on screen */
static unsigned char red[16],grey[16];  /* STCFN nibble -> PLAYPAL index */
static unsigned short glyphAt[128];     /* STCFN: where code c's rows start */

/* ------------------------------------------------------------------------ the VDP2 */
/* what SCL's buffers hold for register o: the state the game shows next */
static Uint16 bufReg(int o)
{const Uint16 *p;
 if (o<0x28)
    p=(const Uint16 *)&Scl_s_reg+(o>>1);
 else if (o<0x70)
    p=(const Uint16 *)&Scl_d_reg+((o-0x28)>>1);
 else if (o<0xb0)
    p=(const Uint16 *)&Scl_n_reg+((o-0x70)>>1);
 else if (o<0xe0)
    p=(const Uint16 *)&Scl_w_reg+((o-0xc0)>>1);
 else if (o<0xf0)
    p=(const Uint16 *)&SclOtherPri+((o-0xe0)>>1);
 else if (o<0x100)
    p=(const Uint16 *)&SclBgPriNum+((o-0xf8)>>1);
 else if (o<0x110)
    p=(const Uint16 *)&SclBgColMix+((o-0x108)>>1);
 else
    p=(const Uint16 *)&SclColOffset+((o-0x110)>>1);
 return *p;
}

/* the pause's value of register o, with the veil at ratio v */
static Uint16 pauseReg(int o,int v)
{Uint16 b=bufReg(o);
 switch (o)
    {case 0x18:                         /* B0, solo: NBG0 bitmap T0-T1, NBG1 T2-T3, CPU T4-T7 (as */
	return mpSkyOn? 0x554f: 0x4455; /* the game's B1).  Split: B1 stays the sky's (MPSKY.C),  */
     case 0x1a:                         /* CPU at T7 only: B0's CPU slots must match B1's, after a */
	return mpSkyOn? 0x44fe: 0xeeee; /* no-access T6 (partitioned VRAM, VDP2 p.36): NBG1 T0-T1, */
					/* NBG0 T2/T4/T5 */
     case 0x20: return (b&~0x0303)|0x0003;  /* NBG0, NBG1 on, pixel 0 transparent */
     case 0x28: return 0x1216;          /* NBG1 512x256, NBG0 512x512: 256-colour bitmaps */
     case 0x2c: return b&~0x3737;       /* palette 0, no special bits */
     case 0x3c: return (b&~0x0077)|0x0022;  /* both bitmaps at 0x40000 (B0) */
     case 0x78: case 0x7c: return 1;    /* NBG0 1:1 at 0,0 */
     case 0x84: return VEIL_Y;          /* NBG1 at 0,224 ... */
     case 0x8a: case 0x8e: return 0x1000;   /* ... zoomed x16: 1/16 bitmap pixel a pixel */
     case 0x98: return b&~0x0303;       /* no reduction */
     case 0x9a: return b&~0x3f3f;       /* no line or cell scroll */
     case 0xd0: return 0;               /* no window on either */
     case 0xe4: return b&~0x0077;       /* CRAM bank 0: PLAYPAL */
     case 0xe8: return b&~0x0003;       /* no line colour */
     case 0xea: return b&~0x000f;       /* priority per screen, not per pixel */
     case 0xec: return (b&~0xff03)|0x0002;  /* NBG1 mixed, by its own ratio; NBG0 opaque */
     case 0xee: return b&~0x000f;       /* colour calculation per screen */
     case 0xf8: return 0x0607;          /* NBG0 7 (the menu), NBG1 6 (the veil); SP0 is 4 */
     case 0x108: return (b&0x001f)|(v<<8);
     case 0x110:                        /* colour offset: never on the menu; on the veil as on */
     case 0x112:                        /* SP0, the view under it (it applies to the mixed    */
	return (b&~0x0003)|((b>>5)&2);  /* result, by the top screen's bit): a damage flash stays */
     default: return 0;                 /* the other scroll and zoom words */
    }
}

static void vblankIn(void)
{while (VDP2R(TVSTAT)&8)
    ;
 while (!(VDP2R(TVSTAT)&8))
    ;
}

/* at the next vblank: the pause's registers (on), or the game's (off) */
static void setRegs(int on)
{int i;
 vblankIn();
 for (i=0;i<(int)NREGS;i++)
    VDP2R(regList[i])=on? pauseReg(regList[i],31): bufReg(regList[i]);
}

/* Split screen: the B0 bytes the menu covers that the sky keeps (rows 0..PAUSE_ROWS-1, columns
   0..319, inside MPSKY.H's ranges), to buf (save) or back from it.  buf NULL: only the count. */
static int skyBytes(Uint32 *buf,int save)
{static const Uint32 keep[2][2]={{MPSKY_B0_HAZE,MPSKY_B0_HAZE_END},
				 {MPSKY_B0_CLOUD,MPSKY_B0_CLOUD_END}};
 int y,k,n=0;
 for (y=0;y<PAUSE_ROWS;y++)
    for (k=0;k<2;k++)
       {Uint32 a=0x40000+(y<<9),e=a+PAUSE_COLS;
	volatile Uint32 *v;
	if (a<keep[k][0])
	   a=keep[k][0];
	if (e>keep[k][1])
	   e=keep[k][1];
	for (v=(volatile Uint32 *)(SCL_VDP2_VRAM+a);a<e;a+=4,v++,n+=4)
	   if (buf)
	      {if (save)
		  *buf++=*v;
	       else
		  *v=*buf++;
	      }
       }
 return n;
}

/* ------------------------------------------------------------------------ letters */
/* STCFN's layout (PRINT.C initFonts' format) and its colours as PLAYPAL indices: each of the 15
   CLUT colours is found in CRAM bank 0 (PLAYPAL); the grey of a disabled line follows its red */
static void fontInit(void)
{const unsigned char *f=doom_font_stcfn;
 int h=*(const short *)f,c,n,i,at=0;
 for (c=0;c<256;c++)
    {n=f[34+c];
     if (c<128)
	glyphAt[c]=at;
     at+=h*((n+1)>>1);
    }
 for (n=1;n<16;n++)
    {Uint16 col=((const Uint16 *)(f+2))[n]&0x7fff;
     red[n]=176;
     for (i=1;i<255;i++)
	if ((CRAM0[i]&0x7fff)==col)
	   {red[n]=i;
	    break;
	   }
     grey[n]=96+((31-(col&31))>>1);     /* PLAYPAL 96..111: mid to dark greys */
    }
}

/* one character at x,y; big = the title's shape.  -> its advance */
static int putChar(int x,int y,int c,const unsigned char *lut,int big)
{const unsigned char *f=doom_font_stcfn,*s;
 int h=*(const short *)f,w,i,j,n,cx=0;
 if (c>='a' && c<='z')
    c-='a'-'A';
 if (c<=0 || c>=128 || !(w=f[34+c]))
    return (big? 6: 4)+1;               /* the title fonts' space */
 s=f+34+256+glyphAt[c];
 for (j=0;j<h;j++,s+=(w+1)>>1)
    {volatile unsigned char *d=B0+((y+(j<<big))<<9)+x;
     for (i=0,cx=0;i<w;i++)
	{n=(i&1)? s[i>>1]&15: s[i>>1]>>4;
	 if (n)
	    {d[cx]=lut[n];
	     if (big)
		{d[cx+512]=lut[n];
		 if (!(i&1))
		    d[cx+1]=d[cx+513]=lut[n];
		}
	    }
	 cx+=(big && !(i&1))? 2: 1;
	}
    }
 return cx+1;
}

static int textWidth(const char *t,int big)
{const unsigned char *f=doom_font_stcfn;
 int w=-1,c,n;
 for (;*t;t++)
    {c=*t;
     if (c>='a' && c<='z')
	c-='a'-'A';
     n=(c>0 && c<128)? f[34+c]: 0;
     w+=(n? (big? (3*n+1)>>1: n): (big? 6: 4))+1;
    }
 return w;
}

static void text(int x,int y,const char *t,const unsigned char *lut,int big)
{if (x<0)                               /* centred */
    x=(PAUSE_COLS-textWidth(t,big))>>1;
 for (;*t;t++)
    x+=putChar(x,y,*t,lut,big);
}

/* ------------------------------------------------------------------------ the page */
static void clearRows(int y0,int y1)
{int y,x;
 for (y=y0;y<y1;y++)
    {volatile Uint32 *d=(volatile Uint32 *)(B0+(y<<9));
     for (x=0;x<PAUSE_COLS/4;x++)
	d[x]=0;
    }
}

/* the skull beside line i: frame 0/1, or none (-1: its box cleared) */
static void skull(int i,int frame)
{const unsigned char *s=frame>=0? pauseSkull[frame]: NULL;
 int x,y;
 for (y=0;y<PAUSE_SKULL_H;y++)
    {volatile unsigned char *d=B0+((lineY[i]-2+y)<<9)+pgX-32;
     for (x=0;x<PAUSE_SKULL_W;x++)
	d[x]=s? *s++: 0;
    }
}

static int skullFrame(void)
{return (field/BLINK)&1;
}

/* a page: its heading, and where its n lines and the skull go (the lines' tops go to lineY) */
static void pageStart(const char *title,int x,int n,unsigned int live)
{clearRows(0,PAUSE_ROWS);
 text(-1,TITLE_Y,title,red,1);
 pgX=x;
 pgN=n;
 pgLive=live;
}

static void pageSkull(void)
{skull(sel,skullShown=skullFrame());
}

/* line i again, its value changed: the old words cleared from the line's left edge */
static void lineAgain(int i,const char *t)
{int y,x;
 for (y=lineY[i];y<lineY[i]+BIG_H;y++)
    {volatile unsigned char *d=B0+(y<<9);
     for (x=pgX;x<PAUSE_COLS;x++)
	d[x]=0;
    }
 text(pgX,lineY[i],t,red,1);
}

static void drawMain(void)
{int i;
 pageStart("PAUSE",ITEM_X,NITEMS,LIVE);
 for (i=0;i<NITEMS;i++)
    text(ITEM_X,lineY[i]=ITEM_Y0+i*ITEM_PITCH,items[i],((LIVE>>i)&1)? red: grey,1);
 text(-1,HELP_Y,"A SELECT   B BACK   START RESUME",red,0);
 pageSkull();
}

/* ------------------------------------------------------------------------ the pad, the fields */
/* the next field: the sounds and the music step, the veil fades in; -> the buttons pressed since
   the last field */
static Uint16 nextField(void)
{Uint16 now,hit;
 while (vtimer==lastField)
    ;
 lastField=vtimer;
 sound_nextFrame();
 if (++field<=VEIL_FIELDS)            /* at the vblank: vtimer steps at its end, mid-screen soon */
    {vblankIn();
     VDP2R(0x108)=pauseReg(0x108,31-(31-GP_PAUSE_VEIL)*field/VEIL_FIELDS);
    }
 now=lastInputSampleP[pad];
 hit=held&~now;
 held=now;
 return hit;
}

/* A (or C) yes, B (or START) no */
static int confirm(const char *question)
{Uint16 hit;
 clearRows(0,PAUSE_ROWS);
 text(-1,88,question,red,1);
 text(-1,116,"A YES   B NO",red,1);
 do
    hit=nextField();
 while (!(hit&(BUTTONS)));
 return !!(hit&(PER_DGT_A|PER_DGT_C));
}

static void move(int d)
{int i=sel;
 do
    i=(i+d+pgN)%pgN;
 while (!((pgLive>>i)&1));
 if (i==sel)
    return;
 skull(sel,-1);
 sel=i;
 pageSkull();
 doom_playerSound(sfx_pstop);
}

/* the next field, the skull blinking; -> the buttons pressed */
static Uint16 step(void)
{Uint16 hit=nextField();
 if (skullFrame()!=skullShown)
    pageSkull();
 return hit;
}

/* UP / DOWN: -> 1 if the press moved the skull */
static int navigate(Uint16 hit)
{if (hit&PER_DGT_U)
    move(-1);
 else if (hit&PER_DGT_D)
    move(1);
 else
    return 0;
 return 1;
}

/* ------------------------------------------------------------------------ OPTIONS */
/* CONTROLS: the eight actions and the button each is on.  Press a button on an action's line and
   it does that action, the action it did going to the button the line had (INTRO.C remapMenu's
   rule, on the same controllerConfig: every pad).  -> 1 = START: the pause closes. */
static void ctlLine(int i)
{lineAgain(i,actName[i]);
 text(CTL_BX,lineY[i],buttonName[(int)controllerConfig[i]],red,1);
}

static void drawControls(void)
{int i;
 pageStart("CONTROLS",CTL_X,CTL_BACK+1,(1<<(CTL_BACK+1))-1);
 for (i=0;i<=CTL_BACK;i++)
    {lineY[i]=CTL_Y0+i*ITEM_PITCH;
     if (i<CTL_BACK)
	ctlLine(i);
    }
 text(CTL_X,lineY[CTL_BACK],"BACK",red,1);
 text(-1,HELP_Y,"PRESS A BUTTON TO GIVE IT THAT ACTION",red,0);
 pageSkull();
}

static int controls(void)
{int r=-1,b,i;
 Uint16 hit;
 sel=0;
 drawControls();
 while (r<0)
    {hit=step();
     if (hit&PER_DGT_S)
	r=1;
     else if (navigate(hit))
	;
     else if (sel==CTL_BACK)
	{if (hit&(PER_DGT_A|PER_DGT_B|PER_DGT_C))
	    r=0;
	}
     else
	for (b=0;b<8;b++)
	   if (hit&buttonMasks[b])
	      {for (i=0;i<8 && controllerConfig[i]!=b;i++)
		  ;
	       if (i<8 && i!=sel)
		  {controllerConfig[i]=controllerConfig[sel];
		   controllerConfig[sel]=(char)b;
		   ctlLine(i);
		   ctlLine(sel);
		  }
	       doom_playerSound(sfx_pistol);
	       break;
	      }
    }
 return r;
}

/* LIGHTS: the tuner's rows (TUNER.C).  LEFT / RIGHT change the chosen one (held, they repeat
   after half a second), A steps it up, or does RESET / BACK.  -> 1 = START: the pause closes. */
static void drawLights(void)
{char t[40];
 unsigned int live=0;
 int r,y=LIT_Y0;
 for (r=0;r<LR_NM;r++)
    if (lightRowShown(r))
       live|=1<<r;
 pageStart("LIGHTS",LIT_X,LR_NM,live);
 for (r=0;r<LR_NM;r++)
    if ((live>>r)&1)
       {lightLine(r,t);
	text(LIT_X,lineY[r]=y,t,red,1);
	y+=LIT_PITCH;
       }
 text(-1,LIT_NOTE_Y,"CHANGES SHOW WHEN YOU RESUME",red,0);
 text(-1,LIT_HELP_Y,"LEFT RIGHT CHANGE   B BACK",red,0);
 pageSkull();
}

static int lights(void)
{char t[40];
 int r=-1,d,hold=0;
 Uint16 hit;
 sel=lightRow;
 drawLights();
 while (r<0)
    {hit=step();
     d=0;
     if (!(held&PER_DGT_L))             /* active low: held down */
	d=-1;
     else if (!(held&PER_DGT_R))
	d=1;
     if (!d)
	hold=0;
     else if (hit&(PER_DGT_L|PER_DGT_R))
	hold=1;
     else if (++hold<30 || (hold&3))
	d=0;
     if (hit&PER_DGT_S)
	r=1;
     else if ((hit&PER_DGT_B) || ((hit&(PER_DGT_A|PER_DGT_C)) && sel==LR_BACK))
	r=0;
     else if (navigate(hit))
	lightRow=sel;
     else
	{if (hit&(PER_DGT_A|PER_DGT_C))
	    d=1;
	 if (d && sel!=LR_BACK)
	    {lightAdjust(sel,d);
	     doom_playerSound(sfx_stnmov);
	     if (sel==LR_EFFECT || sel==LR_RESET)
		drawLights();           /* every line, and DURATION comes or goes */
	     else
		{lightLine(sel,t);
		 lineAgain(sel,t);
		}
	    }
	}
    }
 return r;
}

/* the options' words: the values they have now */
static const char *optText(int i)
{static const char *const fog[4]={"FOG  OFF","FOG  LOW","FOG  MEDIUM","FOG  HIGH"};
 switch (i)
    {case OPT_CONTROLS: return "CONTROLS";
     case OPT_SOUND:    return enable_stereo? "SOUND  STEREO": "SOUND  MONO";
     case OPT_MUSIC:    return enable_music? "MUSIC  ON": "MUSIC  OFF";
     case OPT_FOG:      return fog[fogLevel()];
     case OPT_LIGHTS:   return "LIGHTS";
     default:           return "BACK";
    }
}

static void drawOptions(void)
{int i;
 pageStart("OPTIONS",ITEM_X,OPT_NM,(1<<OPT_NM)-1);
 for (i=0;i<OPT_NM;i++)
    text(ITEM_X,lineY[i]=ITEM_Y0+i*ITEM_PITCH,optText(i),red,1);
 text(-1,HELP_Y,"A CHANGE   B BACK   START RESUME",red,0);
 pageSkull();
}

/* -> 1 = START: the pause closes; 0 = back to the first page */
static int options(void)
{int r=-1,i;
 Uint16 hit;
 sel=0;
 drawOptions();
 while (r<0)
    {hit=step();
     if (hit&PER_DGT_S)
	r=1;
     else if (hit&PER_DGT_B)
	r=0;
     else if (!navigate(hit) && (hit&(PER_DGT_A|PER_DGT_C)))
	{i=sel;
	 if (i==OPT_BACK)
	    {r=0;
	     continue;
	    }
	 doom_playerSound(sfx_pistol);
	 switch (i)
	    {case OPT_CONTROLS:
	     case OPT_LIGHTS:
		if (i==OPT_CONTROLS? controls(): lights())
		   r=1;
		else
		   {doom_playerSound(sfx_swtchn);
		    sel=i;
		    drawOptions();
		   }
		continue;
	     case OPT_SOUND:            /* SOUND.C pans by it; the title reads it from here */
		enable_stereo=!enable_stereo;
		systemMemory=(systemMemory&~PER_MSK_STEREO)|(enable_stereo? 0: PER_MSK_STEREO);
		break;
	     case OPT_MUSIC:
		enable_music=!enable_music;
		if (enable_music)
		   playCDTrackForLevel(currentState.currentLevel);
		else
		   stopCD();
		break;
	     case OPT_FOG:              /* SRUINS.C mpSetViewFog applies it to the next image */
		fogCap=fogLevels[(fogLevel()+1)&3];
		break;
	    }
	 lineAgain(i,optText(i));
	}
    }
 return r;
}

/* ------------------------------------------------------------------------ the entry */
int pause_main(int k,char *freeBase,char *freeEnd)
{Uint32 *sky=NULL;
 int r=-1,i;
 Uint16 hit;
 pad=k;
 field=0;
 sel=ITEM_RESUME;
 lastField=vtimer;
 held=lastInputSampleP[k];              /* the START that opened it is not a press */
 if (mpSkyOn)
    {freeBase=(char *)(((int)freeBase+3)&~3);
     if (freeEnd-freeBase<skyBytes(NULL,0))
	{while (!(lastInputSampleP[k]&PER_DGT_S))
	    ;                           /* never: doorwayCache holds 66 000 bytes */
	 return PAUSE_RESUME;
	}
     sky=(Uint32 *)freeBase;
     vblankIn();                        /* the mist and the clouds go before their bytes do */
     VDP2R(0x20)=bufReg(0x20)&~0x0003;
     skyBytes(sky,1);
    }
 saveSoundState();
 stopAllLoopedSounds();
 doom_playerSound(sfx_swtchn);
 fontInit();
 for (i=0;i<VEIL_H;i++)
    {volatile unsigned char *d=B0+((VEIL_Y+i)<<9);
     int x;
     for (x=0;x<VEIL_W;x++)
	d[x]=VEIL_INDEX;
    }
 drawMain();
 setRegs(1);
 while (r<0)
    {hit=step();
     if (hit&(PER_DGT_S|PER_DGT_B))
	r=PAUSE_RESUME;
     else if (navigate(hit))
	;
     else if (hit&(PER_DGT_A|PER_DGT_C))
	{doom_playerSound(sfx_pistol);
	 if (sel==ITEM_OPTIONS)
	    {if (options())
		r=PAUSE_RESUME;
	     else
		{doom_playerSound(sfx_swtchn);
		 sel=ITEM_OPTIONS;
		 drawMain();
		}
	    }
	 else if (sel==ITEM_QUIT)
	    {if (confirm("QUIT TO THE TITLE?"))
		r=PAUSE_QUIT;
	     else
		{doom_playerSound(sfx_swtchx);
		 drawMain();
		}
	    }
	 else
	    r=PAUSE_RESUME;
	}
    }
 while ((held&BUTTONS)!=BUTTONS)        /* released: the hook must not see START again */
    nextField();
 if (sky)
    {vblankIn();
     VDP2R(0x20)=bufReg(0x20)&~0x0003;  /* menu and veil off before the sky's bytes return */
     skyBytes(sky,0);
    }
 setRegs(0);
 if (r==PAUSE_RESUME)
    {doom_playerSound(sfx_swtchx);
     restoreSoundState();
    }
 return r;
}

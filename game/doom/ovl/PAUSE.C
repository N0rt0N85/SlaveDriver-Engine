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
 * on the next image), SHADOWS (the blob under a thing and the spectre: WALLS.C shadowMode,
 * shadowPct, spectreMode), LIGHTS (the light tuner, TUNER.C).  The view stays frozen: what the lights
 * and the fog change shows when the game resumes. */
#include <stdio.h>
#include <string.h>
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
#include "pmap.h"
#include <sega_bup.h>
#include "bup.h"

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
#define STAT_X      40                  /* STATS: the names, their values right-aligned here */
#define STAT_RX     284
#define STAT_Y0     58
#define STAT_NAME_Y 40                  /* the level's name, small, over the rows */
#define STAT_ROW_Y0 52                  /* split screen: a small table, a row a player */
#define STAT_ROW_P  14
#define WPN_X       40                  /* WEAPONS: the names, their damage right-aligned ... */
#define WPN_RX      214
#define WPN_Y0      46
#define WPN_PICX    224                 /* ... and the chosen one's pickup in this box */
#define WPN_PICW    94
#define WPN_PICY    40
#define WPN_PICH    56
#define WPN_AMMO_Y  192
#define SAV_X       56                  /* SAVE / LOAD: the slots, two lines each */
#define SAV_Y0      52
#define SAV_PITCH   26
#define SAV_SUB     17                  /* the small line under a slot's big one */
#define SAV_DEV_Y   38
#define SAV_FOOT_Y  212
#define SAV_LIB     (16*1024)           /* what BUP_Init wants, out of the free memory */
#define SAV_WORK    (8*1024)
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
#define ITEM_RESUME  0
#define ITEM_MAP     1
#define ITEM_STATS   2
#define ITEM_WEAPONS 3
#define ITEM_OPTIONS 4
#define ITEM_SAVE    5
#define ITEM_RESTART 6
#define ITEM_QUIT    7

/* Doom's own par times for episode 1, in seconds (g_game.c pars[1]).  The STATS page shows them
   next to the time played, as the intermission does. */
static const short parTime[DOOM_NMLEVELS]={30,75,120,90,165,180,180,30,165};

/* The eight weapons in slot order (doom_weapontype_t), what Doom's rules do to a target, and the
   ammo each spends.  The damage is FIXED TEXT: these are Doom v1.9's numbers (p_pspr.c, info.c),
   not something the port computes, and the port has no berserk, so the fist stays 2-20. */
#define WPN_NM 8                        /* weaponOwned is 8 bits: fist .. chainsaw */
static const char *const wpnName[WPN_NM]=
   {"FIST","PISTOL","SHOTGUN","CHAINGUN","ROCKET LAUNCHER","PLASMA RIFLE","BFG 9000","CHAINSAW"};
static const char *const wpnDamage[WPN_NM]=
   {"2-20","5-15","35-105","5-15","20-160","5-40","100-800","2-20"};
static const char *const ammoName[DOOM_NUMAMMO]={"CLIP","SHELL","CELL","ROCKET"};

enum {OPT_CONTROLS,OPT_SOUND,OPT_MUSIC,OPT_FOG,OPT_SHADOWS,OPT_LIGHTS,OPT_BACK,OPT_NM};
/* SHADOWS: the blob's mode, its size, the spectre's mode.  The blob may be off; the spectre may
   not (it would be invisible), so the two lists differ. */
enum {SHD_MODE,SHD_SIZE,SHD_SPECTRE,SHD_BACK,SHD_NM};
static const char *const compoName[CFG_COMPO_NM]=
   {"NONE","OPAQUE","MESH","SHADOW","GRAIN","SEE THROUGH","SEE THRU GRAIN","DARK",
    "GREY + SHADOW","SHADOW X1-3"};
#define SHADOW_NM   6
#define SPECTRE_NM  6
static const unsigned char shadowList[SHADOW_NM]=
   {CFG_COMPO_NONE,CFG_COMPO_OPAQUE,CFG_COMPO_MESH,CFG_COMPO_SHADOW,CFG_COMPO_GRAIN,
    CFG_COMPO_DARK};
/* DARK left the spectre's list: two shadows over a whole monster is a flat quarter, and the
   grouse of the thing is that its pixels are all alike.  SHADOW X1-3 took its place -- one full
   shadow then two meshed ones, each offset by a random pixel, so a pixel takes one, two or three
   shadows and the draw changes every image.  It sits next to SHADOW, where it is looked for. */
static const unsigned char spectreList[SPECTRE_NM]=
   {CFG_COMPO_SHADOW,CFG_COMPO_FUZZ,CFG_COMPO_GRAIN,CFG_COMPO_MESH,CFG_COMPO_TRANS,
    CFG_COMPO_TBW};
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
static char *pmapFree,*pmapEnd;         /* what the loader left free: the MAP browser's scratch */
/* Which half of the NBG0 bitmap the drawing below writes into: 0 = VRAM B0 (rows 0..255, what the
   menu shows at scroll 0), 1 = VRAM B1 (rows 256..511), where the MAP builds its next frame while
   this one is on screen.  One VDP2 bank each, so the chip reads one while the CPU fills the other.
   The veil's own bytes are outside this: they are always in B0, rows 224..237. */
static Uint32 halfOff;
#define HALF (B0+halfOff)

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
    {volatile unsigned char *d=HALF+((y+(j<<big))<<9)+x;
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

/* the same, ending at xr: a column of numbers lines up on its right edge */
static void textRight(int xr,int y,const char *t,const unsigned char *lut,int big)
{text(xr-textWidth(t,big),y,t,lut,big);
}

/* ------------------------------------------------------------------------ the page */
/* GCC14: THE HUD IS HIDDEN, NOT COVERED.  The status bar belongs to the FROZEN VDP1 image --
   the chip is idle and nothing can rub it out -- and the menu's own bitmap is transparent
   wherever it draws nothing, so the bar showed straight through the page.  The lines at the
   bottom, a page's footer among them (SAV_FOOT_Y), were written over its brass and its faces
   and could not be read; NBG0 already owns priority 7, so this was never a question of what
   passes in front.  So the bar's rows take PLAYPAL 247 instead of the transparent index: the
   veil's own opaque black, which is what the rest of the page is read on.
   The bar is st_stuff.c 168..199, which DOOM_HUD.C's HUD_Y(y)=y-88 puts at local 80..111 --
   rows 192..223 of the 224 the pause owns. */
#define HUD_ROW0    192

static void clearRows(int y0,int y1)
{int y,x;
 for (y=y0;y<y1;y++)
    {volatile Uint32 *d=(volatile Uint32 *)(HALF+(y<<9));
     unsigned int v=(y>=HUD_ROW0)? VEIL_INDEX*0x01010101u: 0;
     for (x=0;x<PAUSE_COLS/4;x++)
	d[x]=v;
    }
}

/* the skull beside line i: frame 0/1, or none (-1: its box cleared) */
static void skull(int i,int frame)
{const unsigned char *s=frame>=0? pauseSkull[frame]: NULL;
 int x,y;
 for (y=0;y<PAUSE_SKULL_H;y++)
    {volatile unsigned char *d=HALF+((lineY[i]-2+y)<<9)+pgX-32;
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
    {volatile unsigned char *d=HALF+(y<<9);
     for (x=pgX;x<PAUSE_COLS;x++)
	d[x]=0;
    }
 text(pgX,lineY[i],t,red,1);
}

/* the lines the skull may stop on.  MAP is grey in split screen: VRAM B belongs to the split
   sky there (MPSKY.C), and the browser draws into it.  SAVE / LOAD asks bup_canSaveGame
   (DOOM_SAVE.C): a co-operative game may save, a competitive one has no single arsenal to write. */
static unsigned int liveItems(void)
{unsigned int m=(1<<ITEM_RESUME)|(1<<ITEM_STATS)|(1<<ITEM_WEAPONS)|(1<<ITEM_OPTIONS)|
							  (1<<ITEM_RESTART)|(1<<ITEM_QUIT);
 if (!mpSkyOn)
    m|=1<<ITEM_MAP;
 if (bup_canSaveGame())                 /* one player, a campaign, and a device that answered:
						    a split or competitive game has no single arsenal to write */
    m|=1<<ITEM_SAVE;
 return m;
}

static void drawMain(void)
{unsigned int live=liveItems();
 int i;
 pageStart("PAUSE",ITEM_X,NITEMS,live);
 for (i=0;i<NITEMS;i++)
    text(ITEM_X,lineY[i]=ITEM_Y0+i*ITEM_PITCH,items[i],((live>>i)&1)? red: grey,1);
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

/* any button, once the player has read it */
static void notice(const char *a,const char *b)
{Uint16 hit;
 clearRows(0,PAUSE_ROWS);
 text(-1,88,a,red,1);
 if (b)
    text(-1,116,b,red,0);
 text(-1,160,"PRESS A BUTTON",red,0);
 do
    hit=nextField();
 while (!(hit&BUTTONS));
}

/* A (or C) yes, B (or START) no.  `sub` is the line of context over the question -- the slot being
   overwritten, the level being loaded.  It is drawn HERE and nowhere else: the page is cleared
   first, so anything painted before the call would go with it. */
static int confirmSub(const char *sub,const char *question)
{Uint16 hit;
 clearRows(0,PAUSE_ROWS);
 if (sub)
    text(-1,64,sub,red,0);
 text(-1,88,question,red,1);
 text(-1,116,"A YES   B NO",red,1);
 do
    hit=nextField();
 while (!(hit&(BUTTONS)));
 return !!(hit&(PER_DGT_A|PER_DGT_C));
}

static int confirm(const char *question)
{return confirmSub(NULL,question);
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

/* ------------------------------------------------------------------------ STATS */
/* Doom's intermission numbers, read where the game already keeps them: mpStat[k] counted as the
   level ran, mpTotal[] counted when the level was placed (MPRULES.C), the clock doomLevelTime,
   which does not move while the game is paused because no tic runs.  SECRETS is new with this
   build (DOOM_GAME.C DOOM_SECRET_BIT); a map whose total is 0 shows "--" rather than a division. */
static void statPct(char *t,int n,int total)
{if (total>0)
    sprintf(t,"%d / %d   %d%%",n,total,(n*100)/total);
 else
    sprintf(t,"%d / %d    --",n,total);
}

static void statTime(char *t,int seconds)
{sprintf(t,"%d:%02d",seconds/60,seconds%60);
}

/* one line: its name on the left, its value against STAT_RX */
static void statRow(int i,const char *name,const char *value)
{int y=STAT_Y0+i*ITEM_PITCH;
 text(STAT_X,y,name,red,1);
 textRight(STAT_RX,y,value,red,1);
}

static void drawStats(void)
{char t[48],u[48];
 int l=currentState.currentLevel,s=doomLevelTime/35,k,y;
 pageStart("STATS",STAT_X,1,1);         /* no skull: there is nothing to choose here */
 if (l>=0 && l<DOOM_NMLEVELS)
    text(-1,STAT_NAME_Y,doomMapTitles[l],red,0);
 if (mpPlayers>1)
    {/* a row per player, small: P1..P4 against the same three totals */
     text(112,STAT_ROW_Y0,"KILLS",red,0);
     text(168,STAT_ROW_Y0,"ITEMS",red,0);
     text(224,STAT_ROW_Y0,"SECRET",red,0);
     if (mpCompetitive())
	   text(280,STAT_ROW_Y0,"FRAGS",red,0);
     for (k=0;k<mpPlayers;k++)
	   {y=STAT_ROW_Y0+(k+1)*STAT_ROW_P;
	    sprintf(t,"PLAYER %d",k+1);
	    text(STAT_X,y,t,red,0);
	    statPct(t,mpStat[k].kills,mpTotal[0]);   textRight(160,y,t,red,0);
	    statPct(t,mpStat[k].items,mpTotal[1]);   textRight(216,y,t,red,0);
	    statPct(t,mpStat[k].secrets,mpTotal[2]); textRight(276,y,t,red,0);
	    if (mpCompetitive())
	       {sprintf(t,"%d",mpStat[k].frags);
	        textRight(312,y,t,red,0);
	       }
	   }
     y=STAT_ROW_Y0+(mpPlayers+2)*STAT_ROW_P;
     statTime(t,s);
     sprintf(u,"TIME  %s",t);
     text(STAT_X,y,u,red,0);
     sprintf(u,"SKILL  %s",doom_skillName(mpSkill));
     textRight(STAT_RX,y,u,red,0);
    }
 else
    {k=mpCur;
     statPct(t,mpStat[k].kills,mpTotal[0]);   statRow(0,"KILLS",t);
     statPct(t,mpStat[k].items,mpTotal[1]);   statRow(1,"ITEMS",t);
     statPct(t,mpStat[k].secrets,mpTotal[2]); statRow(2,"SECRETS",t);
     statTime(t,s);
     if (l>=0 && l<DOOM_NMLEVELS)
	   {statTime(u,parTime[l]);
	    strcat(t,"   PAR ");
	    strcat(t,u);
	   }
     statRow(3,"TIME",t);
     statRow(4,"SKILL",doom_skillName(mpSkill));
     if (mpMode==MP_HORDE)
	   {sprintf(t,"%d",doom_hordeWave());
	    statRow(5,"WAVE",t);
	   }
    }
 text(-1,HELP_Y,"B BACK   START RESUME",red,0);
}

/* -> 1 = START: the pause closes; 0 = back to the first page */
static int stats(void)
{Uint16 hit;
 drawStats();
 while (1)
    {hit=nextField();
     if (hit&PER_DGT_S)
	   return 1;
     if (hit&(PER_DGT_A|PER_DGT_B|PER_DGT_C))
	   return 0;
    }
}

/* ------------------------------------------------------------------------ WEAPONS */
/* One line per weapon the player owns, in slot order, with Doom's damage and, beside the chosen
   one, the pickup as the WAD draws it (pause_art.h, in the overlay).  The shareware WAD has no
   pistol, plasma or BFG pickup: those lines simply show no picture. */
static int wpnList(signed char *slot)
{int w,n=0;
 for (w=0;w<WPN_NM;w++)
    if (doomPlayer.weaponOwned & (1<<w))
       slot[n++]=(signed char)w;
 return n;
}

static void wpnPicture(int w)
{int x,y,pw,ph;
 const unsigned char *s;
 for (y=0;y<WPN_PICH;y++)               /* the box first: the previous weapon's picture goes */
    {volatile unsigned char *d=HALF+((WPN_PICY+y)<<9)+WPN_PICX;
     for (x=0;x<WPN_PICW;x++)
	   d[x]=0;
    }
 if (w<0 || w>=PAUSE_PIC_NM || !(s=pausePic[w]))
    return;
 pw=pausePicW[w];
 ph=pausePicH[w];
 if (pw>WPN_PICW || ph>WPN_PICH)
    return;                             /* a WAD with a bigger pickup than the box: none shown */
 for (y=0;y<ph;y++)
    {volatile unsigned char *d=HALF+((WPN_PICY+((WPN_PICH-ph)>>1)+y)<<9)+WPN_PICX+((WPN_PICW-pw)>>1);
     for (x=0;x<pw;x++,s++)
	   if (*s)
	      d[x]=*s;
    }
}

static void drawWeapons(signed char *slot,int n)
{char t[64],u[16];
 int i,w,a;
 pageStart("WEAPONS",WPN_X,n,(1u<<n)-1);
 for (i=0;i<n;i++)
    {w=slot[i];
     lineY[i]=WPN_Y0+i*ITEM_PITCH;
     text(WPN_X,lineY[i],wpnName[w],w==doomPlayer.readyWeapon? red: grey,1);
     textRight(WPN_RX,lineY[i],wpnDamage[w],red,0);
    }
 t[0]=0;
 for (a=0;a<DOOM_NUMAMMO;a++)
    {sprintf(u,"%s %d/%d  ",ammoName[a],doomPlayer.ammo[a],doomPlayer.maxAmmo[a]);
     strcat(t,u);
    }
 text(-1,WPN_AMMO_Y,t,red,0);
 text(-1,HELP_Y,"THE ONE IN HAND IS LIT   B BACK",red,0);
 pageSkull();
}

static int weapons(void)
{signed char slot[WPN_NM];
 int n=wpnList(slot),shown=-1,i;
 Uint16 hit;
 sel=0;
 for (i=0;i<n;i++)                      /* the page opens on the weapon in hand */
    if (slot[i]==doomPlayer.readyWeapon)
	   sel=i;
 drawWeapons(slot,n);
 while (1)
    {if (shown!=sel)
	   wpnPicture(slot[shown=sel]);
     hit=step();
     if (hit&PER_DGT_S)
	   return 1;
     if (hit&(PER_DGT_A|PER_DGT_B|PER_DGT_C))
	   return 0;
     navigate(hit);
    }
}

/* ------------------------------------------------------------------------ SAVE / LOAD */
/* A save is the START of the level being played (DOOM_SAVE.C): map, skill, health, armour,
   weapons, ammo, backpack -- a PlayStation Doom password, in other words.  The footer says so, and
   each slot names a level rather than a place in one.
   The BUP library's 24 KB come from the memory the overlay loader left free, so the level's own
   pool is never asked for them; they are given back by doing nothing, because the next image
   rebuilds that buffer from scratch anyway. */
static int savLoadPage;                 /* 0 = SAVE, 1 = LOAD */
static int savDevice;

static void savLine(int slot,char *big,char *sub)
{const DoomSaveRec *r=doom_saveSlot(slot);
 int t;
 if (!r)
    {sprintf(big,"%d  EMPTY",slot+1);
     sub[0]=0;
     return;
    }
 sprintf(big,"%d  %s  %s",slot+1,doom_levelLabel(r->level),doom_skillName(r->skill));
 t=r->gameSeconds;
 /* these are the values the level BEGAN with, not the ones in hand: that is what a save is
    here, and a player who has since found a soulsphere must not read it as a mistake. */
 sprintf(sub,"STARTED %d HP  %d AP  %d:%02d   %02d/%02d %02d:%02d",
	   r->health,r->armorPoints,t/60,t%60,r->day,r->month,r->hour,r->min);
}

static const char *savDeviceName(int d)
{return d? "< CARTRIDGE >": "< INTERNAL MEMORY >";
}

static void drawSave(void)
{char big[48],sub[64];
 int i;
 pageStart(savLoadPage? "LOAD GAME": "SAVE GAME",SAV_X,DOOM_NMSAVES,(1u<<DOOM_NMSAVES)-1);
 if (doomSaveDevices>1)
    text(-1,SAV_DEV_Y,savDeviceName(savDevice),red,0);
 else
    text(-1,SAV_DEV_Y,"A SLOT HOLDS THE LEVEL AS IT BEGAN",red,0);
 for (i=0;i<DOOM_NMSAVES;i++)
    {savLine(i,big,sub);
     lineY[i]=SAV_Y0+i*SAV_PITCH;
     text(SAV_X,lineY[i],big,doom_saveSlotUsed(i)? red: grey,1);
     if (sub[0])
	   text(SAV_X,lineY[i]+SAV_SUB,sub,red,0);
    }
 text(-1,SAV_FOOT_Y,savLoadPage? "A LOAD RESTARTS THAT LEVEL FROM ITS BEGINNING":
								   "SAVES THE START OF THIS LEVEL",red,0);
 text(-1,HELP_Y,doomSaveDevices>1? "A CHOOSE   L R SAVE/LOAD   LEFT RIGHT DEVICE   B BACK":
									       "A CHOOSE   L R SAVE/LOAD   B BACK",red,0);
 pageSkull();
}

/* read the chosen device's file into the list.  The library is already open. */
static void savRefresh(void)
{doom_saveRead(savDevice);
}

/* -> 0 the slot was not written, 1 it was */
static int savStore(int slot)
{char t[64];
 int shortBy=0,r=doom_saveRoom(savDevice,&shortBy);
 if (r==BUP_UNFORMAT)
    {sprintf(t,"%s IS NOT FORMATTED",savDevice? "THE CARTRIDGE": "THE INTERNAL MEMORY");
     if (!confirmSub(t,"FORMAT IT?"))
	   return 0;
     if (doom_saveFormat(savDevice))
	   {notice("FORMAT FAILED",NULL);
	    return 0;
	   }
     savRefresh();
    }
 else if (r==BUP_NOT_ENOUGH_MEMORY)
    {sprintf(t,"FREE %d BLOCK%s WITH THE SATURN MEMORY MANAGER",
	    shortBy,(shortBy==1)? "": "S");
     notice("NOT ENOUGH SPACE",t);
     return 0;
    }
 else if (r)
    {notice("NO BACKUP MEMORY",NULL);
     return 0;
    }
 if (doom_saveSlotUsed(slot))
    {char sub[64];
     savLine(slot,t,sub);                /* two buffers: savLine writes 48 and 64 bytes */
     if (!confirmSub(t,"OVERWRITE IT?"))
	   return 0;
    }
 clearRows(0,PAUSE_ROWS);
 text(-1,100,"SAVING",red,1);
 r=doom_saveStore(savDevice,slot);
 if (r)
    {sprintf(t,"BACKUP ERROR %d",r);
     notice("SAVE FAILED",t);
     savRefresh();
     return 0;
    }
 doom_saveSetDevice(savDevice);
 return 1;
}

/* -> PAUSE_RESUME (back to the menu), PAUSE_RESTART (a record was applied), or -1 = START */
static int saveLoad(char *freeBase,char *freeEnd)
{Uint16 hit;
 int r=-2;
 if (freeEnd-freeBase<SAV_LIB+SAV_WORK+16)
    {notice("NOT ENOUGH MEMORY",NULL);   /* never: doorwayCache has 66 000 bytes */
     return PAUSE_RESUME;
    }
 freeBase=(char *)(((int)freeBase+3)&~3);
 doom_bupOpen(freeBase,freeBase+SAV_LIB);
 savDevice=doom_saveLoadDevice();
 if (savDevice<0 || !doom_saveDevicePresent(savDevice))
    {savDevice=doom_saveDevicePresent(1)? 1: 0;
     if (!doom_saveDevicePresent(savDevice))
	   {doom_bupClose();
	    notice("NO BACKUP MEMORY FOUND",NULL);
	    return PAUSE_RESUME;
	   }
    }
 savLoadPage=0;
 savRefresh();
 sel=0;
 drawSave();
 while (r==-2)
    {hit=step();
     if (hit&PER_DGT_S)
	   r=-1;
     else if (hit&PER_DGT_B)
	   r=PAUSE_RESUME;
     else if (hit&(PER_DGT_TL|PER_DGT_TR))
	   {savLoadPage=!savLoadPage;
	    doom_playerSound(sfx_swtchn);
	    drawSave();
	   }
     else if ((hit&(PER_DGT_L|PER_DGT_R)) && doomSaveDevices>1)
	   {savDevice=!savDevice;
	    savRefresh();
	    doom_playerSound(sfx_pstop);
	    drawSave();
	   }
     else if (navigate(hit))
	   ;
     else if (hit&(PER_DGT_A|PER_DGT_C))
	   {doom_playerSound(sfx_pistol);
	    if (!savLoadPage)
	       {if (savStore(sel))
		  notice("SAVED",NULL);
	        savRefresh();
	        drawSave();
	       }
	    else if (!doom_saveSlotUsed(sel))
	       drawSave();                     /* an empty slot: nothing to load */
	    else
	       {char t[48],u[64];
	        savLine(sel,t,u);
	        if (confirmSub(t,"LOAD IT?  THIS LEVEL IS LOST"))
		  {doom_saveApply(doom_saveSlot(sel));
		   doom_saveSetDevice(savDevice);
		   r=PAUSE_RESTART;
		  }
	        else
		  drawSave();
	       }
	   }
    }
 doom_bupClose();
 return r;
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

/* SHADOWS: the blob under a thing and the spectre.  Both are a VDP1 composition mode, so they
   are one page: GRAIN is the shadow calculation on one pixel in two (grain, and what is behind
   shows through the other half), HALF the only mode that keeps the monster's own shading. */
static int compoIndex(const unsigned char *list,int nm,int mode)
{int i;
 for (i=0;i<nm;i++)
    if (list[i]==mode)
       return i;
 return 0;
}

static void shadowLine(int r,char *text)
{switch (r)
    {case SHD_MODE:    sprintf(text,"SHADOW  %s",compoName[shadowMode]); break;
     case SHD_SIZE:    sprintf(text,"SIZE  %d PCT",shadowPct); break;
     case SHD_SPECTRE: sprintf(text,"SPECTRE  %s",compoName[spectreMode]); break;
     default:          strcpy(text,"BACK"); break;
    }
}

static void shadowAdjust(int r,int d)
{int i;
 switch (r)
    {case SHD_MODE:
	i=compoIndex(shadowList,SHADOW_NM,shadowMode)+d;
	if (i<0) i=SHADOW_NM-1;
	if (i>=SHADOW_NM) i=0;
	shadowMode=shadowList[i];
	compoPlanOf(&shadowPlan,shadowMode);
	break;
     case SHD_SIZE:
	setShadowPct(shadowPct+10*d);
	break;
     case SHD_SPECTRE:
	i=compoIndex(spectreList,SPECTRE_NM,spectreMode)+d;
	if (i<0) i=SPECTRE_NM-1;
	if (i>=SPECTRE_NM) i=0;
	spectreMode=spectreList[i];
	compoPlanOf(&spectrePlan,spectreMode);
	break;
    }
}

static void drawShadows(void)
{char t[40];
 int r;
 pageStart("SHADOWS",ITEM_X,SHD_NM,(1<<SHD_NM)-1);
 for (r=0;r<SHD_NM;r++)
    {shadowLine(r,t);
     text(ITEM_X,lineY[r]=ITEM_Y0+r*ITEM_PITCH,t,red,1);
    }
 text(-1,LIT_NOTE_Y,"CHANGES SHOW WHEN YOU RESUME",red,0);
 text(-1,LIT_HELP_Y,"LEFT RIGHT CHANGE   B BACK",red,0);
 pageSkull();
}

static int shadows(void)
{char t[40];
 int r=-1,d,hold=0;
 Uint16 hit;
 sel=SHD_MODE;
 drawShadows();
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
     else if ((hit&PER_DGT_B) || ((hit&(PER_DGT_A|PER_DGT_C)) && sel==SHD_BACK))
	r=0;
     else if (!navigate(hit))
	{if (hit&(PER_DGT_A|PER_DGT_C))
	    d=1;
	 if (d && sel!=SHD_BACK)
	    {shadowAdjust(sel,d);
	     doom_playerSound(sfx_stnmov);
	     shadowLine(sel,t);
	     lineAgain(sel,t);
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
     case OPT_SHADOWS:  return "SHADOWS";
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
	     case OPT_SHADOWS:
	     case OPT_LIGHTS:
		if (i==OPT_CONTROLS? controls(): i==OPT_SHADOWS? shadows(): lights())
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

/* ------------------------------------------------------------------------ lent to the MAP */
/* PMAP.C rasterises its own pixels, but it borrows the letters, the field and the buttons rather
   than carrying a second copy of them in the same overlay (pmap.h). */
void pause_half(int h)
{halfOff=h? PAUSE_HALF_BYTES: 0;
}

void pause_text(int x,int y,const char *t,int big,int dim)
{text(x,y,t,dim? grey: red,big);
}

Uint16 pause_field(void)
{return nextField();
}

Uint16 pause_held(void)
{return held;
}

/* ------------------------------------------------------------------------ the entry */
int pause_main(int k,char *freeBase,char *freeEnd)
{int r=-1,i;
 Uint16 hit;
 pad=k;
 field=0;
 sel=ITEM_RESUME;
 lastField=vtimer;
 held=lastInputSampleP[k];              /* the START that opened it is not a press */
 /* GCC14: the split sky's own bytes in B0 are NOT saved -- MPSKY.C makes them again on the way
    out (mpSkyRepaintB0).  Saving them wanted 46208 bytes of the memory the loader leaves here,
    which is 43105: the guard that stood here fired on EVERY split game, waited for START to be
    released and returned, so the menu never opened -- the game froze for the read and carried
    on (reported 2026-09-23).  The map browser gets those 46 KB of scratch instead. */
 if (mpSkyOn)
    {vblankIn();                        /* the mist and the clouds go before the menu lands on them */
     VDP2R(0x20)=bufReg(0x20)&~0x0003;
    }
 pmapFree=freeBase;
 pmapEnd=freeEnd;
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
	 else if (sel==ITEM_MAP)
	    {if (doom_pmapBrowse(pmapFree,pmapEnd))
		r=PAUSE_RESUME;
	     else
		{/* the browser left the map on the OTHER half: this page is painted unseen,
		    and only then does the screen scroll back to it -- no flash of the map. */
		 doom_playerSound(sfx_swtchn);
		 sel=ITEM_MAP;                 /* before drawMain: it ends with the skull */
		 drawMain();
		 vblankIn();
		 VDP2R(PAUSE_SCYIN0)=0;
		}
	    }
	 else if (sel==ITEM_STATS || sel==ITEM_WEAPONS)
	    {int back=sel;
	     if (sel==ITEM_STATS? stats(): weapons())
		r=PAUSE_RESUME;
	     else
		{/* GCC14: the line we came from is set BEFORE the page is drawn.  A submenu owns
		    `sel` while it runs -- weapons() opens on the weapon in hand and navigates with
		    it -- and drawMain ends with pageSkull, so the old order painted a skull on the
		    submenu's last line and then a second one on the real line, without erasing the
		    first (reported 2026-09-23). */
		 doom_playerSound(sfx_swtchn);
		 sel=back;
		 drawMain();
		}
	    }
	 else if (sel==ITEM_SAVE)
	    {int a=saveLoad(pmapFree,pmapEnd);
	     if (a<0)
		r=PAUSE_RESUME;				   /* START from the page */
	     else if (a==PAUSE_RESTART)
		r=PAUSE_RESTART;			   /* a record was applied: the level it names starts */
	     else
		{doom_playerSound(sfx_swtchn);
		 sel=ITEM_SAVE;                /* before drawMain: it ends with the skull */
		 drawMain();
		}
	    }
	 else if (sel==ITEM_RESTART)
	    {if (confirm("RESTART THIS LEVEL?"))
		{/* the level over again with the arsenal it began with: DOOM_PLAYER.C armed
		    every player's stash, SRUINS.C's main case 7 starts CFG_START_LEVEL. */
		 mpStartLevel=currentState.currentLevel;
		 doom_playerRestart();
		 r=PAUSE_RESTART;
		}
	     else
		{doom_playerSound(sfx_swtchx);
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
 if (mpSkyOn)
    {vblankIn();
     VDP2R(0x20)=bufReg(0x20)&~0x0003;  /* menu and veil off before the sky comes back */
     mpSkyRepaintB0();
    }
 setRegs(0);
 if (r==PAUSE_RESUME)
    {doom_playerSound(sfx_swtchx);
     restoreSoundState();
    }
 return r;
}

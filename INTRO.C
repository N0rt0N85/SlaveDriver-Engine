#include <machine.h>

#include <libsn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sega_spr.h"
#include "sega_scl.h"
#include "sega_int.h"
#include "sega_mth.h"
#include "sega_sys.h"
#include "sega_dbg.h"
#include "sega_per.h"
#include "sega_cdc.h"
#include "sega_snd.h"
#include "sega_gfs.h"

#include "pic.h"
#include "file.h"
#include "util.h"
#include "print.h"
#include "v_blank.h"
#include "art.h"
#include "local.h"
#include "sound.h"
#include "menu.h"
#include "bup.h"
#include "spr.h"
#include "mov.h"

#define MAXNMPICS 20
static unsigned short *picPals[MAXNMPICS];
static unsigned int *picDatas[MAXNMPICS];

static void setupVDP2(void)
{static Uint16 cycle[]=
    {0x44ee, 0xeeee,
     0x44ee, 0xeeee,
     0x55ee, 0xeeee,
     0x55ee, 0xeeee};
 SclConfig scfg;
 SclVramConfig vcfg;
 SCL_InitVramConfigTb(&vcfg);
 vcfg.vramModeA=OFF;
 vcfg.vramModeB=OFF;
 vcfg.vramA0=SCL_NON;
 vcfg.vramB0=SCL_NON;
 SCL_SetVramConfig(&vcfg);
 SCL_SetCycleTable(cycle);

 /* setup VDP2Sprite screen */
 SCL_InitConfigTb(&scfg);
 scfg.dispenbl=ON;
 scfg.bmpsize=SCL_BMP_SIZE_512X256;
 scfg.coltype=SCL_COL_TYPE_256;
 scfg.datatype=SCL_BITMAP;
 scfg.mapover=SCL_OVER_0;
 scfg.plate_addr[0]=0;
 scfg.patnamecontrl=0;
 SCL_SetConfig(SCL_NBG0, &scfg);
 scfg.plate_addr[0]=256*1024;
 SCL_SetConfig(SCL_NBG1, &scfg);
 SCL_SET_N0CAOS(0);
 SCL_SET_N1CAOS(1);
 SCL_SetPriority(SCL_NBG0,2);
 SCL_SetPriority(SCL_NBG1,1);
 SCL_SET_N0CCEN(1);
}

static void loadVDPPic(int picNm,int bankNm)
{int i,width,height,x,y,yoffs;

 for (i=0;i<256;i++)
    POKE_W(SCL_COLRAM_ADDR+(bankNm?512:0)+(i<<1),picPals[picNm][i]);


 width=picDatas[picNm][0];
 height=picDatas[picNm][1];
 yoffs=(240-height)>>1;

 SCL_SetWindow(bankNm?SCL_W0:SCL_W1,
	       0,
	       bankNm?(SCL_NBG1|SCL_NBG2|SCL_NBG3):
	              (SCL_NBG0|SCL_NBG2|SCL_NBG3),
	       0xfffffff,0,yoffs,
	       320,yoffs+height-1);

 for (y=0;y<yoffs;y++)
    for (x=0;x<(width>>2);x++)
       POKE(SCL_VDP2_VRAM+(y<<9)+(x<<2)+(bankNm?1024*256:0),0);
 i=2;
 for (y=0;y<height;y++)
    for (x=0;x<(width>>2);x++)
       POKE(SCL_VDP2_VRAM+((y+yoffs)<<9)+(x<<2)+(bankNm?1024*256:0),
	    picDatas[picNm][i++]);
 for (y=height+yoffs;y<240;y++)
    for (x=0;x<(width>>2);x++)
       POKE(SCL_VDP2_VRAM+(y<<9)+(x<<2)+(bankNm?1024*256:0),0);
}

static void fadeUp(void)
{int f;
 for (f=-255;f<=0;f+=8)
    {SCL_SetColOffset(SCL_OFFSET_A,SCL_NBG1|SCL_NBG0,f,f,f);
     SCL_DisplayFrame();
    }
 SCL_SetColOffset(SCL_OFFSET_A,SCL_NBG1|SCL_NBG0,0,0,0);
}

#define CANCELKEYS (PER_DGT_A|PER_DGT_B|PER_DGT_C|PER_DGT_X|PER_DGT_Y|PER_DGT_Z|PER_DGT_S)

#ifndef JAPAN
static char *buttonNames[8]={"A","B","C","X","Y","Z","TL","TR"};
#else
static char *buttonNames[8]={"\001","\002","\003","\004","\005",
				"\006","\007\010","\007\011"};
#endif

static void remapMenu(int hx,int hy,int lx,int ly)
{int menuSel;
 int i,len,button,temp;
 char btext[8][40];
 dlg_clear();
#ifndef JAPAN
 dlg_addFontText(hx,hy,320,2,getText(LB_OPTIONMENU,4));
#else
 dlg_addFontText(hx,hy,320,3,getText(LB_OPTIONMENU,4));
#endif

 for (i=0;i<8;i++)
    sprintf(btext[i],
#ifdef JAPAN
	    "%s\012%s",
#else
	    "%s- %s",
#endif
	    buttonNames[(int)controllerConfig[i]],
	    getText(LB_ACTIONNAMES,(i<6)?i:i+2));
#ifndef JAPAN
 for (i=0;i<8;i++)
    dlg_addBigWavyButton(i,(i&1)?10:-150,(i/2)*20,btext[i]);
#else
 for (i=0;i<8;i++)
    dlg_addBigWavyJapaneseButton(i,-150,i*22-100,btext[i]);
#endif
 len=getStringWidth(2,getText(LB_OPTIONMENU,5));
#ifndef JAPAN
 dlg_addBigWavyButton(i,-len>>1,(i/2)*20,getText(LB_OPTIONMENU,5));
#else
 dlg_addBigWavyJapaneseButton(i,-len>>1,(i/2)*20,getText(LB_OPTIONMENU,5));
#endif
 dlg_setupSlideIn();
 dlgItem[0].x1=dlgItem[0].x2; dlgItem[0].y1=dlgItem[0].y2;
 dlg_runSlideIn();
 while (1)
    {for (i=0;i<8;i++)
	sprintf(btext[i],
#ifdef JAPAN
		"%s\012%s",
#else
		"%s- %s",
#endif
		buttonNames[(int)controllerConfig[i]],
		getText(LB_ACTIONNAMES,(i<6)?i:i+2));
     menuSel=dlg_run(0,1,MENUMOVE_FREE);
     if (menuSel==8)
	break;
     for (button=0;button<8;button++)
	if (lastUsedSelButton==buttonMasks[button])
	   break;
     if (button==8)
	continue;
     for (i=0;i<8;i++)
	if (controllerConfig[i]==button)
	   break;
     if (i==8)
	continue; /* don't think this can happen, but let's be safe */
     temp=controllerConfig[i];
     controllerConfig[i]=controllerConfig[menuSel];
     controllerConfig[menuSel]=temp;
    }
 dlg_setupSlideOut();
 dlgItem[0].x2=lx; dlgItem[0].y2=ly;
 dlg_runSlideIn();
}

static void optionMenu(int hx,int hy,int lx,int ly)
{int menuSel=-1;
 enable_stereo=!(systemMemory & PER_MSK_STEREO);

 bigReset:
 dlg_clear();
#ifndef JAPAN
 dlg_addBigWavyButton(0,0,-24+30,getText(LB_OPTIONMENU,4));
 dlg_addBigWavyButton(1,0,-24+50,getText(LB_OPTIONMENU,0+!enable_stereo));
 dlg_addBigWavyButton(2,0,-24+70,getText(LB_OPTIONMENU,2+!enable_music));
 dlg_addBigWavyButton(3,0,-24+90,getText(LB_OPTIONMENU,5));
#else
 dlg_addBigWavyJapaneseButton(0,0,-24+30,
			      getText(LB_OPTIONMENU,4));
 dlg_addBigWavyJapaneseButton(1,0,-24+55,
			      getText(LB_OPTIONMENU,0+!enable_stereo));
 dlg_addBigWavyJapaneseButton(2,0,-24+80,
			      getText(LB_OPTIONMENU,2+!enable_music));
 dlg_addBigWavyJapaneseButton(3,0,-24+105,
			      getText(LB_OPTIONMENU,5));
#endif
 dlg_centerStuff();
#ifndef JAPAN
 dlg_addFontText(hx,hy,100,2,getText(LB_MAINMENU,2));
 dlg_addFontText(0,400,320,2,"");
 dlg_addRect(-60,-7,120,1,RGB(31,31,31));
#else
 dlg_addFontText(hx,hy,300,3,getText(LB_MAINMENU,2));
 dlg_addFontText(0,400,320,3,"");
 dlg_addRect(-60,-2,120,1,RGB(31,31,31));
#endif
 dlg_setupSlideIn();
 if (menuSel!=-1)
    {dlg_selectButton(menuSel);
     dlgItem[menuSel].x1=dlgItem[menuSel].x2;
     dlgItem[menuSel].y1=dlgItem[menuSel].y2;
    }
 else
    {dlgItem[4].x1=dlgItem[4].x2;
     dlgItem[4].y1=dlgItem[4].y2;
    }
 dlg_runSlideIn();

 while (1)
    {menuSel=dlg_run(0,1,MENUMOVE_VERT);
     switch (menuSel)
	{case 0:
	    dlg_setupSlideOut();
	    dlgItem[0].x2=dlgItem[0].x1;
#ifndef JAPAN
	    dlgItem[0].y2=-26;
#else
	    dlgItem[0].y2=-140;
#endif
	    dlgItem[5].x2=0; dlgItem[5].y2=400;
	    dlgItem[5].x1=0; dlgItem[5].y1=400;
	    dlg_runSlideIn();
	    remapMenu(dlgItem[0].x2,dlgItem[0].y2,
		      dlgItem[0].x1,dlgItem[0].y1);
	    goto bigReset;
	    break;
	 case 1:
	    enable_stereo=!enable_stereo;
	    dlgItem[5].text=dlgItem[1].text;
	    dlgItem[1].text=getText(LB_OPTIONMENU,0+!enable_stereo);
	    dlgItem[5].xp=dlgItem[1].xp; dlgItem[5].yp=dlgItem[1].yp;
	    dlg_centerStuff();
	    dlg_setupNoSlide();
	    dlgItem[5].x2+=250;
	    dlgItem[1].x1+=-250;
	    dlg_runSlideIn();
	    break;
	 case 2:
	    enable_music=!enable_music;
	    dlgItem[5].text=dlgItem[2].text;
	    dlgItem[2].text=getText(LB_OPTIONMENU,2+!enable_music);
	    dlgItem[5].xp=dlgItem[2].xp; dlgItem[5].yp=dlgItem[2].yp;
	    dlg_centerStuff();
	    dlg_setupNoSlide();
	    dlgItem[5].x2+=-300;
	    dlgItem[2].x1+=300;
	    dlg_runSlideIn();
	    break;
	   }
     if (menuSel==3)
	break;
    }
 dlgItem[5].yp=400;
 dlg_setupSlideOut();
 dlgItem[4].x2=lx; dlgItem[4].y2=ly;
 dlgItem[5].x2=0; dlgItem[5].y2=400;
 dlgItem[5].x1=0; dlgItem[5].y1=400;
 fadeEnd=0;
 fadeDir=5;
 dlg_runSlideIn();
 PER_SMPC_SET_SM((systemMemory & ~PER_MSK_STEREO) |
		 (enable_stereo?0:PER_MSK_STEREO));
}

static int loadMenu(int hx,int hy,int lx,int ly)
{int menuSel=-1;
 int i;
 const char *emptyText=getText(LB_BUP,2);
#ifdef JAPAN
 emptyText="EMPTY";
#endif
 dlg_clear();
#ifndef JAPAN
 dlg_addFontText(hx,hy,250,2,getText(LB_MAINMENU,1));
 dlg_addRect(-80,-65,160,1,RGB(31,31,31));
#else
 dlg_addFontText(hx,hy,250,3,getText(LB_MAINMENU,1));
 dlg_addRect(-80,-60,160,1,RGB(31,31,31));
#endif

 for (i=0;i<6;i++)
    if (bup_getGameData(i))
       dlg_addGameButton(-150,-84+30+20*i,i);
    else
       dlg_addFontString(-getStringWidth(2,emptyText)/2,
			 -84+30+20*i,2,(char *)emptyText);

#ifndef JAPAN
 dlg_addBigWavyButton(100,0,-84+30+20*7,getText(LB_PROMPTS,1));
#else
 dlg_addBigWavyJapaneseButton(100,0,-84+20+20*7,getText(LB_PROMPTS,1));
#endif
 dlg_centerStuff();
 dlg_setupSlideIn();
 if (menuSel!=-1)
    {dlg_selectButton(menuSel);
     dlgItem[menuSel].x1=dlgItem[menuSel].x2;
     dlgItem[menuSel].y1=dlgItem[menuSel].y2;
    }
 else
    {dlgItem[0].x1=dlgItem[0].x2;
     dlgItem[0].y1=dlgItem[0].y2;
    }
 dlg_runSlideIn();
 while (1)
    {menuSel=dlg_run(0,1,MENUMOVE_VERT);
     if (menuSel==100)
	break;
     if (bup_loadGame(menuSel))
	break;
     return 1;
    }
 dlg_setupSlideOut();
 dlgItem[0].x2=lx; dlgItem[0].y2=ly;
 fadeEnd=0;
 fadeDir=5;
 dlg_runSlideIn();
 return 0;
}

static int newMenu(int hx,int hy,int lx,int ly)
{int menuSel=-1;
 const char *emptyText=getText(LB_BUP,2);
 int i;
#ifdef JAPAN
 emptyText="EMPTY";
#endif
 bigReset:
 dlg_clear();
#ifndef JAPAN
 dlg_addFontText(hx,hy,250,2,getText(LB_MAINMENU,0));
 dlg_addRect(-80,-65,160,1,RGB(31,31,31));
#else
 dlg_addFontText(hx,hy,250,3,getText(LB_MAINMENU,0));
 dlg_addRect(-80,-60,160,1,RGB(31,31,31));
#endif
 for (i=0;i<6;i++)
    if (bup_getGameData(i))
       dlg_addGameButton(-150,-84+30+20*i,i);
    else
       dlg_addBigWavyButton(i,-getStringWidth(2,emptyText)/2,
			    -84+30+20*i,(char *)emptyText);
#ifndef JAPAN
 dlg_addBigWavyButton(100,0,-84+30+20*7,getText(LB_PROMPTS,1));
#else
 dlg_addBigWavyJapaneseButton(100,0,-84+20+20*7,getText(LB_PROMPTS,1));
#endif

 dlg_centerStuff();
 dlg_setupSlideIn();
/* dlg_setupSlideUp();*/
 if (menuSel==-1)
    {dlgItem[0].x1=dlgItem[0].x2; dlgItem[0].y1=dlgItem[0].y2;
    }
 dlg_runSlideIn();
 while (1)
    {menuSel=dlg_run(0,1,MENUMOVE_VERT);
     if (menuSel==100)
	break;
     if (bup_newGame(menuSel))
	goto bigReset;
     return 1;
    }
 dlg_setupSlideOut();
 dlgItem[0].x2=lx; dlgItem[0].y2=ly;
 fadeEnd=0;
 fadeDir=5;
 dlg_runSlideIn();
 return 0;
}


void playIntro(void)
{int i;
 /* GCC14 (BOOTPROBE=1): playIntro runs with the display off, so a hang in here shows as a
    black screen.  These checkpoints paint it instead -- see the table in PORTING_NOTES. */
 BOOT_PROBE(0x03e0);	/* green  = playIntro entered */
 displayEnable(0);
 mem_init();
 initSound();
 setupVDP2();
 EZ_initSprSystem(500,8,500,
		  240,0x0000);
#ifndef JAPAN
 i=initFonts(0,7);
 initPicSystem(i,((int []){50,0,0,0,0,-1}));
#else
 i=initFonts(0,4);
 initPicSystem(i,((int []){40,0,0,0,0,70,-1}));
#endif
 BOOT_PROBE(0x7c00);	/* blue   = sound, VDP2, sprites, fonts and pics set up */
 stopCD();
 BOOT_PROBE(0x03ff);	/* yellow = stopCD returned */
 {
#ifndef JAPAN
  int fd=fs_open("+INTRO.PCS");
#else
  int fd=fs_open("+JINTRO.PCS");
#endif
  loadPicSet(fd,picPals,picDatas,MAXNMPICS);
  loadPicSetAsPics(fd,TILE16BPP);
  loadSound(fd);
  loadSound(fd);
  fs_close(fd);
 }
 BOOT_PROBE(0x7fe0);	/* cyan   = INTRO.PCS loaded */
#ifdef JAPAN
 loadJapanFontPics();
#endif
 SCL_Open(SCL_NBG0); SCL_MoveTo(0,0,0); SCL_Close();
 SCL_Open(SCL_NBG1); SCL_MoveTo(0,0,0); SCL_Close();

 SCL_SetColOffset(SCL_OFFSET_A,SCL_NBG1|SCL_NBG0,-255,-255,-255);
 SCL_SetColOffset(SCL_OFFSET_B,SCL_SP0,0,0,0);
 SCL_DisplayFrame();
 SCL_DisplayFrame();

 for (i=0;i<512*1024;i+=4)
    POKE(SCL_VDP2_VRAM+i,0);
 BOOT_PROBE(0x7c1f);	/* magenta = VDP2 VRAM cleared */

#ifndef PAL
 loadVDPPic(0,1);
 loadVDPPic(0,0);
#else
 loadVDPPic(1,1);
 loadVDPPic(1,0);
#endif
 BOOT_PROBE(0x01ff);	/* orange = title picture loaded */
 {int y,m,d,h,min;
  getDateTime(&y,&m,&d,&h,&min);
  if (m==12 && d==10 && y>16)
     {loadVDPPic(2,1);
      loadVDPPic(2,0);
     }
 }
 SCL_SetColMixRate(SCL_NBG0,31);
 SCL_SET_N0CCEN(0);
 BOOT_PROBE(0x7c0f);	/* purple = clock read (getDateTime returned) */
 if (enable_music)
    playCDTrack(titleMusic,1);
 BOOT_PROBE(0x7fff);	/* white  = music started; the title screen comes up next */

 Scl_s_reg.dispenbl|=0xf00;
 if (SclProcess==0)
    SclProcess=1;

 displayEnable(1);

 fadeUp();

 {int menuSel=-1;
  int canLoad;
  canLoad=bup_canLoadGame();
  SCL_SetFrameInterval(0xfffe);
  while (1)
     {dlg_clear();
      for (i=0;i<3;i++)
	 {int len;
	  if (i==1 && !canLoad)
	     {dlg_addText(10,10,10,"");
	      continue;
	     }
#ifndef JAPAN
	  len=getStringWidth(2,getText(LB_MAINMENU,i));
	  dlg_addBigWavyButton(i,-len>>1,20+30*i,getText(LB_MAINMENU,i));
#else
	  len=getStringWidth(3,getText(LB_MAINMENU,i));
	  dlg_addBigWavyJapaneseButton(i,-len>>1,20+30*i,
				       getText(LB_MAINMENU,i));
#endif
	 }

      {SaveState *s;
       int i,power;
       power=0;
       for (i=0;i<6;i++)
	  {s=bup_getGameData(i);
	   if (s && (s->gameFlags & GAMEFLAG_DOLLPOWERMODE))
	      power=1;
	  }
       if (power ||
	   (controllerConfig[0]==2 && controllerConfig[2]==0 &&
	    controllerConfig[3]==5 && controllerConfig[5]==3 &&
	    controllerConfig[6]==7 && controllerConfig[7]==6))
	  {int len=getStringWidth(2,"DEATH TANK");
	   dlg_addBigWavyButton(69,-len>>1,-10,"DEATH TANK");
	  }
      }

      dlg_setupSlideIn();
      if (menuSel!=-1)
	 {dlg_selectButton(menuSel);
	  dlgItem[menuSel].x1=dlgItem[menuSel].x2;
	  dlgItem[menuSel].y1=dlgItem[menuSel].y2;
	 }

      dlg_runSlideIn();
      menuSel=dlg_run(0,1,MENUMOVE_VERT);
      dlg_setupSlideOut();
      fadeEnd=-150;
      fadeDir=-5;
      switch (menuSel)
	 {case 0:
	     if (!bup_canSaveGame())
		{bup_initCurrentGame();
		 EZ_clearScreen();
		 return;
		}
	     dlgItem[0].x2=dlgItem[0].x1;
	     dlgItem[0].y2=-84;
	     dlg_runSlideIn();
	     if (newMenu(dlgItem[0].x2,dlgItem[0].y2,
			  dlgItem[0].x1,dlgItem[0].y1))
		{EZ_clearScreen();
		 return;
		}
	     break;
	  case 1:
	     dlgItem[1].x2=dlgItem[1].x1;
	     dlgItem[1].y2=-84;
	     dlg_runSlideIn();
	     if (loadMenu(dlgItem[1].x2,dlgItem[1].y2,
			  dlgItem[1].x1,dlgItem[1].y1))
		{EZ_clearScreen();
		 return;
		}
	     break;
	  case 2:
	     dlgItem[2].x2=dlgItem[2].x1;
	     dlgItem[2].y2=-26;
	     dlg_runSlideIn();
	     optionMenu(dlgItem[2].x2,dlgItem[2].y2,
			dlgItem[2].x1,dlgItem[2].y1);
	     break;
	  case 69:
	     if (lastInputSample==((~(PER_DGT_S|PER_DGT_C))&0xffff))
		{enableDoorReset=0;
		 while (lastInputSample==((~(PER_DGT_S|PER_DGT_C))&0xffff)) ;
		 fs_init();
		}
	     link("BONUS.BIN");
	     break;
	    }
     }
 }

}



#define NMSPERM 250
#define HISTORY 3
void teleportEffect(void)
{int i,j;
 int done;
 struct spermLocation
    {int spermX[NMSPERM][HISTORY],spermY[NMSPERM][HISTORY];
     char spermDead[NMSPERM];
    };
 extern struct spermLocation doorwayCache;
 struct spermLocation *s=(struct spermLocation *)&doorwayCache;
 unsigned short lcolor[]={RGB(31,31,31),RGB(15,15,15)};
 XyInt pos[2];
 fadePos=255;
 fadeEnd=0;
 fadeDir=-10;
 EZ_initSprSystem(1000,4,1000,239,RGB(0,0,0));
 for (i=0;i<NMSPERM;i++)
    {s->spermDead[i]=0;
     do
	{s->spermX[i][0]=((short)getNextRand())<<4;
	 s->spermY[i][0]=((short)getNextRand())<<4;
	}
     while (MTH_Mul(s->spermX[i][0],s->spermX[i][0])+
	    MTH_Mul(s->spermY[i][0],s->spermY[i][0])>(1<<22));

     for (j=1;j<HISTORY;j++)
	{s->spermX[i][j]=s->spermX[i][0];
	 s->spermY[i][j]=s->spermY[i][0];
	}
    }
 SCL_SetFrameInterval(1);
 do
    {EZ_openCommand();
     EZ_localCoord(160,120);
     done=1;
     for (i=0;i<NMSPERM;i++)
	{if (s->spermDead[i])
	    continue;
	 done=0;
	 for (j=0;j<HISTORY-1;j++)
	    {pos[0].x=f(s->spermX[i][j]); pos[0].y=f(s->spermY[i][j]);
	     pos[1].x=f(s->spermX[i][j+1]); pos[1].y=f(s->spermY[i][j+1]);
	     EZ_line(COLOR_5,lcolor[j],pos,NULL);
	    }
	}

     for (i=0;i<NMSPERM;i++)
	{int xo,yo;
	 for (j=HISTORY-1;j>0;j--)
	    {s->spermX[i][j]=s->spermX[i][j-1];
	     s->spermY[i][j]=s->spermY[i][j-1];
	    }
	 xo=s->spermX[i][0];
	 yo=s->spermY[i][0];
	 s->spermX[i][0]+=(yo>>4)+(xo>>4);
	 s->spermY[i][0]+=(-xo>>4)+(yo>>4);
	 if (abs(xo)>F(170) || abs(yo)>F(150))
	    {s->spermDead[i]=1;
	     /*  s->spermX[i][0]=((short)getNextRand())<<4;
		 s->spermY[i][0]=((short)getNextRand())<<4;
		 for (j=1;j<HISTORY;j++)
		 {s->spermX[i][j]=s->spermX[i][0];
		 s->spermY[i][j]=s->spermY[i][0];
		 } */
	    }
	}
     EZ_closeCommand();
     SCL_DisplayFrame();
    }
 while (!done);
}

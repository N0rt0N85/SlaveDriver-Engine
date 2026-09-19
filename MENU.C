#include <machine.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <string.h>

#include "menu.h"
#include "print.h"
#include "v_blank.h"
#include "util.h"
#include "spr.h"
#include "pic.h"
#include "file.h"
#include "local.h"
#include "sound.h"
#include "bup.h"

#include "weapon.h"
#include "gamestat.h"
#include "mplayer.h"

#define BUTTONBASECOLOR RGB(12,12,31)
#define BUTTONHICOLOR RGB(25,25,31)
#define BUTTONLOCOLOR RGB(5,5,21)

enum {IT_RECT,IT_TEXT,IT_BUTTON,IT_PICTURE,IT_FONTTEXT,IT_WAVYBUTTON,
	IT_GAMEBUTTON,IT_FONTSTRING
#ifdef JAPAN
	   ,IT_WAVYJBUTTON
#endif
	     };

#ifdef JAPAN
#define DLGFONT 3
#else
#define DLGFONT 1
#endif
#define MAXDLGSIZE 20
#define VRAMSTART (VRAM_ADDR)
DlgItem dlgItem[MAXDLGSIZE];

unsigned short lastUsedSelButton;
static int nmItems;
static int currentButton;

#define MAXNMPICS 48

static unsigned short *picPals[MAXNMPICS];
static unsigned int *picDatas[MAXNMPICS];
static int picVram[MAXNMPICS];
static int vramUsed;

static int texHeight,texWidth,texVramPos,texX,texY;
static int fontHeight;

void dlg_selectButton(int b)
{currentButton=b;
}

void dlg_centerStuff(void)
{int i;
 for (i=0;i<nmItems;i++)
    {switch (dlgItem[i].type)
	{case IT_WAVYBUTTON:
	    dlgItem[i].xp=F(-(getStringWidth(2,dlgItem[i].text)/2));
	    break;
#ifdef JAPAN
	 case IT_WAVYJBUTTON:
	    dlgItem[i].xp=F(-(getStringWidth(3,dlgItem[i].text)/2));
	    break;
#endif
	   }
    }
}

void initOverPics(void)
{
#ifndef JAPAN
 vramUsed=1024*256;
#else
 vramUsed=1024*200;
#endif
}

int decompressOverPic(int picNm,unsigned short *outData)
{unsigned char *picData=(unsigned char *)picDatas[picNm];
 unsigned short *pallete=picPals[picNm];
 int xsize,ysize;
 xsize=*(int *)picData;
 ysize=*(((int *)picData)+1);
 picData+=8;
 {register int outSize=0,i;
  register int nmPixels=xsize*ysize;
  register unsigned char *inPos=picData;
  while (outSize<nmPixels)
     {/* decode blank space */
      i=*(inPos++);
      for (;i;i--)
	 {*outData=0;
	  outData++;
	  outSize++;
	 }
      /* decode not blank space */
      i=*(inPos++);
      for (;i;i--)
	 {*outData=pallete[(int)*(inPos++)];
	  outSize++;
	  outData++;
	 }
     }
 }
 return xsize*ysize*2;
}

void loadOverPic(int picNm)
{picVram[picNm]=vramUsed;
 vramUsed+=decompressOverPic(picNm,(unsigned short *)(VRAMSTART+vramUsed));
 assert(vramUsed<512*1024);
}

typedef struct
{int x1,y1,x2,y2,width,type;
} BevelData;

int loadOverBase(unsigned char *picData,unsigned short *pallete,int xsize,
		 int ysize,BevelData *bevels,int nmBevels)
{int txstart,tx,ty,x1,y1,width,height;
 int vramBase=vramUsed;
 extern unsigned short doorwayCache;
 unsigned short *tempData=&doorwayCache;
 checkStack();
 width=*(int *)picData;
 height=*(((int *)picData)+1);
 assert(width*height<=1024*4);
 decompressOverPic(0,tempData);
 txstart=MTH_GetRand() % (width-1);
 ty=MTH_GetRand() % (height-1);
 txstart=0;
 ty=0;
 for (y1=0;y1<ysize;y1++)
    {tx=txstart;
     for (x1=0;x1<xsize;x1++)
	{int bevel=0;
	 int lineSide[4];
	 int b;
	 unsigned short c;
	 for (b=0;b<nmBevels;b++)
	    {if (x1<bevels[b].x1 || x1>=bevels[b].x2 ||
		 y1<bevels[b].y1 || y1>=bevels[b].y2)
		continue;
	     if (bevels[b].type==0 &&
		 x1>bevels[b].x1+bevels[b].width &&
		 y1>bevels[b].y1+bevels[b].width &&
		 x1<bevels[b].x2-bevels[b].width &&
		 y1<bevels[b].y2-bevels[b].width)
		continue;


	     lineSide[0]=(x1-bevels[b].x1)>(y1-bevels[b].y1);
	     lineSide[1]=-(y1-bevels[b].y1)>(x1-bevels[b].x2);
	     lineSide[2]=(x1-bevels[b].x2)>(y1-bevels[b].y2);
	     lineSide[3]=-(y1-bevels[b].y2)>(x1-bevels[b].x1);

	     if (y1<bevels[b].y1+bevels[b].width && lineSide[0] && lineSide[1])
		bevel=1;
	     if (x1>=bevels[b].x2-bevels[b].width && !lineSide[1] &&
		 lineSide[2])
		bevel=2;
	     if (y1>=bevels[b].y2-bevels[b].width && !lineSide[2] &&
		 !lineSide[3])
		bevel=3;
	     if (x1<bevels[b].x1+bevels[b].width && lineSide[3] &&
		 !lineSide[0])
		bevel=4;
	     if (bevels[b].type==1)
		if (bevel)
		   bevel=((bevel+1)&3)+1;
		else
		   bevel=5;
	     if (bevel)
		break;
	    }

	 c=tempData[tx+ty*width];
	  /* pallete[picData[tx+ty*width+8]]; */
	 if (bevel)
	    {int r,g,b,off;

	     r=c & 0x1f;
	     g=(c>>5) & 0x1f;
	     b=(c>>10) & 0x1f;

	     off=0;
	     if (bevel==2)
		off=-5;
	     if (bevel==3)
		off=-6;
	     if (bevel==1)
		off=6;
	     if (bevel==4)
		off=5;
	     if (bevel==5)
		off=-3;

	     r+=off;
	     g+=off;
	     b+=off;
	     if (b<0) b=0;
	     if (g<0) g=0;
	     if (r<0) r=0;
	     if (b>31) b=31;
	     if (g>31) g=31;
	     if (r>31) r=31;
	     c=RGB(r,g,b);
	    }
	 POKE_W(VRAMSTART+vramUsed,c);
	 vramUsed+=2;
	 assert(vramUsed<512*1024);
	 if (++tx>=width)
	    tx=0;
	}
     if (++ty>=height)
	ty=0;
    }
 return vramBase;
}


void plotOverPicW(int x,int y,int w,int h,int vram,int drawWord)
{unsigned short cmd[16];
 cmd[0]=0;
 cmd[1]=0;
 cmd[2]=drawWord;
 cmd[3]=0;
 assert(!(vram & 7));
 cmd[4]=vram>>3;
 assert(!(w & 7));
 cmd[5]=(w<<5) | h;
 cmd[6]=x;
 cmd[7]=y;
 cmd[8]=0; cmd[9]=0; cmd[10]=0; cmd[11]=0; cmd[12]=0; cmd[13]=0;
 cmd[14]=0; cmd[15]=0;
 EZ_cmd((struct cmdTable *)cmd);
}

void plotOverPic(int x,int y,int picNm)
{plotOverPicW(x,y,*(int *)picDatas[picNm],*(((int *)picDatas[picNm])+1),
	      picVram[picNm],COLOR_5|ECD_DISABLE);
}

void plotOverPicShadow(int x,int y,int picNm)
{plotOverPicW(x,y,*(int *)picDatas[picNm],*(((int *)picDatas[picNm])+1),
	      picVram[picNm],COLOR_5|ECD_DISABLE|COMPO_SHADOW);
}

void plotOverPicHarf(int x,int y,int picNm)
{plotOverPicW(x,y,*(int *)picDatas[picNm],*(((int *)picDatas[picNm])+1),
	      picVram[picNm],COLOR_5|ECD_DISABLE|COMPO_TRANS);
}

void dlg_clear(void)
{nmItems=0;
 currentButton=-1;
 texVramPos=-1;
 fontHeight=getFontHeight(DLGFONT);
}

void dlg_init(int fd)
{loadPicSet(fd,picPals,picDatas,MAXNMPICS);
 mem_lock();
 dlg_clear();
}

void dlg_addBase(int x,int y,int w,int h,BevelData *bevel,int nmBevels)
{texX=x;
 texY=y;
 texWidth=w;
 texHeight=h;
 texVramPos=loadOverBase((char *)(picDatas[0]),picPals[0],w,h,bevel,
			 nmBevels);
}

void dlg_addRect(int x,int y,int w,int h,int color)
{dlgItem[nmItems].xp=F(x);
 dlgItem[nmItems].yp=F(y);
 dlgItem[nmItems].w=w;
 dlgItem[nmItems].h=h;
 dlgItem[nmItems].color=color;
 dlgItem[nmItems].type=IT_RECT;
 nmItems++; assert(nmItems<MAXDLGSIZE);
}

void dlg_addGameButton(int x,int y,int gameNm)
{dlgItem[nmItems].xp=F(x);
 dlgItem[nmItems].yp=F(y);
 dlgItem[nmItems].code=gameNm;
 dlgItem[nmItems].color=gameNm;
 dlgItem[nmItems].type=IT_GAMEBUTTON;
 if (currentButton==-1)
    currentButton=nmItems;
 nmItems++; assert(nmItems<MAXDLGSIZE);
}

void dlg_addFontText(int x,int y,int w,int font,char *text)
{dlgItem[nmItems].xp=F(x);
 dlgItem[nmItems].yp=F(y);
 dlgItem[nmItems].w=w;
 dlgItem[nmItems].type=IT_FONTTEXT;
 dlgItem[nmItems].text=text;
 dlgItem[nmItems].color=font;
 nmItems++; assert(nmItems<MAXDLGSIZE);
}

void dlg_addFontString(int x,int y,int font,char *text)
{dlgItem[nmItems].xp=F(x);
 dlgItem[nmItems].yp=F(y);
 dlgItem[nmItems].type=IT_FONTSTRING;
 dlgItem[nmItems].text=text;
 dlgItem[nmItems].color=font;
 nmItems++; assert(nmItems<MAXDLGSIZE);
}

void dlg_addText(int x,int y,int w,char *text)
{dlgItem[nmItems].xp=F(x);
 dlgItem[nmItems].yp=F(y);
 dlgItem[nmItems].w=w;
 dlgItem[nmItems].type=IT_TEXT;
 dlgItem[nmItems].text=text;
 nmItems++; assert(nmItems<MAXDLGSIZE);
}

void dlg_addCenteredText(int x,int y,int w,char *text)
{dlg_addText(x+(w-getStringWidth(DLGFONT,text))/2,y,w,text);
}

void dlg_addButton(int pressCode,int x,int y,int w,int h,char *text)
{dlgItem[nmItems].xp=F(x);
 dlgItem[nmItems].yp=F(y);
 dlgItem[nmItems].w=w;
 dlgItem[nmItems].h=h;
 dlgItem[nmItems].code=pressCode;

 dlgItem[nmItems].type=IT_BUTTON;
 dlgItem[nmItems].text=text;
 if (currentButton==-1)
    currentButton=nmItems;
 nmItems++; assert(nmItems<MAXDLGSIZE);
}

#ifdef JAPAN
void dlg_addBigWavyJapaneseButton(int pressCode,int x,int y,char *text)
{dlgItem[nmItems].xp=F(x);
 dlgItem[nmItems].yp=F(y);
 dlgItem[nmItems].code=pressCode;
 dlgItem[nmItems].w=0;
 dlgItem[nmItems].h=0;

 dlgItem[nmItems].type=IT_WAVYJBUTTON;
 dlgItem[nmItems].text=text;
 if (currentButton==-1)
    currentButton=nmItems;
 nmItems++; assert(nmItems<MAXDLGSIZE);
}
#endif

void dlg_addBigWavyButton(int pressCode,int x,int y,char *text)
{dlgItem[nmItems].xp=F(x);
 dlgItem[nmItems].yp=F(y);
 dlgItem[nmItems].code=pressCode;
 dlgItem[nmItems].w=0;
 dlgItem[nmItems].h=0;

 dlgItem[nmItems].type=IT_WAVYBUTTON;
 dlgItem[nmItems].text=text;
 if (currentButton==-1)
    currentButton=nmItems;
 nmItems++; assert(nmItems<MAXDLGSIZE);
}

/* returns height of draw text */
enum drawMode {DM_NORM,DM_SHADOW,DM_NODRAW};
static int drawCenteredText(int x,int y,int width,char *text,int drawMode,
			    int dlgFont)
{int x1,y1,lineWidth,j;
 char *lstart,*lend,*p,*c;
 lstart=text;
 lend=text-1;
 lineWidth=0;
 y1=y;
 for (p=text;;p++)
    {
#ifndef JAPAN
     if (*p==' ' || *p==0 || *p=='\n')
#else
     if (1)
#endif
	{j=lineWidth;
	 for (c=lend+1;c<p;c++)
	    j+=getCharWidth(dlgFont,*c)+1;
	 if (j<width)
	    {/* add this unit to the line */
	     lend=p-1;
	     lineWidth=j;
#ifndef JAPAN
	     if (*p==' ')
#else
	     if (*p!=0 && *c!='\n')
#endif
		continue;
	    }
	 /* otherwise flush out the line */
#ifndef JAPAN
	 x1=x+((width-lineWidth)>>1);
#else
	 x1=x;
#endif
	 for (c=lstart;c<=lend;c++)
	    {switch (drawMode)
		{case DM_SHADOW:
		    drawCharShadow(x1,y1,dlgFont,*c);
		    break;
		 case DM_NORM:
		    drawChar(x1,y1,dlgFont,*c);
		    break;
		   }
	     x1+=getCharWidth(dlgFont,*c)+1;
	    }
	 y1+=fontHeight;
#ifdef JAPAN
	 if (*c=='\n')
	    lend++;
#endif
	 lstart=lend+1;
	 lineWidth=0;
	 if (!*p)
	    {if (lend>=p-1)
		return y1-y;
	     p--;
	    }
	 p=lend+1; /* just added this */
	}
    }
}


static void drawText(int x,int y,int width,char *text,int shadow,int dlgFont)
{int rside=x+width;
 int x1,y1,j;
 char *w,*p,*chr;
 /* w points to beginning of next word */
 x1=x; y1=y;
 w=text;
 for (p=text;;p++)
    {if (*p==' ' || *p==0)
	{/* time to draw the next word.  Decide if it should be on this line
	    or the next */
	 j=x1-1;
	 for (chr=w;chr<p;chr++)
	    j+=getCharWidth(dlgFont,*chr)+1;

	 if (j>rside)
	    {x1=x;
	     y1+=fontHeight+1;
	    }
	 while (w<p)
	    {if (shadow)
		drawCharShadow(x1,y1,dlgFont,*w);
	     else
		drawChar(x1,y1,dlgFont,*w);
	     x1+=getCharWidth(dlgFont,*w)+1;
	     w++;
	    }
	 x1+=getCharWidth(dlgFont,' ')+1;
	 w++;
	}
     if (*p==0)
	break;
    }
}


static void dlg_draw(int currentButton,int pressed)
{int i;
 XyInt p[5];
 for (i=0;i<nmItems;i++)
    {switch (dlgItem[i].type)
	{
#if 0
      case IT_PICTURE:
	    {unsigned short cmd[16];
	     cmd[0]=0;
	     cmd[1]=0;
	     cmd[2]=COLOR_5|ECD_DISABLE|SPD_DISABLE;
	     cmd[3]=0;
	     assert(!(dlgItem[i].color & 7));
	     cmd[4]=dlgItem[i].color>>3;
	     assert(!(dlgItem[i].w & 7));
	     cmd[5]=(dlgItem[i].w<<5)|dlgItem[i].h;
	     cmd[6]=dlgItem[i].x;
	     cmd[7]=dlgItem[i].y;
	     cmd[8]=0; cmd[9]=0; cmd[10]=0; cmd[11]=0; cmd[12]=0; cmd[13]=0;
	     cmd[14]=0; cmd[15]=0;
	     EZ_cmd(0,(struct cmdTable *)cmd);
	     break;
	    }
#endif
         case IT_RECT:
	    {int x=f(dlgItem[i].xp);
	     int y=f(dlgItem[i].yp);
	     p[0].x=x; p[0].y=y;
	     p[1].x=x+dlgItem[i].w; p[1].y=y;
	     p[2].x=x+dlgItem[i].w; p[2].y=y+dlgItem[i].h;
	     p[3].x=x; p[3].y=y+dlgItem[i].h;
	     EZ_polygon(ECD_DISABLE|SPD_DISABLE|COMPO_REP,
			dlgItem[i].color,p,NULL);
	     break;
	    }
	 case IT_BUTTON:
	    {int drawWord=COMPO_REP|ECD_DISABLE|SPD_DISABLE;
	     int x=f(dlgItem[i].xp);
	     int y=f(dlgItem[i].yp);
	     if (i!=currentButton)
		drawWord|=COMPO_HARF;
	     p[0].x=x; p[0].y=y;
	     p[1].x=x+dlgItem[i].w; p[1].y=y;
	     p[2].x=x+dlgItem[i].w; p[2].y=y+dlgItem[i].h;
	     p[3].x=x; p[3].y=y+dlgItem[i].h;
	     EZ_polygon(drawWord,BUTTONBASECOLOR,p,NULL);
	     EZ_polyLine(drawWord,0x8000,p,NULL);
	     p[0].x++; p[0].y++;
	     p[1].x--; p[1].y++;
	     p[2].x--; p[2].y--;
	     p[3].x++; p[3].y--;
	     p[4]=p[0];
	     if (i!=currentButton || !pressed)
		{EZ_line(drawWord,BUTTONHICOLOR,p,NULL);
		 EZ_line(drawWord,BUTTONLOCOLOR,p+1,NULL);
		 EZ_line(drawWord,BUTTONLOCOLOR,p+2,NULL);
		 EZ_line(drawWord,BUTTONHICOLOR,p+3,NULL);
		}
	     else
		{EZ_polyLine(drawWord,0x8000,p,NULL);
		 p[0].x++; p[0].y++;
		 p[1].x--; p[1].y++;
		 p[2].x--; p[2].y--;
		 p[3].x++; p[3].y--;
		 EZ_polyLine(drawWord,BUTTONLOCOLOR,p,NULL);
		}

	     x=f(dlgItem[i].xp)+dlgItem[i].w/2;
	     y=f(dlgItem[i].yp)+dlgItem[i].h/2;
	     y-=fontHeight/2;
	     x-=getStringWidth(DLGFONT,dlgItem[i].text)/2;
	     drawText(x,y,dlgItem[i].w,dlgItem[i].text,0,DLGFONT);
	     break;
	    }
	 case IT_TEXT:
	    {int x=f(dlgItem[i].xp);
	     int y=f(dlgItem[i].yp);
	     drawCenteredText(x+1,y+1,dlgItem[i].w,dlgItem[i].text,DM_SHADOW,
			      DLGFONT);
	     drawCenteredText(x+1,y+1,dlgItem[i].w,dlgItem[i].text,DM_SHADOW,
			      DLGFONT);
	     drawCenteredText(x,y,dlgItem[i].w,dlgItem[i].text,DM_NORM,
			      DLGFONT);
	     break;
	    }
	 case IT_FONTTEXT:
	    {int x=f(dlgItem[i].xp);
	     int y=f(dlgItem[i].yp);
	     drawText(x+1,y+1,dlgItem[i].w,dlgItem[i].text,1,
		      dlgItem[i].color);
	     drawText(x+1,y+1,dlgItem[i].w,dlgItem[i].text,1,
		      dlgItem[i].color);
	     drawText(x,y,dlgItem[i].w,dlgItem[i].text,0,
		      dlgItem[i].color);
	     break;
	    }
	 case IT_FONTSTRING:
	    {int x=f(dlgItem[i].xp);
	     int y=f(dlgItem[i].yp);
	     drawString(x,y,dlgItem[i].color,dlgItem[i].text);
	     break;
	    }
	 case IT_WAVYBUTTON:
	    {static Fixed32 waveCycle=0;
	     if (i==currentButton)
		{int color=MTH_Sin(waveCycle)>>12;
		 waveCycle+=F(8);
		 if (waveCycle>F(180))
		    waveCycle-=F(360);
		 drawStringGouro(f(dlgItem[i].xp),f(dlgItem[i].yp),2,
				 greyTable[16+color],greyTable[16-color],
				 dlgItem[i].text);
		}
	     else
		drawString(f(dlgItem[i].xp),f(dlgItem[i].yp),2,
			   dlgItem[i].text);
	     break;
	    }
#ifdef JAPAN
	 case IT_WAVYJBUTTON:
	    {static Fixed32 waveCycle=0;
	     if (i==currentButton)
		{int color=MTH_Sin(waveCycle)>>12;
		 waveCycle+=F(8);
		 if (waveCycle>F(180))
		    waveCycle-=F(360);
		 drawStringGouro(f(dlgItem[i].xp),f(dlgItem[i].yp),3,
				 greyTable[16+color],greyTable[16-color],
				 dlgItem[i].text);
		}
	     else
		drawString(f(dlgItem[i].xp),f(dlgItem[i].yp),3,
			   dlgItem[i].text);
	     break;
	    }
#endif
	 case IT_GAMEBUTTON:
	    {char buff1[80],buff2[80];
	     int a;
	     XyInt pos;
	     static Fixed32 waveCycle=0;
	     SaveState *stat;
	     stat=bup_getGameData(dlgItem[i].color);
	     assert(stat);
#if 1
	     sprintf(buff1,"%d/%d/%02d",
		     stat->month,stat->day,(stat->year+80)%100);
	     sprintf(buff2,"%d:%02d",
		     stat->hour,stat->min);
#else
	     strcpy(buff1,getText(LB_LEVELNAMES,stat->currentLevel));
	     buff2[0]=0;
#endif
	     if (i==currentButton)
		{int color=MTH_Sin(waveCycle)>>12;
#if 1
		 XyInt ppos[4];
		 ppos[0].x=f(dlgItem[i].xp)-4; ppos[0].y=f(dlgItem[i].yp)-1;
		 ppos[1].x=ppos[0].x+310; ppos[1].y=ppos[0].y;
		 ppos[2].x=ppos[0].x+310; ppos[2].y=ppos[0].y+23;
		 ppos[3].x=ppos[0].x; ppos[3].y=ppos[0].y+23;
		 EZ_polygon(COLOR_5,RGB(2,2,3),ppos,NULL);
#endif
		 waveCycle+=F(8);
		 if (waveCycle>F(180))
		    waveCycle-=F(360);
		 drawStringGouro(f(dlgItem[i].xp),f(dlgItem[i].yp),2,
				 greyTable[16+color],greyTable[16-color],
				 buff1);
		 drawStringGouro(f(dlgItem[i].xp)+93,f(dlgItem[i].yp),2,
				 greyTable[16+color],greyTable[16-color],
				 buff2);
		}
	     else
		{drawString(f(dlgItem[i].xp),f(dlgItem[i].yp),2,
			    buff1);
		 drawString(f(dlgItem[i].xp)+93,f(dlgItem[i].yp),2,
			    buff2);
		}
	     pos.y=f(dlgItem[i].yp);
	     pos.x=f(dlgItem[i].xp)+135;
	     {static char xw[6]={40,20,15,40,20,20};
	      static char yo[6]={2,0,0,0,0,0};

	      for (a=0;a<6;a++)
		 {pos.y=f(dlgItem[i].yp)+yo[a];
		  if ((stat->inventory>>a)&1)
		     EZ_normSpr(0,COLOR_5,0,mapPic(a),&pos,NULL);
		  else
		     EZ_normSpr(0,COMPO_HARF|COLOR_5,0,mapPic(a+6),&pos,NULL);
		  pos.x+=xw[a];
		 }
	     }
	    }
	   }
    }
}

static int moveSel(int cb,int dx,int dy,int movement)
{int nowx,nowy;
 int d,i;
 int bestButton,bestDist;
 int ccx,ccy,icx,icy;
 nowx=f(dlgItem[cb].xp);
 nowy=f(dlgItem[cb].yp);
 bestButton=-1;
 bestDist=0x7fffffff;
 ccx=f(dlgItem[cb].xp)+(dlgItem[cb].w>>1);
 ccy=f(dlgItem[cb].yp)+(dlgItem[cb].h>>1);
 for (i=0;i<nmItems;i++)
    {if (dlgItem[i].type!=IT_BUTTON &&
	 dlgItem[i].type!=IT_WAVYBUTTON &&
	 dlgItem[i].type!=IT_GAMEBUTTON
#ifdef JAPAN
	  && dlgItem[i].type!=IT_WAVYJBUTTON
#endif
	 )
	continue;
     icx=f(dlgItem[i].xp)+(dlgItem[i].w>>1);
     icy=f(dlgItem[i].yp)+(dlgItem[i].h>>1);
     d=(icx-ccx)*dx+
       (icy-ccy)*dy;
     if (d<=0)
	continue;
     d=0;
     if (movement==MENUMOVE_FREE)
	{d+=abs(icx-ccx);
	 d+=abs(icy-ccy);
	 if (icx*dy==ccx*dy &&
	     icy*dx==ccy*dx)
	    d-=10000;
	}
     if (movement==MENUMOVE_HORZ)
	{d+=abs(icx-ccx);
	 if (icx*dy==ccx*dy)
	    d-=10000;
	}
     if (movement==MENUMOVE_VERT)
	{d+=abs(icy-ccy);
	 if (icy*dx==ccy*dx)
	    d-=10000;
	}
     if (d<bestDist)
	{bestButton=i;
	 bestDist=d;
	}
    }
 if (bestButton!=-1)
    return bestButton;
 return cb;
}

#define SELKEYS (PER_DGT_A|PER_DGT_B|PER_DGT_C|PER_DGT_X|PER_DGT_Y|PER_DGT_Z|PER_DGT_S|PER_DGT_TL|PER_DGT_TR)

int dlg_run(int selSound,int pushSound,int movement)
{int data,lastData,changeData,i;
#if 0
 int first=2;
#endif
 int pressed=0;
 int returnTime=0;

 fontHeight=getFontHeight(DLGFONT);
 data=lastInputSample;
 SCL_SetFrameInterval(0xfffe);

 while (1)
    {EZ_openCommand();
     EZ_sysClip();
     EZ_localCoord(320/2,240/2);

     if (texVramPos>=0)
	plotOverPicW(texX,texY,texWidth,texHeight,texVramPos,
		     COLOR_5|ECD_DISABLE);
     dlg_draw(currentButton,pressed);
     SPR_WaitDrawEnd();
     EZ_closeCommand();
#if 0
     if (first>=0)
	{if (!first)
	    SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0,
			     0,0,0);
	 first--;
	}
#endif
     sound_nextFrame();
#ifdef JAPAN
     pic_nextFrame(NULL,NULL);
#endif
     SCL_DisplayFrame();
     if (returnTime)
	{if (returnTime==1)
	    {
#ifndef JAPAN
	     resetPics();
#endif
	     return dlgItem[currentButton].code;
	    }
	 returnTime--;
	 continue;
	}

     if (currentButton==-1)
	{if (!returnTime)
	    returnTime=2;
	 continue;
	}
     lastData=data;
     data=lastInputSample;
     changeData=lastData ^ data;

     i=currentButton;
     if (movement==MENUMOVE_VERT || movement==MENUMOVE_FREE)
	{if ((changeData & PER_DGT_D) && !(data & PER_DGT_D))
	    currentButton=moveSel(currentButton,0,1,movement);
	 if ((changeData & PER_DGT_U) && !(data & PER_DGT_U))
	    currentButton=moveSel(currentButton,0,-1,movement);
	}
     if (movement==MENUMOVE_HORZ || movement==MENUMOVE_FREE)
	{if ((changeData & PER_DGT_L) && !(data & PER_DGT_L))
	    currentButton=moveSel(currentButton,-1,0,movement);
	 if ((changeData & PER_DGT_R) && !(data & PER_DGT_R))
	    currentButton=moveSel(currentButton,1,0,movement);
	}
     if (i!=currentButton && selSound>=0)
	playSound(0,selSound);

     if ((changeData&~data)&SELKEYS)
	{lastUsedSelButton=(changeData&~data) & SELKEYS;
	 if (pushSound>=0)
	    playSound(0,pushSound);
	 pressed=1;
	}

     if ((data & SELKEYS)==SELKEYS && pressed)
	{returnTime=5;
	 pressed=0;
	}
    }
}

void dlg_flashMessage(char *message1,char *message2,int w,int h)
{BevelData bevel;
 w=(w+7)&(~7);
 bevel.x1=0; bevel.y1=0;
 bevel.x2=w; bevel.y2=h;
 bevel.width=5; bevel.type=0;
 initOverPics();
 dlg_clear();
 dlg_addBase(-w/2,-h/2,w,h,&bevel,1);
 dlg_addText(-160,-h/2+13,320,message1);
 dlg_addText(-160,-h/2+23,320,message2);
 dlg_run(-1,-1,MENUMOVE_FREE);
#ifdef JAPAN
 resetPics();
#endif
}

void dlg_runMessage(char *message,int w)
{int bw=80;
 int bh=20;
 int h,textHeight;
 static BevelData bevel[2]=
    {{0,0,0,0,5,0},
     {9,9,0,0,2,1}};
 dlg_clear();
 w=(w+7)&(~7);
 textHeight=drawCenteredText(0,0,w-30,message,DM_NODRAW,DLGFONT);
 h=textHeight+bh+40;
 bevel[0].x2=w; bevel[0].y2=h;
 bevel[1].x2=w-9; bevel[1].y2=h-40;
 initOverPics();
 dlg_addBase(-w/2,-h/2,w,h,bevel,2);

 dlg_addText(-w/2+15,-h/2+12,w-30,message);
#ifndef JAPAN
 dlg_addButton(1,-bw/2,h/2-bh-13,bw,bh,"OK");
#else
 dlg_addButton(1,-bw/2,h/2-bh-13-4,bw,bh+8,"");
#endif
 dlg_run(-1,-1,MENUMOVE_FREE);
#ifdef JAPAN
 resetPics();
#endif
}

int dlg_runYesNo(char *message,int w)
{int bw=80;
 int bh=20;
 int space=20;
 int textHeight;
 int h,ret;
 static BevelData bevel[2]=
    {{0,0,0,0,5,0},
     {9,9,0,0,2,1}};

 dlg_clear();
 w=(w+7)&(~7);
 textHeight=drawCenteredText(0,0,w-30,message,DM_NODRAW,DLGFONT);
 h=textHeight+bh+40;

 bevel[0].x2=w; bevel[0].y2=h;
 bevel[1].x2=w-9; bevel[1].y2=h-40;
 initOverPics();

 dlg_addBase(-w/2,-h/2,w,h,bevel,2);

 dlg_addText(-w/2+15,-h/2+12,w-30,message);

#ifndef JAPAN
 dlg_addButton(1,-(2*bw+space)/2,h/2-(bh+13),bw,bh,
	       getText(LB_PROMPTS,3));
 dlg_addButton(0,space/2,h/2-(bh+13),bw,bh,
	       getText(LB_PROMPTS,4));
#else
 dlg_addButton(1,-(2*bw+space)/2,h/2-(bh+13)-4,bw,bh+8,
	       getText(LB_PROMPTS,3));
 dlg_addButton(0,space/2,h/2-(bh+13)-4,bw,bh+8,
	       getText(LB_PROMPTS,4));
#endif
 ret=dlg_run(-1,-1,MENUMOVE_FREE);
#ifdef JAPAN
 resetPics();
#endif
 return ret;
}

int runTravelQuestion(char *destination)
{char buff[160];
 POKE_W(SCL_VDP2_VRAM+0x180114,-255); /* reset color offsets */
 POKE_W(SCL_VDP2_VRAM+0x180116,-255);
 POKE_W(SCL_VDP2_VRAM+0x180118,-255);
 SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0|SCL_RBG0,
		  -255,-255,-255);
 dontDisplayVDP2Pic();
 EZ_openCommand(); EZ_closeCommand(); SCL_DisplayFrame();
 EZ_openCommand(); EZ_closeCommand(); SCL_DisplayFrame();
 SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0,
		  0,0,0);
 sprintf(buff,getText(LB_PROMPTS,5),destination);
 return dlg_runYesNo(buff,200);
}


#ifndef JAPAN
#define INVWIDTH 31*8
#define INVHEIGHT (155+12)
#else
#define INVWIDTH 35*8
#define INVHEIGHT 200
#endif

#ifndef JAPAN
#define PICX 58
#define PICY -37
static BevelData inventoryBevel[]=
{{108+16,9,INVWIDTH-9,74+9,3,1},
 {9,111,INVWIDTH-9,INVHEIGHT-9,3,1},
 {0,0,INVWIDTH,INVHEIGHT,5,0}};
#else
#define WINX 150
#define WINY 20
#define PICX 69
#define PICY -43
static BevelData inventoryBevel[]=
{{WINX,WINY,WINX+115,WINY+74,3,1},
 {9,141,INVWIDTH-9,INVHEIGHT-9,3,1},
 {0,0,INVWIDTH,INVHEIGHT,5,0}};
#endif
#define NMINVBUTTONS 5

/* static int inventoryNum[NMINVBUTTONS]={2,8,6,0,1};*/
static int inventoryPicStart[NMINVBUTTONS]={29,14,22,31,40};
static int textStart[NMINVBUTTONS]={0,2,10,17,18};

#define NMDUSTMOTES 128

char endUserCheatsEnabled=0;
/* returns 1 if quit requested */
void runInventory(int inventory,int keyMask,int *mapState,
		  int fade,int fadeButton,int fadeSelection)
{XyInt p;
 int i,canGoUp,canGoDown;
 int selectedButton;
 int data,lastData,changeData;
 int slidePos[MAXNMPICS];
 int quitEnable=0;
 int frameCount;
 int commScreenSize,staticCount=0;
 struct soundSlotRegister *tinkleSound;
 extern unsigned short doorwayCache;
 unsigned short *tempData=&doorwayCache;

 XyInt dust[NMDUSTMOTES];
 int dustAge[NMDUSTMOTES];
 int dustHead=0,dustTail=0;

 int fadeReg=0,fadeWidth=0,fadeHeight=0,fadeSize=0,fadePic=0,fadeCount=0;
 int gotScreenPic=0;

 checkStack();

 saveSoundState();
 stopAllLoopedSounds();
 displayEnable(0);
 SCL_DisplayFrame();
 SCL_SetColOffset(SCL_OFFSET_A,SCL_SP0|SCL_NBG0,
		  0,0,0);
 for (i=0;i<NMINVBUTTONS;i++)
    slidePos[i]=0;

 slidePos[0]=*mapState;
 slidePos[1]=bitScanForward((inventory>>8)&0xff,-1);
 slidePos[2]=bitScanForward(inventory&0x3f,-1);
 dontDisplayVDP2Pic();
 initOverPics();
 for (i=1;i<41;i++)
    loadOverPic(i);

 data=lastInputSample;
 lastData=data;
 SCL_SetFrameInterval(0xfffe);

 dlg_clear();
 dlg_addBase(-INVWIDTH/2,-INVHEIGHT/2,INVWIDTH,INVHEIGHT,inventoryBevel,3);
 selectedButton=0;

 commScreenSize=(*(int *)picDatas[39]) * *(((int *)picDatas[39])+1);
 if (fade)
    {selectedButton=fadeButton;
     if (fadeButton!=3)
	slidePos[selectedButton]=fadeSelection;
     fadePic=inventoryPicStart[selectedButton]+fadeSelection;
     fadeReg=1;
     fadeWidth=*(int *)picDatas[fadePic];
     fadeHeight=*(((int *)picDatas[fadePic])+1);
     fadeSize=fadeWidth*fadeHeight;
     fadeCount=0;
     dustHead=dustTail=0;
     for (i=0;i<fadeSize*2;i+=2)
	{POKE_W(VRAMSTART+picVram[fadePic]+i,0);
	}
     decompressOverPic(fadePic,tempData);
     {struct soundSlotRegister ssr;
      initSoundRegs(level_staticSoundMap[ST_INTERFACE]+2,0,0,&ssr);
      ssr.reg[4]=(0<<11)+(16<<6)+5; /* dd2r, d1r, and ar */
      ssr.reg[5]=(31<<5)+5; /* dl & rr */
      tinkleSound=playSoundMegaE(54,&ssr);
     }
    }

 frameCount=0;
 while (1)
    {EZ_openCommand();
     EZ_sysClip();
     EZ_localCoord(320/2,240/2);

#if 0
#ifndef JAPAN
     /* game paused message */
     drawString(-(getStringWidth(1,getText(LB_PROMPTS,2))>>1),
		-95,
		1,getText(LB_PROMPTS,2));
#endif
#endif
     /* draw background */
     plotOverPicW(texX,texY,texWidth,texHeight,texVramPos,COLOR_5|ECD_DISABLE);

     /* buttons */
#ifndef JAPAN
     for (i=0;i<NMINVBUTTONS;i++)
	if (i==selectedButton)
	   plotOverPic(-114,-74+i*20,2);
	else
	   plotOverPic(-114,-74+i*20,1);
#else
     for (i=0;i<NMINVBUTTONS;i++)
	if (i==selectedButton)
	   plotOverPic(-130,-91+i*26,2);
	else
	   plotOverPic(-130,-91+i*26,1);
#endif

     for (i=0;i<NMINVBUTTONS;i++)
	{int x,y;
#ifndef JAPAN
	 x=-62-getStringWidth(1,getText(LB_INVBUTTONS,i))/2;
	 y=-68+i*20-getFontHeight(1)/2;
	 if (i!=selectedButton)
	    drawString(x,y,1,getText(LB_INVBUTTONS,i));
	 else
	    drawStringGouro(x,y,1,RGB(31,31,15),RGB(31,31,15),
			    getText(LB_INVBUTTONS,i));
#else
	 x=-66-getStringWidth(3,getText(LB_INVBUTTONS,i))/2;
	 y=-78+i*26-getFontHeight(3)/2;
	 if (i!=selectedButton)
	    drawString(x,y,3,getText(LB_INVBUTTONS,i));
	 else
	    drawStringGouro(x,y,3,RGB(31,31,15),RGB(31,31,15),
			    getText(LB_INVBUTTONS,i));
#endif
	}

     /* key label */
#if 0
     plotOverPic(-6,12,3);
     drawString(-3,12,1,getText(LB_INVBUTTONS,NMINVBUTTONS));
#endif

#ifndef JAPAN
     /* keys */
     for (i=0;i<4;i++)
	plotOverPic(20+19*i,6,i+4+(((keyMask>>i)&1)?4:0));
#else
     /* keys */
     for (i=0;i<4;i++)
	plotOverPic(28+19*i,6,i+4+(((keyMask>>i)&1)?4:0));
#endif

     /* decide mobility */
     canGoUp=-1;
     canGoDown=-1;
     if (slidePos[selectedButton]!=-1)
	switch (selectedButton)
	   {case 0:
	       if (slidePos[selectedButton]>0) canGoDown=0;
	       if (slidePos[selectedButton]<1) canGoUp=1;
	       break;
	    case 1:
	       canGoDown=bitScanBackwards((inventory>>8)&0xff,
					  slidePos[selectedButton]);
	       canGoUp=bitScanForward((inventory>>8)&0xff,
				      slidePos[selectedButton]);
	       break;
	    case 2:
	       canGoDown=bitScanBackwards(inventory&0x3f,
					  slidePos[selectedButton]);
	       canGoUp=bitScanForward(inventory&0x3f,
				      slidePos[selectedButton]);
	       if (canGoUp==-1 && slidePos[selectedButton]<6 &&
		   currentState.dolls)
		  canGoUp=6;
	       break;
	      }
     /* arrows */
     if (canGoDown!=-1)
#ifndef JAPAN
	plotOverPic(8,-39,12);
#else
	plotOverPic(8+9,-39-8,12);
#endif
     if (canGoUp!=-1)
#ifndef JAPAN
	plotOverPic(98,-39,13);
#else
	plotOverPic(98+9,-39-8,13);
#endif

     /* picture window */
     if (slidePos[selectedButton]!=-1/* && inventoryNum[selectedButton]*/)
	{int p,px=0,py=0;
	 if (selectedButton!=3)
	    {/* do normal pictures */
	     p=inventoryPicStart[selectedButton]+slidePos[selectedButton];
	     px=PICX-((*(int *)picDatas[p])>>1);
	     py=PICY-((*(((int *)picDatas[p])+1))>>1);
	     if (selectedButton!=4)
		plotOverPicShadow(px+2,py+2,p);
	     plotOverPic(px,py,p);
	    }
	 else
	    {/* do tranmitter picture */
	     int inv=(currentState.inventory & INV_TRANSMITTER)>>16;
	     int i,picStart;
	     static char offsets[][2]=
		{{20,23},{-30,-0},{-40,-30},{-50,-10},
		    {-14,2},{-16,12},{5,-27},{-1,-35}};
	     picStart=inventoryPicStart[selectedButton];
	     for (i=0;i<8;i++)
		{if (inv&1)
		    {plotOverPic(PICX+offsets[i][0],PICY+offsets[i][1],
				 picStart+i);
		    }
		 else
		    {plotOverPicHarf(PICX+offsets[i][0],PICY+offsets[i][1],
				     picStart+i);
		    }
		 inv>>=1;
		}
	     if (!fade && (currentState.inventory&INV_TRANSMITTER)==0xff0000)
		{/* unsigned short *pallete=picPals[38]; */
		 if (!gotScreenPic)
		    {decompressOverPic(39,tempData);
		     gotScreenPic=1;
		    }
		 if (staticCount)
		    {staticCount--;
		     for (i=0;i<commScreenSize;i++)
			{unsigned short color=
			    tempData[i];
			 if (color)
			    POKE_W(VRAMSTART+picVram[39]+(i<<1),
				   greyTable[getNextRand()&0x0f]);
			}
		    }
		 else
		    {if (!(getNextRand()&0x3f))
			staticCount=3;
		     if (!(getNextRand()&0x7f))
			staticCount=29;
		     for (i=0;i<commScreenSize;i++)
			{unsigned short color=
			    tempData[i];
			 POKE_W(VRAMSTART+picVram[39]+(i<<1),color);
			}
		    }
		 plotOverPic(PICX+offsets[6][0],PICY+offsets[6][1],
			     picStart+8);
		}
	     if (fade)
		{px=PICX+offsets[fadeSelection][0];
		 py=PICY+offsets[fadeSelection][1];
		}
	    }
	 if (fade)
	    {/* unsigned short *pallete=picPals[fadePic]; */
	     XyInt line[2];
	     if (fadeCount>8196/64+40)
		fade=0;
	     if (fadeCount<8196/64)
		{for (i=0;i<64;i++)
		    {if (fadeReg & 1)
			fadeReg=(fadeReg>>1) ^ (0x1b00);
		     else
			fadeReg=fadeReg>>1;
		     if (fadeReg<fadeSize)
			{unsigned short color=
			    tempData[fadeReg];
			 /*pallete[((unsigned char *)(picDatas[fadePic]))
				    [fadeReg+8]]; */
			 POKE_W(VRAMSTART+picVram[fadePic]+(fadeReg<<1),color);

			 if (color && !(i&7))
			    {int x,y;
			     for (x=fadeReg,y=0;x>=fadeWidth;x-=fadeWidth,y++);
			     dust[dustHead].x=x+px;
			     dust[dustHead].y=y+py;
			     dustAge[dustHead]=fadeCount;
			     dustHead=(dustHead+1)&(NMDUSTMOTES-1);
			     if (dustHead==dustTail)
				dustTail=(dustTail+1)&(NMDUSTMOTES-1);
			    }
			}
		    }
		}
	     fadeCount++;
	     /* kill old dust */
	     while (dustAge[dustTail]<fadeCount-25 && dustHead!=dustTail)
		dustTail=(dustTail+1)&(NMDUSTMOTES-1);
	     /* draw dust */
	     if (dustHead!=dustTail)
		{i=dustTail;
		 do
		    {dust[i].y+=1;
		     line[1]=dust[i];
		     line[0]=dust[i];
		     EZ_line(COMPO_TRANS|ECD_DISABLE|SPD_DISABLE,
			     greyTable[31-fadeCount+dustAge[i]],line,
			     NULL);

		     i=(i+1)&(NMDUSTMOTES-1);
		    }
		 while (i!=dustHead);
		}
	    }
	}

     /* text window */
     if (slidePos[selectedButton]!=-1/* && inventoryNum[selectedButton]*/)
	{int nmLines=1;
	 char *c;
	 char *string;
	 char buffer[80];
	 char buff2[80];
	 int bpos;
	 string=getText(LB_INVTEXT,slidePos[selectedButton]+
			textStart[selectedButton]);

	 if (selectedButton==3)
	    {int cnt,q;
	     q=(currentState.inventory & INV_TRANSMITTER)>>16;
	     for (cnt=0;q;)
		{if (q & 1)
		    cnt++;
		 q=q>>1;
		}
	     sprintf(buff2,string,cnt);
	     string=buff2;
	     if (cnt==8)
		string=getText(LB_ITEMMESSAGE,23);
	    }

	 if (selectedButton==2 && slidePos[selectedButton]==6)
	    {int cnt,q;
	     q=currentState.dolls;
	     for (cnt=0;q;)
		{if (q & 1)
		    cnt++;
		 q=q>>1;
		}
	     sprintf(buff2,string,cnt);
	     string=buff2;
	    }

	 if (selectedButton==0 && currentState.currentLevel<22)
	    {sprintf(buff2,"%s\n%s",
		     getText(LB_LEVELNAMES,currentState.currentLevel),
		     string);
	     string=buff2;
	    }

	 for (c=string;*c;c++)
	    if (*c=='\n')
	       nmLines++;
#ifndef JAPAN
	 p.y=(INVHEIGHT-119)-((getFontHeight(1)-1)*nmLines)/2;
#else
	 p.y=(INVHEIGHT-136)-((getFontHeight(3)-1)*nmLines)/2;
#endif
	 bpos=0;
	 for (c=string;;c++)
	    {buffer[bpos++]=*c;
	     if (*c=='\n' || *c==0)
		{buffer[bpos-1]=0;
#ifndef JAPAN
		 p.x=-(getStringWidth(1,buffer))/2;
		 drawString(p.x,p.y,1,buffer);
		 p.y+=getFontHeight(1)-1;
#else
		 p.x=-(getStringWidth(3,buffer))/2;
		 drawString(p.x,p.y,3,buffer);
		 p.y+=getFontHeight(3)-1;
#endif
		 bpos=0;
		}
	     if (!*c)
		break;
	    }
	}

     lastData=data;
     data=lastInputSample;
     changeData=lastData ^ data;

     if (!fade)
	{if ((changeData & PER_DGT_D) && !(data & PER_DGT_D))
	    if (selectedButton<4)
	       {selectedButton++;
		playStaticSound(ST_INTERFACE,0);
	       }
	 if ((changeData & PER_DGT_U) && !(data & PER_DGT_U))
	    if (selectedButton>0)
	       {selectedButton--;
		playStaticSound(ST_INTERFACE,0);
	       }
	 if ((changeData & PER_DGT_L) && !(data & PER_DGT_L))
	    if (canGoDown!=-1)
	       {slidePos[selectedButton]=canGoDown;
		playStaticSound(ST_INTERFACE,1);
	       }
	 if ((changeData & PER_DGT_R) && !(data & PER_DGT_R))
	    if (canGoUp!=-1)
	       {slidePos[selectedButton]=canGoUp;
		playStaticSound(ST_INTERFACE,1);
	       }
	 if ((selectedButton==4 || !(data & PER_DGT_S)) &&
	     !(data & (PER_DGT_A|PER_DGT_B|PER_DGT_C)))
	    {extern int quitRequest;
	     quitRequest=1;
	     stopAllSound(54);
	     return;
	    }

	 if (endUserCheatsEnabled && (changeData & PER_DGT_X)
	     && !(data & PER_DGT_X))
	    {currentState.health=currentState.nmBowls*200;
	     for (i=0;i<WP_NMWEAPONS;i++)
		currentState.weaponAmmo[i]=weaponMaxAmmo[i];
	    }
	 if (cheatsEnabled)
	    {if ((changeData & PER_DGT_X) && !(data & PER_DGT_X))
		{if (selectedButton==3)
		    currentState.inventory|=INV_TRANSMITTER;
		 if (selectedButton==2)
		    currentState.dolls=ALLDOLLS;
		 currentState.health=currentState.nmBowls*200;
		 for (i=0;i<WP_NMWEAPONS;i++)
		    currentState.weaponAmmo[i]=weaponMaxAmmo[i];
		 {extern int keyMask;
		  keyMask=0xf;
		 }
		}
	     if ((changeData & PER_DGT_Y) && !(data & PER_DGT_Y))
		currentState.inventory^=INV_FEATHER;
	    }

	 if (!(data & PER_DGT_S))
	    quitEnable=1;
	 if (quitEnable && (data & PER_DGT_S))
	    {*mapState=slidePos[0];
	     resetPics();
	     EZ_closeCommand();
	     SCL_DisplayFrame();
	     EZ_openCommand();
	     EZ_closeCommand();
	     SCL_DisplayFrame();
	     stopAllSound(54);
	     restoreSoundState();
	     return;
	    }
	}

     EZ_closeCommand();
     SCL_DisplayFrame();
     sound_nextFrame();
     pic_nextFrame(NULL,NULL);
     if (frameCount++==2)
	displayEnable(1);
    }
}

void dlg_setupSlideIn(void)
{int i;
 for (i=0;i<nmItems;i++)
    {dlgItem[i].x2=f(dlgItem[i].xp);
     dlgItem[i].y2=f(dlgItem[i].yp);
     dlgItem[i].x1=dlgItem[i].x2+(i&1?-300:300);
     dlgItem[i].y1=dlgItem[i].y2;
     dlgItem[i].xv=0;
     dlgItem[i].yv=0;
     if (dlgItem[i].type==IT_RECT)
	{dlgItem[i].x1=0;
	}
    }
}

void dlg_setupSlideUp(void)
{int i;
 for (i=0;i<nmItems;i++)
    {dlgItem[i].x2=f(dlgItem[i].xp);
     dlgItem[i].y2=f(dlgItem[i].yp);
     dlgItem[i].x1=dlgItem[i].x2+(i&1?-100:100);
     dlgItem[i].y1=180;
     dlgItem[i].xv=0;
     dlgItem[i].yv=0;
     if (dlgItem[i].type==IT_RECT)
	{dlgItem[i].x1=0;
	}
    }
}

void dlg_setupSlideOut(void)
{int i;
 for (i=0;i<nmItems;i++)
    {dlgItem[i].x1=f(dlgItem[i].xp);
     dlgItem[i].y1=f(dlgItem[i].yp);
     dlgItem[i].x2=dlgItem[i].x2+(i&1?-300:300);
     dlgItem[i].y2=dlgItem[i].y2;
     dlgItem[i].xv=0;
     dlgItem[i].yv=0;
     if (dlgItem[i].type==IT_RECT)
	{dlgItem[i].x2=0;
	}
    }
}

void dlg_setupNoSlide(void)
{int i;
 for (i=0;i<nmItems;i++)
    {dlgItem[i].x1=f(dlgItem[i].xp);
     dlgItem[i].y1=f(dlgItem[i].yp);
     dlgItem[i].x2=f(dlgItem[i].xp);
     dlgItem[i].y2=f(dlgItem[i].yp);
     dlgItem[i].xv=0;
     dlgItem[i].yv=0;
    }
}

void dlg_runSlideIn(void)
{Fixed32 f;
 int i;
 for (f=0;f<F(1);f+=(F(1)/32))
    {for (i=0;i<nmItems;i++)
	{dlgItem[i].xp=evalHermite(f,F(dlgItem[i].x1),
				   F(dlgItem[i].x2),0,dlgItem[i].xv);
	 dlgItem[i].yp=evalHermite(f,F(dlgItem[i].y1),
				   F(dlgItem[i].y2),0,dlgItem[i].yv);
	 if (dlgItem[i].type==IT_RECT)
	    dlgItem[i].w=abs(2*f(dlgItem[i].xp));
	}

     EZ_openCommand();
     EZ_sysClip();
     EZ_localCoord(320/2,240/2);
     dlg_draw(currentButton,0);
     EZ_closeCommand();
     SPR_WaitDrawEnd();
     SCL_DisplayFrame();
#ifdef JAPAN
     pic_nextFrame(NULL,NULL);
#endif
    }

 for (i=0;i<nmItems;i++)
    {dlgItem[i].xp=F(dlgItem[i].x2);
     dlgItem[i].yp=F(dlgItem[i].y2);
    }
}

#include <sega_spr.h>
#include <sega_scl.h>
#include "util.h"
#include "spr.h"
#include "print.h"

/* #include "font0.h" */
#include "font1.h"
#include "font2.h"

#ifdef JAPAN
#define MAXNMFONTS 4
#else
#define MAXNMFONTS 3
#endif

static unsigned char *fontList[]={brianFont,brianFont,bigFont,NULL};

static short charMap[MAXNMFONTS][256];
static char widths[MAXNMFONTS][256];
static char heights[MAXNMFONTS];

#ifdef JAPAN
#include "file.h"
#include "pic.h"

#define MAXNMJCHARS 240
static unsigned int *charData[MAXNMJCHARS];
static int nmJChars;

void loadJapanFontData(int fd)
{unsigned short *palData[MAXNMJCHARS];
 nmJChars=loadPicSet(fd,palData,charData,MAXNMJCHARS);
}

#define PICDATA(d) (((unsigned char *)d)+8)
static int firstJFontPic;
void loadJapanFontPics(void)
{int i,res;
 for (i=0;i<nmJChars;i++)
    {res=addPic(TILEJCHAR,PICDATA(charData[i]),NULL,4);
     if (i==0)
	firstJFontPic=res;
     widths[3][i]=17;
    }
 heights[3]=18;
 EZ_setLookupTbl(3,(struct sprLookupTbl *)(fontList[1]+2));
}
#endif

int initFonts(int spriteNm,int fontMask)
{int f,i,x,y,c,fontc,fontHeight,widthBy8;
 unsigned char buffer[32*32];

 fontMask&=~1;

 for (f=0;fontList[f];f++)
    {i=fontMask&1;
     fontMask=fontMask>>1;
     if (!i)
	continue;
     heights[f]=*(short *)fontList[f];
     fontHeight=heights[f];
     EZ_setLookupTbl(f,(struct sprLookupTbl *)(fontList[f]+2));
     for (i=0;i<256;i++)
	widths[f][i]=*(fontList[f]+2+32+i);
     fontc=0;
     for (i=0;i<256;i++)
	{if (widths[f][i]==0)
	    {charMap[f][i]=-1;
	     continue;
	    }
	 c=0;
	 widthBy8=(widths[f][i]+7)&(~7);
	 for (y=0;y<fontHeight;y++)
	    {for (x=0;x<((widths[f][i]+1)>>1);x++)
		buffer[c++]=fontList[f][2+32+256+fontc++];
	     for (;x<(widthBy8>>1);x++)
		buffer[c++]=0;
	    }
	 EZ_setChar(spriteNm,COLOR_1,widthBy8,fontHeight,buffer);
	 charMap[f][i]=spriteNm;
	 spriteNm++;
	}
     /* if font does not have lower case chars, then map lowers to uppers */
     for (i='a';i<='z';i++)
	if (charMap[f][i]==-1)
	   {charMap[f][i]=charMap[f][i+'A'-'a'];
	    widths[f][i]=widths[f][i+'A'-'a'];
	   }
     /* map accented chars */
     {static unsigned char *charEquiv[]=
	 {"\355\315","\372\332","\351\311",
		  /* GCC14: codes that occur in the shipped subtitle and menu text but have no
		     glyph in the font -- mapped onto the nearest glyph it does have, through
		     the same mechanism.  Measured on the retail OPEN/GOOD/BAD.MOV (all four
		     language blocks) and on INITLOAD.DAT; without them a debug build stops on
		     assert(0) in drawStringN and a release build drops the character. */
		  "\222'",		/* right single quote, French subtitles -> apostrophe */
		  "\357i",		/* i diaeresis, French subtitles       -> i          */
		  "\334U",		/* U diaeresis, German subtitles       -> U          */
		  "\261\361",	/* the Spanish menu text encodes n tilde as 0xb1     */
		  NULL};
      for (i=0;charEquiv[i];i++)
	 {int one=charEquiv[i][0];
	  int two=charEquiv[i][1];
	  if (charMap[f][one]==-1 && charMap[f][two]!=-1)
	     {charMap[f][one]=charMap[f][two];
	      widths[f][one]=widths[f][two];
	     }
	  if (charMap[f][two]==-1 && charMap[f][one]!=-1)
	     {charMap[f][two]=charMap[f][one];
	      widths[f][two]=widths[f][one];
	     }
	 }
     }
    }

 for (i=0;i<256;i++)
    {widths[0][i]=widths[1][i];
     charMap[0][i]=charMap[1][i];
    }
 heights[0]=heights[1];

 return spriteNm;
}

void drawString(int x,int y,int font,unsigned char *text)
{XyInt pos;
 pos.x=x; pos.y=y;
 for (;*text;text++)
    {if (!widths[font][(int)*text])
	{/* assert(0); */
	 continue;
	}
#ifdef JAPAN
     if (font==3)
	{EZ_normSpr(DIR_NOREV,ECD_DISABLE|COLOR_1|COMPO_REP,3,
		    mapPic((*text)+firstJFontPic),&pos,NULL);
	}
     else
#endif
     EZ_normSpr(DIR_NOREV,ECD_DISABLE|COLOR_1|COMPO_REP,font,
		charMap[font][(int)*text],&pos,NULL);
     pos.x+=widths[font][(int)*text]+1;
    }
}

void drawStringN(int x,int y,int font,unsigned char *text,int n)
{XyInt pos;
 pos.x=x; pos.y=y;
 for (;n;n--,text++)
    {if (!widths[font][(int)*text])
	{dPrint("unknown character %d\n",(int)*text);
	 assert(0);
	 continue;
	}
     EZ_normSpr(DIR_NOREV,UCLPIN_ENABLE|ECD_DISABLE|COLOR_1|COMPO_REP,font,
		charMap[font][(int)*text],&pos,NULL);
     pos.x+=widths[font][(int)*text]+1;
    }
}

void drawStringFixedPitch(int x,int y,int font,unsigned char *text,
			  int pitch)
{XyInt pos;
 pos.x=x; pos.y=y;
 for (;*text;text++)
    {if (!widths[font][(int)*text])
	{assert(0);
	 continue;
	}
     EZ_normSpr(DIR_NOREV,ECD_DISABLE|COLOR_1|COMPO_REP,font,
		charMap[font][(int)*text],&pos,NULL);
     pos.x+=pitch+1;
    }
}


void drawStringBulge(int x,int y,int font,int buldgeCenter,
		     unsigned char *text)
{XyInt pos;
 int cnm,bright;
 struct gourTable gtable;
 pos.x=x; pos.y=y;
 for (cnm=0;*text;text++,cnm++)
    {if (!widths[font][(int)*text])
	{assert(0);
	 continue;
	}
     bright=31-3*abs(buldgeCenter-cnm);
     if (bright<16) bright=16;
     gtable.entry[0]=greyTable[bright];
     gtable.entry[1]=greyTable[bright];
     gtable.entry[2]=greyTable[bright];
     gtable.entry[3]=greyTable[bright];
     EZ_normSpr(DIR_NOREV,DRAW_GOURAU|ECD_DISABLE|COLOR_1|COMPO_REP,font,
		charMap[font][(int)*text],&pos,&gtable);
     pos.x+=widths[font][(int)*text]+1;
    }
}

void drawStringGouro(int x,int y,int font,unsigned short gourTop,
		     unsigned short gourBot,
		     unsigned char *text)
{XyInt pos;
 struct gourTable gtable;
 gtable.entry[0]=gourTop;
 gtable.entry[1]=gourTop;
 gtable.entry[2]=gourBot;
 gtable.entry[3]=gourBot;
 pos.x=x; pos.y=y;
 for (;*text;text++)
    {if (!widths[font][(int)*text])
	{/*assert(0);*/
	 continue;
	}
#ifdef JAPAN
     if (font==3)
	{EZ_normSpr(DIR_NOREV,DRAW_GOURAU|ECD_DISABLE|COLOR_1|COMPO_REP,3,
		    mapPic((*text)+firstJFontPic),&pos,&gtable);
	}
     else
#endif
     EZ_normSpr(DIR_NOREV,DRAW_GOURAU|ECD_DISABLE|COLOR_1|COMPO_REP,font,
		charMap[font][(int)*text],&pos,&gtable);
     pos.x+=widths[font][(int)*text]+1;
    }
}

void drawChar(int x,int y,int font,unsigned char text)
{XyInt pos;
 pos.x=x; pos.y=y;
 if (!widths[font][(int)text])
    {/*assert(0);*/
     return;
    }
#ifdef JAPAN
 if (font==3)
    {EZ_normSpr(DIR_NOREV,ECD_DISABLE|COLOR_1|COMPO_REP,3,
		mapPic(text+firstJFontPic),&pos,NULL);
    }
 else
#endif
 EZ_normSpr(DIR_NOREV,ECD_DISABLE|COLOR_1|COMPO_REP,font,
	    charMap[font][(int)text],&pos,NULL);
}

void drawCharShadow(int x,int y,int font,unsigned char text)
{XyInt pos;
 pos.x=x; pos.y=y;
 if (!widths[font][(int)text])
    {/*assert(0);*/
     return;
    }
#ifdef JAPAN
 if (font==3)
    {EZ_normSpr(DIR_NOREV,ECD_DISABLE|COLOR_1|COMPO_REP|COMPO_SHADOW,3,
		mapPic(text+firstJFontPic),&pos,NULL);
    }
 else
#endif
 EZ_normSpr(DIR_NOREV,ECD_DISABLE|COLOR_1|COMPO_SHADOW,font,
	    charMap[font][(int)text],&pos,NULL);
}

int getStringWidth(int font,const unsigned char *c)
{int tot;
 for (tot=0;*c;c++)
    tot+=widths[font][(int)*c]+1;
 return tot-1;
}

int getCharWidth(int font,unsigned char c)
{return widths[font][(int)c];
}

int getFontHeight(int font)
{return heights[font];
}

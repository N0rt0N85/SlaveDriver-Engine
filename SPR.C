#include <machine.h>

#include <sega_mth.h>
#include<sega_scl.h>
#include<sega_spr.h>
#include<string.h>

#include "util.h"
#include "spr.h"
#include "dma.h"


#define BUFFERWRITES 1

static int bank;
static BYTE *commandStart[2],*clutStart,*gourStart[2],*charStart;
static BYTE *ccommand,*cgouraud;

#define MAXNMCHARS 512
#define CMDBUFFERSIZE 256
#define GOURBUFFERSIZE 256

typedef struct
{unsigned short addr; /* (address-VRAM START)/8 */
 short xysize; /* (xs/8)<<8 + ysize */
} CharData;

static CharData chars[MAXNMCHARS];
static int nmChars;

#if BUFFERWRITES
static int commandAreaSize,gourauAreaSize;
static int cmdBufferUsed,totCommand;
static int gourBufferUsed,totGourau;
static struct gourTable gourBuffer[GOURBUFFERSIZE];
static struct cmdTable cmdBuffer[CMDBUFFERSIZE];
#endif

#define ERASEWRITESTARTLINE 110

void EZ_setErase(int eraseWriteEndLine,unsigned short eraseWriteColor)
{struct cmdTable *first=(struct cmdTable *)VRAM_ADDR;
 if (eraseWriteEndLine>0)
    {
     SPR_SetEraseData(eraseWriteColor,0,0,319,eraseWriteEndLine);
    }
 else
    SPR_SetEraseData(eraseWriteColor,0,0,1,1);

 memset(first,0,32);
 first->link=32>>3;
 if (eraseWriteEndLine>=ERASEWRITESTARTLINE)
    {first->control=JUMP_ASSIGN|ZOOM_NOPOINT|DIR_NOREV|FUNC_POLYGON;
     first->drawMode=COLOR_5|ECDSPD_DISABLE;
     first->color=eraseWriteColor;
     first->ax=-160; first->ay=ERASEWRITESTARTLINE-120;
     first->bx= 160; first->by=ERASEWRITESTARTLINE-120;
     first->cx= 160; first->cy=eraseWriteEndLine-120;
     first->dx=-160; first->dy=eraseWriteEndLine-120;
    }
 else
    first->control=SKIP_ASSIGN;
}

void EZ_initSprSystem(int nmCommands,int nmCluts,int nmGour,
		      int eraseWriteEndLine,unsigned short eraseWriteColor)
{int i;
 Uint8 *vram;
 SPR_Initial(&vram);
 SPR_SetEosMode(0);
 commandStart[0]=(BYTE *)64;
 commandStart[1]=commandStart[0]+(nmCommands<<5);
 clutStart=commandStart[1]+(nmCommands<<5);
 gourStart[0]=clutStart+(nmCluts<<5);
 gourStart[1]=gourStart[0]+(nmGour<<3);
 charStart=gourStart[1]+(nmGour<<3);
 nmChars=0;
 for (i=0;i<MAXNMCHARS;i++)
    chars[i].addr=0;
 bank=0;

 EZ_setErase(eraseWriteEndLine,eraseWriteColor);
#if BUFFERWRITES
 cmdBufferUsed=0;
 gourBufferUsed=0;
 commandAreaSize=nmCommands;
 gourauAreaSize=nmGour;
#endif

 {struct cmdTable *end=((struct cmdTable *)VRAM_ADDR)+1;
  memset(end,0,32);
  end->control=CTRL_END;
 }
}

void EZ_setChar(int charNm,int colorMode,int width,int height,BYTE *data)
{int size=width*height;
 assert(charNm>=0);
 assert(charNm<MAXNMCHARS);
 assert(!(width&3));
 /* character is not allocated, allocate it */
 if (colorMode>=COLOR_5)
    size<<=1;
 if (colorMode<=COLOR_1)
    size>>=1;
 assert(!(((int)charStart)&0x1f));
 if (!chars[charNm].addr)
    {chars[charNm].addr=((int)charStart)>>3;
     charStart+=size;
     charStart=(BYTE *)((((int)charStart)+31)&(~0x1f));
     assert(((int)charStart)<1024*512);
     chars[charNm].xysize=((width>>3)<<8)|height;
    }
 if (data)
    {/* copy char data into area */
     validPtr(data);
     dmaMemCpy(data,(BYTE *)((chars[charNm].addr<<3)+VRAM_ADDR),size);
    }
}

void EZ_setLookupTbl(int tblNm,struct sprLookupTbl *tbl)
{assert(tblNm>=0);
 assert(tblNm<=10);
 validPtr(tbl);
 dmaMemCpy(tbl,(BYTE *)(VRAM_ADDR+clutStart+(tblNm<<5)),1<<5);
}

void EZ_openCommand(void)
{bank=!bank;
 ccommand=commandStart[bank];
 cgouraud=gourStart[bank];
 totCommand=0;
 totGourau=0;
#if BUFFERWRITES
 cmdBufferUsed=0;
 gourBufferUsed=0;
#endif
}

#if BUFFERWRITES
static void flushCmdBuffer(void)
{if (cmdBufferUsed+totCommand>commandAreaSize)
    cmdBufferUsed=commandAreaSize-totCommand;
 dmaMemCpy(cmdBuffer,ccommand+VRAM_ADDR,cmdBufferUsed<<5);
 ccommand+=cmdBufferUsed<<5;
 totCommand+=cmdBufferUsed;
 cmdBufferUsed=0;
}

static void flushGourBuffer(void)
{if (gourBufferUsed+totGourau>gourauAreaSize)
    gourBufferUsed=gourauAreaSize-totGourau;
 dmaMemCpy(gourBuffer,cgouraud+VRAM_ADDR,gourBufferUsed<<3);
 cgouraud+=gourBufferUsed<<3;
 totGourau+=gourBufferUsed;
 gourBufferUsed=0;
}

static inline struct cmdTable *getCmdTable(void)
{if (cmdBufferUsed==CMDBUFFERSIZE)
    flushCmdBuffer();
 return cmdBuffer+(cmdBufferUsed++);
}

static inline void setGourPara(struct cmdTable *cmd,struct gourTable *gTable)
{if (!gTable)
    cmd->grshAddr=0;
 else
    {validPtr(gTable);
     if (gourBufferUsed==GOURBUFFERSIZE)
	flushGourBuffer();
     gourBuffer[gourBufferUsed]=*gTable;
     cmd->grshAddr=(((int)cgouraud)>>3)+gourBufferUsed++;
    }
}
#else
static inline struct cmdTable *getCmdTable(void)
{struct cmdTable *ret=(struct cmdTable *)(ccommand+VRAM_ADDR);
 ccommand+=32;
 return ret;
}

static inline void setGourPara(struct cmdTable *cmd,struct gourTable *gTable)
{if (!gTable)
    cmd->grshAddr=0;
else
    {struct gourTable *g=(struct gourTable *)(cgouraud+VRAM_ADDR);
     validPtr(gTable);
     *g=*gTable;
     cmd->grshAddr=(((int)cgouraud)>>3);
     cgouraud+=8;
    }
}
#endif

static inline void setCharPara(struct cmdTable *cmd,short charNm)
{validPtr(cmd);
 cmd->charAddr=chars[charNm].addr;
 cmd->charSize=chars[charNm].xysize;
}

static inline void setDrawPara(struct cmdTable *cmd,short drawMode,
			       short color)
{validPtr(cmd);
 cmd->drawMode=drawMode;
 if ((drawMode&DRAW_COLOR)==COLOR_1)
    cmd->color=(((int)clutStart)+(color<<5))>>3;
 else
    cmd->color=color;
}

void EZ_normSpr(short dir,short drawMode,
		short color,short charNm,XyInt *pos,
		struct gourTable *gTable)
{struct cmdTable *cmd;
 validPtr(pos);
 cmd=getCmdTable();
 cmd->control=((ZOOM_NOPOINT|DIR_NOREV|FUNC_NORMALSP)&~CTRL_DIR) | dir;
 setCharPara(cmd,charNm);
 setDrawPara(cmd,drawMode,color);
 cmd->ax=pos->x;
 cmd->ay=pos->y;
 setGourPara(cmd,gTable);
}

#if 1
struct slaveDrawResult
{XyInt poly[4];
 struct gourTable gtable;
 short tile;
};
void EZ_specialDistSpr(struct slaveDrawResult *sdr,int charNm)
{struct cmdTable *cmd;
 cmd=getCmdTable();
 cmd->control=((ZOOM_NOPOINT|DIR_NOREV|FUNC_DISTORSP)&~CTRL_DIR);
 setCharPara(cmd,charNm);
 setDrawPara(cmd,UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU,0);
 {int i;
  int *from=(int *)sdr->poly,
        *to=(int *)&cmd->ax;
  for (i=4;i;i--)
     *(to++)=*(from++);
 }
 setGourPara(cmd,&sdr->gtable);
}
/* GCC14: like EZ_specialDistSpr2, but reads only rows [v0,v0+vh) of the pattern.
   V-windowing is the ONLY legal source cut: CMDSRCA is an address and CMDSIZE a size, with no
   stride register, so skipping whole rows stays contiguous while a U sub-rectangle shears
   (HW_VDP1.md:160; Mimas measure 2026-08-24).  COLOR_5 = 16 bpp, so a row is width*2 bytes
   = (xysize>>8)*2 units of 8 -- exact for our 64-wide tiles (16 units per row). */
void EZ_specialDistSpr2V(short charNm,int t0,int t1,XyInt *xy,
			 struct gourTable *gTable)
{struct cmdTable *cmd;
 int h,v0,vh;
 validPtr(xy);
 h=chars[charNm].xysize&0xff;
 v0=(t0*h)>>10;            /* t0,t1: fractions of the pattern height, in 1/1024 */
 vh=((t1*h)>>10)-v0;
 if (vh<1) vh=1;
 if (v0+vh>h) vh=h-v0;
 cmd=getCmdTable();
 cmd->control=((ZOOM_NOPOINT|DIR_NOREV|FUNC_DISTORSP)&~CTRL_DIR);
 cmd->charAddr=chars[charNm].addr+v0*((chars[charNm].xysize>>8)*2);
 cmd->charSize=(chars[charNm].xysize&0x3f00)|(vh&0xff);
 setDrawPara(cmd,UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU,0);
 {int i;
  short *from=(short *)xy,
        *to=(short *)&cmd->ax;
  for (i=8;i;i--)
     *(to++)=*(from++);
 }
 setGourPara(cmd,gTable);
}

void EZ_specialDistSpr2(short charNm,XyInt *xy,struct gourTable *gTable)
{struct cmdTable *cmd;
 validPtr(xy);
 cmd=getCmdTable();
 cmd->control=((ZOOM_NOPOINT|DIR_NOREV|FUNC_DISTORSP)&~CTRL_DIR);
 setCharPara(cmd,charNm);
 setDrawPara(cmd,UCLPIN_ENABLE|COLOR_5|HSS_ENABLE|ECD_DISABLE|DRAW_GOURAU,0);
 {int i;
  short *from=(short *)xy,
        *to=(short *)&cmd->ax;
  for (i=8;i;i--)
     *(to++)=*(from++);
 }
 setGourPara(cmd,gTable);
}
#endif

void EZ_distSpr(short dir,short drawMode,
		short color,short charNm,XyInt *xy,
		struct gourTable *gTable)
{struct cmdTable *cmd;
 validPtr(xy);
 cmd=getCmdTable();
 cmd->control=((ZOOM_NOPOINT|DIR_NOREV|FUNC_DISTORSP)&~CTRL_DIR) | dir;
 setCharPara(cmd,charNm);
 setDrawPara(cmd,drawMode,color);
 {int i;
  short *from=(short *)xy,
        *to=(short *)&cmd->ax;
  for (i=8;i;i--)
     *(to++)=*(from++);
 }
 setGourPara(cmd,gTable);
}

void EZ_cmd(struct cmdTable *inCmd)
{struct cmdTable *cmd;
 validPtr(inCmd);
 cmd=getCmdTable();
 qmemcpy(cmd,inCmd,sizeof(struct cmdTable));
}

void EZ_scaleSpr(short dir,short drawMode,
		 short color,short charNm,XyInt *pos,
		 struct gourTable *gTable)
{struct cmdTable *cmd;
 validPtr(pos);
 cmd=getCmdTable();
 cmd->control=((ZOOM_NOPOINT|DIR_NOREV|FUNC_SCALESP)&~CTRL_DIR&~CTRL_ZOOM)|dir;
 setCharPara(cmd,charNm);
 setDrawPara(cmd,drawMode,color);

 if (dir & CTRL_ZOOM)
    {cmd->ax=pos[0].x;
     cmd->ay=pos[0].y;
     cmd->bx=pos[1].x;
     cmd->by=pos[1].y;
    }
 else
    {cmd->ax=pos[0].x;
     cmd->ay=pos[0].y;
     cmd->cx=pos[1].x;
     cmd->cy=pos[1].y;
    }

 setGourPara(cmd,gTable);
}

void EZ_localCoord(short x,short y)
{struct cmdTable *cmd;
 cmd=getCmdTable();
 cmd->control=FUNC_LCOORD;
 cmd->ax=x;
 cmd->ay=y;
}

void EZ_userClip(XyInt *xy)
{struct cmdTable *cmd;
 validPtr(xy);
 cmd=getCmdTable();
 cmd->control=FUNC_UCLIP;
 cmd->ax=xy[0].x;
 cmd->ay=xy[0].y;
 cmd->cx=xy[1].x;
 cmd->cy=xy[1].y;
}

void EZ_sysClip(void)
{struct cmdTable *cmd;
 cmd=getCmdTable();
 cmd->control=FUNC_SCLIP;
 cmd->cx=319;
 cmd->cy=239;
}

void EZ_polygon(short drawMode,short color,XyInt *xy,
		struct gourTable *gTable)
{struct cmdTable *cmd;
 validPtr(xy);
 cmd=getCmdTable();
 cmd->control=ZOOM_NOPOINT | DIR_NOREV | FUNC_POLYGON;
 setDrawPara(cmd,drawMode,color);
 {int i;
  short *from=(short *)xy,
        *to=(short *)&cmd->ax;
  for (i=8;i;i--)
     *(to++)=*(from++);
 }
 setGourPara(cmd,gTable);
}

void EZ_polyLine(short drawMode,short color,XyInt *xy,
		 struct gourTable *gTable)
{struct cmdTable *cmd;
 validPtr(xy);
 cmd=getCmdTable();
 cmd->control=ZOOM_NOPOINT | DIR_NOREV | FUNC_POLYLINE;
 setDrawPara(cmd,drawMode,color);
 {int i;
  short *from=(short *)xy,
        *to=(short *)&cmd->ax;
  for (i=8;i;i--)
     *(to++)=*(from++);
 }
 setGourPara(cmd,gTable);
}

void EZ_line(short drawMode,short color,XyInt *xy,
		struct gourTable *gTable)
{struct cmdTable *cmd;
 validPtr(xy);
 cmd=getCmdTable();
 cmd->control=ZOOM_NOPOINT | DIR_NOREV | FUNC_LINE;
 setDrawPara(cmd,drawMode,color);

 cmd->ax=xy[0].x;
 cmd->ay=xy[0].y;
 cmd->bx=xy[1].x;
 cmd->by=xy[1].y;

 setGourPara(cmd,gTable);
}

int EZ_charNoToVram(int charNm)
{return chars[charNm].addr;
}

void EZ_closeCommand(void)
{
#if BUFFERWRITES
 if (cmdBufferUsed>0)
    flushCmdBuffer();
 if (gourBufferUsed>0)
    flushGourBuffer();
#endif
 /* make first command link to the frame's command table */
 {struct cmdTable *first=(struct cmdTable *)VRAM_ADDR;
  first->link=((int)commandStart[bank])>>3;
 }
 /* make last command point to end command */
 if (ccommand==commandStart[bank])
    {/* no commands */
     struct cmdTable *last=((struct cmdTable *)(VRAM_ADDR+ccommand));
     last->control|=SKIP_ASSIGN;
     last->link=32>>3;
    }
 else
    {struct cmdTable *last=((struct cmdTable *)(VRAM_ADDR+ccommand))-1;
     /* last=((struct cmdTable *)(VRAM_ADDR+commandStart[bank]))+8; */
     last->control|=JUMP_ASSIGN;
     last->link=32>>3;
    }
}

void EZ_executeCommand(void)
{SPR_WRITE_REG(SPR_W_PTMR,1);
}

void EZ_clearCommand(void)
{struct cmdTable *last=((struct cmdTable *)(VRAM_ADDR+commandStart[bank]));
 last->control|=SKIP_ASSIGN;
 last->link=32>>3;
}

/* Commands still free in this bank's list.  Overflowing it is not benign: flushCmdBuffer()
   clamps, the surplus commands are dropped, and a list left without its end marker makes the
   VDP1 run into whatever follows in VRAM -- SPR_WaitDrawEnd() then never returns. */
int EZ_getCmdRoom(void)
{
#if BUFFERWRITES
 int room=commandAreaSize-totCommand-cmdBufferUsed;
 return room<0? 0: room;
#else
 return 0x7fffffff;
#endif
}

/* GCC14: the image's command ledger, for the split-screen budget (SRUINS.C mpBalance): what is
   emitted so far, and the list's size -- past it flushCmdBuffer drops the tail silently. */
int EZ_cmdsUsed(void)
{
#if BUFFERWRITES
 return totCommand+cmdBufferUsed;
#else
 return (ccommand-commandStart[bank])>>5;
#endif
}

int EZ_cmdsCap(void)
{return commandAreaSize;
}

int EZ_getNextCmdNm(void)
{
#if BUFFERWRITES
 int c=(((unsigned int)ccommand)>>5)+cmdBufferUsed;
 int maxc=(((unsigned int)commandStart[bank])>>5)+commandAreaSize;
 if (c>maxc-1)
    c=maxc-1;
 return c;
#else
 return ((unsigned int)ccommand)>>5;
#endif
}

void EZ_linkCommand(int cmdNm,int mode,int to)
{struct cmdTable *cmd;
#if BUFFERWRITES
 /* if command to be linked is still in the command buffer */
 int offs=cmdNm-(((unsigned int)ccommand)>>5);
 if (offs>=0)
    {assert(offs<cmdBufferUsed);
     cmdBuffer[offs].control|=mode;
     cmdBuffer[offs].link=to<<2;
     return;
    }
#endif
 cmd=(struct cmdTable *)(VRAM_ADDR+(cmdNm<<5));
 cmd->control|=mode;
 cmd->link=to<<2;
}

void EZ_clearScreen(void)
{XyInt pos[4];
 pos[0].x=-160; pos[0].y=-120;
 pos[1].x= 160; pos[1].y=-120;
 pos[2].x= 160; pos[2].y= 120;
 pos[3].x=-160; pos[3].y= 120;
 EZ_openCommand();
 EZ_localCoord(160,120);
 EZ_polygon(ECDSPD_DISABLE|COLOR_5,0x8000,pos,NULL);
 EZ_closeCommand();
 SPR_WaitDrawEnd();
 SCL_DisplayFrame();
 EZ_openCommand();
 EZ_localCoord(160,120);
 EZ_polygon(ECDSPD_DISABLE|COLOR_5,0x8000,pos,NULL);
 EZ_closeCommand();
 SPR_WaitDrawEnd();
 SCL_DisplayFrame();
}

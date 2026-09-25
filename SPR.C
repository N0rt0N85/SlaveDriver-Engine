#include <machine.h>

#include <sega_mth.h>
#include<sega_scl.h>
#include<sega_spr.h>
#include<string.h>

#include "util.h"
#include <sega_sys.h>    /* GCC14: SYS_GETSYSCK, the clock the erase width follows */
#include "spr.h"
#include "dma.h"


#define BUFFERWRITES 1

static int bank;
static int listEnd[2];      /* GCC14: one past each bank's last command at its close (CRASH.C) */
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
static int gourTaken;      /* GCC14: tables blocks built elsewhere took from the top (EZ_gourTop) */
static struct gourTable gourBuffer[GOURBUFFERSIZE];
static struct cmdTable cmdBuffer[CMDBUFFERSIZE];
#endif

#define ERASEWRITESTARTLINE 110

void EZ_setErase(int eraseWriteEndLine,unsigned short eraseWriteColor)
{struct cmdTable *first=(struct cmdTable *)VRAM_ADDR;
 if (eraseWriteEndLine>0)
    {
     /* GCC14: the erase covers the whole raster, so it follows the dot clock -- at 352 the
	right 32 columns would otherwise hold power-on framebuffer garbage. */
     SPR_SetEraseData(eraseWriteColor,0,0,SYS_GETSYSCK? 351: 319,eraseWriteEndLine);
    }
 else
    SPR_SetEraseData(eraseWriteColor,0,0,1,1);

 memset(first,0,32);
 first->link=32>>3;
 if (eraseWriteEndLine>=ERASEWRITESTARTLINE)
    {first->control=JUMP_ASSIGN|ZOOM_NOPOINT|DIR_NOREV|FUNC_POLYGON;
     first->drawMode=COLOR_5|ECDSPD_DISABLE;
     first->color=eraseWriteColor;
     /* GCC14: this is command 0 -- it runs BEFORE any local coordinate of the frame being built,
	so it inherits the one the PREVIOUS list left, which on the 352 arm is 176 and not 160
	(viewOrgOff above).  Translating it by 16 would leave screen columns 0..15 to the hardware
	erase alone, which does not finish a framebuffer in one vblank: a strip of last frame's
	garbage down the left.  So it is WIDENED, not moved -- +/-192 covers the whole 0..351
	raster for either origin, at 32 columns of extra walk (~0.25 ms). */
     {int h=SYS_GETSYSCK? 192: 160;
      first->ax=-h;   first->ay=ERASEWRITESTARTLINE-120;
      first->bx= h;   first->by=ERASEWRITESTARTLINE-120;
      first->cx= h;   first->cy=eraseWriteEndLine-120;
      first->dx=-h;   first->dy=eraseWriteEndLine-120;
     }
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
 gourTaken=0;
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
{if (gourBufferUsed+totGourau>gourauAreaSize-gourTaken)
    gourBufferUsed=gourauAreaSize-gourTaken-totGourau;
 if (gourBufferUsed<0)
    gourBufferUsed=0;
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

/* GCC14: the 352-dot raster is 32 dots wider than the picture, which stayed 320 (the projection
   is isotropic, so no width/focal pair reproduces the 320 image at 352, and widening the render
   costs more than the clock gives -- the measured bill is in the 2026-09-25 study).  viewOrgOff
   carries the 16-dot centring for the WHOLE program, in the only two commands that place
   anything: the LOCAL COORDINATE, an additive offset the hardware applies to every command after
   it -- so it moves the world, the things, the gun, the HUD and the text together -- and the USER
   CLIP, which has to be moved by hand because the hardware does NOT add the local coordinate to
   the clipping coordinates (HW_VDP1.md: "the clipping area does not move").
   Every caller therefore stays written for a 320-wide picture, in solo and in split screen alike.
   What must NOT follow it is anything that addresses the RASTER: the erase data, the system clip
   and the erase polygon below -- those are 0..351 and are handled where they are written.
   SRUINS.C main() sets it; INIT leaves it at 0 (a cold boot is always 320 dots). */
int viewOrgOff;

void EZ_localCoord(short x,short y)
{struct cmdTable *cmd;
 cmd=getCmdTable();
 cmd->control=FUNC_LCOORD;
 cmd->ax=x+viewOrgOff;
 cmd->ay=y;
}

void EZ_userClip(XyInt *xy)
{struct cmdTable *cmd;
 validPtr(xy);
 cmd=getCmdTable();
 cmd->control=FUNC_UCLIP;
 cmd->ax=xy[0].x+viewOrgOff;
 cmd->ay=xy[0].y;
 cmd->cx=xy[1].x+viewOrgOff;
 cmd->cy=xy[1].y;
}

void EZ_sysClip(void)
{struct cmdTable *cmd;
 cmd=getCmdTable();
 cmd->control=FUNC_SCLIP;
 /* GCC14: the drawing area is the RASTER, not the picture -- at 352 the erase polygon and the
    centred picture both reach past 319, and a system clip left at 319 walks those columns and
    then throws them away, leaving them unerased. */
 cmd->cx=SYS_GETSYSCK? 351: 319;
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

/* GCC14: blocks of commands built elsewhere -- the slave's cells (WALLS.C drawSlaveWalls).
   Their gouraud tables take the top of the bank's area, down from what the image's earlier
   blocks took: EZ_gourTop() is the grshAddr one past the next block's first table, whose k-th
   table is EZ_gourTop()-1-k.  EZ_appendGourTop puts n of them there, g[0] being the lowest; the
   list's own tables keep the bottom, and their room shrinks by what the blocks took. */
int EZ_gourTop(void)
{return (((int)gourStart[bank])>>3)+gourauAreaSize-gourTaken;
}

void EZ_appendGourTop(struct gourTable *g,int n)
{int top=EZ_gourTop(),room;
#if BUFFERWRITES
 if (gourBufferUsed>0)
    flushGourBuffer();
 room=gourauAreaSize-gourTaken-totGourau;
#else
 room=gourauAreaSize-gourTaken-((cgouraud-gourStart[bank])>>3);
#endif
 if (n>room)            /* past the area: the lowest go, as flushGourBuffer drops the last */
    {g+=n-room;
     n=room;
    }
 if (n<=0)
    return;
 dmaMemCpy(g,(BYTE *)VRAM_ADDR+((top-n)<<3),n<<3);
 gourTaken+=n;
}

/* the block itself goes in where the next command would, by DMA: what is buffered first, and
   the list's end cuts it as it cuts the rest (flushCmdBuffer).  Returns the number of its first
   command. */
int EZ_appendCmds(struct cmdTable *cmds,int n)
{int first;
#if BUFFERWRITES
 if (cmdBufferUsed>0)
    flushCmdBuffer();
 if (n>commandAreaSize-totCommand)
    n=commandAreaSize-totCommand;
 totCommand+=n>0? n: 0;
#endif
 first=((unsigned int)ccommand)>>5;
 if (n>0)
    {dmaMemCpy(cmds,ccommand+VRAM_ADDR,n<<5);
     ccommand+=n<<5;
    }
 return first;
}

/* the pattern of such a command: tile charNm, rows [t0,t1) of it in 1/1024 -- the fields
   EZ_specialDistSpr2V fills, or EZ_specialDistSpr2 for the whole of it (t0 0, t1 1024) */
void EZ_setCharWindow(struct cmdTable *cmd,short charNm,int t0,int t1)
{if (t0>0 || t1<1024)
    {int h=chars[charNm].xysize&0xff,v0,vh;
     v0=(t0*h)>>10;
     vh=((t1*h)>>10)-v0;
     if (vh<1) vh=1;
     if (v0+vh>h) vh=h-v0;
     cmd->charAddr=chars[charNm].addr+v0*((chars[charNm].xysize>>8)*2);
     cmd->charSize=(chars[charNm].xysize&0x3f00)|(vh&0xff);
    }
 else
    {cmd->charAddr=chars[charNm].addr;
     cmd->charSize=chars[charNm].xysize;
    }
}

void EZ_closeCommand(void)
{
#if BUFFERWRITES
 if (cmdBufferUsed>0)
    flushCmdBuffer();
 if (gourBufferUsed>0)
    flushGourBuffer();
#endif
 listEnd[bank]=((int)ccommand)>>5;
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

/* GCC14: the freeze report (CRASH.C).  In command numbers (VRAM address / 32): info[0] the bank
   last opened, info[1..2] bank 0's first command and one past its last as its close left them,
   info[3..4] the same for bank 1.  The VDP1 draws the bank closed before the last one. */
void EZ_listInfo(int *info)
{info[0]=bank;
 info[1]=((int)commandStart[0])>>5;
 info[2]=listEnd[0];
 info[3]=((int)commandStart[1])>>5;
 info[4]=listEnd[1];
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

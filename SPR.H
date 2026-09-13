struct cmdTable
{/* Sprite Command Table */
 Uint16  control;                    /* control word                     */
 Uint16  link;                       /* command link                     */
 Uint16  drawMode;                   /* draw mode                        */
 Uint16  color;                      /* color info.                      */
 Uint16  charAddr;                   /* character address                */
 Uint16  charSize;                   /* character size                   */
 Sint16  ax;                         /* point A x                        */
 Sint16  ay;                         /* point A y                        */
 Sint16  bx;                         /* point B x                        */
 Sint16  by;                         /* point B y                        */
 Sint16  cx;                         /* point C x                        */
 Sint16  cy;                         /* point C y                        */
 Sint16  dx;                         /* point D x                        */
 Sint16  dy;                         /* point D y                        */
 Uint16  grshAddr;                   /* gouraud shading table address    */
 Uint16  dummy;                      /* dummy area                       */
};

struct gourTable
{Uint16  entry[4];
};

struct sprLookupTbl
{Uint16  entry[16];
};


#define  CTRL_END            0x8000     /* control word end bit mask        */
#define  CTRL_SKIP           0x4000     /* control word skip bit mask       */
#define  CTRL_JUMP           0x3000     /* control word jump bit mask       */
#define  CTRL_ZOOM           0x0f00     /* control word zoom point bit mask */
#define  CTRL_FUNC           0x000f     /* control word function bit mask   */
#define  CTRL_DIR            0x0030     /* control word char read bit mask  */

#define  DRAW_COMPO          0x0003     /* draw mode word color compose mask*/
#define  DRAW_GOURAU         0x0004     /* draw mode word gouraud shading   */
#define  DRAW_GOURAU_GRAY    0x0804     /* draw mode word gray gour shading */
#define  DRAW_COLOR          0x0038     /* draw mode word color mode mask   */
#define  DRAW_MESH           0x0100     /* draw mode word mesh on           */

#define  COMPO_REP           0x0000     /* color compose reprace mode       */
#define  COMPO_SHADOW        0x0001     /* color compose shadow mode        */
#define  COMPO_HARF          0x0002     /* color compose harf luminance mode*/
#define  COMPO_TRANS         0x0003     /* color compose trunslucent mode   */

#define  FUNC_NORMALSP       0x0000     /* draw normal sprite function      */
#define  FUNC_SCALESP        0x0001     /* draw scaled sprite function      */
#define  FUNC_DISTORSP       0x0002     /* draw distorted sprite function   */
#define  FUNC_POLYGON        0x0004     /* draw polygon function            */
#define  FUNC_POLYLINE       0x0005     /* draw polyline function           */
#define  FUNC_LINE           0x0006     /* draw line function               */
#define  FUNC_SCLIP          0x0009     /* set system clipping function     */
#define  FUNC_UCLIP          0x0008     /* set user clipping function       */
#define  FUNC_LCOORD         0x000a     /* set local coordinate function    */
#define  FUNC_TEXTURE        0x0004     /* texture command group mask       */

#define  JUMP_NEXT           0x0000     /* jump next command                */
#define  JUMP_ASSIGN         0x1000     /* jump assign command              */
#define  JUMP_CALL           0x2000     /* call assign command              */
#define  JUMP_RETURN         0x3000     /* return command subroutine        */
#define  SKIP_NEXT           0x4000     /* skip next command                */
#define  SKIP_ASSIGN         0x5000     /* skip assign command              */
#define  SKIP_CALL           0x6000     /* skip call assign command         */
#define  SKIP_RETURN         0x7000     /* skip return command subroutine   */

#define  ZOOM_NOPOINT        0x0000     /* zoom no point                    */
#define  ZOOM_TL             0x0500     /* zoom point top left              */
#define  ZOOM_TM             0x0600     /* zoom point top middle            */
#define  ZOOM_TR             0x0700     /* zoom point top right             */
#define  ZOOM_ML             0x0900     /* zoom point middle left           */
#define  ZOOM_MM             0x0a00     /* zoom point center                */
#define  ZOOM_MR             0x0b00     /* zoom point middle right          */
#define  ZOOM_BL             0x0c00     /* zoom point bottom left           */
#define  ZOOM_BM             0x0e00     /* zoom point bottom middle         */
#define  ZOOM_BR             0x0f00     /* zoom point bottom right          */

#define  DIR_NOREV           0x0000     /* char read not reverse            */
#define  DIR_TBREV           0x0020     /* char read top/bottom reverse     */
#define  DIR_LRREV           0x0010     /* char read left/right reverse     */
#define  DIR_LRTBREV         0x0030     /* char read left/right/top/bot rev */

#define  ECD_DISABLE         0x0080     /* ECD disabe & SPD enable          */
#define  SPD_DISABLE         0x0040     /* ECD enable & SPD disable         */
#define  ECDSPD_DISABLE      0x00c0     /* ECD & SPD disable                */
#define  UCLPIN_ENABLE       0x0400     /* CLIP IN enable                   */
#define  UCLPOUT_ENABLE      0x0600     /* CLIP OUT enable                  */
#define  HSS_ENABLE          0x1000     /* HSS enable                       */
#define  PCLP_ENABLE         0x0800     /* PCLP disable                     */

#define  COLOR_0             0x0000     /* 4 bit/pixel & 16 color bank mode */
#define  COLOR_1             0x0008     /* 4 bit/pixel & 16 color lookup tbl*/
#define  COLOR_2             0x0010     /* 8 bit/pixel &  64 color bank mode*/
#define  COLOR_3             0x0018     /* 8 bit/pixel & 128 color bank mode*/
#define  COLOR_4             0x0020     /* 8 bit/pixel & 256 color bank mode*/
#define  COLOR_5             0x0028     /* 16 bit/pixel & RGB mode          */


void EZ_initSprSystem(int nmCommands,int nmCluts,int nmGour,
		      int eraseWriteEndLine,unsigned short eraseWriteColor);
void EZ_setErase(int eraseWriteEndLine,unsigned short eraseWriteColor);
void EZ_setChar(int charNm,int colorMode,int width,int height,BYTE *data);
void EZ_setLookupTbl(int tblNm,struct sprLookupTbl *tbl);
void EZ_openCommand(void);
void EZ_normSpr(short dir,short drawMode,short color,short charNm,XyInt *pos,
		struct gourTable *gTable);
void EZ_distSpr(short dir,short drawMode,short color,short charNm,XyInt *xy,
		struct gourTable *gTable);
void EZ_cmd(struct cmdTable *inCmd);
void EZ_scaleSpr(short dir,short drawMode,short color,short charNm,XyInt *pos,
		 struct gourTable *gTable);
void EZ_localCoord(short x,short y);
void EZ_sysClip(void);
void EZ_polygon(short drawMode,short color,XyInt *xy,
		struct gourTable *gTable);
void EZ_polyLine(short drawMode,short color,XyInt *xy,
		 struct gourTable *gTable);
void EZ_line(short drawMode,short color,XyInt *xy,
		struct gourTable *gTable);
int EZ_charNoToVram(int charNm);
void EZ_userClip(XyInt *xy);

void EZ_closeCommand(void);
void EZ_executeCommand(void);
void EZ_clearCommand(void);

int EZ_getNextCmdNm(void);
int EZ_getCmdRoom(void);
void EZ_specialDistSpr2(short charNm,XyInt *xy,struct gourTable *gTable);
void EZ_specialDistSpr2V(short charNm,int t0,int t1,XyInt *xy,
			 struct gourTable *gTable);
void EZ_linkCommand(int cmd,int mode,int to);

void EZ_clearScreen(void);

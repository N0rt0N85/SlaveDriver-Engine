#ifndef __INCLUDEDmenuh
#define __INCLUDEDmenuh

#ifdef SEGA_MTH_H
typedef struct
{int type;
 Fixed32 xp,yp;
 short x1,y1,x2,y2,xv,yv;
 int w,h;
 int color,code;
 char *text;
} DlgItem;

extern DlgItem dlgItem[];
extern unsigned short lastUsedSelButton;
#endif

void dlg_clear(void);
void dlg_addRect(int x,int y,int w,int h,int color);
void dlg_addText(int x,int y,int w,char *text);
void dlg_addFontText(int x,int y,int w,int font,char *text);
void dlg_addCenteredText(int x,int y,int w,char *text);
void dlg_addButton(int pressCode,int x,int y,int w,int h,char *text);
void dlg_addBigWavyButton(int pressCode,int x,int y,char *text);
void dlg_addGameButton(int x,int y,int gameNm);
void dlg_addFontString(int x,int y,int font,char *text);
#ifdef JAPAN
void dlg_addBigWavyJapaneseButton(int pressCode,int x,int y,char *text);
#endif

void dlg_selectButton(int b);
void dlg_centerStuff(void);
void dlg_setCancel(int code);   /* GCC14: B returns this code instead of selecting (MENU.C) */

enum {MENUMOVE_FREE,MENUMOVE_HORZ,MENUMOVE_VERT};
int dlg_run(int selSound,int pushSound,int movement);
void dlg_runMessage(char *message,int w);
void dlg_flashMessage(char *message1,char *message2,int w,int h);
int dlg_runYesNo(char *message,int w);
void runInventory(int inventory,int keyMask,int *mapState,
		  int fade,int fadeButton,int fadeSelection);

void dlg_init(int fd);
/* GCC14: what the menu picture stream managed last time (SRUINS.C overlay "P:") */
extern int menuStreamPics,menuStreamW;

void dlg_setupSlideIn(void);
void dlg_setupSlideOut(void);
void dlg_setupSlideUp(void);
void dlg_setupNoSlide(void);
void dlg_runSlideIn(void);

int runTravelQuestion(char *destination);

#endif

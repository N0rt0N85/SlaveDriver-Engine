/* plrcyl.h -- PLAYER MODEL "cylinder", pulled in by SPRITE.H when gameparams.cfg says
   PLAYER_MODEL = cylinder.  The camera sprite stops being a ball and becomes a vertical cylinder:
   `radius` still gives the horizontal half-width, but the body now spans
   [pos.y-cylFoot+cylStep, pos.y+cylHead] vertically, and pos.y is the EYE.
   Every number comes from gameparams.cfg (see PLRCYL.C); nothing here is game-specific. */
#ifndef __INCLUDEDplrcylh
#define __INCLUDEDplrcylh
extern Sprite *camera;
extern Fixed32 cylFoot,cylHead,cylStep;
#define CYL_CAM(o)         ((o)==camera)
#define SPR_FOOT(o)        (CYL_CAM(o)?cylFoot:(o)->radius)
#define SPR_HOVER(o)       (CYL_CAM(o)?0:F(GP_PLAYER_EYE_HOVER))
#define SPR_HEAD(o)        (CYL_CAM(o)?cylHead:(o)->radius)
#define SPR_SQUISH(o,c,fl) (CYL_CAM(o)?(c)-cylHead:(((c)+(fl))>>1))
#define SPR_STEP(o)        (CYL_CAM(o)?cylStep:F(GP_PLAYER_STEP))
#define SPR_YLO(o)         (CYL_CAM(o)?(o)->pos.y-cylFoot+cylStep:(o)->pos.y)
#define SPR_YHI(o)         (CYL_CAM(o)?(o)->pos.y+cylHead:(o)->pos.y)
#define SPR_YOUT(o,d)      (CYL_CAM(o)?F(4096):(d))   /* body clear of the edge: no contact */
#define SPR_SUPPORT(o,w)   ((CYL_CAM(o)&&(w)->normal[1]>0)?f(cylFoot-(o)->radius)*(w)->normal[1]:0)
#define SPR_DY(m,s)        cylDY((m),(s))
#define CFG_PLAYER_INIT(s) cylPlayerInit(s)
#define CFG_POSTURE(in,p)  cylPosture((in),(p))
Fixed32 cylDY(Sprite *m,Sprite *s);
void cylPlayerInit(Sprite *s);
void cylPosture(unsigned short input,unsigned short pushed);
int cylWeaponNext(unsigned int mask,int d);
void cylShrink(void);
#endif

#ifndef __INCLUDEDwallsh
#define __INCLUDEDwallsh

#define FOCALDIST   160

/* SONDE (STATUSTEXT) -- plus grande coordonnee projetee |x| ou |y| depuis le dernier
   affichage, mesuree AVANT bornage par clampVDP1() (wallasm.h).  Le VDP1 n'accepte que
   -1024..1023 et au-dela "operation cannot be guaranteed" (HW_VDP1.md:184).  Sert a
   trancher : magnification normale du plan proche (~1e3) ou geometrie corrompue (>>1e4).
   -fcommon fusionne cette definition provisoire, donc zero ligne ajoutee dans un .C. */
/* Pipeline de traversee -- 0 arret, 1 mesure (resultat jete), 2 levier.  Le pourquoi et les
   conditions de validite sont en tete de WALLS.C. */
#define WALLPIPE 2
void wallsPipeKick(MthMatrix *view);
void wallsPipeJoin(void);
void wallsPipeDiscard(void);  /* joint puis jette : la camera va bouger, ou le niveau partir */
extern int pipeSpin;          /* tours d'attente a la jointure ; -1 = rien n'etait en vol */

/* Brume de profondeur : table de 256 octets indexee par z>>24, lue par l'assembleur.
   setFog(d) = un secteur a pleine lumiere atteint le noir a d unites.  4096 rend le
   comportement d'origine (plus loin que toute ligne de vue, donc pas de brume). */
extern int fogDist;
void setFog(int dist);

/* LOD par lumiere (WALLS.C, en tete de drawRectWall) : un mur que la brume a deja rendu noir
   partout devient UN quad plat.  0 = jamais, 1 = fusion, 2 = fusion peinte en bleu pour voir
   lesquels partent.  lodFused/lodCells = murs fusionnes et cellules economisees dans l'image. */
extern int lodEnable,lodFused,lodCells,lodFlat;
extern int nmMeshPolys;   /* cellules venues du chemin maillage : sols, plafonds, murs courbes */
extern int lodWeldWhy[2]; /* soudures refusees : [0] hors bande, [1] sommet hors de l'arete */

int vdp1NmClipped;  /* cellules fenetrees en V dans l'image : remplace les maxima cx/cy */

int vdp1MaxCoord;   /* max(|x|,|y|) -- conserve pour compatibilite d'affichage */
int vdp1MaxX,vdp1MaxY;  /* separes : dit SUR QUEL AXE on sort de la plage */
static __inline__ int vdp1TakeMaxX(void){int m=vdp1MaxX; vdp1MaxX=0; return m;}
static __inline__ int vdp1TakeMaxY(void){int m=vdp1MaxY; vdp1MaxY=0; return m;}
#define PROJECT(x,z) ( ((x)*(FOCALDIST>>4))/(int)((z)>>4) )

void drawWalls(MthMatrix *view);
void drawWallsFinish(void);
void wallRenderSlaveMain(void);
void startSlave(void *slaveMain);
void drawSprites(MthXyz *playerPos,MthMatrix *view,int sector);
void initWallRenderer(void);
void removeLight(Sprite *s);
void addLight(Sprite *s,int r,int g,int b);
void changeLightColor(Sprite *s,int r,int g,int b);

void initWater(void);
void stepWater(void);

extern int nmPolys,nmSlavePolys;
extern Sprite *autoTarget;
extern MthXyz autoTFormedPos;

extern short plaxBBymax,plaxBBxmax,plaxBBymin,plaxBBxmin,mipBase;
extern int mipEnable; /* GCC14: runtime mip toggle, see WALLS.C */
#endif

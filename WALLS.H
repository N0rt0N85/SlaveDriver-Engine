#ifndef __INCLUDEDwallsh
#define __INCLUDEDwallsh

#define FOCALDIST   160

/* GCC14: PROBE (STATUSTEXT) -- largest projected |x| or |y| since the last display, measured
   BEFORE clampVDP1() (wallasm.h).  The VDP1 takes only -1024..1023 (HW_VDP1.md:184): tells
   normal near-plane magnification (~1e3) from corrupt geometry (>>1e4).  -fcommon merges
   this tentative definition, so no line in a .C. */
/* GCC14: traversal pipeline -- 0 off, 1 measure (result discarded), 2 lever.  Why, and when
   it is valid: top of WALLS.C. */
#define WALLPIPE 2
void wallsPipeKick(MthMatrix *view);
void wallsPipeJoin(void);
void wallsPipeDiscard(void);  /* join then discard: the camera will move, or the level go */
extern int pipeSpin;          /* spins at the join; -1 = nothing in flight */

/* GCC14: depth fog: 256-byte table indexed by z>>24, read by the asm.  setFog(d): a fully
   lit sector reaches black at d units.  4096 gives the original behaviour (no fog). */
extern int fogDist;
void setFog(int dist);

/* GCC14: light LOD (WALLS.C, top of drawRectWall): a wall the fog has turned black
   everywhere becomes ONE flat quad.  0 = never, 1 = fuse, 2 = fuse painted blue.
   lodFused/lodCells = walls fused and cells saved in the frame. */
extern int lodEnable,lodFused,lodCells,lodFlat;
/* GCC14: far LOD (WALLS.C wallIsFar): a wall wholly beyond lodFar units folds into one quad painted
   in its first tile's first texel, a mesh wall welds its strips of one tile.  0 = off. */
extern int lodFar;

/* GCC14: PROBE (WALKPROBE) -- the VDP1's walk over the frame's cells, WALLASM.H
   vdp1WalkProbe: all cells / the cells over VDP1BIGWALK / the same walk with the walls drawn
   from patterns turned a quarter, in units of 64 pixels */
int vdp1Walk,vdp1Big,vdp1BigWalk,vdp1RotWalk;
/* The same for the list the VDP1 is drawing, the previous image's: saved by drawWalls before it
   counts the next one, for the freeze report (CRASH.C) */
int vdp1PrevWalk,vdp1PrevMaxX,vdp1PrevMaxY;

int vdp1MaxCoord;   /* kept for display compatibility */
int vdp1MaxX,vdp1MaxY;  /* separate: says WHICH AXIS leaves the range (walk probe, per image) */
static __inline__ int vdp1TakeMaxX(void){int m=vdp1MaxX; vdp1MaxX=0; return m;}
static __inline__ int vdp1TakeMaxY(void){int m=vdp1MaxY; vdp1MaxY=0; return m;}
/* GCC14: split screen.  The window of the view being drawn, and its focal (65 deg over 160 px
   in 2-4p, 90 deg over 320 in solo).  Set per view by SRUINS.C mpSetViewport. */
extern int viewXmin,viewXmax,viewYmin,viewYmax,viewCx,viewCy,focalDist;
#define PROJECT(x,z) ( ((x)*(focalDist>>4))/(int)((z)>>4) )

void drawWalls(int view,MthMatrix *matrix);   /* view: 0 solo; 0..n-1 in split screen */
/* GCC14: split screen, views 1.. traversed by the slave (WALLS.C TravSet).  At the level start
   wallsSplitReset; wallsSplitAlloc once the level has several players; each image, before
   drawWalls(0), wallsQueueView(1..n-1) with the matrix and camera each view is drawn from;
   wallsQueueJoin before the geometry moves. */
void wallsSplitReset(void);
void wallsSplitAlloc(void);
void wallsQueueView(int k,MthMatrix *view,Sprite *c);
void wallsQueueJoin(void);
void drawWallsFinish(void);
void wallRenderSlaveMain(void);
void startSlave(void *slaveMain);
void drawSprites(MthXyz *playerPos,MthMatrix *view,int sector);
void initWallRenderer(void);
void removeLight(Sprite *s);
void addLight(Sprite *s,int r,int g,int b);
void changeLightColor(Sprite *s,int r,int g,int b);
void addLightEx(Sprite *s,int r,int g,int b,int radius,int peak);    /* GCC14: k 0..16, radius, intensity 0..31 */
void changeLightEx(Sprite *s,int r,int g,int b,int radius,int peak); /* GCC14: in the light's model; radius <= 0 keeps it */
int  hasLight(Sprite *s);                       /* GCC14: no duplicates in the list */

void initWater(void);
void stepWater(void);

extern int nmPolys,nmSlavePolys;
#define MAXNMSLAVEPOLYS 1300    /* the slave's cell records; it drops a wall past this less 50 */
extern Sprite *autoTarget;
extern MthXyz autoTFormedPos;

extern short plaxBBymax,plaxBBxmax,plaxBBymin,plaxBBxmin,mipBase;
extern int mipEnable; /* GCC14: runtime mip toggle, see WALLS.C */
#endif

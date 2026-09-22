#ifndef __INCLUDEDlevelh
#define __INCLUDEDlevelh

#include "slevel.h"

extern sVertexType *level_vertex;
extern sFaceType *level_face;
extern sWallType *level_wall;
extern sSectorType *level_sector;
extern unsigned char *level_texture;
extern unsigned char (*level_cutPlane)[][MAXCUTSECTORS];
extern sOrderPair *level_orderPair;
extern int level_nmOrderPairs;
extern unsigned char *level_reject;
extern int level_nmRejectClasses;
extern unsigned char *level_vertexLight;
extern unsigned char *level_objectParams;
extern sObjectType *level_object;
extern sPBType *level_pushBlock;
extern sPBVertex *level_PBVert;
extern short *level_PBWall;
extern WaveVert *level_waveVert;
extern WaveFace *level_waveFace;
extern int level_nmSectors;
extern int level_nmWalls;
extern int level_nmObjects;
extern int level_nmPushBlocks;
extern int level_nmWaveVert;
extern int level_nmVertex;

int loadLevel(int fd,int tileBase);
int level_maySee(int s1,int s2);        /* GCC14: 0 when the reject table says never */

#endif

#include"level.h"
#include"file.h"
#include"util.h"

sVertexType *level_vertex;
sFaceType *level_face;
sWallType *level_wall;
sSectorType *level_sector;
unsigned char *level_texture;
unsigned char *level_vertexLight;
sObjectType *level_object;
unsigned char *level_objectParams;
sPBType *level_pushBlock;
sPBVertex *level_PBVert;
short *level_PBWall;
WaveVert *level_waveVert;
WaveFace *level_waveFace;
unsigned char (*level_cutPlane)[][MAXCUTSECTORS];

int level_nmSectors;
int level_nmWalls;
int level_nmObjects;
int level_nmPushBlocks;
int level_nmWaveVert;
int level_nmVertex;

#define LOADPART(array,type,number) \
 size=number*sizeof(type);\
 array=(type *)mem_malloc(1,size);\
 fs_read(fd,(char *)array,size);

int loadLevel(int fd,int tileBase)
{int size;
 int i;
 struct sLevelHeader *head;
 assert(fd>=0);
 fs_read(fd,(char *)&size,4);
 dPrint("level size=%d\n",size);
 dPrint("coreleft=%d\n",mem_coreleft(1));
 assert(size>0);
 assert(size<900000);
 head=(struct sLevelHeader *)mem_malloc(1,sizeof(struct sLevelHeader));
 fs_read(fd,(char *)head,sizeof(struct sLevelHeader));
 level_nmSectors=head->nmSectors;
 level_nmWalls=head->nmWalls;
 level_nmObjects=head->nmObjects;
 level_nmPushBlocks=head->nmPushBlocks;
 level_nmWaveVert=head->nmWaveVert;
 level_nmVertex=head->nmVerticies;

 LOADPART(level_sector,sSectorType,head->nmSectors);
 LOADPART(level_wall,sWallType,head->nmWalls);
 LOADPART(level_vertex,sVertexType,head->nmVerticies);
 LOADPART(level_face,sFaceType,head->nmFaces);
 LOADPART(level_object,sObjectType,head->nmObjects);
 LOADPART(level_pushBlock,sPBType,head->nmPushBlocks);
 LOADPART(level_PBVert,sPBVertex,head->nmPBVert);
 LOADPART(level_waveVert,WaveVert,head->nmWaveVert);
 LOADPART(level_waveFace,WaveFace,head->nmWaveFace);
 LOADPART(level_PBWall,short,head->nmPBWalls);
 LOADPART(level_objectParams,unsigned char,head->nmObjectParams);
 LOADPART(level_texture,unsigned char,head->nmTextureIndexes);
 LOADPART(level_vertexLight,char,head->nmLightValues);
 /* GCC14: LOADPART used a cast as lvalue (GCC 2.x extension); expanded by hand */
 size=(head->nmCutSectors*MAXCUTSECTORS)*sizeof(char);
 level_cutPlane=(unsigned char (*)[][MAXCUTSECTORS])mem_malloc(1,size);
 fs_read(fd,(char *)level_cutPlane,size);

 for (i=1;i<head->nmTextureIndexes;i+=2)
    level_texture[i]+=tileBase;
 for (i=0;i<head->nmFaces;i++)
    level_face[i].tile+=tileBase;

 /* paranoia checks */
 assert(!(((int)level_sector) & 3));
 assert(!(((int)level_wall) & 3));
 assert(!(((int)level_vertex) & 3));
 assert(!(((int)level_face) & 3));
 assert(!(((int)level_object) & 3));

 return 1;
}

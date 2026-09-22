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
sOrderPair *level_orderPair;
int level_nmOrderPairs;
unsigned char *level_reject;
int level_nmRejectClasses;

int level_nmSectors;
int level_nmWalls;
int level_nmObjects;
int level_nmPushBlocks;
int level_nmWaveVert;
int level_nmVertex;

/* GCC14: whether leaves s1 and s2 may see each other -- 0 only when the level's reject table
   (SLEVEL.H) says they never do; 1 without a table.  canSee reads it before it traces a line, as
   Doom's P_CheckSight reads its REJECT; the Doom lamps read it to go out unseen (DOOM_GAME.C). */
int level_maySee(int s1,int s2)
{int a,b;
 if (!level_reject)
    return 1;
 assert(s1>=0 && s1<level_nmSectors && s2>=0 && s2<level_nmSectors);
 a=level_sector[s1].rejectClass;
 b=level_sector[s2].rejectClass;
 if (a>b)
    {int t=a; a=b; b=t;}
 a=a*level_nmRejectClasses-((a*(a-1))>>1)+b-a;
 return !(level_reject[a>>3] & (1<<(a&7)));
}

#define LOADPART(array,type,number) \
 size=number*sizeof(type);\
 array=(type *)mem_malloc(1,size);\
 fs_read(fd,(char *)array,size);\
 used+=size;

int loadLevel(int fd,int tileBase)
{int size;
 int total,used;
 int i;
 struct sLevelHeader *head;
 assert(fd>=0);
 fs_read(fd,(char *)&size,4);
 dPrint("level size=%d\n",size);
 dPrint("coreleft=%d\n",mem_coreleft(1));
 assert(size>0);
 assert(size<900000);
 total=size;
 head=(struct sLevelHeader *)mem_malloc(1,sizeof(struct sLevelHeader));
 fs_read(fd,(char *)head,sizeof(struct sLevelHeader));
 used=sizeof(struct sLevelHeader);
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
 used+=size;

 /* Ordering pairs (SLEVEL.H): an optional block our converter appends after cutPlane.  A retail
    level stops here, so "is it there" is simply "are there bytes left". */
 level_nmOrderPairs=0;
 level_orderPair=NULL;
 if (used<total)
    {fs_read(fd,(char *)&level_nmOrderPairs,4);
     used+=4;
     assert(level_nmOrderPairs>=0);
     if (level_nmOrderPairs)
	{LOADPART(level_orderPair,sOrderPair,level_nmOrderPairs);}
    }
 /* GCC14: the reject table (SLEVEL.H), optional too, after the pairs */
 level_nmRejectClasses=0;
 level_reject=NULL;
 if (used<total)
    {fs_read(fd,(char *)&level_nmRejectClasses,4);
     used+=4;
     assert(level_nmRejectClasses>0);
     LOADPART(level_reject,unsigned char,((level_nmRejectClasses*(level_nmRejectClasses+1)>>1)+7)>>3);
    }
 assert(used==total);

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

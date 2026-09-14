#ifndef __INCLUDEDsequenceh
#define __INCLUDEDsequenceh

#include "slevel.h"

extern int level_nmSequences;
extern int level_nmFrames;
extern int level_nmChunks;

extern short *level_sequence;
extern sFrameType *level_frame;
extern sChunkType *level_chunk;
extern short *level_sequenceMap;

/* sequences for the weapons */
extern short *level_wSequence;
extern sFrameType *level_wFrame;
extern sChunkType *level_wChunk;

int loadSequences(int fd,int tileBase,int soundBase);
int loadWeaponSequences(int fd);

void initWeaponQ(void);
int weaponSequenceQEmpty(void);
int getCurrentWeaponSequence(void);
int getWeaponFrame(void);
void setWeaponFrame(int f);
void queueWeaponSequence(int seqNm,int cx,int cy);
int advanceWeaponSequence(int xbase,int ybase,int hack);
int getWeaponSequenceQSize(void);
void addWeaponSequence(int seqNm);
void clearWeaponQ();
void setWeaponSequence(int seqNm,int cx,int cy);   /* GP_GAME_DOOM only: pin, not queue (SPEC_PLAYER 2.3) */
#endif

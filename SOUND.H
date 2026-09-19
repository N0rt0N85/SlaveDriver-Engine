#ifndef __INCLUDEDsoundh
#define __INCLUDEDsoundh

#define SNDBASE 0x05a00000

struct soundSlotRegister
{unsigned short reg[12];
 int soundNm;
};
void initSoundRegs(int sNm,int vol,int pan,struct soundSlotRegister *ssr);
/* returns pointer to actual sound regs for aftertouch */
struct soundSlotRegister *playSoundMegaE(int source,
					 struct soundSlotRegister *ssr);

extern short level_objectSoundMap[];
extern short level_staticSoundMap[];

enum {ST_JOHN,ST_ITEM,ST_FLAMER,ST_PUSHBLOCK,ST_INTERFACE,ST_RING,
	 ST_BLOWPOT,ST_MANACLE,
	 ST_NMSTATICSOUNDGROUPS};

void initSound(void);
void loadSound(int fd);
void loadDynamicSounds(int fd);
int loadStaticSounds(int fd);
void playSound(int source,int sNm);
void playSoundE(int source,int sNm,int vol,int pan);

void stopSound(int source,int sNm);
void stopAllSound(int source);
void stopAllLoopedSounds(void);
void adjustSounds(int source,int vol,int pan);
int getSoundTop(void);

extern char mapMusic,titleMusic,endMusic,firstVoiceTrack;

void playCDTrackForLevel(int lev);

#define playStaticSound(group,offs) \
   playSound(0,level_staticSoundMap[group]+(offs))

#define stopStaticSound(group,offs) \
   stopSound(0,level_staticSoundMap[group]+(offs))

void testSound(void);

#ifdef SEGA_MTH_H
void posGetSoundParams(MthXyz *pos,int *vol,int *pan);
void posMakeSound(int source,MthXyz *pos,int sndNm);
void posAdjustSound(int source,MthXyz *pos);
#endif
void sound_nextFrame(void);
extern struct __sprite *soundEar[4];   /* GCC14: split screen's listeners (SOUND.C) */
extern int soundNmEars;
void setMasterVolume(int vol);


void restoreSoundState(void);
void saveSoundState(void);
#endif

#include <machine.h>
#include "sega_int.h"
#include "sega_mth.h"
#include "sega_sys.h"
#include "sega_per.h"
#include "file.h"
#include "util.h"

#include "slevel.h"
#include "sound.h"
#include "sruins.h"

static int soundTop=0; /* first unused byte of sound memory */
static unsigned char soundFrame=1;

struct soundSaveData
{int owner;
 short sound;
 short pan,vol;
} soundSave[32];

/* GCC14: split screen.  Every player is an ear (SRUINS.C sets them each image): a positioned
   sound is heard by the nearest one, not by whoever the engine had loaded when it was made.  The
   voices then have a budget: at most SOUND_MPBUDGET one-shots start in an image, the loudest, a
   frame late (sound_nextFrame) -- four players firing, their monsters answering, in one room,
   were a wall of noise on the 32 voices.  And a level that lowers with the number of players, all
   of them mixed into one pair of speakers.  soundNmEars < 2: the engine as it was. */
#define SOUND_MPBUDGET 4
struct __sprite *soundEar[4];
int soundNmEars;
static struct {int source; short sNm,vol,pan;} soundPending[SOUND_MPBUDGET];
static int soundNmPending;

static int slotOwner[32]; /* -1 if empty */
static short slotSound[32];
static char slotDirty[32];

#define SCSP_REG_SET    0x0200
#define ADR_SCSP_REG    ((volatile Uint8 *)0x25b00400)

short level_objectSoundMap[OT_NMTYPES];
short level_staticSoundMap[ST_NMSTATICSOUNDGROUPS];

typedef struct
{int startAddr;
 int loopStart; /* or -1 if no loop */
 unsigned short sampleRate;
 unsigned short size;
 unsigned char lastFrameUsed,bps;
} SoundRec;

#define MAXNMSOUNDS 80

SoundRec sounds[MAXNMSOUNDS];
static int nmSounds;

#define SILENCE 8 /* must be power of 2 */
static char silentQ[SILENCE];
static int qHead,qTail;

void setMasterVolume(int vol)
{POKE_W(SNDBASE+0x100400,0x0200+(vol&0xf));
 /* set dac18, mem4mb & master volume */
}

int getSoundTop(void)
{return soundTop;
}

static void silenceVoice(void)
{static int slot=0;
 while (((qHead+1)&(SILENCE-1))!=qTail)
    {/* q has space for more silence */
     while (1)
	{slot=(slot+1)&0x1f;
	 if (slot==16 || slot==17)
	    continue;
	 if (slotOwner[slot]!=-1 &&
	     sounds[slotSound[slot]].loopStart!=-1)
	    continue;
	 break;
	}
     silentQ[qHead]=slot;
     qHead=(qHead+1)&(SILENCE-1);
     /* GCC14: key off and nothing else.  The word also holds the sample's format and the top of
	its address, read live while the voice sounds: writing 0x1000 turned a voice still playing
	(taken 23 sounds after it started) into 16-bit reads from the bottom 64 KB for the ~1 s of
	its release -- noise, with Doom's 8-bit sounds. */
     POKE_W(0x20*slot+SNDBASE+0x100000+0,
	    (PEEK_W(0x20*slot+SNDBASE+0x100000+0) & 0x7ff) | 0x1000);
     slotOwner[slot]=-1;
    }
}

/* GCC14: the three stops below end with a konex written into slot 0.  It re-read slot "32" --
   the loop's end, the common register, which reads back 0 -- so slot 0 got silenceVoice's old
   0x1000: keyed off whatever it was playing, its format and the top of its address cleared.
   They now re-write slot 0's own word. */
void stopAllLoopedSounds(void)
{int i;
 for (i=0;i<32;i++)
    if (sounds[slotSound[i]].loopStart!=-1)
       {POKE_W(0x20*i+SNDBASE+0x100000,
	       PEEK_W(0x20*i+SNDBASE+0x100000) & 0x7ff);
	/* set key off on all voices without disturbing other settings */
	slotOwner[i]=-1;
       }
 /* konex */
 POKE_W(SNDBASE+0x100000+0,
	PEEK_W(SNDBASE+0x100000) | 0x1000);
}

void stopAllSound(int source)
{int i;
 for (i=0;i<32;i++)
    {if (slotOwner[i]==source)
	{POKE_W(0x20*i+SNDBASE+0x100000,
		PEEK_W(0x20*i+SNDBASE+0x100000) & 0x7ff);
	 /* set key off on all voices without disturbing other settings */
	 slotOwner[i]=-1;
	}
    }
 /* konex */
 POKE_W(SNDBASE+0x100000+0,
	PEEK_W(SNDBASE+0x100000) | 0x1000);
}

void stopSound(int source,int sNm)
{int i;
 for (i=0;i<32;i++)
    {if (slotOwner[i]==source && slotSound[i]==sNm)
	{POKE_W(0x20*i+SNDBASE+0x100000,
		PEEK_W(0x20*i+SNDBASE+0x100000) & 0x7ff);
	 slotOwner[i]=-1;
	 return;
	}
    }
 POKE_W(SNDBASE+0x100000,
	PEEK_W(SNDBASE+0x100000) | 0x1000);
}

static int deQVoice(void)
{int retVal;
 assert(qHead!=qTail);
 retVal=silentQ[qTail];
 qTail=(qTail+1)&(SILENCE-1);
 silenceVoice();
 return retVal;
}

static void initSlot(int i)
{POKE_W(0x20*i+SNDBASE+0x100000+2,   0x0000);  /* start address */
 POKE_W(0x20*i+SNDBASE+0x100000+4,   0x0000);  /* loop start address */
 POKE_W(0x20*i+SNDBASE+0x100000+6,   0x0000);  /* loop end address */
 POKE_W(0x20*i+SNDBASE+0x100000+8,   0x001f);  /* adsr */
 POKE_W(0x20*i+SNDBASE+0x100000+0x0a,0x000f);
 POKE_W(0x20*i+SNDBASE+0x100000+0x0c,0x0000);  /* direct out */
 POKE_W(0x20*i+SNDBASE+0x100000+0x0e,0x0000);
 POKE_W(0x20*i+SNDBASE+0x100000+0x10,0x0000);
 POKE_W(0x20*i+SNDBASE+0x100000+0x12,0x8000);
 POKE_W(0x20*i+SNDBASE+0x100000+0x14,0x0000);
 POKE_W(0x20*i+SNDBASE+0x100000+0x16,0xe000);
}

void initSound(void)
{int i;

 volatile Uint8 *SMPC_SF=(Uint8 *)0x20100063;
 volatile Uint8 *SMPC_COM=(Uint8 *)0x2010001f;

 /* aaaaaaaaaaaarrrrrrrrghhhhhhhhhh!!!!!!!! */
 while ((*SMPC_SF & 0x01)==0x01) ;
 *SMPC_SF=1;
 *SMPC_COM=7; /*SMPC_SNDOFF;*/

 POKE_W(ADR_SCSP_REG, SCSP_REG_SET);

 while ((*SMPC_SF & 0x01)==0x01) ;
#if 0
 *SMPC_SF=1;
 *SMPC_COM=6; /*SMPC_SNDON;*/
 /* must wait 30us for sound cpu to init */
#endif

 soundTop=0;
 nmSounds=0;
 soundNmPending=0;

 setMasterVolume(15);
 /* initialize slots */
 for (i=0;i<32;i++)
    {POKE_W(0x20*i+SNDBASE+0x100000+0,0x0000);  /* koff */
     initSlot(i);
    }
 /* make cd audible.  GCC14: the CD's two channels come in as the effect sends of slots 16
    (left) and 17 (right): byte +0x17 of each, EFSDL 7 and EFPAN hard left / hard right.  The
    second write went to 0x220, slot 17's key byte, so the right channel stayed at EFSDL 0 and
    the left one played centred: the music was mono. */
 POKE_B(SNDBASE+0x100000+0x217,0xff);
 POKE_B(SNDBASE+0x100000+0x237,0xef);

 for (i=0;i<32;i++)
    {slotOwner[i]=-1;
     slotDirty[i]=0;
    }

 qHead=0;
 qTail=0;
 silenceVoice();
 silenceVoice();
 silenceVoice();
 silenceVoice();
}

static void playSoundNow(int source,int sNm,int vol,int pan);

void sound_nextFrame(void)
{int i;
 for (i=0;soundNmEars>1 && i<soundNmPending;i++)   /* the level gone: dropped */
    playSoundNow(soundPending[i].source,soundPending[i].sNm,soundPending[i].vol,soundPending[i].pan);
 soundNmPending=0;
 soundFrame++;
 if (!soundFrame)
    {soundFrame=1;
     for (i=0;i<nmSounds;i++)
	sounds[i].lastFrameUsed=0;
    }
 cdTic();                       /* GCC14: the music's own step (FILE.C) */
}

void loadSound(int fd)
{int size,rate,bps,loopStart;
 int i;
 unsigned short *buffer;
 fs_read(fd,(char *)&size,4);
 fs_read(fd,(char *)&rate,4);
 fs_read(fd,(char *)&bps,4);
 fs_read(fd,(char *)&loopStart,4);
 buffer=(unsigned short *)mem_malloc(0,size);
 assert(buffer);
 fs_read(fd,(char *)buffer,size);
 assert(nmSounds<MAXNMSOUNDS);
 sounds[nmSounds].startAddr=soundTop;
 sounds[nmSounds].sampleRate=rate;
 sounds[nmSounds].size=size;
 sounds[nmSounds].bps=bps;
 sounds[nmSounds].loopStart=loopStart;
 sounds[nmSounds].lastFrameUsed=0;
 nmSounds++;
 assert(soundTop+size<1024*512);
 assert(!(soundTop & 1));
 for (i=0;i<size/2;i++)
    {POKE_W(SNDBASE+soundTop,buffer[i]);
     soundTop+=2;
    }
 mem_free(buffer);
#ifndef NDEBUG
 extraStuff=(512*1024-soundTop)/1024;
#endif
}

int loadSoundSet(int fd,short *out_map,int mapSize)
{int nmSounds,i;
 fs_read(fd,(char *)&nmSounds,4);
 assert(nmSounds==mapSize);
 fs_read(fd,(char *)out_map,2*mapSize);

 fs_read(fd,(char *)&nmSounds,4);
 assert(nmSounds>=0 && nmSounds<MAXNMSOUNDS);
 for (i=0;i<nmSounds;i++)
    loadSound(fd);
 return nmSounds;
}

void loadDynamicSounds(int fd)
{int i;
 int nmStaticSounds=nmSounds;
 loadSoundSet(fd,level_objectSoundMap,OT_NMTYPES);
 for (i=0;i<OT_NMTYPES;i++)
    level_objectSoundMap[i]+=nmStaticSounds;
}

int loadStaticSounds(int fd)
{return loadSoundSet(fd,level_staticSoundMap,ST_NMSTATICSOUNDGROUPS);
}

#if 0
void testSound(void)
{int i,j;
 for (i=0;i<nmSounds;i++)
    {dPrint("Testing sound %d\n",i);
     playSound(0,i);
     for (j=0;j<300;j++)
	SCL_DisplayFrame();
    }
}
#endif

void initSoundRegs(int sNm,int vol,int pan,struct soundSlotRegister *ssr)
{assert(vol>=0 && vol<=255);
 assert(pan>=0 && pan<=31);
 if (!enable_stereo)
    pan=0;
 ssr->reg[0]=0x1800|(sounds[sNm].startAddr>>16);
 if (sounds[sNm].bps==8)
    ssr->reg[0]|=0x10;
 ssr->reg[1]=sounds[sNm].startAddr;  /* start address */
 if (sounds[sNm].loopStart!=-1)
    {ssr->reg[2]=sounds[sNm].loopStart; /* loop start */
     ssr->reg[0]|=1<<5; /* normal loop */
    }
 else
    ssr->reg[2]=0x0000;  /* loop start address */
 if (sounds[sNm].bps==8)
    ssr->reg[3]=sounds[sNm].size;
 else
    ssr->reg[3]=sounds[sNm].size>>1;
 ssr->reg[4]=0x001f;  /* adsr */
 ssr->reg[5]=0x000f;
 ssr->reg[6]=vol;
 ssr->reg[7]=0x0000;
 ssr->reg[8]=sounds[sNm].sampleRate;
 ssr->reg[9]=0x8000;
 ssr->reg[10]=0x0000;
 ssr->reg[11]=(7<<13)|(pan<<8);
 ssr->soundNm=sNm;
}

struct soundSlotRegister *playSoundMegaE(int source,
					 struct soundSlotRegister *ssr)
{int i,slot,base;
 slot=deQVoice();
 slotOwner[slot]=source;
 slotSound[slot]=ssr->soundNm;
 base=0x20*slot+SNDBASE+0x100000;
 for (i=1;i<12;i++)
    POKE_W(base+(i<<1),ssr->reg[i]);
 POKE_W(base,ssr->reg[0]);
 slotDirty[slot]=1;
 return (struct soundSlotRegister *)(base);
}

void playSoundE(int source,int sNm,int vol,int pan)
{int i;
 assert(sNm<nmSounds);
 assert(sNm>=0);
 if (soundNmEars>1)
    vol+=(soundNmEars>2)? 16: 8;        /* 6 or 3 dB under: everyone shares the speakers */
 if (vol>255) return;
 /* if sound is a looping sound, make sure that no other copies of it are
    playing */
 if (sounds[sNm].loopStart!=-1)
    {for (i=0;i<32;i++)
	if (slotOwner[i]==source && slotSound[i]==sNm)
	   return;
    }
 else
    {if (sounds[sNm].lastFrameUsed==soundFrame)
	return;
     sounds[sNm].lastFrameUsed=soundFrame;
     if (soundNmEars>1)
	{/* the image's budget: a free place, or the quietest waiting sound if this is louder */
	 int q=soundNmPending;
	 if (q==SOUND_MPBUDGET)
	    {for (q=0,i=1;i<SOUND_MPBUDGET;i++)
		if (soundPending[i].vol>soundPending[q].vol)
		   q=i;
	     if (vol>=soundPending[q].vol)
		return;
	    }
	 else
	    soundNmPending++;
	 soundPending[q].source=source;
	 soundPending[q].sNm=(short)sNm;
	 soundPending[q].vol=(short)vol;
	 soundPending[q].pan=(short)pan;
	 return;
	}
    }
 playSoundNow(source,sNm,vol,pan);
}

static void playSoundNow(int source,int sNm,int vol,int pan)
{int slot;
 unsigned short zeroReg;
 int base;
 if (!enable_stereo)
    pan=0;

 slot=deQVoice();
 if (slotDirty[slot])
    {initSlot(slot);
     slotDirty[slot]=0;
    }
 slotOwner[slot]=source;
 slotSound[slot]=sNm;
 base=0x20*slot+SNDBASE+0x100000;
 POKE_W(base+2,sounds[sNm].startAddr); /* start address */
 /* loop end address */
 if (sounds[sNm].bps==8)
    POKE_W(base+6,sounds[sNm].size);
 else
    POKE_W(base+6,sounds[sNm].size>>1);
 POKE_W(base+0x10,sounds[sNm].sampleRate);  /* not really the sample rate */

 assert(vol>=0 && vol<=255);
 assert(pan>=0 && pan<=31);
 POKE_W(base+0x16,(7<<13)|(pan<<8));
 POKE_W(base+0x0c,vol);
 zeroReg=0x1800|(sounds[sNm].startAddr>>16);

 if (sounds[sNm].loopStart!=-1)
    {POKE_W(base+0x4,sounds[sNm].loopStart); /* loop start */
     zeroReg|=1<<5; /* normal loop */
    }
 if (sounds[sNm].bps==8)
    zeroReg|=0x10;
 POKE_W(base,zeroReg);  /* kon & ex */
}

void playSound(int source,int sNm)
{playSoundE(source,sNm,0,0);
}


enum {S_KARNAK=2,S_TRIBAL,S_SELKIS,S_SWAMP,S_ROCKIN,
	 S_QUARRY,S_MAGMA,S_SANCTUM,S_ENDCREDIT,
	 S_KILMAT1,S_KILMAT2,S_WATER2,S_MAP};

#ifndef GP_MUSIC_TRACK
static char trackMap[]={S_KARNAK,S_TRIBAL,S_QUARRY,S_KARNAK,S_SELKIS,
			   S_WATER2,S_TRIBAL,S_ROCKIN,S_WATER2,S_SELKIS,
			   S_MAGMA,S_ROCKIN,S_MAGMA,S_KILMAT1,S_QUARRY,
			   S_ROCKIN,S_MAGMA,S_QUARRY,S_SWAMP,S_WATER2,
			   S_TRIBAL,S_QUARRY,S_TRIBAL,S_KILMAT1,S_KILMAT1,
			   S_KILMAT1,S_KILMAT1,S_KILMAT1,S_KILMAT1,S_KILMAT2,
			   S_KARNAK};

char mapMusic=S_MAP;
char titleMusic=S_SANCTUM;
char endMusic=S_ENDCREDIT;
#else
/* GCC14: gameparams MUSIC_TRACK -- one CD track for the whole game, every level included */
char mapMusic=GP_MUSIC_TRACK;
char titleMusic=GP_MUSIC_TRACK;
char endMusic=GP_MUSIC_TRACK;
#endif
char firstVoiceTrack=S_MAP+1;

void playCDTrackForLevel(int lev)
{playCDTrackForLevelFrom(lev,0);
}

/* GCC14: from a cdMark taken before the load, when it is the same track (FILE.C playCDTrackFrom) */
void playCDTrackForLevelFrom(int lev,int mark)
{if (enable_music)
#ifndef GP_MUSIC_TRACK
    playCDTrackFrom(trackMap[lev],mark);
#else
    playCDTrackFrom(GP_MUSIC_TRACK,mark);
#endif
}

void posGetSoundParams(MthXyz *pos,int *vol,int *pan)
{int angle,k,d;
 Sprite *ear=camera;
 *vol=f(approxDist(pos->x-camera->pos.x,
		   pos->y-camera->pos.y,
		   pos->z-camera->pos.z));
 for (k=0;k<soundNmEars;k++)     /* GCC14: the nearest player hears it */
    if (soundEar[k] && soundEar[k]!=camera &&
	(d=f(approxDist(pos->x-soundEar[k]->pos.x,pos->y-soundEar[k]->pos.y,
			pos->z-soundEar[k]->pos.z)))<*vol)
       {*vol=d;
	ear=soundEar[k];
       }
 *vol=(*vol>>5)-15;
 if (*vol>255) return;
 if (*vol<0) *vol=0;

 angle=getAngle(ear->pos.x-pos->x,
		ear->pos.z-pos->z);
 angle=-normalizeAngle(F(90)+angle-ear->angle);
 /* fold back half onto front half */
 if (angle>F(90))
    angle=F(180)-angle;
 if (angle<F(-90))
    angle=F(-180)-angle;
 *pan=((unsigned int)(abs(angle)))>>19;
 if (angle<0)
    *pan|=0x10;
}


void posMakeSound(int source,MthXyz *pos,int sndNm)
{int vol,pan;
 assert(sndNm>=0);
 posGetSoundParams(pos,&vol,&pan);
 if (vol>255) return;
 playSoundE(source,sndNm,vol,pan);
}

void posAdjustSound(int source,MthXyz *pos)
{int vol,pan;
 posGetSoundParams(pos,&vol,&pan);
 if (vol>255) return;
 adjustSounds(source,vol,pan);
}

void adjustSounds(int source,int vol,int pan)
{int s,base;
 if (soundNmEars>1)
    vol+=(soundNmEars>2)? 16: 8;        /* GCC14: the same cut as playSoundE */
 if (vol>255)
    vol=255;
 for (s=0;s<32;s++)
    if (slotOwner[s]==source)
       {base=0x20*s+SNDBASE+0x100000;
	POKE_W(base+0x0c,vol);
	POKE_W(base+0x16,(7<<13)|(pan<<8));
       }
}


void saveSoundState(void)
{int i;
 for (i=0;i<32;i++)
    {if (sounds[slotSound[i]].loopStart!=-1)
	{int base=0x20*i+SNDBASE+0x100000;
	 soundSave[i].owner=slotOwner[i];
	 soundSave[i].sound=slotSound[i];
	 soundSave[i].pan=(PEEK_W(base+0x16)>>8)&0x1f;
	 soundSave[i].vol=PEEK_W(base+0x0c)&0xff;
	}
     else
	soundSave[i].sound=-1;
    }
}

void restoreSoundState(void)
{int i;
 for (i=0;i<32;i++)
    {if (soundSave[i].sound!=-1)
	{playSoundE(soundSave[i].owner,
		    soundSave[i].sound,
		    soundSave[i].vol,
		    soundSave[i].pan);
	}
     else
	soundSave[i].sound=-1;
    }
}

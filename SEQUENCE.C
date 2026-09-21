#include<sega_spr.h>
#include<sega_scl.h>

#include"sequence.h"
#include"file.h"
#include"util.h"
#include"spr.h"
#include"pic.h"
#include"sound.h"
#include"sprite.h"
#include "grenpal.h"
#include "manpal.h"
#include "mplayer.h"
extern int viewXmin,viewXmax,viewYmin,viewYmax,viewCx,viewCy;   /* WALLS.C: the view being drawn */


int level_nmSequences;
int level_nmFrames;
int level_nmChunks;

short *level_sequence;
sFrameType *level_frame;
sChunkType *level_chunk;
short *level_sequenceMap;

int loadSequences(int fd,int tileBase,int soundBase)
{int size,i;
 char *buffer;
 struct seqHeader *head;
 assert(fd>=0);
 fs_read(fd,(char *)&size,4);
 assert(size>0 && size<1024*1024);
 buffer=(char *)mem_malloc(0,size);
 assert(buffer);
 fs_read(fd,buffer,size);

 head=(struct seqHeader *)buffer;
 level_nmSequences=head->nmSequences;
 level_nmFrames=head->nmFrames;
 level_nmChunks=head->nmChunks;

 assert(level_nmSequences>=0);
 assert(level_nmFrames>=0);
 assert(level_nmChunks>=0);

 assert(((unsigned int)size)==sizeof(struct seqHeader)+
	level_nmSequences*sizeof(short)+
	level_nmFrames*sizeof(sFrameType)+
	level_nmChunks*sizeof(sChunkType)+
	OT_NMTYPES*sizeof(short));

 level_frame=(sFrameType *)(buffer+sizeof(struct seqHeader));
#ifndef NDEBUG
 {int i;
  for (i=0;i<level_nmFrames;i++)
     {assert(level_frame[i].pad[0]==0);
      assert(level_frame[i].pad[1]==0);
     }
 }
#endif

 level_chunk=(sChunkType *)(level_frame+level_nmFrames);

#ifndef NDEBUG
 {int i;
  for (i=0;i<level_nmChunks;i++)
     {assert(level_chunk[i].pad==0);
     }
 }
#endif

 level_sequence=(short *)(level_chunk+level_nmChunks);

 level_sequenceMap=level_sequence+level_nmSequences;

 for (i=0;i<level_nmChunks;i++)
    level_chunk[i].tile+=tileBase;
 for (i=0;i<level_nmFrames;i++)
    if (level_frame[i].sound!=-1)
       level_frame[i].sound+=soundBase;

 assert(level_sequence[0]==0);
 /* paranoia checks */
 assert(!(((int)level_sequence) & 3));
 assert(!(((int)level_frame) & 3));
 assert(!(((int)level_chunk) & 3));
 return 1;
}

short *level_wSequence;
sFrameType *level_wFrame;
sChunkType *level_wChunk;

int loadWeaponSequences(int fd)
{int size;
 int level_nmSequences,level_nmFrames,level_nmChunks;
 char *buffer;
 struct seqHeader *head;
 assert(fd>=0);
 fs_read(fd,(char *)&size,4);
 assert(size>0 && size<1024*1024);
 buffer=(char *)mem_malloc(0,size);
 assert(buffer);
 fs_read(fd,buffer,size);

 head=(struct seqHeader *)buffer;

 level_nmSequences=head->nmSequences;
 level_nmFrames=head->nmFrames;
 level_nmChunks=head->nmChunks;
 assert(level_nmSequences>=0);
 assert(level_nmFrames>=0);
 assert(level_nmChunks>=0);

 level_wFrame=(sFrameType *)(buffer+sizeof(struct seqHeader));
#ifndef NDEBUG
 {int i;
  for (i=0;i<level_nmFrames;i++)
     {assert(level_wFrame[i].pad[0]==0);
      assert(level_wFrame[i].pad[1]==0);
     }
 }
#endif

 level_wChunk=(sChunkType *)(level_wFrame+level_nmFrames);
 level_wSequence=(short *)(level_wChunk+level_nmChunks);

 assert(level_wSequence[0]==0);
 /* paranoia checks */
 assert(!(((int)level_wSequence) & 3));
 assert(!(((int)level_wFrame) & 3));
 assert(!(((int)level_wChunk) & 3));
 return 1;
}



#define QSIZE 8
typedef struct
{int seq,seqWith;
 int centerx,centery;
} QType;

static QType sequenceQ[QSIZE];
static int qHead,qTail;
static int frame;
static int clock;
static int sequenceOver;

/* GCC14: the weapon's animation queue belongs to a player (MPLAYER.H) */
void sequenceMpRegister(void)
{MPREG(sequenceQ); MPREG(qHead); MPREG(qTail);
 MPREG(frame); MPREG(clock); MPREG(sequenceOver);
}


void initWeaponQ(void)
{sequenceQ[0].seq=-1;
 qHead=0; qTail=0; frame=0; clock=0; sequenceOver=1;
}


void queueWeaponSequence(int seqNm,int cx,int cy)
{qHead=(qHead+1)&0x7;
 assert(qHead!=qTail);
 sequenceQ[qHead].seq=seqNm;
 sequenceQ[qHead].centerx=cx;
 sequenceQ[qHead].centery=cy;
 sequenceQ[qHead].seqWith=-1;
 sequenceOver=0;
}

void addWeaponSequence(int seqNm)
{sequenceQ[qHead].seqWith=seqNm;
}

int getWeaponSequenceQSize(void)
{return (qHead-qTail)&0x7;
}

int getCurrentWeaponSequence(void)
{return sequenceQ[qTail].seq;
}

int weaponSequenceQEmpty(void)
{return sequenceOver;
}

int getWeaponFrame(void)
{return frame;
}

void setWeaponFrame(int f)
{frame=f;
}

/* returns cumulative OR of the flags of the new frames passed and displayed */
int advanceWeaponSequence(int xbase,int ybase,int hack)
{int j;
 int gframe,chunk;
 int sequence,flip;
 int VDP2PicOn;
 int retVal=0;
 int sound;
 int overlay;
 int xo,yo;
 static int loadedPal=0;

 XyInt pos;

 sequence=sequenceQ[qTail].seq;
 if (sequence<0)
    {if (qTail!=qHead)
	{qTail=(qTail+1)&0x7;
	 sequence=sequenceQ[qTail].seq;
	 frame=0;
	 retVal|=level_wFrame[frame+level_wSequence[sequence]].flags;
	}
     else
	{frame=0;
	 return 0;
	}
    }
 clock--;
 while (clock<0)
    {clock+=2;
     frame++;
     if (frame+level_wSequence[sequence]>=level_wSequence[sequence+1])
	{if (qTail!=qHead)
	    {qTail=(qTail+1)&0x7;
	     sequence=sequenceQ[qTail].seq;
	     if (sequence<0)
		return retVal;
	     assert(sequence>=0);
	     frame=0;
	    }
	else
	   {frame=0;
	    /*frame=level_wSequence[sequence+1];*/
	    sequenceOver=1;
	   }
	}
     if (sequence<0)
	continue;
     retVal|=level_wFrame[frame+level_wSequence[sequence]].flags;

     sound=level_wFrame[frame+level_wSequence[sequence]].sound;
     if (sound!=-1)
	playSound(0,sound);
    }

 if (sequence<0)
    return retVal;

 xo=xbase+sequenceQ[qTail].centerx;
 yo=ybase+sequenceQ[qTail].centery;

 VDP2PicOn=0;
 overlay=0;
 if (sequence>=CFG_WOVERLAY_FROM)
    overlay=0x4000;
 {XyInt pos[2];                  /* GCC14: the view being drawn; solo = 0,0-320,192 */
  pos[0].x=viewCx+viewXmin;
  pos[0].y=viewCy+viewYmin;
  pos[1].x=viewCx+viewXmax;
  pos[1].y=viewCy+viewYmax;
  EZ_userClip(pos);
 }
 while (1)
    {gframe=frame+level_wSequence[sequence];
     for (chunk=level_wFrame[gframe].chunkIndex;
	  chunk<level_wFrame[gframe+1].chunkIndex;
	  chunk++)
	{sChunkType *c=level_wChunk+chunk;
	 if (getPicClass(c->tile)==TILEVDP && weaponSpritesOn)
	    {/* GCC14: split screen -- the gun is not a background plane here but the sub-tiles
		PIC.C cut out of the sheet, half resolution, so 128 frame pixels go down as a
		64 x 64 tile.  Same placement as the VDP1 chunks below, one scale up.  The two
		weapons with a palette of their own (grenade, manacle) take the object palette
		here: their CRAM bank is NBG0's, and NBG0 is the sky's now. */
	     int vw=viewXmax-viewXmin,cx,cy,sub;
	     XyInt sp[2];
	     for (cy=0;(sub=picVdp2Sub(c->tile,0,cy))>=0;cy++)
		for (cx=0;(sub=picVdp2Sub(c->tile,cx,cy))>=0;cx++)
		   {sp[0].x=((xo+c->chunkx+(cx<<7)-320/2)*vw)/320;
		    sp[0].y=viewYmax+((yo+c->chunky+(cy<<7)-CFG_WCLIP_BOTTOM)*vw)/320;
		    sp[1].x=(128*vw)/320;
		    sp[1].y=(128*vw)/320;
		    EZ_scaleSpr(ZOOM_TL,UCLPIN_ENABLE|COLOR_4|HSS_ENABLE|ECD_DISABLE,
				overlay,mapPic(sub),sp,NULL);
		    weaponSpriteDrawn++;   /* the overlay's "W:" fourth field */
		   }
	     if (hack)
		break;
	    }
	 else if (getPicClass(c->tile)==TILEVDP)
	    {unsigned short *colorRam=(unsigned short *)SCL_COLRAM_ADDR;
	     SCL_SET_N0CAOS(0);
	     if (sequence>=30 && sequence<35)
		{if (loadedPal!=1)
		    {for (j=0;j<256;j++)
			colorRam[(NMOBJECTPALLETES+1)*256+j]=
			   ((unsigned short *)grenadePal)[j];
		     loadedPal=1;
		    }
		 SCL_SET_N0CAOS(6);
		}
	     if (sequence>=44 && sequence<50)
		{if (loadedPal!=2)
		    {for (j=0;j<256;j++)
			colorRam[(NMOBJECTPALLETES+1)*256+j]=
			   ((unsigned short *)manaclePal)[j];
		     loadedPal=2;
		    }
		 SCL_SET_N0CAOS(6);
		}
	     displayVDP2Pic(c->tile,
			    xo+c->chunkx,
			    yo+c->chunky);
	     VDP2PicOn=1;
	     if (hack)
		break;
	    }
	 else
	    {int vw=viewXmax-viewXmin;
	     flip=0;
	     if (c->flags & 1)
		flip|=DIR_LRREV;
	     if (c->flags & 2)
		flip|=DIR_TBREV;
	     assert(getPicClass(c->tile)==TILE8BPP);
	     if (vw==320)
		{pos.x=xo-320/2+c->chunkx;
		 pos.y=yo-CFG_YCENTER+c->chunky;
		 EZ_normSpr(flip,UCLPIN_ENABLE|COLOR_4|HSS_ENABLE|ECD_DISABLE,
			    overlay,mapPic(c->tile),&pos,NULL);
		}
	     else
		{/* GCC14: split screen.  The gun is laid out for 320 x 192; scaled by the view's
		    width and sat on its bottom edge, as Doom scales its psprites (pspritescale). */
		 XyInt sp[2];
		 sp[0].x=((xo+c->chunkx-320/2)*vw)/320;
		 sp[0].y=viewYmax+((yo+c->chunky-CFG_WCLIP_BOTTOM)*vw)/320;
		 sp[1].x=(64*vw)/320;
		 sp[1].y=(64*vw)/320;
		 EZ_scaleSpr(ZOOM_TL|flip,UCLPIN_ENABLE|COLOR_4|HSS_ENABLE|ECD_DISABLE,
			     overlay,mapPic(c->tile),sp,NULL);
		}
	    }
	}
     if (sequenceQ[qTail].seqWith==-1 || sequence==sequenceQ[qTail].seqWith)
	break;
     sequence=sequenceQ[qTail].seqWith;
     overlay=CFG_WFLASH_OVERLAY;
     if (hack)
	break;
    }
 if (!VDP2PicOn)
    delay_dontDisplayVDP2Pic();
 return retVal;
}


void clearWeaponQ(void)
{initWeaponQ();
}

#ifdef GP_GAME_DOOM
/* Doom (SPEC_PLAYER section 2.3, deviation a): PIN the current entry instead of queueing it --
   the 8-deep queue overflows with 1-tic weapon states.  Overwrites sequenceQ[qTail] (qHead stays
   equal to qTail: initWeaponQ), resets frame/clock, clears the flash (addWeaponSequence re-adds
   it).  seqNm < 0 = no weapon drawn.  At the end of the file: no assert follows, the __LINE__
   of the default build is untouched. */
void setWeaponSequence(int seqNm,int cx,int cy)
{assert(qHead==qTail);
 sequenceQ[qTail].seq=seqNm;
 sequenceQ[qTail].centerx=cx;
 sequenceQ[qTail].centery=cy;
 sequenceQ[qTail].seqWith=-1;
 frame=0;
 clock=0;
 sequenceOver=(seqNm<0);
}
#endif

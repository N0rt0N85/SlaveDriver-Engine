#include <sega_mth.h>
#include <sega_scl.h>
#include <limits.h>

#include "slevel.h"
#include "level.h"
#include "sprite.h"
#include "ai.h"
#include "util.h"
#include "sequence.h"
#include "hitscan.h"
#include "sound.h"
#include "sruins.h"
#include "route.h"
#include "walls.h"
#include "pic.h"
#include "file.h"
#include "gamestat.h"
#include "bigmap.h"
#include "v_blank.h"
#include "weapon.h"
#include "local.h"

#include "aicommon.h"
#include "jasonpal.h"

/********************************\
 *        RAMSESLID STUFF       *
\********************************/
enum {AI_RLID_RISE,AI_RLID_FALL,AI_RLID_IDLE};
#define CHANNEL_RISE 839402849
#define CHANNEL_DROP 290808021

void ramsesLid_func(Object *_this,int msg,int param1,int param2)
{RamsesLidObject *this=(RamsesLidObject *)_this;
 switch (msg)
    {case SIGNAL_SWITCH:
	if (param1==CHANNEL_RISE)
	   {this->state=AI_RLID_RISE;
	    moveObject((Object *)this,objectRunList);
	    break;
	   }
	if (param1==CHANNEL_DROP)
	   {this->state=AI_RLID_FALL;
	    this->vel=0;
	    moveObject((Object *)this,objectRunList);
	    break;
	   }
	break;
     case SIGNAL_MOVE:
	switch (this->state)
	   {case AI_RLID_RISE:
	       {int d;
		d=F(this->distanceToRise)>>8;
		if (d<F(3))
		   d=F(3);
		pbObject_move((PushBlockObject *)this,d);
		if (this->offset>F(this->distanceToRise))
		   {pbObject_moveTo((PushBlockObject *)this,
				    this->distanceToRise);
		    moveObject((Object *)this,objectIdleList);
		   }
		break;
	       }
	    case AI_RLID_FALL:
	       this->vel-=GRAVITY>>1;
	       pbObject_move((PushBlockObject *)this,this->vel);
	       if (this->offset<0)
		  {pbObject_moveTo((PushBlockObject *)this,0);
		   setEarthQuake(20);
		   pushBlockMakeSound((PushBlockObject *)this,
				      level_objectSoundMap[OT_RAMSESLID]);
		   moveObject((Object *)this,objectIdleList);
		  }
	       break;
	      }
	break;
       }
}

Object *constructRamsesLid(int pb)
{int fs;
 MthXyz pos;
 RamsesLidObject *this=(RamsesLidObject *)
    getFreeObject(ramsesLid_func,OT_RAMSESLID,CLASS_PUSHBLOCK);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 registerPBObject(pb,(Object *)this);
 this->pbNum=pb;
 this->counter=0;
 this->waitCounter=0;
 this->offset=0;
 fs=level_pushBlock[this->pbNum].floorSector;
 pos.x=0; pos.y=0; pos.z=0;
 {int a,b;
  a=findCeilDistance(fs,&pos);
  b=findFloorDistance(fs,&pos);
  this->distanceToRise=f(b-a);
 }
 movePushBlock(this->pbNum,0,-120,0);
 this->distanceToRise+=120;
 if (!(currentState.gameFlags & GAMEFLAG_FIRSTLEVEL))
    pbObject_move((PushBlockObject *)this,F(this->distanceToRise));
 return (Object *)this;
}


/********************************\
 *        RTRIGGER STUFF        *
\********************************/
enum  {AI_RTRIGGER_WAIT,AI_RTRIGGER_ALIGNPLAYER,
	  AI_RTRIGGER_RISE,
	  AI_RTRIGGER_TALK,
	  AI_RTRIGGER_FADE,
	  AI_RTRIGGER_DROPMUMMY,
	  AI_RTRIGGER_EARTHQUAKE,
	  AI_RTRIGGER_WAITFORDISABLE};

#define SAMPLESPERFRAME (22050/(FRAMESPERSEC/2))

#define RINGSIZE (SAMPLESPERFRAME*32)
/* in samples */

static void loadRamsesBlock(RamsesTriggerObject *this)
{int amount,s;
#ifdef JAPAN
 int saveHead=this->soundRingHead;
#endif
 amount=SAMPLESPERFRAME;
 while (amount)
    {s=RINGSIZE-this->soundRingHead;
     if (s>amount)
	{fs_read(this->fd,(char *)(SNDBASE+this->soundRingBase+
				   (this->soundRingHead<<1)),(amount<<1));
	 this->soundRingHead+=amount;
	 break;
	}
     fs_read(this->fd,
	     (char *)(SNDBASE+this->soundRingBase+
		      (this->soundRingHead<<1)),
	     s<<1);
     this->soundRingHead=0;
     amount-=s;
    }
#ifdef JAPAN
 this->soundRingHead=saveHead;
 amount=SAMPLESPERFRAME;
 while (amount)
    {s=RINGSIZE-this->soundRingHead;
     if (s>amount)
	{fs_read(this->fd,(char *)(SNDBASE+this->soundRingBase2+
				   (this->soundRingHead<<1)),(amount<<1));
	 this->soundRingHead+=amount;
	 break;
	}
     fs_read(this->fd,
	     (char *)(SNDBASE+this->soundRingBase2+
		      (this->soundRingHead<<1)),
	     s<<1);
     this->soundRingHead=0;
     amount-=s;
    }
#endif
}

static void chooseVoiceFile(char *nameBuff)
{static char *lingoCode[]={"ENG","SPA","FRE","GER"};
 char *foo=NULL;
 int number=1,artifact;
 foo=lingoCode[getLanguageNumber()];
#ifdef JAPAN
 foo="JAP";
#endif
 artifact=bitScanBackwards(currentState.gameFlags & 0x3f,7);
 switch (artifact)
    {case -1: /* no artifact */
	if (currentState.gameFlags & GAMEFLAG_TALKEDTORAMSES)
	   number=2;
	else
	   number=1;
	break;
     case 0: /* have sandals */
	number=3;
	break;
     case 1: /* have mask */
	if (currentState.inventory & INV_GRENADE)
	   number=5;
	else
	   number=4;
	break;
     case 2: /* have shawl */
	if (currentState.inventory & INV_FLAMER)
	   number=7;
	else
	   number=6;
	break;
     case 3: /* have anklet */
	if (currentState.gameFlags & GAMEFLAG_TALKEDTORAMSES)
	   number=9;
	else
	   number=8;
	break;
     case 4: /* have scepter */
	number=10;
	break;
     case 5: /* have feather */
	if (currentState.levFlags[13] & LEVFLAG_CANENTER)
	   number=12;
	else
	   number=11;
	break;
       }
 if (currentState.currentLevel!=3)
    {if ((currentState.inventory & INV_TRANSMITTER)==INV_TRANSMITTER)
	number=14; /* good end */
     else
	number=13; /* bad end */
    }

 sprintf(nameBuff,"SP_%s%02d.LIP",foo,number);
}

static char droppedMummy=0;
void ramsesTrigger_func(Object *_this,int msg,int param1,int param2)
{RamsesTriggerObject *this=(RamsesTriggerObject *)_this;
 int fflag,i;
 switch (msg)
    {case SIGNAL_FLOORCONTACT:
	if (this->disabled)
	   {this->disabled=5;
	    break;
	   }
	delay_moveObject((Object *)this,objectRunList);
	switchWeapons(0);
	this->state=AI_RTRIGGER_ALIGNPLAYER;
	if (currentState.gameFlags & GAMEFLAG_FIRSTLEVEL)
	   signalAllObjects(SIGNAL_SWITCH,CHANNEL_RISE,0);
	{char name[80];
	 chooseVoiceFile(name);
	 this->fd=fs_open(name);
	}
	assert(this->fd>=0);
	switchPlayerMotion(0);
	this->timer=0;
	this->disabled=5;
	{unsigned short *colorRam=(unsigned short *)SCL_COLRAM_ADDR;
	 for (i=0;i<256;i++)
	    colorRam[NMOBJECTPALLETES*256+i]=
	       ((unsigned short *)jasonPallete)[i];
	}
	break;
     case SIGNAL_VIEW:
	if (this->state!=AI_RTRIGGER_DROPMUMMY)
	   this->ramses->flags|=SPRITEFLAG_FLASH;
	break;
     case SIGNAL_MOVE:
	if (currentState.currentLevel==3 &&
	    this->state==AI_RTRIGGER_TALK &&
	    !(lastInputSample & PER_DGT_A))
	   {this->state=AI_RTRIGGER_FADE;
	    this->ramses->sequence=
	       level_sequenceMap[OT_RAMSESTRIGGER]+2;
	    stopAllSound((int)this);
	    mem_free(this->frames);
	    fs_close(this->fd);
	   }
	switch (this->state)
	   {case AI_RTRIGGER_ALIGNPLAYER:
/* #ifndef PSYQ */
	       fs_execOne();
/* #endif */
	       camera->vel.x+=(F(level_sector[this->sectorNm].center[0])-
			       camera->pos.x)>>5;
	       camera->vel.z+=(F(level_sector[this->sectorNm].center[2])-
			       camera->pos.z)>>5;
	       camera->vel.x-=camera->vel.x>>3;
	       camera->vel.z-=camera->vel.z>>3;

	       playerAngle.yaw-=playerAngle.yaw>>3;
	       playerAngle.pitch-=playerAngle.pitch>>3;
	       playerAngle.roll=0;

	       if (this->timer++<=30)
		  break;
	       if (currentState.currentLevel!=3 && !droppedMummy)
		  {this->timer=0;
		   this->state=AI_RTRIGGER_DROPMUMMY;
		   this->ramses=newSprite(this->homeSector,F(16),F(1),0,
					  level_sequenceMap[OT_RAMSIDEMUMMY],
					  SPRITEFLAG_IMATERIAL|
					  SPRITEFLAG_IMMOBILE,
					  (Object *)this);
		   this->ramses->pos.x=F(level_sector[this->homeSector].
					 center[0])+F(25);
		   this->ramses->pos.y=F(level_sector[this->homeSector].
					 center[1])+F(128);
		   this->ramses->pos.z=F(level_sector[this->homeSector].
					 center[2]);
		   this->ramses->scale=38000;
		   break;
		  }
	       this->state=AI_RTRIGGER_RISE;
	       this->ramses=newSprite(this->homeSector,F(16),F(1),0,
				      level_sequenceMap[OT_RAMSESTRIGGER],
				      SPRITEFLAG_INVISIBLE|
				      SPRITEFLAG_IMATERIAL|
				      SPRITEFLAG_IMMOBILE|
				      SPRITEFLAG_NOSCALE,
				      (Object *)this);
	       this->ramses->pos.x=F(level_sector[this->homeSector].
				     center[0])+F(25);
	       this->ramses->pos.y=F(level_sector[this->homeSector].
				     center[1])-F(32);
	       this->ramses->pos.z=F(level_sector[this->homeSector].
				     center[2]);
	       this->ramses->scale=65536+16000;

	       addLight(this->ramses,0,0,0);
	       this->timer=0;
	       this->soundRingBase=(getSoundTop()+3)&~3;
	       assert(this->soundRingBase+2*RINGSIZE<1024*512);
#ifdef JAPAN
	       this->soundRingBase2=this->soundRingBase+2*RINGSIZE;
	       assert(this->soundRingBase2+2*RINGSIZE<1024*512);
#endif
	       this->soundRingHead=0;
	       {short s;
		fs_read(this->fd,(char *)&s,2);
		this->nmFrames=s;
	       }
	       this->frames=mem_malloc(0,this->nmFrames);
	       this->framePos=0;
	       fs_read(this->fd,this->frames,this->nmFrames);
	       for (i=0;i<16;i++)
		  loadRamsesBlock(this);
	       {struct soundSlotRegister ssr;
		initSoundRegs(0,0,
#ifdef JAPAN
			      0x1f,
#else
			      0,
#endif
			      &ssr);
		ssr.reg[0]=
		   (1<<5) | /* turn on loop */
		      (0xf & (this->soundRingBase>>16))| /* high start */
			 (0x1800); /* konex */
		ssr.reg[1]=this->soundRingBase & 0xffff;/* low start */
		ssr.reg[0]|=(1<<5);
		ssr.reg[2]=0; /* loop start */
		ssr.reg[3]=RINGSIZE-1;
		ssr.reg[8]=0x7800; /* rate=22,050 hz */
		ssr.soundNm=level_staticSoundMap[ST_PUSHBLOCK];
		/* choose this one cause we know it's looped */
		{struct soundSlotRegister *f;
		 f=playSoundMegaE((int)this,&ssr);
		 this->sndSlot=(((int)f)-(SNDBASE+0x100000))>>5;
#ifdef JAPAN
		 ssr.reg[0]=
		    (1<<5) | /* turn on loop */
		       (0xf & (this->soundRingBase2>>16))| /* high start */
			  (0x1800); /* konex */
		 ssr.reg[1]=this->soundRingBase2 & 0xffff;/* low start */
		 ssr.reg[11]=(7<<13)|(0xf<<8);
		 playSoundMegaE((int)this,&ssr);
#endif
		}
	       }
	       POKE_W(SNDBASE+0x100408,this->sndSlot<<11);
	       this->lastSndPos=0;
	       /* set slot to monitor */
	       break;
	    case AI_RTRIGGER_DROPMUMMY:
	       if (this->timer++>=180)
		  {freeSprite(this->ramses);
		   this->state=AI_RTRIGGER_ALIGNPLAYER;
		   droppedMummy=1;
		   break;
		  }
	       this->ramses->pos.y-=F(1);
	       break;
	    case AI_RTRIGGER_RISE:
	       loadRamsesBlock(this);
#if 0
	       {int soundPos;
		if ((this->framePos&31)!=31)
		   this->framePos++;
		soundPos=(PEEK_W(SNDBASE+0x100408)>>7)&0xf;
		if (soundPos<this->lastSndPos)
		   this->framePos=(this->framePos+32)&(~31);
		this->lastSndPos=soundPos;
	       }
#else
	       this->framePos++;
#endif
	       if (this->timer>20)
		  {this->ramses->pos.y+=F(1);
		   playerAngle.pitch+=F(1)>>1;
		   this->ramses->flags&=~SPRITEFLAG_INVISIBLE;
		   fflag=spriteAdvanceFrame(this->ramses);
		   if (fflag & FRAMEFLAG_ENDOFSEQUENCE)
		      {this->state=AI_RTRIGGER_TALK;
		       this->ramses->sequence=
			  level_sequenceMap[OT_RAMSESTRIGGER]+3;
		       removeLight(this->ramses);
		       this->baseY=this->ramses->pos.y;
		       this->timer=0;
		       break;
		      }
		  }
	       if (this->timer<64)
		  {int color;
		   color=f(evalHermite(F(this->timer)>>6,0,F(25),F(0),F(0)));
		   changeLightColor(this->ramses,color,color,color);
		  }
	       this->timer++;
	       break;
	    case AI_RTRIGGER_TALK:
	       if (this->framePos+2<this->nmFrames)
		  {int soundPos;
		   loadRamsesBlock(this);
		   if ((this->framePos&31)!=31)
		      this->framePos++;
		   soundPos=(PEEK_W(SNDBASE+0x100408)>>7)&0xf;
		   if (soundPos<this->lastSndPos)
		      this->framePos=(this->framePos+32)&(~31);
		   this->lastSndPos=soundPos;
		   if (this->framePos+2>=this->nmFrames)
		      break;
		   this->ramses->frame=this->frames[this->framePos+2];
		  }
	       else
		  {this->ramses->frame=0;
		   stopAllSound((int)this);
		   fs_close(this->fd);
		   this->state=AI_RTRIGGER_FADE;
		   this->ramses->sequence=
		      level_sequenceMap[OT_RAMSESTRIGGER]+2;
		   mem_free(this->frames);
		  }
	       break;
	    case AI_RTRIGGER_FADE:
	       fflag=spriteAdvanceFrame(this->ramses);
	       if (this->ramses->frame>8)
		  {playerAngle.pitch-=playerAngle.pitch>>2;
		   if (abs(playerAngle.pitch)<F(1))
		      playerAngle.pitch=0;
		  }
	       if (fflag & FRAMEFLAG_ENDOFSEQUENCE)
		  {freeSprite(this->ramses);
		   this->disabled=5;
		   this->state=AI_RTRIGGER_WAITFORDISABLE;
		   switchPlayerMotion(1);
		   switchWeapons(1);
		   signalAllObjects(SIGNAL_SWITCH,1020,0);
		   if (currentState.currentLevel==30)
		      {if ((currentState.inventory & INV_TRANSMITTER)!=
			   INV_TRANSMITTER)
			  {playerGotEntombedWithRamses();
			   break;
			  }
		       signalAllObjects(SIGNAL_SWITCH,CHANNEL_DROP,0);
		      }
		   playCDTrackForLevel(currentState.currentLevel);
		  }
	       break;
	    case AI_RTRIGGER_WAITFORDISABLE:
	       if (--this->disabled<=0)
		  {if (currentState.currentLevel==30)
		      {this->state=AI_RTRIGGER_EARTHQUAKE;
		       this->disabled=1;
		       this->timer=0;
		       break;
		      }
		   this->disabled=0;
		   delay_moveObject((Object *)this,objectIdleList);
		  }
	       break;
	    case AI_RTRIGGER_EARTHQUAKE:
	       if (this->timer--<=0)
		  {setEarthQuake(25);
		   playSound((int)this,level_objectSoundMap[OT_RAMSESTRIGGER]);
		   this->timer=getNextRand()&0x7f;
		  }
	       break;
	      }
       }
}

Object *constructRamsesTrigger(int triggerSec,int homeSector)
{RamsesTriggerObject *this=(RamsesTriggerObject *)
    getFreeObject(ramsesTrigger_func,OT_RAMSESTRIGGER,CLASS_SECTOR);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 this->aiSlot=nextAiSlot++;
 this->sectorNm=triggerSec;
 this->homeSector=homeSector;
 this->state=AI_RTRIGGER_WAIT;
 this->disabled=0;
 this->timer=0;
 markSectorFloor(triggerSec,(Object *)this);
 if (currentState.gameFlags & GAMEFLAG_JUSTTELEPORTED)
    {movePlayerToSector(triggerSec);
     playerAngle.yaw=0;
     currentState.gameFlags&=~GAMEFLAG_JUSTTELEPORTED;
    }
 return (Object *)this;
}


/********************************\
 *         SWITCH STUFF         *
\********************************/
enum {AI_SWITCH_OFF,AI_SWITCH_SWITCHING,AI_SWITCH_ON,AI_SWITCH_OFFING};

void switch_func(Object *_this,int msg,int param1,int param2)
{int frame,fflags;
 SwitchObject *this=(SwitchObject *)_this;
 switch (msg)
    {case SIGNAL_SWITCHRESET:
	if (this->state==AI_SWITCH_ON && param1==this->channel)
	   {this->state=AI_SWITCH_OFFING;
	    this->sequence--;
	    this->frame=0;
	    if (level_sequence[this->sequence]==
		level_sequence[this->sequence+1]-1)
	       delay_moveObject((Object *)this,objectIdleList);
	   }
	break;
     case SIGNAL_PRESS:
	if (this->state==AI_SWITCH_OFF)
	   {MthXyz *pos=(MthXyz *)param1;
	    if (CFG_SWITCH_AIM && approxDist(pos->x-this->orficePos.x,
			   pos->y-this->orficePos.y,
			   pos->z-this->orficePos.z)>F(40))
	       break;
	    this->state=AI_SWITCH_SWITCHING;
	    this->sequence++;
	    this->frame=0;
	    delay_moveObject((Object *)this,objectRunList);
	   }
	break;
     case SIGNAL_MOVE:
	frame=level_sequence[this->sequence]+this->frame;
	/* dPrint("fr:%d\n",level_chunk[level_frame[frame].chunkIndex].tile);
	assert(level_chunk[level_frame[frame].chunkIndex].tile<127); */
	*this->tilePos=level_chunk[level_frame[frame].chunkIndex].tile;
	if (level_frame[frame].sound!=-1)
	   posMakeSound((int)this,&this->orficePos,level_frame[frame].sound);
	this->frame++;
	fflags=0;
	if (frame+1>=level_sequence[this->sequence+1])
	   {this->frame=0;
	    fflags=FRAMEFLAG_ENDOFSEQUENCE;
	   }
	switch (this->state)
	   {case AI_SWITCH_SWITCHING:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {this->state=AI_SWITCH_ON;
		   this->sequence+=2;
		   this->frame=0;
		   dPrint("switched channel %d\n",this->channel);
		   signalAllObjects(SIGNAL_SWITCH,this->channel,0);
		  }
	       break;
	    case AI_SWITCH_OFFING:
	       if (fflags & FRAMEFLAG_ENDOFSEQUENCE)
		  {this->state=AI_SWITCH_OFF;
		   this->sequence-=2;
		   this->frame=0;
		  }
	       break;
	    case AI_SWITCH_ON:
	       break;
	      }
	break;
       }
}

Object *constructSwitch(int type)
{int t,ourTile,wall;
 sWallType *w;
 SwitchObject *this=(SwitchObject *)
    getFreeObject(switch_func,type,CLASS_WALL);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 this->sectorNm=suckShort();
 this->channel=suckShort();
 this->orficePos.x=F(suckShort());
 this->orficePos.y=F(suckShort());
 this->orficePos.z=F(suckShort());

 this->state=AI_SWITCH_OFF;
 this->aiSlot=nextAiSlot++;

 this->sequence=level_sequenceMap[this->type];
 this->frame=0;
 if (level_sequence[this->sequence]==
     level_sequence[this->sequence+1]-1)
    moveObject((Object *)this,objectIdleList);
 else
    moveObject((Object *)this,objectRunList);
 /* find our tile on the wall */
 ourTile=
    level_chunk[level_frame[level_sequence[this->sequence]].chunkIndex].tile;
 this->tilePos=NULL;
 w=NULL;
 for (wall=level_sector[this->sectorNm].firstWall;
      wall<=level_sector[this->sectorNm].lastWall;
      wall++)
    {w=level_wall+wall;
     if (w->flags & WALLFLAG_PARALLELOGRAM)
	{for (t=w->textures+1;t<w->textures+2*w->tileLength*w->tileHeight;t+=2)
	    if (level_texture[t]==ourTile)
	       {this->tilePos=&(level_texture[t]);
		this->wallNm=wall;
		break;
	       }
	}
     else
	{for (t=w->firstFace;t<=w->lastFace;t++)
	    if (level_face[t].tile==ourTile)
	       {this->tilePos=&(level_face[t].tile);
		this->wallNm=wall;
		break;
	       }
	}
     if (this->tilePos)
	break;
    }
 assert(this->tilePos);

 w->object=(Object *)this;
 return (Object *)this;
}


/********************************\
 *         SSWITCH STUFF        *
\********************************/
enum  {AI_SSWITCH_OFF,AI_SSWITCH_ON};

void sswitch_func(Object *_this,int msg,int param1,int param2)
{SectorSwitchObject *this=(SectorSwitchObject *)_this;
 switch (msg)
    {case SIGNAL_ENTER:
	if (this->state!=AI_SSWITCH_OFF)
	   break;
	this->state=AI_SSWITCH_ON;
	signalAllObjects(SIGNAL_SWITCH,this->channel,0);
	break;
     case SIGNAL_SWITCHRESET:
	if (param1!=this->channel)
	   break;
	if (camera->s==this->sectorNm)
	   signalAllObjects(SIGNAL_SWITCH,this->channel,0);
	else
	   this->state=AI_SSWITCH_OFF;
	break;
       }
}

Object *constructSectorSwitch(void)
{SectorSwitchObject *this=(SectorSwitchObject *)
    getFreeObject(sswitch_func,OT_SECTORSWITCH,CLASS_SECTOR);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectIdleList);
 this->sectorNm=suckShort();
 this->channel=suckShort();
 this->state=AI_SSWITCH_OFF;
 level_sector[this->sectorNm].object=this;
 return (Object *)this;
}


/********************************\
 *         BLOWPOT STUFF        *
\********************************/

unsigned short blowPotSequenceMap[]={0};

void blowPot_func(Object *_this,int msg,int param1,int param2)
{static int lightP[]={0,F(25),0,0,
			 F(5),F(25),0,0,
			 F(25),F(25),0,0};
 BlowPotObject *this=(BlowPotObject *)_this;
 switch (msg)
    {case SIGNAL_OBJECTDESTROYED:
	{Object *killed=(Object *)param1;
	 if (killed==_this)
	    freeSprite(this->sprite);
	 break;
	}
     case SIGNAL_MOVE:
	spriteAdvanceFrame(this->sprite);
	if (this->health<=0)
	   {if (this->boomTimer++<15)
	       break;
	    constructOneShot(this->sprite->s,
			     this->sprite->pos.x,
			     this->sprite->pos.y+F(30),
			     this->sprite->pos.z,
			     OT_GRENPOW,F(2),0,0);
	    explodeAllMaskedWallsInSector(this->sprite->s);
	    radialDamage((Object *)this,&this->sprite->pos,this->sprite->s,
			 200,F(300));
	    {int seq,nm;
	     seq=this->sprite->sequence+1;
	     nm=level_sequence[seq+1]-level_sequence[seq];
#if 0
	     for (i=0;i<nm;i++)
		constructBouncyBit(this->sprite->s,&this->sprite->pos,
				   seq,i);
#endif
	    }
	    delayKill(_this);
	   }
	break;
     case SIGNAL_HURT:
	if (monsterObject_signalHurt((MonsterObject *)this,param1,param2))
	   {if (this->type!=OT_BOOMPOT1 && this->type!=OT_BOOMPOT2)
	       {/* make random suprise */
		int goodie=getNextRand()%100;
		int type;
		MthXyz exitPoint;
		enum {E_HBALL,E_ABALL,E_HORB,E_AORB,
			 E_BOMB,E_SNAKENEST,E_SPIDER,E_INVIS,E_BOOST,
			 E_MAP,NMEVENTS};
		static const char prob[]={20,20,10,10,
					     3,3,5,3,3,
					     0};
		static short item[]=
		   {OT_HEALTHBALL,OT_AMMOBALL,OT_HEALTHORB,OT_AMMOORB,
		       -1,-1,-1,OT_INVISIBLEBALL,OT_WEAPONPOWERBALL,
		       OT_EYEBALL};
		for (type=0;type<NMEVENTS;type++)
		   if ((goodie-=prob[type])<=0)
		      break;

		if (this->iAmMapHolder)
		   type=E_MAP;

		exitPoint.x=this->sprite->pos.x;
		exitPoint.y=this->sprite->pos.y+F(30);;
		exitPoint.z=this->sprite->pos.z;
		if (type<NMEVENTS)
		   if (item[type]!=-1)
		      constructThing(this->sprite->s,
				     exitPoint.x,exitPoint.y,exitPoint.z,
				     item[type]);
		switch (type)
		   {case E_SPIDER:
		       if (level_sequenceMap[OT_SPIDER]>=0)
			  constructSpider(this->sprite->s,0,&exitPoint,NULL);
		       break;
		    case E_BOMB:
		       {MthXyz vel;
			vel.x=(MTH_GetRand()&0xfffff)-F(8);
			vel.z=(MTH_GetRand()&0xfffff)-F(8);
			vel.y=F(10)+(MTH_GetRand()&0x7ffff);
			constructGrenade(this->sprite->s,
					 &exitPoint,&vel,
					 NULL);
			break;
		       }
		    case E_SNAKENEST:
#define NMSNAKESPERNEST 5
		       {Fixed32 angle=0;
			int i;
			for (i=0;i<NMSNAKESPERNEST;i++)
			   {constructCobra(this->sprite->s,
					   exitPoint.x,
					   exitPoint.y,
					   exitPoint.z,
					   angle,F(2),(SpriteObject *)player,
					   OT_COBRA,0);
			    angle=
			       normalizeAngle(angle+(F(360)/NMSNAKESPERNEST));
			   }
			break;
		       }
		     default:
			break;
		      }


		/* make incidental effects */
		constructLight(this->sprite->s,
			       this->sprite->pos.x,
			       this->sprite->pos.y+F(30),
			       this->sprite->pos.z,
			       lightP,F(1)/16);
		constructOneShot(this->sprite->s,
				 this->sprite->pos.x,
				 this->sprite->pos.y+F(30),
				 this->sprite->pos.z,
				 OT_POOF,F(2),0,0);
		{int seq,i,nm;
		 seq=this->sprite->sequence;
		 nm=level_sequence[seq+1]-level_sequence[seq];
		 for (i=1;i<nm;i++)
		    if (this->type==OT_CONTAIN10)
		       constructBouncyBit(this->sprite->s,&this->sprite->pos,
					  this->sprite->sequence,i,1);
		    else
		       constructBouncyBit(this->sprite->s,&this->sprite->pos,
					  this->sprite->sequence,i,0);
		}
		if (this->type!=OT_CONTAIN10)
		   posMakeSound((int)this,&this->sprite->pos,
				level_staticSoundMap[ST_BLOWPOT]+3);
		delayKill(_this);
	       }
	   }
	break;
       }
}

Object *constructBlowPot(int sector,int type)
{BlowPotObject *this;
 this=(BlowPotObject *)
    getFreeObject(blowPot_func,type,CLASS_MONSTER);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 if (type==OT_BOOMPOT1 || type==OT_BOOMPOT2)
    {moveObject((Object *)this,objectRunList);
     this->health=45;
    }
 else
    {moveObject((Object *)this,objectIdleList);
     this->health=30;
    }
 this->sprite=newSprite(sector,F(32),F(1),0,
			0,SPRITEFLAG_IMMOBILE,
			(Object *)this);
 suckSpriteParams(this->sprite);
 this->sequenceMap=blowPotSequenceMap;
 this->enemy=NULL;
 this->aiSlot=nextAiSlot++;
 this->boomTimer=0;
 this->iAmMapHolder=0;
 setState((SpriteObject *)this,0);
 if (type==OT_BOOMPOT1 || type==OT_BOOMPOT2)
    spriteRandomizeFrame(this->sprite);
 return (Object *)this;
}


/********************************\
 *    EARTHQUAKEBLOCK STUFF     *
\********************************/

enum {AI_EARTHQUAKEBLOCK_WAIT,AI_EARTHQUAKEBLOCK_MOVE};

void earthQuakeBlock_func(Object *_this,int msg,int param1,int param2)
{EarthQuakeBlockObject *this=(EarthQuakeBlockObject *)_this;
 switch (msg)
    {case SIGNAL_MOVE:
	switch (this->state)
	   {case AI_EARTHQUAKEBLOCK_WAIT:
	       if (getEarthQuake()<=0)
		  break;
	       this->counter--;
	       if (this->counter<=0)
		  {this->state=AI_EARTHQUAKEBLOCK_MOVE;
		   this->vel=((short)getNextRand())<<2;
		   this->counter=getNextRand()&0x1f;
		  }
	       break;
	    case AI_EARTHQUAKEBLOCK_MOVE:
	       pbObject_move((PushBlockObject *)this,this->vel);
	       if (this->offset<0)
		  pbObject_moveTo((PushBlockObject *)this,0);
	       if (this->offset>F(16))
		  pbObject_moveTo((PushBlockObject *)this,16);
	       this->counter--;
	       if (this->counter<=0)
		  this->state=AI_EARTHQUAKEBLOCK_WAIT;
	       break;
	      }
	break;
       }
}

Object *constructEarthQuakeBlock(int pb)
{EarthQuakeBlockObject *this=(EarthQuakeBlockObject *)
    getFreeObject(earthQuakeBlock_func,OT_EARTHQUAKEBLOCK,CLASS_PUSHBLOCK);
 assert(sizeof(*this)<sizeof(Object));
 assert(this);
 moveObject((Object *)this,objectRunList);
 registerPBObject(pb,(Object *)this);
 this->pbNum=pb;
 this->counter=0;
 this->waitCounter=0;
 this->vel=0;
 this->state=AI_EARTHQUAKEBLOCK_WAIT;
 this->offset=0;
 return (Object *)this;
}

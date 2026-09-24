/* DOOM_SAVE.C -- Doom's SAVE / LOAD, in place of the engine's BUP.C (GCC14).
 *
 * This file REPLACES BUP.C in the Doom link (the Makefile drops BUP from MAIN_C for GAME = doom)
 * and provides the same bup_* seams the title and the main loop call.  That is not tidiness: the
 * PowerSlave module writes a ten-block POWERSLAVE1 file into the player's backup memory on the
 * FIRST BOOT of a game that never uses it, asks to format an unformatted device before the title
 * even shows, and drops the player into the BIOS (SYS_EXECDMP) when they say no.  Doom does none
 * of that.  It also costs MAIN about a kilobyte less than BUP.o did, most of it the 1 000-byte
 * saveGames array of PowerSlave states.
 *
 * WHAT A SAVE IS.  The START of the level being played: map, skill, health, armour, weapons,
 * ammo, backpack -- exactly what G_PlayerFinishLevel carries from one level to the next, and
 * exactly what a PlayStation Doom password held.  LOAD runs that level with that kit.  The pause's
 * RESTART LEVEL is the same thing from the copy already in memory (DOOM_PLAYER.C's stash), so one
 * mechanism serves both and neither has to serialise a level in flight.  A full save state would
 * be about 17 KB -- half the internal backup memory for ONE save -- and a permanent tile or two
 * off every map for the restore code, which cannot live in an overlay because it runs at load.
 *
 * WHERE THE LIBRARY LIVES.  BUP_Init wants 16 KB + 8 KB of work memory for the whole session.  At
 * boot and at the title the level pool is empty, so they come from it as PowerSlave's did.  During
 * a pause the pool belongs to the level, and the buffers go in the memory the overlay loader hands
 * over instead (doorwayCache's tail): the pause never asks the level for 24 KB.
 *
 * THE FILE.  One file a device, 6 records of 32 bytes behind a 16-byte header -- 4 blocks of 64,
 * under one per cent of the internal memory.  A bad sum or an unknown version makes the whole file
 * count as "no save"; it is never read as one, and never deleted behind the player's back. */
#include <machine.h>
#include <sega_int.h>
#include <sega_per.h>
#include <sega_bup.h>
#include <stdio.h>
#include <string.h>

#include "v_blank.h"
#include "util.h"
#include "print.h"
#include "gamestat.h"
#include "sprite.h"
#include "mplayer.h"
#include "doom.h"

typedef char doomSaveRecIs32_[(sizeof(DoomSaveRec)==32)?1:-1];

/* PowerSlave's BUP.C exported these two; INITMAIN and the crash handler look at them. */
void *bupSpace=NULL;
void *bupWork=NULL;

#define SAVE_FILE   "AGUZDOOM_01"       /* Sega's 8 + _NN pattern, 11 characters */
#define SAVE_MAGIC  0x41475a44          /* 'AGZD' */
#define SAVE_VER    1
#define SAVE_BYTES  (16+DOOM_NMSAVES*(int)sizeof(DoomSaveRec))   /* 16 + 6*32 = 208 */
#define SAVE_BLOCKS 4                   /* ceil((208 + a 32-byte directory entry) / 64) */

/* the file, header then records, read and written whole */
typedef struct
{unsigned int magic;
 unsigned char version,slots;
 unsigned short sum;                    /* over the records only */
 unsigned short levels;                 /* saveLevelStamp(): WHICH disc's level list */
 unsigned char spare[6];
 DoomSaveRec rec[DOOM_NMSAVES];
} DoomSaveFile;

static DoomSaveFile saveFile;           /* 208 B of BSS, against PowerSlave's 1 000 */
static BupConfig config[3];
static signed char saveDevice=-1;       /* the device the list on screen came from */
static char saveOpen;                   /* the library is initialised right now */
char doomSaveAny;                       /* a file of ours was found at boot: LOAD GAME is live */
char doomSaveDevices;                   /* how many devices answered at boot: 1 = no device line */
DoomSaveRec doomLevelStartRec;          /* the level being played, as it began */
unsigned short doomGameSeconds;         /* play time up to this level's start */

/* ----------------------------------------------------------------- the library */
/* lib / work NULL: from the level pool, which is empty at boot and at the title.  From the pause
   they come out of the memory the overlay loader left free -- the level keeps its pool. */
void doom_bupOpen(void *lib,void *work)
{if (saveOpen)
    return;
 bupSpace=lib? lib: mem_malloc(0,16*1024);
 bupWork=work? work: mem_malloc(0,8*1024);
 if (!bupSpace || !bupWork)
    {if (!lib && bupSpace)  mem_free(bupSpace);
     if (!work && bupWork)  mem_free(bupWork);
     bupSpace=bupWork=NULL;
     return;
    }
 saveOpen=lib? 2: 1;                    /* 2 = borrowed memory: nothing to give back */
 resetDisable();
 BUP_Init((Uint32 *)bupSpace,(Uint32 *)bupWork,config);
 resetEnable();
}

void doom_bupClose(void)
{if (saveOpen==1)
    {mem_free(bupWork);
     mem_free(bupSpace);
    }
 bupSpace=bupWork=NULL;
 saveOpen=0;
}

/* the devices that answered, and how many.  Read at run time, never at compile time: a memory
   card or an Action Replay in the slot is found the same way the internal memory is. */
int doom_saveDevices(void)
{int d,n=0;
 if (!saveOpen)
    return 0;
 for (d=0;d<2;d++)
    if (config[d].unit_id)
       n++;
 return n;
}

int doom_saveDevicePresent(int d)
{return (d>=0 && d<2 && saveOpen && config[d].unit_id)? 1: 0;
}

/* a cartridge has partitions; the first is ours (the prior art in Tethys does the same) */
static void selPart(int d)
{if (d>0 && config[d].partition>1)
    BUP_SelPart(d,0);
}

/* ----------------------------------------------------------------- the file */
/* GCC14: the disc's own level list, in one word.  A record carries a level INDEX, and the list
   is no longer the C's but the .cfg's (tools/doom2ps/episodes.py) -- so the same index means a
   different map on a disc built from a different .cfg, and a save made on one would send the
   game to the wrong level.  The stamp says which list the file was written against; a file that
   does not match is left ALONE on the card and read as an empty list, exactly as a file from an
   unknown version is.  0 means a file written before the list could change at all, which can
   only be the shareware one: those are still read. */
static unsigned short saveLevelStamp(void)
{unsigned short s=0;
 const char *p;
 int i;
 for (i=0;i<DOOM_NMLEVELS;i++)
    for (p=doomLevelNames[i];*p;p++)
       s=(unsigned short)(s*31+(unsigned char)*p);
 return s? s: 1;                        /* never 0: that value means "no stamp" */
}

static unsigned short saveSum(const DoomSaveFile *f)
{const unsigned char *p=(const unsigned char *)f->rec;
 int i,n=DOOM_NMSAVES*(int)sizeof(DoomSaveRec);
 unsigned short s=0;
 for (i=0;i<n;i++)
    s=(unsigned short)(s*31+p[i]);
 return s;
}

static void saveEmpty(void)
{memset(&saveFile,0,sizeof(saveFile));
 saveFile.magic=SAVE_MAGIC;
 saveFile.version=SAVE_VER;
 saveFile.slots=DOOM_NMSAVES;
 saveFile.levels=saveLevelStamp();
}

/* -> 1 = a file of ours, read and sound.  Anything else leaves an empty list: a file another
   game wrote, or one of ours from a version we do not know, is never read as a save and never
   deleted -- the BIOS manager is the place for that. */
int doom_saveRead(int device)
{BupDir dir;
 saveEmpty();
 if (!doom_saveDevicePresent(device))
    return 0;
 selPart(device);
 if (BUP_Dir(device,(Uint8 *)SAVE_FILE,1,&dir)!=1)
    return 0;
 if (dir.datasize!=SAVE_BYTES)
    return 0;                           /* not our shape: leave it entirely alone */
 if (BUP_Read(device,(Uint8 *)SAVE_FILE,(Uint8 *)&saveFile))
    {saveEmpty();
     return 0;
    }
 if (saveFile.magic!=SAVE_MAGIC || saveFile.version!=SAVE_VER ||
     saveFile.slots!=DOOM_NMSAVES || saveFile.sum!=saveSum(&saveFile))
    {saveEmpty();
     return 0;
    }
 if (saveFile.levels && saveFile.levels!=saveLevelStamp())
    {saveEmpty();                       /* another disc's levels: the indices mean other maps */
     return 0;
    }
 return 1;
}

/* -> 0 written, else the BUP error (BUP_WRITE_PROTECT, BUP_NOT_ENOUGH_MEMORY ...) */
int doom_saveWrite(int device)
{BupDir dir;
 BupDate date;
 int year,month,day,hour,min,ret;
 if (!doom_saveDevicePresent(device))
    return BUP_NON;
 selPart(device);
 saveFile.magic=SAVE_MAGIC;
 saveFile.version=SAVE_VER;
 saveFile.slots=DOOM_NMSAVES;
 saveFile.levels=saveLevelStamp();
 saveFile.sum=saveSum(&saveFile);
 memset(&dir,0,sizeof(dir));
 strcpy((char *)dir.filename,SAVE_FILE);
 strcpy((char *)dir.comment,"DOOM");
 dir.language=BUP_ENGLISH;
 getDateTime(&year,&month,&day,&hour,&min);
 date.year=(Uint8)year;
 date.month=(Uint8)month;
 date.day=(Uint8)day;
 date.time=(Uint8)hour;
 date.min=(Uint8)min;
 date.week=0;
 dir.date=BUP_SetDate(&date);
 dir.datasize=SAVE_BYTES;
 dir.blocksize=SAVE_BLOCKS;
 resetDisable();
 ret=(int)BUP_Write(device,&dir,(Uint8 *)&saveFile,OFF);
 resetEnable();
 return ret;
}

/* the device's state for the messages: 0 fine, BUP_UNFORMAT, or BUP_NOT_ENOUGH_MEMORY with the
   blocks still wanted in *shortBy */
int doom_saveRoom(int device,int *shortBy)
{BupStat st;
 int r;
 if (shortBy)
    *shortBy=0;
 if (!doom_saveDevicePresent(device))
    return BUP_NON;
 selPart(device);
 r=(int)BUP_Stat(device,SAVE_BYTES,&st);
 if (r)
    return r;                           /* BUP_UNFORMAT, mostly */
 if (st.datanum==0)
    {/* no room for one more file of our size: say how many blocks are missing, as the engine's
	own message did, but with the device's block size rather than a hard 64 */
     int want=(SAVE_BYTES+(int)st.blocksize-1)/(int)(st.blocksize? st.blocksize: 64)+1;
     if (shortBy)
	*shortBy=want-(int)st.freeblock;
     return BUP_NOT_ENOUGH_MEMORY;
    }
 return 0;
}

int doom_saveFormat(int device)
{int r;
 if (!doom_saveDevicePresent(device))
    return BUP_NON;
 resetDisable();
 r=(int)BUP_Format(device);
 resetEnable();
 return r;
}

/* ----------------------------------------------------------------- the records */
const DoomSaveRec *doom_saveSlot(int slot)
{const DoomSaveRec *r;
 if (slot<0 || slot>=DOOM_NMSAVES || !saveFile.rec[slot].used)
    return NULL;
 r=&saveFile.rec[slot];
 /* the sum already said the bytes are ours; this says they still MEAN something.  A record that
    names a level or a skill this build does not have is not shown and never applied -- the slot
    reads EMPTY rather than sending the game to a map that is not on the disc. */
 if (r->level>=DOOM_NMLEVELS || r->skill>=CFG_MP_NMSKILLS)
    return NULL;
 return r;
}

int doom_saveSlotUsed(int slot)
{return doom_saveSlot(slot)!=NULL;
}

/* the newest save, by the rotating serial: the list marks it */
int doom_saveNewest(void)
{int i,best=-1,s=-1;
 for (i=0;i<DOOM_NMSAVES;i++)
    if (saveFile.rec[i].used && (int)saveFile.rec[i].serial>s)
       {s=saveFile.rec[i].serial;
	best=i;
       }
 return best;
}

/* THE CAPTURE, at the end of every level start (DOOM_PLAYER.C): one record, no CPU per frame. */
void doom_saveCapture(void)
{DoomSaveRec *r=&doomLevelStartRec;
 int i;
 r->used=1;
 r->level=(unsigned char)currentState.currentLevel;
 r->skill=(unsigned char)mpSkill;
 r->weaponOwned=doomPlayer.weaponOwned;
 r->readyWeapon=(signed char)doomPlayer.readyWeapon;
 r->armorType=(unsigned char)doomPlayer.armorType;
 r->backpack=(unsigned char)(doomPlayer.backpack?1:0);
 r->health=(short)currentState.health;
 r->armorPoints=(short)doomPlayer.armorPoints;
 for (i=0;i<DOOM_NUMAMMO;i++)
    r->ammo[i]=(short)doomPlayer.ammo[i];
 r->gameSeconds=doomGameSeconds;
}

/* Apply a record: the next doom_playerInit keeps what is set here instead of giving the pistol
   kit, because doomCarry says so.  maxAmmo is NOT stored -- it follows from the backpack, so no
   save can carry an impossible limit. */
void doom_saveApply(const DoomSaveRec *r)
{int i;
 if (!r || !r->used)
    return;
 mpStartLevel=r->level;
 mpSkill=r->skill;
 currentState.currentLevel=r->level;
 currentState.health=r->health;
 doomPlayer.health=r->health;
 doomPlayer.armorPoints=r->armorPoints;
 doomPlayer.armorType=r->armorType;
 doomPlayer.weaponOwned=r->weaponOwned;
 doomPlayer.readyWeapon=r->readyWeapon;
 doomPlayer.pendingWeapon=r->readyWeapon;
 doomPlayer.backpack=r->backpack;
 for (i=0;i<DOOM_NUMAMMO;i++)
    {doomPlayer.ammo[i]=r->ammo[i];
     doomPlayer.maxAmmo[i]=doom_maxAmmoOf(i)<<r->backpack;
    }
 doomGameSeconds=r->gameSeconds;
 doom_playerCarry();                    /* the arsenal above survives the next level start */
}

/* SAVE: the level-start record into a slot, then the whole file.  -> 0 written, else a BUP code. */
int doom_saveStore(int device,int slot)
{int i,s=-1;
 if (slot<0 || slot>=DOOM_NMSAVES)
    return BUP_NON;
 for (i=0;i<DOOM_NMSAVES;i++)
    if (saveFile.rec[i].used && (int)saveFile.rec[i].serial>s)
       s=saveFile.rec[i].serial;
 saveFile.rec[slot]=doomLevelStartRec;
 saveFile.rec[slot].serial=(unsigned char)(s+1);
 {int year,month,day,hour,min;
  getDateTime(&year,&month,&day,&hour,&min);
  saveFile.rec[slot].year=(unsigned char)year;
  saveFile.rec[slot].month=(unsigned char)month;
  saveFile.rec[slot].day=(unsigned char)day;
  saveFile.rec[slot].hour=(unsigned char)hour;
  saveFile.rec[slot].min=(unsigned char)min;
 }
 i=doom_saveWrite(device);
 if (!i)
    doomSaveAny=1;                      /* the title's LOAD GAME is live from now on, without a
										 reboot: the boot scan is not the only thing that knows */
 return i;
}

int doom_saveLoadDevice(void)
{return saveDevice;
}

void doom_saveSetDevice(int d)
{saveDevice=(signed char)d;
}

/* ----------------------------------------------------------------- the engine's seams */
/* Boot (SRUINS.C): SCAN ONLY.  No format question, no file written, no exit to the BIOS.  It
   leaves the list of the first device that holds one of our files, so the title's LOAD GAME is
   live or empty without a second read. */
void bup_initialProc(void)
{int d;
 doomSaveAny=0;
 saveDevice=-1;
 saveEmpty();
 doom_bupOpen(NULL,NULL);
 if (!saveOpen)
    return;
 doomSaveDevices=(char)doom_saveDevices();
 for (d=1;d>=0;d--)                     /* the highest device first: a cartridge wins a tie */
    if (doom_saveRead(d))
       {saveDevice=(signed char)d;
	doomSaveAny=1;
	break;
       }
 if (saveDevice<0)
    {/* none of ours anywhere: point at a device that exists, so SAVE opens on something real */
     for (d=1;d>=0;d--)
	if (doom_saveDevicePresent(d))
	   {saveDevice=(signed char)d;
	    break;
	   }
     saveEmpty();
    }
 doom_bupClose();
}

int bup_canLoadGame(void)
{return doomSaveAny;
}

/* One player, in a campaign.  A split-screen or competitive game has no single arsenal to write,
   and the pause says which rule refused it. */
/* GCC14: A CO-OPERATIVE GAME SAVES.  What a record holds is the START of the level with player
   1's arsenal, which exists whoever else is in the game -- the pause runs with player 1's state
   loaded (SRUINS.C switches to it before the menu), so the save is the ordinary one and the
   others simply are not in it.  mpPlayers==1 kept it out of co-op for no reason; the mode test
   is the one that matters, and it still refuses a competitive game, where there is no single
   arsenal to write. */
int bup_canSaveGame(void)
{return (saveDevice>=0 && mpMode==MP_COOP)? 1: 0;
}

/* The title's LOAD GAME picked a slot (INTRO.C loadMenu).  The list on screen is the one the boot
   scan read; applying it is all that is left. */
int bup_loadGame(int slot)
{const DoomSaveRec *r=doom_saveSlot(slot);
 if (!r)
    return 1;                           /* an empty slot: the menu stays up */
 doom_saveApply(r);
 return 0;
}

/* PowerSlave wrote a save between levels.  Doom saves only when the player asks. */
void bup_saveGame(void)
{
}

/* A new game: the engine's own start values, minus PowerSlave's inventory (INTRO.C, after the
   skill and players screen).  Health is the engine's, the arsenal is doom_playerInit's. */
void bup_initCurrentGame(void)
{int i;
 currentState.inventory=0;
 currentState.gameFlags=0;
 currentState.nmBowls=1;
 currentState.health=100;              /* G_PlayerReborn, as DOOM_PLAYER.C's kit does */
 currentState.dolls=0;
 for (i=0;i<WP_NMWEAPONS;i++)
    currentState.weaponAmmo[i]=0;       /* also DOOM_PLAYER.C's level-start stash: none yet */
 for (i=0;i<NMLEVELS;i++)
    currentState.levFlags[i]=0;
 currentState.currentLevel=0;
 currentState.desiredWeapon=0;
 doomGameSeconds=0;
 memset(&doomLevelStartRec,0,sizeof(doomLevelStartRec));
 {int year,month,day,hour,min;
  getDateTime(&year,&month,&day,&hour,&min);
  currentState.year=(short)year;
  currentState.month=(short)month;
  currentState.day=(short)day;
  currentState.hour=(short)hour;
  currentState.min=(short)min;
 }
}

/* PowerSlave's NEW GAME wrote its slot here.  Doom's new game asks for a skill, not a slot. */
int bup_newGame(int slot)
{(void)slot;
 bup_initCurrentGame();
 return 0;
}

/* One row of the title's LOAD GAME list (MENU.C IT_GAMEBUTTON, Doom's branch): the level the save
   names, the skill it was played on, and the kit it started with.  The skull beside the chosen
   row is the one the rest of the Doom title uses (CFG_MENU_CURSOR), not PowerSlave's dark box. */
void doom_saveDrawSlot(int slot,int x,int y,int selected)
{const DoomSaveRec *r=doom_saveSlot(slot);
 char line[72],lab[16];
 int t;
 if (!r)
    return;
 if (selected)
    CFG_MENU_CURSOR(x,y);
 strcpy(lab,doom_levelLabel(r->level));
 drawString(x,y,2,(unsigned char *)lab);
 strcpy(line,doom_skillName(r->skill));
 drawString(x+58,y,1,(unsigned char *)line);
 t=r->gameSeconds;
 sprintf(line,"STARTED %d HP  %d AP  %d:%02d  %02d/%02d %02d:%02d",
	 r->health,r->armorPoints,t/60,t%60,r->day,r->month,r->hour,r->min);
 drawString(x+58,y+9,1,(unsigned char *)line);
}

/* PowerSlave's dialogs draw a SaveState; Doom's slot lines are its own (CFG_SAVE_DRAWSLOT). */
SaveState *bup_getGameData(int slot)
{(void)slot;
 return NULL;
}

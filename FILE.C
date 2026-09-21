#include <machine.h>

#include <libsn.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_cdc.h>
#include <sega_gfs.h>
#include <sega_sys.h>
#include <sega_int.h>

#include <string.h>
#include "util.h"
#include "spr.h"
#include "file.h"
#include "v_blank.h"
#include "print.h"
#include "local.h"

void startSlave(void *slaveMain);

static int progressTotalSize,progressTotalRead;
static int progressOn=0; /* 1 if on, 2 if swirly */
static Uint32 progressNextUpdate;

#ifdef PSYQ
#define PSYQBASEPATH "c:\\sr3\\data\\"

void psyq_init(void)
{PCinit();
}

int psyq_open(char *filename)
{char buff[160];
 int retVal;
 strcpy(buff,PSYQBASEPATH);
 strcat(buff,filename);
 retVal=PCopen(buff,0,0);
 assert(retVal>=0);
 return retVal;
}

int psyq_read(int fd, char *buf, int n)
{int v;
 if (n==0)
    return 0;
 v=PCread(fd,buf,n);
 assert(v>=0);
 return v;
}

int psyq_getFileSize(int fd)
{int size=PClseek(fd,0,2);
 PClseek(fd,0,0);
 return size;
}

int psyq_close(int fd)
{return PCclose(fd);
}

#endif

void errorFunc(void *null,Sint32 errorCode)
{if (errorCode==GFS_ERR_CDOPEN)
    SYS_EXECDMP();
 assert(0);
}

static void whackCD(void)
{int ret;
 CdcStat stat;
 int startTime;
 CdcPos pos;
 CDC_POS_PTYPE(&pos)=CDC_PTYPE_NOCHG;
 ret=CDC_CdSeek(&pos);
 if (ret!=CDC_ERR_OK)
    SYS_EXECDMP();
 startTime=vtimer;
 while (vtimer-startTime<60*2)
    {ret=CDC_GetPeriStat(&stat);
     if (ret==CDC_ERR_PERI)
        continue;
     if (ret!=CDC_ERR_OK)
        SYS_EXECDMP();
     if (CDC_GET_STC(&stat)==CDC_ST_PAUSE)
	return;
    }
 SYS_EXECDMP();
}

#define OPENMAX 1
#define DIRMAX 100
static int cdWork[(GFS_WORK_SIZE(OPENMAX)+3)/4];
static GfsDirName dir[DIRMAX];
static GfsDirTbl dirtbl;

static char sectorBuff[2048] __attribute__ ((aligned (4)));
static char *sectorBuffPos;
static GfsHn openCDFile; /* only one cd file may be open at a time */
#define CDHANDLE 8000

#ifdef FLASH
static void changeDir(char *dirNm)
{Sint32 fid;
 fid=GFS_NameToId(dirNm);
 GFS_DIRTBL_TYPE(&dirtbl)=GFS_DIR_NAME;
 GFS_DIRTBL_NDIR(&dirtbl)=DIRMAX;
 GFS_DIRTBL_DIRNAME(&dirtbl)=dir;
 GFS_LoadDir(fid,&dirtbl);
 GFS_SetDir(&dirtbl);
}
#endif

void fs_init(void)
{int tryCount,ret;
 progressOn=0;
#ifdef PSYQ
 psyq_init();
 PSYQcdinit();
#endif
 openCDFile=NULL;
 sectorBuffPos=sectorBuff+2048;
 GFS_DIRTBL_TYPE(&dirtbl)=GFS_DIR_NAME;
 GFS_DIRTBL_NDIR(&dirtbl)=DIRMAX;
 GFS_DIRTBL_DIRNAME(&dirtbl)=dir;
 tryCount=0;
 while (1)
    {ret=GFS_Init(OPENMAX,cdWork,&dirtbl);
     tryCount++;
     if (ret>=0)
	break; /* success */
     if (tryCount>3)
	SYS_EXECDMP();
     if (ret==GFS_ERR_CDRD || ret==GFS_ERR_FATAL)
	{whackCD();
	 continue;
	}
     SYS_EXECDMP();
    }
 GFS_SetErrFunc(errorFunc,NULL);
#ifdef FLASH
 changeDir("1999");
#endif
}

/* GCC14: a file a game may do without (PSMULTI.C PSPLAYER.DAT) -- fs_open would assert */
int fs_exists(char *filename)
{
#ifdef PSYQ
 return 1;
#else
 if (filename[0]=='+')
    filename++;
 return GFS_NameToId(filename)>=0;
#endif
}

int fs_open(char *filename)
{int id;
#ifdef PSYQ
 if (filename[0]=='+')
    return psyq_open(filename+1);
#else
 if (filename[0]=='+')
    filename++;
#endif
 assert(!openCDFile);
 id=GFS_NameToId(filename);
 assert(id>=0);
 sectorBuffPos=sectorBuff+2048;
 openCDFile=GFS_Open(id);
 assert(openCDFile); /* do retry processing here */
 GFS_SetTransPara(openCDFile,10);
 GFS_SetTmode(openCDFile,GFS_TMODE_CPU);
 GFS_NwCdRead(openCDFile,500*2048); /* start prefetch */
 return CDHANDLE;
}

void fs_close(int handle)
{
#ifdef PSYQ
 if (handle!=CDHANDLE)
    {psyq_close(handle);
     return;
    }
#endif
 assert(handle==CDHANDLE);
 assert(openCDFile);
 GFS_NwStop(openCDFile); /* stop prefetch */
 GFS_Close(openCDFile);
 openCDFile=NULL;
}

int fs_getFileSize(int fd)
{Sint32 sctsize,nsct,lastsize;
#ifdef PSYQ
 if (fd!=CDHANDLE)
    return psyq_getFileSize(fd);
#endif
 assert(fd==CDHANDLE);
 assert(openCDFile);
 GFS_GetFileSize(openCDFile,&sctsize,&nsct,&lastsize);
 return sctsize*(nsct-1)+lastsize;
}

void fs_read(int fd,char *buf,int n)
{int s,bufferLeft;
#ifdef PSYQ
 if (fd!=CDHANDLE)
    {psyq_read(fd,buf,n);
     return;
    }
#endif
 assert(fd==CDHANDLE);
 assert(openCDFile);
 while (n)
    {assert(n>0);
     bufferLeft=2048-(sectorBuffPos-sectorBuff);
     assert(bufferLeft>=0);
     assert(bufferLeft<=2048);
     if (!bufferLeft)
	{GFS_Fread(openCDFile,1,sectorBuff,2048);
	 if (progressOn)
	    {progressTotalRead++;
	     if (progressOn==1 && vtimer>progressNextUpdate)
		{XyInt parms[4];
		 int p;
		 progressNextUpdate=vtimer+2;
		 EZ_openCommand();
		 EZ_localCoord(320/2,240/2);
		 p=(200*progressTotalRead)/progressTotalSize;
		 parms[0].x=-100;   parms[0].y=-5;
		 parms[1].x=-100+p; parms[1].y=-5;
		 parms[2].x=-100+p; parms[2].y=5;
		 parms[3].x=-100;   parms[3].y=5;
		 EZ_polygon(ECD_DISABLE|SPD_DISABLE,RGB(0,0,31),parms,NULL);
		 parms[1].x=100;
		 parms[2].x=100;
		 EZ_polyLine(ECD_DISABLE|SPD_DISABLE,RGB(31,0,0),
			     parms,NULL);
		 parms[0].x--; parms[0].y--;
		 parms[1].x++; parms[1].y--;
		 parms[2].x++; parms[2].y++;
		 parms[3].x--; parms[3].y++;
		 EZ_polyLine(ECD_DISABLE|SPD_DISABLE,RGB(31,0,0),
			     parms,NULL);
#ifndef JAPAN
		 drawString(-getStringWidth(1,getText(LB_PROMPTS,0))/2,-20,
			    1,getText(LB_PROMPTS,0));
#endif

		 EZ_closeCommand();
		 SPR_WaitDrawEnd();
		 SCL_DisplayFrame();
		}
	    }
	 GFS_NwExecOne(openCDFile);

	 sectorBuffPos=sectorBuff;
	 bufferLeft=2048;
	}
     s=n;
     if (bufferLeft<s)
	s=bufferLeft;

     qmemcpy(buf,sectorBuffPos,s);
     buf+=s;
     sectorBuffPos+=s;

     n-=s;
    }
 assert(n==0);
}

void playWholeCD(void)
{CdcPly cp;
 CDC_PLY_STYPE(&cp)=CDC_PTYPE_TNO;
 CDC_PLY_STNO(&cp)=2;
 CDC_PLY_SIDX(&cp)=0;
 CDC_PLY_ETYPE(&cp)=CDC_PTYPE_TNO;
 CDC_PLY_ETNO(&cp)=12;
 CDC_PLY_EIDX(&cp)=0;
 CDC_PLY_PMODE(&cp)=0x0f; /* infinite repeat */
 CDC_CdPlay(&cp);
}

void playCDTrack(int track,int repeat)
{CdcPly cp;
#ifdef FLASH
 track=4;
#endif
 CDC_PLY_STYPE(&cp)=CDC_PTYPE_TNO;
 CDC_PLY_STNO(&cp)=track;
 CDC_PLY_SIDX(&cp)=0;
 CDC_PLY_ETYPE(&cp)=CDC_PTYPE_TNO;
 CDC_PLY_ETNO(&cp)=track;
 CDC_PLY_EIDX(&cp)=0;
 CDC_PLY_PMODE(&cp)=repeat?0x0f:0x00;
 CDC_CdPlay(&cp);
}

int getTrackStartFAD(int track)
{Uint32 toc[102];
 CDC_TgetToc(toc);
 return (toc[track-1] & 0x00ffffff);
}

int getCurrentFAD(void)
{CdcStat ret;
 CDC_GetPeriStat(&ret);
 return CDC_STAT_FAD(&ret);
}

int getCurrentStatus(void)
{CdcStat ret;
 CDC_GetPeriStat(&ret);
 return CDC_STAT_STATUS(&ret);
}

void stopCD(void)
{CdcPos pos;
 CDC_POS_PTYPE(&pos)=CDC_PTYPE_NOCHG;
 CDC_CdSeek(&pos);
}

void fs_getStatus(int *stat,int *ndata)
{GFS_NwGetStat(openCDFile,(Sint32 *)stat,(Sint32 *)ndata);
}

void fs_execOne(void)
{GFS_NwExecOne(openCDFile);
}

void fs_startProgress(int swirly)
{progressTotalSize=1; /* prevent div by 0 */
 progressTotalRead=0;
 progressOn=1;
 progressNextUpdate=vtimer;
}

void fs_addToProgress(char *filename)
{int id;
 GfsHn hn;
 Sint32 nsct;
#ifdef PSYQ
 if (filename[0]=='+')
    return;
#endif
 if (filename[0]=='+')
    filename++;
 id=GFS_NameToId(filename);
 assert(id>=0);
 hn=GFS_Open(id);
 GFS_GetFileSize(hn,NULL,&nsct,NULL);
 GFS_Close(hn);
 progressTotalSize+=nsct;
}

void fs_closeProgress(void)
{progressOn=0;
}

void executeLink(void *data,int size);

/* GCC14: called before another program replaces this one, so a service that pointed a vector
   into it takes it back (CRASH.C).  FILE.C is in every program: a pointer, not a call. */
void (*linkHook)(void);

void link(char *filename)
{char *data;
 void (*code)(void *data,int size);
 int fd;
 int size;
 if (linkHook)
    linkHook();
 mem_init();
 fd=fs_open(filename);
 size=fs_getFileSize(fd);
 dPrint("size=%d\n",size);
 size=(size+3)&(~3);
 data=mem_malloc(0,size);
 fs_read(fd,data,size);
 fs_close(fd);
 code=mem_malloc(0,256);
 memcpy(code,executeLink,256);
 dPrint("linking\n");
 INT_SetMsk(INT_MSK_ALL);
 INT_SetScuFunc(INT_SCU_HBLK_IN,NULL);
 INT_SetScuFunc(INT_SCU_VBLK_IN,NULL);
 INT_SetScuFunc(INT_SCU_VBLK_OUT,NULL);
 /* set_imask(0xffffffff); */
 (*code)(data,size>>2);
}

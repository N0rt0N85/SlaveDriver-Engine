/* MPRULES.C -- local multiplayer, an engine service (GCC14): the rules of a game of several
   players (MPLAYER.H mpMode), its score, its spawn spots, the title's multiplayer screen and the
   end-of-level score.  Apart from MPLAYER.C, whose player swap is on every image's path: this
   runs a few times a level, or at the title, and is built for size (Makefile). */
#include <string.h>
#include <stdio.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include "util.h"
#include "spr.h"
#include "sprite.h"
#include "object.h"
#include "mplayer.h"
#include "print.h"
#include "sound.h"
#include "v_blank.h"
#include <sega_per.h>

/* --- rules, score, spawn spots ------------------------------------------------------------- */

int mpMode=MP_COOP;
unsigned char mpTeam[MPMAX];
unsigned char mpRole[MPMAX];
int mpFragLimit,mpTimeLimit;
int mpStartLevel;
MpStat mpStat[MPMAX];
short mpTotal[3];

int mpAllies(int a,int b)
{if (a<0 || b<0)
    return 0;
 if (a==b)
    return 1;
 switch (mpMode)
    {case MP_COOP:     return 1;
     case MP_TEAM:     return mpTeam[a]==mpTeam[b];
     case MP_MONSTERS: return mpRole[a]==mpRole[b];
     case MP_BOSS:     return mpRole[a] && mpRole[b];   /* the bosses stand together */
     default:          return 0;           /* deathmatch, the boss battle's marines: every player for themself */
    }
}

int mpSide(int k)
{return (mpMode==MP_TEAM)? MPMAX+mpTeam[k]: k;
}

void mpScoreDeath(int victim,int killer)
{assert(victim>=0 && victim<MPMAX);
 mpStat[victim].deaths++;
 if (killer==-2)
    return;                             /* a monster's kill: no frag either way */
 if (killer<0 || killer==victim)
    mpStat[victim].frags--;             /* the world, or oneself */
 else if (mpAllies(killer,victim))
    mpStat[killer].frags--;
 else
    mpStat[killer].frags++;
}

#define MPMAXSPOTS 32
static struct {MthXyz feet; short sector,yaw;} mpSpot[MPMAXSPOTS];
static int mpNmSpots;

void mpSpotAdd(int sector,MthXyz *feet,int yaw)
{if (mpNmSpots>=MPMAXSPOTS)
    return;
 mpSpot[mpNmSpots].feet=*feet;
 mpSpot[mpNmSpots].sector=(short)sector;
 mpSpot[mpNmSpots].yaw=(short)(normalizeAngle(yaw)>>16);
 mpNmSpots++;
}

/* The spot whose nearest other player is the farthest away -- one of those that come within a
   quarter of the best, at random, so that a respawn cannot be camped. */
int mpSpotFar(int k,int *sector,MthXyz *feet,int *yaw)
{int i,j,n,pick;
 Fixed32 d,nearest,best=-3,cut,far[MPMAXSPOTS];
 if (!mpNmSpots)
    return 0;
 for (i=0;i<mpNmSpots;i++)
    {nearest=0x7fffffff;
     for (j=0;j<mpPlayers;j++)
	if (j!=k && mpBody[j])
	   {d=abs(mpBody[j]->pos.x-mpSpot[i].feet.x)+abs(mpBody[j]->pos.z-mpSpot[i].feet.z);
	    if (d<nearest)
	       nearest=d;
	   }
     if (!CFG_MP_SPOTSAFE(mpSpot[i].sector))
	nearest=-2;                     /* a floor that hurts: never, while another spot is left */
     far[i]=nearest;
     if (nearest>best)
	best=nearest;
    }
 cut=(best>0)? best-(best>>2): best;    /* nothing safe: any spot, as unsafe as the others */
 for (i=0,n=0;i<mpNmSpots;i++)
    if (far[i]>=cut)
       n++;
 pick=getNextRand()%n;
 for (i=0;i<mpNmSpots-1;i++)
    if (far[i]>=cut && !pick--)
       break;
 *sector=mpSpot[i].sector;
 *feet=mpSpot[i].feet;
 *yaw=mpSpot[i].yaw<<16;
 return 1;
}

void mpLevelReset(void)
{mpNmSpots=0;
 memset(mpStat,0,sizeof(mpStat));
 memset(mpTotal,0,sizeof(mpTotal));
}

/* --- the title's game screens ------------------------------------------------------------
   NEW GAME (co-op rules, 1 to 4 players) and MULTIPLAYER (every mode, 2 to 4).  Their own loop
   rather than a dialog: dlg_run answers only a press, and every line here is a value the d-pad
   turns -- but MR_BOSS, which only says what the boss battle's map holds.  bigFont (2), like the
   title's buttons: capitals, digits, spaces and '-' only. */
enum {MR_MAP,MR_BOSS,MR_MODE,MR_SKILL,MR_PLAYERS,MR_FRAGS,MR_TIME,MR_P1,MR_START=MR_P1+MPMAX,MR_BACK,MR_NM};
static const char *const mpModeName[MP_NMMODES]=
   {"COOPERATIVE","DEATHMATCH","TEAM DEATHMATCH",CFG_MP_MONSTERS_NAME,"BOSS BATTLE"};
static const unsigned char mpFragChoice[5]={0,10,20,30,50};
static const unsigned char mpTimeChoice[5]={0,5,10,15,20};
/* what the screen shows, kept from one visit to the next: players 2 and 4 start on the other
   side -- the red team, the monsters, the bosses */
static unsigned char mpMenuTeam[MPMAX]={0,1,0,1};
static unsigned char mpMenuRole[MPMAX]={0,1,0,1};
static unsigned char mpMenuBoss[MPMAX]={0,1,0,1};
int mpSkill=CFG_MP_SKILLDEFAULT;

static int mpRowShown(int r,int players,int multi)
{if (!multi)
    return r==MR_SKILL || r==MR_PLAYERS || r==MR_START || r==MR_BACK;
 if (r==MR_BOSS)
    return mpMode==MP_BOSS;
 if (r==MR_SKILL)
    return mpMode==MP_COOP || mpMode==MP_MONSTERS;   /* the modes with monsters */
 if (r==MR_FRAGS || r==MR_TIME)
    return mpCompetitive();
 if (r>=MR_P1 && r<MR_P1+MPMAX)
    return (mpMode==MP_TEAM || mpMode==MP_MONSTERS || mpMode==MP_BOSS) && r-MR_P1<players;
 return 1;
}

/* BOSS BATTLE: as many bosses as the map holds, and one marine at least */
static int mpBossCap(int players,int level)
{int n=CFG_MP_BOSSCOUNT(level);
 if (n<1)
    n=1;
 return (n<players)? n: players-1;
}

/* player k is a boss: marked, and among the first mpBossCap marked (the map, or the player
   count, may have changed since the marks were set) */
static int mpIsBoss(int k,int players,int level)
{int j,n=0;
 if (!mpMenuBoss[k] || k>=players)
    return 0;
 for (j=0;j<k;j++)
    n+=mpMenuBoss[j];
 return n<mpBossCap(players,level);
}

/* the d-pad on player k's line: a marine takes a boss -- from the first other boss when all
   the map's are taken; a boss gives it up, unless it is the last one */
static void mpToggleBoss(int k,int players,int level)
{unsigned char boss[MPMAX];
 int j,n=0;
 for (j=0;j<MPMAX;j++)
    n+=(boss[j]=(unsigned char)mpIsBoss(j,players,level));
 if (boss[k])
    {if (n>1)
	mpMenuBoss[k]=0;
     return;
    }
 for (j=0;j<players;j++)
    mpMenuBoss[j]=boss[j];
 if (n>=mpBossCap(players,level))
    for (j=0;j<players;j++)
       if (mpMenuBoss[j])
	  {mpMenuBoss[j]=0;
	   break;
	  }
 mpMenuBoss[k]=1;
}

static int mpCycle(int v,int n,int d)
{return (v+d+n)%n;
}

static int mpIndexOf(const unsigned char *t,int n,int v)
{int i;
 for (i=0;i<n;i++)
    if (t[i]==v)
       return i;
 return 0;
}

/* the next level from `l` (d = +1 / -1) the mode may be played on */
static int mpNextLevel(int l,int d)
{int i;
 for (i=0;i<CFG_MP_NMLEVELS;i++)
    {l=mpCycle(l,CFG_MP_NMLEVELS,d);
     if (mpMode!=MP_BOSS || CFG_MP_BOSSLEVEL(l))
	return l;
    }
 return l;
}

static void mpMenuLine(int r,int players,int level,char *text)
{int k;
 switch (r)
    {case MR_MODE:    sprintf(text,"MODE  %s",mpModeName[mpMode]); break;
     case MR_SKILL:   sprintf(text,"SKILL  %s",CFG_MP_SKILLNAME(mpSkill)); break;
     case MR_PLAYERS: sprintf(text,"PLAYERS  %d",players); break;
     case MR_MAP:     sprintf(text,"MAP  %s",CFG_MP_LEVELLABEL(level)); break;
     case MR_BOSS:
	k=CFG_MP_BOSSCOUNT(level);
	sprintf(text,(k>1)? "BOSS  %s X%d": "BOSS  %s",CFG_MP_BOSSNAME(level),k);
	break;
     case MR_FRAGS:
	if (mpFragLimit)
	   sprintf(text,"FRAG LIMIT  %d",mpFragLimit);
	else
	   strcpy(text,"FRAG LIMIT  NONE");
	break;
     case MR_TIME:
	if (mpTimeLimit)
	   sprintf(text,"TIME LIMIT  %d MIN",mpTimeLimit);
	else
	   strcpy(text,"TIME LIMIT  NONE");
	break;
     case MR_START:   strcpy(text,"START"); break;
     case MR_BACK:    strcpy(text,"BACK"); break;
     default:
	k=r-MR_P1;
	if (mpMode==MP_TEAM)
	   sprintf(text,"PLAYER %d  %s TEAM",k+1,mpMenuTeam[k]? "RED": "GREEN");
	else if (mpMode==MP_BOSS)
	   sprintf(text,"PLAYER %d  %s",k+1,mpIsBoss(k,players,level)? "BOSS": "MARINE");
	else
	   sprintf(text,"PLAYER %d  %s",k+1,mpMenuRole[k]? CFG_MP_MONSTER_ROLE: "MARINE");
	break;
    }
}

/* multi = 0: NEW GAME, 1: MULTIPLAYER.  1 = start the game set up here (mpMode, mpArmed,
   mpStartLevel, mpSkill...), 0 = back to the title */
static int mpGameMenu(int multi)
{static Fixed32 wave;
 const char *title=multi? "MULTIPLAYER": "NEW GAME";
 int lo=multi? 2: 1,players,level,row,data,last,edge,r,d,y,k,color,shown,pitch;
 char text[40];
 if (!multi)
    mpMode=MP_COOP;
 players=(mpArmed>1)? mpArmed: (multi && mpPadsPresent>1)? mpPadsPresent: lo;
 if (players>MPMAX)
    players=MPMAX;
 level=multi? mpStartLevel: 0;
 row=multi? MR_MAP: MR_SKILL;
 fadeEnd=-150;                  /* the title picture dims behind the lines, as for a submenu */
 fadeDir=-5;
 SCL_SetFrameInterval(0xfffe);
 data=lastInputSample;
 while (1)
    {if (mpMode==MP_BOSS && !CFG_MP_BOSSLEVEL(level))
	level=mpNextLevel(level,1);
     EZ_openCommand();
     EZ_sysClip();
     EZ_localCoord(320/2,240/2);
     drawString(-getStringWidth(2,(unsigned char *)title)/2,-110,2,(unsigned char *)title);
     color=MTH_Sin(wave)>>12;
     wave+=F(8);
     if (wave>F(180))
	wave-=F(360);
     for (r=0,shown=0;r<MR_NM;r++)
	shown+=mpRowShown(r,players,multi);
     pitch=(shown>11)? 16: 18;  /* the boss battle, 4 players: 12 lines */
     for (r=0,y=multi? -84: -40;r<MR_NM;r++)
	{if (!mpRowShown(r,players,multi))
	    continue;
	 mpMenuLine(r,players,level,text);
	 if (r==MR_START)
	    y+=6;
	 if (r==row)
	    drawStringGouro(-getStringWidth(2,(unsigned char *)text)/2,y,2,
			    greyTable[16+color],greyTable[16-color],(unsigned char *)text);
	 else
	    drawString(-getStringWidth(2,(unsigned char *)text)/2,y,2,(unsigned char *)text);
	 y+=pitch;
	}
     SPR_WaitDrawEnd();
     EZ_closeCommand();
     sound_nextFrame();
     SCL_DisplayFrame();

     last=data;
     data=lastInputSample;
     edge=(last^data)&~data;    /* pad bits are active low: this frame's presses */
     d=0;
     if (edge & (PER_DGT_U|PER_DGT_D))
	{int step=(edge & PER_DGT_U)? -1: 1;
	 do
	    row=mpCycle(row,MR_NM,step);
	 while (!mpRowShown(row,players,multi) || row==MR_BOSS);
	 playSound(0,0);
	}
     if (edge & PER_DGT_L)
	d=-1;
     if (edge & PER_DGT_R)
	d=1;
     if ((edge & PER_DGT_B) || ((edge & (PER_DGT_A|PER_DGT_C|PER_DGT_S)) && row==MR_BACK))
	{playSound(0,1);
	 fadeEnd=0;
	 fadeDir=5;
	 return 0;
	}
     if ((edge & (PER_DGT_A|PER_DGT_C|PER_DGT_S)) && row==MR_START)
	break;
     if (edge & (PER_DGT_A|PER_DGT_C))
	d=1;
     if (!d || row==MR_START || row==MR_BACK)
	continue;
     playSound(0,1);
     switch (row)
	{case MR_MODE:    mpMode=mpCycle(mpMode,MP_NMMODES,d); break;
	 case MR_SKILL:   mpSkill=mpCycle(mpSkill,CFG_MP_NMSKILLS,d); break;
	 case MR_PLAYERS: players=lo+mpCycle(players-lo,MPMAX+1-lo,d); break;
	 case MR_MAP:     level=mpNextLevel(level,d); break;
	 case MR_FRAGS:   mpFragLimit=mpFragChoice[mpCycle(mpIndexOf(mpFragChoice,5,mpFragLimit),5,d)]; break;
	 case MR_TIME:    mpTimeLimit=mpTimeChoice[mpCycle(mpIndexOf(mpTimeChoice,5,mpTimeLimit),5,d)]; break;
	 default:
	    k=row-MR_P1;
	    if (mpMode==MP_TEAM)
	       mpMenuTeam[k]^=1;
	    else if (mpMode==MP_BOSS)
	       mpToggleBoss(k,players,level);
	    else
	       mpMenuRole[k]^=1;
	    break;
	}
    }
 playSound(0,1);
 /* the rules of this game: teams and roles only where the mode has them, and a monster game
    keeps at least one normal player -- the monsters need someone to hunt */
 for (k=0;k<MPMAX;k++)
    {mpTeam[k]=(mpMode==MP_TEAM && k<players)? mpMenuTeam[k]: 0;
     mpRole[k]=(mpMode==MP_MONSTERS && k<players)? mpMenuRole[k]:
	       (mpMode==MP_BOSS)? (unsigned char)mpIsBoss(k,players,level): 0;
    }
 for (k=0;k<players && mpRole[k];k++)
    ;
 if (k==players)
    mpRole[0]=0;
 if (!mpCompetitive())
    mpFragLimit=mpTimeLimit=0;
 mpArmed=players;
 mpStartLevel=level;
 fadeEnd=0;
 fadeDir=5;
 return 1;
}

int mpMenu(void)
{return mpGameMenu(1);
}

int mpNewGameMenu(void)
{return mpGameMenu(0);
}

/* A new game from the title's own path (PowerSlave's, a loaded game): one player's rules */
void mpSoloRules(void)
{int k;
 mpMode=MP_COOP;
 mpStartLevel=0;
 mpFragLimit=mpTimeLimit=0;
 for (k=0;k<MPMAX;k++)
    mpTeam[k]=mpRole[k]=0;
}

/* --- the score at the end of a level ------------------------------------------------------
   Inside the level, which still owns the VDP1 and the game's fonts (font 1).  Held until a
   button on any pad, after half a second; tics > 0 also ends it on its own. */

/* A game's font may have no space (Doom's STCFN): drawString would drop it -- 4 pixels, as
   Doom's own HUD writes it */
static int mpSpace(int font)
{int w=getCharWidth(font,' ');
 return w? w+1: 4;
}

static int mpTextWidth(int font,const char *t)
{int w=0;
 for (;*t;t++)
    w+=(*t==' ')? mpSpace(font): getCharWidth(font,(unsigned char)*t)+1;
 return w;
}

static void mpText(int x,int y,int font,const char *t)
{char word[48];
 int n;
 while (*t)
    {if (*t==' ')
	{x+=mpSpace(font);
	 t++;
	 continue;
	}
     for (n=0;t[n] && t[n]!=' ' && n<47;n++)
	word[n]=t[n];
     word[n]=0;
     drawString(x,y,font,(unsigned char *)word);
     x+=mpTextWidth(font,word);
     t+=n;
    }
}

static void mpTextCentred(int y,int font,const char *t)
{mpText(-mpTextWidth(font,t)/2,y,font,t);
}
static void mpPercent(char *out,int n,int total)
{if (total<=0)
    strcpy(out,"-");
 else
    sprintf(out,"%d%%",n*100/total);
}

/* the leader: frags (a team's summed) when players fight; kills, then items, in a co-operative
   game -- which is a race too.  -1 = a tie between teams. */
static int mpLeader(int *score)
{int i,k,side=-1,coop=(mpMode==MP_COOP || mpMode==MP_MONSTERS);
 for (i=0;i<MPMAX+2;i++)
    score[i]=0;
 for (k=0;k<mpPlayers;k++)
    if (coop)
       score[k]=mpStat[k].kills*256+mpStat[k].items;
    else
       score[mpSide(k)]+=mpStat[k].frags;
 for (i=0;i<MPMAX+2;i++)
    if ((mpMode==MP_TEAM)? i>=MPMAX: i<mpPlayers)
       if (side<0 || score[i]>score[side])
	  side=i;
 if (mpMode==MP_TEAM && score[MPMAX]==score[MPMAX+1])
    return -1;
 return side;
}

void mpIntermission(const char *title,int seconds,int tics)
{static const short col[6]={-150,-92,-48,-4,50,98};
 static const char *const head[6]={"","KILLS","ITEMS","SECRET","FRAGS","DEATHS"};
 XyInt quad[4];
 char text[48];
 int t,k,i,y,side,score[MPMAX+2],pad[MPMAX],down;
 for (k=0;k<MPMAX;k++)
    pad[k]=lastInputSampleP[k];
 side=(mpPlayers>1)? mpLeader(score): 0;
 for (t=0;!tics || t<tics;t++)
    {EZ_openCommand();
     EZ_sysClip();
     EZ_localCoord(320/2,224/2);
     quad[0].x=-160; quad[0].y=-112;
     quad[1].x=159;  quad[1].y=-112;
     quad[2].x=159;  quad[2].y=111;
     quad[3].x=-160; quad[3].y=111;
     EZ_polygon(ECDSPD_DISABLE|COLOR_5,RGB(0,0,0),quad,NULL);
     mpTextCentred(-92,1,title);
     sprintf(text,"TIME %d:%02d",seconds/60,seconds%60);
     mpTextCentred(-78,1,text);
     y=-54;
     for (i=1;i<6;i++)
	if (i<4 || mpPlayers>1)
	   mpText(col[i],y,1,head[i]);
     for (k=0;k<mpPlayers;k++)
	{y+=14;
	 if (mpMode==MP_TEAM)
	    sprintf(text,"P%d %s",k+1,mpTeam[k]? "RED": "GRN");
	 else
	    sprintf(text,"P%d",k+1);
	 mpText(col[0],y,1,text);
	 mpPercent(text,mpStat[k].kills,mpTotal[0]);
	 mpText(col[1],y,1,text);
	 mpPercent(text,mpStat[k].items,mpTotal[1]);
	 mpText(col[2],y,1,text);
	 mpPercent(text,mpStat[k].secrets,mpTotal[2]);
	 mpText(col[3],y,1,text);
	 if (mpPlayers>1)
	    {sprintf(text,"%d",mpStat[k].frags);
	     mpText(col[4],y,1,text);
	     sprintf(text,"%d",mpStat[k].deaths);
	     mpText(col[5],y,1,text);
	    }
	}
     if (mpPlayers>1)
	{if (mpMode==MP_TEAM)
	    sprintf(text,"GREEN %d  RED %d  %s",score[MPMAX],score[MPMAX+1],
		    side<0? "DRAW": side==MPMAX? "GREEN WINS": "RED WINS");
	 else
	    sprintf(text,"PLAYER %d %s",side+1,mpCompetitive()? "WINS": "LEADS");
	 mpTextCentred(y+24,1,text);
	}
     if (t>=35 && (t&32))
	mpTextCentred(90,1,"PRESS START");
     EZ_closeCommand();
     SPR_WaitDrawEnd();
     sound_nextFrame();
     SCL_DisplayFrame();
     for (k=0,down=0;k<MPMAX;k++)
	{int now=lastInputSampleP[k];
	 if ((pad[k]^now)&~now&(PER_DGT_A|PER_DGT_C|PER_DGT_S))
	    down=1;
	 pad[k]=now;
	}
     if (down && t>=35)
	break;
    }
}

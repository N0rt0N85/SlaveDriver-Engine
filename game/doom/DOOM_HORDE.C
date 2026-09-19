/* DOOM_HORDE.C -- MP_HORDE (MPLAYER.H mpMode), the survival mode: the level's own monsters never
 * stand up, and waves of them are born instead, more of them and worse of them each time.  One
 * life a player; the run ends when the last marine falls, and the wave reached is the score.
 *
 * Three rules make it fit the machine rather than fight it:
 *
 *  - A wave is drawn from the FAMILIES THE LEVEL SHIPS and no others.  A level carries the sprite
 *    sequences of the things doom2ps placed on it (wad2sprites `present`), so asking for a
 *    cacodemon on E1M1 would ask for frames that are not in RAM.  doom_modePlace tells us what it
 *    saw as it refuses to place it, which is exactly the list we may draw from.  E1M5, E1M6, E1M7
 *    and E1M9 carry all five of the shareware's walkers; E1M1 and E1M2 carry two.
 *  - Nothing is born while the object pool is near its end.  doom_spawn already answers NULL, but
 *    a horde that filled the pool would leave no room for a bullet's puff or a drop, so the birth
 *    stops a margin short (DOOM_HORDE_KEEP).
 *  - The monsters alive are COUNTED, never bookkept.  The level's own are gone, so every walker
 *    standing belongs to the horde: one walk of the object lists a tic, and a monster that dies
 *    some way we never thought of cannot stall the wave.
 *
 * The drops are Doom's own pickups (p_inter.c gives them their effect), rolled on one 8-bit draw
 * and filtered the same way: a level that ships no megaarmour drops the green one instead. */
#include <string.h>
#include <stdio.h>
#include "util.h"
#include "level.h"
#include "sprite.h"
#include "object.h"
#include "ai.h"
#include "sruins.h"
#include "gamestat.h"
#include "sound.h"
#include "doom.h"
#include "mplayer.h"

#define DOOM_HORDE_MAXFAM   8          /* the walkers of the shareware, at most                  */
#define DOOM_HORDE_REST     140        /* 4 s of quiet between two waves                         */
#define DOOM_HORDE_KEEP     40         /* objects left free for the shots, the blood, the drops  */
#define DOOM_HORDE_BASE     5          /* monsters in wave 1, per player, at HURT ME PLENTY      */
#define DOOM_HORDE_GROW     3          /* ... and per wave after it                              */
#define DOOM_HORDE_NEWFAM   2          /* a new family every this many waves                     */
#define DOOM_HORDE_NEAR     512        /* no birth nearer than this to ANY marine                */
#define DOOM_HORDE_REACH    24         /* hops the reach plot walks out from the marines         */
#define DOOM_HORDE_QUEUE    96         /* leaves it may hold at once: the plot's whole cost      */
#define DOOM_HORDE_SPREAD   4          /* hops past the nearest spot that may still be drawn     */
#define DOOM_HORDE_REPLOT   35         /* one second: the marines have moved, plot again         */
/* A monster's own blocking bits (DOOM_ACTOR.C doom_spawn), the plot's rule for a portal.  Note
   WATERBNDRY: since ML_BLOCKMONSTERS borrows it, the plot refuses a line Doom forbids monsters,
   so the horde is never asked to be born behind one and walk through it. */
#define DOOM_HORDE_MFLAGS   (SPRITEFLAG_BBLOCKED|SPRITEFLAG_BWATERBNDRY| 			     SPRITEFLAG_BCLIFF|SPRITEFLAG_BSHORT)

/* Doom's skill sieves the things a map places and softens the damage; a horde places its own
   things, so here the skill is the PRESSURE and nothing else -- how many a wave brings, how fast
   they come, how many stand at once.  Where they come from is the same map on every skill
   (doom_modePlace).  Index 0..3: TOO YOUNG TO DIE, NOT TOO ROUGH, HURT ME PLENTY, ULTRA-VIOLENCE;
   the count is in eighths, so HURT ME PLENTY is the 8/8 the constants above are written for. */
static const unsigned char hordeSkillNum[4]  ={4,6,8,11};   /* eighths of the wave's size        */
static const unsigned char hordeSkillGap[4]  ={24,17,12,8}; /* tics between two births           */
static const unsigned char hordeSkillAlive[4]={5,7,10,14};  /* alive at once, per player         */
#define hordeSkill() ((mpSkill>=0 && mpSkill<4)? mpSkill: 2)

/* The walkers in the order the mode lets them in.  MT_SERGEANT is the pink demon, MT_SHOTGUY the
   shotgun zombie: the wave that adds a family adds the next one down this list. */
static const short hordeOrder[DOOM_HORDE_MAXFAM]=
{MT_POSSESSED,MT_SHOTGUY,MT_TROOP,MT_SERGEANT,MT_SHADOWS,MT_SKULL,MT_HEAD,MT_BRUISER};

/* What a kill leaves behind.  One P_Random draw (0..255) walks down: the first entry whose cut is
   above the draw wins, -1 = nothing.  The rare kits sit at the bottom, and an entry the level
   does not ship falls back to the one above it -- a cheaper kit, never no kit at all. */
typedef struct {unsigned short cut; short mt;} DoomHordeDrop;
static const DoomHordeDrop hordeDropTable[]=
{{ 96,-1},                             /* 37 % : nothing                                         */
 {150,MT_MISC2},                       /* 21 % : health bonus, 1 hp                              */
 {178,MT_MISC3},                       /* 11 % : armour bonus                                    */
 {200,MT_CLIP},                        /*  9 % : a clip                                          */
 {216,MT_MISC10},                      /*  6 % : stimpack                                        */
 {230,MT_MISC22},                      /*  5 % : four shells                                     */
 {242,MT_MISC11},                      /*  5 % : medikit                                         */
 {250,MT_MISC0},                       /*  3 % : green armour                                    */
 {253,MT_MISC17},                      /*  1 % : box of bullets                                  */
 {255,MT_MISC1},                       /*  1 % : megaarmour                                      */
 {256,MT_MISC12}};                     /* 0.4 %: soulsphere                                      */
#define DOOM_HORDE_NMDROPS  ((int)(sizeof(hordeDropTable)/sizeof(hordeDropTable[0])))

static unsigned char hordeHas[(NUMMOBJTYPES+7)>>3];   /* the MTs this level ships, one bit each */
static short hordeFam[DOOM_HORDE_MAXFAM];
static short hordeNmFam;
static short hordeWave;                /* 0 = not started yet                                    */
static short hordeLeft;                /* of this wave, still to be born                         */
static short hordeClock;               /* tics until the next birth                              */
static short hordeRest;                /* tics until the next wave                               */
static char  hordeDone;                /* every marine is down: the run is over                  */
static short hordePlot;                /* tics until the reach is plotted again                  */
/* The reach: hops from the nearest marine ON FOOT, 0 = not reached.  792 bytes of BSS, and the
   queue's bound is what bounds the plot's cost -- it stops expanding rather than run the map. */
static unsigned char hordeHop[MAXNMSECTORS];
static short hordeQ[DOOM_HORDE_QUEUE];

#define hordeShips(mt) (hordeHas[(mt)>>3] & (1<<((mt)&7)))

int doom_hordeWave(void)
{return hordeWave;
}

/* doom_modePlace, as it refuses to place a monster (or accepts a pickup): this level ships it. */
void doom_hordeSaw(int mt)
{if (mt>=0 && mt<NUMMOBJTYPES)
    hordeHas[mt>>3]|=(unsigned char)(1<<(mt&7));
}

/* doom_modesLevelReset, a level's placement begins: forget the previous level's list. */
void doom_hordeLevelReset(void)
{memset(hordeHas,0,sizeof(hordeHas));
}

/* SRUINS.C runLevel, every player built: the level's list of families, and wave 1 on its way. */
void doom_hordeLevelStart(void)
{int i;
 hordeNmFam=0;
 hordeWave=0;
 hordeLeft=0;
 hordeClock=0;
 hordeDone=0;
 hordeRest=DOOM_HORDE_REST;
 hordePlot=0;                           /* the first birth plots the reach */
 memset(hordeHop,0,sizeof(hordeHop));
 if (mpMode!=MP_HORDE)
    return;
 for (i=0;i<DOOM_HORDE_MAXFAM;i++)
    if (hordeShips(hordeOrder[i]))
       hordeFam[hordeNmFam++]=hordeOrder[i];
 doom_setMessage(hordeNmFam? "SURVIVE": "THIS MAP HAS NO MONSTERS");
}

/* The walkers standing.  The level's own were never placed, so this is the horde and nothing
   else -- and a death by any road at all (a crush, a fall, a barrel) is seen here. */
static int hordeAlive(void)
{Object *o;
 DoomActor *a;
 int l,n=0;
 for (l=0;l<2;l++)
    for (o=l?objectIdleList:objectRunList;o;o=o->next)
       {if (o->func!=game_actor_func)
	   continue;
	a=(DoomActor *)o;
	if (a->sprite && (a->mflags & DF_SHOOTABLE) && a->health>0 &&
	    (doomMobjInfo[a->mt].flags & MF_COUNTKILL))
	   n++;
       }
 return n;
}

/* The family of the next birth.  Wave w has the first `1 + (w-1)/NEWFAM` families; from wave 5 on
   the draw is made twice and the harder one kept, so the newcomers take the room over. */
static int hordePick(void)
{int open=1+(hordeWave-1)/DOOM_HORDE_NEWFAM,i,j;
 if (open>hordeNmFam)
    open=hordeNmFam;
 if (open<1)
    open=1;
 i=getNextRand()%open;
 if (hordeWave>=5)
    {j=getNextRand()%open;
     if (j>i)
	i=j;
    }
 return hordeFam[i];
}

/* P_DamageMobj's wake (DOOM_ACTOR.C doom_damageActor): the monster comes into the world already
   hunting one marine, drawn at random, exactly as if that one had shot it -- target, Doom's own
   threshold of 100 tics on it, and the see state.  A_Chase then walks it there whether it can see
   the marine or not; the threshold running out lets it turn on whoever is nearer.  Without this a
   wave born across the map would stand in its spawn room until someone walked into its view. */
static void hordeHunt(DoomActor *a)
{int k,n=0;
 unsigned char pick[MPMAX];
 for (k=0;k<mpPlayers;k++)
    if (mpObj[k] && doom_targetAlive(mpObj[k]) && !doom_isMonsterPlayer(k))
       pick[n++]=(unsigned char)k;
 if (!n)
    return;
 k=pick[getNextRand()%n];
 a->target=mpObj[k];
 a->lastlook=(short)k;                  /* A_Look starts its round on the same one */
 a->threshold=DOOM_BASETHRESHOLD;
 a->reactiontime=0;
 if (doomMobjInfo[a->mt].seestate)
    doom_setState(a,doomMobjInfo[a->mt].seestate);
}

/* MEASURED, and the reason this plot exists at all: from a level's start leaf, the median spawn
   spot is 14 to 22 leaves away (E1M5 14, E1M1 18, E1M6 22), and half of them have no route to
   the marine at all -- a lift parked up, a CLIFFBNDRY drop, a key door.  A_Chase is Doom's greedy
   eight-direction wall follower with no knowledge of the map: over two or three leaves it gets
   there, over twenty it cannot, which is why a wave born across the map only closed in when the
   marine walked to IT.  So the horde is born WHERE A_Chase can finish the walk: one breadth-first
   plot of the leaves reachable on foot from the marines, doors included (doomBlocked presses a
   door it bumps, DOOM_VERBS.C), and the spots are ranked by that distance.
   No radius is picked: a fixed one is a number with nothing behind it -- at 8 hops E1M1 offers 2
   spots and E1M2 four -- so the nearest usable spot wins, whatever its distance. */
static void hordePlotReach(void)
{int head=0,tail=0,k,u,w,n,hop;
 memset(hordeHop,0,sizeof(hordeHop));
 for (k=0;k<mpPlayers;k++)
    if (mpBody[k] && mpBody[k]->s>=0 && mpBody[k]->s<level_nmSectors &&
	!hordeHop[mpBody[k]->s] && tail<DOOM_HORDE_QUEUE)
       {hordeHop[mpBody[k]->s]=1;
	hordeQ[tail++]=(short)mpBody[k]->s;
       }
 while (head<tail)
    {u=hordeQ[head++];
     hop=hordeHop[u];
     if (hop>=DOOM_HORDE_REACH)
	continue;
     for (w=level_sector[u].firstWall;w<=level_sector[u].lastWall;w++)
	{n=level_wall[w].nextSector;
	 if (n==-1)
	    break;                              /* portals come first (WALLS.C findDoorways) */
	 if (level_wall[w].normal[1]!=0)
	    continue;                           /* a floor portal is not a step */
	 if (!(level_wall[w].flags & WALLFLAG_DOORWALL) &&
	     ((level_wall[w].flags & DOOM_HORDE_MFLAGS) & WALLFLAG_BLOCKBITS))
	    continue;
	 if (n<0 || n>=level_nmSectors || hordeHop[n])
	    continue;
	 hordeHop[n]=(unsigned char)(hop+1);
	 if (tail<DOOM_HORDE_QUEUE)
	    hordeQ[tail++]=(short)n;
	}
    }
}

/* Far enough from every marine that nothing pops into a face */
static int hordeClearOfMarines(MthXyz *feet)
{int k;
 for (k=0;k<mpPlayers;k++)
    if (mpBody[k] &&
	doom_approxDist2(feet->x-mpBody[k]->pos.x,feet->z-mpBody[k]->pos.z)<=F(DOOM_HORDE_NEAR))
       return 0;
 return 1;
}

/* The nearest spot on foot, drawn among those within DOOM_HORDE_SPREAD hops of it so the wave
   does not always come through the same door.  0 = the plot reached none of them. */
static int hordeNearSpot(int *sector,MthXyz *feet,int *yaw)
{int i,n=mpSpotCount(),best=0,cnt=0,pick,s2,y2,hop;
 MthXyz f2;
 for (i=0;i<n;i++)
    {if (!mpSpotGet(i,&s2,&f2,&y2) || s2<0 || s2>=level_nmSectors)
	continue;
     hop=hordeHop[s2];
     if (!hop || !hordeClearOfMarines(&f2))
	continue;
     if (!best || hop<best)
	best=hop;
    }
 if (!best)
    return 0;
 for (i=0;i<n;i++)
    {if (!mpSpotGet(i,&s2,&f2,&y2) || s2<0 || s2>=level_nmSectors)
	continue;
     hop=hordeHop[s2];
     if (hop && hop<=best+DOOM_HORDE_SPREAD && hordeClearOfMarines(&f2))
	cnt++;
    }
 if (!cnt)
    return 0;                           /* the pass above found one: never, but no divide by 0 */
 pick=getNextRand()%cnt;
 for (i=0;i<n;i++)
    {if (!mpSpotGet(i,&s2,&f2,&y2) || s2<0 || s2>=level_nmSectors)
	continue;
     hop=hordeHop[s2];
     if (hop && hop<=best+DOOM_HORDE_SPREAD && hordeClearOfMarines(&f2) && !pick--)
	{*sector=s2;
	 *feet=f2;
	 *yaw=y2;
	 return 1;
	}
    }
 return 0;
}

/* One monster at a spot the marines can be walked to, with the teleport fog Doom gives an
   arrival.  0 = no spot, or the pool said no: try again next tic. */
static int hordeBirth(void)
{MthXyz feet,pos;
 DoomActor *a,*f;
 int sector,yaw,mt;
 if (!hordeNearSpot(&sector,&feet,&yaw))
    {/* the marines are sealed in (a lift not yet called, a door not yet opened): the wave still
	comes, from the farthest spot, as before the plot */
     if (!mpSpotFar(-1,&sector,&feet,&yaw))
	return 0;
    }
 mt=hordePick();
 pos=feet;
 pos.y+=F(doomMobjInfo[mt].height/2);   /* a dynamic spawn takes the sphere's centre */
 /* mpSpotAdd stored the player's convention (the thing's angle less 90): give it back */
 a=doom_spawn(mt,sector,&pos,normalizeAngle(yaw+F(90)),0);
 if (!a || a->type==OT_DEAD)
    return 0;
 hordeHunt(a);
 pos=feet;
 pos.y+=F(doomMobjInfo[MT_TFOG].height/2);
 f=doom_spawn(MT_TFOG,sector,&pos,0,0);
 doom_sound((f && f->type!=OT_DEAD)?f->sprite:NULL,sfx_telept);
 return 1;
}

static void hordeNextWave(void)
{char msg[24];
 int n;
 hordeWave++;
 n=(DOOM_HORDE_BASE+DOOM_HORDE_GROW*(hordeWave-1))*mpPlayers*hordeSkillNum[hordeSkill()]/8;
 if (n<1)
    n=1;
 if (n>0x7000)
    n=0x7000;
 hordeLeft=(short)n;
 hordeClock=0;
 hordeRest=DOOM_HORDE_REST;
 sprintf(msg,"WAVE %d",hordeWave);
 doom_setMessage(msg);
}

/* One life a player, and doom_mpRespawnHold never lets one back up: the run is over when no
   marine is left standing.  Read the HEALTH rather than the score -- mpStat counts the deaths
   doom_playerKilled saw, and a run must end on a death by any road at all.  mpPeekInt answers
   for the loaded player too (MPLAYER.C:128). */
static int hordeAllDown(void)
{int k;
 for (k=0;k<mpPlayers;k++)
    if (mpPeekInt(k,&currentState.health)>0)
       return 0;
 return 1;
}

/* doom_modesTic, 35 Hz, once for the game. */
void doom_hordeTic(void)
{int alive,cap;
 if (mpMode!=MP_HORDE || !hordeNmFam || doom_exiting())
    return;
 if (hordeDone)
    return;
 if (hordeAllDown())
    {char msg[28];
     hordeDone=1;
     sprintf(msg,"SURVIVED %d WAVE%s",hordeWave,(hordeWave==1)?"":"S");
     doom_setMessage(msg);
     doom_endRound();
     return;
    }
 /* hordeAlive walks the object lists, so it is asked only when its answer is about to be used:
    at a birth (one tic in hordeSkillGap) and, between waves, one tic in four. */
 if (hordeLeft>0)
    {if (hordeClock>0)
	{hordeClock--;
	 return;
	}
     hordeClock=hordeSkillGap[hordeSkill()];
     if (hordePlot>0)
	hordePlot-=hordeSkillGap[hordeSkill()];
     if (hordePlot<=0)
	{hordePlotReach();                      /* the marines have moved: plot again, once a second */
	 hordePlot=DOOM_HORDE_REPLOT;
	}
     cap=hordeSkillAlive[hordeSkill()]*mpPlayers;
     alive=hordeAlive();
     if (alive<cap && objectsFree()>DOOM_HORDE_KEEP && spritesFree()>DOOM_HORDE_KEEP)
	{if (hordeBirth())
	    hordeLeft--;
	}
     return;
    }
 /* the wave is all born: poll one tic in four for the last of them to fall, and only then let
    the quiet of DOOM_HORDE_REST run out. */
 if (--hordeClock>0)
    return;
 hordeClock=4;
 if (hordeAlive())
    {hordeRest=DOOM_HORDE_REST;
     return;
    }
 hordeRest-=4;
 if (hordeRest<=0)
    hordeNextWave();
}

/* doomKill (DOOM_ACTOR.C), after Doom's own drop: what this one leaves for the survivors. */
void doom_hordeKilled(DoomActor *this)
{MthXyz pos;
 int r,i,mt=-1;
 if (mpMode!=MP_HORDE || !this || !this->sprite)
    return;
 if (!(doomMobjInfo[this->mt].flags & MF_COUNTKILL))
    return;                                     /* a barrel leaves nothing */
 r=P_Random();
 for (i=0;i<DOOM_HORDE_NMDROPS;i++)
    if (r<(int)hordeDropTable[i].cut)
       {mt=hordeDropTable[i].mt;
	break;
       }
 while (mt>=0 && !hordeShips(mt))                /* not on this level: the kit just below it */
    mt=(--i>=0)?hordeDropTable[i].mt:-1;
 if (mt<0)
    return;
 if (objectsFree()<=DOOM_HORDE_KEEP/2 || spritesFree()<=DOOM_HORDE_KEEP/2)
    return;
 pos=this->sprite->pos;
 pos.y=(pos.y-findFloorDistance(this->sprite->s,&pos))+F(doomMobjInfo[mt].height/2);
 doom_spawn(mt,this->sprite->s,&pos,0,0);
}

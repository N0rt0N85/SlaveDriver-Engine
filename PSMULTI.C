/* PSMULTI.C -- PowerSlave's side of local multiplayer (GCC14): the maps the multiplayer screen
 * offers, the body the other players wear, their colours, the compact HUD of a split view, the
 * spawn spots of a fighting game, the frags and the end-of-level score.
 *
 * The engine owns the service: MPLAYER.C swaps the per-player globals, MPRULES.C keeps the
 * rules, the sides, the spots, the menu and the score table.  game/doom/DOOM_MODES.C answers
 * the same CFG_MP_* hooks on the Doom runtime; this file answers them on PowerSlave's, and
 * nothing here is Doom's business or the engine's.
 *
 * PowerSlave never shipped a multiplayer game, so three things it simply does not have had to
 * be found in what it does have:
 *   - a body.  Lt. Curtis has no sprite of his own in any .LEV (the player's sequence list is
 *     `playerSeqList = {-1}`, AI.C:45), so the other players wear one of the level's OWN
 *     monsters -- the nearest thing to a skin the data holds.  psSkins[] below.
 *   - a colour.  Doom reads its green marine ramp as another of PLAYPAL's; PowerSlave's object
 *     palette is a different one in every level, so there is no ramp to remap.  The players
 *     take a colour FILTER on a CRAM bank instead (PIC.C buildTintedBank).
 *   - spawn spots.  No .LEV carries a deathmatch start, so a fighting game is born where the
 *     level's monsters stood, exactly as the Doom side does it.
 */
#include <string.h>
#include <stdio.h>
#include "util.h"
#include "slevel.h"
#include "level.h"
#include "sprite.h"
#include "object.h"
#include "ai.h"
#include "aicommon.h"
#include "sruins.h"
#include "gamestat.h"
#include "weapon.h"
#include "sequence.h"
#include "sound.h"
#include "print.h"
#include "spr.h"
#include "pic.h"
#include "bigmap.h"
#include "mplayer.h"
#include "psmulti.h"
#include "file.h"
#include "walls.h"
#include <sega_scl.h>

/* --- the maps ------------------------------------------------------------------------------
   BIGMAP.C's levelGraph has 31 entries, but the disc holds 24 .LEV files: QUARRY and the six
   KILMAAT rooms were never built, and TEST is the development map.  The multiplayer screen
   cycles through THIS list, so it can never name a file that is not there. */
static const unsigned char psMpLevel[PS_NMLEVELS]=
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,15,16,17,18,19,20,21,29,30};
/* The file's own stem, in bigFont's letters.  No spaces: a name is one word, so a font without
   a space (the menu draws with drawString, which would drop it) can never bite. */
static const char *const psMpName[PS_NMLEVELS]=
{"KARNAK","SANCTUARY","PASS","TOMB","SHRINE","MINES","SETPALACE","SETARENA","CAVERN","THOTH",
 "CHAOS","COLONY","SELPATH","KILENTRY","SELBUROW","MAGMA","PEAK","MARSH","SUNKEN","SLAVCAMP",
 "GORGE","KILARENA","TOMBEND"};

/* the menu's count and the table's are the same number, in two files (SPRITE.H) */
typedef char psLevelCountCheck[(PS_NMLEVELS==CFG_MP_NMLEVELS)? 1: -1];

int ps_mpLevel(int i)
{return (i>=0 && i<PS_NMLEVELS)? psMpLevel[i]: 0;
}

const char *ps_levelLabel(int i)
{return (i>=0 && i<PS_NMLEVELS)? psMpName[i]: "";
}

/* SRUINS.C, leaving the title.  A multiplayer game goes straight to the map the menu chose;
   PowerSlave's own game keeps its map screen, and the Ramses teleport that short-cuts it. */
int ps_startLevel(void)
{if (mpArmed>1 || mpCompetitive())
    return ps_mpLevel(mpStartLevel);
 if ((currentState.gameFlags & GAMEFLAG_JUSTTELEPORTED) &&
     !(currentState.inventory & INV_MUMMY))
    return 3;
 return runMap(currentState.currentLevel);
}

/* --- the fog's colour -----------------------------------------------------------------------
   PowerSlave is an Egyptian game: sandstone, torches, ochre skies.  Its haze has no business
   being neutral grey, and the ramp can carry a hue for nothing (UTIL.C setFogTint bends the
   middle of the light ramp and leaves both ends alone, so the light LOD still sees a fully
   fogged cell as black and still folds it).
   Which hue is not a taste: it is the level.  Counted over the 24 retail .LEV (tools/lev.py,
   sSectorType flags & SECFLAG_WATER, 2026-09-20): SUNKEN is 436 water sectors out of 570 --
   76 % -- and the next wettest map is KARNAK at 17 %.  One map is under water and twenty-three
   are in the desert, so the rule needs no threshold worth arguing about: a level that is mostly
   water fades to AZURE, every other one to SAND. */
/* 0..15 a channel, in gouraud units: what a surface reaches once the fog has it whole -- and
   what the fusion paints the cells it has taken.  Raise them and the distance gets milkier and
   hides less; (0,0,0) is the black fog the engine shipped with. */
#define PS_FOG_SAND    9,7,3
#define PS_FOG_AZURE   3,6,10

static void ps_levelFogColour(void)
{int s,water=0;
 for (s=0;s<level_nmSectors;s++)
    if (level_sector[s].flags & SECFLAG_WATER)
       water++;
 if (water*3>level_nmSectors)
    setFogColour(PS_FOG_AZURE);
 else
    setFogColour(PS_FOG_SAND);
}

/* --- the body the others wear ---------------------------------------------------------------
   A monster of the level itself, whichever of these it holds: `idle` and `walk` are the entries
   of that monster's own sequence map (AI.C anubisSeqMap and friends), directional, so the eight
   rotations come out of the .LEV exactly as the monster's do.  The order is how much the thing
   reads as a man on two legs; a level with none of them shows no body at all, which is what
   PowerSlave did before.  The sequence map is the level's, so the choice is made once a level
   (psSkin), not once a frame. */
typedef struct {short type; unsigned char idle,walk;} PsSkin;
static const PsSkin psSkins[]=
{{OT_ANUBIS, 0,8},              /* the Anubis warrior: a man with a jackal's head */
 {OT_MUMMY,  8,0},
 {OT_SELKIS, 0,8},
 {OT_SET,    0,0},
 {OT_BASTET, 0,0},              /* its idle frame is not directional: walk answers for both */
 {OT_SENTRY, 0,0},
 {OT_SPIDER, 0,0},
 {OT_WASP,   0,0},
 {OT_FISH,   8,0},
 {OT_HAWK,   8,8}};
#define PS_NMSKINS ((int)(sizeof(psSkins)/sizeof(psSkins[0])))

static const PsSkin *psSkin;            /* this level's, NULL = none of them is in it */

/* the roles (the table and the code are at the end of the file) */
short psRoleMt[MPMAX];          /* what each player wears, 0 = Lt. Curtis.  Read ACROSS players
				   (bodies, targeting), so not a per-player global */
static short psRoleCool[MPMAX]; /* tics left before it may attack again */
static short psRoleMax[MPMAX];  /* its full health, for the HUD's share */
static short ps_roleBodySeq(int k,Sprite *body,Sprite *viewer);
static int   ps_takeOver(int k,int bossOnly);
static struct __monster *ps_pickMonster(int bossOnly,Sprite *near);
static void  ps_become(int k,int mt);
int          ps_levelBossMt(int l);
/* The body's own clock.  A monster walks because its AI function calls spriteAdvanceFrame once
   a tic (SPRITE.C:801); a player's sprite has no AI function, so nothing would ever move its
   frame on and every other player would stand frozen in its first pose.  ps_playerTic counts
   the tics, one for one with a monster's, and the frame is read off that count -- set, not
   advanced, so the same body can be drawn in four views of the same image without running four
   times as fast, and so the animation's own sounds (a frame may carry one) stay the monsters'. */
static short psBodyTic[MPMAX];

static void ps_pickSkin(void)
{int i;
 psSkin=NULL;
 for (i=0;i<PS_NMSKINS;i++)
    if (level_sequenceMap[psSkins[i].type]>=0)
       {psSkin=&psSkins[i];
	return;
       }
}

/* --- the body: Lt. Curtis himself --------------------------------------------------------------
   tools/psplayer builds it (planche.py cuts the figures out of generated boards, mkdat.py makes
   PSPLAYER.DAT of them): 64x64 RLE tiles of the sprites' class, frames, chunks and 41 sequences --
   idle, run, aim, fire, pain, eight views each, then the death.  Loaded at the start of a level
   played by several, AFTER the split screen's traversal sets (mpLevelBuild), from what they
   leave: the file holds the body at half resolution then at full (24.9 KB, 81.5 KB), and the
   best that fits is kept -- the half one is drawn at twice the scale, the same size on screen.
   Neither fits: the level's monster, as before (psSkins).
   The sequences are numbered on from the level's (SEQUENCE.H extra_*), so a body carries one in
   its sprite like any monster.  Its palette is its own, brought onto the level's OBJECT palette
   once, nearest colour: that palette changes with every level, and it is what the players'
   tinted banks are built from (PIC.C buildTintedBank). */
#define PSB_REPOS   0           /* the sequences, eight views each (mkdat.py ANIMS) */
#define PSB_COURSE  1
#define PSB_VISEE   2
#define PSB_TIR     3
#define PSB_DOULEUR 4
#define PSB_MORT    40          /* the death: one sequence, every view */
#define PSB_TICS    3           /* tics a pose is held (planche.py TICS_PAR_POSE) */
#define PSB_SPARE   (24*1024)   /* left past the body: PowerSlave's own allocations in a level
				   (Ramses' voice, AI2.C; the map, BIGMAP.C) */
static int psBodyOn;            /* this level carries the body */
static Fixed32 psBodyScale;     /* its sprite scale: 48000 full, twice that half */
static unsigned short psFireTic[MPMAX],psPainTic[MPMAX];   /* on psBodyTic's clock */
static short psDeadTic[MPMAX];

static void ps_loadBody(void)
{int hdr[3],var[2][2],room,left,need,pick,fd,i,n,first;
 int *cnt;
 unsigned char *buf,*p,remap[256];
 unsigned short bank0[256];
 psBodyOn=0;
 if (!(mpArmed>1 || mpPadsPresent>1) || !fs_exists("+PSPLAYER.DAT"))
    return;
 fd=fs_open("+PSPLAYER.DAT");
 fs_read(fd,(char *)hdr,sizeof(hdr));
 if (hdr[0]!=0x5053504c || hdr[1]!=1 || hdr[2]!=2)   /* "PSPL", version 1, two variants */
    {fs_close(fd);
     return;
    }
 fs_read(fd,(char *)var,sizeof(var));  /* {scale, bytes}: the half one, then the full one */
 room=mem_coreleft(0)>mem_coreleft(1)? mem_coreleft(0): mem_coreleft(1);
 left=mem_coreleft(0)+mem_coreleft(1);
 need=wallsSplitNeed()+PSB_SPARE;       /* the views' sets come first */
 for (pick=1;pick>=0;pick--)
    if (var[pick][1]<room && left-var[pick][1]>=need)
       break;
 if (pick<0 || !(buf=(unsigned char *)mem_nocheck_malloc(0,var[pick][1])))
    {fs_close(fd);
     return;
    }
 fs_read(fd,(char *)buf,var[0][1]);     /* the file only reads forward: the half one first, */
 if (pick)
    fs_read(fd,(char *)buf,var[1][1]);  /* ... and the full one over it */
 fs_close(fd);
 cnt=(int *)(buf+512);                  /* tiles, frames, chunks, sequences (records) */
 if (picRoom()<cnt[0])
    {mem_free(buf);                     /* the last allocation: it gives the room back */
     return;
    }
 /* its palette onto the level's object palette (bank 0), nearest colour, index 0 = clear --
    bank 0 copied out of CRAM first: 65 000 comparisons read it, and CRAM is on the B-bus */
 for (i=0;i<256;i++)
    bank0[i]=((const unsigned short *)SCL_COLRAM_ADDR)[i];
 remap[0]=0;
 for (i=1;i<256;i++)
    {unsigned short c=((unsigned short *)buf)[i];
     int r=c & 0x1f,g=(c>>5) & 0x1f,b=(c>>10) & 0x1f,j,d,best=1<<30;
     remap[i]=1;
     for (j=1;j<256;j++)
	{unsigned short o=bank0[j];
	 int dr=(o & 0x1f)-r,dg=((o>>5) & 0x1f)-g,db=((o>>10) & 0x1f)-b;
	 d=dr*dr+dg*dg+db*db;
	 if (d<best)
	    {best=d;
	     remap[i]=(unsigned char)j;
	    }
	}
    }
 p=buf+512+16;
 first=-1;
 for (i=0;i<cnt[0];i++)
    {unsigned char *rle=p+4;
     int size=*(unsigned short *)p,pos=0,pix=0,k;
     while (pix<64*64)                  /* PIC.C unRle's walk: <clear run><opaque run><indexes> */
	{pix+=rle[pos++];
	 n=rle[pos++];
	 for (k=0;k<n;k++)
	    rle[pos+k]=remap[rle[pos+k]];
	 pos+=n;
	 pix+=n;
	}
     n=picAddSpriteRle(rle);
     if (first<0)
	first=n;
     p+=4+((size+3) & ~3);
    }
 extra_frame=(sFrameType *)p;
 p+=cnt[1]*sizeof(sFrameType);
 extra_chunk=(sChunkType *)p;
 for (i=0;i<cnt[2];i++)
    extra_chunk[i].tile+=first;
 p+=cnt[2]*sizeof(sChunkType);
 extra_sequence=(short *)p;
 extra_nmSequences=cnt[3]-1;
 psBodyScale=var[pick][0];
 psBodyOn=1;
}

/* the body's frame as `viewer` sees it: dead, hurt, firing, running, standing -- in that order */
static short ps_bodySeq(int k,Sprite *body,Sprite *viewer,int health)
{int view,saved,seq,f,n;
 unsigned short t=(unsigned short)((k>=0)? psBodyTic[k]: 0);
 body->scale=psBodyScale;
 body->flags&=~SPRITEFLAG_NOSHADOW;     /* a man casts a shadow; a borrowed monster did not */
 if (health<=0)
    {seq=PSB_MORT;
     f=((k>=0)? psDeadTic[k]: 0)/PSB_TICS;
    }
 else
    {saved=body->angle;
     body->angle=normalizeAngle(body->angle+F(90));
     view=getFacingAngle(body,viewer);
     body->angle=saved;
     if (k>=0 && (unsigned short)(t-psPainTic[k])<2*PSB_TICS)
	{seq=PSB_DOULEUR; f=0;}
     else if (k>=0 && (unsigned short)(t-psFireTic[k])<3*PSB_TICS)
	{seq=PSB_TIR;                   /* the flash first, then the aim held */
	 f=((unsigned short)(t-psFireTic[k])<PSB_TICS)? 1: 0;
	}
     else if (body->vel.x || body->vel.z)
	{seq=PSB_COURSE; f=t/PSB_TICS;}
     else
	{seq=PSB_REPOS; f=0;}
     seq=seq*8+view;
    }
 n=extra_sequence[seq+1]-extra_sequence[seq];
 if (n<1)
    return -1;
 body->frame=(seq==PSB_MORT)? ((f<n)? f: n-1): f%n;
 return (short)(level_nmSequences+seq);
}

/* SRUINS.C mpShowBodies, through CFG_MP_BODYSEQ: the frame of player `body` as `viewer` sees
   it.  -1 = nothing drawn -- a dead player (PowerSlave's monsters burst into guts, they leave
   no corpse to borrow) or a level with no skin in it.  The camera's sprite carries the yaw,
   ninety degrees off the sprite convention (AI.C constructPlayer), so it is put back first. */
short ps_playerBodySeq(Sprite *body,Sprite *viewer,int health)
{int base,view,saved,seq,nmFrames,k;
 k=mpIndexOfSprite(body);
 if (health<=0)                         /* Lt. Curtis falls; a worn monster bursts, as its own do */
    return (psBodyOn && !(k>=0 && psRoleMt[k]))? ps_bodySeq(k,body,viewer,health): -1;
 if (k>=0 && psRoleMt[k])
    {body->flags|=SPRITEFLAG_NOSHADOW;
     body->scale=48000;                 /* the monster's own scale (SPRITE.C newSprite) */
     return ps_roleBodySeq(k,body,viewer);      /* it wears a monster, not the level's skin */
    }
 if (psBodyOn)
    return ps_bodySeq(k,body,viewer,health);
 if (!psSkin)
    return -1;
 body->flags|=SPRITEFLAG_NOSHADOW;
 base=(body->vel.x || body->vel.z)? psSkin->walk: psSkin->idle;
 saved=body->angle;
 body->angle=normalizeAngle(body->angle+F(90));
 view=getFacingAngle(body,viewer);
 body->angle=saved;
 seq=level_sequenceMap[psSkin->type]+base+view;
 if (seq<0 || seq>=level_nmSequences)
    return -1;
 nmFrames=level_sequence[seq+1]-level_sequence[seq];
 if (nmFrames<1)
    return -1;                          /* an empty sequence draws the next one's frames */
 body->frame=(k>=0? psBodyTic[k]: 0)%nmFrames;
 return (short)seq;
}

/* --- the colours -----------------------------------------------------------------------------
   MPLAYER.C mpSetBanks, through CFG_MP_TINT: 0 = the player keeps bank 0 and its fog banks,
   otherwise a colour filter, 0..31 a channel, 31 = keep.  Player 1 wears its own colours in
   every mode but team play, where it wears its team's.  Kept dark enough on two channels that a
   body reads at a glance in a 160-pixel view and light enough that the sprite's own shading
   survives the filter. */
int ps_playerTint(int k)
{static const unsigned short tint[MPMAX]=
    {0,RGB(12,18,31),RGB(12,31,14),RGB(31,22,8)};       /* natural, azure, green, amber */
 if (k<0 || k>=MPMAX)
    return 0;
 if (mpMode==MP_TEAM)
    return mpTeam[k]? RGB(31,10,10): 0;                 /* the red team and the natural one */
 return tint[k];
}

/* --- the split view's HUD ---------------------------------------------------------------------
   PowerSlave's status bar is a 320-wide character; it cannot be cut in four.  A split view gets
   its numbers written over its own bottom corner instead, in the game's own font: health out of
   the bowls the player carries, the ammunition of the weapon in hand, and the frags where the
   mode counts them.  drawString works in screen coordinates once the local origin is at 0,0. */
void ps_drawSplitHud(int view,int nmViews)
{XyInt clip[2];
 char text[24];
 int x0,y0,w,h,mx,w2;
 if (nmViews==2)
    {x0=160*view; y0=0; w=160; h=192;}
 else
    {x0=160*(view&1); y0=112*(view>>1); w=160; h=96;}

 EZ_localCoord(0,0);
 clip[0].x=x0;       clip[0].y=y0;
 clip[1].x=x0+w-1;   clip[1].y=y0+h-1;
 EZ_userClip(clip);

 mx=psRoleMt[mpCur]? psRoleMax[mpCur]: currentState.nmBowls*200;
 if (mx<200)
    mx=200;
 sprintf(text,"%d/%d",(currentState.health>0)? currentState.health: 0,mx);
 drawString(x0+3,y0+h-10,1,(unsigned char *)text);

 if (currentState.desiredWeapon>WP_SWORD &&
     currentState.desiredWeapon<WP_NMWEAPONS)
    {sprintf(text,"%d",currentState.weaponAmmo[(int)currentState.desiredWeapon]);
     w2=getStringWidth(1,(unsigned char *)text);
     drawString(x0+w-3-w2,y0+h-10,1,(unsigned char *)text);
    }

 if (mpCompetitive())
    {sprintf(text,"%d",mpStat[mpCur].frags);
     w2=getStringWidth(1,(unsigned char *)text);
     drawString(x0+w-3-w2,y0+2,1,(unsigned char *)text);
    }
}

/* --- the player's kit ------------------------------------------------------------------------- */

static short psLastHurtBy[MPMAX];       /* who hit this player last: the frag goes to them */
static char  psScored[MPMAX];           /* its death has been counted */
static int   psLevelTics;               /* 30 Hz (CFG_TIC_UNIT 2 over 60 vblanks) */
static char  psEnding;

void ps_mpRegister(void)
{/* PowerSlave's player IS the engine's: health, inventory, ammunition and the weapon in hand
    are all in currentState, which mpRegisterEngine already registers field by field.  Nothing
    of PowerSlave's own is per player -- the bowls, the dolls and the level flags belong to the
    saved game, which is one game however many play it. */
}

/* SRUINS.C mpBuild and mpRespawn (CFG_MP_NEWPLAYER), the loaded player's slot holding player
   1's copies: what a player starts a life with.  A fighting game hands out the arsenal -- there
   is nothing to explore and nobody would enjoy hunting with the sword -- and co-operation keeps
   PowerSlave's own kit, so the game is still the game. */
void ps_mpNewPlayer(void)
{int i,mx=currentState.nmBowls*200;
 if (mx<200)
    mx=200;
 currentState.health=mx;
 if (mpCompetitive())
    {currentState.inventory|=INV_SWORD|INV_PISTOL|INV_M60|INV_GRENADE|
			     INV_FLAMER|INV_COBRA|INV_RING;
     for (i=0;i<WP_NMWEAPONS;i++)
	currentState.weaponAmmo[i]=weaponMaxAmmo[i];
     currentState.desiredWeapon=WP_M60;
    }
 else
    {currentState.inventory|=INV_SWORD|INV_PISTOL;
     currentState.desiredWeapon=WP_PISTOL;
    }
 psLastHurtBy[mpCur]=-1;
 psScored[mpCur]=0;
}

/* --- the level ---------------------------------------------------------------------------------
   SRUINS.C runLevel (CFG_LEVEL_PLACED): the level's own objects are placed and the palettes are
   loaded, and no player but the level's own start has been built yet.  That order matters -- the
   spots have to exist BEFORE mpLevelBuild places players 2 to 4 on them, or a deathmatch would
   begin with everybody standing on the level's one start. */
void ps_levelPlaced(void)
{Object *o,*next;
 int list,k;

 ps_levelFogColour();           /* before mpLevelBuild's mpSetBanks, which bakes the colour in */
 ps_pickSkin();
 psBodyOn=0;                    /* ps_loadBody, once every player is built */
 psLevelTics=0;
 psEnding=0;
 for (k=0;k<MPMAX;k++)
    {psLastHurtBy[k]=-1;
     psScored[k]=0;
     psBodyTic[k]=0;
     psFireTic[k]=psPainTic[k]=(unsigned short)-1000;   /* long ago */
     psDeadTic[k]=0;
     psRoleMt[k]=0;
     psRoleCool[k]=0;
     psRoleMax[k]=0;
    }
 if (mpPlayers<2 && !mpCompetitive())
    return;

 /* Where the level's monsters stand is where the players are born and born again.  A fighting
    game then takes the monsters out: they would otherwise own the map, and every spot they
    hold is a spot a player is about to appear on.  Co-operation keeps them, of course -- they
    are the game -- and only counts them, for the score at the end. */
 for (list=0;list<2;list++)
    for (o=(list? objectIdleList: objectRunList)->next;o;o=next)
       {next=o->next;
	if (o->class!=CLASS_MONSTER || o->type==OT_PLAYER || mpIndexOfObject(o)>=0)
	   continue;
	/* the boss battle keeps the boss standing: a boss player is about to WEAR it
	   (ps_mpLevelStart), which cannot happen if the sweep has taken it away first */
	if (mpMode==MP_BOSS && o->type==ps_levelBossMt(mpStartLevel))
	   continue;
	{Sprite *s=((SpriteObject *)o)->sprite;
	 MthXyz feet;
	 if (!s)
	    continue;
	 if (!mpCompetitive())
	    {mpTotal[0]++;              /* co-operation: they are the game, and the score counts them */
	     continue;
	    }
	 /* A spot is the FEET and a player's yaw (MPLAYER.H): a sprite sits SPR_FOOT above its
	    floor and +y is up (UTIL.C findFloorDistance), and the sprite convention leads the
	    camera's yaw by ninety degrees (AI.C constructPlayer). */
	 feet=s->pos;
	 feet.y-=SPR_FOOT(s);
	 mpSpotAdd(s->s,&feet,s->angle-F(90));
	 delayKill(o);
	}
       }
 processDelayedMoves();
}

/* SRUINS.C runLevel (CFG_MP_LEVELSTART), every player built: the roles take their monster.  It
   runs here and not in ps_levelPlaced because a role needs a BODY to move onto the monster. */
void ps_mpLevelStart(void)
{int k,prev=mpCur;
 ps_loadBody();                         /* the traversal sets are in: the body takes what is left */
 if (mpMode!=MP_MONSTERS && mpMode!=MP_BOSS)
    return;
 for (k=0;k<mpPlayers;k++)
    {if (!mpRole[k] || !mpBody[k])
	continue;
     mpSwitch(k);
     if (!ps_takeOver(k,mpMode==MP_BOSS))
	{mpRole[k]=0;                   /* nothing left to wear: it stays itself */
	 changeMessage("NO MONSTER TO TAKE OVER");
	}
    }
 mpSwitch(prev);
}

/* SRUINS.C mpRespawn (CFG_MP_RESPAWNED), player k loaded and its kit handed out */
void ps_mpRespawned(int k)
{psLastHurtBy[k]=-1;
 psScored[k]=0;
 ps_become(k,0);
 if ((mpMode==MP_MONSTERS || mpMode==MP_BOSS) && mpRole[k] &&
     !ps_takeOver(k,mpMode==MP_BOSS))
    {currentState.health=0;             /* no monster left: it stays down, fire asks again */
     changeMessage("NO MONSTER TO TAKE OVER");
    }
}

/* SRUINS.C mpRespawn (CFG_MP_RESPAWN_HOLD), BEFORE the player is moved: 1 = leave it where it
   fell.  A monster player with nothing left to wear would otherwise stand its body on a spawn
   spot with no monster in it. */
int ps_mpRespawnHold(int k)
{if (mpMode!=MP_MONSTERS && mpMode!=MP_BOSS)
    return 0;
 if (!mpRole[k] || ps_pickMonster(mpMode==MP_BOSS,NULL))
    return 0;
 changeMessage("NO MONSTER TO TAKE OVER");
 return 1;
}

/* The bosses: the map's own, one per map.  A boss battle is played on a map that holds one, and
   the mode's menu only offers those (MPRULES.C mpNextLevel). */
int ps_levelBossMt(int l)
{switch (ps_mpLevel(l))
    {case 10: case 16: return OT_MAGMANTIS;    /* CHAOS, MAGMA */
     case 15: return OT_SELKIS;                /* SELBUROW */
     case  7: return OT_SET;                   /* SETARENA */
    }
 return 0;
}

int ps_bossLevel(int l)
{return ps_levelBossMt(l)!=0;
}

int ps_bossCount(int l)
{return ps_bossLevel(l)? 1: 0;
}

const char *ps_bossName(int l)
{switch (ps_levelBossMt(l))
    {case OT_MAGMANTIS: return "MAGMANTIS";
     case OT_SELKIS:    return "SELKIS";
     case OT_SET:       return "SET";
    }
 return "";
}

/* SRUINS.C mpPollStart (CFG_MP_JOINED): a player came in mid-level */
void ps_mpJoined(int k)
{psLastHurtBy[k]=-1;
 psScored[k]=0;
}

/* AI.C player_func (CFG_PLAYER_HURT), the victim already loaded by the engine.  `source` is the
   object that did it: a player's own weapon carries them as its owner, so a frag can be put on
   the right name -- and a shot that would hurt somebody on the same side does not land. */
void ps_playerHurt(int hpLost,Object *source)
{int from=-1;
 if (source)
    {from=mpIndexOfObject(source);
     if (from<0 && source->class==CLASS_PROJECTILE &&
	 ((ProjectileObject *)source)->owner)
	from=mpIndexOfObject(((ProjectileObject *)source)->owner);
    }
 if (from>=0 && from!=mpCur && mpSpares(from,mpCur))
    return;                             /* the same side: no damage between them */
 if (from>=0)
    psLastHurtBy[mpCur]=(short)from;
 else if (source)
    psLastHurtBy[mpCur]=-2;             /* a monster */
 psPainTic[mpCur]=(unsigned short)psBodyTic[mpCur];   /* the body flinches (ps_bodySeq) */
 playerHurt(hpLost);
}

/* The round is over: back to the map it began on, through the engine's own teleport exit, so
   runLevel leaves the way it always does (SRUINS.C, action 200..399). */
static void ps_endRound(void)
{if (psEnding)
    return;
 psEnding=1;
 playerHitTeleport(currentState.currentLevel);
}

static int ps_bestFrags(void)
{int k,best=-32768;
 for (k=0;k<mpPlayers;k++)
    if (mpStat[k].frags>best)
       best=mpStat[k].frags;
 return best;
}

/* SRUINS.C's tic loop (CFG_PLAYER_TIC), player mpCur loaded, 30 Hz.  A death is counted here
   rather than where the damage lands: drowning, a fall and a crushing floor all go straight to
   playerHurt, and this sees every one of them. */
void ps_playerTic(void)
{if (mpPlayers<2 && !mpCompetitive())
    return;
 psBodyTic[mpCur]++;                    /* the body's animation, one frame a tic like a monster's */
 if (currentState.health>0)
    psDeadTic[mpCur]=0;
 else if (psDeadTic[mpCur]<32767)
    psDeadTic[mpCur]++;                 /* how far into its fall (ps_bodySeq) */
 if (psRoleCool[mpCur]>0)
    psRoleCool[mpCur]--;
 if (currentState.health<=0 && !psScored[mpCur])
    {psScored[mpCur]=1;
     mpScoreDeath(mpCur,psLastHurtBy[mpCur]);
    }
 if (mpCur)
    return;                             /* the rest is the round's, counted once */
 psLevelTics++;
 if (mpCompetitive() && !psEnding &&
     ((mpTimeLimit && psLevelTics>=mpTimeLimit*60*30) ||
      (mpFragLimit && ps_bestFrags()>=mpFragLimit)))
    ps_endRound();
}

/* SRUINS.C runLevel (CFG_LEVEL_END), the level still loaded, over a black screen */
void ps_levelEnd(int action)
{char title[32];
 int i;
 if (mpPlayers<2)
    return;
 strcpy(title,"LEVEL OVER");
 for (i=0;i<PS_NMLEVELS;i++)
    if (psMpLevel[i]==currentState.currentLevel)
       {sprintf(title,"%s OVER",psMpName[i]);
	break;
       }
 mpIntermission(title,psLevelTics/30,0);
}

/* --- the roles: a player wearing a monster ------------------------------------------------------
 * MP_MONSTERS and MP_BOSS on the PowerSlave runtime.  The form is Doom's (game/doom/DOOM_MODES.C):
 * a role is NOT a possessed object.  The player stays the engine's camera -- its movement,
 * collision, input and per-player swap untouched -- and WEARS the monster: its frames, its health
 * and its attack on the fire button.  Taking one over moves the camera to where it stood and
 * removes it.
 *
 * What Doom reads out of info.c, PowerSlave has nowhere: every monster's health sits in its own
 * constructor and every attack is written by hand inside its AI function.  So here is the table
 * Doom has, filled from those two places, one line per monster a player may wear.  Every number
 * is the monster's own, cited by the line of AI.C it was read from -- change the monster and this
 * table has to follow, which is why the citations are here.
 *
 *   health   its constructor's `this->health`
 *   melee    the damage of its touch, signalObject(SIGNAL_HURT, n) in its AI
 *   missile  the projectile its AI builds, and `damage` what that projectile carries
 *   seq      its sequence map's entries (AI.C <name>SeqMap): idle, walk, attack -- the base of
 *            the eight rotations, so a worn monster is drawn exactly as the AI draws it
 *   cool     tics between two attacks at 30 Hz; the monsters' own cadence is their animation's,
 *            which a worn monster does not run, so this replaces it
 */
enum {PSM_NONE,PSM_ANUBALL,PSM_SETBALL,PSM_MAGBALL,PSM_MUMBALL,PSM_SBALL};

typedef struct
{short mt;                      /* the OT_ type worn */
 short health;
 unsigned char missile,damage,melee,cool;
 unsigned char idle,walk,atk;
} PsRoleInfo;

static const PsRoleInfo psRoles[]=
{/*  monster        hp   missile      dmg melee cool  idle walk atk   AI.C           */
 {OT_ANUBIS,       100, PSM_ANUBALL,  0,  20,  18,    0,   8,  24},  /* :2617 :2566 :2584 */
 {OT_MUMMY,        130, PSM_MUMBALL,  0,  20,  20,    8,   0,  28},  /* :3428 :3378 :3392 */
 {OT_SELKIS,      2000, PSM_ANUBALL,  0,  20,  22,    0,   8,  16},  /* :2863 :2718        */
 {OT_SET,         1300, PSM_SETBALL, 30,  40,  22,    0,   0,  40},  /* :3050 :2943 :2961 */
 {OT_MAGMANTIS,    400, PSM_MAGBALL, 50,   0,  24,    0,   0,   0},  /* :2479 :2441        */
 {OT_SENTRY,       180, PSM_SBALL,    0,   0,  20,    0,   0,   8},  /* :3293 :3222        */
 {OT_BASTET,       150, PSM_NONE,     0,  10,  14,    0,   0,  10},  /* :3675 :3519        */
 {OT_SPIDER,        20, PSM_NONE,     0,  10,  12,    0,   0,   0},  /* :617  :583         */
 {OT_WASP,          60, PSM_NONE,     0,  20,  14,    0,   0,   0},  /* :1801 :1763        */
 {OT_FISH,          40, PSM_NONE,     0,  20,  14,    8,   0,   0},  /* :767  :731         */
 {OT_HAWK,          10, PSM_NONE,     0,  20,  14,    8,   8,   8}}; /* :1635 :1592        */
#define PS_NMROLES ((int)(sizeof(psRoles)/sizeof(psRoles[0])))

static const PsRoleInfo *ps_roleInfo(int mt)
{int i;
 for (i=0;i<PS_NMROLES;i++)
    if (psRoles[i].mt==mt)
       return &psRoles[i];
 return NULL;
}

int ps_isMonsterPlayer(int k)
{return k>=0 && k<MPMAX && psRoleMt[k]!=0;
}

/* The loaded player becomes `mt` (0 = itself again) */
static void ps_become(int k,int mt)
{const PsRoleInfo *ri=ps_roleInfo(mt);
 psRoleMt[k]=(short)(ri? mt: 0);
 psRoleCool[k]=0;
 if (!ri)
    {psRoleMax[k]=0;
     return;
    }
 psRoleMax[k]=ri->health;
 currentState.health=ri->health;
}

/* The nearest monster of a type the table knows, in full health: the one the player takes over.
   A boss is never taken by a DEMON -- it is the other mode's -- and a dead one is no use. */
static MonsterObject *ps_pickMonster(int bossOnly,Sprite *near)
{Object *o;
 MonsterObject *best=NULL;
 Fixed32 d,bestD=0;
 int list;
 for (list=0;list<2;list++)
    for (o=(list? objectIdleList: objectRunList)->next;o;o=o->next)
       {MonsterObject *m=(MonsterObject *)o;
	const PsRoleInfo *ri;
	if (o->class!=CLASS_MONSTER || o->type==OT_PLAYER || mpIndexOfObject(o)>=0)
	   continue;
	ri=ps_roleInfo(o->type);
	if (!ri || m->health<ri->health)
	   continue;
	if (bossOnly!=(o->type==OT_SET || o->type==OT_SELKIS || o->type==OT_MAGMANTIS))
	   continue;
	if (!m->sprite)
	   continue;
	d=near? spriteDistApprox(near,m->sprite): 0;
	if (!best || d<bestD)
	   {best=m;
	    bestD=d;
	   }
       }
 return best;
}

/* Take it over: the camera goes where it stood, it goes away, and the player wears it. */
static int ps_takeOver(int k,int bossOnly)
{MonsterObject *m=ps_pickMonster(bossOnly,mpBody[k]);
 if (!m)
    return 0;
 moveSpriteTo(mpBody[k],m->sprite->s,&m->sprite->pos);
 mpBody[k]->vel.x=mpBody[k]->vel.y=mpBody[k]->vel.z=0;
 ps_become(k,m->type);
 delayKill((Object *)m);
 return 1;
}

/* WEAPON.C fireWeapon (CFG_ROLE_FIRE): the shot shows on the body (ps_bodySeq); a worn monster
   throws what it throws instead, and the gun does nothing -- 1 */
int ps_fire(void)
{psFireTic[mpCur]=(unsigned short)psBodyTic[mpCur];
 if (!ps_roleNoWeapon())
    return 0;
 ps_roleFire();
 return 1;
}

/* SEQUENCE.C / WEAPON.C: a worn monster has no gun to draw and no gun to fire */
int ps_roleNoWeapon(void)
{return psRoleMt[mpCur]!=0;
}

/* WEAPON.C fireWeapon: the monster's attack instead of the player's.  Same projectile, same
   damage and the same throw as its AI builds (initProjectile at the height its AI uses). */
void ps_roleFire(void)
{const PsRoleInfo *ri=ps_roleInfo(psRoleMt[mpCur]);
 MthXyz to,pos,vel;
 int heading;
 if (!ri || psRoleCool[mpCur]>0)
    return;
 psRoleCool[mpCur]=ri->cool;
 /* aim where the eye looks, one wall away: the monsters aim at an enemy, a player aims itself */
 to.x=camera->pos.x+(MTH_Sin(playerAngle.yaw)<<9);
 to.y=camera->pos.y-(MTH_Sin(playerAngle.pitch)<<9);
 to.z=camera->pos.z-(MTH_Cos(playerAngle.yaw)<<9);
 if (ri->missile==PSM_NONE)
    {/* a touch: whatever stands within its reach in front of it */
     int j;
     for (j=0;j<mpPlayers;j++)
	if (j!=mpCur && mpBody[j] && !mpSpares(mpCur,j) &&
	    spriteDistApprox(camera,mpBody[j])<F(96))
	   signalObject(mpObj[j],SIGNAL_HURT,ri->melee,(int)mpObj[mpCur]);
     return;
    }
 initProjectile(&camera->pos,&to,&pos,&vel,F(0),4);
 heading=getAngle(vel.x,vel.z);
 switch (ri->missile)
    {case PSM_ANUBALL:
	constructAnuball(camera->s,&pos,&vel,(SpriteObject *)mpObj[mpCur],heading);
	break;
     case PSM_SETBALL:
	constructGenproj(camera->s,&pos,&vel,(SpriteObject *)mpObj[mpCur],heading,
			 0,OT_SETBALL,ri->damage,RGB(15,15,15),0,0,0,0);
	break;
     case PSM_MAGBALL:
	constructGenproj(camera->s,&pos,&vel,(SpriteObject *)mpObj[mpCur],heading,
			 1,OT_MAGBALL,ri->damage,RGB(15,5,5),0,0,0,0);
	break;
     case PSM_MUMBALL:
	constructCobra(camera->s,pos.x,pos.y,pos.z,heading,0,
		       (SpriteObject *)mpObj[mpCur],OT_MUMBALL,0);
	break;
     case PSM_SBALL:
	constructSball(camera->s,&pos,&vel,(SpriteObject *)mpObj[mpCur],heading,24,15000);
	break;
    }
}

/* The frames of a worn monster, in place of the level's skin (ps_playerBodySeq). */
static short ps_roleBodySeq(int k,Sprite *body,Sprite *viewer)
{const PsRoleInfo *ri=ps_roleInfo(psRoleMt[k]);
 int base,view,saved,seq,nmFrames;
 if (!ri || level_sequenceMap[ri->mt]<0)
    return -1;
 base=(psRoleCool[k]>ri->cool/2)? ri->atk:
      (body->vel.x || body->vel.z)? ri->walk: ri->idle;
 saved=body->angle;
 body->angle=normalizeAngle(body->angle+F(90));
 view=getFacingAngle(body,viewer);
 body->angle=saved;
 seq=level_sequenceMap[ri->mt]+base+view;
 if (seq<0 || seq>=level_nmSequences)
    return -1;
 nmFrames=level_sequence[seq+1]-level_sequence[seq];
 if (nmFrames<1)
    return -1;
 body->frame=psBodyTic[k]%nmFrames;
 return (short)seq;
}

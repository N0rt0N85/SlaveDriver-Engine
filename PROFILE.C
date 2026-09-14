#ifndef NPROFILE

#include "util.h"
#include "string.h"
#include "print.h"
#include "spr.h"

#define FRT 0xfffffe10
#define TCR 6
#define TIER 0
#define TCSR 1
#define FRT_H 2
#define FRT_L 3

void setFastTimer(void)
{/* set timer to cycles/32 */
 POKE_B(FRT+TCR,(PEEK_B(FRT+TCR)&~3)|1);
 /* turn off interrupts */
 POKE_B(FRT+TIER,PEEK_B(FRT+TIER)&~8);
 /* turn off compare/reset & zero overflow bit */
 POKE_B(FRT+TCSR,PEEK_B(FRT+TCSR)&~1);
 /* clear frt */
 POKE_B(FRT+FRT_H,0);
 POKE_B(FRT+FRT_L,0);
}

unsigned short getTimer(void)
{unsigned char h,l;
 h=PEEK_B(FRT+FRT_H);
 l=PEEK_B(FRT+FRT_L);
 return (h<<8)|l;
}

#define MAXNMCHILDREN 8

typedef struct _node
{char *id;
 unsigned int totalTime,lastReportedTime,lastShownTime;
 struct _node *child[MAXNMCHILDREN];
 struct _node *parent;
 int nmChildren;
} ProfileNode;

#define MAXNMNODES 60
static ProfileNode nodes[MAXNMNODES];

static int nmNodes;
static int level;
static unsigned short lastTime;
ProfileNode *currentNode;

void initProfiler(void)
{int i;
 setFastTimer();
 level=0;

 for (i=0;i<MAXNMNODES;i++)
    {nodes[i].nmChildren=0;
     nodes[i].totalTime=0;
     nodes[i].parent=NULL;
     nodes[i].lastReportedTime=0;
     nodes[i].lastShownTime=0;
    }

 nmNodes=1;
 currentNode=nodes;
 currentNode->id="root";
 lastTime=getTimer();
}

void pushProfile(char *id)
{int i;
 for (i=0;i<currentNode->nmChildren;i++)
    if (currentNode->child[i]->id==id)
       break;

 currentNode->totalTime+=(getTimer()-lastTime)&0xffff;

 if (i==currentNode->nmChildren)
    {assert(nmNodes<MAXNMNODES);
     assert(currentNode->nmChildren<MAXNMCHILDREN);
     currentNode->child[currentNode->nmChildren++]=nodes+nmNodes;
     nodes[nmNodes].parent=currentNode;
     nodes[nmNodes].id=id;
     currentNode=nodes+nmNodes;
     nmNodes++;
    }
 else
    currentNode=currentNode->child[i];

 lastTime=getTimer();
}

void popProfile(void)
{currentNode->totalTime+=(getTimer()-lastTime)&0xffff;
 currentNode=currentNode->parent;
 assert(currentNode);
 lastTime=getTimer();
}

static unsigned int sumChildren(ProfileNode *node)
{int i;
 unsigned int tot;
 tot=node->totalTime;
 for (i=0;i<node->nmChildren;i++)
    tot+=sumChildren(node->child[i]);
 return tot;
}

static void printTree(ProfileNode *tree,int level,
		      unsigned int parentTotal,unsigned int parentDifference)
{int i;
 char buff[80];
 unsigned int difPercent,totPercent;
 unsigned int sum;
 for (i=0;i<level;i++)
    debugPrint(" ");
 sum=sumChildren(tree)>>10;
 if (parentTotal<1)
    totPercent=0;
 else
    totPercent=(1000*sum)/parentTotal;

 if (parentDifference<1)
    difPercent=0;
 else
    difPercent=(1000*(sum-tree->lastReportedTime))/parentDifference;

 sprintf(buff,"%s : %d (%d)\n",tree->id,difPercent,totPercent);
 debugPrint(buff);
 for (i=0;i<tree->nmChildren;i++)
    printTree(tree->child[i],level+1,sum,sum-tree->lastReportedTime);

 tree->lastReportedTime=sum;
}

/* On-screen profile.  debugPrint() is a no-op macro unless the build talks to a Psy-Q
   host (UTIL.H), so dumpProfileData() writes to nothing on a console; this draws the same
   tree with the game's own font, one line per node, as the milliseconds spent in that node
   since the previous frame.

   The FRT counts cycles/32, so 28.636 MHz gives 894875 ticks/s: a tick is 1/895 ms and
   tenths of a millisecond are ticks*10/895 == ticks*2/179. */
#define PROF_TENTHS(t) (((t)*2)/179)
#include "gameparams.h"     /* here, below every assert: their __LINE__ stays the baseline's */
#ifdef GP_GAME_DOOM
/* the 224-line Doom frame: 11 lines on the left (84..184, above the status bar), then a
   second column on the right under the vswaps/used lines -- 14 lines cut the tree before
   anything that follows Run Objects */
#define MAXPROFLINES 24
#define PROF_COL1 11
#define PROF_COL2_X 2
#define PROF_COL2_Y (-58)
#else
#define MAXPROFLINES 14
#endif

/* drawString() costs one VDP1 command per character, so the overlay has to live inside
   whatever the frame left free -- see EZ_getCmdRoom().  PROF_RESERVE keeps a margin for
   everything drawn after this point. */
#define PROF_RESERVE 128

int profileShow=0;
static int profLine,profRoom;
static char profBuff[64];

static void drawTree(ProfileNode *tree,int level,int x,int y)
{int i;
 unsigned int now,delta;
 if (profLine>=MAXPROFLINES)
    return;
 now=sumChildren(tree);
 delta=now-tree->lastShownTime;
 tree->lastShownTime=now;
 sprintf(profBuff,"%s %d.%d",tree->id,
	 (int)(PROF_TENTHS(delta)/10),(int)(PROF_TENTHS(delta)%10));
 {int len=strlen(profBuff);
  if (len>profRoom)
     {profLine=MAXPROFLINES; /* out of command list: stop, keep the lines already drawn */
      return;
     }
  profRoom-=len;
 }
#ifdef GP_GAME_DOOM
 if (profLine>=PROF_COL1)
    drawString(PROF_COL2_X+6*level,PROF_COL2_Y+10*(profLine-PROF_COL1),1,
	       (unsigned char *)profBuff);
 else
#endif
 drawString(x+6*level,y+10*profLine,1,(unsigned char *)profBuff);
 profLine++;
 for (i=0;i<tree->nmChildren;i++)
    drawTree(tree->child[i],level+1,x,y);
}

void drawProfileData(int x,int y)
{profLine=0;
 profRoom=EZ_getCmdRoom()-PROF_RESERVE;
 drawTree(nodes,0,x,y);
}

void dumpProfileData(void)
{unsigned int sum;
 debugPrint("\n\n");
 sum=sumChildren(nodes)>>10;
 printTree(nodes,0,sum,sum-nodes->lastReportedTime);
}

#endif

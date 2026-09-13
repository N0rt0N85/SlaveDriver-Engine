/* PLRCYL.C -- PLAYER MODEL "cylinder" (gameparams.cfg: PLAYER_MODEL = cylinder).
   Compiled only for that model; the ball model needs none of this and keeps its binaries.

   The camera is a vertical cylinder: `radius` is the horizontal half-width, pos.y is the EYE,
   the feet are at pos.y-cylFoot and the head at pos.y+cylHead.  Three postures move the feet
   without moving the eye's relation to the floor; the free height decides whether we may grow.

   Every constant is a gameparams.cfg key (GP_*), so a different game only edits that file.
   The shipped duke.cfg preset carries the Duke Nukem 3D numbers measured by
   tools/duke2ps/player_dims.py (a simulation of jfduke3d's loops; no code was copied):
   eye 80/36/16 u standing/crouched/shrunk, 8 u of head, step 40 u standing and nothing crouched,
   shrunk for 9.6 s, crushed when the gap falls under 48 u while growing back. */
#include <sega_mth.h>
#include <sega_per.h>
#include "level.h"
#include "util.h"
#include "sprite.h"
#include "sruins.h"

#define FOOT_STAND  F(GP_PLAYER_EYE_STAND)
#define FOOT_CROUCH F(GP_PLAYER_EYE_CROUCH)
#define FOOT_SHRUNK F(GP_PLAYER_EYE_SHRUNK)
#define FOOT_SWIM   F(GP_PLAYER_EYE_SWIM)
#define HEAD        F(GP_PLAYER_HEAD)
#define STEP_STAND  F(GP_PLAYER_STEP_STAND)
#define STEP_LOW    F(GP_PLAYER_STEP_CROUCH)
#define EYE_SPEED   F(GP_PLAYER_EYE_SPEED)
#define CRUSH_GAP   F(GP_PLAYER_CRUSH_GAP)
#ifdef PAL
#define SHRINKFRAMES (50*GP_PLAYER_SHRINK_TENTHS/10)
#else
#define SHRINKFRAMES (60*GP_PLAYER_SHRINK_TENTHS/10)
#endif

Fixed32 cylFoot=FOOT_STAND,cylHead=HEAD,cylStep=STEP_STAND;
static int shrinkTimer;

void cylPlayerInit(Sprite *s)
{cylFoot=FOOT_STAND; cylHead=HEAD; cylStep=STEP_STAND;
 shrinkTimer=0;
}

void cylShrink(void)
{if (!shrinkTimer)
    shrinkTimer=SHRINKFRAMES;
}

/* y distance from sprite s to the camera's core segment (capsule of radius camera->radius) */
Fixed32 cylDY(Sprite *m,Sprite *s)
{Fixed32 d=m->pos.y-s->pos.y,lo,hi,c;
 if (m!=camera && s!=camera)
    return d;
 lo=camera->radius-cylFoot;                  /* below the eye */
 hi=cylHead-camera->radius;
 if (hi<lo)                                   /* body lower than a sphere: use its middle */
    lo=hi=(cylHead-cylFoot)>>1;
 c=(m==camera)?-d:d;
 if (c<lo) c=lo;
 if (c>hi) c=hi;
 return (m==camera)?d+c:d-c;
}

int cylWeaponNext(unsigned int mask,int d)
{int n=bitScanForward(mask,d);
 if (n==-1)
    n=bitScanForward(mask,-1);               /* wrap around: one button cycles the weapons */
 return (n==d)?-1:n;
}

void cylPosture(unsigned short input,unsigned short pushed)
{Fixed32 target,step,room,dy;
 if (shrinkTimer)
    shrinkTimer--;
 /* Free height at the camera, own sector only (Duke probes a 163-unit radius instead).
    BOTH helpers return `p->y - plane`, so the ceiling one is NEGATIVE under a ceiling
    (UTIL.C:67-89): the gap is their DIFFERENCE, never their sum. */
 room=findFloorDistance(camera->s,&camera->pos)-findCeilDistance(camera->s,&camera->pos);
 if (camera->flags & SPRITEFLAG_UNDERWATER)
    {target=FOOT_SWIM; step=F(1);}
 else if (shrinkTimer)
    {target=FOOT_SHRUNK; step=STEP_LOW;}
 else if (!(input & IMASK(ACTION_WEPDN)))    /* pad bits are active low */
    {target=FOOT_CROUCH; step=STEP_LOW;}
 else
    {target=FOOT_STAND; step=STEP_STAND;}
 if (target>cylFoot && room<target+HEAD)
    {/* no room to grow: end of shrink -> crushed under CRUSH_GAP, else crouched; crouch
	release under a low ceiling -> stay as we are */
     if (cylFoot<FOOT_CROUCH && !shrinkTimer && room<CRUSH_GAP)
	{playerHurt(10000);
	 target=cylFoot;
	}
     else if (room>=FOOT_CROUCH+HEAD)
	{target=FOOT_CROUCH; step=STEP_LOW;}
     else
	target=cylFoot;
    }
 dy=target-cylFoot;                           /* feet stay put: the eye moves with the body */
 if (dy>EYE_SPEED) dy=EYE_SPEED;
 if (dy<-EYE_SPEED) dy=-EYE_SPEED;
 cylFoot+=dy;
 camera->pos.y+=dy;
 cylStep=step;
}

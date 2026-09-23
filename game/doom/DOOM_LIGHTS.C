/* DOOM_LIGHTS.C -- the dynamic lights of the Doom port: what each effect asks for.
 *
 * params/doom.cfg gives a build its starting values (GP_LIGHT_*); doomLightFx holds them from
 * then on, so a light can be judged and changed with the level running.  An effect asks for a
 * tint (k 0..16 a channel), a radius and an intensity; WALLS.C then applies the player's own
 * settings -- the switch, the two scales, the tint offsets -- and places the light.
 *
 * The tuner that edits them runs only in a menu, so it waits on the disc: PAUSE.OVL
 * (game/doom/ovl/LIGHTS.C, GCC14).  Two ways in: the title's OPTIONS -> LIGHTS and the pause's
 * OPTIONS -> LIGHTS; the in-level chord and its HUD lines are gone.  Nothing is saved: the numbers
 * on screen are what params/doom.cfg should be given once they are right. */
#include <string.h>
#include <stdio.h>
#include <sega_spr.h>
#include <sega_scl.h>
#include <sega_mth.h>
#include <sega_per.h>
#include "util.h"
#include "spr.h"
#include "sprite.h"
#include "object.h"
#include "walls.h"
#include "mplayer.h"
#include "print.h"
#include "sound.h"
#include "v_blank.h"
#include "doom.h"
#include "doom_lights.h"

DoomLightFx doomLightFx[DLF_NM]=
   {{GP_LIGHT_IMP,0},              /* a missile's light lives as long as the missile does */
    {GP_LIGHT_CACO,0},
    {GP_LIGHT_BARON,0},
    {GP_LIGHT_ROCKET,0},
    {GP_LIGHT_PLASMA,0},
    {GP_LIGHT_EXPLODE,GP_LIGHT_EXPLODE_TICS},
    {GP_LIGHT_MUZZLE_PLAYER,GP_LIGHT_MUZZLE_TICS},
    {GP_LIGHT_MUZZLE_MONSTER,GP_LIGHT_MUZZLE_TICS},
    {GP_LIGHT_TELEPORT,0}};             /* the teleport fog: lit as long as the fog lives */

/* --- what the effects ask the engine --------------------------------------------------------- */

/* Who keeps a slot when the 15 are taken (WALLS.C lightPut): the player's own flash, then the
   explosions and the lamps, then the missiles in flight, then the monsters' flashes. */
static int doomLightPrio(int fx)
{switch (fx)
    {case DLF_MUZZLE_PLAYER:
	return DOOM_LIGHTPRIO_PLAYER;
     case DLF_EXPLODE:
	return DOOM_LIGHTPRIO_EXPLODE;
     case DLF_MUZZLE_MONSTER:
	return DOOM_LIGHTPRIO_MUZZLE;
     case DLF_TELEPORT:
	return DOOM_LIGHTPRIO_EXPLODE;
     default:
	return DOOM_LIGHTPRIO_MISSILE;
    }
}

void doom_lightAdd(Sprite *s,int fx)
{const DoomLightFx *f=&doomLightFx[fx];
 assert(s && fx>=0 && fx<DLF_NM);
 addLightPrio(s,f->r,f->g,f->b,f->radius,f->peak,doomLightPrio(fx));
}

void doom_lightChange(Sprite *s,int fx)
{const DoomLightFx *f=&doomLightFx[fx];
 assert(s && fx>=0 && fx<DLF_NM);
 changeLightEx(s,f->r,f->g,f->b,f->radius,f->peak);
}

/* Scales the intensity to left/tics; tint and radius unchanged.  The tuner can shorten a flash
   under one that is running, so left is held to the duration. */
void doom_lightFade(Sprite *s,int fx,int left)
{const DoomLightFx *f=&doomLightFx[fx];
 int total=f->tics;
 assert(s && fx>=0 && fx<DLF_NM);
 if (total<1)
    total=1;
 if (left>total)
    left=total;
 changeLightEx(s,f->r,f->g,f->b,0,f->peak*left/total);
}

/* A_Explode.  A rocket already carries its flight light and the list takes no duplicates, so
   that light is brightened and widened instead.  Fading and removal go through flashTics. */
void doom_explosionLight(DoomActor *this)
{assert(this);
 if (!this->sprite)
    return;
 if (hasLight(this->sprite))
    doom_lightChange(this->sprite,DLF_EXPLODE);
 else
    doom_lightAdd(this->sprite,DLF_EXPLODE);
 this->flashTics=doomLightFx[DLF_EXPLODE].tics;
}

/* A flash lasts two tics.  Solo draws an image every tic or two and that is a flash; in split
   screen three or four tics run between two images, so a flash could be lit and put out inside
   one logic block, with no view ever drawing it -- players 2 to 4 lost theirs most often, their
   views being drawn at the END of that block.  So the flashes advance at most once per image:
   an image marks itself (doom_lightImage, DOOM_PLAYER.C at 60 Hz), the tic consumes the mark
   (doom_lightTicStart), and every fade asks doom_lightStep whether this tic counts. */
static char lightImageSeen,lightStepNow;

void doom_lightImage(void)
{lightImageSeen=1;
}

void doom_lightTicStart(void)
{lightStepNow=lightImageSeen;
 lightImageSeen=0;
}

int doom_lightStep(void)
{return lightStepNow;
}

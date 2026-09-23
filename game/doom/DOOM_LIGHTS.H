/* DOOM_LIGHTS.H -- the Doom port's dynamic lights (an addition: Doom has none).
 * params/doom.cfg holds the values a build starts with (LIGHT_* = R,G,B,RADIUS,INTENSITY, plus
 * the two durations), turned into GP_LIGHT_*.  From there they are a live table, doomLightFx,
 * which the tuner edits (PAUSE.OVL, game/doom/ovl/LIGHTS.C): the effect asks, WALLS.C's player
 * settings scale.
 * DOOM_ACTOR.C, DOOM_VERBS.C and DOOM_WEAPON.C only name an effect. */
#ifndef DOOM_LIGHTS_H
#define DOOM_LIGHTS_H

#if !defined(GP_LIGHT_IMP) || !defined(GP_LIGHT_CACO) || !defined(GP_LIGHT_BARON) ||     \
    !defined(GP_LIGHT_ROCKET) || !defined(GP_LIGHT_PLASMA) || !defined(GP_LIGHT_EXPLODE) || \
    !defined(GP_LIGHT_MUZZLE_PLAYER) || !defined(GP_LIGHT_MUZZLE_MONSTER) ||                \
    !defined(GP_LIGHT_TELEPORT) ||                                                          \
    !defined(GP_LIGHT_EXPLODE_TICS) || !defined(GP_LIGHT_MUZZLE_TICS)
#error "LIGHT_* keys missing from the game parameters: copy them from params/doom.cfg"
#endif

enum {DLF_IMP,DLF_CACO,DLF_BARON,DLF_ROCKET,DLF_PLASMA,DLF_EXPLODE,
      DLF_MUZZLE_PLAYER,DLF_MUZZLE_MONSTER,DLF_TELEPORT,DLF_NM};

/* k 0..16 per channel, radius in world units, intensity 0..31 at the centre, tics the flash
   lasts (0 = the effect's own life, a missile in flight) */
typedef struct {short r,g,b,radius,peak,tics;} DoomLightFx;
/* when the 15 slots are taken, a light takes the slot of a lower one (WALLS.C lightPut) */
#define DOOM_LIGHTPRIO_MUZZLE  0               /* a monster's shot */
#define DOOM_LIGHTPRIO_MISSILE 1               /* a missile in flight */
#define DOOM_LIGHTPRIO_EXPLODE 2               /* an explosion, a lamp */
#define DOOM_LIGHTPRIO_PLAYER  3               /* the player's own shot */
extern DoomLightFx doomLightFx[DLF_NM];

void doom_lightAdd(Sprite *s,int fx);          /* the effect's light on s */
void doom_lightChange(Sprite *s,int fx);       /* ... on a light s already carries */
void doom_lightFade(Sprite *s,int fx,int left);/* intensity * left/tics; tint and radius kept */
void doom_explosionLight(DoomActor *this);     /* A_Explode */

/* A flash may not be put out in the same image it was lit in (DOOM_LIGHTS.C) */
void doom_lightImage(void);                    /* an image was drawn (60 Hz, player 1) */
void doom_lightTicStart(void);                 /* the tic takes the mark (35 Hz, player 1) */
int  doom_lightStep(void);                     /* 1 = the flashes advance this tic */

/* The tuner (the title's and the pause's OPTIONS -> LIGHTS) is in PAUSE.OVL: game/doom/ovl/. */

#endif

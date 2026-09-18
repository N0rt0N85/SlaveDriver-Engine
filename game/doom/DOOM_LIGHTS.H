/* DOOM_LIGHTS.H -- the Doom port's dynamic lights (an addition: Doom has none).
 * Settings are build parameters: params/doom.cfg, LIGHT_* = R,G,B,RADIUS,INTENSITY, turned
 * into GP_LIGHT_* and passed as is to addLightEx / changeLightEx (WALLS.C).  This file only
 * checks the keys and holds the prototypes shared by DOOM_ACTOR.C, DOOM_VERBS.C, DOOM_WEAPON.C. */
#ifndef DOOM_LIGHTS_H
#define DOOM_LIGHTS_H

#if !defined(GP_LIGHT_IMP) || !defined(GP_LIGHT_CACO) || !defined(GP_LIGHT_BARON) ||     \
    !defined(GP_LIGHT_ROCKET) || !defined(GP_LIGHT_PLASMA) || !defined(GP_LIGHT_EXPLODE) || \
    !defined(GP_LIGHT_MUZZLE_PLAYER) || !defined(GP_LIGHT_MUZZLE_MONSTER) ||                \
    !defined(GP_LIGHT_EXPLODE_TICS) || !defined(GP_LIGHT_MUZZLE_TICS)
#error "LIGHT_* keys missing from the game parameters: copy them from params/doom.cfg"
#endif

/* DOOM_ACTOR.C */
void doom_lightFade(Sprite *s,int r,int g,int b,int radius,int peak,int left,int total);
                                        /* intensity * left/total; tint and radius kept */
void doom_explosionLight(DoomActor *this);     /* A_Explode */

#endif

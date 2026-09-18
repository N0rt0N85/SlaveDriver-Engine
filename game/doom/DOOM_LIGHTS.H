/* DOOM_LIGHTS.H -- les lumieres dynamiques du portage Doom.
 *
 * Doom n'en a AUCUNE : ses images de tir sont marquees fullbright, ce qui eclaire le sprite et
 * rien autour.  Tout ceci est un AJOUT.
 *
 * Les reglages ne sont PAS ici : teinte, rayon, intensite et durees de chaque source sont des
 * parametres de build, dans params/doom.cfg (cles LIGHT_*), que gameparams.py transforme en
 * GP_LIGHT_* (build/.../gameparams.h, inclus par SPRITE.H).  Une source y est un quintuplet
 * R,G,B,RAYON,INTENSITE, passe tel quel a addLightEx / changeLightEx (WALLS.C).
 *
 * Ce fichier ne garde que la verification des cles et les prototypes partages par DOOM_ACTOR.C,
 * DOOM_VERBS.C et DOOM_WEAPON.C. */
#ifndef DOOM_LIGHTS_H
#define DOOM_LIGHTS_H

#if !defined(GP_LIGHT_IMP) || !defined(GP_LIGHT_CACO) || !defined(GP_LIGHT_BARON) ||     \
    !defined(GP_LIGHT_ROCKET) || !defined(GP_LIGHT_PLASMA) || !defined(GP_LIGHT_EXPLODE) || \
    !defined(GP_LIGHT_MUZZLE_PLAYER) || !defined(GP_LIGHT_MUZZLE_MONSTER) ||                \
    !defined(GP_LIGHT_EXPLODE_TICS) || !defined(GP_LIGHT_MUZZLE_TICS)
#error "cles LIGHT_* absentes des parametres : les recopier depuis params/doom.cfg"
#endif

/* DOOM_ACTOR.C */
void doom_lightFade(Sprite *s,int r,int g,int b,int radius,int peak,int left,int total);
					/* intensite * left/total ; teinte et rayon gardes */
void doom_explosionLight(DoomActor *this);     /* A_Explode */

#endif

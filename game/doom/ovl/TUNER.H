/* TUNER.H -- the light tuner's rows, in PAUSE.OVL (GCC14): drawn by the pause's LIGHTS page
 * (PAUSE.C) and by the title's LIGHTS screen (TUNER_TITLE.C).  The values they edit stay in MAIN
 * (DOOM_LIGHTS.C doomLightFx, WALLS.C lightOn ...): the renderer reads them. */
#ifndef __INCLUDEDtunerh
#define __INCLUDEDtunerh

enum {LR_EFFECT,LR_ON,LR_INT,LR_RAD,LR_RED,LR_GREEN,LR_BLUE,LR_TICS,LR_RESET,LR_BACK,LR_NM};

extern int lightRow;                    /* the chosen row: each run of the overlay opens on EFFECT */
int  lightRowShown(int r);              /* DURATION only for an effect that has one */
void lightLine(int r,char *text);       /* the row's words, at most 40 bytes */
void lightAdjust(int r,int d);          /* -1 / +1 on row r; RESET puts params/doom.cfg back */
void lightMove(int d);                  /* the next shown row up (-1) or down (+1) */

#endif

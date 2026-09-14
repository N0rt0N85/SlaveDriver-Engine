/* DOOM_ACTIONS.H -- GENERE par tools/doom2ps/info2tables.py depuis Mimas/core/info.c : NE PAS EDITER.
 * Verbes A_* distincts de states[], ordre d'apparition = index doom_action_t (DOOM_TABLES.H).
 *   M(name) : verbe d'acteur   void name(DoomActor *this)
 *   P(name) : verbe d'arme     void name(DoomPlayer *p, int ps)   (etats psprite 1..89)
 * A inclure APRES DOOM.H (DoomActor, DoomPlayer, DoomAction).  DOOM_VERBS.C :
 *   DOOM_ACTIONS_PROTOTYPES
 *   #define M(n) { n },              (premier membre de l'union : .mobj)
 *   #define P(n) { .psp = n },       (designateur : extension GNU acceptee en gnu89)
 *   const DoomAction doomActions[DOOM_NUMACTIONS] = { { 0 }, DOOM_ACTIONS_LIST(M, P) };
 *   #undef M / #undef P ; un verbe non implemente se met a { 0 } (SPEC_RUNTIME section 3). */
#ifndef DOOM_ACTIONS_H
#define DOOM_ACTIONS_H

#define DOOM_ACTIONS_LIST(M, P) \
    P(A_Light0) \
    P(A_WeaponReady) \
    P(A_Lower) \
    P(A_Raise) \
    P(A_Punch) \
    P(A_ReFire) \
    P(A_FirePistol) \
    P(A_Light1) \
    P(A_FireShotgun) \
    P(A_Light2) \
    P(A_FireShotgun2) \
    P(A_CheckReload) \
    P(A_OpenShotgun2) \
    P(A_LoadShotgun2) \
    P(A_CloseShotgun2) \
    P(A_FireCGun) \
    P(A_GunFlash) \
    P(A_FireMissile) \
    P(A_Saw) \
    P(A_FirePlasma) \
    P(A_BFGsound) \
    P(A_FireBFG) \
    M(A_BFGSpray) \
    M(A_Explode) \
    M(A_Pain) \
    M(A_PlayerScream) \
    M(A_Fall) \
    M(A_XScream) \
    M(A_Look) \
    M(A_Chase) \
    M(A_FaceTarget) \
    M(A_PosAttack) \
    M(A_Scream) \
    M(A_SPosAttack) \
    M(A_VileChase) \
    M(A_VileStart) \
    M(A_VileTarget) \
    M(A_VileAttack) \
    M(A_StartFire) \
    M(A_Fire) \
    M(A_FireCrackle) \
    M(A_Tracer) \
    M(A_SkelWhoosh) \
    M(A_SkelFist) \
    M(A_SkelMissile) \
    M(A_FatRaise) \
    M(A_FatAttack1) \
    M(A_FatAttack2) \
    M(A_FatAttack3) \
    M(A_BossDeath) \
    M(A_CPosAttack) \
    M(A_CPosRefire) \
    M(A_TroopAttack) \
    M(A_SargAttack) \
    M(A_HeadAttack) \
    M(A_BruisAttack) \
    M(A_SkullAttack) \
    M(A_Metal) \
    M(A_SpidRefire) \
    M(A_BabyMetal) \
    M(A_BspiAttack) \
    M(A_Hoof) \
    M(A_CyberAttack) \
    M(A_PainAttack) \
    M(A_PainDie) \
    M(A_KeenDie) \
    M(A_BrainPain) \
    M(A_BrainScream) \
    M(A_BrainDie) \
    M(A_BrainAwake) \
    M(A_BrainSpit) \
    M(A_SpawnSound) \
    M(A_SpawnFly) \
    M(A_BrainExplode)

#define DOOM_ACTIONS_PROTO_M_(n) void n(DoomActor *this);
#define DOOM_ACTIONS_PROTO_P_(n) void n(DoomPlayer *p, int ps);
#define DOOM_ACTIONS_PROTOTYPES DOOM_ACTIONS_LIST(DOOM_ACTIONS_PROTO_M_, DOOM_ACTIONS_PROTO_P_)

#endif /* DOOM_ACTIONS_H */

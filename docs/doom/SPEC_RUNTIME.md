# SPEC_RUNTIME — runtime Doom côté moteur (`game/doom/*.C`), E1M1 (2026-09-14)

Implémente le contrat [DOOM_ABI.md](DOOM_ABI.md) (§1 numérotation OT, §2 séquences, §3 sons, §4 tables,
§8 build). Tout est sous `#ifdef GP_GAME_DOOM` (`GAME = doom` dans `params/doom.cfg`, gameparams.py:56-58).
Unités : 1 u Doom = 1 u Saturn ; angles moteur = degrés × 65536 (`F(45)`), soit 5760 par pas Doom de
360/4096° (OBJECT.C:194) ; `Fixed32` 16.16.

## Écarts au contrat et à la brève

| Sujet | Écart | Raison |
|---|---|---|
| `spriteObject_makeSound(this,n)` | **non utilisé** : il calcule `level_objectSoundMap[this->type]+n` (AICOMMON.C:250-253) alors que le contrat §3 indexe la carte par `sfxenum_t`. Remplacé par `doom_sound(sprite, sfx)` §8 | contrat > brève |
| Ramassages « idle list » | les objets Doom-item vivent dans **`objectRunList`** comme `constructThing` (AI.C:3915) : la liste idle ne reçoit pas `SIGNAL_MOVE` (`runObjects` AICOMMON.C:454-457) et la collision joueur n'est détectée que par `moveSprite` (AI.C:3819-3820) | mécanique moteur |
| Gibs (`constructBit`, plafond 20, AI.C:196-199) | **non utilisés** : Doom fait la gibbe par l'animation XDIE ; le contrat ne réserve pas de séquences de bits, et `constructBit` écrit `level_sequenceMap[OT_BIT]=0` (AI.C:205) dans la carte indexée par spritenum | contrat §2 |
| `constructGenproj` | non utilisé : `genproj_func` a sa propre `sequenceMap` et fait `OT_POOF` à l'impact (AI.C:108-114). Le projectile est un `DoomActor` ordinaire (§5) | fidélité (état death BAL1 + firxpl) |
| `radialDamage` (OBJECT.C:431-504) | non utilisé : sans LOS, chute linéaire `dam·(1−d/r)`, pousse la vélocité. `A_Explode` réécrit §4 | fidélité |
| Joueur (`playerHurt`, `playerGetObject`, `controlInput`) | **non touchés ici** : SPEC_PLAYER possède `DoomPlayer`, `doom_playerDamage` (seul calcul d'armure), `doom_playerGetObject` (effets des ramassages), `doom_playerTic` ; ce document fournit la collision (`doom_item_func`), l'appel de `doom_playerDamage` depuis `doom_damage`, et **l'horloge unique** qui appelle `doom_playerTic` (§1) | un seul `DOOM.H` (contrat §8) |

## 1. Cadence : 35 Hz par l'accumulateur

`monsterMoveCounter += vtimer` (vblanks 60 Hz, SRUINS.C:2302) ; boucle `for (;mmc>1;mmc-=2)` (:2142) et
plafond 8 (:2139-2140) = 30 Hz, 4 tics max/trame ; la même paire pilote `advanceWallAnimations/stepWater`
(:2196). **Choix : accumulateur ×7, pas 12** : `mmc += vtimer*7`, `for (;mmc>=12;mmc-=12)`, plafond
48 ⇒ 60·7/12 = **35,000 Hz** exact, 4 tics max. Justification contre le rescale ×6/7 des `tics` :
(a) les `tics` 1..4 (S_TBALLX 6/5/4, S_PUFF 4, S_BLOOD 8, marche TROO 3) s'arrondiraient à ±17 % ;
(b) les portes (2 u/tic, AI.C:4312) passent de 60 à **70 u/s = Doom** et l'attente 128 tics de 4,3 à
3,7 s (Doom 150 tics = 4,3 s : `+22` sur AI.C:4358, hors périmètre) ; (c) `doomStates[].tics` restent
tels quels (contrat §4). PAL non traité (50·7/12 = 29 Hz, comme l'original non compensé). Les compteurs
partageant la boucle (`ltHurtTime`, pouvoirs :2143-2160) tournent 17 % plus vite : inoffensif.

**Une seule horloge** : le corps de cette boucle (:2142) appelle **d'abord `doom_playerTic()`** (joueur +
psprites, SPEC_PLAYER §1.2, qui lit l'entrée mémorisée par `doom_playerFrame` à 60 Hz), **puis `runObjects()`**
(acteurs) — l'ordre de `P_Ticker` (joueurs, puis thinkers). Pas de second accumulateur dans `movePlayer` :
deux accumulateurs de même moyenne dérivent d'un tic (phases différentes) et consomment `P_Random` dans un
ordre non-Doom. Macro `CFG_PLAYER_TIC()` (§9).

## 2. `game_actor_func` — l'objet générique

```c
typedef struct {                       /* DOOM.H — préfixe SpriteObject (OBJECT.H:62-69) */
 short type,class; Object *next,*prev; messHandler func;
 Sprite *sprite; unsigned short *sequenceMap; short state,pad1;   /* state = statenum_t */
 short mt;                 /* mobjtype_t */
 short tics;               /* -1 = infini (états terminaux) */
 short health, reactiontime, threshold, movecount;
 unsigned char movedir, dirCur, nDir; char dirTry[8];     /* §3 A_Chase */
 unsigned short mflags;    /* DF_AMBUSH 1, DF_JUSTHIT 2, DF_JUSTATTACKED 4, DF_CORPSE 8,
                              DF_MISSILE 16, DF_NOBLOOD 32, DF_SHOOTABLE 64, DF_DROPPED 128 */
 Object *target;           /* cible (monstre) / tireur (projectile) — purgé par OBJECTDESTROYED */
 int collide;              /* résultat du dernier moveSprite, consommé par A_Chase */
 short lastlook, pad2;
} DoomActor;               /* ~76 o < sizeof(Object) 144 = 4 + 8 + 4 + 128 (OBJECT.H:30-35) : asserté */
```

| Signal (OBJECT.H:5-14) | Traitement |
|---|---|
| `SIGNAL_MOVE` (1 tic) | si `tics>0` : `collide=moveSprite(sprite)` sauf `DF_CORPSE` ; `--tics==0` ⇒ `doom_setState(this, doomStates[state].nextstate)` ; projectile : impact §5 ; monstre : `collide` mémorisé pour A_Chase |
| `SIGNAL_VIEW` (au dessin, WALLS.C:2695) | `sprite->sequence = doom_seq(sprite_t, frame, getFacingAngle(sprite,camera))` §7 |
| `SIGNAL_HURT(hp, hurter)` | `doom_damageActor` §4 |
| `SIGNAL_OBJECTDESTROYED(o)` | `o==this` ⇒ `freeSprite(sprite)` (comme `monsterObject_signalDestroyed` AICOMMON.C:188-194) ; `o==target` ⇒ `target=NULL` |
| `SIGNAL_ENTER` | ignoré (secteurs : `OT_SECTORSWITCH` moteur, contrat §6) |

`doom_setState` = `P_SetMobjState` (p_mobj.c:49-72) : boucle tant que `tics==0` ; `state==0` (S_NULL) ⇒
`delayKill` (:56-60) ; sinon `tics=st->tics`, `sprite->frame=0`, séquence recalculée §7, puis
`doomActions[st->action].mobj(this)` (contrat §4 : union, membre `mobj` ici — les états 1..89 à bit
« verbe d'arme » ne passent jamais par un acteur). États à `tics=-1` sans vélocité ⇒
`delay_moveObject(this, objectIdleList)` **pour le décor et les cadavres seulement** (plus de `SIGNAL_MOVE`,
`SIGNAL_VIEW` continue) — **jamais pour un objet `doom_item_func`** (tout `MF_SPECIAL`) : les états de spawn
de STIM, MEDI, CLIP, SHOT, SHEL, AMMO, SBOX ont `tics = −1` (info.c:968-1012) et la liste idle ne reçoit pas
`SIGNAL_MOVE` (`runObjects` AICOMMON.C:454-457 : `signalList(objectRunList, …)` seulement) ⇒ 13 des 53
ramassages d'E1M1 seraient imprenables (§6).

**Spawn** — `int game_placeObject(int ot)` (crochet OBJECT.C:212, contrat §1) : `ot` moteur ⇒ 0 ;
`OT_DOOM_EXIT/SECRETEXIT/LIGHT/DAMAGE` ⇒ constructeurs de DOOM_GAME.C ; sinon `mt = doomOtToMt[ot]`, lit
**6 shorts** `sector,x,y,z,angle,flags` (`suckShort`, contrat §1) et appelle `doom_spawn`.
`doom_spawn(mt, sector, pos, angle, flags)` : `getFreeObject(game_actor_func, ot, class)` avec class =
`CLASS_MONSTER` si `MF_SHOOTABLE` (monstres **et barils** : `autoTarget` élit les `CLASS_MONSTER`,
WALLS.C:2724-2732 ; `delayKill` ne diffuse `OBJECTDESTROYED` qu'à eux, OBJECT.C:134-138), `CLASS_PROJECTILE`
si `MF_MISSILE`, sinon `CLASS_SPRITE` ; **`NULL` ⇒ retour `NULL`** ; puis **`moveObject((Object*)this,
objectRunList)` immédiatement** — `getFreeObject` renvoie `objectFreeList->next` **sans le retirer** de la
liste libre (OBJECT.C:80-89), chaque constructeur moteur enchaîne le `moveObject` (AI.C:2582-2586, 3916,
4386-4389) ; sans lui, deux spawns consécutifs (puff + blood d'un même hitscan) reçoivent le même slot.
`newSprite(sector, F(height/2), F(1), gravity, -1, flags, this)` (SPRITE.C:68 ; **`NULL` ⇒ rendre l'objet à
la liste libre et retourner `NULL`**, comme `constructThing` AI.C:3913-3914) avec gravity = 0 si
`MF_NOGRAVITY` sinon `GRAVITY<<1` (Anubis AI.C:2588) ; **`sprite->scale = 65536`** (contrat §2 : 1 texel/u ;
`newSprite` pose 48000, SPRITE.C:86, et le dessin fait `scale = MTH_Div(scale·FOCALDIST, z)`, `width64 =
scale>>10`, WALLS.C:2716-2723 : à 48000 un chunk 64×64 couvre 47 u, monstres à 73 % — `SPRITEFLAG_NOSCALE`
n'est pas une option, il supprime la perspective :2718-2719) ; sprite flags :
`SPRITEFLAG_IMATERIAL|IMMOBILE` pour items/décor (constructThing AI.C:3910-3912), `IMATERIAL` pour
puff/blood/projectile, `BWATERBNDRY|BCLIFF` monstres (AI.C:2589). **Rayon = height/2** (56 → 28, baril
21) : **choix** (une seule sphère, SPRITE.C:490-500 collision par rayons cumulés) qui couvre la hauteur Doom au
prix de la largeur : +8 u par côté pour un monstre (56 contre 40 de diamètre), +11 pour un baril (42 contre
20) — dans un couloir de 64 u, joueur 16 + baril 21 = 37 > 32 ⇒ le baril n'est plus contournable (Doom :
26 < 32), deux POSS côte à côte demandent 112 u au lieu de 80 ; **vérificateur PC** : distance de chacun des
6 barils d'E1M1 aux murs de sa feuille (SPEC_CONVERTER §7). Les pieds restent à `pos.y−radius`
(WALLS.C:2697, shiftSprites SPRITE.C:60-66). **Spawn dynamique** (hors `placeObjects`, où `shiftSprites`
OBJECT.C:382 ajoute le rayon une fois) : `pos` est pris tel quel — les appelants posent `pos.y` eux-mêmes
(§5 puff/blood/missile ; drops §6 : sol + rayon). `health=spawnhealth`, `reactiontime=8` (info.c:1132),
`threshold=0`, `movedir=8` (DI_NODIR), `mflags` depuis `flags` (bit 3 = `DF_AMBUSH`) et `info.flags` ;
`doom_setState(spawnstate)`. Compteur `nmSpawnFail` (overlay) à chaque `NULL` ; les appelants puff/blood/
missile ignorent un retour `NULL` (fusil : 7 puff/blood par tir, cadence 44 tics ⇒ pic possible avec 30 monstres).

**Pools E1M1** (contrat §1, 124 things) : 116 mobjs (1 joueur, 29 monstres, 6 barils, 53 ramassages,
27 décors ; 8 starts ignorés) + 4 portes + 2 ascenseurs + 2 sector-switch + 1 switch + 1 exit + n
`OT_DOOM_DAMAGE` = **126 + n objets**, pic dynamique ≤ 4 fireballs + ~30 puff/blood ⇒ ≤ 165 + n < 347 libres
(`MAXOBJECTS 350`, 3 têtes, OBJECT.C:9,56-59) ; sprites ≤ 165 < 450 (SPRITE.C:12) ; `MAXNMMOVES 100`
(OBJECT.C:17) : `nmMoves` n'est remis à 0 que par `processDelayedMoves`, appelé **une fois par trame** après
la boucle de tics (SRUINS.C:2201, jusqu'à 4 tics :2142) ⇒ plafond **100 par trame** (mises en idle + kills),
asserté :20 ; marge ×0,8 au pire tic ×4 avec 30 kills/tic — acceptable, `nmSpawnFail`-like compteur
`nmMoves` max sur l'overlay. Aucune constante à changer.

## 3. Verbes E1M1 — `DOOM_VERBS.C`

| Verbe | Source Doom | Implémentation |
|---|---|---|
| `A_Look` | p_enemy.c:591-657 | `threshold=0` ; `t = doomSoundTarget[sprite->s]` (§3.1) : si vivant et (non-ambush ou `canSee`) ⇒ cible ; sinon `doom_lookForPlayer(this, 0)` : `canSee(sprite, camera)` (HITSCAN.C:342) + cône : `a = getAngle(dx,dz) − sprite->angle` normalisé, si \|a\| > 90° et `spriteDistApprox > F(64)` ⇒ dos tourné (:526-537) ; pas de plafond de distance (moteur : 1600 u AICOMMON.C:315). Son : posit1-3 / bgsit1-2 tirés `P_Random()%3/%2` (:611-624) ; `doom_setState(seestate)` |
| `A_Chase` | :659-767 | §3.2 |
| `A_FaceTarget` | :769-787 | `sprite->angle = getAngle(t.x−x, t.z−z)` = `PlotCourseToObject` (AICOMMON.C:100-110) ; `DF_AMBUSH` effacé |
| `A_PosAttack` | :789-806 | FaceTarget ; son `sfx_pistol` (statique 0) ; `angle += (P_Random()−P_Random())*5760` (`<<20` sur 2^32 tours = 360/4096°) ; `dmg=((P_Random()%5)+1)*3` ; `doom_lineAttack` §3.3 |
| `A_SPosAttack` | :808-830 | son `sfx_shotgn` (statique 1) ; FaceTarget ; 3 × {spread, dmg, lineAttack} sur le même `bangle` |
| `A_TroopAttack` | :900-913 | FaceTarget ; `doom_meleeRange` (:170-186 : `dist < F(64−20) + rayon cible`, `canSee`) ⇒ `sfx_claw`, `doom_damage(target, this, this, (P_Random()%8+1)*3)` ; sinon `doom_spawnMissile(this, target, MT_TROOPSHOT)` §5 |
| `A_Pain` | :1583-1587 | `doom_sound(sprite, painsound)` |
| `A_Scream` | :1541-1570 | podth1-3 / bgdth1-2 tirés ; sinon `deathsound` |
| `A_XScream` | :1578 | `sfx_slop` (statique 15) |
| `A_Fall` | :1591-1594 | `sprite->flags |= SPRITEFLAG_IMATERIAL` (plus de collision ni de hitscan : `hitSpriteP` respecte `NOHITSCAN`, HITSCAN.C:33-34) **et `this->class = CLASS_SPRITE`** : l'autoaim WALLS.C:2724-2733 ne teste que `owner->class == CLASS_MONSTER` (pas les flags) — un cadavre resté `CLASS_MONSTER` serait visé, la balle pointée vers son centre au sol finirait dans le plancher et un monstre vivant derrière un cadavre deviendrait intouchable ; inoffensif pour la diffusion `OBJECTDESTROYED` (seulement dans `delayKill`, OBJECT.C:134-138, et un cadavre n'est jamais `delayKill`é). Les verbes d'arme (SPEC_PLAYER §2.5) n'ont donc **aucun filtre** à faire. `pos.y` posé sur le sol (`findFloorDistance` UTIL.C:39) puis `radius >>= 2` (P_KillMobj p_inter.c:681 `height>>=2`) |
| `A_Explode` | :1604-1607 | `doom_radiusAttack(this, this->target, 128)` §4 |
| `A_BossDeath`, `A_KeenDie`, `A_VileChase`… | — | `NULL` dans `doomActions[]` : non appelés en E1M1 |

### 3.1 Alerte par le bruit — `doom_noiseAlert(Object *emitter, int sector)`

Flood de secteurs comme `P_RecursiveSound` (p_enemy.c:101-160) : `doomSoundTarget[MAXNMSECTORS]`
(`Object*`) + `soundTraversed[]` (char) + `validcount` incrémenté par appel. Depuis `sector`, pour chaque
mur `w` de `firstWall..lastWall` avec `nextSector >= 0` (portail, ROUTE.C:95) : bloqué si
`WALLFLAG_DOORWALL` et porte fermée (bits `WALLFLAG_BLOCKBITS` du mur posés par `setDoorBlockBits`,
AI.C:4294-4309 ; Doom : `openrange <= 0`, :126-127) ; pas d'équivalent `ML_SOUNDBLOCK` ⇒ un seul niveau.
Appelé par les verbes d'arme du joueur (hors de ce document) avec `emitter = (Object*)player`.

### 3.2 `A_Chase` en vélocité

Doom déplace `speed` u **à chaque appel** de `A_Chase` (P_Move :280-281), soit tous les `tics` de l'état
(POSS RUN 4 tics ⇒ 2 u/tic = 70 u/s ; TROO 3 tics). Ici `moveSprite` tourne à chaque tic (§2) :
**`vel = F(speed)/tics × (cos, sin)(movedir·F(45))`**, `friction = F(1)` (aucune, comme Anubis) ⇒ même
déplacement moyen, lissé. `speed` = 8 u/tic du contrat §4 (info.c:1142), aucun rescale. Par appel, dans
l'ordre Doom :

1. `reactiontime--` ; `threshold--` (ou 0 si cible morte) (:662-672).
2. Rotation : `angle` quantifié à 45°, tourne de 45° vers `movedir·45°` (:674-682).
3. Cible absente, morte ou sans `DF_SHOOTABLE` ⇒ `doom_lookForPlayer(this, 1)` sinon
   `spawnstate` (:684-691). **La cible usuelle est l'objet joueur** (`PlayerObject`, AI.C:39-58), qui n'est
   pas un `DoomActor` : `((DoomActor*)target)->health` y lirait `this->health = 700` figé (AI.C:54). Règle :
   `target == (Object*)player` ⇒ vivant = `currentState.health > 0` (SRUINS.C:962), `DF_SHOOTABLE` réputé
   vrai ; sinon `((DoomActor*)target)->health > 0 && mflags & DF_SHOOTABLE`.
4. `DF_JUSTATTACKED` ⇒ effacé, `doom_newChaseDir`, return (:693-699).
5. `meleestate && doom_meleeRange` ⇒ `attacksound`, `meleestate` (:701-708).
6. `missilestate && !movecount && doom_missileRange` ⇒ `missilestate`, `DF_JUSTATTACKED` (:710-722).
   `doom_missileRange` (:188-256) : `canSee` ; `DF_JUSTHIT` ⇒ vrai ; `reactiontime` ⇒ faux ;
   `d = dist − 64 (−128 sans mêlée)`, clamp 200, `P_Random() < d` ⇒ faux.
7. `--movecount < 0 || bloqué` ⇒ `doom_newChaseDir` (:730-734) ; « bloqué » = `collide` du dernier tic
   contient `COLLIDE_WALL` ou `COLLIDE_SPRITE` (sauf la cible). **Portes** : `COLLIDE_WALL|w` avec
   `level_wall[w].object` et `WALLFLAG_DOORWALL` ⇒ `signalObject(object, SIGNAL_PRESS, 0, 0)` (door_func
   AI.C:4313-4382, `SIGNAL_PRESS` accepté de n'importe qui si `channel == −1` :4321-4325) =
   `P_UseSpecialLine` de P_Move (:305-313) ; les routes moteur (ROUTE.C) ne servent pas.
8. `activesound && P_Random() < 3` ⇒ son (:736-740).

`doom_newChaseDir` (:353-487) : `d1 = signe(dx) si |dx| > 10`, `d2` idem en z ; liste de candidats
`dirTry[]` dans l'ordre Doom (diagonale, axe dominant, axe mineur, ancien dir, balayage des 8 dans un
sens tiré `P_Random()&1`, demi-tour), **sans sonde** (le moteur n'a pas de `P_TryMove` non destructif) :
`dirCur=0`, `movedir=dirTry[0]`, `movecount = P_Random()&15` ; un tic bloqué avance `dirCur` (latence
1 tic au lieu du choix immédiat de `P_TryWalk` :339-351). `movedir==8` ⇒ vel 0.

### 3.3 Hitscan monstre — `doom_lineAttack(DoomActor *src, int yaw, int damage)`

Œil = `pos.y − radius + F(height/2 + 8)` = **F(36)** pour height 56 (Doom `shootz = z + (height>>1) + 8*FRACUNIT`,
p_map.c:1116, 1158 ; info.c:1144). Rayon : `yaw` (angle 16.16
avec spread appliqué), `pitch = atan((cible.y − œil)/dist)` visé sur le centre de la sphère cible
(`P_AimLineAttack`), longueur `F(2048)` (MISSILERANGE p_local.h:55) ; `hitScan(src->sprite, &ray, &eye,
sprite->s, &hit, &hitSec)` (HITSCAN.H:4-5). `COLLIDE_SPRITE|i` ⇒ `doom_damage(&sprites[i], src, src,
damage)` puis `doom_spawnPuff` si `DF_NOBLOOD` de la cible (baril) sinon `doom_spawnBlood(hit, damage)`
(p_map.c:1083-1089) ; `COLLIDE_WALL/FLOOR/CEILING` ⇒ `doom_spawnPuff(hit, hitSec)` (:1043) ; 0 ⇒ rien.

## 4. Dégâts, mort, infighting, hasard

`void doom_damage(Sprite *target, Object *inflictor, Object *source, int damage)` :

| Cible | Règle | Source |
|---|---|---|
| joueur (`target==camera`) | **`doom_playerDamage(damage, source)`** (SPEC_PLAYER §1.3, remplace `playerHurt` SRUINS.C:159-178) — **aucun calcul d'armure ici** : `doom_playerDamage` est le seul à faire `saved = damage/3 ou /2` (p_inter.c:860-875), le `damageCount`, le son `plpain` et la mort. Son paramètre `hurter` = **`source`** (le tireur d'un projectile, p_inter.c:781), pas l'inflicteur |
| `DoomActor` | `signalObject(owner, SIGNAL_HURT, damage, (int)source)` (param2 = **source**, pas l'inflicteur : un projectile passe son `target`) | p_inter.c:781 |

`doom_damageActor(this, damage, source)` = P_DamageMobj :792-926 sans la poussée (`thrust`, :814-831,
pousserait la sphère dans `moveSprite` — omis en E1M1, masse 100 partout) :
1. `!DF_SHOOTABLE || health<=0` ⇒ return (cadavre).
2. `health −= damage` ; `health<=0` ⇒ **mort** : `DF_SHOOTABLE` effacé, `DF_CORPSE` (A_Fall suit dans
   les états) ; `health < −spawnhealth && xdeathstate` ⇒ `xdeathstate` (gibbe par animation XDIE, POSS
   `S_POSS_XDIE1..9`) sinon `deathstate` ; `tics −= P_Random()&3`, min 1 (P_KillMobj :668-730) ;
   `A_Scream` vient des états. Le cadavre reste (pas de `delayKill`).
3. sinon `P_Random() < painchance` (POSS 200, SPOS 170, TROO 200, info.c:1135,1161,1395) ⇒ `DF_JUSTHIT`,
   `painstate`.
4. `reactiontime = 0` ; **infighting** : `(!threshold) && source && source != this` ⇒ `target = source`,
   `threshold = 100` (BASETHRESHOLD p_local.h:58), et `spawnstate → seestate` (:910-925). La règle
   d'espèce n'est **pas** ici mais dans la collision des projectiles (p_map.c:325-343 « Don't hit same
   species as originator » ; :319-323 est le test au-dessus/au-dessous, §5) : un tir hitscan de POSS blesse
   un SPOS qui riposte ; un fireball de TROO traverse les TROO.

`doom_radiusAttack(DoomActor *spot, Object *source, int damage)` (`A_Explode`, PIT_RadiusAttack
p_map.c:1240-1280) : parcours de `sprites[0..449]` actifs (`owner` non NULL, `DF_SHOOTABLE`, joueur
inclus) ; `dist = max(|dx|,|dz|) − radius` en u, clamp 0 ; `dist < damage && canSee(thing, spot)` ⇒
`doom_damage(thing, spot, source, damage − dist)`. Un scan par explosion : 6 barils, négligeable.

**Hasard** : `rndtable[256]` (m_random.c:24-45) et `P_Random` (`prndindex=(prndindex+1)&0xff`, :50-54)
recopiés dans `DOOM_TABLES.C` ; `M_Random` idem (:56-60) pour le non-simulé ; `getNextRand` du moteur
reste pour ses propres objets. Remise à 0 dans `game_placeObject` du premier objet.

## 5. Projectiles, puff, sang

`Object *doom_spawnMissile(DoomActor *src, Object *dest, int mt)` (p_mobj.c:1121-1160) :
`doom_spawn(mt, src->sprite->s, pos = src + (0, −radius + F(32), 0), an, 0)` ; son `seesound` (firsht) ;
`target = src` ; `an = getAngle(dest−src)` ; `vel.xz = speed × (cos, sin)` avec **speed = 10 u/tic**
(info.c:1922, `speed` du contrat = 10) ; `vel.y = (dest.y − src.y) / (dist/speed)` ; puis
`P_CheckMissileSpawn` : avance d'un demi-tic (`pos += vel/2`) et impact immédiat si collision.
`newSprite` : rayon `F(6)`, `friction F(1)`, `gravity 0`, `IMATERIAL` ; classe `CLASS_PROJECTILE`.

`SIGNAL_MOVE` d'un `DF_MISSILE` : `collide = moveSprite` ; `COLLIDE_SPRITE|i` : `owner == target` ou
`((DoomActor*)owner)->mt == ((DoomActor*)target)->mt` (**espèce**, p_map.c:325-343 ; exception joueur,
KNIGHT/BRUISER hors E1M1) ⇒ ignorer ; sinon
`doom_damage(&sprites[i], this, target, (P_Random()%8+1)*damage)` (`damage` 3 → 3..24) et exploser ;
`COLLIDE_WALL/FLOOR/CEILING` ⇒ exploser. Exploser = `P_ExplodeMissile` (p_mobj.c:85-98) : `vel = 0`,
`DF_MISSILE` effacé, `doom_setState(deathstate)` (S_TBALLX1-3 : BAL1 C-E), `tics −= P_Random()&3`,
`deathsound` (firxpl). Après le dernier état, `S_NULL` ⇒ `delayKill`.

`doom_spawnPuff(MthXyz *p, int sector, int melee)` (p_mobj.c:1022-1036) : `MT_PUFF`,
`pos.y += (P_Random()−P_Random())<<10`, `vel.y = F(1)`, `tics −= P_Random()&3`, `melee` ⇒ `S_PUFF3`.
`doom_spawnBlood(p, sector, damage)` (:1049-1066) : `MT_BLOOD`, `vel.y = F(2)`, gravité, `damage 9..12 ⇒
S_BLOOD2`, `< 9 ⇒ S_BLOOD3`. Secteur = `hitSec` du hitscan (`findSectorContaining` SPRITE.H:94 sinon).

## 6. Ramassages — `DOOM_GAME.C`

**Une seule chaîne de ramassage** : `doom_item_func` (ici, la **collision**) = cas `SIGNAL_MOVE` de `thing_func`
(AI.C:3737, 3819-3820) : `collide = moveSprite` (IMMOBILE ⇒ collision seule, SPRITE.C:908-913) ;
`&sprites[collide&0xffff] == camera` et `currentState.health > 0` ⇒
**`doom_playerGetObject(this->mt, this->mflags & DF_DROPPED)`** (SPEC_PLAYER §4.3 : **effets**, son
ITEMUP/WPNUP, message `GOT*`, visage, `doomPlayer.bonusCount += 6`) ; retour 1 ⇒ `delayKill`. L'objet reste
dans `objectRunList` (§2). **État joueur = `DoomPlayer` de SPEC_PLAYER §1.2, un seul typedef dans `DOOM.H`**
(`ammo[4]`, `maxAmmo[4]`, `armorPoints/armorType`, `weaponOwned`, `keys` bits BKEY/YKEY/RKEY, `backpack`,
`bonusCount`) ; santé = `currentState.health` ; armure/clés non sauvées (E1M1). La table des effets par `SPR`
(p_inter.c:335-662) vit dans SPEC_PLAYER §4.3 — pas de `doom_touch` ici.

Le `playerGetObject` moteur (SRUINS.C:1583) n'est pas appelé. Drops (P_KillMobj :690-716 : POSS → CLIP,
SPOS → SHOT) : `doom_spawn(mt, sector, pos, 0, 0)` + `DF_DROPPED`, dans `doom_damageActor`, avec
**`pos.y = (pos.y − findFloorDistance(s, &pos)) + F(height/2)`** = sol + rayon de l'item : spawné à la position
du cadavre l'item hériterait de `pos.y` = centre de la sphère du monstre (rayon 28) et, IMMOBILE, ne subirait
jamais `internal_moveSprite` (SPRITE.C:908-913 ; `shiftSprites` ne court qu'à `placeObjects`, OBJECT.C:382)
⇒ il flotterait 18 u au-dessus du sol.

**Secteurs à dégâts** — `OT_DOOM_DAMAGE` (contrat §6, 179 ; params `sectorNm, hp`) : `game_placeObject`
pose `doomSectorDamage[sectorNm] = hp` (char[MAXNMSECTORS], 0 sinon) ; dans **`doom_playerTic`** (SPEC_PLAYER
§1.2), `if (doomSectorDamage[camera->s] && !(leveltime & 0x1f)) doom_playerDamage(doomSectorDamage[camera->s],
NULL)` = `P_PlayerInSpecialSector` (p_spec.c, special 7 : 5 hp / 32 tics ; pas de combinaison anti-radiation en
E1M1). ~20 lignes ; sans cela les secteurs 13, 55, 57, 61 (nukage) ne blessent pas — écart visible avec Mimas.

## 7. Rotations

`SIGNAL_VIEW` est envoyé **à chaque dessin** aux sprites à `owner` (WALLS.C:2694-2695) : le handler
recalcule `sprite->sequence = doom_seq(spr, frame, view)` avec la formule du contrat §2
(`stride = map[spr]&0x8000 ? 8 : 1`, `view = getFacingAngle(sprite, camera)` AICOMMON.C:73-98, 0..7,
vue k = rotation Doom k+1). `doom_setState` recalcule avec la vue courante pour qu'un objet jamais dessiné
ait une séquence valide (`sequence ≠ -2`, asserté SPRITE.C:778). Le miroir est **dans le .LEV** (vue 7 =
tuile de la vue 1 + flag 1, WALLS.C:2789-2791) : le runtime n'y touche pas. `sprite->frame = 0` toujours
(une frame par séquence) ; `spriteAdvanceFrame` jamais appelé. `fullbright` (contrat §4) : ignoré en E1M1
(la lumière est par sommet, AICOMMON.C:50-70 ; à traiter avec `OT_DOOM_LIGHT`).

## 8. Sons d'acteur

`int doom_sfxIndex(int sfx)` — **défini une seule fois, dans `DOOM_SOUND.C` (SPEC_PLAYER §4.1)**, déclaré dans
`DOOM.H` : `sfx` dans la liste statique (`doomStaticSfx[20]`, contrat §3-§4) ⇒
`level_staticSoundMap[ST_JOHN] + n` ; sinon `level_objectSoundMap[sfx]` (déjà décalé de `nmStaticSounds`,
SOUND.C:243-249) ; `< 0` ⇒ −1 (absent). `void doom_sound(Sprite *s, int sfx)` (même fichier) : index ≥ 0 ⇒
`spriteMakeSound(s, idx)` (macro → `posMakeSound`, SPRITE.H:96 : volume/pan SOUND.C:392-421) ;
`s == NULL` (ramassages, `S_StartSound(NULL,…)`) ⇒ `playSound(0, idx)` (SOUND.H:26). Ce document ne fait
qu'appeler ces deux fonctions.

## 9. Touch points moteur (tous sous `GP_GAME_DOOM`, macros sur la ligne existante — règle SPRITE.H:105-110 sur `__LINE__`)

| Fichier:ligne | Aujourd'hui | Sous `GP_GAME_DOOM` |
|---|---|---|
| SPRITE.H:129 (après le bloc `PLAYER_MODEL` :113-128) | — | bloc `#ifdef GP_GAME_DOOM` : `CFG_TIC_ADD(v) ((v)*7)`, `CFG_TIC_UNIT 12`, `CFG_TIC_CAP 48`, `CFG_PLACE(t) game_placeObject(t)`, `CFG_PLAYER_TIC() doom_playerTic()`, `CFG_DOOR_FIT GP_PLAYER_FIT_HEIGHT`, `CFG_SPRITE_NEARCLIP F(10)`, `CFG_LIFT_RESET(o) if ((o)->channel!=-1) signalAllObjects(SIGNAL_SWITCHRESET,(o)->channel,0)` ; `#else` identités (`(v)`, 2, 8, 0, vide, 80, `F(32)`, vide) |
| SRUINS.C:2139-2140 | `if (monsterMoveCounter>8) monsterMoveCounter=8;` | `CFG_TIC_CAP` |
| SRUINS.C:2142 | `for (;monsterMoveCounter>1;monsterMoveCounter-=2)` | `for (;monsterMoveCounter>=CFG_TIC_UNIT;monsterMoveCounter-=CFG_TIC_UNIT) { CFG_PLAYER_TIC(); …` (**horloge unique**, §1 : joueur puis acteurs) |
| SRUINS.C:2196 | `for (;mmcSave>1;mmcSave-=2)` | idem `CFG_TIC_UNIT` |
| SRUINS.C:2302 | `monsterMoveCounter+=vtimer;` | `+=CFG_TIC_ADD(vtimer);` |
| SRUINS.C:159-178 (`playerHurt`) | `health −= hp`, ouch (`3+i` :174) | **non compilé** : remplacé par `doom_playerDamage` (SPEC_PLAYER §1.3, §5) — plus de `CFG_OUCH` |
| **AI.C:4304** (`setDoorBlockBits`) | `if (level_vertex[…v[1]].y-level_vertex[…v[2]].y<80)` | `<CFG_DOOR_FIT` (**56**) — bloquant : joueur `SPRITEFLAG_BSHORT` (AI.C:47) refusé par `bumpSectorBoundries` (SPRITE.C:414) sur tout DOORWALL < 80 ; portes d'E1M1 = 68 u ouvertes (contrat §6) |
| **AI.C:4660-4663** (`elevator_func`, retour en haut `direction==1 && offset>=0`) | `pbObject_moveTo(this,0); wait=1;` | `+ CFG_LIFT_RESET(this);` — réarme le `OT_SECTORSWITCH` (AI2.C:654-661) pour l'ascenseur WR (contrat §6) |
| AI.C:4673 | `+1` à l'arrêt d'ascenseur | `+3` (PSTOP, contrat §3 ; listé aussi SPEC_PLAYER §5) |
| **WALLS.C:2701** | `if (tformed.z<F(32)) continue;` | `<CFG_SPRITE_NEARCLIP` (F(10)) : un CLIP/BON1/MEDI (height 16, rayon 8) est ramassé à 16 + 8 = 24 u de la caméra et disparaîtrait à 32 u, 8 u **avant** le ramassage — pop visible sur les 53 ramassages ; `NEAR_CLIP` 10 (doom.cfg:34) est la cohérence |
| OBJECT.C:212 | `assert(level_object[o].firstParam==objectPPos);` | `… ; if (CFG_PLACE(level_object[o].type)) continue;` (crochet contrat §1) |
| AICOMMON.C | — | **aucune modification** : `getFacingAngle`, `findPlayer`, `PlotCourseToObject` sont publics (AICOMMON.H:15-24) |
| AI.C:3819-3836 | `thing_func` | **non modifié** (`doom_item_func` séparé, §6) |
| Makefile:164 / après :174 | `MAIN_C := …` | `GAME_C`, `MAIN_C += $(GAME_C)`, `vpath %.C game/doom`, `CDDIR := cd_doom`, règle `doom_art.h` (contrat §8) |
| **BIGMAP.C:84-86** (`getLevelName`) | `return levelGraph[lNm].levelFile;` | `return doomLevelNames[lNm];` (`DOOM_GAME.C`, contrat §9, borné `DOOM_NMLEVELS`) |
| **SRUINS.C:2491-2500** (`runMap`) | `level=runMap(currentState.currentLevel);` | `level=0;` (warp direct, comme `TESTCODE` :2499) |
| **INITMAIN.C:185-246** | logos + `OPEN.MOV` | sautés (`#ifndef GP_GAME_DOOM`) |
| `exit_func` (`DOOM_GAME.C`) | — | `next = currentLevel+1 ; next < DOOM_NMLEVELS ? playerHitTeleport(next) : playerHitTeleport(2−200)` (action 2 = quit → intro, SRUINS.C:2544-2546 ; contrat §6/§9) |

## 10. Signatures publiques — `game/doom/DOOM.H`

```c
/* DOOM_ACTOR.C */
void        game_actor_func(Object *this, int message, int param1, int param2);
DoomActor  *doom_spawn(int mt, int sector, MthXyz *pos, int angle, int thingFlags);
void        doom_setState(DoomActor *this, int state);
short       doom_seq(int sprite, int frame, int view);
void        doom_damage(Sprite *target, Object *inflictor, Object *source, int damage);
void        doom_damageActor(DoomActor *this, int damage, Object *source);
void        doom_radiusAttack(DoomActor *spot, Object *source, int damage);
void        doom_noiseAlert(Object *emitter, int sector);
Object     *doom_spawnMissile(DoomActor *src, Object *dest, int mt);
void        doom_spawnPuff(MthXyz *pos, int sector, int melee);
void        doom_spawnBlood(MthXyz *pos, int sector, int damage);
int         doom_lineAttack(DoomActor *src, int yaw, int damage);   /* retour = code hitScan */
/* DOOM_VERBS.C — verbes d'acteur ET d'arme (SPEC_PLAYER §2.2), une seule table */
typedef union { void (*mobj)(DoomActor *this); void (*psp)(DoomPlayer *p, int ps); } DoomAction;  /* contrat §4 */
extern const DoomAction doomActions[];       /* membre choisi par doomStates[].flags bit 1 */
int         doom_lookForPlayer(DoomActor *this, int allaround);
int         doom_meleeRange(DoomActor *this);
int         doom_missileRange(DoomActor *this);
void        doom_newChaseDir(DoomActor *this);
/* DOOM_GAME.C */
int         game_placeObject(int ot);
void        doom_item_func(Object *this, int message, int param1, int param2);   /* collision → doom_playerGetObject (SPEC_PLAYER §4.3) */
extern unsigned char doomSectorDamage[MAXNMSECTORS];                             /* OT_DOOM_DAMAGE, §6 */
extern const char *doomLevelNames[DOOM_NMLEVELS];
/* DOOM_SOUND.C (SPEC_PLAYER §4.1) : int doom_sfxIndex(int sfx); void doom_sound(Sprite *s,int sfx);
   DOOM_PLAYER.C (SPEC_PLAYER §1.2-1.3) : typedef DoomPlayer ; extern DoomPlayer doomPlayer ;
   void doom_playerTic(void); void doom_playerFrame(...); void doom_playerDamage(int damage,Object *source);
   int doom_playerGetObject(int mt,int dropped);
   DOOM_TABLES.C (généré) : doomStates[], doomMobjInfo[], doomStaticSfx[20], doomOtToMt[227],
   doomMtToOt[137], rndtable[256] ; int P_Random(void), M_Random(void); */
```

## 11. Ordre d'implémentation (tâches ≤ 1 j)

| # | Tâche | Vérification PC | Console (vu → sens) |
|---|---|---|---|
| T1 | `info2tables.py` → `DOOM_TABLES.C` + `doom_ids.json` ; Makefile `GAME_C` ; `DOOM.H` unique (contrat §8) | compile ; `assert(sizeof(DoomState)==8)`, **`sizeof(DoomMobjInfo)==44`** dans `doom_init` ; `doomStates[S_POSS_RUN1].tics==4` ; `doomStates[S_PISTOL1].flags & 2` (verbe d'arme) | disque boote comme avant (tables inertes) |
| T2 | SPRITE.H macros + SRUINS.C cadence 35 Hz + **AI.C:4304 `CFG_DOOR_FIT`** + AI.C:4660 `CFG_LIFT_RESET` + WALLS.C:2701 | compile ; build `gameparams.cfg` inchangé **octet pour octet** (`cmp` des .BIN) | porte : ouverture visiblement plus rapide (70 u/s) ; ascenseur idem ; **le joueur franchit les 4 portes d'E1M1** |
| T3 | `game_placeObject` + `doom_spawn` (moveObject, scale 65536, NULL) + `game_actor_func` MOVE/VIEW/DESTROYED, `doom_setState`, `doom_seq` | `verif_doom.py` : **5 shorts joueur + 6 × 115 mobjs**, `firstParam` cumulés ; assert `sizeof(DoomActor) < sizeof(Object)` ; assert `seq < nmSequences` sur tout état atteignable ; `nmSpawnFail == 0` | 124 things visibles à leur place **et à la bonne taille** (un POSS = 56 u, la hauteur d'une porte moins 12), animés (BON1 pulse), monstres en STND tournés vers leur angle, rotations correctes en tournant autour |
| T4 | `A_Look`, `doom_lookForPlayer` (sans bruit) ; appels à `doom_sound` (DOOM_SOUND.C, SPEC_PLAYER 2) | assert index < `nmSounds` pour les 36 sons | POSS/SPOS/TROO crient (posit/bgsit) en voyant le joueur, passent en RUN |
| T5 | `A_Chase` vélocité + `doom_newChaseDir` + ouverture des portes | **simulateur Python** `tools/doom2ps/sim_chase.py` : `doom_newChaseDir`/`A_Chase` rejoués sur le graphe de feuilles d'E1M1 (`adjacency.py`) avec `rndtable` — trajectoire d'un POSS de la salle de départ vers le joueur en N tics, égale à celle de Mimas (même `prndindex` de départ) ; 0 traversée de mur | ils avancent à ~70 u/s, contournent, ouvrent la porte du secteur 4 ; ne traversent pas les murs |
| T6 | `doom_damage/damageActor`, `rndtable`, pain/death/xdeath, `A_Fall` cadavre (`CLASS_SPRITE`), HURT depuis WEAPON.C:582 | compile ; assert `health` cohérent ; PC : histogramme `P_Random() < painchance` sur 256 tirages = 200/256 | pistolet moteur tue un POSS en 2 tirs (15 hp × 2 ≥ 20), pain 78 %, cadavre traversable, XDIE à ≥ 40 dégâts (baril) |
| T7 | `doom_lineAttack`, `A_PosAttack/SPosAttack`, puff/blood ; branche joueur de `doom_damage` → `doom_playerDamage` | **histogrammes PC** des dégâts `A_PosAttack` (3·(1..5)) et `A_SPosAttack` (3 balles) sur `rndtable` = Doom (m_random.c) ; spread ±5,6° max | impacts au mur (PUFF) et sang sur les monstres ; santé HUD baisse ; spread visible |
| T8 | `doom_spawnMissile`, projectile, `A_TroopAttack`, espèce | PC : table `vel` (10 u/tic) et temps de vol vers une cible à 512 u = 51 tics ; dégâts `(P_Random()%8+1)·3` histogramme = Doom | fireball BAL1 vole à 10 u/tic, explose BAL1 C-E au mur/joueur ; TROO à < 60 u griffe |
| T9 | `A_Explode`, `doom_radiusAttack` ; infighting | **table PC** `doom_radiusAttack` (dist → dégâts, `128 − dist`) pour les 6 barils d'E1M1 : voisins touchés, LOS `canSee` rejoué sur la géométrie | baril blesse à 128 u, tue un POSS voisin qui ne riposte pas au baril mais au tireur ; POSS touché par un SPOS le poursuit |
| T10 | `doom_item_func` (collision) → `doom_playerGetObject` (SPEC_PLAYER 5), drops au sol, `OT_DOOM_DAMAGE` | PC : rayon de collision 16 + 8 = 24 u > `CFG_SPRITE_NEARCLIP` ; `doomSectorDamage` non nul sur toutes les feuilles des secteurs 13/55/57/61 | ramassage avec son ITEMUP, disparition, +ammo/+santé ; refus à 100 % santé ; nukage −5 toutes les ~0,9 s |
| T11 | `doom_noiseAlert` depuis les verbes d'arme joueur | **flood PC** `doom_noiseAlert` rejoué en Python sur les secteurs/portails d'E1M1 : salle de départ atteinte, secteur 4 derrière porte fermée non | un tir réveille les POSS de la salle de départ derrière le pilier, pas ceux derrière la porte fermée |

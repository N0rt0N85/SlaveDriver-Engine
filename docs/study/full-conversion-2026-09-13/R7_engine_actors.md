# R7 — Système d'acteurs, monstres et IA de SlaveDriver (lecture du 2026-09-14)

Rapport brut d'un lecteur (révision v2 du plan, après les réserves owner). Racine du dépôt, variantes
FLASH/OLDJAP/JEFF ignorées. `AIRBUB.C` n'est pas de l'IA : c'est le bitmap `meter_back[]` du compteur
d'air (AIRBUB.C:1).

## 1. Comment un acteur « pense »

| Point | Fait | Réf. |
|---|---|---|
| Représentation | `Object` = `{type, class, next, prev, messHandler func, pad[128]}` ; chaque sous-type (`MonsterObject`, `ProjectileObject`…) redéfinit le préfixe et remplit le pad (`assert(sizeof(*this)<sizeof(Object))`) | OBJECT.H:31-38, AI.C:2584 |
| Dispatch | **pointeur de fonction par objet + switch sur un message** : `object->func(object,message,p1,p2)` ; messages `SIGNAL_MOVE/ENTER/PRESS/VIEW/HURT/OBJECTDESTROYED/SWITCH…` | OBJECT.C:95-105, OBJECT.H:5-17 |
| Listes | 3 listes doublement chaînées à tête : `objectRunList` (pensent), `objectIdleList` (items/props — pas de MOVE), `objectFreeList` ; pool statique `MAXOBJECTS 350` | OBJECT.C:10-14, 56-75 |
| Appel | `runObjects()` = `signalList(objectRunList,SIGNAL_MOVE)` + `aicount++` | AICOMMON.C:461-464 |
| Cadence | **tic fixe 30 Hz** : `monsterMoveCounter += vtimer` (vblanks), boucle `for(;mmc>1;mmc-=2) runObjects()`, plafonné à 8 (4 tics max/frame) | SRUINS.C:2139-2151, 2302 |
| Throttle | `AISLOT(mask)` : décision toutes les 16 tics (`0xf`), scan joueur toutes les 32 (`0x1f`), slot = `nextAiSlot++` par monstre | AICOMMON.C:23, 306, 372 ; AI.C:2596 |
| Mort différée | `delayKill` → `SIGNAL_OBJECTDESTROYED` broadcasté aux deux listes (purge des pointeurs `enemy`), retour au free list à `processDelayedMoves()` en fin de frame | OBJECT.C:112-128, SRUINS.C:2229 |

**Verdict : générique** (bus de messages + listes), mais *aucune* table d'états : la logique est dans
chaque `xxx_func`.

## 2. Monstres : un bloc par monstre

| Monstre (`_func`+`construct`) | AI.C | ~l. | Monstre | AI.C | ~l. |
|---|---|---|---|---|---|
| Spider | 462-608 | 146 | Anubis | 2481-2624 | 143 |
| Fish | 608-760 | 152 | Selkis | 2624-2862 | 238 |
| GlowWorm | 1145-1352 | 207 | Set | 2862-3039 | 177 |
| Hawk | 1490-1631 | 141 | Sentry | 3117-3295 | 178 |
| Wasp | 1631-1790 | 159 | Mummy | 3295-3426 | 131 |
| Magmantis | 2274-2481 | 207 | Bastet | 3426-3668 | 242 |
| Blob | 4024-4100 | 76 | Queen / Qegg / Qhead | 5146-5718 | 572 |
| BlowPot (pot, `CLASS_MONSTER`) | AI2.C:685-858 | 173 | | | |

≈ 2 900 lignes pour 17 types. Chaque monstre = `enum` d'états + `unsigned short xxxSeqMap[]`
(état → séquence de base, bit `HB=0x8000` = « ajouter la rotation ») + un `switch(this->state)`
codé en dur (AI.C:2470-2479, 2524-2570). Les 6 premiers états sont normalisés
(`STATE_IDLE/WALK/SRA/LRA/HIT/SEEK`, AICOMMON.H:56) et servis par des briques génériques :

| Brique | Rôle Doom équivalent | Réf. |
|---|---|---|
| `normalMonster_idle(this,speed,icuSound)` | `A_Look` (findPlayer ≤1600 u + son « je te vois ») | AICOMMON.C:305-325 |
| `decideWhatToDo(this,route,floater)` → `DO_CHARGE/FOLLOWROUTE/RANDOMWANDER/GOIDLE` | cœur de `A_Chase` | AICOMMON.C:328-366 |
| `normalMonster_walking(this,collide,fflags,speed)` | `A_Chase` (morsure au contact, rebond mur, 1/2 tir LRA) | AICOMMON.C:368-423 |
| `monster_seekEnemy` + `followRoute` | poursuite sans LOS via route | AICOMMON.C:259-303, 213-253 |
| `PlotCourseToObject`, `spriteHome` | `A_FaceTarget`, homing | AICOMMON.C:100-112, 172-186 |

**Verdict : hybride** — automate codé en dur par monstre, mais ~40 % du corps est la machine
générique d'AICOMMON.C.

## 3. Animation

| Point | Fait | Réf. |
|---|---|---|
| Données | `level_sequence[seq]` = index de première frame ; `sFrameType{chunkIndex,flags,sound}` ; `sChunkType{chunkx,chunky,tile,flags(1=flipX,2=flipY)}` | SLEVEL.H:229-254, SEQUENCE.C:23-84 |
| Sprite | `Sprite{sequence,frame,…}` ; `level_sequenceMap[type]` = base de séquences du type | SPRITE.H:43-57, SEQUENCE.C:62 |
| Avance | `spriteAdvanceFrame(sprite)` : **1 frame par tic, aucune durée** ; joue `frame.sound`, retourne `flags` ∣ `FRAMEFLAG_ENDOFSEQUENCE` au bouclage (frame revient à 0) | SPRITE.C:775-791 |
| État → séquence | `setState(this,state)` : `sequence = map[type] + (seqMap[state]&0x7fff)`, `frame=0` | AICOMMON.C:255-262 |
| Rotation | `setSequence` (sur `SIGNAL_VIEW`, envoyé **au dessin** par WALLS.C:2695) ajoute `getFacingAngle(sprite,camera)` ∈ 0..7 → **8 séquences par état** | AICOMMON.C:73-98, 148-160 |
| Action à une frame | **pas de callback** : soit test `frame==5 \|\| frame==18` (griffe), soit bit de frame `FRAMEFLAG_FIRE 0x80` (Mummy), soit `ENDOFSEQUENCE` | AI.C:2535, 3363-3365 ; SLEVEL.H:238-239 |
| Bug notable | `if (fflags && FRAMEFLAG_ENDOFSEQUENCE)` (`&&` au lieu de `&`) ×5 | AI.C:2528, 2671, 2905, 3162 |

**Verdict : données** pour sprite↔frame↔tuile ; **codé en dur** pour le déclenchement d'actions.

## 4. Projectiles

| API | Réf. |
|---|---|
| `initProjectile(from,to,&pos,&vel,height,speed2)` : vel = (to−pos)/(dist>>speed2), pos avancée de 8 pas | AICOMMON.C:426-443 |
| `constructGenproj(sector,pos,vel,owner,heading,flat,type,damage,explosionColor,r,g,b,armTime)` — projectile paramétré (lumière dynamique, poof à l'impact) | AI.C:119-161 |
| `genproj_func` MOVE : `moveSprite` → `COLLIDE_SPRITE` & owner≠ → `signalObject(owner,SIGNAL_HURT,damage,this)` ; mur/sol/plafond → `constructOneShot(OT_POOF)` + `delayKill` | AI.C:81-115 |
| Spécifiques : anuball, ringo, flameball, cobra, badCobra, zap, sball, cloud (dégât retardé sur cible), grenade | AI.C:1790, 1867, 2032, 760, 987, 1352, 3039, 4237, 2182 |
| **Radius** : `radialDamage(this,center,sector,damage,radius)` — flood de secteurs par distance plan-mur, `dam = damage·(1−d/r)`, push de vélocité, `SIGNAL_HURT` à tout `CLASS_MONSTER` (joueur inclus) ; **pas de test LOS** | OBJECT.C:377-449 ; grenade : 180 hp / 300 u AI.C:2226 |

**Verdict : générique** (`constructGenproj` + `radialDamage`).

## 5. Hitscan

`int hitScan(Sprite *dontHit, MthXyz *ray, MthXyz *pos, int sector, MthXyz *outPos, int *outSector)`
(HITSCAN.C:185-247) : marche de secteur en secteur, teste d'abord les sphères sprites (`hitSpriteP`,
rayon `radius2`, `SPRITEFLAG_NOHITSCAN`, HITSCAN.C:22-70), puis murs verticaux, puis sols/plafonds ;
retourne `COLLIDE_SPRITE|idx` / `COLLIDE_WALL|w` / 0 (parallaxe). `canSee(s1,s2)` = variante murs
seuls (`WALLFLAG_BLOCKSSIGHT`) HITSCAN.C:342-373.
Utilisé par le joueur : `WEAPON.C:574` → `hurtSprite(…,player,15/20)`. **Autoaim 3D** : `autoTarget`
élu au dessin (monstre à ±20 px du centre, WALLS.C:2724-2732), rayon dirigé dessus avec jitter M60
(WEAPON.C:273-315). **Aucun monstre n'utilise hitScan** (seul le `shooter` laser AI.C:6167).

## 6. LOS, alerte, poursuite, douleur, mort

| Sujet | Fait | Réf. |
|---|---|---|
| Détection | `findPlayer(sprite,dist)` = distance approx + `canSee` ; **pas d'alerte par bruit**, pas de cône | AICOMMON.C:130-138 |
| Poursuite | BFS sur secteurs, route de 8 portes, exclut `NOTSTEPABLE`/`DOORWALL`/bits de blocage du sprite (→ **les monstres ne franchissent jamais une porte**) | ROUTE.C:44-118, 97-100 ; UTIL.H:23 |
| Douleur | `SIGNAL_HURT` → `monsterObject_signalHurt` (flash, `health-=`, retarget) ; état HIT + `stunCounter=20` **sans painchance** | AICOMMON.C:184-203 ; AI.C:2503-2508 |
| Mort | zorch + `constructOneShot(OT_LANDGUTS)` + `makeExplosion(this,seqBase,4)` (gibs = `constructBit`, plafond 20) + son + `delayKill` → **pas de cadavre** | AI.C:2490-2500, 228-234, 196-199 |
| Infighting | oui, réactif : `enemy=hurter` (ou owner du projectile) ; jamais spontané | AICOMMON.C:191-197 |

## 7. Dégâts et santé

Fonction centrale = message `SIGNAL_HURT(hp, hurter)` (`hurtSprite` OBJECT.C:372-375). **Aucune
table** : santé et dégâts sont des littéraux (Anubis 100 hp AI.C:2593, Mummy 130 AI.C:3407, joueur 700
AI.C:56 ; griffe 20 AI.C:2543). Joueur : `player_func` → `playerHurt(hp)` — pas d'armure, pas d'état
douleur (AI.C:31-36, SRUINS.C:159-178).

## 8. Spawn depuis le .LEV

`sObjectType{type,firstParam}` + flux `level_objectParams` (LEVEL.C:55,61) ; `placeObjects()` =
**switch géant `OT_*` → `constructXxx(suckShort()…)`** (OBJECT.C:186-364) ; monstres lisent `secteur`
puis `suckSpriteParams` (x,y,z,angle·5760) OBJECT.C:171-183. Spawn runtime : Selkis→spider
AI.C:2792, Queen→egg AI.C:5420. **Aucun respawn** (grep vide). Pools : 350 objets, `MAXNMSPRITES 450`
(SPRITE.C:12), bits 20.

## 9. Sons

`spriteObject_makeSound(this,n)` → `level_objectSoundMap[type]+n` (AICOMMON.C:165-169) →
`posMakeSound(source,pos,snd)` : volume = dist>>5−15 (coupé à 255), pan = angle replié sur ±90°
(SOUND.C:392-421). Sons de frame automatiques dans `spriteAdvanceFrame`. **Générique**.

## Conclusion : héberger `states[]` + `A_*` de Doom

**(a) Services réutilisables tels quels**

| Service Doom | Ici |
|---|---|
| `P_RunThinkers` | `runObjects` / listes OBJECT.C (ajouter `tics` décrémenté) |
| `P_SpawnMobj` | `getFreeObject` + `newSprite(sector,radius,friction,gravity,seq,flags,owner)` SPRITE.C:68 |
| `P_XYMovement/ZMovement/TryMove` | `moveSprite` → `COLLIDE_*` SPRITE.C:908 ; flags `BCLIFF/BWATERBNDRY` |
| `P_SpawnMissile` | `initProjectile` + `constructGenproj` |
| `P_AimLineAttack/LineAttack` | `hitScan` (sphères + murs, `dontHit`) |
| `P_CheckSight` | `canSee` |
| `P_RadiusAttack` | `radialDamage` |
| `P_DamageMobj` | `SIGNAL_HURT` + `monsterObject_signalHurt` (retarget inclus) |
| `S_StartSound(mo,…)` | `spriteObject_makeSound` |
| `A_Look/A_Chase/A_FaceTarget` | `normalMonster_idle/walking`, `PlotCourseToObject`, `plotRouteToObject` |
| Rotations, gibs, poof | `getFacingAngle`, `makeExplosion`, `constructOneShot` |

**(b) Ce qui manque ou diffère**

| Manque | Coût |
|---|---|
| Table d'états avec durée `tics` et pointeur d'action (le moteur n'a que 1 frame/tic + bit FIRE) | cœur du travail |
| Alerte sonore par propagation (`P_NoiseAlert`) : à écrire en flood de secteurs (patron déjà dans `radialDamage`/ROUTE.C) | ~60 l. |
| Attaque hitscan monstre (`A_PosAttack` etc.) : `hitScan` existe, jamais appelé par un monstre | ~40 l. |
| Cadavres / `A_Fall` : les monstres sont libérés à la mort ; garder l'objet en état mort sans collision | ~50 l. |
| Portes : les routes évitent `DOORWALL` — Doom ouvre les portes (`SIGNAL_PRESS` sur l'objet porte) | ~60 l. |
| `mobjinfo` (santé, painchance, speed, mass, sons) : tout est littéral | données |
| Painchance, `reactiontime`, `threshold`, respawn nightmare, armure joueur | ~120 l. |
| Mouvement 8 directions pas-à-pas (`P_Move`) vs vélocité continue + friction : réécrire `A_Chase` en vélocité | ~150 l. |
| 8 rotations = 8 séquences par état (Doom : 5 + miroir) → le convertisseur duplique via `chunk.flags&1` | convertisseur |
| 30 Hz vs 35 Hz : rescaler `tics` (×6/7) ou changer le pas SRUINS.C:2142 | trivial |
| Pools : 350 objets/450 sprites/20 gibs — insuffisant pour les grosses maps Doom | constantes |
| `radialDamage` sans LOS, `SIGNAL_HURT` sans espèce (pas de règle « même type ») | ~20 l. |

**(c) Estimation**

| Volet | Lignes | Jours |
|---|---|---|
| Service moteur : `doom_monster_func` générique (états+tics+action, MOVE/HURT/VIEW/DESTROYED), noise alert, cadavres, portes, hitscan monstre, painchance/reaction, respawn, pools | ~700 | 8-10 |
| Verbes d'action : ~50 `A_*` monstres (`A_Chase` adapté ~150 l., reste 10-25 l. chacun ; `A_VileChase/A_PainAttack/A_SkullAttack/A_Tracer/A_BrainSpit` ~300 l.) | ~900 | 8-10 |
| Données : `states[]`/`mobjinfo[]` générés depuis `info.c` par script + `seqMap` 8 rotations | générées | 3-5 |
| Débogage sur console | — | 5 |
| **Total** | **~1 600 l. de C + tables générées** | **≈ 25-30 j** |

Le moteur fournit déjà tous les *services* d'un `A_*` Doom ; ce qui manque est l'*ossature* (états à
durée + callback) et trois comportements de niveau (bruit, portes, cadavres).

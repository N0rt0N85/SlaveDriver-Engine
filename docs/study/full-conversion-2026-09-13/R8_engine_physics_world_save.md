# R8 — Physique, monde mobile, joueur, sauvegarde de SlaveDriver (lecture du 2026-09-14)

Rapport brut d'un lecteur (révision v2 du plan). Unités partout : `Fixed32` 16.16 (`F()`), longueurs
en « unités Saturn » u (8 unités Build = 1 u, gameparams.cfg:11), sommets en `short` (SLEVEL.H:227),
vitesses en u/frame (60 Hz NTSC ; PAL multiplie pos/vel par 1,2 à SPRITE.C:469-477).

## 1. Mouvement/collision d'un acteur (SPRITE.C)

| Élément | Où | Fait |
|---|---|---|
| Pas de mouvement | `moveSprite` :908 = `doFriction` :480 → `internal_moveSprite` :459 → `collideSprite` :569 ; joueur = `moveCamera` :902 ; `moveSpriteTo` :916 = téléport (change de liste secteur, sans collision) |
| Gravité | `vel.y -= sprite->gravity` chaque frame, ÷2 sous l'eau :460-463 ; valeur **par sprite** (`newSprite` :68) ; joueur `GRAVITY=6<<12`=F(0,375) u/frame² (AICOMMON.C:21, AI.C:46) |
| Friction | `vel.x/z *= sprite->friction` chaque frame sauf pente glissante :480-487 ; joueur 0,90 (AI.C:46) |
| Forme | **sphère** de `radius` (SPRITE.H:46) : murs `bumpWall` :120 (repousse le long de la normale :293-296, annule la composante normale de vel :298-304 ⇒ glissement), arêtes :213-268, sols/plafonds `bumpFloor` :336, sprites `collideSpriteSprite` :490 (sphère-sphère, push + annulation vel) |
| Filtrage | `(wall->flags & sprite->flags) & WALLFLAG_BLOCKBITS` :414,435 — 5 classes de blocage (SLEVEL.H:249-254) |
| Marche | `STEPHEIGHT = SPR_STEP` = `GP_PLAYER_STEP` 32 u :597, F(1) sous l'eau :594 ; sols admis si `≤ y+STEP` :369-371 |
| Pose au sol | snap complet ou ÷8 :650-657 ; atterrissage `vel.y=0` ou rebond BOUNCY :684-687 ; pente >~24° = glissante :653 |
| Secteur | suivi par franchissement de portails :217-223,277-281 ; `SIGNAL_ENTER` :731-733 |
| Retour | `COLLIDE_SPRITE/WALL/FLOOR/CEILING | index` :745-758 |

**Verdict** : paramétrable. Rayon/hauteur/marche/hover du joueur = clés `GP_*` (gameparams.cfg:19-40,
SPRITE.H:111-129) ; friction/gravité = arguments de `newSprite` par acteur. Câblé : marche sous l'eau
F(1), snap ÷8, seuil de pente 60000.

**PLRCYL.C (non commité)** : modèle « cylindre » — `pos.y` = œil, pieds à `−cylFoot`, tête à
`+cylHead` (PLRCYL.H:105-108) ; postures debout/accroupi/rétréci/nage avec vérification de place et
écrasement (PLRCYL.C:70-104) ; remplace les macros `SPR_*` (PLRCYL.H:114-126). C'est **exactement le
modèle Doom** (rayon 16, hauteur 56, œil 41 → 3 clés).

## 2. Joueur (SRUINS.C)

| Sujet | Où | Valeur |
|---|---|---|
| Boucle | `movePlayer` :857 — une itération **par échantillon vblank** (`inputQ`, V_BLANK.C:89-90,153-154) ⇒ 60 Hz fixe |
| Avance | force = (sin·`RUNVELOCITY` 96)>>2 :510-518 ; strafe 60 :545-555 ; `vel=(15·vel+force)>>4` :633-634 puis ×0,9 |
| Tourner | ±`RTURNVELOCITY` 12000/frame, plafond 200000 (≈3°/frame) , friction 15000 :578-606 ; angles en degrés 16.16 :1029-1030 ; roulis = −yavel :669 |
| Vue haut/bas | `xavel` ±10000, clamp ±90°, recentrage auto :450-462 |
| Saut | oui : F(4,875), sandales F(7,5) :100-101,609-620 ; bouton maintenu = +3<<12/frame (gravité réduite) :611 ; chute max −80 u/frame :653 |
| Nage | `underWaterControl` :249, gravité 0 :265 ; flag via `SECFLAG_WATER` (SPRITE.C:736) ; splash (SPRITE.C:541) |
| Dégâts | `playerHurt` :159 → `currentState.health`, flash rouge ; lave/marais `playerLongHurt` :181 (20 hp × 30 tics) ; **chute** `playerDYChange` :236 : >15 u/frame ⇒ nmBowls·(dv−12)² |
| Mort | :961-975 ; caméra qui tombe :887-903 ; retour 1 :2321 ⇒ niveau relancé :2542 |

## 3. Armes (WEAPON.C)

- 8 armes (GAMESTAT.H:27), munitions max :63-73, stock dans `currentState.weaponAmmo[]`.
- Sélection : `weaponUp/Down` par bit-scan du masque inventaire :973-993 ; `desiredWeapon` ⇒
  `weaponOut/In` :815-829, 15 frames de latence :824.
- Cadence = longueur de la séquence d'animation : `fireWeapon` :141 si file vide et FIRE tenu
  (SRUINS.C:977-980), munition−− :260 ; le frame porteur de `FRAMEFLAG_FIRE` appelle `weaponFire`
  :920 ⇒ `hitScan` pour pistolet/M60/épée :574-626 (dégâts 15/20/20 via `hurtSprite`) ; projectiles
  `constructCobra/Flameball/Grenade/Ringo/Zap` :470-560.
- Autoaim : `getAutoAimRay` :273-315 sur `autoTarget`.
- Bob : ressort `weaponPos/Vel` :118-123 ; marche (SRUINS.C:657-665).
- **Ramassage** : `thing_func` — `moveSprite` retourne `COLLIDE_SPRITE == camera` ⇒
  `playerGetObject(type)` (AI.C:3819-3836) ; table type→effet SRUINS.C:1583-1799 (retour 0 = refus si
  plein). Clés = `keyMask` global (:105), remis à 0 par niveau (:2024), **non sauvé**.

## 4. Monde mobile

Il n'existe **pas de hauteur de secteur** : sols/plafonds sont des `sWallType` (normal[1]≠0) dont les
hauteurs vivent dans les sommets. Un **push block** (`sPBType`, SLEVEL.H:208-215) = plage de sommets +
murs + `floorSector`.

| API | Où |
|---|---|
| `movePushBlock(block,dx,dy,dz)` accumule | SPRITE.C:893-900 |
| `updatePushBlockPositions` applique **dy seulement** à `level_vertex[].y`, entraîne sprites+caméra dont `floorSector==fs` | SPRITE.C:852-890, appelé 1×/frame SRUINS.C:2200 ; la version dx/dz est `#if 0` :806-850 |
| `pbObject_move/moveTo` (offset 16.16) | AICOMMON.C:464-476 |

Automates (génériques « bloc X se déplace de dy/tic jusqu'à offset cible », params lus du niveau par
`suckShort`) : `door_func` AI.C:4313 (2 u/tic, attente 128 tics, clé via `keyMask` :4328), `downDoor`
:4407, `elevator` :4590 (5 u/tic, course = upper−lower, déclenché PRESS/SWITCH/FLOORCONTACT),
`upDownElevator` :4717, `bobBlock` :4457 (sinus), `sinkBlock` :4496 (chute gravitaire), `floorSwitch`
:4799, `earthQuakeBlock` AI2.C:858, `ramsesLid` AI2.C:34.

Cadence : `SIGNAL_MOVE` tous les 2 vblanks (SRUINS.C:2142-2151) = 30 Hz. Rendu : **rien à
recalculer** — `getVertex` lit `level_vertex` en direct (UTIL.C:32-37), `sectorDraw[].flags` remis à
0 chaque frame (WALLS.C:2266-2267), normales inchangées (translation verticale) ; seul
`setDoorBlockBits` met à jour `SHORTOPENING` (AI.C:4294-4309). **Portes rotatives/coulissantes
horizontales : inexistantes.**

## 5. Déclencheurs

- Messages (OBJECT.H:75-84) : `SIGNAL_PRESS` = `push()` hitscan <120 u → `wall->object`
  (SRUINS.C:829-855) ; `SIGNAL_ENTER` = `sector->object` (SPRITE.C:731) ; `FLOORCONTACT/CEILCONTACT` =
  `floor->object` (SPRITE.C:688-691,712) ; `SIGNAL_SWITCH(channel)`/`SWITCHRESET` diffusés à **tous**
  les objets (`signalAllObjects` AICOMMON.C:459).
- « Tag » = `channel` (AI.C:4394, AI2.C:590) + canaux virtuels 1000+n (AI.C:3830), 10000+ch (:4730).
- Interrupteurs : mural (AI2.C:518-637, échange de tuile, rayon 40 u), secteur (:645-676, marcher
  dedans), au sol (AI.C:4799).
- **Tirer dessus : aucun** — hitscan mur ⇒ seulement `constructKapow` (WEAPON.C:582-593). Murs
  explosables via grenade `explodeMaskedWall` (AICOMMON.C:478).

## 6. Lumière

Statique par **sommet** (`sVertexType.light`, `level_vertexLight` par tuile) ; `sSectorType.light`
**n'est lu par personne**. `setSectorBrightness(s,level)` réécrit toutes les lumières de sommets du
secteur (AICOMMON.C:50-70) — seul appelant : halo de téléporteur (AI.C:5740-5768). Lumières
ponctuelles dynamiques `addLight/changeLightColor/removeLight` (WALLS.C:501-533), max 15, rayon 256 u,
appliquées par sommet dans `getLight` :649-690 ; `LightObject` Hermite (AI.C:4100-4165). Aucun
automate scintillement/flash.

## 7. Sauvegarde (BUP.C)

| Champ `SaveState` (GAMESTAT.H:54-65) | Taille |
|---|---|
| `inventory, dolls, nmBowls, health, gameFlags` (int) | 20 |
| `weaponAmmo[8]` (int) | 32 |
| `levFlags[31]`, `currentLevel`, `desiredWeapon` (char) | 33 (+1 pad) |
| `year,month,day,hour,min` (short) | 10 |
| **sizeof = 96** ; `SaveRec` = +`short valid` ⇒ 100 (BUP.C:18-21) | |

6 slots (BUP.C:23) ⇒ 600 o ⇒ **10 blocs BUP** de 64 o (`SPACENEEDED` :40), fichier `POWERSLAVE1` :33,
table entière réécrite à chaque fois (`BUP_Write` :139). API SGL :
`BUP_Init/Read/Write/Delete/Stat/Format/SetDate` (:66,81,92,96,139,239,250,123), zones 16 K+8 K
allouées seulement pendant l'E/S (:57-77), périphériques 0 et 1 sondés (:91). Quand : création de
partie (:211), **sortie de niveau vers la carte** (SRUINS.C:2530), warp (:2559), momie (:2581) —
jamais en cours de niveau. Restauré : `currentState` seulement (BUP.C:160) ⇒ le niveau se recharge à
neuf (position au start, portes fermées, objets replacés sauf ceux supprimés par
`inventory/dolls/levFlags`, AI.C:3871-3880). « Recommencer » = snapshot RAM `levStart`
(SRUINS.C:2504,2543).

## 8. Cadences

Physique joueur 60 Hz (par échantillon d'entrée) ; objets/IA/push blocks 30 Hz (SRUINS.C:2142) ;
animations de tuiles 15 Hz (PIC.C:120-122) ; rendu libre, `framesElapsed` plafonné à 8 (:2102) ;
PAL : 50 Hz avec vitesses ×1,2 mais objets à 25 Hz non compensés.

## Conclusion

**(a) Services portables tels quels**

| Doom | SlaveDriver |
|---|---|
| P_XYMovement/P_ZMovement, slide | `moveSprite` + rayon/friction/gravité par `newSprite` |
| T_VerticalDoor / T_PlatRaise / T_MoveFloor | push block par secteur mobile + `door_func`/`elevator_func` (30 Hz, vitesse en u/tic, `channel` = tag) — le convertisseur doit émettre **un push block** (faces de sol + bas des murs adjacents) par secteur tagué |
| Linedef use / W1-WR / tags | `SIGNAL_PRESS` (`wall->object`), `SIGNAL_ENTER` (`sector->object`), `SIGNAL_SWITCH(channel)` |
| P_LineAttack / autoaim | `hitScan` + `autoTarget` |
| P_TouchSpecialThing | `playerGetObject` (réécrire la table) |
| Psprites | file `queueWeaponSequence` + `FRAMEFLAG_FIRE` |
| T_LightFlash/Strobe/Glow | à **écrire** (≈50 lignes) : un objet secteur appelant `setSectorBrightness` à 30 Hz |

**(b) Ce que la sensation Doom impose**
- Modèle **cylindre** (PLRCYL) avec `PLAYER_RADIUS/EYE_STAND/HEAD/STEP` = 16/41/15/24 à l'échelle
  choisie : 4 clés cfg.
- Intégrateur joueur à réécrire : Doom = 35 Hz, `vel += thrust`, `×0,90625`/tic ; ici filtre
  `(15v+f)/16` + ×0,9 à 60 Hz (SRUINS.C:633-634, AI.C:46). Soit cadencer `movePlayer` à 35 Hz
  (accumuler les échantillons), soit recalibrer thrust/friction pour 60 Hz.
- Tourner **sans inertie** (Doom 3,5°/7° par tic) : `tvel = plafond`, friction infinie (:578-606).
- Supprimer saut (:609-627), tangage (`xavel`=0), roulis (:669), dégâts de chute (:240-244), eau/lave
  (spécials secteurs à re-router en `FLOORCONTACT`).
- Sprites sphériques vs cylindres Doom : acceptable ; les monstres peuvent se chevaucher en hauteur.
- **Pas de tir-déclencheur** (G1/GR) : ajouter un `SIGNAL_HURT` sur `wall->object` dans WEAPON.C:582.

**(c) Sauvegarde checkpoint à la PowerSlave = exactement le modèle Doom entre niveaux** (player_t
transporté, niveau rechargé à neuf, pistol-start non forcé). Il manque : `armor`+`armortype`, `skill`,
flag sac à dos ; déjà disponibles : `health`, 4 munitions dans `weaponAmmo[0..3]`, 8 armes dans les
bits 8-15 d'`inventory`, épisode·carte dans `currentLevel` (NMLEVELS 31 ≥ 27 cartes), clés inutiles
(Doom ne les transporte pas). Coût : +8 o ⇒ `SaveRec` 108, 6 slots = 648 o ⇒ **11 blocs** au lieu de
10 (sur 512 de la RAM interne). Négligeable ; le seul travail réel est de réécrire
`bup_initCurrentGame` (BUP.C:184-208) et les 3 sites d'appel de `bup_saveGame` (sortie de niveau).

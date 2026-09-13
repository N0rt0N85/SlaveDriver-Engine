# Duke Nukem 3D PC → format PowerSlave (fork) — topologie, modding, plan du jalon M1 (2026-09-10)

Décision owner (2026-09-10) : build2dex vise **Duke PC → Saturn** (PowerSlave DOS abandonné, aucune paire
DOS ↔ Saturn). Premier jalon : convertir les cartes Duke **au format PowerSlave** (moteur du fork) pour
valider le principe, puis comparer avec Duke Saturn pour décider de la suite. Les §1-§4 sont les rapports
d'un workflow de 4 agents (mesure, vérification adversariale, recherche sourcée, plan), conservés tels
quels. Scripts et journaux : `build/tmp-gen2/` (⚠ un `make` qui efface `build/` les emporterait, comme il a
déjà emporté `build/tmp-build-map/`). Données PC : `refs/build/duke13/`.

## 0. Synthèse

- **Topologie (source + mesure).** Build est un moteur à portails (murs rouges `nextsector`, parcours
  `scansector`), mais une carte Duke est un **archipel d'îlots que seuls les téléporteurs SE7/SE17
  relient** : dans E1L1, le départ (sur le toit) n'atteint que 18 secteurs sur 317 par portails, 311 avec
  les téléporteurs ; E1L4 : 18 → 544 (32 paires d'eau). En plus : pièces-miroirs (secteur caché derrière
  un mur `cstat&32`), petites « salles-machines » isolées (SE, activateurs, musique), jamais vues.
- **Ce que Lobotomy en a fait (mesure, confirmée en 3D).** Les zones d'eau déconnectées sont remises
  **à leur place en vraie 3D** : surface et fond empilés, reliés par un portail horizontal à ≤ 12 u du sol
  Build du bassin (TOXDUMP c1/c4, SECRET1 c4/c6, DEATHROW c1). Le toit d'E1L1 est replacé en
  (4504, −3361), à ~122 u de la prédiction du SE7. Les téléporteurs « au sol » (lotag 0) n'ont aucune
  relation géométrique.
- **Repère complet Duke Saturn** : X = x/8, Z = −y/8, **Y = −z/128**, sans décalage (hauteur mesurée
  exacte sur 7 niveaux ; exception ABYSS1).
- **Couverture des murs PC** avec un décalage par zone : TOXDUMP 21 → 45 %, SECRET1 54 → 67 %,
  DEATHROW +2 points, HOLYWOOD +1. REDLIGHT et ABYSS1 contiennent en plus des **blocs déplacés à
  l'intérieur d'une même zone** (vérification). Il reste entre ~33 % (SECRET1) et ~85 % (ABYSS) de murs
  non retrouvés : refonte, hauteurs modifiées (31 % des murs de SECRET1 appariés en XZ ont perdu leur
  hauteur Build), murs redécoupés.
- **Modding.** L'éditeur Build/Mapster32 *place* (géométrie, sprites picnum/lotag/hitag), il ne programme
  pas. Chez Duke, les monstres et comportements sont dans les **scripts CON** (ex. LIZTROOP,
  GAME.CON:1940-2271), sauf les Sector Effectors et une douzaine d'acteurs codés en C. **SlaveDriver n'a
  aucun script** : un monstre = une fonction C (`anubis_func`, AI.C:2481), points de vie et dégâts en dur.
  Options : A table de paramètres data-driven (1-3 j), C mini-interpréteur façon CON compilé en tables
  (1-2 semaines), B portage de la machine CON de jfduke3d (plusieurs semaines, appels moteur BUILDLIC à
  réécrire).
- **Licence.** Le code jfduke3d (GPL-2 ou ultérieure) peut entrer dans le fork ; EDuke32/NBlood/Mapster32
  (GPL-2 seule), le moteur Build (BUILDLIC) et les CON/art/cartes de 3D Realms, non. La licence du
  shareware (LICENSE.TXT [4][A]) exige que tout nouveau niveau ne fonctionne qu'avec la version
  enregistrée : **une carte shareware convertie reste de la recherche, jamais diffusable**.
- **Plan M1** (E1L1 marchable dans le fork, sans monstres) : voie B, un script Python écrit le `.LEV`
  directement, `tools/lev.py` sert de vérificateur. La voie A (CONVERT.C) compile sur PC mais exige tout
  l'arbre d'assets Lobotomy (≈100 `.seq`, BMP, `.til`). Repère Duke Saturn, cellules de 128 u, emplacement
  TOMB (nouvelle partie, sans code), queue de KILENTRY comme donneur. Effort ≈ 13,5-14,5 j + 2 j (toit en
  3D) + 1 j (comparaison avec HOLYWOOD). Risque principal : la **sphère du joueur PowerSlave (rayon 47 u)
  contre 20,5 u pour Duke** — 47 ouvertures d'E1L1 font moins de 90 u de haut.
- **Décisions ouvertes (owner)** : rayon du joueur (bouton) ou échelle ×2 ; cellules 128 u ou 64 u
  (Lobotomy tient 13 821 primitives à 64 u en simplifiant la géométrie ; à 64 u sans simplifier, on
  estime 36-39 k, soit ~2× le maximum retail) ; 74 tuiles rééchantillonnées ou 131 sous-tuiles ; toit en
  M1 ou en M1b ; état figé des portes et ponts ; TOMB sans code ou TEST via un bouton `TESTCODE`.
- **Décisions prises (owner, 2026-09-10)** : rayon du joueur réduit derrière un bouton de build (niveaux
  PowerSlave intacts par défaut) ; cellules de 128 u ; toit d'E1L1 exclu de M1 puis placé en 3D en M1b ;
  étapes E0-E2 lancées (code dans `tools/duke2ps/`, hors commits ; sorties dans `build/duke2ps/`).
  Restent ouverts : 74 ou 131 tuiles (74 par défaut), état figé des portes et ponts, pentes.
- **E0-E2 PASS (2026-09-10, §5)** : écrivain `.LEV` identique octet pour octet sur les 24 niveaux retail ;
  E1L1 importé = 276 secteurs / 1 648 murs / 572 sprites ; découpe convexe exacte = **446 morceaux**
  (≤ 600), à parité avec HOLYWOOD (298 morceaux contre 292 conteneurs sur 170 secteurs appariés).

## 1. Mesure : zones déplacées et téléporteurs

### Hypothèse « zones empilées déplacées et reliées par SE7 » : test sur E1L1-E1L6

L'hypothèse explique une partie des faibles taux, mais pas la majorité. Chaque zone d'eau (secteurs lotag 1/2) qu'on retrouve est remise, à 0-4 u près, exactement à l'endroit que donne son téléporteur partenaire. Le gain de couverture est de +12,7 points sur TOXDUMP. En revanche, l'hypothèse n'explique ni REDLIGHT (gain 0) ni ABYSS (gain 0).

Fichiers produits :
- script : `build\tmp-gen2\duke_components.py` (réutilise `calibrate.py` et `gen2_lev.py`) ;
- journal : `build\tmp-gen2\duke_components.log` ;
- données : `build\tmp-gen2\duke_components.json` ;
- durée d'exécution : 353 s.

#### SOURCE (`refs\build\jfduke3d\src`)
- **Picnum :** SECTOREFFECTOR = 1 (`names.h:1`).
- **Appariement SE lotag 7 :** le partenaire est le premier SE de lotag 7 ou 23 ayant le même hitag (`game.c:4613-4628`). « SE au sol » (onfloorz) veut dire z du SE = floorz (`game.c:4626`).
- **Téléporteur au sol (secteur lotag 0) :** le joueur est posé exactement sur le partenaire (`actors.c:2686-2712`). Il n'y a donc aucune relation géométrique attendue.
- **SE hors sol :** transport relatif (`actors.c:2728-2748`).
- **Eau :** secteur lotag 1 = au-dessus de l'eau, on plonge (`actors.c:2752`) ; lotag 2 = sous l'eau, on remonte (`actors.c:2771`). Le décalage x,y appliqué au joueur vaut position du partenaire − position du SE (`actors.c:2791-2792`).
- **Autres lotags SE rencontrés :** 12 = lumières (`actors.c:5811`), 25 = pistons (`:6527`), 31 = sol qui descend (`:6744`), 32 = plafond qui descend (`:6881`).

#### Paramètres (fixés dans le script)
- **Transformation imposée :** orientation `X=+x, Z=-y`, échelle 1/8.
- **Taille minimale d'une composante :** 12 points uniques.
- **Condition « retrouvée » :** au moins 8 points à ≤ 4 u, et f4 − p0 ≥ 0,20 (0,10 si n ≥ 100).
- **Niveau de hasard p0 :** le maximum de trois valeurs :
  - témoin décalé de (37,53) ;
  - moyenne de 8 décalages ;
  - vote maximisé contre SPACPORT et RAWMEAT (garde contre le hasard).
- **Tours supplémentaires :** jusqu'à 3 tours de vote sur les points résiduels d'une composante (une composante peut être placée en plusieurs morceaux).
- **Tours EXPLO :** marge 0,05, seulement sur le résidu des grandes composantes ; ils ne servent jamais au classement.
- **Hauteur :** y = −z/128 + dy. On l'estime par secteur Saturn dont le centroïde tombe dans le secteur Build, avec sol et plafond d'accord à 4 u près.

#### MESURE 1 : composantes connexes
Total Build → composantes. Détail : secteurs/murs, SE7 (hitags), secteurs lotag 1/2. Chaque carte contient aussi une boîte d'un secteur et 12 murs en [64512..65408], jamais jugeable (null ≥ 91,7 %).

- **E1L1 (317/1937 → 6 composantes) :**
  - c0 293/1781 ;
  - c2 18/118, SE7 252 « relatif » (hors sol, lotag 0).
- **E1L2 (278/1757 → 6) :**
  - c0 272/1719 porte 98 % des murs ;
  - ses deux paires SE7 (223 et 279) sont internes à c0.
- **E1L3 (478/3050 → 10) :**
  - c0 417/2675 ;
  - c1 19/116, lotag 2 ×18, SE7 50 et 51 ;
  - c2 32/205, lotag 1 ×1, SE 25/31/32.
- **E1L4 (557/3437 → 15, 70 SE7 appariés) :**
  - c0 193/1179, lotag 1 ×13 ;
  - c1 66/518, lotag 2 ×60 ;
  - c2 218/1242, lotag 1 ×66 ;
  - c3 18/86, lotag 1 ×14, SE31 ×16 ;
  - c4 38/239, lotag 2 ×38, SE32 ×17 ;
  - c5 11/107, lotag 2 ×9.
- **E1L5 (479/3198 → 10) :**
  - c0 408/2761, lotag 1 ×28 ;
  - c9 63/360, reliée à c0 seulement par des téléporteurs lotag 0 (524, 525) et un SE relatif (7070).
  - Aucune paire d'eau entre composantes.
- **E1L6 (341/1924 → 11) :**
  - c0 276/1554 ;
  - c3 41/241, SE12 ×41, téléporteur 29 ;
  - c4 5/34, lotag 2 ;
  - c6 9/41, lotag 1.
- **Secteurs Build déjà superposés en 2D** (échantillonnage au pas de 128) :

| Carte | Paires | Secteurs |
|---|---|---|
| E1L1 | 84 | 88 |
| E1L2 | 23 | 26 |
| E1L3 | 56 | 52 |
| E1L4 | 15 | 20 |
| E1L5 | 75 | 52 |
| E1L6 | 1 | 2 |

  Presque toutes ces paires sont à l'intérieur d'une même composante.

#### MESURE 2-3 : composantes déplacées et test au décalage prédit par le partenaire
Le test au décalage prédit ne maximise rien : la prédiction vient du SE, pas d'un ajustement.

**DEATHROW**
- **c1 (eau, hitag 50) :** décalage (+3072,+256). 84,3 % des points (43/51), 42 murs sur 66. Écart à la prédiction : 0 u.
- **c2 (hitag 51, via c1) :** le vote l'avait rejetée (46,3 % contre un null de 42,6 %). Au décalage prédit (+5504,+256), elle monte à 46,3 % contre un témoin de 5,1 %, mais seulement 22 murs sur 137. Décalage vertical +176.

**TOXDUMP** — les deux .LEV partagent le même repère : décalage de carte (0,0) pour les deux.
- **c1 → TOXDUMP2 :** décalage (+3648,+384), 73,4 %, 212 murs sur 340, dy +824.
  - hitag 16 (partenaire c2) : 0 u ;
  - hitags 6/7/8/245 : 0 u, via un morceau de c0 recopié dans TOXDUMP2 en (−800,+416), dy +1064.
- **c4 → TOXDUMP1 :** décalage (+3968,+252), 81,8 %, 91 murs sur 170, dy −921.
  - hitags 161/162/163/170/172 : 4 u.
- **c3 (hitags 130-146, via c4) :** rejetée par le vote, mais à 90,0 % contre 5,0 % au décalage prédit, avec 40 murs sur 57.
- **Paires non confirmées :**
  - hitags 19/20 : 769 u d'écart, 3,9 % ;
  - hitag 231 : 878 u d'écart, 4 murs ;
  - c5 (210-215) : 13,8 % contre 4,6 %, 0 mur. c5 n'est retrouvée nulle part.
- **Morceaux de c0 dans TOXDUMP1 (hors SE) :** (+64,0) avec 130 murs et (+80,0) avec 51 murs. Il peut s'agir d'un artefact de la grille de 64.

**SECRET1**
- **c4 (hitag 39) :** décalage (−1920,0), 100 %, 28 murs sur 30, dy −448. Écart 0 u.
- **c6 (hitag 44, via c4) :** décalage (−2816,0), 100 %, 29 murs sur 29, dy −372. Écart 0 u.
- **c3 (téléporteur 29) :** bien déplacée, en (−1280,−1568), 97,9 % et 115 murs sur 133. Mais pas au décalage prédit : 657 u d'écart, 14,9 % des points.

**Cas non confirmés**
- **HOLYWOOD c2 (SE relatif 252) :** 46,8 % contre p0 17,0 %, seulement 13 murs sur 61. Au décalage prédit, 2,1 % contre 4,3 %. Placement douteux.
- **ABYSS (téléporteurs 524/525/7070) :** tous les tests au décalage prédit sont ≤ 3,3 %, au niveau du témoin.

**Vertical**
- **Décalage vertical de carte :** y = −z/128 + 0 exactement.

| .LEV | Secteurs cohérents |
|---|---|
| HOLYWOOD | 203/232 |
| REDLIGHT | 107/109 |
| DEATHROW | 135/153 |
| TOXDUMP1 | 73/78 |
| TOXDUMP2 | 116/132 |
| ABYSS2 | 18/18 |
| SECRET1 | 142/179 |

  Exception : ABYSS1, où le décalage est (+272,−24) et dy +439,5 (38/57).
- **Au point du téléporteur :** dans les 6 paires vérifiées, on trouve deux secteurs Saturn empilés, reliés par un portail horizontal à la surface, et les deux côtés tombent au même point XZ (0-4 u). Le portail est à ≤ 12 u du sol Build du bassin.
  - DEATHROW 50 : #467 −960..−368 sous #466 −368..192 ; sol Build du bassin −368.
  - SECRET1 39 : #171 sous #10, portail à −104 ; sol Build −112.
  - TOXDUMP2 16 : #9 sous #204, portail à 760 ; sol Build 752.
  - TOXDUMP1 161 : #14 sous #196, portail à 188 ; sol Build 176.
- **Pas une translation rigide « surface contre surface » :** le secteur sous l'eau est étiré jusqu'au portail. Exemples :
  - SECRET1 c4 : dy mesuré −448, alors que l'alignement des surfaces donnerait −64 ;
  - DEATHROW c1 : plafond à dy 0, fond 112 u plus bas.

#### MESURE 4 : couverture des murs PC
Paliers : transformation unique → + composantes déplacées entières → + morceaux stricts → + morceaux EXPLO.

| Carte | Unique | + déplacées entières | + morceaux stricts | + EXPLO |
|---|---|---|---|---|
| E1L1 | 37,3 % | 38,3 % (c2, douteuse) | 38,3 % | 38,3 % |
| E1L2 | 14,2 % | 14,2 % | 14,2 % | 21,2 % |
| E1L3 | 28,0 % | 30,0 % | 30,0 % | 30,0 % |
| E1L4 (union des 2 .LEV) | 21,3 % | 34,0 % | 45,0 % | 47,7 % |
| E1L5 | 12,6 % | 12,6 % | 12,6 % | 17,1 % |
| E1L6 | 53,6 % | 66,9 % | 66,9 % | 66,9 % |

- **Non comptés :** les confirmations obtenues seulement au décalage prédit (TOXDUMP c3 +40 murs, DEATHROW c2 +22 murs).
- **Base à transformation unique :** échelle fixe 1/8, donc les chiffres diffèrent un peu de ceux du §6 (échelle affinée) :

| Paire | Ce test | §6 |
|---|---|---|
| E1L1 → HOLYWOOD | 37,3 % | 34,9 % |
| E1L2 → REDLIGHT | 14,2 % | 12,6 % |
| E1L4 → TOXDUMP1 | 7,3 % | 5,3 % |

#### MESURE 5 : empilement vrai 3D côté Saturn
Critère : empreintes XZ qui se recouvrent sur ≥ 64 u², et plages verticales disjointes.

| .LEV | Secteurs empilés | Plans portail horizontaux |
|---|---|---|
| HOLYWOOD | 146 / 415 | 6 |
| REDLIGHT | 59 / 415 | 8 |
| DEATHROW | 150 / 476 | 26 |
| TOXDUMP1 | 122 / 345 | 78 |
| TOXDUMP2 | 142 / 464 | 50 |
| ABYSS1 | 6 / 459 | 0 |
| ABYSS2 | 0 / 64 | 0 |
| SECRET1 | 91 / 471 | 20 |

- **Sensibilité à la définition :** SECRET1 passe à 41 si la plage verticale est prise sur tous les coins des plans plutôt que sur les seuls plans horizontaux.
- **Origine des secteurs empilés :**
  - les composantes déplacées n'en expliquent que TOXDUMP2 35, TOXDUMP1 6, SECRET1 4 ;
  - les secteurs de DEATHROW c1 ne sont pas attribuables (dy indéterminé) ;
  - des paires venant de secteurs Build déjà superposés (placement de carte des deux côtés) : HOLYWOOD 48, DEATHROW 15 ;
  - le reste tombe dans des secteurs Build en pente, ou a des hauteurs changées.

#### Conclusion
**MESURE :**
- **TOXDUMP :** la couverture passe de 21,3 % à 34,0 % avec les deux composantes sous l'eau, et à 45,0 % en ajoutant les morceaux de c0.
- **SECRET1 :** de 53,6 % à 66,9 %.
- **DEATHROW :** +2 points seulement.
- **REDLIGHT :** une seule composante, donc rien à déplacer.
  - Son meilleur décalage de carte est (0,+320) et non (0,0), où f4 ne vaut que 14,5 %.
  - Un morceau EXPLO apparaît en (+160,−240).
- **ABYSS :** aucune zone d'eau déconnectée.
  - c9 (= ABYSS2) reste au décalage de carte (0,0).
  - ABYSS1 s'aligne au mieux en (+272,−24) avec dy +440, plus un morceau EXPLO en (0,0).
- **Téléporteurs lotag 0 :** aucune relation géométrique (SECRET1 c3 déplacée ailleurs, ABYSS c9 non déplacée).

**Reste inexpliqué :** 55-66 % des murs de TOXDUMP, 86 % de REDLIGHT, 83-87 % d'ABYSS.

**HYPOTHÈSES à vérifier, non mesurées ici :**
- Lobotomy aurait aussi déplacé des morceaux à l'intérieur d'une même composante (REDLIGHT, ABYSS1, TOXDUMP (+64,0)).
- Des murs Saturn redécoupés échouent au critère des murs, qui exige une arête Saturn d'un seul tenant.
- Des hauteurs ont été modifiées : beaucoup de secteurs ne sont retrouvés qu'en XZ.
- Il y a eu une vraie refonte.

La preuve « à la même place que le partenaire » ne porte que sur 5 composantes d'eau déplacées et 2 chaînes, avec des effectifs petits.

## 2. Vérification adversariale de la mesure

### Vérification adversariale de « zones empilées déplacées + SE7 » (E1L1-E1L6)

Le constat de l'autre agent tient pour l'essentiel, mais sa preuve principale ne vaut pas ce qu'il en dit. Qu'une zone d'eau retrouvée se place à 0 u du décalage prédit par le SE7 est en partie automatique en 2D. La preuve indépendante, je ne l'obtiens qu'en 3D. Deux autres points :
- **Son seuil de hasard est biaisé vers le bas** : ses 8 témoins tombent tous hors de la grille de 8 u.
- **Son « gain 0 » sur REDLIGHT et ABYSS est trop sévère** : les morceaux qu'il a relégués en EXPLO passent tous mes tests de hasard.

Aucun fichier suivi n'a été modifié, rien n'a été commité, aucun émulateur n'a été lancé.

#### SOURCE (relue)
- **Sources Duke citées par l'agent : exactes.**
  - Picnum : `names.h:1`.
  - Appariement : `game.c:4617-4622` (premier SE de lotag 7 ou 23 ayant le même hitag) ; onfloorz : `game.c:4626`.
  - Téléporteur au sol : `actors.c:2686-2712` ; transport relatif : `actors.c:2728-2748`.
  - Eau : plongée `actors.c:2752` puis z = plafond du partenaire + 7<<8 (`actors.c:2763`) ; remontée `actors.c:2771` puis z = sol du partenaire − 7<<8 (`actors.c:2782`). Décalage x,y : `actors.c:2791-2792`.
- **Pentes pour mon test 3D :** formule de Build `engine.c:10550-10561`.

#### Fichiers produits (tous dans `build\tmp-gen2\`)
- `duke_verif_common.py` : recherche de voisins rapide, vote réimplémenté, chargeur de .LEV, reconstruction des placements de l'agent.
- `duke_components_verif_a` à `_f` : scripts `.py`, journaux `.log`, et `.json` pour b à e.

La reconstruction des placements redonne tous les f4 de l'agent au millième près, et ma recherche de voisins donne les mêmes appariements que la sienne (100 %).

#### 1. Calcul des composantes — CONFIRMÉ
Mesuré dans `verif_a.log`. J'ai refait l'union des secteurs par `nextsector` puis par `nextwall` : les deux partitions sont identiques à celle de l'agent.

| Carte | Tailles des composantes |
|---|---|
| E1L1 | [293, 18, 3, 1, 1, 1] |
| E1L3 | [417, 32, 19, …] |
| E1L4 | [218, 193, 66, 38, 18, 11, …] |
| E1L5 | [408, 63, …] |
| E1L6 | [276, 41, 9, 5, …] |

Intégrité des cartes :
- 0 erreur de couverture `wallptr`, 0 boucle ouverte, 0 asymétrie `nextwall`.
- Aucune jonction cachée entre composantes : les arêtes inversées sans `nextsector` (2 dans E1L1, 4 dans E1L4) sont toutes internes à une composante.

#### 2. Hasard — le seuil de l'agent est biaisé vers le bas
**Défaut de ses témoins (MESURE).** Ses 8 décalages témoins valent ±3 ou ±5 modulo 8 sur les deux axes. Ils tombent donc tous à 4,24 u de la grille de 8, juste au-delà de la tolérance de 4 u. Or la plupart des points sont sur cette grille :
- points Build sur la grille de 64 unités Build (= 8 u Saturn) : 72 à 95 % ;
- points Saturn sur la grille de 8 u : 31 à 74 %.

Les témoins à 0-5 % sous-estiment donc le hasard de façon systématique.

**Tests de hasard alignés sur la grille** (`verif_b.log`, `verif_e.log`) :
- valeur au décalage trouvé comparée aux 1080 décalages multiples de 8 u voisins, et à 2000 décalages aléatoires alignés ;
- vote de l'agent maximisé contre les 22 niveaux Duke hors épisode 1 et dans les 7 mauvaises orientations ;
- **25 morceaux de carte Build de même taille**, tirés des autres cartes PC puis votés, jugés en 2D et en 3D. C'est le test le plus juste.

| Placement | f observé | Morceaux ≥ obs (max) | Verdict 2D |
|---|---|---|---|
| HOLYWOOD c2 (47 pts) | 0,468 | 5/25 et 8/25 (0,702-0,723) | pas significatif (p ≈ 0,11) |
| TOXDUMP c0 r3 (+80,0), 51 murs comptés dans le palier « strict » | 0,253 | 6-7/25 (0,356) | pas significatif (p = 0,145) |
| TOXDUMP c1 EXPLO | 0,425 | 6-9/25 | pas significatif |
| TOXDUMP c2 EXPLO | 0,266 | 4-5/25 | pas significatif |
| DEATHROW c1 | 0,843 | 0/25 (0,588) | significatif |
| TOXDUMP c1 | 0,734 | 0/25 (0,307) | significatif |
| TOXDUMP c4 | 0,818 | 0/25 (0,432) | significatif |
| TOXDUMP c0 (+64,0) | 0,292 | 0/25 (0,228) | significatif |
| TOXDUMP c0 (−800,+416) | 0,348 | 0/25 (0,318) | significatif |
| SECRET1 c3 | 0,979 | 0/25 (0,468) | significatif |
| SECRET1 c4 | 1,000 | 0/25 (0,769) | significatif |
| SECRET1 c6 | 1,000 | 0/25 (0,762) | significatif |

Deux remarques :
- **SECRET1 c3 :** une des 7 mauvaises orientations donne aussi 0,979. Le décalage est retrouvé, l'orientation n'est pas identifiable (pièce symétrique).
- **Test joint 2D + 3D :** pour tous les placements votés, 0 morceau sur 25 atteint à la fois le f 2D et le pic 3D observés.

**Faux positifs dans son journal (pas dans son rapport).** Trois tests marqués « CONFIRME » de TOXDUMP c4 ne résistent pas à un test de hasard aligné :

| Décalage prédit | f | Voisins ≥ obs | Morceaux ≥ obs |
|---|---|---|---|
| (+4032,+256) | 38,6 % | 8/1080 | 2/25 |
| (+4048,+256) | 15,2 % | 143/1080 | 24/25 |
| (+3104,+96) | 11,4 % | 48/2000 (aléatoires) | 25/25 |

#### 3. Lien avec les SE7 — PLAUSIBLE en 2D (en partie tautologique), CONFIRMÉ en 3D

**MESURE : la concordance 2D est souvent automatique.** La copie Build sous l'eau reprend le contour du bassin de surface, et les deux SE sont à des points correspondants. Si l'on remplace Saturn par les seuls points Build des autres placements, le vote retrouve le même décalage. Autrement dit, « écart 0 u » est garanti même si Saturn ne contenait que la surface.

| Composante | Points qui recouvrent la géométrie Build déjà placée | Support Saturn exclusif | Vote sur Build seul |
|---|---|---|---|
| DEATHROW c1 | 0,882 | 0,000 | 0,882 à 0 u |
| TOXDUMP c3 | 0,950 | 0,000 | 0,950 à 0 u |
| SECRET1 c4 | 0,615 | 0,385 | — |
| TOXDUMP c4 | 0,455 | 0,371 | — |
| TOXDUMP c1 | 0,168 | 0,584 | — (seul cas vraiment non tautologique) |

**MESURE 3D (indépendante).** Pour chaque point, je prends la hauteur de sol et de plafond Build (pentes comprises), et je cherche un sommet Saturn à ≤ 4 u en XZ et ≤ 4 u en y. Je balaie le décalage vertical dy de −4096 à +4096. Le « max morceaux » est le meilleur pic 3D obtenu par les 25 morceaux votés.

| Composante | dy du pic | Paires appariées | Max morceaux |
|---|---|---|---|
| TOXDUMP c1 | +828 | 68,4 % | 4,2 % |
| TOXDUMP c4 | −924 | 31,2 % | 10,2 % |
| TOXDUMP c3 (au décalage prédit) | −926, même dy que c4, chaîne cohérente | 56,7 % | 25,3 % |
| SECRET1 c4 | −444 | 85,7 % | 27,8 % |
| SECRET1 c6 | −372 | 100 % | 39,6 % |

Deux cas à part :
- **DEATHROW c1 :** 41 des 51 sommets de sol sont appariés à dy −116, et aucun n'est déjà expliqué par c0. Au plafond, 23 paires appariées à dy 0, toutes partagées avec c0 : c'est la surface portail. L'agent dit « fond 112 u plus bas » : c'est confirmé.
- **Nuance TOXDUMP c3 :** seules 12 des 90 paires sont exclusives (`verif_f.log`). Le soutien est donc faible.

Verdicts :
- **CONFIRMÉ en 3D :** TOXDUMP c1 et c4, SECRET1 c4 et c6, DEATHROW c1.
- **PLAUSIBLE :** TOXDUMP c3 et DEATHROW c2.
  - DEATHROW c2 au décalage prédit : f 0,463, contre 0,352 au mieux sur la grille voisine, avec 2/25 morceaux ≥ obs.
  - Son meilleur dy 3D est +148 (48 paires exclusives sur 340), et non le +176,5 de l'agent (16 paires exclusives seulement).
- **Téléporteurs lotag 0 sans relation géométrique : CONFIRMÉ.** SECRET1 c3 au décalage prédit fait 14,9 %, avec 73/1080 voisins ≥ obs, soit le niveau du hasard.

#### 4. Chiffres de couverture — CONFIRMÉS exactement, mais fragiles

**MESURE (`verif_d.log`).** Avec sa propre fonction `seg_hits`, je retrouve exactement ses chiffres :

| Carte | Paliers (unique / + entières / + strict / + EXPLO) |
|---|---|
| E1L1 | 485 / 498 / 498 / 498 |
| E1L2 | 172 / 172 / 172 / 257 |
| E1L3 | 588 / 630 / 630 / 630 |
| E1L4 | 506 / 809 / 1070 / 1136 |
| E1L5 | 275 / 275 / 275 / 373 |
| E1L6 | 694 / 866 / 866 / 866 |

Le hasard sur la carte entière (32 décalages de 8k u) ne dépasse pas 0,1 à 0,9 %.

**Fragilité : la façon dont sa fonction départage deux voisins à égale distance pèse sur le résultat.** Si l'on compte un mur dès qu'une paire quelconque de candidats forme une arête Saturn :

| Carte | Unique | + EXPLO / + entières |
|---|---|---|
| E1L2 (REDLIGHT) | 14,2 % → 18,6 % | 21,2 % → 25,6 % |
| E1L1 | — | 38,3 % → 39,8 % (+ entières) |
| E1L4 | — | 45,0 % → 45,7 % (+ strict) |

**Les gains survivent en 3D** (un mur compte si son arête de sol ou de plafond est une arête 3D Saturn) :

| Carte | Unique → + entières | Gain |
|---|---|---|
| SECRET1 | 36,9 % → 50,2 % | +13,3 |
| TOXDUMP | 18,9 % → 30,0 % → 39,4 % → 42,2 % | +11,1 puis +9,4 |
| DEATHROW | 24,1 % → 26,1 % | +2,0 |
| ABYSS (avec dy +439,5) | 12,1 % | — |

À noter : sur SECRET1, 31 % des murs appariés en XZ perdent leur hauteur Build. C'est une mesure qui soutient l'hypothèse « hauteurs modifiées ».

#### 5. Ses conclusions « gain 0 » et « reste inexpliqué » — RÉFUTÉES en partie
**MESURE.**
- **REDLIGHT (+160,−240), relégué en EXPLO par l'agent**, passe tous mes tests :
  - f 0,199, au-dessus de tous les tests de hasard (le plus haut : 0,164 pour les morceaux) ;
  - en 3D : 16,0 % à dy ≈ 0, contre 3,4 % au plus pour les morceaux ;
  - 254 paires 3D sur 254 sont exclusives, et 85 murs contre 4,6 en moyenne au hasard.
- **ABYSS1, morceau (0,0)** : f 0,118, au-dessus de tous les tests de hasard (le plus haut : 0,050) ; pic 3D à dy −503 avec 210 paires, alors que le 99e percentile du profil est à 31.
- **TOXDUMP (+64,0)** : voir le tableau du §2 (significatif).

Son hypothèse « déplacements internes à une composante » devient donc une mesure pour REDLIGHT, ABYSS1 et TOXDUMP (+64,0). Le « 86 % inexpliqué » de REDLIGHT tombe à environ 74 % sans départage arbitraire et avec le morceau.

**HOLYWOOD c2 (qu'il juge douteuse) : PLAUSIBLE, réelle.**
- En 2D seule, elle n'est pas significative.
- Mais en 3D : pic à dy +85, 36 paires sur 138 (26,1 %), contre 17,0 % au plus pour les morceaux ; test joint 0/25.
- 12-13 murs, contre 0,0 en moyenne au hasard (maximum 1).
- Elle n'est pas au décalage prédit par le SE 252 : l'écart est de 122 u.

**Hauteur de carte y = −z/128 + 0 : CONFIRMÉ.** Le pic 3D forme un plateau de −4 à +4 ; ABYSS1 donne +436. Un deuxième pic apparaît systématiquement vers ±64-68.

#### HYPOTHÈSES (non testées ici)
- REDLIGHT et ABYSS1 pourraient être des assemblages de plusieurs blocs rigides. Ce ne serait pas un défaut d'échelle : le saut est (160, −560) u d'un seul coup.
- Dans l'eau, les secteurs Saturn sont étirés : DEATHROW c1 a son sol à dy −116 et son plafond à dy 0, et le pic de plafond isolé à dy −183 reste inexpliqué.
- Mes tests « morceaux » ne font que 25 tirages : la résolution de p est d'environ 0,04.

## 3. Recherche : portails de Build, scripts CON, SlaveDriver

### Rapport : Build (portails, SE), scripts CON de Duke, et ce que ça implique pour SlaveDriver

**Résumé** :
- **Build est bien un moteur à portails.** Chaque carte Duke est pourtant découpée en îlots que seuls les Sector Effectors SE7 (téléporteur) et SE17 (ascenseur-téléporteur) relient. Dans E1L1, le départ n'atteint que 18 secteurs sur 317 par portails, et 311 avec SE7/SE17.
- **L'éditeur place, il ne programme pas.** Le comportement des monstres est dans les scripts CON, sauf les Sector Effectors et une douzaine d'acteurs, écrits en dur en C.
- **SlaveDriver n'a aucun script** : tout est en C.

Aucun fichier suivi modifié, rien commité, pas d'émulateur. Mes fichiers sont dans `build\tmp-gen2\` : `duke_topo.py` et `duke_topo2.py` (mesures), et `duke_con\` pour les CON extraits. Le nom `con\` demandé est un nom réservé sous Windows, d'où `duke_con\`.

---

#### Q1. Build est-il un moteur à portails ?

##### SOURCE : le moteur (jfbuild)

- **Structure d'un mur** : chaque mur porte `nextwall` et `nextsector` (`jfbuild/include/build.h:139`). Un « mur rouge » est un mur avec `nextsector >= 0`.
- **Parcours du rendu** : `drawrooms` appelle `scansector(globalcursectnum)` (`engine.c:5686`).
  - `scansector` (`engine.c:610-743`) est un parcours en largeur de secteur en secteur, à travers les murs rouges.
  - Il ne traverse que les murs `(cstat&32)==0`, pas encore visités (masque `gotsector`, l.643), et dont l'arête est dans le champ (`engine.c:659-665`).
- **Ordre de dessin** : les « bunches » sont dessinés de l'avant vers l'arrière (`engine.c:5715-5746`).
- **Mur-portail** : `drawalls` (`engine.c:2469-2496`) dessine les marches haute et basse, puis resserre `umost`/`dmost`. Ces deux tableaux sont la fenêtre de clip par colonne, c'est-à-dire le portail.
- Il n'y a ni BSP ni PVS.

**Localiser un point ne dépend pas de la connectivité.** `updatesector` (`engine.c:9590-9615`) essaie le secteur courant, puis ses voisins par `nextsector`, puis tous les secteurs en force brute (`inside()`, l.8381). `updatesectorz` (l.9625-9650) départage par l'altitude z. `cansee` suit lui aussi `nextsector` (l.8602-8627).

**Conséquence** : un secteur n'a qu'un sol et un plafond. Deux secteurs peuvent se recouvrir en XY s'ils ne sont jamais visibles ensemble. Un vrai empilement (voir à travers un sol) est impossible sans astuce.

##### Ce qui n'est PAS relié par portail dans une carte Duke

**Miroir** (SOURCE)
- `prelevel` (`premap.c:868-889`) repère les murs `overpicnum==MIRROR` (560, `names.h:162`) avec `cstat&32`. Leur `nextsector` devient la « pièce miroir » : son sol et son plafond reçoivent le picnum MIRROR.
- Rendu (`game.c:3081-3109`) : `preparemirror`, puis `drawrooms(..., mirrorsector+MAXSECTORS)`, décodé par `engine.c:5671-5672` et `5688-5713`, puis `completemirror` retourne l'image.
- `scansector` n'entre jamais par un mur `cstat&32` (l.659).
- MESURE : E1L1 a 2 miroirs (secteurs 78 et 179), E1L2 en a 2 (101 et 177), E1L3 à E1L6 aucun.

**Eau** (SOURCE)
- Les secteurs lotag 1 (surface) et lotag 2 (sous l'eau) sont gérés dans `player.c:48`, `2372-2385`, `2645` et `3303`.
- Ils sont reliés par des paires de SE7 de même hitag :
  - au spawn, `OW` = le partenaire, et le sprite passe en statnum 9 (`game.c:4613-4629`) ;
  - dans `movetransports` (`actors.c:2644-2985`), un sprite en lotag 2 près du plafond, ou en lotag 1 près du sol, est téléporté (l.2843-2847) ;
  - ses x,y sont translatés et son z est recalé sur le plafond ou le sol du partenaire (l.2948-2970).
- La surface et le fond sont donc deux secteurs disjoints. En 1.3D, on ne voit pas à travers la surface.

**Zones empilées ou « chutes »** (SOURCE) : SE7 dans un secteur lotag 0, avec téléport au sol (`actors.c:2849-2857` et `2902-2946`). SE23 est la sortie à sens unique (`game.c:4614`, « XPTR END »).

**Ascenseur-téléporteur** (SOURCE) : secteur lotag 15 (`sector.c:683-711`) plus SE17 (`actors.c:6008-6067`). Il déplace sol, plafond et sprites, puis `activatewarpelevators` envoie vers le SE17 partenaire (l.6062-6066).

**Caméras de surveillance** (SOURCE)
- `VIEWSCREEN` pose `p->newowner = CAMERA1` (`sector.c:3147-3171`).
- Rendu vers une tuile par `xyzmirror` (`premap.c:385-398`) : `drawrooms` depuis le secteur du sprite.
- Branche `ud.camerasprite` : `game.c:2939-2955`.
- MESURE : les caméras sont posées dans la géométrie normale. En E1L2, E1L3 et E1L6, 8/8, 12/12 et 6/6 CAMERA1 sont dans la composante du départ. Il n'y a pas de salle cachée.
- HYPOTHESE : la branche `newowner` (non lue) rend de la même façon.

**ROR SE40-45** (Atomic 1.5, contraste)
- `se40code` (`game.c:2879-2902`) et `SE40_Draw` (`2780-2874`) abaissent ou élèvent temporairement les sols et plafonds marqués 40/41 (l.2833-2844). Ils rendent depuis le partenaire décalé (l.2848-2852), puis restaurent (l.2856-2872). C'est un trucage d'affichage.
- MESURE : aucun SE de lotag ≥ 37 dans les 6 cartes 1.3D.

**Géométrie mobile** (SOURCE) : `ms()` (`actors.c:710-739`) fait un `dragpoint` sur tous les murs du secteur à chaque tic (SE 0, 6, 14, 30…).

##### Contraste : le « TROR » d'EDuke32

C'est un vrai empilement. `eduke32/source/build/include/build.h:130-199` : « True Room over Room (YAX) ».
- Les secteurs sont liés verticalement par leur plafond ou leur sol en « bunches » (`yax_getbunch`, `YAX_MAXBUNCHES` 256/512).
- Chaque mur a un `nextwall` par plafond et par sol (`YAX_NEXTWALL`).
- Le rendu et les collisions traversent verticalement, sans téléport.
- Licence GPL-2 seule + BUILDLIC : on s'en inspire, on n'en copie pas de code.

##### MESURE : topologie des cartes (`duke_topo.py` / `duke_topo2.py`)

Les composantes sont non dirigées ; le départ vient du `cursectnum` de l'en-tête.

| carte | secteurs | murs rouges | composantes portails → +SE7/SE17 | atteint depuis le départ (portails → +SE7/17) | paires SE7 (dont liant 2 composantes) |
|---|---|---|---|---|---|
| E1L1 | 317 | 1274 | 6 → 5 | **18 → 311** | 1 (1) |
| E1L2 | 278 | 1086 | 6 → 6 | 272 → 272 | 2 (0) |
| E1L3 | 478 | 1902 | 10 → 8 | 417 → 468 | 3 (2) |
| E1L4 | 557 | 2100 | 15 → 10 | **18 → 544** | 39 (34), dont 32 paires eau (lotag 1,2) ; 93 secteurs lotag 1 / 109 lotag 2 |
| E1L5 | 479 | 2042 | 10 → 9 | 408 → 471 | 5 (3) |
| E1L6 | 341 | 1260 | 11 → 8 | 276 → 331 | 4 (3) |

**Îlots restants** (4, 5, 7, 9, 8 et 7 selon la carte), de 1 à 3 secteurs chacun :
- des SEENINE (1247), SE2 et MASTERSWITCH (8). E1L5 en a 4, avec 12 SEENINE chacun ;
- ou un secteur lotag 20 avec SE8, ACTIVATOR (2), MUSICANDSFX (5) ou GPSPEED (10) ;
- plus 3 secteurs vides isolés par carte.

HYPOTHESE : ce sont des « salles-machines » hors carte (temporisations, chaînes d'événements). Elles ne sont jamais vues, mais leur logique compte.

##### SE qui touchent la géométrie ou la topologie

Sens tiré du code : spawn dans `game.c:4603-5012`, animation dans `actors.c:4805-7010`. Les noms viennent de `eduke32/.../game.h:485-529`.

| SE | Nom | Effet | Source |
|---|---|---|---|
| 0 / 1 | secteur tournant / pivot | cherche le SE1 de même hitag ; murs déplacés par `ms`/`dragpoint` | game.c:4895-4937, actors.c:4829, 4985 |
| 2 | séisme | met `floorheinum` à zéro puis le rejoue | game.c:5007-5010, actors.c:5403 |
| 6 / 14 / 30 | métro / wagon / train 2 sens | secteurs mobiles ; `sector.hitag = SE` ; exige un voisin « zéroté » de lotag < 3 | game.c:4954-4991, actors.c:5003, 5046, 5226 |
| 7 / 23 | téléport / sortie à sens unique | lien topologique | voir ci-dessus |
| 11 | porte battante | rotation des murs | game.c:4891, actors.c:5753 |
| 13 | charge C-9 | écrase plafond et sol sur `sp->z`, puis ouvre | game.c:4677-4730, actors.c:5879 |
| 15 | porte coulissante | — | actors.c:5944 |
| 16 | réacteur | — | actors.c:5976 |
| 17 | ascenseur-téléporteur | lien vertical | voir ci-dessus |
| 18 | montée/descente incrémentale | — | game.c:4634-4650, actors.c:6124 |
| 19 | boucliers | retire les murs BIGFORCE (`cstat`, `overpicnum`) | actors.c:6205-6222 |
| 20 | pont extensible | les 2 murs les plus proches | game.c:4755-4796, actors.c:6297 |
| 21 | sol qui tombe | — | actors.c:6363 |
| 22 | porte à dents | — | actors.c:6393 |
| 25 | piston | — | game.c:4655, actors.c:6527 |
| 26 | escalator | — | game.c:4996-5006, actors.c:6551 |
| 29 | vagues | `floorz = z + sin` | actors.c:6739-6743 |
| 31 / 32 | sol / plafond monte-descend | — | game.c:4824-4851, actors.c:6744, 6881 |
| 35 | écraseur de plafond | — | game.c:4661, actors.c:6494-6525 |
| 128 | bris de verre | `cstat` des murs ; créé à l'exécution | actors.c:6959-6983 |
| 130 / 131 | effets d'explosion | créés à l'exécution (`EGS`) | sector.c:1644, actors.c:6985, 7001 |
| 40-45 | ROR | trucage d'affichage (1.5 seulement) | game.c:2879-2902 |

**SE sans effet sur la géométrie** :
- 3 et 4 : lumières ;
- 5 : « Boss Creature », vise le joueur (`actors.c:5568-5573`) ;
- 8 et 9 : lumières de porte ;
- 10 : fermeture automatique de porte ;
- 12 : interrupteur de lumière ;
- 24 et 34 : tapis roulant, qui déplace les sprites (`actors.c:6403`) ;
- 27 : caméra de démo ;
- 28 : éclair ;
- 33 : débris de séisme ;
- 36 : lanceur de projectiles (`actors.c:6947-6952`).

**Lotags de secteur** :
- 1 et 2 : eau ;
- 9, 15-23, 25, 26 et 29 : portes et ascenseurs (`isanearoperator`, `sector.c:173-192`) ; 27 et 28 sont gérés par `operatesectors` (`sector.c:552-1000`), hors de cette liste ;
- 32767 : secret (`premap.c:745`) ;
- -1 : fin de niveau (`premap.c:751`) ;
- 10000-16382 : son à l'entrée (`sector.c:2964-2967`).

**MESURE : SE par lotag**
- E1L1 : 176 SE, dont 12:45, 4:35 et 13:22.
- E1L4 : 364 SE, dont 24:116 et 7:74.
- E1L6 : 232 SE, dont 12:62, 31:38, 24:35 et 13:34.

---

#### Q2. Peut-on modifier les monstres et leurs comportements avec Build ?

**(a) L'éditeur Build ou Mapster32 place, il ne programme pas.** Un `.MAP` ne contient que des secteurs, des murs et des sprites. Les champs d'un sprite sont `x, y, z, cstat, picnum, shade, pal, clipdist, xrepeat, yrepeat, offsets, sectnum, statnum, ang, owner, vel, lotag, hitag, extra` (`jfbuild/include/build.h:162-173`). C'est le jeu qui leur donne un sens. Même l'extension d'éditeur de jfduke3d ne fait que compter et étiqueter (`astub.c:914-984`).

**(b) Duke 3D : le comportement est dans les scripts CON.**
- Chaque script porte « (c) 1996 3D Realms » (`GAME.CON:4-6`, `USER.CON:5-7`).
- **Bloc réel du LIZTROOP** (shareware 1.3D) :
  - `DEFS.CON:425` : `define LIZTROOP 1680` (le picnum) ;
  - `USER.CON:137` : `define TROOPSTRENGTH 30` (points de vie). Les dégâts des armes sont en `USER.CON:108-133` ;
  - `GAME.CON:1940-1959` : 19 `action`, par exemple `action ATROOPSTAND 0 1 5 1 1` (frame, nombre de frames, vues, incrément, délai) ;
  - `GAME.CON:1961-1966` : les `move` (`TROOPWALKVELS 72`…) ;
  - `GAME.CON:1970-1979` : les `ai` (`AITROOPSEEKPLAYER ATROOPWALKING TROOPWALKVELS seekplayer`) ;
  - `GAME.CON:1981-2258` : les `state`, dont `state troopcode` en l.2181-2258 ;
  - `GAME.CON:2271` : `actor LIZTROOP TROOPSTRENGTH ATROOPSTAND state troopcode enda` ;
  - variantes en l.2261-2270 (`LIZTROOPONTOILET` : `sound FLUSH_TOILET operate`).
- **Côté C** :
  - le tableau de mots-clés numérotés compte 108 lignes (`gamedef.c:82` pour `actor`, `105` pour `ai`, `113` pour `move`, `179` pour `useractor //98`) ;
  - `parse()` est en l.2128, et le script de chaque acteur est rangé dans `actorscrptr[picnum]` (l.978 et 1041, `global.c:120`) ;
  - `execute()` (l.3194-3206) est appelé depuis `moveactors` (`actors.c:4256-4258`) ;
  - au spawn, la santé, l'action et le move sont lus depuis `actorscrptr` (`game.c:3218-3223`).
- MESURE : 119 `actor`/`useractor` du `GAME.CON` 1.3D se résolvent en picnum.
- MESURE : sprites placés. E1L1 a 18 LIZTROOP, 13 LIZTROOPDUCKING et 11 PIGCOP. E1L4 a 39 OCTABRAIN. Chaque carte a 14 APLAYER, qui sont les départs multijoueur.

**(c) Ce qui est en dur en C dans jfduke3d** :
- tous les Sector Effectors ;
- `movetransports` ;
- les armes (`moveweapons`, `actors.c:2298`) ;
- `hitradius` (`actors.c:278`) ;
- la physique du joueur (`player.c`) ;
- une partie des acteurs, traités dans le `switch` de `moveactors` avant `execute` (`actors.c:2989-4241`) : RAT (3100), RECON (3249), OOZ (3426), GREENSLIME (3444-3451, qui avale LIZTROOP, LIZMAN, PIGCOP et NEWBEAST en 3727-3737), FORCESPHERE (3205), REACTOR (4060-4088), CAMERA1 (4209) ;
- des réglages au spawn : LIZTROOP en `pal 22`, taille et `clipdist` des boss (`game.c:4255-4304`).

**(d) EDuke32** étend fortement le CON : `gamevar` (`gamedef.cpp:267`), `onevent` (425), `defstate` (219), `getactor`/`setactor` (268 et 485), qui donnent accès aux structures. Licence GPL-2 seule (`eduke32/.../game.cpp:7-9`).

---

#### Q3. Conséquence pour SlaveDriver

##### SOURCE : où vivent monstres et comportements

- **Types d'objets** : `enum ObjectType` (`SLEVEL.H:22-80`, `OT_NMTYPES`, `OT_DEAD=8196`).
- **Création** : `OBJECT.C:289-321` choisit le constructeur selon le type (`case OT_ANUBIS: constructAnubis(suckShort())`, l.292-293).
- **Comportement : une fonction C par monstre.** Exemple, `anubis_func` (`AI.C:2481-2579`), qui réagit aux messages `SIGNAL_HURT`, `SIGNAL_MOVE` et `SIGNAL_VIEW`. Tout y est en dur :
  - dégâts de griffe 20 (l.2543) ;
  - portée `F(150)` (l.2539) ;
  - vitesse du projectile `F(46)` (l.2560) ;
  - `health=100` (l.2594).
- **Code commun** dans `AICOMMON.C` : `monsterObject_signalHurt` (169), `monster_seekEnemy` (280), `normalMonster_idle` (316), `decideWhatToDo` (339).
- **Points de vie** : 17 affectations en dur dans `AI.C` (de 54 = 700 à 5512 = 5000), plus `AI2.C:830` et `834`.
- **Convertisseur** : `jeffMonsterMap` (`UTIL/CONVERT.C:407-545`, terminé par `{-1,-1}`). La recherche est en l.2920-2932, l'écriture en l.2959-2962 (type, secteur, x>>1, z>>1). Les séquences et sons par type sont en l.4333-4374.
- MESURE : aucun script. Chercher `script|interpret|bytecode` dans les `*.C`/`*.H` ne trouve que `SCL_FUNC.C` (scroll VDP2) et `SEGA_INT.H`.

**Donc** : un convertisseur ne peut que traduire les sprites Build (picnum, lotag, hitag, pal) vers des `OT_*` existants, avec le comportement PowerSlave. Un LIZTROOP deviendrait par exemple un Anubis. Changer un comportement, c'est changer le C du fork.
- Portes et ascenseurs ont des équivalents (`OBJECT.C:255-288`).
- `OT_TELEPSECTOR` et `OT_TELEPRETURN` existent (`SLEVEL.H:63`). HYPOTHESE à vérifier : ils pourraient servir pour SE7.
- Les secteurs mobiles (SE 0, 6, 14, 30) n'ont pas d'équivalent connu.

##### Trois options pour pouvoir modifier monstres et comportements

**A. Table de paramètres dans le fork** (coût faible, 1 à 3 jours)
- Une structure par `OT_*` : santé, vitesses, dégâts, portées, projectile, séquences, sons, drapeaux.
- Elle remplace les ~20 constantes dispersées.
- La table peut vivre dans un fichier du CD, ou par objet dans le `.LEV` : le format a déjà `nmObjectParams` (`SLEVEL.H:13`) et `suckShort`.
- On règle les monstres, mais on n'ajoute pas de nouveau comportement.
- Licence : notre code, GPL-3.

**B. Porter la machine CON de jfduke3d** (coût élevé, plusieurs semaines)
- Cela concerne `gamedef.c` (`parse` et `execute`) et les aides d'`actors.c`.
- Le code jfduke3d est en GPL-2 ou ultérieure (`game.c:7-10`), donc il peut passer en GPL-3.
- Mais cette machine suppose le monde Build : les tableaux `sprite`/`sector`/`hittype`, `actorscrptr[MAXTILES]`, et des actions exprimées en frames de tuiles. SlaveDriver utilise des séquences `.seq`.
- Les appels moteur (`cansee`, `hitscan`, `clipmove`) sont sous BUILDLIC : il faut les réécrire.
- Les scripts de 3D Realms ne peuvent pas être distribués. L'utilisateur fournirait les siens, ou on en écrit de nouveaux.
- Il y a en plus un coût d'interprétation par acteur et par tic sur SH-2, et de RAM.

**C. Mini-interpréteur maison, inspiré du CON** (coût moyen, 1 à 2 semaines pour le cœur)
- Un sous-ensemble : `state`, `action`, `move`, `ai`, quelques `if*` (vue, distance, santé, hasard), `shoot`/`hurt`, branché sur le modèle existant des messages `SIGNAL_*`.
- Compilé sur PC en tables ou en bytecode, pour garder un coût d'exécution faible sur Saturn.
- Les idées du CON ne sont pas protégées ; le code serait propre et en GPL-3.
- Rappel : le statu quo (éditer `AI.C`) reste le moins cher fonctionnalité par fonctionnalité.

##### Licences : ce qui peut entrer dans le dépôt (GPL-3, `LICENSE.txt:1-2`)

**Peut entrer** :
- notre propre code ;
- le C de jfduke3d (GPL-2 ou ultérieure), en conservant les mentions.

**Ne peut pas entrer** :
- **Code EDuke32, NBlood, Mapster32** : GPL-2 seule (`game.cpp:7-9` ; `refs/README.md:47-48`).
- **Code moteur Build** (jfbuild, ken) : BUILDLIC, qui limite à internet, gratuit, et soumet tout usage commercial à licence (`buildlic.txt [2][3][5]` ; `README.md:49-53`).
- **CON, art et cartes de 3D Realms** : `LICENSE.TXT [2]`, `[3][C]` (fichiers non modifiés) et `[7][D]` (pas de modification). En plus, `[4][A]` exige que tout nouveau niveau ne fonctionne qu'avec la version retail, jamais avec l'Épisode 1. Convertir les cartes shareware reste donc de la recherche, pas quelque chose à diffuser.

Utiliser Mapster32 comme outil n'engage à rien.

## 4. Plan du jalon M1 (E1L1 dans le fork, sans monstres)

### Plan : jalon E1L1 (Hollywood Holocaust) jouable et marchable dans le fork gen1, sans monstres

#### 0. En bref

- **Voie recommandée : B.** Un script Python écrit directement le `.LEV`, `tools\lev.py` sert de vérificateur. Le compilateur n'est pas ce qui bloque la voie A : c'est la montagne d'assets Lobotomy qu'elle exige (§1).
- **Repère : celui de Duke Saturn, mesuré sur HOLYWOOD.** X = x/8, Z = −y/8, Y = −z/128, sans décalage.
- **Cellule de 128 u.** C'est la seule option compatible avec les plafonds de volume. À 64 u, on estime 36-39 k primitives, soit environ 2 fois le maximum retail (19 253).
- **Emplacement disque : TOMB.LEV.** C'est le niveau d'une nouvelle partie, donc aucun changement de code.
- **Effort : 13,5 à 14,5 j** pour le jalon M1 (itérations console comprises), plus environ 2 j pour placer le toit en 3D (M1b) et 1 j pour la comparaison avec HOLYWOOD.

Je n'ai rien committé ni modifié de suivi. Tout ce que j'ai écrit est sous `build\tmp-gen2\`.

#### 1. Voie A contre voie B

**MESURE**
- `C:\msys64\mingw32\bin\gcc.exe` (GCC 16.1.0, i686) compile `UTIL\CONVERT.C` avec `-x c -std=gnu89 -fcommon`. Code retour 0, 165 warnings en `-Wall` (surtout `-Wswitch`).
- Il le lie avec `DEXSHELL.C` et un `LEVELFILE` factice (`build\tmp-gen2\voieA\dexwrap.c`, `dummy_lev.c`). `convert.exe` s'exécute et répond « Args bad. ».
- Cela contredit `LEVEL_PIPELINE.md:13` (« aucun gcc hôte ici »). Je laisse la correction au propriétaire.

**SOURCE : ce qui bloque vraiment la voie A**
- **Séquences** : environ 100 `.seq` sont obligatoires. `exit(-1)` au premier manquant (CONVERT.C:4032-4036, noms câblés 4253-4918), et l'éditeur de séquences est absent.
- **Ciel et palette** : il faut `DAYSKY*.BMP` (CONVERT.C:5039-5072) et `ruinspal.bmp` (5103-5105).
- **Tuiles** : le `.til` `BR` est limité à 126 entrées (651-658), avec repli `default.til` puis sortie (641-649). Les BMP de `tiles\` manquants provoquent une sortie (805-808, 1143-1146). Sans tuile 8 bpp : « No objects! », sortie (5166-5168).
- **Géométrie** : l'entrée Dex est déjà faite de quads et de faces pavées (DEXSHELL.H:124-163). La découpe convexe, l'empilement et le pavage restent donc à notre charge dans les deux voies. CONVERT n'ajoute que la détection rectangle, la lumière, les drapeaux, les portes/push-blocks, l'eau et les cut-planes, dont on n'a pas besoin pour un niveau statique.

**Verdict** : la voie A demande tout le travail géométrique de B, plus la régénération d'un arbre d'assets Lobotomy complet. Je recommande B.

**Principe de la voie B**
- On écrit `sky + level + sounds + palettes + tiles + sequences` au format lu par `tools\lev.py`.
- On recopie la **queue** (sons, palettes, tuiles, séquences) d'un `.LEV` retail « donneur ».
- Nos tuiles murales passent **en tête** du jeu de tuiles. Les tuiles du donneur suivent, et chaque `chunk.tile` de ses séquences est décalé de +N (N = nombre de nos tuiles murales ; SEQUENCE.C:74-75, `short` en SLEVEL.H:223).
- La palette Duke est ajoutée à la fin du bloc palettes. Nos tuiles pointent sur son index (PIC.C:517 ; objet = premier short, PIC.C:630).

#### 2. Faits utiles au plan

##### 2.1 E1L1 (`e1l1_plan_stats*.py`) — MESURE

| sujet | valeur |
|---|---|
| Carte | v7, 317 secteurs, 1 937 murs, 639 sprites |
| Départ | (−31243, 7160, −181472), ang 422, secteur 309 (lotag 10189) |
| Convexité | 232/317 convexes sans trou ; 334 sommets rentrants ; 33 trous ; pire secteur 295 (39 rentrants) |
| Morceaux convexes | borne ⌈r/2⌉+1−h = **471** ; coupe par sommet rentrant r+1−h = **618** ; HOLYWOOD = **415** conteneurs |
| Hauteurs à /128 | médiane 192 u (p10 88, p90 776) ; PS retail : 160-512 u |
| Secteurs fermés | 20 (lotags 0×6, 20×4 portes, 23×6 portes battantes, 27×4 ponts) |
| Pentes | 34 sols, 35 plafonds |
| Parallaxe | 38 plafonds, tous le picnum `LA` (128×300) ; 4 sols |
| Boîte englobante (/8) | X −5280..8176, Z −8176..525, Y −392..3640 → tient dans un `short` |
| Murs latéraux gen1 | 2 576 = 663 à une face + 1 274 à deux faces + 377 bas + 262 hauts |
| Murs gen1 estimés | **3 826 à 4 414** (plafond 5 500) |
| Picnums visibles | 61 murs, 38 sols, 24 plafonds, 4 masqués → **74** uniques (70 sans masqués) ; 31 dépassent 64 px ; découpe en 64×64 → 131 sous-tuiles ; 2 animés (+11 images) |
| Sprites perdus | 138 sprites muraux (dont 30 `MASKWALL2`), 8 sprites de sol ; 19 murs masqués, 8 murs à sens unique |
| Composantes connexes | 6 : 293 secteurs (principale), **18 (toit, contient le départ)**, puis 3, 1, 1 et 1 (inaccessibles, aux coins de la carte) |
| SE7 | 2 (hitag 252) : spr 84 (secteur 259, z −70656) ↔ spr 552 (secteur 272, z −35840) ; Δ = (35055, 26816, −34816) |
| Chevauchements XY | 70 paires de secteurs superposés, dont **3** avec des plages z qui se chevauchent (géométrie impossible en vraie 3D) |
| Ouvertures des portails | 573 portails entre secteurs vivants ; ouverture verticale < 90 u : **47** (14 sous 47 u) ; longueur < 94 u : 301 (y compris les découpes internes de pièces) |
| Volume estimé | cellules 64 u : 36 212 (avec plafond de ciel) à 39 307 ; **cellules 128 u : 11 995 à 12 838** |

**Rôle des SE7** (SOURCE jfduke3d `actors.c:2728-2737`) : en secteur de lotag 0 avec `onfloorz = 0`, le joueur est transféré sans coupure dès que |z − z du sprite| < 6144, en gardant son décalage xy. Le toit est donc un « puits de chute » placé ailleurs dans le plan XY. Qu'`onfloorz` vaille 0 ici est une HYPOTHESE : les sprites sont très au-dessus des sols.

##### 2.2 HOLYWOOD (décodeur gen2) — MESURE

**Repère**
- Décalage xy par vote : (0, 0). 56,5 % des points PC tombent à 4 u d'un point Saturn (témoin décalé : 11,3 %).
- Loi verticale : `alt = −floorz/128 + 0` explique 76,1 % des 272 conteneurs à 2 u près. Moindres carrés sur 215 inliers : floorz/−128,23 + 0,4. Les autres lois testées restent à 4 % ou moins.
- La règle R1 (`z_dex = −z/128`, soit un monde en −z/256) donne une échelle 2 fois plus petite que Duke Saturn.

**Le toit (composante de 18 secteurs)**
- En place : 0 % des points retrouvés. Translaté du Δ SE7 /8 = (4382, −3352) : 9,3 %.
- Le vote donne **(4504, −3361) : 55,3 %**. Lobotomy a donc replacé le toit en vraie 3D, avec environ 122 u d'ajustement en X.
- Le décalage vertical reste indéterminé (votes −687:59, −176:54, +89:52).

**Structure**
- Y des sommets entre −192 et 1760, contre des plafonds Build jusqu'à 3640 : Saturn a plafonné le ciel.
- 3 592 plans, dont 1 520 portails (1 456 invisibles) et 830 horizontaux, soit **exactement 2 × 415** : un sol et un plafond par conteneur, comme en gen1.
- 238 plans à grille → 2 658 cellules (2 043 de 64 u, 337 de 128 u), plus 11 163 quads explicites, soit **13 821 primitives**.
- 213 paires d'empilement vertical (orientées).

##### 2.3 PowerSlave retail comme donneur (`ps_donor_stats.py`)

**MESURE**
- Sur 23/23 niveaux, les tuiles 16 bpp ne sont **pas** toutes en tête du jeu de tuiles. Mais l'index de tuile maximal vaut 71 pour les faces et 74 pour la table `textures`.
- Entre 25 et 183 chunks par niveau pointent vers des tuiles 16 bpp : on garde donc les tuiles du donneur intactes (décalées), on ne les filtre pas.
- Les sols sont la grille **découpée par l'empreinte du secteur** : 9 366/9 376 sommets de faces à l'intérieur du polyèdre (à 2 u) sur KARNAK, 4 704/4 704 sur TOMB. Faces dégénérées (triangles) : 300 sur KARNAK, 256 sur TOMB. Arêtes de 64 u (7 301) et 32 u (902) sur KARNAK. Sols en PARALLELOGRAM : 195/352 (KARNAK), 50/157 (TOMB).
- Faces + cellules de grille : 4 145 (KILENTRY) à 19 253 (MAGMA). Taille du bloc niveau : 171 185 à 773 018.
- « Demande » mémoire = taille + palettes + tuiles + séquences : 497 286 (KILENTRY) à **1 343 769 (SHRINE)**. SHRINE est le niveau retail le plus serré (21 328 o libres, `RETAIL_DISCS.md` §4).

**SOURCE**
- Plafond d'index des tuiles : `face.tile` est un `unsigned char` auquel on ajoute `tileBase` (LEVEL.C:69-72, SLEVEL.H:122). `tileBase` = 40 tuiles d'armes (`RETAIL_DISCS.md:69`). Donc **215 tuiles murales au maximum** ; la limite de 126 est propre à CONVERT.
- Faces et textures doivent être de classe TILE16BPP (asserts WALLS.C:1171 et 1235).
- Une tuile 16 bpp coûte 4 096 o en HWRAM (`mem_malloc(1)`, PIC.C:500-532).
- `MAXVPERWALL` vaut 700 (WALLS.C:975, 1020, 1207). Budget esclave de 1 300 polys : au-delà, le mur est **sauté silencieusement** (WALLS.C:1336, 1374-1375). VDP1 : 1 448 commandes (SRUINS.C:1879).
- `TILESIZE` n'apparaît que dans CONVERT et CUTLIST (grep). **Le moteur lit `tileLength`/`tileHeight` : la taille de cellule est libre.**
- Joueur PS : une **sphère de rayon F(47)** (AI.C:46), bornée au sol et au plafond par pos.y ± rayon (SPRITE.C:642, 703-708), avec `SPRITEFLAG_BSHORT` (AI.C:47).
  - Paramètres de départ : 5 shorts (secteur, x, y, z, angle) (OBJECT.C:191-194, 201-203, 231-234). L'angle vaut ×5760, soit 4 096 unités par tour, puis le moteur retire 90° (AI.C:51).
  - En face, Duke : rayon de collision 164 xy (player.c:2660, 3320), soit **20,5 u à /8** ; PHEIGHT 38<<8 (duke3d.h:140), soit 76 u.
- Nouvelle partie : `currentLevel = 3` et `levFlags[3] = CANENTER` (BUP.C:196-197), donc TOMB (BIGMAP.C:48). `GAMEFLAG_FIRSTLEVEL` ne touche que les déclencheurs Ramsès et les blocs montants (AI2.C:103, 237) : sans effet pour nous.

#### 3. Étapes du jalon M1

| # | étape | j | critère d'acceptation (hors console) |
|---|---|---|---|
| E0 | Écrivain « identité » : re-sérialiser la sortie de `lev.parse_lev` | 1 | 23/23 `.LEV` retail ré-émis **octet pour octet** (sha1 identique) |
| E1 | Import Build et nettoyage (voir détail ci-dessous) | 1 | une seule composante ; 0 secteur de hauteur ≤ 0 ; 0 auto-intersection de boucle ; nombre de secteurs vivants imprimé |
| E2 | Découpe convexe (voir détail ci-dessous) | 2,5 | tous les morceaux convexes (0,5 u) ; somme des aires = aire Build (±0,1 %) ; chaque arête interne a exactement un jumeau ; **≤ 520 morceaux** (cible, marge sous 600) ; aucun morceau < 1 u² |
| E3 | Géométrie 3D gen1 (voir détail ci-dessous) | 3,5 | contrôleur `lev_check` (voir ci-dessous) sans erreur |
| E4 | Tuiles, palette, ciel (voir détail ci-dessous) | 2 | N ≤ 215 (attendu 74) ; toutes les tuiles des faces sont en 16 bpp ; planche PNG des tuiles pour l'œil ; taille palettes = 2 + 512 × (palettes du donneur + 1) |
| E5 | Assemblage (voir détail ci-dessous) | 1 | `lev.py` : validate OK, `size == 56 + Σparts`, 0 octet résiduel, séquences à la bonne taille ; demande ≤ 1 250 000 (≤ 1 343 769 SHRINE en dur) ; `size` < 900 000 ; sphère de départ entièrement dans son secteur |
| E6 | Disque (voir §4) | 0,5 | l'ISO contient TOMB.LEV à notre taille et notre sha1 |
| E7 | Itérations console (le propriétaire lance l'émulateur) | 2-3 | table §4 |
| M1b | Toit en 3D : translation (4504, −3361) u mesurée sur HOLYWOOD, portail horizontal entre les deux puits, départ d'origine | 2 | les emprises des deux puits se recouvrent (aire ≥ 95 %) ; portail symétrique ; ≥ 50 % des points du toit à 4 u de HOLYWOOD |

**E1 — Import et nettoyage**
- Supprimer les 6 secteurs inaccessibles.
- Secteurs fermés, rendus dans un état **statique ouvert** (HYPOTHESE, à confirmer en console) : lotag 20 → plafond du voisin ; lotag 23 → vantaux supprimés ; lotag 27 → pont déployé (sol du voisin) ; lotag 0 fermés → volumes pleins.
- Plafonner les plafonds de ciel à Y ≤ 1760 u.
- Pour M1, exclure le toit : départ sur le SE7 A (secteur 259, (9206, 41590), Y = −64 + 48).

**E2 — Découpe convexe**
- Triangulation par oreilles avec pont vers les trous, puis Hertel-Mehlhorn, puis fusion gloutonne.
- Fusionner aussi des secteurs Build voisins identiques (hauteurs, picnums, pentes) quand leur union reste convexe.
- Scinder les arêtes de portail aux jonctions en T.

**E3 — Géométrie 3D gen1** (règles R3/R6/R7)
- Par arête : mur bas / portail INVISIBLE / mur haut ; diagonales en portails des deux côtés.
- Sol et plafond : quad englobant ; normales vers l'intérieur ; `d` ; `center` ; `floorLevel`.
- Murs rectangulaires en PARALLELOGRAM, avec (L+1)(H+1) < 700 ; trapèzes (pentes) en liste de faces.
- Sols : grille 128 u découpée par le polygone, découpée elle-même en quads ou triangles (sommet répété).
- Drapeaux BLOCKED, PARALLAX (ciel, sans faces), SHORTOPENING (1 < h < 90) et CLIFFBNDRY (> 320) (CONVERT.C:2555-2585).
- `nmVertex` pair (CONVERT.C:3228).

**Contrôleur `lev_check`** (appuyé sur `lev.py`)
- `validate() == []` ; ≤ 600 secteurs, ≤ 5 500 murs.
- Convexité : tous les sommets du secteur à ≥ −2 u de chaque plan.
- Le centre du secteur est strictement à l'intérieur.
- Exactement un sol et un plafond par secteur.
- Symétrie des portails à 100 %.
- Indices locaux des faces < 700.
- Coordonnées dans ±16 383.
- Faces + cellules entre 11 000 et 13 000, et ≤ 19 253 (maximum retail).

**E4 — Tuiles, palette, ciel**
- Lecture ART : en-tête de 4 int, tailles, `picanm`, pixels en colonnes.
- `PALETTE.DAT` : composantes VGA 6 bits → 15 bits BGR.
- Chaque picnum rééchantillonné en 64×64 ; échange d'index 0↔255, puisque la transparence Duke est à 255 (CONVERT.C:837-840, 1271-1277).
- Drapeaux 0x32 ; `palNm` = index de la palette Duke ajoutée.
- Ciel : bloc du donneur au départ, puis `LA` répété 4× et ramené à 512×256.

**E5 — Assemblage**
- Queue du donneur **KILENTRY** : demande 497 286, dont 326 101 pour la queue.
- Nos N tuiles en tête ; `chunk.tile` du donneur décalés de +N.
- Un objet `OT_PLAYER` : 5 shorts, y = sol + 48.

**Estimation du total** (HYPOTHESE) : environ 12 k primitives × 31-35 o ≈ 380-450 Ko de bloc niveau, + 74 × 4 100 o ≈ 303 Ko de tuiles, + 326 Ko de queue ≈ **1,05-1,08 Mo** de demande. Avec la découpe en 131 sous-tuiles : environ 1,3 Mo, à la limite.

#### 4. Disque et test par le propriétaire

**Intégration sans code**
1. Créer `build\e1l1\cd\` avec des liens durs vers les 96 fichiers de `cd\`, et remplacer `TOMB.LEV` par notre fichier.
2. Lancer `make iso CDDIR=build/e1l1/cd ISO=build/e1l1/e1l1.iso`. `CDDIR ?=` est en Makefile:252 ; `ISO :=` (253) se surcharge en ligne de commande.
3. Garder le build **debug** : les asserts affichent « Write This Down » avec fichier:ligne (UTIL.C:181).
4. Pour les chiffres, construire aussi `STATUSTEXT=1` (Makefile:63-66 : fps, polys, temps de calcul et de dessin, mémoire libre).

Alternative : `TESTCODE` (SRUINS.C:2453-2478) lance directement TEST.LEV avec tout l'inventaire. Mais aucun bouton du Makefile ne l'expose, donc c'est une modification suivie, à décider par le propriétaire.

Le propriétaire lance lui-même l'émulateur : nouvelle partie → carte → TOMB.

| ce que tu vois | ce que ça veut dire |
|---|---|
| « Write This Down » LEVEL.C:41 / PIC.C:701 / SEQUENCE.C:44 | taille du bloc niveau / drapeaux de tuile / taille des séquences : bug de l'écrivain |
| Assert WALLS.C:1171 ou 1235 | une face pointe vers une tuile non 16 bpp : index ou `tileBase` faux |
| Assert WALLS.C:1020 ou 1207 | mur de plus de 700 sommets : grille trop grande |
| Assert SPRITE.C:72-73 ou OBJECT.C:211-212 | secteur du joueur hors bornes / paramètres d'objet décalés |
| Chute sans fin, ou vue du vide au départ | point de départ hors de son secteur, ou sol mal orienté |
| Mauvaise direction au départ | signe ou décalage de la conversion ang → angle PS |
| Couleurs aberrantes, une seule texture partout | `palNm` ou index de tuile faux |
| Textures en miroir | ordre v0..v3 ou motif de retournement |
| Trous, traînées, murs vus à travers | portail non symétrique, secteur non convexe ou normale inversée |
| Murs qui disparaissent en tournant (rue ouverte) | budget esclave de 1 300 polys (WALLS.C:1374) : limite de perf, pas un bug ; noter l'endroit et le compteur de polys |
| Passage ou conduit infranchissable | la sphère de 94 u est plus grosse que l'ouverture (47 portails sous 90 u) ; noter l'endroit |
| Mur invisible qui bloque | BLOCKED posé sur un portail, ou secteur fermé mal ouvert |
| Blocage dans les escaliers | hauteur de marche PS (non vérifiée) |
| Sols qui « ondulent » de près | distorsion affine des quads de 128 u : attendu |
| Impact d'arme : sprite correct / sprite corrompu | séquences du donneur bien décalées / décalage des `chunk.tile` faux |

#### 5. Risques

1. **Plafond de 600 secteurs.** Bornes mesurées 471 à 618 ; Lobotomy a atteint 415. Parades : découpe soignée plus fusion ; en dernier recours, simplification manuelle du secteur 295 et des grands secteurs non convexes.
2. **Taille du joueur.** Sphère PS de rayon 47 contre Duke à 20,5 u : 47 ouvertures verticales sous 90 u, et la liste réelle des étranglements horizontaux reste inconnue. C'est le **risque principal pour « marchable »**.
3. **Performance en extérieur.** Environ 12 k primitives, comparable à HOLYWOOD (13 821) et sous le maximum PS (19 253). Mais la charge par image est inconnue : au-delà du budget, des murs disparaissent en silence.
4. **Mémoire.** Chaque tuile 16 bpp prend 4 Ko de HWRAM. La découpe en 131 sous-tuiles pousse la demande vers 1,3 Mo.
5. **Tri des secteurs empilés.** Pas de cut-planes au départ ; PEAK retail tourne avec 482 paires empilées et 0 cut-plane. Les 3 paires dont les z se chevauchent sont impossibles en 3D : correction manuelle.
6. **Fonctions Duke perdues.** Sprites muraux, murs masqués, textures animées, pan/repeat des textures, portes et ascenseurs figés, ombrage remplacé par une lumière constante.
7. **Conversion de l'angle de départ.** Signe et décalage sont des HYPOTHESES, à valider avec la table §4.

#### 6. Décisions qui reviennent au propriétaire

1. **Voie B** (recommandée) plutôt que la voie A.
2. **Repère Duke Saturn** (x/8, −y/8, −z/128, mesuré) plutôt que R1 (/16, /256). Le choix conditionne la taille du joueur.
3. **Rayon du joueur** : garder F(47), le réduire à environ 24 u (AI.C:46, modification suivie qui touche aussi les niveaux PS, donc plutôt derrière un bouton), ou passer l'échelle à ×2 (quatre fois plus de cellules).
4. **Cellules de 128 u** (recommandé) ou 64 u, non viable sans simplification lourde.
5. **Tuiles** : 74 rééchantillonnées (303 Ko) ou 131 sous-tuiles (537 Ko, au-dessus des 101 tuiles 16 bpp retail maximum, soit 414 Ko).
6. **Donneur KILENTRY.** La queue de TOMB est trop lourde (848 Ko).
7. **Emplacement** : TOMB sans code, ou TEST avec `TESTCODE` via un bouton du Makefile.
8. **Toit** : l'exclure pour M1 puis le placer en 3D en M1b, ou le faire tout de suite.
9. **État statique** des portes et ponts ; pentes gardées ou aplaties ; ciel du donneur ou `LA` ; lumière constante ou dérivée de l'ombrage Duke.

#### 7. Méthode de comparaison avec Duke Saturn

Notre `.LEV` et HOLYWOOD sont dans **le même repère** (décalage 0) : on les compare directement. Un script `compare_holywood.py` lirait les deux (`lev.py` pour le nôtre, `gen2_lev.parse` pour HOLYWOOD) et imprimerait le tableau suivant. Pour relier chaque conteneur à son secteur Build, il projette les centres dans la carte PC par test point-dans-polygone.

| métrique | HOLYWOOD (mesuré) | nous |
|---|---|---|
| secteurs après découpe / secteurs Build | 415 / 317 = 1,31 | à mesurer (bornes 471-618) |
| découpe par secteur Build | conteneurs par secteur (272 conteneurs tombent dans un seul secteur plat) | morceaux par secteur → carte des endroits où Lobotomy a moins découpé |
| plans ou murs ; portails ; horizontaux | 3 592 ; 1 520 (1 456 invisibles) ; 830 = 2 × 415 | nmWalls ; portails ; 2 × secteurs |
| primitives ; taille de cellule | 13 821 (2 658 cellules + 11 163 quads) ; 64 u dominant, 128 u × 337 | faces + cellules ; 128 u |
| couverture géométrique | 56,5 % des points PC | 100 % par construction ; la différence liste, secteur par secteur, ce que Lobotomy a retiré |
| hauteurs | Y −192..1760 (ciel plafonné) | après plafonnement |
| empilement vertical | 213 paires | à mesurer |
| zones SE7 | toit à (4504, −3361), 55,3 % | position choisie en M1b |
| tuiles et lumière | non mesurées ici (la queue gen2 de Duke n'est pas décodée jusqu'au bout, `RETAIL_DISCS` §5) | 74 tuiles ; lumière constante |

Plus une vue de dessus superposée des deux niveaux en PNG sous `build\`.

#### 8. Fichiers

Tous dans `C:\Users\pcico\Projects\SlaveDriver-Engine\build\tmp-gen2\`, en lecture seule sur les données, sortie console uniquement :

- `e1l1_plan_stats.py` : E1L1 et HOLYWOOD, repère et loi verticale.
- `e1l1_plan_stats2.py` : composantes, plafond du ciel, secteurs fermés, chevauchements.
- `e1l1_plan_stats3.py` : placement du toit, estimation des faces.
- `e1l1_plan_stats4.py` : ouvertures des portails, vertical du toit.
- `ps_donor_stats.py` : ordre des tuiles, séquences, sols et demande mémoire des niveaux retail.
- `voieA\` : `convert.o`, `dexshell.o`, `convert.exe`, `dummy_lev.c`, `dexwrap.c` (preuve que CONVERT compile et se lie).

## 5. Résultats E0-E2 (workflow du 2026-09-10)

Chaque étape : implémentation, puis vérificateur contradictoire écrit sans importer le code testé. Les
trois ont passé du premier coup (0 tour de correction). Code dans `tools\duke2ps\` (non suivi, jamais
commité), sorties dans `build\duke2ps\`, notes détaillées dans `tools\duke2ps\NOTES_E0/E1/E2.md`.
Aucun fichier suivi modifié (`git status --untracked-files=no` vide).

### E0 — écrivain `.LEV` (`lev_io.py`, `lev_write.py`) : PASS

- **24/24 niveaux retail ré-émis à sha1 identique** (24 et non 23 : TEST.LEV = SANCTUAR.LEV). Le modèle ne
  contient ni offset, ni compteur, ni taille : l'écrivain recalcule `size`, les 14 compteurs, les tailles
  des sons, des RLE, du bloc palettes (2 + 512·N) et des séquences.
- Vérificateur : 2 486 mutations champ par champ, 156 éditions structurelles, 42 témoins négatifs, 12
  modèles composites (anti-recopie) — tout passe.
- 63,6 % des octets restent des blobs opaques, parce que le chargeur les copie sans les décoder : ciel,
  PCM, pixels des tuiles, RLE.
- Pièges pour E3/E5 : `wall.flags` est un short **signé** (EXPLODABLE 0x8000 à donner négatif) ; pas de
  constructeur à partir de zéro, E3/E5 partent d'un modèle donneur (`lev_io.read_model`). Contrôles
  mémoire (sons < 80, SCSP, VDP2) laissés à E5.

### E1 — import d'E1L1 (`build_import.py` → `e1l1_import.json`) : PASS

- **276 secteurs vivants, 1 648 murs, 572 sprites** ; une composante ; 1 036/1 036 murs rouges symétriques ;
  0 hauteur ≤ 0 ; 0 boucle auto-intersectée (la carte d'origine en a 14, toutes causées par des portes
  fermées : vantaux, rideaux, panneaux coulissants).
- Retirés : le toit (18 secteurs) et 5 petits îlots dont une salle de commandes (126/186/187) reliée à la
  partie gardée par des hitags de déclenchement, pas par des portails.
- États figés choisis (HYPOTHÈSES à valider sur console) : portes lotag 20 ouvertes au plafond voisin
  (101 et 286 ne s'ouvrent qu'à 64 u, comme dans Duke) ; vantaux lotag 23 supprimés ; lotag 27 = **rideaux
  de 8 u tirés par un SE20**, pas des ponts, remplacés par de l'air ; coulissantes lotag 25 ouvertes ;
  4 secteurs lotag 0 fermés rendus pleins. Les 21 secteurs que Duke ferme au spawn (SE13 ×18, SE32 ×3)
  restent ouverts, dans l'état du fichier.
- Ciel : 16 plafonds ramenés à Y 1760.
- Départ M1 : SE7 552 (toit) apparié au SE7 84, secteur Build 259 ; Saturn X 1150,75, Z −5198,75, sol
  Y −64 ; obstacle réel le plus proche à 57,3 u (> rayon 47).
- Hauteurs (utile pour le bouton de rayon) : sur 518 passages, 46 < 90 u, 7 < 47 u, 3 ≤ 0 (55↔109 à
  −120 u, 5↔203, 174↔266) ; 41 secteurs < 94 u aux sommets, minimum 28 u (secteur 122).

### E2 — découpe convexe (`convex.py` → `e1l1_convex.json`) : PASS

- **446 morceaux convexes**, en entiers exacts, sans point de Steiner : 215 secteurs déjà convexes, 61
  découpés (456 morceaux), puis 10 fusions de secteurs voisins identiques. Aires exactes (écart 0),
  1 428 arêtes internes toutes jumelées, 0 jonction en T, plus petit morceau 7,5 u².
- Bornes recalculées sur 276 secteurs : 379-481. **HOLYWOOD : 298 morceaux contre 292 conteneurs** sur les
  170 secteurs appariés = quasi-parité. Écarts : 295 (salle ronde à 6 trous) 28 contre 18, puis 210 +4,
  200 +4, 163 +3.
- Réserves du vérificateur (non bloquantes, pour E3) :
  - **181 paires de morceaux se recouvrent en XY**, parce que 102 paires de secteurs Build se superposent
    (normal en Build). La partition est exacte secteur par secteur, pas globalement ; 7 paires ont des
    plages z brutes qui se chevauchent.
  - **Éclats** : 8 morceaux de moins de 1 u de large (minimum 0,125 u), hérités de la géométrie Build →
    risque en virgule fixe.
  - La clé de fusion ignore `visibility`, et les SE des secteurs fusionnés se perdraient si l'on revient
    au dynamique (`--no-fusion-secteurs` les garde séparés).

### Hauteurs : ce qu'a fait Lobotomy dans HOLYWOOD (MESURE, `build\tmp-gen2\e1l1_tall_ceilings*.py`)

Méthode (HYPOTHÈSE de rattachement) : un conteneur HOLYWOOD appartient au secteur Build dont le
polygone contient le centroïde de ses sommets, dans le repère X = x/8, Z = −y/8, Y = −z/128.

- **Y max global de HOLYWOOD = 1760**, mais cette hauteur ne vaut que pour le puits du départ (Build 258,
  259, 308, où le joueur tombe depuis le toit).
- **Les ciels de la ville, à Y 3640 dans Build, sont coupés à Y 960** (153, 182-184, 204, 208, 210, 261) ;
  les ciels bas 211/212 (448) sont au contraire montés à 960.
- Plafonds intérieurs hauts : 154 (3400) → 837 ; 163 (3384) → 960 ; 169 (3384) → 552 ; les conteneurs
  sous 203 (3400) s'arrêtent à 504.
- **La pièce 306** (sol 3216, 120 u de haut) est **descendue de 2 563 u** : conteneur 211, Y 653..773,
  même hauteur de 120 u.
- 6 secteurs n'ont aucun conteneur rattaché (164, 185, 209, 262, 293, 294) : non concluant pour eux.
- Parmi les 276 secteurs gardés, aucun sol ne dépasse Y 576, sauf 306. Un plafond à 960 ne coupe donc rien.

### Décisions avant E3

1. Plafonds : en attente, après la mesure HOLYWOOD ci-dessus (proposition : 960, 1760 pour le puits
   258/259/308, 306 descendu à 653 comme Lobotomy).
2. **Plateforme lotag 17 n° 55 figée en position basse** (owner, ok).
3. **Secteurs lotag 3 (4) et secrets 32767 (6) laissés en secteurs ordinaires** pour M1 (owner, ok pour l'instant).
4. **Passages trop bas acceptés pour M1** (7 < 47 u, 46 < 90 u ; PowerSlave ne s'accroupit pas) ; à
   reprendre comme amélioration plus tard (owner).

## 6. Taille du joueur : debout, accroupi, rétréci (étude du 2026-09-11)

Demande du propriétaire : s'accroupir et être rétréci dans le fork ; la vue suit la taille du personnage
(pas de décalage œil/boule) ; la taille standard doit passer les ouvertures standard. Détail dans
`tools\duke2ps\NOTES_PLAYER.md` et `build\duke2ps\player\openings_report.md`.

**Duke** (SOURCE jfduke3d, converti en u Saturn) :

| Posture | Œil au-dessus du sol | Hauteur libre nécessaire | Marche franchissable |
|---|---|---|---|
| Debout | 80 u | 88 u | 40 u |
| Accroupi | 36 u | 44 u | aucune |
| Rétréci | 16 u | 24 u | aucune |

- Le rayon reste **20,5 u dans toutes les postures** (clipdist 164 constant, player.c:3320/3368) :
  rétréci, Duke passe sous des plafonds plus bas, jamais dans des passages plus étroits.
- Rétrécissement : 9,6 s ; mort écrasé à la fin si la hauteur libre fait moins de 48 u. Le joueur est
  rétréci par le NEWBEAST de l'Atomic Edition (122 dans 11 cartes), par son propre tir renvoyé par un
  miroir, ou par un autre joueur.

**PowerSlave** : l'œil est à **55 u** du sol, pas 47, parce que la caméra flotte 8 u au-dessus du
contact de la boule (SPRITE.C:643-644).

**Ouvertures** (MESURE, 128 cartes, 92 886 passages) : l'ouverture standard fait 128 u de haut sur 64 ou
128 u de large ; 5 % des passages franchissables font moins de 64 × 62 u.

**Verdict : une boule seule ne suffit pas.** Elle a besoin de 2R en largeur ET en hauteur, et c'est la
largeur qui limite :

| Rayon de la boule | Passages debout couverts | Œil |
|---|---|---|
| 47 | 83 % | 55 u |
| 31 | 97 % | 39 u |
| 20,5 | 100 % | 28,5 u |

- Une boule qui rétrécit ouvre aussi des milliers de fentes que Duke ne franchit jamais : 1 150 à R 18,
  7 753 à R 8.
- Recommandation de l'étude : **un cylindre vertical pour la seule caméra** — rayon 20,5, pieds à 80, 36
  ou 16 u sous l'œil selon la posture. Il couvre 100 % des passages de chaque posture, sans champ ajouté
  au sprite. Diff proposé derrière `DUKEPLAYER=1` (arbre `build/duke`, `DUKEPLR.C/H`), NON appliqué ;
  bouton Y = s'accroupir, Z = cycle d'armes.
  **ADOPTÉ (owner, 2026-09-11)** : cylindre et ce mappage des boutons. Reste à trancher : la marche
  franchissable accroupi (0 u comme Duke, ou 32 u).
- Correctif gratuit côté données, vérifié : `WALLFLAG_SHORTOPENING` (0x1000, SLEVEL.H:143) bloque les
  sprites qui portent `SPRITEFLAG_BSHORT`, c'est-à-dire le joueur (AI.C:47 ; `WALLFLAG_BLOCKBITS` 0x1f00,
  SLEVEL.H:138 ; tests SPRITE.C:218, 273, 414, 435). Le convertisseur peut le poser sur les passages que
  Duke ne franchit pas.

## 7. Règles de hauteur H1-H3 (étude du 2026-09-11)

Module `tools\duke2ps\height_rules.py`, notes `tools\duke2ps\NOTES_HEIGHTS.md`, rapport
`build\duke2ps\rules\heights_report.md`. L'étude a été coupée par une limite de quota avant son rapport
final, qui a été régénéré depuis ses scripts. Vérification contradictoire lancée le 2026-09-11 puis coupée
par la limite de session : seule la lentille **budget** a tourné, et elle a **réfuté** la section Budget
(ci-dessous). Les lentilles code et statistiques et le correctif n'ont pas tourné : la règle `fov_bas` et
le constat Lobotomy restent donc NON vérifiés, à l'exception de la section budget.

- **27 paires PC ↔ Saturn** sur E1-E3 (23 cartes, 4 scindées en deux `.LEV`) ; 7 cartes PC sans paire,
  3 `.LEV` sans carte (LORD2, STADIUM, UREA51).
- **Constantes du moteur (SOURCE)** : œil 55 u ; focale 160 px, fenêtre y −110..90 ⇒ demi-champ vertical
  haut 34,5° (tan 0,6875) ; apogée du saut 56 u, point le plus haut atteint 158 u ; pas de plan lointain
  (`ENABLEFARCLIP 0`, mais `FARCLIP F(1024)` défini).
- **Constat** : Lobotomy n'applique **aucune loi unique**. Le Y max par niveau va de 64 à 2 048. Il garde
  presque tous les plafonds non ciel jusqu'à 1 024 u, baisse les ciels très hauts vers une valeur par
  niveau, remonte parfois les ciels bas à cette même valeur, et recolle des zones entières en Y. Deux
  ciels voisins peuvent avoir des hauteurs différentes : la marche entre eux est un mur parallaxe.
- **Règle retenue `fov_bas`** (H1 + H2 + H3, portée de vue D = 1 024 u, arrondi à 128 au-dessus du sol),
  ajustée sur E1L1 seul :
  - E1L1 : ciels 3 640 → 952 (Lobotomy 960) ; 154 → 832 (837) ; 203 → 624 (504) ; la pièce 306 descend de
    2 712 u (2 563) ; 163 → 720 (960) ; 169 → 720 (552).
  - Hors E1L1 : 44,9 % des secteurs à ±64 u de Lobotomy, écart moyen 235 u. Garder les hauteurs Build
    donne 46,9 % mais 250 u d'écart moyen. **Aucune famille de règles ne reproduit Lobotomy**, qui a
    vraisemblablement réglé à la main, par niveau (HYPOTHÈSE).
  - D = 1 024 est exactement le `FARCLIP` défini mais désactivé de PowerSlave (HYPOTHÈSE : activé en gen 2 ?).
- **Budget — RÉFUTÉ par la vérification (2026-09-11).** L'affirmation « la hauteur du ciel ne coûte presque
  rien par image » est fausse : le slave réserve ses 1 300 polys sur le nombre **total** de cellules de
  chaque mur (`height*width`, WALLS.C:1374) avant tout rejet à l'écran ; un ciel plus haut ⇒ des murs plus
  hauts ⇒ des murs entiers sautés silencieusement. Le modèle d'octets ignorait sols/plafonds et confondait
  les deux stockages (parallélogramme = 2 o/cellule, faces explicites = 10 o/cellule), donc classait mal :
  DUKEDCSL (~809 ko) et DUKEDC4 (~772 ko) sont au bord des 900 ko mais étaient déclarés sûrs. Sur 91 cartes,
  la règle budget ne baisse en fait aucun ciel. **À refaire sur le vrai modèle du slave + les vrais octets.**
- **Correction mesurée (2026-09-11, `tools\duke2ps\budget_correct.py`)** : un mur de `ceil(L/128)·ceil(H/128)`
  > 1 250 cellules est toujours sauté par le slave. Confirmé : **E1L5** a 2 tels murs à ciel Build (0 après
  ciel ≤ 1 024 u, 98 492 → 72 327 cellules) ; **dz2/E1L5** en a 7 (4 restent même à 1 024 u → il faut découper
  les faces). E1L1 est petit : aucun mur ne dépasse. Trois budgets statiques (secteurs ≤ 600, murs ≤ 5 500,
  octets < 900 000 — DUKEDC4/DUKEDCSL ont ~1 020 secteurs Build, au-dessus de 600) + un budget par image
  (1 250 cellules de slave). Détail : `tools\duke2ps\NOTES_BUDGET.md`.
- **Trouvaille pour E3** : `MAXVPERWALL` (700) n'est pas un problème de ciel. Sur E1L5, deux faces font
  1 494 et 1 743 sommets — des murs pleins de 2 128 et 2 490 u de long. Il faudra **découper les faces**
  dans le convertisseur, quel que soit le plafond.

## 8. E2.5 / E2.5b — quantification sur la grille du `.LEV` (2026-09-11)

Garde-fou demandé par le propriétaire : « la conversion doit être la même des deux côtés d'une jonction ;
s'assurer que l'arrondi est le même si deux sections avaient un décalage de 2 u ou moins ».
Modules `tools\duke2ps\quantize.py` et `tools\duke2ps\snapmap.py`, notes `tools\duke2ps\NOTES_QUANT.md`.

**Principe** : le `.LEV` ne stocke que des `short` entiers (SLEVEL.H:115-118), donc la résolution est de
1 u = 8 unités Build. On quantifie **tôt**, dans l'espace Build, sur la liste de sommets partagée, AVANT la
découpe convexe : la conversion aval (X = x/8, Z = −y/8, Y = −z/128) devient alors **exacte**, sans aucun
arrondi tardif, et les invariants déjà vérifiés sont recontrôlés après.

**E2.5 — le garde-fou** : 8 critères durs, tous OK sur E1L1. 0 coordonnée hors grille, **0 jonction plate
scindée par l'arrondi**, symétrie des murs rouges intacte (1 036 paires), une seule composante connexe,
départ dans son secteur. Les soudures ≤ 2 u ne se déclenchent pas sur E1L1 : la carte était déjà exacte.

**E2.5b — préservation des features sub-résolution** : 27 groupes de coordonnées d'E1L1 tombent sur le même
point de grille. Trois bornaient de vraies features que l'arrondi détruisait — notamment le **secteur 23**,
qui n'est pas une encoche mais un **mur-secteur d'une unité Build d'épaisseur** séparant les secteurs
1/19/21 des secteurs 2/20/22, et le **secteur 122**, un cadre filiforme autour des secteurs 120/121. La
règle : arrondir normalement, sauf quand la collision crée une dégénérescence (arête nulle, sommet répété,
ou sommet tombant à l'intérieur d'une arête non adjacente) ; les valeurs en cause sont alors écartées d'un
cran de grille, en gardant fixe celle déjà sur la grille, **en préservant l'ordre** et en n'écartant que les
paires réellement contraintes.

| | E2 (non quantifié) | E2.5b (quantifié) |
|---|---|---|
| Boucles dégénérées | 1 | **0** |
| Murs de longueur nulle | 8 | **0** |
| Morceaux convexes | 446 | **440** |
| Morceaux < 1 u de large | 7 | **0** |
| Morceau le plus mince | 0,250 u | **1,315 u** |
| Aire minimale | 7,50 u² | **32,00 u²** |

`convex.py` sur le modèle quantifié : **tous les critères OK** (440/440 strictement convexes, aires exactes,
1 420 arêtes jumelées, 0 jonction en T). Dérive maximale d'un sommet : 0,875 u, soit moins d'un pas de
grille. **La réserve d'E2 sur les éclats sub-unitaires est levée.** Restent déférés à E3 : 8 petits écarts
aux jonctions de pente (plan incliné continu contre hauteur entière).

## 9. E3 — géométrie 3D génération 1 (2026-09-11)

`tools\duke2ps\geom3d.py` + vérificateur indépendant `verif_e3.py` → `build\duke2ps\e1l1_geom3d.json`.
Détail complet dans `tools\duke2ps\NOTES_E3.md`.

**Méthode** : aucune convention du format n'a été devinée. Chacune est mesurée sur les 24 `.LEV` retail
*et* relue dans `UTIL\CONVERT.C` (l'émetteur de Lobotomy) et `WALLS.C` (le moteur) — les deux sources se
recoupent à chaque fois. Sol et plafond sont des **murs** du secteur (exactement un de chaque dans les
**8 408** secteurs retail), la grille de tuiles fait **64 unités**, les faces portent des indices de
sommet **locaux au mur**, et un parallélogramme stocke 2 octets par cellule (`[motif, tuile]`) plus
`(L+1)(H+1)` octets de lumière.

**Deux corrections à des conclusions antérieures :**

1. **La cellule fait 64 unités, pas 128** (`SLEVEL.H:126`, `CONVERT.C:1802`). La section budget
   « corrigée » du matin sous-estimait donc la réservation du slave d'un **facteur 4**. Chiffres
   refaits sur 128 cartes : **421** murs hors budget aux hauteurs Build, **183** après plafond de ciel
   à 1 024 u (E1L1 lui-même en a 1). Le plafonnement du ciel ne règle que 57 % des cas ⇒ **le découpage
   des faces est le mécanisme principal, la règle de hauteur n'est qu'un appoint.**
2. **« Max 748 cellules en retail » était une erreur de catégorie de ma part** : `tileLength·tileHeight`
   ne compte les cellules que pour un mur *parallélogramme*. Vraies bornes mesurées : **242** cellules
   pour le plus gros parallélogramme retail, **376** faces pour le plus gros mur à faces. Bornes dures :
   `assert(tileLength*tileHeight < 700)` (`WALLS.C:1017`) et `vCalc[700]` rempli sur `(L+1)(H+1)`.

**Quatre défauts trouvés par le vérificateur** (le générateur, lui, passait ses propres critères du
premier coup — c'est la raison d'être du vérificateur séparé) : portail en nœud papillon quand
l'ouverture se referme en cours d'arête entre deux pentes ; pavage de sol qui écrasait un éclat de
0,66 u (moitié du sol perdue) ; erreur d'arrondi qui se composait de coupe en coupe (1,12 u → 0,49 u
en ancrant chaque croisement sur l'arête *d'origine*) ; découpe désynchronisée entre les deux faces
d'un portail. Un cinquième défaut a été trouvé en relisant le moteur : **un trapèze ne peut pas être
marqué `PARALLELOGRAM`**, car `drawRectWall` reconstruit sa grille avec deux vecteurs constants — et
les **11 712** murs `PARALLELOGRAM` du retail vérifient `v0+v2 == v1+v3` avec un écart exactement nul.

**Résultat (MESURE)** : 440 morceaux → **440 secteurs, 3 564 murs, 31 555 sommets, 10 930 faces**, bloc
niveau **589 627 octets** (limite 900 000). 12/12 critères du générateur OK, **aucune mise en défaut**
sur les 11 propriétés du vérificateur (dont l'étanchéité verticale, l'étanchéité du pavage, et le bord
du sol à moins de 0,49 u).

**Étalonnage retail** — E1L1 tombe *à l'intérieur* de la distribution des 24 niveaux de Lobotomy, avec
des ratios par secteur quasi identiques :

| | secteurs | murs | sommets | faces | octets | murs/sect | faces/sect |
|---|---|---|---|---|---|---|---|
| médiane retail | 349 | 2 881 | 27 232 | 10 394 | 474 351 | 8,3 | 29,8 |
| **E1L1 (nous)** | **440** | **3 564** | **31 555** | **10 930** | **589 627** | **8,1** | **24,8** |
| max retail | 570 | 4 939 | 44 906 | 16 913 | 762 614 | 8,7 | — |

**Reste HYPOTHÈSE** : rien n'a encore été *rendu* (tout est géométrique ou structurel ; l'aspect se juge
en E7 sur console) ; tuiles, motifs de coin et lumières sont des bouchons pour E4 ; le picnum du bas et
du haut d'un portail est celui du mur alors que Build les distingue ; testé sur E1L1 seulement.

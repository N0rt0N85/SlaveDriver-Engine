# Faire tourner Doom sur le moteur SlaveDriver — étude (2026-09-12)

Question posée : *utiliser le moteur pour faire tourner Doom ; récupérer tout ce qui n'est pas du
rendu ; convertir les WADs en `.LEV` ou format propre.*

Réponse courte, en trois phrases. **(1)** La coupure demandée existe et elle est nette : le
renderer, c'est **26 % des lignes** de Doom, et le reste ne l'appelle que par **une dizaine de
fonctions**. **(2)** Ce qui lie vraiment le playsim au renderer, ce ne sont pas des appels, ce sont
les **structures de carte** (`sector_t / line_t / seg_t / subsector_t / node_t`) — donc la bonne
architecture n'est pas « porter Doom au format `.LEV` » mais **garder les structures Doom pour le
jeu et en DÉRIVER un modèle de rendu au format PowerSlave**. **(3)** Toutes les mesures de
capacité passent, sauf une qui est juste à la limite : **E1M6 = 606 sous-secteurs contre
`MAXNMSECTORS 600`**.

`[mesuré]` = calculé ce jour (`tools/study/`, cf. `tools/study/README.md`) sur `DOOM1.WAD`
shareware v1.9 et les 24 `.LEV` retail. `[src]` = lu dans ce dépôt ou dans `../Mimas/core/`.

---

## 1. Pourquoi la question se pose maintenant

Le dossier `POWERSLAVE_GAP_VERDICT.md` (2026-08-31) a établi que l'écart PowerSlave/Mimas est une
**classe d'architecture** : SlaveDriver n'a aucune boucle par pixel côté CPU, son coût est
`k1*secteurs + k2*tuiles + k3*commandes`, l'occlusion est du user-clip par secteur plus un peintre
en O(portails). Mimas paie un plancher proportionnel à l'écran.

Et la branche `psw-world` de Mimas — 107 rounds — a construit *dans Mimas* un peintre tout-VDP1
qui plafonne : l'émission y coûte **95-100 µs par commande** (mesure round 28, linéaire en nombre
de commandes, identique en couloir et en scène ouverte). C'est ce plafond-là que « faire tourner
Doom sur SlaveDriver » contourne : **on ne réécrit pas un émetteur, on emprunte celui qui marche.**

**La comparaison symétrique existe depuis le 2026-09-12** (`STEXT_BASELINE_2026-09-12.md`,
5 captures console sur notre Duke E1L1 converti) :

| | µs / commande VDP1 émise | source |
|---|---|---|
| **SlaveDriver** | **39,2** (+ un fixe de 14,9 ms/frame) | console, build assert |
| Mimas, loi L4 | 64,5 | A/B console 2026-08-31 |
| Mimas, `psw-world` r28 | 95-100 | 4 captures console |

**L'hypothèse centrale de ce document est donc vérifiée** : l'émetteur de SlaveDriver coûte 1,6× à
2,5× moins cher, asserts allumés. ⚠ Reste à mesurer ce que vaut ce chiffre en **`-NDebug`** : il y a
deux `assert()` **par cellule**, dont un appel de fonction (`WALLS.C:1142,1172`).

---

## 2. La coupure « rendu / pas rendu » dans Doom — mesurée

`[mesuré]` sur `../Mimas/core` (85 `.c`, 72 594 lignes) :

| bloc | fichiers | lignes | part |
|---|---|---|---|
| **renderer** | `r_bsp r_data r_draw r_main r_plane r_segs r_sky r_things r_parallel i_scale` | 19 155 | 26 % |
| playsim | `p_*.c` (19 fichiers) | 16 522 | 23 % |
| jeu / UI / son / réseau | `g_ m_ st_ wi_ hu_ f_ am_ d_ s_ i_sound i_system deh_` | 22 075 | 30 % |
| données et services | `info tables w_wad z_zone v_video` | 9 959 | 14 % |

**Le point décisif** `[mesuré]` : en dehors des `r_*.c`, tout Doom n'appelle le renderer que par
**dix symboles** — `R_TextureNumForName` (18 usages), `R_PointToAngle2` (12), `R_SetViewWindow` (8),
`R_ExecuteSetViewSize` (8), `R_RenderPlayerView` (6), `R_PointInSubsector` (6), `R_FlatNumForName`
(5), `R_GetColumn` (4), `R_DrawPlayerSprites` (4), `R_PrecacheLevel` (3). Sur ces dix :

* `R_PointInSubsector` et `R_PointToAngle2` ne sont **pas du rendu** : ce sont des utilitaires de
  géométrie (descente BSP, `atan` tabulé). Ils restent tels quels.
* `R_TextureNumForName` / `R_FlatNumForName` sont de la **résolution de noms de lumps**. Ils
  deviennent « index de tuile dans le `.LEV` ».
* `R_RenderPlayerView` et `R_DrawPlayerSprites` sont **les deux seuls vrais points de bascule**.

Autrement dit : **Doom est déjà découplé de son renderer.** Ce qui ne l'est pas, c'est sa
représentation du monde.

---

## 3. Les trois architectures possibles — et celle qu'il faut prendre

### A. « Doom devient un jeu PowerSlave » — réécrire les règles de Doom dans le modèle objet du moteur

Le moteur a son propre framework de jeu : `Object` de 144 octets avec un `messHandler`
(`OBJECT.H:30-36`), messages `SIGNAL_*`, `Sprite` avec collision **par plans** (`SPRITE.C:940-960`,
`pointInSectorP`), IA de 6 335 lignes (`AI.C`), armes (`WEAPON.C`). Écrire « Doom » là-dedans, c'est
réécrire `p_*.c` à la main.

**Rejeté** : ça jette précisément ce que la question demande de récupérer, et ça garantit des règles
approximatives (le comportement de Doom est un fait, pas une intention).

### B. « SlaveDriver devient le renderer de Mimas » — importer le peintre dans Doom

C'est ce que fait `psw-world` depuis 107 rounds, avec l'émetteur *réécrit*. Importer l'émetteur
**verbatim** (le couple `rectTransform` en assembleur + `getCmdTable`/`flushCmdBuffer` + DMA) au
lieu de le réécrire est une variante défendable — le staging SlaveDriver a d'ailleurs déjà été
emprunté au round 28.

**Écart fatal** : l'émetteur de SlaveDriver n'est pas rapide *tout seul*, il est rapide **parce que
la donnée est pré-mâchée** — secteurs convexes, cellules de 64 unités cuites hors ligne, portails
triés en tête, lumière par sommet pré-calculée. Sans le format, on n'emprunte que la moitié qui
compte le moins. (C'est la leçon `lobotomy-gen2-findings` : Lobotomy cuit hors ligne exactement la
décomposition que `psw` recalcule à chaud.)

### C. **Playsim Doom + modèle de rendu PowerSlave dérivé** — recommandé

```
  WAD  --(chargement Doom normal, p_setup.c intact)-->  sector_t/line_t/seg_t/node_t/blockmap
                                                              |  (le playsim vit ici, inchangé)
                                                              v
  WAD  --(conversion hors ligne)-->  .LEV  --(LEVEL.C)-->  level_sector/level_wall/level_vertex
                                                              ^  (le renderer vit ici, inchangé)
                                                              |
                            TABLE DE LIAISON  doom_sector -> [pièces convexes]
                                              doom_side   -> [murs .LEV]
                                              mobj        -> Sprite
```

Le jeu (thinkers, IA, armes, dégâts, portes, ascenseurs, secrets, sauvegarde, menu, HUD, son)
tourne sur les structures Doom **sans modification**. Chaque tic, une passe de **synchronisation**
recopie dans le modèle `.LEV` ce qui a bougé : hauteurs de sol/plafond, textures commutées,
positions et frames des mobjs. Puis `drawWalls()` du moteur dessine.

**Pourquoi c'est la bonne** : c'est la seule qui répond littéralement à la question (« récupérer
tout ce qui n'est pas du rendu »), et c'est la seule où les deux moitiés restent *chacune* dans son
domaine de validité éprouvé.

**Prix à payer, nommé d'emblée** : deux représentations du monde en RAM en même temps. Voir §7.

---

## 4. La conversion WAD -> `.LEV` — ce qui est déjà écrit, et ce qui manque

Le fork a **déjà** une chaîne Build -> `.LEV` complète et validée console
(`docs/DUKE_PC_TO_SATURN.md`, `tools/duke2ps/`, étapes E0 à E6, E1L1 jouable sur console le
2026-09-12). Un WAD Doom est une entrée **plus simple** qu'une carte Build sur presque tous les
axes. Réutilisable tel quel :

| brique existante | réutilisable pour Doom ? |
|---|---|
| `lev_io.py` / `lev_write.py` (ré-émission `.LEV` sha1-identique sur 24 niveaux) | **oui, tel quel** |
| `convex.py` (décomposition convexe de secteurs) | **oui** — mais c'est le goulot connu : échoue sur 161/194 cartes Build. Doom a une alternative gratuite, voir ci-dessous |
| `quantize.py` / `snapmap.py` (grille entière, soudure <= 2 u, anti-dégénérescence) | **oui** — Doom est déjà en entiers, donc en grande partie sans objet |
| `geom3d.py` (murs = quads, sol/plafond = murs du secteur, faces à indices locaux) | **oui, tel quel** |
| `verif_e3.py` (11 critères indépendants) | **oui** |
| `duketiles.py` (ART -> tuiles 64x64, palette BGR555, échange index 0/255) | **à refaire pour le WAD**, plus simple (voir §4.3) |
| `assemble.py` (donneur, insertion des tuiles en tête, décalage des séquences) | **oui** |

Les **trois invariants durement acquis** en E5/E6 s'appliquent identiquement et doivent être des
critères d'acceptation dès le premier `.LEV` Doom :

1. **Les portails sont EN TÊTE de la liste des murs d'un secteur** — `findDoorways` fait
   `if (nextSector == -1) return;` (`WALLS.C:1740-1743`). 0 violation sur 8 211 secteurs retail.
2. **Les tuiles de géométrie sont exactement `flags 0x32`** (64x64, palette) — pas `0x72` (les
   mêmes en RLE = des sprites).
3. **Un ciel `INVISIBLE` n'est jamais dessiné** : le test INVISIBLE (`WALLS.C:1616`) passe avant la
   branche parallax (`WALLS.C:1666`) ; les 2 844 murs PARALLAX retail valent tous `320`.

### 4.1 La décomposition convexe — Doom la donne gratuitement

Le moteur exige des **secteurs convexes** : la collision (`pointInSectorP`, `SPRITE.C:940-960`),
la recherche de sol (`findFloorDistance`, `UTIL.C:39-64`) et l'occlusion en dépendent.

Doom fournit deux sources de convexes :

* **les feuilles du BSP (`SSECTORS`)** — convexes par construction, déjà dans le WAD, zéro calcul.
  Le polygone de la feuille n'est pas explicite : il se reconstruit en clippant la bbox de la carte
  par les splitlines ancêtres puis par les segs de la feuille (Mimas le fait déjà, `psw-world`
  STEP 2, reconstruit au chargement dans `r_bsp.c`). **Et il est vérifiable** : `psw_leaf_check.py`
  de Mimas rejoue ce clip en rationnels et a innocenté le builder sur 3 423 feuilles.
* **une décomposition convexe des secteurs** (`convex.py`) — moins de pièces, donc moins de
  portails et moins de murs, mais c'est le morceau fragile de la chaîne Duke.

**Recommandation : partir des feuilles BSP.** Elles sont gratuites, prouvées, et elles portent déjà
la correspondance `subsector -> sector` dont la table de liaison a besoin. Le coût est un nombre de
secteurs plus élevé — ce qui nous amène à la seule mesure qui ne passe pas.

### 4.2 Les capacités — une seule mesure est juste à la limite `[mesuré]` (`wadstat.py`)

| carte | verts | lines | sect | **ssec** | **segs** |
|---|---|---|---|---|---|
| E1M1 | 467 | 475 | 85 | 237 | 732 |
| E1M2 | 942 | 1033 | 200 | 448 | 1463 |
| E1M4 | 780 | 830 | 139 | 355 | 1172 |
| **E1M6** | 1207 | 1352 | 250 | **606** | 1862 |
| E1M8 | 328 | 333 | 74 | 177 | 586 |
| médiane | | 830 | 147 | 384 | 1172 |

* `MAXNMSECTORS 600` (`UTIL.H:21`) : **E1M6 dépasse de 6** avec les feuilles BSP 1:1. Trois
  parades, par ordre de préférence : (a) **fusionner les feuilles soeurs du même secteur** quand
  leur union reste convexe — c'est le levier « moins de cordes, zéro risque » déjà identifié dans
  `psw-world` round 26 ; (b) relever la constante (coût : `sectorDraw` = 60 o/secteur,
  `sectorSpriteList` 4 o, `leafList/drawList/updateList` 4 o chacun -> **~76 octets par secteur**,
  soit 7,6 Ko pour passer à 700) ; (c) accepter que E1M6 ne rentre pas au premier jalon.
* `MAXNMWALLS 5500` (`UTIL.H:22`) : les murs = arêtes des feuilles + 2 par feuille (sol et
  plafond). Estimation E1M6 : ~1 862 segs + arêtes de splitline + 1 212 sol/plafond, de l'ordre de
  3 500-4 000. **Passe**, mais `doorwayCache[MAXNMWALLS]` coûte 12 o/mur (66 Ko aujourd'hui) et il
  est **aliasé** avec les résultats de l'esclave (`WALLS.C:1345-1350`) — toute hausse est à faire
  en connaissance de ça.
* `MAXVPERWALL 700` (`WALLS.C:976`) et l'assert dur `tl*th < 700` (`WALLS.C:1017`) : bornes
  retail mesurées = 242 cellules pour un parallélogramme, 376 faces pour un mur à faces. Sans
  objet pour du Doom découpé sur la grille 64.
* Budget fichier : `assert(size < 900000)` (`LEVEL.C:42`) et **demande mémoire totale <= ~1 250 000**
  (bloc niveau + palettes + tuiles + séquences ; mesures : TOMB retail 1 143 922, SHRINE 1 343 773).
  E1L1 Duke converti = 589 627 o pour 440 secteurs / 3 564 murs. Une carte Doom de la même classe
  y rentre.
* `MAXNMSPRITES 450` (`SPRITE.C:12`), `MAXOBJECTS 350` (`OBJECT.C:9`) : E1M1 a ~140 things ; les
  grandes cartes montent à ~400 en comptant projectiles et effets. **À surveiller**, ajustable
  (un `Sprite` fait ~64 o, un `Object` 144 o).

### 4.3 Les textures — c'est là que Doom est un client *idéal*

Ce qui tombe juste, et ce n'est pas un hasard (les deux moteurs datent de la même génération) :

* **Un flat Doom fait 64x64 en indices 8 bits.** Une tuile de géométrie SlaveDriver est
  `flags 0x32` = **4 096 octets d'indices 8 bits + une palette de 256 entrées BGR555**
  (`PIC.C:500-531` `load16BPPTile` : lit `palNm` puis `width*height` octets ; le « 16 bpp » qualifie
  la palette, pas les pixels). **Les flats de Doom se convertissent à l'identité.**
* La palette de Doom (`PLAYPAL`, 256 entrées) devient la palette du niveau. Le moteur expanse en
  RGB555 au moment du cache (`PIC.C:352`) et dessine en `COLOR_5 | DRAW_GOURAU` — donc Doom gagne au
  passage la **lumière Gouraud par sommet** et les **15 lumières ponctuelles** du moteur
  (`WALLS.C:464-474`), ce qui est strictement mieux que `COLORMAP`.
* **Le nombre de textures distinctes tient** `[mesuré]` (`wadtex.py`) : 35 à 87 par carte shareware
  (médiane 68), contre un budget de tuiles mesuré à **~103 sûres, ~126 en poussant**
  (`NOTES_E41.md`).
* **Et surtout, le cache de 28 tuiles tient** — c'est LA contrainte du moteur
  (`initPicSystem(i,{28,31,1,10,12,-1})`, `SRUINS.C:1903` ; au-delà de 28 tuiles distinctes
  visibles, une commande déjà en file pointe sur des pixels devenus ceux d'une autre : **texture
  fausse EN PLUS de lente**, `PIC.C:271-275`). `[mesuré]` (`wadcache.py`), textures distinctes
  dans un voisinage de portails, médiane sur les 9 cartes shareware :

  | | secteur seul | 1 saut | 2 sauts | 3 sauts |
  |---|---|---|---|---|
  | **Doom shareware** | **4** | **7** | **11** | **15** |
  | PowerSlave retail (mesure NOTES_E41) | 4 (médiane) | | | 12 au p99, 25 au max |

  Doom est **exactement dans le régime pour lequel le cache a été dimensionné**. Pire cas mesuré :
  E1M4 à 20 textures sur 3 sauts, toujours sous 28.

  > **CONFIRMÉ SUR CONSOLE le 2026-09-12**, sur notre Duke E1L1 converti (même classe de contenu) :
  > `used[0] = 14, 16, 17, 14, 18` sur 28, `vswaps[0] = 0` dans 4 captures sur 5. La contrainte qui
  > avait tué E4.1 est sous contrôle, avec ~10 slots de marge.

**Le renoncement réel est ailleurs : la résolution des murs.** Une texture de mur Doom fait
typiquement 128x128 (ou 64x128, 256x128) à **1 texel par unité monde** ; une cellule SlaveDriver
fait 64x64 texels. Deux options, et la leçon E4.1/E4.1b tranche :

* découper en sous-tuiles 2x2 pour garder 1 texel/unité -> **multiplie les tuiles distinctes par
  vue** -> fait exploser le cache de 28. C'est *littéralement* la catastrophe E4.1
  (« catastrophe en terme de performance, et l'aspect graphique n'est pas corrigé »).
* **une seule tuile par texture, et la taille de cellule en unités monde ajustée à UNE répétition**
  (`sWallType.tileLength/tileHeight`, `SLEVEL.H:170-172`) -> **demi-résolution sur les murs**, plein
  débit, cache intact. C'est le remède E4.1b, mesuré : cellules 25 772 -> 8 342, demande mémoire
  1 236 058 -> 1 033 770. **Prendre celui-là**, avec les bornes dissymétriques déjà calibrées
  (`CELL_MIN_U,CELL_MAX_U = 64,256` / `CELL_MIN_V,CELL_MAX_V = 64,256` : un quad VDP1 est affine et
  se déforme avec l'écart de **profondeur** entre ses coins, donc l'horizontal est le vrai goulot).
* Les **sols et plafonds ne paient pas ce prix** : flat 64x64, cellule 64 unités, 1:1 exact.

### 4.4 Les sprites `[mesuré]` (`wadsprites.py`)

483 lumps de sprites dans `DOOM1.WAD`, 825 Ko bruts. Découpés en chunks 64x64 (le modèle du moteur :
une séquence -> des frames -> des `sChunkType` avec `chunkx/chunky` et une tuile, `SLEVEL.H:217-222`) :
**83 % des frames tiennent en 1 chunk**, moyenne **1,27 chunk/frame**, maximum 154x151 (6 chunks).
Une frame de monstre coûte donc ~1,3 commande VDP1 — la même classe que PowerSlave.

Contraintes : classe `TILE8BPP` = 31 slots de 64x64 en `COLOR_4` (banque 256 couleurs,
`PIC.C:84-88`), et l'aire son+séquences+tuiles RLE vit en **LWRAM 0x200000-0x300000 (1 Mo)**
(`UTIL.C:345-353`). Il faut donc **sous-ensembler les sprites par niveau** (seuls les monstres
présents) — ce que Doom fait déjà nativement avec `R_PrecacheLevel`.

---

## 5. Les points durs, un par un

### 5.1 La géométrie mobile — portes, ascenseurs, plates-formes, écrasements

C'est **le** point dur, et il a une bonne nouvelle et une mauvaise.

**La bonne** : le moteur ne stocke pas ses plans, il les recalcule depuis les sommets. `findFloorDistance`
(`UTIL.C:39-64`) et `pointInSectorP` (`SPRITE.C:940-960`) prennent `wall->normal` et le **sommet
`v[0]` lu en direct** (`getVertex`, `UTIL.C:32-37`) ; le champ `sWallType.d` n'est jamais relu sur
ces chemins. Donc **translater des sommets translate le plan, sans aucune mise à jour**. C'est
exactement ce que font les push-blocks (`updatePushBlockPositions`, `SPRITE.C:807-850` : `+= dx/dy/dz`
sur une plage de sommets, plus le transport des sprites posés sur le sol qui bouge). Un secteur Doom
dont le sol monte = un groupe de sommets qu'on translate en Y. **Le mécanisme est déjà là.**

**La mauvaise** : une porte de Doom ne translate pas, elle **change de hauteur**. Le mur de
« linteau » (upper texture) voit deux de ses quatre sommets descendre : le quad se déforme, et comme
`tileLength/tileHeight` sont figés au chargement, **la texture s'écrase au lieu d'être coupée**.
Doom coupe (ou décale, selon le pegging). Trois réponses possibles :

* **(a) accepter l'écrasement** — 1,8 s pour une porte de 128 unités, visible mais pas fatal ;
* **(b) couper par rangée de cellules** — les rangées passées sous le seuil ne sont plus émises :
  la coupe est juste, mais quantifiée à 64 unités ;
* **(c) (b) + écrasement du reliquat** — au plus 64 unités d'écrasement. **C'est celle à prendre.**
  Coût : un intervalle `[ylo,yhi]` par mur mobile, testé dans la boucle de rangées de
  `drawRectWall` / `drawWall`.

Cas particuliers à traiter explicitement : plate-forme qui écrase (`SPR_SQUISH` existe déjà),
**sol et plafond qui se rejoignent** (le secteur devient dégénéré : le clip near du moteur le
gère, mais `pointInSectorP` peut renvoyer faux — à border), et les **secteurs à mouvement
indépendant dans un même secteur Doom découpé en K pièces** (les K pièces bougent ensemble : une
seule liste de sommets par secteur Doom, pas par pièce).

### 5.2 Les particularités de Doom qui n'ont pas d'équivalent

| fonctionnalité Doom | statut sur SlaveDriver |
|---|---|
| textures **midtex** à deux faces (grilles, barreaux) | le moteur n'a pas de quad masqué sur portail. Faisable : une commande supplémentaire en `ECD/SPD` sur le portail, à l'ordre peintre du secteur proche |
| **F_SKY1** (plafond ciel) | direct : `WALLFLAG_PARALLAX` + le ciel RBG0 existant. C'est le même objet |
| **scrolling walls** (`Scroll_Texture`) | le décalage de texture n'existe pas dans une cellule (pas d'UV). Approximation : faire tourner l'index de tuile dans une animation (le moteur a `PICFLAG_ANIM`, `PIC.C:440-443`) |
| **transparent/translucent** (Boom) | hors sujet pour le shareware |
| secteurs **lumineux clignotants** | la lumière est par sommet et par secteur (`sSectorType.light`, -16..16). Le clignotement se fait en écrivant `light` au tic : gratuit |
| **teleporters, tags, lignes spéciales** | 100 % côté playsim Doom. Aucun impact renderer |
| **sector over sector / 3D floors** | Doom n'en a pas. Le moteur, si (eau, portails horizontaux) — bonus non utilisé |

### 5.3 L'allocateur

Le moteur n'a **pas de tas** : `mem_malloc` est une **pile LIFO à 2 aires et 8 niveaux**
(`UTIL.C:336-405`), aire 0 = LWRAM 1 Mo, aire 1 = HWRAM de `&end` à 0x6100000
(438 720 o en GCC 14). Doom veut `Z_Malloc` avec des tags purgeables.

**Parade** : prendre **un** bloc de `mem_malloc` et y installer `z_zone.c` de Doom tel quel. Une
allocation, une libération, zéro friction. (C'est aussi ce qui rend la double représentation du §7
gérable.)

### 5.4 Le son

Pas de driver 68 K : le 68 K est parqué en `nop;bra` et le SH-2 poke directement les **32 slots
SCSP** via l'adresse **cachée** 0x05a00000 (`SOUND.H:4`, `SOUND.C:153`, `MEGAINIT.C:221`).
512 Ko de RAM son en allocateur bump remis à zéro par niveau, `MAXNMSOUNDS 80`, slots 16/17
réservés au mix CDDA.

Doom shareware a ~60 sons : **ça rentre**. La musique passe par **CDDA** (comme PowerSlave) ou par
le séquenceur MUS que Mimas fait tourner sur le MSH2 en build `-Mus`. Rien de bloquant, mais
`s_sound.c` doit être re-câblé sur `posMakeSound`/`playStaticSound` (mapping par type d'objet
`level_objectSoundMap`, `SOUND.C:29`).

### 5.5 Sauvegarde, menus, intermissions

`p_saveg.c` (1 892 lignes) sérialise les structures **Doom**, pas les `.LEV` — donc il survit
intact dans l'architecture C. Les menus, l'intermission (`wi_stuff.c`), le HUD (`st_stuff.c`)
dessinent des patches via `v_video.c` sur un framebuffer logiciel : il faut soit les rendre en
commandes VDP1 (le moteur a `drawStringf`, `EZ_setChar`, `STATBAR.C`), soit garder une petite
surface logicielle. C'est du travail mécanique, pas un risque.

---

## 6. Ce que ça coûte en RAM — le vrai arbitrage de l'architecture C

Les deux représentations coexistent :

| | source | ordre de grandeur |
|---|---|---|
| structures Doom (secteurs, lines, sides, segs, ssectors, nodes, blockmap, reject) | `p_setup.c` | E1M6 : ~1 352 lines x 24 o + 1 727 sides x 12 + 606 ssec + 605 nodes x 32 + blockmap. Ordre : **150-250 Ko** |
| modèle `.LEV` | `LEVEL.C` | E1L1 Duke = 589 Ko ; une carte Doom du même gabarit : **400-600 Ko** |
| tuiles + palettes + séquences + sons | `PIC.C`/`SOUND.C` | **300-500 Ko** (budget mesuré : demande totale <= ~1 250 000) |

Contre **438 Ko de HWRAM libre** (`&end`..0x6100000) plus **1 Mo de LWRAM**. Ça ne rentre pas
naïvement. Trois leviers, à décider avant d'écrire du code :

1. **Ne pas charger deux fois la même chose.** Les sommets et les hauteurs n'ont besoin d'exister
   qu'une fois : le `.LEV` peut *être* la source de vérité géométrique, et les `sector_t`/`line_t`
   de Doom ne garder que ce que le playsim lit vraiment (`floorheight`, `ceilingheight`, `special`,
   `tag`, `lightlevel`, `thinglist`, les listes de lines). Une passe de mesure honnête sur
   `r_defs.h` dira combien de chaque struct est *lue par le playsim seul*.
2. **Le BSP, le blockmap et le reject sont gros et statiques** : ils peuvent vivre en LWRAM
   (2,1x plus lent, mais ils sont lus, pas écrits).
3. **La cartouche 4 Mo** : PowerSlave ne l'utilise pas (`validPtr` n'accepte que les deux aires,
   `UTIL.H:74`), Mimas si. C'est le levier de réserve, et il change la classe du problème.

---

## 7. Jalons proposés

| # | jalon | critère d'acceptation | coût estimé |
|---|---|---|---|
| ~~**0**~~ | ~~capture console STATUSTEXT~~ | **FAIT 2026-09-12** : `calc ≈ 14,9 ms + 39,2 µs/cellule`, cache 14-18/28. `STEXT_BASELINE_2026-09-12.md` | — |
| **0b** | les mêmes captures en **`-NDebug`**, puis une sonde `slaveSize` / spins / `updateListSize` / commandes de sprites | de combien baissent le 39,2 µs **et** le fixe de 14,9 ms (qui vaut encore 1/3 du temps CPU à 700 cellules) ; et ce qui explique les +24 ms de la capture 5 | 1 disque + 2 lignes |
| **1** | `wad2lev.py` : E1M1 -> `.LEV` « boîte grise » (géométrie + flats réels, murs en tuiles bouchon), donneur KILENTRY, 3 invariants du §4 comme critères | **E1M1 se parcourt sur console** avec le moteur inchangé. C'est le jumeau exact d'E5/E6 de la chaîne Duke, qui a marché | 8-12 j |
| **2** | textures de murs E4.1b (une tuile par texture, cellule = 1 répétition, bornes 64..256) | l'aire à la bonne échelle > 60 %, cache 28 jamais dépassé (`vswaps` sous STATUSTEXT) | 4-6 j |
| **3** | **pont playsim** : `p_*.c` + `g_game.c` + `p_tick.c` tournent, table de liaison, synchro des hauteurs par tic, mobjs -> `Sprite` | les monstres bougent, les portes s'ouvrent, on prend des dégâts | 15-25 j |
| **4** | armes, HUD, son, sauvegarde, intermission | une partie complète E1M1 -> E1M9 | 10-15 j |
| **5** | **sol dominant VDP2** (`VDP2_DOMINANT_FLOOR.md`) — c'est ici qu'il paie vraiment | gain mesuré sur `polys` et fps | 5-8 j |

Le jalon 1 est le vrai test de l'hypothèse et il est **court**, parce que 80 % de son outillage
existe déjà (`tools/duke2ps/`). **Ne pas commencer par le jalon 3.**

---

## 8. Ce qui reste inconnu (à ne pas présenter comme acquis)

1. ~~**Le µs/commande de SlaveDriver.**~~ **MESURÉ 2026-09-12 : 39,2 µs + 14,9 ms de fixe** (build
   assert). Reste inconnu : **la valeur en `-NDebug`**, et **ce que contient le fixe de 14,9 ms** —
   il vaut encore un tiers du temps CPU à 700 cellules et rien ne le décompose.
2. **Le nombre de commandes d'une vue Doom** dans ce modèle. Sur Duke E1L1 converti : **190-824
   cellules par frame**, soit 20-30 fps au modèle. Une carte Doom n'a pas été mesurée. ⚠ les
   sprites ne sont **pas** dans ce compte (`nmPolys` n'est incrémenté que sur les murs).
3. **La densité de things.** Doom met 20-40 monstres visibles dans une arène ; PowerSlave en met
   5-10. `drawSprites` est maître-only (`WALLS.C:2571`) et l'esclave n'aide pas.
4. **E1M6 à 606 feuilles** et la fusion des feuilles soeurs : plausible, non implémentée.
5. **L'écrasement des portes** (§5.1) : le choix (c) est raisonné, pas testé.
6. **Le budget RAM du §6** : les ordres de grandeur sont des estimations, pas un `.map`.

# Joueur Duke dans le fork SlaveDriver : taille, accroupi, rétréci — conception et diff proposé

Chiffres : `build\duke2ps\player\openings_report.md`. Scripts : `tools\duke2ps\player_dims.py`,
`player_openings.py`, `player_report.py` et `player_shrinkers.py`. Étiquettes :
- **MESURE** : le résultat vient d'une commande, citée ;
- **SOURCE** : `fichier:ligne`. Pour Duke, les fichiers sont dans `refs\build\jfduke3d\src`, en lecture seule ; aucune ligne n'en est recopiée ;
- **HYPOTHÈSE** : ni mesuré ni sourcé.

Fichiers moteur considérés : uniquement ceux que compile le Makefile (`MAIN_C`, Makefile:146-147). `FLASH\`, `OLDJAP\`, `SAVE\` et `JEFF\` sont exclus.

```powershell
python tools\duke2ps\player_dims.py; python tools\duke2ps\player_openings.py
python tools\duke2ps\player_shrinkers.py; python tools\duke2ps\player_report.py
```

## 1. Duke : dimensions (MESURE `player_dims.py`, simulation des boucles SOURCE)

| grandeur (u) | debout | accroupi | rétréci |
|---|---|---|---|
| œil au-dessus du sol (physique) | 80 | 36 | 16 |
| œil rendu | 80 | 36 | 8 |
| hauteur libre pour passer (œil + 8) | **88** | **44** | **24** |
| rayon (clipdist 164) | 20,5 | 20,5 | 20,5 |
| largeur mini entre jambages | 41 (dans l'axe), 58 (à 45°) | idem | idem |
| marche franchissable | 40 | < 2 | < 2 |
| saut (montée de l'œil / rebord max) | 127 / 167 (plafond > 112 exigé) | — | — |

**Debout** :
- SOURCE : œil cible à `sol − (40<<8)`, player.c:2785, 3026-3035 ;
- SOURCE : `clipmove(…, 164L, 4L<<8, 20L<<8…)`, player.c:3303-3320 ;
- SOURCE : test des murs rouges, jfbuild engine.c:9194-9213 ;
- SOURCE : carrés axés aux extrémités des murs, engine.c:9219-9228.

**Accroupi** :
- SOURCE : `posz += 2048+768` à chaque tic, player.c:3048-3052 ;
- MESURE : combiné au lissage `(fz−40<<8−posz)>>1`, cela donne un œil stable à `sol − (18<<8)` = 36 u.

**Rétréci** :
- SOURCE : `yrepeat < 32`, player.c:2375 ;
- SOURCE : `posz += 32<<8` après clipmove, player.c:3322-3323 ;
- SOURCE : clipmove voit l'œil à 16 u ;
- SOURCE : le rendu est borné à `sol − 4<<8`, game.c:3065-3073.

**Le rayon ne change jamais.**
- SOURCE : `164L` est une constante dans clipmove (player.c:3320) et dans pushmove (player.c:3368).
- Dans Duke, être rétréci ne fait donc pas passer par un endroit plus **étroit**, seulement par un endroit plus **bas** : entre 24 et 44 u de hauteur libre (trous de souris, conduits).
- La largeur requise reste de 41 u.
- Rétréci, Duke est aussi plus lent : friction × 0,75, SOURCE player.c:3292-3298.
- Il ne ramasse rien : `ifp pshrunk { }`, SOURCE GAME.CON d13:644 et suivantes.

**Comment on est rétréci.** Un `SHRINKSPARK` touche l'APLAYER (SOURCE GAME.CON d13:1735-1741). Les tireurs possibles :
1. **NEWBEAST** (Atomic Edition) : le seul acteur qui fait `shoot SHRINKER` (SOURCE maps/atomic/GAME.CON:8485-8494). Il ne tire jamais sur un joueur déjà rétréci (SOURCE ligne 8487).
   - MESURE `player_shrinkers.py` : 122 NEWBEAST dans 11 cartes Atomic ;
   - aucun dans d13, dukedc, dz2 et xtreme_sp ;
   - aucun acteur de 1.3D ne tire le rétrécisseur (grep : 0).
2. **Son propre tir renvoyé par un miroir** : tout projectile, sauf RPG, FREEZEBLAST et SPIT, rebondit sur un mur MIRROR. Il devient alors son propre propriétaire, et peut donc toucher son tireur (SOURCE actors.c:2509-2515).
   - MESURE : murs miroir : d13 22, atomic 34, dukedc 11, dz2 8, xtreme_sp 18 ;
   - MESURE : un rétrécisseur est posé au sol dans 94 cartes sur 128.
3. **Un autre joueur**, en multijoueur.

**Durée et chronologie**, à 30 tics/s (SOURCE duke3d.h:100-101 : TICSPERFRAME = 120/26 = 4) :
- `move PSHRINKING` remet le compteur à 0 (gamedef.c:2665), qui avance d'un cran par tic (gamedef.c:1921) ;
- de 0 à 31 : `sizeto 8 9`, un cran par tic (gamedef.c:2365-2374) ;
- à partir de 270 (SHRUNKCOUNT) : `sizeto 42 36` ;
- à 304 (SHRUNKDONECOUNT) : fin (SOURCE USER.CON d13:79-80, GAME.CON:1648-1670).

MESURE : Duke reste « rétréci » du tic 4 au tic 291, soit **9,6 s** ; il regrandit à 9,0 s et c'est fini à 10,1 s. Deux cas particuliers :
- **Écrasement** : pendant qu'il regrandit, s'il y a moins de 48 u entre le sol et le plafond (`ifgapzl 24`), il meurt : `strength 0` + `SQUISHED` (SOURCE gamedef.c:2973-2975, GAME.CON:1656-1662). Entre 48 et 88 u, il regrandit quand même, l'œil bloqué sous le plafond, c'est-à-dire « accroupi de fait ».
- **Stéroïdes** : ils annulent le rétrécissement, le compteur passant à SHRUNKCOUNT (SOURCE GAME.CON:1664-1668).

**PowerSlave aujourd'hui** :
- boule de 47 u (SOURCE AI.C:46) ;
- l'œil est à **55 u** du sol et non à 47. SOURCE SPRITE.C:643-644 : `floorDistance += F(8)` pour la caméra ; l'équilibre est donc à R + 8 ;
- hauteur libre requise : 2R + 8 = 102 u (SOURCE SPRITE.C:703-708) ;
- STEPHEIGHT de 32 u, 1 u dans l'eau (SOURCE SPRITE.C:592-597).

## 2. Ouvertures des cartes (MESURE `player_openings.py` + `player_report.py`)

Couverture du relevé :
- 128 cartes, 92 886 passages, c'est-à-dire des paires de murs rouges ;
- dont 24 607 cloisons (même sol et même plafond des deux côtés).

Répartition par posture (tous corpus) :

| posture | passages |
|---|---|
| debout (h ≥ 88) | 69 688 |
| accroupi | 4 635 |
| rétréci | 936 |
| infranchissable, trop bas (h < 24) | 2 155 |
| infranchissable, trop étroit (< 41 u selon Chebyshev) | 9 737 |
| bloquant (cstat&1) | 5 468 |
| vantail fermé | 267 |

**Ouverture standard.**
- **Linteaux**, c'est-à-dire les passages dont le plafond diffère d'un côté à l'autre (22 586 franchissables) :
  - hauteur modale **128 u** ;
  - couples (h × w_eff) les plus fréquents : **128 × 128** (406), **128 × 64** (390), 64 × 64 (280) ;
  - p5 : 64 × 62 u ; p10 : 73 × 64 u.
- **Passages qui touchent une porte** : h modale 128, puis 64 ; w_eff 256, 128, puis 64.
- **La porte type d'un niveau Duke fait 128 u de haut et 64 ou 128 u de large.**
- Parmi les linteaux debout, 4,2 % font moins de 64 u de large et 0,8 % moins de 48 u.

Couloirs (secteurs de passage où Duke tient) : p5 64 u et p10 90 u. 3,2 % font moins de 64 u de large et 8,2 % moins de 80 u, mais cela ne représente que 0,5 % de la surface.

## 3. Rayons de la boule et verdict

La boule PowerSlave demande :
- en hauteur : h ≥ 2R + 8 (flottement caméra) ;
- en largeur : w ≥ 2R (SPRITE.C:141, 224).

MESURE, couverture « les deux », tous les passages de chaque posture :

| R | œil | debout (les deux) | debout, linteaux | accroupi | rétréci | parasites* |
|---|---|---|---|---|---|---|
| 47 (actuel) | 55 | 83,0 % | 74,8 % | 0 % | 0 % | 0 |
| 40 | 48 | 88,1 % (hauteur 100 %) | 83,3 % | 0 % | 0 % | 0 |
| 32 | 40 | 96,8 % | 95,8 % | 29,0 % | 0 % | 0 |
| 31 | 39 | 97,1 % (2 036 manques) | 96,1 % | 29,5 % | 0 % | 0 |
| 24 | 32 | 99,4 % | 99,2 % | 88,1 % | 0 % | 197 |
| 20 | 28 | 100 % | 100 % | 99,2 % | 0 % | 1 015 |
| 18 | 26 | 100 % | 100 % | **100 %** | 0 % | 1 150 |
| 8 | 16 | 100 % | 100 % | 100 % | **100 %** | 7 753 |

\* Parasites : passages que Duke ne franchit dans **aucune** posture, faute de largeur, alors que la boule, elle, passe (h ≥ 2R+8 et w ≥ 2R). Exemples : fentes, barreaux, meurtrières.

**Verdict : une boule unique ne suffit pas.** Le corps de Duke mesure 41 u de large sur 88 u de haut. Une sphère de même hauteur est deux fois trop large ; une sphère de même largeur place l'œil à la moitié de la hauteur de Duke.

- **Debout.** La hauteur n'est jamais limitante (100 % jusqu'à R = 40), la largeur l'est.
  - Pour passer la porte standard de 64 u de large, il faut R ≤ 32. Cela revient à un ajustement exact : R = 31 pour garder une marge.
  - L'œil est alors à **39 u, contre 80 pour Duke** : le monde paraît deux fois plus haut. Et 2,9 % des passages debout sont encore manqués (linteaux de 41 à 62 u).
  - Viser 99 % impose R = 24 (œil à 32 u) ; viser 100 %, R = 20,5 (œil à 28,5 u).
  - Un œil de Duke (80 u) exigerait R = 72, donc 144 u de large : impossible.
- **Accroupi et rétréci.** R = 18 (œil 26 contre 36) et R = 8 (œil 16, comme Duke) couvrent 100 % des passages.
  - Mais comme la largeur rétrécit avec la hauteur, la boule passe par 1 150 puis **7 753** fentes que Duke ne passe jamais.
  - Or c'est justement le contraire du comportement de Duke rétréci : son rayon reste fixe (§1).
  - Risque : raccourcis et sorties de carte.

**Alternative la moins invasive : un cylindre vertical pour la caméra seule** (B, recommandée).
- Rayon horizontal de 20,5 u, soit le clipdist de Duke, conservé dans `radius`/`radius2`. Les murs, les arêtes, les sprites et le hitscan continuent de l'utiliser sans modification.
- Côté vertical, le corps s'étend de `pos.y − pied + marche` à `pos.y + 8`, où `pos.y` est l'œil ; la caméra reste le sprite, sans décalage.
- Pied / marche par posture : debout 80 / 40, accroupi 36 / 2, rétréci 16 / 2, nage 30 / 1 (SOURCE player.c:2831, `fz − 15<<8`).

Bilan de l'alternative B :
- **Couverture** : 100 % des passages de chaque posture, par construction (mêmes seuils que Duke).
- **Parasites** : 620, 700 et 738 (MESURE). Ce sont uniquement des passages diagonaux de 41 à 58 u, que les carrés axés de Build refusent.
- **Coût** : aucun octet dans `Sprite`, 3 `Fixed32` globaux, un test `o == camera` par macro, et environ 150 lignes dans un nouveau fichier.

Autres pistes écartées :
- **Deux sphères empilées** : `collideSprite` deux fois pour la caméra (coût ×2), avec des transitions de secteur à concilier.
- **Ellipsoïde** : il faudrait renormaliser la normale de chaque mur (une racine par mur et par trame).

**Complément côté données, gratuit pour le moteur.** PowerSlave bloque déjà certains portails pour le seul joueur :
- `WALLFLAG_SHORTOPENING` (0x1000, SLEVEL.H:143) a la même valeur que `SPRITEFLAG_BSHORT` (SPRITE.H:32) ;
- seul le joueur porte ce drapeau (AI.C:47, MESURE : grep, une seule occurrence) ;
- un portail qui porte ce drapeau devient un mur plein pour lui (SPRITE.C:217-218, 273, 414, 435).

Le convertisseur peut donc poser 0x1000 sur les portails `infr_l` et `bloque` ; les parasites disparaissent, quelle que soit la variante.

**Variante A (l'hypothèse du propriétaire, si l'on accepte un œil bas).** On garde la boule, avec R = 31 debout, 18 accroupi et 8 rétréci ; les macros par défaut du diff restent en place. À chaque changement de posture :
- `camera->radius` **et** `camera->radius2 = MTH_Mul(R,R)` : le carré est mis en cache (SPRITE.C:79) et relu par SPRITE.C:224 et HITSCAN.C:44-46 ;
- `pos.y += ΔR`, pour que les pieds, à `pos.y − R − 8`, restent en place.

## 4. Conception moteur (fichiers compilés, lecture seule)

### 4.1 Lecteurs de `radius` / `radius2` et suppositions d'un joueur de taille fixe

- **SPRITE.H:46** : les champs. **SPRITE.C:79** : `radius2` mis en cache, jamais recalculé ailleurs (MESURE : grep `radius=` ; la seule écriture à l'exécution est dans `SAVE\AI.C:3205`, qui n'est pas compilé).
- **SPRITE.C** :
  - 65 (`shiftSprites` au chargement) ;
  - 141, 145, 224, 252, 293 (`bumpWall`) ;
  - 497-522 (`collideSpriteSprite`, les trois axes) ;
  - 544, 549, 561-562 (éclaboussure : volume, bulles) ;
  - 642-644 (sol et +F(8) de la caméra) ;
  - 703-708 (plafond, écrasement au point milieu) ;
  - 592-597 (STEPHEIGHT 32/1).
- **HITSCAN.C:44-46** : sphère `radius2`, utilisée aussi par les pièges laser OT_SHOOTER3 qui visent la caméra (AI.C:6158-6177).
- **AICOMMON.C:35** et **AI.C:5869** : placent le joueur à `radius` au-dessus du sol.
- **AI.C:46** : `F(47)`.
- **AI.C:1047-1053, 1206-1210** : le cobra vise `enemy.y + radius ± 20`.
- **WEAPON.C:599** : portée de l'épée augmentée du rayon de la **cible** (un monstre).
- **WALLS.C:2698** : pieds des sprites dessinés ; la caméra n'est pas dessinée. WALLS.C:2795 est en `#if 0`, de même que OBJECT.C:395.
- **OBJECT.C:432-530** : `radius` y est un paramètre de dégâts de zone, pas le rayon d'un sprite.
- **Constantes de taille codées en dur** :
  - SPRITE.C:158 (`F(15)` : bord de l'eau) ;
  - SRUINS.C:412-417 (`F(14)` : sortie de l'eau) ;
  - SRUINS.C:210-234 et 2099 (`playerHeightOffset` : rebond d'atterrissage de la vue, ≤ F(32)) ;
  - SRUINS.C:847 (portée de « pousser » : F(120) depuis l'œil) ;
  - WEAPON.C:356-358 (orifice de l'anneau : 48 × rayon depuis l'œil).

### 4.2 Changement de posture

- **Pieds au sol** : `pos.y` est l'œil. On fait varier le pied de Δ et `pos.y` du même Δ, par pas de 6 u par trame au plus (Duke : environ 11 u par tic à 30 Hz).
- **Se relever** seulement si `sol→plafond ≥ pied_cible + 8`, soit 88 u pour être debout (l'équivalent de « 2·R_debout »). Sinon, on reste accroupi.
- **Fin du rétrécissement** :
  - hauteur libre < 48 u : mort (`playerHurt` massif), comme `ifgapzl 24` ;
  - entre 48 et 88 u : accroupi forcé ;
  - au-delà : debout.
- **Écrasement** (plafond et sol touchés à la fois) : l'œil est plaqué à `plafond − 8`, comme le bornage de Duke (player.c:3099-3106). Le calcul de posture impose l'accroupi à la trame suivante.
- HYPOTHÈSE : la hauteur libre est lue par `findFloorDistance + findCeilDistance` (UTIL.C:39, 67) dans le **seul** secteur de la caméra. Duke sonde un rayon de 163 (player.c:2376). Au bord d'un secteur, on peut donc se relever d'une trame sous un plafond voisin ; la collision de plafond rattrape le cas.

### 4.3 Manette

- **Les 8 boutons sont pris.** Actions FIRE/JUMP/PUSH/FREELOC/WEPDN/WEPUP/STRAFE/RUN sur A/B/C/X/Y/Z/L/R (SOURCE UTIL.H:245-248, UTIL.C:515-518). Le menu les permute entre elles sans en ajouter (INTRO.C:131-181). START sert à la pause et au menu. **Aucun bouton n'est libre.**
- **Proposition** : dans le build Duke, l'emplacement `ACTION_WEPDN` (Y par défaut) devient « s'accroupir », en maintien. On change d'arme avec `ACTION_WEPUP` seul, en boucle. `bitScanForward` renvoie −1 en bout de liste (UTIL.C:491-501), d'où `dukeWeaponNext`.
- **Reste à faire** : le libellé du menu (`LB_ACTIONNAMES`, texte des données du CD) dira encore « arme précédente ». HYPOTHÈSE : ce texte se trouve dans les données, il est hors du diff.
- HYPOTHÈSE : la disposition des boutons de Duke Saturn (Lobotomy) n'a pas été vérifiée.

### 4.4 Effets de bord

- **Origine des tirs** : c'est `camera->pos` (WEAPON.C:356-358, 574). Accroupi ou rétréci, on tire donc depuis l'œil abaissé, comme Duke.
- **Monstres** : ils visent `camera->pos.y` (AI.C:3772, 3795, 4880, 4898, 5732, 5758). Leurs projectiles touchent le joueur par `collideSpriteSprite`. Le diff y remplace `dp.y` par la distance au **segment** du cylindre (`dukeDY`) ; sans cela, un tir bas passerait sous la sphère de tête. Le hitscan des pièges laser reste une sphère de 20,5 u à l'œil. HYPOTHÈSE : ces pièges n'existent pas dans les cartes Duke.
- **Sons** : le volume de l'éclaboussure dépend de `radius` : 20,5 u au lieu de 47 donne une éclaboussure plus discrète. C'est cosmétique.
- **Sauvegarde** : `SaveState` ne contient ni position ni rayon (GAMESTAT.H:54-65) et le joueur est recréé à chaque niveau (AI.C:40-58). On remet la posture à zéro dans `dukePlayerInit` ; **aucun effet sur la sauvegarde**.
- **Eau et nage** : sous l'eau, la gravité est nulle (SRUINS.C:265) et STEPHEIGHT vaut F(1) (SPRITE.C:592-594). Le rayon compte pour les murs et pour le sol et le plafond. Dans le build Duke, la posture « nage » est la suivante : pied 30, tête 8, accroupi désactivé, rétrécissement conservé. HYPOTHÈSE : il faudra revoir la sortie d'eau (`F(14)`, SRUINS.C:416).
- **Marches** : Duke monte 40 u, PowerSlave 32 u. MESURE : 1 972 passages debout ont une marche comprise entre 32 et 40 u ; d'où `SPR_STEP` à 40 pour la caméra debout.
  - Accroupi ou rétréci, Duke ne monte **rien** (MESURE : 3 891 passages concernés). Le diff reproduit ce comportement avec une marche de 2 u. C'est une **décision du propriétaire** : on peut mettre 32 pour ménager la jouabilité.
- **Pentes** : le sol est lu au centre du cylindre, comme `getflorzofslope` au centre chez Duke. Pour les pentes raides traitées comme des murs (0 < n_y < 0,75), le diff décale le plan du « support » du pied (`SPR_SUPPORT`, SPRITE.C:133). HYPOTHÈSE : approximation.
- **Départ de niveau** : `shiftSprites` ajoute `radius` (SPRITE.C:60-66). Le convertisseur doit donc écrire le Y de départ à `sol + 80 − 20,5`, faute de quoi la caméra se relève d'elle-même pendant environ 20 trames (`floorDistance>>3`, SPRITE.C:657).

### 4.5 Déclenchement du rétrécissement

- **API** : `dukeShrink()` arme une minuterie de 9,6 s, soit 576 trames à 60 Hz ou 480 en PAL. HYPOTHÈSE : une trame de `movePlayer` = une VBL. Un deuxième tir pendant le rétrécissement est ignoré (NEWBEAST ne tire d'ailleurs pas, GAME.CON:8487).
- **Appelants** : il n'y en a aucun aujourd'hui ; ils viendront du portage des armes et ennemis Duke (rétrécisseur, NEWBEAST, renvoi par un miroir).
- **Pendant le rétrécissement** : saut, écrasement et ralentissement ne sont **pas** reproduits dans ce diff ; c'est une étape suivante.

## 5. Diff proposé (NON appliqué)

- **Principe** : le build par défaut doit produire **les mêmes binaires**. En débogage, `assert` fige `__LINE__` dans le code (UTIL.H:82). Aucune ligne n'est donc ajoutée ni retirée dans les `.C` existants : chaque point d'accroche est une macro écrite **sur la ligne qu'elle remplace**. Par défaut, elle se développe en l'expression d'origine.
- **Emplacement du code nouveau** : il vit dans `DUKEPLR.C` et `DUKEPLR.H`, deux nouveaux fichiers. SPRITE.H gagne un bloc en fin de fichier ; un en-tête sans code ne décale pas `__LINE__` dans les `.C`.
- **Espaces** : indentation à la tabulation, comme le fichier ; à ajuster à l'application.

```diff
--- a/Makefile
+++ b/Makefile
@@ -7,2 +7,3 @@
 #   make PAL=1       -> build/pal/: the European (Exhumed) configuration, for EU game data
+#   make DUKEPLAYER=1 -> build/duke/: the Duke Nukem 3D player (crouch, shrink) for converted Duke maps
 #   make BOOTPROBE=1 iso iso-ipjump -> build/probe/: INIT paints a colour per boot step (INITMAIN.C
@@ -73,2 +74,11 @@
 endif
+# DUKEPLAYER=1: the Duke Nukem 3D player for maps converted by tools/duke2ps -- the camera sprite is a
+# vertical cylinder (radius 20.5 u, Duke's eye heights: stand 80 / crouch 36 / shrunk 16), crouch on the
+# ACTION_WEPDN slot, shrink timer (DUKEPLR.C).  Own tree, like the other switches.  The default build is
+# untouched: every hook is a same-line macro that expands to the original expression (SPRITE.H).
+ifeq ($(DUKEPLAYER),1)
+  BUILD   := $(BUILD)/duke
+  DEFINES += -DDUKEPLAYER
+  DUKE_C  := DUKEPLR
+endif
 OBJDIR   := $(BUILD)/obj
@@ -146,2 +156,2 @@
 MAIN_C       := AI AI2 AICOMMON ART BIGMAP BUP DMA FILE HITSCAN INTRO LOCAL MAP MENU OBJECT PIC \
-                PICSET PLAX PRINT PROFILE ROUTE SEQUENCE SOUND SPR SPRITE SRUINS UTIL WEAPON $(COMMON4) WALLS
+                PICSET PLAX PRINT PROFILE ROUTE SEQUENCE SOUND SPR SPRITE SRUINS UTIL WEAPON $(COMMON4) WALLS $(DUKE_C)
@@ -304,2 +314,2 @@
 help:
-	@sed -n '2,13p' Makefile
+	@sed -n '2,14p' Makefile
--- a/SPRITE.H
+++ b/SPRITE.H
@@ -103,3 +103,27 @@
 void suckSpriteParams(Sprite *s);
 
+/* DUKE: Duke Nukem 3D player (Makefile DUKEPLAYER=1, DUKEPLR.C).  The hooks in the .C files are macros
+   written on the line they replace, so the default build keeps its line numbers (assert() embeds
+   __LINE__, UTIL.H:82) and its binaries: the defaults below are the original expressions. */
+#ifdef DUKEPLAYER
+#include "dukeplr.h"
+#else
+#define SPR_FOOT(o)        ((o)->radius)             /* centre -> floor contact (SPRITE.C:642)  */
+#define SPR_HOVER(o)       F(8)                      /* camera hover (SPRITE.C:644)             */
+#define SPR_HEAD(o)        ((o)->radius)             /* centre -> ceiling (SPRITE.C:703,708)    */
+#define SPR_SQUISH(o,c,fl) (((c)+(fl))>>1)           /* squeezed (SPRITE.C:706)                 */
+#define SPR_STEP(o)        F(32)                     /* STEPHEIGHT (SPRITE.C:597)               */
+#define SPR_YLO(o)         ((o)->pos.y)              /* body interval vs wall edges (SPRITE.C:200,205) */
+#define SPR_YHI(o)         ((o)->pos.y)
+#define SPR_YOUT(o,d)      (d)
+#define SPR_SUPPORT(o,w)   0                         /* steep-slope plane offset (SPRITE.C:133) */
+#define SPR_DY(m,s)        ((m)->pos.y-(s)->pos.y)   /* sprite/sprite (SPRITE.C:499)            */
+#define PLAYER_RADIUS      F(47)                     /* AI.C:46                                 */
+#define DUKE_PLAYER_INIT(s)
+#define DUKE_POSTURE(in)
+#define DUKE_WEPDN         1
+#define WEP_NEXT(m,d)      bitScanForward((m),(d))   /* WEAPON.C:977                            */
+#endif
+
 #endif
--- a/SPRITE.C
+++ b/SPRITE.C
@@ -133 +133 @@
-    (f(o->pos.z-wallP.z))*wall->normal[2];
+    (f(o->pos.z-wallP.z))*wall->normal[2]-SPR_SUPPORT(o,wall);
@@ -200,2 +200,2 @@
-     if (o->pos.y>wallTop)
-        {yDist=wallTop-o->pos.y;
+     if (SPR_YLO(o)>wallTop)
+        {yDist=SPR_YOUT(o,wallTop-o->pos.y);
@@ -205,2 +205,2 @@
-        {if (o->pos.y<wallBottom)
-            {yDist=wallBottom-o->pos.y;
+        {if (SPR_YHI(o)<wallBottom)
+            {yDist=SPR_YOUT(o,wallBottom-o->pos.y);
@@ -499 +499 @@
- dp.y=mobile->pos.y-stat->pos.y;
+ dp.y=SPR_DY(mobile,stat);
@@ -597 +597 @@
-    STEPHEIGHT=F(32);
+    STEPHEIGHT=SPR_STEP(o);
@@ -642 +642 @@
-    {int floorDistance=o->radius+bestFloorHeight-o->pos.y;
+    {int floorDistance=SPR_FOOT(o)+bestFloorHeight-o->pos.y;
@@ -644 +644 @@
-	floorDistance+=F(8);
+	floorDistance+=SPR_HOVER(o);
@@ -703 +703 @@
- if (ceilValid && o->pos.y>bestCeilingHeight-o->radius)
+ if (ceilValid && o->pos.y>bestCeilingHeight-SPR_HEAD(o))
@@ -706 +706 @@
-	o->pos.y=(bestCeilingHeight+bestFloorHeight)>>1;
+	o->pos.y=SPR_SQUISH(o,bestCeilingHeight,bestFloorHeight);
@@ -708 +708 @@
-	o->pos.y=bestCeilingHeight-o->radius;
+	o->pos.y=bestCeilingHeight-SPR_HEAD(o);
--- a/AI.C
+++ b/AI.C
@@ -46,3 +46,3 @@
- this->sprite=newSprite(sector,F(47),0.90*65536.0,GRAVITY,
+ this->sprite=newSprite(sector,PLAYER_RADIUS,0.90*65536.0,GRAVITY,
 			-1,SPRITEFLAG_BSHORT,
-			(Object *)this);
+			(Object *)this); DUKE_PLAYER_INIT(this->sprite);
@@ -5869 +5869 @@
-     pos.y+=camera->radius-findFloorDistance(playerSec,&pos);
+     pos.y+=SPR_FOOT(camera)-findFloorDistance(playerSec,&pos);
--- a/AICOMMON.C
+++ b/AICOMMON.C
@@ -35 +35 @@
- pos.y+=camera->radius-findFloorDistance(playerSec,&pos);
+ pos.y+=SPR_FOOT(camera)-findFloorDistance(playerSec,&pos);
--- a/SRUINS.C
+++ b/SRUINS.C
@@ -987 +987 @@
-	 if (pushed&IMASK(ACTION_WEPDN))
+	 if (DUKE_WEPDN && (pushed&IMASK(ACTION_WEPDN)))
@@ -1039 +1039 @@
-      moveCamera();
+      DUKE_POSTURE(input); moveCamera();
--- a/WEAPON.C
+++ b/WEAPON.C
@@ -977 +977 @@
- newD=bitScanForward(weaponMask,currentState.desiredWeapon);
+ newD=WEP_NEXT(weaponMask,currentState.desiredWeapon);
--- /dev/null
+++ b/DUKEPLR.H
@@ -0,0 +1,33 @@
+/* dukeplr.h -- DUKE: Duke Nukem 3D player, included by SPRITE.H when DUKEPLAYER is defined.
+   Only the camera changes: it is a vertical cylinder of radius `radius` (Duke's clipdist 164/8 = 20.5 u,
+   jfduke3d player.c:3320) whose reference point pos.y is the EYE; the body spans
+   [pos.y-dukeFoot+dukeStep, pos.y+dukeHead] against wall edges, the feet are at pos.y-dukeFoot. */
+#ifndef __INCLUDEDdukeplrh
+#define __INCLUDEDdukeplrh
+extern Sprite *camera;
+extern Fixed32 dukeFoot,dukeHead,dukeStep;
+#define DUKE_CAM(o)        ((o)==camera)
+#define SPR_FOOT(o)        (DUKE_CAM(o)?dukeFoot:(o)->radius)
+#define SPR_HOVER(o)       (DUKE_CAM(o)?0:F(8))
+#define SPR_HEAD(o)        (DUKE_CAM(o)?dukeHead:(o)->radius)
+#define SPR_SQUISH(o,c,fl) (DUKE_CAM(o)?(c)-dukeHead:(((c)+(fl))>>1))
+#define SPR_STEP(o)        (DUKE_CAM(o)?dukeStep:F(32))
+#define SPR_YLO(o)         (DUKE_CAM(o)?(o)->pos.y-dukeFoot+dukeStep:(o)->pos.y)
+#define SPR_YHI(o)         (DUKE_CAM(o)?(o)->pos.y+dukeHead:(o)->pos.y)
+#define SPR_YOUT(o,d)      (DUKE_CAM(o)?F(4096):(d))   /* body clear of the edge: no contact */
+#define SPR_SUPPORT(o,w)   ((DUKE_CAM(o)&&(w)->normal[1]>0)?f(dukeFoot-(o)->radius)*(w)->normal[1]:0)
+#define SPR_DY(m,s)        dukeDY((m),(s))
+#define PLAYER_RADIUS      (F(20)+(1<<15))             /* 20.5 u */
+#define DUKE_PLAYER_INIT(s) dukePlayerInit(s)
+#define DUKE_POSTURE(in)   dukePosture(in)
+#define DUKE_WEPDN         0                           /* Y = crouch, Z cycles weapons */
+#define WEP_NEXT(m,d)      dukeWeaponNext((m),(d))
+Fixed32 dukeDY(Sprite *m,Sprite *s);
+void dukePlayerInit(Sprite *s);
+void dukePosture(unsigned short input);
+int dukeWeaponNext(unsigned int mask,int d);
+void dukeShrink(void);
+#endif
--- /dev/null
+++ b/DUKEPLR.C
@@ -0,0 +1,95 @@
+/* DUKEPLR.C -- DUKE: Duke Nukem 3D player postures for maps converted by tools/duke2ps (DUKEPLAYER=1).
+   Values from tools/duke2ps/player_dims.py (simulation of jfduke3d player.c; no code copied):
+   eye above the floor 80 / 36 / 16 u (stand / crouch / shrunk), 8 u of head above the eye (ceildist
+   4<<8), step 40 u standing, nothing crouched or shrunk; shrunk for 9.6 s; crushed if the gap is under
+   48 u when growing back (GAME.CON ifgapzl 24). */
+#include <machine.h>
+#include "sega_mth.h"
+#include "level.h"
+#include "util.h"
+#include "sprite.h"
+#include "sruins.h"
+
+#define FOOT_STAND  F(80)
+#define FOOT_CROUCH F(36)
+#define FOOT_SHRUNK F(16)
+#define FOOT_SWIM   F(30)
+#define HEAD        F(8)
+#define STEP_STAND  F(40)
+#define STEP_LOW    F(2)
+#define EYE_SPEED   F(6)
+#ifdef PAL
+#define SHRINKFRAMES (50*96/10)
+#else
+#define SHRINKFRAMES (60*96/10)
+#endif
+
+Fixed32 dukeFoot=FOOT_STAND,dukeHead=HEAD,dukeStep=STEP_STAND;
+static int shrinkTimer;
+
+void dukePlayerInit(Sprite *s)
+{dukeFoot=FOOT_STAND; dukeHead=HEAD; dukeStep=STEP_STAND;
+ shrinkTimer=0;
+}
+
+void dukeShrink(void)
+{if (!shrinkTimer)
+    shrinkTimer=SHRINKFRAMES;
+}
+
+/* y distance from sprite s to the camera's core segment (capsule of radius camera->radius) */
+Fixed32 dukeDY(Sprite *m,Sprite *s)
+{Fixed32 d=m->pos.y-s->pos.y,lo,hi,c;
+ if (m!=camera && s!=camera)
+    return d;
+ lo=camera->radius-dukeFoot;                 /* below the eye */
+ hi=dukeHead-camera->radius;
+ if (hi<lo)                                   /* body lower than a sphere: use its middle */
+    lo=hi=(dukeHead-dukeFoot)>>1;
+ c=(m==camera)?-d:d;
+ if (c<lo) c=lo;
+ if (c>hi) c=hi;
+ return (m==camera)?d+c:d-c;
+}
+
+int dukeWeaponNext(unsigned int mask,int d)
+{int n=bitScanForward(mask,d);
+ if (n==-1)
+    n=bitScanForward(mask,-1);               /* wrap around: Z is the only weapon button */
+ return (n==d)?-1:n;
+}
+
+void dukePosture(unsigned short input)
+{Fixed32 target,step,room,dy;
+ if (shrinkTimer)
+    shrinkTimer--;
+ /* free height at the camera, own sector only (Duke probes a 163-unit radius) */
+ room=findFloorDistance(camera->s,&camera->pos)+findCeilDistance(camera->s,&camera->pos);
+ if (camera->flags & SPRITEFLAG_UNDERWATER)
+    {target=FOOT_SWIM; step=F(1);}
+ else if (shrinkTimer)
+    {target=FOOT_SHRUNK; step=STEP_LOW;}
+ else if (!(input & IMASK(ACTION_WEPDN)))    /* pad bits are active low */
+    {target=FOOT_CROUCH; step=STEP_LOW;}
+ else
+    {target=FOOT_STAND; step=STEP_STAND;}
+ if (target>dukeFoot && room<target+HEAD)
+    {/* no room to grow: shrink end -> crushed under 48 u, crouched under 88 u; crouch release -> stay */
+     if (dukeFoot<FOOT_CROUCH && !shrinkTimer && room<F(48))
+	{playerHurt(10000);
+	 target=dukeFoot;
+	}
+     else if (room>=FOOT_CROUCH+HEAD)
+	{target=FOOT_CROUCH; step=STEP_LOW;}
+     else
+	target=dukeFoot;
+    }
+ dy=target-dukeFoot;                          /* feet stay put: the eye moves with the body */
+ if (dy>EYE_SPEED) dy=EYE_SPEED;
+ if (dy<-EYE_SPEED) dy=-EYE_SPEED;
+ dukeFoot+=dy;
+ camera->pos.y+=dy;
+ dukeStep=step;
+}
```

**Vérifier que le binaire par défaut est inchangé** (après application, à faire par le propriétaire) :

```powershell
# avant le diff
C:\msys64\usr\bin\make; C:\msys64\usr\bin\make NDEBUG=1; C:\msys64\usr\bin\make PAL=1
Get-FileHash build\*.BIN, build\ndebug\*.BIN, build\pal\*.BIN | Format-Table Hash, Path > before.txt
# appliquer le diff, puis mêmes commandes -> after.txt
Compare-Object (Get-Content before.txt) (Get-Content after.txt)   # doit être vide
C:\msys64\usr\bin\make size                                       # text/data/bss identiques
C:\msys64\usr\bin\make DUKEPLAYER=1                               # build\duke\*.BIN, différents (attendu)
```

- **Comparer les `.BIN`, pas les `.elf`** : les informations DWARF de SPRITE.H changent, mais `objcopy -O binary` les laisse de côté.
- **Reproductibilité** : aucune source compilée n'utilise `__DATE__` ni `__TIME__` (MESURE : grep). Deux builds identiques sont donc attendus.

## 6. Risques et points ouverts

1. **Identité du binaire par défaut.** Elle repose sur le repli par GCC de `1 && (x)` (SRUINS.C:987), de `x − 0` (SPRITE.C:133) et de `; moveCamera();` en code identique. HYPOTHÈSE à confirmer par la comparaison des hachages ci-dessus.
2. **Cylindre approché.**
   - Pentes raides : `SPR_SUPPORT`.
   - Collision entre sprites : segment de capsule. Rétréci, le corps est plus plat que la sphère de 20,5 u, et la collision reste sphérique au milieu du corps.
   - Pièges laser : sphère de tête.
   - Hauteur libre : sondée dans le secteur de la caméra seulement.
3. **620 à 738 passages diagonaux** que Duke refuse et que le cercle passe. Correctif côté données : `WALLFLAG_SHORTOPENING` (§3).
4. **Décisions du propriétaire** :
   - marche accroupi ou rétréci : 2 u (comme Duke) ou 32 u ;
   - suppression du flottement de +8 pour la caméra Duke, pour que l'œil soit exactement à 80 u ;
   - perte du bouton « arme précédente » ;
   - libellé du menu à changer.
5. **Pas encore couverts** : les appelants de `dukeShrink`, la vitesse réduite et l'absence de ramassage une fois rétréci, le saut Duke (127 u de montée contre 56 u pour PowerSlave), et la surface de l'eau (`F(14)`).
6. **Timer de rétrécissement** : il compte en trames de `movePlayer`. HYPOTHÈSE : une VBL par trame, 60 Hz en NTSC et 50 Hz en PAL.

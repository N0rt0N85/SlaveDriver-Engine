# Sol dominant VDP2 dans SlaveDriver — étude (2026-09-12)

Question posée : *implémenter le sol dominant VDP2* dans le fork. Réponse courte : **c'est faisable
et le point d'accroche existe déjà dans le moteur (le ciel `WALLFLAG_PARALLAX` en est le patron
exact), mais RBG0 est DÉJÀ dépensé sur le ciel** — donc l'étude porte moins sur « comment dessiner
un sol » que sur **comment faire tenir deux plans de rotation sur une seule couche RBG0**.

Tout ce qui est marqué `[mesuré]` a été calculé ce jour sur les 24 `.LEV` retail
(`tools/study/`, scripts listés dans `tools/study/README.md`). `[src]` = lu dans ce dépôt.
`[HW-Mimas]` = fait matériel mesuré par Mimas, référencé dans `../saturn-refs/knowledge/HW_VDP2.md`.

---

## 1. Le gisement — combien de commandes VDP1 un sol dominant retire-t-il ?

Un mur de PowerSlave est découpé en cellules ; **une cellule = une commande VDP1**
(`WALLS.C:1135`, `WALLS.C:1179`). Le sol et le plafond d'un secteur sont deux **murs** du secteur
(normale dominante sur Y) — donc ils paient au même tarif que les murs verticaux.

**Part statique des cellules, 24 niveaux retail** `[mesuré]` (`tools/study/floorshare.py`) :

| | cellules | part |
|---|---|---|
| murs verticaux | 227 614 | 66,8 % |
| **sols** | **66 860** | **19,6 %** |
| plafonds | 46 021 | 13,5 % |

Le sol pèse donc ~1/5 des commandes du niveau entier — **et bien plus à l'écran**, parce que c'est
la surface la plus proche de la caméra (donc la plus grande en pixels : c'est le *fill*, loi L5 de
`POWERSLAVE_GAP_VERDICT.md`, pas seulement le compte de commandes).

**Mais un plan VDP2 n'affiche qu'UNE texture répétée.** Or les sols de PowerSlave sont des
mosaïques : chaque cellule porte son propre index de tuile (`level_texture[tex+1]`, `WALLS.C:1135`).
Deux mesures, dans cet ordre :

* **niveau entier** `[mesuré]` (`domfloor2.py`) : le couple (hauteur, tuile) le plus fréquent ne
  couvre que **13,8 % des cellules de sol** (médiane ; top-3 = 32,8 %). Pour les plafonds, 11,7 %.
  Il y a **35 à 93 hauteurs de sol distinctes par niveau**. Pris comme ça, le levier est mort.
* **voisinage de portails** `[mesuré]` (`neigh.py`) — la bonne unité, parce que le plan dominant
  s'élit *par image*, et qu'une image ne voit qu'un voisinage : en partant de chaque secteur et en
  suivant les portails, la part des cellules de sol qui partagent la hauteur **et** la tuile du
  secteur de départ est de **70 % à 1 saut, 50 % à 2 sauts, 37 % à 3 sauts** (médiane des médianes).

**Verdict du gisement** : élire « le plan sous les pieds du joueur » retire de l'ordre de **1/3 à
1/2 des cellules de sol visibles**, soit ~7-10 % des commandes de la frame.

> **CHIFFRÉ le 2026-09-12 par la mesure console** (`STEXT_BASELINE_2026-09-12.md`) : une cellule
> émise coûte **39,2 µs de CPU** et une frame en tient 190-824. 7-10 % = **50-70 cellules =
> 2,0-2,7 ms**. Or `framesElapsed` est entier : à 700 cellules il faut en retirer **227** pour
> passer de 20 à 30 fps. **Le sol dominant vaut ~1/6 d'un palier de vblank — réel, jamais
> suffisant seul.**
> Et l'argument « *plus* le *fill*, qui est disproportionné » que portait la première version de ce
> document est **affaibli** : `lastDraw` (l'attente résiduelle du VDP1) vaut ~1 ms dans 3 captures
> sur 5. Le CPU est le pôle long la plupart du temps — pas toujours (8,6 et 19,8 ms dans les deux
> autres), donc le fill n'est pas nul, mais on ne peut plus le mettre en avant.

Deux faits utiles trouvés au passage :

* 98 % des murs-sol **PARALLELOGRAM** n'utilisent qu'**une seule tuile** `[mesuré]` (`unif.py`) —
  la mosaïque vient des sols en **liste de faces** (75 % des sols), pas des sols rectangulaires.
* Le contenu **Doom** est un bien meilleur client que le contenu PowerSlave : un secteur Doom a
  **un** flat de sol, et un flat Doom fait **64x64, exactement la taille d'une tuile**. Voir
  `DOOM_ON_SLAVEDRIVER.md`. **Séquencement recommandé : le sol dominant VDP2 est un levier qui
  paie surtout APRÈS la conversion WAD vers `.LEV`.**

---

## 2. L'état des lieux VDP2 du moteur — et la surprise de la table K

`setVDP2()` (`SRUINS.C:1051-1077`) `[src]` :

| banc | 128 Ko | occupant | RDBS |
|---|---|---|---|
| A0 | 0x00000 | **table de coefficients RBG0** (`vcfg.vramA0=SCL_RBG0_K`) : 1 280 o utilisés sur 131 072 | `01` |
| A1 | 0x20000 | **bitmap RBG0 = le CIEL**, 512x256 8 bpp, lu du `.LEV` (`PLAX.C:84-115`) | `11` |
| B0 | 0x40000 | moitié haute du bitmap NBG0 512x512 8 bpp (`SRUINS.C:1079-1097`) | cycle `0x44ee` |
| B1 | 0x60000 | moitié basse du même bitmap NBG0 | cycle `0x44ee` |

* Cycles `{A0:0xeeee.., A1:0xeeee.., B0:0x44ee/0xeeee, B1:0x44ee/0xeeee}` : les bancs A sont
  *don't-care* (un banc de rotation est entièrement consommé, VDP2 p.31), et B ne donne à NBG0 que
  ses 2 lectures char/dot, donc **6 slots sur 8 libres dans B**.
* NBG0 ne sert **pas** en jeu : `updateVDP2Pic()` reprogramme W0 chaque frame et la fenêtre est
  `(0,0,0,0)` sauf pendant les séquences parlées (`PIC.C:455-497`, `SEQUENCE.C:288`).
* Le ciel est masqué par **W1 = bbox écran des murs PARALLAX**, recalculée chaque frame
  (`SRUINS.C:2293`, accumulation `WALLS.C:1666-1690`).
* L'erase VDP1 en jeu vaut **0x0000 = transparent** (`SRUINS.C:1974`), donc **le VDP2 traverse
  partout où le VDP1 n'a rien peint**. Le mécanisme dont un sol VDP2 a besoin est déjà en place.

**La surprise** `[mesuré]` (`tools/study/ktab.py`) : la table K du ciel est **par DOT, pas par
ligne**. `PLAX.C:110-112` écrit `k_delta.y = 1<<16` et `k_delta.x = 0` ; dans `SclRotreg`
(`sega_scl.h:197-216`) les offsets tombent sur la table de paramètres de rotation à
`k_tab = KAst (+0x54)`, `k_delta.x = dKAst (+0x58, par ligne)`, `k_delta.y = dKAx (+0x5C, par dot)`.
Donc dKAx = 1,0 et dKAst = 0 : **320 entrées, une par colonne d'écran, constantes sur la verticale**.
Et les 320 entiers lus du `.LEV` correspondent **exactement, 320/320**, à la formule laissée en
commentaire dans `PLAX.C:117-128` : `atan((x-160)/160)*128/0,785398`, normalisée par `(x-160)` —
**la correction cylindrique (atan) du panorama, appliquée par colonne**. Le `/* are these
backwards?? */` de l'auteur (`PLAX.C:112`) est tranché : elles sont dans le bon sens.

Conséquence pour la suite : **le ciel a besoin d'un coefficient par DOT**, un sol perspectif a
besoin d'un coefficient **par LIGNE**. Ce sont deux régimes différents — et c'est ce qui décide
du mode de sélection RPA/RPB.

---

## 3. Le point d'accroche moteur — déjà écrit, il s'appelle PARALLAX

`drawSector()` (`WALLS.C:1665-1692`) `[src]` :

```c
if (theWall->flags & WALLFLAG_PARALLAX)
   { /* n'émet AUCUNE commande : accumule seulement la bbox écran */
     for (i=0;i<4;i++) { if (poly[i].x<plaxBBxmin) plaxBBxmin=poly[i].x; ... }
     continue;
   }
```

Un **sol dominant** est littéralement le même objet : un mur qu'on **n'émet pas** et dont on
**accumule la bbox** pour en faire une fenêtre VDP2. Le patch moteur tient en trois morceaux :

1. **Élection** (1x/frame, avant `drawWalls`) : plan = `(floorLevel, tuile)` du secteur de la
   caméra (`camera->s`). Le champ `sSectorType.floorLevel` existe déjà (`SLEVEL.H:184`) ; la tuile
   dominante d'un mur-sol se pré-calcule **au chargement** (un octet par secteur ; 98 % des sols
   PARALLELOGRAM sont mono-tuile, les sols en faces demandent un vote — hors ligne de préférence).
2. **Saut + bbox** dans `drawSector`, juste avant le bloc PARALLAX : mur dont la normale est +Y,
   dont le plan est à la hauteur élue et dont la tuile est la tuile élue, donc `floorBB` puis
   `continue`. Le **cas de sûreté** est déjà là : si l'élection est fausse, on n'affiche pas un
   trou, on affiche le sol VDP2.
3. **Programmation** (fin de frame, à côté de `movePlax`, `SRUINS.C:2288-2295`) : matrice de
   rotation du sol (yaw + hauteur oeil-plan), table K par ligne, `W0 = floorBB`.

Coût estimé : **~150-250 lignes**, aucune structure nouvelle dans le `.LEV`, zéro octet de plus dans
le budget niveau. Le pré-calcul de la tuile dominante par secteur = 600 octets de `.bss`.

---

## 4. Les trois configurations possibles, et laquelle choisir

### O1 — RBG0 bi-paramètre : RPA = sol (K par ligne), RPB = ciel (K par dot) — **recommandé**

| banc | contenu | RDBS |
|---|---|---|
| A0 | coefficients : table ciel par dot (1 280 o) **et** table sol par ligne (240x4 = 960 o) | `01` |
| A1 | bitmap **sol** 512x256 8 bpp (une tuile 64x64 répliquée 8x4, donc répétition exacte : 1 texel = 1 unité monde, `TILESIZE 64`) | `11` |
| B0 | bitmap **ciel** 512x256 8 bpp (déménagé de A1) | `11` |
| B1 | bitmap NBG0 ramené à 512x256 (feuille de sprites VDP2) | `0x44ee` |

* `MPOFR` : `RAMP` (bits 2~0) = base du bitmap RPA, `RBMP` (bits 6~4) = base RPB, frontière
  x0x20000 (ST-58-R2 p.85-86). Ici `RAMP=1` (A1), `RBMP=2` (B0), donc `MPOFR = 0x0021`.
* **Sélection RPA/RPB : `RPMD = 3` (fenêtre de paramètre de rotation), PAS `RPMD = 2`.**
  Justification dure : en `RPMD=2` le sélecteur est le **MSB du coefficient de RPA**, lu au régime
  de RPA. Le ciel exige un régime **par dot** (§2) ; or une table par dot est indexée par X et
  **constante sur la verticale**, elle ne peut donc pas exprimer une frontière horizontale
  (l'horizon). `RPMD=3` donne à chaque jeu de paramètres son propre `KAst/dKAst/dKAx` et sépare par
  **rectangle d'écran** : c'est le seul mode qui garde le ciel par dot ET ajoute un sol par ligne.
* **Fenêtres** : `W0` = `floorBB` (sert deux fois — fenêtre de paramètre de rotation *et* moitié de
  la fenêtre de couche), `W1` = `plaxBB` comme aujourd'hui ; couche RBG0 activée en `W0 OR W1`
  (bit de logique par couche dans WCTLB). Aucune fenêtre supplémentaire n'est nécessaire.
* **Ce que SBL expose déjà** `[src]` : `SCL_InitRotateTable(Address, Mode, rA, rB)`
  (`sega_scl.h:1378`) prend **rA et rB** — le jeu passe `SCL_NON` pour rB (`PLAX.C:96`) ; et
  `Scl_r_reg.paramode` (`sega_scl.h:106`) **est** le registre RPMD. Le second jeu de paramètres est
  donc dans l'API, pas à poker à la main.

**Risques, nommés** :

* **R1 — RPB en bitmap n'a JAMAIS été validé sur matériel, nulle part.** Mimas a tenté ~10 builds HW
  (mémoire `rbg0-rpb-cell-mode-only`) : le mécanisme bi-paramètre tourne **propre, zéro neige**,
  mais RPB rendait **noir sous Ymir / bavure sur Saturn**. La cause a été trouvée plus tard
  (`vdp2-second-surface-plan`) — SGL écrivait `MPOFR` en mot entier sans masque (`RBMP=7`) et la
  copie de la table RPB tombait à `+0x68` au lieu de `+0x80` — mais **le correctif n'a jamais été
  testé sur console**. Il faut vérifier si `SCL_SetConfig(SCL_RBG0, ...)` de SBL 6.01 a le même
  défaut ; la lib est en COFF, ça se lit au désassemblage (même méthode que `SCL_VBLV.C`).
* **R2 — 3 bancs de rotation au lieu de 2.** Chaque banc de rotation est *entièrement* consommé par
  un seul type (VDP2 p.31) ; ici A0 sert deux flux de coefficients (un par dot, un par ligne). Le
  par-ligne coûte ~1/320 du par-dot, donc c'est *plausible*, pas prouvé.
  **Loi de sûreté `[HW-Mimas]`** : un banc K en régime par ligne peut loger n'importe où
  (VDP2 p.148) **mais doit rester déclaré `01`** — le rendre `00` corrompt les lectures sans neige
  (`HW_VDP2.md` §2, sonde `RBG0_A0_PROBE`).
* **R3 — la feuille NBG0 passe de 512x512 à 512x256.** C'est une modification de `STATIC.DAT` :
  vérifier d'abord que la moitié basse est réellement utilisée (mesure : dumper les
  `vdp2PicData[]` chargés par `PIC.C:681-686` et regarder `y + h`).

### O2 — sol sur RBG0, ciel rétrogradé en scroll normal

RBG0 redevient mono-paramètre (exactement la configuration **shippée sur matériel** par Mimas :
bitmap 512x256 en A1 + K par ligne en A0, `vdp2-floor-snows-on-hardware`), et le ciel part sur NBG1
(inutilisé) en cellules dans le banc B.

* **Avantage** : les deux moitiés sont prouvées sur console, séparément.
* **Coût rédhibitoire** : un scroll normal n'a qu'un **line scroll** (par ligne), alors que la
  correction atan du ciel est **par colonne**. On la perd : le ciel devient planaire et sa vitesse
  de défilement est fausse aux bords de l'écran quand on tourne. Dans un jeu extérieur, régression
  visible.
* Coût annexe : un ciel en cellules 8 bpp = **3 lectures/dot** (PN + 2 char), et c'est exactement la
  configuration qui a **neigé sur matériel** chez Mimas `[HW-Mimas]` ; la parade mesurée est le
  **4 bpp** (2 lectures/dot) — mais la CRAM est déjà pleine (8/8 bancs, `PIC.C:625-666`) et un
  panorama en 16 couleurs est un second renoncement visuel.

### O3 — ne pas le faire

Défendable **tant que le contenu est PowerSlave** : §1 donne 13,8 % au niveau, et le vrai gain
(1/3 à 1/2 des sols visibles) ne se lit que dans un voisinage. **Indéfendable dès que le contenu est
Doom** : un flat Doom est mono-tuile 64x64 et les secteurs sont grands.

### Ce qu'il ne faut PAS proposer

**RBG1 = impasse absolue** (ST-58-R2 p.7/148/150/162 + Table 1.4) : il tue tous les scrolls normaux,
câble B0+B1, est cell-only et force `RPMD=0`. Ne jamais le re-proposer (`vdp2-second-surface-plan`).

---

## 5. Le coût visuel : la frontière d'occlusion

Le plan VDP2 est derrière tout et **infini**. Là où la géométrie réelle descend sous le plan élu
(une fosse, une marche descendante, un secteur que la traversée de portails n'a pas atteint), le sol
VDP2 se voit **par-dessus le vide** : le joueur voit du sol là où il devrait voir un trou.

Trois parades, coût croissant (l'arbitrage est celui déjà posé dans `POWERSLAVE_GAP_VERDICT.md` §6
pour Mimas — **décision propriétaire 2026-08-31 : on accepte**) :

1. **Accepter** — la bbox `floorBB` borne déjà la fuite à l'enveloppe des sols dominants émis.
2. **Jupes de bordure** — sur les murs qui bordent une chute (`nextSector` dont le `floorLevel` est
   plus bas), émettre quand même la première rangée de cellules VDP1 du sol dominant : elle recouvre
   la couture. Coût : quelques commandes par bordure.
3. **Mini-clamp** — restreindre `floorBB` au rectangle des secteurs dont TOUS les voisins visibles
   partagent le plan. Plus sûr, gisement plus petit.

Atout propre à SlaveDriver que Mimas n'a pas : le peintre **connaît déjà la bbox écran de chaque
secteur** (`SectorDrawRecord.xmin..ymax`, utilisée pour `EZ_userClip`, `WALLS.C:2419`). La parade 3
est donc une intersection de rectangles déjà calculés, pas une machinerie nouvelle.

---

## 6. Ordre d'implémentation proposé (chaque étape se juge seule)

| # | étape | ce que ça prouve | coût |
|---|---|---|---|
| ~~0~~ | ~~`make STATUSTEXT=1`, une capture console~~ | **FAIT le 2026-09-12** : `calc ≈ 14,9 ms + 39,2 µs/cellule`, cache `used[0] = 14-18/28`. Voir `STEXT_BASELINE_2026-09-12.md` | — |
| 0b | refaire les mêmes captures en **`-NDebug`** | les captures étaient un build **assert** ; il y a 2 `assert()` **par cellule** dont un appel de fonction (`WALLS.C:1142,1172`). De combien baisse le 39,2 µs ? **C'est gratuit et ça passe avant tout le reste** | 1 disque |
| 1 | compter les cellules de sol sautées par l'élection, sans rien changer au rendu : `polys` baisse-t-il de 7-10 % ? | **le gisement réel, en jeu**, avant d'écrire une ligne de VDP2 | 1 j |
| 2 | O2 réduit : sol RBG0 **sans ciel** (`plaxOff()`), K par ligne, bitmap en A1 | que le sol perspectif est propre **sur console** dans ce moteur | 2-3 j |
| 3 | ciel sur RPB (O1), `RPMD=3`, `MPOFR=0x0021` | **R1** — le seul vrai inconnu | 3-5 j |
| 4 | jupes de bordure si la frontière gêne | visuel | 1-2 j |

⚠ Règle du projet : ne pas lancer l'émulateur soi-même ; livrer un disque et une table
« vu -> sens » (`never-launch-emulator-yourself`).

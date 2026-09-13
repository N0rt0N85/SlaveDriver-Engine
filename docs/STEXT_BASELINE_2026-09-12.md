# Première mesure console de SlaveDriver — 5 captures STATUSTEXT, Duke E1L1 converti (2026-09-12)

C'est **l'étape 0** que les trois études (`DOOM_ON_SLAVEDRIVER.md`, `VDP2_DOMINANT_FLOOR.md`,
`MULTIPLAYER_STUDY.md`) déclaraient obligatoire avant toute arithmétique. Le moteur n'avait jamais
été chronométré. Il l'est.

Contenu : **notre conversion de Duke Nukem 3D E1L1** (Hollywood Holocaust), pas du PowerSlave retail.
Build : **assert ON** (voir §5 — ce n'est pas un détail).

---

## 1. Décodage de l'overlay — depuis la source, pas depuis le glyphe

`SRUINS.C:2199-2227` et `:2253` `[src]` :

| ligne | format | ce que c'est vraiment |
|---|---|---|
| `fps:A B` | `60/framesElapsed`, `60/(smoothVTime+1)` | A = **instantané, quantifié aux vblanks** (60/1/2/3/4/5 = 60/30/20/15/12) ; B = la cible du gouverneur |
| `polys:N` | `nmPolys + nmSlavePolys` | **cellules de MURS, SOLS et PLAFONDS uniquement**. Les incréments sont en `WALLS.C:1136,1184,1243,1327` (maître) et `:1355,1495,1540,1583,1993` (esclave) — **les sprites ne sont PAS comptés** |
| `time:a b:c` | `(lastCalc+lastLastCalc)>>1`, `lastDraw`, `lastCalc+lastDraw` | **a est une moyenne sur 2 frames**, c utilise le `lastCalc` COURANT ⇒ `c ≠ a+b` en général, et **`calc` courant = c − b** |
| `mem:...` | `mem_coreleft(0)`, `mem_coreleft(1)` | aire 0 = LWRAM, aire 1 = HWRAM |
| `vswaps:` / `used:` | `pic_nextFrame(nmSwaps,used)` | par classe : **[0] = TILE16BPP = les tuiles de géométrie, 28 slots** |

**L'unité de `time` est la LIGNE DE BALAYAGE** : `htimer` est remis à 0 en tête de boucle
(`SRUINS.C:2066`) et incrémenté par l'interruption HBlank-IN (`V_BLANK.C:41-42`). En NTSC
320×240 non entrelacé, 262,5 lignes par field ⇒ **1 unité = 63,56 µs**, 1 field = 16,68 ms.

* `lastCalc` = **tout le travail CPU de la frame** : traversée de portails, émission, tic de jeu,
  jointure de l'esclave, arme, HUD, et l'overlay lui-même.
* `lastDraw` = le **reliquat d'attente du VDP1** après `SPR_WaitDrawEnd()`. ⚠ Le tracé démarre au
  changement de frame, donc `lastDraw` mesure la queue du **tracé de la frame PRÉCÉDENTE** : il ne
  s'attribue pas au `polys` affiché sur la même ligne.

---

## 2. Les cinq captures, en millisecondes

| secteur | polys | fps | **calc** | **draw** | travail | quantum vblank | oisif | used[0] | vswaps[0] |
|---|---|---|---|---|---|---|---|---|---|
| 319 | 190 | 30 | **22,5** | 8,6 | 31,1 | 33,4 (2 fields) | 2,2 | 14 | 0 |
| 317 | 824 | 12 | **47,2** | 19,8 | 67,0 | 83,4 (5) | 16,4 | 16 | 0 |
| 394 | 697 | 20 | **44,4** | 0,9 | 45,3 | 50,0 (3) | 4,8 | 17 | 0 |
| 13 | 662 | 20 | **38,7** | 0,8 | 39,5 | 50,0 (3) | 10,5 | 14 | 0 |
| 100 | 498 | 15 | **58,2** | 1,0 | 59,2 | 66,7 (4) | 7,6 | **18** | **1** |

Script : `tools/study/stext_fit.py`.

---

## 3. Ce que ça établit

### 3.1 La loi de coût : **calc ≈ 14,9 ms + 39,2 µs par cellule**

Régression sur les 4 captures sans swap, résidus **±2,2 ms** :

```
polys  190  calc 22,5  modele 22,4   (+0,1)
polys  824  calc 47,2  modele 47,2   (-0,1)
polys  697  calc 44,4  modele 42,3   (+2,1)
polys  662  calc 38,7  modele 40,9   (-2,2)
```

**Comparaison, enfin possible** — coût marginal d'une commande VDP1 émise :

| | µs / commande | source |
|---|---|---|
| **SlaveDriver** (ce jour, console) | **39** | ci-dessus |
| Mimas, loi L4 | 64,5 | A/B console 2026-08-31 |
| Mimas, branche `psw-world` round 28 | 95-100 | 4 captures console, linéaire en commandes |

**SlaveDriver émet 1,6× à 2,5× moins cher que Mimas**, et c'est avec les asserts allumés (§5).
C'est la justification chiffrée de l'étude `DOOM_ON_SLAVEDRIVER.md` : on n'écrit pas un émetteur,
on emprunte celui qui coûte 39 µs.

### 3.2 Le plancher de 14,9 ms est la moitié du problème

À 700 cellules, le fixe est encore **1/3** du temps CPU. Ce qu'il contient n'est pas mesuré :
traversée `findDoorways` (proportionnelle aux **secteurs visités**, pas aux cellules), tic de jeu,
`drawSprites` (maître seul, non compté dans `polys`), arme + barre d'état, **et l'overlay
STATUSTEXT lui-même** (6 `drawStringf`, chaque glyphe = une commande VDP1).

### 3.3 Le budget par palier de vblank — la vraie unité de décision

`framesElapsed` est entier : le gain ne se voit que s'il fait franchir un palier.

| palier | budget de cellules (au modèle) |
|---|---|
| 30 fps (33,4 ms) | **470** |
| 20 fps (50,0 ms) | **896** |
| 15 fps (66,7 ms) | **1321** |

Passer de 697 cellules (20 fps) à 30 fps demande de **retirer 227 cellules (−33 %)** *ou* de baisser
le fixe. Aucun levier isolé ne le fait : **il faut les cumuler.**

### 3.4 Le cache de 28 tuiles TIENT — sur du contenu Build converti

`used[0]` = **14, 16, 17, 14, 18** sur 28 ; `vswaps[0]` = 0, 0, 0, 0, 1.

C'est la **validation directe** de la prédiction de `DOOM_ON_SLAVEDRIVER.md` §4.3 (Doom shareware
mesuré à 11-15 tuiles distinctes à 2-3 sauts de portail) : une carte Build convertie avec des
cellules à l'échelle E4.1b tient dans le cache, avec ~10 slots de marge. La contrainte qui avait
tué E4.1 est **sous contrôle**.

### 3.5 Le VDP1 n'est pas systématiquement le second rôle

`draw` vaut ~1 ms dans 3 captures sur 5 (CPU largement le pôle long) mais **8,6 et 19,8 ms** dans
les deux autres. Comme `draw` est la queue du tracé de la frame *précédente*, on ne peut pas
l'attribuer au `polys` affiché — mais l'ordre de grandeur (47,2 + 19,8 = 67 ms de tracé pour une
frame chargée) est **cohérent avec ~60 µs de tracé par commande**, c'est-à-dire la loi L5/L4 de
Mimas. **Conclusion prudente : CPU et VDP1 sont du même ordre ; le CPU passe devant la plupart du
temps, pas toujours.**

### 3.6 L'anomalie de la capture 5 : **+24 ms que `polys` n'explique pas**

498 cellules, 58,2 ms de calc, contre 34,4 ms au modèle. Un seul `vswap` ne peut pas coûter 24 ms
(l'expansion palette = 4 096 itérations + un DMA de 8 Ko, `PIC.C:352,397`, soit ~3 ms au plus).
Trois candidats, aucun tranché :

1. **Le maître qui spinne sur la jointure de l'esclave.** `drawWallsFinish` fait
   `while (!(*FTCSR & 0x80)) i++;` (`WALLS.C:2452-2455`) **et ce spin est compté dans `calc`**.
   `slaveSize` ne s'ajuste que de **±1 secteur par frame** sur un seuil de 100 spins
   (`WALLS.C:2281-2285`) : après un changement de scène, le maître peut attendre plusieurs frames.
2. **Les sprites**, qui ne sont pas dans `polys` (§1). Scène de salle à torches.
3. La traversée de portails, proportionnelle aux **secteurs visités**.

**Sonde pour trancher, 2 lignes** : afficher `slaveSize`, le compteur de spins `i`, le nombre de
secteurs de `updateListSize` et le nombre de commandes de sprites. C'est la mesure suivante.

---

## 4. Ce que ça change dans les trois études

| étude | avant | après cette mesure |
|---|---|---|
| **Doom sur SlaveDriver** | « l'émetteur de SlaveDriver est *probablement* moins cher que celui de Mimas » | **39 µs vs 64,5-100 µs, mesuré des deux côtés.** Et le cache de 28 tuiles est validé sur du contenu converti (§3.4). L'hypothèse centrale tient |
| **Sol dominant VDP2** | gain = commandes **+ fill disproportionné** | l'argument *fill* est **affaibli** : `draw` ≈ 1 ms dans 3/5 (§3.5). Le gain se compte côté CPU : ~7-10 % des cellules ⇒ **50-70 cellules ⇒ 2,0-2,7 ms**, soit **~1/6 d'un palier de vblank**. Réel, mais **jamais suffisant seul** (§3.3) |
| **Multi 2/3/4p** | banque de 1 448 commandes = mur n°1 | **moins grave que prévu** : 190-824 cellules pour une vue plein écran ⇒ 4 vues recadrées ≈ 1 100-1 300, ça rentre de justesse. En revanche `used[0] = 14-18` **pour une seule vue** ⇒ 4 vues dispersées = 56-72 tuiles pour 28 slots : **le cache reste le mur, et il est le seul** |

**Projection 4p, maintenant chiffrée** (modèle §3.1, recadrage FOV à 53°, ~40 % des cellules par
vue, fixe supposé ~2/3 par vue et ~1/3 par frame) : `5 + 4 × (10 + 0,039 × 280)` ≈ **89 ms ≈ 11 fps**
en build assert. C'est **en dessous** des 17-19 fps que j'avais extrapolés, et au-dessus des
4,7-6,0 fps de Mimas en 4p. Le NDEBUG et la coupe du fixe décident si ça devient jouable.

---

## 5. ⚠ Le plus gros levier est un **flag de build**, et il est gratuit

**Ces captures sont un build ASSERT.** La ligne `extra:` et le bloc `vswaps/used` sont sous
`#ifndef NDEBUG` (`SRUINS.C:2204`, `:2232`) — donc `NDEBUG` n'est pas défini, donc **tous les
`assert()` du moteur tournent**, y compris dans les boucles les plus chaudes :

* `assert(level_texture[tex]<8)` — **par cellule** (`WALLS.C:1142`) ;
* `assert(getPicClass(level_texture[tex])==TILE16BPP)` — **par cellule, et c'est un APPEL DE
  FONCTION** (`WALLS.C:1172`) ;
* `assert(width*height<MAXVPERWALL)` par mur, 3 asserts de normale par mur dans `drawSector`
  (`WALLS.C:1614-1616`), `validPtr()` à chaque `dmaMemCpy`…

Et le retail, lui, est NDEBUG (chaîne « PRINT.C » absente du `MAIN.BIN` retail). **La mesure à
faire avant toute autre : refaire ces cinq captures avec `build.ps1 -NDebug -StatusText`**
(`Makefile:48`, arbres séparés — `STATUSTEXT` seul reste possible, seuls `extra`/`vswaps`/`used`
disparaissent avec les asserts). Deux inconnues que ça lève d'un coup : de combien baisse le
39 µs/cellule, et de combien baisse le fixe de 14,9 ms.

**Ordre recommandé de la suite** :

1. `-NDebug` + STATUSTEXT, mêmes 5 endroits → nouvelle loi de coût. **Gratuit.**
2. Sonde `slaveSize` / spins / `updateListSize` / commandes de sprites (§3.6) → nomme les 14,9 ms
   et l'anomalie de +24 ms. **2 lignes.**
3. Alors seulement : sol dominant VDP2, LOD, multi-vue — avec un budget en cellules par palier
   (§3.3) et non plus en pourcentages.

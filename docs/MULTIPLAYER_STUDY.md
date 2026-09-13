# Multijoueur 2/3/4 joueurs sur SlaveDriver — étude (2026-09-12)

Question posée : *multijoueur 2, 3 et 4 p. Comment, limitations, hypothèses pour rendre ça viable.*

Réponse courte. **Le rendu multi-vue est facile dans ce moteur** — bien plus facile que dans Mimas :
la projection est déjà centrée, l'origine VDP1 est déjà une commande (`FUNC_LCOORD`), le clip est
déjà par secteur, et **l'entrée multitap est déjà initialisée pour 6 périphériques par port**.
**Ce qui est difficile, ce sont deux plafonds matériels partagés par toutes les vues** — la banque
de 1 448 commandes VDP1 et, plus grave, le **cache de 28 tuiles** — et **le fait que le jeu n'a
aucune règle multijoueur** (PowerSlave est strictement solo : aucune trace de MP dans les sources).

`[src]` = lu dans ce dépôt. `[mesuré]` = calculé ce jour. `[Mimas]` = mesuré sur l'autre port.

---

## 1. Ce qui est déjà en place — et c'est plus que prévu

| brique | état | référence |
|---|---|---|
| **Entrée multitap** | **déjà armée** : `PER_LInit(PER_KD_PERTIM, 6, PER_SIZE_NCON_15, PadWorkArea, 0)` — **6 périphériques par port**, et `PadWorkArea` est dimensionné `4*(15*6+69)`. `processInput` boucle bien `Mul[0].con` périphériques — mais **écrase tout dans un seul accumulateur et `break` au premier pad** | `V_BLANK.C:234`, `:39`, `:46-91` |
| **Origine d'écran par vue** | `EZ_localCoord(x,y)` est une **commande VDP1** (`FUNC_LCOORD`), donc gratuite et re-émissible autant de fois qu'on veut dans une frame. Le jeu la pose une fois à (160,120) | `SPR.C:314-320`, `SRUINS.C:2109` |
| **Clip par vue** | `EZ_userClip` est déjà émis **par secteur** avant chaque `drawSector` (`parms = bbox + 160/+120`). Il suffit de remplacer les constantes 160/120 par le centre de la vue | `SPR.C:322-330`, `WALLS.C:2415-2423` |
| **Fenêtre de traversée** | la traversée de portails démarre du rectangle `sectorDraw[camera->s].xmin..ymax = XMIN..YMAX`. Le poser au rectangle de la vue **clippe toute la traversée** et cull plus tôt | `WALLS.C:2270-2276`, `WALLS.C:84-87` |
| **Projection** | `project_point` sort des coordonnées **centrées en (0,0)** ; FOCALDIST = 160 en dur dans l'asm (`mov #80 ; shll`) — FOV horizontal 90° | `wallasm_gnu.s:29-64` |
| **État par vue** | **il n'y en a pas** : `sectorDraw[]`, `updateList/drawList/leafList`, `doorwayCache[]` sont tous remis à zéro en tête de `drawWalls` et réutilisables en séquence. Le seul état par joueur est `camera` (un `Sprite*`) et `playerAngle` | `WALLS.C:2266-2268` |
| **Règles multijoueur** | **aucune**. Pas de deathmatch, pas de respawn, pas de frags, pas de starts multiples. `currentState` est un état de partie unique (`SaveState`) | — |
| **Réseau** | **aucun** dans PowerSlave. Duke Nukem Saturn = SlaveDriver v2, **avec NETLINK/XBAND**, mais sans sources publiées | `RETAIL_DISCS.md` |

---

## 2. Le rendu multi-vue : la forme du code

La frame actuelle (`SRUINS.C:2102-2180`) `[src]` :

```
EZ_openCommand(); sysClip; userClip(plein); localCoord(160,120);
drawWalls(view);                 <- lance l'esclave, dessine les secteurs lointains
movePlayer(); runObjects();      <- LA LOGIQUE DE JEU TOURNE PENDANT QUE L'ESCLAVE CALCULE
drawWallsFinish();               <- attend l'esclave (FRT), ré-émet ses <=1300 records
HUD, arme, barre d'état
EZ_closeCommand(); SPR_WaitDrawEnd();
```

La forme multi-vue :

```
EZ_openCommand(); sysClip;
for (v = 0; v < nviews; v++) {
    localCoord(cx[v], cy[v]);            /* 1 commande VDP1 */
    userClip(viewport[v]);
    camera = players[v].sprite; playerAngle = players[v].angle;
    buildViewMatrix();
    drawWalls(view_v);                   /* la traversee part du rect de la vue */
    if (v == 0) { movePlayer_all(); runObjects(); }   /* une seule fois, dans la fenetre esclave */
    drawWallsFinish();
    drawHud(v);
}
EZ_closeCommand(); SPR_WaitDrawEnd();
```

**Géométrie** (reprendre celle de Mimas, éprouvée — `split-screen-geometry`) : 2p = **vertical
gauche/droite** (frontière x = 160), 3/4p = **quadrants**. Attention : l'aire de jeu de PowerSlave
est `y ∈ [-110, +90]` (320x200), la barre d'état occupe les lignes 200-240 (`WALLS.C:84-87`).

**Deux façons de traiter le FOV**, et elles ne coûtent pas la même chose :

* **(a) recadrer** — garder FOCALDIST = 160 et poser le rectangle de vue à ±80 / ±55. Zéro ligne
  d'assembleur. FOV horizontal 2·atan(80/160) = **53°** au lieu de 90°. Chaque vue voit **moins de
  monde** : c'est le levier de coût.
* **(b) dézoomer** — rendre FOCALDIST variable (une lecture mémoire au lieu de `mov #80 ; shll`
  dans `project_point` **et** dans `rectTransform`). FOV 90° conservé, mais **chaque vue coûte
  autant qu'une vue plein écran** en commandes.

Recommandation : **(a) en 3/4p** (le recadrage est de toute façon esthétiquement acceptable sur un
quadrant), **(b) en 2p** si la mesure le permet.

---

## 3. Les deux plafonds durs — c'est ici que se joue la viabilité

### 3.1 La banque de commandes VDP1 : 1 448, partagées

`EZ_initSprSystem(1448, 4, 1224, 240, 0x8000)` (`SRUINS.C:1879`) `[src]`. Les commandes tronquent
**en silence** (`SPR.C:141-143`) : au-delà, des murs disparaissent. En 4p c'est **362 commandes par
vue**.

> **MESURÉ le 2026-09-12** (`STEXT_BASELINE_2026-09-12.md`, Duke E1L1 converti) : une vue plein
> écran émet **190 à 824 cellules**. Quatre vues recadrées à ~40 % ⇒ **1 100-1 300 commandes** :
> **ça rentre de justesse dans les 1 448**. Ce mur-ci est donc **moins grave que je ne l'écrivais** —
> mais il n'a aucune marge, et les sprites ne sont pas dans ce compte.

Peut-on monter 1 448 ? La VRAM VDP1 fait 512 Ko et elle est pleine : 2×1448×32 = 92 672 (commandes)
+ 2×1224×8 = 19 584 (gouraud) + 128 (CLUT) + ~389 Ko de slots de textures
(`initPicSystem(i,{28,31,1,10,12,-1})`, `SRUINS.C:1903` : 28×8 Ko + 31×4 Ko + 10×2 Ko + 12×1 Ko).
**Doubler la banque de commandes coûte 92 Ko, c'est-à-dire 11 slots de tuiles de géométrie sur 28**
— or ces 28 slots sont l'autre plafond. **Les deux leviers se mangent l'un l'autre.**

### 3.2 Le cache de 28 tuiles : le vrai mur, et il est *correctif*, pas seulement perf

C'est la contrainte la plus dure du moteur, déjà payée une fois dans la chaîne Duke (E4.1). `map()`
part de `oldTime = frameCount+1` et évince tout slot dont `lastUse < oldTime` (`PIC.C:271-275`) :
**une tuile déjà utilisée DANS la frame courante est éligible à l'éviction**. Au-delà de 28 tuiles
distinctes dans une frame, une commande déjà en file pointe sur des pixels devenus ceux d'une autre
tuile : **texture fausse, en plus de lente** (un miss coûte 4 096 itérations d'expansion palette +
un DMA de 8 192 o, `PIC.C:352,397`).

Or les vues d'un multijoueur sont, par construction, dans des endroits différents de la carte.
`[mesuré]` — tuiles distinctes dans un voisinage de portails :

| contenu | 1 vue (2 sauts) | 4 vues, pire cas |
|---|---|---|
| PowerSlave retail | 4 médian, 12 au p99, 25 au max | 16 à 100 |
| Doom shareware (`wadcache.py`) | 11 (2 sauts), 15 (3 sauts) | 44 à 60 |

**Conclusion : en 3/4p avec des joueurs dispersés, le cache est dépassé par construction.** Ce n'est
pas un réglage, c'est un fait d'architecture.

> **MESURÉ le 2026-09-12**, Duke E1L1 converti, **une seule vue plein écran** : `used[0]` =
> **14, 16, 17, 14, 18** sur 28 slots. Quatre vues dispersées demandent donc **56 à 72 tuiles pour
> 28 slots**. Le chiffre est confirmé et **le cache reste le seul vrai mur du multi-vue.**

### 3.3 Les quatre parades, par ordre de force

1. **Coups d'envoi séquentiels (le vrai remède).** Rendre une vue, `SPR_WaitDrawEnd()`, puis
   réutiliser **la même** banque de commandes et **le même** cache pour la vue suivante, dans le
   même back-buffer, chaque passe bornée par son `userClip`. Chaque vue récupère alors
   **1 448 commandes et 28 slots pour elle seule**. Prix : (a) il faut passer le VDP1 en **départ
   manuel** (aujourd'hui `PTMR=2`, départ automatique au changement de frame, via `SPR_Initial` +
   `SCL_SetFrameInterval(0xfffe)`) — Mimas l'a fait (« present manuel v2 ») et c'est documenté comme
   délicat ; (b) on perd le recouvrement CPU/VDP1 entre les vues. **C'est la parade à instruire en
   premier** : c'est la seule qui lève les deux plafonds d'un coup.
2. **Co-op rapproché plutôt que deathmatch dispersé.** Si les joueurs restent dans le même
   voisinage, les vues partagent leurs tuiles et les deux plafonds redeviennent ceux du solo.
   C'est une **décision de design**, pas un contournement : le co-op est la forme viable, le DM est
   la forme coûteuse.
3. **Recadrer + LOD par vue** (option (a) du §2, plus un arrêt de la traversée par profondeur).
   Réduit les commandes, pas le nombre de tuiles distinctes.
4. **Verrouiller les tuiles communes** (`PICFLAG_LOCKED` existe, `PIC.C`) : épingler les N tuiles
   les plus utilisées de la carte pour qu'elles ne soient jamais évincées. Réduit la casse, ne la
   supprime pas.

---

## 4. Le budget de temps — modèle et ce qu'on peut en dire honnêtement

Coût d'une vue ≈ `k1·secteurs + k2·cellules + k3·commandes` (aucune boucle par pixel côté CPU).
La partie *fill* (le temps de tracé VDP1) est à peu près **constante** quel que soit le nombre de
vues : quatre quadrants couvrent le même écran.

* avec le **recadrage (a)** : une vue de quadrant voit de l'ordre de **35-45 %** du monde d'une vue
  plein écran (53° au lieu de 90° en horizontal, moitié moins de lignes) -> 4 vues ≈ **1,6-1,8×** le
  coût CPU du solo.
* avec le **dézoom (b)** : 4 vues ≈ **4×**.

**RÉVISÉ avec la mesure console du 2026-09-12** — le modèle n'est plus un pourcentage mais une loi :
`calc ≈ 14,9 ms + 39,2 µs par cellule` (build assert, `STEXT_BASELINE_2026-09-12.md`). Le **fixe de
14,9 ms** change la conclusion, parce qu'une bonne partie en est **par vue** (traversée de portails,
jointure de l'esclave, émission) et non par frame.

En posant ~2/3 du fixe par vue et ~280 cellules par vue recadrée :
`5 + 4 × (10 + 0,039 × 280)` ≈ **89 ms ≈ 11 fps en 4p**, pas 17-19 comme je l'avais extrapolé.
Le solo mesuré est à **20 fps (44 ms) sur cette carte**, pas 30. À comparer aux **4,7-6,0 fps** de
Mimas en 4p sur matériel : c'est **~2× mieux**, mais ce n'est pas jouable en l'état.

**Les deux leviers qui décident** : (1) le build **`-NDebug`** (deux `assert()` par cellule, dont un
appel de fonction — jamais mesuré) ; (2) **décomposer le fixe de 14,9 ms**, puisqu'il est multiplié
par le nombre de vues. Tant que ces deux-là ne sont pas faits, tout chiffre 4p reste une projection.

Point de comparaison `[Mimas]` : Mimas fait **4,7-6,0 fps en 4p sur vrai matériel** (MST 166-212 ms,
vidéo 2026-08-20) et son goulot en split est le **maître** — pas le fill, pas l'esclave. Si
SlaveDriver arrive à 15-19 fps en 4p, ça vaut le détour ; c'est précisément l'hypothèse à tester.

**L'esclave** : il transforme les murs des `slaveSize+1` secteurs les plus proches
(`WALLS.C:1806-1819`), avec équilibrage ±1 secteur/frame sur un seuil de 100 spins
(`WALLS.C:2281-2285`). En multi-vue il faut **un `slaveSize` par vue** (un seul compteur global
oscillerait entre des vues de charges différentes) : 4 octets de `.bss`, pas un chantier.

---

## 5. Le VDP2 en multi-vue — la contrainte que personne n'anticipe

Le ciel est une couche **RBG0 unique**, avec **un seul jeu de paramètres de rotation par frame** et
**une seule fenêtre W1** (`SRUINS.C:2288-2295`, `PLAX.C:37-51`). Or deux joueurs regardent dans des
directions différentes.

* **2p : soluble proprement.** `RPMD = 3` (fenêtre de paramètre de rotation) + W0 = moitié gauche
  -> **RPA = ciel de P1, RPB = ciel de P2**. C'est exactement l'idée « N1 » de Mimas
  (`vdp2-second-surface-plan`), et le découpage 2p est **vertical**, donc un rectangle : c'est le
  bon sélecteur. ⚠ RPB en bitmap n'a jamais été validé sur matériel (voir
  `VDP2_DOMINANT_FLOOR.md` §4, risque R1).
* **3/4p : insoluble en l'état.** Deux jeux de paramètres, quatre yaws. Trois issues : le ciel du
  joueur 1 pour tout le monde (faux mais peu visible dans un quadrant), un ciel plat (couleur
  unie / dégradé par ligne via un scroll normal), ou un ciel en quads VDP1 (cher).
* **La fenêtre de couche** : chaque vue accumule sa `plaxBB` dans son propre repère, donc dans son
  propre quadrant ; il faut prendre l'**union** (les fuites ne peuvent apparaître que là où le VDP1
  n'a rien peint — c'est-à-dire, en pratique, là où il devrait y avoir du ciel).
* **Conflit direct avec le sol dominant VDP2** : il réclame W0 (`VDP2_DOMINANT_FLOOR.md` §4).
  En 2p, W0 est pris par le sélecteur de ciel. **En multijoueur, c'est ciel HW OU sol HW, pas les
  deux.** À décider, pas à découvrir.

---

## 6. Ce qu'il faut ÉCRIRE côté jeu (ce n'est pas du rendu, et c'est le gros du travail)

PowerSlave est un jeu solo de bout en bout. Il manque :

1. **Entrée par joueur** — ouvrir `processInput` (`V_BLANK.C:46-91`) : indexer par (port, slot) au
   lieu d'accumuler et de `break`. `PER_LInit` est déjà à 6 périphériques.
   ⚠ piège connu `[Mimas]` : le second terminal SMPC commence à l'index brut **15**, pas 1.
   ~30 lignes.
2. **Un état par joueur** — aujourd'hui `player` (`PlayerObject*`), `camera` (`Sprite*`),
   `playerAngle`, `currentState` (`SaveState`) et la file d'entrée sont **globaux**
   (`SRUINS.C:78-83`). Il faut un tableau et un « joueur courant ». C'est mécanique mais large :
   `AI.C` (6 335 lignes) et `WEAPON.C` (1 006) supposent un joueur unique partout.
3. **Points d'apparition multiples** — `OT_PLAYER` est **un** objet par `.LEV`
   (5 shorts : secteur, x, y, z, angle). Il faut un type d'objet « départ co-op » et un
   « départ deathmatch », donc une extension du format ou un détournement d'un type libre.
4. **Règles** : respawn, frags, télé-fragging, objets qui repoussent, fin de partie, score. Rien de
   tout ça n'existe.
5. **HUD par vue** — `STATBAR.C` dessine une barre unique en lignes 200-240 en chars VDP1.
   En quadrants il faut une bande par vue (`[Mimas]` : 16 px par quadrant, `hud-3-4p-band`).
6. **Son** — 32 slots SCSP, pan/volume par source (`SOUND.C`). En multi-vue le pan n'a plus de
   référentiel unique : convention à choisir (le plus proche des N joueurs, ou joueur 1).

**Estimation** : 2 à 4 semaines pour un co-op 2p jouable, la moitié dans le point 2.

---

## 7. Les autres transports — pour mémoire, et parce que le précédent existe

* **Split-screen local (multitap)** — l'objet de cette étude. Déterministe par construction, pas de
  désynchronisation possible. **C'est le seul chemin où 3 et 4 joueurs ont un sens** ; les deux
  autres sont intrinsèquement à 2.
* **Câble Taisen (HSS-0107, port série arrière)** — 2 consoles, 2 joueurs plein écran, ~6 jeux
  officiels l'ont utilisé, **dont Doom Saturn (Rage, 1997) en coop ET deathmatch**. Lève TOUS les
  plafonds du §3 (chaque console a ses 1 448 commandes et ses 28 tuiles) et tout le §5. Coût :
  un transport + du lockstep.
* **NetLink (modem 28,8k en port cartouche)** — UART 16550 sur A-bus CS2
  (`0x25895001/05/09/...`), IRQ SCU EXT 12, alimentation par commande SMPC 0x0B/0x0A, commandes
  Hayes AT ; **SBL 6.01 embarque une lib NetLink**. Point à point = **2 joueurs**. Précédent direct
  et gênant : **Duke Nukem 3D Saturn, c'est-à-dire SlaveDriver v2, a shippé le deathmatch NetLink
  en 1997** — le moteur de ce dépôt est l'ancêtre direct de ce code, mais les sources v2 ne sont pas
  publiées. Homebrew moderne : « Coup » (2026) a fait tourner du NetLink 6 joueurs sur vrai matériel
  et parle de publier sa pile réseau.

---

## 8. Hypothèses pour rendre ça viable — dans l'ordre où je les testerais

| # | étape | ce qu'elle tranche | coût |
|---|---|---|---|
| ~~**0**~~ | ~~capture console solo STATUSTEXT~~ | **FAIT 2026-09-12** : 39,2 µs/cellule + 14,9 ms de fixe ; `used[0]` 14-18/28 ; 190-824 cellules/vue | — |
| **0b** | les mêmes captures en **`-NDebug`**, + une sonde décomposant le fixe (`slaveSize`, spins de jointure, `updateListSize`) | le fixe est **multiplié par le nombre de vues** : c'est lui qui décide si le 4p est à 11 fps ou à 18 | 1 disque + 2 lignes |
| **1** | **2 vues du MÊME joueur** (écran coupé en deux, même caméra), sans toucher au jeu | isole **entièrement** le coût du rendu multi-vue : `polys` double-t-il ? `vswaps` explose-t-il ? le fps ? Zéro ligne de gameplay | 2-3 j |
| **2** | coups d'envoi séquentiels (VDP1 en départ manuel + `WaitDrawEnd` entre vues) | **la parade n°1 du §3.3** : est-ce que chaque vue retrouve ses 1 448 commandes et ses 28 slots ? | 4-6 j |
| **3** | entrée multitap + état par joueur (§6.1 et §6.2) | le gros morceau mécanique | 10-15 j |
| **4** | co-op 2p : départs, respawn, HUD par vue, ciel `RPMD=3` | premier jeu à deux réel | 8-12 j |
| **5** | 3/4p quadrants + recadrage FOV + `slaveSize` par vue | l'hypothèse 15-19 fps du §4 | 5-8 j |

**La marche à ne pas sauter, c'est l'étape 1** : elle mesure exactement le prix du multi-vue sans
payer une ligne des trois semaines de l'étape 3, et elle répond à la seule question qui décide de
tout — *est-ce que deux vues tiennent dans une banque de commandes et un cache de 28 tuiles ?*

⚠ Règle du projet : ne pas lancer l'émulateur soi-même ; livrer un disque et une table
« vu -> sens » (`never-launch-emulator-yourself`).

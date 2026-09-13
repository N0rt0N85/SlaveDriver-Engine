# R10 — Ce que le moteur fait par frame, et comment le mesurer étage par étage (lecture du 2026-09-14)

Rapport brut d'un lecteur (révision v2 du plan). Contexte : loi de coût 14,9 ms + 39,2 µs/cellule
mesurée en build ASSERT+STATUSTEXT sur Duke E1L1 converti (docs/STEXT_BASELINE_2026-09-12.md).

## 0. État du disque (lecture seule)

`git diff --stat` : **16 fichiers, +363/−42** — les 14 annoncés **+ `Makefile` (+40)** ; nouveaux non
suivis : `PLRCYL.C/H`, `ubc_gnu.S`, `gameparams.cfg`, `params/`, `docs/*` (11), `tools/*`,
`build_sd.log`, et un dossier parasite `C\357\200\272/` (« C: » mal encodé).

`git diff PROFILE.C PROFILE.H` (+53/+3) : ajoute `lastShownTime` au nœud (PROFILE.C:38), un rendu
**à l'écran** de l'arbre — `drawTree`/`drawProfileData` (PROFILE.C:156-183) via `drawString`, en ms
depuis la frame précédente (`PROF_TENTHS = ticks*2/179`, :144), 14 lignes max (:145), gardé par
`EZ_getCmdRoom()-128` (:150,:181) — et `int profileShow` (:152). PROFILE.H expose les deux et les vide
sous `NPROFILE` (:10-11,:19). Le reste du diff : SRUINS.C bascule L+R+Y / A+B+C → `profileShow`
(:2084-2099) et appelle `drawProfileData(-158,-28)` (:2243-2244), `polys:` gagne `cx/cy` (:2233) ;
WALLS.C/WALLASM.H/SPR.C = sonde `probeVDP1` + `EZ_distSprVClip` (fenêtrage V, disque diag `VDP1DIAG 1`
qui **peint à plat** les cellules hors ±1023) ; SPRITE.H = macros `gameparams.h`.

## 1. La boucle `runLevel` — étages dans l'ordre (SRUINS.C:2065-2347)

| # | étage | où | coût ∝ | attend |
|---|---|---|---|---|
| 1 | `htimer=0`, chords pad (le pad est lu à VBlank-OUT par `processInput`, V_BLANK.C:151-154 → `inputQ`) | :2066-2100 | const | – |
| 2 | matrice vue `MTH_*` | :2105-2118 | const | – |
| 3 | `EZ_openCommand` (bascule de banc) | :2120 | const | – |
| 4a | `drawWalls` → lumières, **Find Doorways** : `findDoorways(camera->s)` + boucle `NEEDTOPROCESS` (par portail : 4 `MTH_CoordTrans`, `clipZ`, 4 `project_point`, `approxDist`) | WALLS.C:2257-2290, 1721-1969 | **secteurs visités × portails** (cache `doorwayCache` invalidé chaque frame par `flags=0`, :2266) | – |
| 4b | `buildTree`, feuilles, `sortLeafList` (2 tris O(n²)), inversion loin→près | :2307-2413 | secteurs² | – |
| 4c | **kick esclave** `0x21000000` ; maître : `drawSector`+`drawSprites` des secteurs **lointains** `i=N-1..slaveDrawStart+1` | :2421-2430 | murs, cellules, sprites | – |
| 4d | `drawSprites` des secteurs esclave, en sous-listes `JUMP_CALL` | :2434-2444 | sprites × chunks (+ `findFloorDistance` par ombre, :2744) | – |
| 5 | `movePlayer` : un sous-pas **par vblank écoulé** (`inputQ`, :864-1048), chacun → `collideSprite` (SPRITE.C:904-905) | :2137 | framesElapsed | – |
| 6 | boucle **30 Hz** (`monsterMoveCounter-=2`) : `runObjects` = `signalList(SIGNAL_MOVE)` (AICOMMON.C:454) | :2142-2190 | objets actifs × ⌊mmc/2⌋ | – |
| 7 | `drawWallsFinish` : **spin jointure esclave**, ajuste `slaveSize`, `drawSlaveWalls` (émission des résultats esclave), `updateLights` | WALLS.C:2448-2482 | cellules esclave | **esclave SH-2** |
| 8 | `advanceWallAnimations`/`stepWater` (30 Hz), push blocks, moves différés, `drawMap` si `mapOn` | :2196-2205 | const | – |
| 9 | `runWeapon` (1 `EZ_distSpr` par chunk, WEAPON.C:955) | :2207 | const | – |
| 10 | `drawMessage`/`drawStatBar`/`drawAirMeter` (9 sites d'émission, :1206-1500) | :2211-2214 | const | – |
| 11 | overlay STATUSTEXT : 6 `drawStringf` + (non-NDEBUG) 3 + 12 `drawStringf` `vswaps/used` ; chaque `drawStringf` = **sprintf** (PRINT.H:17-20) + 1 commande VDP1 **par glyphe** (PRINT.C:138-143) | :2218-2263 | const | – |
| 12 | **`lastCalc=htimer`** | :2249 | | |
| 13 | `sound_nextFrame` (compteur seul, SOUND.C:189-197), `pic_nextFrame` (non-NDEBUG : balayage ~150 slots, PIC.C:171-186) | :2250-2253 | ~0 | – |
| 14 | `EZ_closeCommand` : flush DMA résiduel + liens de fin (SPR.C:423-448) | :2273 | const | DMA |
| 15 | `SPR_WaitDrawEnd` : attend CEF = **tracé de la frame précédente** (lancé au changement de frame, mode `0xfffe` SCL_VBLV.C:100-107 ; aucun `EZ_executeCommand` en jeu, seul MOV.C:449) → `lastDraw` | :2274-2275 | | **VDP1** |
| 16 | gouverneur `smoothVTime` ; `while (vtimer<smoothVTime)` ; **`SCL_DisplayFrame` bloque jusqu'au VBlank-OUT suivant** (`while (ReqDisplayFlag)`, SCL_VBLV.C:69-76, libéré :130-155) | :2277-2293 | | **VBlank ×2** |
| 17 | `framesElapsed=vtimer`, `movePlax`, `SCL_SetWindow`, `updateVDP2Pic` — **hors calc et hors draw** | :2302-2318 | const | – |

Conséquence de 16 : période = max(smoothVTime+1, ⌈travail/16,68 ms⌉) fields — le plancher est
**2 fields = 30 fps**, ce que dit `fps B = 60/(smoothVTime+1)` (:2218). La comptabilité `calc` = 1→11
**inclus overlay et spin esclave** ; `draw` = 14-15.

Émission par cellule (maître) : `pattern`, clip flags, `clip_visible` (WALLS.C:195-210), `mapPic`
(PIC.C:436-451), `EZ_distSprVClip` → `getCmdTable` : buffer de 256 commandes flushé par `dmaMemCpy`
(SPR.C:159-163, 141-148) qui **attend le DMA précédent** (`while (dmaActive())`, DMA.C:84) — une
attente DMA SCU toutes les 256 cellules.

## 2. L'esclave SH-2

| fait | où |
|---|---|
| Lancement : SMPC SSHOFF/SSHON + `SYS_SETSINT(0x94, wallRenderSlaveMain)` | WALLS.C:2111-2125 |
| Boucle esclave : `while(!(*FTCSR&0x80));` (ICF, capture déclenchée par l'écriture maître `0x21000000`, :2421) → `slaveDraw()` → signale le maître par `0x21800000` | :2096-2108 |
| Travail : purge cache, `drawSector(…,slave=1)` pour `i=slaveDrawStart..0` = les secteurs **les plus proches, caméra incluse** (updateList[0] = plus proche, :2412-2413) ; même transformation asm (`rectTransform`/`normTransform`), même clip, mais **aucune émission** : écrit `{gtable, poly[4], tile}` dans `slaveResult` via l'alias cache-through `+0x20000000` | :1981-1993, 1368-1369, 1531-1539 |
| **Budget** : `if (height*width+nmSlavePolys+50>MAXNMSLAVEPOLYS(1300)) return;` → mur **sauté par personne** (trou visuel silencieux) | :1374, 1555 |
| Jointure maître : spin `while(!(*FTCSR&0x80)) i++;` puis `slaveSize±1` sur seuil `i=100` (~400 cycles ≈ 14 µs), clamp ≤ N−1, ≤ 50 ; l'esclave rend en secteurs, pas en cellules | :2452-2459, 2417 |
| Puis `drawSlaveWalls` : purge cache, par résultat `assert(getPicClass)` + `mapPic` + `EZ_specialDistSpr` (copie 32 o + gouraud) | :1997-2094 |

Qui attend qui : le maître lance l'esclave **avant** ses propres secteurs, ses sprites et tout le tic
(`Motion`) ; il n'attend en :2452 que si la part esclave dépasse `drawSector(loin)+drawSprites+Motion`.
L'esclave, lui, dort le reste de la frame (:2103). **Le spin est compté dans `calc`** (avant :2249)
**et dans le nœud profil « Walls »** (SRUINS.C:2192-2194, fusionné avec le « Walls » de :2128 — même
littéral, même nœud). Rien ne chronomètre le côté esclave.

## 3. PROFILE.C / PROFILE.H

| propriété | valeur |
|---|---|
| Timer | FRT **du maître**, TCR clock φ/32 (PROFILE.C:15-17) → 894,9 kHz, **1 tick = 1,117 µs**, 16 bits → **wrap 73 ms** (deltas `&0xffff`, :77,:95 ; tout intervalle > 73 ms est aliasé) |
| Structure | arbre 60 nœuds × 8 enfants (:34-45), clé = (parent, **pointeur** du littéral `id`, :73-75) ; push/pop ≈ 2 lectures + recherche linéaire ≈ 1 µs |
| Activation | `NPROFILE` **n'existe nulle part** sauf PROFILE.C:1/H:4 → profileur compilé et actif dans **tous** les builds, NDEBUG compris |
| Sortie | `dumpProfileData` → `debugPrint` = **no-op hors PSYQ** (UTIL.H:145-152), déclenché par ACTION_PUSH (SRUINS.C:995) ; nouveau : `drawProfileData` sous STATUSTEXT, bascule L+R+Y |
| Partage FRT | `*FTCSR=0` (WALLS.C:2455) efface aussi OVF ; inoffensif pour `getTimer` (compteur FRC :27-32) |

Sondes existantes : `Walls` (SRUINS.C:2128, 2192) ⊃ `Find Visible` (WALLS.C:2260-2415) ⊃ `Find
Doorways` (:2277-2291) ; `Walls` ⊃ `2nd Half` (:1070-1189 = boucle cellules de `drawRectWall`
**seulement**, pas `drawWall` ni la transfo) ; `Motion` (SRUINS.C:2136-2191) ⊃ `Run Objects` (:2149)
⊃ `Collide Sprite` (SPRITE.C:577-771) ⊃ `Walls` (:433 `bumpWalls`), `SectorBndry` (:410), `Sprite`
(:614). `Collide Sprite` apparaît aussi directement sous `Motion` via `moveCamera`.

## 4. Les asserts

586 sites (`grep assert\(` racine) : AI.C 157, **WALLS.C 102**, PIC.C 34, SPR.C 8 + `validPtr`
(UTIL.H:82-83 : 4 comparaisons). NDEBUG retire aussi `checkStack` (UTIL.H:110-115), `drawDebugLines`
(WALLS.C:2461), les vérifs d'arbre (:2340-2347), le balayage `pic_nextFrame`, et **15 `drawStringf`**
d'overlay (SRUINS.C:2222-2231, 2254-2263).

| boucle | sites | coût estimé |
|---|---|---|
| **par cellule maître** | :1141 ; :1171 `getPicClass` (**appel**, PIC.C:190) ; `mapPic` 5 asserts (PIC.C:438-449) ; 4 `validPtr` (SPR.C:278,169,197,204) | ≈ 80-120 cycles ≈ **3-4 µs des 39** |
| par cellule esclave / émission | :1540 ; :2083 `getPicClass` + `mapPic` | ≈ 2-3 µs |
| par **mur** (avant backface, tous les murs des secteurs visibles) | `drawSector` :1605-1615 (2 + 3 `abs`) ; `drawRectWall` :1019-1020 ; `drawWall` :1207-1226 ; `clipZ` :247-279 | ≈ 0,3 µs × 300-800 murs ≈ 0,1-0,2 ms |
| constante | 15 sprintf+drawString ≈ 0,5-0,8 ms ; `pic_nextFrame` ≈ 0,02 ms | ≈ **0,6-0,9 ms** |

Prédiction NDEBUG seul : pente 39 → **~35 µs**, constante 14,9 → **~14 ms**. Sans STATUSTEXT
s'ajoutent 6 sprintf (~0,3 ms) et surtout l'**IRQ HBlank-IN à 15,7 kHz** (V_BLANK.C:238-240, :41-43) :
dispatch SBL + handler ≈ 60-100 cycles × 15 734/s ≈ **4-6 % du CPU ≈ 2-3 ms sur une frame de 47 ms** —
proportionnel au temps, donc lu comme une constante. Hypothèse à trancher par A/B, la sonde FRT
ci-dessous n'en dépend pas.

## 5. Instrumentation proposée (10 étages, µs, aucune dépendance HBlank)

Lecture µs : `getTimer()` (PROFILE.C:27-32, non-static, à déclarer dans PROFILE.H) ; tenths de ms =
`ticks*2/179` (:144). Optionnel : TCR `|3` (φ/128, :17) → 4,5 µs/tick, wrap 293 ms, plus sûr sur les
frames lentes. Mécanique : `static unsigned int st[10]; static unsigned short t0;`
`#define STG(n) (st[n]+=(getTimer()-t0)&0xffff, t0=getTimer())` — une lecture par frontière.

| n | étage | début → fin | attendu (700 cellules, calc 47 ms) |
|---|---|---|---|
| 0 | Doorways | WALLS.C:2278 → :2290 (existe : `Find Doorways`) | 2-4 ms (100-200 portails × 15-25 µs) |
| 1 | Tree+Sort | :2307 → :2413 | 0,3-1 ms |
| 2 | Master walls (transfo+clip+émission) | `STG` autour de :2428, cumulé | ≈ nmPolys × 39 µs = 10-20 ms (la pente) |
| 3 | Sprites | autour de :2429 et :2436 | 0,5-3 ms |
| 4 | Player | SRUINS.C:2137 → :2138 | 0,5-1,5 ms |
| 5 | Run Objects | :2150 (existe) | 1-4 ms **par tic**, 1-2 tics/frame → jitter ±2-3 ms (les résidus ±2,2 ms) |
| 6 | **Join** (spin esclave) | WALLS.C:2451 → :2455, + afficher `i`, `slaveSize`, `updateListSize` | **0-10 ms** — candidat n°1 des +24 ms |
| 7 | Slave emit | :2460 | nmSlavePolys × 8-12 µs ≈ 3-5 ms |
| 8 | Weapon+HUD | SRUINS.C:2207 → :2214 | 0,3-0,6 ms |
| 9 | Overlay | :2217 → :2242 | 0,8-1,5 ms |
| + | VDP1 wait / VB wait / Plax | :2273→2275, :2290→2293, :2308→2318 | hors calc, contrôle |

Affichage (2 lignes, ≤35 caractères, sous `mem:` à y=−30/−20, l'arbre profil étant à −28 → le
décaler à −8) : `drawStringf(-158,-30,1,"dw%d tr%d mw%d sp%d pl%d",…)` et
`"ro%d jn%d se%d hu%d ov%d"` en dixièmes de ms, `st[]` remis à zéro après. Somme des candidats
« constante » : 8-29 ms — encadre 14,9 ms et l'anomalie ; par cellule : `st[2]/nmPolys` et
`st[7]/nmSlavePolys` donnent directement les deux pentes séparées, chose que la régression sur
`polys` (maître+esclave confondus, :2233) ne peut pas faire.

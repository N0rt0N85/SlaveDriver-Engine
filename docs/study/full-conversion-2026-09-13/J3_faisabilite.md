# J3 — Faisabilité et économie : arithmétique refaite contre le code (2026-09-13)

Juge adverse. Tout ce qui suit a été relu dans le code ce jour : `[src] FICHIER:LIGNE` ; `[mesuré]` = calculé aujourd'hui (`scratchpad/reports/j3_check.py`, `awk` sur les `.map`, `tools/study/wadcache.py` exécuté) ; `[doc]` ; `[est]`. Aucun fichier de dépôt modifié.

## 1. Notes (sur 10)

| critère | D1 fidélité | D2 pipeline | D3 risques |
|---|---|---|---|
| Fidélité Doom | **7** — playsim intact, 8 facings = `getFacingAngle` `[src] AICOMMON.C:72-100` ; mais promet « 30 fps verrouillés » sur une loi de coût qu'il n'applique pas (§2.1) | 6 — même ossature, moins détaillé sur la synchro (`side_t` en file de sales absente) | 7 — file de sales des `side_t` (seuls écrivains p_switch/p_spec) = la synchro la plus juste des trois |
| Fidélité Duke | 3 — délégué à R4 sans plan | 4 — produits `GAME.BC`/`.BLD` nommés, rien sur le CPU de `parse()` | **6** — D0 clean-room + harnais différentiel + D1 headless = la seule voie qui mesure avant d'écrire |
| Complétude vs « TOUT » | 8 — 40 lignes, DEH/démos/split inclus | **8** — le disque et les 6 écrivains ; DEH avant sous-ensembles (juste) | 7 — DEH et split déclarés hors plan |
| Faisabilité RAM/CPU | **4** — E1M1 « tient à ~50 Ko près » avec les tuiles E4.1c : faux de −260 Ko (§2.2) ; framebuffer 71 680 o non budgété | 6 — sélecteur 0x32/0x72 par l'outil = le bon levier, mais « 100-350 µs/tic » ignore le tic lui-même | 6 — B plancher / C déverrouillage = lecture correcte de R5 ; `PICFLAG_LOCKED` en conflit avec MIPMAP (§2.6) |
| Économie des mécanismes | 5 — 31→40 slots proposé avant mesure ; `SCL_224LINE` gratuit (vrai, `[src] sega_scl.h:350`) | **8** — 6e fichier `.LNK`, zéro seek, aucune constante relevée avant échec de l'outil | 6 — « userClip élargi », recalcul de normales : mécanismes posés avant l'étage mesuré |
| Testabilité console | 6 — J1 statique ne stresse pas le cache 31 (§2.5) | 5 — J1 à 7 j, statique aussi | **8** — J0 à 4 j, 4 instruments existants, kill criteria chiffrés ; mais sprites donneur = cache non stressé |
| **Total /60** | 33 | 37 | **40** |

## 2. Réfutations (vérifiées dans le code)

**2.1 « E1M1 ≈ 14,9 ms + 39,2 µs × ~140 cellules ≈ 20 ms » (D1 §1.3) — non étayé, et probablement faux ×2.** Le `.LEV` E1M1 du jour porte **8 245 cellules de murs + 3 240 faces de sols/plafonds pour 236 secteurs** `[mesuré] build/doom2ps/e1m1_geom3d.json stats` = 35 cellules/secteur, contre 8 342 / 440 = 19 pour le Duke E1L1 qui a donné **190-824 `polys` par frame** `[doc] STEXT_BASELINE §2`. Rien ne permet « 140 » ; l'ordre de grandeur attendu est 200-900 cellules → calc **22-50 ms** avant tic, sprites et 2D. Corrigé (§5) : 30 fps en couloir, 20 fps médian, 15 fps en arène, en build assert.

**2.2 « E1M1 tient sans cartouche à ~50 Ko près » (D1 §1.8) avec « .LEV E4.1c tel quel ».** Le `.LEV` du jour a **141 tuiles** `[mesuré] e1m1_tiles.json` = 577 536 o en 0x32, toutes déversées en LWRAM puisque la géométrie (237 932 o `[mesuré]` en-tête `.LEV`) + palettes remplissent déjà la HWRAM restante. LWRAM E4.1c : 578 + 270 (sprites RLE, R5) + 200 (zone) + 137 (`.DAT` + `UI.DAT`) + 10 + 18 + 70 (framebuffer) = **1 283 Ko > 1 024 : −260 Ko**. Ce n'est qu'à **E4.1b (~70 tuiles, une par texture)** que E1M1 rentre : 280 + 270 + 200 + 137 + 98 = 985 Ko, **+39 Ko** (+139 avec miroirs A2A8). La première décision du projet est E4.1b/E4.1c (290 Ko), pas « NBG0 vs NBG1 ».

**2.3 « Le coût du tic est l'inconnue n° 1, non mesurée sur E1M1 » (D1 §1.3, §5).** Mimas l'a mesuré sur console : **T 3-18 ms par frame en shareware** `[doc] Mimas/docs/RESOURCE_BUDGETS.md:38` où `T = sat_tic_ms` = durée de `TryRunTics` par frame `[src] core/d_main.c:724,758` — avec `mobj_t` en LWRAM, exactement le régime du fork. Le kill « T > 12 ms » de D1 J2 est donc déjà atteint sur les grosses scènes shareware ; il faut le poser à 20 ms/frame ou le remplacer par un palier de vblank.

**2.4 « Pile LIFO 8 niveaux : 8 allocations vivantes max par aire, le bloc zone alloué en premier » (R2 §9, D1 #3, D3 #4).** `mem_nocheck_malloc` est un **bump illimité** : `stackPos=(stackPos+1)&7` boucle silencieusement, seul l'historique de `mem_free` a 8 profondeurs, et `mem_free` n'accepte que le **dernier** bloc d'une aire `[src] UTIL.C:365-406`. `loadLevel` fait 16 `mem_malloc(1,…)` d'affilée sans problème `[src] LEVEL.C:26-67`. La vraie contrainte, qu'aucune proposition ne nomme : **`runLevel` commence par `mem_init()`** `[src] SRUINS.C:1869`, qui ramène les deux aires aux tops posés par `mem_lock()` `[src] INITMAIN.C:602, UTIL.C:356-359`. Une zone Doom allouée après `mem_lock` est **détruite à chaque niveau** alors que Doom exige des `PU_STATIC` persistants (`lumpinfo`, `hu_font`, `players`). La zone doit être prise **avant `mem_lock()`**, dans INITMAIN — c'est un changement de l'init du fork, pas de `runLevel`.

**2.5 « J1 : 30 zombies statiques prouvent le cache 31 slots » (D1 J1, D2 J1).** Une marionnette immobile en frame A occupe 1 tuile par rotation visible ; le risque réel est le **roulement d'animation** : POSS A→D toutes 4 tics × 8 rotations = jusqu'à 32 tuiles distinctes pour **une** famille, + arme 4-6 chunks. `map()` évince la plus ancienne **même utilisée dans la frame** sauf `PICFLAG_LOCKED` `[src] PIC.C:262-272`, et chaque swap décode 4 096 px de RLE + DMA `[src] PIC.C:292-400` (~0,5-1 ms `[est]`). Un J1 statique mesure `drawSprites`, pas le cache. D3 J0 aux sprites donneur (peu de tuiles distinctes) le stresse encore moins.

**2.6 « `PICFLAG_LOCKED` sur l'arme active » (D3 R-5, #16).** `[src] PIC.C:26` : « *mipmaping is incompatible with locked tiles* » ; `MIPMAP 1` est compilé `[src] PIC.C:24, WALLS.C:50` et `createMippedPics` copie les flags (dont LOCKED) et `charNm` déjà mappé `[src] PIC.C:423-433`. Un pic verrouillé est mappé à `addPic` `[src] PIC.C:416-417` et occupe son slot **en permanence** : 6 chunks verrouillés = 25 slots restants. Mécanisme à requalifier avant usage.

**2.7 « `open_doors()` doom3d.py:479-512 » (R3 #5, D1 #7).** La fonction existe mais à `[src] tools/doom2ps/doom3d.py:589-618` ; les lignes 476-515 sont l'émission des ouvertures fermées. Les numéros de ligne de R3 sur ce fichier (modifié 18:33 ce jour) sont périmés — à ne pas recopier dans un plan.

**2.8 « MAXNMSECTORS non vérifié au chargement → corruption silencieuse » (R3 §5, D1 #32).** Il **est** asserté : `assert(level_nmSectors<=MAXNMSECTORS)` `[src] SRUINS.C:1982-1983` — mais après `initWallRenderer()/initMap()` `:1978-1979`, D2 a raison sur ce point.

**2.9 « validPtr UTIL.H:74 »** (R4, D3, HW_USAGE) : la macro est à `[src] UTIL.H:83`. Détail, mais un plan qui cite une ligne pour « 1 ligne à changer » doit citer la bonne.

**2.10 « Core non-renderer à ajouter = 341 269 o » (R5 §2, repris D1 §1.1).** Somme par objet sur `build/Mimas.map` (core/ hors `r_*.o`, `i_scale.o`) : **336 234 o** (text 177 908, rodata 69 638, data 59 364, bss 29 324) `[mesuré]` = le chiffre de R6. Écart 5 Ko, sans conséquence — mais D1 omet le **framebuffer `I_VideoBuffer` 320×224 = 71 680 o** (plateforme, pas core) qu'il place en HWRAM `[D1 §1.4]` : HWRAM libre après échange ≈ 273 Ko (image 600 184 − 184 000 + 336 234 = 752 418 sur 1 032 192) → 343 Ko avec les tables en LWRAM → **≈ 240 Ko** avec le framebuffer, `mystack` 32 Ko (+12) et `sprites[]` 650 (+14). La géométrie E1M1 (238 Ko) y tient tout juste ; celle d'E1M6 (380-607 Ko `[est R5]`) non.

**2.11 « Synchro ≈ 100-350 µs/tic = < 1,2 % d'un tic » (D2).** Le pourcentage est calculé contre 28,6 ms de **période**, pas contre le coût du tic (2-10 ms `[est]` depuis T 3-18 ms/frame). Vrai en absolu, trompeur en relatif ; et les `moveSpriteTo` sont O(n) sur la liste du secteur `[src] SPRITE.C:916-938` — négligeable seulement parce qu'un secteur `.LEV` porte peu de sprites.

**2.12 « `SoundRec` sans limite par son ».** `size` est un `unsigned short` `[src] SOUND.C:30-36` : ≤ 65 535 o par son. Doom passe (max DSBAREXP 18 600 `[mesuré]`) ; Duke `BONUS.VOC` 268 Ko (R4) est **impossible**, pas seulement « coupé ». Et `initSound` remet `soundTop=0` par niveau `[src] SOUND.C:164` : les ~470 Ko de SFX Doom sont **re-pokés mot par mot** `[src] SOUND.C:221-224` à chaque carte (~240 k `POKE_W`, ~0,3-1 s `[est]`).

**2.13 « HEAP_SIZE Mimas 4 Ko » (R6 §10, D1 #3, D3 #4).** Aujourd'hui `HEAP_SIZE (1536)` `[src] Mimas/src/syscalls.c:104` (r39). Le shim du fork échoue toujours `[src] shim/syscalls.c:9-14` : le remplacement est obligatoire, la taille à re-mesurer (`hp`).

## 3. Confirmations dont le plan dépend

| affirmation | preuve |
|---|---|
| HWRAM libre 432 008 o ; `.bss` 305 768 ; `_end` 0x06096878 | `[mesuré]` MAIN.map:466,2207,2636 |
| Loi de coût 14,9 ms + 39,2 µs/cellule, build **assert**, sprites hors `polys`, STATUSTEXT compté dans `calc` ; paliers 470/896/1321 cellules | `[doc]` STEXT_BASELINE §1-3.3 |
| Budget esclave : mur **sauté en silence** si `height*width+nmSlavePolys+50>1300` | `[src]` WALLS.C:1336,1374,1555 |
| `drawSprites` : `drawList[100]`/secteur, `light=0` forcé, 8 bpp → `light<<8` = banque CRAM, ombre pour tout sprite sans `NOSHADOW` (owner ou non) | `[src]` WALLS.C:2572-2594, 2708-2712, 2736, 2836-2840 |
| Sprites clippés à la bbox écran de **leur** secteur : `EZ_userClip` par secteur lointain avant `drawSector/drawSprites`, et par secteur esclave dans `drawSlaveWalls` | `[src]` WALLS.C:2422-2429, 2012, 2075 |
| `getFacingAngle` 0..7, bornes ±23°, ±68°… = rotations Doom | `[src]` AICOMMON.C:72-100 |
| Marionnette : `newSprite(owner NULL)`, `moveSpriteTo` sans physique, `Sprite` 72 o × 450 | `[src]` SPRITE.C:12,68-101,916-938 ; SPRITE.H:43-59 |
| `MAXNMPICS` 1600 sous MIPMAP, doublé par `createMippedPics` ⇒ 800 de base | `[src]` PIC.C:28-30,423-433 |
| Entrée + `vtimer++` à `UsrVblankEnd` ; `processInput` s'arrête au 1er pad | `[src]` V_BLANK.C:45-92,151-154 |
| Arme = `EZ_normSpr` COLOR_4 par chunk à `(xo−160+chunkx, yo−120+chunky)`, `assert TILE8BPP` | `[src]` SEQUENCE.C:296-305 |
| NBG0 prio 6 **au-dessus** de SP0 prio 4 ; `SCL_224LINE` = 0 existe | `[src]` SRUINS.C:1071-1073 ; sega_scl.h:350 |
| CD : un seul handle (`assert(!openCDFile)`), pas de seek | `[src]` FILE.C:99,155-160 |
| Multi-WAD = `I_Error` (per-lump `wad_file` retiré R4.3c) | `[src]` core/w_wad.c:227-231 |
| Tailles Doom : seg 14, node 28, line 24, side 16, mobj 156 (180 en zone) | `[src]` core/p_setup.c:81-89 |
| `.LEV` E1M1 : 236 sect × 24, 1 878 murs × 48, 13 171 sommets × 8, 3 240 faces × 10 = 237 932 o | `[mesuré]` en-tête ; `[src]` SLEVEL.H:115-124,150-190 |
| PLAYPAL[0] : noirs 0 et 247 ; STBAR 10 240 px sans index 0 ; 55 DS\* = 535 127 o | `[mesuré]` j3_check.py |
| Licences : jfduke3d GPL-2+, jfbuild BUILDLIC, NBlood GPL-2 seule ; fork GPL-3 ; `TICRATE 120/26 = 4` ; `clipmove` engine.c:9131 | `[src]` game.c:7-10 ; jfbuild/LICENSE ; actor.cpp:9 ; duke3d.h:100-101 |
| `wadcache.py` : textures **murs+flats** distinctes à 3 sauts, médiane 15 (E1M4 20, E1M7 19) ≪ 28 — **sans les sprites**, qui sont une autre classe (31 slots) | `[mesuré]` exécuté ce jour |

## 4. Manques — ce qu'aucune proposition ne couvre

1. **`polys`/`calc` d'E1M1 par classe de scène, à coût nul.** Le disque `TOMB_e1m1.LEV` existe et STATUSTEXT affiche `polys`/`time` `[src] SRUINS.C:2232-2237` : trois captures (couloir, salle, arène) donnent le plancher de fps **avant une ligne de Doom**. D1 J0 demande `used/vswaps` à vide, pas `polys`. C'est la première mesure.
2. **Le levier E4.1b/E4.1c (≈ 290 Ko de LWRAM sur E1M1)** n'est posé comme décision que par D2 (#18, sélecteur) ; D1 le prend E4.1c et se trompe de 260 Ko.
3. **`mem_lock()`/`mem_init()` par niveau** (§2.4) : la zone Doom persistante doit précéder `mem_lock` dans INITMAIN — aucun plan ne touche INITMAIN.
4. **Le cache 31 sous roulement d'animation** (§2.5) : J1 doit cycler les frames, pas poser des statues.
5. **Le fill VDP1 des sprites proches.** Un chunk 64×64 zoomé à 200 px = 40 k px ; Mimas psw-world a mesuré **VD1 38 ms en scène ouverte** faute d'occlusion de plans `[mem] psw-world-branch-state`. STEXT §3.5 voit déjà `draw` 8,6-19,8 ms sur des murs seuls. Seul D3 a un kill sur `draw` ; personne ne budgète le fill des 20-40 monstres.
6. **La distribution des assets convertis.** Le `.LEV` embarque sprites/sons/ciel/STBAR **dérivés de l'IWAD** ; la règle Mimas est « jamais d'IWAD » `[mem] doomsrl-licensing`, et le pipeline `/ship` aux testeurs expédierait des données id modifiées. Le convertisseur doit tourner **chez l'utilisateur** (règle déjà appliquée aux CON Duke) — D2 le sous-entend, personne ne l'écrit.
7. **Le coût d'opportunité contre `psw-world`.** Mimas a déjà un peintre tout-VDP1 1p validé console (P90 : couloir 50 fps, spawn 24-33 ms), avec le **même** playsim, la 2D, le son, le WAD et le split `[mem] psw-world-branch-state`. Le fork promet 39 µs/cmd contre 64,5 et l'esclave sur les murs, mais coûte 45-75 j de replomberie. Aucune proposition ne pose « fork vs psw-world » en chiffres — et la règle « budget avant mécanisme » l'exige.
8. **Sons re-pokés par niveau et `size` u16** (§2.12) — à réconcilier avec le bump de Mimas si l'on veut garder l'upload unique.
9. **Framebuffer 71 680 o** : HWRAM (lecture V_DrawPatch rapide, −70 Ko de géométrie) ou LWRAM (2,1× plus lent sur des écritures de 10-70 Ko/frame) — non budgété.

## 5. Synthèse recommandée

**Ossature = D3** (risques ordonnés, B plancher / C déverrouillage, 5 verbes extraits de Doom, Duke gaté par D0/D1 sur PC). **Greffes** : de D2, le `.LNK` 6e fichier + `.PSW` par carte + `disc.py` + sélecteur 0x32/0x72 (économie des mécanismes) ; de D1, `SCL_224LINE`, le blit de lignes sales vers la feuille NBG0 avec LUT 0→247 (`[mesuré]` PLAYPAL), la table des 14 teintes en colour-offset, et la liste des 20 `R_*` du shim. **Retirer** : `PICFLAG_LOCKED` (§2.6), « 31→40 slots », l'automap en `EZ_line` (le tampon 8 bpp suffit, 0 mécanisme).

**Chiffres corrigés `[est]`, build assert, E1M1** :

| scène | cellules | calc (loi) | tic/frame (T Mimas) | sprites + sync + blit | total | palier |
|---|---|---|---|---|---|---|
| couloir | 200 | 22,7 | 3-6 | 1 | 27-30 | **30 fps** (33,4), juste |
| salle, 10 monstres | 450 | 32,5 | 6-10 | 2,5 | 41-45 | **20 fps** |
| arène, 25 monstres | 800 | 46,3 | 10-18 | 4 + swaps 0-10 | 60-78 | **15 fps**, 12 si swaps |

Le tic tourne dans la fenêtre esclave `[src] SRUINS.C:2137-2191` où le maître spinne aujourd'hui (`while (!(*FTCSR & 0x80))` compté dans `calc` `[src] WALLS.C:2452-2455`) : une part du tic est absorbée, d'où la fourchette basse. « 30 fps verrouillés » (D1) est irréaliste ; **20 fps médian, 15-30 selon la scène**, contre 6,6-23 fps pour Mimas M7 1p et 15-50 pour psw-world. Le NDEBUG (jamais mesuré, STEXT §5) peut valoir un palier.

**RAM corrigée** : HWRAM ≈ 240-310 Ko selon le framebuffer ; E1M1 passe **uniquement en E4.1b** (+40..+140 Ko LWRAM) ; E1M6 est mort sans cartouche **deux fois** (606 feuilles, −380 Ko) ; Doom II = cartouche + fusion de feuilles.

**Efforts corrigés** : D1 44,5 j / D3 52 j reposent sur ~3 000 l. de C + ~2 500 l. de Python `[est]` ; à 150-250 l./j **avec** les allers-retours console (psw-world a eu besoin de 90 rounds pour « validé »), **Doom « TOUT » 1p = 65-80 j**, Duke **≥ 90-120 j** au-dessus, avec un plancher CPU probable de 10-15 fps (5-25 ms de `parse()` sur un maître déjà à 22-47 ms). Licences : rien de bloquant côté Doom/jfduke3d ; le point ouvert est la **redistribution des assets convertis**, pas le code.

**Ordre des jalons** : **J0 (5 j, zéro playsim)** = (a) 3 captures STATUSTEXT sur TOMB_e1m1 (`polys`, `time`, `mem:`) ; (b) D3 J0 headless (`P_SetupLevel + P_Ticker` dans le trou, champ `tic`) ; (c) **30 POSS animés** (frames A-D cyclées, 8 rotations, arme 4 chunks) et non statiques → `used[1]`, `vswaps[1]`, `draw` ; (d) `mem:` avec sprites + sons. Kill : `vswaps[1]` > 4/frame à 15 visibles ⇒ 4 rotations ou 32×32 avant tout autre mécanisme ; `mem:` < 100 Ko ⇒ E4.1b obligatoire ; `draw` > 25 ms ⇒ budget de fill sprites. Puis J1 marionnettes (D3), J2 portes fermées + `.LNK` (D2), J3-J4 2D/son/disque (D1 pour le détail des lumps). **Première chose à mesurer : `polys` et `time` sur le disque E1M1 qui existe déjà** — c'est gratuit, et toutes les promesses de fps des trois propositions en dépendent.

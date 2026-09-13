# Convertir un WAD ou un GRP EN ENTIER sur le fork SlaveDriver — analyse, manques, plan (2026-09-13)

Question posée par le propriétaire : *« convertir WADs et Build dans leur ensemble — cartes, menus,
HUD, armes, monstres, logiques, physiques, règles… TOUT. Pour que quand on convertit un WAD, j'aie
l'impression de jouer à Doom, et que seul le graphisme change, car utilisant le moteur de
SlaveDriver. »*

Méthode : 6 lecteurs (Doom `core/`, moteur, convertisseurs et formats, Duke/Build, mémoire, couche
plateforme Mimas), 3 architectes sous trois angles (fidélité Doom, pipeline et disque, Duke et
risques), 3 juges adverses (exactitude contre le code, complétude contre « TOUT », faisabilité et
économie) — 12 agents, ≈ 530 lectures de fichiers. Les douze rapports sont archivés dans
`docs/study/full-conversion-2026-09-13/`, les scripts de mesure dans `tools/study/fullconv/`.
Ce document est la synthèse : il ne garde que ce que les juges ont confirmé, et corrige ce qu'ils ont
réfuté. Étiquettes : `[src] FICHIER:LIGNE` lu dans le code ce jour ; `[mesuré]` calculé ce jour
(script nommé) ; `[HW]` mesuré sur console ; `[doc]` ; `[est]` estimation.

Prolonge `DOOM_ON_SLAVEDRIVER.md` (2026-09-12), dont les jalons 1 et 2 sont **faits** (E1M1 se
parcourt sur console, murs texturés E4.1c). Ce qui suit est ce qu'il reste pour « TOUT ».

---

## 0. Réponse courte

1. **Oui pour Doom, avec l'architecture déjà retenue** : le playsim de Doom tourne **intact** sur
   ses propres structures de carte ; un modèle de rendu `.LEV` est dérivé hors ligne ; une passe de
   synchro par tic recopie ce qui a bougé ; le moteur dessine. Le renderer représente 24,6 % de Doom
   `[mesuré]` ; **18 sous-systèmes restent intacts**, 4 sont à re-câbler (`D_Display`, cast de la
   finale, palette, 3 résolutions de noms), 4 à réécrire (couche 2D, wipe, tracé automap, tracé de
   l'arme) — voir §2.
2. **Le moteur est un hôte propre** : la boucle de frame a déjà un « trou logique » où la logique
   tourne pendant la fenêtre esclave `[src] SRUINS.C:2137-2191` ; un `Sprite` se pilote en
   marionnette par `moveSpriteTo` + 6 champs `[src] SPRITE.C:916-938` ; les **8 rotations de Doom
   = une séquence par vue** (facing calculé par l'hôte avec la formule Doom ; `getFacingAngle`
   `[src] AICOMMON.C:72-100` n'est pas appelé pour une marionnette sans `owner`) ; la feuille NBG0 est un bitmap 8 bpp
   au-dessus du monde, prêt pour le 2D de Doom sans une ligne de `v_video.c` modifiée
   `[src] SRUINS.C:1071-1073` ; le FOV est **90° exactement** (`FOCALDIST 160` `[src] WALLS.H:4`).
3. **Deux murs, chiffrés.** *RAM* : les deux mondes ne rentrent qu'en **régime B** (tuiles murs
   E4.1b, une par texture ; tables Doom en LWRAM ; `objects[]` retiré) : E1M1 n'y tient qu'à ±50-100 Ko
   près, en tirant deux leviers de plus (4 rotations ou tables en HWRAM, zone mesurée) — B n'est pas
   acquis, C (cartouche) est le plan confortable ; **E1M6 et tout Doom II exigent la cartouche 4 Mo
   ET la fusion de feuilles** (§5.1). *CPU* :
   la loi mesurée `14,9 ms + 39,2 µs/cellule` `[HW]` appliquée au `.LEV` d'E1M1 (35 cellules par
   secteur `[mesuré]`) donne **30 fps en couloir, 20 en salle, 15 en arène**, tic Doom inclus — pas
   « 30 fps verrouillés » (§5.2).
4. **Ce qui ne sera pas Doom** : éclairage en 6 paliers CRAM (pas 32 colormaps ni atténuation par
   pixel), flashs de palette en teinte uniforme (colour offset), inversion d'invulnérabilité
   irreproductible sur les murs, spectre = silhouette sombre, scrollers immobiles, melt remplacé par
   un fondu, sauvegarde différentielle (pas le format vanilla en 32 Ko de BUP), murs à demi-résolution
   (une tuile 64×64 par texture), cadence 15-30 fps.
5. **Duke est une autre classe de problème.** Le jeu Duke (jfduke3d, GPL-2+) fait **1 302 appels** à
   l'API du moteur Build et **≈ 3 950 accès directs** à ses tableaux `[mesuré]` — Doom touche son
   renderer par 20 fonctions et 75 sites `[mesuré]`. La physique (`clipmove`, `hitscan`, `cansee`…)
   est **BUILDLIC**, non intégrable dans un dépôt GPL-3 : **≈ 1 900 lignes à réécrire clean-room** avec
   un harnais différentiel sur PC. Le CON s'interprète par acteur et par tic (5-25 ms `[est]`), le 2D
   passe par `rotatesprite` (≈ 700 sites). Miroirs, caméras, métro, panning : impossibles ou dégradés.
   **90-120 j au-dessus de Doom**, plancher CPU probable 10-15 fps.
6. **Effort Doom « TOUT » 1p : 70-90 j** `[est]`, en huit jalons dont le premier coûte 0,5 j et
   tranche les deux murs à coût nul (§6). Duke démarre en parallèle par ce qui ne dépend pas du
   moteur (clean-room + harnais sur PC).
7. **Réserve, posée avant d'arbitrer** : Mimas `psw-world` a déjà un peintre tout-VDP1 validé
   console (P90 : couloir 50 fps, spawn 24-33 ms) avec le **même** playsim, la 2D, le son, le WAD et
   le split. Le fork promet 39 µs/cmd contre 64,5 et l'esclave sur les murs, au prix de 70-90 j de
   replomberie et d'un renderer étranger au projet. Ce document chiffre, il ne tranche pas cette
   question-là ; le jalon J0 (gratuit) donne les `polys`/`calc` d'E1M1 qui la trancheront.

---

## 1. Ce qui est établi (et ce que les juges ont corrigé)

### 1.1 La coupure Doom, mesurée sur `Mimas/core` `[mesuré]` (72 594 lignes)

| bloc | lignes | part | sort |
|---|---|---|---|
| renderer `r_*.c` (dont `r_parallel` 3 750) | 17 877 | 24,6 % | remplacé par SlaveDriver ; **≈ 1 200 l. de `r_main/r_data/r_sky/r_things` restent** (registres de noms, BSP, angles, `sprites[]`) |
| playsim `p_*.c` | 16 522 | 22,8 % | intact |
| UI 2D (`m_menu hu_* st_* wi_ f_* am_map`) | 9 601 | 13,2 % | intact : tout dessine dans **un** buffer 8 bpp `I_VideoBuffer` 320×224 `[src] i_video.h:27-28, v_video.c:657-661` |
| boucle/jeu/sauvegarde (`d_main d_loop d_net g_game p_saveg`) | 7 789 | 10,7 % | intact sauf `D_Display` |
| tables `info.c` (138 sprites, 967 états, 137 mobjtypes) | 4 662 | 6,4 % | intact = le contrat de données |
| son | 2 360 | 3,3 % | intact ; backend = celui de Mimas (§2.6) |

Le non-rendu appelle **20 fonctions `R_*` sur 75 sites** `[mesuré]` : 2 pures (`R_PointInSubsector`,
`R_PointToAngle2`), 4 registres de noms (`R_TextureNumForName` ×18, `R_FlatNumForName` ×5, `Check*`),
11 de viewport (no-op), 3 de rendu (`R_RenderPlayerView`, `R_DrawPlayerSprites`, split). Trois tables
du renderer sont lues par le **playsim** : `textureheight[]` (`[src] p_floor.c:377-386`),
`texturetranslation[]/flattranslation[]` (écrites par `p_spec.c:1112-1136`). C'est toute la frontière.

Ce que le playsim **exporte** chaque tic (R1 §2) : par mobj `x,y,z,angle,sprite,frame` (bit 15 =
`FF_FULLBRIGHT`), `flags` (`MF_SHADOW`, `MF_TRANSLATION`, `MF_NOSECTOR`), `subsector` ; par secteur
`floorheight, ceilingheight, floorpic, ceilingpic, lightlevel` ; par sidedef `top/mid/bottomtexture`
(interrupteurs `[src] p_switch.c:223-247`) et `textureoffset` (scroller 48) ; `viewz`, `viewangle`,
`fixedcolormap`, `extralight`, `psprites[2].{state,sx,sy}`, l'indice PLAYPAL 0-13
(`[src] st_stuff.c:955-1033`), et l'écran 2D complet. **La rotation 0..7 est choisie par le rendu**
(`[src] r_things.c:864-878`) ; **la taille d'un sprite n'est jamais exportée** (elle vient des
lumps).

### 1.2 Le moteur comme hôte — faits vérifiés `[src]`

| brique | fait | référence |
|---|---|---|
| Boucle | `runLevel` : init 1847-2063, boucle 2065-2347 ; trou logique 2137-2191 (`movePlayer/runObjects` pendant la fenêtre esclave) ; monde mobile 2197-2201 (**seul endroit où la géométrie s'écrit**) ; `drawWallsFinish` spin FRT ; gouverneur 2277-2300 (60/30 Hz, hystérésis 10) | `SRUINS.C`, `WALLS.C:2448-2459` |
| Horloge | `vtimer++` à vblank-OUT, `framesElapsed` plafonné 8 ; pas de pas fixe global | `V_BLANK.C:151-154`, `SRUINS.C:2102` |
| Entrée | `processInput` : 1 échantillon/vblank, **`break` au 1er pad**, `inputQ[16]` ; **A+B+C+Start = reset console** (`abcResetEnable`) ; multitap déjà armé à 6/port | `V_BLANK.C:45-91, 79-83, 234` |
| Marionnette | `Sprite` 72 o × `MAXNMSPRITES 450` ; `newSprite(owner NULL)` ; **rend NULL en silence** si plus de libre (assert commenté) ; `moveSpriteTo` = délier/relier O(n) + `pos` ; `scale` par défaut **48 000** ; pieds à `pos.y − radius` ; projection `width64 = (scale·160/z)>>10` ⇒ **1 texel = 1 unité seulement à `scale = 65 536`** ; sprite non dessiné si `z < F(32)` | `SPRITE.H:43-59`, `SPRITE.C:12, 68-101, 71-75, 86, 916-938`, `WALLS.C:2698, 2701, 2716-2723` |
| Rotations | une vue par séquence ; `setSequence` = base + `getFacingAngle` (0..7, bords à ±23°) n'est déclenché que par `SIGNAL_VIEW` sur un sprite à `owner` ⇒ pour une marionnette (`owner NULL`) **l'hôte calcule le facing** avec la formule Doom (22,5°) ; miroir = flag bit 0 | `AICOMMON.C:72-100, 146-158`, `SEQUENCE.C:299-302` |
| Lumière sprite | `light = 0` forcé ; `FLASH` → banque 5 ; banque CRAM = `light<<8` dans le mot couleur ; `setSectorBrightness(s, level)` écrit déjà `vertexLight`/`vertex.light` | `WALLS.C:2705-2712, 2840`, `AICOMMON.C:50-70` |
| Caches VDP1 | slots `{28, 31, 1, 10, 12}` ; **une tuile utilisée dans la frame est évictible** sauf `PICFLAG_LOCKED` ; **`PICFLAG_LOCKED` est incompatible avec les mipmaps** compilées (`MIPMAP 1`, `createMippedPics` copie les flags) et occupe son slot en permanence ; `MAXNMPICS 1600 ÷ 2` (mips) = **800 pics de base** | `SRUINS.C:1903`, `PIC.C:24-30, 47, 262-272, 423-433` |
| Tuiles | 0x32 = 4 096 o d'indices en **HWRAM** ; 0x6A/0x6C/0x72 = RLE en **LWRAM** ; `tileBase` s'ajoute à tout **octet** de texture et à `sFaceType.tile` (**u8**) ; l'indice de tuile d'un chunk de sprite est un **short** | `PIC.C:513, 547, 568-580, 603-610`, `LEVEL.C:69-72`, `SLEVEL.H:120-123, 221-226` |
| Palettes | banque 0 objets (entrée 255 = 0xffff), 1-4 = −i/canal, 5 = flash blanc, 6 = arme VDP2, 7 = ciel → **8/8** | `PIC.C:629-659`, `SEQUENCE.C:269-286`, `PLAX.C:84-87` |
| VDP2 | A0 = table K ciel (banc dédié coefficient), A1 = ciel 512×256, **B0+B1 = feuille NBG0 512×512 8 bpp** (0 o libre) ; **NBG0 prio 6 AU-DESSUS de SP0 prio 4** ; la barre d'état (mot couleur `0x4000`) et l'arme « overlay » passent en SP2 = 7, au-dessus de NBG0 ; `SCL_SetColOffset(OFFSET_A, SP0\|NBG0\|RBG0)` = teinte plein écran ; `SCL_224LINE` existe | `PLAX.C:97-118`, `SRUINS.C:141-142, 1071-1073, 1085-1093, 1322`, `SEQUENCE.C:252-253`, `sega_scl.h:350` |
| Cadre 3D | clip câblé `XMIN −160, YMIN −110, XMAX 160, YMAX 90` = rangées **10..210 = 200 lignes** en mode 240 ; relu par `drawSprites` (FOOTCLIP), bbox ciel, `MAP.C`, clip arme `0..320×0..210` | `WALLS.C:128-131, 2474-2481, 2851-2854`, `SEQUENCE.C:254-260` |
| Géométrie mobile | le moteur **n'écrit jamais une normale à l'exécution** (le seul bloc est sous `#if 0`) ; `wall->d` n'est relu que par `HITSCAN.C:89` ; sol et appartenance recalculés depuis `v[0]` + `normal` ⇒ translater des sommets en Y est sûr | `SRUINS.C:1952-1961`, `UTIL.C:53-67`, `SPRITE.C:135-138`, `SPRITE.C:866-871` |
| Budget esclave | mur **sauté en silence** si `height*width + nmSlavePolys + 50 > 1300` (cellules **totales**, avant rejet écran) | `WALLS.C:1336, 1374, 1555` |
| Allocateur | 2 aires (LWRAM 0x200000-0x300000 ; HWRAM `&end`-0x6100000), repli automatique d'une aire sur l'autre ; **l'allocation est un anneau** (`stackPos = (stackPos+1)&7`), seul `mem_free` est limité au dernier bloc — **pas de limite de 8 allocations** ; **`runLevel` commence par `mem_init()`** qui remet les deux aires au niveau posé par `mem_lock()` | `UTIL.C:343-353, 356-359, 377, 382-386`, `SRUINS.C:1869, 2415-2422` |
| Son | 68K coupé ; 32 slots pokés par le SH-2 (`SNDBASE 0x05a00000` **caché**) ; `MAXNMSOUNDS 80` total ; `assert(soundTop+size < 512 Ko)` ; **`soundTop = 0` à chaque niveau** ; slots 16/17 = mix CDDA ; **dédup : un one-shot ne part qu'une fois par frame, toutes sources confondues ; un son bouclé est refusé tant que la même source le joue** ; **`SoundRec.size` est un `unsigned short`** ⇒ ≤ 65 535 o par son | `SOUND.C:36-37, 40, 63-65, 148-153, 164, 213, 218, 320-329, 337-340` |
| CD | un seul handle (`assert(!openCDFile)`), aucun `fs_seek`, lecture séquentielle par secteurs, préfetch 500 | `FILE.C:99, 153-163, 194-254` |
| Sauvegarde | `SaveRec` 100 o × 6 slots ; backup interne **32 Ko utiles** | `BUP.C:23, 40-42`, `[doc] HW_BACKUP_BUP.md:275` |
| Mémoire du build | `.text 232 740`, `.data 59 868`, `.bss 305 768`, `_end 0x06096878` ⇒ **HWRAM libre 432 008 o** (DOOM_ON_SLAVEDRIVER disait 438 720 sur un build antérieur) | `[mesuré] build/stext/MAIN.map` |
| Sondes | `used/vswaps`, `mem:` n'existent **qu'en build assert + STATUSTEXT** ; la loi 14,9 + 39,2 a été mesurée en build assert (deux `assert()` par cellule) ; la valeur `-NDebug` est inconnue | `SRUINS.C:2240-2262`, `[doc] STEXT_BASELINE §5` |

### 1.3 Ce que les juges ont réfuté (à ne plus recopier)

| affirmation circulante | correction `[src]` |
|---|---|
| « pile LIFO 8 niveaux ⇒ 8 allocations vivantes max » | anneau ; `loadLevel` fait 15 `mem_malloc(1,…)` d'affilée `UTIL.C:377`, `LEVEL.C:42-66` |
| « libérer le niveau précédent par `mem_free` » | `mem_init()` balaie tout ; une zone Doom prise **après `mem_lock()`** disparaît à chaque niveau `SRUINS.C:1869` |
| « NBG0 est sous les sprites » | prio 6 > SP0 4 : tout pixel ≠ 0 de la feuille **masque le monde** `SRUINS.C:1071-1073` |
| « FOV du moteur inconnu » | 90° = Doom `WALLS.H:4` + `EZ_localCoord(160,120)` |
| « cadre 224 lignes = `SCL_224LINE` + `localCoord`, 0,5 j » | + `XMIN/YMIN/XMAX/YMAX`, FOOTCLIP, bbox ciel, clip arme : ~1 j |
| « `newSprite` asserte quand plus de libre » | rend **NULL** ; à traiter `SPRITE.C:71-75` |
| « `MAXNMSECTORS` non vérifié au chargement » | asserté en build assert `SRUINS.C:1982-1983` ; silencieux en NDEBUG |
| « `open_doors()` à `doom3d.py:479-512` », « `light_of` à `:137-149` » | `doom3d.py:589` et `:182` (fichier modifié le 12 à 18:33) |
| « le `-Mus` de Mimas vit sur le driver 68K » | faux : le séquenceur poke les slots 8-22 sans 68K ; `SRL::Sound::Hardware::Initialize` n'est appelé qu'en CDDA `i_sound_saturn.cxx:493-511` |
| « animdefs = 9 flats + 14 textures » | 13 textures `p_spec.c:110-124` |
| « E1M1 : 138 things vivants » | 138 enregistrements THINGS dont 14 multi ; **115 mobjs** au skill 4 en 1p `[mesuré]` |
| « Duke choisit 8 vues par `k = ((ang+3072+128−a)&2047)>>8` » | puis `&7` et `if (k>4) k = 8−k` : **5 vues + miroir** `game.c:5196-5199` |
| « HEAP_SIZE Mimas 4 Ko » | 1 536 o `Mimas/src/syscalls.c:104` |
| « sommets `.LEV` partagés : −70..−90 Ko » | `sVertexType.light` est **par sommet** et `setSectorBrightness` l'écrit **par secteur** ; `normTransform` transforme une plage **contiguë** par mur ⇒ partage seulement via `v[4]` des parallélogrammes, et interdit aux secteurs à lumière animée ; gain non compté ici `SLEVEL.H:115-118`, `AICOMMON.C:63-67`, `WALLS.C:1210` |
| « `validPtr` à `UTIL.H:74` » | `UTIL.H:83` |
| « E1M1 ≈ 140 cellules par frame ⇒ 20 ms » | le `.LEV` d'E1M1 porte **8 245 cellules de murs + 3 240 faces** pour 236 secteurs (35/secteur) `[mesuré] e1m1_geom3d.json` ; Duke E1L1 (19/secteur) donnait 190-824 `polys` `[HW]` ⇒ 200-900 attendus |

---

## 2. Architecture retenue

```
CD ─┬─ E1Mx.LEV   ciel PLAX + géométrie convexe FERMÉE + course + tuiles murs/flats (E4.1b) + sprites RLE 0x6A du niveau
    │              + séquences (sprite, frame, rotation) + sons dynamiques du niveau            → aire 1 (HWRAM) / aire 0 (LWRAM)
    ├─ E1Mx.LNK   table de liaison (6e fichier, ouvert après le .LEV) : feuille→secteur .LEV, secteur Doom→sommets suiveurs,
    │              secteur→plages de lumière, sidedef→cellules, texture/flat→tuile, (sprite,frame)→séquence, sfx→son   → LWRAM ≈ 17 Ko
    ├─ RES.PSW    mini-WAD résident (PLAYPAL, STCFN, ST*, AMMNUM, flats de finale ≈ 90 Ko), chargé une fois
    ├─ E1Mx.PSW   mini-WAD par carte : les 10 lumps de carte sans SEGS
    ├─ TITLE.PSW / MENU.PSW / INTER.PSW   mini-WADs par état (TITLEPIC/HELP/CREDIT ; M_* ; WI*) — chargés à la place du niveau
    ├─ STATIC.DAT Doom : écran de chargement, feuille NBG0 (STBAR), ~30 sons statiques (armes/joueur/portes), psprites 0x6A
    └─ pistes CDDA (13 MUS rendus hors ligne)  — ou séquenceur MUS SH-2 de Mimas (décision owner)
MSH2 ── playsim Doom INTACT (core/ moins r_*.c, + r_shim.c ≈ 1 200 l.) dans UN bloc mem_malloc(0) de 256-512 Ko pris AVANT mem_lock()
     ── sat_sync_world() : écrit level_vertex[].y, octets de tuile des cellules, vertexLight, Sprite[] (marionnettes)
     ── drawWalls / drawWallsFinish / drawSprites du moteur, inchangés (3 patchs de 2-3 lignes)
     ── 2D Doom : I_VideoBuffer 320×224 → lignes sales recopiées dans la feuille NBG0 (LUT 0→247)
SSH2 ── transformation des murs (inchangé)
VDP2 ── RBG0 ciel (PLAYPAL banque 7) · NBG0 = écran 8 bpp de Doom (prio 6 ; HUD, menus, automap AM_Drawer intact) · colour offset = 13 teintes PLAYPAL
VDP1 ── murs RGB16 gouraud · sprites 8 bpp banques CRAM 0-5 · arme = chunks EZ_normSpr
SCSP ── slots 0-7 SFX (backend Mimas en C, key-off/KYONEX) · 16/17 CDDA · 8-22 MUS si séquenceur
BUP  ── 6 slots : sauvegarde DIFFÉRENTIELLE 2-4 Ko (masque THINGS + états secteurs + player_t) ; intégrale = cartouche backup
```

### 2.1 La boucle hôte (`runLevel`, `SRUINS.C:2065-2347` conservé sauf le trou logique)

```c
zone = mem_malloc(0, ZONE);  Z_Init(zone);       /* AVANT mem_lock() — INITMAIN/main, pas runLevel [J1 §2-1] ; ZONE = 256 Ko sans cart, 512 avec (§5.1) */
camera = newSprite(link[ssec(player->mo)], /*radius*/0, 0, 0, /*sequence*/-1, SPRITEFLAG_NOSHADOW, NULL);  /* WALLS.C:2592 exige -1 */
for (;;) {
  view.yaw = angle_t→F(deg); view.pitch = view.roll = 0;
  view.pos = { mo->x>>16, viewz>>16, mo->y>>16 }; camera->s = link[ssec(mo)];
  EZ_openCommand(); sysClip; userClip; EZ_localCoord(160, 120 ou 96);
  drawWalls(&view);                                    /* kick esclave — la géométrie est GELÉE jusqu'à Finish */
  acc += framesElapsed*35; while (acc >= 60) { G_BuildTiccmd(inputQ); G_Ticker(); acc -= 60; }   /* 35 Hz, trou logique */
  S_UpdateSounds(displayplayer);
  drawWallsFinish();                                   /* join FRT — la géométrie redevient écrivable */
  sat_sync_world();                                    /* §2.2 : la SEULE écriture de level_* et de Sprite[] */
  draw_psprites();                                     /* EZ_normSpr par chunk, patron SEQUENCE.C:296-305 */
  D_Display_2D();                                      /* ST/HU/AM/M/WI/F → I_VideoBuffer (inchangés) */
  sound_nextFrame(); pic_nextFrame(NULL, NULL);        /* GARDÉS : horloge LRU du cache (SRUINS.C:2250-2253) */
  EZ_closeCommand(); SPR_WaitDrawEnd();
  gouverneur; SCL_DisplayFrame(); framesElapsed = vtimer; vtimer = 0;
  blit_2d_dirty_rows(); movePlax(viewangle, 0); SCL_SetColOffset(OFFSET_A, SP0|NBG0|RBG0, tint[st_palette]);
  if (gamestate != GS_LEVEL || gameaction) return;    /* WI/finale/menus tournent dans la même boucle, 3D coupée */
}
```

Retiré en bloc : `movePlayer/controlInput`, `runObjects`, `stepPlayerHeight`, powerups NBG0,
`updatePushBlockPositions/processDelayedMoves`, `runWeapon`, `drawStatBar/drawMessage/drawAirMeter`,
`runInventory`, `hitCamel/hitPyramid`, `initObjects/placeObjects` (−50 Ko de `.bss`), `runMap`/MAP.DAT,
MOV/LIP/BONUS. Gardé verbatim : `drawWalls`, `drawWallsFinish`, `drawSprites`, `EZ_*`, `pic_*`,
`SOUND.C` (mix CDDA), `PLAX.C`, gouverneur, `V_BLANK.C`.

**Contraintes du moteur, les seules** : `camera->s` = secteur `.LEV` de l'œil `[src] WALLS.C:2268-2276` ;
la géométrie ne bouge qu'entre `drawWallsFinish` et le `drawWalls` suivant (l'esclave lit
`level_vertex` par l'alias non caché pendant sa fenêtre). Corollaire : l'image montre l'état des tics
de la frame précédente (`drawWalls` part avant les tics), soit +16 ms de latence par rapport à Doom PC —
acceptable, à énoncer.

**Quatre lignes que les trois propositions avaient oubliées** (juges) : `o->scale = 65 536` et
`radius = 0` (ou `pos.y = z + radius`) sur chaque marionnette ; `if (!spr) …` après `newSprite` ;
`abcResetEnable = 0` (A = tir, B = use, C = courir, Start = menu : un joueur peut reset la console) ;
constante `F(32)` de `WALLS.C:2701` abaissée vers 8-10 u (un cadavre sous les pieds disparaît à 32 u,
Doom dessine jusqu'au contact).

### 2.2 La passe de synchro `sat_sync_world()` — champs exacts, coût E1M1 `[est]`

85 secteurs, 648 sidedefs, 115 mobjs vivants (+ transitoires), 4 secteurs à lumière animée, 4 de
flats animés, 8 scrollers, 1 interrupteur `[mesuré]`.

| source playsim (écrivain) | cible moteur via `.LNK` | déclencheur | coût |
|---|---|---|---|
| `sector.floorheight / ceilingheight` (thinkers `p_doors/plats/floor/ceilng`) | `level_vertex[v].y` de chaque sommet suiveur (`vtx_follow` : sommet, secteur, {sol, plafond}, op {=, max(voisin), min(voisin)}) — le geste de `updatePushBlockPositions` `[src] SPRITE.C:852-890` | ombre 2 shorts × 85 ; 0-5 secteurs mobiles/tic × 60-200 sommets | 10 + 10-150 µs |
| `sector.lightlevel` (+ `extralight`, `fixedcolormap`) (`p_lights`, `A_Light*`) | `setSectorBrightness(pièce, LUT256→32)` sur les ~2,8 pièces du secteur `[src] AICOMMON.C:50-70` | ombre × 85 ; strobes | 5-60 µs |
| `sides[].top/mid/bottomtexture` (**seuls écrivains** : `P_ChangeSwitchTexture`, `buttonlist`) → **file de sales**, pas de scan | octet impair de `level_texture[]` des cellules liées (`side2cells`) | à l'événement | ≈ 5 µs/événement |
| `flattranslation[]/texturetranslation[]` (`P_UpdateSpecials`, 8 tics) | octet de tuile des cellules des familles animées (toutes les frames sont des tuiles du `.LEV` ; **ne pas** utiliser `PICFLAG_ANIM` : cadence ≈ 1 frame par 4 pas de `monsterMoveCounter` ≠ 8 tics, 15 sets `[src] PIC.C:110-127`) | `leveltime % 8 == 0` | ≈ 10 µs / 8 tics |
| `sides[].textureoffset` (special 48, +1 px/tic) | **pas d'UV par cellule** (`[doc] LEV_FORMAT §2`) : statique, ou cycle de 8 tuiles pré-décalées | — | dégradé (§9) |
| par `mobj` : `x,y,z,angle,sprite,frame,flags,subsector` | `Sprite.pos` ; `moveSpriteTo` si la feuille change ; `sequence = seq[sprite][frame][rot]` avec **`rot = (R_PointToAngle2(viewx,viewy,x,y) − angle + ANG45/2·9) >> 29`** calculé côté hôte `[src] r_things.c:867-870` ; `flags` : `MF_NOSECTOR`/caméra → `INVISIBLE`, `MF_SHADOW` → `COMPO_SHADOW`, bit 15 → banque 0 ; banque = f(`lightlevel`, `extralight`, `fixedcolormap` : lampe IR = banque 0 partout, inversion = banque inversée) ; `scale = 65 536` | tous ; `rot` seulement si la pièce est `SDFLAG_NEEDTOPROCESS` de la frame précédente `[src] WALLS.C:81-97` | 115-150 × 0,5 µs + 30 mobiles × 3 µs ≈ 150-450 µs |
| spawn / `P_RemoveMobj` | `newSprite` (NULL toléré) / `freeSprite` ; hook 1 ligne `// SATURN:` dans `P_SpawnMobj/P_RemoveMobj` | événement | 2-5 µs |
| `viewz`, `mo->angle`, `psprites[2]`, indice PLAYPAL | caméra ; chunks d'arme à `(sx>>16 −160 + chunkx, sy>>16 −96 + chunky)` ; `SCL_SetColOffset` table de 14 teintes = moyenne PLAYPAL[i] − PLAYPAL[0] `[mesuré]` : rouge 8 = (+101, −88, −77), or 12 = (+37, +43, −9), vert 13 = (−17, +19, −10) | chaque frame | ≈ 15 µs |
| `I_VideoBuffer` | lignes sales → feuille NBG0 stride 512 ; **en jeu** les lignes 0-191 sont copiées telles quelles (0 = transparent : la 3D passe, tout pixel ≠ 0 la masque) et la LUT 0→247 ne s'applique qu'aux lignes 192-223 (STBAR n'a aucun pixel d'index 0 `[mesuré]`) ; **hors `GS_LEVEL`** (menus, automap, intermission) la LUT s'applique partout (PLAYPAL a deux noirs, 0 et 247) | `sat_hud_dirty` ; plein écran hors `GS_LEVEL` | HUD 10 Ko ≈ 0,3 ms ; plein 72 Ko ≈ 1,5-2 ms |

**Total ≈ 0,3-0,8 ms par frame hors 2D** `[est]` — négligeable ; le poste qui compte est le **tic**
(§5.2). ⚠ E1M1 a 0-5 secteurs mobiles par tic ; MAP30 (dizaines de `MT_SPAWNSHOT`), les crushers et
les escaliers massifs sont les extrêmes à mesurer avant de généraliser le chiffre.

### 2.3 L'adaptateur commun Doom / Duke — cinq verbes, extraits de Doom au jalon J2

`host_puppet(id, pos, secteur, séquence, frame, flags, banque)`, `host_heights(jeu_de_sommets, dy)`,
`host_cell_tile(mur, cellule, tuile)`, `host_light(secteur_lev, niveau)`, `host_2d(tampon, rect)`.
Doom les remplit depuis `mobj_t/sector_t/side_t`, Duke depuis `sprite[]/sector[]/wall[]`. Ce qui est
propre à Duke (quads muraux/sol, recalcul de normale) est un **ajout au moteur**, pas à l'adaptateur.
Conçu depuis Duke il serait surdimensionné pour rien ; conçu depuis Doom il est petit et prouvable
complet (20 fonctions / 75 sites contre 1 302 appels).

### 2.4 Le 2D — feuille NBG0 existante (0 VRAM, 0 cycle)

`v_video.c` continue d'écrire `I_VideoBuffer` ; `DG_DrawFrame` copie les lignes sales dans la
feuille 512×512 déjà en B0+B1 `[src] SRUINS.C:1085-1093`, fenêtrée par W0 `[src] PIC.C:456-462`,
palette banque 6 = PLAYPAL. `AM_Drawer` (7 sous-tracés : grille, murs à 4 familles de couleurs,
joueurs, things IDDT, réticule, marques `AMMNUM`) reste **intact** dans ce tampon — la réécriture en
`EZ_line` proposée par deux plans coûterait plus et couvrirait moins. Repli si neige console
(famine de cycles VDP2 = console-only `[doc] HW_VDP2.md`) : NBG1 en B1 après réduction de la feuille à
512×256 (+2 j). La barre d'état peut aussi être un char VDP1 `0x4000` (SP2 = 7) comme aujourd'hui.

### 2.5 Le cadre et l'aspect

Le core Mimas dessine déjà **320×224 = vue 192 + STBAR 32** `[src] i_video.h:27-29` ; le fork affiche
320×240 avec une vue 3D de 200 lignes (rangées 10..210). Passer en `SCL_224LINE` = `localCoord(160,96)`
+ `XMIN/YMIN/XMAX/YMAX` + clip arme + bbox ciel ≈ 1 j. **Aspect** : Doom PC étire 320×200 ×1,2 sur un
4:3 ; la Saturn en 224 lignes étire ×1,07 (240 : ×1,0) ⇒ un monde 1:1 paraît **11-17 % plus plat**
`[est]`. Le remède propre est une focale verticale **×1,12 en 224 lignes** (1,2/1,07 ; ×1,2 seulement en 240) dans `project_point`/`rectTransform`
(une constante d'assembleur, comme la variante FOCALDIST déjà étudiée dans `MULTIPLAYER_STUDY.md`)
plus la hauteur des sprites (commande scaled sprite : largeur et hauteur indépendantes) et la bande
ciel — décision owner §7, 1-2 j, après J3.

### 2.6 Le son — backend Mimas porté en C, pas `playSoundE`

`SOUND.C` ne lance un one-shot qu'**une fois par frame, toutes sources confondues** `[src] SOUND.C:320-329`
(deux zombies qui tirent le même tic = un seul `DSPOSIT`) ; ses 8 voix pré-éteintes tournent **sans
priorité** (`silenceVoice` `[src] SOUND.C:58-76`) alors que Doom priorise (`S_GetChannel`) et coupe/relance
le même son de la même origine. Le backend de Mimas (`i_sound_saturn.cxx:584-647` : DMX
→ SCSP, 8 voix slots 0-7, key-off + attente KYONEX, TL/pan/pitch) poke la SCSP depuis le SH-2 sans 68K
— **même geste que le fork**, même adresse cachée. On garde du fork le mix CDDA (slots 16/17) et on
importe le backend Mimas en C (~300 l.). Conséquence : `soundTop = 0` par niveau `[src] SOUND.C:164`
devient un bump à **deux régions** : les ~30 sons statiques sont pokés une fois puis une marque est
posée ; les sons dynamiques de chaque `.LEV` repartent de la marque (la liste blanche par niveau
reste nécessaire : 535 > 512 Ko) — sans quoi ~470 Ko seraient re-pokés mot par mot à chaque carte
(~0,3-1 s `[est]`).

### 2.7 La sauvegarde — différentielle, 2-4 Ko

Le format vanilla `p_saveg` = 27-30 Ko pour E1M1, 87-101 Ko pour E1M6 `[mesuré]` contre 32 Ko de
backup interne. Et `M_DoSave` n'est déclenché que si le nom du slot, **tapé au clavier**, est non vide
`[src] m_menu.c:635-645, 1594-1596` — aucun plan ne l'avait vu. Retenu : (a) nom automatique
(`HU_TITLE` + heure) ; (b) enregistrement différentiel « recharger la carte puis appliquer » : masque
des THINGS tués/ramassés (58 o pour 463), état des secteurs (250 × 4 o), spéciaux actifs, `player_t`
≈ 2-4 Ko `[est]` ⇒ 6 slots ; (c) `p_saveg` intact (LZSS) **uniquement sur cartouche backup** ;
(d) relinker les thinkers parqués avant `P_ArchiveThinkers` (le code l'exige lui-même
`[src] p_tick.c:162-170`).

---

## 3. Inventaire des manques — Doom

Sévérité : **B** bloquant, **M** majeur, **m** mineur, **c** cosmétique. Coût en jours `[est]`.
Preuves dans les rapports R1-R6, vérifiées J1-J3.

### 3.1 Hôte et playsim

| # | manque | sév. | mécanisme (fichier touché) | j | risque / preuve |
|---|---|---|---|---|---|
| 1 | Boucle hôte : `TryRunTics` dans le trou logique, sorties par `gamestate` | B | réécrire `runLevel` (§2.1) | 2 | un tic > fenêtre esclave allonge la frame |
| 2 | Horloge 35 Hz depuis les vblanks (`I_GetTime = vbl×35/60`, accumulateur) | B | `V_BLANK.C:151-153` | 0,2 | 7 tics / 12 vblanks irréguliers, comme PC 60 Hz |
| 3 | **Zone Doom persistante** : `mem_malloc(0, 256-512 Ko, §5.1)` pris **avant `mem_lock()`** (INITMAIN), `Z_Init` dessus ; `_sbrk` statique de Mimas (le shim du fork échoue toujours `[src] shim/syscalls.c:9-14`) ; pile ≥ 24 Ko (`mystack` 20 384 → 32 Ko) ; règle de build : `core/` en `-std=gnu11 -fsigned-char -DCMAP256 -DSAT_SND_PRECACHE`, newlib complète (`FILE*` pour `p_saveg/m_menu`), `make size` + pré-vol du `.map` à chaque build (le pool part de `end`) | B | `INITMAIN.C`, `shim/syscalls.c`, `UTIL.C:29` | 1 | `mem_init()` par niveau `[src] SRUINS.C:1869` ; `M_StringDuplicate` malloc au boot ⇒ `I_Error` avant la 1re frame |
| 4 | Accès WAD : `w_wad.c` n'accepte **qu'un** fichier (`I_Error` multi-fichier `[src] w_wad.c:230-231`), `FILE.C` sans seek | B | **deux** mini-WADs mappés (chargement in-place `[src] p_setup.c:165-178`) : `RES.PSW` résident (PLAYPAL, STCFN, ST*, AMMNUM, flats de finale ≈ 90 Ko, chargé une fois — les caches `PU_STATIC` de `HU_Init`/`ST_loadGraphics` pointent dedans) + un `.PSW` par état/niveau (`E1Mx.PSW`, `TITLE/MENU/INTER.PSW`) ; réactiver le 2e fichier dans `w_wad.c` (~1 j) ; SEGS absent ⇒ `P_LoadSegs` court-circuité (0 seg ; `subsector_t.firstline` inutile au playsim) ; lumps alignés 4 | 3 | un `W_SwapWad` à fichier unique laisserait pendre les pointeurs `PU_STATIC` du bloc précédent ; `.PSW` E1 = 47-130 Ko `[mesuré]` sans SEGS (jamais lus hors `p_setup.c` `[mesuré]`) |
| 5 | `r_shim.c` : `R_PointInSubsector`, `R_PointToAngle2`, 4 registres nom→tuile, `textureheight[]`, `texturetranslation/flattranslation`, `sprites[]` (cast), `skyflatnum/skytexture`, 11 no-op de viewport, `R_RenderPlayerView/R_DrawPlayerSprites` → pont | B | nouveau fichier ≈ 1 200 l. reprises de `r_main/r_data/r_sky/r_things` ; tables émises par doom2ps dans le `.LNK` ; **`r_parallel.h` de stubs `RP_*`** (inclus par `p_tick/p_mobj/p_sight/d_main/z_zone`, 23 appels `[mesuré]` R6 §9) et `r_defs.h` conservé (structs rétrécies consommées par le playsim) | 2,5 | `textureheight` lu par `EV_DoFloor raiseToTexture` |
| 6 | Table de liaison `.LNK` (format §2, ≈ 17 Ko) + lecteur C | B | `pslib/link_write.py`, `loadLink()` ~120 l. | 2,5 | à figer avant J2 ; `conv.remap` existe déjà pour le départ `[src] doom3d.py:245, 661` |
| 7 | Marionnettes mobj→Sprite (`newSprite/moveSpriteTo/freeSprite`, hooks 1 ligne dans `P_SpawnMobj/P_RemoveMobj`), `scale = 65 536`, `radius = 0` (l'origine Doom est le bas du sprite, `MF_SPAWNCEILING` compris), NULL toléré, `MAXNMSPRITES` 450 → **650** (+14 Ko ; E1M6 439 mobjs + transitoires) | B | `SPRITE.C:12`, hôte | 2 | thing chevauchant deux pièces dessiné dans **une** (`sectorSpriteList[o->s]`) → clip visible ? mesure J0b |
| 8 | Lumière des sprites : `light = o->color & 7` à `WALLS.C:2708` ; banques 1-5 = COLORMAP Doom aux niveaux 6/12/18/24/30 ; fullbright = banque 0 ; pas de terme de distance | M | 3 lignes `WALLS.C` + palettes du convertisseur | 0,5 | 6 paliers au lieu de 32 |
| 9 | Spectre / Blur Sphere (`MF_SHADOW`) → `COMPO_SHADOW` au lieu de `COLOR_4` | m | `WALLS.C:2838` | 0,3 | silhouette sombre ≠ fuzz |
| 10 | Couleurs joueurs `MF_TRANSLATION` (multi) | c | 3 banques CRAM libérables (5 = flash inutilisé par Doom ; 6 et 7 = copies de PLAYPAL si NBG0 et RBG0 pointent sur la banque 0) ⇒ 3 translations en CRAM, 0 tuile de plus | 0,5 | — |
| 11 | Entrée : table pad→touches Mimas `[src] dg_saturn.cxx:17063-17078` dans `processInput` ; ouvrir la boucle `break` par port ; **`abcResetEnable = 0`** ; couche « chord » pour automap follow/grid/marks, gamma, sélection d'arme (`[src] m_controls.c:122-131, 143-145, 170`) ; cheats = page de menu qui poste les lettres (`cht_CheckCheat` intact) | M | `V_BLANK.C:45-91` | 1,2 | — |
| 12 | Cadre 224 lignes + aspect ×1,12 (§2.5) | M | `SRUINS.C:2399`, `WALLS.C:128-131`, `SEQUENCE.C:254-260`, asm | 1 + 1-2 | décision owner |
| 13 | Gouverneurs Mimas (décimation, parqués) OFF par défaut : ils désynchronisent démos et sauvegardes ; ON si T > 20 ms/frame | m | `p_tick.c:92-174` | 0 | — |
| 14 | Split-screen 2-4 p | m | **hors plan** : `drawWalls` lit la globale `camera`, un seul `sectorDraw[]`, un seul kick esclave, W0 unique `[src] WALLS.C:2421, 2428-2429, 2592` ; deux rondes esclave par frame = étude, pas 4 j | — | à dire au public Mimas |

### 3.2 Monde mobile et géométrie

| # | manque | sév. | mécanisme | j | risque / preuve |
|---|---|---|---|---|---|
| 15 | **Portes/ascenseurs** : `open_doors()` (`[src] doom3d.py:589`) ouvre statiquement ; il faut la géométrie **fermée + course** : contremarche `[fh, maxFloor]`, portail `[bot, top]` (hauteur 0 = fenêtre vide), linteau `[minCeil, ch]`, **par rangées de 64 u** (reliquat étiré confiné à la rangée haute), `WALLFLAG_DOORWALL` pour le recalcul `SHORTOPENING` déjà dans le moteur `[src] AI.C:4300-4307` ; sommets liés → `vtx_follow` ; bornes `P_FindLowestCeilingSurrounding − 4` etc. | B | `doom3d.py` (`mobile_bounds`, `emit_edge_mobile`), sync §2.2 | 5 | le budget esclave compte les cellules **totales** ⇒ murs sautés en silence : **compteur à ajouter au `return` de `WALLS.C:1374`** (1 ligne, STATUSTEXT) |
| 16 | Interrupteurs : 2 tuiles + `side2cells` | M | `doomtiles.py` | 1 | 1-5/carte, 13 en MAP19 `[mesuré]` |
| 17 | Flats/textures animés (9 familles de flats + 13 de textures `[src] p_spec.c:96-124`) : toutes les frames en tuiles + runs `tex2tile` | M | `doomtiles --anim` | 2 | Doom II > 15 familles/carte ; cap tuiles §5.1 |
| 18 | Lumière par secteur : export secteur→plages `firstLight` + sommets ; interdit de partager un sommet entre secteurs à lumière animée | M | `link_write.py` | 1,5 | 6 paliers |
| 19 | Midtextures 2 faces (grilles) : mur à faces 0x32, indice 0 transparent, sur le portail ; BLOCKED si `ML_BLOCKING` | M | `emit_midtex` | 2 | ordre peintre par secteur |
| 20 | `xoffset` des sidedefs ignoré (`rowoffset` + DONTPEG traités `[src] doom3d.py:437-482` ; `xoffset` absent du fichier) | m | `uoff` dans la clé de tuile | 1 | — |
| 21 | Scrollers (special 48) | c | statique, ou cycle de 8 tuiles pré-décalées (+32 Ko LWRAM/texture) | 0-1 | dégradé accepté |
| 22 | Ciel par épisode : SKY1 256×128 → 512×256 (×2), PLAYPAL banque 7, table K recopiée (identique dans les `.LEV` mesurés) ; `movePlax` = 128 px/45° = **256 px/90° = Doom** (`ANGLETOSKYSHIFT 22`) | M | `wad2sky.py` | 1 | `F_SKY1` = `WALLFLAG_PARALLAX` (déjà) |
| 23 | Feuilles > 600 : E1M6 **606**, Doom II MAP14 851 / **MAP15 875** / MAP17 615 / MAP19 721 / MAP24 697 / MAP29 751, Ultimate 9 cartes (E4M9 **956**) `[mesuré]` | B (E1M6) | `merge_leaves()` : fusion de feuilles sœurs dont l'union reste convexe ; repli `MAXNMSECTORS` 600 → 900 (+23 Ko : `sectorDraw`, `penetrate[600]` `SPRITE.C:962`, `processed[600]` `OBJECT.C:433`) | 3 | `size < 900 000` `[src] LEVEL.C:41` reste le plafond dur |
| 24 | Sprites coupés aux frontières de pièces convexes ; `F(32)` de clip proche | M | `userClip` élargi du rayon ; constante → 8-10 u | 0,5 | mesure J0b |

### 3.3 Assets, 2D, son, disque

| # | manque | sév. | mécanisme | j | risque / preuve |
|---|---|---|---|---|---|
| 25 | **Sprites par niveau** : S_* → chunks 64×64 (et 32×32 `0x6C` pour les petits) RLE `0x6A` + `sFrame/sChunk` + une séquence par (sprite, frame, rotation), miroirs A2A8 = flag bit 0, sous-ensemble par things présents + chaînes d'états `info.c` (+ PUFF/BLUD/TFOG/IFOG/MISL ; **PLAY exclu en 1p : −74 Ko**) ; carte objet→séquence 227 × (−1) | B | `wad2sprites.py`, `rle8.py` | 4 | E1M1 ≈ 270 Ko, E1M3/E1M6 ≈ 390, E1M8 ≈ 460 Ko RLE `[mesuré]` ; MAP15 1,44 Mo, MAP29 2,05 Mo (LWRAM 1 Mo) ; `MAXNMPICS` 800 (MAP29 1 993 ⇒ ne pas mipper les 8 bpp `[src] PIC.C:423-433`) |
| 26 | **Sons** : DS* (DMX 8 bits 11 025 Hz) → `{size, 0x7000/0x7800, 8, −1}` + PCM `^0x80` ; ~30 statiques (armes/joueur/portes ≈ 250 Ko) dans STATIC, dynamiques des monstres présents dans le `.LEV` ; `sfx2snd` | B | `wad2snd.py` | 1,5 | **55 DS* = 535 127 o > 524 288** `[mesuré]` (E1M8 526 Ko : retirer DSITMBK 22 kHz ou tronçonneuse) ; 80 sons max ; ≤ 65 535 o/son (Doom max 18 600 : OK) |
| 27 | **STATIC.DAT Doom** : écran 320×240, feuille NBG0 (STBAR), sons statiques, **psprites** (30 lumps → 95 chunks / 130 063 o `[mesuré]`), `wSequence[0] = 0` | B | `wad2static.py` (ordre `[src] SRUINS.C:1925-1931`) | 2 | **`tileBase` = nb de tuiles d'armes ⇒ ≤ 160 tuiles de géométrie (u8)** : compatible E4.1b (≤ ~100) ; repli = psprites dans le `.LEV` (index short des chunks, +130 Ko LWRAM/niveau) si une carte dépasse |
| 28 | **Arme (psprites)** : chunks émis en `EZ_normSpr COLOR_4` à `(sx>>16 − 160 + chunkx, sy>>16 − 96 + chunky (cadre 224 ; −120 en 240))` ; bob/abaissement = `sx/sy` de `p_pspr` ; flash fullbright ; **pas `PICFLAG_LOCKED`** (incompatible mips, slot perdu en permanence) | B | remplace `runWeapon` | 1,5 | partage des **31 slots TILE8BPP** avec les monstres (`assert` `[src] SEQUENCE.C:303`) — mesure J0b avec arme |
| 29 | **Calque 2D** : lignes sales → feuille NBG0, palette banque 6, LUT 0→247 partout hors `GS_LEVEL` et sur les lignes 192-223 seulement en jeu (0 = transparent sur la vue), `HU_Erase/R_VideoErase/BRDR*/screenblocks` supprimés (~20 sites) | B | `DG_DrawFrame` | 2 | neige console ; copie plein écran 1,5-2 ms hors jeu seulement |
| 30 | 13 flashs PLAYPAL → `SCL_SetColOffset` (table de 14 teintes `[mesuré]`) ; invulnérabilité = blanc + banque inversée sprites | M/c | `st_stuff` → `I_SetPalette(i)` | 0,3 | teinte uniforme ≠ lerp |
| 31 | Wipe « melt » → fondu colour offset du fork `[src] V_BLANK.C:97-118` | c | `f_wipe` court-circuité | 0,2 | sacrifice |
| 32 | Menus / intermission / finale / titre / HELP : patches intacts ; `WI.PSW`/`TITLE.PSW`/`MENU.PSW` par état ; **flats de fond des finales** (FLOOR4_8, SFLR6_1, MFLR8_4/3 ; Doom II SLIME16, RROCK*) dans le jeu résident `[src] f_finale.c:70-83` ; cast → `sprites[]` du shim | m | `wad_psw.py` | 1 | INTERPIC/VICTORY2/PFUB absents du shareware `[mesuré]` |
| 33 | **Sauvegarde** différentielle + nom automatique (§2.7) | M | `bup_diff.c`, `m_menu` | 3 | `BUP_Write` bloquant (8 Ko ≈ plusieurs frames) ; `BUP_Init` fait `mem_malloc(0)` 16 + 8 Ko **à chaque sauvegarde** `[src] BUP.C:57-68` ⇒ 24 Ko de LWRAM libres exigés mi-niveau |
| 34 | Progression : `G_DoCompleted` → `+E1Mx.LEV` + `.LNK` + `.PSW`, court-circuit de `runMap` `[src] SRUINS.C:2463, 2512`, `levelGraph` remplacé, sortie secrète ; `NMLEVELS 31` et `trackMap[31]` (`[src] GAMESTAT.H:30`, `SOUND.C:370-390`) à relever pour Doom II (32) / Ultimate (36) | B (J5) | hôte | 1,5 | zone persistante (#3) |
| 35 | **Musique** : 13 MUS (245 179 o) → MIDI → rendu SF2 → WAV → `sox` raw 2352 → pistes (recette Mimas `build.ps1:248-262`) ; `trackMap[]` régénéré ; `idmus` = `playCDTrack(n)`, pause/reprise = commande de pause CDC (Mimas : `StopPause/Resume`) ; **ou** séquenceur MUS SH-2 de Mimas (slots 8-22, S 0 ms mesuré, 3 ondes, pas de percussions) | M | `disc.py` / `mus_step` | 2 | CDDA : 260-390 Mo, Doom II 32 morceaux ≤ 74 min ; le CD est libre en jeu (tout est chargé par packs) ; MUS : banque PCM en SCSP concurrence les SFX |
| 36 | Disque : `mkdisc.py --game doom` (N `.LEV/.LNK/.PSW`, STATIC, `IP.BIN` via `make_ip.py`, `xorrisofs` `[src] Makefile:278-308`, `.cue` multi-fichiers), MOV/LIP/BONUS retirés, `INITLOAD` minimal | B (J5) | `pslib/disc.py`, `verif_disc.py` (8.3, `size`, 2352, ≤ 74 min) | 2 | — |
| 37 | Validateurs : `verif_lnk.py` (tout secteur mobile lié, tout sidedef commutable cellulé, `sprite_seq` couvre les états atteignables, `sfx2snd` couvre les sons des mobjs présents, `tileBase + tuiles ≤ 255`, caps moteur), `verif_static.py` | M | `tools/pslib/` | 2 | le générateur passe toujours ses propres critères (leçon E3) |
| 38 | Démos attract DEMO1-3 (présentes dans le WAD strippé, 44 Ko `[mesuré]`) : ré-activer `d_main.c:895-897` — test de déterminisme gratuit | c | 1 ligne | 0,5 | gouverneurs OFF |
| 39 | PWAD : `merge_wad.py` (dernier gagne) + **bug `wad.py:51`** `setdefault` = premier gagne → inverser ; padding 4 | m | 2 lignes | 0,5 | silencieux |
| 40 | DEHACKED : **absent du core** (`deh_*.h` seuls `[src] deh_str.h:38`) ; `deh.py` → patch de `states[]` (27 076 o) / `mobjinfo[]` (12 604) / `S_sfx[]` en `.data` au boot + chaînes ; **appliqué avant** le calcul des sous-ensembles sprites/sons | m | outil + 100 l. C | 2,5 | Misc/Text partiels |
| 41 | Doom II / Ultimate : 32-36 `.LEV` (≈ 60 Mo), LWRAM sprites (miroirs, 0x6C, sous-ensemble par skill, cart), `MAXNMPICS`, fusion ≥ 875 feuilles, textures 428/147 vs 125/54 | M | J6 | 6-8 | cartouche obligatoire (§5.1) |
| 42 | Distribution des assets convertis : les `.LEV` **dérivent de l'IWAD** ; la règle Mimas « jamais d'IWAD » et le `/ship` aux testeurs s'appliquent ⇒ le convertisseur tourne **chez l'utilisateur**, comme pour les CON Duke | M | `ship.config.json` du fork | 0 | décision owner (shareware dérivé ≠ shareware intact) |

**Total Doom ≈ 70 j d'écriture** (somme des lignes ≈ 68 : outils ≈ 33, moteur/plateforme ≈ 35) ; **70-90 j** avec les
allers-retours console (`psw-world` a eu besoin de 90 rounds pour « validé »).

---

## 4. Inventaire des manques — Duke / Build

### 4.1 Le couplage, mesuré `[mesuré] tools/study/fullconv/r4_count.py`

| catégorie | appels | fonctions (n) | sort sur SlaveDriver |
|---|---|---|---|
| physique / collision (BUILDLIC) | **108** | `updatesector` 39, `cansee` 26, `hitscan` 13, `clipmove` 11, `pushmove` 6, `neartag` 6, `updatesectorz` 3, `getzrange` 2, `clipinsidebox` 2 | **clean-room** |
| géométrie pure | 133 | `getangle` 73, `getflorzofslope` 20, `nextsectorneighborz` 13, `ksqrt` 8, `rotatepoint` 5, `krand` 3 (LCG **identique** ou les tirages divergent) | réécrire (trivial) |
| listes de sprites | 179 | `changespritestat` 85, `setsprite` 52, `deletesprite` 24, `changespritesect` 14 | réécrire (273 l.) |
| rendu 3D | 144 | `drawrooms`, `drawmasks`, `setview`, `setviewtotile` (caméras), `preparemirror`… | remplacé ; miroirs/caméras impossibles |
| rendu 2D | **184** directs + ≈ 550 via 9 wrappers (`gametext` 219, `menutext` 148, `FTA` 81, `myospal` 68, `minitext` 55…) | `rotatesprite` 156 (27 avec angle ≠ 0, 32 zoom libre, 20 `pal ≠ 0`), `printext256` 18, `drawline256` 10 | **un** primitif `tile2d(x, y, zoom, angle, shade, pal, flags, clip)` sur `EZ_scaleSpr/EZ_distSpr/EZ_line`, `COMPO_TRANS`, gouraud ; 3 fontes |
| fichiers/cache `cache1d` (BUILDLIC) | 289 | `kdfread` 87 + `dfwrite` 76 (sauvegardes), `kread`, `kopen4load`… | `FILE.C` ; sauvegarde à repenser |
| baselayer / réseau / OSD | 254 | | shim / supprimer |
| **total** | **1 302** | + accès directs `sprite[]` 1 497, `sector[]` 587, `wall[]` 464, `tsprite/spritesortcnt` 77 (`animatesprites` modifie la liste triée du renderer), `sintable` 219, `totalclock` 147, macros `pragmas.h` (BUILDLIC) 421 | |

Cadence : `TICSPERFRAME = 120/26 = 4` ⇒ **30 tics/s** `[src] duke3d.h:100-101`.

### 4.2 Les manques

| # | manque | sév. | mécanisme | j | risque / preuve |
|---|---|---|---|---|---|
| D1 | **Physique/collision** BUILDLIC : `clipmove` 318 l., `hitscan` 271, `getzrange` 193, `pushmove` 131, `neartag` 118, `cansee` 45, `updatesector*`, `inside`, aides = **1 318 l.** + géométrie 281 + listes 273 = **≈ 1 872 l.** `[mesuré] engine.c:8321-9895` | B | **clean-room par comportement** depuis `jfbuild/doc/buildinf.txt` (882 l.) : un auteur écrit la spec, un autre code sans ouvrir `engine.c` ; **harnais différentiel PC hors dépôt** (jfbuild à côté, 10 000 pas enregistrés × 6 cartes, comparaison bit à bit) ; seules nos sources et les traces entrent au dépôt | 10-15 | aucune implémentation GPL-3-compatible connue (Raze = BUILDLIC + GPL-2 ; NBlood/PCExhumed GPL-2 **only** ; BuildGDX non vérifié) ; **question juridique des structs `sectortype/walltype/spritetype` redéfinies à l'identique** (format de fichier MAP v7) à poser avant tout commit |
| D2 | **CON** : `gamedef.c` compile GAME/DEFS/USER.CON (16 888 tokens shareware `[mesuré]`) → bytecode 17-20 k mots = **70-80 Ko** ; `parse()` 1 065 l. exécuté **par acteur et par tic** | B | compilateur **hors ligne** (port GPL-3 de `parsecommand`, chez l'utilisateur, CON jamais au dépôt) ; interprète seul embarqué (~1 500 l.) en LWRAM ; labels (272 Ko) supprimés | 5-8 | **CPU 5-25 ms/tic pour 30-60 acteurs `[est]`** sur un maître déjà à 22-47 ms de rendu = **risque n° 1 de Duke** ; mesurer en headless (D1 §6) avant tout le reste |
| D3 | `rotatesprite` → primitif 2D + 3 fontes + `displayweapon` (64 blits) + HUD/inventaire/quotes/bonus screen | B | `tile2d` (300-500 l.) | 10-15 | `pal ≠ 0` = banque CRAM (8/8 prises) → tuiles recolorées |
| D4 | RAM du playsim : tableaux Build « tels quels » = **≈ 3,4 Mo** (V8) / 1,23 Mo (V7) ; régime 640 secteurs / 4 096 murs / 1 536 sprites + `hittype` réduit + bytecode ⇒ **≈ 505 Ko** `[mesuré]` (E1L4 : 557 / 3 437 / 1 179) | B | structs à nous, `wall[]` en LWRAM, `.map` du jour | 5-10 | + `.LEV` E1L1 589 Ko ⇒ régime sévère ou cartouche |
| D5 | Sprites ART → 0x6A : familles complètes présentes = **300-400 chunks, 312-444 Ko RLE/carte** `[mesuré]` (LIZTROOP 54 tuiles, PIGCOP 51, BOSS1 39/307 Ko) ; **5 vues + miroir** ; plages de frames lues du CON | B | `duke_sprites.py` | 4 | LWRAM |
| D6 | Sprites **muraux** (cstat 16 : 138/639 E1L1, 22 % — interrupteurs, affiches) et **au sol** (cstat 32) : le moteur n'a que des billboards + `SPRITEFLAG_LINE/THINLINE` | M | quad `EZ_distSpr` collé au mur/sol, 100-200 l. `WALLS.C` | 2-3 | budget esclave |
| D7 | Géométrie mobile Duke : hauteurs (portes ST, lifts SE) = même recette que Doom ; **translation** de secteurs SE6/14/30 (métro) → **parquée** (un wagon traverse plusieurs cellules convexes figées → `nextSector` faux) ; **rotation** SE0/1/11 → recalcul de normale (~50 l.) + plans de coupe à tester ; SE2 séisme → **`earthQuake` du moteur existe** `[src] SRUINS.C:1831-1838, 2109-2114` ; `rotscrnang` → roll caméra existant | M | hôte | 2 | dégradé |
| D8 | Panning/repeat (E1L1 : 674 murs à panning, 1 814/1 937 repeat ≠ 8 `[mesuré]`) : aucun UV par cellule | m | statique | 0 | dégradé |
| D9 | Eau SE7 (téléport entre secteurs) : le playsim téléporte, les deux secteurs sont dans le `.LEV` ; `SECFLAG_WATER`/ondulation optionnels | M | — | 1 | OK |
| D10 | Verre / murs cassables / forcefields (`picnum`/`overpicnum` + éclats `[src] sector.c:466-495, 1134`) | M | `host_cell_tile` | 1 | — |
| D11 | Tripbomb laser → `SPRITEFLAG_LINE` `[src] SPRITE.H:38` ; jauge d'air → `drawAirMeter` `[src] SRUINS.C:1427` ; shrinker → `Sprite.scale` : trois mécanismes **gratuits** du moteur | m | hôte | 1 | — |
| D18 | Joueur Duke : accroupi / saut / jetpack / natation / holoduke = logique `player.c` intacte ; la caméra suit `ps->posz` et `horiz` → `playerAngle.pitch` (le moteur pitche déjà : `MTH_RotateMatrixX` `[src] SRUINS.C:2106-2108`) ; nightvision `pal` → colour offset vert ; pentes statiques = faces libres du `.LEV` (E3, OK) | m | hôte | 1 | pitch affine (quads) |
| D12 | Sons : **181 VOC = 2,9 Mo** (107 @ 8 kHz, 35 @ 11 025, 29 @ 5 988) `[mesuré]` vs 512 Ko / 80 sons ⇒ 40-60 par carte ; **`BONUS.VOC` 268 Ko et toute voix > 64 Ko sont inchargeables** (`size` u16) sans découpe en sons chaînés (le moteur n'enchaîne pas) ; MID → CDDA | M | `voc2snd.py` | 1-2 | voix longues |
| D13 | Sauvegarde : `saveplayer` dumpe `hittype[MAXSPRITES]` + tableaux (≥ 300 Ko) | M | checkpoint niveau + inventaire (10 slots Duke → 6 `SaveRec`) | 2 | — |
| D14 | Cheats tapés (`cheatquotes` `[src] game.c:5940-5959`), démos | m | page de menu | 1 | — |
| D15 | Convertisseur : `convex.py` réussit sur **33 cartes brutes / 194** (26 quantifiées, 137 plantages amont `[doc] NOTES_QUANT.md:150-157`) | B | robustesse (trous, éclats, superpositions) | 6 | **le goulot** de toute la voie Duke |
| D16 | Miroirs (E1L1 4 murs, E1L2 3), caméras `setviewtotile`, ROR/TROR (1.5), `visibility` brouillard | — | **impossibles** → mur opaque / tuile fixe ; brouillard approximé | 0 | — |
| D17 | Licences : `LICENSE.TXT [4][A]` de 3D Realms interdit les niveaux fonctionnant avec l'épisode 1 ⇒ cartes converties = recherche, jamais diffusées ; jfduke3d GPL-2+ ✓ ; jfaudiolib GPL-2+ inutile (SCSP direct) ; jfmact sans licence ✗ | — | convertisseur local | 0 | — |

**Verdict Duke** : ≈ 30 000 l. GPL-2+ portables telles quelles, ≈ 1 900 à réimplémenter, ≈ 1 500 à
ré-exprimer (2D, `animatesprites` → hook, caméra, palette) ; **90-120 j au-dessus de Doom** `[est]`
(J3 corrige les 55-90 des architectes), plancher CPU probable 10-15 fps tant que le coût de
`parse()` + `clipmove` sur SH-2 n'est pas mesuré. « L'impression de jouer à Duke » restera en dessous
de celle de Doom : ce qui manque n'est pas du polish, ce sont des mécanismes (métro, miroirs, panning)
que le moteur ne peut pas rendre.

### 4.3 Doom contre Duke, dix axes

| axe | Doom | Duke |
|---|---|---|
| couplage au renderer | 20 fonctions / 75 sites | 1 302 appels + 3 950 accès directs |
| propriétaire des structures monde | le playsim (GPL) | le moteur (BUILDLIC) → structs à redéfinir |
| physique | `p_map.c/p_maputl.c` dans le playsim | `engine.c` 1 318 l. clean-room |
| comportements | tables `info.c` compilées | CON : compilateur + interprète, CPU par acteur |
| 2D | `V_DrawPatch` maison | `rotatesprite` moteur, ≈ 700 sites |
| géométrie mobile | verticale | verticale + translation + rotation + panning |
| sprites | billboards, 8 rotations | billboards + muraux (22 %) + sol ; 5 vues ; `pal` |
| son | 55 lumps 535 Ko | 181 VOC 2,9 Mo, 3 taux, voix > 64 Ko |
| RAM playsim | 80-290 Ko | ≈ 505 Ko après régime |
| sauvegarde | `P_Archive*` compact | dump ≥ 300 Ko |

---

## 5. Budgets

### 5.1 RAM — trois plans, l'arithmétique refaite (J3)

HWRAM libre aujourd'hui **432 008 o** `[mesuré]`. Après échange de code (−184 Ko de PowerSlave :
AI/OBJECT/WEAPON/HITSCAN/ROUTE/MENU/INTRO/BIGMAP, `objects[]` ; +336 Ko de Doom non-renderer
`[mesuré] Mimas.map` : text 178 + rodata 70 + data 59 + bss 29 Ko) : ≈ 275 Ko ; +70 Ko si les tables
`.rodata` (`finesine`, `finetangent`, `tantoangle`) vont en LWRAM par l'éditeur de liens ; −72 Ko de
framebuffer `I_VideoBuffer` (HWRAM, lu par `V_DrawPatch`), −12 Ko de pile, −14 Ko de sprites 650 ⇒
**HWRAM libre ≈ 240-310 Ko** `[est]`. Géométrie `.LEV` E1M1 = 237 932 o (y tient) ; E1M6 ≈ 380-607 Ko
`[est]` (n'y tient pas).

LWRAM (1 024 Ko) — E1M1 :

| poste | E4.1c (141 tuiles) | **E4.1b (~70 tuiles)** | source |
|---|---|---|---|
| tuiles 0x32 déversées (HWRAM pleine) / 0x72 | 578 | 280 | `[mesuré] e1m1_tiles.json` ; `[doc] DOOM_ON §4.3` |
| sprites RLE du niveau (8 rotations ; **PLAY exclu en 1p : −74 Ko**) + séquences | 200-225 | 200-225 | `[mesuré]` R5 §5 |
| psprites RLE de STATIC — **en aire 0 aussi** (`load8BPPRLETile` `[src] PIC.C:574`) | 130 | 130 | `[mesuré]` |
| **bloc zone réservé avant `mem_lock()`** (E1M1 : PU_LEVEL 81 Ko + transitoires + `lumpinfo` ; les lumps 2D sont mappés dans les `.PSW`, pas copiés) | 256 (à fixer par J0a-4) | 256 | `[est]` |
| `RES.PSW` + `.PSW` du niveau + `.LNK` (mappés) | 137 | 137 | `[mesuré]` |
| divers (staging son 18 Ko, tables `.rodata` reloguées 70 Ko, marge) | 98 | 98 | |
| **total** | **≈ 1 425 → NON (−400 Ko)** | **≈ 1 100-1 125 → NON (−75..−100 Ko)** | |
| leviers de B : 4 rotations (÷1,6 : −75..−85 Ko) ; tables `.rodata` laissées en HWRAM (−70 Ko LWRAM, +70 HWRAM) ; zone mesurée < 256 Ko | | **≈ 940-980 → OUI (+45..+85 Ko)** | `[est]` |

| plan | E1M1 | E1M6 | Doom II MAP15 | verdict |
|---|---|---|---|---|
| **A** : structures Doom complètes + `.LEV` E4.1c | −54 Ko | −806 Ko | — | ne rentre **nulle part** |
| **B** : E4.1b, segs supprimés, `objects[]` retiré, PLAY exclu, + deux leviers (4 rotations ou tables en HWRAM, zone mesurée) | −100..+85 Ko selon les leviers | −325 Ko **avec** sommets partagés (non prouvé, §1.3) ; ≈ −550 sans | — | **plancher** : Saturn nue ; E1M1/E1M8, probablement E1M4/E1M5, **non acquis** avant J0a |
| **C** : B + cartouche 4 Mo à 0x22400000 (cache-through ; **exclusive** avec l'IWAD-en-cart de Mimas, 19 Ko restants) : tuiles brutes + sprites RLE + séquences + lumps sur cart, géométrie chaude en HWRAM | +694 Ko | +349 Ko | +309 Ko | **déverrouillage** : E1M6, Doom II ; exige `validPtr` +1 ligne `[src] UTIL.H:83`, RLE décodé depuis un tampon HWRAM (A-bus ~4× lent, lecture octet par octet disproportionnée `[doc] HW_MEMORY_AND_BUS §6`), **jamais de géométrie parcourue par frame sur la cart** |

**B n'est pas acquis** : avec le bloc zone compté à sa vraie taille (réservé en permanence) et les
psprites en LWRAM, E1M1 en E4.1b est à −75..−100 Ko ; il ne passe qu'en tirant deux leviers (4
rotations ou tables en HWRAM, zone mesurée). J0a-2 et J0a-4 tranchent ; **C (cartouche) est le plan
confortable**, B le plan « Saturn nue » à défendre chiffre par chiffre.
**Décision de fond** : la première décision du projet est **E4.1b ou E4.1c** (290 Ko de LWRAM sur
E1M1), pas « NBG0 ou NBG1 ». Et E1M6 est mort sans cartouche **deux fois** (606 feuilles > 600, et
−325 Ko) : cart **et** `merge_leaves` obligatoires ; Doom II idem. La zone Doom : E1M1 ≈ 160-200 Ko, E1M6
≈ 350-440 Ko, MAP15 ≈ 470 Ko `[est]` ⇒ un bloc de **256 Ko** sans cartouche (shareware hors E1M6), **512 Ko** avec (C délestant la LWRAM) — à mesurer par un `printf(Z_FreeMemory())`
dans Mimas (compteurs `zf` existants `[src] dg_saturn.cxx:209`).

### 5.2 CPU — fps attendus, build assert, E1M1 `[est]`

| scène | cellules `[est]` | calc (loi) | tic/frame (T Mimas console shareware `[doc] RESOURCE_BUDGETS.md:38`) | sprites + sync + blit | total | palier vblank |
|---|---|---|---|---|---|---|
| couloir | 200 | 22,7 | 3-6 | 1 | 27-30 | **30 fps** (33,4), juste |
| salle, 10 monstres | 450 | 32,5 | 6-10 | 2,5 | 41-45 | **20 fps** |
| arène, 25 monstres | 800 | 46,3 | 10-18 | 4 + swaps 0-10 | 60-78 | **15 fps**, 12 si swaps |

Le tic tourne dans la fenêtre esclave (`[src] SRUINS.C:2137-2191`, où le maître spinne aujourd'hui :
`while (!(*FTCSR & 0x80))` compté dans `calc` `[src] WALLS.C:2452-2455`) : une part du tic est
absorbée, d'où la fourchette basse. **20 fps médian, 15-30 selon la scène** — contre 6,6-23 fps pour
Mimas M7 1p et 15-50 pour `psw-world`. Le build `-NDebug` (jamais mesuré) peut valoir un palier.
Le fill VDP1 des sprites proches (un chunk zoomé à 200 px = 40 k px ; `psw-world` a vu VD1 38 ms en
scène ouverte) n'est budgété par personne : c'est le kill de J0b sur `draw`.

### 5.3 VDP1 et caches

| ressource | valeur | conséquence |
|---|---|---|
| VRAM VDP1 | 516 864 / 524 288 utilisés (**7 424 o libres**) `[mesuré]` | tout slot de cache en plus se paie en commandes (1 448 → 1 024 = +27 Ko) |
| cache murs/flats `TILE16BPP` | 28 slots ; Doom : textures + flats distincts à 3 sauts = médiane **15**, E1M4 20, E1M7 19 `[mesuré] wadcache.py` ; console Duke E1L1 `used 14-18`, `vswaps 0` `[HW]` | tient |
| cache sprites/arme `TILE8BPP` | **31 slots** partagés ; POSS A→D × 8 rotations = jusqu'à 32 tuiles pour **une** famille ; une tuile évincée en frame = **texture fausse**, pas seulement lente ; chaque swap = 4 096 px de RLE + DMA (~0,5-1 ms `[est]`) | **le risque n° 1 de Doom** ; leviers : 4 rotations (÷1,6), 32×32 pour les pickups, `NOSHADOW` sur tout Doom (−1 cmd, −`findFloorDistance` par sprite), pas de FOOTCLIP hors eau (−2 cmds) |
| commandes | 1 448 par banque ; 20-40 monstres × 1,3 chunk + murs 200-900 + arme ≤ 8 + HUD | dans la banque ; le plot est le mur, pas la file |
| `drawList[100]` par secteur, tri par insertion ~O(n) en régime stable `[src] WALLS.C:2572, 2612-2629` | | OK |

### 5.4 Son, sauvegarde, disque

| | budget | Doom shareware | Duke shareware |
|---|---|---|---|
| RAM SCSP | 512 Ko, 80 sons, ≤ 65 535 o/son | 55 DS* = 535 Ko : liste blanche par niveau (E1M1 469, E1M8 526 Ko) ; max 18 600 o | 181 VOC 2,9 Mo → 40-60/carte ; `BONUS.VOC` 268 Ko impossible tel quel |
| backup | 32 Ko, blocs de 64 o | vanilla 27-101 Ko ⇒ différentiel 2-4 Ko × 6 | `saveplayer` ≥ 300 Ko ⇒ checkpoint |
| disque | ISO + CDDA ≤ 74 min | 9 `.LEV` × 1,4-2,0 Mo + `.PSW` 1,5 Mo + STATIC 0,7 Mo ≈ 20 Mo ; CDDA 13 pistes 260-390 Mo | 6 `.LEV` ; 7 MID |

---

## 6. Plan en jalons

Règles : chaque jalon livre **un disque** et une table « vu → sens » (le propriétaire teste sur
console, jamais l'assistant `[mem]`) ; les sondes existent seulement en build **assert +
STATUSTEXT** ; toute mesure de temps se fait sur console, jamais sur Ymir ; `make size` et pré-vol du `.map` avant
chaque disque (le pool part de `end`, comme le TLSF de Mimas).

| J | livrable | j | dépend | kill criterion |
|---|---|---|---|---|
| **J0a — mesures gratuites** | (1) 3 captures STATUSTEXT sur le disque `TOMB_e1m1` **existant** : couloir / salle / arène → `polys`, `time`, `mem:` (les deux aires séparément) ; (2) le même disque avec un `mem_malloc(0, 256 Ko)` factice pris **avant `mem_lock()`** (2 lignes ; 512 Ko déborderait la LWRAM du disque E4.1c actuel, ≈ 430 Ko restants `[est]` R5 §4) → `mem:`, à refaire sur le premier disque E4.1b ; (3) champ **T** de l'overlay Mimas en 1p console sur E1M1 et E1M6 (même playsim, même LWRAM) ; (4) `printf(Z_FreeMemory())` après `P_SetupLevel` dans Mimas (1 ligne) | 0,5 | — | `mem:` aire 0 < 100 Ko avec la zone ⇒ E4.1b obligatoire dès J1 ; < 64 Ko ⇒ B mort, C seul ; T > 20 ms/frame ⇒ gouverneurs ON ou palier 20 fps assumé ; `polys` > 900 en salle ⇒ la réserve §0-7 se pose avant J1 |
| **J0b — densité** | 30 marionnettes **animées** (POSS frames A-D cyclées, 8 rotations, angle des THINGS) + 4 chunks d'arme dans `TOMB_e1m1`, `scale = 65 536`, `radius = 0`, `F(32)` → 10, `NOSHADOW` ; d'abord avec 25 sprites du **donneur** (0 convertisseur), puis `wad2sprites.py` réduit à POSS + `wad2snd.py` (un cri par entrée de frame) | 4 | J0a | `vswaps[1]` > 4/frame à 15-20 visibles ⇒ 4 rotations / 32×32 avant tout autre mécanisme ; `draw` > 25 ms ⇒ budget de fill sprites ; monstre coupé net au bord d'une dalle ⇒ `userClip` élargi (2× le coût) ou toléré ; texture fausse ⇒ éviction en frame |
| **J1 — E1M1 se joue** | core compilé dans le fork (gnu11), `r_shim.c`, zone avant `mem_lock`, `_sbrk`/pile, `E1M1.PSW` mappé, `.LNK` v1, boucle hôte, 35 Hz, pad, marionnettes complètes (POSS/SPOS/TROO + pickups + projectiles ≈ 270 Ko), caméra `viewz` ; portes **ouvertes** dans le `.LEV` mais fermées dans le playsim ; STATUSTEXT : santé, munitions, `tic`, `sync` | 15 | J0b | tic + sync + `drawSprites` > 33 ms avec 10 monstres visibles ⇒ gouverneurs ; `mem:` négatif ⇒ C |
| **J2 — le monde bouge** | `.LEV` fermé + course par rangées, `merge_leaves` (E1M6), tuiles d'anim et d'interrupteurs, table lumière, ciel SKY1, midtex, `xoffset` ; `sat_sync_world` complet ; **compteur de murs sautés** au `return` de `WALLS.C:1374` ; adaptateur à 5 verbes **extrait** | 12 | J1 | murs sautés > 0 sur E1M1 avec portes fermées ⇒ rangées de 32 u / ciel plus bas ; reliquat étiré visible ⇒ rangées de 32 u |
| **J3 — ça ressemble à Doom** | `SCL_224LINE`, feuille NBG0 = écran Doom (STBAR, visage, messages, menus, automap, intermission, titre, HELP), 13 teintes, fondu, arme en chunks, aspect ×1,12 si décidé | 8 | J1 | neige NBG0 sur console ⇒ NBG1/B1 (+2 j) ; arme évincée du cache avec 6+ monstres ⇒ chunks 32×32 ; copie 2D > 3 ms/frame en jeu ⇒ dirty-rows seulement |
| **J4 — ça sonne Doom** | backend SFX Mimas en C (slots 0-7), liste blanche par niveau, musique (CDDA ou MUS SH-2 selon §7) | 5 | J1 | latence > 1 frame ou clics de key-off ; `assert soundTop` sur E1M8 après liste blanche |
| **J5 — le shareware entier** | 9 `.LEV/.LNK/.PSW`, `TITLE/MENU/INTER.PSW`, STATIC Doom, `mkdisc.py`, `.cue`, progression + sortie secrète, sauvegarde différentielle + nom auto, démos, `verif_lnk/static/disc`, `/ship` | 10 | J2-J4 | E1M6 refuse sans cartouche = attendu (dire « cart only ») |
| **J6 — autres WADs** | Doom II / Ultimate / PWAD (`merge_wad` corrigé) / DEH ; plan C (cart) ; fusion ≥ 875 feuilles ; LWRAM sprites (miroirs, 0x6C, skill) ; `MAXNMPICS` | 9-11 | J5 | MAP15/MAP29 hors budget sans cart ; `size ≥ 900 000` ⇒ carte déclarée non portable |
| **J7 — étude split 2p** | deux rondes esclave par frame, deux caméras, `movePlax` unique, W0 unique : **étude chiffrée**, pas de promesse | 2 | J5 | — |
| **Duke D0** (parallèle, PC, hors dépôt) | clean-room 1 872 l. + harnais différentiel + `krand` identique ; traces 6 cartes × 10 000 pas | 10-15 | — | < 100 % bit-exact après 15 j ⇒ **stop Duke** (Doom intact) |
| **Duke D1** | CON compilé hors ligne + VM headless sur SH-2 : E1L1 acteurs tournent sans rendu ; `tic` STATUSTEXT | 5-8 | D0 | `tic` médian > 20 ms avec les acteurs actifs d'E1L1 ⇒ VM sur esclave ou stop |
| **Duke D2-D5** | monde (adaptateur J2 + quads muraux/sol + `duke_sprites.py` + régime RAM), 2D/arme/HUD/menus, son + SE/ST carte par carte, disque | 55-85 | J2, D1 | murs sautés > 0 ou `used[0]` ≥ 28 avec les 138 sprites muraux d'E1L1 ⇒ muraux en billboards ; E1L1 injouable (SE7, ST) ⇒ carte non portable |

**Total Doom J0-J6 ≈ 70 j d'écriture, 70-90 j avec les allers-retours console.** Duke D0-D5 :
70-110 j d'écriture (somme des lignes), 90-120 j avec les allers-retours ; D0-D1 (15-23 j)
s'exécutent **maintenant**, sans le moteur.

### Vu → sens (extraits ; le détail par jalon est dans D1/D2/D3 §3)

- **J0a** : `mem:` imprime deux nombres — le seuil se pose sur l'**aire 0** (LWRAM) seule, car
  l'aire 1 coule automatiquement dans l'aire 0 (`[src] UTIL.C:382-386`) et un cumul masquerait une
  HWRAM à zéro. `polys` en salle > 900 ⇒ la loi 39,2 µs donne > 50 ms avant tic.
- **J0b** : `used[1]` ≤ 28 et `vswaps[1]` ≤ 2/frame face à ≈ 10 monstres ⇒ le cache tient ; un zombie
  « à la mauvaise texture » ⇒ éviction en frame ; zombie coupé au bord d'une dalle ⇒ clip de
  frontière ; zombie de la mauvaise taille ⇒ `scale` ; zombie enterré ⇒ `radius`.
- **J1** : un zombie tire et la santé baisse ⇒ playsim + `P_LineAttack` tournent ; « mur invisible »
  devant une porte fermée = attendu (J2) ; monstres qui glissent sans marcher ⇒ table
  (sprite, frame) → séquence ; monstre qui « saute » d'une dalle ⇒ `link[]` faux.
- **J2** : la porte d'entrée d'E1M1 monte en 35 tics (2 u/tic, 4 pour les blazing), le linteau suit,
  aucun mur ne disparaît (compteur = 0) ; la mare de nukage clignote à 8 tics ; le couloir secret
  strobe ; un interrupteur change de tuile ; fissure d'un pixel au linteau ⇒ sommet max/min mal lié.
- **J3** : visage qui grimace + flash rouge à un coup ⇒ palette ; « neige » sur le HUD ⇒ cycles B ;
  arme qui « manque » avec 6+ monstres ⇒ 31 slots.
- **J4** : porte + interrupteur + tir simultanés sans coupure ⇒ 8 voix ; « sound RAM full » à E1M8 ⇒
  liste blanche ; D_E1M1 en piste 2.
- **J5** : E1M1 → E1M2 via l'intermission avec temps/par ; E1M3 → E1M9 par la sortie secrète ;
  « game saved. » puis reboot → « Load » reprend avec les mêmes portes ouvertes et les mêmes morts.

---

## 7. Décisions owner

| décision | recommandé | pourquoi |
|---|---|---|
| Tuiles de murs : **E4.1b** (une par texture, demi-résolution) vs E4.1c (141 tuiles, pleine échelle) | **E4.1b** par défaut ; E4.1c seulement avec cartouche **et** psprites dans le `.LEV` (E1M6 E4.1c ≈ 170 tuiles + 95 > 255) | 290 Ko de LWRAM sur E1M1 (§5.1) ; `tileBase` ≤ 160 tuiles de géométrie avec les psprites en STATIC |
| Mémoire : B plancher + C déverrouillage | **oui** | la Saturn nue boote E1M1-E1M5/E1M8 ; la cart ouvre E1M6/Doom II ; « C seul » = public réduit aux cartouches |
| Cadre : 224 lignes (vue 192 + STBAR 32) vs 240 | **224** | le core est déjà câblé ainsi ; ≈ 1 j (clip + arme + ciel) |
| Aspect vertical ×1,12 (focale asm + hauteur sprites) vs 1:1 « texel exact » | **×1,12, après J3** | c'est « le graphisme change » dans le mauvais sens sinon (11-17 % plus plat) ; 1-2 j |
| 2D : feuille NBG0 existante vs NBG1 en B1 | **NBG0** ; repli NBG1 si neige irréductible en J3 | 0 VRAM, 0 cycle, prio déjà au-dessus |
| Flashs : colour offset vs réécriture CRAM | **colour offset** | couvre les murs RGB16 ; cohérence > exactitude ; invulnérabilité approximée |
| Musique : CDDA rendue (SF2 au choix) vs séquenceur MUS SH-2 de Mimas | **CDDA** | le CD est libre en jeu (packs chargés d'un bloc) ; le séquenceur = 3 ondes, sans percussions, 15 slots en concurrence avec 470-526 Ko de SFX ; MUS en secours |
| Sons : liste blanche par niveau vs troncature | **liste blanche** | 535 > 512 Ko de 11 Ko seulement |
| Rotations : 8 + miroirs A2A8 vs 4 | **8** ; 4 si J0b montre `vswaps[1]` > 2-4/frame | une séquence par vue, facing côté hôte ; le repli 4 vues existe déjà dans Mimas (`rlvl`, `>>30 <<1` `[src] r_things.c`) ; ÷1,6 de RLE |
| Portes : rangées de 64 u vs 32 u | **64** ; 32 si le reliquat gêne | ×2 murs en 32 |
| Sauvegarde : différentielle 2-4 Ko vs checkpoint 100 o vs intégrale | **différentielle** ; intégrale sur cartouche backup | mi-niveau sans cartouche ; le vanilla ne tient pas en 32 Ko |
| Psprites : STATIC (tuiles d'armes) vs `.LEV` | **STATIC** avec vérif `tileBase + tuiles ≤ 255` | 0 changement moteur ; repli `.LEV` (+130 Ko LWRAM/niveau) |
| Gouverneurs Mimas (décimation, parqués) | **OFF** par défaut | désynchronisent démos et sauvegardes ; ON si T mesuré > 20 ms |
| Distribution : disques dérivés de l'IWAD aux testeurs | **le convertisseur tourne chez le testeur** | shareware dérivé ≠ shareware intact ; même règle que les CON Duke |
| Duke : lancer D0 maintenant (PC) ou reporter après J3 | **D0 maintenant** + poser la question juridique des structs avant D2 | aucune dépendance au moteur ; le kill criterion se mesure sans lui |
| Split-screen | **hors plan**, étude J7 | 1 caméra, 1 kick esclave, W0 unique |
| Fork vs `psw-world` | **J0a décide** | `polys`/`calc` d'E1M1 contre P90 : couloir 50 fps, spawn 24-33 ms |

---

## 8. Inconnues à mesurer d'abord (par ordre)

| inconnue | instrument | tranche |
|---|---|---|
| `polys` et `calc` d'E1M1 par classe de scène | STATUSTEXT sur le disque existant (J0a-1) | fps plancher ; fork vs `psw-world` |
| LWRAM restante avec E1M1 + zone 256 Ko | `mem:` aire 0 après le `mem_malloc` factice avant `mem_lock()` (J0a-2) | B vs C ; E4.1b |
| Coût du tic Doom E1M1/E1M6 | T de l'overlay Mimas console (J0a-3) | gouverneurs ; palier |
| Taille réelle de la zone (`Z_FreeMemory` E1M1/E1M6) | 1 `printf` dans Mimas (J0a-4) | 256 ou 512 Ko ? |
| `used[1]/vswaps[1]` sous 15-20 marionnettes **animées** + arme | STATUSTEXT (J0b) | 8 vs 4 rotations, 32×32 |
| `draw` par sprite visible et fill des sprites proches | `draw` − référence J0a (J0b) | budget fill ; `NOSHADOW`, cull |
| Clip aux frontières de pièces convexes | œil (J0b) | `userClip` élargi ou toléré |
| Murs sautés par le budget esclave, portes fermées | compteur `WALLS.C:1374` (J2) | rangées 32 u |
| Neige NBG0 + RBG0 K-par-dot + écritures CPU pendant l'affichage | console seulement (J3) | NBG1/B1 |
| Valeur de la loi de coût en `-NDebug` | 5 captures `-NDebug` STATUSTEXT | un palier ? |
| Coût A-bus d'un `map()` de tuile/chunk depuis la cartouche | `htimer` autour de `dmaMemCpy` | sprites RLE sur cart ou non (C) |
| Durées des 13 MUS rendus | `mus2mid` + FluidSynth | taille CDDA |
| Coût `parse()` + `clipmove` par acteur Duke | D1 headless | R-8 ; VM esclave |
| Facteur de partage des sommets `.LEV` (hors lumière animée) | script sur `doom3d.py` | E1M6 en HWRAM en C ? |

---

## 9. Ce que ce plan sacrifie

- **Éclairage** : 32 colormaps + atténuation à la distance → 6 banques CRAM pour les sprites, gouraud
  par sommet sur les murs ; l'inversion d'invulnérabilité et la lampe IR (`fixedcolormap` 32 / 1) ne s'appliquent qu'aux sprites
  (banque inversée / banque 0), jamais aux murs 16 bpp ;
  la Blur Sphere et le Spectre sont une silhouette sombre, pas le fuzz.
- **Palette** : 13 flashs = teintes uniformes (colour offset), pas le lerp PLAYPAL.
- **Textures** : murs à demi-résolution (E4.1b, une tuile 64×64 par texture) ; scrollers immobiles ;
  midtextures et ordre peintre par secteur ; sols rognés aux bords des feuilles (structurel, `[doc]`
  doom2ps).
- **Wipe** : fondu au lieu du melt.
- **Cadence** : 15-30 fps selon la scène, 1-2 tics par frame ; en build assert.
- **Cartes** : E1M6 et Doom II exigent la cartouche et la fusion de feuilles ; au-delà de ~900
  feuilles (`size < 900 000`), rien.
- **Sprites** : clippés à leur pièce convexe tant que `userClip` n'est pas élargi ; pas de couleurs de
  joueurs avant le multi.
- **Sauvegarde** différentielle, nom automatique ; **cheats** par menu ; **DEHACKED** partiel
  (Misc/Text) ; **démos** seulement gouverneurs OFF.
- **Split-screen**, deathmatch et coop locaux hors plan (ils en dépendent).
- **Duke** : miroirs, caméras, métro, panning, brouillard, RTS, réseau impossibles ou dégradés ;
  181 VOC réduits à 40-60 par niveau et voix > 64 Ko coupées ; physique clean-room dont la fidélité ne
  tient que par le harnais ; 10-15 fps probables.
- **Disque** : le WAD n'est plus lu à l'exécution — tout PWAD passe par le convertisseur hors ligne,
  chez l'utilisateur.

---

## 10. Corrections à reporter dans les documents existants

- `DOOM_ON_SLAVEDRIVER.md` §5.3 : HWRAM libre **432 008 o** (build 2026-09-13), pas 438 720 ;
  « pile LIFO 2 aires et 8 niveaux » : l'allocation est un anneau, seule la libération est LIFO ;
  ajouter : **`mem_init()` en tête de `runLevel`** balaie les deux aires ⇒ la zone Doom se prend avant
  `mem_lock()`.
- `DOOM_ON_SLAVEDRIVER.md` §4.4 : les chunks d'arme et de monstres partagent les **31 slots**
  `TILE8BPP` ; ajouter le plafond **`tileBase` u8** (LEVEL.C:69-72).
- `DOOM_ON_SLAVEDRIVER.md` §7 : jalons 1-2 faits ; le jalon 3 est ici J1-J2 avec les quatre lignes
  oubliées (§2.1).
- `HW_USAGE_VS_MIMAS.md` : `validPtr` est à `UTIL.H:83` ; « NBG0 sous les sprites » est faux
  (prio 6 > SP0 4).
- Mémoires `slavedriver-doom-port-study` et `doom2ps-converter` : lignes de `doom3d.py` périmées
  (`open_doors` :589, `light_of` :182).

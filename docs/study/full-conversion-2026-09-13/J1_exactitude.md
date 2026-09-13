# J1 — Exactitude contre le code (juge adverse, 2026-09-13)

Lecture seule de `SlaveDriver-Engine/*.C/*.H`, `sdk/sbl6/include/sega_scl.h`, `build/stext/MAIN.map`, `tools/doom2ps`, `Mimas/core`, `Mimas/src`, `refs/build/jfduke3d`, `refs/build/jfbuild`. Étiquettes : `[src] FICHIER:LIGNE` (ouvert ce jour), `[mesuré]` (script `scratchpad/reports/j1_wadcheck.py` sur `Mimas/cd/data/DOOM1.WAD`), `[est]`. Environ 60 lignes de code citées par les propositions ont été ouvertes ; ce qui suit ne cite que ce qui a été vu.

## 1. Notes (sur 10)

| Critère | D1 fidélité Doom | D2 pipeline disque | D3 Duke/risques |
|---|---|---|---|
| Fidélité Doom | **8** — playsim intact, 2D intact, CDDA, 8 rotations ; mais oublie l'échelle des marionnettes (§2-12) et le cadre 224 lignes est sous-chiffré (§2-8) | **7** — même ossature, mais rendu SF2 pour la musique et 2D décrit sans le détail du blit | **6** — synthé MUS 3 ondes, sauvegarde différentielle non-vanilla, split abandonné, 6 paliers |
| Fidélité Duke | **3** — reportée, une vtable et rien de vérifié | **5** — données en parallèle, playsim « programme séparé » sans jalon chiffré | **7** — D0 clean-room + harnais différentiel maintenant, liste honnête des impossibles, question juridique posée |
| Complétude vs « TOUT » | **8** — menus/HUD/intermission/finale/cast/démos/DEH/progression/sauvegarde tous adressés | **9** — + DEH par patch `.data`, PWAD, validateurs, `.cue`, disque chiffré | **6** — DEH « non supporté », split « hors plan », démos absentes |
| Faisabilité RAM-CPU | **5** — E1M6 cart-only accepté, mais zone Doom balayée par `mem_init()` (§4-a), débordement d'index de tuile (§4-c), coût du tic non mesuré | **6** — nomme le plafond `tileBase` (§3), `.PSW` d'un bloc ; même angle mort sur `mem_init()` | **7** — plans B/C explicites, régime chiffré, kill criteria numériques ; même angle mort |
| Économie des mécanismes | **7** — feuille NBG0 existante, backend Mimas en C ; mais 3 patchs moteur + `r_shim` + `dg_lev` | **8** — zéro seek, zéro format `.LEV` modifié, 6e fichier | **7** — 5 verbes, mais ajoute une VM CON et un primitif 2D |
| Testabilité console | **8** — J0 gratuit, tables « vu → sens », instruments nommés ; ne dit pas que `used/vswaps` n'existent qu'en build assert (§4-g) | **7** — J2 « portes au chronomètre sans playsim » est la meilleure sonde des trois ; même silence sur NDEBUG | **8** — J0 = playsim headless + sprites donneur = la sonde la moins chère ; compteur de murs sautés à ajouter (1 ligne) |

## 2. Réfutations (vérifiées dans le code)

1. **D1 #27 « `mem_free` LIFO du niveau précédent »** — faux mécanisme. `runLevel` commence par `mem_init()` `[src] SRUINS.C:1869`, qui remet les deux piles à `mem1Start`/`mem2Start` `[src] UTIL.C:347-354` ; rien n'est libéré pièce à pièce. Conséquence non vue par les trois plans : un bloc zone Doom pris par `mem_malloc(0, 512 Ko)` **dans** `runLevel` disparaît à chaque niveau ; il doit être pris **avant** `mem_lock()` (appelé par `dlg_init`/`loadLocalText`, « locks memory » `[src] SRUINS.C:2415-2422`, `mem_lock` `[src] UTIL.C:356-359`) ou vivre en `.bss`. Doom garde `lumpinfo`, `textureheight`, `sprites[]`, `S_sfx[].driver_data` en PU_STATIC entre niveaux.

2. **R2 §9 / D3 #4 « pile LIFO 8 niveaux ⇒ 8 allocations vivantes max par aire »** — inexact. `stackPos=(stackPos+1)&(STACKSIZE-1)` est un **anneau** `[src] UTIL.C:373-374` : `loadLevel` fait à lui seul 15 `mem_malloc(1,…)` `[src] LEVEL.C:42-66` sans jamais échouer. La limite ne porte que sur `mem_free` (les 8 derniers). Pas une contrainte pour allouer la zone.

3. **R3 §5 « `MAXNMSECTORS` non vérifié au chargement → corruption silencieuse »** — faux en build assert : `assert(level_nmSectors<=MAXNMSECTORS); assert(level_nmWalls<=MAXNMWALLS)` `[src] SRUINS.C:1982-1983`. Vrai seulement sous `NDEBUG=1`. D2 l'avait corrigé (lignes citées 1988-1989, réelles 1982-1983).

4. **R3 #5, D1 #7, D2 #1, D3 #7 « `open_doors()` `doom3d.py:479-512` »** — `open_doors` est à `doom3d.py:589` ; 476-500 est le bouchon plein de `emit_edge` (« ouverture fermée … bouchon plein »). La fonction existe, la ligne est fausse dans les quatre documents.

5. **R3 #6, D1 #8 « `light_of` `doom3d.py:137-149` »** — `light_of` est à `doom3d.py:182` ; 137-149 est `snap_poly`.

6. **R5 §7 « NBG0 … priorité 6 sous les sprites »** — faux, et la correction de D1 est incomplète. `SCL_SetPriority(SP0..SP7,7)` puis `NBG0,6` puis `SP0,4` `[src] SRUINS.C:1069-1073`. Un sprite VDP1 dont le mot couleur n'a pas de bits de priorité (murs RGB16, sprites 8 bpp `light<<8` `[src] WALLS.C:2840`, chunks d'arme `overlay=0` `[src] SEQUENCE.C:304`) tombe en SP0 = 4 **sous** NBG0. Mais la barre d'état est émise avec `0x4000` `[src] SRUINS.C:1322` et l'arme « overlay » (`sequence>=50`) aussi `[src] SEQUENCE.C:248-249` → type 1, bits 15-13 → SP2 = 7 **au-dessus** de NBG0. Le levier « VDP1 par-dessus la 2D » existe déjà et aucun plan ne le nomme.

7. **D1 §5 « FOV du moteur vs 90° Doom : inconnue »** — connu : `#define FOCALDIST 160` `[src] WALLS.H:4` avec `EZ_localCoord(160,120)` `[src] SRUINS.C:2127` ⇒ demi-largeur 160 à focale 160 ⇒ **90° exactement** = Doom. Rien à mesurer.

8. **D1 #17 « cadre 224 lignes = `SCL_224LINE` + `localCoord(160,96)` + sysClip, 0,5 j »** — `SCL_224LINE` existe `[src] sega_scl.h:350` (D1 a raison là-dessus), mais le clip 3D est câblé en constantes `XMIN -160, YMIN -110, XMAX 160, YMAX 90` `[src] WALLS.C:128-131`, relues par `drawSprites` (restauration FOOTCLIP `[src] WALLS.C:2851-2854`), par le bbox ciel `[src] WALLS.C:2474-2481` et par `MAP.C`. La fenêtre 3D actuelle est donc **rangées 10..210 = 200 lignes** en mode 240 — le moteur est déjà un « vue 200 + HUD 30 » ; passer en 224 exige de toucher ces défines et le clip d'arme `0..320×0..210` `[src] SEQUENCE.C:254-260`.

9. **R2 §3 / R3 §5 « `newSprite` … assert liste libre »** — l'assert est commenté : `/* assert(freeList); */ … if (!freeList) return NULL;` `[src] SPRITE.C:71-75`. Au 451e sprite, `newSprite` rend **NULL en silence** ; le hook `P_SpawnMobj` de D1 #11 et `wl_puppet` de D2 doivent traiter un mobj sans marionnette, pas compter sur un plantage.

10. **R2 §6 / R4 §7 / D3 #40 « voix Duke longues coupées ou streamées »** — le plafond dur n'est dit nulle part : `SoundRec.size` est `unsigned short` `[src] SOUND.C:36-37`, `sounds[nmSounds].size=size` `[src] SOUND.C:213` tronque tout PCM > 65 535 o, et le registre de fin de boucle est 16 bits `[src] SOUND.C:337-340`. Doom : max DSBAREXP 18 600 o `[mesuré]` — sûr. Duke : BONUS.VOC 268 Ko et toute voix > 64 Ko sont **inchargeables** telles quelles.

11. **R2 §2 « `getFacingAngle` … bords à ±22,5° »** — ±23° `[src] AICOMMON.C:79-99` (D3 a la bonne valeur). Doom : `ANG45/2` = 22,5° `[src] r_things.c:870`. Écart de 0,5° sur les frontières de rotation : cosmétique, mais D1 écrit que les 8 facings sont « à l'identique ».

12. **D1 §1.3 « `pos = (x>>16, viewz>>16, y>>16)` sans échelle »** — vrai pour la caméra, insuffisant pour les marionnettes : `newSprite` pose `o->scale=48000` `[src] SPRITE.C:86` et `drawSprites` projette `width64 = (o->scale·FOCALDIST/z)>>10` `[src] WALLS.C:2716-2722` ⇒ à 1:1 un chunk de 64 texels ne fait 64 unités que si `o->scale = 65536`. Ni D1 ni le `wl_puppet` de D2 ni le `host_puppet` de D3 ne posent `scale`. De plus les pieds sont `pos.y − radius` `[src] WALLS.C:2698-2699` : `pos.y = z + radius`, pas `z`.

13. **R4 §5 / D3 R-2 « seule écriture de normale : `SRUINS.C:1958` »** — ce bloc est sous `#if 0` `[src] SRUINS.C:1952-1961`. Le moteur n'écrit **jamais** une normale à l'exécution (plus fort que dit) ; `wall->d` n'est relu que par `HITSCAN.C:89` — `findFloorDistance` et `pointInSectorP` recalculent le plan depuis `v[0]` `[src] UTIL.C:53-67, SPRITE.C:135-138`. La translation verticale des portes est donc sûre pour tout sauf le hitscan moteur, que Doom n'utilise pas.

14. **R1 §1b « animdefs : 9 flats + 14 textures »** — 13 textures `[src] p_spec.c:108-122` (R3 avait 13).

15. **D1 « E1M1 : 138 things [mesuré] »** — 138 enregistrements THINGS, dont 14 réservés au multi (bit 16) ; 124 en 1p, **115 mobjs** hors starts au skill 4 `[mesuré]`. R1 (124) et R5 (115) comptent juste ; D1 surcompte les vivants d'environ 20 %.

16. **R4 §4 « 8 directions : `k=((ang+3072+128-a)&2047)>>8` »** — incomplet : `&7` puis `if(k>4) k=8-k` `[src] game.c:5196-5199` ⇒ Duke stocke **5 vues + miroir**, pas 8. Le convertisseur `duke_sprites.py` (D2 #31, D3 #37) doit émettre 5 séquences + flag bit0, pas 8.

17. **D2 « `MAXNMSECTORS/MAXNMWALLS` assertés après `initWallRenderer/initMap` `[src SRUINS.C:1988-1989]` »** — vrai sur le fond, lignes 1982-1983 ; et `initSpriteSystem()` est à 1984, `placeObjects()` à 1994 (D1/R2 citent juste).

18. **D3 R-3 « tri O(n²) par secteur »** — tri par insertion avec `break` `[src] WALLS.C:2612-2629` : O(n²) au pire, ~O(n) quand la liste est déjà ordonnée, ce qui est le cas courant (la liste du secteur change peu d'une frame à l'autre). Le coût réel est dans les 2-3 `MTH_CoordTrans` et le `findFloorDistance` par sprite, que `NOSHADOW` supprime `[src] WALLS.C:2734-2764`.

## 3. Confirmations dont le plan dépend

| Affirmation | Preuve |
|---|---|
| Slots de cache `{28,31,1,10,12}` ; une tuile utilisée **dans la frame** est évictible (`lastUse<frameCount+1`) sauf `PICFLAG_LOCKED` | `[src] SRUINS.C:1903`, `PIC.C:262-272`, `PIC.C:47` |
| Budget esclave : mur **sauté en silence** si `height*width+nmSlavePolys+50>1300` ; idem faces | `[src] WALLS.C:1336, 1374, 1555` |
| `moveSpriteTo` = délier/relier O(n) + `pos=*newPos`, sans physique ; `assert(camera->sequence==-1)` ; `SPRITEFLAG_INVISIBLE` saute le sprite ; `drawList[100]`/secteur | `[src] SPRITE.C:916-938`, `WALLS.C:2572, 2591-2594` |
| `light=0` forcé, `FLASH` → banque 5, banque = `light<<8` dans le mot couleur ; `setSectorBrightness(s,level)` écrit `level_vertexLight` (parallélogrammes) ou `level_vertex[].light` (mesh) | `[src] WALLS.C:2705-2712, 2840`, `AICOMMON.C:50-70` |
| `getFacingAngle` 0..7 ; `setSequence` = base + facing si bit 15 | `[src] AICOMMON.C:72-100, 146-158` |
| Arme : `EZ_normSpr(flip, COLOR_4…, overlay, mapPic(tile), (xo−160+chunkx, yo−120+chunky))`, `assert(TILE8BPP)`, horloge 2 vblanks/frame, clip 0..320×0..210 | `[src] SEQUENCE.C:216-218, 254-260, 296-305` |
| Bloc séquences = en-tête + séquences + frames(8 o) + chunks(8 o) + **227 shorts** de carte, `sequence[0]==0` ; armes sans carte, `wSequence[0]==0` | `[src] SEQUENCE.C:45-48, 80`, `:126`, `SLEVEL.H:211-227` |
| Tuile 0x6A : `palNm` lu et ignoré, `size` short, RLE en aire 0 (LWRAM) ; tuile 0x32 = 4 096 o en aire 1 (HWRAM), palette expansée au `map()` ; `MAXNMPICS 1600` (MIPMAP=1) doublés par `createMippedPics` ⇒ 800 de base | `[src] PIC.C:24, 30, 423-433, 505-528, 565-575` |
| Palettes : banque 0 objets (entrée 255 = 0xffff), 1-4 = −i/canal, 5 = blanc flash ; banque 6 arme VDP2 ; banque 7 ciel | `[src] PIC.C:629-659`, `SEQUENCE.C:269-286`, `PLAX.C:84-86` |
| Ciel : 256 u16 → banque 7, 512×256 assertés, K-table 320×4 → A0 ; `movePlax` : 128 px/45°, `y=−pitch·128/45−100` | `[src] PLAX.C:25-53, 83-118` |
| Son : `MAXNMSOUNDS 80` total, `assert(soundTop+size<512 Ko)`, `initSound` remet `soundTop=0`, 68K coupé par SMPC (`*SMPC_COM=7`), slots 16/17 réservés, dédup 1 son/frame, `vol=(dist>>5)−15` | `[src] SOUND.C:40, 148-153, 164-165, 218, 63-65, 326-328, 392-397` ; `MEGAINIT.C:210-221` (`nop;bra` en 0x400) |
| `fs_open` mono-handle (`assert(!openCDFile)`), aucun `fs_seek`, lecture par secteur de 2 048 o, préfetch 500 secteurs | `[src] FILE.C:153-163, 194-254` |
| Allocateur : aire 0 = 0x200000-0x300000, aire 1 = `&end`-0x6100000, repli automatique sur l'autre aire ; `_end`=0x06096878 ⇒ **432 008 o** ; `.text` 232 740, `.bss` 305 768 | `[src] UTIL.C:344-353, 380-386`, `[mesuré] MAIN.map:466, 2207, 2636` |
| `mem:%dk+%dk=%dk` imprime les deux aires ; `used/vswaps` sous `#ifndef NDEBUG` **et** `STATUSTEXT` | `[src] SRUINS.C:2240-2241, 2255-2262` |
| `Sprite` 72 o × 450 ; `Object` 144 o × 350 ; `sectorDraw` 600×60 ; `doorwayCache` 5 500×12 aliasé `slaveResult` ; `mystack` 20 384 | `[src] SPRITE.H:43-59`, `SPRITE.C:12`, `OBJECT.H:29-35`, `OBJECT.C:9`, `WALLS.C:82-97, 1330-1349`, `UTIL.C:28-29` ; `[mesuré] MAIN.map` |
| `SaveState` 96 o, `SaveRec` 100 o, 6 slots, 10 blocs ; backup interne 32 Ko | `[src] GAMESTAT.H:54-65`, `BUP.C:17-21, 40` ; `[doc] HW_BACKUP_BUP.md:275` |
| `vtimer++` et `processInput` à vblank-OUT ; 1er pad seulement (`break`) ; A+B+C+Start ⇒ `SYS_EXECDMP` ; `PER_LInit(…,6,…)` | `[src] V_BLANK.C:45-91, 151-155, 234` |
| Trou logique 2137-2191 ; monde mobile 2197-2201 ; gouverneur 2277-2300 ; `drawWallsFinish` spin FRT + `slaveSize` ±1 | `[src] SRUINS.C`, `WALLS.C:2448-2459` |
| Doom : cap `>= 9` ; `SCREENHEIGHT 224` ; `P_UpdateSpecials` (8 tics, special 48) ; `P_ChangeSwitchTexture` mute `sides[]` ; `textureheight` lu par `p_floor.c:377-386` ; rotation `>>29` ; `FF_FULLBRIGHT → colormaps` ; `ANGLETOSKYSHIFT 22` ; `fixedcolormap` 32/1 ; bob psprite ; `deh_*.h` seuls ; `fopen` → `I_Error` en sauvegarde ; `_open` ENOENT ; `_sbrk` 4 Ko ; `doom_stack` 24 Ko ; MUS slots 8-22 | `[src] d_loop.c:207`, `i_video.h:28`, `p_spec.c:1112-1136`, `p_switch.c:223-247`, `r_things.c:870, 940`, `r_sky.h:29`, `p_user.c:356-378`, `p_pspr.c:321-324`, `g_game.c:1774-1785`, `syscalls.c:120-160`, `main.cxx:56`, `i_sound_saturn.cxx:18, 135` |
| jfduke3d : `TICRATE 120`, `TICSPERFRAME 120/26` = 4 ⇒ 30 Hz ; GPL-2+ ; 113 mots-clés ; `parse` 2128-3193 ; `execute` 3194 ; `clipmove` 9131, `pushmove` 9454, `getzrange` 9703, `hitscan` 8652, `cansee` 8602, `updatesector` 9590 ; `buildinf.txt` 882 l. ; jfbuild BUILDLIC ; NBlood GPL-2 seule | `[src] duke3d.h:100-101, 164`, `game.c:7-10`, `gamedef.c:63-193`, `engine.c`, `jfbuild/LICENSE:1-4`, `NBlood/…/actor.cpp:7-9` |
| `[mesuré]` : 55 DS* = 535 127 o (> 524 288) ; PLAYPAL[0] noirs {0, 247} ; STBAR 320×32 sans index 0 ; E1M6 606 SSECTORS/250 secteurs/1 727 sidedefs ; E1M1 237/85/648 ; 483 sprites = 612 chunks ; 30 psprites = 95 chunks/149 952 o ; teintes moyennes PLAYPAL 8 = (+101,−88,−77), 12 = (+37,+43,−9), 13 = (−17,+19,−10) | `j1_wadcheck.py` |

## 4. Manques (aucune proposition ne le couvre)

- **a. Persistance de la zone Doom** — voir §2-1. La vraie question n'est pas « 512 Ko en LWRAM » mais « alloué avant `mem_lock()` ou pas ». Sinon `Z_Init` par niveau + re-création de tout le PU_STATIC de Doom (tables, `W_CacheLumpNum` épinglés, `driver_data` des sons).
- **b. Sommets partagés vs lumière par sommet** — R5/D3 comptent −70..−90 Ko en partageant les sommets `.LEV`, mais `sVertexType.light` est **par sommet** `[src] SLEVEL.H:113-116` et `setSectorBrightness` l'écrit par secteur `[src] AICOMMON.C:63-67` ; un sommet partagé entre deux secteurs prend la lumière du dernier écrivain. De plus `normTransform(level_vertex+wall->firstVertex,…)` transforme une **plage contiguë** par mur `[src] WALLS.C:1207-1208` : le partage n'est possible que par `v[4]` des parallélogrammes, pas par les faces mesh. Le gain est `[est]` et en conflit avec D1 #8 / D3 #10.
- **c. Débordement de l'index de tuile** — `level_texture[i]+=tileBase` sur un `u8`, `sFaceType.tile` `u8` `[src] LEVEL.C:69-72, SLEVEL.H:118-121`. D2 le chiffre (95 tuiles d'armes ⇒ ≤ 160 tuiles de géométrie) ; D1 #16 met les 95 chunks de psprites en STATIC **et** vise E4.1c à 141-170 tuiles : 95 + 170 = 265 > 255 ⇒ **retour à zéro silencieux** sur E1M6/E1M8 en E4.1c. Il faut choisir : psprites dans le `.LEV` (LWRAM, index short des chunks) ou plafond E4.1b.
- **d. `newSprite` NULL silencieux** — §2-9. E1M6 a 439 mobjs vivants + transitoires `[mesuré]` contre 450 ; D1 monte à 650 (+14 Ko), D2/D3 aussi, mais aucun ne traite le NULL.
- **e. Plafond 64 Ko par échantillon** — §2-10 ; tue `voc2snd.py` (D2 #32, D3 #40) sur les voix longues sans découpe en deux sons chaînés (le moteur n'enchaîne pas).
- **f. `tformed.z < F(32)` ⇒ sprite non dessiné** `[src] WALLS.C:2701` : tout thing à moins de 32 u de l'œil disparaît (Doom dessine jusqu'au contact — cadavres sous les pieds, barils collés). `NEAR_CLIP` de `doom.cfg` est à 10 pour les murs, la constante sprite reste 32.
- **g. Les sondes des jalons n'existent qu'en build assert** — `used/vswaps` sous `#ifndef NDEBUG` `[src] SRUINS.C:2255`, `extraStuff` (son) idem, `validPtr` dans `map()` `[src] PIC.C:257`. J0/J1 de D1 et D3 mesurent donc sur un build plus lent que celui livré ; la loi 14,9 ms + 39,2 µs/cellule doit être rattachée au build qui l'a produite (STEXT_BASELINE ne le dit pas dans ce qui a été relu).
- **h. Dédup son 1/frame** — `playSoundE` refuse un même son deux fois par frame `[src] SOUND.C:326-328` ; Doom lance couramment 2-3 `DSPOSIT`/`DSPISTOL` le même tic (plusieurs zombies). D1 (backend Mimas) l'évite ; D3 #21 (`playSoundE`) ne le dit pas.
- **i. `abcResetEnable`** — A+B+C+Start = reset console `[src] V_BLANK.C:79-83` ; avec A = tir, B = use, C = courir, Start = menu (R6 §2) un joueur peut le faire en jeu. Une ligne (`abcResetEnable=0`) qu'aucun plan n'écrit.
- **j. Levier VDP1 au-dessus de NBG0** — §2-6 : `0x4000` dans le mot couleur ⇒ SP2 = 7. Résout la crainte « W0 unique = pas d'arme VDP2 » de D2/D3 par un autre chemin (HUD en NBG0, arme VDP1 par-dessus si besoin) sans toucher la VRAM.
- **k. `SectorDrawRecord` n'a pas de champ `processed`** (D1 §1.3) — c'est `flags & SDFLAG_NEEDTOPROCESS` `[src] WALLS.C:82-97` ; la logique tient, le nom est inventé.
- **l. Split-screen** — D1 #35 (4 j) suppose « 2 passes `drawWalls/Finish` » ; `drawWalls` lit la globale `camera` `[src] WALLS.C:2430, 2591`, un seul `sectorDraw[]`, un seul kick esclave `[src] WALLS.C:2421`, `W0` unique. Deux rondes esclave par frame + deux caméras + `movePlax` unique : le 4 j n'a aucune base. D3 (« hors plan ») est le seul honnête.

## 5. Synthèse recommandée

**Ossature : D2** (pipeline + disque + `.LNK` sidecar + `.PSW` par carte + validateurs) — c'est la seule des trois dont chaque brique a une preuve de format vérifiable hors console (`lev_write` sha1-exact, `fs_open` mono-handle contourné par un 6e fichier, plafond `tileBase` chiffré). **Greffes de D1** : la boucle hôte détaillée (§1.2 avec `pic_nextFrame` gardé), la table de synchro champ par champ, la 2D = feuille NBG0 avec LUT 0→247, les 13 teintes mesurées, le backend son Mimas en C (évite la dédup §4-h), CDDA. **Greffes de D3** : J0 = playsim headless + 25 sprites **donneur** (aucun convertisseur à écrire), les 8 kill criteria, le compteur de murs sautés au `return` de `WALLS.C:1374`, D0 clean-room Duke lancé maintenant sur PC (hors moteur), et la question juridique des structs avant tout commit.

**Corrections à inscrire avant J0** : (1) la zone Doom est allouée **avant `mem_lock()`** et mesurée dans `mem:` ; (2) `o->scale=65536`, `pos.y=z+radius`, NULL de `newSprite` géré ; (3) psprites dans le `.LEV` ou plafond 160 tuiles de géométrie — décision owner ; (4) `XMIN/YMIN/XMAX/YMAX` + clip arme pour 224 lignes ; (5) `abcResetEnable=0` ; (6) sommets partagés **interdits** pour les secteurs à lumière animée tant que §4-b n'est pas tranché ; (7) Duke : 5 vues + miroir, découpe des VOC > 64 Ko.

**Ordre des jalons** : J0 (D3, 4 j : `mem:`, `tic`, `draw`, `vswaps`, murs sautés, en build **STATUSTEXT et NDEBUG** pour avoir les deux chiffres) → J1 D2 « densité » (sprites + sons Doom vrais, 31 slots) → J2 D2 « portes au chronomètre » (géométrie fermée + `.LNK`, sans playsim) → J3 D1 « E1M1 se joue » → J4 D2 « disque shareware » → J5 Doom II/PWAD/DEH → Duke D1-D5 en parallèle à partir de J2.

**Première mesure** : `mem_coreleft(0)+mem_coreleft(1)` (`mem:`) après `runLevel` a chargé `TOMB_e1m1.LEV` **plus** un `mem_malloc(0, 512 Ko)` factice pris avant `mem_lock()` — c'est le seul chiffre qui décide entre le plan B et le plan C de R5, et il coûte deux lignes.

Copie identique : `C:/Users/pcico/AppData/Local/Temp/claude/c--Users-pcico-Projects-Mimas/3f9e95f7-bb08-41a7-9eb9-5ec71ed92341/scratchpad/reports/J1_exactitude.md` ; script `j1_wadcheck.py` à côté. Aucun fichier de dépôt modifié.

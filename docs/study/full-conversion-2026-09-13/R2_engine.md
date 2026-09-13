# R2 — Le moteur SlaveDriver comme HÔTE d'un playsim étranger (Doom / Duke)

Lecture seule du fork `C:/Users/pcico/Projects/SlaveDriver-Engine` (sources `*.C/*.H`, `build/stext/MAIN.map`), 2026-09-13. Étiquettes : `[src]` lu dans le code, `[mesuré]` calculé ce jour (script `r2_wadstat.py` sur `Mimas/cd/data/DOOM1.WAD` strippé — les `DS*` et sprites y sont conservés, seuls `DP*`/GENMIDI/DMXGUS sont retirés `[src tools/strip_wad.py:9-11]` ; arithmétique sur les structs et le `.map`), `[doc]` doc existante, `[est]` estimation. Ne refait pas PORTING_NOTES / HW_USAGE_VS_MIMAS / MULTIPLAYER_STUDY §1-2 ni DOOM_ON_SLAVEDRIVER (architecture C = playsim Doom + `.LEV` dérivé + table de liaison) : ce rapport dit **ce que le moteur OFFRE à ce playsim et ce qu'il faut contourner**, brique par brique.

**Verdict en trois lignes.** (1) Le moteur est un **hôte propre** : la boucle de frame a déjà un « trou » où la logique de jeu tourne pendant la fenêtre esclave (`SRUINS.C:2137-2191`), le `Sprite` est pilotable en marionnette par `moveSpriteTo` + 4 champs, et les 8 facings de Doom existent déjà (`getFacingAngle` retourne 0..7 par secteurs de 45° décalés de 22,5°). (2) Les vrais murs sont **des budgets pleins** : VDP1 VRAM à 98,6 % (7 424 o libres avant fontes), CRAM 8/8, 31 slots de tuiles 8 bpp pour tous les things + l'arme, SCSP 512 Ko < 535 Ko de sons shareware, backup 32 Ko ≪ une sauvegarde Doom. (3) Le 2D (menus/HUD/intermissions) a **trois voies déjà démontrées dans le moteur** (bitmap VDP2 8 bpp type écran-titre ; char VDP1 plein écran type inventaire ; rectangle du sheet NBG0 fenêtré) — le choix se fait sur la VRAM VDP2 et sur W0, pas sur la faisabilité.

---

## 1. La boucle de frame (`runLevel`, `SRUINS.C:1847-2345`)

| étape | fichier:lignes | ce que ça fait | verdict hôte Doom |
|---|---|---|---|
| Horloge | `V_BLANK.C:151-154` (`vtimer++` à vblank-OUT, 60 Hz NTSC) ; `V_BLANK.C:41-43,238-239` (`htimer++` par hblank, **STATUSTEXT seulement**) | Pas de pas fixe global : `framesElapsed=vtimer` au `SCL_DisplayFrame` (`SRUINS.C:2302-2303`), plafonné 8 (`:2102`) | **Tel quel** : `I_GetTime()=vblanks*35/60` ; Doom découple déjà tics et frames |
| Entrée | `V_BLANK.C:45-91` (`processInput` à vblank-out : ET logique de tous les pads, `break` au 1er, file `inputQ[16]` `:89-90`, `lastInputSample`, reset A+B+C+Start `:79-83`) | 1 échantillon/vblank, bits actifs à 0 | **À adapter** : `G_BuildTiccmd` lit `inputQ` (35 Hz) ; ouvrir la boucle `:58-78` par port pour le MP |
| Cadence joueur | `movePlayer(inputEnd,framesElapsed)` `SRUINS.C:2137`, `:857-1045` : rejoue **un échantillon `inputQ` par vblank** (`:864-868`), `moveCamera()` `:1039` | Pas fixe 60 Hz avec rattrapage ≤ 8 | **Retirer** (remplacé par `P_PlayerThink` à 35 Hz) |
| Cadence objets | `monsterMoveCounter+=vtimer` `:2302`, cap 8 `:2139`, `for(;>1;-=2) runObjects()` `:2141-2150` ; `runObjects`=`signalList(objectRunList,SIGNAL_MOVE)` `AICOMMON.C:454-457` | IA à **30 Hz** fixe, ≤ 4 pas/frame | **Retirer** (remplacé par `TryRunTics`, 35 Hz) |
| Caméra | `MTH_PushMatrix/RotateZ/X/Y/Move(-camera->pos, +playerHeightOffset)` `:2105-2118` | yaw/pitch/roll en `F(deg)` ; y = haut | **Tel quel** (pitch=roll=0, `playerHeightOffset` ← `viewz-z`) |
| Ouverture VDP1 | `EZ_openCommand; sysClip; userClip(plein); localCoord(160,120)` `:2120-2127` | | **Tel quel** |
| `drawWalls(view)` | `WALLS.C:2237-2445` : traversée portails depuis `sectorDraw[camera->s]` `:2251-2258`, arbre, tri, **kick esclave** `:2421`, secteurs lointains + leurs sprites `:2423-2430`, sprites des secteurs esclaves liés en `JUMP_RETURN` `:2433-2444` | maître seul pour les sprites | **GARDER TEL QUEL** |
| **Trou logique** | `:2137-2191` : movePlayer, runObjects, `stepColorOffset` `:2152`, powerups NBG0 (`SCL_SetColOffset(OFFSET_B,NBG0)` `:2161-2188`) | tourne pendant que l'esclave transforme | **= emplacement de `TryRunTics()`** |
| `drawWallsFinish()` | `WALLS.C:2448-2483` : spin FRT `:2452`, équilibrage `slaveSize` ±1 `:2455-2459`, ré-émission des records `drawSlaveWalls` `:2460`, `updateLights` `:2464`, union bbox ciel | | **Tel quel** |
| Monde mobile | `advanceWallAnimations/stepWater` `:2197-2198`, `updatePushBlockPositions` `:2200` (déplace `level_vertex[].y`, `SPRITE.C:852-890`), `processDelayedMoves` `:2201` | **c'est ICI qu'on écrit la géométrie**, hors fenêtre esclave | **Remplacer** par la passe de synchro (hauteurs, tuiles commutées, mobj→Sprite) |
| Automap | `drawMap` `:2204`, `MAP.C:116-263` | `EZ_line` COLOR_5 par mur, `userClip` `:127-130` | **À adapter** (mêmes primitives, liste de `line_t` Doom) |
| Arme | `runWeapon(framesElapsed,…)` `:2207` | §4 | **Remplacer** (psprites) |
| 2D | `drawMessage/drawStatBar/drawAirMeter` `:2211-2214` | §5 | **Remplacer** |
| Compteurs | `sound_nextFrame` `:2250` (dédup son/frame), `pic_nextFrame` `:2253` (**horloge LRU du cache**) | | **GARDER** (sinon le cache tuiles n'évince plus correctement) |
| Clôture | `EZ_closeCommand; SPR_WaitDrawEnd` `:2273-2274` (stall unique) | | **Tel quel** |
| Gouverneur | `:2277-2300` : `smoothVTime∈{1,2}`, hystérésis 10 frames `:2278-2288`, `while(vtimer<smoothVTime)` `:2290`, `SCL_DisplayFrame` `:2293` (mode `0xfffe` = erase+swap VBE, `SCL_VBLV.C:52-58,100-108`) | verrou 60/30, libre au-delà | **Tel quel** |
| Ciel / VDP2 | `movePlax(yaw,pitch)` `:2308`, fenêtre W1 = bbox ciel `:2315-2317`, `updateVDP2Pic` `:2318` | §8 | Tel quel / à retirer si sheet abandonné |
| Sorties | mort, chameau, inventaire (`runInventory` `:2336`), quit `:2322-2345` | codes retour 1..6, 100+, 200+ | **Remplacer** par `gamestate` Doom |

**Fait clé** : la seule chose que le moteur impose, c'est que **`camera->s` soit le secteur `.LEV` de l'œil** (`WALLS.C:2251-2253`) et que **la géométrie ne bouge qu'entre `drawWallsFinish` et le `drawWalls` suivant** (l'esclave lit `level_vertex` via l'alias non-caché pendant sa fenêtre).

---

## 2. Rendu des sprites (`drawSprites`, `WALLS.C:2570-2857`)

| aspect | preuve | conséquence Doom |
|---|---|---|
| Sélection | `frame=o->frame+level_sequence[o->sequence]` `:2778` ; chunks `level_frame[frame].chunkIndex..+1` `:2779-2781` ; `sChunkType{chunkx,chunky,tile,flags}` `SLEVEL.H:221-226` | une frame = N tuiles 64×64 / 32×32 placées ; taille arbitraire (max Doom 154×151 = 6 chunks `[mesuré]`) |
| Rotations | **Une vue par sequence** ; l'angle choisit la *sequence* : `setSequence` `AICOMMON.C:146-158` (`seqBase&0x8000` → `+getFacingAngle`), `getFacingAngle` `:72-100` retourne **0..7** (secteurs de 45°, bords à ±22,5°) | **= les 8 rotations Doom, à l'identique.** Le miroir A2A8 = même tuile + flag LR (`flags&1` → `DIR_LRREV` `:2785-2788`) ; appelé au dessin via `SIGNAL_VIEW` `:2696` — pour une marionnette, calculer le facing dans la passe de synchro |
| Flags de dessin | `SPRITE.H:17-40` : `NOSHADOW`, `FLASH` (1 frame), `INVISIBLE` (non listé `:2592-2594`), `NOSCALE` (`scale=65536` `:2719-2720`), `FOOTCLIP` (userClip jusqu'aux pieds `:2767-2775`, restauré `:2847-2855` = 2 cmds), `LINE/THINLINE` (laser : polygone/`EZ_line` `:2652-2694`), `COLORED` (gouraud `o->color` `:2819`) | fullbright / spectre / clipping des pieds dans l'eau couverts |
| Lumière | **`light=0` toujours** `:2708` (la version par distance est commentée `:2705-2707`) ; `FLASH` → `light=NMOBJECTPALLETES` (=5) `:2710-2712` ; 8 bpp : `color=light<<8` `:2840` = **banque CRAM** `COLOR_4` ; 16 bpp : gouraud `greyTable[16-light*2]` `:2824` | la lumière par sprite EST câblée : banque CRAM 0..5 → mapper `sector.lightlevel`(+distance) sur N banques COLORMAP ; frames « bright » Doom = banque 0 |
| Palettes | `loadPalletes` `PIC.C:611-668` : banque 0 objets `:634`, banques 1-4 = −1..−4/canal `:640-656`, banque 5 = blanc flash `:658-665` ; banque 6 = palette arme VDP2 `SEQUENCE.C:272-287` ; banque 7 = ciel `PLAX.C:87` → **8/8** | pour Doom : banque 0 = PLAYPAL (partagée sprites/ciel/2D/arme), 1-6 = 6 niveaux COLORMAP, 7 libérée si le ciel passe en PLAYPAL |
| Transparences VDP1 | ombre au sol `COLOR_4\|COMPO_SHADOW` sur `mapPic(0)` `:2762` ; `COMPO_TRANS` (laser `:2680`, automap `MAP.C:204`) ; `COMPO_HARF` (menus `MENU.C:254`) ; `DRAW_MESH` (jauge d'air `SRUINS.C:1576-1580`) | Spectre = dessiner la frame en `COMPO_SHADOW` (assombrit sa silhouette) ou `MESH` ; pas de translucidité nécessaire en Doom 1 |
| Tri / occlusion | par secteur : ≤ **100** sprites (`drawList[100]` `:2572`, `nmDraw<100` `:2594`), insertion loin→près `:2611-2628` ; clip = `userClip` bbox du secteur posé avant `drawSector` `:2424-2428` ; sprites des secteurs esclaves émis tôt et re-liés par `JUMP_RETURN` `:2433-2444` | ordre peintre secteur par secteur (pas de z par pixel) ; un thing chevauchant deux pièces convexes n'est dessiné que dans **son** secteur (`sectorSpriteList[o->s]`) → clip visible sur les bords : **question console n°2** |
| Coût / sprite | 1 cmd/chunk + 1 ombre + 2 clips (FOOTCLIP) + `findFloorDistance` `:2743` + `MTH_CoordTrans` ×2-3 + `signalObject` ; tri O(n²) par secteur | 20-40 monstres Doom visibles → **maître seul** (esclave = murs) : mesure console obligatoire |

**Contrainte 8 rotations + fullbright** : zéro contrainte moteur (sequence par facing, banque 0). La contrainte est **mémoire** : 445 frames de things `[mesuré]` → 502 chunks 64×64, 860 587 px (≈ 840 Ko en 8 bpp brut, RLE en LWRAM `PIC.C:568-580`) — sous-ensembler par niveau (DOOM_ON §4.4).

---

## 3. Le modèle Sprite / Object

| brique | fichier:lignes | état | réutilisable | contrainte chiffrée |
|---|---|---|---|---|
| `Sprite` | `SPRITE.H:42-59` : pos, vel, radius/radius2, angle/scale/frame, next, friction/gravity, `Object *owner`, flags, `s`, `sequence`, `floorSector`, `color` | **72 o** `[mesuré]` × `MAXNMSPRITES 450` `SPRITE.C:12` = 32 400 o (.bss `SPRITE.o` 0x882a = 34 858 o `[mesuré MAIN.map:2449]`) | **tel quel** en marionnette | 450 sprites ; Doom E1M1 ≈ 150 mobjs, cartes denses 300-400 `[est]` → à surveiller, ou monter la constante (+72 o/unité) |
| Appartenance | `sectorSpriteList[MAXNMSECTORS]` `SPRITE.C:19`, chaînage `o->next`, `o->s` | listes simples par secteur `.LEV` | tel quel | 600 secteurs |
| Créer / détruire | `newSprite(sector,radius,friction,gravity,sequence,flags,owner)` `SPRITE.C:68-101` (owner **peut être NULL** : `drawSprites` teste `o->owner` `:2695,2725`), `freeSprite` `:103-117` (O(n) sur la liste du secteur) | free-list | tel quel | — |
| **Déplacer** | **`moveSpriteTo(o,newSector,&pos)`** `SPRITE.C:916-938` : délie/relie + `pos=*newPos`, **sans physique** | | **= l'API marionnette** | 1 appel/mobj/tic ; secteur = `link[R_PointInSubsector]` |
| Physique/IA | `moveSprite/moveCamera` `:902-914`, `collideSprite`, `spriteAdvanceFrame` `:775-791` (joue `level_frame[].sound`) | | **ne pas appeler** | — |
| Piloter une marionnette | écrire `pos`, `angle` (`F(deg)`), `sequence` (base+facing), `frame`, `scale` (48000 défaut `:86`), `flags` (`NOSHADOW`, `FLASH`, `INVISIBLE`, `FOOTCLIP`), `radius` (pieds = `pos.y-radius` `:2698-2700`) | | tel quel | `owner=NULL` ⇒ pas d'autoaim ni `SIGNAL_VIEW` |
| `Object` | `OBJECT.H:30-35` : type, class, next/prev, `messHandler func`, `pad[128]` = **144 o** × `MAXOBJECTS 350` `OBJECT.C:9` = 50 400 o (`OBJECT.o` 0xc4ec `[mesuré :2416]`) ; listes run/idle/free `:13-15` ; `SIGNAL_*` `OBJECT.H:6-15` ; `placeObjects` `OBJECT.C:197-422` (227 `OT_*` `[mesuré SLEVEL.H]`) | framework PowerSlave | **à contourner** : Doom a `mobj_t`/thinkers ; `initObjects` inutile → **−50 Ko de .bss** | `level_object[]` du `.LEV` peut rester vide (Doom lit THINGS) |
| Caméra | `camera` = `player->sprite`, `sequence=-1` **assert** `WALLS.C:2591` | | créer un `Sprite` joueur `sequence=-1`, `camera->s` synchronisé chaque tic | — |

---

## 4. L'arme (`WEAPON.C`, `SEQUENCE.C:136-318`)

| aspect | preuve |
|---|---|
| Données | `STATIC.DAT` : `loadWeaponTiles` `PIC.C:702-704` (tuiles 64×64 8 bpp RLE + tuiles `TILEVDP`), `loadWeaponSequences` `SEQUENCE.C:92-132` (`level_wSequence/wFrame/wChunk`) `SRUINS.C:1930-1931` |
| Dessin | `advanceWeaponSequence(xbase,ybase,hack)` `SEQUENCE.C:190-318` : file de 8 séquences `:136-167`, horloge 2 vblanks/frame `:216-218` = 30 Hz, `userClip 0..320×0..210` `:254-260`, **par chunk** : `TILEVDP` → `displayVDP2Pic` sur le sheet NBG0 + palette banque 6 `:267-294` ; sinon `EZ_normSpr(flip, COLOR_4\|HSS\|ECD_DISABLE, overlay, mapPic(tile), pos)` à `(xo-160+chunkx, yo-120+chunky)` `:296-305` |
| Position | `weaponCenter[w]` `WEAPON.C:51-60` (`WBASEX 160`, `WBASEY 130`) + ressort `weaponPos/Vel` (`moveWeapon` `:118-123`, impulsions `weaponForce` depuis marche/tour/saut `SRUINS.C:180,739,748`) ; appel `:811-813` |
| Sons | `level_wFrame[].sound` → `playSound(0,…)` `SEQUENCE.C:239-241` ; flag `FRAMEFLAG_FIRE` déclenche `weaponFire` `WEAPON.C:928-929` |

**Pour les psprites Doom** (38 frames, 229 622 px, **110 chunks 64×64** `[mesuré]`) : voie (i) = découper chaque patch en chunks 8 bpp au convertisseur et émettre `EZ_normSpr` COLOR_4 banque 0 à `(psp->sx>>16 −160 + chunkx, psp->sy>>16 −120 + chunky)` — c'est exactement le format `wChunk`, ~2-6 cmds/frame, pas de `TILEVDP`. Coût : les chunks d'arme passent par le **même cache `TILE8BPP` (31 slots)** que les monstres (`mapPic` `PIC.C:436-452`). Voie (ii) = sheet NBG0 : **une seule** fenêtre W0 par frame (`displayVDP2Pic` `PIC.C:463-492`) ⇒ arme OU flash, pas les deux → non.

---

## 5. Le 2D — trois voies pour les patches 8 bpp de Doom

Existant : barre d'état = char VDP1 `COLOR_4` 320×42 = **13 440 o** (`ART.C:74`, `EZ_setChar` `SRUINS.C:1892`, 1 `EZ_normSpr` `:1322`, pokes CPU `redrawBowlDots` `:1168-1196`) ; menus `dlg_*` = polygones + texte (`dlg_run` `MENU.C:735-820`), « overpics » 16 bpp écrits **directement en VRAM VDP1** (`loadOverPic` `:113-117`, `plotOverPicW` = 1 cmd brute `:227-242`) puis `resetPics()` au retour `:774` ; police = glyphes 4 bpp `COLOR_1`, 1 cmd/caractère (`PRINT.C:51-147`) ; écran-titre = **bitmaps VDP2 NBG0/NBG1 512×256 8 bpp** + CRAM banques 0/1 (`INTRO.C:36-100`, cycles `{0x44ee,…,0x55ee,…}` `:37-40`) ; carte du monde `runMap` `BIGMAP.C:139` ; films VDP1 16 bpp `MOV.C:185,274`.

VDP2 en jeu `[src]` : A0 = K-table + table de rotation RBG0, banc **dédié** coefficient (`PLAX.C:96,115`, `SRUINS.C:1058`) ; A1 = ciel 128 Ko (`PLAX.C:94`) ; **B0+B1 = sheet NBG0 512×512 8 bpp = 256 Ko** (`SRUINS.C:1084`, cycles `0x44ee` `:1055-1056`) ⇒ **0 octet libre** tant que le sheet existe. CRAM 8/8 (§2).

| voie | mécanisme | VRAM/CRAM | commandes VDP1 | verdict |
|---|---|---|---|---|
| **(a) couche VDP2 bitmap 8 bpp** (NBG1) | copie de l'écran-titre `INTRO.C:36-69` : NBG1 bitmap 512×256 `COL_TYPE_256`, `plate_addr=256K`, cycles `0x55ee` (`0x5`=char NBG1, `0xE`=CPU `[doc HW_VDP2.md:122-124]`) ; transparence code 0 désactivable (`dispenbl\|=0x1000` comme `PLAX.C:111`) ; V_DrawPatch de Doom écrit dans un tampon 320×200 puis **copie CPU** (jamais SCU-DMA vers VDP2 `[doc RESOURCE_BUDGETS.md:143-145]`) | **128 Ko = B1** ⇒ réduire le sheet NBG0 à 512×256 (B0) ou le supprimer ; CRAM : banque 0 PLAYPAL partagée | **0** | la meilleure pour menus/intermission/HUD ; ⚠ **neige console** si les cycles B sont mal posés `[doc HW_VDP2.md:604]` → test console |
| **(b) patches → char VDP1** | le patron `runInventory`/`plotOverPicW` : un char 320×200 `COLOR_4` (64 000 o) rempli par copie CPU depuis le tampon Doom, dessiné par **1** `EZ_normSpr` (largeur ≤ 504, hauteur ≤ 255) ; HUD partiel = char 320×32 (10 240 o) | VDP1 : **7 424 o libres** aujourd'hui `[mesuré]` (2×1448×32 + 2×1224×8 + 128 + 13 440 + 3×640 + 28×8 Ko + 31×4 Ko + 10×2 Ko + 12×1 Ko = 516 864 / 524 288) ⇒ prendre sur les cmds (1448→1024 = +27 Ko) ou évincer le cache hors 3D (menus) | 1-3 | HUD en jeu possible en remplaçant `stat_bar` (13 440 o déjà réservés) ; menus plein écran seulement le cache évincé (comme l'inventaire) |
| **(c) sheet NBG0 + W0** | `displayVDP2Pic/updateVDP2Pic` `PIC.C:455-492` : **un rectangle** du sheet par frame, pokes CPU dans le sheet | 0 Ko de plus (sheet existant) ; banque CRAM 0 | 0 | STBAR 320×32 (10 Ko du sheet) mis à jour par `ST_Drawer` écrivant dans le sheet ; **exclut** l'arme VDP2 et le sélecteur de ciel MP (W0 unique) |

Recommandation : **(a) pour tout le 2D plein écran + HUD messages**, **(b)-HUD ou (c) pour la barre** ; PLAYPAL en banque 0 unifie le tout (Doom : TITLEPIC/HELP/CREDIT/WIMAP0 = 320×200 chacun 68 168 o, STBAR 320×32, M_DOOM 123×60, 323 patches UI = 639 564 o `[mesuré]` — en LWRAM, chargés par écran).

---

## 6. Le son (`SOUND.C/H`)

| brique | preuve | pour `s_sound.c` |
|---|---|---|
| Matériel | 68K parqué `nop;bra` + SNDOFF `MEGAINIT.C:206-229`, `SOUND.C:151-153` ; SH-2 poke 32 slots via `SNDBASE 0x05a00000` **caché** `SOUND.H:4` ; slots 16/17 réservés mix CDDA `:63-65` | 30 slots ≥ 8 canaux Doom |
| Allocateur | bump `soundTop` `:13`, `loadSound` `:199-228`, `assert(soundTop+size<512 Ko)` `:218`, `MAXNMSOUNDS 80` `:40`, remis à 0 par niveau (`initSound` `:164`) ; statiques `STATIC.DAT` + dynamiques par niveau (`level_objectSoundMap[227]` `:29,243-249`) | **DOOM1 : 55 `DS*`, 535 127 o > 524 288** `[mesuré]` (max 18 600 o, 54×11 025 Hz, 1×22 050) ⇒ sous-ensembler par niveau (modèle PS) ou tronquer ; Doom 2 (~100 sons) idem |
| Format | `bps` 8/16 (`reg0\|=0x10` si 8 `:273-274,360-361`), `sampleRate` écrit **brut** dans OCT/FNS `:348` (« not really the sample rate »), `loopStart` ou −1 `:276-281` | Doom = 8 bits **non signé** → XOR 0x80 hors ligne `[est]` ; 11 025 Hz = 44 100/4 → OCT −2 `[est]` ; pas de boucles en Doom 1 |
| API | `playSoundE(source,sNm,vol,pan)` `:311-363` (TL 0..255, pan 5 bits signe bit4), dédup **1 même son/frame** `:326-328`, `stopSound/stopAllSound(source)` `:92-119`, `adjustSounds` `:430-438`, `posMakeSound/posAdjustSound` `:415-428` | `S_StartSound(origin,id)` → `stopAllSound((int)origin); playSoundE(...)` ; `S_UpdateSounds` → `adjustSounds` ; `source`=pointeur mobj |
| Atténuation | `posGetSoundParams` `:392-412` : `vol=approxDist>>5 −15` (TL ; inaudible si >255 ⇒ rayon 8 640 u), pan = angle replié ±90° `>>19` | Doom : `S_CLIPPING_DIST 1200`, `S_CLOSE_DIST 200` `[src core/s_sound.c:57,65]`, vol linéaire 0..127 → `TL≈(127−vol)*2` `[est]` ; échelle u Doom/u PS à fixer par le convertisseur |
| Vol de canal | `silenceVoice` : file ronde 8 slots pré-éteints `:58-76`, **sans priorité** | Doom priorise (`S_getChannel`) : à faire côté `s_sound.c` (inchangé) |
| Musique | CDDA `playCDTrack` `FILE.C:275`, `trackMap[]` `SOUND.C:374-390` | CDDA par carte, ou MUS sur MSH2 (Mimas `-Mus`) — hors moteur |

---

## 7. Sauvegarde (`BUP.C`)

`SaveState` `GAMESTAT.H:54-65` = **95 → 96 o**, `SaveRec` 100 o × `NMSAVEGAMES 6` = 600 o → `SPACENEEDED` 10 blocs de 64 `BUP.C:40` ; **un seul enregistrement** `"POWERSLAVE1"` `:33` écrit entier (`BUP_Write(device,&writetb,saveGames,OFF)` `:139`) ; `BUP_Init` sur 16 Ko + 8 Ko `mem_malloc(0)` à chaque sauvegarde `:57-68` ; formatage/choix interne-cartouche `bup_initialProc` `:232-306`. Backup interne = **32 Ko utiles** `[doc HW_BACKUP_BUP.md:275-278]`, écriture bloquante (8 Ko ≈ plusieurs frames `[doc :343]`).

Doom : `SAVEGAMESIZE 0x2c000` (180 Ko) `[src core/g_game.c:76]`, sauvegarde réelle **20-50 Ko `[est]`** ⇒ **impossible en interne**. Contournement = modèle PS : checkpoint de début de niveau (map, skill, `player_t` réduit : health/armor/ammo/weapons/keys/backpack ≈ 100-200 o) × 6 slots → 1 enregistrement ~1,3 Ko ; sauvegarde complète réservée à la cartouche backup (512 Ko) ou remplacée par mots de passe (PSX Doom).

---

## 8. VDP2 & palette

| effet | mécanisme existant | Doom |
|---|---|---|
| Teinte plein écran | `SCL_SetColOffset(OFFSET_A, SP0\|NBG0\|RBG0, r,g,b)` `SRUINS.C:141-142`, −255..+255 ; `playerHurt` pose (63,−63,−63) `:169-171` décru de 3/pas 30 Hz `:128-140` ; stun bleu `:1841-1845` ; vert bonus `:202-204` ; fondu noir en vblank `V_BLANK.C:99-116` | PLAYPAL 1-8 (rouge), 9-12 (jaune), 13 (vert) = **colour offset stateless** depuis `damagecount/bonuscount/powers[radsuit]` chaque frame ; couvre aussi les murs RGB16 (qu'une palette ne peut pas teinter) |
| Inversion (invulnérabilité) | aucune ; murs 16 bpp non palettisés | **irréproductible** sur les murs ; approximation = offset vers blanc + banque CRAM inversée pour les sprites `[est]` |
| Flash sprite | `SPRITEFLAG_FLASH` → banque 5 blanche | non utilisé par Doom (banque récupérable) |
| Réécriture CRAM | `retryPlaxPal` `PLAX.C:64-69`, banque 6 réécrite à chaud `SEQUENCE.C:270-287` (256 `POKE_W` en mots) | banques de lumière statiques par niveau ; écrire CRAM pendant l'affichage peut perturber l'image `[doc HW_VDP2.md:359-361]` |
| Mix NBG0 | `SCL_SetColMixRate(NBG0,c)` + `N0CCEN` `SRUINS.C:2043-2044,2175,2188` (invisibilité) | disponible pour un fondu de la couche 2D |
| Ciel | `initPlax` `PLAX.C:81-162` : palette banque 7 `:84-87`, bitmap 512×256 8 bpp en A1 `:90-94`, K-table **320 colonnes** lue du `.LEV` `:115` (formule atan en commentaire `:117-131`), rotation A0+0x500 `:96` ; `movePlax` `:25-53` : `x=−yaw·128/45°` mod 256, `y=−pitch·128/45°−100` | **256 px par 90° = exactement Doom** (`ANGLETOSKYSHIFT 22` ⇒ 1 024 col/360° `[src core/r_sky.h:29]`) : SKY1 256×128 (35 080 o `[mesuré]`) tuilé 2× horizontalement, 1 texel = 1 px, `skytexturemid=100` ↔ le −100 `[est]` ; K-table = équivalent de `xtoviewangle` ; palette = PLAYPAL ⇒ banque 7 libérable ; `F_SKY1` = `WALLFLAG_PARALLAX` |

---

## 9. Mémoire (`UTIL.C:339-406`, `MAIN.map`)

| poste | preuve | valeur |
|---|---|---|
| Allocateur | 2 aires × pile LIFO **8 niveaux** (`STACKSIZE 8` `UTIL.C:340`, `mem_nocheck_malloc` `:365-394` déborde sur l'autre aire, `mem_free` `:396-406` LIFO strict) | ⇒ **8 allocations vivantes max par aire** : Doom prend **un** bloc pour `z_zone` |
| Aire 0 LWRAM | `0x200000-0x300000` `:344,352` = 1 Mo ; y vont : séquences `SEQUENCE.C:31,100`, tuiles 8 bpp/16 bpp RLE `PIC.C:574,588,603`, staging son `SOUND.C:207`, BUP `BUP.C:60-62`, textes | 2,1× plus lente `[doc]` ; contenu Doom : sprites RLE + sons + BSP/blockmap/reject (lus, pas écrits) |
| Aire 1 HWRAM | `&end..0x6100000` `:345,353` ; `_end=0x06096878` `[mesuré MAIN.map:2636]` ⇒ **432 008 o** ; y vont : géométrie `.LEV` (`LOADPART` `LEVEL.C:27-30`, `assert size<900000` `:41`), tuiles 16 bpp 4 Ko (`PIC.C:513,547`), palettes `:618-626`, écran de chargement 76 800 o temporaire `SRUINS.C:1106` | `.text 232 740 + .data 59 868 + .bss 305 768` `[mesuré :466,1992,2207]` |
| Gros `.bss` | `WALLS.o` 108 448 (`doorwayCache` 5500×12 = 66 000 `WALLS.C:1334` — réutilisé comme `slaveResult` 1300×28 = 36 400 `:1336-1349` et comme scratch par menus/téléporteur `MENU.C:127,972`, `INTRO.C:585` ; `sectorDraw` 600×60 = 36 000) ; `OBJECT.o` 50 412 ; `PIC.o` 35 792 (`pics[1600]` + `mipbuff` 8 192) ; `SPRITE.o` 34 858 ; `UTIL.o` 20 397 (`mystack[5096]` = 20 384 `UTIL.C:29`) ; `SPR.o` 12 352 ; `FILE.o` 7 820 ; `MAP.o` 5 500 `[mesuré :2548,2416,2421,2449,2475,2258,2240,2411]` | récupérable pour Doom : `objects` 50 Ko, `mapColor` 5,5 Ko, AI/ROUTE ~4 Ko ⇒ **≈ 60 Ko** ; `mystack` 20 Ko suffit au playsim `[est]` |
| Scratch fixe | `rleBuffer=0x6001000` 4 Ko `PIC.C:208` (sous l'image) | — |
| CD | `fs_open/fs_read` **séquentiels**, 1 handle, prefetch 500 secteurs `FILE.C:146-165,194-262`, **pas de seek** | un WAD à accès aléatoire exige d'ajouter `GFS_Seek`/`fs_seek` (SBL l'a) ou de pré-linéariser les lumps par niveau dans le `.LEV` |

Bilan : deux mondes en RAM (Doom 150-250 Ko + `.LEV` 400-600 Ko + tuiles/sons 300-500 Ko `[doc DOOM_ON §6]`) contre 432 Ko + 1 Mo : **la cartouche 4 Mo (Mimas) reste le levier**, `validPtr` ne l'accepte pas (`UTIL.H:83`) — assert seulement, 1 ligne.

---

## 10. Entrée

`ACTION_{FIRE,JUMP,PUSH,FREELOC,WEPDN,WEPUP,STRAFE,RUN}` `UTIL.H:245-246`, `IMASK(a)=buttonMasks[controllerConfig[a]]` `:248`, `buttonMasks={A,B,C,X,Y,Z,TL,TR}` / `controllerConfig={0..7}` `UTIL.C:515-518` (remap à l'écran-titre `INTRO.C:120-186`) ; d-pad câblé en dur (`PER_DGT_U/D/L/R`), analogique lu (`analogX/Y/TL/TR` `V_BLANK.C:65-73`) ; multitap `PER_LInit(…,6,…)` `:234`, mais accumulateur unique + `break` `:58-78`. Doom : `forwardmove/sidemove/angleturn/buttons` (`BT_ATTACK/USE/CHANGE`) — 8 actions suffisent (fire, use, strafe, run, arme ±, automap, menu=Start) ; le remap existant est réutilisable tel quel.

---

## La « boucle hôte » proposée (pseudo-code ; `SRUINS.C` conservé / retiré)

```c
/* runLevel() : init 1847-1994 GARDÉ sauf initObjects(1911)/placeObjects(1994) → doom_P_SetupLevel + link table ;
   camera = newSprite(link[ssec(player)], r, 0, 0, /*sequence*/-1, SPRITEFLAG_NOSHADOW, NULL) */
while (1) {                                       /* 2065 */
  if (framesElapsed>8) framesElapsed=8;           /* 2102 (garde-fou) */
  viewTransform ← yaw=doom_angle_to_F(viewangle), pitch=roll=0, pos=(x, viewz, y)   /* 2105-2118 sans earthQuake */
  EZ_openCommand(); sysClip; userClip(plein); localCoord(160,120);                  /* 2120-2127 */
  drawWalls(viewTransform.current);               /* 2130 : kick esclave, secteurs lointains + sprites */
  /* --- TROU LOGIQUE : remplace 2137-2191 --- */
  doom_tics += framesElapsed*35/60 (accumulateur) ; while (tics dus) { G_BuildTiccmd(inputQ) ; G_Ticker() ; }
  /* ---------------------------------------- */
  drawWallsFinish();                              /* 2193 : join FRT, ré-émission esclave */
  /* remplace 2197-2201 : la géométrie ne bouge qu'ICI */
  sat_sync_world();   /* secteurs Doom modifiés → level_vertex[].y ; switches/anim → pics[].flags/animTileChunk ;
                         mobjs : moveSpriteTo(spr, link[ssec], pos) + sequence=base(sprite,state)+facing, frame,
                         flags (bright→light 0, SHADOW spectre, FOOTCLIP eau) ; morts/spawns : freeSprite/newSprite */
  if (automapactive) doom_AM_Drawer_EZ();         /* remplace 2204 : EZ_line COLOR_5 (MAP.C:190-208) */
  doom_R_DrawPlayerSprites_chunks();              /* remplace 2207 : EZ_normSpr COLOR_4 par chunk (SEQUENCE.C:296-305) */
  doom_ST_HU_Drawer();                            /* remplace 2211-2214 : voie (a)/(b)/(c) du §5 */
  sound_nextFrame(); pic_nextFrame(NULL,NULL);    /* 2250, 2253 : GARDÉS */
  EZ_closeCommand(); SPR_WaitDrawEnd();           /* 2273-2274 */
  gouverneur smoothVTime ; while (vtimer<smoothVTime) ; SCL_DisplayFrame();   /* 2277-2300 : GARDÉ */
  framesElapsed=vtimer; vtimer=0;                 /* 2303-2305 ; monsterMoveCounter (2302) SUPPRIMÉ */
  movePlax(viewangle,0); SCL_SetWindow(W1, bbox ciel);   /* 2308-2317 ; updateVDP2Pic (2318) si voie (c) */
  SCL_SetColOffset(OFFSET_A, SP0|NBG0|RBG0|NBG1, tint(damagecount,bonuscount,radsuit));  /* remplace stepColorOffset 2152 */
  if (gamestate!=GS_LEVEL || gameaction) return code;    /* remplace 2322-2345 */
}
```

Retiré en bloc : `movePlayer/controlInput` (857-1045), `runObjects`, `stepPlayerHeight`, powerups NBG0 (2141-2191), `updatePushBlockPositions/processDelayedMoves` (2200-2201), `runWeapon`, `drawStatBar/drawMessage/drawAirMeter`, `runInventory`, `hitCamel/hitPyramid`. Gardé verbatim : `drawWalls`, `drawWallsFinish`, `drawSprites`, `EZ_*`, `pic_*`, `SOUND.C`, `PLAX.C`, gouverneur, `V_BLANK.C`.

---

## Les 5 questions que seul un test console tranchera

1. **Le cache `TILE8BPP` (31 slots, `SRUINS.C:1903`) sous densité Doom** — 10-20 monstres × 1,3 chunk + arme 2-6 chunks : une tuile utilisée dans la frame est évincible (`PIC.C:264-272`) ⇒ texture fausse, pas seulement lente. VRAM VDP1 à 7 424 o libres `[mesuré]` : tout slot de plus se paie en commandes. Relever `used[1]/vswaps[1]` (STATUSTEXT `SRUINS.C:2253-2262`) sur E1M1 avec things.
2. **Le coût de `drawSprites` maître-seul** (tri O(n²) par secteur, `findFloorDistance` par ombre, 2 clips par FOOTCLIP) à 20-40 things visibles, et le **clip aux frontières de pièces convexes** (un thing n'est dessiné que dans `sectorSpriteList[o->s]`) : artefact visible ou non ?
3. **La couche 2D NBG1 en B1 (`0x55ee`) à côté de RBG0 K-par-dot en A0 + ciel A1** : neige ou image propre ? (loi Mimas : famine de cycles = console-only.) Et le coût de la copie CPU 64 000 o/frame vers VDP2 quand le HUD change.
4. **Teintes par colour-offset sur SP0+RBG0+NBG1 simultanés** et banques CRAM de lumière (6 niveaux COLORMAP) : rendu des sprites à distance sans le `scalelight` par pixel de Doom — acceptable ou faut-il un terme de distance (1 ligne dans `getLight` `WALLS.C:647`) ?
5. **Cadence 35 Hz dérivée du vblank 60 Hz** (7 tics / 12 vblanks, irrégulier) avec le gouverneur 60/30 : sensation Doom correcte ? Et le son : 30 slots SCSP pokés depuis le SH-2 via l'adresse cachée `0x05a00000` sous le débit `S_StartSound` de Doom (10-30/s) — key-off/latence, plus `BUP_Write` bloquant à la sauvegarde.

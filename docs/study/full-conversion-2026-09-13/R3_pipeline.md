# R3 — Convertisseurs, formats et disque « complet » (2026-09-13)

Étiquettes : `[src] FICHIER:LIGNE` ; `[mesuré]` = scripts du jour dans `scratchpad/reports/` (`r3_doom_measure.py` sur `Mimas/cd/data/DOOM1.WAD`, `Mimas/wads_temoins/Doom2.wad`, `Doom-ud.wad` ; `r3_duke_measure.py` sur `refs/build/duke13/DUKE3D.GRP` via `tools/grp.py` + `tools/buildmap.py` ; `r3_static_measure.py` sur `cd/STATIC.DAT`, `cd/KILENTRY.LEV`, `cd/TOMBEND.LEV`, `cd/SUNKEN.LEV`, `cd/TOMB.LEV`) ; `[doc]` ; `[est]`.

## 1. Ce que les chaînes produisent aujourd'hui

Doom : `doom3d.py` → `doomtiles.py` → `duke2ps/assemble.py` → `verif_doom.py`. Build : `build_import.py` → `quantize.py`/`snapmap.py` → `convex.py` → `geom3d.py` → `duketiles.py` → `assemble.py` → `verif_e3.py`. Émetteur commun `geom3d.Emitter` `[src] doom3d.py:40-42`.

| morceau du `.LEV` | Doom (E1M1 = `cd/TOMB.LEV` du jour) | Build (E1L1) | source |
|---|---|---|---|
| ciel (pal 256, 512×256, table K 320) | **donneur KILENTRY**, SKY1 non converti | donneur (ciel `LA` prévu, non fait) | `[src] assemble.py:117` |
| sectors / walls | 236 (1 feuille BSP = 1 secteur) / 1 878, portails en tête | 440 / 3 564 | `[mesuré]` json ; `[src] doom3d.py:200-210` |
| vertices / faces | 13 171 / 3 240 (sols+plafonds pavés 64 u) | 31 555 / 10 930 | `[mesuré]` |
| objects / objectParams | **1 `OT_PLAYER`**, 10 o | idem | `[src] assemble.py:189-191` |
| pushBlocks, PBVert, PBWall, waveVert, waveFace, cutPlane | **vides** | vides | `[src] assemble.py:190-193` |
| texture (2 o/cellule) | 1 612 o = 806 cellules | 8 342 cellules | `[src] assemble.py:151-153` |
| vertexLight | `light_of(sector.light)` (table Doom, j=32) | linéaire depuis `shade`, non validé | `[src] doom3d.py:137-149` |
| sons dynamiques | **donneur** : 4 sons / 43 115 o, carte → 3 types ; **rien des DS\*** | donneur | `[mesuré]` |
| palettes | 23 donneur **+ PLAYPAL** (#23, entrée 0 = 0, indice 0 → `sub0`) | 23 + Duke (0↔255) | `[src] doomtiles.py:38-56`, `assemble.py:126-128` |
| tuiles | **141 Doom 0x32** (120 murs E4.1c + 21 flats) en tête + donneur : 26×0x32 (référencées par séquences), 144×0x6A, 12×0x6C, 12×0x34, 10×0x72 | 64 Duke 0x32 + donneur | `[mesuré]` ; `[src] assemble.py:112-135` |
| séquences | **donneur** : 41 séq / 286 frames / 655 chunks recalés +141, 31 `OT_ANIM_*` neutralisés | idem | `[src] assemble.py:60-90` |
| STATIC.DAT | **PowerSlave inchangé** | idem | — |

Le disque du jour = géométrie + textures Doom dans un habitacle PowerSlave. Rien ne passe pour ciel, sons, sprites, objets, HUD, armes, textes.

Critères `[src]` : `doom3d.check()` 11 (≤600 sect., ≤5 500 murs, portails en tête, 1 sol/1 plafond, |x,z|<16 000, motif<8, parallax sans INVISIBLE, cellules/faces ≤256, sommets/mur<700, voisins bornés, départ) `doom3d.py:429-475` ; **`verif_doom.py` 20** (normales intérieures, point intérieur, portails en tête, 1 sol/1 plafond, `nextSector` borné, aucune frontière sans mur, motif<8, index<tileset, tuiles 16 bpp, parallax sans INVISIBLE, `tl·th<700`, réservation ≤1 250, 1 seul OT_PLAYER, y départ==floorLevel, départ dans son secteur, hauteur libre ≥57, joueur non scellé ≥60 %, frontières bloquantes expliquées, aucun mur fantôme, échelle verticale 2 %) `verif_doom.py:55-330` ; **`verif_e3.py` 12 `rec()`** (pas 11 : pavage sans arête répétée, bord du sol 1 u, étanchéité verticale, quads dans leur plan, normales unitaires, faces non dégénérées, tuiles cohérentes, portails en tête, ciel non invisible, parallélogrammes réels, grille<700, portails réciproques) `verif_e3.py:102-254` ; `lev_write.engine_problems()` = asserts des 5 chargeurs `lev_write.py:157-195`, E0 24/24 sha1 `[doc] NOTES_E0`.

## 2. Formats des morceaux MANQUANTS

**(a) Sprites.** Tuile 0x6A = `u16 flags, u16 palNm, u16 size, RLE` `[src] PIC.C:568-580` (`size` short ≤ 32 767), tampon **LWRAM** (`mem_malloc(0)`, aire 0 = 0x200000-0x300000 `[src] UTIL.C:344-353`). RLE = `[u8 zeroRun][u8 litRun][lit…]` jusqu'à 4 096 px `[src] PIC.C:293-346` ; classe `TILE8BPP` COLOR_4, **31 slots** `[src] SRUINS.C:1903`, `PIC.C:87`. `palNm` **non lu** en 8 bpp : tous les sprites utilisent la **palette objet** `palletes[*palletes]` → CRAM banques 0..4 assombries (`NMOBJECTPALLETES` 5 `[src] UTIL.H:24`, `PIC.C:630-657`), entrée 255 forcée 0xffff (:631), banque 5 = flash. Retail : palette objet #0, entrée 0 == 0 dans 100 % des palettes `[mesuré]`. `sChunkType` {s16 chunkx, chunky, tile, s8 flags, pad==0} `[src] SLEVEL.H:221-226` ; flags bit0 miroir G/D, bit1 H/B `[src] SEQUENCE.C:299-302` (16-104 miroirs/niveau retail `[mesuré]`). `sFrameType` {chunkIndex, flags 0x4000 END / 0x80 FIRE, sound −1, pad==0} `[src] SLEVEL.H:211-219`, son joué à l'entrée de frame `[src] SPRITE.C:782-783`. `level_sequence[s]` = 1re frame, sentinelle finale, `sequence[0]==0` `[src] SLEVEL.H:206-209`, `SEQUENCE.C:80`. Type → séquence : `level_sequenceMap[OT]` + **offsets câblés dans AI.C** (:230 `+bitSeqStart`, :950 `+8`, :1505 `+24`, `anubisSeqMap` :2592). En architecture C : sans objet — `newSprite(sector, radius, friction, gravity, sequence…)` `[src] SPRITE.C:68` prend n'importe quelle séquence ; il faut une table (sprite, frame, rot) → séquence et une carte 227×(−1) pour `SEQUENCE.C:44-48`. Dessin : `mapPic` + `EZ_scaleSpr`, classes 16 bpp acceptées `[src] WALLS.C:2808-2831`.

| corpus `[mesuré]` | lumps S_ | chunks 64×64 | RLE moteur | chunks/lump |
|---|---|---|---|---|
| DOOM1.WAD | 483 (61 préfixes) | 612 | **662 146 o** | 1,27 {1:402, 2:61, 3:2, 4:13, 6:5} |
| Doom-ud.wad | 764 | 1 565 | 1 867 525 o | 2,05 |
| Doom2.wad | 1 381 (138 préfixes) | 2 860 | 3 117 690 o | 2,07, max 10 |

Par niveau (things → `core/info.c` → chaînes d'états + projectiles + PLAY/PUFF/BLUD/TFOG/IFOG/MISL) : shareware **270-350 lumps, 278-409 chunks, 268-429 Ko RLE** (E1M8 = Baron) ; Doom II MAP15 763 / 1 253 / **1,44 Mo**, MAP29 1 057 / 1 993 / **2,05 Mo** ; ensemble « toujours » 81 lumps / 78 798 o. Retail : 144 (KILENTRY, 84 872 o) à 385 tuiles 0x6A (TOMBEND, 485 346 o) `[mesuré]`. Le shareware est dans la classe retail ; Doom II dépasse la LWRAM (1 Mo, partagée sons+séquences) → sous-ensemble par niveau et miroirs A2A8 (÷~1,6). `MAXNMPICS 1600` `[src] PIC.C:30` mais `createMippedPics()` **double tous les pics** `[src] SRUINS.C:2063`, `PIC.C:423-433` → **≤ 800 pics de base** ; E1M8 : 141+26+409+95+40 = 711 ✓, MAP29 1 993 ✗ (relever, 16 o/pic, ou ne pas mipper les 8 bpp).

**(b) Sons.** `{i32 size, i32 rate, i32 bps, i32 loopStart}` + PCM copié en RAM SCSP `[src] SOUND.C:199-223`. `rate` = `((octave<<11)&0x7800)|fns`, `octave=floor(log2(f/44100))`, `fns=frac·1024` `[src] UTIL/CONVERT.C:3783-3790` → 11 025 Hz = 0x7000. PCM **16 bits BE signé** ou **8 bits = source non signée `^0x80`** `[src] CONVERT.C:3796-3812`. DMX (`u16 3, u16 rate, u32 n, 16 o pad, n×u8`) → XOR 0x80, 0x7000/0x7800, loop −1. Bornes : `nmSounds<80` **total** (`[src] SOUND.C:40, 210, 237`), `soundTop+size < 512 Ko` (:218), remise à zéro par niveau (`initSound` `SRUINS.C:1866`), slots 16/17 réservés (:64). Retail : 43 statiques 316 160 o (42×8 bits ; 21 @11 025, 14 @22 050) + 4-28 dyn. `[mesuré]`. **Doom shareware : 55 DS\*, 535 127 o (54 @11 025) → dépasse seul les 512 Ko** ; Doom II 107 / 1,24 Mo ; Ultimate 122 (`sounds.h` : 109 `sfx_`) `[mesuré]`. → ~30 statiques armes/joueur/portes/interrupteurs (~250 Ko `[est]`) + dynamiques des monstres présents. Carte 227 shorts + `nmStaticSounds` `[src] SOUND.C:243-249` → carte 227×(−1) et appel direct `posMakeSound(source,pos,idx)` avec table `sfx_id → idx`. Groupes statiques : 8 `[src] SOUND.H:18-20`, retail `[13,21,27,30,33,36,37,41]` `[mesuré]`.

**(c) Ciel.** 256 u16 → CRAM banque 7 `[src] PLAX.C:84-87` ; i32 512, i32 256 assertés (:90-93) ; 131 072 o → VRAM A1, RBG0 bitmap 256 couleurs (:94-101) ; 320 i32 → VRAM A0 (:115) = K par colonne (générateur `atan` désactivé :117-131 ; min 52 428 / max 66 754 **identiques** dans les 4 `.LEV` mesurés et dans Duke Saturn `[doc] RETAIL_DISCS §5`) → recopier la table. SKY1 256×128 composite → ×2 largeur, ×2 hauteur `[est]` ; palette PLAYPAL ; un ciel par épisode (SKY1-3/4 ; Doom II SKY1 01-11, SKY2 12-20, SKY3 21-32) porté par chaque `.LEV` (132 872 o disque, 0 RAM).

**(d) Objets.** `placeObjects` : **un** `OT_PLAYER` (1re occurrence → `constructPlayer(secteur)`, `camera=player->sprite`) `[src] OBJECT.C:197-207`, puis `assert(type<227)`, `assert(firstParam==objectPPos)` (:211-212) ; 5 shorts, y == floorLevel (23/23 retail `[doc] NOTES_E5`). Minimum = ce qu'émet `assemble.py`. Things/portes/monstres restent playsim → aucun `OT_*` à écrire ; il faut **exporter feuille → secteur `.LEV`** (`conv.remap` `[src] doom3d.py:178`, aujourd'hui seulement pour le départ) pour `P_SpawnMapThing → newSprite`.

**(e) Palettes.** `i32 size` (<1 Mo :615), `u16 objectPalette`, N×256 u16 BGR555 bit 15 sauf entrée 0 `[src] PIC.C:611-630` ; **HWRAM** ; `pal = palletes+256*palNm+1` sans borne (:517). Doom : **une** palette PLAYPAL[0] (murs expansés au `map()` :349-355, sprites via CRAM). Flashs rouge/jaune/vert = offset couleur VDP2 déjà utilisé pour les fondus `[src] SRUINS.C:2354-2383, 1916` — petite modif moteur.

**(f) VDP2 0x01.** 8 o `{s16 x,y,w,h}` dans la feuille NBG0 512×512 8 bpp de STATIC.DAT (VRAM+0x40000) `[src] PIC.C:682-686`, `SRUINS.C:1080-1095` ; `displayVDP2Pic` = NBG0 + fenêtre W0 → **une image à la fois** (:455-470), CRAM banque 6 `[src] SEQUENCE.C:280-291` ; cap 50 (:40, :683) ; 18 dans STATIC, 0 dans les `.LEV` `[mesuré]`. Doom : STBAR 320×32 en VDP2, visages/chiffres en 8 bpp.

**(g) STATIC.DAT** `[mesuré]` 678 511 o, ordre `[src] SRUINS.C:1925-1931` :

| # | bloc | format | o |
|---|---|---|---|
| 1 | `loadLoadingScreen` | 256 u16 + i32 320 + i32 240 (assertés :1105-1106) + 76 800 → framebuffer | 77 320 |
| 2 | `loadVDP2Sprites` | 512×512 8 bpp brut | 262 144 |
| 3 | `loadStaticSounds` | i32 8, 8 s16, i32 43, 43 enregistrements | 316 872 |
| 4 | `loadWeaponTiles` | **40 = 22×0x6A + 18×0x01** → `tileBase 40` | 11 693 |
| 5 | `loadWeaponSequences` | i32 10 474 = 12 + 613 frames + 681 chunks + 55 shorts, **sans carte** `[src] SEQUENCE.C:92-132` | 10 478 |

`weaponMap[] = {0,13,17,30,35,40,50,44}` `[src] WEAPON.C:35-46`, offsets câblés (:927). STATIC Doom = TITLEPIC (320×200 + bande), feuille VDP2 (STBAR), ~30 sons statiques, **30 psprites shareware = 95 chunks 0x6A / 130 063 o RLE** (Doom II 49 / 167 / 233 902 o) `[mesuré]`, séquences minimales (`wSequence[0]==0` `[src] SEQUENCE.C:126`) — le psprite est piloté par `P_SetPsprite`. ⚠ Chunks d'arme et de monstres partagent les **31 slots TILE8BPP** (`assert` `[src] SEQUENCE.C:303`) : CHGG = 4 chunks + flash.

**(h) `level_wSequence`** : voir (g).

## 3. Le disque

| élément | PowerSlave | Doom | Duke |
|---|---|---|---|
| `.LEV` | 24 fichiers (23 distincts) / 31 slots `levelGraph` `[src] BIGMAP.C:43-82`, `NMLEVELS 31` `[src] GAMESTAT.H:30` ; 0,67-1,64 Mo | shareware 9, Doom 27, Ultimate 36, Doom II 32 ; `.LEV` complet ≈ ciel 133 Ko + niveau 300-600 + sons 100-250 + tuiles 4 098×(60-141) + sprites 270-430 + séq 20-40 = **1,4-2,0 Mo** `[est]` ; E1M1 du jour 1 228 974 o `[mesuré]` | 6 / 28 / 39 |
| progression | graphe 4 directions + `runMap` (MAP.DAT 687 502 o : picset SMALL16BPP + 2 sons + ≤100 tuiles 16 bpp `[src] BIGMAP.C:110-127, 166-188`) ; nouvelle partie = niveau 3 `[src] BUP.C:194-197` | playsim (`G_DoCompleted`, sortie secrète) → table `map → "+E1M1.LEV"` et **court-circuit de `runMap`** dans `action=runLevel()` `[src] SRUINS.C:2463, 2512` ; MAP.DAT inutile | idem |
| INITLOAD.DAT 81 948 | picset ignoré + dialogues + textes `[src] SRUINS.C:2408-2423`, `LOCAL.C:23-31` | textes = code → fichier minimal | idem |
| LOGOS/INTRO.PCS | logos, titre, menus `[src] INITMAIN.C:185-189`, `INTRO.C:418-423` | TITLEPIC, M_\*, HELP, CREDIT → PCS (`UTIL/MKPICSET.C`) — R1 | idem |
| .MOV (44 Mo), BONUS.BIN, 56 LIP (≈73 Mo) | `playMovie` `[src] INITMAIN.C:246, 608-610`, `INTRO.C:565` | **retirer**, neutraliser les appels | idem |
| CDDA | 13 pistes 02-14, `trackMap[31]` `[src] SOUND.C:370-390`, `CDC_CdPlay` repeat 0x0f `[src] FILE.C:275-288`, 377 Mo `[doc] RETAIL_DISCS §1, §7` | MUS : 13 lumps shareware (245 179 o), 35 Doom II (920 416), 45 Ultimate `[mesuré]` → WAV rendu hors ligne → raw 2352 + `.cue` multi-fichiers (recette Mimas `build.ps1:251-258, 452-458`, sox ; fork : « décrit, pas construit » `[doc] RETAIL_DISCS §9`). ≈10,1 Mo/min : shareware 13×2-3 min = 260-390 Mo ✓ ; Doom II 32 morceaux → ≤74 min (~2:07 chacun) ou dédoublonner. Alternative : synthé MUS de Mimas (SH-2 → SCSP comme le fork) mais 32 slots − 2 CDDA − SFX | 7 MID 186 449 o `[mesuré]`, même recette |
| IP/ISO | `IPFILE = SRL/modules/sgl/IP.BIN`, `xorrisofs -generic-boot`, `0.BIN`=INIT, `CD_DATA=cd/*` `[src] Makefile:278-308` | idem + `make cue` à écrire (0,5 j) | idem |

## 4. Ce qui manque pour « TOUT »

Effort en j `[est]`.

### 4.1 Doom

| # | item | source / état | cible | existant | à écrire | j | risque |
|---|---|---|---|---|---|---|---|
| 1 | Textures : **xoffset ignoré** (seuls `rowoffset` + DONTPEG `[src] doom3d.py:262-268`) | SIDEDEFS.xoff | `(uoff,voff)` dans la tuile | `tile_from_wall` | ext. `voff` | 1 | faible |
| 2 | Midtex 2 faces | SIDEDEFS.middle, ligne 2 côtés | mur à faces 0x32 (indice 0 transparent) sur le portail, BLOCKED si ML_BLOCKING | `Emitter.add_wall` | `emit_midtex` | 2 | ordre peintre |
| 3 | Flats/textures animés | `animdefs[]` `[src] core/p_spec.c:96-124` : 9 familles flats, 13 textures ; 1/carte shareware, MAP28 18+5 `[mesuré]` | toutes les tuiles de la famille + table `texnum→tuile` pour la synchro (`PICFLAG_ANIM` moteur : 15 sets `[src] PIC.C:110`, cadence 2 frames ≠ 8 tics → ne pas l'utiliser) | `doomtiles` | `--anim` + `sync_textures()` | 2 | Doom II >15 |
| 4 | Interrupteurs | `switchlist[]` `[src] core/p_switch.c:45-88` ; 1-5/carte, 13 MAP19 `[mesuré]` | 2 tuiles + réécriture octet impair au `P_ChangeSwitchTexture` | idem | table mur→cellules | 1 | — |
| 5 | **Portes/ascenseurs** : `open_doors()` ouvre statiquement `[src] doom3d.py:479-512` | SECTORS + specials/tags | géométrie **fermée** + course : par ligne 2 côtés à voisin mobile, contremarche `[fh,maxFloor]`, portail `[bot,top]` (hauteur 0 = fenêtre vide), linteau `[minCeil,ch]`, `WALLFLAG_DOORWALL` pour le recalcul `SHORTOPENING` **déjà dans le moteur** `[src] AI.C:4300-4307` (80 → 56) ; bornes `P_FindLowestCeilingSurrounding−4`, etc. | `emit_edge` | `mobile_bounds()`, `emit_edge_mobile`, synchro par translation de sommets `[doc] DOOM_ON_SLAVEDRIVER §5.1` | 5 | budget esclave compte les cellules **totales** `[src] WALLS.C:1374` ; texture écrasée |
| 6 | Lumière par secteur | `lightlevel` change au tic | table secteur → plages `firstLight` (PARALLELOGRAM) + sommets (faces) ; écriture déjà dans `[src] AICOMMON.C:44-70` | `light_of` | export + `sync_light()` | 1,5 | aucun (sommets privés) |
| 7 | **Sprites par niveau** | S_\*, things → info.c | 0x6A RLE + frames/chunks/séquences + table (sprite,frame,rot) ; miroirs A2A8 | `wadsprites.py`, `wad.patch` | `wad2sprites.py` | 4 | LWRAM (Doom II), 31 slots, 800 pics |
| 8 | Sons | DS\* | statiques STATIC + dynamiques/niveau, XOR 0x80, rate SCSP | — | `wad2snd.py` + table sfx | 1,5 | 535 Ko > 512 Ko |
| 9 | Ciel/épisode | SKY1-4 | bloc PLAX + table K recopiée | `texture()` | `wad2sky.py` | 1 | angle `[est]` |
| 10 | STATIC.DAT Doom | TITLEPIC, STBAR, psprites, sons | §2(g) | `lev_write.serialize_*` | `wad2static.py` | 2 | 31 slots partagés |
| 11 | Titre/menus/fontes | M_\*, STCFN\*, HELP, WI\* | PCS + `FONT0-2.H` (`initFonts` `[src] PRINT.C:51`) | `MKPICSET.C`, `BMP2H*` | `wad2pcs.py`, `wad2font.py` | 2 (+R1) | — |
| 12 | Table de liaison | SSECTORS/segs | feuille↔secteur, ligne→murs ; fichier `.LNK` séparé (le chargeur ne lit que 14 parts `[src] LEVEL.C:51-67`) | `remap`, `segidx` | `link_write.py` + lecteur C | 1,5 | figer avant jalon 3 |
| 13 | DEHACKED | core Mimas : `deh_*.h` seulement, **aucun `deh_*.c`** `[src] core/` | non supporté côté playsim | — | 0 | — |
| 14 | PWAD | `merge_wad.py` (dernier gagne) `[src] Mimas/tools/merge_wad.py` | idem | ✓ | **bug** `wad.py:51` `setdefault` = premier gagne → inverser | 0,5 | silencieux |
| 15 | Doom II / Ultimate | `[mesuré]` >600 feuilles : Doom II MAP14 851, **MAP15 875**, MAP17 615, MAP19 721, MAP24 697, MAP29 751 ; Ultimate 9 cartes (E4M9 **956**) ; shareware E1M6 606 ; textures 428/147 flats vs 125/54 | fusion de feuilles sœurs convexes, ou `MAXNMSECTORS` → 1 000 (~76 o/sect `[doc] DOOM_ON_SLAVEDRIVER §4.2` + `penetrate[600]` `[src] SPRITE.C:962`, `processed[600]` `[src] OBJECT.C:433`) | `adjacency.build` | `merge_leaves()` | 3 | 4 Ko HWRAM/tuile 0x32 |
| 16 | Multi-`.LEV` + cue | — | 9-36 `.LEV`, `make cue` | `mkiso` | `wad2disc.py` | 1 | — |

Total ≈ **29 j**, hors playsim (jalon 3) et rendu 2D (R1).

### 4.2 Build

| # | item | état | cible / à écrire | j | risque |
|---|---|---|---|---|---|
| 17 | MAP → `.LEV` | E1L1 E0-E6 console ✓ `[doc] NOTES_E5` ; 194 cartes : convexe OK brut **33**, quantifié 26, 137 plantages amont `[doc] NOTES_QUANT.md:150-157` (= 161/194 échouent `[doc] DOOM_ON_SLAVEDRIVER §4`) | robustesse `convex.py` (trous, éclats, superpositions) | 6 | **goulot** |
| 18 | ART → murs/sols | `duketiles.py` ✓ ; 1 605 tuiles ART, 6,8 Mo ; 46-83 distinctes/carte `[mesuré]` | — | 0 | — |
| 19 | ART → sprites | 52-113 picnums/carte (83-173 chunks, 102-222 Ko) ; familles complètes présentes → **300-400 chunks, 312-444 Ko RLE/carte** (LIZTROOP 54 tuiles/146 Ko, PIGCOP 51/153 Ko, OCTABRAIN 23/88 Ko, BOSS1 39/307 Ko, RECON 14/44 Ko) `[mesuré]` | `duke_sprites.py` ; plages de frames = `action`/`ai` de GAME.CON (lecture, aucun CON au dépôt) ; `pal>0` (LOOKUP.DAT) **impossible** en CRAM (8 banques prises) → recolorer en tuiles distinctes | 4 | tuiles ×pal |
| 20 | Panning/repeat | E1L1 : 674 murs panning, 1 814/1 937 repeat≠8 `[mesuré]` ; E4.1b = 50,5 % d'aire juste, plafond 66 %, panning ignoré `[doc] NOTES_E41` | `pu/pv` par cellule | 1,5 | cache 28 |
| 21 | Sprites muraux/plats (cstat 16/32) | 51-171 / 1-14 par carte `[mesuré]` | mur à faces 0x32 superposé (= #2), BLOCKED si cstat 1 | 2 | ordre peintre |
| 22 | Masqués / sens unique | 12-46 / 0-14 | idem #2 | 1 | — |
| 23 | Pentes | 44-225 secteurs/carte ; E3 en faces ✓ | — | 0 | ondulation affine |
| 24 | VOC → sons | **181 VOC, 2 912 824 o** ; 107 @8 000, 35 @11 025, 29 @5 988, 1×16 bits `[mesuré]` | `voc2snd.py` (XOR 0x80, rate SCSP ; `CONVERT.C loadVoc` :3814 lit déjà le VOC) | 1 | 2,9 Mo ≫ 512 Ko → partition |
| 25 | Ciel `LA` 128×300 | non fait | via `wad2sky.py` générique | 0,5 | — |
| 26 | Miroirs, caméras, RTS, pal swaps | — | **NON** | — | — |

## 5. Capacités et budgets

| constante | valeur | source | si dépassé | levier |
|---|---|---|---|---|
| `MAXNMSECTORS` | 600 | `[src] UTIL.H:21` | **non vérifié au chargement** → débordement de `sectorDraw[600]`, `sectorSpriteList[600]`, `penetrate[600]` (`[src] WALLS.C:1587-1590`, `SPRITE.C:19, 962`) : corruption silencieuse ; `lev_write` strict refuse | fusion de feuilles ; relever (+~76 o/sect) |
| `MAXNMWALLS` | 5 500 | `[src] UTIL.H:22` | idem ; `doorwayCache[5500]` 12 o/mur aliasé esclave `[src] WALLS.C:1334-1349` | découper |
| `MAXVPERWALL`, `tl·th` | 700 | `[src] WALLS.C:975, 1017, 1207` | **assert** | `CAP_CELLS 256` ✓ |
| budget esclave | 1 300−50 | `[src] WALLS.C:1336, 1374, 1555` | mur **sauté en silence** (cellules totales) | découpe, ciel bas |
| `size` niveau | <900 000 | `[src] LEVEL.C:40-41` | assert | — |
| demande mémoire | ≤~1 250 000 (SHRINE 1 343 769) | `[doc] NOTES_E5` ; assert `[src] UTIL.C:392` ; HWRAM 438 720 o + LWRAM 1 Mo `[src] UTIL.C:344-353` | assert | tuiles 0x32 = 4 Ko HWRAM (141 → 578 Ko) ; 8 bpp/sons/séq en LWRAM |
| cache VDP1 | 28 / 31 / 1 / 10 / 12 | `[src] SRUINS.C:1903` | >28 tuiles/vue : texture fausse + `map()` 8 Ko pile `[doc] NOTES_E41` | 1 tuile/texture ; 32×32 |
| `MAXNMPICS` | 1 600 **÷2 mipmaps** | `[src] PIC.C:30, 408, 430`, `SRUINS.C:2063` | assert | relever (16 o/pic) / ne pas mipper |
| `MAXNMSPRITES`, `MAXOBJECTS` | 450, 350 | `[src] SPRITE.C:12`, `OBJECT.C:9` | assert liste libre | MAP09 392 things → relever (`Sprite` 72 o `[src] SPRITE.H:43-59`) |
| `MAXNMSOUNDS`, SCSP | 80 total, 512 Ko | `[src] SOUND.C:40, 210, 218, 237` | assert | partition statiques/dynamiques |
| `MAXNMVDP2PICS` | 50 | `[src] PIC.C:40, 683` | assert | — |
| `MAXNMANIMSETS` | 15 | `[src] PIC.C:110` | non vérifié | ne pas utiliser `PICFLAG_ANIM` |
| index tuile géométrie | u8 + `tileBase 40` → 215 | `[src] SLEVEL.H:122`, `LEVEL.C:69-72` | wrap silencieux | moins de tuiles d'armes en tête |
| 126 tuiles `.til` | limite de CONVERT, pas du moteur | `[doc] LEVEL_PIPELINE §2` | — | sans objet |
| `MAXCUTSECTORS` | 128 | `[src] SLEVEL.H:172` | — | Doom : 0 cut |

**Saillant** : (1) la ressource rare est la **HWRAM** (4 Ko par tuile de mur : E1M1 141 tuiles = 578 Ko) et la **LWRAM** pour les sprites (MAP29 2 Mo) ; (2) « TOUT » côté données = **six écrivains** (`wad2sprites`, `wad2snd`, `wad2sky`, `wad2static`, `wad2pcs`/`wad2font`, `link_write`) sur `lev_write` déjà sha1-identique ; (3) les portes imposent de **défaire `open_doors()`** (fermé + course) — seul point du pipeline couplé au jalon 3 ; (4) le shareware tient dans toutes les capacités retail sauf **le son (535 Ko > 512 Ko) et E1M6 (606 feuilles)** ; Doom II casse en plus `MAXNMPICS`, la LWRAM et 6 cartes > 600.

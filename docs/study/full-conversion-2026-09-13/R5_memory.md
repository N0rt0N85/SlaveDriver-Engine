# R5 — Le mur mémoire : playsim Doom + modèle .LEV + assets dans 1 Mo HWRAM + 1 Mo LWRAM (+ cartouche)

Étiquettes : `[src] FICHIER:LIGNE` lu dans le code ; `[mesuré]` calculé ce jour (outil nommé) ; `[doc]` ; `[est]`. Scripts dans `scratchpad/reports/` (`parsemap.py`, `wadmem.py`, `wadassets.py`, `fields.sh`). Aucun fichier de dépôt modifié.

## 1. Le fork aujourd'hui (`build/stext/MAIN.map`, build 2026-09-13 15:09)

| section | adresse | taille | source |
|---|---|---|---|
| .text | 0x06004000 | 232 740 | `[mesuré]` MAIN.map:466 |
| .rodata | 0x0603cd24 | 1 800 | id. :1902 |
| .data | 0x0603d430 | 59 868 | id. :1992 |
| .bss (dont COMMON 269 254) | 0x0604be10 | 305 768 | id. :2207 ; COMMON sommé par `parsemap.py` |
| `_end` | **0x06096878** | image = 600 184 o | id. :2636 |
| **HWRAM libre = 0x06100000 − end** | | **432 008 o** | `[mesuré]` (le doc DOOM_ON_SLAVEDRIVER §5.3 disait 438 720 sur un build antérieur) |

Aires de `mem_malloc` `[src] UTIL.C:343-353` : aire 0 = LWRAM 0x200000→0x300000 (1 048 576 o), aire 1 = `&end`→0x6100000. **Repli automatique** : `mem_nocheck_malloc` essaie l'aire demandée puis l'autre `[src] UTIL.C:380-386` — la « demande totale ≤ 1 250 000 » des notes E5 est donc bien la somme des deux aires, et **tout ce que HWRAM ne peut pas prendre coule en LWRAM sans que le code le sache**. `rleBuffer` fixe à 0x6001000 (4 Ko sous l'image) `[src] PIC.C:208` ; `mystack` 20 384 o en COMMON UTIL.o à 0x06075400 `[mesuré]`. `validPtr` n'accepte que 0x200000-0x2fffff et 0x6004000-0x60fffff `[src] UTIL.H:74` (debug seulement).

**Où va chaque chargeur** `[src]` : géométrie `LEVEL.C:29` aire **1** ; palettes `PIC.C:617` aire **1** (COMPRESS16BPP) ; tuiles 0x32/0x34 (4 096 / 1 024 o d'indices 8 bits) `PIC.C:513,547` aire **1**, jamais libérées si `lock=0` ; tuiles RLE 0x6A/0x6C/0x72 `PIC.C:574,588,603` aire **0** ; séquences `SEQUENCE.C:31,100` aire **0** ; sons : staging aire **0** puis `mem_free`, PCM copié en SCSP `SOUND.C:207-224` (`assert(soundTop+size<512 Ko)` :218) ; picsets (menus/intro) aire **0** `PICSET.C:27,37,51`. `STEXT_BASELINE_2026-09-12.md` documente le champ `mem:` (SRUINS.C:2240) mais **ne rapporte aucune valeur** — les restes ci-dessous sont déduits de la marche du .LEV (§4).

**Top .bss/COMMON** `[mesuré]` (taille exacte = bloc COMMON par objet) : WALLS.o 111 452 (doorwayCache 66 000 = MAXNMWALLS 5 500×12, sectorDraw 36 000 = 600×60, updateList/drawList 2×2 400, debugLines 1 600, .bss 3 004) · OBJECT.o 51 220 (objects 50 400 = MAXOBJECTS 350×144 `[src] OBJECT.C:9-11`) · PIC.o 36 716 (pics 25 600 = MAXNMPICS 800×32, mipbuff 8 192, mippedSlot 1 600) · SPRITE.o 34 870 (sprites 32 400 = MAXNMSPRITES 450×72 `[src] SPRITE.C:12`, sectorSpriteList 2 400) · UTIL.o 20 485 (mystack) · SPR.o (SBL) 12 352 · FILE.o 7 820 · MAP.o 5 552 (mapColor) · ROUTE.o 4 404 · SCL_FUNC 3 824 (SclK_TableBuff 3 280) · PROFILE.o 3 448 · SOUND.o 2 386 · PRINT.o 2 307 · MENU.o 1 508 · BUP.o 1 024 (saveGames 1 000). **Top .data** : stat_bar 14 340 (ART), randTable 8 232, meter_bubble 8 224 + meter_back 8 200 (SRUINS), bigFont 6 548 + brianFont 4 012, _stackinit 4 100, compass ×3 1 944. **Top .text** : AI 41 408, WALLS 21 344, SRUINS 19 836, MENU 11 676, SPRITE 6 952, WEAPON 6 544.

**Ce qui DISPARAÎT si le playsim Doom remplace AI/AI2/AICOMMON/OBJECT/WEAPON/HITSCAN/ROUTE/MENU/INTRO/BIGMAP** (MOV n'est lié que dans INIT, AIRBUB est `#include` dans SRUINS `[src] Makefile:133,162`) `[mesuré]` : .text **85 000** ; .bss **64 243** (objects 51 220, ROUTE 4 404, mapColor 5 552, MENU 1 508, saveGames 1 000, divers) ; .data **~35 000** (art HUD PowerSlave stat_bar/meters/compass 32 908, palettes séquences 1 059, tables AI/MENU/BIGMAP). **Total ≈ 184 Ko.** Restent : `sprites[]` (la cible de la table mobj→Sprite — à porter à ~650 entrées pour E1M6/MAP10 : +14 Ko), `sectorDraw` (+7,6 Ko pour MAXNMSECTORS 700, +21 Ko pour 900 `[doc] DOOM_ON_SLAVEDRIVER §4`).

## 2. Le code Doom non-renderer compilé SH-2 (`Mimas/build/Mimas.map`, `[mesuré]` par objet)

| groupe | .text | .rodata | .data | .bss | total |
|---|---|---|---|---|---|
| playsim `p_*.o` (19 objets) | 78 952 | 3 051 | 1 640 | 9 212 | 92 855 |
| jeu `g_game d_main d_loop d_net d_event d_items d_mode doomstat dstrings` | 23 152 | 5 817 | 656 | 12 368 | 41 993 |
| données `info tables sounds` | 0 | **68 456** | **46 560** | 0 | 115 016 |
| UI `m_menu st_* wi_stuff hu_* f_* am_map` | 45 848 | 14 025 | 5 296 | 4 348 | 69 517 |
| services `w_wad w_main w_file z_zone v_video m_misc m_random m_bbox m_cheat m_argv s_sound memio` | 17 848 | 2 580 | 20 | 1 440 | 21 888 |
| **À AJOUTER au MAIN du fork** | **165 800** | **93 929** | **54 172** | **27 368** | **341 269** |
| jetable (m_config, sha1, d_iwad, stubs i_*, statdump, m_controls, r_flatcache) | 12 884 | 7 473 | 5 192 | 2 104 | 27 653 |
| renderer `r_* i_scale` (non repris) | 105 172 | 1 795 | 900 | 118 100 | 225 967 |
| plateforme `src/*` (remplacée par celle du fork) | 82 692 | 16 849 | 901 | 131 449 | 244 022 |

Gros postes nommés : `states` 27 076 + `mobjinfo` 12 604 + `sprnames` 608 (.data info.o) ; `finesine` 40 960 + `finetangent` 19 996 + `tantoangle` 8 196 + `gammatable` 1 280 (.rodata tables.o = 70 432) ; `S_sfx` 5 232 + `S_music` 1 104 ; `.bss` d_loop 5 232 (ticdata, BACKUPTICS 32).

**Bilan image du fork après échange** : 600 184 − 184 000 + 341 269 ≈ **757 Ko → HWRAM libre ≈ 275 Ko** `[est]`. Levier gratuit : reloger les 70 Ko de tables `.rodata` en LWRAM par une section de l'éditeur de liens (lecture seule ; le playsim de Mimas vit déjà tout entier en LWRAM) → **HWRAM libre ≈ 345 Ko**. C'est le chiffre que j'utilise en §6.

**Zone Doom de Mimas** : 1 040 384 o (1016 Ko) en LWRAM `[doc] RESOURCE_BUDGETS §2.2`, base 0x00200000 `[src] dg_saturn.cxx:20,260`. Pool TLSF HWRAM = 0x060fa000 − `_end` 0x060eaab0 = **62 800 o** `[mesuré] Mimas.map:5537`. Planchers PU_STATIC mesurés seulement sur IWAD commerciaux (Doom II 494,7 / TNT 618,8 / Plutonia 552,9 / Ultimate 403,0 Ko `[doc] ENDGAME_ROADMAP:179`) — et ils sont **essentiellement du renderer** (répertoires de colonnes ~157 Ko, composites, flats, 142 lumps ST+HU = 66,2 Ko). Mimas expose `zf` (Z_TrueFree), `lg`, `ca`, `zb` `[src] dg_saturn.cxx:209,4060-4146` mais **aucune capture shareware n'est consignée** → §6 en `[est]`.

## 3. Structures de carte Doom à `P_SetupLevel`

Tailles SH-2 : `seg_t 14`, `node_t 28`, `line_t 24`, `side_t 16`, `mobj_t 156` (zone : 180 avec en-tête) `[src] p_setup.c:81-90 _Static_assert` ; `vertex_t 8` `[src] r_defs.h:67-72` ; `subsector_t 8` `[src] r_defs.h:254-260` ; `sector_t 88` `[est]` (18 champs, `degenmobj_t` 24 inclus, pas d'assert) ; `thinker_t 12` `[src] d_think.h`. Comptes `[mesuré] wadmem.py` sur `wads_temoins/Doom1.WAD` (v1.9, 4 196 020 o) et `Doom2.wad` ; « statique » = verts+segs+ssec+nodes+sectors+lines+sides+BLOCKMAP+REJECT+table `sector->lines` (≈ lines×8) ; « mobjs » = THINGS vivants au skill 4 hors DM × 156.

| carte | verts | lines | sides | sect | ssec | segs | blockmap | reject | **statique** | mobjs (n) | **total PU_LEVEL** |
|---|---|---|---|---|---|---|---|---|---|---|---|
| E1M1 | 467 | 475 | 648 | 85 | 237 | 732 | 6 922 | 904 | **63 362** | 17 940 (115) | **81 302** |
| E1M3 | 946 | 1 026 | 1 326 | 177 | 461 | 1 445 | 8 894 | 3 917 | 126 801 | 56 784 (364) | 183 585 |
| **E1M6** (pire shareware) | 1 207 | 1 352 | 1 727 | 250 | 606 | 1 862 | 15 804 | 7 813 | **174 025** | 68 484 (439) | **242 509** |
| E1M8 | 328 | 333 | 511 | 74 | 177 | 586 | 19 400 | 685 | 62 601 | 17 472 (112) | 80 073 |
| Doom II MAP10 | 961 | 1 054 | 1 312 | 186 | 455 | 1 417 | 10 954 | 4 325 | 130 245 | 75 348 (483) | 205 593 |
| **Doom II MAP15** | 1 601 | 1 690 | 2 361 | 301 | 875 | 2 647 | 18 452 | 11 326 | **229 460** | 56 160 (360) | **285 620** |
| Doom II MAP29 | 1 180 | 1 152 | 1 653 | 205 | 751 | 1 911 | 13 062 | 5 254 | 162 870 | 41 184 (264) | 204 054 |

Ajouter ~50-100 mobjs transitoires (projectiles, puffs, fog) × 180 = 9-18 Ko `[est]`.

**Ce que le playsim lit vraiment** `[mesuré] fields.sh` (occurrences `->champ` dans p_*.c, g_game, s_sound, p_saveg, am_map, st/hu) : `sector_t` — **les 16 champs** sont lus (special 92, floorheight 83, specialdata 42, soundorg 37, tag 36, lightlevel 36, linecount/lines 22/21…) ; `line_t` — tout est lu (flags 143, special 92, sidenum 42, bbox16 9, slope 14) ; `side_t` — textureoffset/rowoffset (scrollers, p_spec), top/bottom/mid (p_switch 8/14/15) et `seci` : tout est lu. **`seg_t` : jamais lu hors `p_setup.c`** (le hit `p_sight.c` est un commentaire) ; `R_PointInSubsector` (6 appelants `[src] p_map.c:160,450 p_maputl.c:424 p_mobj.c:409,805 g_game.c:1173`) ne descend que `nodes` ; blockmap/reject : p_map, p_maputl, p_sight, p_tick.

**Élagage possible si le .LEV est la vérité de rendu** : **les segs seulement** — E1M1 10 248 / E1M6 26 068 / MAP15 37 058 o (≈ 16 % du statique). Les vertices restent (P_PointOnLineSide via `v1i/v2i`, `line_t` n'a plus de dx/dy). Optionnel et à effet de jeu : remplacer `R_PointInSubsector` par le `pointInSector` du moteur supprimerait nodes+ssec (E1M6 21 788 o) — je ne le recommande pas (le secteur de spawn/téléport doit rester celui de Doom). **Ce qui peut vivre en LWRAM sans coût de code** (lu, jamais écrit) : blockmap, reject, nodes, vertices = E1M6 50 Ko, MAP15 67 Ko — et de fait `mem_malloc(1,…)` y coule déjà tout seul (§1).

## 4. Le modèle .LEV Doom — marche de `build/doom2ps/TOMB_e1m1.LEV` `[mesuré]`

| bloc | octets | aire | détail |
|---|---|---|---|
| ciel (palette 512 + bitmap 131 072 + K 1 280) | 132 872 | **VRAM** A1 + A0, CRAM banque 7 | `[src] PLAX.C:83-118` ; 0 o de RAM |
| géométrie `size` 237 932 (+4) | **237 936** | **HWRAM** | 236 sect×24 = 5 664 ; 1 878 murs×48 = 90 144 ; **13 171 sommets×8 = 105 368** ; 3 240 faces×10 = 32 400 ; tex 1 612 ; light 2 674 |
| sons dynamiques (4) | 43 115 | SCSP (staging LWRAM transitoire) | |
| palettes (24) | 12 290 | HWRAM | |
| tuiles 0x32 ×167 (141 Doom + 26 donneur) | **684 032** | **HWRAM** (4 096 chacune) | tuiles Doom seules = 577 536 |
| tuiles 0x34 ×12 | 12 288 | HWRAM | |
| tuiles RLE 0x6A ×144 + 0x6C ×12 + 0x72 ×10 (sprites du donneur TOMB) | 96 115 | **LWRAM** | |
| séquences (41 seq, 286 frames, 655 chunks) | 8 076 | LWRAM | |
| **demande aire 1** | **946 546** | | > 432 008 libres ⇒ **514 538 coulent en LWRAM** via UTIL.C:380 |
| **demande aire 0** | 104 191 (+514 538 débordés = 618 729) | | LWRAM restante ≈ **430 Ko** `[est]` |
| **demande totale** | **1 050 737** | | = le « 1 052 449 » du doc à 0,2 % |

La boîte grise E5 (`e5/TOMB.LEV`) : géométrie 232 591, 75 tuiles 0x32 → demande 668 560. Les 24 .LEV retail : CAVERN 1 335 755, CHAOS 1 247 227, COLONY 1 219 548 (géométrie 458-679 Ko, tuiles 0x32 60-83) `[mesuré]`.

**Deux leçons du .LEV E1M1** : (a) **44 % de la géométrie sont des sommets** (13 171 pour 467 dans le WAD — 7 par mur, non partagés). `sWallType.v[4]` indexe des sommets globaux `[doc] LEV_FORMAT §2`, donc le partage est permis par le format : **−70 à −90 Ko sur E1M1** `[est]`, proportionnellement plus sur E1M6. (b) **Les tuiles pèsent 2,4× la géométrie** : 141 × 4 Ko = 578 Ko = **134 % de la HWRAM libre**. Le format 0x72 (16BPP|RLE, décodé au `map()` `[src] PIC.C:603-610,340-346`) place la même tuile en **LWRAM** à ~4,2 Ko (le RLE de Doom, opaque, ne compresse pas) pour ~3 ms par swap `[doc] STEXT §3.6`, swaps mesurés 0-1/frame `[doc] STEXT §3.4` : c'est le levier de placement sans perte.

**Extrapolations** `[est]` (facteur segs) : E1M6 ×2,55 → géométrie ≈ 607 Ko brute / ≈ 380 Ko à sommets partagés ; tuiles ≈ 100 en E4.1b (410 Ko) à ≈ 170 en E4.1c (696 Ko). Doom II MAP15 ×3,6 → ≈ 860 Ko brute — contre `assert(size<900000)` `[src] LEVEL.C:41` et **MAXNMSECTORS 600 < 875 feuilles** (`[src] UTIL.H:21`, 76 o/secteur à étendre `[doc]`).

## 5. Assets Doom résidents (`[mesuré] wadassets.py`, `wadsprites.py` sur Doom1.WAD)

**Sprites** : 483 lumps, 825 576 o bruts, 1,27 chunk 64×64/frame (402 en 1 chunk, 13 en 4, 5 en 6 ; max 154×151) → 613 chunks pour tout le shareware, contre `MAXNMPICS 800` `[src] PIC.C:32` et 31 slots VDP1 `TILE8BPP` `[src] SRUINS.C:1903`. Par famille : BOSS 139 828 · SARG 120 704 · TROO 77 424 · PLAY 74 028 (inutile en 1p) · POSS 70 988 · SPOS 68 604 · armes HUD 149 952 (SHTG 33 296, SAWG 29 216, PISG 18 572, PUNG 16 576, CHGG 16 384, MISG 12 564 + flashes) — **contre 22 tuiles RLE = 11 381 o d'armes PowerSlave dans STATIC.DAT** `[mesuré]` · projectiles/effets ≈ 59 Ko · pickups/décor ≈ 30 Ko. Le moteur garde **tout le RLE d'un niveau résident en LWRAM** (`load8BPPRLETile`), donc par niveau, RLE ≈ ×1,0 ±15 % du lump `[est]` : **E1M1 (POSS/SPOS/TROO) ≈ 270 Ko · E1M3/E1M6 (+SARG) ≈ 390 Ko · E1M8 (SPOS/TROO/SARG/BOSS) ≈ 460 Ko** — armes (150 Ko) en sus si elles vont en RLE LWRAM comme aujourd'hui.

**Sons** : 55 lumps DS* = **535 127 o > 524 288 de SCSP**. Jeu de base (armes 97 558 + joueur 98 750 + monde 90 123 + monstres communs 47 825) = 334 256 ; + POSS/SPOS 71 780, TROO 62 609, SARG 32 789, BOSS 24 813 → **E1M1 468 645 · E1M3 501 434 · E1M8 526 247 (déborde de 2 Ko)**. Liste blanche par niveau obligatoire (DSSAWFUL 18 104 + DSSAWUP 16 297 si pas de tronçonneuse, etc.). Aujourd'hui STATIC.DAT charge 316 160 o de sons statiques + 43-194 Ko dynamiques `[mesuré]`. Staging LWRAM transitoire = plus gros sample 18 600 o.

**2D** : 636 904 o dont WI* 184 552 (intermission — fenêtre de chargement, .LEV déchargé), M_* 103 060, ST* 68 268 + STCFN 7 324 + AMMNUM 524 (**≈ 76 Ko résidents en jeu**), HELP1/HELP2/CREDIT/TITLEPIC 4×68 168 (hors niveau). PLAYPAL 10 752, COLORMAP 8 704 (inutile sans renderer logiciel).

**Musique** : 13 MUS = 245 179 o, max D_E1M8 59 535. Le fork parque le 68K `[doc] DOOM_ON_SLAVEDRIVER §5.4` ; le `-Mus` de Mimas vit **sur le driver 68K SGL** (SDDRVS.TSK 26 610 + .DAT 163 119 en SCSP `[mesuré] cd/data`). CDDA = 0 o (voie PowerSlave) ; un séquenceur MUS SH-2 = 20-60 Ko de lump + banque d'instruments PCM en SCSP (non chiffrée ici — elle concurrence les 470-526 Ko de SFX).

## 6. Bilan — trois plans (HWRAM libre après échange de code = 345 Ko, §2)

**Zone Doom installée dans un bloc `mem_malloc`** `[doc] §5.3` : PU_LEVEL E1M6 243 Ko (mobjs comptés 156 ; 180 en zone : +10,5 Ko) + transitoires 18 Ko + PU_STATIC (lumpinfo ~20, PLAYPAL 11, ST/STCFN 76) + marge 20 % ⇒ **≈ 440 Ko pour E1M6, ≈ 200 Ko pour E1M1, ≈ 470 Ko pour MAP15** `[est]` ; **un bloc de 512 Ko**. À mesurer : un `printf(Z_FreeMemory())` au chargement dans Mimas (compteurs déjà là, §2).

| poste | E1M1 | E1M6 | où | source |
|---|---|---|---|---|
| **A — sans cart, structures Doom complètes, .LEV E4.1c tel quel** | | | | |
| géométrie .LEV + palettes | 250 | 619 | HWRAM 345 → E1M6 coule 274 en LWRAM | §4 `[mesuré]`/`[est]` |
| tuiles 0x32 | 578 | 696 | LWRAM (débordement) | §4 |
| zone Doom | 200 | 440 | LWRAM | §6 |
| sprites RLE + séquences + staging | 300 | 420 | LWRAM | §5 |
| **LWRAM demandée / 1 024** | **1 078 → NON (−54)** | **1 830 → NON (−806)** | | |
| **B — sans cart, élagué** : segs supprimés, sommets partagés, tuiles E4.1b (68/~100), tables Doom en LWRAM (+70) | | | | |
| géométrie partagée | 160 | 380 | HWRAM 345 → E1M6 coule 35 | `[est]` |
| tuiles 0x32 E4.1b | 279 | 410 | LWRAM | `[doc] §4 médiane 68` |
| zone (−segs) | 190 | 414 | LWRAM | |
| sprites+seq+staging | 300 | 420 | LWRAM | |
| **LWRAM / 1 024** | **839 → OUI (+185)** | **1 349 → NON (−325)** | | |
| **C — cart 4 Mo (0x22400000 cache-through)** : tuiles brutes + sprites RLE + séquences + lumps du niveau sur cart | | | | |
| cart | ≤ 1,6 Mo | ≤ 1,6 Mo | A-bus | IWAD entier = 4 174 732 o ⇒ **19 Ko restants** `[doc] RESOURCE_BUDGETS §2.3` : exclusif avec les assets convertis → IWAD sur CD (le fork streame déjà STATIC.DAT + .LEV au chargement) |
| géométrie chaude (secteurs+murs) | 96 | 245 | HWRAM 345 | |
| géométrie froide (sommets/faces/lumière) + tables + zone | 70+70+190 | 165+70+440 | LWRAM | |
| **LWRAM / 1 024** | **330 → OUI (+694)** | **675 → OUI (+349)** | | |
| MAP15 (géométrie ≈ 600 partagée, zone 470) | | 355 HWRAM + 715 LWRAM → OUI (+309) | | mais MAXNMSECTORS 600→900 (+23 Ko), `size<900000`, perf hors sujet |

**Verdicts.** A ne rentre nulle part, même E1M1. **B = shareware petites/moyennes cartes** (E1M1/E1M8, probablement E1M4/E1M5 ~ 1 000-1 100 Ko à vérifier) ; E1M6 est **hors de portée sans cartouche** (et heurte MAXNMSECTORS 600 de toute façon). **C rend le shareware entier faisable et met MAP15 à portée**, au prix de : (i) `validPtr` `[src] UTIL.H:74` à étendre à 0x22400000-0x227FFFFF (macro, 1 ligne, debug seulement) ; (ii) A-bus ~4× la work RAM `[doc] HW_MEMORY_AND_BUS §6, TB#47 non re-vérifié]`, lecture **octet par octet disproportionnée** `[doc] id. :340-343` → décoder le RLE des sprites depuis un tampon HWRAM rempli par DMA (≤ 4 Ko/chunk), pas depuis la cart ; tuiles 4 Ko copiées A-bus→VDP1 (`dmaMemCpy`, 0-1 swap/frame) ; (iii) **jamais de géométrie parcourue par frame sur la cart** (le 14,9 ms fixe + 39 µs/cellule a été mesuré avec tuiles/géométrie débordant en LWRAM — pas sur A-bus) ; (iv) la Saturn nue n'a pas de cart : **B reste le plan de repli obligatoire**, C un accélérateur. Interdits en B : E1M6, tout Doom II. Interdits en C : Doom II au-delà de ~900 feuilles (géométrie > 900 Ko, `size` assert) sans fusion de feuilles côté convertisseur.

## 7. VDP2 pour un calque 2D

État en niveau `[src] SRUINS.C:1050-1077 setVDP2` : `vramModeA/B=ON` (4 bancs), **A0 = RBG0_K, A1 = RBG0_CHAR** (RDBS), cycles `{A0 eeee eeee, A1 eeee eeee, B0 44ee eeee, B1 44ee eeee}` = 2 lectures char NBG0 en T0/T1 de B0 et B1, tout le reste CPU R/W ; CRAM `CRM15_2048` (8 banques de 256).

| banc | contenu | plein ? | source |
|---|---|---|---|
| A0 | table K du ciel 320×4 = 1 280 o (+0) + paramètres rotation (+0x500) | **1 % occupé mais exclusif** : banc déclaré coefficient, `CYCA0` don't-care, une lecture NBG y échoue `[doc] HW_VDP2 §2 :140-158` ; RDBS→00 corrompt le K `[HW] id. :169-177` | `[src] PLAX.C:99,118` |
| A1 | ciel bitmap 512×256 8bpp | 131 072 = **100 %** | `[src] PLAX.C:97` |
| B0+B1 | feuille NBG0 bitmap 512×512 8bpp @ VRAM+0x40000 | 262 144 = **100 %** | `[src] SRUINS.C:1085,1093` |

⇒ **0 octet de VRAM libre pour un NBG1** ; côté cycles B0/B1 ont 6 slots libres chacun (un bitmap 8bpp = 2 accès `[doc] HW_VDP2 table 3.3`), la contrainte est le **stockage** — et les tailles de bitmap sont fermées : 512×256 minimum, pas de 320×224 `[doc] HW_VDP2 :58`. Deux issues : (1) réduire la feuille NBG0 à 512×256 → B1 libéré → NBG1 512×256 8bpp (128 Ko, pas 70) avec `CYCB1 = 4455eeee` ; (2) **préférable : dessiner le 2D de Doom dans la feuille NBG0 existante** — c'est déjà un bitmap 8bpp de 512×512 fenêtré par `SCL_W0` `[src] PIC.C:456-462`, priorité 6 sous les sprites ; `v_video.c` écrit des indices 8 bits, il suffit d'un pas de ligne 512 (ou d'un tampon HWRAM 320×200 = 64 Ko blitté). Coût : **0 VRAM, 0 cycle nouveau**. **Palette** : banque 0 = palette objets `[src] PIC.C:629-630`, 1-4 = copies assombries (NMOBJECTPALLETES 5 `[src] UTIL.H:24`, PIC.C:633-650), 5 = flash (PIC.C:655-659), **6 = palette des pics NBG0** (`SCL_SET_N0CAOS(6)`, `SEQUENCE.C:269-286`), 7 = ciel (PLAX.C:85). **Les 8 banques sont prises** ; PLAYPAL devient la palette objets (banque 0, partagée sprites/tuiles — c'est le cas de Doom) et **recopiée en banque 6** pour le calque 2D ; les flashs de palette Doom (douleur/objet) = réécrire les banques 0-4 et 6 (1 536 mots) au vblank. L'ombrage passe de 32 colormaps à **5 paliers** — la quantification de lumière est la perte visible, pas la mémoire.

## 8. Sauvegarde

BUP interne : **32 Ko utiles**, blocs de 64 o, affichage `ceil((n+32)/64)`, ≤ 256 Ko/enregistrement `[doc] HW_BACKUP_BUP §8` ; `SaveRec` PowerSlave = **100 o** (saveGames 1 000 o / 10, `[src] BUP.C:23,42`), 6 slots.

Doom `p_saveg.c` `[src]` : mobj = 1 tag + pad + 24 write32 + 8 writep = **132 o** ; secteur 14 o, ligne 6 o + 10 o/side ; joueur ≈ 300 o. **E1M1** `[mesuré]` : monde 85×14 + 475×6 + 648×10 = 10 520 + 115 mobjs × 132 = 15 180 + spéciaux ~800 + joueur ≈ **27 Ko → un seul slot et rien d'autre**. **E1M6** : 28 882 + 439×132 = 57 948 → **≈ 87 Ko : ne tient pas du tout** ; MAP10 ≈ 85 Ko. Le flux est très compressible (pointeurs, momentums nuls, validcount) : ×3-5 en LZSS `[est]` → E1M6 ≈ 20-30 Ko = **1 slot au mieux**. Recommandation : (a) checkpoint de début de niveau à la PowerSlave = `player_t` + numéro de carte (~400 o, 7 blocs) → 6 slots en 42 blocs ; (b) sauvegarde différentielle « recharger la carte puis appliquer » : masque des 463 THINGS tués/ramassés (58 o) + états secteurs (250 × 4 o) + joueur ≈ **2-4 Ko** `[est]`, compatible 6 slots ; (c) la sauvegarde intégrale Doom **uniquement sur cartouche mémoire** (device 1, lecture à l'exécution `conf[1].unit_id` `[doc] §3`).

## Ce qui reste `[est]` et comment le lever

1. `Z_FreeMemory` au chargement d'E1M1/E1M6 dans Mimas (compteurs présents, jamais imprimés pour le shareware) — fixe la taille du bloc zone.
2. Le vrai coût A-bus du `map()` d'une tuile/chunk depuis la cart (une sonde `htimer` autour de `dmaMemCpy`) — décide si les sprites RLE peuvent rester sur cart.
3. Le facteur de partage des sommets .LEV (à sortir de `doom3d.py` : sommets distincts (x,y,z) sur E1M1) — décide si E1M6 tient en HWRAM en plan C.
4. `mem:` sur console avec E1M1 chargé (la ligne existe, SRUINS.C:2240) — valide les 430 Ko de LWRAM restants du §4.

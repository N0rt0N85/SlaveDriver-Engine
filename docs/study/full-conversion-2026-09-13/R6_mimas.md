# R6 — Mimas comme DONNEUR pour le fork SlaveDriver (inventaire de la couche plateforme, 2026-09-13)

Étiquettes : `[src]` lu dans le code, `[mesuré]` calculé ce jour (outil indiqué), `[doc]` document du dépôt, `[mem]` fichier de mémoire, `[est]` estimation.

## 0. Chiffres-cadre

| Fait | Valeur | Preuve |
|---|---|---|
| Ce que Mimas fait tourner | menus, HUD (1p/2p/3-4p), intermission, automap, son SFX+MUS, CDDA (parqué), 1-4 joueurs split local, Doom II/TNT/Plutonia en streaming CD | `[src]` core/g_game.c:1860-1885, d_main.c:385-470, i_sound_saturn.cxx ; `[doc]` ENDGAME_ROADMAP.md §1 |
| Ce que Mimas NE fait PAS | **sauvegardes** (une tentative = `I_Error` = freeze), **DEHACKED** (absent), **cheats clavier** (remplacés par un chord), config = défauts compilés | `[src]` syscalls.c:159-164, g_game.c:1774-1785, deh_str.h:25-40 ; `[doc]` TESTING.md §5 « There are no saves » |
| Poids lié du core NON-renderer (ce que le fork hériterait tel quel) | **336 234 o** = text 177 908 + rodata 69 638 + data 59 364 + bss 29 324 (tables.o 66,8 K, info.o 40,2 K, g_game 15,6 K, p_saveg 13,5 K, wi_stuff 12,1 K, am_map 10,8 K, m_menu 10,6 K) | `[mesuré]` awk sur build/Mimas.map (objets core/ hors r_*.o, i_scale.o) |
| Poids du renderer Mimas (ce que le fork REMPLACE) | 225 148 o (text 105 948, **bss 118 248**) | `[mesuré]` idem, r_bsp/r_data/r_draw/r_main/r_plane/r_segs/r_sky/r_things/r_parallel/r_flatcache/i_scale |
| Couche plateforme src/ | 212 176 o (text 71 708, bss 127 000 dont framebuffer 71 680 + pile Doom 24 576) | `[mesuré]` idem ; `[src]` dg_saturn.cxx:1389, main.cxx:56 |
| HWRAM libre côté fork | 438 720 o (`&end`..0x06100000, aire `mem_malloc(1)`) | `[doc]` SlaveDriver docs/PORTING_NOTES.md:86-87 |
| Zone Doom Mimas | LWRAM entière moins RP_CMD_BUF 8 Ko ≈ 1 040 Ko ; planchers PU_STATIC : Ultimate 403 K / Doom II 495 K / TNT 619 K | `[src]` dg_saturn.cxx:1672-1676, Makefile `-DRP_CMD_BUF_SIZE=0x2000` ; `[doc]` ENDGAME_ROADMAP.md §2 |
| Disque courant | `cd/data/DOOM1.WAD` = **5 154 720 o** (shareware aplati + patches coupés, > 4 Mo) ⇒ le build par défaut STREAME depuis le CD même en shareware, avec `DOOMRP.DRP` 12 961 487 o | `[mesuré]` ls cd/data ; `[src]` build.ps1:140-165 (flatten 128 puis split) ; wads_temoins/Doom1.WAD brut = 4 196 020 o |

Lecture : la plateforme Mimas est **SGL brut + registres** sous un mince shim SRL (boot, `SRL::Cd::File`, `SRL::Sound::Cdda`, `SRL::Debug::Print`) `[mem doomsrl-srl-usage-and-lineage]` — c'est ce qui la rend transplantable vers SBL : chaque appel SRL a un équivalent GFS/CDC/SCL direct que **le fork appelle déjà**.

## 1. Chargement WAD

| Brique | Fichier:lignes | Ce qu'elle fait | Dépend SRL/SGL | Équivalent SBL (fork) | Portable | Pièges |
|---|---|---|---|---|---|---|
| Deux modes cart/CD | w_file_saturn.cxx:14-27 ; dg_saturn.cxx:5709-5830 | cart 4 Mo détectée ET WAD ≤ 4 Mo ⇒ IWAD copié en cart (zéro-copie `W_CacheLumpNum` = pointeur) ; sinon `sat_streaming_mode=1` + lecture par lump | `SRL::Cd::Initialize`, `SRL::Cd::File` | `fs_init` (`GFS_Init`, FILE.C:114-140) | adapter | le WAD aplati par build.ps1 dépasse la cart → streaming silencieux (`[mesuré]` ci-dessus) |
| Cartouche : activation + sonde | dg_saturn.cxx:1680-1700 | écrit 0x25FE00B0=0x23301FF0, 0x25FE00B8=0x13, 0x257EFFFE=1 ; sentinelles 1 par Mo pour détecter le repliement 1M/2M/4M | aucune (registres) | aucun (le fork n'a pas de cart, `validPtr` refuse) `[doc]` HW_USAGE_VS_MIMAS.md | copier | un AR en mode 1M replie ⇒ WAD tronqué = écran noir `[src]` :1687 |
| `load_wad` cart | dg_saturn.cxx:1750-1830 ; `sat_cart_load_region` :1738 | lit l'en-tête, refuse > 4 Mo, copie par blocs de 256 Ko (secteur→cart non-cachée + purge), % à l'écran | `SRL::Cd::File::LoadBytes` | `GFS_Fread` | adapter (~80 l.) | débit non mesuré ici ; **lumps alignés 4** obligatoires en lecture en place `[mem saturn-cart-lump-alignment]` |
| Lecture CD par lump | w_file_saturn.cxx:364-415 | `LoadBytes(secteur, n, dst)` = un `GFS_Load` complet par lecture ; dest **aligné 4** sinon `GFS_ERR_ALIGN=-21` ; lecture non alignée bouncée par tranches de 8 secteurs (16 Ko, `Z_Malloc PU_STATIC` tardif :319-331) ; retry ×8 (« pattern whackCD de SlaveDriver », :199-217) ; k-mètre FRT+vblank :219-243 | `SRL::Cd::File` | `fs_read` : `GFS_Fread` via `sectorBuff` 2 Ko + `GFS_NwCdRead` préfetch 500 secteurs (FILE.C:156-163, 194-254) | adapter | la poignée GFS persistante (R2.2) a **régressé sur ODE SD** (dilatation du temps) et est compilée OFF `[src]` :57-77, `[mem r2-persistent-cd-handle]` — le fork fait exactement ça (`fs_open` garde la poignée) : à re-mesurer sur ODE |
| Taille du WAD | w_file_saturn.cxx:109-145 | `infotableofs + numlumps*16` depuis l'en-tête (la sonde EOF de SRL se trompe) | — | `GFS_GetFileSize` | copier | — |
| Hooks core `W_` | w_wad.c:103-121 (un seul fichier, `W_MainWadFile`), :151-153 (`lumpinfo` en zone, pas en tas libc), :429-490 (cart memcpy → hook DRP → `W_Read` → **court = zéro-fill** au lieu d'`I_Error`), w_wad.h:64-91 (`W_LumpResident`, `W_CacheLumpPrefix`, `W_PtrIsMapped`) ; w_file.c:29-33 (classe `saturn_wad_file` câblée en dur) | — | — | tel quel (vient avec core/) | `W_CacheLumpPrefix` est un contrat renderer (R_GenerateLookup) — inoffensif |
| `strip_wad.py` | tools/strip_wad.py:2-15 | retire GENMIDI/DMXGUS/DMXGUSC/DP* ⇒ 4 196 020 → < 4 194 304 ; **garde DEMO1-3** (contredit le commentaire d_main.c:895 « demo lumps are stripped ») | — | — | copier | build.ps1 ne l'appelle PAS (WAD « bundled raw ») `[src]` build.ps1:~100 |
| `flatten_textures.py` / `split_patches.py` | build.ps1:140-165 | textures rendues mono-patch par colonne (plus de composite) + patches > 128 px coupés ⇒ plus gros lump ~17,5 Ko | — | — | **inutile au fork** (pas de composites Doom) sauf pour le convertisseur doom2ps | grossit le WAD au-delà de la cart |
| `.DRP` (DOOMRP.DRP) | tools/repack_wad.py:21-80 (format) ; w_drp_saturn.cxx | **pack par carte** : pour chaque MAPxx, le sur-ensemble sûr des lumps référencés, LZSS-12/4, concaténés en un blob ; en-tête 32 o (« DRP1 », n_lumps, **crc32 du répertoire**, n_maps, codec\|flags), table de cartes 24/28 o, entrées 16 o (lump_idx, data_ofs, csize, usize) ; index des en-têtes de sprites (R3.1, 1 lecture au lieu de ~1381) :432-451 ; **staging du blob en cart** par niveau ⇒ CD libre ⇒ CDDA possible :556-593 ; décodage-préfixe bit-exact :731-747 ; scratch LZSS persistant :164-207 ; préload R5.1 **OFF** :481-483 | `SRL::Cd::File` seulement | `GFS_*` | adapter (C++ → C trivial ; 851 l.) | **un DRP périmé échoue en silence** (crc ≠ ⇒ raw) et coûte ~4 min de boot `[mem drp-repack-must-be-rebuilt]` ; règle Makefile avec tampon `.rotlevel` `[src]` Makefile section repack |
| `merge_wad.py` | tools/merge_wad.py:1-15 | IWAD + PWAD concaténés en un IWAD (le scan arrière de `W_CheckNumForName` donne la priorité) | — | — | copier | **ne padde pas à 4** ⇒ interdit en mode cart `[mem saturn-cart-lump-alignment]` |
| `bake_levels.py` | tools/bake_levels.py:7-25 | pré-cuit VERTEXES/LINEDEFS/SEGS/NODES en layout moteur | — | — | **rejeté** (grossit le disque, le CD est le mur) | hors pipeline |
| IP.BIN | tools/make_ip.py:1-25, pre.makefile | recopie l'IP.BIN SRL et réécrit maker/product(10 c.)/version/date/titre ; product = clé de config SAROO | template `modules/sgl/IP.BIN` | le fork utilise le même template (`make iso`) `[src]` SlaveDriver Makefile:~20 | copier | le `;` parasite de shared.mk (patch SRL) `[doc]` patches/README.md |

## 2. Entrée

| Brique | Fichier:lignes | Fait | Dépend | Équivalent SBL | Portable | Pièges |
|---|---|---|---|---|---|---|
| Table pad→touche | dg_saturn.cxx:17063-17078 | D-pad→flèches, START→ESC, A→FIRE **(+ENTER)**, B→USE, C→RSHIFT (courir), X→TAB, Y→'y', L→',', R→'.' ; en mode overlay ≠ 0, L/R → KEY_STRAFE_L/R :17790-17800 | `Smpc_Peripheral[]` (SGL global) + `PER_DGT_*` | `processInput` : `PER_LGetPer` → `Pad[]`, `PER_LInit(PER_KD_PERTIM, 6, ...)` V_BLANK.C:45-92, 234 | adapter (~40 l.) | `processInput` ne lit que le **premier** pad et fait `accum &=` ; A+B+C+START ⇒ `SYS_EXECDMP` (reset) V_BLANK.C:79-83 |
| File d'événements | dg_saturn.cxx:17080-17130 ; core/i_input.c:279-320 | anneau 32 entrées, touche codée 7 bits + bit pressé ; `DG_GetKey` → `I_GetEvent` → `D_PostEvent` | — | `inputQ[16]` V_BLANK.C:88-91 | copier | — |
| Chords de debug | dg_saturn.cxx:117 (L+Z), 741 (L+Down), 1105 (L+A), 2137-2138 (L+C, L+Y), 17341-17347 (**R+Down = god/noclip**, `SAT_CycleCheat` p_tick.c:304-312) | leviers live ; L+R cycle l'overlay :591 | — | — | ne pas transférer (méthode oui, §11) | les cheats clavier (idkfa…) sont **inatteignables** au pad |
| Multi-pad | mp_input.cxx:32-43 (index brut : multitap port 1 `[p]`, sinon port 2 `[15+p-1]`), :49-61 (compte contigu), :65-80 (A/START pad 2 au titre), :100-125 (`DG_BuildLocalTiccmd` : même mapping, L/R = strafe), :128-131 (hook) | ticcmd des joueurs 2-4 construit directement depuis le pad, hook `sat_build_local_ticcmd` appelé dans d_loop.c:220-240 | `Smpc_Peripheral` | `PER_LGetPer` (6 périph./port déjà armés) `[doc]` MULTIPLAYER_STUDY.md:7,20 | adapter | Ymir n'expose que 2 pads (J3/J4 miroir) `[src]` dg_saturn.cxx:17760 |

## 3. Temps

| Brique | Fichier:lignes | Fait | Dépend | Équivalent SBL | Portable | Pièges |
|---|---|---|---|---|---|---|
| `DG_GetTicksMs` | dg_saturn.cxx:16991-17048 ; handler :1884-1908 | FRT maître sysclk/128 (~4,47 µs/tick, TCR :5715) + accumulateur µs par vblank ; **borné par le nombre de champs réellement vus** (`vbl_count`), monotone | `SRL::Core::OnVblank +=` | `SetVblank`/`UsrVblankStart` V_BLANK.C:95,221 ; `htimer` hblank :41 | copier (registres) | l'ancien garde-fou « +5 s » était un **latch** ⇒ jeu au ralenti :16999-17016 ; un handler qui rate des champs = horloge lente :1870-1880 |
| `I_GetTime` | core/i_timer.c:43-55 | `ms*35/1000` | — | — | tel quel | — |
| Cap de tics | core/d_loop.c:183-205 | `maketic - gameticdiv >= 9` (la branche `new_sync` est **morte**, `new_sync=0` :478) | — | — | tel quel | `[mem gametic-slowmotion-tic-cap]` : la première « validation » testait du code mort |
| `BACKUPTICS 32` | core/net_defs.h:51 | anneau ticcmd 128→32 = +15 Ko de bss | — | — | tel quel | — |
| Gouverneur de tic | core/p_tick.c:92-104 (décimation des monstres loin, `SAT_DECIM_DIST` 1536), :162-174 (**thinkers parqués** `-2`, délinkés mais vivants), :210-237 (fenêtre du cache de visibilité suivant les `MF_SHOOTABLE` vivants) ; p_mobj.c:496-506 | le tic est **memory-bound** sur console (T 69-83 ms, jusqu'à T165 > R141) `[mem game-tic-overtook-renderer]` | `RP_Think*` (r_parallel.h, no-op hors RP_PROF) | — | tel quel ; **stubber `RP_*`** | **p_saveg ne voit pas les parqués** (p_tick.c:167-168) ⇒ à relinker avant sauvegarde |

## 4. Son

| Brique | Fichier:lignes | Fait | Dépend | Équivalent SBL | Portable | Pièges |
|---|---|---|---|---|---|---|
| SFX | i_sound_saturn.cxx:218-277 (`cache_sfx`), :584-647 | DMX `03 00 rate len` ; 8 bits non signés → signés (`^0x80`), écrits en mots dans 0x25A00000 par bump-allocator (base 0x100 en MUS, **0x8000 en CDDA** pour sauter SDDRVS.TSK 26 Ko :505-511) ; 8 voix = slots 0-7 ; pitch = octave+FNS :188-197, TL :199-205, pan :207-216 ; **key-off puis attente KYONEX** avant re-key-on :605-614 ; `I_SoundIsPlaying` par horloge de fin :631,643 ; garde `Z_LargestAllocatable` :242 | aucune (registres) + `DG_GetTicksMs` | `initSound` = **même SNDOFF + 32 slots** SOUND.C:144-187, `SNDBASE` **caché** 0x05A00000 `[doc]` HW_USAGE_VS_MIMAS.md | copier | MVOL=0 laissé par SGL (:518, :599-602) ; le fork remet `soundTop=0` par niveau ⇒ invalider `sfx_cache[]`/`driver_data` ou garder le bump Mimas |
| Précache SFX du niveau | core/p_setup.c:1060-1117 ; `I_CacheSound` :660-667 | liste fixe + 5 sons de chaque mobj spawné → SCSP au chargement (coût zone 0) | — | `loadStaticSounds`/`loadDynamicSounds` SOUND.C:243-251 | tel quel | `[mem precache-streaming-verdict]` : le vrai hitch en jeu était le SON |
| Séquenceur MUS | i_sound_saturn.cxx:135-172, 316-337, 343-456, 552-555 | slots 8-22 (15 voix), 3 ondes de 32 échantillons (scie/sinus/triangle), 140 Hz, `mus_step` appelé par `I_UpdateSound` chaque frame ; percussions (canal 15) **ignorées** ; volume 0-127 | aucune | — (le fork n'a pas de musique synthé) | copier | coût mesuré console : **S 0 ms** dans l'identité row-2 `[doc]` RESOURCE_BUDGETS.md:40 ; `mus2mid.c` existe dans core/ mais **n'est pas compilé** `[src]` Makefile DOOM_CORE_C |
| CDDA | i_sound_saturn.cxx:788-918 ; cd/data/CDDAMAP.TXT | `PlaySingle(track, loop)`, `StopPause/Resume`, volume 0-127 → **0-7** (`SND_SetCdDaLev`) ; carte `<nom_musique> <piste>` lue sur les 2047 premiers octets ; détection par marqueur `CDAUDIO.TXT` (la sonde TOC **pend 10 min** sous Ymir :462-474) ; choix à `I_InitSound` via `sat_cd_free_during_play` (w_drp:842-851) | `SRL::Sound::Cdda`, `SRL::Sound::Hardware::Initialize` (68K + SDDRVS) | `playCDTrack` = `CDC_CdPlay` FILE.C:275-288 ; « make cd audible » 0x217/0x220=0xE0 SOUND.C:171-172 ; slots 16/17 réservés au mix `[doc]` DOOM_ON §5.4 | adapter (le fork a déjà le chemin, **sans 68K**) | `[mem cdda-8min-load-diagnosis]` : 2e `CDC_CdInit` derrière GFS ; build `-Mus` par défaut `[mem mus-build-default]` ; streaming ⇒ tête CD occupée ⇒ pas de CDDA sauf blob en cart |
| Multi-auditeur | core/s_sound.c:404-410, 480, 602-608 | volume/pan évalués contre chaque joueur local, le plus fort gagne | — | `posGetSoundParams` SOUND.C:392 (1 caméra) | tel quel | — |

## 5. Vidéo 2D (menus/HUD/intermission/automap) — la référence du « calque VDP2 »

| Brique | Fichier:lignes | Fait | Dépend | Équivalent SBL | Portable | Pièges |
|---|---|---|---|---|---|---|
| Framebuffer | dg_saturn.cxx:1389, :1553 ; core/i_video.c:289-293, 327-331 ; i_video.h:27-28 | `static uchar framebuffer[320*224]` HWRAM = `I_VideoBuffer` ; `I_FinishUpdate` → `DG_DrawFrame` ; **SCREENHEIGHT 224** (bande 200-223 : letterbox intermission wi_stuff.c:85-91, 1769-1852 ; `ST_DELTAY` st_stuff.c:91-96) | — | — | copier | 224 lignes ⇒ tous les décalages HUD viennent avec core |
| Blit vers VDP2 | dg_saturn.cxx:5838-5845 (NBG1 512×256 8bpp, banc B0 0x25E40000, palette 1), :16649-16700 (W5 : lignes 3D toujours, **lignes HUD seulement si `sat_hud_dirty`** st_stuff.c:274-280 / st_lib.c:101-107), :1110-1121 (blit **CPU**, DMA mort : bus B saturé) | `slBitMapNbg1`, `slBMPaletteNbg1`, `slScrPosNbg1`, `slScrScaleNbg1` | `SCL_*` (SCL_FUNC.C) ou pokes registres VDP2 ; le fork a **A0 ~126 Ko vides** `[doc]` HW_USAGE_VS_MIMAS.md:32 | adapter | NBG1 est **au-dessus** des quads VDP1 : l'index 0 = transparent, tout pixel non nul masque le monde :1454-1459, :16663-16667 ; ordonnancement des bancs ⇒ neige `[mem doomsrl-vdp2-capacity]` |
| Palette | core/i_video.c:402-431 (`I_SetPalette` → `colors[]`, `palette_changed`) ; dg_saturn.cxx:6878 (RGB555 `0x8000\|b<<10\|g<<5\|r` → `pending_cram`), :1895-1899 (**écrit en CRAM dans le handler vblank**) | flashs dégâts/objets = palette entière PLAYPAL 1-13 | aucune | `SCL_SetColRamMode` puis écriture CRAM 0x25F00000 directe | copier | en split la palette est **partagée** ⇒ flash par moitié en LUT logicielle :1417-1470, st_stuff.c:963-970, 1034-1040 |
| Fondu de transition | dg_saturn.cxx:6860-6920 ; core/d_main.c:270-320 | dip-to-black CRAM en 16 pas remplace le « melt » `f_wipe` (192 Ko de zone introuvables en streaming) | — | `fadeDir/fadePos` = **color offset VDP2** V_BLANK.C:97-118 (mieux : matériel, sans réécrire la palette) | remplacer par celui du fork | le melt reste en mode cart |
| Console texte | dg_saturn.cxx:1560-1620 (40 col × 26 lignes, NBG3), :1639 (`DG_Fatal` : message + boucle `FATAL loop n=`) | `printf` → `_write` → console + port debug Ymir 0x22100001 syscalls.c:146-155 | `SRL::Debug::Print` | `drawStringf`/`PRINT.C`, `assertFail` « Write This Down » UTIL.C:181 `[doc]` PORTING_NOTES.md:124 | adapter | — |
| Message HUD en VDP1 | core/hu_stuff.c:383-405 ; dg_saturn.cxx:15201 | glyphes isolés dans un tampon → sprite VDP1 prio 7 (net sous le zoom ×2) | — | `EZ_setChar`/police VDP1 du fork | optionnel | — |
| Panneaux HUD 2p/4p | tools/make_hud2p.py, make_hud4p.py → src/hud2p_panel.h (160×64), hud4p_panel.h (160×16), core/hud2p_layout.h, hud4p_layout.h ; core/st_stuff.c:1122 (`ST_DrawCompactWidgets`), :1237 (`ST_DrawQuadHud`), :1168 (`ST_SplitHudSig`) | recomposition des pixels réels de STBAR en indices PLAYPAL | — | `STATBAR.C` (char VDP1 13 440 o, 1 cmd/frame) | tel quel si surface 8bpp, sinon ré-émettre en sprites VDP1 | bande 4p **opaque** (aucun index 0) pour occulter VDP1 |
| Minimap 3p | core/am_map.c:1295-1460 | 4e quadrant 160×112, écrit direct dans `I_VideoBuffer` | — | `BIGMAP.C` | tel quel | — |

## 6. Sauvegardes — Mimas ne sauvegarde PAS

| Fait | Preuve |
|---|---|
| `_open` → `ENOENT`, `_read` → 0, `rename`/`remove` → -1 | `[src]` syscalls.c:157-164, 220-229 |
| Menu Load : `fopen` NULL ⇒ tous les slots « EMPTYSTRING » | `[src]` core/m_menu.c:504-520 |
| Menu Save : `G_DoSaveGame` `fopen` NULL ×2 ⇒ `I_Error("Failed to open either…")` ⇒ `DG_Fatal` (boucle infinie) | `[src]` core/g_game.c:1774-1785, i_system.c:438-441 |
| Config : `M_LoadDefaults` sur `fopen` raté = défauts ; `M_SaveDefaults` en `I_AtExit` ne tire jamais | `[src]` core/m_config.c:1881, d_main.c:1745 |
| `p_saveg.c` est lié (13 460 o) et intact : il sérialise les structures **Doom**, donc survit à l'architecture C | `[mesuré]` map ; `[doc]` DOOM_ON_SLAVEDRIVER.md §5.5 |
| Fork : `BUP_Init/Read/Write/Stat/Format`, 6 slots `SaveRec`, blocs de 64 o, formatage auto | `[src]` BUP.C:23, 40-42, 66-139, 239-256 |
| Backup interne = 32 Ko utiles | `[doc]` saturn-refs/knowledge/HW_BACKUP_BUP.md:275 |

Verdict : **à écrire** — un `FILE*` mémoire (`fmemopen`/`fopencookie` newlib, ou remplacer `saveg_write8/16/32` par un écrivain-tampon) puis `BUP_Write` ; taille d'une partie E1Mx `[est]` 20-40 Ko contre 32 Ko de backup ⇒ compression ou cartouche backup ; relinker les thinkers parqués (`-2`) avant `P_ArchiveThinkers` `[src]` p_tick.c:167-168.

## 7. Multi local (split 2/3/4)

| Brique | Fichier:lignes | Fait | Portable |
|---|---|---|---|
| Globales | core/g_game.c:1860-1867 (`sat_local_players`, `sat_armed_players`, `sat_deathmatch`, hook), :1868-1885 (`G_DoNewGame` : `netgame=true`, `playeringame[1..3]`) | déterministe, sans réseau (`FEATURE_MULTIPLAYER` off) | tel quel |
| ticcmds | core/d_loop.c:220-240 ; d_net.c:74-90 (garde `ingame[]` en retard) | joueurs 1..N-1 depuis le hook | tel quel |
| Drop-in | core/g_game.c:1321-1440 (`G_SatDropInService`), d_main.c:967-975 (`D_StartTitle` remet 1p) | START pad 2 en jeu cycle 1→4 | tel quel |
| Géométrie | core/d_main.c:385-470 (boucle `D_Display` par vue), hud4p_layout.h (`HUD4P_QUAD_H 112`) | 2p : moitiés x=160, vue 160 lignes + HUD 64 ; 3/4p : quadrants 160×112 (96 + bande 16) `[mem split-screen-geometry]` | **renderer-couplé** : c'est ici que `R_RenderPlayerView` est appelé par vue → à réécrire pour le peintre |
| Détails | wi_stuff.c:1320-1326 (têtes par joueur, teintes équipe), m_menu.c:910 (restart autorisé) | — | tel quel |

## 8. Contenu

| Sujet | État Mimas | Preuve |
|---|---|---|
| IWAD quelconque | `D_FindIWAD` retourne « DOOM1.WAD », `mission=none` ⇒ `D_IdentifyVersion` par lumps (E1M1/MAP01) | `[src]` core/d_iwad.c:702-712 |
| Doom II / TNT / Plutonia | bootent en streaming ; TNT 23/32 cartes ne tenaient pas avant R4 (résolu : « tout charge sauf Nuts ») | `[doc]` ENDGAME_ROADMAP.md §1 ; `[mem zone-contiguity-wall-loadsegs]` |
| PWAD | pas de multi-fichier à l'exécution ; `merge_wad.py` hors ligne | `[src]` w_wad.c:227-229 (2e fichier = garde bruyante) |
| DEHACKED | **absent** : `FEATURE_DEHACKED` non défini ⇒ `DEH_String(x)=(x)` ; aucun `deh_*.c` dans core/ | `[src]` deh_str.h:25-40 ; `[mesuré]` ls core |
| Cheats | clavier impossible ; `SAT_CycleCheat` (god/noclip, réappliqué chaque tic) sur R+Down ; `SAT_TEST_GOD`, `SAT_WARP_MAP` à la compilation | `[src]` p_tick.c:279-312 ; Makefile ; dg_saturn.cxx:17837-17860 |
| Démos | attract = titre↔crédits seulement | `[src]` d_main.c:895-897 |

## 9. Patchs `// SATURN:` du playsim — utiles au fork vs liés au renderer

| Fichier (nb) | Utile au fork (vient avec core/) | Lié au renderer Mimas — à neutraliser |
|---|---|---|
| p_tick.c (11) | gouverneur (parqués, décimation, fenêtre sight), `SAT_ApplyCheats` | brackets `RP_Think*` → stubs vides |
| p_sight.c (9) | cache temporel 128 entrées / 4 tics (~2 Ko, remplace REJECT ; walks 1218→600, T 42→21 sur MAP15) `[mem sight-temporal-cache]` ; `REJECT` NULL toléré :365-373 ; divline via accesseurs `NODE_*` :305-315 | `RP_Sight*` |
| p_mobj.c (8) | **slab** 64 mobj/chunk (26 Ko d'en-têtes économisés sur SCYTHE MAP30) :541-551 ; parcage :496-506 | `RP_ThkPhys/State` |
| p_setup.c (36) | chargement **in-place** :165-175 ; `node_t` 28 o / `seg_t` 14 o / `line_t` scindée (`lines_validcount`) :114, 272-289, 403-417, 505-513 ; REJECT gaté par taille :887 ; asserts de layout `mobj_t` :75-93 ; précache SFX :1060-1117 ; hook DRP :1212 | `P_StageBSP` (inerte), `R_SetupTextureCaches`/carve flat pool :1280-1290, `sat_sky_precache_hook` :1237, `sat_sector_bbox` (RBG0) :667 |
| z_zone.c (31) | garde pointeur cart :29-31,184 ; **pass 1 sans éviction** :284-286 ; ré-ancrage du rover :602 ; `Z_LargestAllocatable` + jumeau à sortie anticipée :863 ; rover sauvage :484 ; multi-zone :995 | `RP_Stamp*` |
| w_wad.c (20) | tout (§1) | `R_LumpPin` :496 (patch de ciel) |
| d_loop.c (10) | cap `>= 9`, ticcmds MP | compteurs `sat_tic_avail/built` |
| d_main.c (60) | garde `gamestate` :260-266, fondu :300-320, `D_StartTitle` | **toute la boucle split + probes `SATURN_TICK_DEBUG` :165-230, 385-470** — `D_Display` à réécrire |
| g_game.c (11) | MP, drop-in, `sat_session_tics`, `SAT_TEST_GOD` | — |
| s_sound.c (4), m_menu.c (2), st_*.c (16), hu_stuff.c (7), wi_stuff.c (5), am_map.c (4) | multi-auditeur ; Quit → titre :1144-1150 ; HUD compact + `sat_hud_dirty` ; bande 224 | `sat_hu_msg_buf` (optionnel), `sat_lowres` dans am_map |
| r_defs.h (5), p_mobj.h (1) | **les structs rétrécies sont consommées par le playsim** (`P_CheckSight`, `P_LoadNodes`) ⇒ garder r_defs.h même sans renderer | — |

Important : `r_parallel.h` est inclus par p_tick/p_mobj/p_sight/d_main/z_zone `[mesuré]` grep (8/7/3/1/4 appels `RP_`) ; le fork fournit un `r_parallel.h` de stubs ou compile `r_parallel.c` avec `RP_PROF` off.

## 10. Build

| Point | Mimas | Fork | Conflit / action |
|---|---|---|---|
| Standard C | `core/%.o` et `src/%.o` forcés en **`-std=gnu11`** (shared.mk impose `-std=c2x`, qui casse les déclarations implicites) `[src]` Makefile:~255-262 | `-x c -m2 -O2 -std=gnu89 -fgnu89-inline -fcommon -fno-builtin` `[src]` SlaveDriver Makefile:111 | règle séparée pour core/ en gnu11 (les patchs SATURN déclarent dans les `for`) ; `-fcommon` inoffensif pour core |
| Flags Doom | `-w -fsigned-char -DCMAP256 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DNDEBUG -DMAXVISPLANES -DSAT_VISPLANE_POOL -DVP_POOL_PLANES=64 -DRP_CMD_BUF_SIZE=0x2000 -DTEXCACHE_MARGIN=0x20000 -DSAT_REPACK -DSAT_DEFER_SOUND_INIT -DSAT_SND_PRECACHE -Isaturn_libc -Isrc -Icore` `[src]` Makefile | — | garder `-fsigned-char`, `CMAP256`, `SAT_REPACK`, `SAT_SND_PRECACHE` ; les `VISPLANE`/`TEXCACHE`/`RP_CMD_BUF` tombent avec le renderer ; `SAT_DEFER_SOUND_INIT` est un patch **SRL** (patches/saturnringlib.patch) sans objet |
| Headers libc | retire `modules/dummy` (stdio stub sans `FILE`) pour que la vraie newlib serve `fopen/printf` ; `saturn_libc/` = stdio.h/stdlib.h/string.h locaux `[src]` Makefile ; ls | newlib via `-specs=nosys.specs` | Doom a besoin de `FILE*` (p_saveg, m_menu) : vérifier que la newlib du fork est complète |
| Tas libc | `_sbrk` sur tableau statique **4 Ko** (pic mesuré 1 256 o ; `hp<peak>/<cap>!<fail>`) `[src]` syscalls.c:55, 123-144 | `shim/syscalls.c` : `_sbrk` **échoue toujours** (ENOMEM) pour protéger `mem_malloc(1)` qui part de `end` `[src]` shim/syscalls.c:1-14 | **conflit dur** : `M_StringDuplicate`/`M_StringJoin`/`myargv` mallocent au boot ⇒ `I_Error` avant la 1re frame ; prendre le `_sbrk` Mimas (4 Ko statiques) |
| Allocateur | zone Doom = LWRAM entière (`DG_ZoneBase`) | pile LIFO 2 aires × 8 niveaux, pas de tas `[doc]` DOOM_ON §5.3 | un `mem_malloc(0, …)` de N Ko → `Z_Init` dessus (déjà prévu §5.3) |
| Pile | Doom sur pile dédiée 24 Ko (40→24, sentinelle `sk`) `[src]` main.cxx:46-70, 217-231 | `mystack[]` UTIL.C, r15 par crt0 `[src]` saturn.ld:13 | la récursion BSP de Doom reste (P_CheckSight, blockmap) ⇒ dimensionner ≥ 24 Ko |
| Cartes mémoire | `build/Mimas.map` 586 Ko | `make size`, `build/stext/MAIN.map` | même discipline (§b-1) |
| Syscalls | `_write` → console, `_exit` → `DG_Fatal` `[src]` syscalls.c:146-155, 210-217 | `sn_stubs.c` | copier |

## 11. Outils de test (la méthode se transfère, pas l'overlay)

| Outil | Contenu | Preuve |
|---|---|---|
| TESTING.md | protocole testeur : identité = nom de build (pas `V0.NNN`), hiérarchie **Ymir > Mednafen > Kronos** (« Kronos cache des fautes »), format de rapport Build/Version/Hardware/Map/Je fais/Attendu/Observé/Reproductible, photo/vidéo | `[doc]` TESTING.md §1-5 |
| `/ship` | build.ps1 (`-Mus` par défaut) → notes rédigées → archive → release GitHub **privée** `mimasengine/mimas-builds` → Discord ; un `ship.config.json` par projet (réutilisable tel quel pour le fork) | `[src]` ship.config.json ; `[doc]` tools/ship/README.md ; `[mem test-build-ship-pipeline]` |
| Table « vu → sens » | ne jamais lancer l'émulateur soi-même : livrer le disque + dire quoi regarder et ce que chaque symptôme signifie | `[mem never-launch-emulator-yourself]` |
| Overlay | NBG3 texte 40 col, L+R cycle 0 plein / 1 fps / 2 off ; règle « ligne changée ⇒ légende mise à jour » ; brackets FRT `RP_*` par phase (le fork a déjà `STATUSTEXT calc/draw`) | `[src]` dg_saturn.cxx:591, 1560 ; `[mem debug-overlay-legend]` |
| Discipline de mesure | juger un levier par **toggle en session**, jamais build-contre-build (décalages de .bss = +6 ms sur scène identique) ; Ymir n'est pas un oracle de temps | `[mem interbuild-perf-noise]`, `[mem ymir-not-a-perf-oracle]` |

## (a) Fonctions que le fork doit fournir pour que core/ (moins le renderer) compile et tourne

| Fonction | Source Mimas | Verdict |
|---|---|---|
| `DG_Init` | dg_saturn.cxx:5709-5830 | **écrire** (le fork a son init) ; réutiliser `cart_enable/cart_probe_size/load_wad` (~150 l.) si cart |
| `DG_DrawFrame` | :15879 | **écrire** : présenter la surface 2D (menus/HUD) sur une NBG ; le monde est au peintre |
| `DG_SleepMs`, `DG_SetWindowTitle`, `DG_FrameBuffer` | :17050, :17828, :1553 | copier |
| `DG_GetTicksMs` (+ handler vblank) | :16991-17048, :1884-1908 | copier ; brancher sur `UsrVblankStart` |
| `DG_GetKey` + `poll_pad` + `keyq_*` | :17700-17826, :17063-17130 | adapter (`PER_LGetPer`) |
| `DG_ZoneBase` | :1672 | adapter (`mem_malloc` bloc) |
| `DG_Fatal` (cible de `I_Error` sous `__sh__`) | :1639 ; i_system.c:438 | adapter (`drawStringf`) |
| `DG_FadeOut/In` | :6909-6914 | remplacer par le color-offset du fork |
| `dbg_print`, `sat_console_putc/clear`, `sat_debug_row0` | :1370, :1591-1619 | adapter |
| `W_SaturnCDInit`, `saturn_wad_file` (`Saturn_OpenFile/Close/Read`), `sat_cd_bounce`, `sat_cd_stage_get` | w_file_saturn.cxx | adapter (`GFS_Seek+GFS_Fread` ou `GFS_Load`) |
| `sat_drp_select_map`, `sat_drp_read_lump(_n)`, `sat_drp_sprite_headers`, `sat_drp_preload`, `sat_cd_free_during_play`, `sat_cart_load_region` | w_drp_saturn.cxx ; dg_saturn.cxx:1738 | adapter (seul `SRL::Cd::File` à remplacer) |
| `sat_frt`, `sat_vbl`, `sat_cd_clock_add`, `w_cd_ms10` | w_file_saturn.cxx:227-243 | copier ou stubber (télémétrie) |
| `I_InitSound … I_BindSoundVariables` (12 fonctions) | i_sound_saturn.cxx:476-667, 958 | copier (en C) |
| `I_InitMusic … I_MusicIsPlaying` (10 fonctions) | :922-956 | MUS copier ; CDDA adapter à `CDC_CdPlay` (FILE.C:275) |
| `sat_build_local_ticcmd`, `sat_count_local_pads`, `sat_mp_pad2_a/start`, `sat_mp_input_init` | mp_input.cxx | adapter |
| `_sbrk`, `_write`, `_open`, `_exit`… | syscalls.c | copier (remplace le shim du fork) |
| `I_SetPalette` (core) consommé via `colors[]`/`palette_changed` | dg_saturn.cxx:6878, 1895 | adapter (CRAM directe) |
| **`R_RenderPlayerView`** (r_main.c:152), **`R_DrawPlayerSprites`** (r_things.c) | — | **écrire** : pont vers le peintre SlaveDriver ; l'arme = sprite VDP1 (référence : `sat_vdp1_wpn_draw` dg_saturn.cxx:14576) |
| À **garder** du renderer : `R_PointInSubsector`, `R_PointToAngle2`, `R_PointToDist` (r_main.c ; appelés par p_enemy/p_inter/p_map/p_maputl/p_mobj/p_pspr/p_user/g_game/s_sound `[mesuré]` grep), `R_TextureNumForName`/`R_FlatNumForName`/`R_InitData` (r_data.c ; p_setup, p_spec, p_switch), `R_InitSpriteDefs` (r_things.c ; mobj→Sprite), `skyflatnum` (r_sky.c), `R_Init`/`R_SetViewSize` (menu détail) | — | « moins r_*.c » est donc **inexact** : ~1 200 l. de r_main/r_data/r_sky/r_things restent |

## (b) Les 5 pièges Mimas les plus coûteux à ne pas rejouer

1. **Boot loop par famine du pool HWRAM** — chaque `.bss` ajouté déplace `_end` ; le pool n'est pas monotone en taille de code (supprimer 200 o de `.text` a fait BAISSER le pool) ; pré-vol obligatoire du `.map` avant de livrer ; la marge se prend sur `HEAP_SIZE`, pas en coupant des fonctions `[mem boot-loop-can-be-tlsf-pool-starvation]`, `[mem heap-size-doubles-the-pool]`. Fork : l'aire `mem_malloc(1)` part aussi de `end` (saturn.ld:84-86) — même piège, `make size` à chaque build.
2. **Fichier périmé chargé en silence** — stash `-Wad` d'un `.bin` ancien (corrigé 2026-07-13) et `DOOMRP.DRP` non régénéré (crc ≠ ⇒ raw, ~4 min de boot) : « construis l'image que le propriétaire charge, et si tu changes laquelle, dis-le » `[mem build-stale-stash-wad-swap]`, `[mem drp-repack-must-be-rebuilt]`.
3. **`grep` sans `-a` s'arrête au « § » de dg_saturn.cxx:62, rend 0 et ne dit rien** — toute recherche dans `src/*.cxx` en `grep -a`/`rg` `[mem grep-truncates-dg-saturn]`.
4. **Ymir ≠ oracle de perf** — tic T 69-83 ms console vs 8-14 Ymir (6-9×) ; jamais calibrer un seuil sur Ymir ; Ymir verrouille aussi `build/*.bin` (erreur 1224) `[mem ymir-not-a-perf-oracle]`, `[mem ymir-locks-mimas-bin]`. Le fork a déjà sa loi de coût mesurée console (14,9 ms + 39,2 µs/cellule) : y rester.
5. **GFS et alignement** — destination 4-alignée obligatoire (`GFS_ERR_ALIGN`), offsets en **secteurs** pas en octets, lecture en place en cart ⇒ lumps paddés à 4 (`merge_wad.py` ne le fait pas), poignée persistante = régression sur ODE à seek instantané `[src]` w_file_saturn.cxx:380-396, 57-70 ; `[mem saturn-cart-lump-alignment]`, `[mem r2-persistent-cd-handle]`.

Bonus spécifique au chantier : la 2e `CDC_CdInit` derrière GFS (8 min de chargement CDDA) `[mem cdda-8min-load-diagnosis]` — le fork n'a pas de driver 68K, donc ce piège disparaît **si** on n'importe pas `SRL::Sound::Hardware::Initialize`.

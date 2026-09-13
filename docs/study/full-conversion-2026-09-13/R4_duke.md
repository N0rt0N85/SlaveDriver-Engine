# R4 — « Avoir l'impression de jouer à Duke » : couplage jeu ↔ moteur Build, et coût du portage sur SlaveDriver (2026-09-13)

Étiquettes : `[src]` lu dans le code, `[mesuré]` calculé ce jour (scripts dans le scratchpad : `r4_count.py`, extractions GRP/MAP inline), `[doc]` document du dépôt, `[web]` page consultée ce jour, `[est]` estimation.

## 0. Synthèse en cinq lignes

- Le jeu Duke (jfduke3d, ≈36 000 lignes utiles, GPL-2+) n'est **pas** un playsim autonome : il fait **1 302 appels** à 10 familles de fonctions du moteur Build et **≈3 950 accès directs** à ses tableaux (`sprite[]` 1 497, `sector[]` 587, `wall[]` 464…) `[mesuré]`. Doom, lui, ne touche son renderer que par 10 symboles / 74 usages `[doc DOOM_ON_SLAVEDRIVER.md §2]`.
- Le cœur non remplaçable par SlaveDriver est la **physique/collision** de `engine.c` (BUILDLIC) : **1 318 lignes** (clipmove, pushmove, getzrange, hitscan, cansee, neartag, updatesector, inside…) + 281 de géométrie + 273 de listes de sprites = **≈1 900 lignes à réécrire clean-room** `[mesuré]`. Les algorithmes sont décrits dans `jfbuild/doc/buildinf.txt` (882 lignes, signatures + sémantique) `[src]`. Aucune réimplémentation GPL-3-compatible connue : Raze = « Non-Build code GPL v2 » + Build sous BUILDLIC `[web]` ; NBlood/PCExhumed = GPL-2 seule `[src]` ; BuildGDX : non vérifié (404).
- Le **2D** (HUD, armes, menus, textes, intermission) passe par **un seul primitif** `rotatesprite` : 156 appels directs + ≈550 via 9 wrappers (`gametext` 219, `menutext` 148, `FTA` 81, `myospal` 68, `minitext` 55…) `[mesuré]`. Il se réécrit une fois sur VDP1 (`EZ_scaleSpr`/`EZ_distSpr` du fork font zoom + rotation).
- **CON** : 16 888 tokens, 113 mots-clés, 119 acteurs, interprète `parse()` 1 065 lignes exécuté **par acteur et par tic à 30 Hz** (TICSPERFRAME = 120/26 = 4 en entier) `[mesuré][src]`. Coût SH-2 estimé 5-25 ms/tic pour 30-60 acteurs `[est]` — c'est le risque n° 1, avant la RAM.
- **RAM** : tableaux Build « tels quels » (limites V8 de jfbuild) = **≈3,4 Mo** ; en limites V7 = 1,23 Mo ; dimensionnés pour E1L1-E1L6 (max 557 secteurs / 3 437 murs / 1 179 sprites `[mesuré]`) ≈ **500 Ko** — contre 432 Ko de HWRAM libre `[mesuré MAIN.map]` + 1 Mo LWRAM, dont il faut aussi loger le `.LEV` (589 Ko pour E1L1 `[doc]`). Ça ne rentre qu'avec un régime sévère et la LWRAM.

**Verdict** : ≈30 000 lignes GPL-2+ portables telles quelles, ≈1 900 à réécrire clean-room, ≈1 500 à ré-exprimer (2D + `animatesprites` + caméra + palette), une liste d'impossibles (miroirs, caméras, secteurs qui translatent/tournent, panning) — **≈2× l'effort de Doom** (55-90 j contre 25-40 j `[doc][est]`).

---

## 1. Inventaire des appels JEU → MOTEUR Build

Périmètre `[mesuré]` (`r4_count.py`, commentaires retirés) : `game.c actors.c player.c sector.c premap.c gamedef.c sounds.c rts.c menues.c global.c config.c osdcmds.c grpscan.c duke3d.h funct.h` (hors `astub.c` éditeur, `startwin/startgtk`). Tailles `[mesuré wc]` : game.c 10 422, actors.c 7 033, player.c 4 558, menues.c 4 539, gamedef.c 3 379, sector.c 3 263, premap.c 1 641, sounds.c 747, rts.c 264, global.c 183 → **36 029 lignes** de jeu.

| Catégorie | Appels | Détail (fonction : n · fichiers) | Sort sur SlaveDriver |
|---|---|---|---|
| **Physique / collision** (BUILDLIC) | **108** | `updatesector` 39 (game 12, actors 11, gamedef 6, sector 5, player 4) · `cansee` 26 (actors 10, player 8, gamedef 6, sounds 2) · `hitscan` 13 (player 10) · `clipmove` 11 (actors 5, player 4, game 2) · `pushmove` 6 · `neartag` 6 (sector 5) · `updatesectorz` 3 · `getzrange` 2 · `clipinsidebox` 2 | **Réimplémenter clean-room** |
| **Géométrie pure** | **133** | `getangle` 73 · `getflorzofslope` 20 · `nextsectorneighborz` 13 (sector 11) · `getceilzofslope` 9 · `ksqrt` 8 · `rotatepoint` 5 (actors, SE0/1/11) · `krand` 3 · `getzsofslope` 1 · `alignflorslope` 1 | Réécrire (trivial, mais le **LCG de `krand`** doit être identique pour la fidélité) |
| **Listes de sprites** (structure moteur) | **179** | `changespritestat` 85 (game 79) · `setsprite` 52 (actors 44) · `deletesprite` 24 · `changespritesect` 14 · `dragpoint` 3 (SE20) · `insertsprite` 1 | Réécrire (listes doublement chaînées par secteur et par statut, 273 l. dans engine.c) |
| **Rendu 3D** | **144** | `nextpage` 42 · `clearallviews` 35 · `flushperms` 13 · `setview` 12 · `setgamemode` 8 · `setbrightness` 7 · `drawrooms` 5 · `drawmasks` 5 · `setviewtotile` 4 (caméras) · `setviewback` 3 · `setpalettefade` 2 · `preparemirror`/`completemirror` 1+1 · `drawmapview` 1 · `setrollangle` 1 · `setaspect` 2 · `makepalookup` 1 | **Remplacé par SlaveDriver** (caméra, présentation) ; miroirs/caméras : impossibles |
| **Rendu 2D** | **184** directs | `rotatesprite` **156** (game 85, menues 63, player 6, premap 2) · `printext256` 18 (game, debug) · `drawline256` 10 (menues, carte) | **Réécrire un primitif** (§4.2D) |
| **Données ART/MAP** | 11 | `loadboard` 1, `loadpics` 1, `loadtile` 1, `invalidatetile` 4, `allocatepermanenttile` 1, `squarerotatetile` 1, `loadmaphack` 1, `saveboard` 1 | Remplacé par le convertisseur hors ligne + `.LEV` |
| **Fichiers / cache (cache1d, BUILDLIC)** | 289 | `kdfread` 87 + `dfwrite` 76 (**sauvegardes**, menues.c) · `kread` 39 · `kclose` 30 · `kopen4load` 19 · `allocache` 9 · `kfilelength` 9 | Réécrire sur `FILE.C` du fork ; sauvegarde à repenser (§7) |
| **Baselayer (OS)** | 203 | `buildprintf` 143 · `handleevents` 23 · `buildputs` 12 · divers | Shim trivial |
| **Réseau (mmulti)** | 33 | `sendpacket` 23, `getpacket` 1… | Hors sujet, supprimer |
| **OSD/console** | 18 | `OSD_RegisterFunction` 17 | Supprimer |
| **Total appels** | **1 302** | | |

**Accès directs aux données du moteur** `[mesuré]` : `sprite[]` 1 497 (actors 670, player 333, game 211, sector 179) · `sector[]` 587 · `wall[]` 464 (player 159, sector 121) · `headspritestat/nextspritestat` 81+83 · `headspritesect/nextspritesect` 51+52 · `tsprite[]` 37 + `spritesortcnt` 40 (**le jeu modifie la liste triée du renderer** dans `animatesprites`, game.c:5160-5931, 772 l.) · `tilesizx/y` 33+51 · `picanm` 8 · `waloff` 19 · `walock` 20 · `totalclock` 147 · `sintable[]` 219 · `xdim/ydim` 222+228 · `windowx1..y2` 115 · `gotpic` 7 · `pskyoff` 21. **Macros `pragmas.h`** (BUILDLIC) : 421 usages (`klabs` 126, `mulscale*` 103, `ksgn` 34, `dmulscale*` 27, `scale` 22, `clearbufbyte` 19…) — à redéfinir dans un en-tête à nous (arithmétique fixe évidente).

**Cadence** `[src duke3d.h:100-101]` : `TICRATE 120`, `TICSPERFRAME (TICRATE/26)` = **4** en division entière → **30 tics/s** (pas 26). Boucle : `while (totalclock >= ototalclock+TICSPERFRAME) domovethings()` (game.c:8136-8479), interpolation des sprites par `smoothratio` (game.c:8109). `MOVEFIFOSIZ 256` sert au réseau (game.c 37 usages).

---

## 2. Taille de la réimplémentation clean-room

Spans dans `jfbuild/src/engine.c` (11 422 l.) `[mesuré]` :

| Fonction | Lignes | Fonction | Lignes |
|---|---|---|---|
| `clipmove` 9131-9448 | **318** | `hitscan` 8652-8922 | **271** |
| `getzrange` 9703-9895 | 193 | `pushmove` 9454-9584 | 131 |
| `neartag` 8928-9045 | 118 | `cansee` 8602-8646 | 45 |
| `updatesector`/`updatesectorz` | 34+37 | `inside` / `clipinsidebox` / `clipinsideboxline` | 23+25+25 |
| `raytrace` / `keepaway` / `clippoly4` (aides de clipmove) | 32+16+50 | **Sous-total physique** | **1 318** |
| `nextsectorneighborz` 63 · `lintersect` 27 · `rintersect` 26 · `getzsofslope` 18 · `getflorzofslope`/`getceilzofslope` 12+12 · `alignflor/ceilslope` 16+16 · `sectorofwall` 17 · `lastwall` 16 · `loopnumofsector` 14 · `getangle` 11 · `rotatepoint` 11 · `ksqrt` 4 + `initksqrt` 13 · `krand` 5 | **281** | Listes : `insertspritesect/stat` 23+23, `deletespritesect/stat` 19+19, `initspritelists` 29, `setsprite`/`setspritez` 17+17, `insertsprite`/`deletesprite` 5+5, `changespritesect/stat` 9+9, `dragpoint` 41, `setfirstwall` 57 | **273** |

**Total ≈ 1 872 lignes** de C dense (fixe 16.16, `MAXCLIPNUM` de `clipit[]`, engine.c:530-532). `loadboard` (123 l.) est inutile : le `.MAP` est converti hors ligne dans un binaire à nous. Les structures `sectortype` 40 o / `walltype` 32 o / `spritetype` 44 o (`build.h:107-173`) sont le **format de fichier MAP v7** : on définit nos propres structs équivalentes (un format n'est pas une expression protégée `[est juridique]`).

**Documentation disponible** `[src jfbuild/doc/buildinf.txt]` : signatures et sémantique de `clipmove` (l.530), `pushmove` 553, `getzrange` 560, `hitscan` 579, `neartag` 622, `cansee` 635, `updatesector` 645, `inside` 652, `changespritesect` 734, `setsprite` 747. Plus le code review public de F. Sanglard (2013) `[connaissance générale, non vérifié ce jour]`.

**Levier décisif** : test différentiel sur PC — notre clean-room compilé à côté de jfbuild, nourri des mêmes entrées (positions, vecteurs) sur E1L1-E1L6, comparaison bit à bit des sorties. Il n'y a pas de meilleur filet pour 1 900 lignes de collision.

**Implémentations sous licence compatible ?** Aucune connue : Raze README `[web]` : « Non-Build code is licensed under the GPL v2 » + « "Build Engine & Tools" … See BUILDLIC.TXT » → doublement inutilisable ; NBlood/PCExhumed : « GNU GPL version 2 » sans « or later » (`NBlood/source/blood/src/actor.cpp:9`, `exhumed/...:9`) `[src]` ; jfbuild/eduke32/kenbuild : BUILDLIC (`jfbuild/LICENSE:1-4`, `buildlic.txt [2][3]`) `[src]` ; BuildGDX : **je ne sais pas** (dépôt m210/BuildGDX en 404 ce jour).

---

## 3. CON : compilateur, interprète, RAM

**Bytecode** `[src]` : `int script[MAXSCRIPTSIZE+16]` (global.c:114) avec `MAXSCRIPTSIZE 32768` (duke3d.h:164) = **128 Ko** statiques ; labels `calloc(MAXLABELS=4096, MAXLABELLEN=64)` = **256 Ko** + `labelcode` 16 Ko (game.c:7477-7479), `realloc` à `labelcnt` après compilation (7490). `actorscrptr[MAXTILES=9216]` 36 Ko + `actortype` 9 Ko.

**Taille réelle du shareware 1.3D** `[mesuré]` (GRP 215 entrées, 11 032 323 o ; CON extraits) : DEFS.CON 28 893 o, GAME.CON 99 639 o, USER.CON 36 960 o ; **16 888 tokens** hors commentaires (2 820 / 10 544 / 3 524). Chaque token ≈ 1 mot de bytecode, `if*` ajoute un pointeur else : `[est]` **17-20 k mots = 70-80 Ko** — donc `MAXSCRIPTSIZE` peut tomber à ~20 k et le bytecode vivre en LWRAM. Contenu : 119 `actor`/`useractor`, 136 `state`, 189 `action`, 76 `move`, 83 `ai` (GAME.CON), 1 043 `define`, 291 `definesound`, 122 `definequote`. **113 mots-clés** (`keyw[]`, gamedef.c:63-193) `[mesuré]`.

**Recommandation** : compiler les CON **hors ligne** (port GPL-3 de `parsecommand`, 1 080 l., dans l'outil PC) et n'embarquer que l'interprète (`parse` 1 065 l. + `execute` 88 + `move` 196 + aides ≈ 1 500 l.) : on économise 256 Ko de labels et le texte CON sur la console. Les scripts 3D Realms restent chez l'utilisateur (comme l'IWAD).

**Coût CPU** `[est]` : `execute()` (gamedef.c:3194-3281) est appelé pour chaque sprite de statut ACTOR (actors.c:2989 boucle `headspritestat[1]`, l.4258), STANDABLE (l.2246/2250), PROJECTILE (2635), EFFECTOR (4673) ; les acteurs endormis (ZOMBIEACTOR, `movefta` 741-837) ne font qu'une distance et un `cansee` **throttlé** par `timetosleep >= x>>8` (l.762). Par acteur actif et par tic : 30-80 mots dispatchés dans le `switch` de `parse()` (1-5 µs/mot à 28 MHz si le script est en LWRAM, 2,1× plus lente `[doc]`) + `move()` → `movesprite` → **`clipmove`** (actors.c:459-543 ; boucle sur les murs du secteur et les sprites, avec `ksqrt`/divisions) 100-300 µs. **30-60 acteurs → 5-25 ms/tic** sur les 33 ms d'un tic 30 Hz, sur le même SH-2 maître qui porte déjà 14,9 ms + 39,2 µs/cellule de rendu `[doc STEXT_BASELINE]`. À mesurer au premier jalon ; le plan B est de placer le playsim sur l'esclave (SlaveDriver l'utilise pour les transformations — à arbitrer).

**RAM des tableaux Build** `[mesuré depuis build.h/duke3d.h ; sizeof(weaponhit)=72 o calculé sur duke3d.h:451-460]` :

| Tableau | jfbuild V8 (4096 / 16384 / 16384) | Limites V7 (1024 / 8192 / 4096) | **Proposé E1Lx** (640 / 4096 / 1536) |
|---|---|---|---|
| `sector[]` ×40 o | 160 Ko | 40 Ko | **25 Ko** |
| `wall[]` ×32 o | 512 Ko | 256 Ko | **128 Ko** (LWRAM) |
| `sprite[]` ×44 o | 704 Ko | 176 Ko | **66 Ko** |
| `hittype[]` ×72 o | 1 152 Ko | 288 Ko | **108 Ko** |
| listes prev/next sect/stat ×8 o + heads | 138 Ko | 34 Ko | **13 Ko** |
| `spriteext[]` ×12 o (modèles 3D) | 195 Ko | 51 Ko | 0 (supprimé) |
| `tsprite[2048]` ×44 o | 88 Ko | 88 Ko | 0 (liste de tirage SlaveDriver) |
| tables tuiles (`tilesizx/y`, `picanm`, `waloff`, `walock`, `actorscrptr`, `actortype`, `gotpic`) | 167 Ko (MAXTILES 9216) | 167 Ko | **43 Ko** (MAXTILES 3328 = 13 ART × 256 `[mesuré]`, sans `waloff`/`walock`) |
| `script[]` | 128 Ko | 128 Ko | **80 Ko** (taille réelle, LWRAM) |
| labels (`label`+`labelcode`) | 272 Ko | 272 Ko | 0 (compilation hors ligne) |
| `inputfifo[256][16]` ×10 o + `syncval` + `my*bak` | 49 Ko | 49 Ko | **<1 Ko** (MAXPLAYERS 4, MOVEFIFOSIZ 8) |
| `ps[16]` (~1 Ko chacun) + sons (`SoundOwner[450][4]` 14 Ko, `sounds[450][14]` 6 Ko, tables 5 Ko) + `animwall[512]` 4 Ko + `cyclers` 3 Ko + interpolations 2048×12 = 24 Ko + `spriteq` 2 Ko | ≈75 Ko | ≈75 Ko | **≈40 Ko** (4 joueurs, NUM_SOUNDS 300, 256 interpolations) |
| **Total** | **≈3,4 Mo** | **≈1,23 Mo** | **≈505 Ko** |

Contexte : les six cartes shareware font au plus **557 secteurs, 3 437 murs, 1 179 sprites** (E1L4) `[mesuré en-têtes .MAP v7]` ; 1 536 sprites laissent ~350 emplacements dynamiques (projectiles, débris, `lotsofglass`). HWRAM libre du build courant : **432 008 o** (`MAIN.map` : fin `.bss` 0x06096878 → 0x06100000) `[mesuré]` ; LWRAM 1 Mo (aire 0, `mem_malloc(0,…)`) `[doc PORTING_NOTES:85-86]`. Le `.LEV` d'E1L1 pèse 589 Ko `[doc DOOM_ON_SLAVEDRIVER §6]`. **Bilan** : ≈505 Ko de playsim + ≈600 Ko de modèle de rendu + tuiles/sons > 1,43 Mo disponibles au total si le rendu garde sa part actuelle — il faut un régime (sprites 1 280 ? `hittype` réduit aux champs lus ?) et un `.map` du jour pour trancher. La cartouche 4 Mo n'est pas acceptée par `validPtr` (`UTIL.H:74`) `[doc]`.

---

## 4. Sous-systèmes Duke à porter (lignes `[mesuré]`, dépendances `[src]`)

| Sous-système | Lignes | Dépend du moteur pour | Traitement |
|---|---|---|---|
| **player.c** — `processinput` 2343-4118 (**1 776 l.** : marche, accroupi, saut, jetpack, natation, bob, `look_ang`/`rotscrnang`), `shoot` 312-1174 (863 l., 10 `hitscan`), `displayweapon` 1355-1849 (495 l., **64 blits** `myos/myospal/rotatesprite`), `computergetinput` 371 (bot, à jeter) | 4 558 | `clipmove` 4, `pushmove` 2, `getzrange` 1, `hitscan` 10, `cansee` 8, `getflorzofslope` 7, `sintable` 100, `wall[]` 159 | Tel quel + clean-room physique ; `displayweapon` → primitif 2D |
| **actors.c** — `moveeffectors` 4805-7032 (**2 228 l.**, 42 `case` SE), `moveactors` 1 277, `movestandables` 900, `moveexplosions` 536, `moveweapons` 344, `movetransports` 342 (SE7 eau/téléport), `hitradius` 179, `movesprite` 85 | 7 033 | `clipmove` 5, `cansee` 10, `setsprite` 44, `changespritesect` 10, `rotatepoint` 5, `dragpoint` 3, `ms()` 15 sites (murs déplacés), `sprite[]` 670 | Tel quel ; SE géométriques : voir §5 |
| **sector.c** — `operatesectors` 437 (17 `case` ST : 9, 15-23, 25-31), `checkhitsprite` 437, `checkhitswitch` 377, `checksectors` 337, `checkhitwall` 238, `animatewalls` 94 (panning `xpanning` 7 refs) | 3 263 | `neartag` 5, `nextsectorneighborz` 11, `deletesprite` 8, `wall[]` 121 | Tel quel |
| **gamedef.c** — `parsecommand` 1 080 (compilateur), `parse` 1 065 (interprète), `move` 196, `alterang` 67, `dodge`/`furthestangle`… | 3 379 | `cansee` 6, `updatesector` 6, `hitscan` 2, `getangle` 10 | Compilateur → outil PC ; interprète tel quel |
| **premap.c** — `prelevel` 314 (spawn initial, miroirs 868-889), `cachespritenum`/`cacheit`/`cachegoodsprites` 270 (précache des tuiles par lecture du CON), `enterlevel` 94 | 1 641 | `loadboard`, `loadtile`, `allocache`, `gotpic` | Tel quel moins le cache (fait hors ligne par le convertisseur : le jeu de tuiles d'un niveau **est** ce que `cacheit` calcule) |
| **sounds.c** — `xyzsound` 133, `pan3dsound` 79, `sound` 48, MIDI | 747 | jfaudiolib (`FX_*`, `MUSIC_*`) : GPL-2+ `[web fx_man.c : « either version 2 … or (at your option) any later version », Apogee 1994-1995]` | Réécrire le backend sur `SOUND.C` du fork (`playSoundE`, `posMakeSound`, `MAXNMSOUNDS 80` `[src SOUND.C:40]`, `assert(soundTop+size<1024*512)` `[src SOUND.C:218]`) ; musique → `playCDTrack` `[src SOUND.C:387-389]` |
| **rts.c** | 264 | — | Supprimer (railleries multijoueur) |
| **menues.c** — `menus` 764, `loadplayer`/`saveplayer` 312+161, `drawoverheadmap` 247 (10 `drawline256`), `menutext`/`probe`/`bar` ≈600, `playanm` 90 | 4 539 | `rotatesprite` 63, `kdfread`/`dfwrite` 160, `sector/wall/sprite` 68 (carte) | Logique telle quelle ; dessin → primitif 2D ; sauvegarde → §7 ; ANM : **0 fichier .ANM dans le GRP shareware** `[mesuré]`, écrans = tuiles |
| **game.c** — `spawn` 1 855 (355 `case`), `animatesprites` 772, `dobonus` 672 (intermission), `displayrest` 249 (HUD), `displayrooms` 239 (caméra, miroirs 3081-3109, `setviewtotile` caméras), `coolgaugetext` 236, statusbar (`displayinventory` 60, `weapon_amounts` 55, `digitalnumber` 24, `patchstatusbar` 32…), `FTA`/quotes, `cheats` 75 ; **à jeter** : `getpackets` 327, `faketimerhandler` 285, `fakedomovethings` 468, `checkcommandline` 323, démo 310, `SE40_Draw` 134 (1.5), `typemode` 115, joystick 220 | 10 422 → ≈7 900 utiles | `rotatesprite` 85, `tsprite`/`spritesortcnt` 77, `drawrooms`/`drawmasks` 4+4, `nextpage` 33, `clearallviews` 27, `changespritestat` 79 | `animatesprites` → hook de synchro par objet visible (frame 8 directions `k=((ang+3072+128-a)&2047)>>8` game.c:5197, `xrepeat/yrepeat`, ombres, flips) ; `displayrooms` → caméra SlaveDriver |

**2D — le chiffre qui compte** `[mesuré]` : 156 `rotatesprite` directs dont angle ≠ 0 : **27**, zoom ≠ 65536/32768 : **32**, `pal` ≠ 0 : 20, `shade` ≠ 0 : 74 ; orientations les plus fréquentes : `10` (41), `2+8+16+64` (24), `8+16+64+128` (17), `2+8` (14), `2+16` (10) — c.-à-d. translucide (2), sans clip (8), centré (16), coordonnées 320×200 (64), tuile complète (128). Wrappers : `gametext` 219, `menutext` 148, `FTA` 81, `myospal` 68, `minitext` 55, `gametextpal` 16, `digitalnumber` 9, `displayfragbar` 6, `menutextc` 1 → **≈700 sites** convergent vers **un** primitif *tuile(x, y, zoom, angle, shade, pal, flags, clip)* + 3 fontes en tuiles + ligne. Côté fork : `EZ_scaleSpr` (zoom), `EZ_distSpr` (quad quelconque = rotation), `EZ_line`, `COMPO_TRANS` (translucidité), gouraud (shade), `drawString` (`PRINT.C`), over-pics VDP2 (`MENU.C:227-254`) `[src SPR.C, WALLS.C:2669-2680, PRINT.C:128]`. Le VDP1 fait zoom/rotation/demi-transparence nativement `[doc HW_VDP1.md]` ; `pal` ≠ 0 = autre banque CRAM (Duke a 25 `pal` mais 20 usages 2D seulement).

---

## 5. Mapper le monde Duke sur l'architecture C

Principe (identique à Doom `[doc DOOM_ON_SLAVEDRIVER §3C]`) : le playsim garde `sector[]/wall[]/sprite[]` (binaire compact dérivé du `.MAP`), le `.LEV` sert au rendu, une passe par tic synchronise. Ce qui diffère de Doom, point par point :

| Mécanisme Duke | Fréquence `[mesuré .MAP / doc]` | Support SlaveDriver `[src]` | Verdict |
|---|---|---|---|
| Hauteurs sol/plafond (portes ST, ascenseurs, SE13/18/21/31/32/35, `doanimations` sector.c:274-334) | Partout | Sommets `y` des murs-sol/plafond ; push-blocks écrivent déjà `level_vertex[].y` (SPRITE.C:870) | **OK** — même recette que Doom §5.1 |
| Pentes (`floorheinum`, `getflorzofslope` engine.c:10550) | 44-225 secteurs pentus par carte (E1L5 : 225) | Faces libres ; `findFloorDistance` gère une normale quelconque (UTIL.C:39-64) | **OK statique** ; SE2 séisme (`floorheinum` animé) : ignorer |
| **Translation de secteurs** SE6/14/30 métro, SE0/1 pivot, SE11 porte battante, SE20 pont (`ms()`/`dragpoint`/`rotatepoint`, actors.c:710-735, 6339-6340) | E1L1 : 0 métro ; répartition SE E1L1 = 12:45, 4:35, 13:22 `[doc]` | Le moteur translate des sommets (push-blocks SPRITE.C:824-826, axes verrouillés `PBVFLAG_*LOCK` SLEVEL.H:106-108) mais **ne recalcule jamais une normale** (seule écriture : SRUINS.C:1958, inversion X d'un niveau entier) ; normales lues par HITSCAN.C (15), SPRITE.C (46), WALLS.C (24), UTIL.C (13) | Translation d'un **secteur-îlot** : possible tant qu'il reste dans sa cellule convexe (la découpe convexe du parent est figée → un wagon qui traverse plusieurs cellules casse `nextSector`) → **parquer SE6/14/30**. Rotation SE0/1/11 : ajouter un recalcul de normale (~50 l.) ; plans de coupe (`cutPlane`, LEV_FORMAT:117) deviennent faux → **dégradé, à tester** |
| Panning/scrolling de textures (`animatewalls`, `xpanning` 7 refs ; SE24/34 tapis = sprites seulement) | E1L1 : `animwall` 36 refs code | Tuile par cellule = 1 octet orientation (8 flips) + 1 octet tuile (LEV_FORMAT:95-100), **aucun UV** ; `advanceWallAnimations` ne fait que cycler des tuiles (PIC.C:117-128) | **Dégradé** : statique, ou cyclage de tuiles pré-décalées (coûte le cache 28) |
| Sprites face (cstat 0) | 493/639 E1L1 ; 1 007/1 179 E1L4 | Billboards `EZ_scaleSpr`, ombre, `SPRITEFLAG_FLASH`, 5 palettes d'objet (`NMOBJECTPALLETES` UTIL.H:24) ; **pas de sélection de frame par angle de vue** (WALLS.C:2799 `frame=o->frame+…`) | OK ; les 8 directions se font côté hook `animatesprites` (le jeu choisit déjà `picnum`) |
| **Sprites muraux** (cstat 16 : interrupteurs, affiches) / **au sol** (cstat 32 : flaques) | **138 / 8** E1L1, 171/1 E1L4, 157/14 E1L3 | **Aucun** : seuls billboards + `SPRITEFLAG_LINE`/`THINLINE` (lasers, WALLS.C:2656-2680) | **À ajouter** : quad `EZ_distSpr` collé au mur/sol (100-200 l. WALLS.C) — indispensable, les interrupteurs en dépendent |
| Miroirs (`overpicnum` 560, `preparemirror`) | E1L1 : 4 murs (2 secteurs), E1L2 : 3, E1L3-6 : 0 | Aucun second passage | **Impossible** → mur opaque |
| Caméras/écrans (`setviewtotile` 4, `animatecamsprite`) | présents E1L1 | Pas de rendu en texture | **Impossible** → tuile fixe |
| Eau SE7 lotag 1/2 (téléport entre deux secteurs, `movetransports` 2752/2771) | E1L4 : 32 paires | `SECFLAG_WATER`, `WALLFLAG_WATERSURFACE`, `SPRITEFLAG_UNDERWATER`, ondulation `stepWater` (WALLS.C:449) | **OK** : le playsim téléporte, le `.LEV` contient les deux secteurs ; effet visuel optionnel |
| Murs masqués/1-way/translucides | 19/8/7 E1L1 ; 46 masqués E1L3 | Textures avec transparence, `COMPO_TRANS` | OK (déjà traité par duke2ps E4) |
| Lumière `shade` secteur/mur/sprite, `visibility` (5 refs), `pals` (flash rouge, lunettes) | Partout | `light` 0..31 par sommet + biais secteur −16..16 (LEV_FORMAT:70,105) ; pas de brouillard | OK statique ; SE3/4/12 lumières clignotantes = réécrire `light` des sommets du secteur par tic (peu cher) ; palette écran → offset couleur VDP2 |
| ROR SE40-45, TROR | 1.5 seulement | — | Hors périmètre 1.3D |

---

## 6. Matrice des licences (fork GPL-3, `LICENSE.txt` `[doc]`)

| Composant | Licence `[src/web]` | Dans le dépôt ? | Conséquence |
|---|---|---|---|
| jfduke3d `src/*.c` (jeu) | GPL-2 **ou ultérieure** (`game.c:7-10`) | **Oui** (mentions conservées) | ≈30 000 l. portables |
| jfbuild `engine.c`, `pragmas.h`, `cache1d.c`, `build.h` | BUILDLIC (`jfbuild/LICENSE:1-4` ; clauses [2] Internet seulement, [3] gratuit, [5] commercial sous licence) | **Non** | 1 900 l. clean-room + macros + structs à nous ; `buildinf.txt` est une doc, pas du code |
| eduke32 (moteur + `audiolib`), NBlood, PCExhumed, Mapster32 | BUILDLIC / GPL-2 seule (`fx_man.cpp:8`, `actor.cpp:9`) | **Non** | Ne rien copier, même l'audiolib d'eduke32 |
| jfaudiolib (jonof) | GPL-2+ (« or (at your option) any later version », Apogee 1994-95) `[web]` | Possible mais inutile | On cible le SCSP via `SOUND.C` du fork |
| jfmact (`control.c`) | **Aucun en-tête de licence** (« Derived from MACT386.LIB disassembly ») `[web]` | **Non** | Inutile (pads SMPC) |
| Raze | « Non-Build code … GPL v2 » + Build BUILDLIC `[web README]` | **Non** | — |
| BuildGDX | non vérifié (404) | Supposer non | — |
| CON / ART / MAP / VOC / MID 3D Realms | `LICENSE.TXT [2][3][C][7][D]`, `[4][A]` interdit les niveaux fonctionnant avec l'épisode 1 `[doc DUKE_PC_TO_SATURN §3]` | **Jamais** | Convertis **localement** par l'outil (WAD-like) ; cartes shareware converties = recherche, pas de diffusion ; CON compilé hors ligne chez l'utilisateur |
| Notre convertisseur (duke2ps, CON→bytecode), notre clean-room, le primitif 2D | GPL-3 | Oui | — |

---

## 7. Verdict chiffré

| Poste | Volume | Nature |
|---|---|---|
| **Porter tel quel** (GPL-2+) | **≈30 000 l.** : actors 7 033 + player ≈4 190 + sector 3 263 + gamedef (interprète) ≈1 500 + premap ≈1 370 + game ≈7 900 + menues ≈2 500 (logique) + sounds ≈500 + global 183 + `spawn` inclus | Adaptation : en-tête `pragmas` à nous, types, retrait réseau/démo/OSD |
| **Réimplémenter clean-room** | **≈1 900 l.** d'engine.c (physique 1 318 + géométrie 281 + listes 273) + LCG `krand`, `sintable`, `getangle` tabulé, `ksqrt` | + harnais différentiel PC contre jfbuild |
| **Réécrire** (services moteur) | **≈1 500 l. nouvelles** : primitif 2D (300-500) + fontes/over-pics ; `animatesprites` → hook objets (≈400) ; `displayrooms` → caméra (≈80) ; palette/fade (≈50) ; synchro hauteurs + lumières par tic (≈300) ; quads muraux/sol dans WALLS.C (100-200) ; backend son SCSP (≈300) | Touche ≈700 sites d'appel 2D par 9 wrappers |
| **Outil PC** (GPL-3) | Compilateur CON (port de `parsecommand` 1 080 l.) ; `.MAP` → binaire compact ; sélection du jeu de sons par niveau (l'équivalent de `cacheit`) ; VOC → PCM 8 bits SCSP ; MID → pistes CDDA (rendu local) | duke2ps E0-E6 existe déjà pour géométrie/tuiles `[doc]` |

**Impossible / dégradé** : miroirs (impossible) ; caméras de surveillance (impossible) ; secteurs qui translatent SE6/14/30 (impossible sans re-découpe dynamique → parquer) ; rotation SE0/1/11 (dégradé, normales + plans de coupe) ; panning de textures (dégradé) ; SE2 pentes animées (ignoré) ; brouillard `visibility` (approximé) ; **sauvegarde** : `saveplayer` écrit `hittype[MAXSPRITES]` entier + `ps` + tableaux (menues.c:484-644 → ≥300 Ko) contre une BUP interne de 32 Ko `[doc HW_BACKUP_BUP]` → **checkpoint** (niveau + inventaire), comme PowerSlave ; **son** : 181 VOC = 2 844 Ko (moyenne 15,7 Ko, médiane 10,6 Ko ; taux 8 000 Hz ×107, 11 025 ×36, 5 988 ×29 ; `BONUS.VOC` 268 Ko) `[mesuré]` contre 512 Ko de RAM SCSP et `MAXNMSOUNDS 80` → ≈40-60 sons par niveau, voix longues coupées ou streamées ; **musique** MIDI → CDDA par niveau (pas de `music` CON dynamique) ; réseau/démos/RTS : supprimés.

**Effort** `[est]` : clean-room + harnais 10-15 j ; CON hors ligne + interprète 5-8 j ; primitif 2D + fontes + HUD/menus 10-15 j ; hook objets + caméra + quads muraux 8-12 j ; SE/ST vérifiés carte par carte 10-20 j ; son 5-8 j ; régime RAM + `.map` 5-10 j → **55-90 jours**, contre 25-40 j pour les jalons 3-4 de Doom `[doc DOOM_ON_SLAVEDRIVER §7]`.

### Doom vs Duke sur 12 axes

| Axe | Doom (Mimas/core) | Duke (jfduke3d) | Plus dur ? |
|---|---|---|---|
| Couplage au renderer | 10 symboles / 74 usages `[doc]` | 1 302 appels + ≈3 950 accès tableaux `[mesuré]` | **Duke ×20** |
| Propriétaire des structures monde | Le playsim (GPL) | Le moteur (`build.h`, BUILDLIC) → structs à redéfinir | Duke |
| Physique / collision | `p_map.c`, `p_maputl.c` (GPL, dans le playsim) | `engine.c` 1 318 l. clean-room | **Duke** |
| Comportements | Tables `info.c` + `p_enemy.c` compilés | CON : compilateur 1 080 + interprète 1 065 l., 128 Ko + CPU par acteur/tic | **Duke** (CPU = risque n° 1) |
| 2D (HUD/menus/textes) | `V_DrawPatch` maison (GPL) | `rotatesprite` moteur, ≈700 sites via 9 wrappers | Duke, mais **un seul** primitif |
| Géométrie mobile | Verticale seulement | Verticale + translation + rotation + panning | **Duke** (impossibles) |
| Sprites | Billboards, 8 rotations | Billboards + **muraux (22 % E1L1)** + sol ; `pal` | Duke (quads à ajouter) |
| Effets de rendu spéciaux | Ciel, `F_SKY1` | Miroirs, caméras, translucidité murs, lumières SE | Duke (impossibles) |
| Son | Lumps 8 bits 11 kHz, ~110 sons | 181 VOC 2,8 Mo, 3 taux, voix longues | Duke (budget SCSP) |
| Musique | MUS → CDDA | MID → CDDA | Égal |
| Tic | 35 Hz | 30 Hz (`120/26 = 4`) | Égal |
| Licence | GPL-2+ intégral | Jeu GPL-2+, moteur BUILDLIC, audiolib GPL-2+ | Duke (réécriture obligatoire) |
| RAM playsim | 150-250 Ko `[doc]` | ≈505 Ko après régime (≈3,4 Mo naïf) | **Duke ×2-3** |
| Sauvegarde | `P_Archive*` compact | Dump des tableaux (≥300 Ko) | Duke |

**Ce qui reste incertain** : (1) le coût réel de `parse()`+`clipmove` sur SH-2 — mesurer avant tout le reste (un acteur, un tic, `STATUSTEXT`) ; (2) la tenue des plans de coupe sous rotation ; (3) le budget RAM exact (re-dériver du `.map`) ; (4) la licence BuildGDX ; (5) si les structs `sectortype/walltype/spritetype` redéfinies à l'identique sont juridiquement « format de fichier » — poser la question avant de committer.

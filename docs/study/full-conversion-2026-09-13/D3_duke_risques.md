# D3 — « Duke/Build et risques d'abord » : plan pour convertir TOUT un WAD / un GRP sur le fork SlaveDriver (2026-09-13)

Synthèse des rapports R1-R6 sous l'angle des risques qui peuvent tuer le projet, puis la voie Duke. Étiquettes : `[src]` lu dans le code, `[mesuré]` calculé par les scripts des rapports, `[doc]`, `[est]`. Re-vérifié ce jour dans le fork : `UTIL.C:340-399` (LIFO 8 niveaux, repli inter-aires), `SRUINS.C:1903` (slots `{28,31,1,10,12}`), `SPRITE.C:916-938` (`moveSpriteTo`, O(n) sur la liste du secteur), `WALLS.C:1336,1374` (mur **sauté en silence** si `height*width+nmSlavePolys+50>1300`), `PIC.C:262-272,47` (une tuile utilisée dans la frame EST évincible sauf `PICFLAG_LOCKED`), `AICOMMON.C:72-100` (facing 0..7, bornes ±23°), `SPRITE.C:866-871` (`level_vertex[].y+=dy`), `SRUINS.C:2240` (`mem:%dk+%dk`), `duke3d.h:100-101` (`120/26=4` → 30 Hz), `engine.c:8602-9703` (spans clean-room).

## 0. Les huit risques, dans l'ordre où ils tuent

| # | risque | fait qui le fonde | instrument qui tranche | mitigation (coût) | kill criterion |
|---|---|---|---|---|---|
| R-1 | **Mur mémoire** : playsim Doom + `.LEV` + code + tuiles + sprites + sons | plan A ne rentre **nulle part** (E1M1 −54 Ko LWRAM) ; B rentre E1M1/E1M8 (+185), pas E1M6 (−325) ; C (cart) partout `[mesuré R5 §6]` ; HWRAM libre 432 008 o `[mesuré MAIN.map]`, ≈345 Ko après échange de code `[est R5 §2]` | `mem:` STATUSTEXT `[src SRUINS.C:2240]` avec E1M1 complet ; `make size` à chaque build | **B = plancher** (Saturn nue) : segs supprimés (−10 Ko), sommets partagés (−70..−90 Ko `[est]`), tuiles murs en 0x72 LWRAM (`[src PIC.C:603-610]`, ~3 ms/swap, 0-1/frame `[doc STEXT]`), `objects[]` retiré (−50 Ko), tables Doom en LWRAM (+70 Ko HWRAM) ; **C = déverrouillage** E1M6/Doom II | `mem:` < 64 Ko après chargement d'E1M1 avec sprites+sons+zone ⇒ B mort ⇒ public réduit aux cartouches |
| R-2 | **Géométrie mobile** : portes/lifts à 35 Hz ; Duke : secteurs qui translatent/tournent | le moteur ne bouge que `level_vertex[].y` `[src SPRITE.C:870]`, **jamais une normale** `[R4 §5]` ; `open_doors()` fige tout ouvert `[src doom3d.py:479-512]` ; le budget esclave compte les cellules totales `[src WALLS.C:1374]` | compteur de murs sautés (1 ligne au `return` de WALLS.C:1374) + `calc` porte fermée/ouverte | **Fermé + course par rangée** : contremarche en rangées de 64 u (N murs), portail hauteur 0 fermé, synchro = translation des sommets (≈30 µs/secteur mobile `[est]`) ; reliquat étiré confiné à la rangée haute ; Duke SE6/14/30 **parqués**, SE0/1/11 = recalcul normale (~50 l.) | murs sautés > 0 sur E1M1 ⇒ moins de cellules ; Duke : plans de coupe faux sous rotation ⇒ SE0/1/11 parqués |
| R-3 | **Densité de sprites** : 20-40 monstres, `drawSprites` **maître seul** | `drawList[100]`/secteur, tri O(n²), 1 cmd/chunk + ombre + `findFloorDistance` + 2 clips `[src WALLS.C:2572-2855]` ; un thing n'est dessiné que dans `sectorSpriteList[o->s]` | `draw` ms + `used[1]/vswaps[1]`, E1M1 zigzag, 25 things | `NOSHADOW` sur tout Doom (−1 cmd, −`findFloorDistance`), pas de FOOTCLIP hors eau (−2 cmds) ; `userClip` élargi du rayon | `draw` > 25 ms ou `vswaps[1]` > 8/frame à 25 things ⇒ 4 rotations, puis 1 vue |
| R-4 | **Midtex / scrollers / fuzz** | cellule = 1 o orientation + 1 o tuile, **aucun UV** `[doc LEV_FORMAT:95-100]` ; special 48 = +1 px/tic `[src p_spec.c:1134]` ; fuzz = colonnes décalées `[src r_things.c:648]` | œil, E1M1 (grilles) + E1M2 (scrollers) | midtex = faces 0x32 indice 0 transparent sur le portail ; scroller = cycle de tuiles pré-décalées **ou statique** ; fuzz = `COMPO_SHADOW` `[src WALLS.C:2762]` | aucun (dégradations acceptées) |
| R-5 | **Cache 28 tuiles 16 bpp + 31 tuiles 8 bpp** partagés murs/flats et things/arme | `used 14-18/28` murs seuls `[doc STEXT]` ; 141 tuiles E1M1 `[mesuré R3]` ; arme et monstres dans les **mêmes 31 slots** `[src PIC.C:436-452]` ; éviction d'une tuile en usage `[src PIC.C:272]` | `used[*]/vswaps[*]` | `PICFLAG_LOCKED` sur l'arme active `[src PIC.C:47,417]` ; dédup ; classe 32×32 pour pickups ; flats en LOD | `used[0]` ≥ 28 ou `used[1]` ≥ 31 en vue standard ⇒ jeu de tuiles réduit (E4.1b, 68) |
| R-6 | **Cadence** : tic 35 Hz sur vblank 60 Hz + **tic Doom sur le maître déjà chargé** | 14,9 ms + 39,2 µs/cellule `[doc STEXT]` ; tic Mimas console 69-83 ms sur MAP15 `[mem]`, E1M1 non consigné ; le « trou logique » `[src SRUINS.C:2137-2191]` recouvre la fenêtre esclave | champ STATUSTEXT `tic` (FRT autour de `TryRunTics`) | `acc+=framesElapsed*35; while(acc>=60){tic;acc-=60}` ; gouverneur Mimas (décimation, parqués `[src p_tick.c:92-174]`) ; plan B = `P_Ticker` sur l'esclave | frame > 66 ms soutenue sur E1M1 avec 10 monstres éveillés ⇒ tic esclave, sinon Doom « lent » assumé |
| R-7 | **Sauvegarde** | BUP 32 Ko `[doc HW_BACKUP_BUP]` ; E1M1 ≈ 27 Ko, E1M6 ≈ 87 Ko `[mesuré R5 §8]` ; Mimas : `fopen` → `I_Error` `[src g_game.c:1774]` | taille du flux imprimée | **différentielle** « recharger la carte + appliquer » : masque THINGS + secteurs + `player_t` ≈ 2-4 Ko `[est]` ; intégrale sur cartouche backup | aucun (repli = checkpoint 400 o) |
| R-8 | **Licences** | fork GPL-3 ; Doom et jfduke3d GPL-2+ ✓ (`game.c:7-10`) ; jfbuild/eduke32 BUILDLIC ✗, NBlood GPL-2-only ✗ `[R4 §6]` ; CON/ART/MAP jamais | en-têtes de chaque fichier importé | 1 872 l. d'`engine.c` **clean-room** depuis `buildinf.txt` + différentiel PC ; structs à nous | non bit-exact sur 6 cartes après 15 j ⇒ **stop Duke** (Doom intact) |

Choix mémoire : **B comme plancher, C comme déverrouillage**. B est le seul plan qui boote sur une Saturn nue et il rend E1M1/E1M8/E1M4-5 `[est]` jouables ; E1M6 heurte de toute façon `MAXNMSECTORS 600` (606 feuilles `[mesuré]`) et exige la fusion de feuilles, cart ou pas. C est l'accélérateur pour E1M6/Doom II, gaté par `validPtr` (1 ligne) et par la règle « jamais de géométrie parcourue par frame sur l'A-bus » `[doc R5 §6]`.

## 1. Architecture

```
 CD ─┬─ +E1M1.LEV (géométrie fermée+course, tuiles, ciel, sprites RLE, séquences, sons dyn.)
     ├─ +E1M1.LNK (feuille→secteur .LEV, ligne→murs/cellules, sprite/frame/rot→séquence, sfx→idx)
     ├─ DOOM1.WAD (lumps de carte + info : lus par le playsim ; jamais par le rendu)
     └─ STATIC.DAT (TITLEPIC, feuille NBG0 = STBAR/HUD, psprites 0x6A, sons statiques)
 ┌────────── MSH2 ──────────┐        ┌──── SSH2 ────┐   ┌─ VDP1 ─┐  ┌─ VDP2 ─┐  ┌ SCSP ┐
 │ playsim Doom (p_*, g_, d_)│ 35 Hz  │ transfo murs │   │ murs   │  │ RBG0   │  │ 30   │
 │  ↓ sat_sync_world()      │──────► │ (inchangé)   │──►│ sprites│  │  ciel  │  │ slots│
 │ drawWalls/drawSprites    │        └──────────────┘   │ arme   │  │ NBG0   │  │ SFX  │
 │ 2D Doom → feuille NBG0   │                           │ automap│  │  2D    │  │ +MUS │
 └──────────────────────────┘                           └────────┘  └────────┘  └──────┘
 zone Doom = 1 bloc mem_malloc(0) LWRAM 512 Ko ; .LEV géométrie HWRAM ; tuiles 0x72 + RLE LWRAM ; BUP = diff 2-4 Ko
```

1. **Le playsim Doom vit tel quel** (18 sous-systèmes intacts `[R1 §1]`) sur ses propres structures de carte, dans un bloc `mem_malloc(0)` de 512 Ko (E1M6 ≈ 440 Ko `[est R5 §6]`) ; `Z_Init` dessus, `_sbrk` Mimas 4 Ko statique remplace le shim du fork qui échoue toujours `[src shim/syscalls.c:1-14]`.
2. **Le rendu est SlaveDriver inchangé** : `drawWalls/drawWallsFinish/drawSprites`, esclave, gouverneur, `PLAX`, `SOUND.C` gardés verbatim `[R2 §1]` ; la seule exigence du moteur est `camera->s` = feuille de l'œil et « la géométrie ne bouge qu'entre `drawWallsFinish` et le `drawWalls` suivant » `[src WALLS.C:2251, SRUINS.C:2197-2201]`.
3. **Le 2D Doom** (`V_DrawPatch` et tout `m_menu/st_/wi_/f_/hu_`) écrit ses indices 8 bpp dans un tampon 320×200 recopié CPU dans la **feuille NBG0 existante** fenêtrée par W0 `[src PIC.C:456-462]` : 0 VRAM, 0 cycle nouveau `[R5 §7]` ; PLAYPAL en banque CRAM 0 (objets) et recopiée en 6 (NBG0) ; flashs palette = `SCL_SetColOffset` `[src SRUINS.C:141]`.
4. **L'arme** = chunks 0x6A des psprites émis en `EZ_normSpr` COLOR_4 à `(sx>>16−160+chunkx, sy>>16−120+chunky)` — le format `wChunk` exact `[src SEQUENCE.C:296-305]`, verrouillés `PICFLAG_LOCKED`.
5. **Le son** = `s_sound.c` intact sur `playSoundE/adjustSounds` `[src SOUND.C:311-438]` ; liste blanche de sons par niveau (535 Ko > 512 Ko `[mesuré]`) ; musique = séquenceur MUS SH-2 de Mimas (slots 8-22, S 0 ms mesuré `[doc RESOURCE_BUDGETS:40]`) ou CDDA `playCDTrack`.
6. **La sauvegarde** = `BUP_Write` d'un enregistrement différentiel (§0 R-7) ; le format `p_saveg` vanilla n'est pas utilisé (trop gros).
7. **Le disque** = 9-36 `.LEV`+`.LNK`, STATIC.DAT Doom, progression par `gamestate`/`G_DoCompleted` court-circuitant `runMap` `[src SRUINS.C:2463,2512]`, MOV/LIP/BONUS retirés.
8. **La boucle hôte** est celle de R2 : `runLevel` conservé, le trou logique reçoit `TryRunTics`, le bloc « monde mobile » reçoit `sat_sync_world()`.
9. **L'adaptateur commun Doom/Duke** existe et il est **étroit** : cinq verbes — `host_puppet(id, pos, secteur, séquence, frame, flags, banque_lumière)` (sur `moveSpriteTo` + 6 champs), `host_heights(jeu_de_sommets, dy)`, `host_cell_tile(mur, cellule, tuile)`, `host_light(secteur_lev, niveau)`, `host_2d(tampon, rect)`. Doom les remplit depuis `mobj_t/sector_t/side_t` ; Duke depuis `sprite[]/sector[]/wall[]`. Les besoins **propres à Duke** (quads muraux/sol, recalcul de normale) sont des ajouts au **moteur**, pas à l'adaptateur.
10. **Ordre** : Doom d'abord, l'adaptateur est *extrait* de Doom au jalon 2 ; la voie Duke démarre en parallèle par ce qui ne dépend pas du moteur (§3bis).

```c
while (1) {                                    /* SRUINS.C:2065-2345, R2 */
  view ← yaw=viewangle, pitch=roll=0, pos=(x, viewz, y); camera->s = link[ssec(player)];
  EZ_openCommand(); drawWalls(view);           /* kick esclave */
  acc += framesElapsed*35; while (acc>=60) { G_BuildTiccmd(inputQ); G_Ticker(); acc-=60; }
  drawWallsFinish();
  sat_sync_world();                            /* la SEULE écriture de géométrie */
  if (automapactive) am_ez_lines(); psprite_chunks(); doom_2d_to_nbg0();
  sound_nextFrame(); pic_nextFrame();          /* horloges LRU : à GARDER */
  EZ_closeCommand(); SPR_WaitDrawEnd(); gouverneur; SCL_DisplayFrame(); movePlax(viewangle,0);
  SCL_SetColOffset(OFFSET_A, tint(damagecount,bonuscount,radsuit));
  if (gamestate!=GS_LEVEL || gameaction) return;
}
```

**Passe de synchro — champs exacts et coût E1M1 `[est]`** (85 secteurs, 648 sidedefs, 115-150 mobjs vivants `[mesuré R5 §3]`, tic = 28,6 ms) :

| source (écrivain) | champs recopiés | cible moteur | coût/tic E1M1 |
|---|---|---|---|
| `sector_t` (thinkers p_doors/plats/floor/ceilng) | `floorheight, ceilingheight` — comparés à l'ombre précédente | `level_vertex[].y` des rangées de la feuille-set (≈200 écritures/secteur mobile) | 85 × 0,3 µs + ≤3 mobiles × 30 µs ≈ **115 µs** |
| `sector_t` (p_lights, EV_LightTurnOn) | `lightlevel` → 6 paliers | `vertexLight`/`firstLight` des feuilles (`[src AICOMMON.C:44-70]`) | ≤5 changements × 20 µs ≈ **100 µs** |
| `sector_t` (EV_DoFloor change, donut) + `flattranslation[]` (P_UpdateSpecials, 1/8 tics) | `floorpic, ceilingpic` | octet tuile des cellules des faces sol/plafond | ≈10 µs moyen |
| `side_t` (**seuls écrivains** : `P_ChangeSwitchTexture` p_switch.c:223-247, boutons, `P_UpdateSpecials` p_spec.c:1121-1136) → **file de sales, pas de scan** | `toptexture/midtexture/bottomtexture` via `texturetranslation[]`, `textureoffset` (scroller) | octet tuile des cellules du mur lié | 0 µs sans événement ; ≈5 µs/événement |
| `mobj_t` (physique, `P_SetMobjState`, spawn/remove) | `x, y, z, angle, sprite, frame&FF_FRAMEMASK, frame&FF_FULLBRIGHT, flags&(MF_SHADOW\|MF_NOSECTOR\|MF_TRANSLATION), subsector` | `moveSpriteTo(spr, link[ssec], pos)`, `angle`, `sequence = base(sprite,frame)+facing`, `flags`, banque lumière (`fullbright`→0, sinon `lightlevel` du secteur) ; `newSprite/freeSprite` aux spawns/morts | 150 × 0,5 µs + 30 mobiles × 3 µs + 5 spawns × 2 µs ≈ **185 µs** |
| `player_t` / globaux | `viewz, mo->angle, fixedcolormap, extralight, psprites[2].{state,sx,sy}, damagecount, bonuscount, powers[]` | caméra, offset couleur, chunks d'arme | ≈5 µs |
| **Total** | | | **≈ 400 µs ≈ 1,4 % du tic** |

## 2. Inventaire des manques

| # | manque | jeu | sévérité | mécanisme (fichier touché) | j | risque | preuve |
|---|---|---|---|---|---|---|---|
| 1 | Table de liaison feuille↔secteur, ligne→murs/cellules, (sprite,frame,rot)→séquence, sfx→idx | commun | BLOQUANT | fichier `.LNK` séparé (le chargeur ne lit que 14 parts) + lecteur C (`LEVEL.C`) | 1,5 | figer avant J2 | `[src LEVEL.C:51-67]`, R3 #12 |
| 2 | Passe `sat_sync_world()` (§1) | commun | BLOQUANT | remplace `SRUINS.C:2197-2201` ; `moveSpriteTo` | 2 | O(n) relink | `[src SPRITE.C:916]` |
| 3 | Hôte : `TryRunTics` dans le trou logique, entrée 35 Hz depuis `inputQ` par port, `I_GetTime` = vblanks×35/60, `DG_*` de Mimas (timer FRT, keyq, `DG_Fatal`) | commun | BLOQUANT | `SRUINS.C:2137-2191`, `V_BLANK.C:45-91` (ouvrir la boucle `break` par port) | 2 | `processInput` ne lit que le 1er pad | `[src V_BLANK.C:58-78]`, R6 §2-3 |
| 4 | Zone Doom sur `mem_malloc(0)` 512 Ko + `_sbrk` 4 Ko statique + pile ≥ 24 Ko | Doom | BLOQUANT | `main` du fork ; `shim/syscalls.c` remplacé | 1 | LIFO 8 niveaux | `[src UTIL.C:340]`, R6 §10 |
| 5 | Registres de noms (`R_TextureNumForName/R_FlatNumForName/Check*` 25 sites) → id tuile ; `textureheight[]` (p_floor.c:377) ; `texturetranslation/flattranslation` ; `R_PointInSubsector/R_PointToAngle2` déplacés dans core ; 11 fonctions viewport en no-op | Doom | BLOQUANT | nouveau `r_bridge.c` (~1 200 l. de r_main/r_data/r_sky reprises) | 1,5 | — | R1 §3-4, R6 (a) |
| 6 | Sprites par niveau : S_\* → 0x6A RLE + frames/chunks/séquences, miroirs A2A8, sous-ensemble par things+info.c | Doom | BLOQUANT | `wad2sprites.py` → `.LEV` (LWRAM) | 4 | E1M8 429 Ko RLE ; MAP29 2 Mo ; `MAXNMPICS` 800 après mipmaps | `[mesuré R3 §2a]` |
| 7 | Portes/lifts : défaire `open_doors()`, émission fermée + course **par rangée**, bornes `P_FindLowestCeilingSurrounding−4` | Doom | BLOQUANT | `doom3d.py` (`mobile_bounds`, `emit_edge_mobile`) | 5 | budget esclave (cellules totales) | `[src doom3d.py:479-512, WALLS.C:1374]` |
| 8 | Interrupteurs/boutons : 2 tuiles + réécriture de l'octet tuile au `P_ChangeSwitchTexture` | Doom | MAJEUR | `doomtiles.py` + hook p_switch → file de sales | 1 | — | `[src p_switch.c:223-247]` |
| 9 | Flats/textures animés 8 tics (23 familles) | Doom | MAJEUR | toutes les tuiles de la famille + écriture d'octet ; **ne pas** utiliser `PICFLAG_ANIM` (cadence 2 frames, 15 sets) | 2 | Doom II > 15 familles/carte | `[src p_spec.c:96-124, PIC.C:110]` |
| 10 | Lumière par secteur (strobe/glow/flicker à 35 Hz) | commun | MAJEUR | export secteur→plages `firstLight` + sommets ; `host_light` | 1,5 | quantification 6 paliers | `[src AICOMMON.C:44-70]` |
| 11 | Midtex 2 côtés (grilles) | Doom | MAJEUR | mur à faces 0x32 indice 0 transparent sur le portail, BLOCKED si ML_BLOCKING | 2 | ordre peintre | R3 #2 |
| 12 | Scrollers (special 48) + `xoffset` ignoré | Doom | MINEUR | `voff/uoff` dans la tuile ; scroller = cycle de tuiles pré-décalées **ou statique** | 1,5 | cache 28 | `[src doom3d.py:262-268]` |
| 13 | Spectre/invisibilité (fuzz) | Doom | COSMÉTIQUE | frame en `COMPO_SHADOW` ou `DRAW_MESH` | 0,5 | — | `[src WALLS.C:2762]` |
| 14 | Fullbright + éclairage sprite par distance | Doom | MAJEUR | banque CRAM = f(`lightlevel`, dist) dans `getLight`/`drawSprites` (`light=0` forcé aujourd'hui) | 1 | 6 banques (5 objets + 7 libérée) | `[src WALLS.C:2705-2712]` |
| 15 | Ciel par épisode (SKY1-3 → PLAX, K-table recopiée, PLAYPAL) | Doom | MAJEUR | `wad2sky.py` ; `F_SKY1` = `WALLFLAG_PARALLAX` | 1 | angle `[est]` | `[src PLAX.C:83-118]` |
| 16 | Arme (psprites) : chunks 0x6A + `EZ_normSpr` + bob `sx/sy` + flash fullbright | Doom | BLOQUANT | remplace `runWeapon` ; STATIC.DAT Doom (`wad2static.py`) | 3 | 31 slots partagés → `PICFLAG_LOCKED` | `[src SEQUENCE.C:296-305, PIC.C:47]` |
| 17 | HUD/ST + messages HU + automap tracé | Doom | BLOQUANT | 2D → feuille NBG0 fenêtrée W0 (0 VRAM) ; `HU_Erase/R_VideoErase/BRDR*/screenblocks` supprimés (~20 sites) ; automap = `EZ_line` COLOR_5 | 3 | copie CPU 64 Ko/frame si tout change → `sat_hud_dirty` | `[src PIC.C:456-462, MAP.C:190-208]`, R6 §5 |
| 18 | Menus, intermission, finale (cast → table sprite), titre, HELP | Doom | MAJEUR | même surface ; WI\*/M_\*/STCFN → PCS ou lumps WAD lus au besoin (630 Ko de 2D) | 3 | INTERPIC/VICTORY2 absents du shareware | `[mesuré R1 §5]` |
| 19 | Wipe « melt » | commun | COSMÉTIQUE | fondu color-offset vblank du fork | 0,5 | — | `[src V_BLANK.C:97-118]` |
| 20 | Flash palette PLAYPAL 1-13 ; inversion invulnérabilité | Doom | MAJEUR / COSMÉTIQUE | `SCL_SetColOffset` stateless par frame ; inversion **irréproductible** sur murs 16 bpp → offset blanc + banque inversée sprites | 0,5 | — | `[src SRUINS.C:141-171]` |
| 21 | Sons SFX : DS\* → format `SOUND.C` (XOR 0x80, rate 0x7000), liste blanche/niveau, backend `I_StartSound`→`playSoundE`, priorités côté `s_sound.c` | Doom | BLOQUANT | `wad2snd.py` + `i_sound_fork.c` | 3 | 535 Ko > 512 Ko ; 80 sons max ; dédup 1 son/frame | `[src SOUND.C:40,218,326]` |
| 22 | Musique : séquenceur MUS SH-2 (slots 8-22) ou CDDA | commun | MAJEUR | copier `mus_step` de Mimas ; réconcilier avec le bump `soundTop` remis à 0 par niveau | 2 | banque PCM en SCSP concurrence les SFX | `[src i_sound_saturn.cxx:135-456, SOUND.C:164]` |
| 23 | Sauvegarde différentielle BUP (6 slots) | commun | MAJEUR | `bup_diff.c` : masque THINGS + secteurs + `player_t` ; relinker les thinkers parqués avant | 3 | `BUP_Write` bloquant | `[src BUP.C:139, p_tick.c:167]` |
| 24 | Fusion de feuilles sœurs convexes (>600) ou `MAXNMSECTORS` 1 000 | commun | MAJEUR | `merge_leaves()` dans `doom3d.py` | 3 | E1M6 606, MAP15 875, E4M9 956 | `[mesuré R3 #15]` |
| 25 | Régime mémoire B : segs supprimés, sommets partagés, tuiles 0x72 LWRAM, tables `.rodata` en LWRAM, `objects[]` retiré | commun | MAJEUR | `doom3d.py` (partage), `assemble.py` (0x72), `saturn.ld` | 2 | — | R5 §2, §4 |
| 26 | Cartouche (plan C) : `validPtr`, copie A-bus→VDP1 par DMA, RLE décodé depuis tampon HWRAM | commun | MINEUR | `UTIL.H:74`, `PIC.C:map()` | 2 | A-bus ~4× lent | `[doc HW_MEMORY_AND_BUS §6]` |
| 27 | Multi-`.LEV`, progression `G_DoCompleted`→`+E1Mx.LEV`, court-circuit `runMap`, `make cue`, MOV/LIP retirés | commun | MAJEUR | `wad2disc.py`, `SRUINS.C:2463` | 1,5 | — | R3 §3 |
| 28 | Accès WAD : `fs_read` séquentiel sans seek | Doom | MAJEUR | `fs_seek` (GFS_Seek) **ou** lumps de carte linéarisés dans le `.LNK` | 1 | poignée persistante a régressé sur ODE (Mimas) | `[src FILE.C:146-262]`, `[mem r2-persistent-cd-handle]` |
| 29 | Écran de chargement / STATIC.DAT Doom (TITLEPIC, feuille NBG0, sons statiques, psprites) | Doom | BLOQUANT | `wad2static.py` (ordre `SRUINS.C:1925-1931`) | 2 | index tuile u8 + `tileBase` | R3 §2g |
| 30 | PWAD : `merge_wad` (padding 4 absent ; `wad.py:51` premier-gagne) | Doom | MINEUR | inverser `setdefault` ; padder | 0,5 | silencieux | `[src wad.py:51]` |
| 31 | DEHACKED | Doom | MINEUR | non supporté (aucun `deh_*.c` dans core) | 0 | — | `[src deh_str.h:38]` |
| 32 | Split-screen 2-4p | Doom | MINEUR | **hors plan** (1 caméra, W0 unique) | — | — | R2 §5c |
| 33 | Physique/collision Build (clipmove, pushmove, getzrange, hitscan, cansee, neartag, updatesector, inside, listes de sprites) | Duke | BLOQUANT | 1 872 l. clean-room depuis `buildinf.txt` + harnais différentiel PC | 10-15 | fidélité | `[mesuré R4 §2]` |
| 34 | CON : compilateur hors ligne (`parsecommand` 1 080 l.) + interprète embarqué (`parse` 1 065 + `execute` + `move`) | Duke | BLOQUANT | outil PC + `con_vm.c` ; bytecode 70-80 Ko en LWRAM | 5-8 | **CPU 5-25 ms/tic** `[est]` | `[mesuré R4 §3]` |
| 35 | `rotatesprite` → primitif 2D unique (≈700 sites via 9 wrappers) + 3 fontes | Duke | BLOQUANT | `EZ_scaleSpr/EZ_distSpr/EZ_line`, `COMPO_TRANS`, gouraud shade | 10-15 | `pal≠0` = banque CRAM | `[mesuré R4 §4]` |
| 36 | Sprites muraux (cstat 16, 22 % E1L1) / au sol (cstat 32) | Duke | MAJEUR | quad `EZ_distSpr` collé au mur/sol, 100-200 l. dans `WALLS.C` | 2-3 | budget esclave | R4 §5 |
| 37 | Sprites ART → 0x6A (300-444 Ko RLE/carte), `pal>0` recolorés en tuiles distinctes | Duke | BLOQUANT | `duke_sprites.py` (plages de frames lues du CON, jamais au dépôt) | 4 | LWRAM | `[mesuré R3 #19]` |
| 38 | Secteurs qui translatent (SE6/14/30) ; rotations (SE0/1/11) | Duke | MAJEUR | translation **parquée** ; rotation = recalcul de normale (~50 l.) | 2 | plans de coupe | R4 §5 |
| 39 | Panning/repeat de textures ; miroirs ; caméras ; ROR | Duke | MINEUR / COSMÉTIQUE | dégradé (statique) ; **impossibles** → mur opaque / tuile fixe | 0 | — | R4 §5 |
| 40 | Sons VOC (181, 2,9 Mo) → partition ~40-60/carte ; MID → CDDA | Duke | MAJEUR | `voc2snd.py` ; `playCDTrack` | 1-2 | voix longues coupées | `[mesuré R4 §7]` |
| 41 | RAM playsim Duke ≈ 505 Ko après régime (3,4 Mo naïf) | Duke | BLOQUANT | limites 640/4 096/1 536, `hittype` réduit, `wall[]` en LWRAM | 5-10 | + `.LEV` 589 Ko | `[mesuré R4 §3]` |
| 42 | Sauvegarde Duke (`saveplayer` ≥ 300 Ko) | Duke | MAJEUR | checkpoint niveau + inventaire | 2 | — | `[src menues.c:484-644]` |
| 43 | Robustesse `convex.py` (161/194 cartes échouent) | Duke | MAJEUR | trous/éclats/superpositions | 6 | goulot | `[doc NOTES_QUANT:150-157]` |

Total Doom (1-32) ≈ **52 j** ; Duke (33-43) ≈ **50-70 j** au-dessus de Doom.

## 3. Plan Doom en jalons

| J | livrable | j | dépend | kill criterion |
|---|---|---|---|---|
| **J0** sonde de densité + deux mondes | E1M1 `.LEV` du jour + 25 sprites **donneur** (PowerSlave, déjà dans le .LEV) posés aux positions THINGS ; playsim Doom lié **headless** (P_SetupLevel + P_Ticker, aucun rendu Doom) ; champs STATUSTEXT `tic`, `mem:`, `used/vswaps` | 4 | — | R-1, R-3, R-6 (§0) |
| **J1** marionnettes Doom | #1, 2, 3, 4, 5, 6, 25 : monstres Doom vrais (sprites RLE E1M1) bougent, attaquent, meurent via le playsim ; joueur = Doom (`P_PlayerThink`), caméra = `viewz` | 12 | J0 | R-1 (`mem:`), R-5 (`used[1]`) |
| **J2** monde mobile | #7, 8, 9, 10, 11, 12 : portes, lifts, interrupteurs, anims, lumières, grilles ; adaptateur commun **extrait** (5 verbes) | 12 | J1 | R-2 (murs sautés) |
| **J3** jeu 1p complet | #14, 15, 16, 17, 20, 21, 22, 29 : arme, HUD, ciel, sons, musique, flashs | 15 | J2 | R-5 (`used[1]` avec arme), son `assert soundTop` |
| **J4** épisode | #18, 19, 23, 27, 28 : menus, intermission, finale, sauvegarde, 9 `.LEV`, cue | 9 | J3 | aucun |
| **J5** grandes cartes | #24, 26, 30 : fusion de feuilles (E1M6), cart plan C, Doom II | 8 | J4 | `size<900000`, perf hors sujet |

**J0 (4 j) — prouve l'hypothèse la plus risquée pour le moins cher.** Livrable : un disque E1M1 où 25 sprites du donneur TOMB (séquences déjà chargées, 144 tuiles 0x6A dans le `.LEV` `[mesuré R3 §1]`) sont créés par `newSprite(link[ssec], …, NOSHADOW, NULL)` aux positions des THINGS exportées par `doom3d.py` dans un fichier annexe ; en parallèle le playsim Doom est lié et `P_Ticker` tourne dans le trou logique sans qu'aucun mobj ne soit dessiné. Vu → sens : `mem:` après chargement (< 64 Ko ⇒ R-1 tue B) ; `draw` face aux 25 sprites (> 25 ms ⇒ R-3) ; `vswaps[1]` (> 8/frame ⇒ R-5) ; `tic` (> 15 ms sur E1M1 ⇒ R-6 : esclave ou décimation) ; sprites coupés net aux frontières de pièces convexes (⇒ élargir `userClip`). Décision owner à la sortie : B seul, ou C obligatoire.

**J1 (12 j).** Livrable : E1M1 jouable au pad avec les vrais monstres ; le joueur est piloté par `G_BuildTiccmd` depuis `inputQ` ; pas d'arme visible, pas de HUD, pas de portes (encore ouvertes). Vu → sens : un zombie qui tire, meurt et laisse un cadavre à la bonne rotation (8 facings = `getFacingAngle` `[src AICOMMON.C:72]`) ; une boule de feu **claire dans le noir** (fullbright → banque 0) ; `mem:` avec sprites+zone. Kill : `mem:` < 64 Ko ⇒ passer en C avant J2. Décision : rotations 8 ou 4 (÷1,6 de RLE).

**J2 (12 j).** Livrable : géométrie mobile et adaptateur à 5 verbes. Vu → sens : la porte d'entrée d'E1M1 s'ouvre en 35 tics, la contremarche monte par rangées, aucun mur ne disparaît (compteur de murs sautés = 0) ; l'ascenseur de la salle secrète monte ; un interrupteur change de tuile ; la salle qui strobe strobe. Kill : murs sautés > 0 avec la porte fermée ⇒ réduire les cellules des rangées ; reliquat étiré visible ⇒ rangées de 32 u. Décision : scrollers statiques ou cyclés.

**J3 (15 j).** Livrable : Doom 1p sans menu — arme avec bob et flash, STBAR vivant, ciel SKY1 défilant, sons positionnés, musique. Vu → sens : `used[1]` reste < 31 avec le fusil + 10 monstres ; le flash rouge teinte **murs et sprites** (offset VDP2) ; `assert(soundTop+size<512K)` ne tire pas au chargement d'E1M8 (liste blanche). Kill : arme évincée du cache (texture fausse) malgré `PICFLAG_LOCKED` ⇒ chunks 32×32.

**J4 (9 j).** Livrable : épisode 1 de bout en bout avec sauvegarde. Vu → sens : sauver en E1M3, éteindre, recharger : mêmes portes ouvertes, mêmes morts ; `BUP` affiche ≤ 60 blocs pour 6 slots. Kill : aucun (repli checkpoint).

**J5 (8 j).** Livrable : E1M6 (fusion de feuilles) puis Doom II en plan C. Kill : `size≥900000` ou `MAXNMSECTORS` non fusionnable ⇒ carte déclarée non portable.

## 3bis. La voie Duke

**Ce qui se porte tel quel (GPL-2+, ≈30 000 l. `[mesuré R4 §7]`)** : `actors.c` 7 033 (42 SE), `player.c` ≈4 190 (`processinput` 1 776, `shoot` 863), `sector.c` 3 263 (17 ST), `gamedef.c` interprète ≈1 500, `premap.c` ≈1 370, `game.c` ≈7 900 utiles (`spawn` 1 855 ; `animatesprites` 772 devient le hook objets), `menues.c` logique ≈2 500, `sounds.c` ≈500. Retirés : réseau, démo, OSD, RTS, bot, `SE40_Draw`. Les 421 macros `pragmas.h` (BUILDLIC) sont redéfinies dans un en-tête à nous ; les structs `sectortype/walltype/spritetype` (format MAP v7) aussi — **question juridique à poser avant de committer** (R4 §7).

**Clean-room (BUILDLIC, 1 872 l.)** — spécification **par comportement**, jamais par lecture d'`engine.c` : `clipmove` (318 l.) = « déplace (x,y,z) de (vx,vy) dans `sectnum`, glisse le long des murs et sprites bloquants dans le rayon `walldist`, renvoie 0 / 32768+mur / 49152+sprite » `[doc buildinf.txt:530]` ; idem `pushmove` 553, `getzrange` 560, `hitscan` 579, `neartag` 622, `cansee` 635, `updatesector` 645, `inside` 652, listes 734/747 ; plus `krand` (LCG **identique**, sinon les tirages divergent), `getangle`, `ksqrt`, `nextsectorneighborz`, `dragpoint`. Méthode : (1) un auteur écrit la spec depuis `buildinf.txt` ; (2) un autre code sans ouvrir `engine.c` ; (3) **harnais différentiel PC hors dépôt** : jfbuild cloné à côté, un pilote GPL-3 rejoue 10 000 pas enregistrés par carte E1L1-E1L6 sur les deux implémentations et compare bit à bit ; seules les traces `.bin` et notre code entrent au dépôt.

**CON** : 16 888 tokens shareware ⇒ 17-20 k mots = 70-80 Ko de bytecode `[mesuré R4 §3]`, compilé **hors ligne** chez l'utilisateur (port GPL-3 de `parsecommand`), embarqué en LWRAM ; `MAXSCRIPTSIZE` 32 768→20 k, labels (272 Ko) supprimés. Coût : `execute()` par acteur ACTOR/STANDABLE/PROJECTILE/EFFECTOR à 30 Hz `[src actors.c:2989-4673]`, 30-80 mots/acteur + `move()`→`clipmove` 100-300 µs ⇒ **5-25 ms/tic pour 30-60 acteurs `[est]`** — c'est le risque n° 1 de Duke, avant la RAM.

**`rotatesprite` → 2D moteur** : 156 sites directs + ≈550 via 9 wrappers, 27 avec angle ≠ 0, 32 avec zoom libre, 20 `pal≠0` `[mesuré R4 §4]` ⇒ **un** primitif `tile2d(x,y,zoom,angle,shade,pal,flags,clip)` sur `EZ_scaleSpr` (zoom), `EZ_distSpr` (rotation), `COMPO_TRANS` (orientation 2), gouraud (shade), 3 fontes en tuiles, `EZ_line` pour la carte. Le VDP1 fait tout cela nativement `[doc HW_VDP1]`.

**Impossibles** : miroirs (E1L1 4 murs, E1L2 3 → murs opaques), caméras `setviewtotile` (→ tuile fixe), ROR/TROR (1.5 seulement), translation SE6/14/30 (le wagon traverse plusieurs cellules convexes figées → `nextSector` faux), SE2 pentes animées ; **dégradés** : rotation SE0/1/11 (normales recalculées, plans de coupe à tester), panning (statique), `visibility` (approximé), `pal>0` (tuiles recolorées, pas de banque CRAM libre).

**Ordre : Doom d'abord, Duke en parallèle par le PC.** Arguments : (1) Doom touche son renderer par 20 fonctions / 75 sites `[mesuré R1 §4]`, Duke par 1 302 appels + 3 950 accès directs `[mesuré R4 §1]` — l'adaptateur extrait de Doom est petit et prouvable complet ; conçu depuis Duke il serait sur-dimensionné (quads muraux, rotation) pour rien ; (2) tout ce qui est propre à Duke est un **ajout au moteur** (quads dans `WALLS.C`, normales) ou un **outil PC** (clean-room, CON), pas une refonte de l'adaptateur ; (3) les deux kill criteria de Duke (fidélité du clean-room, coût CPU du CON) se mesurent **sans** le moteur — D0 sur PC, D1 en headless — donc ils n'attendent pas J2.

| D | livrable | j | dépend | kill criterion |
|---|---|---|---|---|
| **D0** clean-room + harnais (PC, hors dépôt) | 1 872 l. + traces 6 cartes × 10 000 pas ; `krand` identique | 10-15 | — | < 100 % bit-exact après 15 j ⇒ **stop Duke** |
| **D1** CON hors ligne + VM headless SH-2 | GAME.CON compilé, E1L1 acteurs tournent sans rendu ; STATUSTEXT `tic` | 5-8 | D0 | `tic` médian > 20 ms avec les acteurs actifs d'E1L1 ⇒ VM sur esclave ; si l'esclave n'a pas la fenêtre ⇒ Duke « au ralenti » ou stop |
| **D2** monde | adaptateur J2 + quads muraux/sol (`WALLS.C`) + `duke_sprites.py` + régime RAM 505 Ko + `.map` | 12-18 | J2, D1 | murs sautés > 0 ou `used[0]` ≥ 28 sur E1L1 avec les 138 sprites muraux ⇒ dégrader (muraux en billboards) ; `mem:` < 64 Ko ⇒ cart obligatoire |
| **D3** 2D + arme + HUD + menus | primitif `tile2d`, 3 fontes, `displayweapon` (64 blits) | 10-15 | D2 | — |
| **D4** son + SE/ST carte par carte | 40-60 sons/carte, CDDA, E1L1-E1L6 vérifiées | 15-25 | D3 | E1L1 injouable (SE7 eau, ST portes) ⇒ carte déclarée non portable |
| **D5** disque | 6 cartes shareware, checkpoint, cue | 2-3 | D4 | — |

Total Duke **55-85 j**, dont D0-D1 (15-23 j) exécutables **maintenant**, en parallèle de J0-J2.

## 4. Décisions owner

1. **Mémoire : B plancher + C déverrouillage** (recommandé) — la Saturn nue boote E1M1-E1M5/E1M8 ; la cart ouvre E1M6/Doom II. Alternative « C seul » = public réduit aux cartouches.
2. **Rotations : 8 vues** (recommandé, `getFacingAngle` les donne gratis) ; 4 vues seulement si J1 montre `mem:` ou `vswaps[1]` au rouge (÷1,6 RLE).
3. **Portes : fermé + course par rangées de 64 u** (recommandé) ; rangées de 32 u si le reliquat étiré gêne (×2 murs).
4. **2D : feuille NBG0 fenêtrée** (recommandé, 0 VRAM/0 cycle) plutôt qu'un NBG1 en B1 (test neige console, sheet réduit) — au prix de l'unicité de W0 (pas d'arme VDP2, pas de split).
5. **Musique : MUS SH-2 de Mimas** (recommandé, S 0 ms mesuré, CD libre) ; CDDA si la qualité prime et si le CD n'est pas sollicité en jeu.
6. **Sauvegarde : différentielle 2-4 Ko** (recommandé) ; intégrale seulement sur cartouche backup.
7. **Duke : lancer D0 maintenant** (PC, aucune dépendance) et **poser la question juridique** des structs redéfinies avant D2 ; ou reporter Duke après J3 pour concentrer l'effort.
8. **Split-screen : hors plan** (1 caméra, W0 unique) — à dire franchement au public Mimas.

## 5. Inconnues à mesurer d'abord

| inconnue | instrument | tranche |
|---|---|---|
| LWRAM restante avec E1M1 + sprites + sons + zone | `mem:` `[src SRUINS.C:2240]` (J0) | B vs C |
| Coût du tic Doom E1M1/E1M6 sur SH-2 dans le fork | champ `tic` FRT autour de `TryRunTics` (J0) ; `Z_FreeMemory` imprimé dans Mimas au chargement | R-6, taille du bloc zone |
| `drawSprites` à 25 things visibles + clip aux frontières convexes | `draw`, `vswaps[1]`, œil (J0) | R-3, R-5 |
| Murs sautés par le budget esclave avec portes fermées + rangées | compteur au `return` de `WALLS.C:1374` (J2) | R-2 |
| Facteur de partage des sommets `.LEV` | script sur `doom3d.py` : sommets (x,y,z) distincts d'E1M1 | −70..−90 Ko `[est]` |
| Coût A-bus d'un `map()` de tuile depuis la cart | `htimer` autour de `dmaMemCpy` | sprites RLE sur cart ou non (C) |
| Coût `parse()`+`clipmove` par acteur Duke | D1 headless, `tic` STATUSTEXT | R-8/D1 |
| Neige NBG0 + RBG0 K-par-dot + CRAM réécrite au vblank | console seulement (Ymir ne modélise pas) | voie 2D |

## 6. Ce que ce plan sacrifie

- **Éclairage** : 32 colormaps → **6 paliers** de banques CRAM, sans `scalelight` par pixel ; l'inversion d'invulnérabilité est **irréproductible** sur les murs 16 bpp.
- **Scrollers** statiques (ou cyclés au prix du cache), **fuzz** remplacé par une ombre, **melt** remplacé par un fondu.
- **Sprites coupés** aux frontières de pièces convexes tant que `userClip` n'est pas élargi ; reliquat étiré sur la rangée haute des portes.
- **Cartes** : sans cartouche, E1M6 et tout Doom II sont hors de portée ; au-delà de ~900 feuilles (`size<900000`), rien, même en C. Duke : 161/194 cartes échouent aujourd'hui au convertisseur ; miroirs, caméras, métro, ROR **impossibles**.
- **Split-screen 2-4p** abandonné ; **DEHACKED** non supporté ; **sauvegarde** différentielle (pas le format vanilla) ; **sons** sous-ensemblés par niveau (535 Ko > 512 Ko) et voix Duke coupées.
- **Cadence** : le tic Doom vit sur le maître avec le rendu ; si R-6 tombe, Doom tournera « lent » (décimation Mimas) plutôt qu'à 35 Hz stricts.
- **Duke** est deux fois le coût de Doom et repose sur 1 872 lignes clean-room dont la fidélité n'est garantie que par le harnais : sans lui, ce n'est pas Duke.

# D1 — « Fidélité Doom d'abord » : le playsim de Doom intact sur le moteur SlaveDriver (2026-09-13)

Synthèse des rapports R1-R6 + vérifications du jour. Étiquettes : `[src] FICHIER:LIGNE` lu dans le code ; `[mesuré]` calculé ce jour (Python sur `Mimas/cd/data/DOOM1.WAD`, greps) ; `[R#]` fait sourcé dans un rapport lecteur ; `[doc]` ; `[est]` estimation. Corrections aux rapports établies ce jour : **NBG0 est au-dessus des sprites** (`SCL_SetPriority(SCL_NBG0,6)`, `SCL_SP0,4` `[src] SRUINS.C:1071-1073` — R5 §7 disait le contraire) ; le fork affiche en **320×240** (`SCL_240LINE` `[src] SRUINS.C:2399`, `EZ_localCoord(160,120)` `:2127`) alors que le core Mimas dessine déjà **320×224 = vue 192 + STBAR 32** (`[src] core/i_video.h:27-29`) ; `setSectorBrightness(s,level)` existe déjà (`[src] AICOMMON.C:50-70`) ; PLAYPAL[0] a **deux noirs, 0 et 247**, et STBAR n'a **aucun pixel d'index 0** `[mesuré]`.

## 1. Architecture

```
CD ──┬─ E1Mx.LEV   ciel PLAX + géométrie convexe + tuiles murs/sols + sprites RLE 0x6A + séquences   → mem_malloc aire 1 (HWRAM) / aire 0 (LWRAM)
     ├─ E1Mx.DAT   mini-WAD : les 10 lumps de carte + table de liaison (feuille→pièce, ligne→cellules)  → LWRAM, mode « mapped » de w_wad.c
     ├─ UI.DAT     mini-WAD résident : PLAYPAL, STCFN*, ST*, AMMNUM*, D_* (≈ 90 Ko)                      → LWRAM
     ├─ MENU.DAT / WI.DAT / TITLE.DAT  (M_* 103 Ko, WI* 185 Ko, TITLEPIC/HELP/CREDIT 68 Ko chacun)         → LWRAM, à la demande
     ├─ STATIC.DAT Doom : psprites 0x6A (95 chunks, 130 Ko) + sons statiques + écran de chargement
     └─ pistes CDDA (13 MUS rendus hors ligne)
Maître SH-2 ─ playsim Doom INTACT (core/ moins r_*.c, +r_shim.c ≈ 1 200 l.) ; zone Z_ = UN bloc mem_malloc(0) en LWRAM
            ─ sync_world() : écrit level_vertex[].y, octets tuile des cellules, level_vertexLight, Sprite[] (marionnettes)
            ─ drawWalls / drawWallsFinish / drawSprites du moteur, inchangés (murs RGB16 VDP1, sprites 8 bpp banques CRAM 0-5)
            ─ 2D Doom : I_VideoBuffer 320×224 en HWRAM (71 680 o) → copie longwords des lignes sales vers la feuille NBG0
Esclave SH-2 ─ transformation des murs (inchangé)
VDP2 ─ RBG0 ciel (A1, PLAYPAL en banque 7) · NBG0 feuille B0/B1 = écran 8 bpp de Doom (prio 6 > SP0) · colour offset = 13 flashs PLAYPAL
SCSP ─ slots 0-7 SFX Doom (backend Mimas en C) · 16/17 réservés CDDA · 8-15 + 18-22 MUS de secours
BUP  ─ 6 checkpoints de début de niveau (≈ 100 o chacun) ; partie complète LZSS seulement sur cartouche backup
```

1. **Le playsim vit tel quel** : `p_*.c`, `g_game`, `d_loop`, `d_net`, `m_*`, `st_*`, `hu_*`, `wi_stuff`, `f_finale`, `am_map`, `s_sound`, `p_saveg`, `info.c`, `w_wad`, `z_zone` — 341 269 o d'image `[R5 §2]` — compilés en gnu11 dans le MAIN du fork ; seuls `r_*.c` tombent (225 967 o) et un `r_shim.c` fournit les 20 `R_*` encore appelées hors rendu `[R1 §4]` : 2 pures (`R_PointInSubsector`, `R_PointToAngle2`), 4 registres nom→id de tuile, 11 no-op de viewport, 3 de rendu (`R_RenderPlayerView` → pont peintre).
2. **Le rendu est SlaveDriver verbatim** : `drawWalls` `[src] WALLS.C:2237-2445`, `drawWallsFinish` `:2448-2483`, `drawSprites` `:2570-2857`, gouverneur 60/30 `[src] SRUINS.C:2277-2300`. Trois patchs de 2-3 lignes : `light` par sprite (`:2708` où `light=0`), mode `COMPO_SHADOW` sur un flag (`:2838`), et la boucle hôte de `runLevel`.
3. **Le monde .LEV est un modèle de rendu dérivé hors ligne** (doom2ps, jalons 1-2 faits `[doc] DOOM_ON_SLAVEDRIVER`), 1 unité Doom = 1 unité Saturn (`[src] params/doom.cfg` « converts 1:1 ») — donc `pos = (x>>16, viewz>>16, y>>16)` sans échelle.
4. **La 2D est l'écran 8 bpp de Doom, non réécrite** : `v_video.c` continue d'écrire `I_VideoBuffer` ; `DG_DrawFrame` copie les lignes sales dans la feuille NBG0 512×512 8 bpp existante (`[src] SRUINS.C:1085-1093`), palette banque 6 = PLAYPAL (`[src] SEQUENCE.C:269-286`), index 0 transparent → le blit substitue 0→247 (noir aussi `[mesuré]`) là où Doom veut du noir opaque (automap, menus plein écran).
5. **Le son est le backend Mimas** (`i_sound_saturn.cxx` porté en C : DMX→SCSP, 8 voix slots 0-7, key-off/KYONEX, MUS slots 8-22 `[R6 §4]`) — le fork poke déjà la SCSP depuis le SH-2 sans 68K (`[src] SOUND.C:151-153`), même adresse, même geste.
6. **La sauvegarde garde `p_saveg.c`** derrière un fichier RAM (`_open/_write/_read` de syscalls.c redirigés vers un tampon LWRAM transitoire) puis LZSS → `BUP_Write` ; en pratique 6 checkpoints de début de niveau (`[src] BUP.C:23-42` : 100 o/slot).
7. **Le disque** : un `.LEV` + un `.DAT` sidecar par carte, lus séquentiellement par `fs_read` (pas de seek à écrire `[R2 §9]`), `runMap`/MAP.DAT court-circuités (`[src] SRUINS.C:2463,2512`), STATIC.DAT Doom, CDDA.
8. **La mémoire** : HWRAM libre après échange de code ≈ 345 Ko (tables `.rodata` 70 Ko reloguées en LWRAM) `[R5 §2 est]` ; LWRAM 1 Mo = tuiles 0x72 + sprites RLE + zone Doom + mini-WADs. E1M1 tient sans cartouche à ~50 Ko près `[est]` (§6) ; **E1M6 exige la cartouche 4 Mo** `[R5 §6]`.
9. **Cadence** : tics à 35 Hz par `I_GetTime = vtimer_total*35/60` (`vtimer++` à `UsrVblankEnd` `[src] V_BLANK.C:151-153`), rendu à 30 ou 60 fps par le gouverneur, 1-2 tics par frame ; le rendu lit l'état **après** les tics, jamais interpolé `[R1 §6 (11)]`.
10. **L'adaptateur commun** : une vtable `game_iface {load_level, run_tics(now), sync_world, draw_2d, camera, palette}` — Doom la remplit avec `P_SetupLevel/TryRunTics/…` ; Duke (R4) la remplirait avec `domovethings` à 30 Hz, ses `sector[]/wall[]/sprite[]` propres et `rotatesprite` réémis dans le même tampon 8 bpp. La boucle hôte, la marionnette `Sprite`, la feuille NBG0 et le son ne changent pas.

### Boucle hôte (`runLevel`, remplace `[src] SRUINS.C:2065-2345`)

```c
camera = newSprite(link[ssec(player->mo)], 16, 0, 0, /*sequence*/-1, SPRITEFLAG_NOSHADOW, NULL);   /* SPRITE.C:68 ; WALLS.C:2591 exige sequence==-1 */
for (;;) {
  view.yaw = (viewangle >> 16) * 360; view.pitch = view.roll = 0;             /* angle_t → F(deg) */
  view.pos = { mo->x>>16, viewz>>16, mo->y>>16 }; camera->s = link[ssec(mo)];  /* 1:1, ciel : movePlax(yaw,0) */
  EZ_openCommand(); sysClip(320,224); userClip; EZ_localCoord(160, 96);       /* vue 192 lignes, STBAR en dessous */
  if (!automapactive) drawWalls(&view);                                       /* kick esclave — géométrie GELÉE */
  /* ---- trou logique (ex-2137-2191) : rien n'écrit level_* ici ---- */
  TryRunTics();  S_UpdateSounds(players[displayplayer].mo);  /* M_Ticker est déjà dans loop_interface ; 0-2 tics à 35 Hz */
  drawWallsFinish();                                                          /* join FRT — la géométrie redevient écrivable */
  sync_world();                                                               /* §1.3 : hauteurs, tuiles, lumière, marionnettes */
  draw_psprites(&players[displayplayer]);                                     /* EZ_normSpr par chunk, patron SEQUENCE.C:296-305 */
  D_Display_2D();                                                             /* ST/HU/AM/M/WI/F → I_VideoBuffer ; sat_hud_dirty */
  sound_nextFrame(); pic_nextFrame(NULL,NULL);                                /* GARDÉS : horloge LRU du cache (SRUINS.C:2250-2253) */
  EZ_closeCommand(); SPR_WaitDrawEnd();
  governor(); SCL_DisplayFrame(); framesElapsed = vtimer; vtimer_total += vtimer; vtimer = 0;
  blit_2d_dirty_rows();  SCL_SetColOffset(OFFSET_A, SP0|NBG0|RBG0, tint[st_palette]);   /* §1.3 palette */
  if (gamestate != GS_LEVEL || gameaction) return;                            /* WI/finale/menu tournent dans la même boucle, 3D coupée */
}
```

Ordre des écritures : le playsim ne touche que ses structures pendant la fenêtre esclave ; `sync_world` n'écrit `level_vertex`/`level_texture`/`level_vertexLight`/`Sprite` qu'après `drawWallsFinish` — c'est la seule contrainte du moteur `[R2 §1]`. Latence : l'image montre l'état des tics de la frame précédente (+16 ms vs Doom PC), acceptable.

### La passe de synchro (par frame, après les tics) — E1M1 `[mesuré]` : 85 secteurs, 648 sides, 138 things, 4 secteurs à lumière animée, 4 secteurs de flats animés, 8 scrollers, 1 interrupteur

| champ Doom lu | producteur | cible moteur | déclencheur | coût E1M1 `[est]` |
|---|---|---|---|---|
| `sector.floorheight / ceilingheight` | T_VerticalDoor, T_PlatRaise, T_MoveFloor/Ceiling `[R1 §1b]` | `level_vertex[].y` des faces sol/plafond de ses pièces convexes + contremarches/linteaux des portails voisins (liste par secteur dans `.DAT`) — même geste que `updatePushBlockPositions` `[src] SPRITE.C:852-890` | compare à une ombre 2 shorts × 85 | scan 10 µs ; 0-3 secteurs mobiles × 60-200 sommets = 10-30 µs chacun |
| `sector.lightlevel` (+`extralight`, `fixedcolormap` globaux) | p_lights, `A_Light0/1/2` | `setSectorBrightness(pièce, LUT32[lightlevel>>3])` `[src] AICOMMON.C:50-70` sur les ~2,8 pièces .LEV du secteur | ombre 1 short × 85 | 4 secteurs clignotants, ≈ 160 écritures/changement ≈ 15 µs ; moyen 5-20 µs |
| `flattranslation[floorpic/ceilingpic]` | `P_UpdateSpecials` toutes 8 tics `[src] p_spec.c:1112-1123` | octet tuile des cellules des faces (2 o/cellule `[R3 §1]`) via table flat→tuile | `leveltime % 8 == 0` | 4 secteurs nukage ≈ 150 cellules ≈ 10 µs / 8 tics |
| `sides[].toptexture/midtexture/bottomtexture` (+`texturetranslation`) | `P_ChangeSwitchTexture` `[src] p_switch.c:223-247`, `buttonlist` `[src] p_spec.c:1143-1167` | octets tuile des cellules du mur lié | liste des sides « interrupteur » (1 en E1M1, ≤ 13 Doom II `[R3]`) + `buttonlist[16]` + anims si `leveltime%8==0` | ≤ 2 µs |
| `sides[].textureoffset` (special 48, +1 px/tic) | `P_UpdateSpecials` via `linespeciallist[64]` `[src] p_spec.c:1129-1136` | **pas d'UV par cellule** dans le .LEV `[R4 §5]` → cyclage de 8 tuiles pré-décalées (phase `((offset>>16)&63)>>3`) ou statique | 8 lignes E1M1 | 2 µs — **dégradé** (§6) |
| par `mobj` : `x,y,z,angle,sprite,frame,flags,subsector` (~130 vivants) | p_mobj physique, `P_SetMobjState` | `Sprite.pos` ; `s` via `link[subsector]` → `moveSpriteTo` `[src] SPRITE.C:916-938` si changé ; `sequence = seqtab[sprite][frame&FF_FRAMEMASK][rot]` avec **`rot = (R_PointToAngle2(viewx,viewy,x,y) − angle + ANG45/2·9) >> 29`** `[src] r_things.c:867-870` calculé côté hôte ; `flags` : `MF_NOSECTOR`/joueur-caméra → `INVISIBLE`, `MF_SHADOW` → nouveau bit → `COMPO_SHADOW`, bit 15 du frame → banque 0 ; `color = banque(lightlevel>>LIGHTSEGSHIFT + extralight, fixedcolormap)` | tous ; `rot` seulement si la pièce est `processed` dans `sectorDraw` de la frame précédente | 130 × 3-4 µs = 0,45 ms sans cull, ≈ 0,15 ms avec |
| spawn / `P_RemoveMobj` | p_mobj | `newSprite/freeSprite` (radius = `mobjinfo.radius`, owner NULL) | hook 1 ligne `// SATURN:` dans `P_SpawnMobj`/`P_RemoveMobj` | O(1) / O(n) sur la liste de la pièce |
| `viewz`, `mo->angle`, `mo->x/y` | `P_CalcHeight` (bob inclus) | `viewTransform`, `camera->s` | chaque frame | 5 µs |
| `psprites[2].{state→sprite/frame, sx, sy}` | p_pspr | `EZ_normSpr(COLOR_4, banque 0 si FULLBRIGHT sinon banque secteur)` par chunk à `((sx>>16) − leftoffset + chunkx − 160, (sy>>16) − topoffset + chunky − 96)` — offsets cuits dans les chunks par le convertisseur | chaque frame | 2 calques × ≤ 4 chunks = ≤ 8 cmds ≈ 10 µs |
| indice PLAYPAL 0-13 (`ST_doPaletteStuff`) | st_stuff `[src] :955-1033` | `I_SetPalette(i)` → `SCL_SetColOffset` table de 14 triplets = moyenne PLAYPAL[i]−PLAYPAL[0] `[mesuré]` : rouge 8 = (+101,−88,−77), or 12 = (+37,+43,−9), vert 13 = (−17,+19,−10) | au changement | 1 µs |
| `I_VideoBuffer` (ST/HU/AM/M/WI/F) | UI 2D | lignes sales → feuille NBG0, LUT 0→247 | `sat_hud_dirty` `[src] st_stuff.c:280` ; plein écran si `menuactive/automapactive/gamestate≠GS_LEVEL` | HUD 10 240 o ≈ 0,2-0,3 ms quand sale ; plein 71 680 o ≈ 1,5-2 ms |

**Total sync ≈ 0,3-0,8 ms/frame hors 2D** `[est]`, contre une frame moteur E1M1 ≈ 14,9 ms + 39,2 µs × ~140 cellules ≈ 20 ms `[doc] STEXT_BASELINE`. Le poste qui compte est **le tic lui-même** (`TryRunTics`, 1-2 par frame), non mesuré sur E1M1 : Mimas n'a consigné T que sur les grosses cartes (69-83 ms `[mem]`). C'est l'inconnue n° 1 (§5).

## 2. Inventaire des manques

| # | manque | jeu | sévérité | mécanisme (fichier moteur touché) | j | risque | preuve |
|---|---|---|---|---|---|---|---|
| 1 | Boucle hôte : `TryRunTics` dans le trou logique, sorties par `gamestate` | commun | BLOQUANT | réécrire `runLevel` `[src] SRUINS.C:2065-2345` selon §1 ; retirer movePlayer/runObjects/runWeapon/drawStatBar | 2 | un tic > fenêtre esclave allonge la frame | R2 §1 |
| 2 | Horloge 35 Hz | commun | BLOQUANT | `I_GetTime = vtimer_total*35/60` ; `vtimer_total` cumulé dans `UsrVblankEnd` `[src] V_BLANK.C:151-153` | 0,2 | 7 tics/12 vblanks irréguliers (comme PC 60 Hz) | R6 §3 |
| 3 | Zone Doom + tas libc | Doom | BLOQUANT | `Z_Init` sur un bloc `mem_malloc(0, 512 Ko)` `[src] UTIL.C:365` ; `_sbrk` Mimas 4 Ko statiques (le shim du fork échoue toujours) | 0,5 | LIFO 8 niveaux/aire : le bloc zone alloué en premier | R6 §10 |
| 4 | Accès WAD : mini-WADs mappés | Doom | BLOQUANT | `.DAT` sidecar + `UI.DAT` lus par `fs_read` séquentiel `[src] FILE.C:194-262` ; `w_wadfile->mapped = base` `[src] w_wad.c:436-441` (zéro copie, chargement in-place `[src] p_setup.c:165-178`) | 2 | lumps alignés 4 `[mem]` | R6 §1 |
| 5 | `r_shim.c` : `R_PointInSubsector`, `R_PointToAngle2`, 4 registres nom→tuile, `textureheight[]`, `texturetranslation/flattranslation`, `sprites[]` (cast), `skyflatnum`, 11 no-op viewport | Doom | BLOQUANT | nouveau fichier ; tables émises par doom2ps dans `.DAT` | 2 | `textureheight` lu par `EV_DoFloor raiseToTexture` `[src] p_floor.c:377-386` | R1 §3-4 |
| 6 | Table de liaison feuille→pièce, ligne→cellules, secteur→sommets mobiles, secteur→pièces | Doom | BLOQUANT | `link_write.py` (doom2ps) + lecteur C ; `conv.remap` existe pour le départ `[src] doom3d.py:178` | 1,5 | figer le format avant J2 | R3 #12 |
| 7 | Portes/ascenseurs : `.LEV` fermé + course, sync des hauteurs | Doom | BLOQUANT (J3) | défaire `open_doors()` `[src] doom3d.py:479-512` ; contremarche `[fh,maxFloor]`, linteau `[minCeil,ch]`, `WALLFLAG_DOORWALL` `[src] AI.C:4300-4307` ; sync par translation de sommets | 5+1 | budget esclave compte les cellules **totales** `[src] WALLS.C:1374` → murs sautés en silence | R3 #5 |
| 8 | Lumière par secteur | Doom | MAJEUR | `setSectorBrightness` `[src] AICOMMON.C:50` sur les pièces du secteur, LUT 256→32 identique à `light_of()` `[src] doom3d.py:137-149` | 0,5 | 5 paliers CRAM côté sprites (§6) | R3 #6 |
| 9 | Flats/textures animés + interrupteurs → octets tuile | Doom | MAJEUR | `doomtiles --anim` (toutes les frames des familles), table `texnum→tuile` ; **ne pas** utiliser `PICFLAG_ANIM` (cadence 2 frames ≠ 8 tics, 15 sets `[src] PIC.C:110`) | 3 | E1M6 : 30 secteurs de flats animés `[mesuré]` = +9 tuiles 0x72 | R3 #3-4 |
| 10 | Scrollers special 48 | Doom | MINEUR | 8 tuiles pré-décalées cyclées par `(offset>>16)>>3` (+32 Ko LWRAM par texture scrollée) ou statique | 1 | dégradé | R4 §5 |
| 11 | Marionnettes mobj→Sprite | Doom | BLOQUANT | `newSprite/moveSpriteTo/freeSprite` `[src] SPRITE.C:68-117,916-938` ; hooks 1 ligne dans `P_SpawnMobj/P_RemoveMobj` ; `MAXNMSPRITES` 450→650 (+14 Ko) pour E1M6 (463 things `[mesuré]`) | 2 | thing chevauchant deux pièces dessiné dans **une** (`sectorSpriteList[o->s]` `[src] WALLS.C:2424`) → clip visible ? | R2 §2-3 |
| 12 | Sprites Doom par niveau → 0x6A + `sFrameType/sChunkType` + table (sprite,frame,rot)→séquence, miroirs A2A8 | Doom | BLOQUANT | `wad2sprites.py` ; E1M1 ≈ 270 Ko RLE, E1M8 ≈ 460 Ko `[R5 §5]` ; ÷1,6 par miroirs | 4 | LWRAM ; `MAXNMPICS` 800 effectifs `[src] PIC.C:30, SRUINS.C:2063` | R3 #7 |
| 13 | Lumière des sprites | Doom | MAJEUR | `light = o->color & 7` si nouveau flag, à `[src] WALLS.C:2708` ; banques 1-5 = COLORMAP Doom aux niveaux 6/12/18/24/30 au lieu de −1..−4/canal (`[src] PIC.C:640-656`) ; fullbright = banque 0 | 0,5 | pas de terme distance (1 ligne commentée `:2705`) | R2 §2 |
| 14 | Spectre / Blur Sphere (`MF_SHADOW`) | Doom | MINEUR | flag → `COMPO_SHADOW` au lieu de `COLOR_4` à `[src] WALLS.C:2838` | 0,3 | silhouette assombrie ≠ fuzz | R1 §6 (1) |
| 15 | Couleurs joueurs `MF_TRANSLATION` (MP) | Doom | COSMÉTIQUE | 3 jeux de tuiles PLAY recolorées (aucune banque CRAM libre) | 1 | +3×74 Ko | R5 §5 |
| 16 | Psprites (arme + flash) | Doom | BLOQUANT (J4) | chunks 0x6A dans STATIC.DAT Doom (`wad2static.py`), émission patron `[src] SEQUENCE.C:296-305` sans `TILEVDP` ; bob/abaissement = valeurs `sx/sy` de p_pspr | 3 | partage des **31 slots TILE8BPP** avec les monstres (`assert` `[src] SEQUENCE.C:303`) | R2 §4, R3 §2(g) |
| 17 | Caméra + cadre 224 lignes | commun | BLOQUANT | `SCL_224LINE` à `[src] SRUINS.C:2399`, `EZ_localCoord(160,96)`, sysClip 0..191 ; yaw = `(viewangle>>16)*360` ; FOV du moteur à confronter aux 90° Doom | 0,5 | FOV inconnu | vérifié ce jour |
| 18 | Calque 2D = feuille NBG0 | Doom | BLOQUANT (J4) | `DG_DrawFrame` : copie longwords lignes sales, stride 512, LUT 0→247 ; palette banque 6 = PLAYPAL ; W0 désactivée ; `updateVDP2Pic` retiré | 2 | neige console si cycles B mal posés `[doc] HW_VDP2 :604` ; écriture pendant l'affichage | R5 §7, R2 §5 |
| 19 | 13 flashs PLAYPAL | Doom | MAJEUR | `I_SetPalette(i)` → `SCL_SetColOffset(OFFSET_A, SP0\|NBG0\|RBG0, tint[i])` `[src] SRUINS.C:141-142` ; table `[mesuré]` | 0,3 | teinte uniforme ≠ lerp PLAYPAL ; invulnérabilité irréproductible sur murs RGB16 | R2 §8 |
| 20 | Wipe « melt » | Doom | COSMÉTIQUE | remplacé par le fondu colour offset `[src] V_BLANK.C:97-118` (`DG_FadeOut/In` comme Mimas streaming) | 0,2 | sacrifice | R1 §1a f_wipe |
| 21 | Automap | Doom | MINEUR | `AM_Drawer` intact dans le tampon 8 bpp, `drawWalls` sauté quand `automapactive` (vanilla ne rend pas la vue sous la carte) | 0,3 | copie pleine 1,5-2 ms | R1 am_map |
| 22 | Ciel SKY1-3 par épisode | Doom | MAJEUR | `wad2sky.py` : SKY1 256×128 → 512×256 (×2), palette PLAYPAL en banque 7, table K recopiée du retail ; `movePlax(yaw,0)` = 256 px/90° = Doom `[src] r_sky.h:29` | 1 | angle `[est]` | R2 §8, R3 §2(c) |
| 23 | SFX | Doom | MAJEUR | backend Mimas en C sur slots 0-7 (`SOUND.C` ne garde que le mix CDDA 16/17) ; `soundTop` remis à 0 par niveau + précache `[src] p_setup.c:1060-1117` = liste blanche par niveau (E1M8 526 Ko > 512 → retirer DSITMBK 22 kHz) | 2 | 535 127 o > 524 288 `[mesuré]` | R5 §5, R6 §4 |
| 24 | Musique | Doom | MAJEUR | CDDA : 13 MUS → WAV hors ligne → pistes (`playCDTrack` `[src] FILE.C:275`) ; le CD est libre en jeu (tout est chargé) ; secours = séquenceur MUS Mimas (S 0 ms mesuré) | 1,5 | 260-390 Mo de pistes ; `idmus` = changement de piste | R3 §3 |
| 25 | Entrée + cheats | Doom | MAJEUR | table pad→touches Mimas `[src] dg_saturn.cxx:17063-17078` dans `processInput` `[src] V_BLANK.C:45-91` ; cheats = page de menu `// SATURN:` qui poste les lettres (`cht_CheckCheat` intact, message « Degreelessness mode on ») | 0,7 | `accum &=` du fork lit le 1er pad seulement | R6 §2 |
| 26 | Sauvegarde | Doom | MAJEUR | fichier RAM dans `syscalls.c` (`fopen("doomsav%d")` de g_game intact `[src] g_game.c:1704,1774`) → LZSS → `BUP_Write` ; défaut = checkpoint 100 o × 6 ; complète si cartouche backup ; relinker les parqués avant `P_ArchiveThinkers` `[src] p_tick.c:162-174` | 3 | E1M1 27 Ko, E1M6 87 Ko vs 32 Ko internes | R5 §8 |
| 27 | Progression E1M1→E1M9 | Doom | BLOQUANT (J6) | hook après `P_SetupLevel` (comme `sat_drp_select_map` `[src] p_setup.c:1212`) : `mem_free` LIFO du niveau précédent, `fs_read` `E1M%d.LEV` + `.DAT`, spawn des Sprites par la liste des thinkers ; `runMap` court-circuité | 1,5 | ordre LIFO strict | R3 §3 |
| 28 | Intermission / finale / titre / cast | Doom | MINEUR | patches intacts ; `WI.DAT`/`TITLE.DAT` chargés à la demande (niveau libéré) ; cast → `sprites[]` du shim (V_DrawPatch depuis le lump S_ du `.DAT` ou sprite VDP1) | 1 | WI* 185 Ko transitoires | R1 §1a |
| 29 | Démos attract | Doom | COSMÉTIQUE | ré-activer `demosequence` `[src] d_main.c:895-897` ; gouverneurs Mimas OFF (décimation/parqués désynchronisent) | 0,5 | — | R6 §8 |
| 30 | DEHACKED | Doom | MINEUR | absent du core (`deh_*.h` seuls `[src] deh_str.h:38`) ; ajouter `deh_*.c` de Chocolate **et** faire lire le DEH par wad2sprites (tables états→sprites) | 3 | plus tard | R1 §0 |
| 31 | Midtextures grillagées, `xoffset` | Doom | MAJEUR | `emit_midtex` sur le portail (indice 0 transparent), `voff/uoff` dans la tuile | 3 | ordre peintre | R3 #1-2 |
| 32 | E1M6 (606 feuilles) et mémoire | Doom | MAJEUR | `merge_leaves()` convexes ou `MAXNMSECTORS` 600→700 (`[src] UTIL.H:21`, +7,6 Ko) ; cartouche 4 Mo (`validPtr` 1 ligne `[src] UTIL.H:74`) pour tuiles + sprites | 3+ | non vérifié au chargement → corruption silencieuse | R3 §5, R5 §6 |
| 33 | STATIC.DAT Doom, disque multi-.LEV, cue | Doom | BLOQUANT (J6) | `wad2static.py`, `wad2disc.py`, `make cue` (recette Mimas `build.ps1:251-258`) | 3 | — | R3 #10,#16 |
| 34 | Pile Doom | commun | MAJEUR | récursion BSP/blockmap ≥ 24 Ko (Mimas `[src] main.cxx:46-70`) vs `mystack` 20 384 `[src] UTIL.C:29` → 32 Ko | 0,1 | — | R6 §10 |
| 35 | Split-screen 2p | Doom | MINEUR (hors J1-J6) | 2 passes `drawWalls/Finish` par frame avec sysClip moitiés + 2 caméras ; HUD compact Mimas intact (`ST_DrawCompactWidgets`) ; ticcmds `mp_input` | 4 | 2× le coût rendu → 20-30 fps | R6 §7 |
| 36 | Adaptateur Duke | Duke | BLOQUANT (Duke) | même vtable ; physique clean-room 1 900 l., CON hors ligne, `rotatesprite` → tampon 8 bpp | 55-90 | voir R4 | R4 §7 |
| 37 | Sprites muraux/sol Duke | Duke | MAJEUR | quads `EZ_distSpr` collés au mur (`WALLS.C` 100-200 l.) | 2 | — | R4 §5 |
| 38 | Cache TILE8BPP 31 slots sous densité Doom | commun | BLOQUANT potentiel | mesure J1 ; leviers : 32×32 pour petits things, `SRUINS.C:1903` 31→40 (−36 Ko VRAM sur les 7 424 libres → prendre sur les 1 448 cmds) | 0-2 | texture fausse, pas seulement lente | R2 Q1 |
| 39 | Coût `drawSprites` maître-seul (tri O(n²)/pièce, ombre, clips) | commun | MAJEUR | mesure J1 ; `NOSHADOW` sur tout (Doom n'a pas d'ombres) supprime 1 cmd + `findFloorDistance` par sprite | 0 | — | R2 Q2 |
| 40 | Multi-écouteur son, drop-in, deathmatch local | Doom | MINEUR | viennent avec core (s_sound.c:404-430, g_game.c:1321-1440) ; actifs seulement avec #35 | 0 | — | R6 §7 |

## 3. Plan en jalons

| J | livrable | coût `[est]` | dépend | kill criterion |
|---|---|---|---|---|
| J0 | 4 chiffres à coût nul | 0,5 j | — | aucun : ce sont les seuils des J suivants |
| J1 | **30 zombies dans TOMB_e1m1** : marionnettes POSS statiques, 8 rotations, banque lumière | 3 j | J0 | `vswaps[1]` > 4/frame à 20 visibles, ou draw > 14 ms, ou clip de frontière jugé inacceptable |
| J2 | **E1M1 se joue** : playsim intact, marionnettes complètes, caméra, pad ; pas de 2D ni son | 15 j | J1, #1-6, #11-12, #17 | tic + sync > 12 ms/frame moyen sur console, ou `mem:` négatif sans cartouche |
| J3 | **le monde bouge** : portes/ascenseurs, lumières, flats animés, interrupteurs, ciel, midtex | 10 j | J2, #7-10, #22, #31 | murs sautés par le budget esclave sur E1M1 avec portes fermées |
| J4 | **ça ressemble à Doom** : STBAR, messages, menus, automap, intermission, titre, flashs, fondu, arme | 6 j | J2, #16, #18-21, #28 | neige NBG0 sur console, ou copie 2D > 3 ms/frame en jeu |
| J5 | **ça sonne Doom** : SFX + CDDA (MUS de secours) | 4 j | J2, #23-24 | key-off/latence audible > 1 frame, ou E1M8 hors 512 Ko après liste blanche |
| J6 | **le shareware entier** : 9 `.LEV` + `.DAT`, STATIC Doom, cue, progression, checkpoints, démos | 6 j | J3-J5, #26-27, #29, #33 | E1M6 ne charge pas sans cartouche (attendu : voir #32) |
| J7 | options : cartouche + E1M6 + Doom II (6 j) ; split 2p (4 j) ; Duke (55-90 j) | — | J6 | — |

**Total Doom J0-J6 ≈ 44,5 j `[est]`** (dont ≈ 22 j de convertisseur déjà chiffrés par R3 et ≈ 22 j côté moteur/plateforme). DOOM_ON_SLAVEDRIVER §7 donnait 25-40 j pour ses jalons 3-4 seuls ; l'écart est le « TOUT » (2D, son, disque, sauvegarde, progression).

**J0 — mesures gratuites (0,5 j).** (1) Lire **T** (tic) dans l'overlay Mimas ligne 2 sur E1M1 et E1M6, console, 1p : c'est le même playsim en LWRAM que le fork exécutera — seuil J2 = 10 ms/tic. (2) Lire `mem:` (`[src] SRUINS.C:2240`) sur le disque doom2ps courant : valide les ≈ 430 Ko de LWRAM restants du R5 §4. (3) Un `printf(Z_FreeMemory())` après `P_SetupLevel` dans Mimas (1 ligne, build propriétaire) : fixe la taille du bloc zone (512 Ko ?). (4) Relire `used[1]/vswaps[1]` à vide sur TOMB_e1m1 (référence pour J1). Vu → sens : T > 12 ms sur E1M1 ⇒ 30 fps impossible sans gouverneurs ⇒ décision owner n° 9 avant J2.

**J1 — 30 zombies dans TOMB (3 j) : l'hypothèse la plus risquée pour le moins cher.** Ce que D1 parie et que rien n'a encore prouvé : *les 31 slots TILE8BPP et `drawSprites` maître-seul supportent la densité de things de Doom*, et *un thing ne disparaît pas aux frontières des pièces convexes*. Livrable : `wad2sprites.py` réduit à la famille POSS (49 lumps `[R5]`, 8 rotations, séquence par (frame,rot)), 30 `newSprite` placés aux positions des THINGS de type 3004 d'E1M1 (statiques, `sequence` = marche A-D à 8 rotations, angle des THINGS), patch `light` (#13), banques CRAM 1-5 = COLORMAP. Aucune ligne de playsim. Critère console (vu → sens) : `used[1]` ≤ 28 et `vswaps[1]` ≤ 2/frame en visant le couloir d'entrée (≈ 10 visibles) ⇒ le cache tient ; texture fausse sur un zombie ⇒ éviction en frame (`[src] PIC.C:264-272`) ⇒ #38 ; un zombie coupé net au bord d'une dalle de sol ⇒ #11 clip de frontière ⇒ décider (dessiner dans toutes les pièces touchées = 2× le coût, ou tolérer) ; `draw` STATUSTEXT − référence J0 = coût par sprite visible (attendu ≤ 0,3 ms `[est]`). Décision owner : 8 vs 4 rotations si `vswaps` déborde.

**J2 — E1M1 se joue (15 j).** Livrable : MAIN du fork = moteur + core (moins r_*) + `r_shim.c` + `dg_lev.c` (boucle §1, sync §1.3, pad, horloge, zone, mini-WAD `.DAT`) ; sprites E1M1 complets (POSS/SPOS/TROO + pickups + projectiles, ≈ 270 Ko) ; portes **statiquement ouvertes** dans le `.LEV` (état actuel) mais **fermées dans le playsim** ; STATUSTEXT affiche santé/munitions/`T`/`sync`. Vu → sens : un zombie tire et la santé baisse ⇒ playsim + son propre `P_LineAttack` tournent ; « mur invisible » devant une porte fermée = **attendu** (géométrie J3) ; monstres qui glissent sans marcher ⇒ table (sprite,frame)→séquence fausse ; monstre qui « saute » d'une dalle ⇒ `link[]` faux ; STATUSTEXT `T` > 12 ms ⇒ kill (gouverneurs, ou playsim sur l'esclave — étude, pas de promesse). Dépend : #1-6, #11-12, #17, #34. Owner : cadre 224 lignes (décision 1).

**J3 — le monde bouge (10 j).** Livrable : doom2ps émet portes/ascenseurs fermés avec course, tuiles d'animation et d'interrupteurs, table lumière, ciel SKY1, midtex grillagées, xoffset ; `sync_world` complet. Vu → sens : la première porte d'E1M1 monte à la vitesse Doom (2 u/tic, 4 pour les blazing) et le linteau suit ⇒ #7 ; la mare de nukage clignote à 8 tics ⇒ #9 ; la lumière du couloir secret stroboscope ⇒ #8 ; un mur qui « manque » quand une porte est fermée ⇒ budget esclave (kill : ≥ 1 mur sauté sur E1M1) ⇒ découpe ou ciel plus bas. Owner : scrollers dégradés (décision implicite, §6).

**J4 — ça ressemble à Doom (6 j).** Livrable : `SCL_224LINE`, feuille NBG0 = écran Doom, palette banque 6, STBAR/visage/messages/menus/automap/intermission/titre/HELP/finale intacts, 13 teintes, fondu, arme en chunks. Vu → sens : visage qui grimace + flash rouge à un coup ⇒ #19 ; « neige » ou lignes baveuses sur le HUD ⇒ cycles VRAM B (kill si irréductible ⇒ voie NBG1/B1 de R2 (a), +2 j) ; arme qui « manque » quand 6+ monstres visibles ⇒ 31 slots (#38) ; menu Options → Detail change rien = normal (no-op). Owner : décisions 2 et 3.

**J5 — ça sonne Doom (4 j).** Vu → sens : porte + interrupteur + tir simultanés sans coupure ⇒ 8 voix OK ; « sound RAM full » à E1M8 ⇒ liste blanche ; musique D_E1M1 en CDDA ⇒ piste 2 ; kill : latence > 1 frame ou clics de key-off (le fork n'a jamais fait de key-on à 10-30/s).

**J6 — le shareware entier (6 j).** Vu → sens : E1M1→E1M2 via l'intermission avec temps/par ⇒ progression ; sortie secrète E1M3→E1M9 ; « Save » écrit un checkpoint (message « game saved. ») ; reboot → « Load » reprend au début du niveau ; E1M6 refuse de charger sans cartouche = attendu (#32).

## 4. Décisions owner

1. **Cadre 224 lignes** (`SCL_224LINE`, vue 192 + STBAR 32) plutôt que 240 : le core est déjà câblé ainsi (`[src] i_video.h:28`), aucun offset HUD à toucher, pas de bande noire ; coût : localCoord/sysClip, 0,5 j.
2. **2D = feuille NBG0 existante** (0 o de VRAM, prio 6 déjà au-dessus des sprites) plutôt que NBG1 en B1 (exige de rétrécir la feuille) ou char VDP1 (7 424 o libres) ; repli NBG1 si neige irréductible en J4.
3. **Flashs = colour offset** (couvre les murs RGB16, 0 CPU) plutôt que réécriture des 7 banques CRAM (exacte sur sprites/2D, nulle sur les murs) : la cohérence vaut plus que l'exactitude ; invulnérabilité approximée.
4. **Musique = CDDA** (fidélité, CD libre puisque tout est chargé) avec le séquenceur MUS Mimas en secours ; coût : 260-390 Mo de pistes, rendu hors ligne.
5. **Cartouche 4 Mo = accélérateur, pas prérequis** : développer J1-J6 sans, valider E1M1/E1M8 ; E1M6 et Doom II conditionnés à la cartouche (ou à `merge_leaves`) — dire aux testeurs que E1M6 est « cart only ».
6. **Sauvegarde = checkpoint de début de niveau** (6 slots, 100 o) ; la partie complète (`p_saveg` intact, LZSS) seulement sur cartouche backup — le format vanilla ne tient pas en 32 Ko (E1M6 87 Ko `[R5 §8]`).
7. **Sons = liste blanche par niveau** (précache `p_setup.c:1060-1117` existant) plutôt que tronquer : 535 Ko > 512 Ko de 11 Ko seulement.
8. **8 rotations + miroirs A2A8** (fidélité) — repli 4 rotations si J1 montre `vswaps` > 2/frame (÷2 sur LWRAM et sur le cache).
9. **Gouverneurs Mimas OFF par défaut** (décimation, thinkers parqués) : ils désynchronisent démos et sauvegardes ; ON seulement si T mesuré en J0 dépasse 10 ms sur E1M1.
10. **Duke** : reporter après J6 ; la boucle hôte l'accueille (vtable §1.10), mais le coût est ×2 et la fidélité ≪ Doom (R4).

## 5. Inconnues à mesurer d'abord

| inconnue | instrument | seuil / décision |
|---|---|---|
| Coût du tic Doom E1M1/E1M6 sur console | overlay Mimas ligne 2, champ T (`[mem] row2-phase-terms-truth`), 1p | > 10 ms ⇒ gouverneurs ON ou 30 fps garanti impossible |
| `used[1]/vswaps[1]` sous 10-30 things visibles | STATUSTEXT `[src] SRUINS.C:2253-2262`, J1 | `vswaps` > 2/frame ⇒ 4 rotations / 32×32 / +slots |
| Coût `drawSprites` maître par sprite visible | STATUSTEXT `draw` − référence J0 | > 0,5 ms/sprite ⇒ `NOSHADOW` + cull distance |
| LWRAM restante avec TOMB_e1m1 chargé | `mem:` `[src] SRUINS.C:2240` | < 450 Ko ⇒ tuiles 0x72 + miroirs obligatoires dès J2 |
| `Z_FreeMemory` après `P_SetupLevel` E1M1/E1M6 | 1 `printf` dans Mimas (compteurs `zf` existants `[src] dg_saturn.cxx:209`) | dimensionne le bloc zone (512 Ko ?) |
| Copie CPU 71 680 o vers la feuille NBG0 | sonde `htimer` autour de la copie (V_BLANK.C:41) | > 3 ms ⇒ dirty-rows seulement, jamais plein écran en jeu |
| FOV du moteur vs 90° Doom | largeur apparente d'une porte de 128 u à 256 u : Doom = 160 px | écart > 10 % ⇒ constante de projection |
| Neige NBG0 sous RBG0 K-par-dot + écritures CPU pendant l'affichage | console uniquement (loi Mimas : famine de cycles = console-only) | neige ⇒ décision 2 repli |
| Partage des sommets .LEV | script sur `doom3d.py` : (x,y,z) distincts / 13 171 sur E1M1 | facteur < 0,6 ⇒ E1M6 en HWRAM possible avec cartouche |
| Coût A-bus d'un `map()` de tuile/chunk depuis la cartouche | `htimer` autour de `dmaMemCpy` | > 2× LWRAM ⇒ sprites RLE restent en LWRAM, seules les tuiles vont en cartouche |

## 6. Ce que ce plan sacrifie

- **Éclairage** : 32 colormaps + atténuation à la distance → 5 banques CRAM pour les sprites, gouraud par sommet sur les murs ; pas de « light diminishing » exact ; invulnérabilité = blanc + banque inversée, jamais l'inverse des murs.
- **Palette** : les 13 flashs deviennent des teintes uniformes (colour offset), pas le lerp PLAYPAL ; la Blur Sphere et le Spectre sont une silhouette sombre, pas le fuzz.
- **Wipe melt** remplacé par un fondu ; **scrollers** (special 48) statiques ou à 8 pas ; **midtextures** dépendent de l'ordre peintre du moteur.
- **Résolution des textures** : tuiles 64×64 (E4.1c), cache de 28 tuiles de murs et 31 de sprites — une scène très riche montre une tuile fausse plutôt qu'un ralentissement.
- **Cadence** : 30 fps verrouillés (60 sur scènes vides) avec 1-2 tics par frame — micro-saccade de 35/30 que Doom PC à 60 Hz a aussi, mais plus visible à 30.
- **Cartes** : E1M6 (606 feuilles, 1,35 Mo de LWRAM demandés) et tout Doom II sont **hors de portée sans cartouche** et sans `merge_leaves` ; au-delà de ~900 feuilles (MAP15 875, E4M9 956) rien ne passe (`size<900000` `[src] LEVEL.C:41`).
- **Sprites** clippés à la pièce convexe qui les contient (si J1 le confirme) ; pas de couleurs de joueurs avant #15.
- **Sauvegarde** au début de niveau seulement (mi-niveau = cartouche backup) ; **cheats** par menu et non au clavier ; **DEHACKED** absent ; **démos** seulement gouverneurs OFF.
- **Split 2/3/4 joueurs** après J6 (2× le rendu, 20-30 fps) ; **Duke** : miroirs, caméras, secteurs qui translatent, panning impossibles, physique clean-room 1 900 lignes — « l'impression de jouer à Duke » restera nettement en dessous de celle de Doom.
- **Disque** : 9 `.LEV` de 1,4-2 Mo + STATIC + pistes CDDA ; le WAD n'est plus lu à l'exécution — tout PWAD passe par doom2ps hors ligne (pas de `-file`).

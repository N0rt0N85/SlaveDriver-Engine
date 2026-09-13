# D2 — Pipeline et disque d'abord : « un IWAD (+PWAD+DEH) ou un GRP entre, un .cue jouable sort »

Angle : la chaîne de conversion complète et chaque produit du disque, pour le fork SlaveDriver en architecture C (playsim Doom sur ses structures, `.LEV` dérivé, passe de synchro par tic — `[doc] DOOM_ON_SLAVEDRIVER.md`). Étiquettes : `[src FICHIER:LIGNE]` lu ce jour ; `[src Rn …]` fait sourcé par le lecteur n ; `[mesuré]` calculé ce jour (`scratchpad/reports/d2_playsim_wad.py` sur `wads_temoins/Doom1.WAD` et `Doom2.wad`) ; `[doc]` ; `[est]`. Corrections apportées aux rapports : `fs_open` est **mono-handle** (`assert(!openCDFile)`) et il n'existe **aucun `fs_seek`** (seuls des `CDC_CdSeek` internes) `[src FILE.C:146-166, 75, 311]` ; `MAXNMSECTORS/MAXNMWALLS` **sont** assertés, mais après `initWallRenderer/initMap` `[src SRUINS.C:1988-1989]` ; `tileBase` s'ajoute à tout indice u8 de géométrie et de face `[src LEVEL.C:69-72]`.

## 1. Architecture

```
IWAD (+PWAD +DEH) --> tools/doom2ps (frontal Doom)        GRP Duke (MAP/ART/CON/VOC/MID) --> tools/duke2ps (frontal Build)
   wad.py . deh.py . doom3d.py . doomtiles.py                build_import -> quantize/snapmap -> convex -> geom3d -> duketiles
   wad2sprites . wad2snd . wad2sky . wad_psw                  duke_sprites . voc2snd . con2bc . map2bin
                        \__________________ tools/pslib (COMMUN, extrait de duke2ps) __________________/
        geom3d.Emitter . lev_write.py (sha1-exact) . rle8.py . snd_scsp.py . plax.py . link_write.py
        static_write.py . disc.py (IP.BIN via make_ip.py, xorrisofs, sox -> .raw 2352, .cue multi-fichiers)
                                           |  mkdisc.py --game doom|duke --in X --out build/disc/
 DISQUE : 0.BIN MAIN.BIN | STATIC.DAT | <MAP>.LEV xN | <MAP>.LNK xN | <MAP>.PSW xN (ou PSW.WAD en cartouche)
          GLOBAL.PSW MENU.PSW INTER.PSW TITLE.PSW | [GAME.BC + <MAP>.BLD Duke] | pistes CDDA 02..NN | INITLOAD.DAT minimal
```

1. **Le playsim** (`core/p_*.c`, `g_game`, `d_loop`, `info.c`, 341 Ko liés `[src R5]`) vit dans `MAIN.BIN` et lit ses lumps de carte depuis `<MAP>.PSW`, un mini-WAD par carte (en-tête IWAD + répertoire) chargé **entier en un bloc `mem_malloc(0)`** au début du niveau : 47-130 Ko en shareware, 12-171 Ko en Doom II `[mesuré]` — une lecture séquentielle, aucun seek, aucune modification de `FILE.C`.
2. **Le rendu** ne connaît que le `.LEV` (5 chargeurs séquentiels `[src SRUINS.C:1939-1943]`) : ciel, géométrie **fermée** (portes/ascenseurs en position de repos + course), sons dynamiques, palette PLAYPAL, tuiles de murs/flats, tuiles RLE 8 bpp des sprites du niveau, séquences.
3. **La table de liaison `<MAP>.LNK`** est un 6e fichier ouvert après `fs_close` du `.LEV` (le chargeur ne lit que ses 14 parts et n'accepte qu'un fichier ouvert `[src LEVEL.C:51-67, FILE.C:153]`) : elle dit au playsim quel sommet, quelle cellule, quel secteur `.LEV` correspond à chacune de ses structures.
4. **Le 2D** (menus, HUD, intermission, finale) reste `V_DrawPatch` inchangé, dessinant dans la feuille NBG0 512×512 8 bpp existante (pas 512, fenêtre W0, priorité sous les sprites `[src R5 §7, PIC.C:456-462]`) : **zéro conversion**, les patches sont livrés tels quels dans `GLOBAL.PSW` (PLAYPAL, TEXTURE1/PNAMES, STCFN, ST*, AMMNUM ≈ 100 Ko résidents), `MENU.PSW`, `INTER.PSW`, `TITLE.PSW` (chargés à l'écran, `.LEV` déchargé).
5. **Le son** : ~30 sons statiques (armes, joueur, portes) dans `STATIC.DAT`, les sons des monstres présents dans chaque `.LEV`, PCM `^0x80`, `rate` 0x7000/0x7800 `[src R3 CONVERT.C:3783-3812]` ; `s_sound.c` inchangé appelle `posMakeSound` via `sfx2snd[]` du `.LNK`.
6. **La musique** : MUS → MIDI → rendu SF2 hors ligne → WAV → `sox` raw 2352 → pistes AUDIO du `.cue` (recette Mimas `[src build.ps1:248-262]`), `trackMap[]` régénéré. Le CD n'est lu qu'**entre** les niveaux (tous les packs sont chargés d'un bloc), donc la tête est libre en jeu : le conflit streaming/CDDA de Mimas disparaît par construction.
7. **La sauvegarde** : checkpoint début-de-niveau (carte + `player_t` réduit ≈ 400 o) × 6 slots dans le format `SaveRec` de `BUP.C` ; la sauvegarde intégrale `p_saveg` (27-87 Ko `[src R5 §8]`) n'existe que sur cartouche backup.
8. **Le disque** : IP.BIN SRL réécrit par `make_ip.py`, `xorrisofs -generic-boot` (`[src Makefile:278-308]`), `.cue` multi-fichiers. Shareware ≈ 9 × 1,4-2,0 Mo de `.LEV` + 1,5 Mo de `.PSW` + 0,7 Mo STATIC ≈ **20 Mo de données + 260-460 Mo de CDDA** `[est]` ; Doom II ≈ 60 Mo + ≤ 74 min.
9. **La cartouche 4 Mo** est un accélérateur, pas un prérequis : `PSW.WAD` entier (1,51 Mo shareware, 3,69 Mo Doom II `[mesuré]`) copié en cart au boot par le `load_wad` de Mimas (~80 l., `validPtr` +1 ligne) → zéro lecture playsim par niveau ; et le plan C de R5 (sprites RLE + tuiles sur cart) reste **la seule voie pour E1M6 et Doom II** `[src R5 §6]`.
10. **Duke** suit la même chaîne avec trois produits en plus : `GAME.BC` (CON compilé hors ligne, ~80 Ko `[src R4 §3]`), `<MAP>.BLD` (sector/wall/sprite dans nos structs) et des quads muraux/sol dans le `.LNK` — sa partie données peut avancer en parallèle dès J2, son playsim est un programme séparé (55-90 j `[src R4 §7]`).

**Boucle hôte** (SRUINS.C conservé sauf le trou logique, `[src R2 §1]`) :

```c
while (1) {
  camera <- (player->mo->x, viewz, y), yaw=viewangle, pitch=0;          /* :2105-2118 */
  EZ_openCommand(); drawWalls(view);                                     /* :2120-2130, kick esclave */
  while (tics dus a 35 Hz depuis vtimer) { G_BuildTiccmd(inputQ); G_Ticker(); }   /* remplace :2137-2191 */
  drawWallsFinish();                                                     /* :2193 */
  psw_sync_world();          /* SEUL point d ecriture de la geometrie, hors fenetre esclave (:2197-2201) */
  if (automapactive) AM_Drawer_EZ(); psprites_draw(); V_2D_to_NBG0();   /* :2204-2214 */
  sound_nextFrame(); pic_nextFrame(); EZ_closeCommand(); SPR_WaitDrawEnd(); gouverneur; SCL_DisplayFrame();
  movePlax(viewangle,0); SCL_SetColOffset(tint(damagecount,bonuscount,radsuit));
}
```

**Passe de synchro `psw_sync_world()` — champs recopiés et coût E1M1** (85 secteurs, 648 sidedefs, ~115 mobjs `[src R5 §3]` ; SH-2 28 MHz, ~0,04 µs/instruction, tout `[est]`) :

| Source playsim | Cible `.LEV` via `.LNK` | Quand | Coût E1M1 |
|---|---|---|---|
| `sector.floorheight/ceilingheight` | `level_vertex[v].y` de chaque sommet lié (`vtx_follow`) | si ≠ copie fantôme (test 85 × 0,2 µs) ; 0-5 secteurs mobiles/tic × 100-300 sommets | 17 + 10-150 µs |
| `sector.lightlevel` (+`extralight`, `fixedcolormap`) | `vertexLight[]`/`firstLight` des plages du secteur (`AICOMMON.C:44-70`) | si ≠ fantôme ; strobes 0-10 secteurs/tic | 5-60 µs |
| `sides[].top/mid/bottomtexture` via `texturetranslation[]`, `floorpic` via `flattranslation[]` | octet impair de `level_texture[]` des cellules (`side2cells`) | interrupteurs à l'événement ; anims tous les 8 tics (E1M1 : 1 famille) | 5 µs |
| `mobj.x,y,z,angle,sprite,frame,flags,subsector` | `Sprite.pos, angle, sequence=seq(sprite,frame,facing), frame, flags(NOSHADOW/FLASH→banque 0/INVISIBLE/COMPO_SHADOW)` ; `moveSpriteTo` si la feuille change | test « inchangé » 0,3 µs ; 10-30 mobjs actifs × 2-3 µs | 35 + 30-90 µs |
| spawn/`P_RemoveMobj` | `newSprite(owner=NULL)` / `freeSprite` (O(n) sur la liste du secteur) | à l'événement | 2-5 µs/événement |
| `viewz, mo->angle` ; `psprites[2].{sx,sy,state}` ; indice PLAYPAL | caméra ; chunks d'arme ; colour offset | chaque frame | 3 µs |
| **Total** | | | **≈ 100-350 µs/tic** = < 1,2 % d'un tic 35 Hz |

**Adaptateur commun Doom/Duke** : l'API `world_link` (`wl_bind_vertex`, `wl_set_heights(id, f, c)`, `wl_set_light(id, l)`, `wl_set_cell_tile(cellref, tile)`, `wl_puppet(handle, pos, facing, seq, frame, flags)`, `wl_camera`) et le format `.LNK` sont **agnostiques** : les identifiants sont des « secteurs playsim » (Doom `sector`, Build `sector`), des « polygones playsim » (subsector Doom, sector Build), des « références de cellule ». Duke ajoute deux tables (quads muraux/sol cstat 16/32 ; pitch caméra) et parque translation/rotation `[src R4 §5]`.

**Format `.LNK` proposé** (big-endian, tout aligné 4 ; tailles E1M1 `[est]`) :

| Table | Enregistrement | Octets | E1M1 |
|---|---|---|---|
| en-tête | magic `PLNK`, version u16, 10 compteurs u32 | 48 | 48 |
| `leaf2sector[numsubsectors]` | u16 secteur `.LEV` (N:1 après fusion de feuilles) | 2 | 237×2 = 474 |
| `sector_leaves` | u16 first, u16 count → liste u16 | 4 + 2n | 85×4 + 474 |
| `vtx_follow` | u16 vertex, u16 secteur Doom, u8 which {floor, ceil}, u8 op {=, max(secB), min(secB)}, u16 secB | 8 | ~1 500 sommets liés × 8 = 12 Ko |
| `light_ranges` | u16 secteur Doom, u32 offset `vertexLight`, u16 count ; idem `firstLight` murs | 8 | 85 × 2 × 8 = 1,4 Ko |
| `side2cells` | u16 sidedef, u8 part {top, mid, bottom}, u8 pad, u16 wall, u16 texOff, u8 cell0, u8 ncells | 10 | commutables + animés ≈ 40 × 10 |
| `tex2tile[numtextures]`, `flat2tile[numflats]` | u8 tuile (0xFF = absente) ; familles animées = runs consécutifs | 1 | 125 + 54 |
| `sprite_seq` | par sprite présent : u8 sprite, u8 nframes, u8 flags(rotations), u16 seq0 ; puis nframes × (1 ou 8) s16 | var | ~300 lumps → ~1,2 Ko |
| `sfx2snd[NUMSFX]` | s8 indice son chargé (statique+dynamique), −1 absent | 1 | 109 |
| **Total** | | | **≈ 17 Ko en LWRAM** |

## 2. Inventaire des manques

| # | Manque | Jeu | Sévérité | Mécanisme (fichier touché) | j | Risque | Preuve |
|---|---|---|---|---|---|---|---|
| 1 | Portes/ascenseurs ouverts statiquement | Doom | BLOQUANT | `doom3d.py` : `mobile_bounds()` + `emit_edge_mobile` (contremarche `[fh,maxFloor]`, portail `[bot,top]`, linteau `[minCeil,ch]`, `WALLFLAG_DOORWALL`) ; sommets liés → `vtx_follow` | 5 | budget esclave compte les cellules **totales** (`WALLS.C:1374`) ; mur sauté en silence | `[src R3 doom3d.py:479-512, AI.C:4300-4307]` |
| 2 | Table de liaison absente | commun | BLOQUANT | `pslib/link_write.py` + `loadLink()` C (~120 l.), ouvert après le `.LEV` | 1,5 + 1 | à figer avant J3 | `[src FILE.C:153, LEVEL.C:51-67]` |
| 3 | Sprites non convertis | Doom | BLOQUANT | `wad2sprites.py` : découpe 64×64/32×32 → 0x6A/0x6C RLE, `sChunk/sFrame`, 1 séquence par (sprite, frame, rot), miroirs A2A8 = flag bit0 ; sous-ensemble par niveau via `info.c` | 4 | 31 slots `TILE8BPP` ; 800 pics ; LWRAM Doom II | `[src R3 PIC.C:568-580, SEQUENCE.C:299-302 ; mesuré 612 chunks]` |
| 4 | Sons DS* non convertis | Doom | BLOQUANT | `wad2snd.py` : DMX → `{size, 0x7000/0x7800, 8, −1}` + PCM `^0x80` ; statiques STATIC / dynamiques `.LEV` ; `sfx2snd` | 1,5 | 535 127 > 524 288 ; 80 sons max : liste blanche + downsample prioritaire | `[src SOUND.C:229-241 ; R3 mesuré]` |
| 5 | Ciel = donneur KILENTRY | Doom | MAJEUR | `plax.py` : SKY1 ×2 → 512×256, PLAYPAL banque 7, table K recopiée (identique dans 4 `.LEV`) | 1 | mapping 256 px/90° `[est]` | `[src R2 PLAX.C:25-53, R3 §2c]` |
| 6 | STATIC.DAT PowerSlave | Doom | BLOQUANT | `static_write.py` : TITLEPIC → écran 320×240, feuille VDP2 vide, ~30 sons statiques, **95 chunks psprites en tuiles d'armes**, `wSequence[0]=0` | 2 | `tileBase=95` ⇒ ≤ **160 tuiles de géométrie** par niveau (u8) | `[src SRUINS.C:1925-1931, LEVEL.C:69-72 ; R3 mesuré 95/130 063 o]` |
| 7 | Données playsim absentes du disque | Doom | BLOQUANT | `wad_psw.py` : `<MAP>.PSW` mini-WAD (lumps de carte, SEGS gardés v1), `GLOBAL/MENU/INTER/TITLE.PSW` ; option cart `PSW.WAD` | 1,5 | réactiver le multi-fichier de `w_wad.c` (garde :227-229) ; lumps alignés 4 | `[mesuré 47-130 Ko/carte ; R6 §1]` |
| 8 | Progression `levelGraph`/`runMap` | Doom | MAJEUR | table `map→"+E1M1.LEV"` générée + court-circuit `runMap` (SRUINS.C:2463, 2512) | 0,5 | — | `[src BIGMAP.C:43-82]` |
| 9 | Musique | commun | MAJEUR | `disc.py` : `mus2mid` (Python) → FluidSynth + SF2 utilisateur → WAV → `sox` raw 2352 ; dédoublonnage par hash ; `trackMap[]` régénéré | 2 | Doom II 32 morceaux vs 74 min ; qualité SF2 | `[src build.ps1:248-262 ; SOUND.C:374-390 R3]` |
| 10 | `.cue`/ISO multi-pistes, IP.BIN | commun | MAJEUR | `disc.py` : `xorrisofs` (Makefile:278-308) + écrivain cue + `make_ip.py` (product id) | 1 | — | `[src Makefile:278-308]` |
| 11 | `xoffset` des sidedefs ignoré | Doom | MINEUR | `tile_from_wall` : `uoff` | 1 | — | `[src R3 doom3d.py:262-268]` |
| 12 | Midtextures à 2 côtés | Doom | MAJEUR | `emit_midtex` : mur à faces 0x32 sur le portail, indice 0 transparent, BLOCKED si `ML_BLOCKING` | 2 | ordre peintre | `[src R3 #2]` |
| 13 | Flats/textures animés | Doom | MAJEUR | toutes les tuiles de la famille + runs `tex2tile` ; la synchro écrit l'octet impair (pas `PICFLAG_ANIM` : 2 frames ≠ 8 tics) | 2 | Doom II > 15 familles ; cap 160 tuiles | `[src R3 p_spec.c:96-124, PIC.C:110]` |
| 14 | Interrupteurs | Doom | MAJEUR | 2 tuiles + `side2cells` | 1 | — | `[src R3 p_switch.c:45-88]` |
| 15 | Lumière par secteur | Doom | MAJEUR | export `light_ranges` + `wl_set_light` | 1,5 | 5 banques CRAM au lieu de 32 colormaps | `[src R3 #6, R5 §7]` |
| 16 | Scrollers (special 48) | Doom | COSMÉTIQUE | impossible sans UV : statique v1 ; option tuiles pré-décalées ×8 (coûte le cache 28) | 0 | — | `[src R4 §5 LEV_FORMAT:95-100]` |
| 17 | Feuilles > 600 (E1M6 606, Doom II ×6, Ultimate ×9) | Doom | BLOQUANT (E1M6) | `merge_leaves()` : fusion de feuilles sœurs dont l'union est convexe ; repli `MAXNMSECTORS` 900 (+23 Ko) | 3 | corruption avant l'assert (`sectorDraw[600]` écrit par `initWallRenderer` ?) | `[src SRUINS.C:1988-1989 ; R3 mesuré]` |
| 18 | Budget tuiles par niveau (4 Ko HWRAM/tuile 0x32) | Doom | MAJEUR | sélecteur E4.1b/E4.1c par carte + classe 0x72 (RLE 16 bpp LWRAM, 3 ms/swap) au-delà d'un cap HWRAM | 1,5 | swaps mesurés 0-1/frame | `[src R5 §4 PIC.C:603-610]` |
| 19 | LWRAM sprites Doom II (MAP29 2,05 Mo) | Doom II | BLOQUANT (Doom II) | miroirs ÷1,6 + 0x6C + sous-ensemble par skill + cart (plan C) | 2 | — | `[src R3 mesuré, R5 §6]` |
| 20 | `MAXNMPICS` 800 de base (mipmaps ×2) | Doom II | MAJEUR | ne pas mipper les 8 bpp (`PIC.C:423-433`) ou relever (16 o/pic) | 0,5 | — | `[src R3 PIC.C:30, SRUINS.C:2063]` |
| 21 | PWAD | Doom | MINEUR | `merge_wad.py` (dernier gagne) + **bug** `wad.py:51` `setdefault` = premier gagne → inverser | 0,5 | silencieux | `[src wad.py:50-51]` |
| 22 | DEHACKED | Doom | MINEUR | `deh.py` → `DEH.BIN` : patch de `states[]` (27 076 o) / `mobjinfo[]` (12 604) / `S_sfx[]` en `.data` au boot + chaînes par pointeurs ; **appliqué avant** le calcul des sous-ensembles sprites/sons | 2,5 | Misc/Text partiels (macros `deh_misc.h`) | `[src R5 §2 ; R1 deh_str.h:38]` |
| 23 | Validateurs LNK/STATIC/disque | commun | MAJEUR | `verif_lnk.py` (tout secteur mobile lié, tout sidedef commutable cellulé, `sprite_seq` couvre les états atteignables, `sfx2snd` couvre les sons des mobjs présents, caps moteur), `verif_static.py`, `verif_disc.py` (8.3, `size<900000`, 2352, ≤ 74 min) | 2 | — | `[src lev_write.py:176 engine_problems]` |
| 24 | Sauvegarde | Doom | MAJEUR | checkpoint 6 slots dans `SaveRec` (BUP.C) ; intégrale = cart backup seulement | 2 | 32 Ko BUP | `[src R5 §8, R6 §6]` |
| 25 | 2D menus/HUD/intermission | Doom | BLOQUANT | `V_DrawPatch` inchangé → feuille NBG0 stride 512 + W0 ; lumps depuis `.PSW` | 3 (moteur) | neige si cycles mal posés ; W0 unique | `[src R5 §7, R2 §5]` |
| 26 | Arme (psprites) | Doom | BLOQUANT | `EZ_normSpr` COLOR_4 par chunk à `(sx>>16−160+chunkx, sy>>16−120+chunky)` | 1,5 (moteur) | 31 slots partagés avec les monstres | `[src R2 SEQUENCE.C:296-305]` |
| 27 | Marionnettes mobj→Sprite + synchro | Doom | BLOQUANT | `psw_sync_world` (`moveSpriteTo` SPRITE.C:916) ; `MAXNMSPRITES` 450→650 (+14 Ko) | 3 (moteur) | tri O(n²)/secteur de `drawSprites` | `[src R2 §3]` |
| 28 | Playsim intégré (boucle hôte, shims `DG_*/I_*` Mimas, `_sbrk`, pile 24 Ko, newlib) | Doom | BLOQUANT | R2 §« boucle hôte » + R6 (a) | 10-15 | `_sbrk` du fork échoue toujours → `I_Error` au boot | `[src R6 §10]` |
| 29 | Flash palette, fullbright, spectre | Doom | MINEUR | `SCL_SetColOffset` stateless ; banque 0 ; `COMPO_SHADOW`/MESH | 1 | inversion (invulnérabilité) irreproductible sur murs 16 bpp | `[src R2 §8]` |
| 30 | Duke : `convex.py` 33/194 cartes | Duke | BLOQUANT | robustesse (trous, éclats, superpositions) | 6 | **goulot** | `[doc R3 NOTES_QUANT.md:150-157]` |
| 31 | Duke : ART → sprites, `pal>0` | Duke | BLOQUANT | `duke_sprites.py` : familles complètes (300-400 chunks/carte), palettes cuites en tuiles distinctes (CRAM 8/8) | 4 | tuiles × pal | `[src R3 #19]` |
| 32 | Duke : VOC 2,9 Mo → SCSP | Duke | MAJEUR | `voc2snd.py` + partition par niveau (équivalent `cacheit`) | 1 | 512 Ko / 80 sons | `[src R3 #24, R4 §4]` |
| 33 | Duke : CON | Duke | BLOQUANT | `con2bc.py` (port GPL-2+ de `parsecommand`) → `GAME.BC` ~80 Ko ; interprète seul embarqué | 5-8 | CPU `parse()` 5-25 ms/tic `[est]` | `[src R4 §3]` |
| 34 | Duke : MAP → `.BLD` + physique clean-room | Duke | BLOQUANT | `map2bin.py` (nos structs) + 1 900 l. réécrites + harnais différentiel PC | 10-15 | BUILDLIC | `[src R4 §2]` |
| 35 | Duke : sprites muraux/sol, panning, ciel `LA`, MID→CDDA | Duke | MAJEUR | quads `EZ_distSpr` listés dans le `.LNK` ; `pu/pv` par cellule ; `plax.py` ; recette #9 | 5 | cache 28 | `[src R3 #20-25, R4 §5]` |
| 36 | Duke : miroirs, caméras, secteurs mobiles SE6/14/30 | Duke | impossible | mur opaque / tuile fixe / parqué | 0 | — | `[src R4 §5]` |

Total outils Doom ≈ **32 j** ; moteur/playsim Doom ≈ **25-30 j** ; Duke données ≈ **16 j**, Duke playsim **55-90 j** `[est]`.

## 3. Plan en jalons

| J | Livrable | Coût (j) | Dépend de | Kill criterion |
|---|---|---|---|---|
| **J1 « Densité »** | `mkdisc.py` squelette + `E1M1.LEV` avec **sprites et sons Doom** + hook moteur 40 l. (marionnettes statiques aux THINGS, séquence Doom, un son par frame) | 7 | rien | `vswaps[1]` > 8/frame ou texture fausse à ≤ 15 things visibles ; `mem:` < 100 Ko ; `assert soundTop` |
| **J2 « Portes »** | géométrie fermée + course, `.LNK` v1, `psw_sync_world` piloté par un script (ouvre/ferme toutes les portes au chronomètre, sans playsim) | 9 | J1 | fissures/scintillement visibles ; compteur « murs sautés » > 0 ; recalcul `SHORTOPENING` faux |
| **J3 « E1M1 se joue »** | playsim intégré, `.PSW`, HUD/menus NBG0, arme, sons, progression E1M1→E1M2 | 18 | J2 | tic + synchro + `drawSprites` > 33 ms console avec 10 monstres visibles ; LWRAM négative |
| **J4 « Shareware complet »** | 9 `.LEV`, intermission/finale, CDDA, checkpoints, `merge_leaves` (E1M6), `verif_disc`, `/ship` | 12 | J3 | E1M6 hors budget sans cartouche |
| **J5 « Autres WADs »** | Doom II/Ultimate/PWAD/DEH ; fusion ≥ 875 feuilles ; LWRAM sprites ; `MAXNMPICS` | 8 | J4 | MAP15/MAP29 hors budget sans cartouche |
| **J6 « Duke données »** | `convex.py` robuste, E1L1-E1L6 `.LEV/.LNK/.BLD`, sprites/VOC/MID, disque testable | 16 | J2 (parallélisable) | < 5/6 cartes converties |
| **J7 « Duke joue »** | CON hors ligne + interprète, clean-room, hook objets | 55-90 | J6 + J3 | `parse()`+`clipmove` > 20 ms/tic mesuré sur un acteur × 30 |

**J1 — l'hypothèse la plus risquée pour le moins cher.** Ce qui peut tuer l'idée entière n'est ni la géométrie (E1M1 tourne déjà) ni le playsim (il tourne sur Mimas) : c'est **le cache de 31 tuiles 8 bpp** partagé entre monstres et arme `[src R2 SRUINS.C:1903, PIC.C:264-272]` — une tuile évincée dans la frame donne une texture fausse, pas seulement un ralentissement — et le **budget son** (535 > 512 Ko). Livrable : `wad2sprites.py`, `wad2snd.py`, `rle8.py`, `snd_scsp.py`, `static_write.py` (STATIC Doom), `disc.py` v0 (un `.LEV`, pas de CDDA) ; côté moteur, 40 lignes : après `placeObjects`, pour chaque THING d'E1M1, `newSprite(link[leaf], radius, 0, 0, seq(sprite, A, rot 1), NOSHADOW, NULL)`. **Vu → sens** : (a) 20-30 monstres immobiles aux bonnes positions, bonne taille, bonne face → format RLE/chunks/séquences validé ; (b) STATUSTEXT `used[1]/vswaps[1]` : ≤ 3 swaps/frame en tournant sur soi devant 15 things → le cache tient ; ≥ 8 ou monstres « mélangés » → **kill**, il faut moins de chunks (32×32, miroirs) ou une autre classe ; (c) chaque monstre joue son cri à l'entrée de sa frame → sons DMX validés ; (d) ligne `mem:` ≥ 200 Ko → E1M1 tient en plan B `[src R5 §6]`. Décision owner : classe des tuiles de murs (0x32 vs 0x72) — mesurée ici.

**J2 — la géométrie mute.** Le convertisseur cesse d'appeler `open_doors()` ; le `.LNK` v1 porte `vtx_follow` et `leaf2sector` ; un script C (pas de playsim) translate les sommets de tous les secteurs mobiles entre `drawWallsFinish` et le `drawWalls` suivant. **Vu → sens** : portes qui montent/descendent sans trou ni tremblement → liaison correcte ; « fissure » d'un pixel au linteau → sommets max/min mal liés (op) ; murs qui disparaissent par intermittence → budget esclave (compteur à ajouter à STATUSTEXT). Décision : `merge_leaves` maintenant (E1M6) ou après J3.

**J3 — E1M1 se joue** (R2 boucle hôte + R6 shims). **Vu → sens** : tir → recul de l'arme + flash + imp qui saigne et meurt en 5 frames ; porte à l'usage ; ramassage → flash jaune ; barre d'état vivante ; mort → visage ; fin de niveau → intermission Doom → E1M2 charge. Chiffres : ligne 14 (`calc`) + une ligne `tic/sync/spr` en ms. Décision : cartouche-first ou `.PSW` par carte (recommandé : `.PSW`, marche sur Saturn nue, 0 changement moteur).

**J4 — le disque shareware** : 9 cartes, `MENU/INTER/TITLE.PSW`, CDDA 13 pistes (~45 min, ~450 Mo `[est]`), checkpoints, `verif_disc`, `/ship` aux testeurs. **Vu → sens** : E1M6 boote → `merge_leaves` a ramené 606 → ≤ 600 ; musique change à chaque carte → `trackMap` bon ; retour titre sans reboot.

**J5** : Doom II MAP01-MAP32 chargent (32 `.LEV`, 60 Mo) ; PWAD mergé ; DEH appliqué (Batman Doom : sprites renommés → sous-ensembles recalculés). **J6/J7 Duke** : R4 §7.

## 4. Décisions owner

| Décision | Recommandé | Pourquoi |
|---|---|---|
| Musique : CDDA rendue (SF2 au choix) vs synthé MUS SH-2 de Mimas | **CDDA** | le disque par packs libère la tête CD en jeu ; le synthé Mimas = 3 ondes, pas de percussions, 15 slots SCSP en concurrence avec 470-526 Ko de SFX `[src R5 §5]` |
| Données playsim : `.PSW` par carte vs `PSW.WAD` en cartouche | **`.PSW` d'abord**, cart ensuite | Saturn nue, zéro seek, zéro modification de `FILE.C` ; la cart reste obligatoire pour E1M6/Doom II (plan C R5) |
| Psprites : tuiles d'armes (STATIC) vs tuiles du `.LEV` | **tuiles d'armes** + budget E4.1b (≤ 160 tuiles) | 0 changement moteur ; repli = tuiles par niveau (+130 Ko LWRAM/niveau, ~10 l.) |
| Classe des tuiles de murs : 0x32 (HWRAM) vs 0x72 (LWRAM RLE) | **mixte décidé par l'outil** : 0x32 jusqu'au cap HWRAM, 0x72 au-delà | E1M1 141 × 4 Ko = 134 % de la HWRAM libre `[src R5 §4]` ; à confirmer par `vswaps` en J1 |
| > 600 feuilles : fusion vs `MAXNMSECTORS` 900 | **fusion d'abord** | outil seul ; le relèvement coûte 23 Ko et touche `sectorDraw/penetrate/processed` |
| Sauvegarde : checkpoint vs intégrale | **checkpoint** v1 | 32 Ko BUP vs 27-87 Ko par partie |
| 2D : feuille NBG0 vs NBG1 en B1 | **NBG0 existante** | 0 VRAM, 0 cycle ; NBG1 exige de réduire la feuille et un test neige |
| DEH | oui, après J4 | 2,5 j, tables `.data` déjà en RAM |
| Duke | **données en parallèle dès J2, playsim go/no-go après J3** | 2× l'effort Doom, physique clean-room obligatoire `[src R4]` |

## 5. Inconnues à mesurer d'abord

1. `used[1]/vswaps[1]` (STATUSTEXT `SRUINS.C:2253-2262`) avec 15-20 marionnettes Doom + 4 chunks d'arme — J1.
2. `mem:` (`SRUINS.C:2240`) et `coreleft=` (`dPrint` `LEVEL.C:39`) après chargement complet d'E1M1 avec sprites et sons — fixe le plan B/C.
3. `soundTop` après statiques + dynamiques, et latence key-off sous 10-30 `S_StartSound`/s — sonde `htimer` autour de `playSoundE`.
4. Facteur de partage des sommets `.LEV` (script sur `doom3d.py` : (x,y,z) distincts / 13 171) — décide de la géométrie E1M6 en HWRAM.
5. Murs sautés par le budget esclave avec portes fermées (compteur sur `WALLS.C:1374`) — J2.
6. Durées réelles des 13 MUS rendus (script `mus2mid` + FluidSynth) — taille CDDA exacte.
7. Coût A-bus d'un `map()` de tuile depuis la cartouche (sonde `htimer` autour de `dmaMemCpy`) — si plan C.
8. `Z_FreeMemory()` au chargement d'E1M1/E1M6 dans Mimas — taille du bloc zone.

## 6. Ce que ce plan sacrifie

- **Fidélité de rendu** : éclairage en 5 banques CRAM (pas 32 colormaps ni atténuation par pixel), pas d'inversion sur les murs (invulnérabilité), spectre = ombre/mesh au lieu du fuzz, scrollers immobiles, melt remplacé par un fondu, mid-textures et ordre peintre par secteur avec clip aux frontières de pièces convexes `[src R2 §2]`.
- **Contenu** : démos d'attract supprimées ; ENDOOM jamais ; musique = rendu SF2 (pas l'OPL) ; Doom II au-delà de ~900 feuilles ou 2 Mo de sprites exige la cartouche ; PWAD limités au vanilla de Chocolate (pas de Boom/MAPINFO) ; DEH Misc/Text partiels.
- **Sauvegarde** : checkpoint début de niveau seulement sans cartouche backup.
- **Cartes trop grandes** : E1M6 et 6 cartes Doom II passent par la fusion de feuilles (géométrie approchée le long des partitions), ou ne bootent pas.
- **Duke** : miroirs, caméras, secteurs en translation, panning, brouillard, RTS, réseau — impossibles ou dégradés ; 181 VOC réduits à 40-60 par niveau ; playsim = programme séparé dont la faisabilité CPU n'est pas prouvée.
- **Le plus petit mécanisme partout** : pas de seek CD, pas de format `.LEV` modifié (tout passe par un 6e fichier), pas de constante moteur relevée avant que l'outil ait échoué à contourner.

---
Scripts de mesure : `scratchpad/reports/d2_playsim_wad.py`. Copie identique : `scratchpad/reports/D2_pipeline_disque.md`.

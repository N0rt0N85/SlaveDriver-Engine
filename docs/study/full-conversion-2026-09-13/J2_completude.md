# J2 — Complétude contre « TOUT » : jugement adverse de D1 / D2 / D3 (2026-09-13)

Lentille : « quand je convertis un WAD, j'ai l'impression de jouer à Doom ». J'ai d'abord dressé ma propre liste de contrôle depuis `core/` et `jfduke3d/src`, puis coché chaque proposition. Étiquettes : `[src] FICHIER:LIGNE` ouvert ce jour ; `[mesuré]` = `scratchpad/reports/j2_wadcheck.py` sur `Mimas/cd/data/DOOM1.WAD` ; `[est]`. Codes : ✓ couvert · ~ partiel/sous-budgété · ✗ ignoré · † déclaré impossible/sacrifié.

## 0. Ma liste de contrôle et son cochage

### Doom (core/)

| # | Ce qui fait « jouer à Doom » | Preuve | D1 | D2 | D3 |
|---|---|---|---|---|---|
| 1 | 11 specials de secteur (1-4, 8-10, 12-14, 17 : blink/strobe/glow/flicker, secret, porte 30 s / 5 min) + dégâts 5/10/20 + spécial 11 (E1M8 : dégâts puis `G_ExitLevel`) | `[src] p_spec.c` P_SpawnSpecials cases 1,2,3,4,8,9,10,12,13,14,17 ; P_PlayerInSpecialSector cases 5,7,16,4,9,11 | ✓ (playsim intact + sync lumière/hauteurs) | ✓ | ✓ |
| 2 | Specials de ligne : 85 `case` croisés, 4 tirés, 69 utilisés ; scroller 48 seul animé | `[src] p_spec.c` (comptage awk), :1126-1140 | ✓ / 48 † (8 pas ou statique) | ✓ / 48 † | ✓ / 48 † |
| 3 | Portes/ascenseurs/escaliers/crushers/donut à 35 Hz (géométrie `.LEV` fermée + course) | R3 #5 ; `[src] doom3d.py:479-512` `open_doors()` | ✓ #7 | ✓ #1 | ✓ #7 (rangées 64 u) |
| 4 | Interrupteurs + boutons 35 tics, flats/textures animés 8 tics (23 familles) | `[src] p_spec.c:96-124` animdefs | ✓ #9 | ✓ #13-14 | ✓ #8-9 |
| 5 | Midtextures grillagées, `xoffset`, ciel `F_SKY1` défilant 256 px/90° | `[src] PLAX.C:24,28` PLAXPERSCREEN 128 /F(45) | ✓ #22 #31 | ✓ #5 #11-12 | ✓ #11-12 #15 |
| 6 | Flags visibles : `MF_SHADOW` (fuzz = `colfunc=fuzzcolfunc` sur colormap NULL), `FF_FULLBRIGHT`, `MF_NOSECTOR`, `MF_TRANSLATION` | `[src] r_things.c:645-648` | ✓ ✓ ✓ ✓ (#14 #13 #11 #15) | ~ (#29 : pas de couleurs joueurs) | ~ (#13-14 ; couleurs joueurs ✗) |
| 7 | 8 rotations choisies par l'angle caméra→mobj, `flip` par frame | `[src] r_things.c:864-878` ; `[src] AICOMMON.C:78-99` facing 0..7 bornes ±23° | ✓ (rot côté hôte) | ✓ | ✓ |
| 8 | **Échelle et ancrage des sprites** : 1 texel = 1 unité ⇔ `o->scale=65536` (défaut 48000) ; pieds à `pos.y − radius` | `[src] WALLS.C:2698,2718-2724` | ✗ (#11 : `radius = mobjinfo.radius` ⇒ sprite enterré de 16-64 u) | ✗ (« bonne taille » à l'œil J1) | ✗ |
| 9 | Psprites arme + flash, bob `sx/sy`, fullbright du flash | `[src] SEQUENCE.C:296-305` patron `EZ_normSpr` | ✓ #16 | ✓ #26 | ✓ #16 |
| 10 | 13 palettes (rouge 1-8, or 9-12, vert 13) | `[src] st_stuff.c` ST_doPaletteStuff ; deltas `[mesuré]` | ✓ colour offset (valeurs exactes) | ✓ | ✓ |
| 11 | Inversion (invulnérabilité), lampe IR | `[src] p_user.c` fixedcolormap | † | † | † |
| 12 | HUD complet (STBAR, visage, clés, armes, munitions, armure, messages) | 2D `V_DrawPatch` intact | ✓ #18 | ✓ #25 | ✓ #17 |
| 13 | Automap : `AM_clearFB`, grid, walls (couleurs par flags), players, **things (IDDT)**, crosshair, **marks** | `[src] am_map.c` AM_Drawer (7 sous-tracés) ; `cheat_amap` :266 | ✓ #21 (intact) | ~ (« AM_Drawer_EZ » = lignes seules) | ~ (« am_ez_lines », 0,5 j) |
| 14 | Touches automap follow/grid/marks (`f`/`g`/`m`), gamma F11, pause, sélection d'arme | `[src] m_controls.c:111,143-145,170` ; table pad Mimas dg_saturn.cxx:17063-17078 (10 touches) | ✗ | ✗ | ✗ |
| 15 | Menus : Nouveau (épisode/skill), Options (détail/taille = no-op, volumes, messages), ReadThis HELP1/2, End/Quit (`__sh__` → titre) | `[src] m_menu.c:1131-1153` | ✓ | ✓ | ✓ |
| 16 | **Sauvegarde : 6 slots dont le nom est TAPÉ au clavier, `M_DoSave` refusé si vide** | `[src] m_menu.c:635-645, 1594-1596` | ✗ (backend seul) | ✗ | ✗ |
| 17 | Backend de sauvegarde (BUP 32 Ko vs 27-87 Ko) | R5 §8 | ✓ checkpoint 100 o | ✓ checkpoint | ✓ **différentiel 2-4 Ko** (mi-niveau sans cart) |
| 18 | Intermission (stats, WIA, par, entering), finale (texte + flat de fond, cast Doom II, bunny), HELP2 shareware | `[src] f_finale.c:70-83` flats FLOOR4_8/SFLR6_1/MFLR8_4/MFLR8_3/SLIME16/RROCK* ; :661-680 | ~ #28 (flats de fond absents d'UI.DAT) | ~ | ~ |
| 19 | Wipe melt | `[src] f_wipe.c` | † fondu | † | † |
| 20 | Cheats clavier (iddqd, idkfa, idclev, idmus, iddt) | `[src] st_stuff.c:405-425` | ✓ #25 (page de menu) | ✗ | ✗ |
| 21 | Démos attract DEMO1-3 (présentes : 20 118 / 15 358 / 8 550 o `[mesuré]`), déterminisme | `[src] d_main.c:895-897` alterne 0↔2 | ✓ #29 (+gouverneurs OFF) | † « supprimées » | ✗ |
| 22 | Sortie secrète, progression, épisodes 1-3, Doom II 32 cartes, Ultimate E4 (SKY4) | `[src] g_game.c` | ✓ E1 ; Doom II cart-only | ✓ + Ultimate (J5) | ✓ (J5) |
| 23 | DEHACKED (absent du core) | `[src] deh_str.h:38` | ~ #30 (3 j « plus tard ») | ✓ #22 (2,5 j, appliqué avant les sous-ensembles) | † |
| 24 | PWAD (`merge_wad`, bug `wad.py:51` premier-gagne) | `[src] tools/doom2ps/wad.py:50-51` | ✗ | ✓ #21 | ✓ #30 |
| 25 | Split-screen 2-4p, deathmatch/coop local (respawn objets, frags, couleurs joueurs) | `[src] g_game.c:1860-1885` (Mimas) | ~ #35 #40 (J7, 4 j) | ✗ | † « hors plan » |
| 26 | Sons : 8 canaux, priorités, **re-déclenchement** (Doom coupe et relance le même son de la même origine) | `[src] s_sound.c` ; `[src] SOUND.C:322-330` refuse le même (source, son) tant qu'il joue | ✓ backend Mimas (key-off/KYONEX) | ~ (« s_sound.c inchangé → posMakeSound » hérite du refus) | ~ (idem, #21) |
| 27 | Liste blanche sons/niveau (535 127 > 524 288) | `[src] SOUND.C:218` | ✓ | ✓ | ✓ |
| 28 | Musique : 13 D_* dont D_INTER/D_INTRO/D_VICTOR/D_INTROA, idmus, pause | `[mesuré]` 13 lumps | ✓ CDDA + MUS secours | ✓ CDDA (SF2) | ✓ MUS SH-2 |
| 29 | Nightmare respawn, `-fast`, respawn objets DM | `[src] p_mobj.c:381-534` | ✓ (intact ; gouverneurs OFF) | ✓ | ✓ |
| 30 | Pickups/inventaire, berserk, powers | `[src] p_inter.c` | ✓ | ✓ | ✓ |
| 31 | **Aspect vertical** : Doom 320×200 étiré ×1,2 sur 4:3 ; Saturn 224 lignes ×1,07 / 240 lignes ×1,0 ⇒ monde 11-17 % plus plat à 1:1 | `[src] params/doom.cfg` « converts 1:1 » ; `[est]` | ✗ | ✗ | ✗ |
| 32 | FOV 90° | `[src] WALLS.H:4` FOCALDIST 160 / 320 px | ~ (listé « inconnu ») | ✗ | ✗ |

### Duke (jfduke3d/src)

| # | Item | Preuve | D1 | D2 | D3 |
|---|---|---|---|---|---|
| 1 | Physique/collision (clipmove…), CON compilé/interprété, `rotatesprite` → primitif 2D, HUD/menus/bonus (`dobonus` game.c:9177), quotes (`FTA` 79 sites) | R4 ; `[src] game.c:2023,9177` | ~ (une ligne, 55-90 j) | ✓ (produits nommés) | ✓ (D0-D5, spec par comportement) |
| 2 | 17 ST (`operatesectors`), ~40 SE, eau SE7, pentes | `[src] sector.c` 17 `case` ; actors.c 4805-7032 | ~ | ~ | ✓ |
| 3 | Subway SE6/14/30, rotation SE0/1/11, miroirs, caméras, panning, ROR | R4 §5 | † | † | † / rotation ~ |
| 4 | Inventaire, jetpack, accroupi, saut, natation, air (`scuba_on`), shrinker (→ `Sprite.scale`), holoduke, nightvision | `[src] player.c` 68 occurrences ; `[src] SRUINS.C:1427` drawAirMeter ; `[src] WALLS.C:2718` | ✗ | ✗ | ✗ (playsim « tel quel » sans mapping rendu) |
| 5 | **Séisme SE2 → `earthQuake` du moteur ; `rotscrnang` → roll caméra existant** | `[src] game.c:3053-3056` ; `[src] SRUINS.C:1831-1838, 2106-2109` | ✗ (D1 retire earthQuake) | ✗ | ✗ (« pitch=roll=0 ») |
| 6 | **Tripbomb laser → `SPRITEFLAG_LINE`** existant | `[src] SPRITE.H:38` | ✗ | ✗ | ✗ |
| 7 | Verre/murs cassables, forcefields (`overpicnum` W_FORCEFIELD) | `[src] sector.c:466-495, 1134` | ✗ | ✗ | ✗ |
| 8 | Sprites muraux/sol cstat 16/32 | R4 §5 | ✓ #37 | ✓ #35 | ✓ #36 |
| 9 | 10 slots avec capture (`savegame[10][22]`, `loadpheader`), démos (`opendemoread`), cheats tapés (`cheatquotes` « kroz », « stuff ») | `[src] duke3d.h:303 ; menues.c:127 ; game.c:5940-5959, 8145` | ✗ | ✗ | ~ (checkpoint #42) |
| 10 | VOC 2,9 Mo → 40-60/carte ; MID → CDDA | R4 §7 | ✗ | ✓ #32 | ✓ #40 |
| 11 | Licences (BUILDLIC clean-room, GPL-2+ jeu, jamais de CON/ART) | R4 §6 | ~ | ✓ | ✓ (+ question juridique structs) |

## 1. Notes

| Critère | D1 fidélité | D2 pipeline | D3 risques |
|---|---|---|---|
| Fidélité Doom | **8** — 2D et automap intacts, son Mimas, 35 Hz, cadre 224, tints exacts ; rate l'ancrage/échelle des sprites et l'aspect | **6** — automap réécrit, sémantique son du moteur héritée, démos supprimées, 240 lignes | **6** — idem D2 pour automap/son ; sauvegarde différentielle = seul vrai gain de fidélité |
| Fidélité Duke | **3** — déféré en une ligne | **5** — GAME.BC/.BLD/voc2snd nommés, données en parallèle | **7** — clean-room spécifié, D0 exécutable maintenant, impossibles listés ; ignore 3 mécanismes que le moteur offre déjà (quake, roll, laser) |
| Complétude vs « TOUT » | **7** — 40 items, seul à couvrir démos, cheats, split, couleurs joueurs, cast | **6** — DEH/PWAD/validateurs, mais démos †, cheats ✗, split ✗ | **6** — 43 items mais split †, DEH †, démos/cheats ✗ |
| Faisabilité RAM/CPU | **6** — E1M1 « à 50 Ko près » `[est]`, oublie le multi-WAD et le plafond `tileBase` | **7** — `.PSW` par carte, 0x32/0x72 mixte, plafond 160 tuiles vu | **8** — B plancher / C déverrouillage, régime chiffré, R-1..R-8 avec instruments |
| Économie des mécanismes | **7** — garde `V_DrawPatch`, `p_saveg`, AM_Drawer | **8** — pas de seek, format `.LEV` intact, 6e fichier, psprites en tuiles d'armes sans changement moteur | **6** — nouveau format de sauvegarde, `r_bridge.c`, automap réécrit |
| Testabilité console | **8** — J0 gratuit (T dans l'overlay Mimas), vu→sens par jalon | **7** — kill criteria chiffrés, `verif_*` | **8** — chaque risque a son instrument et son seuil ; J0 headless sans conversion |

## 2. Réfutations (vérifiées dans le code)

1. **D2 (§1.4) : « feuille NBG0 … priorité sous les sprites » (repris de R5 §7) — faux.** `[src] SRUINS.C:1071` `SCL_SetPriority(SCL_NBG0,6)`, `:1073` `SCL_SetPriority(SCL_SP0,4)` : la feuille est **au-dessus** du monde. Conséquence non budgétée par D2 : tout octet ≠ 0 du tampon Doom masque la 3D ; les lignes 0-191 doivent être remises à 0 chaque frame et TITLEPIC (3 411 pixels d'index 0 `[mesuré]`) exige la LUT 0→247 que seul D1 prévoit.
2. **D1 (#4) : `E1Mx.DAT` + `UI.DAT` + `MENU.DAT` + `WI.DAT` ouverts par `w_wad.c`** — `[src] w_wad.c:227-229` : `I_Error("W_AddFile: multi-file not supported (lumpinfo.wad_file dropped, R4.3c)")`. Deux WAD simultanés tuent le boot ; D2 (#7) budgète la réactivation, D1 non.
3. **D1 (#16, décision 2) : psprites en tuiles d'armes de STATIC.DAT sans conséquence** — `[src] LEVEL.C:69-72` : `level_texture[i]+=tileBase` sur `unsigned char` et `level_face[i].tile+=tileBase` ⇒ 95 chunks d'arme (R3) laissent **160 tuiles de géométrie**. D1 §6 annonce « ≈ 170 tuiles E4.1c » pour E1M6 : dépassement silencieux (wrap u8). D2 #6 le voit.
4. **D3 (§1.5) et D2 (§1.5) : « `s_sound.c` intact sur `playSoundE`/`posMakeSound` »** — `[src] SOUND.C:322-330` : le moteur **refuse** un son identique du même `source` tant qu'il joue, et un même son sans source deux fois par frame. Doom (`S_StartSound`) coupe et relance : pistolet/chaingun toutes les 4 tics (114 ms < durée DSPISTOL) perdraient des coups. Il faut `stopAllSound(source)` avant, ou le backend Mimas (D1) qui fait key-off + attente KYONEX `[src] i_sound_saturn.cxx:605-614`.
5. **D1 (§5, #17) : « FOV du moteur inconnu, à confronter aux 90° »** — `[src] WALLS.H:4` `FOCALDIST 160`, `:15` `PROJECT(x,z)=x·160/z` sur 320 px ⇒ tan(FOV/2)=1 ⇒ **90° exactement**. Inconnue déjà levée.
6. **Les trois : sauvegarde = question de backend** — `[src] m_menu.c:642-645` vide le nom du slot, `:1594-1596` `if (savegamestrings[saveSlot][0]) M_DoSave(saveSlot)` : sans **saisie clavier** d'un nom non vide, aucune sauvegarde n'est déclenchée, BUP ou pas. Aucune proposition ne prévoit le nom automatique (`HU_TITLE`) ni un clavier virtuel au pad.
7. **D1 (#11) : `newSprite(..., radius = mobjinfo.radius, …)`** — `[src] WALLS.C:2698` `feetPos.y=o->pos.y-o->radius` : le rayon est **vertical** pour l'ancrage. Un Baron (radius 24) posé à `pos.y = z` aurait les pieds 24 u sous le sol. Et `:2718-2724` : `scale=MTH_Div(o->scale*FOCALDIST,z)`, `width64=scale>>10` ⇒ 1 texel = 1 unité **uniquement** à `o->scale=65536`, pas au défaut 48000 (0,73×). Aucune des trois ne fixe ces deux constantes.
8. **D2/D3 : automap en `EZ_line` pour 0,3-0,5 j** — `[src] am_map.c` `AM_Drawer` = `AM_clearFB` + `AM_drawGrid` + `AM_drawWalls` (4 familles de couleurs, `ML_MAPPED`/`ML_SECRET`/`LINE_NEVERSEE`) + `AM_drawPlayers` + `AM_drawThings` (IDDT, `cheat_amap` :266) + `AM_drawCrosshair` + `AM_drawMarks` (AMMNUM). Sept sous-tracés ; l'`EZ_line` de `MAP.C:204-208` accepte bien une couleur par ligne, mais things/marks/grid ne sont ni listés ni chiffrés. D1 garde `AM_Drawer` intact dans le tampon 8 bpp (0 j).
9. **D1 (§1.3) : « 1 unité Doom = 1 unité Saturn » présenté comme la fidélité** — `[src] params/doom.cfg` le confirme pour les texels, mais Doom affiche 320×200 étiré ×1,2 sur 4:3 ; la Saturn étire 224 lignes ×1,07 (240 : ×1,0). Un monde 1:1 paraît **11 % plus plat** dans le cadre 224 de D1, **17 %** dans le 240 de D2/D3 `[est]`. C'est exactement « le graphisme change » dans le mauvais sens ; personne ne le nomme ni ne propose le facteur (hauteurs ×1,12-1,2 au convertisseur, ou projection verticale non carrée dans `project_point`).
10. **D2 (#7) : `.PSW` « SEGS gardés v1 »** — R5 §3 (vérifié par `fields.sh`) : `seg_t` n'est jamais lu hors `p_setup.c`. 8-37 Ko par carte transportés pour rien ; à retirer dès v1.
11. **D2 (« démos supprimées » en sacrifice) et D3 (silence)** — `[src] d_main.c:895-897` n'alterne 0↔2 que par un patch `// SATURN:` d'une ligne, et DEMO1-3 sont dans le WAD strippé `[mesuré]`. Les rejouer coûte 0,5 j (D1 #29) et c'est un test de déterminisme gratuit du playsim porté.
12. **D3 (décision 5) et R5 §5 : le `-Mus` de Mimas « vit sur le driver 68K »** — `[src] i_sound_saturn.cxx:493-511` : `SRL::Sound::Hardware::Initialize` (SDDRVS) n'est appelé **que** si `sat_music_use_cdda` ; le séquenceur poke `SCSP_SLOT(MUS_SLOT_BASE+chan)` `:135,346,356` sans 68K. D3 a raison de recommander MUS SH-2 ; R5 avait tort, et D2 l'écarte pour la bonne raison (3 ondes, pas de percussions), pas pour le 68K.
13. **D3 (§0 R-1) : « `mem:` < 64 Ko ⇒ B mort »** — `[src] SRUINS.C:2240-2241` imprime `mem_coreleft(0)` **et** `mem_coreleft(1)` : le seuil doit être posé sur l'aire 0 (LWRAM) seule, puisque `UTIL.C:380-386` fait couler HWRAM→LWRAM ; « 64 Ko cumulés » masquerait une aire 1 à zéro.

## 3. Confirmations dont le plan dépend

- Retour **silencieux** si `height*width+nmSlavePolys+50>MAXNMSLAVEPOLYS` `[src] WALLS.C:1374-1375` (D3 R-2) ; `assert(level_nmSectors<=MAXNMSECTORS)` existe bien `[src] SRUINS.C:1982-1983` (D2 ; R3 avait tort).
- Éviction du cache : `!(flags & PICFLAG_LOCKED) && lastUse<oldTime` avec `oldTime=frameCount+1` `[src] PIC.C:264-272` ⇒ une tuile utilisée cette frame est évincible ; `PICFLAG_LOCKED` `:47` protège l'arme (D3).
- `openCDFile` unique, `assert(!openCDFile)` `[src] FILE.C:99,155` ; aucun `fs_seek` (D2). Cinq chargeurs séquentiels `[src] SRUINS.C:1939-1943`.
- `setSectorBrightness(s,level)` écrit `vertexLight`/`vertex.light` `[src] AICOMMON.C:50-70` (D1 #8) ; `getFacingAngle` 0..7 `:72-99`.
- Tints D1 exacts : pal 8 (+101,−88,−77), pal 12 (+37,+43,−9), pal 13 (−17,+19,−10) ; noirs PLAYPAL {0, 247} ; STBAR 0 pixel d'index 0 `[mesuré]`.
- `M_Ticker` est dans `doom_loop_interface` `[src] d_net.c:108-113` ; `vtimer++` à `UsrVblankEnd` `[src] V_BLANK.C:151-153` ; `processInput` `break` au premier pad `:58-78` (D3 #3).
- Thinkers parqués : le commentaire du code exige lui-même le relink avant `P_ArchiveThinkers` `[src] p_tick.c:162-170`.
- `SCL_240LINE` `[src] SRUINS.C:2399,2487` ; `SCL_SetColOffset(OFFSET_A, SP0|NBG0|RBG0)` `:141-142` couvre murs + sprites + HUD comme la PLAYPAL de Doom.
- Duke : `TICSPERFRAME 120/26 = 4` `[src] duke3d.h:100-101` ; 17 `case` dans `operatesectors` ; `cheatquotes` `[src] game.c:5940-5959` ; 10 slots `[src] duke3d.h:303`.

## 4. Manques — ce qu'AUCUNE des trois ne couvre

| Manque | Preuve que Doom/Duke le fait | Coût `[est]` |
|---|---|---|
| Nom de sauvegarde au pad (nom auto = `HU_TITLE`, ou clavier virtuel) | `[src] m_menu.c:1594-1596` | 0,5 j |
| Ancrage vertical (`radius` = 0 ou décalage `chunky`) et `o->scale=65536` par marionnette ; choses au plafond (`MF_SPAWNCEILING`) | `[src] WALLS.C:2698,2718-2724` ; `[src] p_mobj.c` | 0,3 j + test J1 |
| Facteur d'aspect vertical (1,12 en 224 lignes) | `[src] WALLS.H:4` + géométrie du 4:3 | 0,5 j convertisseur, ou 1 constante dans `project_point` |
| Touches sans équivalent pad : automap follow/grid/marks, gamma, sélection directe d'arme (une couche « chord » L+…) | `[src] m_controls.c:111,143-145,170` | 0,5 j |
| Flats de fond des finales et cast dans le pack UI (FLOOR4_8, SFLR6_1, MFLR8_4/3 ; Doom II SLIME16, RROCK07/13/14/17/19) | `[src] f_finale.c:70-83` | 0,1 j |
| Sémantique de re-déclenchement et priorités `S_getChannel` vs `silenceVoice` sans priorité (si backend moteur) | `[src] SOUND.C:322-330, 58-76` | 0 j si backend Mimas |
| Doom II MAP30 (dizaines de `MT_SPAWNSHOT`/monstres télé-fraggés) et crushers/escaliers massifs comme **extrêmes de la synchro** (les coûts sont chiffrés sur E1M1 : 0-5 secteurs mobiles) | `[src] p_enemy.c:1817-1843` braintargets | mesure |
| Duke : SE2 séisme → `earthQuake`, `rotscrnang` → roll caméra, tripbomb → `SPRITEFLAG_LINE`, air meter → `drawAirMeter`, shrink → `Sprite.scale` : quatre effets **gratuits** que le moteur possède et que R4/D1-D3 traitent comme absents ou à supprimer | `[src] SRUINS.C:1831-1838,2106-2109,1427 ; SPRITE.H:38` | 1-2 j |
| Duke : verre/murs cassables et forcefields (changement de `picnum`/`overpicnum` + éclats) | `[src] sector.c:466-495,1134` | 1 j (via `host_cell_tile`) |
| Duke : cheats et démos (mêmes problèmes clavier/déterminisme que Doom) | `[src] game.c:5940, 8145` | 1 j |

## 5. Synthèse recommandée

**Ossature = D1** (le seul qui garde *tout* le 2D de Doom tel quel — automap, menus, intermission, finale — et le backend son de Mimas dont la sémantique est celle de Doom ; cadre 224 ; tics 35 Hz ; les décisions owner sont les bonnes). **Greffes de D2** : le format `.LNK` (tables `vtx_follow`, `side2cells`, `sprite_seq`, `sfx2snd`), les `.PSW` par carte **avec réactivation du multi-fichier `w_wad.c`** (qui répare aussi D1 #4), `pslib`/`mkdisc.py`/`verif_lnk|static|disc`, DEH avant les sous-ensembles, `merge_leaves` avant J3, le plafond `tileBase` (≤ 160 tuiles de géométrie ou psprites dans le `.LEV`), sélection 0x32/0x72 par l'outil. **Greffes de D3** : le registre R-1..R-8 comme kill criteria de chaque jalon, B plancher / C déverrouillage, la **sauvegarde différentielle** 2-4 Ko (mi-niveau sans cartouche — supérieure au checkpoint), `PICFLAG_LOCKED` sur l'arme, `NOSHADOW`/pas de FOOTCLIP, D0 clean-room Duke lancé maintenant sur PC, la question juridique des structs. **Ajouts du juge** (§4) : nom de sauvegarde automatique, `o->scale=65536` + ancrage, facteur d'aspect (décision owner : fidèle ×1,12 ou 1:1 « exact texel »), couche chord pad, flats de finale, démos ON comme test de déterminisme.

**Ordre** : J0 = D1 J0 **élargi** par le J0 de D3 (0,5 j gratuit + 4 j headless) → J1 D1 (30 POSS, avec `scale=65536`, `radius` vertical 0) → J2 « E1M1 se joue » (D1) avec `.LNK`+`.PSW` de D2 → J3 monde mobile (D3 « rangées » + compteur de murs sautés) → J4 2D/arme (D1) → J5 son (D1 backend) → J6 disque (D2 `mkdisc`, `verif_disc`) → J7 sauvegarde différentielle (D3), DEH (D2), split 2p (D1 #35) → Duke D2-D5 après go/no-go de D0-D1.

**Première chose à mesurer** (aujourd'hui, coût nul) : le champ **T** de l'overlay Mimas sur E1M1 et E1M6 en 1p console, et **`mem:`** (`SRUINS.C:2240`, les deux aires séparément) sur le disque doom2ps courant. Ces deux nombres tranchent 30 fps-ou-gouverneurs (R-6) et B-ou-C (R-1) avant qu'une seule ligne de convertisseur de sprites soit écrite.

---
Copie identique : `C:/Users/pcico/AppData/Local/Temp/claude/c--Users-pcico-Projects-Mimas/3f9e95f7-bb08-41a7-9eb9-5ec71ed92341/scratchpad/reports/J2_completude.md` ; script de mesure `j2_wadcheck.py` à côté. Aucun fichier de dépôt modifié.

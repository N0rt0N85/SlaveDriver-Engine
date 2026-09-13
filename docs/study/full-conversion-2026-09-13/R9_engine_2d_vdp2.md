# R9 — La 2D et l'usage VDP2 de SlaveDriver (lecture du 2026-09-14)

Rapport brut d'un lecteur (révision v2 du plan). Périmètre lu : MENU.C, STATBAR.C, PRINT.C, INTRO.C,
BIGMAP.C, MAP.C, WEAPON.C, SEQUENCE.C, PICSET.C, PIC.C, SCL_FUNC.C, SCL_VBLV.C, V_BLANK.C, INITMAIN.C,
MEGAINIT.C, PLAX.C, SPR.C/H, FONT*.H, SRUINS.C (init + boucle), FILE.C, DMA.C ; plus une mesure sur
`cd/STATIC.DAT`. `[src]` partout ; `[mesuré]` = script Python du jour ; `[HW-Mimas]` = cité via
docs/HW_USAGE_VS_MIMAS.md. DIAL.C ne contient que de la donnée d'art (`airMeterDial`).

## 1. Menus

| Sujet | Fait |
|---|---|
| Structure | Table d'items `dlgItem[20]` (MENU.H), 8 types `IT_RECT/TEXT/BUTTON/FONTTEXT/WAVYBUTTON/GAMEBUTTON/FONTSTRING` (MENU.C:24-27) ; constructeurs `dlg_add*` (MENU.C:247-338) ; boucle `dlg_run` (MENU.C:696-790) ; navigation = bouton le plus proche dans la direction du pad (`moveSel`, MENU.C:641-694) ; slide-in/out Hermite (MENU.C:1400-1499) |
| Dessin | **Tout VDP1** : texte = 1 `EZ_normSpr` par caractère ; boutons = `EZ_polygon/polyLine/line` (MENU.C:440-470) ; images = « over-pics » 16 bpp RLE décompressées dans la VRAM VDP1 **à partir de 256 Ko** (MENU.C:73-79, 121-124) tracées par une commande sprite brute `plotOverPicW` (MENU.C:224-241) ; panneau biseauté généré par CPU (`loadOverBase`, MENU.C:129-221) ; `resetPics()` ensuite = cache textures purgé |
| Titre | INTRO.C:395-571 : NBG0 **et** NBG1 bitmap 512×256 8 bpp (A0 et B0, cycle B `0x55ee`, INTRO.C:37-63), image titre dans les deux (INTRO.C:445-446), CRAM banc 0/1 ; 3 boutons `bigFont` ondulants gouraud ; fondu = colour-offset A (INTRO.C:102-109) |
| Options | Remap 8 actions (MENU.C:120-186), stéréo/mono, musique on/off (MENU.C:188-285) ; new/load = 6 slots BUP (MENU.C:287-392). **Pas** de difficulté, épisode, volume, taille écran |
| Pause en jeu | `runInventory` (MENU.C:951-1352) : modal sur fond noir (ciel coupé `enablePlax(0)`, SRUINS.C:2335) — jamais « menu sur la vue 3D » |
| Fin de niveau | **Aucun écran de stats** : sortie → dialogue oui/non `runTravelQuestion` (MENU.C:906-921) → `bup_saveGame` → `runMap` (SRUINS.C:2528-2534) |
| Chargement | `loadLoadingScreen` : 320×240 8 bpp + palette de STATIC.DAT, `POKE_W` en RGB16 **dans le framebuffer VDP1**, erase réduit à 1×1 (SRUINS.C:1099-1121, SPR.C:41-48) ; barre = polygones dans `fs_read` (FILE.C:211-243) |

## 2. HUD en jeu (SRUINS.C, art dans STATBAR.C)

| Élément | Primitive | Détail |
|---|---|---|
| Barre de fond | char 0 `COLOR_4` **320×42** (`stat_bar` en-tête `0,0,1,64 / 42`, STATBAR.C:74), `EZ_normSpr` à (-160,72) → lignes 192-233 (SRUINS.C:1210, 1319) | 13 440 o VRAM VDP1 |
| Bols de vie / armes | pixels pokés **dans le char en VRAM VDP1** (`redrawBowlDots`, SRUINS.C:1149-1175) | seulement quand `nmFullBowls` change |
| Munitions | polygone noir + polygone bleu 87 px + traits si max ≤ 20 (SRUINS.C:1225-1262) | ~1-20 cmds |
| Santé | polygone rouge 87 px, couleur ↑ avec `healthDiff`, « sparkle » LFSR en `EZ_line` (SRUINS.C:1265-1315) | ≤ 128 lignes |
| Boussole | 3 chars 32×20 `COLOR_4`, flips `DIR_LRREV/TBREV` par octant (SRUINS.C:1322-1339) | 1 cmd |
| Air | `TILE16BPP` dans le cache (SRUINS.C:1957-1958) | |
| Message | `drawStringGouro` pulsant, y = -100, 5 s (SRUINS.C:1367-1418) | 1 cmd/car. |
| Clés, visage, armure | **absents** (clés = écran d'inventaire seulement, MENU.C:1010-1018) | |

Mot couleur `0x4000` = type sprite 1 (`SCL_TYPE1`, SRUINS.C:1066) → bits 15-13 = priorité **S2 = 7** ;
things = `light<<8` → banc CRAM 0-5, S0 = 4 (WALLS.C:2838-2840, SRUINS.C:1063-1065). Palette HUD =
banc 0 (« black in ruins pallete » = 96, SRUINS.C:1147).

**VRAM VDP1 en jeu** [src, calculé] : `EZ_initSprSystem(1448,4,1224)` (SRUINS.C:1879) → cmds 92 672
+ CLUT 128 + gouraud 19 584 = chars dès 112 448 ; chars = 13 440 + 1 920 + police 7 104 + tuiles
389 120 (28×8 Ko, 31×4 Ko, 10×2 Ko, 12×1 Ko, SRUINS.C:1902) = **524 032 / 524 288 : 256 o libres**.

## 3. Texte (PRINT.C)

| Police | Hauteur | Glyphes | VRAM VDP1 | Usage |
|---|---|---|---|---|
| `hordeFont` FONT0 | 8 | 89 | — | include commenté (PRINT.C:7) |
| `brianFont` FONT1 | 10 | 98 | 7 104 o | fonts 0 **et** 1 (PRINT.C:17,119-123) ; seule résidente en jeu (`initFonts(4,3)`, masque bit0 effacé, PRINT.C:55) |
| `bigFont` FONT2 | 18 | 59, majuscules seulement (min→maj PRINT.C:86-90) | 8 992 o | menus |

Format : `[h:2][CLUT 16×RGB16][256 largeurs][4 bpp]` ; chars `COLOR_1` + `EZ_setLookupTbl(font)`
(PRINT.C:62-83) → couleur par CLUT, **zéro CRAM** ; teinte par gouraud (`drawStringGouro/Bulge`),
ombre `COMPO_SHADOW` tracée 2× (MENU.C:512-517). Coût = 1 commande VDP1 par caractère. Langue = SMPC
(LOCAL.C:14-19), 10 blocs `LB_*` (LOCAL.H:4-6).

## 4. Arme du joueur

- Données : tuiles + séquences dans STATIC.DAT (`loadWeaponTiles/Sequences`, SRUINS.C:1927-1928),
  chunks `{tile, chunkx, chunky, flags}` (SLEVEL.H:220-226), **non verrouillées** : elles passent
  par le cache LRU commun aux things (PIC.C:530, 261-291).
- Dessin : 1 `EZ_normSpr` **non scalé** par chunk `TILE8BPP` 64×64 `COLOR_4`, banc CRAM 0, S0
  (prio 4) ; `overlay=0x4000` (S2) pour les séquences ≥ 50 = anneau (SEQUENCE.C:250-310) ; clip
  utilisateur 0-320 × 0-210 (SEQUENCE.C:256-262) ; bob = `weaponPos` + `weaponCenter[8]`
  (WEAPON.C:51-60), flamme = `EZ_distSpr` tournée (WEAPON.C:919-957).
- **Grenade (séq. 30-34) et manacle (44-49) sont sur NBG0** : chunks `TILEVDP` → scroll + fenêtre W0
  sur la feuille 512×512 (PIC.C:455-460, 465-490), palette `grenadePal/manaclePal` chargée dans le
  **banc 6** (SEQUENCE.C:268-284, `NMOBJECTPALLETES 5`, UTIL.H:24) ; les autres images VDP2 en banc 0.
- Feuille : 256 Ko lus du CD directement en VRAM VDP2 +0x40000 (SRUINS.C:1084) = **B0+B1 entiers**.
  [mesuré] lignes non vides **0-489**, occupation 10-44 % par bande de 64 lignes, 33 577 px non nuls
  sous la ligne 256 → **B1 n'est pas libre tel quel**.

## 5. Cartes

- **BIGMAP.C = carte du monde** (hub) : 80 tuiles 64×64 16 bpp (`loadMapTiles`, BIGMAP.C:110-127) en
  `EZ_normSpr/scaleSpr` (zoom d'entrée), graphe de niveaux `levelGraph[]` (BIGMAP.C:43-82), œil animé
  30 frames + flèches (BIGMAP.C:283-330), nom du lieu ; fondu = offset A sur SP0 (BIGMAP.C:383-400).
  Contexte VDP1 propre (`EZ_initSprSystem(1248,…)`, 40 slots 16 bpp).
- **MAP.C = automap en jeu** (touche → `mapOn`, SRUINS.C:870, 2203) : 1 `EZ_line` par mur (`mapColor`
  dérivé des flags, MAP.C:46-93), couleur = Δ hauteur de sol, `SECFLAG_SEEN` révélation, zoom,
  rotation, flèche (MAP.C:116-250), **par-dessus la vue 3D**.

## 6. VDP2 en jeu (`setVDP2` SRUINS.C:1051-1077, PLAX.C:84-135)

| Plan | Mode | VRAM | CRAM | Prio | Fenêtre / effets |
|---|---|---|---|---|---|
| RBG0 ciel | bitmap 512×256 8 bpp, K **par dot** 1 280 o A0+0, paramètres A0+0x500 | K A0, char A1 | banc 7 (`R0CAOS 7`) | **non fixée par le jeu** (`SetPriority(SCL_RBG1,7)` vise RBG1, SRUINS.C:1064 ; défaut lib) | W1 = bbox parallax/frame (SRUINS.C:2291-2293), transparence OFF (PLAX.C:108), offset A |
| NBG0 | bitmap 512×512 8 bpp | B0+B1 | banc 0 ou 6 | 6 | W0 = rect de l'image courante sinon (0,0,0,0) ; offset A + **offset B** (power-up, SRUINS.C:2160-2166) + colour-calc (invisibilité, SRUINS.C:2176-2189) |
| NBG1/2/3 | jamais en jeu (NBG1 bitmap au titre/crédits) | | | | |
| Sprites | type 1, MIX, sprite-window ; S0=4, S1-7=7 | | bancs 0-5 | | offset A sur SP0 |

Cycles `{A:eeee, B:44ee/eeee}` → A = bancs de rotation (don't-care), **6/8 slots libres en B0 et
B1**. CRAM mode 1 : 0 objets, 1-4 assombries (PIC.C:637-655), 5 = flash blanc / `jasonPallete`
pendant Ramsès (PIC.C:657-664, AI2.C:247-250), 6 = grenade/manacle, 7 = ciel → **8/8**. Libre : A0 ≈
126,6 Ko mais banc désigné coefficients (K par dot ⇒ doit y rester, [HW-Mimas] via HW_USAGE §8) ;
B0/B1 : 0 o ; CRAM : 0 banc.

## 7. Transitions

| Effet | Mécanisme |
|---|---|
| Fondu niveau/mort | `colorOffset[]→colorCenter[]` à 3/tic → `SCL_SetColOffset(A, SP0\|NBG0\|RBG0)` (SRUINS.C:128-143, 963-965, 1972-1990, 2321) |
| Fondu menus/carte | `fadeDir/fadePos` pokés dans CLOFA 0x180114-118 **à chaque vblank** (V_BLANK.C:99-117 ; INTRO.C:524, BIGMAP.C:161) — second écrivain |
| Dégât / objet / stun | offset (+63,-63,-63) (SRUINS.C:169-171) / vert +32 (203) / bleu +128 (1844) |
| Flash monstre touché | banc CRAM 5 blanc, `light=NMOBJECTPALLETES` (WALLS.C:2711) |
| Écran noir | `displayEnable(0)` = TVMD bit 15 (UTIL.C:551-559) ; `EZ_clearScreen` polygone 0x8000 (SPR.C:503-515) ; erase 0x0000 en jeu (SRUINS.C:1974) vs 0x8000 menus |
| Téléport | lignes VDP1 (INTRO.C:577-646) ; crédits : mosaïque 2 + eau CPU 19 200 `POKE_B`/frame en VRAM VDP2 (INITMAIN.C:327-338, 445) |

## Conclusion

**(A) Reprendre la 2D telle quelle**

| Gratuit (données) | Code (~lignes) | Impossible / cher |
|---|---|---|
| Barre de fond (remplacer `stat_bar[]`, palette = banc 0 = PLAYPAL), bols → arms (bits `WEAPONINV`), barre munitions/santé, message HU, écran de chargement, textes menus (INITLOAD.DAT), polices (blob 4 bpp), écran titre (bitmap NBG0), automap (MAP.C, lignes par mur du .LEV) | Nombres santé/munitions/armure via `drawStringf` (~10) ; clés + visage = 1 char `COLOR_4` chacun, visage 32×29 = 928 o re-uploadé par `EZ_setChar` à chaque changement (~40) — **mais 256 o de VRAM libres** ⇒ rendre 1 slot 8 bpp (4 Ko) ; entrées difficulté/épisode = sous-menus `dlg_*` (~50 chacun) ; curseur crâne = over-pic 2 frames ; intermission = `runMap` sans sélection + stats `drawString` (~150) avec WIMAP en 20 tuiles 64×64 16 bpp (160 Ko VDP1, contexte propre) et anims comme l'œil | Menu **sur la vue 3D** (le modal PowerSlave est sur noir) ; melt/wipe ; palette inverse d'invulnérabilité (pas un offset : réécrire les bancs 0-4, 5×512 o, faisable) ; visage/carte animée **ne sont pas impossibles**, ils coûtent un slot et du code, pas une architecture |

**(B) Feuille bitmap 8 bpp 320×224 pour la 2D Doom inchangée**

| Question | Réponse |
|---|---|
| Plan | **NBG1** (seul plan bitmap libre ; NBG2/3 cell-only) ; la config existe déjà dans l'arbre : NBG1 bitmap 512×256 8 bpp à 0x40000, cycle `0x55ee` (INTRO.C:37-63, INITMAIN.C:56-81) |
| VRAM | un bitmap 512×256 = **128 Ko = un banc entier** ; A0 exclu (rotation) ; **bloquant tel quel** : B0+B1 tenus par la feuille NBG0 (mesuré : lignes 0-489). Deux sorties : (i) supprimer la feuille — Doom n'a pas d'arme VDP2, ses armes = chunks VDP1 comme les 6 autres armes PowerSlave → B0+B1 libres, NBG1 en B1 ; (ii) la re-packer en 512×256 (73 800 px non nuls pour 131 072 cellules, à vérifier rectangle par rectangle) |
| CRAM | 8/8 ; si la 2D Doom est en PLAYPAL = banc 0 (déjà celui des things), `N1CAOS=0` coûte **0 banc** ; sinon banc 6, libéré avec la feuille |
| Conflit arme NBG0 | aucun si (i) ; si la feuille reste (ii) : plans distincts, W0 reste à NBG0, cycles B0 = 4 slots sur 8 |
| Priorité | NBG1 = **5** : au-dessus du 3D (S0=4), sous S2=7 ; index 0 transparent ⇒ remapper le noir Doom 0→247 dans les blits 2D, sinon le 3D perce le STBAR |
| Coût transfert | pas de chemin DMA→VDP2 dans le moteur (`dmaMemCpy` = DMAC SH-2 borné à HWRAM→VDP1, DMA.C:83-101) ; référence [HW-Mimas] 64 Ko en 5,5-11 ms ⇒ HUD 32 lignes (10 Ko) **≈ 0,9-1,7 ms/frame**, écran complet 70 Ko ≈ 6-12 ms **hors jeu seulement** (menus/intermission) ; à flusher seulement quand le HUD change (dirty) ; stride 512 ⇒ 224 copies de 320 o ; à placer dans le spin `while (vtimer<smoothVTime)` après `SPR_WaitDrawEnd` (SRUINS.C:2237-2249) |
| Fondus | ajouter `SCL_NBG1` aux deux écrivains d'offset (SRUINS.C:141, V_BLANK.C:114-116) ou l'exclure volontairement |

**Ce qui bloque vraiment** : uniquement la feuille NBG0 (VRAM B) et, si l'on garde des images VDP2,
la CRAM. Rien côté plans, fenêtres, cycles ou priorité.

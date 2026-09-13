# Calibration Build (.MAP DOS) → Dex/.LEV (Saturn) — PowerSlave (2026-08-31)

> **2026-09-10** : ce document calibre PowerSlave DOS contre PowerSlave Saturn et conclut négativement
> (§0). La cible du convertisseur est désormais **Duke Nukem 3D PC → Saturn** (décision owner), dont
> les niveaux de l'épisode 1 sont des conversions mesurées (`RETAIL_DISCS.md` §6 — la règle R1
> x/8, −y/8 y est confirmée à 7,99-8,00). Restent valables ici : la structure génération 1 (moteur du
> fork, §5), les règles R1-R14 (§7) et l'outillage `tools/calibrate.py`.

Outil reproductible : `tools/calibrate.py` (imprime toutes les tables ci-dessous ; `--scan` refait le
balayage 24×33, `--names` rend les tuiles de noms DOS, `--fill-doc` réinsère les tables imprimées dans ce document (blocs `<!-- @X -->`), `--out DIR` protège les sorties d'un `make` qui
efface `build/`). Entrées : `tools/lev.py` → `build/tmp-b2d/sat/*.json`, `tools/dos_stats.py` →
`build/tmp-b2d/dos/LEV*.MAP`. Formats : `docs/RETAIL_DISCS.md` §4 (.LEV), `docs/LEVEL_PIPELINE.md` §2 (Dex).
`[src]` = fichier:ligne de ce dépôt ; `[ex]` = `refs/build/NBlood/source/exhumed/src/` ; `[jf]` =
`refs/build/jfbuild/src/`.

## 0. Résultat en une phrase

**Aucun `.LEV` Saturn n'est une conversion d'un `.MAP` DOS.** Les deux jeux partagent les monstres, l'art
et deux noms (Karnak, Set/Selkis/Kilmaat comme thèmes), mais les 24 niveaux Saturn ont été construits
dans Brew (`docs/LEVEL_PIPELINE.md` §1) avec une géométrie propre : le meilleur appariement géométrique
sur 24 × 33 × 8 orientations × 4 échelles ne retrouve **aucun mur** Build dans un niveau Saturn
(≤ 3,8 % de segments — bruit de grille — contre 0 % pour le témoin décalé, loin du seuil d'appariement de 25 %, §3), alors qu'un niveau contre lui-même donne 100 %.
Ce que la calibration établit quand même, et qui est *nécessaire* à un convertisseur Build→Dex :
l'échelle naturelle (1/16 en XY, 1/256 en hauteur, §4), le repère (§4), la structure exacte d'un secteur
Saturn (polyèdre convexe à faces quads, sol/plafond = rectangles englobants, empilement de murs par arête,
superposition verticale de secteurs, §5), la relation tuiles/picnums (§6), les règles à implémenter (§7) et
la liste de ce que le Saturn ajoute sans équivalent DOS (§8).

## 1. Noms et appariement par nom

**Saturn** : `levelGraph[31]` BIGMAP.C:43-81 (index → fichier ; QUARRY et KILMAAT1-6 absents du disque,
RETAIL_DISCS §7) ; le nom affiché est `getText(LB_LEVELNAMES, i)` (BIGMAP.C:355-359, LOCAL.C:35-38,
`LB_LEVELNAMES = 2` LOCAL.H:4), chargé depuis `INITLOAD.DAT` (INITMAIN.C:600 `loadLocalText(fd)`,
LOCAL.C:23-33). Les 22 chaînes extraites d'`INITLOAD.DAT` :

<!-- @SAT_NAMES -->
**Noms Saturn (INITLOAD.DAT, LB_LEVELNAMES ; index = levelGraph BIGMAP.C:43-81)**

| idx | fichier | nom affiche | sect | murs |
|---|---|---|---|---|
| 0 | KARNAK | Karnak | 425 | 3184 |
| 1 | SANCTUAR | Karnak Sanctuary | 307 | 2438 |
| 2 | PASS | Sobek Pass | 337 | 2845 |
| 3 | TOMB | Ramses Tomb | 197 | 1555 |
| 4 | SHRINE | Sobek Mountain Shrine | 411 | 3404 |
| 5 | MINES | Amun Mines | 566 | 4147 |
| 6 | SETPALAC | Set Palace | 242 | 1902 |
| 7 | SETARENA | Set Arena | 206 | 1632 |
| 8 | CAVERN | Cavern of Peril | 415 | 3619 |
| 9 | THOTH | Thoth Treasure Reliquary | 488 | 3764 |
| 10 | CHAOS | Canyons of Chaos | 294 | 2094 |
| 11 | COLONY | Kilmaat Colony | 432 | 3378 |
| 12 | SELPATH | Selkis Path | 341 | 2534 |
| 13 | KILENTRY | Kilmaat Haunt | 145 | 1171 |
| 14 | QUARRY | Forgotten Quarry | absent du disque | - |
| 15 | SELBUROW | Selkis Burrow | 278 | 2240 |
| 16 | MAGMA | Magma Fields | 424 | 3171 |
| 17 | PEAK | Horus Peak | 375 | 3271 |
| 18 | MARSH | Heket Marsh | 340 | 2524 |
| 19 | SUNKEN | Sunken Palace | 570 | 4939 |
| 20 | SLAVCAMP | Deserted Slave Camp | 413 | 3395 |
| 21 | GORGE | Nile Gorge | 358 | 2929 |
| 22 | TEST | - | 307 | 2438 |
| 23 | KILMAAT1 | - | absent du disque | - |
| 24 | KILMAAT2 | - | absent du disque | - |
| 25 | KILMAAT3 | - | absent du disque | - |
| 26 | KILMAAT4 | - | absent du disque | - |
| 27 | KILMAAT5 | - | absent du disque | - |
| 28 | KILMAAT6 | - | absent du disque | - |
| 29 | KILARENA | - | 376 | 2917 |
| 30 | TOMBEND | - | 161 | 1390 |
<!-- /@SAT_NAMES -->

**DOS** : `LEV<n>.MAP` chargé par `lev%d.map` ([ex] init.cpp:151) ; LEV0 = Training, LEV1-20 = solo
(`kMap20`), LEV21-32 = deathmatch (rapport du parseur). Les noms sont des **tuiles** (`mapNamePlaques[]`
[ex] menu.cpp:582-604, entrée *i* = niveau *i+1*, champ `text.nTile` = 3411, 3414, …). `tools/calibrate.py
--names` les décode depuis `TILES026.ART`/`PALETTE.DAT` de `STUFF.DAT` (format ART : [jf] engine.c
`loadpics`) en `build/tmp-b2d/names/names_strip.png` ; transcription à la main :

<!-- @DOS_NAMES -->
**Noms DOS (tuiles mapNamePlaques menu.cpp:582-604, transcrits depuis build/tmp-b2d/names/names_strip.png)**

| MAP | nom | sect | murs | sprites |
|---|---|---|---|---|
| LEV0 | Training (exhumed.cpp:2754-2757) | 128 | 865 | 139 |
| LEV1 | Abu Simbel | 416 | 3048 | 515 |
| LEV2 | Dendur | 365 | 2382 | 505 |
| LEV3 | Kalabsh | 361 | 3129 | 439 |
| LEV4 | El Subua | 440 | 3159 | 582 |
| LEV5 | El Derr | 790 | 5205 | 967 |
| LEV6 | Abu Ghurab | 573 | 4349 | 696 |
| LEV7 | Philae | 791 | 5189 | 1741 |
| LEV8 | El Kab | 454 | 3181 | 422 |
| LEV9 | Aswan | 514 | 4324 | 752 |
| LEV10 | (hieroglyphes) | 79 | 656 | 237 |
| LEV11 | Qubbet el Hawa | 601 | 4064 | 529 |
| LEV12 | Abydos | 492 | 3659 | 583 |
| LEV13 | Edufu | 471 | 3403 | 708 |
| LEV14 | West Bank | 547 | 4231 | 665 |
| LEV15 | Luxor | 647 | 4046 | 653 |
| LEV16 | Karnak | 557 | 4306 | 990 |
| LEV17 | Saqqara | 601 | 4183 | 724 |
| LEV18 | Mitrrahn | 573 | 5166 | 948 |
| LEV19 | (hieroglyphes) | 114 | 822 | 103 |
| LEV20 | (hieroglyphes) | 715 | 6226 | 1031 |
| LEV21 | (deathmatch, sans nom) | 92 | 747 | 229 |
| LEV22 | (deathmatch, sans nom) | 85 | 729 | 94 |
| LEV23 | (deathmatch, sans nom) | 99 | 636 | 203 |
| LEV24 | (deathmatch, sans nom) | 109 | 943 | 206 |
| LEV25 | (deathmatch, sans nom) | 136 | 1037 | 213 |
| LEV26 | (deathmatch, sans nom) | 100 | 716 | 144 |
| LEV27 | (deathmatch, sans nom) | 168 | 1122 | 227 |
| LEV28 | (deathmatch, sans nom) | 114 | 1052 | 216 |
| LEV29 | (deathmatch, sans nom) | 110 | 763 | 150 |
| LEV30 | (deathmatch, sans nom) | 97 | 724 | 136 |
| LEV31 | (deathmatch, sans nom) | 76 | 736 | 141 |
| LEV32 | (deathmatch, sans nom) | 176 | 1097 | 239 |
<!-- /@DOS_NAMES -->

**Un seul nom commun : Karnak** (DOS LEV16 ↔ Saturn KARNAK). Les autres noms DOS sont des sites
égyptiens réels (Abu Simbel, Dendur, Philae, Abydos, Saqqara…), les noms Saturn sont thématiques
(Sobek Pass, Ramses Tomb, Kilmaat Colony…) ; les commentaires `DENDUR (level 2)` / `Kalabash`
([ex] menu.cpp:584-585) confirment la lecture des tuiles. Aucun appariement par nom n'est donc possible
au-delà de KARNAK↔LEV16, et §3 montre que même celui-là est purement nominal.

## 2. Méthode du balayage géométrique

Pour chaque couple (LEV Saturn, MAP DOS) : points Saturn = sommets `(x, z)` dédoublonnés (y = hauteur,
SLEVEL.H:115-118 ; CONVERT.C:1362-1364 `out.z = y_dex`) ; points Build = sommets de murs `(x, y)`
dédoublonnés. Transformation testée : `(X, Z) = s · M · (x, y) + o`, M ∈ 8 isométries (4 signes × échange
des axes), s ∈ {1/4, 1/8, 1/16, 1/32} (`--quick`, justifié par §4 ; l'ensemble complet {2 … 1/64} a été
testé sur KARNAK↔LEV16 sans changer le résultat), o = mode des différences point à point quantifiées à
8 unités (160 points Build × 3000 points Saturn ; 120 en `--quick`), affiné par la médiane de la cellule gagnante. Score de
balayage `f4` = fraction de points Build à ≤ ~4 unités d'un point Saturn (cellules 4×4, voisinage 3×3).
Sur le meilleur candidat de chaque LEV : moindres carrés (échelle + translation, 3 itérations,
appariements ≤ 16 u), puis scores **exacts** : points Build→Saturn et Saturn→Build à 1/4/16 u,
**segments** (murs Build dont les deux extrémités tombent sur deux sommets Saturn reliés par un mur
vertical Saturn), **murs Saturn portés par une droite de mur Build** (colinéarité + inclusion, détecte un
re-découpage), et un **témoin** = même transformation décalée de (37, 53) u = niveau de coïncidence
fortuite. Contrôles : KARNAK contre lui-même `f4 = 1,000` ; TOMB contre KARNAK (deux niveaux Saturn sans
lien, échelle 1) donne le bruit de fond.

## 3. Résultat du balayage

<!-- @SCAN -->
**Balayage geometrique : meilleur .MAP par .LEV (f4 = fraction approchee de points Build a <= 4 u d'un sommet Saturn)**

| LEV Saturn | meilleur MAP | f4 | echelle | orientation | 2e MAP | f4 (2e) | mediane f4 (33) | verdict |
|---|---|---|---|---|---|---|---|---|
| CAVERN | LEV29 | 0.558 | 1/16 | X=-x,Z=+y | LEV28 | 0.495 | 0.148 | APPARIE |
| CHAOS | LEV29 | 0.543 | 1/16 | X=-x,Z=-y | LEV28 | 0.512 | 0.141 | APPARIE |
| COLONY | LEV28 | 0.498 | 1/16 | X=+x,Z=-y | LEV29 | 0.494 | 0.155 | aucun appariement (bruit) |
| GORGE | LEV29 | 0.442 | 1/16 | X=+x,Z=+y | LEV27 | 0.405 | 0.119 | aucun appariement (bruit) |
| KARNAK | LEV29 | 0.478 | 1/16 | X=-x,Z=-y | LEV32 | 0.422 | 0.133 | aucun appariement (bruit) |
| KILARENA | LEV29 | 0.353 | 1/16 | X=+y,Z=+x | LEV28 | 0.339 | 0.094 | aucun appariement (bruit) |
| KILENTRY | LEV29 | 0.369 | 1/16 | X=+y,Z=+x | LEV28 | 0.334 | 0.111 | aucun appariement (bruit) |
| MAGMA | LEV29 | 0.504 | 1/16 | X=-y,Z=-x | LEV32 | 0.464 | 0.157 | APPARIE |
| MARSH | LEV28 | 0.441 | 1/8 | X=+x,Z=+y | LEV29 | 0.436 | 0.129 | aucun appariement (bruit) |
| MINES | LEV29 | 0.447 | 1/16 | X=-y,Z=-x | LEV32 | 0.422 | 0.147 | aucun appariement (bruit) |
| PASS | LEV29 | 0.465 | 1/16 | X=+x,Z=+y | LEV28 | 0.418 | 0.130 | aucun appariement (bruit) |
| PEAK | LEV29 | 0.569 | 1/16 | X=+x,Z=+y | LEV28 | 0.555 | 0.181 | APPARIE |
| SANCTUAR | LEV29 | 0.429 | 1/16 | X=-y,Z=+x | LEV28 | 0.398 | 0.118 | aucun appariement (bruit) |
| SELBUROW | LEV28 | 0.449 | 1/16 | X=-x,Z=+y | LEV29 | 0.439 | 0.128 | aucun appariement (bruit) |
| SELPATH | LEV29 | 0.470 | 1/16 | X=+y,Z=+x | LEV28 | 0.438 | 0.138 | aucun appariement (bruit) |
| SETARENA | LEV32 | 0.300 | 1/8 | X=+y,Z=-x | LEV29 | 0.286 | 0.082 | aucun appariement (bruit) |
| SETPALAC | LEV29 | 0.410 | 1/16 | X=-y,Z=+x | LEV28 | 0.406 | 0.130 | aucun appariement (bruit) |
| SHRINE | LEV29 | 0.486 | 1/16 | X=+y,Z=-x | LEV32 | 0.454 | 0.144 | aucun appariement (bruit) |
| SLAVCAMP | LEV29 | 0.506 | 1/16 | X=+x,Z=-y | LEV28 | 0.451 | 0.143 | APPARIE |
| SUNKEN | LEV28 | 0.546 | 1/16 | X=-x,Z=-y | LEV29 | 0.545 | 0.158 | APPARIE |
| THOTH | LEV29 | 0.517 | 1/16 | X=+y,Z=+x | LEV28 | 0.513 | 0.159 | APPARIE |
| TOMB | LEV28 | 0.306 | 1/16 | X=+x,Z=+y | LEV29 | 0.304 | 0.086 | aucun appariement (bruit) |
| TOMBEND | LEV29 | 0.281 | 1/16 | X=+y,Z=-x | LEV28 | 0.236 | 0.073 | aucun appariement (bruit) |
<!-- /@SCAN -->

<!-- @REFINE -->
**Affinage exact (moindres carres echelle+translation, tolerance 1/4/16 u ; segments = murs Build dont les 2 bouts sont des sommets Saturn d'un meme mur ; droite = murs Saturn portes par une droite de mur Build)**

| LEV | MAP | orientation | echelle | offset | B->S 1 | B->S 4 | B->S 16 | S->B 4 | segments | murs S sur droite B | temoin decale : B->S 4 / segments |
|---|---|---|---|---|---|---|---|---|---|---|---|
| CAVERN | LEV29 | X=-x,Z=+y | 1/16.01 | (-127, -512) | 0.286 | 0.553 | 0.623 | 0.060 | 0.012 (517) | 0.043 (1350) | 0.008 / 0.000 |
| CAVERN | LEV28 | X=+x,Z=+y | 1/16.00 | (-512, -0) | 0.484 | 0.491 | 0.552 | 0.051 | 0.005 (793) | 0.053 (1350) | 0.003 / 0.000 |
| CHAOS | LEV29 | X=-x,Z=-y | 1/16.00 | (-1088, 1727) | 0.530 | 0.538 | 0.600 | 0.068 | 0.014 (517) | 0.044 (842) | 0.000 / 0.000 |
| CHAOS | LEV28 | X=+x,Z=-y | 1/16.00 | (-898, 1537) | 0.214 | 0.507 | 0.583 | 0.062 | 0.010 (793) | 0.048 (842) | 0.006 / 0.000 |
| COLONY | LEV28 | X=+x,Z=-y | 1/15.99 | (-704, 1280) | 0.321 | 0.488 | 0.599 | 0.041 | 0.005 (793) | 0.043 (1168) | 0.006 / 0.000 |
| COLONY | LEV29 | X=+x,Z=+y | 1/16.00 | (-256, 128) | 0.465 | 0.486 | 0.574 | 0.037 | 0.004 (517) | 0.022 (1168) | 0.008 / 0.000 |
| GORGE | LEV29 | X=+x,Z=+y | 1/16.00 | (-192, 640) | 0.423 | 0.442 | 0.470 | 0.073 | 0.031 (517) | 0.069 (987) | 0.003 / 0.000 |
| GORGE | LEV27 | X=+y,Z=+x | 1/15.99 | (192, 1985) | 0.178 | 0.400 | 0.473 | 0.091 | 0.014 (711) | 0.059 (987) | 0.002 / 0.000 |
| KARNAK | LEV29 | X=-x,Z=-y | 1/16.00 | (1984, 1728) | 0.465 | 0.473 | 0.519 | 0.039 | 0.023 (517) | 0.049 (1099) | 0.008 / 0.000 |
| KARNAK | LEV32 | X=-x,Z=-y | 1/8.00 | (6272, 3712) | 0.421 | 0.422 | 0.435 | 0.054 | 0.004 (769) | 0.090 (1099) | 0.005 / 0.000 |
| KILARENA | LEV29 | X=+y,Z=+x | 1/16.00 | (-257, -4672) | 0.340 | 0.343 | 0.418 | 0.055 | 0.012 (517) | 0.047 (641) | 0.003 / 0.000 |
| KILARENA | LEV28 | X=-x,Z=+y | 1/16.00 | (449, -5504) | 0.254 | 0.334 | 0.368 | 0.054 | 0.004 (793) | 0.017 (641) | 0.005 / 0.000 |
| KILENTRY | LEV29 | X=+y,Z=+x | 1/15.99 | (-640, 769) | 0.343 | 0.364 | 0.395 | 0.090 | 0.004 (517) | 0.075 (415) | 0.000 / 0.000 |
| KILENTRY | LEV28 | X=-y,Z=-x | 1/16.00 | (-63, 897) | 0.312 | 0.324 | 0.373 | 0.085 | 0.006 (793) | 0.104 (415) | 0.000 / 0.000 |
| MAGMA | LEV29 | X=-y,Z=-x | 1/16.00 | (-1408, 896) | 0.478 | 0.496 | 0.556 | 0.035 | 0.019 (517) | 0.032 (1164) | 0.008 / 0.000 |
| MAGMA | LEV32 | X=-x,Z=+y | 1/8.00 | (4160, -1664) | 0.455 | 0.461 | 0.494 | 0.046 | 0.010 (769) | 0.092 (1164) | 0.000 / 0.000 |
| MARSH | LEV28 | X=+x,Z=+y | 1/8.00 | (-193, -257) | 0.014 | 0.441 | 0.452 | 0.043 | 0.010 (793) | 0.080 (893) | 0.003 / 0.000 |
| MARSH | LEV29 | X=-x,Z=+y | 1/15.99 | (-1024, -192) | 0.384 | 0.434 | 0.465 | 0.040 | 0.027 (517) | 0.060 (893) | 0.003 / 0.000 |
| MINES | LEV29 | X=-y,Z=-x | 1/16.01 | (768, 1153) | 0.281 | 0.426 | 0.512 | 0.033 | 0.012 (517) | 0.046 (1251) | 0.026 / 0.000 |
| MINES | LEV32 | X=+x,Z=-y | 1/8.00 | (-4544, 3648) | 0.389 | 0.408 | 0.468 | 0.047 | 0.008 (769) | 0.075 (1251) | 0.010 / 0.000 |
| PASS | LEV29 | X=+x,Z=+y | 1/16.00 | (320, 703) | 0.462 | 0.462 | 0.512 | 0.059 | 0.015 (517) | 0.037 (1121) | 0.003 / 0.000 |
| PASS | LEV28 | X=-x,Z=+y | 1/16.01 | (127, 576) | 0.176 | 0.417 | 0.465 | 0.051 | 0.006 (793) | 0.035 (1121) | 0.000 / 0.000 |
| PEAK | LEV29 | X=+x,Z=+y | 1/16.00 | (639, -512) | 0.566 | 0.569 | 0.608 | 0.052 | 0.006 (517) | 0.051 (1048) | 0.005 / 0.000 |
| PEAK | LEV28 | X=+x,Z=+y | 1/15.98 | (126, -577) | 0.162 | 0.546 | 0.647 | 0.054 | 0.006 (793) | 0.055 (1048) | 0.009 / 0.000 |
| SANCTUAR | LEV29 | X=-y,Z=+x | 1/16.01 | (64, 1024) | 0.418 | 0.429 | 0.462 | 0.060 | 0.014 (517) | 0.036 (955) | 0.000 / 0.000 |
| SANCTUAR | LEV28 | X=+y,Z=+x | 1/16.00 | (-1472, 1151) | 0.385 | 0.396 | 0.434 | 0.053 | 0.009 (793) | 0.025 (955) | 0.000 / 0.000 |
| SELBUROW | LEV28 | X=-x,Z=+y | 1/16.00 | (-766, -704) | 0.123 | 0.449 | 0.499 | 0.052 | 0.015 (793) | 0.092 (862) | 0.003 / 0.000 |
| SELBUROW | LEV29 | X=+x,Z=-y | 1/15.98 | (-1023, 449) | 0.187 | 0.439 | 0.499 | 0.052 | 0.012 (517) | 0.071 (862) | 0.003 / 0.000 |
| SELPATH | LEV29 | X=+y,Z=+x | 1/16.01 | (-1152, 576) | 0.408 | 0.455 | 0.501 | 0.046 | 0.015 (517) | 0.037 (970) | 0.003 / 0.000 |
| SELPATH | LEV28 | X=+y,Z=-x | 1/16.01 | (-1088, 449) | 0.398 | 0.427 | 0.498 | 0.042 | 0.009 (793) | 0.037 (970) | 0.003 / 0.000 |
| SETARENA | LEV32 | X=+y,Z=-x | 1/8.00 | (-2560, 6079) | 0.297 | 0.297 | 0.318 | 0.068 | 0.010 (769) | 0.088 (513) | 0.002 / 0.000 |
| SETARENA | LEV29 | X=-y,Z=-x | 1/16.00 | (1281, 2048) | 0.278 | 0.281 | 0.314 | 0.045 | 0.006 (517) | 0.012 (513) | 0.008 / 0.000 |
| SETPALAC | LEV29 | X=-y,Z=+x | 1/16.00 | (2688, 576) | 0.358 | 0.382 | 0.483 | 0.040 | 0.012 (517) | 0.044 (721) | 0.000 / 0.000 |
| SETPALAC | LEV28 | X=-x,Z=-y | 1/16.00 | (2241, 1408) | 0.223 | 0.371 | 0.502 | 0.039 | 0.009 (793) | 0.058 (721) | 0.000 / 0.000 |
| SHRINE | LEV29 | X=+y,Z=-x | 1/16.00 | (2751, 3777) | 0.210 | 0.483 | 0.545 | 0.049 | 0.008 (517) | 0.031 (1280) | 0.000 / 0.000 |
| SHRINE | LEV32 | X=+x,Z=+y | 1/8.00 | (-1408, 1536) | 0.454 | 0.454 | 0.462 | 0.068 | 0.005 (769) | 0.075 (1280) | 0.000 / 0.000 |
| SLAVCAMP | LEV29 | X=+x,Z=-y | 1/16.01 | (-64, 1344) | 0.426 | 0.496 | 0.543 | 0.054 | 0.002 (517) | 0.042 (1178) | 0.000 / 0.000 |
| SLAVCAMP | LEV28 | X=+x,Z=+y | 1/16.00 | (-384, 192) | 0.438 | 0.446 | 0.488 | 0.050 | 0.035 (793) | 0.061 (1178) | 0.002 / 0.000 |
| SUNKEN | LEV28 | X=-x,Z=-y | 1/16.00 | (65, 960) | 0.228 | 0.546 | 0.608 | 0.065 | 0.026 (793) | 0.090 (1430) | 0.000 / 0.000 |
| SUNKEN | LEV29 | X=+x,Z=+y | 1/16.03 | (-1, -127) | 0.190 | 0.545 | 0.595 | 0.062 | 0.023 (517) | 0.061 (1430) | 0.031 / 0.000 |
| THOTH | LEV29 | X=+y,Z=+x | 1/16.00 | (-832, 256) | 0.486 | 0.512 | 0.571 | 0.051 | 0.006 (517) | 0.065 (1416) | 0.003 / 0.000 |
| THOTH | LEV28 | X=+y,Z=-x | 1/16.00 | (-513, 641) | 0.206 | 0.499 | 0.577 | 0.052 | 0.038 (793) | 0.081 (1416) | 0.003 / 0.000 |
| TOMB | LEV28 | X=+x,Z=+y | 1/16.00 | (-2752, -4736) | 0.290 | 0.301 | 0.315 | 0.031 | 0.006 (793) | 0.032 (600) | 0.003 / 0.000 |
| TOMB | LEV29 | X=-x,Z=-y | 1/16.01 | (-2623, -3969) | 0.132 | 0.294 | 0.377 | 0.030 | 0.000 (517) | 0.013 (600) | 0.005 / 0.000 |
| TOMBEND | LEV29 | X=+y,Z=-x | 1/16.00 | (-2560, 5184) | 0.273 | 0.281 | 0.306 | 0.060 | 0.002 (517) | 0.030 (466) | 0.003 / 0.000 |
| TOMBEND | LEV28 | X=+y,Z=-x | 1/15.99 | (-2432, 6209) | 0.206 | 0.232 | 0.245 | 0.051 | 0.004 (793) | 0.030 (466) | 0.000 / 0.000 |
| KARNAK | LEV16 | X=-x,Z=-y | 1/8.00 | (5184, -3648) | 0.072 | 0.073 | 0.081 | 0.039 | 0.001 (2970) | 0.052 (1099) | 0.002 / 0.000 |
<!-- /@REFINE -->

<!-- @CONTROLS -->
Temoin : TOMB (comme "Build") contre KARNAK a l'echelle 1 : f4 = 0.149 (X=-x,Z=-y) ; deux niveaux Saturn sans lien donnent ce niveau de bruit.
Temoin : KARNAK contre lui-meme : f4 = 1.000
<!-- /@CONTROLS -->

<!-- @VERDICT -->
**Verdict final par LEV (segments Build retrouves a 4 u sur le meilleur candidat ; APPARIE si > 0.25 et > 5x le temoin)**

| LEV | MAP | segments | temoin | verdict |
|---|---|---|---|---|
| CAVERN | LEV29 | 0.012 | 0.000 | AUCUN |
| CHAOS | LEV29 | 0.014 | 0.000 | AUCUN |
| COLONY | LEV28 | 0.005 | 0.000 | AUCUN |
| GORGE | LEV29 | 0.031 | 0.000 | AUCUN |
| KARNAK | LEV29 | 0.023 | 0.000 | AUCUN |
| KILARENA | LEV29 | 0.012 | 0.000 | AUCUN |
| KILENTRY | LEV29 | 0.004 | 0.000 | AUCUN |
| MAGMA | LEV29 | 0.019 | 0.000 | AUCUN |
| MARSH | LEV28 | 0.010 | 0.000 | AUCUN |
| MINES | LEV29 | 0.012 | 0.000 | AUCUN |
| PASS | LEV29 | 0.015 | 0.000 | AUCUN |
| PEAK | LEV29 | 0.006 | 0.000 | AUCUN |
| SANCTUAR | LEV29 | 0.014 | 0.000 | AUCUN |
| SELBUROW | LEV28 | 0.015 | 0.000 | AUCUN |
| SELPATH | LEV29 | 0.015 | 0.000 | AUCUN |
| SETARENA | LEV32 | 0.010 | 0.000 | AUCUN |
| SETPALAC | LEV29 | 0.012 | 0.000 | AUCUN |
| SHRINE | LEV29 | 0.008 | 0.000 | AUCUN |
| SLAVCAMP | LEV29 | 0.002 | 0.000 | AUCUN |
| SUNKEN | LEV28 | 0.026 | 0.000 | AUCUN |
| THOTH | LEV29 | 0.006 | 0.000 | AUCUN |
| TOMB | LEV28 | 0.006 | 0.000 | AUCUN |
| TOMBEND | LEV29 | 0.002 | 0.000 | AUCUN |
<!-- /@VERDICT -->

Lecture : les `f4` de 0,28-0,57 (médianes 0,07-0,18) sont un artefact de grille (à 1/16, les sommets Build multiples de 64
tombent sur des multiples de 4 et 45-81 % des sommets Saturn sont sur une grille de 64 ; les petites cartes
deathmatch LEV27-29 « gagnent » partout parce qu'elles ont peu de points à placer dans un nuage Saturn
dense). Les critères exacts tranchent : **au plus 3,8 % de segments Build retrouvés** et **au plus 10 % de murs Saturn sur une droite Build** sur les meilleurs candidats — coïncidences de la grille commune de 64 (le témoin décalé hors grille tombe à 0), très loin d'un vrai appariement (100 % en auto-test). Le couple nominal KARNAK↔LEV16 est à 7,3 % de points à 4 u (contre 100 % pour l'auto-appariement) et 4 segments sur 2970 (0,13 %). Conclusion : **pas de
transformation** DOS→Saturn à estimer sur les données ; les lois ci-dessous viennent de la source et des
grilles de conception.

## 4. Échelles et repère (loi des hauteurs)

<!-- @HEIGHTS -->
**Loi des hauteurs et grilles**

| niveau | grille XZ (x%64) | y%16 | y%64 | hauteur piece mediane (u) | y vers le haut | floorLevel = moy. sol |
|---|---|---|---|---|---|---|
| CAVERN | 0.52 | 0.98 | 0.72 | 224 | True | 413/415 |
| CHAOS | 0.67 | 0.98 | 0.75 | 256 | True | 294/294 |
| COLONY | 0.53 | 0.93 | 0.63 | 192 | True | 432/432 |
| GORGE | 0.81 | 0.84 | 0.77 | 256 | True | 350/358 |
| KARNAK | 0.79 | 0.90 | 0.80 | 192 | True | 424/425 |
| KILARENA | 0.54 | 0.94 | 0.51 | 433 | True | 374/376 |
| KILENTRY | 0.63 | 0.92 | 0.73 | 248 | True | 144/145 |
| MAGMA | 0.68 | 0.91 | 0.72 | 448 | True | 420/424 |
| MARSH | 0.77 | 0.92 | 0.70 | 256 | True | 340/340 |
| MINES | 0.65 | 0.81 | 0.59 | 213 | True | 562/566 |
| PASS | 0.51 | 0.99 | 0.86 | 512 | True | 336/337 |
| PEAK | 0.50 | 0.97 | 0.77 | 480 | True | 375/375 |
| SANCTUAR | 0.60 | 0.98 | 0.70 | 208 | True | 306/307 |
| SELBUROW | 0.45 | 0.96 | 0.37 | 160 | True | 276/278 |
| SELPATH | 0.60 | 0.96 | 0.92 | 256 | True | 337/341 |
| SETARENA | 0.70 | 0.91 | 0.77 | 288 | True | 206/206 |
| SETPALAC | 0.71 | 0.85 | 0.68 | 318 | True | 241/242 |
| SHRINE | 0.60 | 0.98 | 0.69 | 224 | True | 407/411 |
| SLAVCAMP | 0.62 | 0.96 | 0.77 | 192 | True | 406/413 |
| SUNKEN | 0.53 | 0.98 | 0.74 | 256 | True | 570/570 |
| THOTH | 0.50 | 0.93 | 0.59 | 208 | True | 486/488 |
| TOMB | 0.66 | 0.88 | 0.68 | 256 | True | 195/197 |
| TOMBEND | 0.47 | 1.00 | 0.70 | 288 | True | 158/161 |

| MAP | floorz%1024 | ceilz%1024 | hauteur piece mediane (z) | /256 | texel xy median | p10 | p90 | x%64 | x%1024 |
|---|---|---|---|---|---|---|---|---|---|
| LEV0 | 1.00 | 1.00 | 44032 | 172 | 16.0 | 16.0 | 17.9 | 0.78 | 0.49 |
| LEV1 | 1.00 | 1.00 | 51200 | 200 | 20.4 | 12.8 | 64.0 | 0.97 | 0.30 |
| LEV2 | 1.00 | 1.00 | 38912 | 152 | 16.0 | 12.0 | 18.7 | 0.95 | 0.23 |
| LEV3 | 1.00 | 1.00 | 69120 | 270 | 42.7 | 16.0 | 56.6 | 0.88 | 0.29 |
| LEV4 | 1.00 | 1.00 | 53248 | 208 | 17.2 | 12.2 | 29.3 | 0.88 | 0.29 |
| LEV5 | 1.00 | 1.00 | 56320 | 220 | 16.1 | 16.0 | 42.7 | 0.91 | 0.38 |
| LEV6 | 1.00 | 1.00 | 46080 | 180 | 16.0 | 16.0 | 23.9 | 0.96 | 0.37 |
| LEV7 | 1.00 | 1.00 | 32768 | 128 | 16.2 | 16.0 | 20.0 | 0.71 | 0.20 |
| LEV8 | 1.00 | 1.00 | 52224 | 204 | 18.3 | 16.0 | 47.7 | 0.89 | 0.47 |
| LEV9 | 1.00 | 1.00 | 40960 | 160 | 16.4 | 16.0 | 48.0 | 0.94 | 0.24 |
| LEV10 | 1.00 | 1.00 | 59392 | 232 | 16.0 | 12.8 | 19.3 | 0.83 | 0.42 |
| LEV11 | 1.00 | 1.00 | 40960 | 160 | 16.0 | 16.0 | 33.9 | 0.88 | 0.14 |
| LEV12 | 1.00 | 1.00 | 50176 | 196 | 20.2 | 12.2 | 34.6 | 0.98 | 0.37 |
| LEV13 | 1.00 | 1.00 | 45056 | 176 | 16.0 | 16.0 | 22.6 | 0.99 | 0.46 |
| LEV14 | 1.00 | 1.00 | 45056 | 176 | 26.7 | 15.8 | 53.3 | 0.93 | 0.31 |
| LEV15 | 1.00 | 1.00 | 43008 | 168 | 16.5 | 16.0 | 23.0 | 0.95 | 0.34 |
| LEV16 | 1.00 | 1.00 | 49664 | 194 | 21.3 | 16.0 | 29.3 | 0.97 | 0.30 |
| LEV17 | 1.00 | 1.00 | 38912 | 152 | 16.0 | 10.7 | 27.4 | 0.95 | 0.27 |
| LEV18 | 1.00 | 1.00 | 35328 | 138 | 16.1 | 15.5 | 25.6 | 0.92 | 0.33 |
| LEV19 | 1.00 | 1.00 | 40960 | 160 | 16.0 | 11.3 | 22.6 | 0.99 | 0.28 |
| LEV20 | 1.00 | 1.00 | 37888 | 148 | 17.3 | 10.7 | 29.4 | 0.91 | 0.25 |
| LEV21 | 1.00 | 1.00 | 50176 | 196 | 16.3 | 16.0 | 20.6 | 0.99 | 0.45 |
| LEV22 | 1.00 | 1.00 | 46080 | 180 | 16.0 | 16.0 | 18.8 | 0.73 | 0.46 |
| LEV23 | 1.00 | 1.00 | 48128 | 188 | 42.7 | 21.3 | 64.0 | 1.00 | 0.44 |
| LEV24 | 1.00 | 1.00 | 50176 | 196 | 16.3 | 16.0 | 18.9 | 0.73 | 0.24 |
| LEV25 | 1.00 | 1.00 | 57856 | 226 | 16.0 | 16.0 | 19.5 | 0.81 | 0.07 |
| LEV26 | 1.00 | 1.00 | 45568 | 178 | 16.0 | 16.0 | 18.1 | 1.00 | 0.42 |
| LEV27 | 1.00 | 1.00 | 49152 | 192 | 16.0 | 16.0 | 18.1 | 0.91 | 0.64 |
| LEV28 | 1.00 | 1.00 | 53248 | 208 | 16.0 | 16.0 | 17.0 | 0.66 | 0.41 |
| LEV29 | 1.00 | 1.00 | 73216 | 286 | 16.0 | 16.0 | 18.1 | 1.00 | 0.67 |
| LEV30 | 1.00 | 1.00 | 69632 | 272 | 16.5 | 16.0 | 18.1 | 1.00 | 0.35 |
| LEV31 | 1.00 | 1.00 | 40960 | 160 | 16.5 | 16.0 | 18.1 | 0.82 | 0.30 |
| LEV32 | 1.00 | 1.00 | 45056 | 176 | 16.0 | 16.0 | 18.1 | 0.99 | 0.49 |
<!-- /@HEIGHTS -->

* **XY Saturn** : 1 unité monde = 1 texel (`TILESIZE 64` SLEVEL.H:126 : une tuile 64 px couvre 64 u ;
  `tileLength = (len+32)/64` CONVERT.C:1802). Sommets 45-81 % sur une grille de 64 u en XZ, `y` 81-100 % sur 16 u (37-92 % sur 64 u).
  1 unité Dex = ½ unité monde (CONVERT.C:1362-1364) ; sortie en `short` (SLEVEL.H:115) ⇒ ±16 383 u.
* **XY Build** : un mur affiche `xrepeat·8` texels ([jf] engine.c:1027 `walxrepeat = xrepeat<<3`) ⇒
  texel = L/(8·xrepeat) : **médiane 16 u xy sur 15/33 cartes, 16-43 sur les autres** (p10 = 10,7-21,3) ; l'éditeur pose
  `xrepeat = L·yrepeat/1024` ([jf] build.c:5994 `fixrepeats`) soit exactement 16 u/texel à `yrepeat 8`.
  Sommets Build 66-100 % sur une grille de 64, 7-67 % sur 1024.
* **Hauteur Build** : `z` a 16 unités par unité xy ; un texel vertical = 2048/yrepeat z
  ([jf] engine.c:2519 `globalyscale = yrepeat << (globalshiftval-19)` avec `globalshiftval = 32-log2(h)`
  :2510-2512, donc texel = dz·yrepeat/2048) = 256 z à `yrepeat 8` = 16 u xy : isotrope avec l'horizontal.
  `floorz`/`ceilingz` sont **tous** multiples de 1024 (33/33 cartes) ; hauteur de pièce médiane
  32 768-73 216 z = 128-286 u Saturn à 1/256.
* **Hauteur Saturn** : `y` vers le **haut** (hauteurs plafond−sol positives sur les 23 niveaux ; TOMB
  secteur 0 : `floorLevel −160`, centre y 20) ; `floorLevel` = moyenne entière des y des 4 sommets des
  murs à `normal[1] > 0` (CONVERT.C:2040-2055, vérifié 424/425 KARNAK, 570/570 SUNKEN ; les écarts sont
  les secteurs à sol en pente où la moyenne est tronquée) ; hauteur de pièce médiane par niveau 160-512 u.
* **Loi retenue pour un convertisseur** (isotrope, cohérente avec les texels des deux moteurs) :
  `X = x_build / 16`, `Z = ±y_build / 16` (Build y croît vers le sud ; le signe de Z est libre car les
  8 isométries sont équivalentes pour le moteur — choisir `Z = −y` pour garder l'orientation des faces),
  `Y = −z_build / 256` (+ constante), donc en Dex (`short`, ½ u) : `x_dex = x/8`, `y_dex = ∓y/8`,
  `z_dex = −z/128`. Un pas de 1024 z = 4 u = un demi-« 8 » Saturn ; une grande case Build (1024 xy) =
  64 u = une tuile. Le champ `xrepeat/yrepeat` n'a pas d'équivalent : chaque face Dex est une tuile
  64×64 étirée sur son quad (LEVEL_PIPELINE §2).

## 5. Structure d'un niveau Saturn (ce que le convertisseur doit produire)

<!-- @SAT_STRUCT -->
**Structure des niveaux Saturn**

| LEV | sect | polyedres convexes (murs lateraux) | portes aplaties | portails lateraux | INVISIBLE | textures | symetriques | portails sol/plafond | murs par arete XZ {k: n} | superposes (au-dessus) | dont +eau | chevauchent | eau | secteurs a pente | murs en pente | objets (types) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CAVERN | 415 | 415 | 13 | 1580 | 1561 | 19 | 1580 | 4 | {1: 1794, 2: 267, 3: 143, 4: 12, 5: 3, 7: 2, 9: 6} | 302 | 0 | 0 | 0 | 81 | 99 | 222 (22) |
| CHAOS | 294 | 292 | 9 | 728 | 720 | 8 | 728 | 2 | {1: 1158, 2: 108, 3: 38, 4: 5, 5: 4, 9: 2} | 201 | 0 | 1 | 0 | 36 | 40 | 223 (27) |
| COLONY | 432 | 400 | 19 | 1504 | 1498 | 6 | 1504 | 0 | {1: 1682, 2: 231, 3: 125, 4: 14, 5: 21} | 129 | 0 | 11 | 0 | 135 | 168 | 250 (21) |
| GORGE | 358 | 354 | 20 | 1244 | 1230 | 14 | 1244 | 100 | {1: 1582, 2: 183, 3: 96, 4: 1} | 149 | 80 | 0 | 54 | 25 | 27 | 238 (29) |
| KARNAK | 425 | 415 | 18 | 1292 | 1282 | 10 | 1292 | 88 | {1: 1638, 2: 324, 3: 46, 4: 11} | 102 | 67 | 3 | 75 | 105 | 135 | 254 (26) |
| KILARENA | 376 | 376 | 11 | 1644 | 1644 | 0 | 1644 | 0 | {1: 1356, 2: 87, 3: 180, 5: 40} | 227 | 0 | 0 | 0 | 75 | 105 | 32 (10) |
| KILENTRY | 145 | 145 | 11 | 634 | 634 | 0 | 634 | 0 | {1: 626, 2: 135, 3: 11} | 190 | 0 | 0 | 0 | 48 | 48 | 68 (13) |
| MAGMA | 424 | 371 | 14 | 1526 | 1520 | 6 | 1526 | 2 | {1: 1694, 2: 234, 3: 87, 4: 6} | 152 | 0 | 24 | 0 | 107 | 128 | 174 (23) |
| MARSH | 340 | 338 | 16 | 1014 | 992 | 22 | 1014 | 24 | {1: 1326, 2: 161, 3: 67, 4: 16, 5: 1, 6: 2} | 159 | 49 | 1 | 22 | 86 | 87 | 273 (27) |
| MINES | 566 | 565 | 11 | 1986 | 1960 | 26 | 1986 | 30 | {1: 1927, 2: 410, 3: 152, 4: 6, 5: 52} | 208 | 26 | 0 | 24 | 307 | 472 | 258 (28) |
| PASS | 337 | 337 | 8 | 1252 | 1240 | 12 | 1252 | 0 | {1: 1482, 2: 232, 3: 65, 4: 6, 5: 7} | 36 | 0 | 0 | 0 | 29 | 29 | 174 (28) |
| PEAK | 375 | 364 | 9 | 1684 | 1682 | 2 | 1684 | 0 | {1: 1532, 2: 206, 3: 207, 4: 10} | 482 | 0 | 0 | 0 | 77 | 84 | 154 (20) |
| SANCTUAR | 307 | 307 | 10 | 958 | 954 | 4 | 958 | 0 | {1: 1161, 2: 255, 3: 55, 4: 2, 5: 1} | 89 | 0 | 0 | 0 | 25 | 27 | 192 (31) |
| SELBUROW | 278 | 264 | 5 | 964 | 944 | 20 | 964 | 0 | {1: 1098, 2: 230, 3: 81, 4: 7, 5: 1} | 81 | 0 | 1 | 0 | 126 | 150 | 51 (17) |
| SELPATH | 341 | 314 | 14 | 1122 | 1122 | 0 | 1122 | 0 | {1: 1337, 2: 198, 3: 69, 4: 1, 6: 3} | 84 | 0 | 19 | 0 | 95 | 112 | 163 (23) |
| SETARENA | 206 | 206 | 6 | 740 | 721 | 19 | 740 | 0 | {1: 658, 2: 188, 3: 72, 6: 8} | 46 | 0 | 0 | 0 | 69 | 78 | 76 (17) |
| SETPALAC | 242 | 238 | 7 | 872 | 860 | 12 | 872 | 0 | {1: 957, 2: 158, 3: 88, 4: 5} | 23 | 0 | 1 | 0 | 101 | 139 | 209 (23) |
| SHRINE | 411 | 403 | 23 | 1452 | 1432 | 20 | 1452 | 6 | {1: 1609, 2: 347, 3: 109, 4: 4, 5: 1} | 126 | 27 | 3 | 17 | 59 | 69 | 223 (29) |
| SLAVCAMP | 413 | 403 | 33 | 1418 | 1402 | 16 | 1418 | 0 | {1: 1688, 2: 280, 3: 98, 4: 4, 5: 4, 8: 8} | 159 | 0 | 4 | 0 | 66 | 75 | 291 (29) |
| SUNKEN | 570 | 563 | 18 | 2448 | 2445 | 3 | 2448 | 132 | {1: 2432, 2: 295, 3: 275} | 510 | 499 | 1 | 436 | 38 | 50 | 206 (20) |
| THOTH | 488 | 441 | 15 | 1758 | 1751 | 7 | 1758 | 22 | {1: 1912, 2: 363, 3: 120, 4: 7, 5: 2} | 282 | 0 | 2 | 0 | 204 | 250 | 218 (27) |
| TOMB | 197 | 197 | 6 | 630 | 616 | 14 | 630 | 0 | {1: 720, 2: 177, 3: 53, 4: 2, 6: 1} | 6 | 0 | 0 | 0 | 53 | 86 | 82 (18) |
| TOMBEND | 161 | 161 | 3 | 644 | 616 | 28 | 644 | 0 | {1: 581, 2: 128, 3: 79} | 11 | 0 | 0 | 0 | 5 | 6 | 127 (14) |
  CAVERN exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(0, 3, -1664, -3296, 'superpose'), (0, 107, -1664, -2624, 'superpose'), (2, 215, -2688, -1664, 'superpose')]
  CHAOS exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(3, 42, -256, -640, 'superpose'), (3, 55, -256, -640, 'superpose'), (3, 57, -256, -1024, 'superpose')]
  COLONY exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(0, 23, -896, -128, 'superpose'), (1, 18, -1024, -256, 'superpose'), (3, 9, -1024, -384, 'superpose')]
  GORGE exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(2, 7, -448, -152, 'superpose+eau'), (3, 6, -448, -152, 'superpose+eau'), (13, 103, 512, -576, 'superpose')]
  KARNAK exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(5, 86, -128, 64, 'superpose'), (7, 93, -128, 64, 'superpose'), (10, 89, -128, 64, 'superpose')]
  KILARENA exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(21, 83, -1920, 736, 'superpose'), (22, 49, -1920, 544, 'superpose'), (22, 67, -1920, 416, 'superpose')]
  KILENTRY exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(0, 43, -128, -512, 'superpose'), (1, 80, -80, -440, 'superpose'), (1, 84, -80, -440, 'superpose')]
  MAGMA exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(14, 19, -288, -160, 'superpose'), (15, 20, -384, -160, 'superpose'), (17, 21, -544, -192, 'superpose')]
  MARSH exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(0, 42, -128, -896, 'superpose+eau'), (0, 296, -128, -896, 'superpose+eau'), (1, 247, -144, -568, 'superpose')]
  MINES exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(13, 67, -512, -1664, 'superpose'), (15, 39, -512, -426, 'superpose'), (15, 67, -512, -1664, 'superpose')]
  PASS exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(9, 146, -512, -320, 'superpose'), (10, 16, 272, -512, 'superpose'), (11, 12, -512, -64, 'superpose')]
  PEAK exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(0, 19, 0, 448, 'superpose'), (0, 163, 0, 1984, 'superpose'), (1, 147, -160, 896, 'superpose')]
  SANCTUAR exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(1, 37, 384, -128, 'superpose'), (1, 42, 384, -16, 'superpose'), (1, 43, 384, -32, 'superpose')]
  SELBUROW exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(6, 236, -144, -1568, 'superpose'), (7, 154, -160, -1568, 'superpose'), (7, 236, -160, -1568, 'superpose')]
  SELPATH exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(0, 16, 0, 0, 'chevauche'), (2, 17, -192, -192, 'chevauche'), (3, 22, 0, 0, 'chevauche')]
  SETARENA exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(19, 71, -384, -1088, 'superpose'), (21, 74, -384, -1088, 'superpose'), (23, 70, -384, -1088, 'superpose')]
  SETPALAC exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(0, 82, -276, 0, 'superpose'), (5, 26, -276, 0, 'superpose'), (9, 23, -384, 0, 'superpose')]
  SHRINE exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(4, 7, 640, 896, 'superpose'), (6, 8, 896, 640, 'superpose'), (6, 140, 896, 640, 'superpose')]
  SLAVCAMP exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(0, 21, -128, -128, 'chevauche'), (3, 23, -128, -128, 'chevauche'), (4, 19, -128, 128, 'superpose')]
  SUNKEN exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(0, 37, -64, 128, 'superpose+eau'), (1, 36, -64, 128, 'superpose+eau'), (2, 28, 192, 448, 'superpose')]
  THOTH exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(0, 146, -32, -384, 'superpose'), (1, 150, 64, -368, 'superpose'), (2, 149, -32, -368, 'superpose')]
  TOMB exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(1, 139, -128, 320, 'superpose'), (56, 128, -432, -256, 'superpose'), (138, 149, 384, -176, 'superpose')]
  TOMBEND exemples (i, j, floorLevel_i, floorLevel_j, classe) : [(32, 61, 320, 528, 'superpose'), (33, 60, 320, 528, 'superpose'), (34, 62, 320, 528, 'superpose')]
<!-- /@SAT_STRUCT -->

<!-- @DOS_STRUCT -->
**Structure des cartes Build (DOS)**

| MAP | sect | murs | 2 faces | convexes | multi-boucles | sommets rentrants | morceaux convexes >= | x | recouvrements XY (pair-impair) | lotags des paires | sprites | max murs/sect |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LEV0 | 128 | 865 | 538 | 94 (73%) | 12 | 59 | 221 | 1.73 | 59 | [((0, 0), 57), ((8, 0), 2)] | 139 | 27 |
| LEV1 | 416 | 3048 | 1690 | 272 (65%) | 29 | 386 | 876 | 2.11 | 129 | [((0, 0), 127), ((21, 0), 1), ((45, 0), 1)] | 515 | 65 |
| LEV2 | 365 | 2382 | 1414 | 289 (79%) | 22 | 224 | 639 | 1.75 | 31 | [((0, 0), 21), ((8, 0), 5), ((21, 0), 3), ((0, 21), 1)] | 505 | 52 |
| LEV3 | 361 | 3129 | 1906 | 264 (73%) | 29 | 305 | 804 | 2.23 | 30 | [((0, 0), 19), ((0, 6), 2), ((0, 8), 2), ((0, 21), 2)] | 439 | 281 |
| LEV4 | 440 | 3159 | 1814 | 361 (82%) | 33 | 332 | 856 | 1.95 | 23 | [((0, 0), 11), ((0, 10034), 2), ((0, 11034), 2), ((0, 8034), 1)] | 582 | 92 |
| LEV5 | 790 | 5205 | 3176 | 610 (77%) | 45 | 529 | 1453 | 1.84 | 125 | [((0, 0), 112), ((21, 0), 3), ((0, 21), 2), ((8, 0), 2)] | 967 | 60 |
| LEV6 | 573 | 4349 | 2408 | 427 (75%) | 27 | 608 | 1257 | 2.19 | 102 | [((0, 0), 82), ((0, 64), 6), ((0, 21), 4), ((64, 0), 2)] | 696 | 137 |
| LEV7 | 791 | 5189 | 3320 | 624 (79%) | 49 | 410 | 1325 | 1.68 | 261 | [((0, 0), 250), ((0, 8), 3), ((0, 56), 3), ((0, 24), 1)] | 1741 | 50 |
| LEV8 | 454 | 3181 | 1797 | 362 (80%) | 12 | 407 | 885 | 1.95 | 122 | [((0, 0), 104), ((3009, 0), 7), ((3007, 0), 4), ((0, 1), 2)] | 422 | 81 |
| LEV9 | 514 | 4324 | 2132 | 338 (66%) | 26 | 664 | 1238 | 2.41 | 128 | [((0, 0), 107), ((56, 0), 5), ((1, 0), 3), ((1010, 0), 2)] | 752 | 73 |
| LEV10 | 79 | 656 | 460 | 56 (71%) | 6 | 76 | 173 | 2.19 | 12 | [((0, 0), 8), ((0, 64), 3), ((2011, 0), 1)] | 237 | 45 |
| LEV11 | 601 | 4064 | 2130 | 464 (77%) | 39 | 400 | 1127 | 1.88 | 85 | [((0, 0), 70), ((0, 8), 3), ((0, 2034), 2), ((1010, 0), 2)] | 529 | 68 |
| LEV12 | 492 | 3659 | 2170 | 330 (67%) | 33 | 520 | 1094 | 2.22 | 45 | [((0, 0), 45)] | 583 | 57 |
| LEV13 | 471 | 3403 | 2074 | 359 (76%) | 38 | 314 | 905 | 1.92 | 25 | [((0, 0), 18), ((21, 0), 2), ((0, 2008), 1), ((0, 8), 1)] | 708 | 70 |
| LEV14 | 547 | 4231 | 2200 | 378 (69%) | 38 | 500 | 1229 | 2.25 | 118 | [((0, 0), 88), ((0, 2008), 5), ((0, 4005), 3), ((0, 21), 3)] | 665 | 156 |
| LEV15 | 647 | 4046 | 2202 | 506 (78%) | 13 | 399 | 1098 | 1.70 | 64 | [((0, 0), 47), ((0, 21), 4), ((21, 0), 3), ((0, 58), 2)] | 653 | 56 |
| LEV16 | 557 | 4306 | 2640 | 386 (69%) | 47 | 493 | 1204 | 2.16 | 55 | [((0, 0), 45), ((21, 0), 4), ((56, 0), 2), ((0, 56), 1)] | 990 | 65 |
| LEV17 | 601 | 4183 | 2322 | 453 (75%) | 36 | 391 | 1104 | 1.84 | 37 | [((0, 0), 14), ((0, 7034), 9), ((8, 0), 5), ((0, 21), 4)] | 724 | 75 |
| LEV18 | 573 | 5166 | 2996 | 382 (67%) | 39 | 933 | 1640 | 2.86 | 47 | [((0, 0), 45), ((0, 11), 1), ((0, 21), 1)] | 948 | 259 |
| LEV19 | 114 | 822 | 626 | 79 (69%) | 8 | 81 | 211 | 1.85 | 9 | [((0, 0), 5), ((56, 0), 4)] | 103 | 28 |
| LEV20 | 715 | 6226 | 3752 | 494 (69%) | 58 | 790 | 1683 | 2.35 | 76 | [((0, 0), 44), ((0, 35), 15), ((0, 64), 2), ((4005, 0), 2)] | 1031 | 109 |
| LEV21 | 92 | 747 | 434 | 58 (63%) | 3 | 144 | 244 | 2.65 | 2 | [((0, 0), 2)] | 229 | 60 |
| LEV22 | 85 | 729 | 492 | 65 (76%) | 9 | 58 | 173 | 2.04 | 20 | [((0, 0), 19), ((21, 0), 1)] | 94 | 42 |
| LEV23 | 99 | 636 | 324 | 72 (73%) | 5 | 51 | 160 | 1.62 | 1 | [((0, 0), 1)] | 203 | 45 |
| LEV24 | 109 | 943 | 580 | 76 (70%) | 3 | 190 | 317 | 2.91 | 8 | [((0, 0), 5), ((0, 4009), 3)] | 206 | 58 |
| LEV25 | 136 | 1037 | 536 | 107 (79%) | 11 | 93 | 263 | 1.93 | 44 | [((0, 0), 42), ((0, 9), 1), ((1010, 0), 1)] | 213 | 58 |
| LEV26 | 100 | 716 | 422 | 75 (75%) | 8 | 94 | 214 | 2.14 | 0 | [] | 144 | 37 |
| LEV27 | 168 | 1122 | 822 | 140 (83%) | 7 | 95 | 287 | 1.71 | 11 | [((0, 0), 9), ((4009, 0), 2)] | 227 | 38 |
| LEV28 | 114 | 1052 | 518 | 73 (64%) | 5 | 195 | 331 | 2.90 | 76 | [((0, 0), 75), ((3009, 0), 1)] | 216 | 38 |
| LEV29 | 110 | 763 | 492 | 87 (79%) | 5 | 63 | 193 | 1.75 | 7 | [((0, 0), 7)] | 150 | 42 |
| LEV30 | 97 | 724 | 502 | 70 (72%) | 3 | 111 | 220 | 2.27 | 11 | [((0, 0), 11)] | 136 | 76 |
| LEV31 | 76 | 736 | 458 | 44 (58%) | 11 | 97 | 199 | 2.62 | 41 | [((0, 0), 41)] | 141 | 58 |
| LEV32 | 176 | 1097 | 652 | 121 (69%) | 11 | 119 | 325 | 1.85 | 33 | [((0, 0), 33)] | 239 | 37 |
<!-- /@DOS_STRUCT -->

Faits établis (chiffres ci-dessus, source citée) :

1. **Un secteur Saturn est un polyèdre convexe**, pas un polygone extrudé : ses « murs » sont toutes ses
   faces — latérales (verticales **ou inclinées** : KARNAK mur 1356 normale (0,89, −0,45, 0) = rampe),
   sol, plafond — en quads `v[0..3]` (v0→v1 = arête haute horizontale, v1→v2 descend : KARNAK mur 1027),
   jamais dégénérés côté murs (0 quad à sommet répété sur les 66 881 murs des 23 niveaux) — les triangles n'existent qu'au niveau des faces (22 169 / 233 880 faces ont un sommet répété). Plan
   `normal·p + d = 0` en 16.16 (CONVERT.C:1955-2003), **normale vers l'intérieur** (TOMB mur 0, KARNAK
   mur 1027 : centre du côté positif). Convexité vérifiée sur les murs latéraux : 415/425 KARNAK,
   197/197 TOMB, 563/570 SUNKEN, 371/424 MAGMA — le pire (tolérance 2 u ; les échecs sont des plans inclinés arrondis).
2. **Sol et plafond = 1 quad chacun, rectangle englobant** de l'empreinte (KARNAK secteur 20 : couloir
   diagonal 181 u de large, quad sol 448×448 = boîte englobante ; CONVERT.C:2064-2083 génère ces deux
   « murs » par secteur), et les **faces** (`firstFace..lastFace`) sont les tuiles 64×64 qui pavent
   l'empreinte (secteur 402 : 30 faces pour 864×96). Le moteur découpe par portail (WALLS.C:1100-1128
   `normTransform` + clip par face). Chaque secteur a exactement 1 sol + 1 plafond (23/23 niveaux).
3. **Murs par secteur** : mode 6 = 4 côtés + sol + plafond ; mais une même **arête XZ porte 2 à 4 murs
   empilés** (KARNAK : 324 arêtes à 2 murs, 46 à 3, 11 à 4 ; TOMB jusqu'à 6) = *bas plein / portail /
   haut plein* : secteur 148, arête x = 1792 (z 512..576) : mur 1028 plein y −128..64 (faces), mur 1025 portail `INVISIBLE` (trapèze, un coin à −128) → secteur 142 ; arête voisine (z 448..512) : mur 1026 portail `SHORTOPENING` → 145, mur 1029 plein
   `PARALLELOGRAM` (table `textures`). C'est la traduction explicite du mur Build à 2 faces
   (lower/portal/upper) — CONVERT ne le fait pas, **Brew l'exporte tel quel**.
4. **Portails** : 100 % des murs latéraux à `nextSector ≠ −1` ont un mur symétrique (même arête XZ) dans
   le secteur voisin (1292/1292 KARNAK, 2448/2448 SUNKEN, 630/630 TOMB) ; 95,7-100 % sont `INVISIBLE`
   (CONVERT.C:1783-1784 : `firstface == −1`), le reste = portails texturés (surfaces d'eau, portes,
   murs explosables : CONVERT.C:2989-2995). Les **portails horizontaux** (`nextSector` sur un sol/plafond,
   88 KARNAK, 132 SUNKEN, 0 TOMB) sont les surfaces d'eau (`WALLFLAG_WATERSURFACE` CONVERT.C:1851-1856,
   `def5_s.bmp` sur un portail à `nz = ±1`) et les empilements verticaux.
5. **Superposition verticale de secteurs** : des paires d'empreintes XZ se recouvrent avec l'un au-dessus
   de l'autre — KARNAK 102 paires (67 avec un secteur `SECFLAG_WATER`), MAGMA 152 (sans eau : étages),
   SUNKEN 510 (499 eau), PEAK 482 (sans eau). Un niveau Saturn est donc de la vraie 3D empilée, ce que Build ne fait qu'en
   secteurs superposés « ROR » (DOS : 0-261 paires par carte — LEV7 max —, lotag 0/0 surtout, sinon 8/21/35/56/64 = eau/portes/ROR
   Exhumed, sprites lotag 80 « underwater » et 99 `SetAbove(sector, hitag)` [ex] init.cpp:952-958,
   1036-1040).
6. **Pentes** : 5-307 secteurs par niveau (TOMBEND 5, MINES 307) ont au moins un mur incliné (105 KARNAK, 107 MAGMA, 53 TOMB) ;
   les 33 cartes DOS n'en ont **aucune** (`stat & 2` jamais posé, parseur). Le SlaveDriver gère les plans
   arbitraires, le Build v6 de 1995 non.
7. **Portes** : plafond aplati sur le sol (`makeDoorWay` CONVERT.C:3608-3612 « fix for brew output doorways
   wrong ») — 18 secteurs KARNAK, 6 TOMB, 14 MAGMA ont leur plan plafond ≤ plan sol ; les côtés restent
   texturés jusqu'à la hauteur ouverte ; le mouvement est un push-block (`sPBType`, PBWall/PBVert).
8. **Convexité côté Build** : seulement 58-83 % des secteurs DOS sont convexes sans trou ; borne
   inférieure des morceaux convexes (sommets rentrants + 1, + 2 par trou) = **1,6 à 2,9 × le nombre de
   secteurs** : LEV5 790 → ≥ 1453, LEV20 715 → ≥ 1683, contre `MAXNMSECTORS 600` (UTIL.H:21) — **aucune
   carte solo DOS ne rentre** dans le cap Saturn après découpe convexe (seules LEV0/10/19 et les 12 cartes deathmatch, 76-176 secteurs, tiennent). Duke Saturn a scindé E1L4/E1L5 pour cette raison
   (RETAIL_DISCS §6) et les .LEV Saturn font 145-570 secteurs pour 5-90 murs/secteur.

## 6. Tuiles

<!-- @TILES -->
**Tuiles : picnums Build distincts (murs+sols+plafonds+overpic) vs tuiles Saturn distinctes (faces U texture) / tuiles du fichier**

| MAP | picnums murs | sols | plafonds | overpic | union | sprites picnums |   | LEV | tuiles faces | table texture | union | tuiles fichier |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| LEV0 | 18 | 10 | 7 | 0 | 25 | 22 |   | CAVERN | 24 | 27 | 35 | 489 |
| LEV1 | 58 | 24 | 24 | 5 | 72 | 59 |   | CHAOS | 47 | 32 | 52 | 549 |
| LEV2 | 55 | 17 | 12 | 4 | 67 | 42 |   | COLONY | 60 | 51 | 65 | 409 |
| LEV3 | 33 | 8 | 8 | 3 | 39 | 53 |   | GORGE | 39 | 46 | 57 | 507 |
| LEV4 | 50 | 23 | 18 | 4 | 63 | 38 |   | KARNAK | 52 | 69 | 75 | 525 |
| LEV5 | 38 | 14 | 15 | 1 | 45 | 49 |   | KILARENA | 20 | 18 | 21 | 652 |
| LEV6 | 47 | 21 | 18 | 4 | 62 | 61 |   | KILENTRY | 22 | 24 | 25 | 226 |
| LEV7 | 41 | 12 | 15 | 3 | 51 | 53 |   | MAGMA | 34 | 39 | 43 | 407 |
| LEV8 | 44 | 14 | 15 | 1 | 51 | 39 |   | MARSH | 50 | 53 | 64 | 522 |
| LEV9 | 25 | 10 | 11 | 2 | 33 | 41 |   | MINES | 36 | 40 | 53 | 482 |
| LEV10 | 21 | 9 | 10 | 1 | 30 | 42 |   | PASS | 25 | 26 | 35 | 353 |
| LEV11 | 58 | 20 | 19 | 2 | 66 | 46 |   | PEAK | 59 | 33 | 67 | 345 |
| LEV12 | 41 | 17 | 13 | 6 | 53 | 41 |   | SANCTUAR | 45 | 49 | 66 | 484 |
| LEV13 | 43 | 16 | 16 | 2 | 49 | 44 |   | SELBUROW | 27 | 25 | 43 | 412 |
| LEV14 | 59 | 17 | 13 | 6 | 74 | 48 |   | SELPATH | 34 | 32 | 43 | 504 |
| LEV15 | 55 | 17 | 14 | 3 | 62 | 58 |   | SETARENA | 21 | 24 | 26 | 495 |
| LEV16 | 27 | 9 | 10 | 2 | 29 | 31 |   | SETPALAC | 26 | 40 | 45 | 422 |
| LEV17 | 69 | 19 | 22 | 3 | 81 | 56 |   | SHRINE | 49 | 44 | 59 | 492 |
| LEV18 | 45 | 20 | 19 | 6 | 57 | 52 |   | SLAVCAMP | 54 | 53 | 72 | 484 |
| LEV19 | 27 | 8 | 8 | 0 | 29 | 17 |   | SUNKEN | 27 | 36 | 42 | 323 |
| LEV20 | 54 | 23 | 24 | 9 | 67 | 54 |   | THOTH | 54 | 50 | 72 | 455 |
| LEV21 | 2 | 3 | 2 | 0 | 6 | 20 |   | TOMB | 34 | 52 | 60 | 526 |
| LEV22 | 3 | 5 | 4 | 0 | 8 | 17 |   | TOMBEND | 27 | 29 | 38 | 498 |
| LEV23 | 6 | 3 | 2 | 0 | 9 | 21 |   |  |
| LEV24 | 6 | 3 | 2 | 1 | 9 | 15 |   |  |
| LEV25 | 23 | 10 | 9 | 0 | 30 | 22 |   |  |
| LEV26 | 6 | 4 | 3 | 0 | 9 | 14 |   |  |
| LEV27 | 2 | 1 | 2 | 0 | 3 | 14 |   |  |
| LEV28 | 6 | 6 | 4 | 2 | 12 | 19 |   |  |
| LEV29 | 3 | 2 | 2 | 0 | 5 | 12 |   |  |
| LEV30 | 1 | 1 | 3 | 0 | 4 | 13 |   |  |
| LEV31 | 7 | 4 | 6 | 1 | 13 | 16 |   |  |
| LEV32 | 11 | 5 | 3 | 1 | 16 | 18 |   |  |
<!-- /@TILES -->

Les picnums Build distincts (murs ∪ sols ∪ plafonds ∪ overpic) vont de 3 (LEV27, deathmatch) à 81 (LEV17) par carte ; côté Saturn, les tuiles distinctes référencées par les faces ∪ la table `textures` vont de 21 à 75,
et le fichier en embarque 226-652 (sprites/séquences/armes compris). Un convertisseur doit donc
(a) découper chaque picnum Build utilisé en sous-tuiles 64×64 (un picnum de 128×128 = 4 tuiles),
(b) rester sous **126 entrées `.til`** (`facetype.tile` est un `signed char`, CONVERT.C:658) ou
court-circuiter CONVERT (voie directe LEVEL_PIPELINE §3), (c) convertir 8 bpp PALETTE.DAT → palette
Saturn avec index 255 transparent (CONVERT.C:837-840). Le compte de tuiles Saturn n'est PAS dérivable du
compte de picnums (un picnum 64×64 = 1 sous-tuile, un 256×128 = 8 ; le facteur dépend des tailles ART, non mesurées ici).

## 7. Règles qu'un convertisseur Build → Dex doit implémenter (avec la preuve)

| # | règle | preuve |
|---|---|---|
| R1 | Repère : `x_dex = x/8`, `y_dex = −y/8` (ou `+y/8`, isométrie libre), `z_dex = −z/128` ; en `short` ⇒ carte Build dans ±131 072 xy et ±2 097 152 z (toutes les 33 cartes tiennent : bbox ±65 792 / z ±1 134 592) | §4 ; CONVERT.C:1362-1364 ; SLEVEL.H:115 |
| R2 | Découpe convexe de chaque secteur Build (boucles multiples = trous à ouvrir) ; **≥ 1,6-2,9 ×** secteurs ; cap 600 secteurs / 5500 murs / 900 000 o (UTIL.H:21-22, LEVEL.C:41) ⇒ scinder les cartes solo | §5.8 |
| R3 | Par secteur convexe : N murs latéraux + **1 sol + 1 plafond** = quads rectangle englobant, normales vers l'intérieur, plan 16.16 ; `floorLevel` = moyenne des y du sol ; `center` = moyenne de tous les sommets | CONVERT.C:2016-2055, 2064-2083 ; §5.1-2 |
| R4 | Faces sol/plafond = pavage 64×64 aligné sur la grille monde (tuiles clipées par le secteur au rendu) ; `tileLength/tileHeight` = (len+32)/64 | CONVERT.C:1797-1810 ; §5.2 |
| R5 | Mur Build à 1 face → 1 mur plein (faces = grille `tileHeight × tileLength` ⇒ `PARALLELOGRAM` + table `textures`, sinon liste de quads + sommets privés) | CONVERT.C:1596-1694 (`mapRectWall`), 1871-1901, 1948-1949 |
| R6 | Mur Build à 2 faces → jusqu'à 3 murs sur la **même arête** : bas plein (sol ici → sol voisin), portail `INVISIBLE` + `nextSector`, haut plein ; le voisin reçoit le miroir (symétrie 100 %) ; les diagonales de la découpe = portails `INVISIBLE` des deux côtés | §5.3-4 ; CONVERT.C:1783-1784 |
| R7 | Flags dérivés à recalculer, pas à copier : `BLOCKED` si `nextsector == −1` (CONVERT.C:1786-1787) ; `SHORTOPENING` si ouverture 1 < h < 90 u (CONVERT.C:2555-2572) ; `CLIFFBNDRY` si chute > 320 u (2575-2585) ; `WATERBNDRY` si le voisin a une surface d'eau ou lave (2505-2540) ; `WATERSURFACE` = portail horizontal texturé `def5_s` (1851-1856) ; `PARALLAX` = `def2_c` ⇒ face supprimée (1860-1868 : ciel Build `ceilingstat & 1` → plafond `PARALLAX` sans faces) | source |
| R8 | Textures : xrepeat/yrepeat/panning/overpicnum perdus ; chaque face = une tuile 64×64 ; picnum → sous-tuiles ; ≤ 126 `.til` ou écriture directe | §6 ; CONVERT.C:658 |
| R9 | Eau : secteur Build `underwater` (sprite lotag 80/99 [ex] init.cpp:1036-1040, 952-958) → `SECFLAG_WATER` (`SR_SPECIAL_ROLE_16`, CONVERT.C:3135-3138) + surface = portail horizontal entre le secteur au-dessus et le secteur en dessous (superposition XZ, §5.5) + `WaveVert/WaveFace` (`initWater` CONVERT.C:2589-2680, généré par CONVERT) | source |
| R10 | Portes/ascenseurs : secteur porte = `DOORWAY_STYLE_n` (plafond aplati au sol, PB avec tous les murs voisins de même normale : CONVERT.C:3554-3625) ; lift = `LIFT_SHAFT_STYLE_n` (butées = min/max des sols voisins, 3628-3700) ; lotag Exhumed 1/8/24/63… ([ex] runlist.cpp:532-1227) → ~15 rôles Dex | LEVEL_PIPELINE §2 |
| R11 | Lumière : `sector.light` + `vertexLight` par sommet (`computeLightValue` CONVERT.C:1553-1590, tuile `value` + `.lit`) ← `shade` Build par mur/secteur (−128..127) : proposition : `light = clamp(80 − shade, 0, 128)` (80 = valeur par défaut sans `.lit`, CONVERT.C:557), à valider sur console | source |
| R12 | Sprites : monstres via `jeffMonsterMap` (CONVERT.C:409-430, `.lit`), objets/clés/interrupteurs via `sectorprops` et `argv[4]` ; lotag sprite Exhumed 100-120 = monstres, 6-60 = objets ([ex] init.cpp:664-780) → `OT_*` (SLEVEL.H:22-81) ; le reste (décor) n'existe pas côté Saturn | source |
| R13 | Pentes : non nécessaires pour DOS (0 pente) mais le format les accepte (R3 : plans arbitraires) | §5.6 |
| R14 | Cut-sort (`SECFLAG_CUTSORT`, `cutPlane[128]`) pour les secteurs qui se recouvrent en profondeur de tri (`SR_SPECIAL_ROLE_18`, CONVERT.C:2396-2425 `findCutWall`) : à générer pour toute superposition XZ non-eau, sinon artefacts de tri | source |

## 8. Ce que le Saturn ajoute et qui n'est PAS dérivable du .MAP DOS (preuves)

| ajout Saturn | où | pourquoi non dérivable |
|---|---|---|
| La géométrie elle-même | 24 .LEV, 0 segment commun (§3) | niveaux redessinés dans Brew ; seuls les noms/thèmes et l'art sont partagés |
| Pentes (5-307 secteurs/niveau) et faces triangulaires | `normal` non axial, faces à sommet répété (22 169) | Build v6 : `stat & 2` = 0 partout |
| Superposition verticale (jusqu'à 510 paires, SUNKEN) | empreintes XZ imbriquées | Build DOS n'a que des ROR à secteurs séparés + sprites 80/99 |
| Lumière par sommet (`nmLightValues`, `vertexLight`, `sVertexType.light`) | CONVERT.C:1553-1590, `.lit` | Build : `shade` par mur/secteur + sprites lumière ; pas de radiosité (`LCONV.C`) |
| Cut-planes (`nmCutSectors` × 128) : THOTH 82, SUNKEN 14, KARNAK 8 | CONVERT.C:2396-2425 | tri de portails Lobotomy ; Build trie par BSP de secteurs |
| Push-blocks (PB 5-34, PBWall, PBVert avec `XLOCK/YLOCK/ZLOCK`) | SLEVEL.H:97-113, CONVERT.C:3554-3700 | portes/ascenseurs Build = mouvement de `ceilingz/floorz` entier, pas de listes de sommets |
| Vagues (`WaveVert`/`WaveFace`, SUNKEN 251/132, GORGE 196/100) | `initWater` CONVERT.C:2589-2680 | générées par CONVERT à partir des surfaces d'eau, `pos = rand()%16−8` (2651) |
| Objets typés `OT_*` (227 types) + `objectParams` (canaux, hauteurs d'ascenseur, tables de puzzle) | SLEVEL.H:22-81, CONVERT.C:3119-3190 | ~15 rôles Dex ⇐ 99 lotags Exhumed ; les puzzles téléport (`SR_12-15`) et Ramsès (`SR_9/23`) sont des scripts Saturn |
| Sons dynamiques embarqués (4-28 par niveau, PCM 16 bits BE) | SOUND.C:231-241 | DOS : VOC dans STUFF.DAT, table par type d'objet, pas par niveau |
| Tuiles embarquées RLE/16 bpp (226-652) + palettes par niveau + séquences `PS` | PIC.C:611-716, SEQUENCE.C:24-89 | DOS : ART global 8 bpp + PALETTE.DAT ; les séquences sont un format Lobotomy (éditeur absent) |
| Ciel 512×256 16 bpp + table de rotation 320 ints | PLAX.C:84-115 | DOS : picnum de plafond parallaxe (ART) |
| Drapeaux dérivés (`SHORTOPENING`, `CLIFFBNDRY`, `WATERBNDRY`, `SWAMP`, `LAVA`, `EXPLODABLE`) | CONVERT.C:2480-2590, 2989-2995 | calculés par CONVERT depuis la géométrie et les **noms de fichiers BMP** des tuiles, pas des cstat Build |
| Champs de rendu (`pixelLength`, `tileLength/Height`, `d`, `center`, `floorLevel`) | CONVERT.C:1797-1810, 2020-2055 | pré-calculs Saturn (recalculables, mais absents du .MAP) |
| Graphe de carte (`levelGraph`, `levelPos`, `trackMap` CDDA) | BIGMAP.C:30-81, SOUND.C:369-380 | DOS : progression linéaire LEV1→20 + carte à plaques |

## 9. Reproduire

```powershell
python tools\dos_stats.py                      # STUFF.DAT -> build\tmp-b2d\dos\, dos_stats.json
python tools\lev.py                            # refs\extract\PS\*.LEV -> build\tmp-b2d\sat\, sat_stats.json
python tools\calibrate.py --scan --quick --names --fill-doc --out build\tmp-b2d   # ~20 min, régénère les tables (blocs <!-- @X -->)
```
Sorties : `build/tmp-b2d/calib_scan.json` (24 × 33 meilleurs candidats par orientation/échelle),
`calib_report.json` (affinages, structure, hauteurs, tuiles), `names/names_strip.png`.

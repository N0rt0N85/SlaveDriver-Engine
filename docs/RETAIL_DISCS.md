# Disques retail PowerSlave (USA), Duke Nukem 3D (USA) et Quake (USA) — inventaire, formats, lignée (2026-08-30 ; Quake §5 bis, 2026-09-10)

Données extraites sous `refs/` (ignoré, `.gitignore:4`) ; scripts : `[ps-lev]` = `build/tmp-ps-lev/lev.py`, `[duke]` = `build/tmp-duke/*.py`, `[bmap]` = `build/tmp-build-map/`, `[ici]` = `build/tmp-retail-docs/checks.py` (tous sous `build/`, `.gitignore:1`). `[src]` = fichier:ligne de l'arbre. `[est]` = estimation non prouvée. Les données retail sont sous copyright : jamais copiées dans un répertoire suivi (`cd/` ignoré, `.gitignore:5`, ajouté ce jour).

## 1. Identité des disques (IP = 256 premiers octets de la piste 01 ; descripteur de volume = `7z l`) [ici]
| | PowerSlave (USA) | Duke Nukem 3D (USA) |
|---|---|---|
| Maker / produit / version / date IP | `SEGA TP T-132` / `T-13205H` / `V1.000` / `19961008` | `SEGA ENTERPRISES` / `MK-81071` / `V1.018` / `19971008` |
| Device / zone / périph. / titre | `CD-1/1` / `U` / `JA` / `POWER SLAVE` | `CD-1/1` / `U` / `JDXEK` / `DUKE NUK'EM` |
| Taille IP (0xE0) / 1re lecture (0xF0) / taille (0xF4) | 0xE2C = 3628 / **0x06004000** / 0 (fichier entier) | 0xE2C = 3628 (= `DUKEIP.BIN` 3628 o à la racine) / **0x06010000** / 0 |
| Volume / éditeur / préparateur / créé | `POWERSLAVE` / `PLAYMATES TOYS` / `LOBOTOMY SOFTWARE` / 1996-10-08 09:39:25 | `DUKE_NUKEM_3D` / `SEGA ENTERPRISES, LTD.` / `LOBOTOMY SOFTWARE, INC.` / 1997-10-07 17:01:02 |
| Piste 01 (MODE1/2352) | 194 075 280 o = 82 515 secteurs = 18:20.20 | 70 153 104 o = 29 827 secteurs = 6:37.69 |
| Pistes / total .bin | 14 (02-14 CDDA), 571 150 272 o | 23 (02-23 CDDA), 564 341 232 o |
| 1er fichier racine (chargé par l'IP) | `0` = INIT, 142 716 o | `00000000.000`, 435 296 o (navigateur NetLink [orchestrateur] ; `DUKE/0` = programme jeu 444 408 o, chargé à 0x06010000 [duke]) |

VolumeSpaceSize déclaré 497 328 128 (PS) / 491 399 168 (Duke) > taille de l'iso extrait (168 990 720 / 61 085 696) → `7z` dit « Unexpected end of archive », inoffensif [ici].

## 2. Inventaire PowerSlave — 98 fichiers, 168 527 665 o, tous datés 1996-10-08 [ici]
| Famille | n | octets | détail |
|---|---|---|---|
| `0` (INIT) / `MAIN.BIN` | 2 | 142 716 / 316 716 | entrée `d0 01 40 2b` = `mov.l @(4,pc),r0 ; jmp @r0` (START.S) → 0x0600b804 / 0x0602d05c |
| `*.LEV` | 24 | 34 426 267 | cf. §4 ; `TEST.LEV` == `SANCTUAR.LEV` octet pour octet (1 416 356 o) |
| `SP_<lang>NN.LIP` | 56 | 85 722 637 | 14 par langue : ENG 22 732 862, FRE 23 474 246, GER 20 441 044, SPA 19 074 485 ; nom bâti par `sprintf("SP_%s%02d.LIP")` AI2.C:221, NN 13 = mauvaise fin, 14 = bonne fin (AI2.C:214-218) |
| `*.MOV` | 3 | 44 566 272 | OPEN 30 295 444, GOOD 8 174 176, BAD 6 096 652 |
| `*.DAT` | 5 | 2 176 867 | STATIC 678 511 (SRUINS.C:1922-1932), MAP 687 502, BONUS 645 010, INITLOAD 81 948, JINITLOD 83 896 |
| `*.PCS` | 4 | 1 010 764 | INTRO / JINTRO 297 380 chacun (non identiques), LOGOS / JLOGOS 208 002 chacun (non identiques) : versions japonaises (`J*`) présentes sur le disque US |
| `BONUS.BIN` | 1 | 165 340 | |
| `NITS_ABS/BIB/CPY.TXT` | 3 | 21 / 25 / 40 | « Copyright Lobotomy Software Inc., 1996 » |

## 3. Inventaire Duke Nukem 3D — 459 fichiers, 5 dossiers, 59 920 522 o [ici]
| Répertoire | n | octets | contenu |
|---|---|---|---|
| racine | 8 | 516 571 | `00000000.000` 435 296, `DUKEIP.BIN` 3628, `FLD_KNL.BIN` 4984, `XBMAP.BIN` 68, `LOADING.BMP` 72 516, 3 `NITS_*.TXT` |
| `DUKE/` | 55 | 52 385 072 | `0` 444 408 (programme unique : **pas de MAIN.BIN**), 30 `.LEV` 43 885 078, 20 `.DAT` 4 884 158, 3 `.MOV` 2 968 440 (LOGO 1 605 260, END2 854 828, END3 508 352), `BONUS.BIN` 202 988 |
| `NETLINK/` | 378 | 4 594 185 | 144 GIF, 112 WAV, 58 BMP, 38 HTM, 11 JPE, 8 WDT, 3 BIN 478 384, 1 EXE 47 432, LOG, TXT, BAK |
| `XBAND/` | 3 | 1 546 272 | 2 BIN 981 232 + 1 DAT 565 040 |
| `DUKEINFO/` | 15 | 878 422 | 2 BIN + TXT, `THEME00/` 12 BMP 868 596 |

## 4. PowerSlave `.LEV` (24) — format [src] et mesures [ps-lev]
Layout réel : palette 512 o + int 512 + int 256 + bitmap ciel 131 072 o (PLAX.C:84-94) **+ table 320×4 = 1280 o lue à PLAX.C:115** = 132 872 o ; puis `size` à 0x20708 et `sLevelHeader` (14 int, SLEVEL.H:5-20, 56 o) à **0x2070C** (= ReyeMe `Powerslave.cs` `stream.Position = 0x2070C`). `size == 56 + Σ LOADPART` sur les 24 (LEVEL.C:37-68 ; tailles `-m2` : sector 24, wall 48, vertex 8, face 10, object 4, PB **18** (SLEVEL.H:97-104 = 9 shorts sans padding ; UTIL/CONVERT.C:3338-3346 en écrit exactement 18 ; la mention « 20 dans la source » d'une première passe était FAUSSE, corrigée 2026-08-30), PBVert 4, WaveVert 16, WaveFace 8, PBWall 2, cutPlane 128/secteur). Puis sons dynamiques (SOUND.C:231-241 : int 227 = OT_NMTYPES + map + {size,rate,bps,loop}+PCM), tuiles (PIC.C:611-716), séquences (SEQUENCE.C:24-89) ; chaque fichier se termine exactement à EOF. Big-endian partout.

| Niveau | fichier | `size` | sect | murs | vert | faces | obj | PB | cut | sons n / PCM | tuiles n | seq | alloc parts | libre total @402 216 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CAVERN | 1 617 500 | 678 759 | 415 | 3619 | 40 892 | 15 290 | 222 | 14 | 0 | 19 / 145 337 | 489 | 39 592 | 678 764 | 26 572 |
| CHAOS | 1 577 796 | 458 027 | 294 | 2094 | 26 444 | 11 590 | 223 | 9 | 0 | 21 / 193 829 | 549 | 74 656 | 458 036 | 115 040 |
| COLONY | 1 454 415 | 581 396 | 432 | 3378 | 34 122 | 12 167 | 250 | 19 | 0 | 15 / 99 075 | 409 | 17 078 | 581 408 | 142 928 |
| GORGE | 1 465 282 | 478 058 | 358 | 2929 | 25 936 | 8617 | 238 | 21 | 2 | 23 / 165 671 | 507 | 29 132 | 478 068 | 199 252 |
| KARNAK | 1 620 769 | 527 285 | 425 | 3184 | 29 162 | 10 329 | 254 | 18 | 8 | 22 / 181 104 | 525 | 33 132 | 527 296 | 59 296 |
| KILARENA | 1 586 164 | 362 676 | 376 | 2917 | 19 962 | 4872 | 32 | 11 | 0 | 16 / 173 696 | 652 | 59 092 | 362 680 | 86 964 |
| KILENTRY | 673 815 | 171 185 | 145 | 1171 | 9484 | 2915 | 68 | 11 | 0 | 4 / 43 115 | 226 | 8076 | 171 196 | 866 624 |
| MAGMA | 1 593 047 | 606 762 | 424 | 3171 | 34 938 | 14 212 | 174 | 18 | 0 | 24 / 200 325 | 407 | 62 634 | 606 768 | 105 732 |
| MARSH | 1 505 581 | 468 734 | 340 | 2524 | 26 586 | 10 582 | 273 | 16 | 0 | 28 / 187 636 | 522 | 23 914 | 468 736 | 181 068 |
| MINES | 1 633 266 | 645 256 | 566 | 4147 | 36 830 | 11 836 | 258 | 11 | 3 | 25 / 187 762 | 482 | 31 908 | 645 260 | 53 328 |
| PASS | 1 249 287 | 521 780 | 337 | 2845 | 31 154 | 11 586 | 174 | 8 | 0 | 20 / 140 647 | 353 | 14 544 | 521 780 | 389 516 |
| PEAK | 1 497 182 | 666 824 | 375 | 3271 | 39 860 | 16 913 | 154 | 34 | 0 | 13 / 94 581 | 345 | 13 848 | 666 832 | 95 372 |
| SANCTUAR = TEST | 1 416 356 | 376 639 | 307 | 2438 | 20 908 | 6581 | 192 | 11 | 3 | 19 / 153 577 | 484 | 29 970 | 376 648 | 235 932 |
| SELBUROW | 1 321 898 | 389 724 | 278 | 2240 | 23 526 | 8341 | 51 | 5 | 0 | 21 / 177 210 | 412 | 32 060 | 389 732 | 353 772 |
| SELPATH | 1 401 464 | 483 592 | 341 | 2534 | 27 878 | 11 263 | 163 | 15 | 7 | 19 / 157 208 | 504 | 31 050 | 483 600 | 254 508 |
| SETARENA | 1 307 096 | 263 856 | 206 | 1632 | 14 488 | 5012 | 76 | 6 | 0 | 16 / 158 340 | 495 | 40 560 | 263 860 | 349 968 |
| SETPALAC | 1 184 011 | 323 163 | 242 | 1902 | 17 388 | 6220 | 209 | 7 | 0 | 20 / 159 851 | 422 | 23 382 | 323 172 | 474 272 |
| SHRINE | 1 641 467 | 578 964 | 411 | 3404 | 33 916 | 11 505 | 223 | 23 | 0 | 22 / 163 996 | 492 | 30 782 | 578 972 | **21 328** |
| SLAVCAMP | 1 543 900 | 550 337 | 413 | 3395 | 31 644 | 10 299 | 291 | 33 | 7 | 24 / 175 309 | 484 | 21 946 | 550 344 | 130 172 |
| SUNKEN | 1 444 286 | **773 018** | **570** | **4939** | 44 906 | 14 204 | 206 | 18 | 14 | 8 / 88 514 | 323 | 12 198 | 773 020 | 142 048 |
| THOTH | 1 579 383 | 594 856 | 488 | 3764 | 34 234 | 10 459 | 218 | 15 | 82 | 14 / 137 118 | 455 | 22 268 | 594 864 | 56 208 |
| TOMB | 1 390 564 | 295 907 | 197 | 1555 | 17 302 | 7015 | 82 | 7 | 0 | 11 / 113 120 | 526 | 11 838 | 295 916 | 221 332 |
| TOMBEND | 1 305 382 | 251 525 | 161 | 1390 | 14 932 | 5491 | 127 | 14 | 0 | 7 / 69 823 | 498 | 10 578 | 251 532 | 263 024 |

Plafonds [src] : `MAXNMSECTORS 600` / `MAXNMWALLS 5500` (UTIL.H:21-22) → SUNKEN 570 / 4939, 30 secteurs de marge ; `assert(size<900000)` LEVEL.C:42 tient (max 773 018) ; `MAXNMSOUNDS 80` (SOUND.C:40) vs 43 statiques (STATIC.DAT) + 28 dyn max (MARSH) = 71. Tuiles VDP2 : 0 dans tout `.LEV`, 18 dans STATIC.DAT (cap 50). Totaux 24 niveaux : 8408 secteurs, 66 881 murs, 657 400 sommets, 233 880 faces, 430 sons / 3 520 421 o PCM, 11 045 tuiles.
Mémoire : la demande brute `mem_malloc(1)` (header+parts+palettes+tuiles 16bpp) dépasse la HWRAM sur 23/24 niveaux (max PEAK 1 116 884, seul KILENTRY 391 872 tient) — c'est normal : `mem_nocheck_malloc` bascule sur l'autre zone quand la première est pleine (UTIL.C:365-388, zones 0x200000-0x300000 et `&end`-0x6100000, UTIL.C:351-352). Rejeu de l'allocateur [ps-lev] avec HWRAM d'origine 402 216 o (= 0x06100000 − (0x06053d40 + 0x49f98), PORTING_NOTES.md:16-17) : tout tient, minimum libre 21 328 o (SHRINE) ; avec la HWRAM de notre build 438 720 o (`_end` 0x06094e40, build/MAIN.map:2617) la marge grandit d'autant. Autre PS : `STATIC.DAT` = écran de chargement 77 320 + sprites VDP2 262 144 + 43 sons 316 872 + 40 tuiles d'armes 11 697 + séquences 10 474 = 678 511.

## 5. Duke Nukem 3D `.LEV` (30, 43 885 078 o) — moteur plus récent, layout différent [duke]
> **Correction 2026-09-10 (`[gen2]` = `build/tmp-gen2/gen2_*.py`)** : les éléments de 40 o ne sont pas des « murs » PowerSlave mais des **plans** au format de Quake (§5 bis) — 4 indices de coins, `nextSector` @8, flags @10, grille @14, plages quads/sommets @18-24, normale s2. f3 ×5 = quads (4 indices u1 locaux + texture), f4 = octets de la table des grilles (2 o/cellule : flip, texture), f5 ×44 = enregistrements de grille (w, h, vecteurs 16.16 base/horizontal/vertical), f6 = octets de lumière des grilles ((w+1)(h+1), valeurs 0-17), f7 ×8 = entités. Tous les indices plan→sommet/grille/quad sont dans les bornes sur les 30 niveaux (seules 2 411 plages de quads vides, codées `fin = début − 1`). Queue : sons, un bloc de 48 168 o, palette 512 o, puis le `loadTileSet` de PIC.C:671 — dans ABYSS1, 93 textures **64×64 4 bpp + CLUT 16 couleurs (type 0x82)**, puis un type non décodé (EOF pas encore atteint). **Duke et Quake partagent le même format « génération 2 »** sur le même moteur secteurs + portails que PowerSlave. Détail, mesures et conséquences : `Mimas/docs/LOBOTOMY_GEN2_FINDINGS.md`.

Même bloc ciel (palette, 512/256, bitmap, table 320 int **identique octet pour octet** à celle de PS TOMB.LEV), puis à 0x20708 une liste `int nBlocks ; nBlocks × {int size ; data}` (2 blocs, 3 pour FUSION2/LORD1/LORD2/STADIUM, 4688-5112 o chacun : rampes de remap 256 entrées, bloc 2 identique sur tous les niveaux) ; header 14 int à `hoff` (0x22E44-0x241E4). Tailles d'éléments par moindres carrés (résidu 4-7 o) : sector **28**, wall **40**, vertex 8, f3 ×5, f4 ×1, f5 ×44, f6 ×1, f7 ×8, f8 ×1, PB 18, PBWall 2, PBVert 4, cut 128, f13 = 0 ; ancre sons = int **293** (OT_NMTYPES Duke, 227 PS). `size` ≠ Σ (écart −1044…−47 012) : sens différent. Étiquettes sûres : sect/wall/vert/pb/pbW/pbV/cut/dynSnd ; f3-f8 non prouvées.

| LEV | octets | hoff | size | sect | murs | vert | f3 | f5 | PB | PBw | PBv | cut | snd dyn |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ABYSS1 | 1 582 760 | 142 988 | 628 010 | 459 | 3993 | 34 438 | 15 974 | 825 | 20 | 194 | 273 | 1 | 37 |
| ABYSS2 | 791 873 | 142 912 | 110 697 | 64 | 500 | 7040 | 3417 | 124 | 7 | 61 | 90 | 0 | 22 |
| BANKROLL | 1 579 481 | 143 020 | 614 403 | 481 | 4072 | 38 070 | 16 494 | 494 | 32 | 240 | 370 | 0 | 34 |
| DARK1 | 1 542 377 | 142 876 | 700 938 | **619** | **5266** | 42 494 | 16 587 | 520 | 43 | 272 | 457 | 0 | 30 |
| DARK2 | 1 485 563 | 142 988 | 444 973 | 433 | 3690 | 25 620 | 10 716 | 290 | 9 | 53 | 76 | 0 | 46 |
| DEATHROW | 1 598 552 | 142 968 | 625 953 | 476 | 4040 | 37 808 | 16 048 | 668 | 38 | 294 | 416 | 0 | 33 |
| FARNHEIT | 1 688 822 | 143 008 | 438 693 | 374 | 3205 | 26 522 | 10 517 | 316 | 24 | 210 | 316 | 0 | 49 |
| FLODZONE | 1 555 975 | 143 000 | 559 763 | 503 | 4169 | 30 558 | 13 324 | 626 | 18 | 129 | 182 | 0 | 38 |
| FUSION1 | 1 479 872 | 142 984 | 460 881 | 351 | 2974 | 28 504 | 12 401 | 310 | 22 | 125 | 232 | 0 | 43 |
| FUSION2 | 1 597 122 | 147 920 | 463 949 | 331 | 2887 | 27 864 | 12 114 | 386 | 19 | 121 | 226 | 0 | 47 |
| HOLYWOOD | 1 572 779 | 142 976 | 469 029 | 415 | 3592 | 28 962 | 11 163 | 248 | 23 | 159 | 255 | 0 | 37 |
| HOTELHEL | 1 583 795 | 143 032 | 486 163 | 402 | 3355 | 29 442 | 11 930 | 409 | 26 | 188 | 284 | 0 | 38 |
| INCUBATR | 1 559 078 | 142 900 | 553 554 | 438 | 3715 | 34 902 | 14 522 | 361 | 47 | 431 | 610 | 0 | 38 |
| LARUMBLE | 1 476 372 | 142 976 | 413 523 | 345 | 2887 | 24 692 | 10 605 | 348 | 11 | 72 | 116 | 0 | 41 |
| LORD1 | 1 475 991 | 147 936 | 367 317 | 341 | 2841 | 21 960 | 9332 | 236 | 15 | 107 | 154 | 0 | 43 |
| LORD2 | 854 778 | 147 708 | 97 915 | 77 | 647 | 5650 | 2594 | 114 | 4 | 28 | 37 | 0 | 26 |
| LUNAR | 1 597 874 | 142 968 | 563 241 | 503 | 4139 | 33 446 | 13 044 | 524 | 32 | 194 | 268 | 0 | 42 |
| MOVIESET | 1 391 371 | 143 028 | 321 790 | 252 | 2137 | 19 516 | 7874 | 312 | 26 | 210 | 283 | 0 | 40 |
| OCCUPIED | 1 573 899 | 142 992 | 479 723 | 320 | 2629 | 29 550 | 13 662 | 439 | 23 | 205 | 291 | 0 | 47 |
| RAWMEAT | 1 665 316 | 143 036 | 535 314 | 488 | 4086 | 32 020 | 12 603 | 332 | 44 | 320 | 493 | 0 | 42 |
| REDLIGHT | 1 595 804 | 142 980 | 477 560 | 415 | 3538 | 28 322 | 10 789 | 422 | 39 | 336 | 478 | 3 | 38 |
| SECRET1 | 1 507 767 | 142 988 | 536 226 | 471 | 3813 | 31 492 | 13 645 | 528 | 19 | 141 | 216 | 0 | 38 |
| SPACPORT | 1 358 020 | 142 900 | 520 752 | 540 | 4529 | 29 606 | 11 538 | 292 | 24 | 153 | 222 | 0 | 27 |
| STADIUM | 1 044 067 | 147 812 | 31 434 | 29 | 210 | 1140 | 575 | 67 | 0 | 0 | 0 | 0 | 36 |
| TIBERIUS | 1 655 112 | 142 992 | 588 472 | 504 | 4398 | 35 220 | 14 116 | 461 | 34 | 208 | 333 | 0 | 41 |
| TOXDUMP1 | 1 391 573 | 142 988 | 398 234 | 345 | 2732 | 23 648 | 10 288 | 391 | 15 | 115 | 156 | 0 | 39 |
| TOXDUMP2 | 1 532 726 | 142 984 | 575 410 | 464 | 3642 | 35 990 | 16 860 | 507 | 11 | 85 | 152 | 0 | 38 |
| TRANSIT | 1 497 493 | 143 028 | 370 498 | 262 | 2235 | 22 676 | 10 345 | 333 | 9 | 59 | 85 | 0 | 49 |
| UREA51 | 1 171 106 | 142 768 | 306 904 | 334 | 2558 | 12 104 | 4260 | 740 | 23 | 156 | 206 | 0 | 29 |
| WRPFACTR | 1 477 760 | 142 900 | 595 520 | 499 | 4007 | 36 524 | 15 970 | 462 | 34 | 246 | 387 | 0 | 27 |

DARK1 619 secteurs > `MAXNMSECTORS 600` (UTIL.H:21, UTIL/CONVERT.C:221) → convertisseur et moteur Duke révisés ; murs ≤ 5266 < 5500 ; tout `size` < 900 000.

## 5 bis. Quake (USA) — identité, inventaire, `.LEV` : autre classe de niveau [quake] (2026-09-10)
Scripts `[quake]` = `build/tmp-quake/quake_ip.py` (IP + conversion 2352→2048) et `quake_lev.py` (inventaire + parcours des `.LEV`).

| | Quake (USA) |
|---|---|
| Maker / produit / version / date IP | `SEGA ENTERPRISES` / `MK-081066` / `V1.019` / `19971113` |
| Device / zone / périph. / titre | `CD-1/1` / `U` / `EATJ` / `QUAKE` |
| Taille IP / 1re lecture / taille | 0xE2C = 3628 (= `USIP.BIN` 3628 o à la racine) / **0x06007000** / 0 (fichier entier) |
| Piste 01 (MODE1/2352) | 66 326 400 o = 28 200 secteurs, 0 secteur non-MODE1 ; 12 pistes (02-12 CDDA) |
| Programme | `0` à la racine, 386 644 o, **unique** (pas de MAIN.BIN, comme Duke) ; entrée `4f 22 d1 0e` = `sts.l pr,@-r15 ; mov.l @(d,pc),r1`, un prologue C — pas le talon `d0 01 40 2b` de START.S de PowerSlave |

VolumeSpaceSize déclaré 547 004 416 > ISO extrait 57 753 600 → même avertissement 7-Zip inoffensif que PS/Duke.

**Lignée** : chaîne `GFS_SBL Version 2.10 1996-02-01` dans `0` = **SBL 2.10, la même version que PowerSlave** ; fichiers ouverts avec la convention `+NOM` de PowerSlave (`+SEGA.PIC`, `+STATIC.DAT`, `+SKANK.DAT`, `+INITLOAD.DAT`…) ; crédit « David Lawson (BREW) » dans le texte. **`INITLOAD.DAT` est identique octet pour octet à celui de PowerSlave** (81 948 o, MD5 `afff1d33969c…`) — livré tel quel ; `STATIC.DAT` diffère (632 565 contre 678 511). Les 21 `.PIC` font chacun 77 320 o, la taille exacte de l'écran de chargement de `STATIC.DAT` PowerSlave.

**Inventaire** : 66 fichiers, 57 347 800 o — 35 `.LEV` 43 243 705 ; 4 `.DAT` 12 083 629 (`SKANK` 11 204 470, `STATIC` 632 565, `INTRO` 164 646, `INITLOAD` 81 948) ; 21 `.PIC` ; `QUAKE.SCR` 6 358 ; `USIP.BIN` ; 3 `NITS_*.TXT`. Niveaux = 30 d'épisodes (E1 ×7, E2 ×6, E3 ×6, E4 ×11) + `START`, `END`, `TITLE`, `CREDITS`, `TEST`.

**Format `.LEV`** : le descripteur `lev_quake.ksy` du projet SaturnQuakePC (`../saturn-refs/SaturnQuakePC/tools/kaitaistruct/`) est **validé sur tout le disque — 35/35 fichiers se terminent exactement à EOF**. Ciel 131 104 o (palette 16 couleurs + 64 × 2048), en-tête 15 u4 = 60 o à 131 104, puis nœuds 28 o, **plans 40 o** (4 indices de sommets, nœud, tuile, plage de quads, plage de sommets, normale + distance), tuiles 44 o (vecteurs base / horizontal / vertical = repère de texture affine), sommets 8 o, **quads 5 o** (4 indices u1 locaux à la plage du plan + texture), entités 4 o, polylinks 18 o, puis sons (PCM 11 025 Hz), palette, ressources (textures type 0x82 = palette 16 couleurs + 64×64 en 4 bpp), nom interne 32 o. **Ni secteur, ni mur, ni cutPlane** : ce n'est pas une variante du format PowerSlave (secteur 24 / mur 48 / face 10, tuiles 16 bpp RLE) comme Duke (§5), c'est une autre classe de niveau. Le chargeur et le renderer de ce dépôt n'ont aucune représentation pour des nœuds, des plans ou des quads.

> **Correction 2026-09-10 `[gen2]` — le paragraphe ci-dessus est FAUX sur la classe.** Les « nœuds » de 28 o sont des **secteurs** : centre @4 = centroïde des coins de leurs plans (médiane 0,4 u), champ @10 = altitude de sol (entre y min et y max des coins pour 18 967/19 093), plage premier/dernier plan @12/14 couvrant exactement tous les plans — la disposition de `sSectorType` (SLEVEL.H:181-190) plus 4 o. Le champ @8 des plans est `nextSector` : 75 512 plans invisibles (flag 0x2 = `WALLFLAG_INVISIBLE`) pointent vers un autre secteur valide, et les 75 442 liens sont tous réciproques. Les polylinks de 18 o sont les push-blocks (`sPBType`, 9 shorts), la queue « ressources » est le `loadTileSet` de PIC.C:671 (0x6a/0x6c/0x34 = TILEFLAG PowerSlave) plus un type 4 bpp 0x82. Quake Saturn tourne sur le **même moteur secteurs + portails**, au même format « génération 2 » que Duke (§5). Jusqu'à 865 secteurs (E4L5) contre `MAXNMSECTORS 600` ici. Vis-à-vis du moteur de ce dépôt (génération 1), Duke et Quake demandent **la même traduction** ; le code de jeu (monstres, armes) manque dans les deux cas.

| LEV | octets | nœuds | plans | sommets | quads | tuiles | ent | sons | res | tex | nom interne |
|---|---|---|---|---|---|---|---|---|---|---|---|
| CREDITS | 996 899 | 167 | 1420 | 6868 | 2251 | 143 | 123 | 38 | 73 | 49 | `TEST2` (comptes = E4L10, diffère dès l'octet 0 : ciel) |
| E1L1 | 1 269 449 | 603 | 5138 | 32 294 | 12 443 | 278 | 249 | 22 | 147 | 120 | |
| E1L2 | 1 363 688 | 640 | 5725 | 36 180 | 13 926 | 209 | 314 | 31 | 111 | 87 | |
| E1L3 | 1 321 209 | 653 | 5406 | 34 950 | 13 306 | 186 | 473 | 31 | 85 | 61 | |
| E1L4 | 1 212 130 | 601 | 4997 | 31 712 | 12 365 | 324 | 363 | 24 | 102 | 78 | |
| E1L5 | 1 355 182 | 592 | 5048 | 31 398 | 11 806 | 333 | 357 | 35 | 104 | 78 | |
| E1L6 | 1 231 850 | 442 | 3765 | 22 376 | 7072 | 160 | 272 | 38 | 101 | 75 | |
| E1L7 | 654 463 | 195 | 1625 | 7312 | 2413 | 222 | 91 | 16 | 60 | 36 | |
| E2L1 | 1 370 111 | 577 | 5022 | 33 618 | 13 134 | 323 | 283 | 26 | 135 | 109 | |
| E2L2 | 1 349 810 | 628 | 5260 | 33 452 | 12 265 | 304 | 300 | 36 | 86 | 62 | |
| E2L3 | 1 591 216 | 820 | 6762 | 35 676 | 11 892 | 428 | 395 | 41 | 95 | 71 | |
| E2L4 | 1 457 515 | 641 | 5396 | 32 924 | 11 774 | 452 | 397 | 43 | 69 | 44 | |
| E2L5 | 1 348 059 | 699 | 5863 | 35 092 | 12 727 | 487 | 324 | 32 | 88 | 62 | |
| E2L6 | 1 500 784 | 671 | 5753 | 35 778 | 11 812 | 362 | 473 | 41 | 84 | 60 | |
| E3L1 | 1 340 381 | 562 | 4904 | 32 822 | 12 863 | 508 | 277 | 26 | 127 | 101 | |
| E3L2 | 1 181 939 | 414 | 3589 | 20 010 | 5336 | 294 | 320 | 36 | 106 | 81 | |
| E3L3 | 1 218 201 | 538 | 4791 | 24 692 | 7741 | 233 | 292 | 37 | 80 | 57 | |
| E3L4 | 1 403 698 | 662 | 5665 | 31 884 | 10 474 | 511 | 370 | 40 | 82 | 59 | |
| E3L5 | 1 434 702 | 674 | 5737 | 30 760 | 10 934 | 605 | 377 | 36 | 106 | 81 | |
| E3L6 | 1 263 349 | 554 | 4792 | 25 102 | 8170 | 375 | 402 | 33 | 87 | 63 | |
| E4L1 | 1 281 308 | 610 | 5293 | 26 442 | 9085 | 330 | 213 | 30 | 127 | 101 | |
| E4L2 | 1 444 268 | 720 | 6621 | 32 210 | 10 159 | 365 | 369 | 34 | 87 | 63 | |
| E4L3 | 1 319 979 | 633 | 5133 | 28 228 | 9175 | 511 | 365 | 36 | 92 | 66 | |
| E4L4 | 1 406 130 | 598 | 5135 | 32 834 | 12 189 | 540 | 377 | 36 | 79 | 53 | |
| E4L5 | 1 441 031 | 865 | 7225 | 33 616 | 10 896 | 739 | 448 | 33 | 83 | 58 | |
| E4L6 | 1 149 961 | 456 | 4030 | 21 936 | 7335 | 506 | 305 | 30 | 100 | 76 | |
| E4L7 | 1 502 013 | 661 | 5803 | 28 932 | 9675 | 634 | 354 | 45 | 106 | 83 | |
| E4L8 | 1 259 942 | 621 | 5173 | 26 854 | 9164 | 505 | 481 | 30 | 88 | 63 | |
| E4L9 | 1 080 583 | 468 | 3893 | 19 840 | 6358 | 429 | 370 | 31 | 67 | 43 | |
| E4L10 | 982 815 | 167 | 1420 | 6868 | 2251 | 143 | 123 | 38 | 73 | 49 | |
| E4L11 | 1 029 717 | 467 | 4024 | 21 570 | 7050 | 369 | 291 | 22 | 65 | 40 | |
| END | 1 220 891 | 505 | 4239 | 20 210 | 7130 | 426 | 241 | 25 | 117 | 93 | |
| START | 1 068 245 | 763 | 6927 | 30 486 | 10 090 | 107 | 171 | 6 | 134 | 109 | |
| TEST | 607 847 | 124 | 1130 | 4634 | 1340 | 24 | 73 | 3 | 90 | 65 | |
| TITLE | 584 340 | 102 | 952 | 4068 | 1474 | 70 | 14 | 12 | 82 | 58 | |

## 6. Cartes Build (DOS 1.3D shareware) vs `.LEV` Saturn [bmap]
`UTIL/CONVERT.C` ne lit **pas** de `.MAP` Build : son entrée est un tableau C `#include LEVELFILE` (UTIL/DEXSHELL.C:160) aux types Lobotomy (DEXSHELL.H:106-160 : sommets 3-D, faces quad) ; chaque secteur reçoit ses murs **+ un « mur » sol + un « mur » plafond** (CONVERT.C:386, 622, 2064-2083). Source DOS : `3dduke13.zip` (archive.org, 5 924 374 o, sha1 72b83273…) → `DUKE3D.GRP` 11 035 779 o, cartes v7 LE.
| Niveau | Build sect / murs / sprites | Saturn | Saturn sect / murs (vert, faces) | murs − 2×sect |
|---|---|---|---|---|
| E1L1 Hollywood Holocaust | 317 / 1937 / 639 | HOLYWOOD | 415 / 3592 (28 962, 11 163) | 2762 |
| E1L2 Red Light District | 278 / 1757 / 834 | REDLIGHT | 415 / 3538 (28 322, 10 789) | 2708 |
| E1L3 Death Row | 478 / 3050 / 908 | DEATHROW | 476 / 4040 (37 808, 16 048) | 3088 |
| E1L4 Toxic Dump | 557 / 3437 / 1179 | TOXDUMP1 + TOXDUMP2 | 345 / 2732 + 464 / 3642 = 809 / 6374 | 4756 |
| E1L5 The Abyss | 479 / 3198 / 1068 | ABYSS1 + ABYSS2 | 459 / 3993 + 64 / 500 = 523 / 4493 | 3447 |
| E1L6 Launch Facility | 341 / 1924 / 727 | **SECRET1** (appariement ci-dessous) ; UREA51 334/2558 = exclusivité Saturn | 471 / 3813 (31 492, 13 645) | 2871 |

**Appariement géométrique mesuré 2026-09-10** (`build/tmp-gen2/duke_match.py` et `duke_match2.py` : méthode, orientations, vote de translation et témoin décalé de `tools/calibrate.py`, carte PC contre les sommets Saturn en (x, z) lus par le décodeur génération 2 du §5). Toutes les paires convergent vers **X = x/8, Z = −y/8** (échelle affinée 7,99-8,00), la règle R1 de `BUILD2DEX_CALIBRATION.md`.

| PC → Saturn | points PC à ≤ 4 u d'un sommet Saturn | murs PC retrouvés | murs Saturn sur une ligne PC | témoin décalé |
|---|---|---|---|---|
| E1L1 → HOLYWOOD | 51,8 % | 34,9 % | 34,4 % | 1,7 % |
| E1L2 → REDLIGHT | 30,0 % | 12,6 % | 19,1 % | 0,0 % |
| E1L3 → DEATHROW | 45,9 % | 27,4 % | 31,7 % | 0,7 % |
| E1L4 → TOXDUMP1 / TOXDUMP2 | 13,5 % / 21,5 % | 5,3 % / 13,4 % | 13,8 % / 17,6 % | 0,4 % / 0,7 % |
| E1L5 → ABYSS1 / ABYSS2 (échelle 1/8 imposée) | 13,4 % / 4,3 % | 8,7 % / 3,9 % | 9,9 % / 31,5 % | 2,3 % / 0,1 % |
| E1L6 → SECRET1 | 68,7 % | 53,4 % | 37,2 % | 1,1 % |
| E1L6 → UREA51 (échelle 1/8 imposée) | 2,4 % | 0,0 % | 0,7 % | 1,8 % |
| paires croisées (E1L1 → REDLIGHT, E1L2 → HOLYWOOD, E1L3 → ABYSS1) | 5,2-6,4 % | ≤ 0,1 % | ≤ 2,5 % | 2,0-5,5 % |

Lecture : **les niveaux Saturn de l'épisode 1 sont les cartes PC** (sauf UREA51, exclusif), retravaillées — E1L4 scindé, E1L5 le plus remanié. Les sommets Saturn en plus (Saturn → PC ≤ 10,5 %) viennent de la découpe convexe et de la grille 64. Données PC : `refs/build/duke13/` (3dduke13.zip retéléchargé, sha1 vérifié).

**Zones reliées par téléporteurs (mesuré 2026-09-10, `build/tmp-gen2/duke_components*.py`, vérification adversariale `duke_components_verif_*`)** : Build est un moteur à portails, mais une carte Duke est un archipel d'îlots que seuls les SE7/SE17 relient (E1L1 : le départ atteint 18 secteurs sur 317 par portails, 311 avec les téléporteurs ; E1L4 : 18 → 544). Lobotomy a remis les zones d'eau déconnectées **à leur place en vraie 3D** : surface et fond empilés, reliés par un portail horizontal à ≤ 12 u du sol Build du bassin (confirmé en 3D pour TOXDUMP c1/c4, SECRET1 c4/c6, DEATHROW c1). Le toit d'E1L1 est replacé en (4504, −3361). Hauteurs : **Y = −z/128** exactement, sans décalage. Couverture des murs PC avec un décalage par zone : TOXDUMP 21 → 45 %, SECRET1 54 → 67 %, DEATHROW +2 points, HOLYWOOD +1 ; REDLIGHT et ABYSS1 contiennent en plus des blocs déplacés à l'intérieur d'une même zone. Il reste entre ~33 % (SECRET1) et ~85 % (ABYSS) de murs PC non retrouvés (refonte, hauteurs modifiées, murs redécoupés). Détail : `docs/DUKE_PC_TO_SATURN.md`.
PowerSlave DOS (`STUFF.DAT`) : commercial seulement (GOG/Steam) ; démo libre `jonof.id.au/files/buildgames/pwrslave.zip` 12 370 108 o (supportée par PCExhumed) [bmap].

## 7. CDDA
**PowerSlave** : `playCDTrack` = `CDC_CdPlay` sur le numéro de piste (FILE.C:275-288) ; `trackMap[31]` indexé par le niveau de `levelGraph` (SOUND.C:369-380, BIGMAP.C:43-77). [src] + durées [ici] :
| Piste | enum | durée (octets) | utilisé par |
|---|---|---|---|
| 02 | S_KARNAK | 2:18.95 (24 510 192) | KARNAK, TOMB, TOMBEND |
| 03 | S_TRIBAL | 2:51.60 (30 270 240) | SANCTUAR, SETPALAC, SLAVCAMP, TEST |
| 04 | S_SELKIS | 2:05.84 (22 198 176) | SHRINE, THOTH |
| 05 | S_SWAMP | 2:06.51 (22 315 776) | MARSH |
| 06 | S_ROCKIN | 2:21.99 (25 046 448) | SETARENA, COLONY, SELBUROW |
| 07 | S_QUARRY | 2:59.56 (31 674 384) | PASS, PEAK, GORGE (+ QUARRY.LEV, absent du disque) |
| 08 | S_MAGMA | 2:32.08 (26 826 912) | CHAOS, SELPATH, MAGMA |
| 09 | S_SANCTUM | 3:27.36 (36 578 304) | `titleMusic` (SOUND.C:383) |
| 10 | S_ENDCREDIT | 5:05.07 (53 813 760) | `endMusic` (SOUND.C:384) |
| 11 | S_KILMAT1 | 4:15.49 (45 069 024) | KILENTRY (+ KILMAAT1-6.LEV, absents du disque) |
| 12 | S_KILMAT2 | 1:47.96 (19 044 144) | KILARENA |
| 13 | S_WATER2 | 2:16.61 (24 098 592) | MINES, CAVERN, SUNKEN |
| 14 | S_MAP | 1:28.60 (15 629 040) | `mapMusic` (SOUND.C:382) |
`firstVoiceTrack = S_MAP+1 = 15` (SOUND.C:385) : le disque n'a que 14 pistes et la variable n'est lue nulle part (grep : SOUND.C:385, SOUND.H:35 seulement) — les voix sont les fichiers `SP_*.LIP` (§2). 7 des 31 noms de `levelGraph` (QUARRY, KILMAAT1-6) sont dans `MAIN.BIN` retail mais absents du disque [ici].
**Duke** : 22 pistes CDDA. 07-20 = 14 musiques de 1:35.15 (13, 16 783 872 o) à 4:43.23 (12, 49 961 184 o). Pistes courtes : 02 1 105 440 (6.27 s), 03 1 058 400 (6.00), 04 1 058 400 (6.00), 05 1 185 408 (6.72), 06 1 152 480 (6.53), 21 1 832 208 (10.39), 22 1 768 704 (10.03), 23 2 991 744 (16.96) ; chacune = ~150 secteurs de silence (1er octet non nul ≈ 354 084 = pregap `INDEX 00 → INDEX 01 00:02:00` du .cue) puis du signal réel (RMS 5200-12 100 sur 16 bits) [ici] → [est] jingles courts (stingers de menu / d'épisode pour 02-06, fins / crédits pour 21-23) ; la source Duke n'étant pas dans l'arbre, aucune table `trackMap` ne le prouve.

## 8. `MAIN.BIN` retail vs `SRUINS.CPE` vs `build/MAIN.BIN` [ici]
Retail 316 716 o ; image du CPE 0x06004000-0x06053D40 (327 488 o, SETREG 0x58 = 0x0602F840 = crt0 SN) : **13 154 / 316 716 octets identiques au même offset**, mais 70 536 / 137 607 fenêtres de 16 o (pas 2) partagées → même code, relogé : deux builds différents. Entrée retail = START.S (`mov.l =0x0602d05c,r0 ; jmp @r0` ; INIT `0` : 0x0600b804). Aucune chaîne `__FILE__` (`*.c`) ni dans le CPE ni dans le retail, 26 dans `build/MAIN.BIN` (build debug, `assert` UTIL.H:73) — contredit PORTING_NOTES.md:14 (« CPE = debug builds, assert strings present »), à réconcilier. Les deux portent `GFS_SBL Version 2.10 1996-02-01` (retail 0x379C8). Lignée Duke [duke] : `DUKE/0` (444 408 o, chargé 0x06010000) partage 32 396 fenêtres de 16 o avec le MAIN.BIN PS, couverture 16,3 % — quasi exclusivement SBL + tables (`randTable` 8206 o, `sqrtTable` 4172 o, jump table INT 1568 o) ; région code jeu 13 202 / 229 376 o en comparaison tolérante aux relogements, plus longue suite 240 o ; même `GFS_SBL 2.10` ; boucle `executeLink` identique (pool 0x06004000 / 0x06001000).

## 9. `make iso` : fichiers nécessaires et ajout des pistes CDDA (décrit, pas construit)
Recette (Makefile:202-231) : tout `cd/*` sauf `README*`/`.gitkeep` → `build/iso/`, + `build/INIT.BIN` copié en **`0.BIN`** (1er fichier trié, chargé par l'IP générique SRL/SGL `IPFILE` Makefile:206 à 0x06004000 — comme l'IP retail §1) + `build/MAIN.BIN` (chargé par INIT via `link("+MAIN.BIN")` INITMAIN.C:608 ; MAIN relance `link("0")` SRUINS.C:2526/2530 → sur notre disque ce nom est `0.BIN`, à vérifier au runtime). Donc `cd/` = **les 96 fichiers de `refs/extract/PS` sauf `0` et `MAIN.BIN`** (168 068 233 o) ; SP_*.LIP, MOV et fichiers `J*` inclus tels quels (le jeu les nomme par `sprintf`). Pistes audio : le `.iso` produit est en secteurs 2048 ; la recette Mimas (`../Mimas/build.ps1:52-58, 249-253, 470-500` via `sox`) convertit chaque musique en `.raw` 2352 o/secteur et écrit un `.cue` multi-fichiers `FILE x BINARY / TRACK NN AUDIO / PREGAP 00:02:00 / INDEX 01`. Ici les `.bin` de `refs/iso/Powerslave (USA)/` sont **déjà** de l'audio brut 2352 o/secteur avec le pregap inclus (`INDEX 00 00:00:00 / INDEX 01 00:02:00`, `.cue` retail) : il suffit d'un `.cue` `FILE slavedriver.iso BINARY / TRACK 01 MODE1/2048` suivi des 13 lignes `FILE "Powerslave (USA) (Track NN).bin" BINARY / TRACK NN AUDIO / INDEX 00 00:00:00 / INDEX 01 00:02:00` (NN = 02-14, numéros conservés pour `trackMap`) — le tout sous `build/` ou `cd/`, jamais suivi.

## 10. `cd/` préparé et ISO construit [ici]
`git check-ignore -v cd` → `.gitignore:5:cd/` (ligne ajoutée ce jour). `cd/` = 96 fichiers, 168 068 233 o. `powershell -ExecutionPolicy Bypass -File build.ps1 iso` → **`build/slavedriver.iso` = 168 957 952 o**, 98 fichiers / 168 473 273 o, volume `SLAVEDRIVER`, système `SEGA SATURN` : `0.BIN` 116 980 (INIT.BIN), `MAIN.BIN` 288 060 (vs retail 142 716 / 316 716), puis les 96 fichiers de données aux tailles retail (BAD.MOV 6 096 652 … TOMBEND.LEV 1 305 382, 24 LEV, 56 LIP, 3 MOV, 4 PCS, 5 DAT, BONUS.BIN, 3 NITS). Non testé au runtime (PORTING_NOTES.md « Not proven: runtime »).

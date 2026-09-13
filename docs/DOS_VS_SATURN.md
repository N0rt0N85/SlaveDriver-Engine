# Exhumed DOS vs PowerSlave Saturn — synthèse pour build2dex

> **2026-09-10 (décision owner) : la cible de build2dex n'est plus PowerSlave DOS** — aucune paire
> DOS ↔ Saturn n'existe — **mais Duke Nukem 3D PC → Saturn**, dont les cartes de l'épisode 1 sont
> celles du Saturn (mesuré : `RETAIL_DISCS.md` §6). Ce document reste la référence sur les différences
> de moteur et de gameplay PowerSlave ; son §5 (conversion des cartes DOS) est périmé.

Synthèse de quatre recherches (niveaux, moteur, gameplay, histoire), 2026-08-31.
Convention : `fichier:ligne` sans préfixe = racine du dépôt `SlaveDriver-Engine` **[local]** ;
`[ex]` = `refs/build/NBlood/source/exhumed/src/` (source reverse du jeu DOS, PCExhumed) ;
`[jf]` = `refs/build/jfbuild/src/` ; `[web:URL]` = source web. « [mesuré] » = extrait de
`build/tmp-b2d/dos_stats.json` / `sat_stats.json` / des `.LEV` de `refs/extract/PS/` via `tools/lev.py`.

**Verdict amont, qui cadre tout le document** : aucun des 24 `.LEV` Saturn n'est une conversion
d'un `.MAP` DOS — ≤ 3,8 % de segments Build retrouvés, témoin 0 % ; les niveaux Saturn ont été
**redessinés dans Brew** [local: docs/BUILD2DEX_CALIBRATION.md §0, §3 ; docs/LEVEL_PIPELINE.md §1].
build2dex fera donc quelque chose que Lobotomy n'a jamais fait pour PowerSlave — mais qu'elle a
fait pour Duke Nukem 3D Saturn (importeur Build→Brew + « substantial reworking »)
[web: https://web.archive.org/web/20081226072803/http://curmudgeongamer.com/article.php?story=20021008212903265].

## 0. TL;DR — les grandes différences

| Axe | Exhumed DOS (Build v6) | PowerSlave Saturn (SlaveDriver) | Impact build2dex |
|---|---|---|---|
| Niveaux | 33 cartes (LEV0 training, LEV1-20 solo, LEV21-32 deathmatch) | 24 `.LEV` sur disque US (dont TEST=SANCTUAR), 31 slots de graphe, 7 coupés | Zéro carte commune ; tout est à convertir |
| Progression | Strictement linéaire, `levelnew = levelnum+1` [ex: exhumed.cpp:1408-1414] | Graphe 2D 4-directions + carte-monde, sorties = chameaux orientés [local: BIGMAP.C:43-81, AI.C:5108-5127] | Synthétiser chameaux + patcher `levelGraph` (compilé dans MAIN.BIN) |
| Secteurs | Polygones 2D concaves extrudés, sols/plafonds plats | Polyèdres **convexes 3D**, face = quad + équation de plan [local: SLEVEL.H:150-191] | Découpe convexe ×1,6-2,9 ⇒ aucune carte solo ne rentre sans scission |
| Empilement | ROR simulé (sprites lotag 80/99) [ex: init.cpp:952-958] | Natif (secteurs superposés + portails horizontaux) | Synthèse eau/superposition (R9, R14) |
| Pentes | 0 sur les 33 cartes [mesuré] | Natives (plans arbitraires) | Rien à faire (R13) |
| Lumière | `shade` par mur/secteur + palookup [jf: engine.c:772,789] | **Par sommet** (gouraud) + 15 lumières ponctuelles + torches [local: SLEVEL.H:115-118, WALLS.C:636-677] | Convertir shade→light sommet + poser des torches, sinon niveau plat |
| Rendu | Portal-flood software 8 bpp, colonnes | VDP1 quads distordus 16 bpp, peintre loin→près, tuiles 64×64 | `xrepeat/panning` perdus ; ≤126 tuiles/niveau ; cut-planes à générer |
| Capacités joueur | Pas d'artefacts ; vies + checkpoints | 6 artefacts (saut, plané, eau, lave, champs de force, lévitation) ; pas de vies | Incohérences de gating à anticiper |
| Armes/munitions | 7 armes + mains momifiées ; munitions par arme | 8 armes (+ Manacle) ; orbes de munitions universelles | Remap des pickups |
| Ennemis | lion, rat, roach, snake, lavadude, rex en exclusivité | Bastet, Sentry, Magmantis, blob, glowworm en exclusivité | Table de remap obligatoire |
| Multi | 12 cartes deathmatch dédiées | **Aucun multijoueur** (hors Death Tank caché) | Les cartes DM = bancs d'essai idéaux du convertisseur |
| Assets | ART global 8 bpp + VOC dans STUFF.DAT | `.LEV` autonome : tuiles RLE 16 bpp, ciel, palettes, PCM par niveau, < 900 000 o [local: LEVEL.C:41-42] | Le convertisseur produit un fichier autonome complet |

## 1. Inventaire des niveaux

### 1.1 Saturn — 31 slots, 24 fichiers

`levelGraph[NMLEVELS]` avec `NMLEVELS 31` [local: BIGMAP.C:43-81, GAMESTAT.H:30] ; 24 `.LEV`
sur le disque US, `TEST.LEV` identique octet pour octet à `SANCTUAR.LEV`
[local: docs/RETAIL_DISCS.md §2]. Noms affichés = 22 chaînes d'`INITLOAD.DAT` (indices 0-21)
[local: BIGMAP.C:355-359, LOCAL.C:23-38].

- **Départ** : TOMB (idx 3) seul déverrouillé [local: BUP.C:196-197].
- **Hub** : KARNAK (idx 0), 4 chameaux vers TOMB/PASS/MINES/SANCTUAR [mesuré].
- **Coupés (absents du disque, traces dans le binaire)** : QUARRY (idx 14, nom « Forgotten
  Quarry » encore dans INITLOAD.DAT, position carte {417,431}) et KILMAAT1-6 (idx 23-28)
  [local: BIGMAP.C:30-34 `levelPos` ; RETAIL_DISCS.md ligne 138] ; pistes CDDA encore assignées
  [local: SOUND.C:369-380] ; cheat « kilmaat » [local: BIGMAP.C:130-134, GAMESTAT.H:53,
  AI2.C:208]. Le vieux build japonais (OLDJAP/) embarque le même graphe [mesuré: diff vide].
- **Cas spéciaux** : TEST (idx 22) injouable — 0 arête, ses 2 chameaux résolvent à −1 =
  « reste ici » [mesuré] ; KILARENA (idx 29, boss final) atteint par **téléporteur** à
  `toLevel` absolu depuis KILENTRY [mesuré ; local: AI.C:5786] ; TOMBEND (idx 30) scripté
  (action 6 « got mummy ») [local: SRUINS.C:2531-2547].
- [web: https://powerslave.wiki.gg/wiki/Levels] concorde avec les 21 noms ; le remaster
  Nightdive n'expédie ni TEST ni les niveaux coupés
  [web: https://steamcommunity.com/sharedfiles/filedetails/?id=2751498248].

### 1.2 DOS — 33 cartes dans STUFF.DAT

Chargées par `lev%d.map`, format Build **v6** [ex: init.cpp:151 ; mesuré: dos_stats.json].
**LEV0** = Training (la finir → `levelnew=100` = fin) [ex: exhumed.cpp:2365, 2754-2760,
1437-1441]. **LEV1-20** = solo (plaques `mapNamePlaques[20]` [ex: menu.cpp:582-604] ;
Karnak = LEV16, seul nom commun avec Saturn — zéro appariement géométrique).
**LEV21-32** = deathmatch : exactement les 12 cartes portant des sprites lotag 62 = spawns
réseau [ex: init.cpp:1089-1096 ; mesuré: 5/4/3/7/7/7/7/7/3/3/3/3 spawns, zéro dans LEV0-20].

**Bilan** : aucune carte n'existe des deux côtés ; 7 slots Saturn n'existent nulle part —
ils sont **réutilisables** pour des niveaux convertis (mais `levelPos` 25-29 = {0,0} et pas
de nom au-delà de l'index 21).

### 1.3 Échelle et contenu géométrique [mesuré]

Unités : 1 u Saturn = 1 texel = 16 u Build en xy = 256 en z [local: BUILD2DEX_CALIBRATION.md §4, R1].

| | horizontal max (u Saturn) | vertical (u Saturn) |
|---|---|---|
| Saturn (23 niveaux) | 1920 / méd. 4608 / 7808 (KARNAK) | 971 / méd. 1888 / 9587 (MAGMA) |
| DOS solo LEV1-20 (÷16, ÷256) | 2688 / méd. 6908 / 7920 | 392 / méd. 1626 / 4980 |
| DOS DM LEV21-32 | 2033-3515 | 380-964 |

Les cartes solo DOS sont ~1,5× plus étendues que la médiane Saturn mais tiennent dans les
`short` Dex. Le mur réel est le **cap de secteurs** : la découpe convexe donne 876-1683
morceaux pour LEV1-20 contre `MAXNMSECTORS 600` / `MAXNMWALLS 5500` [local: UTIL.H:20-21] et
900 000 o par `.LEV` [local: LEVEL.C:41-42] — **aucune carte solo ne rentre sans scission** ;
seules LEV0/10/19 et les 12 DM passent [local: BUILD2DEX_CALIBRATION.md §5.8].

## 2. Moteur

### 2.1 Géométrie

- **Build v6** : secteur = polygone 2D possiblement concave et multi-boucles, extrudé entre
  `floorz`/`ceilingz` (multiples de 1024 sur toutes les cartes DOS) ; mur = segment à 1-2
  faces implicites via `nextsector`. Version **antérieure** à celle de Duke — pas de pentes
  [web: https://en.wikipedia.org/wiki/PowerSlave « slightly earlier version of the engine »].
- **SlaveDriver** : secteur = **polyèdre convexe 3D** ; chaque face (murs latéraux, sol,
  plafond) = quad `v[4]` + équation de plan `normal·p+d=0` en 16.16, normale vers l'intérieur
  [local: SLEVEL.H:150-171 `sWallType`, 182-191 `sSectorType`]. Une arête XZ porte 2 à 4 murs
  empilés (bas plein / portail `INVISIBLE` / haut plein), symétriques à 100 % chez le voisin
  [local: BUILD2DEX_CALIBRATION.md §5.3-4 ; CONVERT.C:1783-1784]. Empilement vertical natif
  (SUNKEN : 510 paires superposées dont 499 eau) et portails **horizontaux** sur sol/plafond.
  Pentes natives (5-307 secteurs pentus par niveau Saturn).

### 2.2 Rendu

- **DOS** : portal-flood software 2.5D (`drawrooms`→`scansector`), clip vertical par colonnes,
  8 bpp [jf: engine.c:5606, 5686, 469]. 320×200 VGA (+ VESA 640×400)
  [web: https://www.pcgamingwiki.com/wiki/PowerSlave]. Tic logique 30 Hz [ex: exhumed.h:38].
- **Saturn** : chaque tuile 64×64 de mur = un **distorted sprite** VDP1 `FUNC_DISTORSP`,
  RGB 16 bpp, `DRAW_GOURAU`, `HSS_ENABLE` [local: SPR.C:246-260, WALLS.C:1082-1086] ; things =
  sprites échelle + ombres `COMPO_SHADOW` [local: WALLS.C:2654-2665, 2587] ; ciel = VDP2 RBG0
  bitmap 512×256 [local: PLAX.C:84-115]. **Peintre loin→près + user-clip rectangulaire par
  secteur** [local: WALLS.C:2248-2255], tri fin par `cutPlane` pré-calculés ; **pas de far
  clip** (`ENABLEFARCLIP 0`, WALLS.C:28 — le « far clip 1024 » cité ailleurs est FAUX
  [local: docs/HW_USAGE_VS_MIMAS.md]). 320×240 non entrelacé, gouverneur 1↔2 vblanks
  (60/30 fps) [local: SRUINS.C:1878, 2239-2266] ; « near-consistent 30fps »
  [web: https://www.hardcoregaming101.net/powerslave-console/].
- **Budgets silencieux** : 1300 polys slave [local: WALLS.C:1278-1279], 1448 cmds VDP1 × 2
  banques + 1224 gouraud [local: SRUINS.C:1879-1880] — dépassés, ils **tronquent sans
  avertir** [local: docs/HW_USAGE_VS_MIMAS.md pt 7].
- **Quads only** : le VDP1 ne dessine que des quadrilatères ; les triangles sont des quads
  dégénérés (22 169 faces à sommet répété dans les .LEV retail)
  [web: Curmudgeon 2002 ; local: BUILD2DEX_CALIBRATION.md §5.1]. Pas de correction de
  perspective ⇒ murs = mailles de quads ⇒ « huge open areas just can't work » (Dreisbach) —
  Lobotomy coupait ou remplaçait ces niveaux (Ziggurat Vertigo ; un secret de Duke)
  [web: Curmudgeon 2002].

### 2.3 Lumière

- **DOS** : `shade` signé −128..127 + tables `palookup` par distance [jf: engine.c:772, 789] ;
  la lumière « dynamique » est un flash de shade scripté [ex: lighting.cpp:213-256].
- **Saturn** : lumière **par sommet** (`sVertexType.light`, `sector.light` −16..16)
  [local: SLEVEL.H:115-118, 186] → gouraud 5 bits/canal + jusqu'à 15 lumières ponctuelles
  [local: WALLS.C:636-677] ; torches = objets `OT_TORCH1..38` [local: SLEVEL.H] ; fichier
  `.lit` d'un éditeur dédié + radiosité offline [local: UTIL/LCONV.C, CONVERT.C:409-430].
  « The walls in PowerSlave were already being gouraud shaded for the static torch lights »
  (Dreisbach) [web: Curmudgeon 2002] — le gouraud est la **signature visuelle** du jeu.

### 2.4 CPU, sons, assets

- Slave SH-2 = co-processeur géométrie des murs (records 28 o via alias non-caché, équilibrage
  ±1 secteur/frame) [local: WALLS.C:1806-1819, 1921, 1936-1948, 2281-2285] — transparent pour
  le format de niveau.
- **SFX** : DOS = `.voc`/`.wav` globaux chargés par nom [ex: sound.cpp:503, 563] ; Saturn =
  **PCM embarqués par niveau** (4-28 sons dynamiques par `.LEV`, big-endian) + 43 statiques
  dans STATIC.DAT, cap 80 [local: SOUND.C:231-241, 40 ; RETAIL_DISCS.md §4] ; le 68000 est
  parqué et le SH-2 poke les slots SCSP en direct [local: MEGAINIT.C:221-223, SOUND.C:207-223].
- **Musique** : DOS = CDDA `(nTrack % 8) + 11` [ex: exhumed.cpp:2556, cd.cpp:52] ; Saturn =
  CDDA + `trackMap[31]` par niveau, **table compilée dans MAIN.BIN** [local: SOUND.C:369-385].
- **Assets** : DOS = ART global 8 bpp + PALETTE.DAT (GRP) ; Saturn = chaque `.LEV` embarque
  ciel (palette + bitmap 131 072 o + table), tuiles RLE 16 bpp (226-652), palettes, séquences,
  sons [local: PIC.C:611-716, SEQUENCE.C:24-89 ; RETAIL_DISCS.md §4], taille < 900 000 o
  [local: LEVEL.C:41-42], minimum libre retail observé 21 328 o (SHRINE).

## 3. Gameplay et progression

### 3.1 Structure

- **DOS** : linéaire ; carte = menu vertical de sélection (rejouable ≤ nBestLevel+1,
  auto-chargement après 12 s) [ex: menu.cpp:656 sqq.] ; cinémas après LEV10/15/20
  [ex: menu.cpp:2288-2295] ; LEV20 = finale minutée [ex: exhumed.cpp:1687-1704] ;
  « backtracking is not present » [web: https://en.wikipedia.org/wiki/PowerSlave].
- **Saturn** : carte-monde persistante en graphe (arêtes `{up,right,down,left}` complétées au
  runtime) [local: BIGMAP.C:42-101] ; sortie = **chameau** dont l'angle (0/1024/2048/3072)
  choisit la direction d'arête, `toLevel = getMapLink(...)` — **la destination n'est pas dans
  le `.LEV`**, elle est dérivée du graphe compilé [local: AI.C:5108-5127 ; vérifié: 42/42
  chameaux collent au graphe]. Toucher le chameau déverrouille la destination
  (`LEVFLAG_CANENTER`), remet la vie à ≥200 et **sauvegarde automatiquement**
  [local: SRUINS.C:2287-2292, 2483-2491]. Backtracking incité par le graphe : 8 pièces
  d'émetteur (`tmitLevels`), 23 poupées, bols de sang [local: BIGMAP.C:217-224,
  SLEVEL.H:48-49, 74-78, SRUINS.C:1725-1727, 2519-2523].

### 3.2 Les 6 artefacts (consoles uniquement — rien de tel en DOS)

| Artefact | Effet [local] |
|---|---|
| Sandals of Ikumptet | saut `SANDALJUMPVEL` — SRUINS.C:613-617 |
| Sobek Mask | respiration sous l'eau (sans : dégâts immédiats) — SRUINS.C:1473, 1526 |
| Shawl of Isis | plané (chute plafonnée saut maintenu) — SRUINS.C:622-624, 648-650 |
| Protective Anklets | lave 20→2, sol brûlant 20→0 — SRUINS.C:182-197 |
| Kilmaat Scepter | dissout les champs de force — AI.C:4874-4890 |
| Horus Feather | lévitation — SRUINS.C:638-646 |

Ordre canonique = discours de Ramses [local: AI2.C:176-211] ; la plume gate Kilmaat Haunt
[local: AI2.C:207-211] ; le téléporteur vers KILENTRY donne d'office les 6 artefacts
[local: SRUINS.C:2496-2500]. En DOS ces capacités n'existent pas
[web: https://en.wikipedia.org/wiki/PowerSlave].

### 3.3 Armes, munitions, systèmes

- **Armes** : Saturn = sword/pistol/M60/grenade/flamer/cobra/ring + **Manacle** (WP_RAVOLT)
  [local: GAMESTAT.H:18-27, WEAPON.C:63-73] ; DOS = mêmes 7 sans Manacle, + mains momifiées
  [ex: gun.h:31-42] (la séquence `kSeqRavolt` traîne en vestige [ex: sequence.h:97]).
  Munitions max japonaises plus généreuses [local: WEAPON.C:63-86].
- **Munitions** : DOS par arme ; consoles = orbes universelles (OT_AMMOBALL/ORB/SPHERE, un
  seul type) [local: SLEVEL.H:28-34 ; web: https://www.hardcoregaming101.net/powerslave-console/].
- **Vies** : DOS = 3 vies + checkpoints scarabées [ex: player.h:42-43] ; Saturn = **aucune
  vie**, santé max = `nmBowls*200`, +200 par Blood Bowl (un par niveau)
  [local: BUP.C:189, SRUINS.C:1721-1728].
- **Sauvegarde** : DOS auto entre niveaux + checkpoints ; Saturn = backup RAM 6 slots, auto
  au chameau, pas de mots de passe [local: BUP.C:23, SRUINS.C:2491, MENU.C].
- **Clés** : Saturn = 4 types bug/time/X/plant + portes assorties [local: SLEVEL.H:28,
  SRUINS.C:1660-1690].
- **Power-ups** : items magiques DOS (torche, invincibilité, double dégâts, invisibilité,
  cœur) [ex: items.h:27-34] absents ; Saturn n'a que invisibilité / boost d'arme /
  All-Seeing Eye [local: SRUINS.C:1590-1604].
- **Ennemis** : DOS-seulement = lion, rat, roach, snake, lavadude, rex ; Saturn-seulement =
  Bastet, Kilmaat Sentry, Magmantis, glowworm, blob ; boss partagés = Set, Selkis, Queen
  [local: SLEVEL.H:26-56 ; ex: fichiers par ennemi].
- **Multi** : DOS = deathmatch null-modem/modem/réseau, 12 cartes dédiées
  [ex: exhumed.cpp:1937-1989] ; Saturn = **aucun multijoueur** (un seul pad lu, un seul
  `constructPlayer`) [local: V_BLANK.C:58-77, OBJECT.C:202-203] — SETARENA/KILARENA sont des
  arènes de boss solo, pas du DM. Seul multi Saturn = Death Tank caché (2-7 joueurs)
  [local: INTRO.C:494-499, BUP.C:216-220 ; web: https://en.wikipedia.org/wiki/Death_Tank].
- **Fins multiples** Saturn : BAD/GOOD/SUPERGOOD selon transmetteur (8 pièces) et 23 poupées
  [local: SRUINS.C:2519-2529, GAMESTAT.H:20, 56].

## 4. Histoire du développement

- **Studio** : Lobotomy Software, fondé le 13 janvier 1993 par des ex-Nintendo (Paul Lange,
  Brian Anderson), Redmond WA [web: https://en.wikipedia.org/wiki/Lobotomy_Software ;
  https://lobotomysoftware.wordpress.com/].
- **Genèse** : le projet naît sur PC/Build sous le titre « **Ruins: Return of the Gods** »
  (chez 3D Realms, lâché, repris par Playmates) [web: https://en.wikipedia.org/wiki/PowerSlave].
  Vestige [local] : le fichier principal Saturn s'appelle **SRUINS.C** (= Saturn Ruins)
  [local: docs/PORTING_NOTES.md:24].
- **Qui** : Brian McNeely = directeur/designer ; **Ezra Dreisbach = unique programmeur de la
  version Saturn** et auteur du SlaveDriver [web: Curmudgeon 2002 ; local: README.md:4] ;
  Jeff Blazier = programmeur PSX ; Scott Branston = musique. Le crédit Wikipedia
  « Programmer: Ezra Dreisbach » global écrase la réalité par-version (Dreisbach dit avoir
  été embauché pour la Saturn) [web: Curmudgeon 2002].
- **Aucun code partagé DOS↔consoles** : « Both games were pretty much rebuilt from the ground
  up. There is no shared code at all » (Dreisbach) [web: Curmudgeon 2002] — cohérent avec le
  0 % d'appariement géométrique mesuré [local: BUILD2DEX_CALIBRATION.md §0].
- **Brew** : outil interne Windows ; niveaux PowerSlave et Quake Saturn faits à la main
  dedans ; **pour Duke, importeur Build→Brew** + retravail substantiel [web: Curmudgeon 2002].
  Traces [local] : magic `BR`, « fix for brew output doorways wrong »
  [local: CONVERT.C:652, 3609 ; docs/LEVEL_PIPELINE.md].
- **Ordre** : développement commencé sur PC/Build (Ruins), mais **la Saturn sort la
  première** (EU « Exhumed » 19 sept. 1996, JP 29 nov. 1996, US 31 déc. 1996) ; DOS janv.
  1997 ; PSX mars 1997, niveaux re-redessinés depuis la base Saturn
  [web: https://en.wikipedia.org/wiki/PowerSlave]. ⚠ Contradictions relevées : la page
  Wikipedia *Lobotomy Software* donne « 26 sept. 1996 tri-plateforme » (moins fiable) ;
  HG101 dit « PC first » (vrai pour le début du développement, faux pour la sortie).
  Le mastering du disque US au 1996-10-08 [local: docs/RETAIL_DISCS.md:8,11] corrobore la
  chronologie par-version.
- **Suite** : sur la force du moteur, Sega contracte Duke 3D Saturn (oct. 1997) et Quake
  Saturn (nov.-déc. 1997) — refontes intégrales coûteuses ; rachat par Crave en 1998, puis
  fermeture [web: https://en.wikipedia.org/wiki/Lobotomy_Software ; Unseen64].
- **Design console voulu, pas subi** : McNeely — mouvement « jog », « soaring jumps »,
  artefacts inspirés de **Metroid**, carte-monde : « a great opportunity to do something
  different with the console versions »
  [web: https://www.unseen64.net/2015/03/09/interview-brian-mcneely-lobotomy-software/].
- **Postérité** : source SlaveDriver publiée le 16 août 2025 (GPL-3.0, approbation
  Dreisbach) — notre dépôt en est le fork [local: README.md:4, 27-30 ;
  web: https://github.com/Lobotomy-Software/SlaveDriver-Engine] ; remaster Nightdive 2022 =
  fusion PSX+Saturn, **pas** le jeu DOS
  [web: https://store.steampowered.com/app/1678430/PowerSlave_Exhumed/] ; le DOS vit dans
  PCExhumed/NBlood — notre référence [ex] [web: https://pcex.retrohost.net/].

## 5. Conséquences pour build2dex

### 5.1 Ce qu'un niveau DOS converti reproduira tel quel

- La **géométrie 2.5D** : empreintes, hauteurs (multiples de 1024 → plans horizontaux
  exacts), portails verticaux lower/portal/upper (R6), bbox dans les `short` Dex (R1).
- Les **pentes** : non-problème — 0 pente dans les 33 cartes DOS [mesuré], le format Dex
  accepte des plans arbitraires (R13).
- Les **textures** en tant qu'images (après découpe 64×64 et conversion 16 bpp), le placement
  des ennemis/pickups **après remap** (§5.4), les portes et ascenseurs via les règles R1-R14
  existantes [local: docs/BUILD2DEX_CALIBRATION.md §7].
- Le **flux de jeu intra-niveau** : combat, clés→portes (4 types mappables), secrets
  géométriques.

### 5.2 Ce qui manquera (perdu ou sans équivalent)

| Perte | Détail | Source |
|---|---|---|
| Progression/pouvoirs | Le `.MAP` DOS n'encode ni artefacts ni gates ; la campagne DOS suppose un joueur « complet » dès LEV1 | §3.2 |
| Lumière par sommet | `shade` est par-mur/secteur ; pas de `.lit`, pas de radiosité — conversion naïve = niveau plat sans la signature gouraud | §2.3 |
| `xrepeat/yrepeat/panning/overpicnum` | Sans équivalent : une face Dex = une tuile 64×64 étirée (R8) | rapport moteur |
| Flash de lumière scripté | Code DOS (lighting.cpp), pas des data — non transposable en data | [ex: lighting.cpp:213-256] |
| Items magiques DOS | torche, invincibilité, double dégâts, cœur : pas d'ObjectType Saturn | [ex: items.h:27-34] |
| Vies/checkpoints | Scarabées/1-up sans équivalent ; la mort Saturn renvoie à l'entrée du niveau | §3.3 |
| 6 ennemis DOS | lion, rat, roach, snake, lavadude, rex : aucun ObjectType | §3.3 |
| Deathmatch | Aucun multi Saturn : les spawns lotag 62 sont morts | §3.3 |
| Sons DOS tels quels | `.voc` globaux ≠ PCM embarqués par niveau, cap 80 | §2.4 |

### 5.3 Ce que le convertisseur doit SYNTHÉTISER

1. **Découpe convexe + scission** : ×1,6-2,9 secteurs ; caps 600/5500 et 900 000 o ⇒ scinder
   toute carte solo (précédent : Duke Saturn E1L4/E1L5 scindés [local: RETAIL_DISCS.md §6]).
   Mesurer l'« ouverture » (grands volumes = mailles de quads) et flaguer/segmenter — leçon
   Ziggurat Vertigo [web: Curmudgeon 2002].
2. **Sorties** : la fin DOS est implicite (`levelnum+1`) ; **fabriquer 1+ OT_CAMEL** par
   niveau, angle cohérent avec la direction d'arête (sinon `toLevel<0` = boucle sur place
   [local: AI.C:5125-5126]) ; c'est le chameau qui déverrouille, soigne et sauvegarde.
3. **Graphe** : `levelGraph`/`levelPos`/`trackMap`/noms INITLOAD.DAT sont **compilés dans
   MAIN.BIN**, pas des données du disque ⇒ patcher BIGMAP.C/SOUND.C et rebuilder (on a la
   source). Plaquage naturel : chaîne linéaire (chameau right→n+1, left→retour). 7 slots
   libres (14, 23-28) disponibles sans toucher NMLEVELS.
4. **Éclairage** : `shade` → lumière par sommet (proposition R11 :
   `light = clamp(80 − shade, 0, 128)`, 80 = défaut CONVERT.C:557) + **poser des
   OT_TORCH*/OT_LIGHT** là où le DOS a des sprites de flamme — obligatoire pour la signature
   visuelle, pas une option (§2.3).
5. **Eau/superposition** : traduire chaque paire ROR lotag 80/99 en deux secteurs superposés
   + portail horizontal + flags `SECFLAG_WATER` (R9) ; générer les cut-planes pour toute
   superposition (R14, `MAXCUTSECTORS 128` [local: SLEVEL.H:173]).
6. **Ciels** : plafonds parallaxe (2331 au total en DOS [mesuré]) → murs `PARALLAX` sans
   faces + fournir un bitmap ciel 512×256 par niveau (R7 ; [local: PLAX.C:84-115]).
7. **Tuiles** : découper les picnums en sous-tuiles 64×64, ≤ 126 entrées `.til`
   (`signed char`, [local: CONVERT.C:658]) ; convertir 8 bpp ART → RLE 16 bpp.
8. **Sons** : générer le bloc PCM dynamique du `.LEV` depuis les OT_* réellement posés
   (mapping OT→sons), conversion VOC→PCM BE (règle rate [local: CONVERT.C:3780-3812]) — ou
   voie rapide : recopier le bloc sons d'un `.LEV` retail [local: docs/LEVEL_PIPELINE.md §3].
9. **Collectibles** : 1 OT_BLOODBOWL (+200 vie) et 1 OT_PYRAMID par niveau pour la boucle
   native ; dolls/pièces d'émetteur optionnelles.
10. **Remaps** : munitions DOS → OT_AMMOBALL/ORB/SPHERE ; clés DOS → bug/time/X/plant ;
    ennemis DOS-only → table (lion→BASTET, lavadude→MAGMANTIS, rat/roach→SPIDER/WASP…) ;
    items magiques → OT_INVISIBLEBALL/OT_WEAPONPOWERBALL/OT_EYEBALL ou suppression ;
    spawns DM lotag 62 → un start + un chameau.
11. **Budget** : simuler l'allocateur (< 900 000 o, minimum libre retail 21 328 o) et les
    budgets runtime (1300 polys slave, 1448 cmds VDP1) qui tronquent silencieusement.

### 5.4 Incohérences de gameplay à anticiper

- **Gating inversé** : un niveau DOS est finissable sans artefact — converti tel quel dans
  une campagne Saturn, il n'exerce aucun pouvoir ; inversement, s'il contient eau profonde,
  lave ou grands dénivelés, un joueur Saturn **sans** Sobek Mask/Anklets/Sandals y subit des
  dégâts ou des impasses que le design DOS ne prévoyait pas (dégâts d'eau immédiats sans
  masque [local: SRUINS.C:1473] ; lave 20/coup sans anklets [local: SRUINS.C:182-197]).
  Règle : soit vérifier la finissabilité « zéro artefact », soit placer le niveau après le
  point du graphe où l'artefact requis est acquis.
- **Saut** : la physique Saturn de base (`NORMALJUMPVEL` [local: SRUINS.C:613-617]) diffère
  du saut DOS ; les passages de plateforme DOS calibrés Build peuvent devenir
  infranchissables ou triviaux selon sandales/plume — à vérifier par niveau (contrôle de
  franchissement des dénivelés dans le convertisseur).
- **Économie de munitions** : par-arme → orbes universelles (une orbe recharge l'arme
  équipée) : les niveaux DOS généreux en munitions spécifiques deviennent famine ou surplus
  selon l'arme portée.
- **Difficulté/rythme** : pas de vies ni de checkpoints intra-niveau sur Saturn — un long
  niveau DOS (médiane 1,5× l'empreinte Saturn) devient punitif ; la scission imposée par les
  caps (§5.3.1) est aussi une opportunité : chaque tronçon se termine par un chameau =
  sauvegarde auto.
- **Finale minutée** : LEV20 DOS repose sur `lCountDown` [ex: exhumed.cpp:1687-1704] — ce
  mécanisme n'existe pas côté Saturn ; à remplacer (boss ou chameau simple).
- **Rythme des ennemis** : les consoles introduisent les ennemis plus lentement
  [web: https://www.hardcoregaming101.net/powerslave-console/] ; les densités DOS remappées
  vers des ennemis Saturn plus coûteux (Bastet téléporteuse vs lion) changent la difficulté
  ET le budget VDP1.
- **Non-linéarité idiomatique** : un niveau DOS converti reste « linéaire, feel PC 1996 » ;
  jouable mais pas idiomatique PowerSlave Saturn (hub, backtracking, artefacts). La
  crédibilité passe autant par les systèmes (progression, collectibles) que par la géométrie
  [web: https://www.unseen64.net/2015/03/09/interview-brian-mcneely-lobotomy-software/].

### 5.5 Ordre de validation recommandé

> **Périmé 2026-09-10** : le « corpus complémentaire » ci-dessous devient LE corpus — les 7 `.LEV` Duke
> Saturn de l'épisode 1 contre les cartes du shareware 1.3D (`RETAIL_DISCS.md` §6).

Les **12 cartes DM (LEV21-32)** sont les seules (avec LEV0/10/19) à passer tous les caps
sans scission et n'ont que 3-16 picnums (≤126 tuiles trivialement) : cibles idéales pour
valider le pipeline de bout en bout (remplacer les spawns lotag 62 par un start + un
chameau), **avant** d'écrire le scindeur de secteurs qu'exigent les cartes solo. Corpus de
calibration complémentaire : les 30 `.LEV` de Duke Saturn, seuls niveaux Build retail
réellement passés par la voie import Build→Brew [local: docs/RETAIL_DISCS.md §3, §6 ;
web: Curmudgeon 2002].

## Annexe — contradictions inter-sources tranchées

| Contradiction | Arbitrage |
|---|---|
| « PC first » (HG101) vs « Saturn first » (Wikipedia) | Les deux : développement commencé sur PC/Build (Ruins), sortie Saturn en premier (EU 19/09/1996) — mastering US 1996-10-08 [local: RETAIL_DISCS.md:8] corrobore |
| Wikipedia Lobotomy « 26/09/1996 tri-plateforme » vs Wikipedia PowerSlave (échelonné) | La page par-version est la plus fiable, corroborée par le disque [local] |
| Wikipedia « Death Tank débloqué par les 23 dolls » | **Incomplet** : il faut dolls==ALLDOLLS ET momie+transmetteur complets, écrasés par une nouvelle partie, OU le code manette 2,0,5,3,7,6 [local: BUP.C:216-220, INTRO.C:494-497] |
| « far clip 1024 » (doc Mimas) | FAUX : `ENABLEFARCLIP 0` [local: WALLS.C:28 ; docs/HW_USAGE_VS_MIMAS.md] |
| Crédit Wikipedia « Programmer: Ezra Dreisbach » (global) | Par-version : Dreisbach = Saturn seul, Blazier = PSX, programmeur(s) DOS non établis (MobyGames 403) [web: Curmudgeon 2002] |

*Document généré le 2026-08-31 à partir de quatre rapports d'angle (niveaux, moteur,
gameplay, histoire) ; toutes les mesures « [mesuré] » sont reproductibles via `tools/lev.py`
sur `build/tmp-b2d/*.json` et `refs/extract/PS/`. Curmudgeon 2002 =
https://web.archive.org/web/20081226072803/http://curmudgeongamer.com/article.php?story=20021008212903265*

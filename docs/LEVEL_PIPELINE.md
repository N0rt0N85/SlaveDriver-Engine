# Pipeline de niveaux SlaveDriver — Brew → Dex → CONVERT → `.LEV`

Lecture intégrale de `UTIL/CONVERT.C` (5 203 lignes) et des outils `UTIL/` par 3 lecteurs +
3 vérificateurs adverses (2026-08-30) ; toute affirmation cite `fichier:ligne` de ce dépôt.
Complète `docs/RETAIL_DISCS.md` (format du `.LEV` côté disque).

## 1. Qui fait quoi

| étape | outil | statut dans le dépôt |
|---|---|---|
| éditer la géométrie 3D | **Brew** (éditeur Lobotomy ; magic `BR` du `.til` CONVERT.C:652, « fix for brew output doorways wrong » CONVERT.C:3609) | **absent, jamais publié** |
| exporter le niveau | Brew → un **fichier C** (tableaux `vertex[]`, `sector[]`, `wall[]`, `face[]`, `sectorprops[]`, DEXSHELL.H:155-163) | aucun exemple versionné |
| compiler le niveau dans le convertisseur | `#include LEVELFILE` (DEXSHELL.C:160, `-DLEVELFILE=` à passer) → **CONVERT est recompilé par niveau** | source OK, hôte DJGPP (fonctions imbriquées CONVERT.C:1443-1453, `case a ... b` 3121, littéraux composés 664-746) ; ~~aucun gcc hôte ici~~ **2026-09-10 : MSYS2 `mingw32` GCC 16.1 (i686) compile et lie CONVERT.C + DEXSHELL.C (`-std=gnu89 -fcommon`, `build/tmp-gen2/voieA/`) ; le vrai verrou de la voie A est l'arbre d'assets (≈100 `.seq`, BMP, `.til`) — `DUKE_PC_TO_SATURN.md` §4** |
| tuiles par face | `<LEVEL>.til` : `'B','R'`, 2 o, `short n` (< 127 ⇒ **126 max**, CONVERT.C:658), `n` lignes = chemin BMP relatif à `tiles\` (660-663) | absent |
| lumière + monstres | `<LEVEL>.lit` : `short nMonstres`, `n × {short type,x,y,z,s,a}` (12 o, 401-403), 8 o ignorés, `short nVertex`, puis **`vertexcount` shorts** de lumière bornés à 128 (562-580) ; absent ⇒ lumière 80 partout, zéro objet (553-559) | absent |
| art | `tiles\**\*.bmp` 8 bpp ≤512×512, **index 255 = transparent** (swap 0↔255, 837-840, 1271-1277), `ruinspal.bmp` = palette 0 obligatoire (5103-5106), ciel `DAYSKY1/2/4/5/6/8/9/10.BMP`/`STARS.BMP` 512×256 choisi par sous-chaîne du nom (5039-5069) | absents |
| sprites | `tiles\*.seq` magic `PS` (format 4037-4110), ~100 noms câblés (4253-4918) ; seul écrivain `PS` du dépôt = MKLSEQ.C (lip-sync, 149-175) ; **l'éditeur de séquences est absent** | absents |
| sons | `sounds\*.wav|.voc` (3994-3999) ; VOC blocs 9/6 seulement, WAV parsé naïvement ; sortie 16 bits big-endian / 8 bits XOR 0x80, `rate` = octave/fns relatif à 44,1 kHz (3780-3812) ; **80 sons max côté moteur** (SOUND.C:40) contre 100 côté outil | absents |
| objets « extra » | `argv[4]` texte : `nom secteur …`, `water <s>`, `teleport` (2783-2895) | facultatif |
| sortie | `.LEV` big-endian : bloc ciel (palette, 512/256, bitmap, table 320 ints) → `size` → `sLevelHeader` → parts dans l'ordre de `LOADPART` (LEVEL.C:51-68) → sons → palettes → tuiles → séquences (3249-3388, 5137-5198) ; **PB = 18 o** (SLEVEL.H:97-104, CONVERT.C:3338-3346 — retail 18 aussi) | conforme |

Autres outils `UTIL/` (rôle vérifié) : `STATIC.C` → `STATIC.DAT` (armes, sons statiques, VDP2) ;
`MKPICSET.C` → `.PCS` (logos/intro) ; `MKPIC/MAKETEX/BMP2H*/PAL2H` = BMP → binaire/tableau C ;
`MAKESND.C` = wav/voc → son moteur ; `RAMSES.C/JEFFRAM.C/MKLIPS.C` = analyse d'amplitude → `SP_*.LIP`
(lip-sync) ; `MKCD.C` + `RED.C` = script de mastering + pistes CDDA `.red` ; `RIPKEN.C` = extracteur
**GRP de Ken Silverman** (le format de `STUFF.DAT`/`DUKE3D.GRP` — c'est ça « ce qui est volé à
Ken », pas le moteur) ; `LCONV.C` = export radiosité `.pat` (outil retour absent, `LTEST.CON` en
est probablement une table) ; `CUTLIST.C` = prototype des plans de coupe ; `PLAX/NEWOTTO.C` = `.ANM`
→ ciel (dépend de `modex.c/anm.c/trigdat.c` absents) ; `STRETCH.C` force un BMP en 64×64.

## 2. Le format Dex, tel que CONVERT le lit

* **Unités** : `vertextype {short x,y,z}` ; `out.x = x<<15`, `out.z = y<<15`, `out.y = z<<15` en 16.16
  (CONVERT.C:1362-1364) ⇒ **1 unité Dex = ½ unité monde**, y éditeur = profondeur, z éditeur =
  hauteur. `TILESIZE 64` unités monde (SLEVEL.H:126) ⇒ une tuile 64×64 px = 128 unités Dex ;
  `tileLength = (len+32)/64` (1802). Sortie en `short` monde ⇒ carte dans ±16 383 unités monde.
* **Secteur** = `firstwall..lastwall` murs latéraux **+ 2 « murs » `lastwall+1`, `lastwall+2` = sol et
  plafond** (boucle `w<=lastwall+2`, CONVERT.C:622, 2016), distingués seulement par la normale
  calculée (`normal[1]>0` = sol, 2045/3417/3719). `cielz/floorz` ne servent qu'aux ascenseurs
  (1529) ; `cielh/floorh/ceils/floors`, `facetype.light`, `facetype.flipBit` **ne sont jamais lus**.
  Secteurs **convexes** obligatoires (le test 2231-2278 n'est qu'un warning `convlog.txt`, mais le
  moteur suppose la convexité partout ; `checkForIntersectingSectors` n'est jamais appelée ;
  `checkLevel` est vide).
* **Mur** = quad de 4 sommets consécutifs `v0..v0+3` (1768-1771) ; `firstface=-1` ⇒ invisible
  (portail) ; `nextsector=-1` ou `isblocked` ⇒ bloquant. Mur « rectangle » si ses faces forment
  exactement une grille `tileHeight×tileLength` à ≤1 unité près (1648-1694) — sinon liste libre de
  quads + sommets privés (1871-1891). Le flip d'une texture est **déduit** de l'ordre des sommets
  (8 motifs, 1596-1606).
* **Sémantique par nom de tuile** : `1default\def5_s.bmp` sur un portail horizontal = surface d'eau,
  `1default\def2_c.bmp` = ciel/parallaxe, `animated\lava\*` = lave, `switches\0280/0282/0285/0295.bmp`
  = interrupteurs → `OT_SW1..4`, `animated\fcefield\force1a.bmp` = champ de force (664-756, 2968-3075) ;
  **tout portail texturé qui n'est ni eau ni `capt0049` devient un mur explosable** (2989-2995).
* **Rôles `sectorprops[]`** (3119-3190) : `DOORWAY_STYLE_1..20` → `makeDoorWay` (styles 3-8 = portes
  à clé / minutées / bloquées, `index` = canal ; Brew exporte la porte **ouverte**, CONVERT l'aplatit
  3608-3612) ; `LIFT_SHAFT_STYLE_1..20` → `makeElevator` (butées = min/max des sols voisins,
  3660-3691) ; `SPECIAL_ROLE_2` bloc oscillant ; 10 interrupteur de secteur ; 12/13/14/15
  téléports de puzzle ; 16 eau ; 18 cut-sort ; 26 murs explosifs ; 27 no-map ; 9/23 Ramsès —
  **sans `break`, ils tombent dans 26** (3139-3152, bug ou intention).
* **Caps** : outil `MAXNMSECTORS 600`, `MAXNMWALLS 8000` (moteur **5500**, UTIL.H:22, non
  vérifié au chargement), `MAXNMOBJECTS 500`, `MAXNMMONSTERS 300`, 126 entrées `.til`
  (`facetype.tile` est un `signed char`), `MAXNMTILES 900` sous-tuiles, `MAXCUTSECTORS 128`,
  `size < 900000` (LEVEL.C:41).

## 3. Réponses

**Peut-on créer des `.LEV` ?** Oui, deux voies :
1. *La voie Lobotomy* : écrire le fichier C Dex + `.til` + `.lit`, compiler CONVERT (gcc 32 bits,
   `-DLEVELFILE`, `stricmp`, chemins `\`), et fournir **tous** les assets (`tiles\`, `.seq`, `sounds\`,
   `ruinspal.bmp`, ciels) — aucun n'est dans le dépôt, mais tous sont **régénérables depuis un `.LEV`
   retail** (tuiles RLE, palettes, séquences et sons y sont intégralement, PIC.C:611-716,
   SEQUENCE.C:24-89, SOUND.C:231-241).
2. *La voie directe* (moins chère) : un écrivain Python du bloc niveau (§4 de RETAIL_DISCS.md) qui
   **recopie octet pour octet** les blocs sons/palettes/tuiles/séquences d'un `.LEV` retail et
   n'ajoute que la géométrie + les tuiles de murs. Court-circuite CONVERT et ses caps de tuiles.

**« Le convertisseur Build → Dex » n'existe pas** : `CONVERT.C` convertit **Dex → `.LEV`**. Rien dans
le dépôt ne lit un `.MAP` Build ni un WAD.

**Ce qui manque** pour un pipeline complet : Brew (éditeur), le producteur du `.til`/`.lit`
(Brew aussi), l'éditeur de séquences `PS`, les assets, un gcc hôte 32 bits, et les dépendances de
NEWOTTO.

## 4. Faisabilité Doom WAD → Dex (E1M1 jouable)

Correspondance : `Dex = 2·Doom` (x, y→profondeur, hauteur→z) ; secteur Doom → **découpe convexe**
(GL nodes ou Hertel-Mehlhorn) en secteurs Dex avec portails invisibles entre eux ; linedef 2 côtés →
par côté un portail invisible + un mur plein « lower » et/ou « upper » (exactement la topologie que
`makeDoorWay`/`makeElevator` attendent, 3613-3621, 3735-3770) ; flats → quads 64×64 alignés sur la
grille monde (même densité que Doom) ; textures composites → pré-découpe en tuiles 64×64 nommées ;
PLAYPAL → palette unique, index 255 réservé transparent ; `lightlevel/2` → `.lit` par sommet ;
things → `jeffMonsterMap` avec des doublures PowerSlave (Zombieman→Anubis, Imp→momie,
Demon→Bastet, Cacodemon→guêpe/faucon, Baron→Set) ; porte Doom → `DOORWAY_STYLE_1` (secteur porte
modélisé plafond = plafond voisin) ; lift → `LIFT_SHAFT_STYLE_1` ; ciel SKY1 ×2 → `DAYSKY1.BMP`.

**Perdu sans code moteur** : offsets/pegging de textures (une face = une tuile étirée), textures
*middle* 2 côtés (grilles → mur cassable), déclencheurs par ligne (W1/WR, exit, planchers/lumières
mobiles, crushers, escaliers, secrets), IA/armes/armure/difficulté Doom, musique MUS (CDDA seul),
colormap/fog, cartes > 600 secteurs convexes. **Effort ≈ 11-18 jours** (convertisseur Python +
extracteur d'assets retail + itérations sur console) pour un E1M1 texturé avec portes, lifts et
quelques monstres PS.

## 5. Faisabilité Build `.MAP` → Dex

Même classe de travail : `vertex = 2·xy`, `z = 2·(z_build>>4)`, secteurs Build (concaves,
N côtés) → découpe convexe, pentes perdues (ou échantillonnées aux 4 coins si quadrilatère),
`xrepeat/yrepeat/panning`/murs masqués/`overpicnum` perdus, sprites → seuls les types de
`jeffMonsterMap`, `lotag/hitag` Exhumed (`hitag%1000` = canal, `/1000` = clé, NBlood
`runlist.cpp:511-522`) → ~15 rôles Dex sur 99 cas. ~~Meilleure première cible : PowerSlave DOS (Exhumed)~~ — **périmé (décision owner 2026-09-10)**.
PowerSlave DOS n'a rien à voir avec PowerSlave Saturn : aucun `.LEV` Saturn n'est une conversion d'un
`.MAP` DOS (0 segment commun, `BUILD2DEX_CALIBRATION.md` §0), donc aucune paire pour valider un
convertisseur. **Cible : Duke Nukem 3D PC → Saturn** — les niveaux Saturn de l'épisode 1 sont les
cartes PC, mesuré (`RETAIL_DISCS.md` §6 : X = x/8, Z = −y/8, 30 à 69 % des points PC retrouvés contre
≤ 2 % au témoin ; E1L4 scindé, E1L5 remanié, E1L6 = SECRET1, UREA51 exclusif). Le seul shareware 1.3D
(légal, `refs/build/duke13/`) donne 7 `.LEV` retail de référence. ⚠ Côté Saturn, ces `.LEV` sont au
format génération 2 (plans, grilles, quads, textures 4 bpp : `RETAIL_DISCS.md` §5), pas au format
Dex/PowerSlave de ce document.

## 6. Convertir des WAD « aussi » ?

Un WAD Doom et un `.MAP` Build sont tous deux du 2,5D à secteurs ; Dex est du 3D à quads convexes.
Le même convertisseur (lecture → secteurs convexes 3D → Dex) sert aux deux, avec un front-end par
format ; c'est **la découpe convexe + l'atlas de tuiles ≤ 126 + les assets** qui coûtent, pas le
parseur. Un WAD n'apporte ni ses monstres ni ses scripts : on obtient un niveau PowerSlave dans une
géométrie Doom.

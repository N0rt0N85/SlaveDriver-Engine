# DOOM_ABI — contrat convertisseur (Python) ↔ moteur (C) pour Doom E1M1 (2026-09-14)

## Révision après jugement (2026-09-14)

Trois juges, 42 constats (doublons compris) : **41 appliqués, 1 rejeté** (fenêtre autoaim : la correction proposée
était déjà l'option retenue ; seul le chiffre 7,1° est corrigé). Décisions tranchées, répercutées dans les 4 documents :
`OT_SECTORSWITCH` = **2 shorts** ; seuil SHORTOPENING → **`GP_PLAYER_FIT_HEIGHT` 56** (SPEC_RUNTIME §9) ; ascenseur WR réarmé par
**`SIGNAL_SWITCHRESET` émis dans `elevator_func`** au retour en haut (1 ligne AI.C, `--lift-contact` = repli) ; **10 familles
d'armes = 95 tuiles, `tileBase = 95`** ; sons `len − 32`, **20 statiques** (+ RLAUNC) ; `sizeof(DoomMobjInfo)` **44** ; un seul `DOOM.H`
(`DoomPlayer` de SPEC_PLAYER, `DoomAction` union, ramassage `doom_item_func → doom_playerGetObject`, armure une seule fois dans
`doom_playerDamage`, une seule horloge 35 Hz) ; art HUD = `build/doom/doom_art.h` (bloc 2 de STATIC.DAT à zéro) ; sortie du dernier
niveau = `playerHitTeleport(2 − 200)` ; nukage = `OT_DOOM_DAMAGE` 179 ; `cd_doom/` racine, `E1M1.LEV` par défaut. Ordre minimal :
(1) ce contrat + `DOOM.H` fusionné ; (2) T1 tables + crochet + SHORTOPENING + T2 cadence ; (3) convertisseur j1-j8 ; (4) T3-T4 →
**1er disque** ; (5) T5-T9 + PLAYER 1-4 avec simulateurs PC ; (6) PLAYER 5-7, T10-T11, nukage, F(32) ; (7) PLAYER 8-11.

Ce que le convertisseur ÉCRIT et ce que le moteur LIT, avec les nombres. `[src]` = fichier:ligne du moteur
ou de `Mimas/core` ; `[wad]` = mesuré sur `DOOM1.WAD` (lecture seule). Échelle : **1 unité Doom = 1 u
Saturn** (`doom3d.py:11-12`, `params/doom.cfg:12-15` ; `assemble.py:196-204` divise par 8 le `build_xy`
que `doom3d.py:662` a multiplié par 8 : net 1:1). Angles : short × 5760 = 360/4096° (OBJECT.C:194).

## 1. Types d'objets — `OT_NMTYPES` reste 227

E1M1, lump THINGS, 1 joueur, **skill index 3 = UV** (`gameskill = sk_hard`, `bit = 1<<(3−1) = 4` posé,
p_mobj.c:953-958 ; bit 16 « netgame » absent) `[wad]` : **124 things** (bit 2 = HMP donnerait 100 things,
POSS 4 / SPOS 0 / TROO 2 : pools et liste blanche de sons changent). 124 = **116 objets** (1 joueur + 115 mobjs)
+ 8 starts ignorés.

| DoomEd | type Doom (info.c) | n | sprite | rôle moteur |
|---|---|---|---|---|
| 1 | MT_PLAYER | 1 | — | `OT_PLAYER` = 13 (OBJECT.C:200-206 le cherche d'abord ; `suckSpriteParams` :183-194) |
| 3004 / 9 / 3001 | MT_POSSESSED / MT_SHOTGUY / MT_TROOP | 9 / 16 / 4 | POSS / SPOS / TROO | monstres (runtime) |
| 2035 | MT_BARREL | 6 | BAR1 + BEXP | monstre sans IA |
| 2001, 2007, 2008, 2048, 2049 | SHOTGUN, CLIP, MISC22, MISC17, MISC23 | 1, 2, 2, 1, 3 | SHOT CLIP SHEL AMMO SBOX | ramassages |
| 2011, 2012, 2014, 2015, 2018, 2019 | MISC10, 11, 2, 3, 0, 1 | 1, 3, 13, 25, 1, 1 | STIM MEDI BON1 BON2 ARM1 ARM2 | ramassages |
| 2028, 48, 35, 24, 10, 12, 15 | MISC31, 48, 50, 71, 68, 69, 62 | 8, 2, 2, 7, 2, 2, 4 | COLU ELEC CBRA POL5 PLAY-W PLAY-W PLAY-N | décor |
| — (spawnés) | TROOPSHOT, PUFF, BLOOD, TFOG, IFOG | — | BAL1 PUFF BLUD TFOG IFOG | projectile / one-shot |
| 2, 3, 4, 11 | starts coop / DM | 3 + 5 | — | **ignorés** en 1p |

**Décision : ne pas redéfinir `OT_NMTYPES`.** 227 est gravé dans trois asserts (SEQUENCE.C:44-48 taille du
bloc séquences ; SOUND.C:233-234 via `loadSoundSet(fd, level_objectSoundMap, OT_NMTYPES)` SOUND.C:246 ;
`level_objectSoundMap[OT_NMTYPES]` SOUND.C:29) et dans les outils (`tools/lev.py:21`,
`duke2ps/lev_io.py:31`, `lev_write.py:189,207`). Les 137 `mobjtype_t` sont numérotés **par-dessus** des
valeurs de l'enum (SLEVEL.H:22-81) — ces valeurs ne sont **pas** « libres » : 14-17 (SPIDER…BASTET), 22-29,
37, 43-47, 65-69, 71-73, 80, 83, 87-89, 97-107, 108-162 (TORCH, CONTAIN), 204-226 (DOLL) sont des `case` du
`switch` OBJECT.C:213-373. Le schéma tient **uniquement** parce que le crochet `game_placeObject` précède le
`switch` (ci-dessous) et parce qu'aucun code moteur ne dispatche par plage de type sur les listes vivantes,
sauf OBJECT.C:401-420 (parcours d'`objectIdleList` pour `OT_CONTAIN1..17` = 146-162, `iAmMapHolder`) : inoffensif
parce que `doom_spawn` place ses objets **immédiatement** dans `objectRunList` (`moveObject`, SPEC_RUNTIME §2) et
que les mises en idle sont différées (`delay_moveObject`, appliquées après `placeObjects`). Numérotation
**générée** (§4, même table des deux côtés) : `OT(MT_PLAYER) = 13` ; sinon les MT sont pris dans l'ordre croissant
et posés sur les valeurs croissantes **[14..47] ∪ [64..90] ∪ [92..162] ∪ [172..226]** (187 valeurs ≥ 136) — d'où
MT 1..34 → 14..47 (POSS 14, SPOS 15, … TROOP = MT 11 → 24), MT 35..61 → 64..90, MT 62..132 → 92..162,
MT 133..136 → 172..175. Objets Doom non-mobj : `OT_DOOM_EXIT` 176, `OT_DOOM_SECRETEXIT` 177,
`OT_DOOM_LIGHT` 178, **`OT_DOOM_DAMAGE` 179** (secteur à dégâts, §6). Réservés au moteur (réutilisés tels
quels) : 13 joueur, 48 `OT_NORMALDOOR`, 49 / 61-63 ascenseurs, 57-58 portes bloquées, 59-60,
91 `OT_SECTORSWITCH`, 163-171 téléporteurs et `OT_SW1..4`.

**Ce que ça touche dans le moteur** : une ligne en tête de la boucle de `placeObjects` (OBJECT.C:212,
après l'assert `firstParam == objectPPos`) : `if (game_placeObject(type)) continue;` — le crochet
consomme ses params (§6), retourne 0 pour les types moteur (le `switch` :213-373 et son
`default: assert(0)` :375-376 restent). `setSequence`/`setState` (AICOMMON.C:146-158, 271-277) ne
servent pas aux objets Doom.

**Params d'un mobj Doom** (lus par le crochet avec `suckShort`, gros-boutiste OBJECT.C:165-170) :
**6 shorts** = `sector, x, y, z, angle, flags` — mêmes 5 premiers que `OT_PLAYER` (OBJECT.C:186-193 +
suckSpriteParams :183-194 ; `assemble.py:14-16, 204`), + `flags` = bits THINGS (bit 3 = ambush).
`y` = `floorLevel` du secteur (mesuré sur 23 niveaux retail, `assemble.py:198-201`) : `shiftSprites`
ajoute le rayon après placement (SPRITE.C:60-66) et le dessin pose les pieds à `pos.y − radius`
(WALLS.C:2697-2698). `sector` = feuille BSP du WAD remappée (`doom3d.py:660-661`). Angle mobj =
`round(deg × 4096/360)` ; **joueur seulement** : `+90°` avant conversion (`constructPlayer` retire
F(90), AI.C:51 ; `doom3d.py:665-670`).

## 2. Séquences — un état = une séquence par vue, tics au runtime

Mécanique : séquence = plage de frames `level_sequence[s] .. [s+1]` (SPRITE.C:775-791) ; frame = chunks
dessinés à `feetScreenPos + scale × (chunkx, chunky)`, carrés `width64 = scale>>10` (WALLS.C:2777-2790,
2723) ; `scale = 65536` ⇒ **1 texel/u** (le runtime écrase le 48000 de `newSprite`, SPRITE.C:86). Flag chunk bit 0 = miroir horizontal, bit 1 = vertical (WALLS.C:2789-2791).
Vue = `getFacingAngle` (AICOMMON.C:73-100) : secteurs de 45° centrés sur 0, −23..23 → 0, puis 1..7 dans
le sens de l'angle croissant ; Doom `R_ProjectSprite` fait `(ang − angle + 180° + 22,5°)/45°` avec
angle croissant dans le même sens (repère `doom3d.py:667-668`) ⇒ **vue moteur k = rotation Doom k+1**
(lump `…Ak+1`). Rotations 2/8, 3/7, 4/6 sont des lumps `A2A8` : vue 7 = tuile de la vue 1 + miroir.

**Carte** `level_sequenceMap` (227 shorts, SEQUENCE.C:72) : indexée par **`spritenum_t` (0..137)** pour
Doom, entrées 163-171 laissées au moteur (`constructSwitch` lit `level_sequenceMap[OT_SW1..4]`,
AI2.C:598). Valeur : `base | 0x8000` si la famille a des rotations (convention `HB` de AI.C:2478,
AICOMMON.H:5), `−2` si absente. **Formule, identique en Python et en C** :

```
if (map[spr] == -2) return -2            /* famille absente : AVANT le test 0x8000 (−2 = 0xFFFE a le bit 15) */
stride = (map[spr] & 0x8000) ? 8 : 1
seq(spr, frame, view) = (map[spr] & 0x7fff) + frame*stride + (stride==8 ? view : 0)
```
Sans la garde, −2 donnerait `0x7FFE + …` et l'`assert(sequence != -2)` SPRITE.C:778 ne se déclencherait
jamais (lecture hors bloc). Les entrées **172-203** de la carte sont lues par `markAnimTiles` (PIC.C:129-161,
`OT_ANIM_CHAOS1..OT_ANM12` : toute valeur ≥ 0 y est prise pour une animation murale) et doivent rester **< 0**
— le convertisseur met −2 partout hors `spritenum` (0..137).
`frame` = `states[s].frame & 0x7fff` (lettre A = 0). Une famille occupe `(maxframe+1)*stride`
séquences contiguës, les non peuplées vides. Frame : 1 par
séquence, `flags = 0`, `sound = −1` (SEQUENCE.C:76-78 ne décale que ≠ −1), `pad = 0` (asserté :51-57) ;
les `tics` de `states[]` sont comptés par le runtime à 35 Hz (SRUINS.C:2141, 2196 pour le 30 Hz
d'origine) — `spriteAdvanceFrame` n'est pas appelé.

**Découpe d'un patch** `(w, h, leftoffset lo, topoffset to)` : pixel `(i,j)` → offset monde
`(i − lo, j − to)`, y écran croissant vers le bas, pieds à `y = 0` (Doom : `to = h` pour un thing posé) ;
chunks `c = 0..ceil(w/64)−1`, `r = 0..ceil(h/64)−1` à `chunkx = −lo + 64c`, `chunky = −to + 64r`, image
calée en haut-gauche du chunk, reste = index 0. Vue miroir : même tuile, flag 1, `chunkx' = −chunkx − 64`
(STATIC.C:512-515 fait le même calcul avec `width − (subx+64)`). Tuiles : 64×64, flags **0x6A** =
`64x64|8BPP|PALLETE|RLE` (SLEVEL.H:192-198 ; PIC.C:688-690), palette objet = PLAYPAL[0] en BGR555 ; index 0 =
transparent, 255 forcé 0xffff (PIC.C:631) ⇒ remapper 0 et 255 vers la couleur non nulle la plus
proche (`doomtiles.py:17-20`).

**Plafonds** : bloc `< 1 Mo` (SEQUENCE.C:30) ; `MAXNMPICS 800` (PIC.C:32) ; `sFaceType.tile` et
`level_texture[]` sont des **u8** auxquels `tileBase = nmWeaponTiles` est ajouté (LEVEL.C:69-72,
SRUINS.C:1940) ⇒ **tuiles de géométrie en tête du jeu de tuiles, ≤ 255 − tileBase** ; les chunks
(`tile` short) viennent après, sans limite u8.

| E1M1 | séquences | frames/chunks | tuiles 8 bpp |
|---|---|---|---|
| POSS, SPOS (A-G ×8, H-U ×1) `[wad]` | 70 + 70 | 70 + 70 | 49 + 49 |
| TROO (A-H ×8, I-U) | 77 | 77 | 53 |
| BAL1 5, PUFF 4, BLUD 3, TFOG 10, IFOG 5, BAR1 2, BEXP 5 | 34 | 34 | 34 |
| 11 ramassages (19 frames), COLU, ELEC (38×128 = 2 chunks), CBRA, POL5 | 23 | 23 / 24 | 24 |
| PLAY (rotations ⇒ 23×8, 2 peuplées : N, W) | 184 | 2 | 2 |
| **total** | **458** | **276 / 277** | **211** (v1 #25 mesure ≈ 270 Ko RLE) |

Bloc séquences = 12 + **459**×2 + **277**×8 + 277×8 + 454 = **5 816 o** (STATIC.C:547-549 n'ajoute qu'une entrée
de séquence et une frame terminales, **pas de chunk terminal** ; SEQUENCE.C:44-48). Tuiles totales E1M1 :
**95** armes (§5, 10 familles) + 141 géométrie (`lev.py` sur `TOMB_e1m1.LEV` : `distinct_tiles 141`) + 211 =
**447 < 800** ; géométrie 141 + 95 = 236 ≤ 255 (**marge : 19** — plafond de géométrie **160** tuiles par
niveau, à vérifier par `verif_doom.py` ; repli `--e1m1-weapons` = 48 tuiles, plafond 207).

## 3. Sons — statiques dans STATIC.DAT, dynamiques dans le .LEV

Format d'un son (SOUND.C:203-228) : `int size, int pitch, int bps, int loopStart` + PCM ; copié à
`soundTop` pair (`assert !(soundTop&1)`, `soundTop+size < 512 Ko` :218-219) ; `SoundRec.size` u16.
DS* Doom = DMX 8 bits 11 025 Hz : en-tête 8 o (`u16 3, u16 rate, u32 len`) ; `len` **inclut 16 octets de
garde de chaque côté** du PCM → **`size = len − 32`** arrondi pair, PCM = `lump[24 : 24 + len − 32]` (Mimas
`src/i_sound_saturn.cxx:254-259` : `length -= 32; src = lump + 24`), `pitch = 0x7000` (octave −2, fns 0 :
STATIC.C:604-613, MAKESND.C:32-40), `bps = 8`, `loop = −1`, PCM `^0x80` (STATIC.C:630-631). Plafond
`MAXNMSOUNDS 80` (SOUND.C:40) toutes sources.

**Statiques** : `loadStaticSounds` exige `8 = ST_NMSTATICSOUNDGROUPS` shorts (SOUND.H:18-20,
SOUND.C:233) ; les portes/ascenseurs jouent `ST_PUSHBLOCK+0` à l'ouverture et au début de fermeture,
`+1` porte close et ascenseur arrêté, `+2` ascenseur en route (AI.C:4336, 4361, 4372, 4645, 4673).
Liste fixe, indices dans le groupe 0 (`ST_JOHN`) ; `outSoundMap[ST_PUSHBLOCK]` pointe l'index 6 :

| n | 0-2 | 3-5 | 6 7 8 9 | 10-12 | 13-15 | 16-18 | 19 |
|---|---|---|---|---|---|---|---|
| lump | PISTOL SHOTGN PUNCH | SWTCHN SWTCHX NOWAY | DOROPN DORCLS PSTART PSTOP | PLPAIN PLDETH OOF | ITEMUP WPNUP SLOP | BAREXP TELEPT STNMOV | RLAUNC |

**20 sons, 176 970 o** `[wad]` (19 = 161 518 + RLAUNC 15 452 : `MT_ROCKET` n'est spawné que par le joueur,
jamais « présent » dans un WAD, donc absent de la liste blanche dynamique — sans cette entrée statique
`doom_sfxIndex(sfx_rlaunc) < 0` et le lance-roquettes est muet ; même règle pour `sfx_bfg`/`sfx_plasma`
hors cible). Écart accepté : DORCLS joue à la fermeture *complète* et à l'arrêt
d'ascenseur (index +1 partagé) ; fix = 1 ligne dans `elevator_func` AI.C:4673 (`+3` = PSTOP).

**Dynamiques** : `level_objectSoundMap` (227 shorts, décalés de `nmStaticSounds` SOUND.C:243-249) est
indexé par **`sfxenum_t` (0..108, `NUMSFX` sounds.h:224)** : valeur = index dans la liste du `.LEV`,
**`−1000` si absent** (reste < 0 après décalage). Liste blanche E1M1 = union des `seesound/attacksound/
painsound/deathsound/activesound` + variantes tirées par `A_Look`/`A_Scream` (posit1-3, bgsit1-2,
podth1-3, bgdth1-2) + sons de projectile (firsht, firxpl) des mobjs présents ou spawnables, hors
statiques : POSIT1-3, POPAIN, PODTH1-3, POSACT, BGSIT1-2, CLAW, BGDTH1-2, BGACT, FIRSHT, FIRXPL =
16 sons, **168 848 o**. Total E1M1 **345 818 o < 524 288**, 36 sons < 80. Le runtime résout
`sfx → index` : statique via `level_staticSoundMap[ST_JOHN] + n`, sinon `level_objectSoundMap[sfx]`
(`doom_sfxIndex`, **une seule définition, `DOOM_SOUND.C`**, SPEC_PLAYER §4.1).

## 4. Tables générées d'`info.c` — `game/doom/DOOM_TABLES.C`

`tools/doom2ps/info2tables.py Mimas/core/info.c Mimas/core/sounds.c` écrit **deux sorties** : le C et
`doom_ids.json` (mêmes numéros pour le convertisseur : `ed→MT`, `MT→OT`, `spritename→spritenum`,
`sfxname→sfx`, liste statique §3). Structures (big-endian SH-2, alignées) :

```c
typedef struct { unsigned char sprite, frame; short tics; short nextstate;
                 unsigned char action, flags; } DoomState;   /* 8 o × 967 = 7 736 o ; flags bit 0 = fullbright,
                                                                 bit 1 = verbe d'arme (psprite, états 1..89) */
typedef struct { short doomednum, spawnstate, seestate, painstate, meleestate, missilestate,
                 deathstate, xdeathstate, raisestate, spawnhealth, speed, radius, height,
                 painchance, damage;                          /* painchance en short : 256 (KEEN, BOSSBRAIN) ne tronque pas */
                 unsigned char seesound, attacksound, painsound, deathsound,
                 activesound, reactiontime, ot, pad; int flags; } DoomMobjInfo;  /* 15×2 + 8 = 38 → 40 (int aligné 4) + 4 = 44 o × 137 */
typedef union { void (*mobj)(DoomActor *this); void (*psp)(DoomPlayer *p, int ps); } DoomAction;
extern const DoomState    doomStates[NUMSTATES];
extern const DoomMobjInfo doomMobjInfo[NUMMOBJTYPES];
extern const DoomAction   doomActions[];          /* [0] = {NULL} ; DOOM_VERBS.C ; membre choisi par DoomState.flags bit 1 */
extern const unsigned char doomStaticSfx[20];      /* §3, ordre STATIC */
```
`mass` est **omis** (inutilisé sans `thrust`, SPEC_RUNTIME §4 ; 10 000 000 de MT_BOSSBRAIN déborderait un short).
`frame` = `states[].frame & 0x7fff`, `flags` bit 0 = bit 15 de `frame` ; `tics` à **35 Hz** tels quels ;
`action` = index dans `doomActions[]` (énumération des `A_*` distincts rencontrés, ordre d'apparition) — une
seule table pour `A_Chase` et `A_FirePistol`, le membre de l'union est choisi par `flags` bit 1 (comme
`actionf_t` de Doom) ; `radius/height` en u (÷FRACUNIT), `speed` en u/tic (`10*FRACUNIT` → 10, monstres 8 tels
quels) ; `ot` = §1. La séquence n'est **pas** stockée : `seq(sprite, frame, view)` §2 est calculé à `setstate`.
Armes : `doomWeaponInfo[9]` recopie `d_items.c:37-68` (ammo, up/down/ready/attack/flash) ;
`wseq(state) = state − 1` (§5).

## 5. STATIC.DAT Doom — ordre exact (SRUINS.C:1922-1932)

| # | bloc | lecteur | contenu Doom | octets |
|---|---|---|---|---|
| 1 | écran de chargement | SRUINS.C:1098-1120 | 256 × BGR555 (bit 15) + `int 320, int 240` + 320×240 index (TITLEPIC 320×200 centré, bandes 0) | 77 320 |
| 2 | feuille VDP2 NBG0 | SRUINS.C:1080-1096 | 512×512 8 bpp, **zéros** : cette feuille ne sert qu'à `displayVDP2Pic` (chunks d'armes de classe TILEVDP, SEQUENCE.C:270-290), pas aux chars VDP1 du HUD — l'art HUD Doom (STBAR, visages, clés, polices) est un **tableau C généré**, `build/doom/doom_art.h` (§8, SPEC_CONVERTER §3bis, SPEC_PLAYER §3) | 262 144 |
| 3 | sons statiques | SOUND.C:231-241 | `int 8`, 8 shorts (tous 0 sauf `[3] = 6`), `int 20`, 20 sons §3 | 4+16+4+20×16+176 970 = **177 314** |
| 4 | tuiles d'armes | PIC.C:709-711 `loadTileSet(fd,0)` | `int n`, n tuiles 0x6A (`short flags, short palNm, short size, RLE`, PIC.C:671-700) | 10 familles : n = **95** (130 063 o RLE) ; `--e1m1-weapons` : 48 |
| 5 | séquences d'armes | SEQUENCE.C:92-132 | `int size` + header + frames + chunks + `nmSequences` shorts, **sans carte** ; `wSequence[0] == 0` asserté :126 | ≈ 2 Ko |

Tuiles d'armes `[wad]`, découpe **lump par lump** (`⌈w/64⌉·⌈h/64⌉`, chaque lump a sa taille propre) : PISG
A0 57×62 → 1, B0 79×82 → 4, C0 66×81 → 4, D0 61×81 → 2, E0 78×103 → 4 = **15** ; PISF A0 41×38 → **1** ; SHTG A0
79×60 → 2, B0 119×121 → 4, C0 87×151 → 6, D0 113×131 → 6 = **18** ; SHTF A0 44×31, B0 54×44 → **2** ; PUNG A0
113×42 → 2, B0 80×41 → 2, C0 107×52 → 2, D0 147×76 → 6 = **12** ⇒ E1M1 (5 familles) = **48**. Cible du
propriétaire (chaingun E1M2, roquettes E1M3) ⇒ **les 10 familles par défaut** : + CHGG 8, CHGF 4, MISG 8,
MISF 9, SAWG 18 = **95**. **`tileBase = 95`** (`loadWeaponTiles` retourne n, SRUINS.C:1930) ⇒ géométrie
≤ 160 (§2). Ordre de découpe = ordre des lumps dans le WAD, patch par patch.

Numérotation `wSequence` : **`wseq(state) = state − 1`** pour `S_LIGHTDONE (1) … S_BFGFLASH2 (89)`
(info.c:129, info.h:262) : 89 séquences + l'entrée terminale (STATIC.C:547-549, lue à SEQUENCE.C:220), une frame
par séquence, `sound = −1`, `flags = 0` (le runtime n'utilise pas `FRAMEFLAG_FIRE` : les verbes
`A_FirePistol…` viennent de `doomStates[].action`, pas de `weaponFire` WEAPON.C:920-921 qui tire sur
tout flag non nul). États des armes absentes du WAD shareware (plasma, BFG) : séquences vides. **Attention** :
dès l'index 50 `advanceWeaponSequence` passe `overlay = 0x4000` (SEQUENCE.C:252-253) ; les palettes 30-34/44-49
(:270-287) ne touchent que des chunks VDP2 — ligne :252 retirée sous `GP_GAME_DOOM` (SPEC_PLAYER §2.3 ; les
10 familles sont dans STATIC dès J3).

Coordonnées psprite : `pos = (xo − 160 + chunkx, yo − 120 + chunky)`, `xo = bob + cx` (SEQUENCE.C:296-297,
origine = centre écran 240 lignes) ; chunks `chunkx = −lo + 64c`, `chunky = −to + 64r` ; le runtime épingle
avec `(cx, cy) = (1, 32 + Y0)` (`psp.sx = 1`, `WEAPONTOP 32`, p_pspr.c:41-42). **`Y0` = 10** en 240 lignes :
la fenêtre 3D est `YMIN −110 … YMAX 90` (WALLS.C:128-131), soit les lignes écran 10..210 — le cadre Doom
0..200 n'est **pas** centré (20 serait 10 px trop bas) ; 0 en 224 lignes (SPEC_PLAYER §3.1). Flash =
`addWeaponSequence(wseq(flashstate))` (:165-167, overlay :308-311). Une séquence d'une frame par état,
épinglée à chaque changement d'état (SPEC_PLAYER §2.3, écart a : la file de 8 déborderait avec les
états à 1 tic).

## 6. Portes, ascenseurs, interrupteurs, sortie — E1M1 `[wad]`

Lignes spéciales : **8 × type 1** (porte manuelle → secteurs 4, 68, 76, 81), **1 × 11** (S1 sortie),
**1 × 36 tag 1** (W1 sol → secteur 59), **1 × 88 tag 2** (WR ascenseur → secteur 70), 8 × 48 (scroll,
ignoré). Secteurs à `special` : **7 (nukage : 13, 55, 57, 61) → `OT_DOOM_DAMAGE`** ; 9 (secret), 8/12/1
(lumière → `OT_DOOM_LIGHT`) hors J3.

**Push block** (`sPBType` 9 shorts SLEVEL.H:97-104 ; `sPBVertex` :109-113 ; `PBWall` shorts) : la version
compilée d'`updatePushBlockPositions` (SPRITE.C:852-890, `#else` ; :806-850 est le `#if 0`) n'applique que
**`dy`** : `level_vertex[vt].y += dy` sur les runs `[vStart, vStart+vNm)` (:863-869), les sprites sur
`floorSector` suivent (:873-887), `registerPBObject` pose l'objet sur
`level_wall[PBWall[w]].object` (OBJECT.C:141-152), cible du `SIGNAL_PRESS` (hitscan < 120 u,
SRUINS.C:843-853). Par secteur mobile : `enclosingSector` = secteur, `PBVert` = **tous les sommets à la hauteur mobile**
(porte : plafond fermé = sol ; ascenseur : sol) appartenant au secteur et à ses murs-portails,
`PBWall` = murs du secteur + portails adjacents (flag `WALLFLAG_DOORWALL` sur les portails, relu par
`setDoorBlockBits` AI.C:4294-4309), `floorSector` = secteur pour un ascenseur, −1 pour une porte,
`dx=dy=dz=0`.

**Seuil SHORTOPENING (bloquant, touch point SPEC_RUNTIME §9)** : `setDoorBlockBits` pose
`WALLFLAG_SHORTOPENING` (0x1000, SLEVEL.H:143, ∈ `BLOCKBITS`) sur tout DOORWALL dont `v[1].y − v[2].y < 80`
(AI.C:4302-4305, constante PowerSlave) ; le joueur est créé `SPRITEFLAG_BSHORT` (0x1000, AI.C:47, SPRITE.H:32)
et `bumpSectorBoundries` refuse le portail (`(wall.flags & o->flags) & BLOCKBITS`, SPRITE.C:414). Les 4 portes
d'E1M1 font **68 u** ouvertes (secteur 4 : sol 0, plafond voisin min 72 ; 68/76/81 : sol −24, plafond 48) :
bloquées **même ouvertes** tant que le seuil vaut 80. Contrat : `80` → **`GP_PLAYER_FIT_HEIGHT` (56)** sous
`GP_GAME_DOOM` (§7) ; fermée (fente 1 u) la porte reste bloquante (1 < 56).

| objet | OT | params (shorts, dans l'ordre `suckShort`) | source | E1M1 |
|---|---|---|---|---|
| porte manuelle | `OT_NORMALDOOR` 48 | `pb`, `channel = −1`, `doorHeight` | OBJECT.C:269-272 ; `constructDoor` AI.C:4384-4398 (`door_func` :4313-4382) | 4 ; `doorHeight` = min plafond voisin − 4 − sol = 68 ; 2 u/trame, attente 128 trames (AI.C:4312, 4358) ≈ Doom 2 u/tic, 150 tics |
| ascenseur WR (88) | `OT_NORMALELEVATOR` 49 | `pb`, `lower`, `upper`, `channel = tag` | OBJECT.C:255-262 ; AI.C:4686-4699 | secteur 70 : `lower` = plus bas sol voisin (−48), `upper` = sol (104), throw = 152 ; 5 u/trame (AI.C:4659). **Réarmement** : `sswitch_func` passe ON au 1er `SIGNAL_ENTER` et ne repasse OFF que sur `SIGNAL_SWITCHRESET(channel)` (AI2.C:648-661), que seul `door_func` émet (AI.C:4375-4376) ⇒ sans retouche, **un seul cycle**. Décision : `elevator_func` émet `signalAllObjects(SIGNAL_SWITCHRESET, channel, 0)` au retour en haut (`direction==1 && offset>=0`, AI.C:4660-4663) si `channel != −1` — 1 ligne sous `GP_GAME_DOOM` (SPEC_RUNTIME §9). Repli : `--lift-contact` (SPEC_CONVERTER E7) = `channel = −1`, déclenchement au contact/press (AI.C:4593-4624), sans sector-switch |
| sol W1 (36) | `OT_STUCKDOWNELEVATOR` 61 | idem, `channel = 1` | AI.C:4649-4653 (`offset <= −throw` → `moveTo(−throw)` ; STUCKDOWN → idle : un aller, reste en bas) | secteur 59 : type 36 = **turboLower, destination = plus HAUT sol voisin + 8** (p_spec.c:655 → `EV_DoFloor(turboLower)` ; p_floor.c:298-305) : sol 96, voisins −48/−48 → `lower = −40`, `upper = 96`, **throw 136** (`throw = upper − lower`, AI.C:4696) |
| déclencheur de ligne W | `OT_SECTORSWITCH` 91 | **`sectorNm`, `channel = tag` — 2 shorts exactement** (`constructSectorSwitch` AI2.C:665-676 ; `case OT_SECTORSWITCH: constructSectorSwitch(); break;` OBJECT.C:213-215 — `constructForceField(suckShort())` est le `case OT_FORCEFIELD` :249-250, sans rapport) | AI2.C:648-653 (`SIGNAL_ENTER` → `SIGNAL_SWITCH`) | 2 (tags 1, 2) : secteur d'entrée = côté traversé (une par feuille bordant la ligne) |
| interrupteur S | `OT_SW1` 168 | `sectorNm`, `channel`, `ox, oy, oz` | AI2.C:582-596 ; press < 40 u de l'orifice :535-538 | 1 (sortie) : orifice = centre du mur |
| sortie | `OT_DOOM_EXIT` 176 | `channel = 900` (901 secrète) | nouveau, `game/doom` | 1 |
| secteur à dégâts | `OT_DOOM_DAMAGE` 179 | `sectorNm`, `hp` (5 pour special 7 ; p_spec.c `P_PlayerInSpecialSector` : `hp` toutes les 32 tics, `!(leveltime & 0x1f)`) | nouveau, `game/doom` (SPEC_RUNTIME §6) | 1 par feuille des secteurs 13, 55, 57, 61 |

Interrupteur = **4 séquences** de tuiles de géométrie (16 bpp, 0x32) : `base` OFF, `+1` anim ON, `+2`
anim OFF, `+3` ON (AI2.C:524-545 ; :549 écrit la tuile dans la cellule) ; tuile OFF **unique dans le
secteur** (recherche :606-632). Canaux :
tags Doom = 1..999, sortie 900/901 ; le moteur s'en réserve 1000-1005, 1010, 10000+, 11000+
(AI.C:3879, 2799, 4730, 5830). Sortie : `exit_func` reçoit `SIGNAL_SWITCH(900)` → `playerHitTeleport(next)` (SRUINS.C:1413-1415) ⇒
`runLevel` retourne `200+next` (:2266-2270), la boucle charge `getLevelName(next)` (:2533-2541) ;
`next = courant+1` **si `next < DOOM_NMLEVELS`** (taille de `doomLevelNames[]`, §9) ; **dernier niveau ⇒
`playerHitTeleport(2 − 200)`** = action 2 = *quit* → `goto intro` (:2544-2546). Jamais −1 : `playerHitTeleport(−1)`
donnerait 199 = branche camel (`case 100 ... 199`, `levFlags[hitCamel−100]` avec `hitCamel = 0` ⇒ `levFlags[−100]`
corrompu, :2527-2533) ; et `next` hors disque ⇒ `fs_open` asserte (FILE.C:155-157). Clés : `getKeyMask()` bit = type − `OT_BUGDOOR` (AI.C:4329) — pas de
porte à clé dans E1M1.

## 7. `params/doom.cfg` — clés `GP_*`

Le fichier existant `[src] params/doom.cfg` fixe **`PLAYER_MODEL = ball`** (pas cylinder) avec
`PLAYER_RADIUS 16`, `PLAYER_EYE_HOVER 25` (œil = 41, `SPR_HOVER` SPRITE.H:117), `PLAYER_STEP 24`,
`NEAR_CLIP 10`, `DEBUG_TOGGLE lr_x` ; hauteur du corps = 41 + `SPR_HEAD` = rayon 16 = 57 ≈ 56
(doom.cfg:17-20). Les clés cylindre sont remplies (41/41/41, `PLAYER_HEAD 15`) si le propriétaire
bascule `cylinder` (PLRCYL.H:10-22). **À ajouter** : `GAME = doom` → `GP_GAME doom` et
`GP_GAME_DOOM 1` (gameparams.py:56-58, forme « mot ») ; **`PLAYER_FIT_HEIGHT = 56`** → `GP_PLAYER_FIT_HEIGHT`
(clé numérique, :53-54), seuil SHORTOPENING de `setDoorBlockBits` (§6, SPEC_RUNTIME §9) — même valeur que
`doom3d.py:66` (`PLAYER_FIT_HEIGHT = 56.0`, à lire depuis le `.cfg` à terme). Pas de clé de vitesse de porte : les
constantes AI.C:4312/4358 donnent 60 u/s et 4,3 s d'attente contre 70 u/s et 4,3 s chez Doom.

## 8. Build — `GAME=doom` ajoute `game/doom/*.C` à MAIN

Sources (`.C` majuscules, `-x c` comme le reste, Makefile:95) : `DOOM_TABLES.C` (généré),
`DOOM_ACTOR.C` (`game_actor_func`, tics 35 Hz), `DOOM_VERBS.C` (`A_*`), `DOOM_GAME.C`
(`game_placeObject`, `exit_func`, `doom_item_func`, `doomLevelNames[]`, hooks §9), `DOOM_PLAYER.C`,
`DOOM_WEAPON.C`, `DOOM_HUD.C`, `DOOM_SOUND.C` (`sfx→index`), **un seul `DOOM.H`** (`DoomActor`, `DoomPlayer`,
`DoomAction`, toutes les signatures : SPEC_RUNTIME §10 + SPEC_PLAYER §1.2). Règle proposée,
à côté de `PLAYER_C` (Makefile:88-91) :

```make
ifeq ($(shell $(PYTHON) tools/gameparams.py --get GAME $(PARAMS)),doom)
  GAME_C   := $(notdir $(basename $(wildcard game/doom/*.C)))
  CDDIR    := cd_doom
  DOOMWAD  ?= ../Mimas/cd/data/DOOM1.WAD
$(BUILD)/doom_art.h: tools/doom2ps/wad2hud.py tools/doom2ps/wad2font.py
	$(PYTHON) tools/doom2ps/wad2hud.py $(DOOMWAD) $@     # STBAR+STARMS, 26 visages, 3 clés, 3 polices (SPEC_CONVERTER §3bis)
$(OBJDIR)/DOOM_HUD.o $(OBJDIR)/PRINT.o: $(BUILD)/doom_art.h
endif
MAIN_C  += $(GAME_C)                       # après la ligne 174
vpath %.C game/doom                        # $(OBJDIR)/%.o: %.C existant (l'arbre est déjà build/doom/, Makefile:82-84)
```
`$(BUILD)` est déjà sur le chemin d'inclusion (`INCLUDES … -I$(BUILD)`, Makefile:110) et gitignoré ; le moteur
lit déjà `stat_bar` comme tableau C à en-tête (STATBAR.C:74, `EZ_setChar(0, COLOR_4, *(int*)stat_bar, …)`
SRUINS.C:1890-1891) — même forme.

## 9. Disque — fichiers minimaux, nom du .LEV, démarrage direct

`fs_open` asserte l'existence (FILE.C:155-157 ; `+` = racine :152-153). Ouverts par INIT et MAIN :
`LOGOS.PCS` + `OPEN.MOV` (INITMAIN.C:185, 246), `INITLOAD.DAT` (SRUINS.C:2408-2424 ; INITMAIN.C:594 en
fin de partie seulement), `MAIN.BIN` (:624), `INTRO.PCS` (INTRO.C:418 via `playIntro` SRUINS.C:2477),
`MAP.DAT` (BIGMAP.C:166 via `runMap` :2496), `STATIC.DAT` (SRUINS.C:1922), le `.LEV` (:1937),
`BONUS.BIN` (INTRO.C:565). Sous `GP_GAME_DOOM` : INITMAIN saute logos/film (:185-246) et `runMap` devient `level = 0`, comme
`TESTCODE` fait `level = 22` (SRUINS.C:2491-2500) — c'est le « warp » (le `case 3` :2548 n'y est pour rien).
**Propriétaire de ces trois touch points : SPEC_RUNTIME §9 (`DOOM_GAME.C`)** — BIGMAP.C:84 `getLevelName` →
`doomLevelNames`, SRUINS.C:2491-2500 `runMap` → `level = 0`, INITMAIN.C:185-246 logos sautés. **Disque minimal
E1M1 (`cd_doom/` à la racine du dépôt, déjà dans `.gitignore:10` ; `CDDIR` est relatif à la racine,
Makefile:280 `CDDIR ?= cd`, :286 `$(wildcard $(CDDIR)/*)` — un `build/doom/cd_doom/` donnerait un ISO sans
données, :291-294)** : `0` (INIT), `MAIN.BIN`, `STATIC.DAT` (§5), **`E1M1.LEV`** (nom par défaut de
`make_e1m1.py`, gravé côté C ; `TOMB.LEV` seulement pour le disque de contrôle sans `GP_GAME_DOOM`),
`INITLOAD.DAT` et `INTRO.PCS` (assets PowerSlave, texte + titre, remplacés à J4 ; rien n'est commité).
Noms : `levelGraph` (BIGMAP.C:43-82, `getLevelName` :84-86) remplacé par `doomLevelNames[DOOM_NMLEVELS] =
{"+E1M1.LEV", …}` (8.3), **borné** (§6 : dernier niveau ⇒ *quit*) et vérifié par `make_e1m1.py` contre le
contenu de `cd_doom/` ; `playCDTrackForLevel` (SRUINS.C:2016) sans piste audio = cas déjà exercé par
`TOMB_e1m1`. Génération : `make PARAMS=params/doom.cfg iso` → `build/doom/` avec
`CDDIR=cd_doom` (Makefile:280-286).

## Vérifications PC

`tools/lev.py` (SEQUENCE.C:44-48, SOUND.C:233) et `verif_doom.py` (relit le fichier, :1-7) à étendre :
formule §2 (avec la garde −2) sur chaque état atteignable, **géométrie ≤ 255 − `tileBase`** (`tileBase` lu dans
STATIC.DAT : 95 ⇒ 160), sons < 80 et `soundTop` < 512 Ko avec les 20 statiques, `objectParams` = Σ (**5 joueur
+ 6 × 115 mobjs**, 3 porte, 4 ascenseur, **2 sector-switch**, 5 switch, 1 exit, 2 × n `OT_DOOM_DAMAGE`),
`firstParam` cumulés ; somme mémoire résidente (niveau + palettes + tuiles + séquences) contre le pool réel
= LWRAM 1 Mo + (0x06100000 − `_end` lu dans `build/doom/MAIN.map`) (UTIL.C:352-359 ; SPEC_CONVERTER §7).

# SPEC_CONVERTER — outils Python `tools/doom2ps` pour le disque Doom E1M1 (2026-09-14)

Spécification d'implémentation du côté **outils** du contrat `docs/doom/DOOM_ABI.md` (« ABI » ci-dessous).
`[src]` = fichier:ligne du moteur ; `[core]` = `Mimas/core` ; `[wad]` = mesuré ce jour sur `DOOM1.WAD`
(lecture seule) avec `tools/doom2ps/wad.py`. Rien de ce document ne modifie le moteur ; les modules
nouveaux vivent sous `tools/doom2ps/`, la sérialisation reste celle de `tools/duke2ps/lev_write.py`
(`serialize_*`, `engine_problems`, `atomic_write`) et le modèle celui de `lev_io.py`.

## 0. Écarts au contrat ABI, mesurés — **tous reportés dans DOOM_ABI.md (révision après jugement)**

| # | ABI disait | mesuré / lu | état après jugement |
|---|---|---|---|
| E1 | 71 tuiles d'armes, `tileBase = 71` (§5) | découpe lump par lump `[wad]` : PISG 1+4+4+2+4 = 15, PISF 1, SHTG 2+4+6+6 = 18, SHTF 1+1, PUNG 2+2+2+6 = 12 → **48** tuiles, 64 526 o RLE ; 10 familles : **95** / 130 063 o | ABI §5 corrigé ; **décision : 10 familles par défaut, `tileBase = 95`**, géométrie ≤ 160 (E1M1 141, marge 19 — vérificateur §7) ; `--e1m1-weapons` = 48 / ≤ 207 en repli |
| E2 | `OT_SECTORSWITCH` = 2 shorts (§6) | **L'ABI avait raison** : `case OT_SECTORSWITCH: constructSectorSwitch(); break;` (OBJECT.C:213-215) ; `constructSectorSwitch` lit exactement `sectorNm = suckShort(); channel = suckShort();` (AI2.C:665-676) ; `constructForceField(suckShort())` est le `case OT_FORCEFIELD` séparé (:249-250) | **rejeté (faux)** : 2 shorts. Un 3e short décale `objectPPos` et fait tomber `assert(level_object[o].firstParam==objectPPos)` (OBJECT.C:212) à l'objet suivant — boot impossible (en release, tous les objets suivants lisent des params faux) |
| E3 | portes : `doorHeight` = min plafond voisin − 4 − sol − fente 1 u (§6 ; ouverte = sol + 68 comme Doom) | E1M1 `[wad]` : secteurs 4/68/76/81 → 68 u ; `setDoorBlockBits` pose SHORTOPENING si hauteur du DOORWALL **< 80** (AI.C:4302-4305, constante PowerSlave) ; joueur `SPRITEFLAG_BSHORT` (AI.C:47) bloqué (SPRITE.C:414) | **attribué : SPEC_RUNTIME §9** (`AI.C:4304` `<80` → `<CFG_DOOR_FIT` = `GP_PLAYER_FIT_HEIGHT` 56, clé ajoutée à `params/doom.cfg`, ABI §6-§7) ; même valeur que `doom3d.py:66` |
| E4 | spécial 36 : « plus bas sol voisin » | type 36 = W1 sol vers **plus haut sol voisin + 8** `[core] p_spec.c:655, p_floor.c:298-305` : secteur 59 (sol 96, voisins −48/−48) → −40, `throw` 136 | ABI §6 corrigé : `lower = max(sol voisins)+8`, `upper = sol` |
| E5 | son = `len` de l'en-tête DMX | DMX a 16 octets de garde de chaque côté : Mimas joue `lump+24`, `len−32` (`src/i_sound_saturn.cxx:254-259`) | ABI §3 corrigé : `size = len − 32` (pair) ; **20 statiques (+RLAUNC 15 452) = 176 970 o**, 16 dynamiques 168 848, total 345 818 < 524 288, 36 < 80 |
| E6 | ≈ 270 Ko RLE (v1 #25) | RLE exact de STATIC.C:252-276 sur 64×64 `[wad]` : POSS 54 976, SPOS 53 602, TROO 59 726, effets/décor/ramassages ≈ 36 Ko → **≈ 204 Ko** hors PLAY (PLAY entier 57 435) | marge ×2 sur le bloc < 1 Mo ; somme mémoire totale : §7 « tailles » |
| E7 | `WR 88` → `channel = tag` + sector-switch | `sswitch_func` passe ON une fois et n'est remis à OFF que par `SIGNAL_SWITCHRESET` (AI2.C:648-661), que `elevator_func` n'émet jamais (seul `door_func`, AI.C:4375-4376) → l'ascenseur ne fait **qu'un** cycle | **décision ABI §6** : `elevator_func` émet `SIGNAL_SWITCHRESET` au retour en haut (1 ligne AI.C:4660-4663, SPEC_RUNTIME §9) ⇒ **`channel = 2` par défaut** ; `--lift-contact` (`channel = −1`, contact/press AI.C:4593-4624, pas de sector-switch) reste l'option de repli |
| E8 | portails de porte « hauteur 0 = fenêtre vide » (v1 #15) | `Emitter.add_wall` rejette un quad sans plan (`geom3d.py:400-404`, `quads_degeneres`) | fente `DOOR_SLIT = 1` u : plafond fermé = sol + 1 ; SHORTOPENING posé (1 < 56) puis recalculé par le moteur (E3) |

## 1. `wad2sprites.py` — S_* → tuiles 0x6A + séquences

**Sous-ensemble E1M1** (`doom_ids.json`, ABI §4) : sprites des états atteignables depuis les 8 champs
d'état des `MT` présents (skill retenu) + spawnables (TROOPSHOT, PUFF, BLOOD, TFOG, IFOG) + PLAY (N, W
seulement) `[wad]` : 26 familles, 259 lumps, 260 chunks dont PLAY 51 → **211 tuiles** (ABI §2).

| étape | règle | source |
|---|---|---|
| lump → chunks | patch `(w, h, lo, to)` (`Wad.patch`, `wad.py:116-137`) ; `c ∈ [0, ⌈w/64⌉)`, `r ∈ [0, ⌈h/64⌉)` ; `chunkx = −lo + 64c`, `chunky = −to + 64r` ; pixel `(i, j)` du patch → tuile `(c, r)` case `(i − 64c, j − 64r)` ; pixels hors masque = 0 | ABI §2 ; WALLS.C:2777-2790 |
| index 0 / 255 | 0 = transparent (RLE PIC.C:305-312), 255 forcé blanc (`objectPal[255]=0xffff`, PIC.C:631) → `remap_index(i)` : 0 → `sub0` (`doomtiles.py:46`), 255 → indice 1..254 le plus proche en RGB | PIC.C:631 |
| RLE | sur les 4 096 octets ligne par ligne : `[n0 ≤ 255 zéros][n1 ≤ 255 littéraux][n1 octets]` répété jusqu'à 4 096, y compris blocs vides `0 0` en fin (le décodeur boucle `while (outSize<nmPixels)`) ; `size ≤ 32 767` | STATIC.C:252-276 ; PIC.C:299-315 |
| tuile | `dict(flags=0x6A, palNm=objectPalette, rle=bytes)` ; **dédoublonnage** par sha1 du RLE (A2A8 : une tuile pour 2 vues) | PIC.C:687-690 |
| rotations | lump `XXXXFR` ou `XXXXFRFR` (`F` lettre, `R` 0..8) ; vue moteur `k` = rotation `k+1` ; lump 8 chars = deux vues, la seconde en **miroir** (`flags = 1`, `chunkx' = −chunkx − 64 + (w − 2·lo)` : leftoffset gardé comme r_things.c) | ABI §2 ; `rle8.mirror_chunkx` |
| séquences | `stride = 8` si la famille a des rotations, sinon 1 ; `seq = base + frame*stride + vue` ; une frame par séquence, `flags 0`, `sound −1`, `pad 0` ; séquence non peuplée = vide (`sequence[s] == sequence[s+1]`) | ABI §2 ; SEQUENCE.C:51-57 |
| carte | `sequenceMap[227]` = `−2` partout, `map[spritenum] = base | (stride==8 ? 0x8000 : 0)`, entrées 163-171 réservées (`OT_SW1` = 168 lue par AI2.C:598) ; entrées **172-203 obligatoirement < 0** (`markAnimTiles` PIC.C:129-161 prend toute valeur ≥ 0 pour une animation murale) ; la formule `seq()` teste `== −2` **avant** le bit 0x8000 (ABI §2) | SEQUENCE.C:72 ; AICOMMON.H:5 |
| terminaux | `sequence[N] = nmFrames` ; frame terminale `chunkIndex = nmChunks` (WALLS.C:2777 lit `frame+1`) ; **pas de chunk terminal** | STATIC.C:547-549 |

Frames max `[core] info.c` : POSS/SPOS/TROO 20, BAL1 4, PUFF 3, BLUD 2, TFOG 9, IFOG 4, BAR1 1, BEXP 4,
BON1/2 3, ARM1/2 1, PLAY 22, reste 0 ⇒ 458 séquences ; bloc `12 + 459×2 + 277×8 + 277×8 + 454 = 5 816 o`.

```python
Chunk = namedtuple("Chunk", "chunkx chunky pixels")            # pixels : 4096 o, 0 = transparent
def cut_patch(wad: Wad, lump: str, remap) -> tuple[list[Chunk], int, int]  # chunks, w, h
def rle8(pixels: bytes) -> bytes                              # STATIC.C:252-276, assert len <= 32767
def object_palette(playpal: list[tuple]) -> tuple[list[int], Callable[[int], int]]  # 256 BGR555, remap_index
def rotations(wad: Wad, family: str) -> dict[tuple[int, int], tuple[str, bool]]  # (frame, vue) -> (lump, miroir)
def families_for(ids: dict, mobj_types: set[int]) -> dict[str, dict]  # sprite -> {maxframe, rotations, reachable_frames}
def build_sprites(wad: Wad, ids: dict, families: dict, remap) -> SpriteSet
# SpriteSet: tiles (list[dict]), frames, chunks, sequence (list[int]), sequenceMap (227), budget (dict)
```

`budget` : tuiles (≤ 800 − géométrie − armes, PIC.C:32), octets RLE, bloc (< 1 Mo, SEQUENCE.C:30),
**géométrie ≤ 255 − tileBase** (LEVEL.C:69-72).

## 2. `wad2snd.py` — DS* → `SoundRec`

| champ | valeur | source |
|---|---|---|
| en-tête lump | `<HHI` : format 3, `rate` 11 025, `len` ; PCM = `lump[24 : 8+len−16]` | E5 ; `wad.py` |
| `size` | `len − 32`, arrondi pair (un 0x80 ajouté = silence) — `assert !(soundTop&1)` | SOUND.C:219 |
| `rate` | **registre SCSP tel quel** : `loadSound` le stocke (SOUND.C:212) et le poke (`reg[8]`, :290 ; `base+0x10` « not really the sample rate », :348) → `scsp_pitch(11025) = ((−2 << 11) & 0x7800) | 0 = 0x7000` (MAKESND.C:32-40) | SOUND.C:290,348 |
| `bps` / `loopStart` | 8 / −1 | STATIC.C:614-615 |
| PCM | `b ^ 0x80` (signé) | STATIC.C:630-631 |

**Statiques** (STATIC.DAT, bloc 3) : `int 8`, 8 shorts = `[0, 0, 0, 6, 0, 0, 0, 0]` (`ST_JOHN` = 0,
`ST_PUSHBLOCK` = 3 → index 6 = DOROPN ; SOUND.H:18-20), `int 20`, puis dans l'ordre ABI §3 : PISTOL SHOTGN
PUNCH SWTCHN SWTCHX NOWAY DOROPN DORCLS PSTART PSTOP PLPAIN PLDETH OOF ITEMUP WPNUP SLOP BAREXP TELEPT
STNMOV **RLAUNC** (176 970 o `[wad]` ; RLAUNC 15 452 : lancer de roquette, jamais dans la liste blanche
dynamique). Bloc = 4 + 16 + 4 + 20×16 + 176 970 = **177 314 o**. Les groupes non utilisés valent 0 plutôt
que −2 (`playStaticSound` indexe sans test, SOUND.H:38-39).

**Dynamiques** (.LEV) : liste blanche = union des 5 champs son des `MT` présents/spawnables + variantes
`A_Look`/`A_Scream` (`posit1-3, bgsit1-2, podth1-3, bgdth1-2`) + `firsht/firxpl`, moins les statiques ⇒
16 sons, 168 848 o. `level_objectSoundMap[227]` indexée par `sfxenum_t` `[core] sounds.c` : pistol 1,
shotgn 2, posit1-3 36-38, popain 27, podth1-3 59-61, posact 75, bgsit1-2 39-40, claw 55, bgdth1-2 62-63,
bgact 76, firsht 16, firxpl 17 ; valeur = index dans la liste, **−1000** si absent (reste < 0 après
`+= nmStaticSounds`, SOUND.C:247-248). Plafonds : 36 < 80 (SOUND.C:40), `soundTop` 345 818 < 524 288.

```python
def scsp_pitch(rate_hz: int) -> int
def sound_rec(wad: Wad, name: str) -> dict         # pcm (bytes), rate, bps, loopStart
STATIC_SOUNDS: list[str]                            # 20, ordre ABI §3
def dynamic_sounds(ids: dict, mobj_types: set[int]) -> list[str]
def object_sound_map(ids: dict, dyn: list[str]) -> list[int]   # 227 shorts
def static_sound_block(wad: Wad) -> bytes           # int 8, 8 shorts, int 20, 20 x (16 o + PCM)
```

## 3. `wad2static.py` — STATIC.DAT Doom (ordre SRUINS.C:1922-1932)

| # | bloc | contenu | source |
|---|---|---|---|
| 1 | écran de chargement | 256 × u16 BGR555 (bit 15) + `int 320` + `int 240` + 76 800 index ; TITLEPIC 320×200 décodé en patch (`wad.patch`), centré (bande 20 lignes d'index 0 haut/bas) ; `--loading black` = tout 0 | SRUINS.C:1098-1120 |
| 2 | feuille VDP2 | 262 144 octets 8 bpp 512×512 → VRAM+0x40000, NBG0 bitmap 256 couleurs ; **zéros, définitivement** : cette feuille ne sert qu'à `displayVDP2Pic` (chunks d'armes TILEVDP, SEQUENCE.C:270-290), pas aux chars VDP1 du HUD — l'habillage Doom est un tableau C (§3bis) | SRUINS.C:1080-1096 |
| 3 | sons statiques | §2 (20 sons, 177 314 o) | SOUND.C:231-241 |
| 4 | tuiles d'armes | `int n` + n × (`short 0x6A, short palNm=0, short size, RLE`) ; ordre = lumps du WAD, patch par patch, chunks `(c, r)` ligne par ligne ; **par défaut les 10 familles** PISG PISF SHTG SHTF PUNG CHGG CHGF MISG MISF SAWG ⇒ **n = 95** (130 063 o ; ABI §5) ; `--e1m1-weapons` = 5 familles, n = 48 (E1) | PIC.C:671-700, 709-711 |
| 5 | séquences d'armes | `int size` + `seqHeader` + frames + chunks + `nmSequences` shorts, **sans carte** ; `wseq(state) = state − 1` pour `S_LIGHTDONE (1) … S_BFGFLASH2 (89)` `[core] info.h:174-262` ; une frame par état (chunks du lump `sprite+frame`, `chunkx = −lo + 64c`, `chunky = −to + 64r`, `flags 0`, `sound −1`) ; états des armes absentes du WAD (plasma, BFG) vides ; + entrée terminale ; `wSequence[0] == 0` | SEQUENCE.C:92-132 ; STATIC.C:547-549 |

Numérotation `[core] info.h` : `S_PUNCH..S_PUNCH5` = 2-9, `S_PISTOL..S_PISTOLFLASH` = 10-17,
`S_SGUN..S_SGUNFLASH2` = 18-31, puis chaingun/roquettes/tronçonneuse ⇒ `wseq` peuplées pour toute famille
dont les lumps existent, 89 terminale ; l'`overlay` dès la séquence 50 (SEQUENCE.C:252-253) est retiré sous
`GP_GAME_DOOM` (SPEC_PLAYER §2.3). Contrôle : `PISGA0 57×62, lo −126, to −106` → chunk
`(126, 106)`, écran `x = 0 + cx(1) + 126 = 127` = Doom `160 + sx − lo`.

```python
def loading_screen(wad: Wad, lump: str | None = "TITLEPIC") -> bytes    # 77 320 o
def vdp2_sheet() -> bytes                                                # 262 144 o de zéros
def weapon_tiles(wad: Wad, ids: dict, families: list[str], remap) -> tuple[list[dict], dict[str, list[Chunk]]]
def weapon_sequences(ids: dict, chunks_by_lump: dict) -> tuple[list[dict], list[dict], list[int]]  # frames, chunks, sequence
def write_static(path: str, wad: Wad, ids: dict, *, loading: str, weapons: list[str]) -> dict    # tailles par bloc, tileBase
```

Taille attendue `[wad]` : 77 320 + 262 144 + 177 314 + (4 + 95×6 + 130 063) + ≈ 2 000 ≈ **649 Ko** (48 armes : ≈ 578 Ko).

### 3bis. `wad2hud.py` (+ `wad2font.py`) — art HUD en tableau C, `build/doom/doom_art.h`

Le moteur lit déjà son fond de barre comme tableau C à en-tête 8 o (STATBAR.C:74 `stat_bar[] = {0,0,1,64,
0,0,0,42, …}` = 320×42 ; SRUINS.C:1890-1891 `EZ_setChar(0, COLOR_4, *(int*)stat_bar, *(int*)(stat_bar+4),
stat_bar+8)`) : même forme, générée. Sortie **`$(BUILD)/doom_art.h`** = `build/doom/doom_art.h` (sur le chemin
d'inclusion, Makefile:110 `-I$(BUILD)` ; jamais commité ; règle Makefile ABI §8, `DOOMWAD ?= ../Mimas/cd/data/DOOM1.WAD`).

| tableau | lumps `[wad]` | forme | consommateur |
|---|---|---|---|
| `doom_stbar[8+320*32]` | `STBAR` 320×32 + `STARMS` 40×32 collé à x = 104 | `{int 320, int 32}` + index 8 bpp PLAYPAL | `DOOM_HUD.C` (SPEC_PLAYER §3.2) |
| `doom_faces[26][24*32]` | `STFST00..42`, `STFKILL0-4`, `STFEVL0-4`, `STFDEAD0` (24 × 29-31, calés en haut, 0 = transparent) | 768 o chacun | visage (§3.5) |
| `doom_keys[3][8*8]` | `STKEYS0-2` 7×5 centrés | 64 o chacun | clés (§3.4) |
| `doom_font_stcfn[]`, `doom_font_sttnum[]`, `doom_font_stysnum[]` | `STCFN033..095` ; `STTNUM0-9`, `STTPRCNT`, `STTMINUS` ; `STYSNUM0-9` | format `initFonts` PRINT.C:51-80 : `short h`, 32 o CLUT BGR555 (quantifiée, `duke2ps/quantize.py`), 256 largeurs, glyphes 4 bpp `h × ⌈l/2⌉` complétés à 8 px | `PRINT.C` (§3.3) |

```python
def hud_char(wad: Wad, lump: str, w: int, h: int, *, at=(0, 0), extra=None) -> bytes   # {w,h} + index 8 bpp
def font_table(wad: Wad, glyphs: dict[int, str], palette: list[int]) -> bytes             # PRINT.C:51-80
def write_doom_art(path: str, wad: Wad) -> dict                                            # tailles ; atomic_write
```

## 4. Things → objets (`things2objects.py`)

| règle | valeur | source |
|---|---|---|
| filtre | `flags & skill_bit` (`--skill 3` = index `sk_hard` UV → `bit = 1<<(3−1) = 4`, `[core] p_mobj.c:953-958`) et `not (flags & 16)` ; DoomEd 1 → joueur, 2-4/11 ignorés | 124 things `[wad]` (9 POSS, 16 SPOS, 4 TROO, 6 BAR1, 25 BON2, 13 BON1, 8 COLU, … ; 48 ambush) = **116 objets** (1 joueur + 115 mobjs) + 8 starts ; bit 2 (HMP) donnerait 100 / POSS 4 / SPOS 0 / TROO 2 |
| type | `OT = ids["MT→OT"][ids["ed→MT"][doomed]]` ; joueur 13 | ABI §1 |
| secteur | `conv.remap[conv.bsp.leaf_at(x, y)]` (`doom3d.py:157-181, 245, 660-661`) ; thing sur une feuille écartée ⇒ erreur | — |
| params mobj | `>6h` : `sector, x, y = floorLevel[sector], z = y_doom, angle = round(deg×4096/360), flags` (bit 3 ambush : 48 `[wad]`) | OBJECT.C:165-194 ; ABI §1 |
| joueur | `>5h` : `sector, x, y, z, angle(+90°)` ; premier de la liste (pas obligatoire, OBJECT.C:200-206 cherche) | `doom3d.py:656-669` ; `assemble.py:204` |
| `firstParam` | offsets cumulés ; `assert(level_object[o].firstParam == objectPPos)` | OBJECT.C:211 |

Repère `X = x_doom, Z = y_doom` (`doom3d.py:10-14`) ; le `×8/÷8` d'`assemble.py:196-204` disparaît (1:1).

```python
def things_to_objects(M: dict, conv: DoomConverter, ids: dict, *, skill: int = 3) -> tuple[list[dict], bytearray]
# -> objects [{type, firstParam}], params ; le joueur en tête
```

## 5. Portes, ascenseur, sol, interrupteur, sortie — E1M1 `[wad]`

Spéciaux réellement présents : lignes **151, 152** (type 1 → secteur 4), **247, 248** (→ 68), **340, 341**
(→ 76), **324, 325** (→ 81), **308** (36, tag 1 : secteurs 72|73), **195** (88, tag 2 : secteurs 60|71),
**330** (11, S1, une face, secteur 82, texture `SW1STRTN` 64×128) ; 8 × 48 ignorées. Secteurs : `special`
**7 (13, 55, 57, 61) → `OT_DOOM_DAMAGE` (§5.2)** ; 9 (68, 69, 70), 8 (44, 45), 1 (40), 12 (72) — hors J3 ;
tags **59** (tag 1) et **70** (tag 2) mobiles.

### 5.1 Géométrie fermée + course (remplace `open_doors`, `doom3d.py:589-621`)

`open_doors` reste derrière `--static-doors` (contrôle visuel). Sinon, par secteur mobile `S` :

| élément | fermé (fichier) | course | rangées |
|---|---|---|---|
| porte (plafond monte) | `ceil = floor + DOOR_SLIT` (E8) ; portail voisin→S `[floor, ceil]`, linteau `[ceil, ch_voisin]` (texture `upper` du sidedef, calage `voff` de `wall_tex` `doom3d.py:258`), murs de S (DOORTRAK) `[floor, ceil]`, plafond de S | `doorHeight = min(ch voisins) − 4 − floor` = 68 | linteau : `tileHeight = ⌈h/64⌉`, reliquat dans la rangée **haute** (v1 #15) ; la tuile est faite pour la cellule fermée (E4.1c) et s'écrase à l'ouverture (dégradé accepté) |
| ascenseur / sol (sol descend) | sol à `upper` (position haute = fichier) ; contremarche voisin→S `[fh_voisin, floor_S]` (texture `lower`), portail `[floor_S, ceil_S]` | `throw = upper − lower` : 70 → 104 − (−48) = 152 ; 59 → 96 − (−40) = 136 (E4) | contremarche de hauteur 0 fermée ⇒ même fente 1 u |

**Sommets mobiles = par rôle, jamais par coordonnée** (sol et plafond fermés coïncident). L'Emitter ne
partage aucun sommet (`geom3d.py:384-393`) : `emit_edge`/`emit_leaf` (`doom3d.py:412, 354`) notent, par mur
émis, les coins à la hauteur mobile — porte : plafond de S (toute la flat), `v[0], v[1]` des murs de S et
des portails voisin→S, `v[2], v[3]` des linteaux ; ascenseur : sol de S, `v[2], v[3]` des murs/portails,
`v[0], v[1]` des contremarches. Chaque bloc contigu → `sPBVertex {vStart, vNm ≤ 255, flags 0}`
(SLEVEL.H:109-113 ; seul `dy` est appliqué, SPRITE.C:852-890).

**Push block** `sPBType` (SLEVEL.H:97-104, 9 shorts) : `enclosingSector = S`, `startWall..endWall` dans
`PBWall` = murs de S + portails voisin→S + linteaux/contremarches (ce sont les cibles du `SIGNAL_PRESS`
par `registerPBObject`, OBJECT.C:141-152, et de `setDoorBlockBits` AI.C:4295-4308), `startVertex..endVertex`
dans `PBVert`, `floorSector = S` pour ascenseur/sol (les sprites suivent, SPRITE.C:873-887), `−1` porte,
`dx = dy = dz = 0`. Flag **`WALLFLAG_DOORWALL` (0x20)** sur les portails de porte seulement (recalcul
SHORTOPENING sur `v[1].y − v[2].y`, AI.C:4302-4307 → E3).

### 5.2 Objets

| objet | OT | params `>Nh` | E1M1 |
|---|---|---|---|
| porte | 48 | `pb, −1, doorHeight` (`constructDoor` AI.C:4384-4398) | 4 (pb 0-3), `doorHeight` 68 (bloquant tant qu'E3 n'est pas dans le moteur : 68 < 80) |
| ascenseur WR 88 | 49 `OT_NORMALELEVATOR` | `pb, lower, upper, channel` (OBJECT.C:255-262 ; AI.C:4686-4699) | secteur 70 : `−48, 104, 2` (réarmé par le moteur, E7/ABI §6) — `−1` sous `--lift-contact` |
| sol W1 36 | 61 `OT_STUCKDOWNELEVATOR` | idem, `channel = 1` ; un aller, reste en bas (AI.C:4649-4653) | secteur 59 : `−40, 96, 1` (E4) |
| déclencheur W | 91 `OT_SECTORSWITCH` | **`sectorNm, channel` — 2 shorts** (E2 rejeté : AI2.C:665-676) ; `SIGNAL_ENTER` = la caméra entre dans la **feuille** (SPRITE.C:731-733) | ligne 308 : une par feuille bordant la ligne des deux côtés (`adjacency`), canal 1 ; ligne 195 idem canal 2 (aucune sous `--lift-contact`) |
| interrupteur S1 | 168 `OT_SW1` | `sectorNm, channel = 900, ox, oy, oz` = centre du mur (son de l'interrupteur ; le press vaut sur tout le mur, `CFG_SWITCH_AIM`) | ligne 330, feuille de 82 |
| sortie | 176 `OT_DOOM_EXIT` | `channel = 900` | 1 |
| secteur à dégâts | 179 `OT_DOOM_DAMAGE` | `sectorNm, hp` (ABI §6 ; special 7 → 5) | une par **feuille** des secteurs 13, 55, 57, 61 (`conv.remap`) |

**Interrupteur** : 4 séquences de **tuiles de géométrie 0x32** (`base` OFF, `+1` anim ON, `+2` anim OFF,
`+3` ON, AI2.C:522-577), `sequenceMap[168] = base` ; le chunk ne porte que `tile`, écrit dans
`level_texture[t]` (u8, AI2.C:549). OFF = `SW1STRTN`, ON = `SW2STRTN` via `tile_from_wall` ; la tuile OFF
doit être **unique dans la feuille** (AI2.C:606-632) ⇒ `doomtiles` lui réserve une tuile dédiée.

```python
Specials = namedtuple("Specials", "doors lifts floors wswitch sswitch exits")   # listes de (ligne/secteur, tag, …)
def specials_of(M: dict) -> Specials
def mobile_bounds(M: dict, sector: int, kind: str) -> tuple[int, int]         # (lower, upper) règle Doom
def emit_mobile(conv: DoomConverter, S: int, kind: str) -> MobileTag         # appelé depuis emit_leaf/emit_edge
def push_blocks(conv: DoomConverter, tags: list[MobileTag]) -> tuple[list[dict], list[dict], list[int]]  # pushBlocks, PBVert, PBWall
def special_objects(M, conv, ids, specials, pb_index: dict, *, lift_contact: bool) -> tuple[list[dict], bytearray]
```

## 6. Assemblage — `make_e1m1.py` (une commande)

Pas de donneur (contrairement à `assemble.py`) : tout est produit. Étapes, sorties atomiques :

| # | étape | sortie |
|---|---|---|
| 1 | `info2tables.py` (ABI §4) | `game/doom/DOOM_TABLES.C`, `build/doom/doom_ids.json` |
| 2 | `doom3d.py --mobile` | `e1m1_geom3d.json` + `mobile` (tags, pushBlocks, PBVert, PBWall) |
| 3 | `doomtiles.py` (+ tuiles d'interrupteur) | `e1m1_tiles.json` |
| 4 | `wad2sprites`, `wad2snd`, `things2objects`, `special_objects` | en mémoire |
| 5 | `assemble_doom()` → modèle `lev_io` : `sky` (v1 #22 : SKY1 256×128 ×2 → 512×256, table K recopiée d'un retail), `level` (géométrie + objets + PB), `sounds` (map 227 + 16), `palettes` (`objectPalette = 0`, palette 0 = PLAYPAL, entrée 0 = 0x0000), `tiles` = **géométrie 0x32 en tête** puis chunks 0x6A, `sequences` | **`cd_doom/E1M1.LEV`** (nom par défaut = celui gravé dans `doomLevelNames[]`, ABI §9 ; `--name TOMB.LEV` seulement pour le disque de contrôle sans `GP_GAME_DOOM`) |
| 6 | `write_static` | `cd_doom/STATIC.DAT` |
| 6bis | `write_doom_art` (§3bis) | `build/doom/doom_art.h` |
| 7 | copie `INITLOAD.DAT`, `INTRO.PCS` retail depuis `cd/` (jamais commités ; `0`/`MAIN.BIN` viennent du build) ; contrôle que chaque nom de `doomLevelNames[]` (`game/doom/DOOM_GAME.C`) existe dans `cd_doom/` | `cd_doom/` |
| 8 | `lev_write.engine_problems` + `verif_doom.py` + `verif_static.py` | code de retour |

**`cd_doom/` est à la racine du dépôt** (`.gitignore:10`) : `CDDIR` est relatif à la racine (Makefile:280
`CDDIR ?= cd`, :286 `CD_DATA := $(wildcard $(CDDIR)/*)`) — un `build/doom/cd_doom/` avec `CDDIR=cd_doom` donne
`CD_DATA` vide, l'avertissement « holds no game data » (:291-294) et un ISO sans `STATIC.DAT` ⇒ `fs_open` asserte
(SRUINS.C:1922-1925). Disque : `build.ps1 PARAMS=params/doom.cfg CDDIR=cd_doom iso` (arbre `build/doom/`
Makefile:82-84 ; `build.ps1:2-3` passe les arguments à `make` tels quels).

```python
def assemble_doom(geom: dict, tiles: dict, sprites: SpriteSet, sounds: dict, objects, params, pb) -> dict  # modèle lev_io
def main(argv=None) -> int      # --wad --map --skill --loading --lift-contact --static-doors --e1m1-weapons --name --out-dir (défaut cd_doom)
```

## 7. Vérificateurs PC

`verif_doom.py` relit le fichier (`:1-7`) ; test 9 (« un seul objet », `:199-201`) est remplacé et les
tests suivants ajoutés (tous « relire le fichier, rejouer le moteur à la ligne ») :

| test | rejoue | critère |
|---|---|---|
| objets dans leur secteur | `pointInSectorP` sur `(x, y, z)` de chaque mobj (murs verticaux, marge −F(1)/4) | **116** (1 joueur + 115 mobjs), 0 hors secteur ; `y == floorLevel` |
| `firstParam` cumulés | Σ (**5 joueur + 6 × 115 mobjs**, 3 porte, 4 ascenseur, **2 sector-switch**, 5 switch, 1 exit, 2 × n `OT_DOOM_DAMAGE`) | `== nmObjectParams` |
| séquences atteignables | pour chaque `(MT, état atteignable, vue 0..7)` : `seq()` ABI §2 (garde `map == −2` d'abord) → `sequence[s] < sequence[s+1]` et chunks → `tile < nmTiles`, flags 0x6A ; `sequenceMap[172..203] < 0` | 0 séquence vide atteignable |
| sons | statiques 20 dans l'ordre, `map[3] == 6` ; `objectSoundMap[sfx]` ≥ 0 pour tout son d'un `MT` présent hors statiques ; `rate == 0x7000`, `size` pair, `size == len − 32` arrondi | 36 < 80 ; Σ `size` + 36 en-têtes < 524 288 |
| tuiles | géométrie = préfixe 0x32 de longueur `G`, `G + tileBase ≤ 255` (`tileBase` lu dans STATIC.DAT : 95 ⇒ **G ≤ 160**, E1M1 141) ; total < 800 | — |
| push blocks | `PBWall` dans `[0, nmWalls)`, `PBVert.vStart+vNm ≤ nmVerticies`, chaque DOORWALL a `v[1].y − v[2].y == DOOR_SLIT` fermé ; `doorHeight`/`throw` == règle Doom recalculée depuis le WAD (36 : plus haut sol voisin + 8) | 4 portes + 2 ascenseurs |
| interrupteur | tuile OFF unique dans la feuille ; `sequenceMap[168]` + 3 < nmSequences | — |
| barils | distance de chaque BAR1 aux murs de sa feuille ≥ 21 + 16 (sphère rayon = height/2, SPEC_RUNTIME §2 : un baril de 21 u dans un couloir de 64 u n'est plus contournable) — sinon avertissement | 6 barils |
| tailles | bloc niveau < 900 000 (LEVEL.C:41), séquences < 1 Mo ; **somme résidente** niveau + palettes + tuiles + séquences (E1M1 ≈ 233 978 + 12 290 + ~580 Ko + 204 Ko + 5 816 ≈ 1,04 Mo) **≤ pool réel** = LWRAM 1 Mo (0x200000-0x300000) + (0x06100000 − `_end`) lu dans `build/ndebug/stext/doom/MAIN.map`, celui du disque de test, le plus gros qu'on grave (sans ce map, la conversion s'arrête ; le build ASSERT, ~12 Ko plus gros, perd la marge de la sauvegarde ; UTIL.C:352-359) − marge de croissance de MAIN.BIN (plan §4.2, 80-100 Ko) — pas la constante `DEMANDE_MAX` d'`assemble.py:45` | — |

`verif_static.py` (nouveau) : 5 blocs dans l'ordre, `320/240`, 262 144 o de zéros, `int 8`, `int 20`, tuiles 0x6A
décodables (RLE → 4 096), `n == 95` (ou 48), `wSequence[0] == 0`, 90 entrées, `size` exact. `tools/lev.py --stats`
(`:259-300`) reste l'inventaire (`distinct_tiles`, `tile_kinds`) ; `lev_io.diff == []` et `L.validate == []`
obligatoires.

## 8. Ordre d'implémentation (tâches ≤ 1 j, critère PC)

| j | tâche | critère PC |
|---|---|---|
| 1 | `wad2snd.py` + bloc statique | 20 + 16 sons, tailles E5 (176 970 / 168 848), `scsp_pitch` = 0x7000 ; test unitaire de relecture `lev_io._decode_sounds` |
| 2 | `rle8`, `cut_patch`, `object_palette` | RLE ↔ décodeur Python (copie de PIC.C:299-315) identité sur les 259 lumps ; 260 chunks, ≈ 204 Ko |
| 3 | `wad2sprites.build_sprites` | 458 séquences, 211 tuiles, `verif` « séquences atteignables » vert sur POSS/SPOS/TROO |
| 4 | `wad2static.py` + `wad2hud.py`/`wad2font.py` + `verif_static.py` | STATIC.DAT ≈ 649 Ko, `tileBase = 95`, séquence `wseq` vérifiée sur tous les états d'armes présents ; `doom_art.h` compile (`gcc -fsyntax-only`) |
| 5 | `things2objects.py` + `verif` objets | 116 objets, tous dans leur secteur |
| 6 | `doom3d --mobile` : portes fermées + tags + `push_blocks` | 4 PB, `verif` push blocks vert, accessibilité `verif_doom` :230-292 inchangée hors portes |
| 7 | ascenseur/sol + sector-switch (2 shorts) + interrupteur + sortie + `OT_DOOM_DAMAGE` | 6 PB, objets 48/49/STUCKDOWN/91/168/176/179 ; tuile OFF unique ; `firstParam` cumulés |
| 8 | `make_e1m1.py`, `.gitignore`, `make iso` | une commande → `cd_doom/E1M1.LEV` + `STATIC.DAT` + `doom_art.h` + `.iso` ; `engine_problems == []` ; ISO contient STATIC.DAT |
| 9 | J3 disque au propriétaire ; retours « vu → sens » | table vu → sens remplie (jamais l'émulateur ici) |

Prérequis moteur hors de ce document (SPEC_RUNTIME) : `info2tables.py` (ABI §4), crochet `game_placeObject`
(ABI §1), **E3** (AI.C:4304, SPEC_RUNTIME §9 — sans lui aucune porte d'E1M1 ne laisse passer le joueur), les
hooks nom/warp/logos (ABI §9) sans lesquels `level 0` ouvre `KARNAK.LEV`.

## 9. Risques

| risque | fait | parade |
|---|---|---|
| ~~sprites coupés aux feuilles~~ | **infondé** : `drawSprites` remet le clip utilisateur à la fenêtre entière (`RECTCLIP 1` WALLS.C:27 ; `:2634-2638` `EZ_userClip(XMIN..XMAX/YMIN..YMAX)`) avant de dessiner ; seuls les sprites `SPRITEFLAG_FOOTCLIP` sont clippés, et sous les pieds (`:2767-2775`) ; le clip secteur de `:2423-2429` sert à `drawSector` et est écrasé | aucune — `merge_leaves`/clip élargi sans objet |
| `F(32)` | sprite omis si `tformed.z < F(32)` (WALLS.C:2701-2702) ; un CLIP (rayon 8) est ramassé à 24 u de la caméra ⇒ pop 8 u avant | **SPEC_RUNTIME §9** : `CFG_SPRITE_NEARCLIP F(10)` |
| cache 8 bpp = **31** tuiles | `initPicSystem(i, {28, 31, 1, 10, 12})` SRUINS.C:1903 : arme (≤ 6 chunks SHTG) + chunks visibles ; au-delà, `map()` par frame | 1 chunk par monstre (POSS/SPOS/TROO ≤ 64×64), ELEC 2 ; surveiller `nmSwaps` |
| séquence vide dessine la suivante | `sequence[s]` et `frame+1` lus sans test (WALLS.C:2777-2779) | vérificateur « atteignables » ; `DOOM_ACTOR` ne pose jamais une séquence vide |
| porte qui s'écrase | cellules étirées, pas glissées | dégradé accepté J3 ; slab mobile = étude J4 |
| WR ascenseur un seul cycle | E7 | réarmement moteur (ABI §6) ; `--lift-contact` en repli |
| `sub0`/255 | 2 indices déplacés | vérifier BAL1 (plein feu) |
| `MAXNMPICS 800` | 95 + 141 + 211 = **447** ; RLE pire cas (pixels alternés 0/x : 3 o par 2 px) = **6 144 o** < 32 767 | — |
| géométrie > 160 tuiles sur un autre niveau | `tileBase 95` laisse 160 tuiles u8 (E1M1 : 141) | `--e1m1-weapons` (48 ⇒ 207) ou fusion de tuiles ; vérificateur §7 |

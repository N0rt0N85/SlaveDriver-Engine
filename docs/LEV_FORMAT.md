# The .LEV level file format

A `.LEV` file holds one complete level: sky, geometry, objects, sounds, textures and
sprite animations.  It is read strictly sequentially by five loaders called in a row on
the same file descriptor (`runLevel`, SRUINS.C):

| # | Loader | Source | Content |
|---|--------|--------|---------|
| 1 | `initPlax` | PLAX.C | sky palette, sky bitmap, sky scroll table |
| 2 | `loadLevel` | LEVEL.C | geometry: header + 14 arrays |
| 3 | `loadDynamicSounds` | SOUND.C | object→sound map + PCM samples |
| 4 | `loadTiles` | PIC.C | texture palettes + tile records |
| 5 | `loadSequences` | SEQUENCE.C | sprite animation tables |

Everything is **big-endian** (SH-2).  Structs use natural alignment; the layouts below
are exactly what `fs_read` pulls into them, verified against the retail US files (e.g.
TOMB.LEV: size field 295907 = 56-byte header + arrays; KARNAK.LEV: 527285, same rule).

The engine opens levels as `+NAME.LEV` — the `+` prefix is a Psy-Q-era host-path
marker; the Saturn build strips it (FILE.C `fs_open`), so it is not part of the filename
on disc.  The level list lives in BIGMAP.C `levelGraph`.

## 1. Sky block — fixed size, offsets 0x0 .. 0x20708

| Offset | Size | Content |
|--------|------|---------|
| 0x00000 | 512 | sky palette: 256 × u16 RGB555, loaded into CRAM bank 7 |
| 0x00200 | 4 | i32 bitmap width, asserted == 512 |
| 0x00204 | 4 | i32 bitmap height, asserted == 256 |
| 0x00208 | 131072 | 512×256 8bpp sky bitmap, copied straight into VDP2 VRAM A1 |
| 0x20208 | 1280 | 320 × i32: RBG0 coefficient per screen column (sky perspective), copied to VDP2 VRAM A0 |

The disabled generator at the end of `initPlax` shows how the 320 coefficients are
derived (an atan() curve across the 320-pixel screen).

## 2. Geometry block — `loadLevel` (LEVEL.C)

| Size | Content |
|------|---------|
| 4 | i32 `size` = 56 + total size of the 14 arrays (sanity only, asserted < 900000) |
| 56 | `sLevelHeader`: 14 × i32 counts, in this order: nmSectors, nmWalls, nmVerticies, nmFaces, nmTextureIndexes, nmLightValues, nmObjects, nmObjectParams, nmPushBlocks, nmPBWalls, nmPBVert, nmWaveVert, nmWaveFace, nmCutSectors |

Then the 14 arrays back to back, in **this** order (all structs from SLEVEL.H):

| Array | Element | Bytes | Count |
|-------|---------|-------|-------|
| sectors | `sSectorType` | 24 | nmSectors |
| walls | `sWallType` | 48 | nmWalls |
| vertices | `sVertexType` | 8 | nmVerticies |
| faces | `sFaceType` | 10 | nmFaces |
| objects | `sObjectType` | 4 | nmObjects |
| push blocks | `sPBType` | 18 | nmPushBlocks |
| push-block vertices | `sPBVertex` | 4 | nmPBVert |
| wave vertices | `WaveVert` | 16 | nmWaveVert |
| wave faces | `WaveFace` | 8 | nmWaveFace |
| push-block walls | s16 | 2 | nmPBWalls |
| object params | u8 | 1 | nmObjectParams |
| texture indexes | u8 | 1 | nmTextureIndexes |
| light values | s8 | 1 | nmLightValues |
| cut planes | u8 row | 128 | nmCutSectors rows |

### sSectorType — 24 bytes

| Off | Type | Field | Meaning |
|-----|------|-------|---------|
| 0 | 4 | `object` | runtime pointer, file value ignored |
| 4 | s16×3 | `center` | sector center, world units |
| 10 | s16 | `floorLevel` | average floor height |
| 12 | s16 | `firstWall`, `lastWall` | inclusive range into the wall array |
| 16 | s16 | `light` | sector light bias (−16..16) |
| 18 | s16 | `flags` | `SECFLAG_*` (water, cut-sort, exploding walls, laser, no-map) |
| 20 | s8 | `cutIndex`, `cutChannel` | indexes into the cut-plane table |
| 22 | s16 | `pad` | |

### sWallType — 48 bytes

Walls are all the surfaces of a sector — vertical faces, floor and ceiling alike are
wall records.

| Off | Type | Field | Meaning |
|-----|------|-------|---------|
| 0 | i32×3 | `normal` | plane normal, 16.16 fixed point |
| 12 | i32 | `d` | plane equation constant |
| 16 | 4 | `object` | runtime pointer, file value ignored |
| 20 | s16 | `flags` | `WALLFLAG_*`; bit 0 = PARALLELOGRAM selects which of the two shapes below is used |
| 22 | u16 | `textures` | PARALLELOGRAM only: start index into the texture-index array |
| 24 | s16 | `firstFace`, `lastFace` | mesh walls only: inclusive face range |
| 28 | u16 | `firstVertex`, `lastVertex` | mesh walls only: vertex range (face `v[]` entries are relative to `firstVertex`) |
| 32 | u16×4 | `v` | the wall's 4 corner vertices (global indexes) |
| 40 | s16 | `nextSector` | sector behind this wall, or −1 |
| 42 | u16 | `firstLight` | start index into the light-value array |
| 44 | s16 | `pixelLength` | |
| 46 | u8 | `tileLength`, `tileHeight` | PARALLELOGRAM only: tile grid, v0→v1 × v1→v2 |

A PARALLELOGRAM wall is a regular grid of `tileLength × tileHeight` 64×64 tiles.  Its
texturing comes from the texture-index array: **2 bytes per grid cell**, row-major from
`textures` — even byte = orientation (index 0..7 into `pattern[][4]`, WALLS.C), odd
byte = tile number.  Its lighting comes from the light-value array: **1 byte per grid
vertex** (`(tileLength+1) × (tileHeight+1)` values, row-major from `firstLight`,
brightness 0..31, consumed by `rectTransform`).  A mesh wall instead lists quads in the
face array and takes lighting from the per-vertex `light` field.

### The remaining structs

* `sVertexType` (8): s16 `x,y,z` world units, s8 `light` (0..31), s8 pad.
* `sFaceType` (10): u16 `v[4]` (relative to the owning wall's `firstVertex`), u8
  `tile`, s8 pad.
* `sObjectType` (4): s16 `type` (the `ObjectType` enum, SLEVEL.H — 227 types), s16
  `firstParam` = index into the object-params byte array; the parameter bytes are
  interpreted per type (OBJECT.C).
* `sPBType` (18): 9 × s16 — `enclosingSector`, `startWall`/`endWall` (into the
  push-block wall array), `startVertex`/`endVertex` (into the push-block vertex array),
  `floorSector` (or −1), `dx`,`dy`,`dz` (push blocks: movable geometry).
* `sPBVertex` (4): u16 `vStart`, u8 `vNm`, s8 `flags` (axis-lock bits).
* `WaveVert` (16): i32 `pos`,`vel`, s16 `connect[4]` (−1 = none); `WaveFace` (8): s16
  `connect[4]` (wavy water surfaces).
* cut planes: `nmCutSectors × 128` bytes; `cutPlane[a][b]` gives the separating plane
  between the sectors of cut indexes `a` and `b` (draw-order resolution, WALLS.C).

After reading, `loadLevel` adds the tile base to every **odd** texture-index byte and to
every face `tile`: tile 0 of a level is the first tile after the weapon tiles that
STATIC.DAT already loaded (`nmWeaponTiles`).

## 3. Dynamic sounds — `loadDynamicSounds` (SOUND.C `loadSoundSet`)

| Size | Content |
|------|---------|
| 4 | i32 map count, asserted == 227 (`OT_NMTYPES`) |
| 227×2 | s16 per object type: index of its sound in this block |
| 4 | i32 `nmSounds` |
| — | `nmSounds` sound records |

Sound record: i32 `size`, i32 `sampleRate`, i32 `bps` (8 or 16), i32 `loopStart` (−1 =
no loop), then `size` bytes of raw PCM, copied verbatim into SCSP sound RAM.  The map
indexes are offset at load by the number of static sounds STATIC.DAT installed first.

## 4. Tiles — `loadTiles` (PIC.C)

**Palettes** (`loadPalletes`): i32 `size`, then `size` bytes: a leading u16 `n`, then
palettes of 256 × u16 RGB555 each (palette *p* starts at word offset `1 + 256p`).
Palette `n` is the "object palette": it is copied to CRAM as-is, in 4 progressively
darkened copies (`NMOBJECTPALLETES` = 5, the object shading ramp) and as a white flash
palette.

**Tile set** (`loadTileSet`): i32 `nmTiles`, then per tile a u16 `flags`
(`TILEFLAG_*` combination) followed by a payload:

| flags | Payload |
|-------|---------|
| 0x32 = 64x64\|16BPP\|PALLETE | u16 palette#, 4096 bytes of 8bpp palette indexes |
| 0x34 = 32x32\|16BPP\|PALLETE | u16 palette#, 1024 bytes of 8bpp palette indexes |
| 0x72 = 64x64\|16BPP\|PALLETE\|RLE | u16 palette#, u16 size, `size` bytes RLE |
| 0x6A = 64x64\|8BPP\|RLE\|PALLETE | u16 palette#, u16 size, `size` bytes RLE |
| 0x6C = 32x32\|8BPP\|RLE\|PALLETE | u16 palette#, u16 size, `size` bytes RLE |
| 0x01 = VDP2 | 8 bytes: 4 × s16 x, y, w, h into the VDP2 sprite sheet (from STATIC.DAT) — no pixel data |

RLE (decoded in PIC.C `map`): repeat `[u8 zeroRun][u8 literalRun][literalRun bytes]`
until the tile's pixel count is produced; index 0 / the zero runs are transparent.

## 5. Sequences — `loadSequences` (SEQUENCE.C)

i32 `size`, then one block of exactly `size` bytes:

| Content | Element |
|---------|---------|
| `seqHeader` | 3 × i32: nmSequences, nmFrames, nmChunks |
| frames | nmFrames × `sFrameType` (8): s16 chunkIndex, s16 flags, s16 sound (−1 = none), 2 pad (asserted 0) |
| chunks | nmChunks × `sChunkType` (8): s16 chunkx, chunky, tile, s8 flags, s8 pad (asserted 0) |
| sequence starts | nmSequences × s16, first entry asserted 0 |
| object→sequence map | 227 × s16 (`OT_NMTYPES`) |

Chunk `tile` gets the same tile-base offset as the geometry; frame `sound` gets the
static-sound-count offset.  A sequence is a run of frames; each frame assembles one or
more 64×64 chunks at (chunkx, chunky) offsets into a sprite.

## Companion files

`STATIC.DAT` is read before every level (SRUINS.C) and supplies what `.LEV` files build
on: the loading screen, the VDP2 sprite sheet, the static sound set, the weapon tiles
and the weapon sequences.  The original converter producing `.LEV` files is
`UTIL/CONVERT.C` (a DOS tool compiled per level against the Dex editor's output).

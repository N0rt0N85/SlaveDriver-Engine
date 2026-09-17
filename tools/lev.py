"""lev.py - verified reader for the Saturn PowerSlave .LEV level block (SlaveDriver engine).

Read order = runLevel SRUINS.C:1935-1943: initPlax (PLAX.C:84-115) -> loadLevel (LEVEL.C:32-75)
-> loadDynamicSounds (SOUND.C:243-248 / loadSoundSet :231-241 / loadSound :199-228)
-> loadTiles (PIC.C:713-716: loadPalletes :611-625, loadTileSet :671-707) -> loadSequences (SEQUENCE.C:24-89).
Field layout of the level block = UTIL/CONVERT.C:3249-3388 writeLevel, structs = SLEVEL.H.
Everything is big-endian (SH-2). Sizes: sector 24, wall 48, vertex 8, face 10, object 4, PB 18,
PBVert 4, WaveVert 16, WaveFace 8, PBWall 2, cutPlane 128 per cut sector (MAXCUTSECTORS, SLEVEL.H:172).

Usage:  python tools\\lev.py [--stats OUT.json] [--dump DIR] [FILE.LEV ...]
        (defaults: all refs\\extract\\PS\\*.LEV, build\\tmp-b2d\\sat_stats.json, build\\tmp-b2d\\sat\\)
"""
import os, sys, json, glob, struct
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PS_DIR = os.path.join(ROOT, 'refs', 'extract', 'PS')
OUT_DIR = os.path.join(ROOT, 'build', 'tmp-b2d')

MAXCUTSECTORS = 128        # SLEVEL.H:172
OT_NMTYPES = 227           # SLEVEL.H:22-81 enum ObjectType (RETAIL_DISCS.md par.4)
MAXNMSOUNDS = 80           # SOUND.C:40
MAXNMSECTORS, MAXNMWALLS = 600, 5500   # UTIL.H:21-22

# SLEVEL.H:128-148
WALLFLAGS = [(0x01, 'PARALLELOGRAM'), (0x02, 'INVISIBLE'), (0x04, 'SLIPPERY'), (0x08, 'BLOCKSSIGHT'),
             (0x10, 'NOTSTEPABLE'), (0x20, 'DOORWALL'), (0x40, 'PARALLAX'), (0x80, 'LAVA'),
             (0x100, 'BLOCKED'), (0x200, 'WATERSURFACE'), (0x400, 'WATERBNDRY'), (0x800, 'CLIFFBNDRY'),
             (0x1000, 'SHORTOPENING'), (0x2000, 'SWAMP'), (0x4000, 'COLLIDEASFLOOR'), (0x8000, 'EXPLODABLE')]
# SLEVEL.H:174-179
SECFLAGS = [(1, 'WATER'), (2, 'CUTSORT'), (4, 'SEEN'), (8, 'EXPLODINGWALLS'), (16, 'LASERSECTOR'), (32, 'NOMAP')]
# SLEVEL.H:192-198
T_VDP2, T_64, T_32, T_8BPP, T_16BPP, T_PAL, T_RLE = 1, 2, 4, 8, 0x10, 0x20, 0x40

HEADER_FIELDS = ['nmSectors', 'nmWalls', 'nmVerticies', 'nmFaces', 'nmTextureIndexes', 'nmLightValues',
                 'nmObjects', 'nmObjectParams', 'nmPushBlocks', 'nmPBWalls', 'nmPBVert', 'nmWaveVert',
                 'nmWaveFace', 'nmCutSectors']   # SLEVEL.H:5-20, written CONVERT.C:3256-3269

# struct formats (big-endian), one record each; sizes checked against LOADPART sizeof
SECTOR_FMT = '>i3hhhhhhbbh'         # SLEVEL.H:181-190 / CONVERT.C:3273-3285 : object(int 0), center[3], floorLevel, firstWall, lastWall, light, flags, cutIndex, cutChannel, pad
WALL_FMT = '>iiiiihHhhHHHHHHhHhBB'   # SLEVEL.H:150-170 / CONVERT.C:3292-3312
VERTEX_FMT = '>hhhbb'               # SLEVEL.H:115-118 / CONVERT.C:3315-3319 (x,y,z = f(16.16) = world units)
FACE_FMT = '>HHHHBb'                # SLEVEL.H:120-124 / CONVERT.C:3322-3328
OBJECT_FMT = '>hh'                  # SLEVEL.H:92-95 / CONVERT.C:3331-3333
PB_FMT = '>hhhhhhhhh'               # SLEVEL.H:97-104 / CONVERT.C:3336-3345 (6 shorts + int 0 + short 0 = dx,dy,dz zeroed)
PBVERT_FMT = '>HBb'                 # SLEVEL.H:109-113 / CONVERT.C:3348-3351
WAVEVERT_FMT = '>iihhhh'            # SLEVEL.H:83-86 / CONVERT.C:3354-3360
WAVEFACE_FMT = '>hhhh'              # SLEVEL.H:88-90 / CONVERT.C:3363-3367
for _fmt, _sz in [(SECTOR_FMT, 24), (WALL_FMT, 48), (VERTEX_FMT, 8), (FACE_FMT, 10), (OBJECT_FMT, 4), (PB_FMT, 18),
                  (PBVERT_FMT, 4), (WAVEVERT_FMT, 16), (WAVEFACE_FMT, 8)]:
    assert struct.calcsize(_fmt) == _sz, (_fmt, struct.calcsize(_fmt), _sz)


class Reader:
    def __init__(self, path):
        self.b = open(path, 'rb').read(); self.p = 0; self.n = len(self.b); self.path = path
    def read(self, n):
        if n < 0 or self.p + n > self.n:
            raise EOFError('read %d at 0x%x beyond size %d (%s)' % (n, self.p, self.n, self.path))
        d = self.b[self.p:self.p + n]; self.p += n; return d
    def i32(self): return struct.unpack('>i', self.read(4))[0]
    def i16(self): return struct.unpack('>h', self.read(2))[0]
    def skip(self, n):
        st = self.p; self.read(n); return st
    def records(self, fmt, count):
        sz = struct.calcsize(fmt)
        recs = [struct.unpack_from(fmt, self.b, self.p + i * sz) for i in range(count)]
        return recs, self.skip(count * sz)


def parse_sky(r):
    """initPlax PLAX.C:84-115: 256 shorts palette, int 512, int 256, 512*256 bitmap, 320 ints (rotation table)."""
    st = r.p
    pal = struct.unpack('>256H', r.read(512))
    w, h = r.i32(), r.i32()
    assert (w, h) == (512, 256), ('PLAX.C:91-94 asserts', w, h)
    bitmap_off = r.skip(512 * 256)
    table_off = r.skip(320 * 4)
    return dict(offset=st, palette=pal, width=w, height=h, bitmap_off=bitmap_off, table_off=table_off, end=r.p)


PART_ORDER = ['sector', 'wall', 'vertex', 'face', 'object', 'pushBlock', 'PBVert', 'waveVert', 'waveFace',
              'PBWall', 'objectParams', 'texture', 'vertexLight', 'cutPlane']   # LEVEL.C:52-68


def parse_level_block(r):
    """LEVEL.C:37-68: int size, sLevelHeader(14 int), then LOADPARTs in that order."""
    out = {}
    out['size_off'] = r.p
    size = r.i32()
    assert 0 < size < 900000, ('LEVEL.C:41-42', size)
    out['size'] = size
    out['header_off'] = r.p
    hdr = dict(zip(HEADER_FIELDS, struct.unpack('>14i', r.read(56))))
    out['header'] = hdr
    parts = {}

    def part(name, fmt, count):
        recs, off = r.records(fmt, count)
        parts[name] = dict(off=off, count=count, size=count * struct.calcsize(fmt))
        return recs

    def raw(name, count):
        off = r.skip(count)
        parts[name] = dict(off=off, count=count, size=count)
        return r.b[off:off + count]

    secs = part('sector', SECTOR_FMT, hdr['nmSectors'])                 # LEVEL.C:52
    walls = part('wall', WALL_FMT, hdr['nmWalls'])                      # LEVEL.C:53
    verts = part('vertex', VERTEX_FMT, hdr['nmVerticies'])              # LEVEL.C:54
    faces = part('face', FACE_FMT, hdr['nmFaces'])                      # LEVEL.C:55
    objs = part('object', OBJECT_FMT, hdr['nmObjects'])                 # LEVEL.C:56
    pbs = part('pushBlock', PB_FMT, hdr['nmPushBlocks'])                # LEVEL.C:57
    pbv = part('PBVert', PBVERT_FMT, hdr['nmPBVert'])                   # LEVEL.C:58
    wv = part('waveVert', WAVEVERT_FMT, hdr['nmWaveVert'])              # LEVEL.C:59
    wf = part('waveFace', WAVEFACE_FMT, hdr['nmWaveFace'])              # LEVEL.C:60
    pbw = [x[0] for x in part('PBWall', '>h', hdr['nmPBWalls'])]        # LEVEL.C:61
    objparams = raw('objectParams', hdr['nmObjectParams'])              # LEVEL.C:62
    textures = raw('texture', hdr['nmTextureIndexes'])                  # LEVEL.C:63 (odd entries += tileBase, :70-71)
    lights = raw('vertexLight', hdr['nmLightValues'])                   # LEVEL.C:64
    cut = raw('cutPlane', hdr['nmCutSectors'] * MAXCUTSECTORS)          # LEVEL.C:66-68
    # OPTIONAL block, appended after cutPlane by our own converter (tools/ordre.py): pairs of
    # sectors with no portal between them, which buildTree would otherwise leave to the distance
    # scalar alone.  The header has no field left, so its presence is read off the block size --
    # a retail level stops at cutPlane and leaves nothing over.
    extra = ['orderPairCount', 'orderPair']
    pairs = []
    if size > 56 + sum(p['size'] for p in parts.values()):
        off = r.p
        n = r.i32()
        parts['orderPairCount'] = dict(off=off, count=1, size=4)
        recs, off = r.records('>hhBB', n)
        parts['orderPair'] = dict(off=off, count=n, size=n * 6)
        pairs = [dict(a=x[0], b=x[1], plane=x[2]) for x in recs]
    else:
        extra = []
    out['orderPairs'] = pairs
    out['parts'] = parts
    out['end'] = r.p
    out['sum_parts'] = sum(p['size'] for p in parts.values())
    out['size_ok'] = (size == 56 + out['sum_parts'])                    # RETAIL_DISCS.md par.4: size == 56 + sum LOADPART
    out['contiguous'] = True
    prev_end = out['header_off'] + 56
    for name in PART_ORDER + extra:
        p = parts[name]
        if p['off'] != prev_end:
            out['contiguous'] = False
        prev_end = p['off'] + p['size']
    assert prev_end == out['end']

    out['sectors'] = [dict(object=s[0], center=list(s[1:4]), floorLevel=s[4], firstWall=s[5], lastWall=s[6],
                           light=s[7], flags=s[8], cutIndex=s[9], cutChannel=s[10], pad=s[11]) for s in secs]
    out['walls'] = [dict(normal=list(w[0:3]), d=w[3], object=w[4], flags=w[5], textures=w[6], firstFace=w[7],
                         lastFace=w[8], firstVertex=w[9], lastVertex=w[10], v=list(w[11:15]), nextSector=w[15],
                         firstLight=w[16], pixelLength=w[17], tileLength=w[18], tileHeight=w[19]) for w in walls]
    out['vertices'] = [dict(x=v[0], y=v[1], z=v[2], light=v[3], pad=v[4]) for v in verts]
    out['faces'] = [dict(v=list(f[0:4]), tile=f[4], pad=f[5]) for f in faces]
    out['objects'] = [dict(type=o[0], firstParam=o[1]) for o in objs]
    out['pushBlocks'] = [dict(enclosingSector=p[0], startWall=p[1], endWall=p[2], startVertex=p[3], endVertex=p[4],
                              floorSector=p[5], dx=p[6], dy=p[7], dz=p[8]) for p in pbs]
    out['PBVert'] = [dict(vStart=p[0], vNm=p[1], flags=p[2]) for p in pbv]
    out['waveVert'] = [dict(pos=w[0], vel=w[1], connect=list(w[2:6])) for w in wv]
    out['waveFace'] = [dict(connect=list(w)) for w in wf]
    out['PBWall'] = pbw
    out['objectParams'] = objparams
    out['texture'] = textures
    out['vertexLight'] = lights
    out['cutPlane'] = cut
    return out


def parse_sounds(r):
    """loadDynamicSounds SOUND.C:243-248 -> loadSoundSet :231-241 (int n==OT_NMTYPES, n shorts, int count) -> loadSound :199-228."""
    st = r.p
    n = r.i32(); assert n == OT_NMTYPES, ('SOUND.C:233', n)
    smap = struct.unpack('>%dh' % n, r.read(2 * n))
    ns = r.i32(); assert 0 <= ns < MAXNMSOUNDS, ns
    lst = []
    for i in range(ns):
        size, rate, bps, loop = r.i32(), r.i32(), r.i32(), r.i32()
        off = r.skip(size); lst.append(dict(off=off, size=size, rate=rate, bps=bps, loopStart=loop))
    return dict(off=st, map=smap, sounds=lst, end=r.p)


def parse_tiles(r):
    """loadTiles PIC.C:713-716: loadPalletes :611-625 (int size + size bytes), loadTileSet :671-707."""
    st = r.p
    psz = r.i32(); assert 0 < psz < 1024 * 1024, psz
    pal_off = r.skip(psz)
    tst = r.p
    nm = r.i32()
    kinds = Counter(); tiles = []
    for i in range(nm):
        f = r.i16(); toff = r.p - 2
        if f == (T_64 | T_16BPP | T_PAL):            # load16BPPTile PIC.C:500-532
            r.i16(); r.skip(4096); k = '16bpp64'
        elif f == T_VDP2:                            # PIC.C:681-686
            r.skip(8); k = 'vdp2'
        elif f == (T_64 | T_8BPP | T_RLE | T_PAL):   # load8BPPRLETile :568-580
            r.i16(); s = r.i16(); assert s > 0; r.skip(s); k = '8rle64'
        elif f == (T_32 | T_8BPP | T_RLE | T_PAL):   # loadSmall8BPPRLETile :582-594
            r.i16(); s = r.i16(); assert s > 0; r.skip(s); k = '8rle32'
        elif f == (T_32 | T_16BPP | T_PAL):          # loadSmall16BPPTile :534-566
            r.i16(); r.skip(1024); k = '16bpp32'
        elif f == (T_64 | T_16BPP | T_PAL | T_RLE):  # load16BPPRLETile :596-609
            r.i16(); s = r.i16(); assert s > 0; r.skip(s); k = '16rle64'
        else:
            raise ValueError('unknown tile flags 0x%x at 0x%x' % (f, toff))
        kinds[k] += 1; tiles.append((toff, k))
    return dict(off=st, palette_off=pal_off, palette_size=psz, tileset_off=tst, count=nm, kinds=dict(kinds),
                tiles=tiles, end=r.p)


def parse_sequences(r):
    """loadSequences SEQUENCE.C:24-89: int size, buffer = seqHeader(3 int) + ... + OT_NMTYPES shorts; asserted :44-49."""
    st = r.p
    size = r.i32(); assert 0 < size < 1024 * 1024
    ns, nf, nc = struct.unpack('>3i', r.b[r.p:r.p + 12])
    expect = 12 + ns * 2 + nf * 8 + nc * 8 + OT_NMTYPES * 2
    r.skip(size)
    return dict(off=st, size=size, nmSequences=ns, nmFrames=nf, nmChunks=nc, size_ok=(size == expect), end=r.p)


def parse_lev(path):
    r = Reader(path)
    lev = dict(file=os.path.basename(path), path=path, fsize=r.n)
    lev['sky'] = parse_sky(r)
    assert r.p == 0x20708, hex(r.p)
    lev['level'] = parse_level_block(r)
    lev['sounds'] = parse_sounds(r)
    lev['tiles'] = parse_tiles(r)
    lev['sequences'] = parse_sequences(r)
    lev['end'] = r.p
    lev['trailing'] = r.n - r.p
    return lev


def validate(lev):
    L = lev['level']
    probs = []
    if L['header_off'] != 0x2070C: probs.append('header not at 0x2070C')
    if not L['size_ok']: probs.append('size != 56+sum(parts)')
    if not L['contiguous']: probs.append('parts not contiguous')
    if L['end'] != lev['sounds']['off']: probs.append('sounds do not start at level end')
    if lev['sounds']['end'] != lev['tiles']['off']: probs.append('tiles do not start at sounds end')
    if lev['tiles']['end'] != lev['sequences']['off']: probs.append('sequences do not start at tiles end')
    if not lev['sequences']['size_ok']: probs.append('sequence size mismatch')
    if lev['trailing'] != 0: probs.append('trailing %d bytes' % lev['trailing'])
    H = L['header']
    if H['nmSectors'] > MAXNMSECTORS: probs.append('nmSectors > 600')
    if H['nmWalls'] > MAXNMWALLS: probs.append('nmWalls > 5500')
    nv, nw, nf, ns, nt = H['nmVerticies'], H['nmWalls'], H['nmFaces'], H['nmSectors'], H['nmTextureIndexes']
    for i, s in enumerate(L['sectors']):
        if not (0 <= s['firstWall'] <= s['lastWall'] < nw): probs.append('sector %d wall range' % i); break
        if s['object'] != 0 or s['pad'] != 0: probs.append('sector %d object/pad != 0' % i); break
    for i, w in enumerate(L['walls']):
        if any(not (0 <= x < nv) for x in w['v']): probs.append('wall %d v out of range' % i); break
        if not (-1 <= w['nextSector'] < ns): probs.append('wall %d nextSector' % i); break
        if w['object'] != 0: probs.append('wall %d object != 0' % i); break
        # firstFace == -1 : wall without faces (CONVERT.C:1764/1894/1946, handled WALLS.C:1167); else face range
        if w['firstFace'] == -1:
            if w['lastFace'] != -1: probs.append('wall %d lastFace without firstFace' % i); break
        elif not (0 <= w['firstFace'] <= w['lastFace'] < nf): probs.append('wall %d face range' % i); break
        if w['flags'] & 0x01:          # PARALLELOGRAM: firstVertex=lastVertex=-1 (0xffff) CONVERT.C:1948-1949, textures index
            if (w['firstVertex'], w['lastVertex']) != (0xffff, 0xffff): probs.append('wall %d para vertex range' % i); break
            if not (0 <= w['textures'] < nt): probs.append('wall %d textures index' % i); break
        elif not (w['firstVertex'] <= w['lastVertex'] < nv): probs.append('wall %d vertex range' % i); break
    for i, f in enumerate(L['faces']):
        if f['pad'] != 0: probs.append('face %d pad != 0' % i); break
    for i, v in enumerate(L['vertices']):
        if v['pad'] != 0: probs.append('vertex %d pad != 0' % i); break
    return probs


def stats(lev):
    L = lev['level']; H = L['header']
    verts = L['vertices']; walls = L['walls']; secs = L['sectors']
    xs = [v['x'] for v in verts]; ys = [v['y'] for v in verts]; zs = [v['z'] for v in verts]
    wflags = Counter(); sflags = Counter()
    for w in walls:
        for bit, name in WALLFLAGS:
            if w['flags'] & bit: wflags[name] += 1
    for s in secs:
        for bit, name in SECFLAGS:
            if s['flags'] & bit: sflags[name] += 1
    wraw = Counter('0x%04x' % (w['flags'] & 0xffff) for w in walls)
    sraw = Counter('0x%04x' % (s['flags'] & 0xffff) for s in secs)
    floors = sum(1 for w in walls if w['normal'][1] == 65536)     # CONVERT.C:2066 floor = normal[1]==1<<16
    ceils = sum(1 for w in walls if w['normal'][1] == -65536)     # CONVERT.C:2080 ceiling = normal[1]==-1<<16
    wps = Counter(s['lastWall'] - s['firstWall'] + 1 for s in secs)
    face_tiles = set(f['tile'] for f in L['faces'])
    tex = L['texture']
    tex_tiles = set(tex[i] for i in range(1, len(tex), 2))        # LEVEL.C:70-71: odd entries are tile numbers
    para = sum(1 for w in walls if w['flags'] & 0x01)
    return dict(
        file=lev['file'], fsize=lev['fsize'], size=L['size'],
        counts=dict(sectors=H['nmSectors'], walls=H['nmWalls'], vertices=H['nmVerticies'], faces=H['nmFaces'],
                    objects=H['nmObjects'], objectParams=H['nmObjectParams'], pushBlocks=H['nmPushBlocks'],
                    PBWalls=H['nmPBWalls'], PBVert=H['nmPBVert'], waveVert=H['nmWaveVert'], waveFace=H['nmWaveFace'],
                    cutSectors=H['nmCutSectors'], textureIndexes=H['nmTextureIndexes'], lightValues=H['nmLightValues'],
                    dynSounds=len(lev['sounds']['sounds']), tiles=lev['tiles']['count'],
                    sequences=lev['sequences']['nmSequences']),
        bbox=dict(x=[min(xs), max(xs)], y=[min(ys), max(ys)], z=[min(zs), max(zs)]),
        walls_with_nextSector=sum(1 for w in walls if w['nextSector'] != -1),
        horizontal_walls=floors + ceils, floor_walls=floors, ceiling_walls=ceils,
        parallelogram_walls=para, nonrect_walls=len(walls) - para,
        wall_flags=dict(wflags), wall_flags_raw=dict(sorted(wraw.items())),
        sector_flags=dict(sflags), sector_flags_raw=dict(sorted(sraw.items())),
        distinct_face_tiles=len(face_tiles), distinct_texture_tiles=len(tex_tiles),
        distinct_tiles=len(face_tiles | tex_tiles),
        tile_kinds=lev['tiles']['kinds'],
        walls_per_sector=dict(sorted(((str(k), v) for k, v in wps.items()), key=lambda kv: int(kv[0]))),
        offsets=dict(sky=lev['sky']['offset'], size=L['size_off'], header=L['header_off'],
                     parts={k: p['off'] for k, p in L['parts'].items()}, level_end=L['end'],
                     sounds=lev['sounds']['off'], palettes=lev['tiles']['off'], tiles=lev['tiles']['tileset_off'],
                     sequences=lev['sequences']['off'], end=lev['end']),
    )


def dump(lev, path):
    L = lev['level']
    d = dict(file=lev['file'],
             vertices=[[v['x'], v['y'], v['z']] for v in L['vertices']],
             vertex_light=[v['light'] for v in L['vertices']],
             sectors=[dict(floorLevel=s['floorLevel'], center=s['center'], firstWall=s['firstWall'],
                           lastWall=s['lastWall'], light=s['light'], flags=s['flags']) for s in L['sectors']],
             walls=[dict(normal=w['normal'], d=w['d'], flags=w['flags'], v=w['v'], nextSector=w['nextSector'],
                         firstFace=w['firstFace'], lastFace=w['lastFace'], firstVertex=w['firstVertex'],
                         lastVertex=w['lastVertex'], textures=w['textures'], pixelLength=w['pixelLength'],
                         tileLength=w['tileLength'], tileHeight=w['tileHeight']) for w in L['walls']],
             faces=[dict(v=f['v'], tile=f['tile']) for f in L['faces']],
             objects=L['objects'])
    tmp = path + '.tmp'
    with open(tmp, 'w') as f:
        json.dump(d, f, separators=(',', ':'))
    os.replace(tmp, path)


def main(argv):
    stats_path = os.path.join(OUT_DIR, 'sat_stats.json'); dump_dir = os.path.join(OUT_DIR, 'sat')
    files = []; i = 0
    while i < len(argv):
        if argv[i] == '--stats': stats_path = argv[i + 1]; i += 2
        elif argv[i] == '--dump': dump_dir = argv[i + 1]; i += 2
        else: files.append(argv[i]); i += 1
    if not files:
        files = sorted(glob.glob(os.path.join(PS_DIR, '*.LEV')))
    os.makedirs(dump_dir, exist_ok=True)
    allstats = {}; ok_all = True
    for p in files:
        lev = parse_lev(p)
        probs = validate(lev)
        st = stats(lev); st['validation'] = probs or 'OK'
        name = os.path.splitext(lev['file'])[0]
        allstats[name] = st
        dump(lev, os.path.join(dump_dir, name + '.json'))
        o = st['offsets']
        print('%-12s fsize=%8d hdr@0x%x size=%6d level_end=%7d snd@%7d pal@%7d tiles@%7d seq@%7d end=%7d %s' % (
            lev['file'], lev['fsize'], o['header'], st['size'], o['level_end'], o['sounds'], o['palettes'],
            o['tiles'], o['sequences'], o['end'], 'OK' if not probs else 'PROBLEMS: ' + '; '.join(probs)))
        if probs: ok_all = False
    tmp = stats_path + '.tmp'
    with open(tmp, 'w') as f:
        json.dump(allstats, f, indent=1)
    os.replace(tmp, stats_path)
    print('wrote', stats_path, '(%d levels)' % len(allstats), 'ALL OK' if ok_all else 'SOME PROBLEMS')
    return 0 if ok_all else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

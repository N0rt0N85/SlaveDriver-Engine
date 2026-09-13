"""lev_write.py - E0: serialize a lev_io content model back into PowerSlave gen1 .LEV bytes.

The writer sees ONLY the content model (lev_io.model_from_bytes): no offsets, no stored counts or
sizes, no copy of the input file. It RECOMPUTES every derived quantity:
  level `size` (= 56 + sum LOADPART, LEVEL.C:37-68), the 14 header counts (SLEVEL.H:5-20),
  sound set counts and PCM sizes (SOUND.C:203-236), palette block size (PIC.C:614), tile count
  (PIC.C:675), RLE sizes (PIC.C:572/586/601), sequence header + buffer size (SEQUENCE.C:29-48).
Identity on the retail files therefore proves both the field layout and those derivations.

Loader asserts that would stop the console (LEVEL.C, PLAX.C, SOUND.C, PIC.C, SEQUENCE.C) are checked
by engine_problems(); write_lev(strict=True) refuses to emit a file that violates them.

Usage:
  python tools\\duke2ps\\lev_write.py                 acceptance suite on every refs\\extract\\PS\\*.LEV
  python tools\\duke2ps\\lev_write.py --emit DIR      ... and also write the re-emitted files to DIR
  python tools\\duke2ps\\lev_write.py --copy IN OUT   parse IN, re-serialize to OUT (atomic)
"""
import os, sys, glob, json, struct, hashlib, time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import lev_io   # noqa: E402
L = lev_io.L

OUT_DIR = os.path.join(lev_io.ROOT, 'build', 'duke2ps', 'e0')
MAXNMSOUNDS = L.MAXNMSOUNDS

_S = {k: struct.Struct(f) for k, f in dict(
    sector=L.SECTOR_FMT, wall=L.WALL_FMT, vertex=L.VERTEX_FMT, face=L.FACE_FMT, object=L.OBJECT_FMT,
    pushBlock=L.PB_FMT, PBVert=L.PBVERT_FMT, waveVert=L.WAVEVERT_FMT, waveFace=L.WAVEFACE_FMT,
    frame=lev_io.FRAME_FMT, chunk=lev_io.CHUNK_FMT).items()}

# field order of each record = order of the struct in SLEVEL.H (and of lev.py's unpack)
ROW = dict(
    sector=lambda s: (s['object'], *s['center'], s['floorLevel'], s['firstWall'], s['lastWall'], s['light'],
                      s['flags'], s['cutIndex'], s['cutChannel'], s['pad']),
    wall=lambda w: (*w['normal'], w['d'], w['object'], w['flags'], w['textures'], w['firstFace'], w['lastFace'],
                    w['firstVertex'], w['lastVertex'], *w['v'], w['nextSector'], w['firstLight'],
                    w['pixelLength'], w['tileLength'], w['tileHeight']),
    vertex=lambda v: (v['x'], v['y'], v['z'], v['light'], v['pad']),
    face=lambda f: (*f['v'], f['tile'], f['pad']),
    object=lambda o: (o['type'], o['firstParam']),
    pushBlock=lambda p: (p['enclosingSector'], p['startWall'], p['endWall'], p['startVertex'], p['endVertex'],
                         p['floorSector'], p['dx'], p['dy'], p['dz']),
    PBVert=lambda p: (p['vStart'], p['vNm'], p['flags']),
    waveVert=lambda w: (w['pos'], w['vel'], *w['connect']),
    waveFace=lambda w: tuple(w['connect']),
    frame=lambda f: (f['chunkIndex'], f['flags'], f['sound'], *f['pad']),
    chunk=lambda c: (c['chunkx'], c['chunky'], c['tile'], c['flags'], c['pad']),
)


def _records(kind, rows, where):
    st, row = _S[kind], ROW[kind]
    out = bytearray()
    for i, r in enumerate(rows):
        try:
            out += st.pack(*row(r))
        except (struct.error, KeyError, TypeError) as e:
            raise ValueError('%s %s[%d]: %s (%r)' % (where, kind, i, e, r)) from None
    return bytes(out)


def _u8(vals, where):
    try:
        return bytes(vals)
    except ValueError as e:
        raise ValueError('%s: %s' % (where, e)) from None


def _pack(fmt, vals, where):
    try:
        return struct.pack(fmt, *vals)
    except struct.error as e:
        raise ValueError('%s: %s' % (where, e)) from None


# ---------------------------------------------------------------- sections
def serialize_sky(sky):
    """initPlax PLAX.C:84-115."""
    return b''.join([_pack('>%dH' % len(sky['palette']), sky['palette'], 'sky.palette'),
                     _pack('>ii', (sky['width'], sky['height']), 'sky.size'),
                     bytes(sky['bitmap']),
                     _pack('>%di' % len(sky['table']), sky['table'], 'sky.table')])


def level_parts(lv):
    """LOADPART order LEVEL.C:51-67 -> list of (name, bytes); header counts derived from content."""
    cut = lv['cutPlane']
    for i, row in enumerate(cut):
        if len(row) != lev_io.MAXCUTSECTORS:
            raise ValueError('cutPlane[%d] has %d entries, need %d (LEVEL.C:65)' % (i, len(row), lev_io.MAXCUTSECTORS))
    return [
        ('sector', _records('sector', lv['sectors'], 'level')),
        ('wall', _records('wall', lv['walls'], 'level')),
        ('vertex', _records('vertex', lv['vertices'], 'level')),
        ('face', _records('face', lv['faces'], 'level')),
        ('object', _records('object', lv['objects'], 'level')),
        ('pushBlock', _records('pushBlock', lv['pushBlocks'], 'level')),
        ('PBVert', _records('PBVert', lv['PBVert'], 'level')),
        ('waveVert', _records('waveVert', lv['waveVert'], 'level')),
        ('waveFace', _records('waveFace', lv['waveFace'], 'level')),
        ('PBWall', _pack('>%dh' % len(lv['PBWall']), lv['PBWall'], 'level.PBWall')),
        ('objectParams', _u8(lv['objectParams'], 'level.objectParams')),
        ('texture', _u8(lv['texture'], 'level.texture')),
        ('vertexLight', _u8(lv['vertexLight'], 'level.vertexLight')),
        ('cutPlane', b''.join(_u8(r, 'level.cutPlane') for r in cut)),
    ]


def level_header(lv):
    """sLevelHeader SLEVEL.H:5-20, in lev.HEADER_FIELDS order, derived from the content."""
    return dict(nmSectors=len(lv['sectors']), nmWalls=len(lv['walls']), nmVerticies=len(lv['vertices']),
                nmFaces=len(lv['faces']), nmTextureIndexes=len(lv['texture']), nmLightValues=len(lv['vertexLight']),
                nmObjects=len(lv['objects']), nmObjectParams=len(lv['objectParams']),
                nmPushBlocks=len(lv['pushBlocks']), nmPBWalls=len(lv['PBWall']), nmPBVert=len(lv['PBVert']),
                nmWaveVert=len(lv['waveVert']), nmWaveFace=len(lv['waveFace']), nmCutSectors=len(lv['cutPlane']))


def serialize_level(lv):
    """LEVEL.C:37-67: int size, sLevelHeader, parts. size = 56 + sum(parts)."""
    parts = level_parts(lv)
    body = b''.join(p for _, p in parts)
    hdr = level_header(lv)
    size = 56 + len(body)
    return struct.pack('>i', size) + _pack('>14i', [hdr[k] for k in L.HEADER_FIELDS], 'level.header') + body


def serialize_sounds(snd):
    """loadSoundSet SOUND.C:230-241 / loadSound :199-209."""
    out = [struct.pack('>i', len(snd['map'])), _pack('>%dh' % len(snd['map']), snd['map'], 'sounds.map'),
           struct.pack('>i', len(snd['sounds']))]
    for i, s in enumerate(snd['sounds']):
        out.append(_pack(lev_io.SOUNDHDR_FMT, (len(s['pcm']), s['rate'], s['bps'], s['loopStart']), 'sounds[%d]' % i))
        out.append(bytes(s['pcm']))
    return b''.join(out)


def serialize_palettes(pal):
    """loadPalletes PIC.C:611-625: int size + (short objectPalette, N x 256 u16)."""
    blk = _pack('>h', (pal['objectPalette'],), 'palettes.objectPalette') + b''.join(
        _pack('>%dH' % len(p), p, 'palettes[%d]' % k) for k, p in enumerate(pal['palettes']))
    return struct.pack('>i', len(blk)) + blk


def serialize_tiles(tiles):
    """loadTileSet PIC.C:671-707 and the per-kind loaders PIC.C:500-609."""
    out = [struct.pack('>i', len(tiles))]
    for i, t in enumerate(tiles):
        f = t['flags']
        out.append(_pack('>h', (f,), 'tiles[%d].flags' % i))
        if f in lev_io.TILE_PIXELS:
            out.append(_pack('>h', (t['palNm'],), 'tiles[%d].palNm' % i)); out.append(bytes(t['pixels']))
        elif f in lev_io.TILE_RLE:
            out.append(_pack('>hh', (t['palNm'], len(t['rle'])), 'tiles[%d].rle' % i)); out.append(bytes(t['rle']))
        elif f == lev_io.TF_VDP2:
            out.append(_pack(lev_io.VDP2PIC_FMT, (t['x'], t['y'], t['w'], t['h']), 'tiles[%d].vdp2' % i))
        else:
            raise ValueError('tiles[%d]: flags 0x%x not accepted by loadTileSet (PIC.C:700 assert)' % (i, f))
    return b''.join(out)


def serialize_sequences(sq):
    """loadSequences SEQUENCE.C:29-72: int size, buffer = seqHeader, frames, chunks, sequence list, map."""
    buf = b''.join([
        struct.pack('>3i', len(sq['sequence']), len(sq['frames']), len(sq['chunks'])),
        _records('frame', sq['frames'], 'sequences'),
        _records('chunk', sq['chunks'], 'sequences'),
        _pack('>%dh' % len(sq['sequence']), sq['sequence'], 'sequences.sequence'),
        _pack('>%dh' % len(sq['sequenceMap']), sq['sequenceMap'], 'sequences.sequenceMap')])
    return struct.pack('>i', len(buf)) + buf


# ---------------------------------------------------------------- engine asserts
def engine_problems(model):
    """Conditions that would trip a loader assert (or read out of bounds) on the console."""
    P = []
    sky = model['sky']; lv = model['level']; snd = model['sounds']; pal = model['palettes']
    sq = model['sequences']
    if (sky['width'], sky['height']) != (512, 256): P.append('sky %dx%d != 512x256 (PLAX.C:91-93)' % (sky['width'], sky['height']))
    if len(sky['palette']) != 256: P.append('sky palette %d != 256 (PLAX.C:84)' % len(sky['palette']))
    if len(sky['bitmap']) != 512 * 256: P.append('sky bitmap %d != 131072 (PLAX.C:94)' % len(sky['bitmap']))
    if len(sky['table']) != 320: P.append('sky table %d != 320 (PLAX.C:115)' % len(sky['table']))
    size = 56 + sum(len(p) for _, p in level_parts(lv))
    if not (0 < size < 900000): P.append('level size %d not in ]0,900000[ (LEVEL.C:40-41)' % size)
    if len(lv['sectors']) > L.MAXNMSECTORS: P.append('nmSectors %d > %d (UTIL.H:21)' % (len(lv['sectors']), L.MAXNMSECTORS))
    if len(lv['walls']) > L.MAXNMWALLS: P.append('nmWalls %d > %d (UTIL.H:22)' % (len(lv['walls']), L.MAXNMWALLS))
    if len(snd['map']) != lev_io.OT_NMTYPES: P.append('sound map %d != OT_NMTYPES (SOUND.C:233)' % len(snd['map']))
    if not (0 <= len(snd['sounds']) < MAXNMSOUNDS): P.append('dyn sounds %d not < %d (SOUND.C:237)' % (len(snd['sounds']), MAXNMSOUNDS))
    psz = 2 + 512 * len(pal['palettes'])
    if not (0 < psz < 1024 * 1024): P.append('palette block %d not in ]0,1MB[ (PIC.C:615)' % psz)
    npal = len(pal['palettes'])
    if not (0 <= pal['objectPalette'] < npal): P.append('objectPalette %d outside %d palettes (PIC.C:630)' % (pal['objectPalette'], npal))
    for i, t in enumerate(model['tiles']):
        f = t['flags']
        if f in lev_io.TILE_PIXELS:
            if len(t['pixels']) != lev_io.TILE_PIXELS[f]: P.append('tile %d pixels %d (PIC.C:518/552)' % (i, len(t['pixels'])))
            if not (0 <= t['palNm'] < npal): P.append('tile %d palNm %d outside %d palettes (PIC.C:517/551)' % (i, t['palNm'], npal))
        elif f in lev_io.TILE_RLE:
            if not (0 < len(t['rle']) <= 32767): P.append('tile %d RLE size %d (short, assert PIC.C:573/587/602)' % (i, len(t['rle'])))
            if f == lev_io.TF_16RLE_64 and not (0 <= t['palNm'] < npal): P.append('tile %d palNm %d (PIC.C:605)' % (i, t['palNm']))
        elif f != lev_io.TF_VDP2:
            P.append('tile %d flags 0x%x (PIC.C:700)' % (i, f))
    ssz = 12 + 2 * len(sq['sequence']) + 8 * len(sq['frames']) + 8 * len(sq['chunks']) + 2 * len(sq['sequenceMap'])
    if not (0 < ssz < 1024 * 1024): P.append('sequence size %d (SEQUENCE.C:30)' % ssz)
    if len(sq['sequenceMap']) != lev_io.OT_NMTYPES: P.append('sequenceMap %d != OT_NMTYPES (SEQUENCE.C:48)' % len(sq['sequenceMap']))
    if any(f['pad'] != [0, 0] for f in sq['frames']): P.append('frame pad != 0 (SEQUENCE.C:54-55)')
    if any(c['pad'] != 0 for c in sq['chunks']): P.append('chunk pad != 0 (SEQUENCE.C:65)')
    if not sq['sequence'] or sq['sequence'][0] != 0: P.append('sequence[0] != 0 (SEQUENCE.C:80)')
    return P


def write_lev(model, strict=True):
    if strict:
        P = engine_problems(model)
        if P:
            raise ValueError('model violates loader asserts: ' + '; '.join(P))
    return b''.join([serialize_sky(model['sky']), serialize_level(model['level']), serialize_sounds(model['sounds']),
                     serialize_palettes(model['palettes']), serialize_tiles(model['tiles']),
                     serialize_sequences(model['sequences'])])


def atomic_write(path, data):
    d = os.path.dirname(os.path.abspath(path))
    os.makedirs(d, exist_ok=True)
    tmp = path + '.tmp'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, path)


def write_file(model, path, strict=True):
    data = write_lev(model, strict)
    atomic_write(path, data)
    return data


# ---------------------------------------------------------------- acceptance suite
def sha1(b):
    return hashlib.sha1(b).hexdigest()


def sum_parts_ok(lay):
    Lv = lay['level']
    return Lv['size'] == 56 + sum(p['size'] for p in Lv['parts'].values())


def byte_diff_positions(a, b):
    import numpy as np
    if len(a) != len(b):
        return None
    x = np.frombuffer(a, np.uint8); y = np.frombuffer(b, np.uint8)
    return [int(i) for i in np.nonzero(x != y)[0]]


def _paths(d):
    return sorted(s.split(':')[0] for s in d)


def mutation_tests(name, data, model, lay):
    """Each mutation: change content, write, re-read. Only the targeted field may change; file stays valid."""
    lv = model['level']; res = []
    k = len(lv['sectors']) // 2; j = len(lv['vertices']) // 3
    sec_off = lay['level']['parts']['sector']['off']; ver_off = lay['level']['parts']['vertex']['off']
    nf = len(model['sequences']['frames'])
    chk_off = lay['sequences']['off'] + 4 + 12 + 8 * nf        # first chunk
    npal = len(model['palettes']['palettes'])

    def fixed(label, obj, key, delta, path, span):
        old = obj[key]; new = old + delta
        obj[key] = new
        try:
            out = write_lev(model)
        finally:
            obj[key] = old
        m2, lay2 = lev_io.model_from_bytes(out, name)
        d = lev_io.diff(model, m2)
        pos = byte_diff_positions(data, out)
        ok_paths = _paths(d) == [path]
        ok_bytes = pos is not None and len(pos) > 0 and all(span[0] <= p < span[1] for p in pos)
        probs = L.validate(lay2)
        ok = ok_paths and ok_bytes and not probs and sum_parts_ok(lay2)
        res.append(dict(file=name, test=label, ok=ok, diff=d, bytes_changed=pos, span=list(span), validate=probs,
                        size_ok=sum_parts_ok(lay2), value='%d -> %d' % (old, new)))

    # M1 sector floorLevel (+16): bytes sector_off + 24k + 10..11 (object 4 + center 6)
    fixed('M1 sectors[%d].floorLevel +16' % k, lv['sectors'][k], 'floorLevel', 16,
          'level.sectors[%d].floorLevel' % k, (sec_off + 24 * k + 10, sec_off + 24 * k + 12))
    # M2 vertex x (+1)
    v = lv['vertices'][j]
    fixed('M2 vertices[%d].x %s1' % (j, '+' if v['x'] < 32767 else '-'), v, 'x', 1 if v['x'] < 32767 else -1,
          'level.vertices[%d].x' % j, (ver_off + 8 * j, ver_off + 8 * j + 2))
    # M3 sequence chunk tile (+1): the field E5 shifts by +N
    fixed('M3 sequences.chunks[0].tile +1', model['sequences']['chunks'][0], 'tile', 1,
          'sequences.chunks[0].tile', (chk_off + 4, chk_off + 6))

    # M4 structural: append one vertex -> nmVerticies+1, size+8, every later offset +8
    lv['vertices'].append(dict(x=0, y=0, z=0, light=0, pad=0))
    try:
        out = write_lev(model)
    finally:
        lv['vertices'].pop()
    m2, lay2 = lev_io.model_from_bytes(out, name)
    d = lev_io.diff(model, m2); probs = L.validate(lay2)
    H1, H2 = lay['level']['header'], lay2['level']['header']
    hdr_d = sorted(k2 for k2 in H1 if H1[k2] != H2[k2])
    later = L.PART_ORDER[L.PART_ORDER.index('vertex') + 1:]
    shift_ok = all(lay2['level']['parts'][p]['off'] == lay['level']['parts'][p]['off'] + 8 for p in later) and \
        lay2['sounds']['off'] == lay['sounds']['off'] + 8 and lay2['sequences']['off'] == lay['sequences']['off'] + 8
    ok = (_paths(d) == ['level.vertices'] and hdr_d == ['nmVerticies'] and H2['nmVerticies'] == H1['nmVerticies'] + 1
          and lay2['level']['size'] == lay['level']['size'] + 8 and len(out) == len(data) + 8 and shift_ok
          and not probs and sum_parts_ok(lay2))
    res.append(dict(file=name, test='M4 append 1 vertex', ok=ok, diff=d, header_changed=hdr_d,
                    size='%d -> %d' % (lay['level']['size'], lay2['level']['size']), later_offsets_plus8=shift_ok,
                    validate=probs, size_ok=sum_parts_ok(lay2)))

    # M5 structural tail: append one palette -> palette block = 2 + 512*(N+1), tiles/sequences +512
    model['palettes']['palettes'].append(list(model['palettes']['palettes'][0]))
    try:
        out = write_lev(model)
    finally:
        model['palettes']['palettes'].pop()
    m2, lay2 = lev_io.model_from_bytes(out, name)
    d = lev_io.diff(model, m2); probs = L.validate(lay2)
    psz = lay2['tiles']['palette_size']
    ok = (_paths(d) == ['palettes.palettes'] and psz == 2 + 512 * (npal + 1)
          and lay2['tiles']['tileset_off'] == lay['tiles']['tileset_off'] + 512
          and lay2['sequences']['off'] == lay['sequences']['off'] + 512 and not probs and sum_parts_ok(lay2))
    res.append(dict(file=name, test='M5 append 1 palette', ok=ok, diff=d,
                    palette_size='%d -> %d (2+512*%d)' % (lay['tiles']['palette_size'], psz, npal + 1), validate=probs))
    return res


def main(argv):
    emit = None; copy = None; i = 0
    while i < len(argv):
        if argv[i] == '--emit': emit = argv[i + 1]; i += 2
        elif argv[i] == '--copy': copy = (argv[i + 1], argv[i + 2]); i += 3
        else: raise SystemExit(__doc__)
    if copy:
        model, _ = lev_io.read_model(copy[0])
        out = write_file(model, copy[1])
        print('wrote %s  %d bytes  sha1 %s' % (copy[1], len(out), sha1(out)))
        return 0

    t0 = time.time()
    files = sorted(glob.glob(os.path.join(lev_io.PS_DIR, '*.LEV')))
    rows = []; muts = []
    MUT_FILES = ('KILENTRY.LEV', 'TOMB.LEV', 'SUNKEN.LEV', 'THOTH.LEV')   # donor, target slot, largest, most cut sectors
    print('E0 - identity writer on %d files of %s' % (len(files), os.path.relpath(lev_io.PS_DIR, lev_io.ROOT)))
    print('%-12s %9s  %-40s %-5s %-5s %-5s %-5s %-5s  %s' % ('fichier', 'taille', 'sha1 (entree)', 'sha1', 'rt-m', 'rt-l', 'valid', 'size', 'octets opaques / u8 / champs'))
    for p in files:
        name = os.path.basename(p)
        with open(p, 'rb') as f:
            data = f.read()
        model, lay = lev_io.model_from_bytes(data, name)
        out = write_lev(model)
        sha_in, sha_out = sha1(data), sha1(out)
        m2, lay2 = lev_io.model_from_bytes(out, name)
        rt_model = (m2 == model)
        rt_layout = (lay2 == lay)
        probs = L.validate(lay2)
        acct = lev_io.blob_accounting(model, len(data))
        row = dict(file=name, fsize=len(data), sha1_in=sha_in, sha1_out=sha_out, identity=(sha_in == sha_out),
                   roundtrip_model=rt_model, roundtrip_layout=rt_layout, validate=probs, size_ok=sum_parts_ok(lay2),
                   engine_problems=engine_problems(model), accounting=acct,
                   first_diff=None if rt_model else lev_io.diff(model, m2, cap=5))
        rows.append(row)
        yn = lambda b: 'ok' if b else 'FAIL'
        print('%-12s %9d  %-40s %-5s %-5s %-5s %-5s %-5s  %d / %d / %d (%.1f %% opaque)' % (
            name, len(data), sha_in, yn(row['identity']), yn(rt_model), yn(rt_layout), yn(not probs),
            yn(row['size_ok']), acct['opaque_bytes'], acct['u8_arrays'], acct['fields'],
            100.0 * acct['opaque_bytes'] / len(data)))
        if emit:
            atomic_write(os.path.join(emit, name), out)
        if name in MUT_FILES:
            muts += mutation_tests(name, data, model, lay)

    # on-disk mutation round trip (write_file -> lev.parse_lev(path), lev.py's own file entry point)
    tomb = os.path.join(lev_io.PS_DIR, 'TOMB.LEV')
    model, lay = lev_io.read_model(tomb)
    k = len(model['level']['sectors']) // 2
    model['level']['sectors'][k]['floorLevel'] += 16
    mpath = os.path.join(OUT_DIR, 'TOMB_M1.LEV')
    write_file(model, mpath)
    disk = L.parse_lev(mpath)
    disk_probs = L.validate(disk)
    model['level']['sectors'][k]['floorLevel'] -= 16
    m_disk, _ = lev_io.read_model(mpath)
    d_disk = lev_io.diff(model, m_disk)
    disk_ok = (not disk_probs and _paths(d_disk) == ['level.sectors[%d].floorLevel' % k] and sum_parts_ok(disk)
               and disk['level']['sectors'][k]['floorLevel'] == lay['level']['sectors'][k]['floorLevel'] + 16)
    muts.append(dict(file='TOMB.LEV', test='M1 on disk -> %s, lev.parse_lev(path)' % os.path.relpath(mpath, lev_io.ROOT),
                     ok=disk_ok, diff=d_disk, validate=disk_probs, size_ok=sum_parts_ok(disk)))

    # negative controls: the strict writer must REFUSE (ValueError), never wrap or emit silently
    model, _ = lev_io.read_model(os.path.join(lev_io.PS_DIR, 'KILENTRY.LEV'))
    negs = []

    def refuse(label, mut, undo):
        mut()
        try:
            write_lev(model); got = 'emitted'
        except ValueError as e:
            got = 'ValueError: ' + str(e)[:110]
        finally:
            undo()
        negs.append(dict(test=label, ok=got.startswith('ValueError'), result=got))
    secs = model['level']['sectors']; s0 = secs[0]; fr0 = model['sequences']['frames'][0]; t0_ = model['tiles'][0]
    old_fl, old_pad, old_f, n_sec = s0['floorLevel'], list(fr0['pad']), t0_['flags'], len(secs)
    refuse('N1 floorLevel = 40000 (short overflow)', lambda: s0.__setitem__('floorLevel', 40000),
           lambda: s0.__setitem__('floorLevel', old_fl))
    refuse('N2 frame pad = [1,0] (SEQUENCE.C:54)', lambda: fr0.__setitem__('pad', [1, 0]),
           lambda: fr0.__setitem__('pad', old_pad))
    refuse('N3 tile flags = 0x99 (PIC.C:700)', lambda: t0_.__setitem__('flags', 0x99),
           lambda: t0_.__setitem__('flags', old_f))
    refuse('N4 601 sectors (UTIL.H:21)', lambda: secs.extend(dict(s0) for _ in range(601 - n_sec)),
           lambda: secs.__delitem__(slice(n_sec, None)))
    neg_clean = sha1(write_lev(model)) == '8d9d9a5f26ff8270403d4503eaa62b486381e2d5'   # model restored -> identity again

    print()
    print('Negative controls (strict writer must refuse):')
    for n_ in negs:
        print('  %-4s %-40s %s' % ('ok' if n_['ok'] else 'FAIL', n_['test'], n_['result']))
    print('  %-4s model restored after the refusals -> KILENTRY sha1 identical again' % ('ok' if neg_clean else 'FAIL'))

    print()
    print('Mutations (content change -> write -> re-read; only the targeted field may differ):')
    for m in muts:
        extra = m.get('value') or m.get('size') or m.get('palette_size') or ''
        print('  %-4s %-12s %-44s %s  diff=%s%s' % ('ok' if m['ok'] else 'FAIL', m['file'], m['test'], extra,
                                                    _paths(m['diff']),
                                                    ('  bytes=%s' % m['bytes_changed']) if 'bytes_changed' in m else ''))

    n = len(rows)
    crit = [
        ('C1 identite octet pour octet (sha1)', sum(r['identity'] for r in rows), n),
        ('C2 aller-retour modele parse(write(parse(x))) == parse(x)', sum(r['roundtrip_model'] for r in rows), n),
        ('C3 aller-retour structure lev.py (offsets, header, size)', sum(r['roundtrip_layout'] for r in rows), n),
        ('C4 lev.validate() == [] sur les fichiers re-emis', sum(not r['validate'] for r in rows), n),
        ('C5 size == 56 + somme des parts (LEVEL.C)', sum(r['size_ok'] for r in rows), n),
        ('C6 aucun assert chargeur viole (engine_problems)', sum(not r['engine_problems'] for r in rows), n),
        ('C7 mutations: seul le champ vise change, fichier valide', sum(m['ok'] for m in muts), len(muts)),
        ('C8 temoins negatifs: modele invalide refuse, puis identite', sum(n_['ok'] for n_ in negs) + neg_clean,
         len(negs) + 1),
    ]
    print()
    print('Criteres d\'acceptation:')
    for label, got, tot in crit:
        print('  %-4s %-60s %d/%d' % ('PASS' if got == tot else 'FAIL', label, got, tot))
    tot_f = sum(r['fsize'] for r in rows); tot_b = sum(r['accounting']['opaque_bytes'] for r in rows)
    tot_u = sum(r['accounting']['u8_arrays'] for r in rows)
    print('Opaque payload (sky bitmap, PCM, 8bpp pixels, RLE streams) = %d / %d bytes (%.1f %%); u8 arrays %d (%.2f %%)'
          % (tot_b, tot_f, 100.0 * tot_b / tot_f, tot_u, 100.0 * tot_u / tot_f))
    same = [(a['file'], b['file']) for x, a in enumerate(rows) for b in rows[x + 1:] if a['sha1_in'] == b['sha1_in']]
    if same:
        print('Identical inputs: %s' % ', '.join('%s = %s' % s for s in same))
    all_ok = all(got == tot for _, got, tot in crit)
    print('E0 %s  (%.1f s)' % ('ALL PASS' if all_ok else 'SOME FAIL', time.time() - t0))

    rep = dict(step='E0', files=rows, mutations=muts, negatives=negs, negatives_restore_identity=neg_clean, criteria=[dict(label=l, got=g, total=t) for l, g, t in crit],
               all_pass=all_ok)
    atomic_write(os.path.join(OUT_DIR, 'e0_report.json'), json.dumps(rep, indent=1, default=str).encode())
    print('wrote %s' % os.path.relpath(os.path.join(OUT_DIR, 'e0_report.json'), lev_io.ROOT))
    return 0 if all_ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

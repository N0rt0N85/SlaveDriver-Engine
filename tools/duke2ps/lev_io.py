"""lev_io.py - field model of a PowerSlave gen1 .LEV, built ON TOP of tools/lev.py (never edits it).

tools/lev.py (verified reader) decodes the level block into fields but only *locates* the tail
(sky bitmap/table, sound PCM, palettes, tile payloads, sequences).  This module:
  * runs lev.py's own parse_* functions on an in-memory buffer (MemReader = lev.Reader without a file),
  * decodes the tail into fields with its own sequential reader, and cross-checks every tail offset
    against lev.py's (two independent walks must agree),
  * returns a MODEL = content only: no offsets, no counts, no sizes, no raw copy of the file.
    Everything derivable (header counts, `size`, sound sizes, RLE sizes, palette size, tile count,
    sequence header/size) is recomputed by the writer (lev_write.py) from the content.

Model categories (reported by blob_accounting):
  F  struct fields  : ints of the SLEVEL.H / loader records (sectors, walls, ..., frames, chunks, maps)
  U  u8 arrays      : arrays the engine declares as unsigned char and walks element-wise
                      (texture: odd entries += tileBase LEVEL.C:69-70; vertexLight; objectParams;
                      cutPlane rows of MAXCUTSECTORS) -> stored as lists of ints
  B  opaque payload : bytes the loader copies without structural decoding (sky bitmap -> VRAM A1
                      PLAX.C:94, PCM -> SCSP SOUND.C:220-223, 8bpp tile pixels PIC.C:518/552,
                      RLE streams handed to addPic PIC.C:576/590/604) -> stored as `bytes`
"""
import os, sys, struct

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
if TOOLS not in sys.path:
    sys.path.insert(0, TOOLS)
import lev as L   # noqa: E402  (tools/lev.py, read-only dependency)

PS_DIR = L.PS_DIR
OT_NMTYPES = L.OT_NMTYPES
MAXCUTSECTORS = L.MAXCUTSECTORS

# tail record formats (big-endian)
FRAME_FMT = '>hhhbb'      # SLEVEL.H:214-219 sFrameType: chunkIndex, flags, sound, pad[2]
CHUNK_FMT = '>hhhbb'      # SLEVEL.H:221-226 sChunkType: chunkx, chunky, tile, flags(char), pad(char)
SOUNDHDR_FMT = '>iiii'    # SOUND.C:203-206: size, rate, bps, loopStart
VDP2PIC_FMT = '>hhhh'     # PIC.C:41-44 struct _vdp2PicData {short x,y,w,h}; read 8 bytes PIC.C:684
assert struct.calcsize(FRAME_FMT) == 8 and struct.calcsize(CHUNK_FMT) == 8

# tile flag values accepted by loadTileSet PIC.C:678-702 -> record kind
TF_16_64 = L.T_64 | L.T_16BPP | L.T_PAL            # 0x32 load16BPPTile      palNm + 64*64 u8
TF_VDP2 = L.T_VDP2                                 # 0x01 vdp2PicData        4 shorts
TF_8RLE_64 = L.T_64 | L.T_8BPP | L.T_RLE | L.T_PAL  # 0x6a load8BPPRLETile    palNm + size + RLE
TF_8RLE_32 = L.T_32 | L.T_8BPP | L.T_RLE | L.T_PAL  # 0x6c loadSmall8BPPRLE   palNm + size + RLE
TF_16_32 = L.T_32 | L.T_16BPP | L.T_PAL            # 0x34 loadSmall16BPPTile palNm + 32*32 u8
TF_16RLE_64 = L.T_64 | L.T_16BPP | L.T_PAL | L.T_RLE  # 0x72 load16BPPRLETile palNm + size + RLE
TILE_PIXELS = {TF_16_64: 64 * 64, TF_16_32: 32 * 32}
TILE_RLE = (TF_8RLE_64, TF_8RLE_32, TF_16RLE_64)
TILE_KIND = {TF_16_64: '16bpp64', TF_VDP2: 'vdp2', TF_8RLE_64: '8rle64', TF_8RLE_32: '8rle32',
             TF_16_32: '16bpp32', TF_16RLE_64: '16rle64'}   # same names as lev.parse_tiles


class MemReader(L.Reader):
    """lev.Reader over a bytes buffer (lev.Reader.__init__ insists on opening a path)."""
    def __init__(self, data, name='<mem>', pos=0):
        self.b = bytes(data); self.p = pos; self.n = len(self.b); self.path = name


def layout(data, name='<mem>'):
    """lev.parse_lev, verbatim sequence of lev.py calls, on a buffer. Returns lev.py's own dict."""
    r = MemReader(data, name)
    lev = dict(file=name, path=name, fsize=r.n)
    lev['sky'] = L.parse_sky(r)
    assert r.p == 0x20708, hex(r.p)
    lev['level'] = L.parse_level_block(r)
    lev['sounds'] = L.parse_sounds(r)
    lev['tiles'] = L.parse_tiles(r)
    lev['sequences'] = L.parse_sequences(r)
    lev['end'] = r.p
    lev['trailing'] = r.n - r.p
    return lev


def validate(data, name='<mem>'):
    """lev.validate on a buffer -> (layout, list of problems)."""
    lay = layout(data, name)
    return lay, L.validate(lay)


# ---------------------------------------------------------------- tail decoders (own walk)
def _decode_sky(data, lay):
    s = lay['sky']
    r = MemReader(data, pos=s['bitmap_off'])
    bitmap = r.read(512 * 256)
    assert r.p == s['table_off']
    table = list(struct.unpack('>320i', r.read(320 * 4)))
    assert r.p == s['end'] == lay['level']['size_off']
    return dict(palette=list(s['palette']), width=s['width'], height=s['height'], bitmap=bitmap, table=table)


def _decode_level(lay):
    Lv = lay['level']
    cut = Lv['cutPlane']
    assert len(cut) % MAXCUTSECTORS == 0
    return dict(
        sectors=[dict(s, center=list(s['center'])) for s in Lv['sectors']],
        walls=[dict(w, normal=list(w['normal']), v=list(w['v'])) for w in Lv['walls']],
        vertices=[dict(v) for v in Lv['vertices']],
        faces=[dict(f, v=list(f['v'])) for f in Lv['faces']],
        objects=[dict(o) for o in Lv['objects']],
        pushBlocks=[dict(p) for p in Lv['pushBlocks']],
        PBVert=[dict(p) for p in Lv['PBVert']],
        waveVert=[dict(w, connect=list(w['connect'])) for w in Lv['waveVert']],
        waveFace=[dict(w, connect=list(w['connect'])) for w in Lv['waveFace']],
        PBWall=list(Lv['PBWall']),
        objectParams=list(Lv['objectParams']),
        texture=list(Lv['texture']),
        vertexLight=list(Lv['vertexLight']),
        cutPlane=[list(cut[i:i + MAXCUTSECTORS]) for i in range(0, len(cut), MAXCUTSECTORS)],
        orderPairs=[(p['a'], p['b'], p['plane']) for p in Lv.get('orderPairs') or []],
    )


def _decode_sounds(data, lay):
    S = lay['sounds']
    r = MemReader(data, pos=S['off'])
    n = r.i32()
    smap = list(struct.unpack('>%dh' % n, r.read(2 * n)))
    ns = r.i32()
    snds = []
    for i in range(ns):
        size, rate, bps, loop = struct.unpack(SOUNDHDR_FMT, r.read(16))
        off = r.p
        assert off == S['sounds'][i]['off'], ('sound offset disagrees with lev.py', i)
        snds.append(dict(rate=rate, bps=bps, loopStart=loop, pcm=r.read(size)))
    assert r.p == S['end']
    return dict(map=smap, sounds=snds)


def _decode_palettes_tiles(data, lay):
    T = lay['tiles']
    r = MemReader(data, pos=T['off'])
    psz = r.i32()
    blk = r.read(psz)
    if psz < 2 or (psz - 2) % 512:
        raise ValueError('palette block size %d is not 2 + 512*N (PIC.C:617,630)' % psz)
    obj = struct.unpack('>h', blk[:2])[0]          # *palletes = object palette number, PIC.C:630
    n = (psz - 2) // 512
    pals = [list(struct.unpack('>256H', blk[2 + 512 * k: 2 + 512 * (k + 1)])) for k in range(n)]
    assert r.p == T['tileset_off']
    nm = r.i32()
    assert nm == T['count']
    tiles = []
    for i in range(nm):
        toff = r.p
        f = r.i16()
        assert (toff, TILE_KIND.get(f)) == tuple(T['tiles'][i]), ('tile walk disagrees with lev.py', i)
        if f in TILE_PIXELS:
            pal = r.i16(); tiles.append(dict(flags=f, palNm=pal, pixels=r.read(TILE_PIXELS[f])))
        elif f in TILE_RLE:
            pal = r.i16(); size = r.i16(); tiles.append(dict(flags=f, palNm=pal, rle=r.read(size)))
        elif f == TF_VDP2:
            x, y, w, h = struct.unpack(VDP2PIC_FMT, r.read(8)); tiles.append(dict(flags=f, x=x, y=y, w=w, h=h))
        else:
            raise ValueError('tile flags 0x%x' % f)
    assert r.p == T['end']
    return dict(objectPalette=obj, palettes=pals), tiles


def _decode_sequences(data, lay):
    Q = lay['sequences']
    r = MemReader(data, pos=Q['off'])
    size = r.i32()
    st = r.p
    ns, nf, nc = struct.unpack('>3i', r.read(12))
    frames = [dict(chunkIndex=a, flags=b, sound=c, pad=[d, e])
              for a, b, c, d, e in struct.iter_unpack(FRAME_FMT, r.read(8 * nf))]     # SEQUENCE.C:50
    chunks = [dict(chunkx=a, chunky=b, tile=c, flags=d, pad=e)
              for a, b, c, d, e in struct.iter_unpack(CHUNK_FMT, r.read(8 * nc))]     # SEQUENCE.C:60
    seq = list(struct.unpack('>%dh' % ns, r.read(2 * ns)))                           # SEQUENCE.C:70
    smap = list(struct.unpack('>%dh' % OT_NMTYPES, r.read(2 * OT_NMTYPES)))          # SEQUENCE.C:72
    if r.p - st != size:
        raise ValueError('sequence buffer %d != size %d (SEQUENCE.C:44-48)' % (r.p - st, size))
    assert r.p == Q['end'] == lay['end']
    return dict(frames=frames, chunks=chunks, sequence=seq, sequenceMap=smap)


def model_from_bytes(data, name='<mem>'):
    """Content model of one .LEV (see module doc). Also returns lev.py's layout dict."""
    lay = layout(data, name)
    if lay['trailing']:
        raise ValueError('%d trailing bytes after sequences (not interpreted by the engine)' % lay['trailing'])
    pal, tiles = _decode_palettes_tiles(data, lay)
    model = dict(sky=_decode_sky(data, lay), level=_decode_level(lay), sounds=_decode_sounds(data, lay),
                 palettes=pal, tiles=tiles, sequences=_decode_sequences(data, lay))
    return model, lay


def read_model(path):
    with open(path, 'rb') as f:
        data = f.read()
    return model_from_bytes(data, os.path.basename(path))


# ---------------------------------------------------------------- helpers for tests / reports
def diff(a, b, path='', out=None, cap=50):
    """Recursive structural diff -> list of 'path: detail' strings (at most `cap`)."""
    if out is None:
        out = []
    if len(out) >= cap:
        return out
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b), key=str):
            p = '%s.%s' % (path, k) if path else str(k)
            if k not in a or k not in b:
                out.append('%s: key only in %s' % (p, 'b' if k not in a else 'a'))
            else:
                diff(a[k], b[k], p, out, cap)
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            out.append('%s: len %d != %d' % (path, len(a), len(b)))
        else:
            for i, (x, y) in enumerate(zip(a, b)):
                if x != y:
                    diff(x, y, '%s[%d]' % (path, i), out, cap)
    elif isinstance(a, (bytes, bytearray)) and isinstance(b, (bytes, bytearray)):
        if a != b:
            out.append('%s: bytes differ (len %d/%d)' % (path, len(a), len(b)))
    elif type(a) is not type(b) or a != b:
        out.append('%s: %r != %r' % (path, a, b))
    return out


def blob_accounting(model, fsize):
    """Bytes of the file carried by opaque `bytes` payloads (B), u8 lists (U), and the rest (F = struct
    fields + derived counts/sizes, which the writer recomputes)."""
    B = len(model['sky']['bitmap']) + sum(len(s['pcm']) for s in model['sounds']['sounds'])
    for t in model['tiles']:
        B += len(t.get('pixels', b'')) + len(t.get('rle', b''))
    Lv = model['level']
    U = len(Lv['objectParams']) + len(Lv['texture']) + len(Lv['vertexLight']) + MAXCUTSECTORS * len(Lv['cutPlane'])
    return dict(opaque_bytes=B, u8_arrays=U, fields=fsize - B - U, fsize=fsize)

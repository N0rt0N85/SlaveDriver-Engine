#!/usr/bin/env python3
"""verif_static.py -- relit STATIC.DAT Doom et rejoue les 5 lecteurs du moteur (SRUINS.C:1922-1932).

Tests (SPEC_CONVERTER section 7, `verif_static.py`) :
  1. loadLoadingScreen SRUINS.C:1098-1120 : 512 o de CRAM, `int 320`, `int 240` (assertes), 76 800 o
  2. loadVDP2Sprites :1080-1096 : 262 144 o, tous a zero
  3. loadStaticSounds / loadSoundSet SOUND.C:231-241 : `int 8`, 8 shorts ([3] == 6), `int 20`, puis
     20 x (int size, rate 0x7000, bps 8, loop -1) + PCM, size pair, soundTop < 512 Ko (SOUND.C:218-219)
  4. loadWeaponTiles PIC.C:709 = loadTileSet : `int n`, n x (flags 0x6A, palNm, size > 0, RLE) ; chaque
     RLE decode exactement 4 096 pixels en consommant `size` octets (PIC.C:299-315) ; n == --tiles (96 : l'ombre + 95)
  5. loadWeaponSequences SEQUENCE.C:92-132 : `int size` == 12 + 8 f + 8 c + 2 s, 90 sequences,
     wSequence[0] == 0, croissante, terminale == nmFrames, frame terminale.chunkIndex == nmChunks,
     pads 0, sound -1, flags 0, chunk.tile < n ; fin de fichier exacte
  + (--ids, --wad) : pour chaque etat 1..89 dont le lump `sprite+lettre+0` est dans le WAD ET dans les
     familles decoupees, wseq(state) non vide ; les autres vides.

Usage : python tools\\doom2ps\\verif_static.py [STATIC.DAT] [--tiles 96] [--ids ...] [--wad ...]
Sortie 0 = tout vert ; sinon la premiere assertion qui tombe.
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import rle8                                            # noqa: E402
import wad2static                                      # noqa: E402

DEFAULT_DAT = wad2static.DEFAULT_OUT


class Reader:
    def __init__(self, data):
        self.b, self.p = data, 0

    def take(self, n):
        assert self.p + n <= len(self.b), "fichier tronque a %d (+%d > %d)" % (self.p, n, len(self.b))
        d = self.b[self.p:self.p + n]
        self.p += n
        return d

    def i32(self):
        return struct.unpack(">i", self.take(4))[0]

    def i16(self):
        return struct.unpack(">h", self.take(2))[0]


def static_summary(data):
    """Relit les blocs 1-4 sans les verifier a fond : -> dict(tileBase, static_pcm, static_sounds,
    weapon_tiles_bytes, wseq_bytes). `tileBase` = `int n` du bloc 4 (loadWeaponTiles retourne n,
    SRUINS.C:1930) : c'est lui que verif_doom.py soustrait de 255 (LEVEL.C:69-72)."""
    r = Reader(data)
    r.take(512 + 8 + 320 * 240)
    r.take(1024 * 256)
    ng = r.i32()
    r.take(2 * ng)
    ns = r.i32()
    pcm = 0
    for _ in range(ns):
        size = struct.unpack(">iiii", r.take(16))[0]
        r.take(size)
        pcm += size
    n = r.i32()
    wb = 0                                   # residents : RLE par tuile, aligne 4 (PIC.C:575, UTIL.C:370)
    for _ in range(n):
        r.take(4)
        sz = r.i16()
        r.take(sz)
        wb += (sz + 3) & ~3
    size = r.i32()                           # bloc wseq : mem_malloc(0, size) SEQUENCE.C:100
    r.take(size)
    assert r.p == len(data)
    return dict(tileBase=n, static_pcm=pcm, static_sounds=ns, weapon_tiles_bytes=wb,
                wseq_bytes=(size + 3) & ~3)


def verify(data, tiles_expected=96, ids=None, wad=None, families=None):
    r = Reader(data)
    rep = {}
    # 1
    cram = struct.unpack(">256H", r.take(512))
    xs, ys = r.i32(), r.i32()
    assert (xs, ys) == (320, 240), "loadLoadingScreen : %dx%d" % (xs, ys)
    screen = r.take(320 * 240)
    rep["screen"] = dict(cram_nonzero=sum(1 for c in cram if c), pixels_nonzero=sum(1 for c in screen if c))
    # 2
    sheet = r.take(1024 * 256)
    assert not any(sheet), "feuille VDP2 non nulle"
    # 3
    ng = r.i32()
    assert ng == 8, "ST_NMSTATICSOUNDGROUPS %d != 8" % ng
    smap = [r.i16() for _ in range(8)]
    assert smap[3] == 6 and smap == [0, 0, 0, 6, 0, 0, 0, 0], smap
    ns = r.i32()
    assert ns == 20, "%d sons statiques != 20" % ns
    top = 0
    for i in range(ns):
        size, rate, bps, loop = struct.unpack(">iiii", r.take(16))
        assert size > 0 and not (size & 1), "son %d size %d" % (i, size)
        assert rate == 0x7000 and bps == 8 and loop == -1, (i, rate, bps, loop)
        r.take(size)
        assert top + size < 512 * 1024
        top += size
    rep["sounds"] = dict(n=ns, pcm=top)
    # 4
    n = r.i32()
    assert n == tiles_expected, "tuiles d'armes n = %d != %d" % (n, tiles_expected)
    rle_total, rle_max = 0, 0
    for i in range(n):
        flags, pal, size = r.i16(), r.i16(), r.i16()
        assert flags == 0x6A and pal == 0 and size > 0, (i, flags, pal, size)
        rle = r.take(size)
        pix, used = rle8.unrle8(rle)
        assert len(pix) == 4096 and used == size, "tuile %d : RLE decode %d px avec %d/%d o" % (i, len(pix), used, size)
        rle_total += size
        rle_max = max(rle_max, size)
    rep["tiles"] = dict(n=n, tileBase=n, rle=rle_total, rle_max=rle_max)
    # 5
    size = r.i32()
    assert 0 < size < 1024 * 1024
    nseq, nfr, nch = struct.unpack(">iii", r.take(12))
    assert size == 12 + 8 * nfr + 8 * nch + 2 * nseq, "size %d != formule (%d, %d, %d)" % (size, nseq, nfr, nch)
    assert nseq == wad2static.WSEQ_LAST - wad2static.WSEQ_FIRST + 2, "nmSequences %d != 90" % nseq
    frames = [struct.unpack(">hhhbb", r.take(8)) for _ in range(nfr)]
    chunks = [struct.unpack(">hhhbb", r.take(8)) for _ in range(nch)]
    seqs = [r.i16() for _ in range(nseq)]
    assert seqs[0] == 0, "wSequence[0] != 0"
    assert all(a <= b for a, b in zip(seqs, seqs[1:])), "wSequence non croissante"
    assert seqs[-1] == nfr - 1, "terminale %d != nmFrames-1 %d" % (seqs[-1], nfr - 1)
    assert frames[-1][0] == nch, "frame terminale.chunkIndex %d != nmChunks %d" % (frames[-1][0], nch)
    for i, (ci, fl, so, p0, p1) in enumerate(frames):
        assert fl == 0 and so == -1 and p0 == 0 and p1 == 0, ("frame", i, fl, so, p0, p1)
        assert 0 <= ci <= nch
    assert all(a[0] <= b[0] for a, b in zip(frames, frames[1:]))
    for i, (cx, cy, t, fl, pad) in enumerate(chunks):
        assert 0 <= t < n and fl == 0 and pad == 0, ("chunk", i, t, fl, pad)
    assert r.p == len(data), "%d octets en trop apres les sequences d'armes" % (len(data) - r.p)
    rep["wseq"] = dict(sequences=nseq - 1, frames=nfr - 1, chunks=nch, block=size,
                       populated=[s for s in range(nseq - 1) if seqs[s] < seqs[s + 1]])
    # croisement avec le WAD / les etats
    if ids is not None and wad is not None:
        cut = set(wad2static.weapon_lumps(wad, families or wad2static.WEAPON_FAMILIES))
        bad = []
        for s in range(wad2static.WSEQ_FIRST, wad2static.WSEQ_LAST + 1):
            k = wad2static.wseq(s)
            expect = wad2static.psprite_lump(ids, s) in cut
            if (seqs[k] < seqs[k + 1]) != expect:
                bad.append((ids["state_names"][s], expect))
        assert not bad, "wseq incoherentes avec le WAD : %s" % bad
        rep["wseq"]["states_checked"] = wad2static.WSEQ_LAST
    rep["blocks"] = [512 + 8 + 76800, 1024 * 256, 4 + 16 + 4 + 20 * 16 + top,
                     4 + 6 * n + rle_total, 4 + size]
    rep["total"] = len(data)
    return rep


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dat", nargs="?", default=DEFAULT_DAT)
    ap.add_argument("--tiles", type=int, default=96, help="n attendu (96 = ombre + 95 ; 49 avec --e1m1-weapons)")
    ap.add_argument("--ids", default=wad2static.DEFAULT_IDS)
    ap.add_argument("--wad", default=wad2static.DEFAULT_WAD)
    ap.add_argument("--e1m1-weapons", action="store_true")
    ap.add_argument("--no-cross", action="store_true", help="sans croisement WAD/etats")
    a = ap.parse_args(argv)
    data = open(a.dat, "rb").read()
    ids = wad = None
    if not a.no_cross:
        import wad as wadmod
        ids = json.load(open(a.ids))
        wad = wadmod.Wad(a.wad)
    fams = wad2static.E1M1_FAMILIES if a.e1m1_weapons else wad2static.WEAPON_FAMILIES
    rep = verify(data, 49 if a.e1m1_weapons else a.tiles, ids, wad, fams)
    assert sum(rep["blocks"]) == rep["total"]
    print("%s : %d o, blocs %s : OK" % (a.dat, rep["total"], rep["blocks"]))
    print("ecran %s ; sons %s ; tuiles %s" % (rep["screen"], rep["sounds"], rep["tiles"]))
    w = rep["wseq"]
    print("wseq : %d sequences, %d frames, %d chunks, bloc %d o, %d peuplees%s"
          % (w["sequences"], w["frames"], w["chunks"], w["block"], len(w["populated"]),
             ", croisement %d etats OK" % w["states_checked"] if "states_checked" in w else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

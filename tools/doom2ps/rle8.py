#!/usr/bin/env python3
"""rle8.py -- tuiles 8 bpp RLE (flags 0x6A) de SlaveDriver a partir de patchs Doom.

Trois briques du contrat docs/doom/DOOM_ABI.md section 2 (SPEC_CONVERTER section 1, tache j2) :

  rle8(pixels)        encodeur EXACTEMENT inverse du decodeur du moteur (PIC.C:299-315) et copie de
                      `writeRLE` de l'outil d'origine (UTIL/STATIC.C:252-276) : sur les 4 096 octets
                      ligne par ligne, blocs `[n0 <= 255 zeros][n1 <= 255 litteraux][n1 octets]`
                      repetes tant qu'il reste des pixels. Le decodeur boucle `while (outSize <
                      nmPixels)` : il n'y a donc PAS de bloc `0 0` terminal (l'outil d'origine n'en
                      ecrit pas non plus). `size` est un short : assert <= 32 767.
  unrle8(data, n)     decodeur de controle = PIC.C:299-315 mot pour mot (identite testee sur tous
                      les lumps de sprites du WAD par `python tools/doom2ps/rle8.py`).
  cut_patch(...)      patch Doom (w, h, leftoffset lo, topoffset to) -> chunks 64x64 aux offsets monde
                      `chunkx = -lo + 64c`, `chunky = -to + 64r` (pixel (i, j) -> (i - lo, j - to),
                      y ecran croissant vers le bas, pieds a y = 0), image calee en haut-gauche du
                      chunk, reste = index 0 (transparent, PIC.C:305-312).
  object_palette(...) PLAYPAL[0] -> 256 BGR555 (bit 15) pour la palette objet + `remap_index` :
                      l'indice 0 est la transparence du moteur et 255 est force a 0xffff en CRAM
                      (PIC.C:631) -> 0 et 255 sont deplaces vers l'indice 1..254 le plus proche en RGB
                      (meme idee que `TileMaker.sub0`, doomtiles.py:46).

Vue miroir (rotations A2A8) : meme tuile, flag chunk bit 0 = miroir horizontal (WALLS.C:2789),
`chunkx' = -chunkx - 64 + (w - 2 lo)` -> `mirror_chunkx` : Doom garde leftoffset pour la vue
retournee (r_things.c) ; la convention PowerSlave `-chunkx - 64` (STATIC.C:512-515, offsets
centres) decalait chaque vue miroir de (w - 2 lo) px.

Usage : python tools\\doom2ps\\rle8.py [--wad W]   (test : identite RLE sur tous les lumps S_START..S_END,
        comptes de chunks et d'octets RLE ; ne modifie rien)
"""
from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
from collections import namedtuple

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import wad as wadmod                                   # noqa: E402
from doomtiles import bgr555                           # noqa: E402

DEFAULT_WAD = os.path.join(os.path.dirname(ROOT), "Mimas", "cd", "data", "DOOM1.WAD")

CELL = 64
NPIX = CELL * CELL
TILEFLAGS_8RLE_64 = 0x6A          # TILEFLAG_64x64 | 8BPP | PALLETE | RLE (SLEVEL.H:192-198)
MAX_RLE = 32767                   # `short size` (PIC.C:576-590)

# chunk d'un patch : offsets monde + 4 096 indices (0 = transparent), 0 = colonne/ligne de chunk
Chunk = namedtuple("Chunk", "chunkx chunky pixels")
Patch = namedtuple("Patch", "name w h lo to pix msk")


# ----------------------------------------------------------------------------- RLE
def rle8(pixels):
    """STATIC.C:252-276 (`writeRLE`) : [zeros][litteraux][octets]... jusqu'a la fin de l'entree."""
    assert len(pixels) == NPIX, "une tuile 0x6A fait 64x64 = 4096 indices (%d)" % len(pixels)
    out = bytearray()
    pos, n = 0, len(pixels)
    while pos < n:
        size = 0
        while size < 255 and pos + size < n and pixels[pos + size] == 0:
            size += 1
        out.append(size)
        pos += size
        size = 0
        while size < 255 and pos + size < n and pixels[pos + size] != 0:
            size += 1
        out.append(size)
        out += pixels[pos:pos + size]
        pos += size
    assert len(out) <= MAX_RLE, "RLE %d > 32767 (short size)" % len(out)
    return bytes(out)


def unrle8(data, nm_pixels=NPIX):
    """PIC.C:299-315 mot pour mot (decodeur de controle)."""
    out = bytearray()
    p = 0
    while len(out) < nm_pixels:
        i = data[p]
        p += 1
        out += b"\0" * i
        i = data[p]
        p += 1
        out += data[p:p + i]
        p += i
    assert len(out) == nm_pixels, "outSize %d != nmPixels %d (PIC.C:314)" % (len(out), nm_pixels)
    return bytes(out), p


# ----------------------------------------------------------------------------- palette objet
def object_palette(playpal):
    """(256 u16 BGR555, remap_index). Entree 0 = 0x0000 (transparence), 255 = 0xffff (PIC.C:631 la
    force de toute facon), 1..254 = PLAYPAL. `remap_index(i)` deplace 0 et 255 vers l'indice 1..254
    le plus proche en RGB (0 -> le plus proche du noir = `sub0`, doomtiles.py:46)."""
    assert len(playpal) == 256
    pal = [0x0000] + [bgr555(playpal[i]) for i in range(1, 255)] + [0xFFFF]

    def nearest(rgb):
        r, g, b = rgb
        return min(range(1, 255),
                   key=lambda i: (playpal[i][0] - r) ** 2 + (playpal[i][1] - g) ** 2
                   + (playpal[i][2] - b) ** 2)

    sub = {0: nearest(playpal[0]), 255: nearest(playpal[255])}

    def remap_index(i):
        return sub.get(i, i)

    remap_index.sub0 = sub[0]
    remap_index.sub255 = sub[255]
    return pal, remap_index


# ----------------------------------------------------------------------------- patchs
def read_patch(wad, name):
    """Patch Doom avec ses offsets (wad.patch jette lo/to : on relit l'en-tete, 4 shorts LE)."""
    d = wad.lump(name)
    w, h, lo, to = struct.unpack("<hhhh", d[:8])
    _w, _h, pix, msk = wad.patch(name)
    assert (_w, _h) == (w, h)
    return Patch(name, w, h, lo, to, pix, msk)


def cut_patch(patch, remap=None):
    """-> (chunks, ncols, nrows) ; chunk (c, r) : `chunkx = -lo + 64c`, `chunky = -to + 64r`, pixel
    (i, j) du patch -> case (i - 64c, j - 64r) ; hors masque = 0. Tous les chunks de la grille sont
    produits (meme vides : le dedoublonnage par sha1 n'en garde qu'une tuile)."""
    w, h, lo, to, pix, msk = patch.w, patch.h, patch.lo, patch.to, patch.pix, patch.msk
    ncols = (w + CELL - 1) // CELL
    nrows = (h + CELL - 1) // CELL
    if remap is None:
        remap = lambda i: i                              # noqa: E731
    table = bytes(remap(i) for i in range(256))
    chunks = []
    for r in range(nrows):
        for c in range(ncols):
            px = bytearray(NPIX)
            for j in range(min(CELL, h - CELL * r)):
                srow = (CELL * r + j) * w + CELL * c
                drow = j * CELL
                for i in range(min(CELL, w - CELL * c)):
                    if msk[srow + i]:
                        px[drow + i] = table[pix[srow + i]]
            chunks.append(Chunk(-lo + CELL * c, -to + CELL * r, bytes(px)))
    return chunks, ncols, nrows


def mirror_chunkx(chunkx, w, lo):
    """Vue miroir d'un chunk (flag 1) : Doom ne symetrise PAS l'offset (r_things.c
    R_ProjectSprite : `tx -= spriteoffset` ; `if (flip) startfrac = spritewidth - 1`), la vue
    retournee occupe la meme plage [-lo, w - lo] ; le pixel i va en x = -lo + (w - 1 - i). Avec
    la colonne de chunk k affichee en 63 - k : chunkx' = -chunkx - 64 + (w - 2 lo)."""
    return -chunkx - CELL + (w - 2 * lo)


def mirror_chunkx_origin(chunkx):
    """Vue miroir (flag 1) : STATIC.C:512-515, `width - (subx + 64)` avec l'origine en -lo."""
    return -chunkx - CELL


def tile_record(rle, pal_nm=0):
    """Enregistrement `tiles[]` du modele lev_io : loadTileSet PIC.C:687-690."""
    return dict(flags=TILEFLAGS_8RLE_64, palNm=pal_nm, rle=rle)


class TileBank:
    """Tuiles 0x6A dedoublonnees par sha1 du RLE (A2A8 : une tuile pour 2 vues)."""

    def __init__(self, pal_nm=0):
        self.tiles = []
        self.bytes_rle = 0
        self.pal_nm = pal_nm
        self._by_sha = {}

    def add(self, pixels):
        rle = rle8(pixels)
        key = hashlib.sha1(rle).digest()
        t = self._by_sha.get(key)
        if t is None:
            t = len(self.tiles)
            self._by_sha[key] = t
            self.tiles.append(tile_record(rle, self.pal_nm))
            self.bytes_rle += len(rle)
        return t


def sprite_lumps(wad):
    """Noms des lumps entre S_START et S_END (les sprites)."""
    a, b = wad.index["S_START"], wad.index["S_END"]
    return [nm for k, (nm, _, _) in enumerate(wad.dir) if a < k < b]


# ----------------------------------------------------------------------------- test
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=DEFAULT_WAD)
    a = ap.parse_args(argv)
    w = wadmod.Wad(a.wad)
    pal, remap = object_palette(w.playpal(0))
    print("palette objet : [0]=0x%04x [1]=0x%04x [255]=0x%04x ; 0 -> %d, 255 -> %d"
          % (pal[0], pal[1], pal[255], remap.sub0, remap.sub255))
    assert pal[0] == 0 and pal[255] == 0xFFFF and all(p & 0x8000 for p in pal[1:])
    # identite RLE sur des motifs synthetiques (bords : tout zero, tout plein, alternance)
    for name, px in (("zeros", bytes(NPIX)), ("plein", bytes([7]) * NPIX),
                     ("alterne", bytes([0, 9] * (NPIX // 2))),
                     ("runs300", (bytes(300) + bytes([3]) * 300) * 6 + bytes(NPIX - 3600))):
        enc = rle8(px)
        dec, used = unrle8(enc)
        assert dec == px and used == len(enc), name
        print("  rle %-8s %5d o" % (name, len(enc)))
    lumps = sprite_lumps(w)
    bank = TileBank()
    nchunks = 0
    maxrle = 0
    for nm in lumps:
        p = read_patch(w, nm)
        chunks, nc, nr = cut_patch(p, remap)
        assert len(chunks) == nc * nr
        for ch in chunks:
            enc = rle8(ch.pixels)
            dec, used = unrle8(enc)
            assert dec == ch.pixels and used == len(enc), nm
            maxrle = max(maxrle, len(enc))
            bank.add(ch.pixels)
        nchunks += len(chunks)
    print("identite RLE : %d lumps S_START..S_END, %d chunks, %d tuiles distinctes, %d o RLE, "
          "RLE max %d o" % (len(lumps), nchunks, len(bank.tiles), bank.bytes_rle, maxrle))
    # controle du contrat : PISGA0 57x62, lo -126, to -106 -> chunk (126, 106)
    if w.has("PISGA0"):
        p = read_patch(w, "PISGA0")
        ch, _, _ = cut_patch(p, remap)
        print("  PISGA0 %dx%d lo %d to %d -> chunk (%d, %d) ; miroir chunkx' = %d"
              % (p.w, p.h, p.lo, p.to, ch[0].chunkx, ch[0].chunky, mirror_chunkx(ch[0].chunkx, p.w, p.lo)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

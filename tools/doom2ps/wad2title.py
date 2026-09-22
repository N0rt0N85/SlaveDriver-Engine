#!/usr/bin/env python3
"""wad2title.py -- DTITLE.DAT : l'ecran titre Doom (logo M_DOOM, feu PSX, polices STCFN, crane).

Lu par game/doom/DOOM_TITLE.C (doom_titleFonts, doom_titlePicture). Tout vient du WAD SHAREWARE
(M_DOOM, STCFN*, M_SKULL1/2, PLAYPAL) : jamais d'art retail. Gros-boutiste, comme le reste du disque.

Bloc logo (LOGO_BLOCK_HEAD + w*h octets, w multiple de 4 : pas de remplissage). Le MEME bloc est le
bloc 1 de STATIC.DAT (wad2static.build_static), lu par doom_loadingScreen : l'ecran de chargement
reprend le logo et le feu du titre, LOADING en pochoir. `logo_block(wad, "black")` (--loading black) :
logo 0x0 et masque vide, le reste identique.

  0     char[4]   "DLG1"
  4     i16 x2    nLevels (37 = FIRE_LEVELS), 0
  8     u16[256]  PLAYPAL[0] en bgr555 (bit 15) -- banque CRAM 0 (le logo, NBG1)
  520   u16[40]   la rampe PSX de 37 couleurs en bgr555 EXACT (bit 15), puis 0 -- banque CRAM 1
                  a l'ecran titre (le feu, NBG0, N0CAOS = 1)
  600   u8[40]    P_title : niveau de feu -> index = l'identite 0..36 (dans la banque 1)
  640   u8[40]    P_load  : niveau de feu -> index PLAYPAL le plus proche (boucle de m_fire.c) ;
                  pendant un chargement la banque 1 ne survit pas (loadPalletes), la 0 si
  680   u8[256]   R : octets aleatoires (LCG, graine 1) -- derive (R & 3) et, a pQ8 = 128, decroissance
  936   u8[256]   U : 2e flux (graine 2) -- decroissance U < pQ8 quand pQ8 != 128
  1192  i16 x4    logoW, logoH, logoX, logoY : rectangle du logo dans la bitmap NBG1 (zoom x2)
  1200  u8[16][40] masque "LOADING" (1 bit par cellule de feu, bit fort = a gauche, 1 = lettre) :
                  STCFN x2, centre sur 320 ; pochoir de l'ecran de chargement (lignes de feu 126..141)
  1840  u8[w*h]   M_DOOM, indices PLAYPAL, 0 = transparent (masque du patch)
  1840 + w*h      fin (multiple de 4)

DTITLE.DAT :
  0     "DTT1", i32 taille du bloc logo, le bloc logo
  ...   i32 n + petite police (STCFN + espace de 4), format initFonts (wad2font), completee a 4
  ...   i32 n + grande police (STCFN x2 en y, x1,5 en x + espace de 6) : a x2 plein, les lignes des
        sous-ecrans du titre (MPRULES, LIGHTS, REMAP) depassaient les 320 px (357 px au pire) ;
        a x1,5 elles tiennent (276 px au pire, colonne REMAP 143 px <= 150)
  ...   i32 24, i32 19, M_SKULL1 puis M_SKULL2 en bgr555 (bit 15), 0 = transparent, lignes de 24 px

Usage : python tools\\doom2ps\\wad2title.py [--wad W] [--out cd_doom/DTITLE.DAT]
Ecriture atomique (temp + os.replace). make_e1m1 l'ecrit par wad2static.write_static.
"""
from __future__ import annotations

import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
for p in (HERE, os.path.join(ROOT, "tools", "duke2ps")):
    if p not in sys.path:
        sys.path.insert(0, p)
import wad as wadmod                                   # noqa: E402
import rle8                                            # noqa: E402
import wad2font                                        # noqa: E402
from doomtiles import bgr555                           # noqa: E402
from lev_write import atomic_write                     # noqa: E402

DEFAULT_WAD = rle8.DEFAULT_WAD
DEFAULT_OUT = os.path.join(ROOT, "cd_doom", "DTITLE.DAT")

TITLE_MAGIC, BLOCK_MAGIC = b"DTT1", b"DLG1"
FIRE_W = 320                                           # DOOM_TITLE.C FIRE_W
RAMP_SLOTS = 40                                        # u16[40] / u8[40] : 37 + remplissage
MASK_ROWS, MASK_BYTES = 16, FIRE_W // 8                # "LOADING" : STCFN x2 = 16 lignes
BITMAP_W, SCREEN_H = 160, 224                          # NBG1 zoom x2 : 320 px -> 160 ; 224 lignes
LOGO_Y = 6                                             # bitmap -> ecran 12
SKULL_W, SKULL_H = 24, 19                              # EZ_setChar : largeur multiple de 8
SMALL_SPACE, BIG_SPACE = 4, 6
BIG_XSCALE = 1.5                                       # grande police : x1,5 en x, x2 en y

# The fire's colours and the spread rule come from Doom 32X Resurrection's m_fire.c, under this
# licence (its header, verbatim):
#
#   Victor Luchits, Samuel Villarreal and Fabien Sanglard
#
#   The MIT License (MIT)
#
#   Copyright (c) 2021 Victor Luchits, Derek John Evans, id Software and ZeniMax Media
#
#   Permission is hereby granted, free of charge, to any person obtaining a copy
#   of this software and associated documentation files (the "Software"), to deal
#   in the Software without restriction, including without limitation the rights
#   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#   copies of the Software, and to permit persons to whom the Software is
#   furnished to do so, subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be included in all
#   copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#   SOFTWARE.
#
# La rampe PSX COMPLETE : fireRGBs de m_fire.c (51-90) dans son ordre, les 11 entrees qu'il met en
# commentaire comprises (marquees `# d32xr: commentee`) -- 26 + 11 = 37 couleurs, celles du feu de
# PSX Doom (repris par Fabien Sanglard, DoomFirePSX).
FIRE_RGBS = [
    (0x00, 0x00, 0x00),
    (0x1F, 0x07, 0x07),
    (0x2F, 0x0F, 0x07),    # d32xr: commentee
    (0x47, 0x0F, 0x07),
    (0x57, 0x17, 0x07),
    (0x67, 0x1F, 0x07),    # d32xr: commentee
    (0x77, 0x1F, 0x07),
    (0x8F, 0x27, 0x07),    # d32xr: commentee
    (0x9F, 0x2F, 0x07),
    (0xAF, 0x3F, 0x07),
    (0xBF, 0x47, 0x07),
    (0xC7, 0x47, 0x07),    # d32xr: commentee
    (0xDF, 0x4F, 0x07),
    (0xDF, 0x57, 0x07),
    (0xDF, 0x57, 0x07),
    (0xD7, 0x5F, 0x07),    # d32xr: commentee
    (0xD7, 0x5F, 0x07),
    (0xD7, 0x67, 0x0F),
    (0xCF, 0x6F, 0x0F),
    (0xCF, 0x77, 0x0F),    # d32xr: commentee
    (0xCF, 0x7F, 0x0F),
    (0xCF, 0x87, 0x17),
    (0xC7, 0x87, 0x17),    # d32xr: commentee
    (0xC7, 0x8F, 0x17),
    (0xC7, 0x97, 0x1F),
    (0xBF, 0x9F, 0x1F),
    (0xBF, 0x9F, 0x1F),    # d32xr: commentee
    (0xBF, 0xA7, 0x27),
    (0xBF, 0xA7, 0x27),
    (0xBF, 0xAF, 0x2F),    # d32xr: commentee
    (0xB7, 0xAF, 0x2F),
    (0xB7, 0xB7, 0x2F),    # d32xr: commentee
    (0xB7, 0xB7, 0x37),
    (0xCF, 0xCF, 0x6F),    # d32xr: commentee
    (0xDF, 0xDF, 0x9F),
    (0xEF, 0xEF, 0xC7),
    (0xFF, 0xFF, 0xFF),
]
FIRE_LEVELS = len(FIRE_RGBS)                           # 37 : niveaux 0..36 (DOOM_TITLE.C FIRE_MAX 36)
assert FIRE_LEVELS == 37 and FIRE_LEVELS <= RAMP_SLOTS

LOGO_BLOCK_HEAD = 4 + 4 + 512 + 2 * RAMP_SLOTS + 2 * RAMP_SLOTS + 256 + 256 + 8 + MASK_ROWS * MASK_BYTES
assert LOGO_BLOCK_HEAD == 1840


# ----------------------------------------------------------------------------- feu
def fire_cram():
    """La rampe en bgr555 exact (bit 15), completee de 0 a RAMP_SLOTS mots."""
    return [bgr555(c) for c in FIRE_RGBS] + [0] * (RAMP_SLOTS - FIRE_LEVELS)


def fire_palette(playpal):
    """P_load : la boucle de m_fire.c (I_InitMenuFire) -- distance RGB au carre, premier meilleur."""
    out = []
    for ar, ag, ab in FIRE_RGBS:
        best, bestdist = 0, 0xFFFFFFF
        for j, (r, g, b) in enumerate(playpal):
            d = (ar - r) ** 2 + (ag - g) ** 2 + (ab - b) ** 2
            if d < bestdist:
                best, bestdist = j, d
        out.append(best)
    return out


def rand_bytes(seed):
    """256 octets d'un LCG 32 bits (Numerical Recipes), l'octet haut de chaque etat."""
    x, out = seed & 0xFFFFFFFF, bytearray()
    for _ in range(256):
        x = (x * 1664525 + 1013904223) & 0xFFFFFFFF
        out.append(x >> 24)
    return bytes(out)


# ----------------------------------------------------------------------------- logo
def logo(wad, lump="M_DOOM"):
    """-> (x, y, w, h, pixels) dans la bitmap NBG1 (160 x 112 visibles au zoom x2). Le patch est
    centre a la colonne 80, puis elargi a un rectangle aligne sur 4 (x et w) par des colonnes
    transparentes : le maitre le copie en mots longs. 0 = transparent ; un pixel opaque d'indice 0
    est refuse (il deviendrait transparent)."""
    w, h, pix, msk = wad.patch(lump)
    x0 = (BITMAP_W - w) // 2                           # 123 -> 18 (ecran 36..281)
    x = x0 & ~3
    ww = (x0 - x + w + 3) & ~3
    assert x + ww <= BITMAP_W and LOGO_Y + h <= SCREEN_H // 2, (x, ww, h)
    out = bytearray(ww * h)
    for j in range(h):
        for i in range(w):
            if msk[j * w + i]:
                v = pix[j * w + i]
                assert v != 0, "%s : pixel opaque d'indice 0 en (%d, %d)" % (lump, i, j)
                out[j * ww + x0 - x + i] = v
    return x, LOGO_Y, ww, h, bytes(out)


def loading_mask(big_font):
    """16 x 320 bits : "LOADING" en STCFN x2 (mask_font), centre ; 1 = lettre."""
    height, _clut, widths, glyphs = wad2font.parse_font(big_font)
    assert height == MASK_ROWS
    text = "LOADING"
    total = sum(widths[ord(c)] for c in text) + 2 * (len(text) - 1)
    x = (FIRE_W - total) // 2
    bits = [[0] * FIRE_W for _ in range(MASK_ROWS)]
    for c in text:
        for y, row in enumerate(glyphs[ord(c)]):
            for i, q in enumerate(row):
                if q:
                    bits[y][x + i] = 1
        x += widths[ord(c)] + 2
    out = bytearray()
    for row in bits:
        for b in range(0, FIRE_W, 8):
            v = 0
            for k in range(8):
                v = (v << 1) | row[b + k]
            out.append(v)
    return bytes(out)


def skulls(wad):
    """M_SKULL1/2 -> 2 x 24*19 mots bgr555 (bit 15), 0 = transparent, cadres a gauche."""
    pal = wad.playpal(0)
    out = []
    for name in ("M_SKULL1", "M_SKULL2"):
        w, h, pix, msk = wad.patch(name)
        assert w <= SKULL_W and h == SKULL_H, (name, w, h)
        words = [0] * (SKULL_W * SKULL_H)
        for j in range(h):
            for i in range(w):
                if msk[j * w + i]:
                    words[j * SKULL_W + i] = bgr555(pal[pix[j * w + i]])
        out.append(struct.pack(">%dH" % len(words), *words))
    return out


def fonts(wad):
    """(petite, grande) au format initFonts : STCFN + espace, STCFN x2 en y / x1,5 en x + espace."""
    g = wad2font.font_glyphs("stcfn")
    pal = wad.playpal(0)
    small, _ = wad2font.font_table(wad, g, pal, scale=1, space=SMALL_SPACE)
    big, _ = wad2font.font_table(wad, g, pal, scale=2, space=BIG_SPACE, xscale=BIG_XSCALE)
    return small, big


def mask_font(wad):
    """La police du masque LOADING : STCFN x2 plein (le feu n'a pas les contraintes des menus)."""
    return wad2font.font_table(wad, wad2font.font_glyphs("stcfn"), wad.playpal(0), scale=2)[0]


# ----------------------------------------------------------------------------- assemblage
def logo_block(wad, loading="TITLEPIC"):
    """Le bloc logo ; `loading == "black"` : sans logo ni masque (ecran de chargement noir + feu)."""
    # --loading ne prend plus de lump (etape 3) : un ancien argument (INTERPIC...) echoue ici
    assert loading in ("TITLEPIC", "black"), "--loading %s : TITLEPIC ou black" % loading
    pal = wad.playpal(0)
    if loading == "black":
        x, y, w, h, pixels = 0, 0, 0, 0, b""
    else:
        x, y, w, h, pixels = logo(wad)
    p_load = fire_palette(pal)
    assert p_load[0] == 0 and 255 not in p_load, p_load
    out = BLOCK_MAGIC + struct.pack(">hh", FIRE_LEVELS, 0)
    out += struct.pack(">256H", *[bgr555(c) for c in pal])
    out += struct.pack(">%dH" % RAMP_SLOTS, *fire_cram())
    out += bytes(list(range(FIRE_LEVELS)) + [0] * (RAMP_SLOTS - FIRE_LEVELS))
    out += bytes(p_load + [0] * (RAMP_SLOTS - FIRE_LEVELS))
    out += rand_bytes(1) + rand_bytes(2)
    out += struct.pack(">hhhh", w, h, x, y)
    out += bytes(MASK_ROWS * MASK_BYTES) if loading == "black" else loading_mask(mask_font(wad))
    assert len(out) == LOGO_BLOCK_HEAD
    out += pixels
    assert not (w & 3) and len(out) % 4 == 0      # doom_loadingScreen lit w*h octets, sans remplissage
    return out


def pad4(b):
    return b + bytes(-len(b) & 3)


def title_file(wad):
    """-> (bytes de DTITLE.DAT, info)."""
    block = logo_block(wad)
    small, big = fonts(wad)
    s1, s2 = skulls(wad)
    out = TITLE_MAGIC + struct.pack(">i", len(block)) + block
    for f in (small, big):
        out += struct.pack(">i", len(pad4(f))) + pad4(f)
    out += struct.pack(">ii", SKULL_W, SKULL_H) + s1 + s2
    info = dict(bytes=len(out), logo_block=len(block), small=len(small), big=len(big),
                skull=len(s1))
    return out, info


def write_title(path, wad):
    data, info = title_file(wad)
    atomic_write(path, data)
    info["path"] = path
    return info


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=DEFAULT_WAD)
    ap.add_argument("--out", default=DEFAULT_OUT)
    a = ap.parse_args(argv)
    info = write_title(a.out, wadmod.Wad(a.wad))
    print("%s : %d o (bloc logo %d, polices %d + %d, cranes 2 x %d)"
          % (info["path"], info["bytes"], info["logo_block"], info["small"], info["big"], info["skull"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

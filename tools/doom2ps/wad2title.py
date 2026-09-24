#!/usr/bin/env python3
"""wad2title.py -- DTITLE.DAT : l'ecran titre Doom (logo M_DOOM, feu PSX, polices STCFN, crane).

Lu par game/doom/DOOM_TITLE.C (doom_titleFonts, doom_titlePicture). Tout vient du WAD SHAREWARE
(M_DOOM, STCFN*, M_SKULL1/2, PLAYPAL) : jamais d'art retail. Gros-boutiste, comme le reste du disque.

Le logo lui-meme est le lump TITLE_LOGO du .cfg, coupe a TITLE_LOGO_ROWS lignes d'ecran et
pose a TITLE_LOGO_Y (episodes.py) : M_DOOM agrandi x2, ou TITLEPIC entiere a x1 sans son logo id.

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
  1192  i16 x4    logoW, logoH, logoX, logoY : rectangle du logo dans la bitmap NBG1 (1:1)
  1200  u8[16][40] masque "LOADING" CORPS (1 bit/cellule, bit fort a gauche, 1 = cellule claire) :
  1840  u8[16][40] masque "LOADING" GLYPHE DILATE (le contour, deja grossi d'une cellule) :
  2480  i16 colonne du champ du POURCENTAGE (multiple de 8), i16 0 :
  2484  12 x (corps, glyphe dilate) x 16 x 2 octets : '0'..'9', '%', ' ' a largeur fixe 16 :
                  STCFN x2, centre sur 320 ; pochoir de l'ecran de chargement (lignes de feu 126..141)
  1840  u8[w*h]   le logo, indices PLAYPAL, 0 = transparent (masque du patch)
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
import math
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
DIGIT_W, DIGIT_BYTES = 24, 3                           # une case du pourcentage, a largeur fixe
                                                       # ('%' de STCFN x2 fait 18 px)
PCT_CELLS = 4                                          # "100%" : trois chiffres et le signe
PCT_GLYPHS = "0123456789% "                            # dans cet ordre dans le DAT
PCT_GAP = 16                                           # px entre "LOADING" et le champ
BITMAP_W, SCREEN_H = 320, 224                          # NBG1 en 1:1 : la bitmap EST l'ecran
LOGO_Y = 12                                            # la ligne d'ecran du haut du logo, par defaut
LOGO_OPAQUE = 247                                      # PLAYPAL 247 = (0,0,0) OPAQUE : l'indice 0
                                                       # est transparent dans la bitmap, donc un
                                                       # pixel noir du WAD passe par la (TITLEPIC
                                                       # en a 845 dans ses 133 premieres lignes)
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

LOGO_BLOCK_HEAD = (4 + 4 + 512 + 2 * RAMP_SLOTS + 2 * RAMP_SLOTS + 256 + 256 + 8
                   + 2 * MASK_ROWS * MASK_BYTES + 4
                   + len(PCT_GLYPHS) * 2 * MASK_ROWS * DIGIT_BYTES)
assert LOGO_BLOCK_HEAD == 3636      # DOOM_TITLE.C LB_PIXELS (masques, champ, glyphes)


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
def _proche(pal, cache, rgb):
    """L'index PLAYPAL le plus proche de rgb. 0 est TRANSPARENT dans la bitmap : jamais rendu."""
    v = cache.get(rgb)
    if v is None:
        r, g, b = rgb
        best, bd = 1, 1 << 30
        for j in range(1, 256):
            pr, pg, pb = pal[j]
            d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
            if d < bd:
                best, bd = j, d
        cache[rgb] = v = best
    return v


def agrandir_logo(w, h, pix, msk, pal, scale=2):
    """(w, h, pix, msk) -> le meme logo a `scale` fois la definition. La FORME du WAD, rien
    d'invente : le patch est reechantillonne en bilineaire, une fois pour le masque et une fois
    pour la couleur, et le resultat est requantifie sur la PLAYPAL.

    Le masque est seuille a la moitie de la couverture, ce qui a trois proprietes qui sont tout
    l'interet de la methode : une arete DROITE ne bouge pas d'un pixel (les poids valent 3/4 et
    1/4, jamais 1/2), un decrochement d'un pixel devient un CHANFREIN a 45 degres au lieu d'une
    marche de deux pixels -- c'est la fin du crenelage gros -- et un detail d'un seul pixel garde
    son aire (il ressort en 2x2). L'escalier des diagonales garde son pas du WAD mais en marches
    deux fois plus fines.

    La couleur, elle, est vraiment ANTIALIASEE : la moyenne ne porte que sur les points opaques
    (`cov`), donc le fond transparent ne deteint jamais sur le bord du logo, et a l'interieur les
    aretes du biseau dore sortent adoucies. Le CONTOUR, lui, ne peut pas l'etre : la bitmap NBG1
    est en 8 bpp indexes, l'index 0 est transparent et il n'y a pas d'alpha par pixel -- un bord
    a demi couvert n'existe pas. C'est la seule limite de la methode, et elle tient au materiel.
    """
    w2, h2 = w * scale, h * scale
    out = bytearray(w2 * h2)
    cache = {}
    inv = 1.0 / scale
    for y2 in range(h2):
        v = (y2 + 0.5) * inv - 0.5
        j0 = int(math.floor(v))
        fy = v - j0
        for x2 in range(w2):
            u = (x2 + 0.5) * inv - 0.5
            i0 = int(math.floor(u))
            fx = u - i0
            cov = nr = ng = nb = 0.0
            for dj, wy in ((0, 1.0 - fy), (1, fy)):
                jj = j0 + dj
                if wy == 0.0 or not (0 <= jj < h):
                    continue
                for di, wx in ((0, 1.0 - fx), (1, fx)):
                    ii = i0 + di
                    if wx == 0.0 or not (0 <= ii < w) or not msk[jj * w + ii]:
                        continue
                    wt = wx * wy
                    r, g, b = pal[pix[jj * w + ii]]
                    nr += wt * r
                    ng += wt * g
                    nb += wt * b
                    cov += wt
            if cov < 0.5:                              # moins de la moitie couverte : transparent
                continue
            out[y2 * w2 + x2] = _proche(pal, cache, (int(nr / cov + 0.5),
                                                     int(ng / cov + 0.5),
                                                     int(nb / cov + 0.5)))
    return w2, h2, bytes(out), bytes(1 if v else 0 for v in out)


def logo_shape(wad, lump="M_DOOM", rows=0):
    """(w, h, pix, msk) du lump APRES agrandissement et coupe -- ce que `logo` va cadrer, et ce
    que verif_static recoupe.

    L'AGRANDISSEMENT est le plus grand multiple ENTIER qui tienne encore dans l'ecran : M_DOOM
    123x60 passe a x2 (246x120, x3 deborderait en largeur) et TITLEPIC 320x200 reste a x1 -- son
    art est DEJA a la definition de l'ecran, l'agrandir puis le requantifier ne ferait que l'abimer.

    `rows` coupe le bas, en lignes d'ECRAN (0 = tout le lump). C'est ce qui garde de TITLEPIC le
    logo et le marine sans le logo id (qui commence a sa ligne 148) ni le bandeau de texte."""
    w, h, pix, msk = wad.patch(lump)
    scale = max(1, min(BITMAP_W // w, SCREEN_H // h))
    if scale > 1:
        w, h, pix, msk = agrandir_logo(w, h, pix, msk, wad.playpal(0), scale)
    if rows and rows < h:
        h = rows
        pix, msk = pix[:w * h], msk[:w * h]
    return w, h, pix, msk


def logo(wad, lump="M_DOOM", rows=0, y=None):
    """-> (x, y, w, h, pixels) dans la bitmap NBG1 (320 x 224, 1:1). Le lump, agrandi et coupe
    (logo_shape), est centre puis elargi a un rectangle aligne sur 4 (x et w) par des colonnes
    transparentes : le maitre le copie en mots longs.

    0 est TRANSPARENT dans la bitmap, donc un pixel opaque d'indice 0 prend LOGO_OPAQUE, qui est
    le MEME noir (0,0,0) mais opaque. Une image pleine comme TITLEPIC en est faite."""
    w, h, pix, msk = logo_shape(wad, lump, rows)
    y0 = LOGO_Y if y is None else y
    x0 = (BITMAP_W - w) // 2                           # 246 -> 37 (ecran 36..283 apres alignement)
    x = x0 & ~3
    ww = (x0 - x + w + 3) & ~3
    assert x + ww <= BITMAP_W and y0 + h <= SCREEN_H, (x, ww, y0, h)
    out = bytearray(ww * h)
    for j in range(h):
        for i in range(w):
            if msk[j * w + i]:
                out[j * ww + x0 - x + i] = pix[j * w + i] or LOGO_OPAQUE
    return x, y0, ww, h, bytes(out)


MASK_CLAIR = 0.35           # part de la luminance MAXIMALE a partir de laquelle une couleur
                            # de la police est du CORPS et non du contour


def loading_mask(big_font):
    """2 x 16 x 320 bits : "LOADING" en STCFN x2 (mask_font), centre. Plan 1 = les cellules
    CLAIRES (le corps lisible de la lettre), plan 2 = toutes les cellules OPAQUES.

    Un glyphe de STCFN n'est pas une silhouette : son corps est en rouge clair (indices 177..186)
    et son CONTOUR -- y compris le trou du O et les contre-formes du A et du D -- en rouge tres
    sombre (191, soit (67, 0, 0)). Un masque bati sur l'opacite prend les trois et donne un pate :
    "LOADING" etait illisible a l'ecran. La CLUT de la police est triee par luminance croissante
    (wad2font.font_table), il suffit donc de couper dedans : sous MASK_CLAIR du maximum, la
    cellule est du contour. Le moteur peint le plan 2 dilate d'une cellule en noir opaque, puis
    le plan 1 par-dessus dans le blanc du feu -- la lettre garde ses trous."""
    height, clut, widths, glyphs = wad2font.parse_font(big_font)
    assert height == MASK_ROWS
    lum = [0.0] * 16
    for i, c in enumerate(clut):
        lum[i] = 0.30 * (c & 31) + 0.59 * ((c >> 5) & 31) + 0.11 * ((c >> 10) & 31)
    seuil = MASK_CLAIR * max(lum)
    clair = [i and lum[i] >= seuil for i in range(16)]
    assert any(clair[1:]) and not all(clair[1:]), "la CLUT de la police ne se coupe pas en deux"
    text = "LOADING"
    mot = sum(widths[ord(c)] for c in text) + 2 * (len(text) - 1)
    total = mot + PCT_GAP + PCT_CELLS * DIGIT_W
    x = (FIRE_W - total) // 2
    plans = [[[0] * FIRE_W for _ in range(MASK_ROWS)] for _ in range(2)]
    for c in text:
        for y, row in enumerate(glyphs[ord(c)]):
            for i, q in enumerate(row):
                if q:
                    plans[1][y][x + i] = 1
                    if clair[q]:
                        plans[0][y][x + i] = 1
        x += widths[ord(c)] + 2
    # le champ du pourcentage commence sur un octet PLEIN : le moteur y recopie des octets
    pctx = ((x - 2 + PCT_GAP) + 7) & ~7
    assert pctx + PCT_CELLS * DIGIT_W <= FIRE_W, pctx
    return _plans_bytes(_dilater(plans)), pctx


def _dilater(plans):
    """plans[1] (le glyphe entier) grossi d'une cellule dans les huit directions : le contour
    noir de la lettre, cuit ici plutot qu'a chaque chargement."""
    h, w = len(plans[1]), len(plans[1][0])
    gros = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if not plans[1][y][x]:
                continue
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if 0 <= y + dy < h and 0 <= x + dx < w:
                        gros[y + dy][x + dx] = 1
    return [plans[0], gros]


def _plans_bytes(plans):
    out = bytearray()
    for bits in plans:
        for row in bits:
            for b in range(0, len(row), 8):
                v = 0
                for k in range(8):
                    v = (v << 1) | row[b + k]
                out.append(v)
    return bytes(out)


def digits_mask(big_font):
    """Les douze glyphes du pourcentage a largeur FIXE (DIGIT_W), corps et glyphe dilate, dans
    l'ordre de PCT_GLYPHS. Le moteur ecrit "  7%", " 42%", "100%" en recopiant quatre cases."""
    height, clut, widths, glyphs = wad2font.parse_font(big_font)
    assert height == MASK_ROWS
    lum = [0.0] * 16
    for i, c in enumerate(clut):
        lum[i] = 0.30 * (c & 31) + 0.59 * ((c >> 5) & 31) + 0.11 * ((c >> 10) & 31)
    seuil = MASK_CLAIR * max(lum)
    clair = [i and lum[i] >= seuil for i in range(16)]
    out = bytearray()
    for ch in PCT_GLYPHS:
        plans = [[[0] * DIGIT_W for _ in range(MASK_ROWS)] for _ in range(2)]
        if ch != " ":
            w = widths[ord(ch)]
            assert 0 < w <= DIGIT_W, (ch, w)
            x0 = (DIGIT_W - w) // 2
            for y, row in enumerate(glyphs[ord(ch)]):
                for i, q in enumerate(row):
                    if q:
                        plans[1][y][x0 + i] = 1
                        if clair[q]:
                            plans[0][y][x0 + i] = 1
        out += _plans_bytes(_dilater(plans))
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
def logo_block(wad, loading="TITLEPIC", lump=None, rows=0, y=None):
    """Le bloc logo ; `loading == "black"` : sans logo ni masque (ecran de chargement noir + feu).
    `lump`, `rows`, `y` : les cles TITLE_LOGO / TITLE_LOGO_ROWS / TITLE_LOGO_Y du .cfg."""
    # --loading ne prend plus de lump (etape 3) : un ancien argument (INTERPIC...) echoue ici
    assert loading in ("TITLEPIC", "black"), "--loading %s : TITLEPIC ou black" % loading
    pal = wad.playpal(0)
    if loading == "black":
        x, y, w, h, pixels = 0, 0, 0, 0, b""
    else:
        x, y, w, h, pixels = logo(wad, lump or "M_DOOM", rows, y)
    p_load = fire_palette(pal)
    assert p_load[0] == 0 and 255 not in p_load, p_load
    out = BLOCK_MAGIC + struct.pack(">hh", FIRE_LEVELS, 0)
    out += struct.pack(">256H", *[bgr555(c) for c in pal])
    out += struct.pack(">%dH" % RAMP_SLOTS, *fire_cram())
    out += bytes(list(range(FIRE_LEVELS)) + [0] * (RAMP_SLOTS - FIRE_LEVELS))
    out += bytes(p_load + [0] * (RAMP_SLOTS - FIRE_LEVELS))
    out += rand_bytes(1) + rand_bytes(2)
    out += struct.pack(">hhhh", w, h, x, y)
    if loading == "black":
        out += bytes(2 * MASK_ROWS * MASK_BYTES) + struct.pack(">hh", 0, 0)
        out += bytes(len(PCT_GLYPHS) * 2 * MASK_ROWS * DIGIT_BYTES)
    else:
        mf = mask_font(wad)
        masque, pctx = loading_mask(mf)
        out += masque + struct.pack(">hh", pctx, 0) + digits_mask(mf)
    assert len(out) == LOGO_BLOCK_HEAD
    out += pixels
    assert not (w & 3) and len(out) % 4 == 0      # doom_loadingScreen lit w*h octets, sans remplissage
    return out


def pad4(b):
    return b + bytes(-len(b) & 3)


def title_file(wad, lump=None, rows=0, y=None):
    """-> (bytes de DTITLE.DAT, info)."""
    block = logo_block(wad, lump=lump, rows=rows, y=y)
    small, big = fonts(wad)
    s1, s2 = skulls(wad)
    out = TITLE_MAGIC + struct.pack(">i", len(block)) + block
    for f in (small, big):
        out += struct.pack(">i", len(pad4(f))) + pad4(f)
    out += struct.pack(">ii", SKULL_W, SKULL_H) + s1 + s2
    info = dict(bytes=len(out), logo_block=len(block), small=len(small), big=len(big),
                skull=len(s1))
    return out, info


def write_title(path, wad, lump=None, rows=0, y=None):
    data, info = title_file(wad, lump, rows, y)
    atomic_write(path, data)
    info["path"] = path
    return info


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=DEFAULT_WAD)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--logo", default="M_DOOM", help="le lump du logo (cle TITLE_LOGO du .cfg)")
    ap.add_argument("--logo-rows", type=int, default=0, help="lignes d'ecran gardees (0 = tout)")
    ap.add_argument("--logo-y", type=int, default=None, help="la ligne d'ecran ou il commence")
    a = ap.parse_args(argv)
    info = write_title(a.out, wadmod.Wad(a.wad), a.logo, a.logo_rows, a.logo_y)
    print("%s : %d o (bloc logo %d, polices %d + %d, cranes 2 x %d)"
          % (info["path"], info["bytes"], info["logo_block"], info["small"], info["big"], info["skull"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

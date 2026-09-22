#!/usr/bin/env python3
"""wad2font.py -- polices Doom (patchs STCFN/STTNUM/STYSNUM) au format lu par initFonts (PRINT.C:51-80).

Format d'une police (FONT1.H `brianFont[]`, FONT2.H `bigFont[]`), relu par PRINT.C:51-80 :

  short hauteur (gros-boutiste, :62)
  32 octets  = CLUT 16 x u16 BGR555 (EZ_setLookupTbl(f, font + 2), :63 ; SPR.C:124-130 : 32 o)
  256 octets = largeur de chaque code (0 = absent ; `char`, donc <= 127) (:64-65)
  puis, code par code croissant, pour chaque largeur != 0 :
      hauteur x ceil(largeur / 2) octets 4 bpp (COLOR_1, pixel gauche = quartet HAUT), :73-75 ;
      initFonts complete lui-meme chaque ligne a un multiple de 8 px (:76-77, buffer 32*32 = 1 024 o
      -> hauteur x ceil8(largeur)/2 <= 1 024).
  Index 0 = transparent ; drawString avance de largeur + 1 (PRINT.C:145).

Un glyphe Doom `(w, h, lo, to)` est dessine par V_DrawPatch a `(x - lo, y - to)` : tous les glyphes d'une
police partagent l'origine (x, y) -> ligne de police = j - to + max(to), hauteur = max(h - to) + max(to)
(STTMINUS 8x6 to -5 tombe aux lignes 5..10 des 16 ; STCFN095 '_' 3 lignes to -4 -> lignes 4..6).
Un `lo` negatif (STTNUM1 : -1, le seul des 3 polices [wad]) devient -lo colonnes vides a gauche du glyphe
(largeur + (-lo)) : le HUD dessine chaque chiffre a gauche de sa case (drawChar), le decalage de Doom
est donc cuit dans la police ; `lo` > 0 est refuse (le glyphe deborderait a gauche de sa case).

Couleurs : les indices PLAYPAL distincts des glyphes ; s'il y en a plus de 15 (STCFN et STTNUM en ont
18), fusion agglomerative de la paire la plus proche en RGB (l'indice le plus frequent survit) -- le
`quantize.py` de duke2ps cite par la spec est une quantification GEOMETRIQUE, pas de couleurs, d'ou ce
petit quantificateur local. CLUT[0] = 0x0000, CLUT[1..k] tries par luminance croissante.

Usage : python tools\\doom2ps\\wad2font.py [--wad W]   (test : construit les 3 polices, verifie la
        relecture PRINT.C:51-80 et imprime les tailles ; n'ecrit rien -- wad2hud.py emet le .h)
"""
from __future__ import annotations

import argparse
import os
import struct
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import wad as wadmod                                   # noqa: E402
import rle8                                            # noqa: E402
from doomtiles import bgr555                           # noqa: E402

DEFAULT_WAD = rle8.DEFAULT_WAD
CLUT_ENTRIES = 16                                      # COLOR_1 : 16 couleurs, 32 octets
MAX_COLORS = CLUT_ENTRIES - 1                          # l'entree 0 est la transparence
MAX_WIDTH = 127                                        # `char widths[]` PRINT.C:19
FONT_BUFFER = 32 * 32                                  # PRINT.C:53

# code -> lump. STGNUM (chiffres gris du panneau ARMS, st_stuff.c) partage la police des STYSNUM au code
# '0'..'9' | 0x80 (DOOM_FONT_GRAY_DIGIT dans doom_art.h) : 0xB0..0xB9 n'ont pas de glyphe dans les
# polices PowerSlave, et la table d'equivalents de PRINT.C:88-100 ne les touche pas (0xB1 y est
# seulement la source d'un alias vers 0xF1, absent ici : PRINT.C:106-109 ne fait alors rien).
GRAY_DIGIT = 0x80


def font_glyphs(name):
    """Les trois jeux du contrat : 'stcfn' (messages), 'sttnum' (grands chiffres), 'stysnum' (petits)."""
    if name == "stcfn":
        return {c: "STCFN%03d" % c for c in range(33, 96)}
    if name == "sttnum":
        g = {ord("0") + i: "STTNUM%d" % i for i in range(10)}
        g[ord("%")] = "STTPRCNT"
        g[ord("-")] = "STTMINUS"
        return g
    if name == "stysnum":
        g = {ord("0") + i: "STYSNUM%d" % i for i in range(10)}
        g.update({(ord("0") + i) | GRAY_DIGIT: "STGNUM%d" % i for i in range(10)})
        return g
    raise ValueError(name)


# ----------------------------------------------------------------------------- couleurs
def quantize_indices(counts, playpal, limit=MAX_COLORS):
    """{indice PLAYPAL: effectif} -> (liste d'indices retenus, {indice: indice retenu}). Fusion
    agglomerative de la paire la plus proche en RGB jusqu'a `limit` couleurs."""
    alive = dict(counts)
    remap = {i: i for i in alive}

    def dist(a, b):
        return sum((playpal[a][k] - playpal[b][k]) ** 2 for k in range(3))

    while len(alive) > limit:
        keys = sorted(alive)
        best = min(((dist(a, b), a, b) for x, a in enumerate(keys) for b in keys[x + 1:]))
        _, a, b = best
        keep, drop = (a, b) if alive[a] >= alive[b] else (b, a)
        alive[keep] += alive.pop(drop)
        for k, v in remap.items():
            if v == drop:
                remap[k] = keep
    kept = sorted(alive, key=lambda i: (sum(playpal[i]), i))     # luminance croissante
    return kept, remap


def read_glyph(wad, lump):
    p = rle8.read_patch(wad, lump)
    assert 0 < p.w <= MAX_WIDTH, "%s : largeur %d hors `char`" % (lump, p.w)
    return p


# ----------------------------------------------------------------------------- police
def font_table(wad, glyphs, playpal=None, scale=1, space=None, xscale=None):
    """glyphs : {code: lump} -> (bytes au format PRINT.C:51-80, info). `playpal` : PLAYPAL[0].
    `scale` 2 : chaque pixel double en x et en y (hauteur et largeurs x2, meme CLUT) -- la grande
    police de l'ecran titre (DTITLE.DAT, wad2title.py). `xscale` (defaut = `scale`) : l'echelle en x
    seule, 1, 1.5 ou 2 ; 1.5 double une colonne sur deux (les paires), largeur = (3w + 1) // 2 -- un
    trait de 2 px en fait toujours 3. `space` : ajoute le code 32 (espace), de
    cette largeur en pixels FINAUX, tout transparent : STCFN n'en a pas, et drawString saute sans
    avancer un code de largeur 0 ("NEW GAME" s'ecrirait "NEWGAME"). Defauts = polices du HUD
    (doom_art.h), octet pour octet."""
    if xscale is None:
        xscale = scale
    assert scale in (1, 2) and xscale in (1, 1.5, 2), (scale, xscale)
    if playpal is None:
        playpal = wad.playpal(0)
    patches = {c: read_glyph(wad, l) for c, l in glyphs.items()}
    max_to = max(p.to for p in patches.values())
    height = max(p.h - p.to for p in patches.values()) + max_to
    counts = Counter()
    for p in patches.values():
        counts.update(p.pix[i] for i in range(p.w * p.h) if p.msk[i])
    kept, remap = quantize_indices(counts, playpal)
    clut_index = {idx: k + 1 for k, idx in enumerate(kept)}        # 1..15
    clut = [0] + [bgr555(playpal[i]) for i in kept]
    clut += [0] * (CLUT_ENTRIES - len(clut))
    rows_of = {}                                                   # code -> lignes d'indices CLUT
    for code in sorted(patches):
        p = patches[code]
        assert p.lo <= 0, "code %d : leftoffset %d > 0 non representable" % (code, p.lo)
        pad = -p.lo
        y0 = -p.to + max_to
        rows = []
        for y in range(height):
            j = y - y0
            row = [0] * pad
            for x in range(p.w):
                v = 0
                if 0 <= j < p.h and p.msk[j * p.w + x]:
                    v = clut_index[remap[p.pix[j * p.w + x]]]
                row.append(v)
            rows.append(row)
        rows_of[code] = rows
    if scale != 1 or xscale != 1:
        rep = lambda x: 2 if xscale == 2 or (xscale == 1.5 and not x & 1) else 1   # noqa: E731
        rows_of = {c: [[v for x, v in enumerate(r) for _ in range(rep(x))] for r in rows for _ in range(scale)]
                   for c, rows in rows_of.items()}
        height *= scale
    if space is not None:
        assert 0 < space <= MAX_WIDTH and 32 not in rows_of, space
        rows_of[32] = [[0] * space for _ in range(height)]
    widths = [0] * 256
    data = bytearray()
    for code in sorted(rows_of):
        rows = rows_of[code]
        widths[code] = len(rows[0])
        assert 0 < widths[code] <= MAX_WIDTH
        assert height * (((widths[code] + 7) & ~7) >> 1) <= FONT_BUFFER
        for row in rows:
            if len(row) & 1:
                row = row + [0]
            for x in range(0, len(row), 2):
                data.append((row[x] << 4) | row[x + 1])
    out = struct.pack(">h", height) + struct.pack(">16H", *clut) + bytes(widths) + bytes(data)
    info = dict(height=height, glyphs=len(rows_of), colors_in=len(counts), colors=len(kept),
                merged={k: v for k, v in remap.items() if k != v}, bytes=len(out))
    return out, info


def parse_font(data):
    """Relecture = PRINT.C:51-80 : (hauteur, CLUT, largeurs, {code: lignes de quartets})."""
    height = struct.unpack(">h", data[:2])[0]
    clut = list(struct.unpack(">16H", data[2:34]))
    widths = list(data[34:290])
    pos = 290
    glyphs = {}
    for code in range(256):
        if not widths[code]:
            continue
        rows = []
        for _y in range(height):
            n = (widths[code] + 1) >> 1
            b = data[pos:pos + n]
            pos += n
            rows.append([q for byte in b for q in ((byte >> 4) & 15, byte & 15)][:widths[code]])
        glyphs[code] = rows
    assert pos == len(data), "octets en trop apres le dernier glyphe (%d != %d)" % (pos, len(data))
    return height, clut, widths, glyphs


def build_fonts(wad):
    """Les 3 polices du contrat -> {nom: (bytes, info)}."""
    pal = wad.playpal(0)
    return {n: font_table(wad, font_glyphs(n), pal) for n in ("stcfn", "sttnum", "stysnum")}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=DEFAULT_WAD)
    a = ap.parse_args(argv)
    w = wadmod.Wad(a.wad)
    for name, (data, info) in build_fonts(w).items():
        h, clut, widths, glyphs = parse_font(data)
        assert h == info["height"] and len(glyphs) == info["glyphs"]
        assert clut[0] == 0 and all(c & 0x8000 for c in clut[1:info["colors"] + 1])
        used = set(q for rows in glyphs.values() for r in rows for q in r)
        assert max(used) <= info["colors"]
        print("%-8s h %2d, %2d glyphes, couleurs %d -> %d (fusions %s), %5d o, largeurs %s"
              % (name, h, len(glyphs), info["colors_in"], info["colors"],
                 info["merged"] or "-", len(data),
                 sorted(set(widths[c] for c in glyphs))))
        if name == "sttnum":
            rows = glyphs[ord("-")]
            filled = [y for y, r in enumerate(rows) if any(r)]
            print("         STTMINUS (to -5) aux lignes %d..%d de %d" % (filled[0], filled[-1], h))
        if name == "stysnum":
            assert widths[ord("2") | GRAY_DIGIT] == 4 and widths[ord("2")] == 4
    return 0


if __name__ == "__main__":
    sys.exit(main())

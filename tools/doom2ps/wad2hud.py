#!/usr/bin/env python3
"""wad2hud.py -- art HUD Doom en tableaux C : build/doom/doom_art.h (SPEC_CONVERTER section 3bis).

Le moteur lit deja son fond de barre comme tableau C a en-tete 8 o (STATBAR.C:74 `stat_bar[] =
{0,0,1,64, 0,0,0,42, ...}` = `int w, int h` gros-boutistes + w*h indices 8 bpp) via
`EZ_setChar(0, COLOR_4, *(int *)stat_bar, *(int *)(stat_bar+4), stat_bar+8)` (SRUINS.C:1890-1891).
EZ_setChar (SPR.C:96-120) exige `!(width & 3)` et copie `width*height` octets en COLOR_4 (8 bpp,
banc CRAM 0 = palette objet du .LEV = PLAYPAL avec 0 = transparent et 255 = 0xffff, PIC.C:631) : les
indices 0/255 des patchs sont donc remappes comme les sprites (rle8.object_palette), pixels hors
masque = 0 (transparent).

Tableaux emis (tous `unsigned char`, non const : `fontList[]` PRINT.C:17 et EZ_setChar prennent des
pointeurs non const ; alignes 4 pour `*(int *)` et dmaMemCpy) :
  doom_stbar[8 + 320*32]     STBAR 320x32 + STARMS 40x32 colle a x = 104 (ST_ARMSBGX), en-tete {320, 32}
  doom_faces[26][24*32]      STFST00..42 (5 paliers x 3), STFKILL0-4, STFEVL0-4, STFDEAD0 : 24 x 29-31,
                             places a (143 - lo, 168 - to) - (148, 169) : le char se dessine en
                             Doom (148, 169) = DOOM_FACE_ORG_X/Y, sans en-tete (768 o)
  doom_keys[3][8*8]          STKEYS0-2 7x5 centres dans 8x8 (bleu, jaune, rouge), sans en-tete
  doom_font_stcfn[]          STCFN033..095                       format initFonts (wad2font.py)
  doom_font_sttnum[]         STTNUM0-9, STTPRCNT '%', STTMINUS '-'
  doom_font_stysnum[]        STYSNUM0-9 '0'..'9', STGNUM0-9 aux codes '0'..'9' | 0x80

Le .h est inclus par DOOM_HUD.C et PRINT.C, avec deux interrupteurs de definition (sinon `extern`) :
  DOOM_ART_DEFINE        -> doom_stbar, doom_faces, doom_keys : DOOM_HUD.C (MAIN seulement)
  DOOM_ART_DEFINE_FONTS  -> doom_font_* : PRINT.C via game/doom/DOOM_FONTS.H -- PRINT.o est lie dans
                            INIT, MAIN et KEYGEN (Makefile INIT_C/KEYGEN_C), les polices doivent donc
                            vivre dans PRINT.o, pas dans DOOM_HUD.o (absent d'INIT/KEYGEN).

Usage : python tools\\doom2ps\\wad2hud.py [WAD] [OUT]   (defauts : ../Mimas/cd/data/DOOM1.WAD,
        build/doom/doom_art.h ; forme de la regle Makefile du contrat section 8 :
        `wad2hud.py $(DOOMWAD) $@`). Ecriture atomique. Ne modifie rien d'autre.
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
from lev_write import atomic_write                     # noqa: E402

DEFAULT_WAD = rle8.DEFAULT_WAD
DEFAULT_OUT = os.path.join(ROOT, "build", "doom", "doom_art.h")

STBAR_W, STBAR_H = 320, 32
ARMS_X = 104                                           # ST_ARMSBGX st_stuff.c
FACE_W, FACE_H = 24, 32
FACE_ORG_X, FACE_ORG_Y = 148, 169                      # Doom screen position of the face char's top-left
KEY_W, KEY_H = 8, 8
FACES = (["STFST%d%d" % (pain, i) for pain in range(5) for i in range(3)]
         + ["STFKILL%d" % i for i in range(5)] + ["STFEVL%d" % i for i in range(5)] + ["STFDEAD0"])
KEYS = ["STKEYS0", "STKEYS1", "STKEYS2"]


# ----------------------------------------------------------------------------- chars 8 bpp
def blit(buf, w, h, patch, at, remap):
    x0, y0 = at
    for j in range(patch.h):
        y = y0 + j
        if not 0 <= y < h:
            continue
        for i in range(patch.w):
            x = x0 + i
            if 0 <= x < w and patch.msk[j * patch.w + i]:
                buf[y * w + x] = remap(patch.pix[j * patch.w + i])


def hud_pixels(wad, lump, w, h, *, at=(0, 0), extra=None, remap=None):
    """w*h indices 8 bpp : `lump` cale a `at` (haut-gauche), puis `extra` = [(lump, (x, y))]."""
    if remap is None:
        _, remap = rle8.object_palette(wad.playpal(0))
    assert not (w & 3), "EZ_setChar : width multiple de 4 (SPR.C:100)"
    buf = bytearray(w * h)
    blit(buf, w, h, rle8.read_patch(wad, lump), at, remap)
    for name, pos in (extra or []):
        blit(buf, w, h, rle8.read_patch(wad, name), pos, remap)
    return bytes(buf)


def hud_char(wad, lump, w, h, *, at=(0, 0), extra=None, remap=None):
    """{int w, int h} gros-boutistes + indices : la forme de stat_bar (STATBAR.C:74)."""
    return struct.pack(">ii", w, h) + hud_pixels(wad, lump, w, h, at=at, extra=extra, remap=remap)


def centered(wad, lump, w, h):
    p = rle8.read_patch(wad, lump)
    assert p.w <= w and p.h <= h, "%s %dx%d > %dx%d" % (lump, p.w, p.h, w, h)
    return ((w - p.w) // 2, (h - p.h) // 2)


# ----------------------------------------------------------------------------- .h
def c_array(name, dims, data, per_line=32, rows=1):
    """`rows` > 1 : tableau [rows][len/rows], chaque ligne entre accolades (-Wmissing-braces)."""
    lines = ["unsigned char %s%s __attribute__((aligned(4))) = {" % (name, dims)]
    stride = len(data) // rows
    assert stride * rows == len(data)
    for r in range(rows):
        row = data[r * stride:(r + 1) * stride]
        if rows > 1:
            lines.append("{")
        for i in range(0, len(row), per_line):
            lines.append(",".join(str(b) for b in row[i:i + per_line]) + ",")
        if rows > 1:
            lines.append("},")
    lines.append("};")
    return "\n".join(lines)


def build_doom_art(wad):
    """-> (texte du .h, info)."""
    _, remap = rle8.object_palette(wad.playpal(0))
    stbar = hud_char(wad, "STBAR", STBAR_W, STBAR_H, extra=[("STARMS", (ARMS_X, 0))], remap=remap)
    faces = []
    for name in FACES:
        p = rle8.read_patch(wad, name)
        # Doom draws the face at (ST_FACESX 143 - lo, ST_FACESY 168 - to) ; the char's origin is
        # (FACE_ORG_X, FACE_ORG_Y) = (148, 169) [wad: lo = -5 for all 26, to = -2 or -1], the
        # offsets are baked in so every face keeps Doom's pixel position under one draw point
        at = (-p.lo - (FACE_ORG_X - 143), -p.to - (FACE_ORG_Y - 168))
        assert at[0] >= 0 and at[1] >= 0, "%s lo/to %d/%d" % (name, p.lo, p.to)
        assert p.w + at[0] <= FACE_W and p.h + at[1] <= FACE_H, "%s %dx%d at %r" % (name, p.w, p.h, at)
        faces.append(hud_pixels(wad, name, FACE_W, FACE_H, at=at, remap=remap))
    keys = [hud_pixels(wad, k, KEY_W, KEY_H, at=centered(wad, k, KEY_W, KEY_H), remap=remap)
            for k in KEYS]
    fonts = wad2font.build_fonts(wad)
    info = dict(stbar=len(stbar), faces=len(faces) * FACE_W * FACE_H, keys=len(keys) * KEY_W * KEY_H,
                fonts={n: i for n, (_, i) in fonts.items()},
                sub0=remap.sub0, sub255=remap.sub255)
    hdr = ["/* doom_art.h -- genere par tools/doom2ps/wad2hud.py depuis %s : NE PAS EDITER.\n"
           "   Art HUD Doom en tableaux C (SPEC_CONVERTER 3bis, SPEC_PLAYER 3, contrat DOOM_ABI 5/8).\n"
           "   Indices 8 bpp = PLAYPAL, banc CRAM 0 (0 transparent -> %d, 255 -> %d) ; polices au\n"
           "   format initFonts PRINT.C:51-80. DOOM_ART_DEFINE (DOOM_HUD.C) definit barre/visages/cles,\n"
           "   DOOM_ART_DEFINE_FONTS (PRINT.C via DOOM_FONTS.H) les polices ; sinon declarations extern. */"
           % (os.path.basename(wad.path), remap.sub0, remap.sub255),
           "#ifndef DOOM_ART_H", "#define DOOM_ART_H", "",
           "#define DOOM_STBAR_W %d" % STBAR_W, "#define DOOM_STBAR_H %d" % STBAR_H,
           "#define DOOM_ARMS_X %d" % ARMS_X,
           "#define DOOM_FACE_W %d" % FACE_W, "#define DOOM_FACE_H %d" % FACE_H,
           "#define DOOM_FACE_ORG_X %d                        /* Doom x of the face char (lo baked in) */"
           % FACE_ORG_X,
           "#define DOOM_FACE_ORG_Y %d                        /* Doom y of the face char (to baked in) */"
           % FACE_ORG_Y,
           "#define DOOM_NMFACES %d" % len(FACES),
           "#define DOOM_FACE_ST(pain, i) ((pain) * 3 + (i))   /* STFST{pain}{i}, pain 0..4, i 0..2 */",
           "#define DOOM_FACE_KILL(pain)  (15 + (pain))        /* STFKILL0-4 */",
           "#define DOOM_FACE_EVL(pain)   (20 + (pain))        /* STFEVL0-4 */",
           "#define DOOM_FACE_DEAD        25                   /* STFDEAD0 */",
           "#define DOOM_KEY_W %d" % KEY_W, "#define DOOM_KEY_H %d" % KEY_H,
           "#define DOOM_NMKEYS 3                              /* STKEYS0 bleu, 1 jaune, 2 rouge */",
           "#define DOOM_FONT_GRAY_DIGIT(c) ((c) | 0x%02x)      /* STGNUM dans doom_font_stysnum */"
           % wad2font.GRAY_DIGIT, ""]
    for n, (_, i) in fonts.items():
        hdr.append("#define DOOM_FONT_%s_H %d" % (n.upper(), i["height"]))
    decl = [("doom_stbar", "[%d]" % len(stbar), stbar, 1),
            ("doom_faces", "[%d][%d]" % (len(faces), FACE_W * FACE_H), b"".join(faces), len(faces)),
            ("doom_keys", "[%d][%d]" % (len(keys), KEY_W * KEY_H), b"".join(keys), len(keys))]
    for n, (data, _) in fonts.items():
        decl.append(("doom_font_%s" % n, "[%d]" % len(data), data, 1))
    out = list(hdr)
    for switch, group in (("DOOM_ART_DEFINE", decl[:3]), ("DOOM_ART_DEFINE_FONTS", decl[3:])):
        out += ["", "#ifdef %s" % switch, ""]
        out += [c_array(n, d, b, rows=r) for n, d, b, r in group]
        out += ["", "#else", ""]
        out += ["extern unsigned char %s%s;" % (n, d) for n, d, _, _ in group]
        out += ["", "#endif /* %s */" % switch]
    out += ["#endif /* DOOM_ART_H */", ""]
    info["arrays"] = {n: len(b) for n, _, b, _ in decl}
    return "\n".join(out), info


def write_doom_art(path, wad):
    text, info = build_doom_art(wad)
    atomic_write(path, text.encode("ascii"))
    info["path"] = path
    info["bytes"] = len(text)
    return info


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("wad", nargs="?", default=DEFAULT_WAD)
    ap.add_argument("out", nargs="?", default=DEFAULT_OUT)
    a = ap.parse_args(argv)
    info = write_doom_art(a.out, wadmod.Wad(a.wad))
    print("%s : %d o de texte ; tableaux %s ; 0 -> %d, 255 -> %d"
          % (info["path"], info["bytes"], info["arrays"], info["sub0"], info["sub255"]))
    for n, i in info["fonts"].items():
        print("  police %-8s h %2d, %2d glyphes, couleurs %d -> %d, %d o"
              % (n, i["height"], i["glyphs"], i["colors_in"], i["colors"], i["bytes"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

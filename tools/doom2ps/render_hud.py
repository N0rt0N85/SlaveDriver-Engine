#!/usr/bin/env python3
"""render_hud.py -- PC render of the Doom status bar as the engine draws it (SPEC_PLAYER 6, task 6).

Two images of the same player state, then a pixel comparison of the bar area:

  engine : build/doom/doom_art.h (what DOOM_HUD.C uploads: STBAR char, faces, keys, the three 4 bpp
           fonts through their CLUT) laid out with DOOM_HUD.C's positions -- x_e = x - 160,
           y_e = y - 88 around the (160, 112) origin of the 320x224 frame, glyphs at their top-left
           (drawChar), right-aligned numbers at fixed pitch (doomHudNum), '%' at the right edge.
  doom   : the WAD patches drawn like st_stuff.c / st_lib.c (V_DrawPatch at (x - lo, y - to)),
           PLAYPAL[0], bar at y = 168 of a 320x200 screen.

Comparison over the 320x32 bar: same set of drawn pixels (mask), exact colour on the 8 bpp art
(STBAR, face, keys) and nearest-CLUT colour on the fonts (quantised to 15 colours by wad2font).
The layout constants are a MODEL of DOOM_HUD.C (kept in sync by hand, see LAYOUT below); the C
itself is only exercised on the console.

Usage : python tools\\doom2ps\\render_hud.py [--wad W] [--art build/doom/doom_art.h]
          [--out-dir build/doom2ps] [--health 67] [--armor 12] [--ready 1] [--owned 7]
          [--keys 5] [--ammo 50,8,0,0] [--max 200,50,300,50] [--face -1]
        writes hud_engine.png (320x224, x2), hud_doom.png (320x200, x2), hud_diff.png (bar, x4:
        red = mask mismatch, yellow = colour mismatch) and prints the counts.  rc 1 on a mismatch.
"""
from __future__ import annotations

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
for p in (HERE, os.path.join(ROOT, "tools", "duke2ps")):
    if p not in sys.path:
        sys.path.insert(0, p)
import wad as wadmod                                   # noqa: E402
import rle8                                            # noqa: E402
import wad2font                                        # noqa: E402

try:
    from PIL import Image
except ImportError:                                    # pragma: no cover
    Image = None

DEFAULT_ART = os.path.join(ROOT, "build", "doom", "doom_art.h")
DEFAULT_OUT = os.path.join(ROOT, "build", "doom2ps")

# DOOM_HUD.C layout (Doom st_stuff.c coordinates; the engine subtracts (160, 88))
AMMO_Y = (173, 179, 191, 185)                          # am_clip, am_shell, am_cell, am_misl
AM_OF_WEAPON = (5, 0, 1, 0, 3, 2, 2, 5, 1)             # d_items.c weaponinfo[].ammo (5 = am_noammo)
FACE_ORG = (148, 169)                                  # DOOM_FACE_ORG_X/Y


# ----------------------------------------------------------------------------- doom_art.h
def parse_art(path):
    text = open(path, encoding="ascii").read()
    arrays = {}
    for m in re.finditer(r"unsigned char (\w+)((?:\[\d+\])+) __attribute__\(\(aligned\(4\)\)\) = \{(.*?)\n\};",
                         text, re.S):
        vals = [int(v) for v in re.findall(r"\d+", m.group(3))]
        dims = [int(d) for d in re.findall(r"\d+", m.group(2))]
        n = 1
        for d in dims:
            n *= d
        assert len(vals) == n, "%s : %d valeurs pour %r" % (m.group(1), len(vals), dims)
        arrays[m.group(1)] = (dims, bytes(vals))
    defines = {k: int(v) for k, v in re.findall(r"#define (DOOM_\w+) (\d+)\b", text)}
    return arrays, defines


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.rgb = [(0, 0, 0)] * (w * h)
        self.drawn = [None] * (w * h)                  # tag of the last layer that drew

    def put(self, x, y, rgb, tag):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.rgb[y * self.w + x] = rgb
            self.drawn[y * self.w + x] = tag

    def image(self, scale):
        im = Image.new("RGB", (self.w, self.h))
        im.putdata(self.rgb)
        return im.resize((self.w * scale, self.h * scale), Image.NEAREST)


def bgr555_rgb(c):
    return ((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31)


# ----------------------------------------------------------------------------- engine model
class EngineHud:
    def __init__(self, arrays, pal):
        self.a = arrays
        self.pal = pal
        self.fonts = {}
        for n, name in ((1, "doom_font_stcfn"), (2, "doom_font_sttnum"), (3, "doom_font_stysnum")):
            h, clut, widths, glyphs = wad2font.parse_font(arrays[name][1])
            self.fonts[n] = (h, clut, widths, glyphs)
        self.c = Canvas(320, 224)

    def char8(self, data, w, h, x, y, tag):          # COLOR_4 char, index 0 transparent
        for j in range(h):
            for i in range(w):
                v = data[j * w + i]
                if v:
                    self.c.put(x + i, y + j, self.pal[v], tag)

    def draw_char(self, x, y, font, code):          # PRINT.C drawChar: top-left, CLUT colours
        h, clut, widths, glyphs = self.fonts[font]
        if not widths[code]:
            return
        for j, row in enumerate(glyphs[code]):
            for i, q in enumerate(row):
                if q:
                    self.c.put(x + i, y + j, bgr555_rgb(clut[q]), "font%d" % font)

    def at(self, x, y):                               # st_stuff.c coordinates -> frame pixels
        return x - 160 + 160, y - 88 + 112

    def num(self, xr, y, font, pitch, n, digits):
        x, y = self.at(xr, y)
        n = max(0, n)
        if not n:
            self.draw_char(x - pitch, y, font, ord("0"))
            return
        while n and digits:
            digits -= 1
            x -= pitch
            self.draw_char(x, y, font, ord("0") + n % 10)
            n //= 10

    def percent(self, xr, y, n):
        self.draw_char(*self.at(xr, y), 2, ord("%"))
        self.num(xr, y, 2, 14, n, 3)

    def draw(self, st):
        stbar = self.a["doom_stbar"][1]
        self.char8(stbar[8:], 320, 32, *self.at(0, 168), "stbar")
        am = AM_OF_WEAPON[st["ready"]]
        if am < 4:
            self.num(44, 171, 2, 14, st["ammo"][am], 3)
        self.percent(90, 171, st["health"])
        for i in range(6):
            code = ord("2") + i
            if not st["owned"] & (1 << (i + 1)):
                code |= wad2font.GRAY_DIGIT
            self.draw_char(*self.at(111 + 12 * (i % 3), 172 + 10 * (i // 3)), 3, code)
        faces = self.a["doom_faces"][1]
        self.char8(faces[st["face"] * 768:(st["face"] + 1) * 768], 24, 32, *self.at(*FACE_ORG), "face")
        self.percent(221, 171, st["armor"])
        keys = self.a["doom_keys"][1]
        for i in range(3):
            if st["keys"] & ((1 << i) | (1 << (i + 3))):
                self.char8(keys[i * 64:(i + 1) * 64], 8, 8, *self.at(239, 171 + 10 * i), "keys")
        for i in range(4):
            self.num(288, AMMO_Y[i], 3, 4, st["ammo"][i], 3)
            self.num(314, AMMO_Y[i], 3, 4, st["max"][i], 3)


# ----------------------------------------------------------------------------- Doom reference
class DoomHud:
    def __init__(self, wad, playpal):
        self.wad = wad
        self.pal = playpal
        self.c = Canvas(320, 200)

    def patch(self, x, y, lump, tag):                 # V_DrawPatch
        p = rle8.read_patch(self.wad, lump)
        for j in range(p.h):
            for i in range(p.w):
                if p.msk[j * p.w + i]:
                    self.c.put(x - p.lo + i, y - p.to + j, tuple(self.pal[p.pix[j * p.w + i]]), tag)

    def num(self, x, y, prefix, n, digits, tag):      # STlib_drawNum
        w = rle8.read_patch(self.wad, prefix + "0").w
        n = max(0, n)
        if not n:
            self.patch(x - w, y, prefix + "0", tag)
            return
        while n and digits:
            digits -= 1
            x -= w
            self.patch(x, y, prefix + str(n % 10), tag)
            n //= 10

    def draw(self, st):
        self.patch(0, 168, "STBAR", "stbar")
        self.patch(104, 168, "STARMS", "stbar")
        am = AM_OF_WEAPON[st["ready"]]
        if am < 4:
            self.num(44, 171, "STTNUM", st["ammo"][am], 3, "font2")
        self.num(90, 171, "STTNUM", st["health"], 3, "font2")
        self.patch(90, 171, "STTPRCNT", "font2")
        for i in range(6):
            lump = ("STYSNUM%d" if st["owned"] & (1 << (i + 1)) else "STGNUM%d") % (i + 2)
            self.patch(111 + 12 * (i % 3), 172 + 10 * (i // 3), lump, "font3")
        self.patch(143, 168, FACE_LUMPS[st["face"]], "face")
        self.num(221, 171, "STTNUM", st["armor"], 3, "font2")
        self.patch(221, 171, "STTPRCNT", "font2")
        for i in range(3):
            if st["keys"] & ((1 << i) | (1 << (i + 3))):
                self.patch(239, 171 + 10 * i, "STKEYS%d" % i, "keys")
        for i in range(4):
            self.num(288, AMMO_Y[i], "STYSNUM", st["ammo"][i], 3, "font3")
            self.num(314, AMMO_Y[i], "STYSNUM", st["max"][i], 3, "font3")


FACE_LUMPS = (["STFST%d%d" % (p, i) for p in range(5) for i in range(3)]
              + ["STFKILL%d" % i for i in range(5)] + ["STFEVL%d" % i for i in range(5)] + ["STFDEAD0"])


def compare(eng, ref, pal_rgb_of_clut):
    """Bar rows: engine 192..223 vs Doom 168..199.  -> (mask, colour, drawn, diff canvas)."""
    diff = Canvas(320, 32)
    mask_bad = col_bad = drawn = 0
    for y in range(32):
        for x in range(320):
            e = eng.c.drawn[(192 + y) * 320 + x]
            d = ref.c.drawn[(168 + y) * 320 + x]
            ergb = eng.c.rgb[(192 + y) * 320 + x]
            drgb = ref.c.rgb[(168 + y) * 320 + x]
            diff.put(x, y, tuple(v // 3 for v in drgb), "")
            if d is None and e is None:
                continue
            drawn += 1
            if (d is None) != (e is None):
                mask_bad += 1
                diff.put(x, y, (255, 0, 0), "")
                continue
            if d.startswith("font"):
                ok = ergb == pal_rgb_of_clut(d, drgb)
            else:
                ok = ergb == drgb
            if not ok:
                col_bad += 1
                diff.put(x, y, (255, 255, 0), "")
    return mask_bad, col_bad, drawn, diff


def save_png(im, path):
    tmp = path + ".tmp"
    im.save(tmp, format="PNG")
    os.replace(tmp, path)


def ints(s):
    return [int(v) for v in s.split(",")]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=rle8.DEFAULT_WAD)
    ap.add_argument("--art", default=DEFAULT_ART)
    ap.add_argument("--out-dir", default=DEFAULT_OUT)
    ap.add_argument("--health", type=int, default=67)
    ap.add_argument("--armor", type=int, default=12)
    ap.add_argument("--ready", type=int, default=1, help="wp_* (1 = pistol)")
    ap.add_argument("--owned", type=int, default=7, help="weaponOwned bits (7 = fist+pistol+shotgun)")
    ap.add_argument("--keys", type=int, default=5, help="keys bits (5 = blue + red card)")
    ap.add_argument("--ammo", type=ints, default=[50, 8, 0, 0])
    ap.add_argument("--max", type=ints, default=[200, 50, 300, 50])
    ap.add_argument("--face", type=int, default=-1, help="doom_faces index (-1: straight, pain step of --health)")
    a = ap.parse_args(argv)
    if Image is None:
        print("PIL absent : pip install pillow")
        return 2
    h = max(0, min(100, a.health))
    st = dict(health=a.health, armor=a.armor, ready=a.ready, owned=a.owned, keys=a.keys,
              ammo=a.ammo, max=a.max,
              face=a.face if a.face >= 0 else ((100 - h) * 5 // 101) * 3)
    wad = wadmod.Wad(a.wad)
    playpal = wad.playpal(0)
    pal_obj, _ = rle8.object_palette(playpal)
    arrays, _defs = parse_art(a.art)
    pal8 = [tuple(playpal[i]) for i in range(256)]
    eng = EngineHud(arrays, pal8)
    eng.draw(st)
    ref = DoomHud(wad, playpal)
    ref.draw(st)

    # a font pixel is right if it is the CLUT colour wad2font gives that PLAYPAL colour
    clut_rgb = {}
    for n, name in ((2, "sttnum"), (3, "stysnum"), (1, "stcfn")):
        glyphs = wad2font.font_glyphs(name)
        pats = [wad2font.read_glyph(wad, l) for l in glyphs.values()]
        counts = {}
        for p in pats:
            for i in range(p.w * p.h):
                if p.msk[i]:
                    counts[p.pix[i]] = counts.get(p.pix[i], 0) + 1
        from collections import Counter
        kept, remap = wad2font.quantize_indices(Counter(counts), playpal)
        m = {}
        for idx in counts:
            m[tuple(playpal[idx])] = bgr555_rgb(wad2font.bgr555(playpal[remap[idx]]))
        clut_rgb["font%d" % n] = m

    def pal_rgb_of_clut(tag, drgb):
        return clut_rgb[tag].get(tuple(drgb))

    mask_bad, col_bad, drawn, diff = compare(eng, ref, pal_rgb_of_clut)
    os.makedirs(a.out_dir, exist_ok=True)
    save_png(eng.c.image(2), os.path.join(a.out_dir, "hud_engine.png"))
    save_png(ref.c.image(2), os.path.join(a.out_dir, "hud_doom.png"))
    save_png(diff.image(4), os.path.join(a.out_dir, "hud_diff.png"))
    print("etat %s" % st)
    print("barre 320x32 : %d px dessines, masque different %d, couleur differente %d"
          % (drawn, mask_bad, col_bad))
    print("PNG : %s" % os.path.join(a.out_dir, "hud_{engine,doom,diff}.png"))
    print("OK" if not (mask_bad or col_bad) else "ECART")
    return 0 if not (mask_bad or col_bad) else 1


if __name__ == "__main__":
    sys.exit(main())

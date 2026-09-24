#!/usr/bin/env python3
"""wad2pause.py -- l'art de la pause Doom en tableaux C : $(BUILD)/pause_art.h (PAUSE.OVL).

Lu par game/doom/ovl/PAUSE.C, compile DANS l'overlay (jamais dans MAIN : 0 octet resident). La pause
dessine au CPU dans une bitmap 8 bpp de la VDP2 (NBG0, banque CRAM 0 = PLAYPAL) : les pixels sont
des indices PLAYPAL bruts, 0 = transparent. Tout vient du WAD SHAREWARE : jamais d'art retail.

  PAUSE_SKULL_W, PAUSE_SKULL_H     M_SKULL1/2 : 20 x 19 (le curseur de Doom, clignote tous les 8 tics)
  pauseSkull[2][H*W]               indices PLAYPAL, 0 = hors masque ; un 0 (ou un 255) OPAQUE du patch
                                   devient l'indice le plus proche (rle8.object_palette : la banque 0
                                   a 0 = transparent et 255 = 0xffff)

Les lettres ne sont pas ici : la pause reprend STCFN, deja resident (doom_art.h, police du HUD), et le
double a la volee a la forme de la grande police du titre (x2 en y, x1,5 en x : wad2title.py).

Usage : python tools\\doom2ps\\wad2pause.py [WAD] [OUT]   (regle du Makefile : `wad2pause.py
        $(DOOMWAD) $@`). Ecriture atomique (temp + os.replace).
"""
from __future__ import annotations

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
for p in (HERE, os.path.join(ROOT, "tools", "duke2ps")):
    if p not in sys.path:
        sys.path.insert(0, p)
import wad as wadmod                                   # noqa: E402
import rle8                                            # noqa: E402
from lev_write import atomic_write                     # noqa: E402

DEFAULT_WAD = rle8.DEFAULT_WAD
DEFAULT_OUT = os.path.join(ROOT, "build", "ndebug", "stext", "doom", "pause_art.h")
SKULLS = ("M_SKULL1", "M_SKULL2")
# Le ramassable de chaque arme, dans l'ordre de doom_weapontype_t (DOOM.H wp_*). None = pas d'arme
# a montrer (le poing) ; un lump absent du WAD sort en NULL et la page ARMES ne montre rien.
WEAPON_PICS = (None,        # wp_fist
               "PISTA0",    # wp_pistol       (absent du shareware)
               "SHOTA0",    # wp_shotgun
               "MGUNA0",    # wp_chaingun
               "LAUNA0",    # wp_missile
               "PLASA0",    # wp_plasma       (absent du shareware)
               "BFUGA0",    # wp_bfg          (absent du shareware)
               "CSAWA0",    # wp_chainsaw
               "SGN2A0")    # wp_supershotgun (Doom 2)


def patch_indices(wad, lump, remap):
    """-> (w, h, octets) : indices PLAYPAL, 0 hors masque, 0/255 opaques remappes."""
    p = rle8.read_patch(wad, lump)
    out = bytearray(p.w * p.h)
    for i in range(p.w * p.h):
        if p.msk[i]:
            out[i] = remap(p.pix[i])
    return p.w, p.h, bytes(out)


def build(wad):
    _, remap = rle8.object_palette(wad.playpal(0))
    skulls = [patch_indices(wad, n, remap) for n in SKULLS]
    w, h = skulls[0][:2]
    assert all(s[:2] == (w, h) for s in skulls), [s[:2] for s in skulls]
    pics, absents = [], []
    for name in WEAPON_PICS:
        try:
            pics.append(None if name is None else patch_indices(wad, name, remap))
        except (KeyError, AssertionError, ValueError):
            pics.append(None)
            absents.append(name)
    lines = ["/* pause_art.h -- genere par tools/doom2ps/wad2pause.py depuis le WAD : ne pas editer. */",
             "#ifndef PAUSE_ART_H", "#define PAUSE_ART_H",
             "#define PAUSE_SKULL_W %d" % w, "#define PAUSE_SKULL_H %d" % h,
             "static const unsigned char pauseSkull[2][%d]={" % (w * h)]
    for _w, _h, px in skulls:
        lines.append("{")
        for y in range(h):
            lines.append(",".join(str(b) for b in px[y * w:(y + 1) * w]) + ",")
        lines.append("},")
    lines.append("};")
    # les ramassables : un tableau plat par arme, plus largeur / hauteur / pointeur
    octets = 2 * w * h
    lines.append("#define PAUSE_PIC_NM %d" % len(pics))
    for i, p in enumerate(pics):
        if p is None:
            continue
        pw, ph, px = p
        octets += pw * ph
        lines.append("static const unsigned char pausePic%d[%d]={" % (i, pw * ph))
        for y in range(ph):
            lines.append(",".join(str(b) for b in px[y * pw:(y + 1) * pw]) + ",")
        lines.append("};")
    lines.append("static const unsigned char *const pausePic[PAUSE_PIC_NM]={%s};"
                 % ",".join("NULL" if p is None else "pausePic%d" % i
                            for i, p in enumerate(pics)))
    lines.append("static const short pausePicW[PAUSE_PIC_NM]={%s};"
                 % ",".join("0" if p is None else str(p[0]) for p in pics))
    lines.append("static const short pausePicH[PAUSE_PIC_NM]={%s};"
                 % ",".join("0" if p is None else str(p[1]) for p in pics))
    lines += ["#endif", ""]
    return "\n".join(lines), dict(w=w, h=h, bytes=octets,
                                  pics=sum(1 for p in pics if p), absents=absents)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("wad", nargs="?", default=DEFAULT_WAD)
    ap.add_argument("out", nargs="?", default=DEFAULT_OUT)
    a = ap.parse_args(argv)
    text, info = build(wadmod.Wad(a.wad))
    atomic_write(a.out, text.encode("ascii"))
    print("%s : crane %dx%d, %d ramassable(s), %d o de tableaux%s"
          % (a.out, info["w"], info["h"], info["pics"], info["bytes"],
             (" (absents du WAD : %s)" % " ".join(info["absents"])) if info["absents"] else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

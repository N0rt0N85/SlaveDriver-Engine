#!/usr/bin/env python3
"""doomtiles.py -- etape E4 doom2ps : les VRAIES textures de Doom en tuiles SlaveDriver.

Cible MESUREE (NOTES_E4.md du convertisseur Duke) : une tuile de geometrie est de flags 0x32 et
contient **4 096 octets d'INDICES 8 bits** plus une palette de 256 entrees BGR555 avec le bit 15
pose, SAUF l'entree 0 qui vaut 0x0000 et sert de transparence (`load16BPPTile`, PIC.C:500-531).

Doom tombe presque a l'identite la-dessus :
  - un FLAT fait 64x64 en indices 8 bits = exactement une tuile, octet pour octet ;
  - la palette PLAYPAL fait 256 entrees RGB 8 bits -> BGR555 par simple decalage ;
  - une texture de mur est composite (TEXTURE1 + PNAMES + patches) : on la compose, puis on
    l'echantillonne dans 64x64 AVEC BOUCLAGE, ce qui gere d'un coup les textures plus petites
    que la cellule (elles se repetent) et plus grandes (elles se sous-echantillonnent).

Le seul vrai piege est l'INDICE 0 : chez Doom c'est un noir opaque tres utilise, chez SlaveDriver
c'est la transparence. On le deplace donc vers l'entree non nulle la plus proche du noir, dans les
pixels comme dans la palette -- sinon toutes les zones noires des murs deviennent des trous.

Usage : python tools\\doom2ps\\doomtiles.py [--wad W] [--geom J] [--out J]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import wad as wadmod                                   # noqa: E402

CELL = 64


def bgr555(rgb):
    r, g, b = rgb
    return 0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


class TileMaker:
    def __init__(self, wad):
        self.w = wad
        self.pal = wad.playpal(0)
        # entree de substitution pour l'indice 0 : la plus proche du noir parmi 1..255
        self.sub0 = min(range(1, 256),
                        key=lambda i: sum(c * c for c in self.pal[i]))
        self.flats = wad.flats()
        self.texdefs = wad.textures()
        self.pnames = wad.pnames()
        self._patch = {}
        self._tex = {}

    def palette(self):
        out = [0x0000]                                  # 0 = transparence (PIC.C:500-531)
        for i in range(1, 256):
            out.append(bgr555(self.pal[i]))
        return out

    def fix(self, v):
        return self.sub0 if v == 0 else v

    def patch(self, idx):
        nm = self.pnames[idx]
        if nm not in self._patch:
            if not self.w.has(nm):
                self._patch[nm] = (1, 1, bytes([0]), bytes([0]))
            else:
                self._patch[nm] = self.w.patch(nm)
        return self._patch[nm]

    def texture(self, name):
        """Compose une texture de mur -> (w, h, bytes des indices)."""
        if name in self._tex:
            return self._tex[name]
        td = self.texdefs.get(name)
        if td is None:                                  # nom inconnu : damier sombre
            w = h = 64
            px = bytearray(w * h)
            for y in range(h):
                for x in range(w):
                    px[y * w + x] = self.sub0 if ((x >> 3) ^ (y >> 3)) & 1 else 4
            self._tex[name] = (w, h, bytes(px))
            return self._tex[name]
        w, h = td["width"], td["height"]
        px = bytearray(w * h)
        for (ox, oy, pi) in td["patches"]:
            pw, ph, ppx, pmk = self.patch(pi)
            for y in range(ph):
                ty = oy + y
                if not (0 <= ty < h):
                    continue
                row = y * pw
                trow = ty * w
                for x in range(pw):
                    if not pmk[row + x]:
                        continue
                    tx = ox + x
                    if 0 <= tx < w:
                        px[trow + tx] = ppx[row + x]
        self._tex[name] = (w, h, bytes(px))
        return self._tex[name]

    def tile_from_wall(self, name, cu, cv, voff=0):
        """64x64 indices : la texture echantillonnee sur UNE cellule de cu x cv unites monde.
        Le bouclage (`% w`, `% h`) rend la formule correcte dans les deux sens : une texture plus
        petite que la cellule se repete, une plus grande se sous-echantillonne.

        `cv` est la hauteur REELLE de la cellule posee dans le mur, pas la hauteur de la texture :
        c'est ce qui met l'echelle verticale d'aplomb (voir `wall_tex`, E4.1c). `voff` est la
        ligne de texture qui tombe en haut de la cellule -- le calage de Doom (r_segs.c)."""
        w, h, px = self.texture(name)
        out = bytearray(CELL * CELL)
        for y in range(CELL):
            sy = (int(y * cv / CELL) + int(voff)) % h
            row = sy * w
            orow = y * CELL
            for x in range(CELL):
                sx = int(x * cu / CELL) % w
                out[orow + x] = self.fix(px[row + sx])
        return bytes(out)

    def tile_from_flat(self, name):
        d = self.flats.get(name)
        if d is None:
            return bytes([self.sub0]) * (CELL * CELL)
        return bytes(self.fix(v) for v in d)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=os.path.join(os.path.dirname(ROOT), "Mimas", "cd", "data",
                                                  "DOOM1.WAD"))
    ap.add_argument("--geom", default=os.path.join(ROOT, "build", "doom2ps", "e1m1_geom3d.json"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "doom2ps", "e1m1_tiles.json"))
    a = ap.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")

    G = json.load(open(a.geom, encoding="utf-8"))
    W = wadmod.Wad(a.wad)
    tm = TileMaker(W)
    sizes = {nm: (t["width"], t["height"]) for nm, t in W.textures().items()}

    # la taille de cellule doit etre EXACTEMENT celle que doom3d a mise dans le mur (E4.1b)
    from doom3d import CELL_MIN_U, CELL_MAX_U, CELL_MIN_V, CELL_MAX_V

    tiles = []
    detail = []
    manquantes = []
    for key in G["tiles"]:
        pic = key[0]
        kind, name = G["picnames"][pic]
        if kind == "flat":
            if name not in tm.flats:
                manquantes.append(("flat", name))
            tiles.append(tm.tile_from_flat(name))
            detail.append(dict(pic=pic, kind=kind, name=name, w=64, h=64, cu=64, cv=64))
        else:
            w, h = sizes.get(name, (64, 128))
            if name not in sizes:
                manquantes.append(("tex", name))
            cu = max(CELL_MIN_U, min(CELL_MAX_U, int(w)))
            if len(key) >= 7:                     # cle E4.1c : (pic,cx,cy,ncx,ncy,cv,voff)
                cv, voff = key[5], int(key[6])
            else:
                cv = max(CELL_MIN_V, min(CELL_MAX_V, int(h)))
                voff = 0
            tiles.append(tm.tile_from_wall(name, cu, cv, voff))
            detail.append(dict(pic=pic, kind=kind, name=name, w=w, h=h,
                               cu=cu, cv=cv, voff=voff))

    exact = sum(1 for d in detail if abs(d["cu"] - d["w"]) < 1)
    out = dict(format="doom2ps/e4-tiles v1",
               source=dict(wad=os.path.basename(a.wad), map=G["source"]["map"]),
               substitut_indice0=tm.sub0,
               palette=tm.palette(),
               detail=detail,
               tiles=[t.hex() for t in tiles])
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    tmp = a.out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(out, f)
    os.replace(tmp, a.out)

    print(f"E4 : {len(tiles)} tuiles 64x64 ({len(tiles) * 4096:,} o), "
          f"indice 0 -> {tm.sub0} (RGB {tm.pal[tm.sub0]})")
    print(f"  {exact}/{len(detail)} a l'echelle exacte (cellule = taille de la texture)")
    for d in detail:
        if d["kind"] == "tex" and (d["cu"] != d["w"] or d["cv"] != d["h"]):
            print(f"    {d['name']:9s} {d['w']}x{d['h']} -> cellule {d['cu']}x{d['cv']}")
    if manquantes:
        print(f"  {len(manquantes)} noms absents du WAD : {manquantes[:6]}")
    print(f"  -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

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
sys.path.append(os.path.join(ROOT, "tools"))           # tuiles.py (reduire), sans rien masquer
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
        self.sub0 = min(range(1, 255),
                        key=lambda i: sum(c * c for c in self.pal[i]))
        # entree 255 : `loadPalletes` force `objectPal[255] = 0xffff` EN MEMOIRE (PIC.C:631) avant
        # que load16BPPTile ne convertisse les indices (PIC.C:517-527) -- la palette 0 est aussi la
        # palette objet (make_e1m1 : objectPalette = 0), donc un pixel 255 de geometrie deviendrait
        # blanc. On le deplace vers l'entree 1..254 la plus proche, comme rle8.object_palette.
        r, g, b = self.pal[255]
        self.sub255 = min(range(1, 255), key=lambda i: (self.pal[i][0] - r) ** 2
                          + (self.pal[i][1] - g) ** 2 + (self.pal[i][2] - b) ** 2)
        self.flats = wad.flats()
        self.texdefs = wad.textures()
        self.pnames = wad.pnames()
        self._patch = {}
        self._tex = {}

    def palette(self):
        out = [0x0000]                                  # 0 = transparence (PIC.C:500-531)
        for i in range(1, 255):
            out.append(bgr555(self.pal[i]))
        out.append(0xFFFF)                              # 255 : ce que PIC.C:631 y met de toute facon
        return out

    def fix(self, v):
        if v == 0:
            return self.sub0
        if v == 255:
            return self.sub255
        return v

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
        if name == "ZZFUSION":
            # Peinture de DIAGNOSTIC (doom3d --diag-fusion). Le damier du repli ci-dessous est
            # sombre, donc invisible dans une piece sombre -- exactement la ou le defaut se voit.
            # Celui-ci prend les deux entrees les plus criardes de la palette DU WAD, calculees et
            # non devinees : la plus claire, et la plus proche du magenta (une couleur que Doom
            # n'utilise nulle part). Cases de 16 px : lisible meme de loin sur une capture.
            blanc = max(range(1, 255), key=lambda i: sum(self.pal[i]))
            mag = min(range(1, 255), key=lambda i: (self.pal[i][0] - 255) ** 2
                      + self.pal[i][1] ** 2 + (self.pal[i][2] - 255) ** 2)
            w = h = 64
            px = bytearray(w * h)
            for y in range(h):
                for x in range(w):
                    px[y * w + x] = mag if ((x >> 4) ^ (y >> 4)) & 1 else blanc
            self._tex[name] = (w, h, bytes(px))
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

    def tile_from_wall(self, name, cu, cv, voff=0, uoff=0):
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
                sx = int(x * cu / CELL + uoff) % w     # uoff : colonne au bord gauche (cle uwin)
                out[orow + x] = self.fix(px[row + sx])
        return bytes(out)

    def tile_from_flat(self, name):
        d = self.flats.get(name)
        if d is None:
            return bytes([self.sub0]) * (CELL * CELL)
        return bytes(self.fix(v) for v in d)

    def tile_of_key(self, key, picnames, sizes):
        """-> (64x64 indices, detail) pour une cle de tuile de doom3d. La taille de cellule doit
        etre EXACTEMENT celle que doom3d a mise dans le mur (E4.1b)."""
        from doom3d import CELL_MIN_U, CELL_MAX_U, CELL_MIN_V, CELL_MAX_V
        if "sw" in key:                       # marqueur d'interrupteur (doom3d.finalize_mobile)
            key = tuple(key[:list(key).index("sw")])
        pic = key[0]
        kind, name = picnames[pic]
        if kind == "flat":
            return (self.tile_from_flat(name),
                    dict(pic=pic, kind=kind, name=name, w=64, h=64, cu=64, cv=64))
        w, h = sizes.get(name, (64, 128))
        cu = max(CELL_MIN_U, min(CELL_MAX_U, int(w)))
        if len(key) >= 7:                     # cle E4.1c : (pic,cx,cy,ncx,ncy,cv,voff)
            cv, voff = key[5], int(key[6])
        else:
            cv = max(CELL_MIN_V, min(CELL_MAX_V, int(h)))
            voff = 0
        uoff = 0
        if len(key) >= 9:                     # + fenetre (cu, u0) : mur plus etroit que la texture
            cu, uoff = key[7], key[8]
        return (self.tile_from_wall(name, cu, cv, voff, uoff),
                dict(pic=pic, kind=kind, name=name, w=w, h=h, cu=cu, cv=cv, voff=voff, uoff=uoff))


def reduire(G, wad, budget, trace=print):
    """Ramene les tuiles de la geometrie G (sortie doom3d, modifiee EN PLACE) a `budget` avec
    tools/tuiles.py : fusion a l'interieur d'une texture, les plus semblables a l'image d'abord,
    ponderees par l'aire. Figees : tuiles d'interrupteur (cle 'sw', OFF et ON) et images des flats
    animes. Reecrit texture, faces, interrupteurs, animations et la liste des tuiles. -> info."""
    import numpy as np
    import tuiles
    tm = TileMaker(wad)
    sizes = {nm: (t["width"], t["height"]) for nm, t in wad.textures().items()}
    pal = np.array([tm.pal[i] for i in range(256)], dtype=np.float32)
    keys = G["tiles"]
    N = len(keys)
    images = np.empty((N, CELL // tuiles.VIGNETTE, CELL // tuiles.VIGNETTE, 3), dtype=np.float32)
    for i, k in enumerate(keys):
        px, _ = tm.tile_of_key(k, G["picnames"], sizes)
        images[i] = tuiles.vignette(pal[np.frombuffer(px, dtype=np.uint8)].reshape(CELL, CELL, 3))
    poids = tuiles.aires(G["walls"], G["vertices"], G["faces"], G["texture"], N)
    groupes = [tuple(G["picnames"][k[0]]) for k in keys]
    mobile = G.get("mobile") or {}
    figees = {i for i, k in enumerate(keys) if "sw" in k}
    figees |= {s[c] for s in mobile.get("switches") or [] for c in ("tile_off", "tile_on")}
    figees |= {t for fam in G.get("anims") or [] for t in fam}
    repr_, info = tuiles.reduire(images, poids, groupes, figees, budget, trace=trace)
    if info["garde"] == N:
        return info
    garder, remap = tuiles.compacter(repr_)
    for i in range(1, len(G["texture"]), 2):
        G["texture"][i] = remap[G["texture"][i]]
    for f in G["faces"]:
        f["tile"] = remap[f["tile"]]
    for s in mobile.get("switches") or []:
        s["tile_off"], s["tile_on"] = remap[s["tile_off"]], remap[s["tile_on"]]
    G["anims"] = [[remap[t] for t in fam] for fam in G.get("anims") or []]
    G["tiles"] = [keys[i] for i in garder]
    # ce que chaque tuile gardee remplace : verif_doom (echelle verticale) distingue ainsi une
    # cellule qui montre la tuile d'une voisine PAR BUDGET d'une cellule fausse
    rep = [[] for _ in garder]
    for i in range(N):
        rep[remap[i]].append(keys[i])
    G["tuiles_representees"] = rep
    G["reduction_tuiles"] = dict(avant=N, apres=len(garder), budget=int(budget),
                                 ecart_rms=round(info["ecart_rms"], 2))
    info["par_texture"] = {}
    for i in range(N):
        if repr_[i] != i:
            nm = G["picnames"][keys[i][0]][1]
            info["par_texture"][nm] = info["par_texture"].get(nm, 0) + 1
    info["pires"] = [(round(c), g[1], keys[t][5:] if len(keys[t]) > 5 else (),
                      keys[r][5:] if len(keys[r]) > 5 else ()) for c, g, t, r in info["pires"]]
    return info


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

    tiles = []
    detail = []
    manquantes = []
    for key in G["tiles"]:
        kind, name = G["picnames"][key[0]]
        if (kind == "flat" and name not in tm.flats) or (kind != "flat" and name not in sizes):
            manquantes.append((kind, name))
        px, d = tm.tile_of_key(key, G["picnames"], sizes)
        tiles.append(px)
        detail.append(d)

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
          f"indice 0 -> {tm.sub0} (RGB {tm.pal[tm.sub0]}), 255 -> {tm.sub255}")
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

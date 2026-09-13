#!/usr/bin/env python3
"""duketiles.py -- etape E4 : les vraies textures de Duke Nukem 3D en tuiles PowerSlave.

Lit `TILES0xx.ART` et `PALETTE.DAT` dans `DUKE3D.GRP` et fabrique, pour chaque picnum utilise par
la geometrie, une tuile 64x64 au format du moteur.

Format ART (Ken Silverman) :
    int32 artversion, numtiles, localtilestart, localtileend
    int16 tilesizx[n], int16 tilesizy[n], int32 picanm[n]      (n = fin - debut + 1)
    puis les pixels bruts, tuile par tuile, **en colonnes** : pixel(x, y) = data[x*sizy + y].

Format PALETTE.DAT : 768 octets R,G,B en 6 bits (0..63), puis les tables de lumiere.

Cible (MESURE sur les 24 .LEV retail) :
  - tuile de geometrie = flags 0x32 (64x64 | 16BPP | PALLETE), 4 096 octets d'INDICES 8 bits ;
  - palette = 256 entrees 16 bits Saturn BGR555 avec le bit 15 pose, SAUF l'entree 0 qui vaut
    0x0000 : l'index 0 est la transparence (verifie sur les 23 palettes de KILENTRY) ;
  - la transparence de Duke est l'index **255** (CONVERT.C:837-840, 1271-1277) : on echange donc
    0 et 255, dans les pixels ET dans la palette.

Usage : python tools\\duke2ps\\duketiles.py [--grp F] [--geom J] [--out J]
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import grp as grpmod

GRP_DEFAULT = os.path.join(ROOT, "refs", "build", "duke13", "DUKE3D.GRP")
GEOM_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_geom3d.json")
OUT_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_tiles.json")

TILE_SIZE = 64
TILE_FLAGS = 0x32          # 64x64 | 16BPP | PALLETE
DUKE_TRANSPARENT = 255     # index transparent cote Duke


def saturn_color(r6, g6, b6):
    """VGA 6 bits -> Saturn BGR555 avec bit 15 (opaque)."""
    r5, g5, b5 = r6 >> 1, g6 >> 1, b6 >> 1
    return 0x8000 | (b5 << 10) | (g5 << 5) | r5


def load_palette(g):
    """Palette de Duke -> 256 couleurs Saturn, index 0 = transparent (echange 0 <-> 255)."""
    raw = g.read("PALETTE.DAT")[:768]
    cols = [saturn_color(raw[3 * i], raw[3 * i + 1], raw[3 * i + 2]) for i in range(256)]
    cols[0], cols[DUKE_TRANSPARENT] = cols[DUKE_TRANSPARENT], cols[0]
    cols[0] = 0x0000                                   # l'index 0 EST la transparence
    return cols


def load_art(g):
    """{picnum: (sizx, sizy, memoryview des pixels)} pour tous les TILES0xx.ART du GRP."""
    out = {}
    for name in sorted(n for n in g.names() if n.upper().startswith("TILES")
                       and n.upper().endswith(".ART")):
        d = g.read(name)
        ver, numtiles, first, last = struct.unpack("<4i", d[:16])
        n = last - first + 1
        p = 16
        sx = struct.unpack("<%dh" % n, d[p:p + 2 * n]); p += 2 * n
        sy = struct.unpack("<%dh" % n, d[p:p + 2 * n]); p += 2 * n
        p += 4 * n                                     # picanm : inutile ici
        for i in range(n):
            sz = sx[i] * sy[i]
            if sz:
                out[first + i] = (sx[i], sy[i], d[p:p + sz])
            p += sz
    return out


def to_tile(entry, cx=0, cy=0, nx=1, ny=1):
    """La case (cx, cy) d'un decoupage nx x ny d'une tuile ART -> 64x64 octets d'index, en LIGNES,
    avec l'echange 0 <-> 255.

    E4.1 : nx/ny > 1 quand une repetition de la texture Build couvre plusieurs cellules de 64 u.
    Sans ce decoupage la texture entiere est ecrasee dans chaque cellule et se repete : c'est ce
    qui rendait illisible le nom du film sur la marquise du cinema d'E1L1.
    Reechantillonnage au plus proche voisin, comme pour la tuile entiere."""
    if entry is None:
        return bytes(TILE_SIZE * TILE_SIZE)            # tout transparent
    sx, sy, px = entry
    u0, u1 = sx * cx // nx, sx * (cx + 1) // nx        # fenetre en texels dans la tuile source
    v0, v1 = sy * cy // ny, sy * (cy + 1) // ny
    w, h = max(1, u1 - u0), max(1, v1 - v0)
    out = bytearray(TILE_SIZE * TILE_SIZE)
    for y in range(TILE_SIZE):
        ty = v0 + y * h // TILE_SIZE
        row = y * TILE_SIZE
        for x in range(TILE_SIZE):
            tx = u0 + x * w // TILE_SIZE
            v = px[tx * sy + ty]                       # ART : colonnes d'abord
            if v == 0:
                v = DUKE_TRANSPARENT
            elif v == DUKE_TRANSPARENT:
                v = 0
            out[row + x] = v
    return bytes(out)


def build(grp_path, keys):
    """`keys` : liste de cles [pic, cx, cy, nx, ny] produites par geom3d (E4.1). Un simple entier
    est accepte et vaut (pic, 0, 0, 1, 1), c'est-a-dire la texture entiere."""
    g = grpmod.GrpFile(grp_path)
    pal = load_palette(g)
    art = load_art(g)
    tiles, manquants = [], []
    for k in keys:
        pn, cx, cy, nx, ny = (k, 0, 0, 1, 1) if isinstance(k, int) else tuple(k)
        e = art.get(pn)
        if e is None:
            manquants.append(pn)
        tiles.append(to_tile(e, cx, cy, nx, ny))
    return pal, tiles, manquants, art


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--grp", default=GRP_DEFAULT)
    ap.add_argument("--geom", default=GEOM_DEFAULT)
    ap.add_argument("--out", default=OUT_DEFAULT)
    a = ap.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")

    G = json.load(open(a.geom, encoding="utf-8"))
    picnums = G["tiles"]
    pal, tiles, manquants, art = build(a.grp, picnums)
    npic = len({k if isinstance(k, int) else k[0] for k in picnums})
    ndec = sum(1 for k in picnums if not isinstance(k, int) and (k[3] > 1 or k[4] > 1))
    print(f"E4 : {len(art)} tuiles dans le GRP ; {len(picnums)} tuiles demandees par la geometrie "
          f"({npic} picnums, dont {ndec} sous-tuiles issues d'un decoupage E4.1)")
    if manquants:
        print(f"  {len(manquants)} picnums ABSENTS de l'ART (tuiles transparentes) : {manquants[:10]}")

    # controles
    opaques = sum(1 for t in tiles for b in t if b != 0)
    print(f"  {len(tiles)} tuiles 64x64 ; {opaques / max(1, len(tiles) * 4096) * 100:.1f} % "
          f"de pixels opaques ; palette : entree0 {pal[0]:#06x}, "
          f"{sum(1 for c in pal if c & 0x8000)}/256 entrees avec le bit 15")
    vides = [picnums[i] for i, t in enumerate(tiles) if not any(t)]
    if vides:
        print(f"  {len(vides)} tuiles entierement transparentes : {vides[:10]}")

    out = dict(format="duke2ps/e4-tiles v1",
               source=os.path.relpath(a.grp, ROOT).replace("\\", "/"),
               picnums=list(picnums), palette=pal,
               tiles=[t.hex() for t in tiles],
               manquants=manquants, vides=vides)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    tmp = a.out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(out, f)
    os.replace(tmp, a.out)
    print(f"  -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""budget_correct.py -- correction de la section Budget de height_rules.py (2026-09-11).

La verification contradictoire (workflow 2026-09-11, lentille budget) a REFUTE la section Budget :
  1. le slave reserve ses 1300 polys sur les cellules TOTALES de chaque mur (height*width), AVANT tout
     rejet ecran (WALLS.C:1374 `if (height*width+nmSlavePolys+50>MAXNMSLAVEPOLYS) return;`) ; chaque
     cellule de mur parallelogramme = 1 poly (WALLS.C:1494/1539), chaque face explicite = 1 poly
     (WALLS.C:1582).  Donc un ciel plus haut => murs plus hauts => plus de cellules => des murs entiers
     sautes silencieusement par le slave.  C'est un cout PAR IMAGE pilote par la hauteur du ciel, que la
     conclusion precedente niait.
  2. le modele d'octets ignorait les sols/plafonds et confondait les deux stockages (parallelogramme
     ~2 o/cellule sans sommets ; faces explicites 8 o/sommet + 10 o/cellule).

Ce script recalcule, sur le modele E1 (import), pour chaque mur : sa hauteur (Build, puis ciel plafonne),
ses cellules totales (c*r, la reservation du slave), et liste les murs qui depassent le budget du slave.
Il donne aussi une estimation d'octets corrigee (parallelogramme + sols/plafonds).

Usage : python tools\\duke2ps\\budget_correct.py [--cap N] [--map d13/E1L5]
"""
from __future__ import annotations
import argparse, json, math, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))      # tools\ (buildmap)
import height_rules as HR
from height_rules import model_from_import, model_from_build

CELL = 64.0      # MESURE : SLEVEL.H:126 TILESIZE 64, et sur les 66 881 murs des 24 .LEV retail
                 # longueur/tileLength = 64 (34 293 murs) et hauteur/tileHeight = 64 (40 991).
                 # CONVERT.C:1802/1808 : tileLength = (len + TILESIZE/2) / TILESIZE, minimum 1.
MAX_SLAVE = 1300          # MAXNMSLAVEPOLYS (WALLS.C:1336)
SLAVE_MARGIN = 50         # WALLS.C:1374
SLAVE_AVAIL = MAX_SLAVE - SLAVE_MARGIN   # 1250 cellules


def wall_cells(model, sky_cap):
    """Pour chaque mur, hauteur (y_bas, y_haut) et cellules totales c*r = reservation du slave.
    Le modele Sector stocke deja des Y Saturn (floor_lo = sol le plus bas, ceil_hi = plafond le plus
    haut) : y0 = floor_lo, y1 = ceil_hi = pleine hauteur du secteur (borne superieure du mur).
    sky_cap=None : hauteurs Build ; sinon plafonne les ciels."""
    rows = []
    for w in model.walls:
        s = model.sectors[w.sector]
        y0 = s.floor_lo
        y1 = s.ceil_hi
        if sky_cap is not None and s.sky:
            y1 = min(y1, sky_cap)
        if y1 <= y0:
            continue
        L = w.length
        c = max(1, int((L + CELL / 2) // CELL))            # CONVERT.C:1802-1806
        r = max(1, int(((y1 - y0) + CELL / 2) // CELL))    # CONVERT.C:1808-1812
        rows.append(dict(wall=w.id, sector=s.id, build_id=s.build_id, L=L, y0=y0, y1=y1,
                         cells=c * r, c=c, r=r, sky=s.sky))
    return rows


def report(model, sky_cap, label):
    rows = wall_cells(model, sky_cap)
    over = [r for r in rows if r['cells'] > SLAVE_AVAIL]
    total_cells = sum(r['cells'] for r in rows)
    print(f'\n== {label} : {len(rows)} murs, {total_cells} cellules totales, '
          f'{len(over)} mur(s) depassant le budget slave (>{SLAVE_AVAIL})')
    for r in sorted(over, key=lambda x: -x['cells'])[:12]:
        print(f"   mur {r['wall']:5d} sect {r['sector']:4d} (build {r['build_id']:4d}) : "
              f"L {r['L']:.0f} x H {r['y1']-r['y0']:.0f} = {r['cells']} cellules "
              f"({r['c']}x{r['r']}) {'ciel' if r['sky'] else ''}")
    return rows, over


def corrected_bytes(model, sky_cap):
    """Estimation d'octets corrigee (borne basse, parallelogramme) :
    mur : 48 o (sWallType) + 2 o/cellule (level_texture) + 1 o/lumiere par sommet de grille.
    sol/plafond : 2 o/cellule pavee (bornes par aire/CELL^2 * 1.5 pour les bords)."""
    rows = wall_cells(model, sky_cap)
    wall_bytes = sum(48 + 2 * r['cells'] + (r['c'] + 1) * (r['r'] + 1) for r in rows)
    flat_cells = 0
    for s in model.sectors:
        a = s.area
        n = math.ceil(a / (CELL * CELL))
        flat_cells += n + (0 if s.sky else n)     # sol (+ plafond si pas ciel)
    flat_bytes = 2 * flat_cells + 8 * 4 * len(model.sectors)  # 2 o/cellule + 4 sommets par secteur approx
    return dict(wall_bytes=wall_bytes, flat_cells=flat_cells, flat_bytes=flat_bytes,
                total=wall_bytes + flat_bytes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cap', type=float, default=None, help='plafond de ciel Y (u)')
    ap.add_argument('--map', default='import', help='corpus/carte ou "import" (e1l1_import.json)')
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')

    if args.map == 'import':
        model = model_from_import(json.load(open(
            r'C:\Users\pcico\Projects\SlaveDriver-Engine\build\duke2ps\e1l1_import.json', encoding='utf-8')))
    else:
        import buildmap
        SD = r'C:\Users\pcico\Projects\SlaveDriver-Engine'
        corpus, name = args.map.split('/') if '/' in args.map else ('d13', args.map)
        m = buildmap.load(os.path.join(SD, 'refs', 'extract', 'KILLATON', 'maps', corpus, name + '.MAP'))
        model = model_from_build(m)

    print(f'modele {model.name or args.map} : {len(model.sectors)} secteurs, {len(model.walls)} murs')
    report(model, None, 'hauteurs Build (ciel non plafonne)')
    report(model, args.cap if args.cap else 1024.0, f'ciel plafonne a {args.cap or 1024.0} u')
    for cap in (None, 1024.0, 768.0):
        b = corrected_bytes(model, cap)
        print(f'octets (parallelogramme, ciel {"Build" if cap is None else str(cap)+"u"}) : '
              f'murs {b["wall_bytes"]:,} + sols/plafonds {b["flat_bytes"]:,} = {b["total"]:,} '
              f'(limite {HR.LEV_MAX_BYTES:,})')


if __name__ == '__main__':
    main()

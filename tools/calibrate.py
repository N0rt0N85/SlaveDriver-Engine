"""calibrate.py -- calibration Build (.MAP DOS PowerSlave) -> Dex/.LEV (Saturn PowerSlave).

Reproduit integralement docs/BUILD2DEX_CALIBRATION.md :
  1. table des noms (DOS : tuiles 34xx de STUFF.DAT decodees en PNG ; Saturn : INITLOAD.DAT bloc
     LB_LEVELNAMES, LOCAL.C:35-38 getText / LOCAL.H:4) et appariement par nom + geometrie ;
  2. balayage exhaustif 24 .LEV x 33 .MAP x 8 orientations x echelles, translation par mode des
     differences, taux d'appariement des points, des segments et des murs sur droite ;
  3. loi des hauteurs (Saturn floorLevel = moyenne des sommets du "mur" sol, CONVERT.C:2040-2055 ;
     Build z = 16 z par unite xy, floorz multiples de 1024) ;
  4. structure des niveaux Saturn (convexite, portails INVISIBLE, symetrie nextSector, empilement
     vertical en XZ, pentes, murs par secteur) ;
  5. tuiles : picnums Build distincts par niveau vs tuiles Saturn distinctes.

Entrees : build\\tmp-b2d\\sat\\<LEV>.json (tools\\lev.py), build\\tmp-b2d\\dos\\LEV<n>.MAP (tools\\dos_stats.py),
          build\\tmp-b2d\\sat_stats.json, build\\tmp-b2d\\dos_stats.json, STUFF.DAT (noms DOS, option --names),
          refs\\extract\\PS\\INITLOAD.DAT (noms Saturn).
Sorties : build\\tmp-b2d\\calib_scan.json (tous les scores), build\\tmp-b2d\\calib_report.json, tables markdown sur stdout.

Usage : python tools\\calibrate.py [--scan] [--names] [--quick] [--sat NAME ...]
        --scan   : balayage exhaustif 24x33 (long, ~15 min) ; sinon relit calib_scan.json s'il existe
        --names  : rend les tuiles de noms DOS en PNG dans build\\tmp-b2d\\names\\
        --quick  : echelles reduites {1/4,1/8,1/16,1/32} et sous-echantillon plus petit
        --out DIR: repertoire des sorties calib_*.json (defaut build\tmp-b2d ; utile car `make` efface build\)
        --fill-doc : insere les tables imprimees dans docs/BUILD2DEX_CALIBRATION.md (balises <<X>> ou blocs <!-- @X -->)
"""
from __future__ import annotations
import glob
import json
import os
import re
import struct
import sys
import time
from collections import Counter, defaultdict

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buildmap  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TMP = os.path.join(ROOT, 'build', 'tmp-b2d')
SAT_DIR = os.path.join(TMP, 'sat')
DOS_DIR = os.path.join(TMP, 'dos')
PS_DIR = os.path.join(ROOT, 'refs', 'extract', 'PS')
STUFF = r'C:\Program Files\GOG Galaxy\Games\Powerslave\STUFF.DAT'

TILESIZE = 64          # SLEVEL.H:126 : une tuile 64 px = 64 unites monde Saturn
BUILD_Z_PER_XY = 16    # Build : 16 unites z par unite xy (engine.c : z>>4 partout)

# ---------------------------------------------------------------------------------------------
# Noms
# ---------------------------------------------------------------------------------------------
# levelGraph BIGMAP.C:43-81 : index -> fichier ; noms affiches = getText(LB_LEVELNAMES, i) (BIGMAP.C:355-359)
SAT_GRAPH = ['KARNAK', 'SANCTUAR', 'PASS', 'TOMB', 'SHRINE', 'MINES', 'SETPALAC', 'SETARENA', 'CAVERN', 'THOTH',
             'CHAOS', 'COLONY', 'SELPATH', 'KILENTRY', 'QUARRY', 'SELBUROW', 'MAGMA', 'PEAK', 'MARSH', 'SUNKEN',
             'SLAVCAMP', 'GORGE', 'TEST', 'KILMAAT1', 'KILMAAT2', 'KILMAAT3', 'KILMAAT4', 'KILMAAT5', 'KILMAAT6',
             'KILARENA', 'TOMBEND']

# Noms DOS transcrits a la main depuis les tuiles texte de mapNamePlaques (exhumed menu.cpp:582-604,
# entree i = niveau i+1, tuile texte = 3411,3414,...) rendues par --names (build\tmp-b2d\names\names_strip.png).
# Les entrees 10, 19, 20 sont des hieroglyphes (pas de texte latin).
DOS_NAMES = {1: 'Abu Simbel', 2: 'Dendur', 3: 'Kalabsh', 4: 'El Subua', 5: 'El Derr', 6: 'Abu Ghurab', 7: 'Philae',
             8: 'El Kab', 9: 'Aswan', 10: '(hieroglyphes)', 11: 'Qubbet el Hawa', 12: 'Abydos', 13: 'Edufu',
             14: 'West Bank', 15: 'Luxor', 16: 'Karnak', 17: 'Saqqara', 18: 'Mitrrahn', 19: '(hieroglyphes)',
             20: '(hieroglyphes)', 0: 'Training (exhumed.cpp:2754-2757)'}
DOS_NAME_TILES = [3411, 3414, 3417, 3420, 3423, 3426, 3429, 3432, 3435, 3418, 3438, 3441, 3444, 3447, 3450, 3453,
                  3456, 3459, 3419, 3421]      # menu.cpp:582-604, champ text.nTile, index 0..19 = LEV1..LEV20
DOS_PLAQUE_TILES = [3376, 3378, 3380, 3382, 3384, 3371, 3387, 3389, 3391, 3409, 3393, 3395, 3397, 3399, 3401, 3403,
                    3405, 3407, 3412, 3415]    # idem, tiles[0].nTile


def sat_names_from_initload():
    """LOCAL.C:23-33 loadLocalText : INITLOAD.DAT, blocs {int size ; data} par langue (EN d'abord) ;
    getText LOCAL.C:35-38 ; LB_LEVELNAMES = 2 (LOCAL.H:4). On repere la table par la chaine 'Karnak'."""
    p = os.path.join(PS_DIR, 'INITLOAD.DAT')
    if not os.path.exists(p):
        return {}
    d = open(p, 'rb').read()
    i = d.find(b'\0Karnak\0Karnak Sanctuary\0')
    if i < 0:
        return {}
    out = {}
    pos = i + 1
    for k in range(len(SAT_GRAPH)):
        j = d.index(b'\0', pos)
        s = d[pos:j].decode('latin-1')
        if s.startswith('The Symbol'):        # premier message du bloc suivant (LB_ITEMMESSAGE)
            break
        out[k] = s
        pos = j + 1
    return out


def render_dos_names(out_dir):
    """Rend les tuiles texte + plaques de mapNamePlaques (menu.cpp:582-604) depuis TILES0xx.ART de STUFF.DAT.
    ART v1 (jfbuild engine.c loadpics) : int32 version=1, numtiles, localtilestart, localtileend ;
    int16 sizx[n], sizy[n] ; int32 picanm[n] ; pixels colonne par colonne. PALETTE.DAT = 768 o VGA 6 bits."""
    import grp
    from PIL import Image
    g = grp.GrpFile(STUFF)
    pal = [v * 4 for v in g.read('PALETTE.DAT')[:768]]
    arts = []
    for e in g.entries:
        if e.name.upper().endswith('.ART'):
            art = g.read(e)
            ver, num, ls, le = struct.unpack_from('<iiii', art, 0)
            n = le - ls + 1
            sx = struct.unpack_from('<%dh' % n, art, 16)
            sy = struct.unpack_from('<%dh' % n, art, 16 + 2 * n)
            off = 16 + 8 * n
            offs = []
            for i in range(n):
                offs.append(off)
                off += sx[i] * sy[i]
            arts.append((ls, le, sx, sy, offs, art))

    def tile(t):
        for ls, le, sx, sy, offs, art in arts:
            if ls <= t <= le:
                i = t - ls
                w, h = sx[i], sy[i]
                im = Image.new('P', (max(w, 1), max(h, 1)), 255)
                im.putpalette(pal)
                if w and h:
                    px = im.load()
                    dat = art[offs[i]:offs[i] + w * h]
                    for x in range(w):
                        for y in range(h):
                            px[x, y] = dat[x * h + y]
                return im
        raise KeyError(t)

    os.makedirs(out_dir, exist_ok=True)
    S = 4
    sheet = Image.new('RGB', (900, 1100), (30, 30, 30))
    y = 0
    for i, (t, p) in enumerate(zip(DOS_NAME_TILES, DOS_PLAQUE_TILES)):
        im = tile(t).convert('RGB')
        im = im.resize((im.width * S, im.height * S), Image.NEAREST)
        pm = tile(p).convert('RGB')
        pm = pm.resize((pm.width * 2, pm.height * 2), Image.NEAREST)
        sheet.paste(im, (10, y))
        sheet.paste(pm, (500, y))
        y += max(im.height, pm.height) + 8
    path = os.path.join(out_dir, 'names_strip.png')
    sheet.save(path)
    return path


# ---------------------------------------------------------------------------------------------
# Chargement
# ---------------------------------------------------------------------------------------------
class Sat:
    """Niveau Saturn depuis build/tmp-b2d/sat/<NAME>.json (tools/lev.py). Unites monde, y = hauteur."""

    def __init__(self, name):
        self.name = name
        d = json.load(open(os.path.join(SAT_DIR, name + '.json')))
        self.V = np.array(d['vertices'], dtype=np.int64)        # (n,3) x,y,z
        self.walls = d['walls']
        self.sectors = d['sectors']
        self.faces = d['faces']
        self.objects = d['objects']
        XZ = self.V[:, [0, 2]]
        self.XZ_all = XZ
        uniq, inv = np.unique(XZ, axis=0, return_inverse=True)
        self.P = uniq.astype(np.float64)                        # points XZ dedoublonnes
        self.pid = inv.reshape(-1)                              # vertex index -> point id
        # murs verticaux : normal[1]==0 (CONVERT.C:2045 : sol si normal[1]>0 ; plafond <0)
        segs = []
        self.vert_walls = []
        for wi, w in enumerate(self.walls):
            if w['normal'][1] == 0:
                a, b = self.pid[w['v'][0]], self.pid[w['v'][1]]
                if a != b:
                    segs.append((a, b))
                    self.vert_walls.append(wi)
        self.segs = np.array(segs, dtype=np.int64) if segs else np.zeros((0, 2), np.int64)
        self.segset = set(frozenset(s) for s in segs)
        self.kind = np.array([2 if w['normal'][1] > 0 else 1 if w['normal'][1] < 0 else 0 for w in self.walls])
        self.flat = np.array([abs(w['normal'][1]) in (0, 65536) for w in self.walls])

    def sector_vertex_ids(self, si, side_only=False):
        s = self.sectors[si]
        ids = set()
        for wi in range(s['firstWall'], s['lastWall'] + 1):
            if side_only and abs(self.walls[wi]['normal'][1]) >= 32768:
                continue
            ids.update(self.walls[wi]['v'])
        return sorted(ids)

    def side_walls(self, si):
        """Murs lateraux = |normal.y| < 0.5 (les "murs" sol/plafond ont |ny| = 1, une rampe a |ny| > 0.5)."""
        s = self.sectors[si]
        return [wi for wi in range(s['firstWall'], s['lastWall'] + 1) if abs(self.walls[wi]['normal'][1]) < 32768]

    def sector_poly(self, si):
        """Empreinte XZ du secteur = enveloppe convexe des sommets des murs lateraux (les quads sol/plafond sont
        des rectangles englobants qui DEPASSENT l'empreinte : KARNAK secteur 20, couloir diagonal)."""
        ids = self.sector_vertex_ids(si, side_only=True) or self.sector_vertex_ids(si)
        if len(ids) < 3:
            return None
        pts = np.unique(self.V[ids][:, [0, 2]], axis=0).astype(np.float64)
        return convex_hull(pts) if len(pts) >= 3 else None

    def sector_is_convex_polyhedron(self, si, eps=2.0):
        """Tous les sommets des murs lateraux sont du cote interieur (normal.p + d >= -eps) de chaque plan de mur
        lateral (plan 16.16 CONVERT.C:1955-1990 ; normale vers l'interieur : TOMB mur 0, KARNAK mur 1027)."""
        ids = self.sector_vertex_ids(si, side_only=True)
        if not ids:
            return True
        P = self.V[ids].astype(np.float64)
        for wi in self.side_walls(si):
            w = self.walls[wi]
            n = np.array(w['normal'], dtype=np.float64) / 65536.0
            d = w['d'] / 65536.0
            if np.any(P @ n + d < -eps):
                return False
        return True

    def sector_is_flat_door(self, si):
        """Plafond aplati sur le sol (porte fermee par CONVERT.C:3608-3612) : plan du "mur" plafond <= plan du sol."""
        s = self.sectors[si]
        fy = cy = None
        for wi in range(s['firstWall'], s['lastWall'] + 1):
            w = self.walls[wi]
            if w['normal'][1] == 65536:
                fy = -w['d'] / 65536.0          # plan y = -d
            elif w['normal'][1] == -65536:
                cy = w['d'] / 65536.0           # plan -y + d = 0 -> y = d
        return fy is not None and cy is not None and cy <= fy


def convex_hull(pts):
    """Enveloppe convexe 2D (chaine monotone d'Andrew), sens trigonometrique."""
    P = sorted(map(tuple, pts.tolist()))
    if len(P) <= 2:
        return np.array(P)

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower = []
    for p in P:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper = []
    for p in reversed(P):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return np.array(lower[:-1] + upper[:-1], dtype=np.float64)


class Dos:
    """Carte Build depuis build/tmp-b2d/dos/LEV<n>.MAP (tools/buildmap.py). Build : y vers le sud, z vers le bas."""

    def __init__(self, n):
        self.n = n
        self.m = buildmap.load(os.path.join(DOS_DIR, 'LEV%d.MAP' % n))
        W = self.m.walls
        XY = np.array([[w.x, w.y] for w in W], dtype=np.int64)
        self.XY_all = XY
        uniq, inv = np.unique(XY, axis=0, return_inverse=True)
        self.P = uniq.astype(np.float64)
        self.pid = inv.reshape(-1)
        self.segs = np.array([(self.pid[i], self.pid[w.point2]) for i, w in enumerate(W)], dtype=np.int64)
        self.two_sided = np.array([w.nextsector >= 0 for w in W])


# ---------------------------------------------------------------------------------------------
# Transformations : Saturn (X,Z) = s * M @ (x,y) + o
# ---------------------------------------------------------------------------------------------
MAPS8 = {
    'X=+x,Z=+y': np.array([[1, 0], [0, 1]]), 'X=+x,Z=-y': np.array([[1, 0], [0, -1]]),
    'X=-x,Z=+y': np.array([[-1, 0], [0, 1]]), 'X=-x,Z=-y': np.array([[-1, 0], [0, -1]]),
    'X=+y,Z=+x': np.array([[0, 1], [1, 0]]), 'X=+y,Z=-x': np.array([[0, 1], [-1, 0]]),
    'X=-y,Z=+x': np.array([[0, -1], [1, 0]]), 'X=-y,Z=-x': np.array([[0, -1], [-1, 0]]),
}
SCALES_FULL = [2.0, 1.0, 0.5, 0.25, 0.125, 1 / 16, 1 / 32, 1 / 64]
SCALES_QUICK = [0.25, 0.125, 1 / 16, 1 / 32]


def mode_offset(Pb, Ps, q=8.0, nb=160, ns=3000, seed=1):
    """Translation la plus frequente entre un sous-ensemble de points Build transformes (Pb) et les points
    Saturn (Ps), quantifiee a q unites, puis affinee par la mediane des differences de la cellule gagnante."""
    rng = np.random.default_rng(seed)
    A = Pb[rng.choice(len(Pb), min(nb, len(Pb)), replace=False)]
    B = Ps[rng.choice(len(Ps), min(ns, len(Ps)), replace=False)]
    D = (B[None, :, :] - A[:, None, :]).reshape(-1, 2)
    K = np.floor(D / q + 0.5).astype(np.int64)
    key = (K[:, 0] + (1 << 20)) * (1 << 21) + (K[:, 1] + (1 << 20))
    vals, cnt = np.unique(key, return_counts=True)
    j = int(np.argmax(cnt))
    kx = vals[j] // (1 << 21) - (1 << 20)
    ky = vals[j] % (1 << 21) - (1 << 20)
    sel = (np.abs(K[:, 0] - kx) <= 1) & (np.abs(K[:, 1] - ky) <= 1)
    off = np.median(D[sel], axis=0) if sel.any() else np.array([kx * q, ky * q], dtype=np.float64)
    return off, int(cnt[j]) / len(A)


def cell_index(P, tol):
    idx = defaultdict(list)
    C = np.floor(P / tol).astype(np.int64)
    for i, (cx, cy) in enumerate(C):
        idx[(int(cx), int(cy))].append(i)
    return idx


def nearest_within(P, Q, tol):
    """Pour chaque point de P : indice du point de Q le plus proche a distance <= tol (ou -1)."""
    idx = cell_index(Q, tol)
    out = np.full(len(P), -1, dtype=np.int64)
    C = np.floor(P / tol).astype(np.int64)
    t2 = tol * tol
    for i in range(len(P)):
        cx, cy = int(C[i, 0]), int(C[i, 1])
        best, bd = -1, t2 + 1
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for j in idx.get((cx + dx, cy + dy), ()):
                    d = (Q[j, 0] - P[i, 0]) ** 2 + (Q[j, 1] - P[i, 1]) ** 2
                    if d < bd:
                        bd, best = d, j
        if bd <= t2:
            out[i] = best
    return out


def frac_fast(P, Q, tol):
    """Fraction approchee (cellules de taille tol, voisinage 3x3) de points de P a <= ~tol d'un point de Q."""
    keyQ = set(map(tuple, np.floor(Q / tol).astype(np.int64).tolist()))
    C = np.floor(P / tol).astype(np.int64)
    hit = 0
    for cx, cy in C.tolist():
        for dx in (-1, 0, 1):
            if any((cx + dx, cy + dy) in keyQ for dy in (-1, 0, 1)):
                hit += 1
                break
    return hit / max(1, len(P))


def transform(dos, s, M, o):
    return (dos.P @ M.T) * s + o[None, :]


def segment_match(dos, sat, Pb, tol):
    """Fraction de murs Build (segments uniques) dont les deux extremites tombent (<= tol) sur deux points Saturn
    formant l'arete basse d'un mur vertical Saturn (dans un sens ou l'autre)."""
    nn = nearest_within(Pb, sat.P, tol)
    segs = set()
    for a, b in dos.segs.tolist():
        if a != b:
            segs.add(frozenset((a, b)))
    hit = 0
    for sgm in segs:
        a, b = tuple(sgm)
        if nn[a] >= 0 and nn[b] >= 0 and frozenset((int(nn[a]), int(nn[b]))) in sat.segset:
            hit += 1
    return hit / max(1, len(segs)), len(segs)


def on_line_fraction(sat, Pb, dos, tol):
    """Fraction de murs verticaux Saturn (segments uniques) dont les deux extremites sont a <= tol d'un meme mur
    Build transforme (colinearite + inclusion dans l'etendue) : detecte les murs Build re-decoupes."""
    A = Pb[dos.segs[:, 0]]
    B = Pb[dos.segs[:, 1]]
    L = np.hypot(*(B - A).T)
    ok = L > 0
    A, B, L = A[ok], B[ok], L[ok]
    U = (B - A) / L[:, None]
    lo = np.minimum(A, B) - tol
    hi = np.maximum(A, B) + tol
    hit = 0
    segs = list(sat.segset)
    for sgm in segs:
        a, b = tuple(sgm)
        pa, pb = sat.P[a], sat.P[b]
        cand = np.all((lo <= np.minimum(pa, pb)) & (hi >= np.maximum(pa, pb)), axis=1)
        if not cand.any():
            continue
        Ac, Uc = A[cand], U[cand]
        da = np.abs((pa - Ac)[:, 0] * Uc[:, 1] - (pa - Ac)[:, 1] * Uc[:, 0])
        db = np.abs((pb - Ac)[:, 0] * Uc[:, 1] - (pb - Ac)[:, 1] * Uc[:, 0])
        if np.any((da <= tol) & (db <= tol)):
            hit += 1
    return hit / max(1, len(segs)), len(segs)


def scan_pair(sat, dos, scales, quick=False):
    best = None
    rows = []
    for s in scales:
        for mname, M in MAPS8.items():
            Pb0 = (dos.P @ M.T) * s
            o, support = mode_offset(Pb0, sat.P, q=8.0, nb=120 if quick else 160)
            Pb = Pb0 + o[None, :]
            f4 = frac_fast(Pb, sat.P, 4.0)
            row = dict(scale=s, map=mname, off=[float(o[0]), float(o[1])], support=support, f4=f4)
            rows.append(row)
            if best is None or f4 > best['f4']:
                best = row
    return best, rows


def refine_pair(sat, dos, best):
    """Affinage continu : appariements <= 16 unites -> moindres carres sur (echelle uniforme, translation),
    orientation fixee ; puis scores exacts a 1/4/16 unites dans les deux sens, segments, murs sur droite."""
    M = MAPS8[best['map']]
    s = best['scale']
    o = np.array(best['off'])
    for _ in range(3):
        Pb = transform(dos, s, M, o)
        nn = nearest_within(Pb, sat.P, 16.0)
        sel = nn >= 0
        if sel.sum() < 8:
            break
        X = (dos.P[sel] @ M.T)
        Y = sat.P[nn[sel]]
        # min sum |s X + o - Y|^2 : s = cov(X,Y)/var(X) sur les deux axes, o = mean(Y) - s mean(X)
        xm, ym = X.mean(axis=0), Y.mean(axis=0)
        num = ((X - xm) * (Y - ym)).sum()
        den = ((X - xm) ** 2).sum()
        s_new = num / den if den > 0 else s
        o_new = ym - s_new * xm
        if abs(s_new - s) < 1e-9 and np.allclose(o_new, o):
            break
        s, o = s_new, o_new
    Pb = transform(dos, s, M, o)
    res = dict(map=best['map'], scale=float(s), off=[float(o[0]), float(o[1])], n_build_pts=len(dos.P),
               n_sat_pts=len(sat.P))
    for tol in (1.0, 4.0, 16.0):
        res['build_in_sat_%g' % tol] = float((nearest_within(Pb, sat.P, tol) >= 0).mean())
        res['sat_in_build_%g' % tol] = float((nearest_within(sat.P, Pb, tol) >= 0).mean())
    res['build_segments_matched_4'], res['n_build_segments'] = segment_match(dos, sat, Pb, 4.0)
    res['sat_walls_on_build_line_4'], res['n_sat_segments'] = on_line_fraction(sat, Pb, dos, 4.0)
    # temoin : meme transformation decalee de (37, 53) unites = niveau de coincidence fortuite
    Pc = Pb + np.array([37.0, 53.0])[None, :]
    res['control_build_in_sat_4'] = float((nearest_within(Pc, sat.P, 4.0) >= 0).mean())
    res['control_segments_4'] = segment_match(dos, sat, Pc, 4.0)[0]
    return res


# ---------------------------------------------------------------------------------------------
# Structure Saturn
# ---------------------------------------------------------------------------------------------
def poly_area(P):
    x, y = P[:, 0], P[:, 1]
    return 0.5 * float(np.dot(x, np.roll(y, -1)) - np.dot(y, np.roll(x, -1)))


def is_convex(P, eps=1e-6):
    n = len(P)
    if n < 3:
        return False
    sign = 0
    for i in range(n):
        a, b, c = P[i], P[(i + 1) % n], P[(i + 2) % n]
        cr = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
        if abs(cr) < eps:
            continue
        s = 1 if cr > 0 else -1
        if sign == 0:
            sign = s
        elif s != sign:
            return False
    return True


def point_in_poly(pt, P):
    x, y = pt
    inside = False
    n = len(P)
    for i in range(n):
        x1, y1 = P[i]
        x2, y2 = P[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            xi = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
            if x < xi:
                inside = not inside
    return inside


def structure(sat):
    W = sat.walls
    S = sat.sectors
    V = sat.V
    polys = [sat.sector_poly(i) for i in range(len(S))]
    convex3d = sum(1 for i in range(len(S)) if sat.sector_is_convex_polyhedron(i))
    flat_doors = sum(1 for i in range(len(S)) if sat.sector_is_flat_door(i))
    sec_of_wall = np.zeros(len(W), dtype=np.int64)
    for si, s in enumerate(S):
        sec_of_wall[s['firstWall']:s['lastWall'] + 1] = si
    # portails : murs avec nextSector (hors sol/plafond) ; INVISIBLE (0x02) ou textures ; symetrie : le voisin
    # a-t-il un mur dont l'arete XZ (v0->v1) est la meme (dans un sens ou l'autre) ?
    seg_by_sector = defaultdict(set)
    for wi, w in enumerate(W):
        if abs(w['normal'][1]) != 65536:
            a, b = int(sat.pid[w['v'][0]]), int(sat.pid[w['v'][1]])
            seg_by_sector[int(sec_of_wall[wi])].add(frozenset((a, b)))
    portals = portals_invisible = portals_textured = symmetric = 0
    horizontal_portals = 0
    for wi, w in enumerate(W):
        if w['nextSector'] < 0:
            continue
        if abs(w['normal'][1]) == 65536:
            horizontal_portals += 1        # sol/plafond avec nextSector = surface d'eau / empilement vertical
            continue
        portals += 1
        if w['flags'] & 0x02:
            portals_invisible += 1
        if w['firstFace'] != -1 or (w['flags'] & 0x01):
            portals_textured += 1
        a, b = int(sat.pid[w['v'][0]]), int(sat.pid[w['v'][1]])
        if frozenset((a, b)) in seg_by_sector[w['nextSector']]:
            symmetric += 1
    # empilement de murs sur une meme arete XZ d'un secteur (bas plein / portail / haut plein)
    edge_walls = Counter()
    for si, s in enumerate(S):
        per_edge = Counter()
        for wi in range(s['firstWall'], s['lastWall'] + 1):
            w = W[wi]
            if abs(w['normal'][1]) == 65536:
                continue
            a, b = int(sat.pid[w['v'][0]]), int(sat.pid[w['v'][1]])
            if a != b:
                per_edge[frozenset((a, b))] += 1
        for k in per_edge.values():
            edge_walls[k] += 1
    # empilement vertical de secteurs : empreintes XZ qui se recouvrent (centre de l'un dans l'autre)
    fy, cy = [], []
    for si, s in enumerate(S):
        f, c = [], []
        for wi in range(s['firstWall'], s['lastWall'] + 1):
            w = W[wi]
            if w['normal'][1] > 0:
                f += [int(V[v][1]) for v in w['v']]
            elif w['normal'][1] < 0:
                c += [int(V[v][1]) for v in w['v']]
        fy.append(np.mean(f) if f else None)
        cy.append(np.mean(c) if c else None)
    stacked = Counter()
    examples = []
    water = [bool(s['flags'] & 1) for s in S]
    centers = [np.array([s['center'][0], s['center'][2]], dtype=np.float64) for s in S]
    bboxes = [(p.min(axis=0), p.max(axis=0)) if p is not None else None for p in polys]
    for i in range(len(S)):
        if polys[i] is None:
            continue
        for j in range(i + 1, len(S)):
            if polys[j] is None:
                continue
            bi, bj = bboxes[i], bboxes[j]
            if np.any(bi[1] < bj[0]) or np.any(bj[1] < bi[0]):
                continue
            if not (point_in_poly(centers[j], polys[i]) or point_in_poly(centers[i], polys[j])):
                continue
            if None in (fy[i], fy[j], cy[i], cy[j]):
                k = 'incomplet'
            elif fy[j] >= cy[i] - 1 or fy[i] >= cy[j] - 1:
                k = 'superpose'
            else:
                k = 'chevauche'
            if water[i] or water[j]:
                k += '+eau'
            stacked[k] += 1
            if len(examples) < 4:
                examples.append((i, j, S[i]['floorLevel'], S[j]['floorLevel'], k))
    sloped_walls = int((~sat.flat).sum())
    sloped_sectors = sum(1 for s in S if any(not sat.flat[wi] for wi in range(s['firstWall'], s['lastWall'] + 1)))
    nfloor = int((sat.kind == 2).sum())
    nceil = int((sat.kind == 1).sum())
    fl_ok = fl_n = 0
    for si, s in enumerate(S):
        ys = []
        for wi in range(s['firstWall'], s['lastWall'] + 1):
            if W[wi]['normal'][1] > 0:
                ys += [int(V[v][1]) for v in W[wi]['v']]
        if ys:
            fl_n += 1
            if abs(sum(ys) / len(ys) - s['floorLevel']) <= 1:
                fl_ok += 1
    wps = Counter(s['lastWall'] - s['firstWall'] + 1 for s in S)
    return dict(sectors=len(S), walls=len(W), convex_polyhedra=convex3d, flat_doors=flat_doors,
                portals=portals, portals_invisible=portals_invisible, portals_textured=portals_textured,
                portals_symmetric=symmetric, horizontal_portals=horizontal_portals,
                walls_per_edge=dict(sorted(edge_walls.items())),
                stacked=dict(stacked), stacked_examples=examples,
                sloped_walls=sloped_walls, sloped_sectors=sloped_sectors, floor_walls=nfloor, ceiling_walls=nceil,
                water_sectors=sum(water), floorLevel_is_mean=(fl_ok, fl_n),
                walls_per_sector_mode=wps.most_common(1)[0][0], objects=len(sat.objects),
                object_types=len(set(o['type'] for o in sat.objects)))


def build_structure(dos):
    """Cote Build : boucles par secteur, convexite de la boucle externe, sommets rentrants (borne inf. du nombre de
    morceaux convexes = rentrants + 1, + 2 par trou), paires de secteurs qui se recouvrent en XY (regle pair-impair
    sur toutes les boucles : un ilot dans un trou n'est PAS un recouvrement), murs a 2 faces."""
    m = dos.m
    W = m.walls
    loops_per_sector = []
    convex = multiloop = reflex_total = pieces = 0
    for s in m.sectors:
        seen = set()
        loops = []
        for w0 in range(s.wallptr, s.wallptr + s.wallnum):
            if w0 in seen:
                continue
            loop = []
            w = w0
            while w not in seen:
                seen.add(w)
                loop.append(w)
                w = W[w].point2
            loops.append(np.array([[W[i].x, W[i].y] for i in loop], dtype=np.float64))
        loops_per_sector.append(loops)
        if len(loops) > 1:
            multiloop += 1
        outer = max(loops, key=lambda L: abs(poly_area(L)))
        sgn = 1 if poly_area(outer) > 0 else -1
        r = 0
        n = len(outer)
        for i in range(n):
            a, b, c = outer[i - 1], outer[i], outer[(i + 1) % n]
            cr = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
            if cr * sgn < 0:
                r += 1
        holes = len(loops) - 1
        reflex_total += r
        if r == 0 and holes == 0:
            convex += 1
        pieces += r + 1 + 2 * holes

    def inside(pt, loops):
        return sum(1 for L in loops if point_in_poly(pt, L)) % 2 == 1

    cents = [max(L, key=lambda L: abs(poly_area(L))).mean(axis=0) for L in loops_per_sector]
    bb = [(np.min([L.min(axis=0) for L in loops], axis=0), np.max([L.max(axis=0) for L in loops], axis=0))
          for loops in loops_per_sector]
    overlap = 0
    overlap_lotag = Counter()
    for i in range(len(loops_per_sector)):
        for j in range(i + 1, len(loops_per_sector)):
            if np.any(bb[i][1] < bb[j][0]) or np.any(bb[j][1] < bb[i][0]):
                continue
            if inside(cents[j], loops_per_sector[i]) or inside(cents[i], loops_per_sector[j]):
                overlap += 1
                overlap_lotag[(m.sectors[i].lotag, m.sectors[j].lotag)] += 1
    return dict(sectors=len(m.sectors), walls=len(W), convex=convex, multiloop=multiloop, reflex=reflex_total,
                pieces_lower_bound=pieces, overlap_pairs=overlap, overlap_lotags=overlap_lotag.most_common(4),
                two_sided=int(dos.two_sided.sum()), sprites=len(m.sprites),
                max_walls_per_sector=max(s.wallnum for s in m.sectors))


# ---------------------------------------------------------------------------------------------
# Hauteurs, tuiles, tables
# ---------------------------------------------------------------------------------------------
def build_height_stats(dos):
    m = dos.m
    fz = np.array([s.floorz for s in m.sectors])
    cz = np.array([s.ceilingz for s in m.sectors])
    h = fz - cz
    tex = []
    for w in m.walls:
        p = m.walls[w.point2]
        L = ((p.x - w.x) ** 2 + (p.y - w.y) ** 2) ** 0.5
        if w.xrepeat > 0 and L > 0:
            tex.append(L / (8.0 * w.xrepeat))     # engine.c:1027 walxrepeat = xrepeat<<3 texels sur le mur
    tex = np.array(tex)
    return dict(floorz_mod1024=float((fz % 1024 == 0).mean()), ceilz_mod1024=float((cz % 1024 == 0).mean()),
                room_height_median_z=float(np.median(h[h > 0])) if (h > 0).any() else 0.0,
                texel_xy_median=float(np.median(tex)), texel_xy_p10=float(np.percentile(tex, 10)),
                texel_xy_p90=float(np.percentile(tex, 90)),
                x_mod64=float((dos.XY_all[:, 0] % 64 == 0).mean()), x_mod1024=float((dos.XY_all[:, 0] % 1024 == 0).mean()))


def sat_height_stats(sat):
    V = sat.V
    hs = []
    for s in sat.sectors:
        fy, cy = [], []
        for wi in range(s['firstWall'], s['lastWall'] + 1):
            w = sat.walls[wi]
            if w['normal'][1] > 0:
                fy += [V[v][1] for v in w['v']]
            elif w['normal'][1] < 0:
                cy += [V[v][1] for v in w['v']]
        if fy and cy:
            hs.append(np.mean(cy) - np.mean(fy))
    hs = np.array(hs)
    return dict(y_mod16=float((V[:, 1] % 16 == 0).mean()), y_mod64=float((V[:, 1] % 64 == 0).mean()),
                x_mod64=float((V[:, 0] % 64 == 0).mean()), room_height_median=float(np.median(hs)) if len(hs) else 0.0,
                y_up=bool(np.mean(hs) > 0))


def fmt(x, nd=2):
    return ('%.' + str(nd) + 'f') % x



# ---------------------------------------------------------------------------------------------
# Remplissage de docs/BUILD2DEX_CALIBRATION.md
# ---------------------------------------------------------------------------------------------
NL = chr(10)
DOC = os.path.join(ROOT, 'docs', 'BUILD2DEX_CALIBRATION.md')
DOC_SECTIONS = {
    'SAT_NAMES': '## Noms Saturn',
    'DOS_NAMES': '## Noms DOS',
    'SCAN': '## Balayage geometrique',
    'REFINE': '## Affinage exact',
    'CONTROLS': 'Temoin :',
    'VERDICT': '## Verdict final',
    'HEIGHTS': '## Loi des hauteurs',
    'SAT_STRUCT': '## Structure des niveaux Saturn',
    'DOS_STRUCT': '## Structure des cartes Build',
    'TILES': '## Tuiles :',
}


def fill_doc(out_lines):
    """Insere les tables imprimees par main() dans docs/BUILD2DEX_CALIBRATION.md : chaque balise
    <<NAME>> (premier remplissage) ou bloc <!-- @NAME --> .. <!-- /@NAME --> (re-executions)
    recoit la section stdout correspondante (DOC_SECTIONS). Ecriture atomique (tmp + os.replace)."""
    blocks = {}
    for name, marker in DOC_SECTIONS.items():
        if name == 'CONTROLS':
            blocks[name] = NL.join(l for l in out_lines if l.startswith('Temoin :'))
            continue
        starts = [k for k, l in enumerate(out_lines) if l.startswith(marker)]
        if not starts:
            continue
        i = starts[0]
        j = i + 1
        while j < len(out_lines) and not out_lines[j].startswith('## ') and not out_lines[j].startswith('Temoin :'):
            j += 1
        body = [l for l in out_lines[i:j] if not re.match(r'^(scan |noms DOS rendus|wrote |doc rempli)', l)]
        while body and not body[-1].strip():
            body.pop()
        body[0] = '**' + body[0][3:] + '**' + NL
        blocks[name] = NL.join(body)
    doc = open(DOC, encoding='utf-8').read()
    for name, blk in blocks.items():
        if not blk:
            continue
        wrapped = ('<!-- @%s -->' % name) + NL + blk + NL + ('<!-- /@%s -->' % name)
        tag = '<<' + name + '>>'
        if tag in doc:
            doc = doc.replace(tag, wrapped)
        else:
            pat = re.compile('<!-- @%s -->.*?<!-- /@%s -->' % (name, name), re.S)
            if pat.search(doc):
                doc = pat.sub(lambda m: wrapped, doc)
            else:
                print('fill_doc: ni balise ni bloc pour', name)
    tmpf = DOC + '.tmp'
    with open(tmpf, 'w', encoding='utf-8', newline=NL) as f:
        f.write(doc)
    os.replace(tmpf, DOC)
    print('doc rempli :', DOC)


def main(argv):
    import builtins
    doc_lines = []

    def print(*args, **kw):
        s = kw.get('sep', ' ').join(str(x) for x in args)
        doc_lines.extend(s.split(chr(10)))
        builtins.print(*args, **kw)

    do_scan = '--scan' in argv
    do_names = '--names' in argv
    quick = '--quick' in argv
    out_dir = argv[argv.index('--out') + 1] if '--out' in argv else TMP
    os.makedirs(out_dir, exist_ok=True)
    only = []
    if '--sat' in argv:
        only = [a for a in argv[argv.index('--sat') + 1:] if not a.startswith('--')]
    os.makedirs(TMP, exist_ok=True)
    sat_stats = json.load(open(os.path.join(TMP, 'sat_stats.json')))
    dos_stats = json.load(open(os.path.join(TMP, 'dos_stats.json')))
    sat_names = sorted(k for k in sat_stats if k != 'TEST')     # TEST == SANCTUAR octet pour octet
    if only:
        sat_names = only
    dos_nums = sorted(int(re.search(r'LEV(\d+)', k).group(1)) for k in dos_stats)
    sat_disp = sat_names_from_initload()

    if do_names:
        print('noms DOS rendus :', render_dos_names(os.path.join(out_dir, 'names')))

    # ---- 1. noms -------------------------------------------------------------------------------
    print('\n## Noms Saturn (INITLOAD.DAT, LB_LEVELNAMES ; index = levelGraph BIGMAP.C:43-81)')
    print('| idx | fichier | nom affiche | sect | murs |')
    print('|---|---|---|---|---|')
    for i, f in enumerate(SAT_GRAPH):
        st = sat_stats.get(f)
        c = st['counts'] if st else None
        print('| %d | %s | %s | %s | %s |' % (i, f, sat_disp.get(i, '-'), c['sectors'] if c else 'absent du disque',
                                             c['walls'] if c else '-'))
    print('\n## Noms DOS (tuiles mapNamePlaques menu.cpp:582-604, transcrits depuis build/tmp-b2d/names/names_strip.png)')
    print('| MAP | nom | sect | murs | sprites |')
    print('|---|---|---|---|---|')
    for n in dos_nums:
        c = dos_stats['LEV%d.MAP' % n]['counts']
        print('| LEV%d | %s | %d | %d | %d |' % (n, DOS_NAMES.get(n, '(deathmatch, sans nom)'), c['sectors'], c['walls'],
                                              c['sprites']))

    # ---- 2. balayage ---------------------------------------------------------------------------
    scan_path = os.path.join(out_dir, 'calib_scan.json')
    scan = {}
    if os.path.exists(scan_path) and not do_scan:
        scan = json.load(open(scan_path))
    sats = {n: Sat(n) for n in sat_names}
    doss = {n: Dos(n) for n in dos_nums}
    if do_scan or not scan:
        scales = SCALES_QUICK if quick else SCALES_FULL
        t0 = time.time()
        for sn in sat_names:
            scan[sn] = {}
            for dn in dos_nums:
                best, rows = scan_pair(sats[sn], doss[dn], scales, quick)
                scan[sn]['LEV%d' % dn] = best
            top = sorted(scan[sn].items(), key=lambda kv: -kv[1]['f4'])[:3]
            print('scan %-9s %5.0fs best: %s' % (sn, time.time() - t0,
                                                 ', '.join('%s f4=%.3f s=%g %s' % (k, v['f4'], v['scale'], v['map']) for k, v in top)), flush=True)
            tmp = scan_path + '.tmp'
            json.dump(scan, open(tmp, 'w'), indent=1)
            os.replace(tmp, scan_path)

    # ---- 3. appariement : meilleur DOS par LEV Saturn, avec le null (les 32 autres cartes) --------
    print('\n## Balayage geometrique : meilleur .MAP par .LEV (f4 = fraction approchee de points Build a <= 4 u d\'un sommet Saturn)')
    print('| LEV Saturn | meilleur MAP | f4 | echelle | orientation | 2e MAP | f4 (2e) | mediane f4 (33) | verdict |')
    print('|---|---|---|---|---|---|---|---|---|')
    report = dict(pairs={}, structure={}, heights={}, tiles={})
    for sn in sat_names:
        if sn not in scan:
            continue
        items = sorted(scan[sn].items(), key=lambda kv: -kv[1]['f4'])
        f4s = np.array([v['f4'] for _, v in items])
        b, sec = items[0], items[1]
        med = float(np.median(f4s))
        verdict = 'APPARIE' if (b[1]['f4'] > 0.5 and b[1]['f4'] > 2.5 * med) else 'aucun appariement (bruit)'
        print('| %s | %s | %s | 1/%g | %s | %s | %s | %s | %s |' % (
            sn, b[0], fmt(b[1]['f4'], 3), 1 / b[1]['scale'], b[1]['map'], sec[0], fmt(sec[1]['f4'], 3), fmt(med, 3), verdict))
        report['pairs'][sn] = dict(best=b[0], best_row=b[1], second=sec[0], second_f4=sec[1]['f4'], median_f4=med, verdict=verdict)

    # ---- 4. affinage exact sur les candidats : KARNAK<->LEV16 (nom) + meilleur geometrique de chaque LEV --------
    print('\n## Affinage exact (moindres carres echelle+translation, tolerance 1/4/16 u ; segments = murs Build dont les 2 bouts sont des sommets Saturn d\'un meme mur ; droite = murs Saturn portes par une droite de mur Build)')
    print('| LEV | MAP | orientation | echelle | offset | B->S 1 | B->S 4 | B->S 16 | S->B 4 | segments | murs S sur droite B | temoin decale : B->S 4 / segments |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|')
    cands = []
    for sn in sat_names:
        if sn in report['pairs']:
            cands.append((sn, report['pairs'][sn]['best']))
            cands.append((sn, report['pairs'][sn]['second']))
    if 'KARNAK' in sats and ('KARNAK', 'LEV16') not in cands:
        cands.append(('KARNAK', 'LEV16'))
    report['refined'] = {}
    for sn, dk in cands:
        dn = int(dk[3:])
        best = scan[sn][dk] if dk in scan.get(sn, {}) else scan_pair(sats[sn], doss[dn], SCALES_FULL)[0]
        r = refine_pair(sats[sn], doss[dn], best)
        report['refined']['%s:%s' % (sn, dk)] = r
        print('| %s | %s | %s | 1/%.2f | (%.0f, %.0f) | %s | %s | %s | %s | %s (%d) | %s (%d) | %s / %s |' % (
            sn, dk, r['map'], 1 / r['scale'] if r['scale'] else 0, r['off'][0], r['off'][1],
            fmt(r['build_in_sat_1'], 3), fmt(r['build_in_sat_4'], 3), fmt(r['build_in_sat_16'], 3), fmt(r['sat_in_build_4'], 3),
            fmt(r['build_segments_matched_4'], 3), r['n_build_segments'], fmt(r['sat_walls_on_build_line_4'], 3), r['n_sat_segments'],
            fmt(r['control_build_in_sat_4'], 3), fmt(r['control_segments_4'], 3)))

    # ---- 5. temoin : un niveau Saturn contre lui-meme (auto-appariement = 1.0) et deux Saturn entre eux --------
    if 'KARNAK' in sats and 'TOMB' in sats:
        a, b = sats['KARNAK'], sats['TOMB']
        class _D:  # pseudo-Dos a partir d'un Saturn (points XZ, segments)
            pass
        d = _D(); d.P = b.P; d.segs = b.segs; d.XY_all = b.XZ_all
        best, _ = scan_pair(a, d, [1.0])
        print('\nTemoin : TOMB (comme "Build") contre KARNAK a l\'echelle 1 : f4 = %.3f (%s) ; '
              'deux niveaux Saturn sans lien donnent ce niveau de bruit.' % (best['f4'], best['map']))
        report['control_tomb_vs_karnak_f4'] = best['f4']
        d.P = a.P; d.segs = a.segs
        best, _ = scan_pair(a, d, [1.0])
        print('Temoin : KARNAK contre lui-meme : f4 = %.3f' % best['f4'])
        report['control_self_f4'] = best['f4']

    # ---- 6. hauteurs ---------------------------------------------------------------------------
    print('\n## Loi des hauteurs et grilles')
    print('| niveau | grille XZ (x%64) | y%16 | y%64 | hauteur piece mediane (u) | y vers le haut | floorLevel = moy. sol |')
    print('|---|---|---|---|---|---|---|')
    for sn in sat_names:
        h = sat_height_stats(sats[sn])
        st = structure(sats[sn])
        report['structure'][sn] = st
        report['heights'][sn] = h
        print('| %s | %s | %s | %s | %.0f | %s | %d/%d |' % (sn, fmt(h['x_mod64']), fmt(h['y_mod16']), fmt(h['y_mod64']),
                                                       h['room_height_median'], h['y_up'], st['floorLevel_is_mean'][0], st['floorLevel_is_mean'][1]))
    print('\n| MAP | floorz%1024 | ceilz%1024 | hauteur piece mediane (z) | /256 | texel xy median | p10 | p90 | x%64 | x%1024 |')
    print('|---|---|---|---|---|---|---|---|---|---|')
    for dn in dos_nums:
        h = build_height_stats(doss[dn])
        report['heights']['LEV%d' % dn] = h
        print('| LEV%d | %s | %s | %.0f | %.0f | %.1f | %.1f | %.1f | %s | %s |' % (
            dn, fmt(h['floorz_mod1024']), fmt(h['ceilz_mod1024']), h['room_height_median_z'], h['room_height_median_z'] / 256,
            h['texel_xy_median'], h['texel_xy_p10'], h['texel_xy_p90'], fmt(h['x_mod64']), fmt(h['x_mod1024'])))

    # ---- 7. structure Saturn -------------------------------------------------------------------
    print('\n## Structure des niveaux Saturn')
    print('| LEV | sect | polyedres convexes (murs lateraux) | portes aplaties | portails lateraux | INVISIBLE | textures | symetriques | portails sol/plafond | murs par arete XZ {k: n} | superposes (au-dessus) | dont +eau | chevauchent | eau | secteurs a pente | murs en pente | objets (types) |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for sn in sat_names:
        st = report['structure'][sn]
        stk = st['stacked']
        sup = stk.get('superpose', 0) + stk.get('superpose+eau', 0)
        supw = stk.get('superpose+eau', 0)
        chev = stk.get('chevauche', 0) + stk.get('chevauche+eau', 0)
        print('| %s | %d | %d | %d | %d | %d | %d | %d | %d | %s | %d | %d | %d | %d | %d | %d | %d (%d) |' % (
            sn, st['sectors'], st['convex_polyhedra'], st['flat_doors'], st['portals'], st['portals_invisible'],
            st['portals_textured'], st['portals_symmetric'], st['horizontal_portals'], st['walls_per_edge'], sup, supw, chev,
            st['water_sectors'], st['sloped_sectors'], st['sloped_walls'], st['objects'], st['object_types']))
    for sn in sat_names:
        ex = report['structure'][sn]['stacked_examples']
        if ex:
            print('  %s exemples (i, j, floorLevel_i, floorLevel_j, classe) : %s' % (sn, ex[:3]))

    print('\n## Structure des cartes Build (DOS)')
    print('| MAP | sect | murs | 2 faces | convexes | multi-boucles | sommets rentrants | morceaux convexes >= | x | recouvrements XY (pair-impair) | lotags des paires | sprites | max murs/sect |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for dn in dos_nums:
        b = build_structure(doss[dn])
        report['structure']['LEV%d' % dn] = b
        print('| LEV%d | %d | %d | %d | %d (%.0f%%) | %d | %d | %d | %.2f | %d | %s | %d | %d |' % (
            dn, b['sectors'], b['walls'], b['two_sided'], b['convex'], 100.0 * b['convex'] / b['sectors'], b['multiloop'], b['reflex'],
            b['pieces_lower_bound'], b['pieces_lower_bound'] / b['sectors'], b['overlap_pairs'], b['overlap_lotags'], b['sprites'],
            b['max_walls_per_sector']))

    # ---- 8. tuiles -----------------------------------------------------------------------------
    print('\n## Tuiles : picnums Build distincts (murs+sols+plafonds+overpic) vs tuiles Saturn distinctes (faces U texture) / tuiles du fichier')
    print('| MAP | picnums murs | sols | plafonds | overpic | union | sprites picnums |   | LEV | tuiles faces | table texture | union | tuiles fichier |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    sl = list(sat_names)
    for i, dn in enumerate(dos_nums):
        p = dos_stats['LEV%d.MAP' % dn]['picnums']
        u = set(p['walls']) | set(p['floors']) | set(p['ceilings']) | set(p['overpicnums'])
        right = ''
        if i < len(sl):
            st = sat_stats[sl[i]]
            right = '%s | %d | %d | %d | %d' % (sl[i], st['distinct_face_tiles'], st['distinct_texture_tiles'], st['distinct_tiles'], st['counts']['tiles'])
            report['tiles'][sl[i]] = dict(faces=st['distinct_face_tiles'], texture=st['distinct_texture_tiles'], union=st['distinct_tiles'], file=st['counts']['tiles'])
        report['tiles']['LEV%d' % dn] = dict(walls=len(p['walls']), floors=len(p['floors']), ceilings=len(p['ceilings']), over=len(p['overpicnums']), union=len(u), sprites=len(p['sprites']))
        print('| LEV%d | %d | %d | %d | %d | %d | %d |   | %s |' % (dn, len(p['walls']), len(p['floors']), len(p['ceilings']),
                                                                  len(p['overpicnums']), len(u), len(p['sprites']), right))

    print('\n## Verdict final par LEV (segments Build retrouves a 4 u sur le meilleur candidat ; APPARIE si > 0.25 et > 5x le temoin)')
    print('| LEV | MAP | segments | temoin | verdict |')
    print('|---|---|---|---|---|')
    for sn in sat_names:
        if sn not in report['pairs']:
            continue
        dk = report['pairs'][sn]['best']
        r = report['refined'].get('%s:%s' % (sn, dk))
        if not r:
            continue
        v = 'APPARIE' if (r['build_segments_matched_4'] > 0.25 and r['build_segments_matched_4'] > 5 * max(1e-3, r['control_segments_4'])) else 'AUCUN'
        report['pairs'][sn]['final'] = v
        print('| %s | %s | %s | %s | %s |' % (sn, dk, fmt(r['build_segments_matched_4'], 3), fmt(r['control_segments_4'], 3), v))

    tmp = os.path.join(out_dir, 'calib_report.json.tmp')
    json.dump(report, open(tmp, 'w'), indent=1, default=lambda o: o.tolist() if hasattr(o, 'tolist') else str(o))
    os.replace(tmp, os.path.join(TMP, 'calib_report.json'))
    print('\nwrote', os.path.join(TMP, 'calib_report.json'))
    if '--fill-doc' in argv:
        fill_doc(doc_lines)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

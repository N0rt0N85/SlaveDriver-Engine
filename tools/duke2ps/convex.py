#!/usr/bin/env python3
"""convex.py -- etape E2 du convertisseur Duke Nukem 3D PC -> .LEV PowerSlave (fork gen1).

Decoupe chaque secteur garde par E1 (boucle exterieure + trous) en morceaux CONVEXES, en
arithmetique entiere EXACTE (coordonnees Build entieres, predicats orient() sur des entiers,
aucun point de Steiner) :
  1. pont vers les trous (sommet le plus a droite du trou -> sommet visible le plus proche) ;
  2. triangulation par oreilles (plusieurs strategies, la meilleure est gardee par secteur) ;
  3. fusion Hertel-Mehlhorn (retrait d'une diagonale si l'union reste convexe) ;
  4. fusion gloutonne (paires, puis triplets) jusqu'a stabilite ;
  5. fusion optionnelle de morceaux de secteurs Build voisins IDENTIQUES (hauteurs/plans, pentes,
     picnums, shade, pal, panning, stat, lotag, hitag ; murs communs sans drapeau 1|16|32|64 ni
     lotag/hitag) quand l'union reste convexe ;
  6. jonctions en T : recouvrements colineaires opposes non apparies ; l'arete interne est scindee ;
  7. jumelles : diagonales (meme id, sens oppose) et portails (nextwall, sens oppose), exactes.

Convexite : un sommet colineaire (angle de 180 deg) est admis -- il est necessaire pour que chaque arete
ait une jumelle exacte (le voisin a un sommet a cet endroit). Aucun pic (aller-retour) n'est admis.

Orientation de sortie : celle des boucles exterieures Build, somme de lacets
sum(x_i*y_{i+1} - x_{i+1}*y_i) > 0 (repere Build, y vers le bas ; clockdir() == 0) ; en repere Saturn
(X = x/8, Z = -y/8) la somme sum(X_i*Z_{i+1} - X_{i+1}*Z_i) est < 0. Le domaine est a GAUCHE de chaque
arete dans le plan (x, y) pris comme un plan cartesien ordinaire.

Usage (depuis la racine du depot) :
    python tools\\duke2ps\\convex.py [--in J] [--out J] [--png P | --no-png] [--no-fusion-secteurs]
                                     [--split-solid-t] [--no-holywood] [--selftest]
Code retour 0 si tous les criteres d'acceptation passent, 1 sinon (le JSON est ecrit dans les deux cas).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import random
import sys
import tempfile
import time
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
IN_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_import.json")
OUT_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_convex.json")
PNG_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_convex.png")
HOLYWOOD = os.path.join(ROOT, "refs", "extract", "DUKE", "DUKE", "HOLYWOOD.LEV")
GEN2 = os.path.join(ROOT, "build", "tmp-gen2", "gen2_lev.py")

BU = 8                       # unites Build par unite Saturn (X = x/8, Z = -y/8)
TARGET, MAXNMSECTORS = 520, 600   # cible du plan ; plafond dur (UTIL.H:21)
PLAN_LB, PLAN_UB, HOLYWOOD_N = 471, 618, 415
CONVEX_TOL_U = 0.5
AREA_TOL = 0.001
MIN_AREA_U2 = 1.0
NOMERGE_WALL_CSTAT = 1 | 16 | 32 | 64   # bloquant, masque, sens unique, bloque hitscan

sys.setrecursionlimit(10000)


# ------------------------------------------------------------------------------------------
# geometrie entiere exacte
# ------------------------------------------------------------------------------------------
def orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def area2(pts):
    n = len(pts)
    return sum(pts[k][0] * pts[(k + 1) % n][1] - pts[(k + 1) % n][0] * pts[k][1] for k in range(n))


def strictly_inside_seg(a, b, p):
    """p dans le segment OUVERT ab (entier exact)."""
    if orient(a, b, p) != 0:
        return False
    return ((p[0] - a[0]) * (b[0] - a[0]) + (p[1] - a[1]) * (b[1] - a[1]) > 0 and
            (p[0] - b[0]) * (a[0] - b[0]) + (p[1] - b[1]) * (a[1] - b[1]) > 0)


def segs_conflict(a, b, u, w):
    """Vrai si le segment ab touche uw autrement qu'en un point extremite COMMUN aux deux."""
    if max(a[0], b[0]) < min(u[0], w[0]) or max(u[0], w[0]) < min(a[0], b[0]) or \
       max(a[1], b[1]) < min(u[1], w[1]) or max(u[1], w[1]) < min(a[1], b[1]):
        return False
    if (a == u and b == w) or (a == w and b == u):
        return True
    if strictly_inside_seg(a, b, u) or strictly_inside_seg(a, b, w) or \
       strictly_inside_seg(u, w, a) or strictly_inside_seg(u, w, b):
        return True
    d1, d2 = orient(a, b, u), orient(a, b, w)
    d3, d4 = orient(u, w, a), orient(u, w, b)
    return d1 * d2 < 0 and d3 * d4 < 0


def in_cone(prev, cur, nxt, t):
    """t strictement dans le coin interieur (domaine a gauche des aretes prev->cur->nxt)."""
    if orient(prev, cur, nxt) >= 0:
        return orient(prev, cur, t) > 0 and orient(cur, nxt, t) > 0
    return orient(prev, cur, t) > 0 or orient(cur, nxt, t) > 0


TOL_B = 0   # tolerance de convexite pour les FUSIONS, en unites Build entieres (0 = exact) ; --tol-u


def is_convex(pts, tol=None):
    """Polygone convexe oriente positivement ; sommets colineaires admis, pics refuses, simple.
    tol (unites Build, entier) > 0 : un sommet peut depasser la droite d'une arete de <= tol (test entier
    exact o^2 <= tol^2 * |ab|^2)."""
    tol = TOL_B if tol is None else tol
    n = len(pts)
    if n < 3 or len(set(pts)) != n or area2(pts) <= 0:
        return False
    turn = 0.0
    for k in range(n):
        a, b, c = pts[k - 1], pts[k], pts[(k + 1) % n]
        o = orient(a, b, c)
        dot = (b[0] - a[0]) * (c[0] - b[0]) + (b[1] - a[1]) * (c[1] - b[1])
        if o < 0 and tol == 0:
            return False
        if o <= 0 and dot <= 0:
            return False
        turn += math.atan2(o, dot)
    if abs(turn - 2 * math.pi) >= 1e-6:
        return False
    if tol:
        for k in range(n):
            a, b = pts[k], pts[(k + 1) % n]
            L2 = (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2
            for v in pts:
                o = orient(a, b, v)
                if o < 0 and o * o > tol * tol * L2:
                    return False
    return True


def convex_violation_u(pts):
    """Plus grand depassement (u Saturn) d'un sommet du cote exterieur d'une arete (<= 0 si convexe)."""
    worst = -math.inf
    n = len(pts)
    for k in range(n):
        a, b = pts[k], pts[(k + 1) % n]
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        for v in pts:
            o = orient(a, b, v)
            worst = max(worst, -o / L / BU)
    return worst


def point_in_loop(p, pts):
    """1 dedans, 0 dehors, 2 sur le bord (entier exact, pair-impair)."""
    px, py = p
    c = False
    n = len(pts)
    for k in range(n):
        a, b = pts[k], pts[(k + 1) % n]
        if orient(a, b, p) == 0 and min(a[0], b[0]) <= px <= max(a[0], b[0]) and \
                min(a[1], b[1]) <= py <= max(a[1], b[1]):
            return 2
        if (a[1] > py) != (b[1] > py):
            lhs = (b[0] - a[0]) * (py - a[1]); rhs = (px - a[0]) * (b[1] - a[1])
            if (lhs > rhs) if b[1] > a[1] else (lhs < rhs):
                c = not c
    return 1 if c else 0


def in_convex(p, pts):
    """Point dans un polygone convexe (bord inclus)."""
    n = len(pts)
    return all(orient(pts[k], pts[(k + 1) % n], p) >= 0 for k in range(n))


def dist_point_seg(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / L))
    return math.hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy))


def min_angle(a, b, c):
    def ang(p, q, r):
        v1 = (q[0] - p[0], q[1] - p[1]); v2 = (r[0] - p[0], r[1] - p[1])
        return abs(math.atan2(v1[0] * v2[1] - v1[1] * v2[0], v1[0] * v2[0] + v1[1] * v2[1]))
    return min(ang(a, b, c), ang(b, c, a), ang(c, a, b))


def canon_line(a, b):
    A, B = b[1] - a[1], a[0] - b[0]
    g = math.gcd(A, B)
    A //= g; B //= g
    if A < 0 or (A == 0 and B < 0):
        A, B = -A, -B
    return (A, B, A * a[0] + B * a[1])


# ------------------------------------------------------------------------------------------
# etiquettes d'aretes : ('w', wall_id) = mur Build (E1) ; ('d', sector_id, n) = diagonale/pont interne
# ------------------------------------------------------------------------------------------
class Ctx:
    def __init__(self, sectors, walls):
        self.S = sectors
        self.W = walls

    def twin_label(self, lab):
        if lab[0] == "d":
            return lab
        nw = self.W[lab[1]]["nextwall"]
        return ("w", nw) if nw >= 0 else None

    def is_internal(self, lab):
        return lab[0] == "d" or self.W[lab[1]]["nextwall"] >= 0


# ------------------------------------------------------------------------------------------
# union de deux (ou plusieurs) polygones par leurs aretes jumelles
# ------------------------------------------------------------------------------------------
def union(ctx, A, B, shared_ok, check_convex=True):
    """A, B : listes de coins (pt, lab) ; lab = etiquette de l'arete pt -> pt suivant.
    Retourne le cycle de l'union (liste de coins) ou None (pas d'arete commune, arete commune non
    fusionnable, union non simple ou non convexe)."""
    na, nb = len(A), len(B)
    Ea = [(A[k][0], A[(k + 1) % na][0], A[k][1]) for k in range(na)]
    Eb = [(B[k][0], B[(k + 1) % nb][0], B[k][1]) for k in range(nb)]
    bl = defaultdict(list)
    for j, e in enumerate(Eb):
        bl[e[2]].append(j)
    ra, rb = set(), set()
    for k, (u, v, l) in enumerate(Ea):
        tl = ctx.twin_label(l)
        if tl is None:
            continue
        for j in bl.get(tl, ()):
            if Eb[j][0] == v and Eb[j][1] == u and j not in rb:
                if not shared_ok(l):
                    return None
                ra.add(k); rb.add(j)
                break
    if not ra:
        return None
    rest = [e for k, e in enumerate(Ea) if k not in ra] + [e for j, e in enumerate(Eb) if j not in rb]
    nxt = {}
    for e in rest:
        if e[0] in nxt:
            return None
        nxt[e[0]] = e
    cyc, cur = [], rest[0]
    while True:
        cyc.append((cur[0], cur[2]))
        cur = nxt.get(cur[1])
        if cur is None or len(cyc) > len(rest):
            return None
        if cur is rest[0]:
            break
    if len(cyc) != len(rest):
        return None
    if check_convex and not is_convex([c[0] for c in cyc]):
        return None
    return cyc


# ------------------------------------------------------------------------------------------
# ponts vers les trous
# ------------------------------------------------------------------------------------------
def bridge_holes(outer, holes, diag_ctr, sid, rng=None):
    """outer/holes : listes de coins (pt, lab). Retourne un polygone faiblement simple unique.
    Le pont est etiquete ('d', sid, n) dans les deux sens (c'est une diagonale interne).
    rng : si fourni, le pont est tire parmi les 3 candidats valides les plus proches."""
    P = list(outer)
    remaining = [list(h) for h in holes]
    order = sorted(range(len(remaining)), key=lambda i: (-max(c[0][0] for c in remaining[i]),
                                                          -max(c[0][1] for c in remaining[i]), i))
    done = set()
    bridges = []
    for hi in order:
        H = remaining[hi]
        mi = max(range(len(H)), key=lambda k: (H[k][0][0], H[k][0][1]))
        M, Mp, Mn = H[mi][0], H[mi - 1][0], H[(mi + 1) % len(H)][0]
        edges = [(P[k][0], P[(k + 1) % len(P)][0]) for k in range(len(P))]
        for hj in range(len(remaining)):
            if hj not in done:
                R = remaining[hj]
                edges += [(R[k][0], R[(k + 1) % len(R)][0]) for k in range(len(R))]
        cands = sorted(range(len(P)), key=lambda k: ((P[k][0][0] - M[0]) ** 2 + (P[k][0][1] - M[1]) ** 2, k))
        valid = []
        for vi in cands:
            V = P[vi][0]
            if V == M:
                continue
            if not in_cone(P[vi - 1][0], V, P[(vi + 1) % len(P)][0], M):
                continue
            if not in_cone(Mp, M, Mn, V):
                continue
            if any(segs_conflict(V, M, u, w) for u, w in edges):
                continue
            valid.append(vi)
            if rng is None or len(valid) >= 3:
                break
        chosen = (valid[0] if rng is None else rng.choice(valid)) if valid else None
        if chosen is None:
            raise RuntimeError(f"secteur {sid} : aucun pont valide pour le trou {hi}")
        b = ("d", sid, next(diag_ctr))
        bridges.append(b)
        vi = chosen
        V = P[vi][0]
        hole_seq = H[mi:] + H[:mi]
        P = P[:vi] + [(V, b)] + hole_seq + [(M, b)] + [(V, P[vi][1])] + P[vi + 1:]
        done.add(hi)
    return P, bridges


# ------------------------------------------------------------------------------------------
# triangulation par oreilles (exacte, polygone faiblement simple)
# ------------------------------------------------------------------------------------------
def ear_ok(C, i):
    n = len(C)
    p, c, q = C[i - 1][0], C[i][0], C[(i + 1) % n][0]
    if orient(p, c, q) <= 0 or p == q:
        return False
    if not in_cone(C[i - 2][0], p, c, q):
        return False
    if not in_cone(c, q, C[(i + 2) % n][0], p):
        return False
    for k in range(n):
        if segs_conflict(p, q, C[k][0], C[(k + 1) % n][0]):
            return False
    return True


def triangulate(P, strategy, diag_ctr, sid, rng=None):
    """Retourne une liste de triangles (listes de 3 coins (pt, lab)). strategy = (nom, param) ;
    'rand' tire l'oreille au hasard (rng seme, deterministe) parmi les oreilles valides."""
    C = list(P)
    tris = []
    name, prm = strategy
    start = (prm * len(C)) // 6 if name == "first" else 0
    while len(C) > 3:
        n = len(C)
        pick = None
        if name == "first":
            for s in range(n):
                i = (start + s) % n
                if ear_ok(C, i):
                    pick = i
                    break
        elif name == "rand":
            ears = [i for i in range(n) if ear_ok(C, i)]
            pick = rng.choice(ears) if ears else None
        else:
            best = None
            for i in range(n):
                if not ear_ok(C, i):
                    continue
                p, c, q = C[i - 1][0], C[i][0], C[(i + 1) % n][0]
                if name == "angle":
                    key = (-min_angle(p, c, q), i)
                elif name == "short":
                    key = ((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2, i)
                else:  # "long"
                    key = (-((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2), i)
                if best is None or key < best:
                    best, pick = key, i
        if pick is None:
            raise RuntimeError(f"secteur {sid} : plus d'oreille ({n} coins, strategie {strategy})")
        i = pick
        p, c, q = C[i - 1], C[i], C[(i + 1) % n]
        d = ("d", sid, next(diag_ctr))
        tris.append([p, c, (q[0], d)])
        C[i - 1] = (p[0], d)
        del C[i]
        start = (i - 1) % len(C)
    if orient(C[0][0], C[1][0], C[2][0]) <= 0:
        raise RuntimeError(f"secteur {sid} : triangle final degenere")
    tris.append(C)
    return tris


# ------------------------------------------------------------------------------------------
# ensemble de morceaux + fusions
# ------------------------------------------------------------------------------------------
class PieceSet:
    def __init__(self, ctx):
        self.ctx = ctx
        self.P = {}          # pid -> coins
        self.meta = {}       # pid -> {"sectors": set, "a2": {sid: aire2}}
        self.owner = defaultdict(set)
        self.nid = 0

    def add(self, corners, meta):
        pid = self.nid
        self.nid += 1
        self.P[pid] = corners
        self.meta[pid] = meta
        for _, l in corners:
            self.owner[l].add(pid)
        return pid

    def remove(self, pid):
        for _, l in self.P[pid]:
            self.owner[l].discard(pid)
        del self.P[pid]
        return self.meta.pop(pid)

    def replace(self, pids, corners):
        metas = [self.remove(p) for p in pids]
        m = {"sectors": set(), "a2": Counter()}
        for mm in metas:
            m["sectors"] |= mm["sectors"]
            m["a2"].update(mm["a2"])
        return self.add(corners, m)

    def neighbors(self, pid, ok_pair):
        """Voisins par une arete jumelle fusionnable -> {pid: longueur commune}."""
        out = Counter()
        for k, (pt, l) in enumerate(self.P[pid]):
            tl = self.ctx.twin_label(l)
            if tl is None:
                continue
            nxt = self.P[pid][(k + 1) % len(self.P[pid])][0]
            for q in self.owner.get(tl, ()):
                if q != pid and ok_pair(pid, q, l):
                    out[q] += math.hypot(nxt[0] - pt[0], nxt[1] - pt[1])
        return out


def hm_and_greedy(ps, ok_pair, hm_order=None, triples=True, stats=None):
    """Hertel-Mehlhorn sur les etiquettes hm_order (diagonales), puis fusion gloutonne (paires puis
    triplets) jusqu'a stabilite. ok_pair(pa, pb, lab) dit si l'arete commune lab est fusionnable."""
    ctx = ps.ctx
    st = stats if stats is not None else Counter()

    def shared_ok_for(pa, pb):
        return lambda l: ok_pair(pa, pb, l)

    if hm_order:
        for lab in hm_order:
            ow = sorted(ps.owner.get(lab, ()))
            if len(ow) != 2:
                continue
            a, b = ow
            if not ok_pair(a, b, lab):
                continue
            U = union(ctx, ps.P[a], ps.P[b], shared_ok_for(a, b))
            if U:
                ps.replace([a, b], U)
                st["hm"] += 1
    changed = True
    while changed:
        changed = False
        cands = []
        for a in sorted(ps.P):
            for b, L in ps.neighbors(a, ok_pair).items():
                if a < b:
                    cands.append((-L, a, b))
        cands.sort()
        for _, a, b in cands:
            if a not in ps.P or b not in ps.P:
                continue
            U = union(ctx, ps.P[a], ps.P[b], shared_ok_for(a, b))
            if U:
                ps.replace([a, b], U)
                st["paires"] += 1
                changed = True
        if changed or not triples:
            continue
        # triplets : A + deux voisins, ou chaine A-B-C
        for a in sorted(ps.P):
            if a not in ps.P:
                continue
            na = sorted(ps.neighbors(a, ok_pair))
            trip = []
            for i1 in range(len(na)):
                for i2 in range(i1 + 1, len(na)):
                    trip.append((a, na[i1], na[i2]))
                for c in sorted(ps.neighbors(na[i1], ok_pair)):
                    if c != a and c not in na:
                        trip.append((a, na[i1], c))
            for x, y, z in trip:
                if not (x in ps.P and y in ps.P and z in ps.P):
                    continue
                U1 = union(ctx, ps.P[x], ps.P[y], shared_ok_for(x, y), check_convex=False)
                if not U1:
                    continue
                # z doit etre voisin de x ou de y : ok_pair teste sur le couple (x|y, z)
                def ok3(l, x=x, y=y, z=z):
                    return ok_pair(x, z, l) or ok_pair(y, z, l)
                U = union(ctx, U1, ps.P[z], ok3)
                if U:
                    ps.replace([x, y, z], U)
                    st["triplets"] += 1
                    changed = True
                    break
            if changed:
                break
    return st


# ------------------------------------------------------------------------------------------
# decoupe d'un secteur
# ------------------------------------------------------------------------------------------
STRATEGIES = [("first", 0), ("first", 2), ("first", 4), ("angle", 0), ("short", 0), ("long", 0)]
N_RANDOM = 64   # essais aleatoires semes (sid*100003 + t) si la borne basse du secteur n'est pas atteinte


def sector_loops_corners(sec, W, stats):
    loops = []
    for L in sec["loops"]:
        pts = [(W[w]["x"], W[w]["y"]) for w in L]
        corners = []
        for k, w in enumerate(L):
            if pts[k] == pts[(k + 1) % len(L)]:
                stats["murs_longueur_nulle"] += 1
                continue
            corners.append((pts[k], ("w", w)))
        loops.append(corners)
    outer = [c for c in loops if area2([p for p, _ in c]) > 0]
    holes = [c for c in loops if area2([p for p, _ in c]) < 0]
    if len(outer) != 1 or len(outer) + len(holes) != len(loops):
        raise RuntimeError(f"secteur {sec['id']} : {len(outer)} exterieure(s)")
    return outer[0], holes


def reflex_count(sec, W):
    r = 0
    for L in sec["loops"]:
        P = [(W[w]["x"], W[w]["y"]) for w in L]
        n = len(P)
        r += sum(1 for k in range(n) if orient(P[k - 1], P[k], P[(k + 1) % n]) < 0)
    return r


def decompose_sector(ctx, sec, stats):
    """Retourne (liste de morceaux [coins], info). Essaie toutes les strategies, garde la meilleure."""
    sid = sec["id"]
    outer, holes = sector_loops_corners(sec, ctx.W, stats)
    opts = [p for p, _ in outer]
    if not holes and is_convex(opts):
        return [outer], {"strategie": "deja_convexe", "essais": {}}
    results = {}
    best = None
    lb = max(1, -(-reflex_count(sec, ctx.W) // 2) + 1 - (len(sec["loops"]) - 1))
    trials = [(s, hm, None) for s in STRATEGIES for hm in ("creation", "longues")]
    trials += [(("rand", t), "creation", t) for t in range(N_RANDOM)]
    for strat, hm, seed in trials:
        if seed is not None and best is not None and best[0][0] <= lb:
            break                                   # borne basse atteinte : inutile de chercher
        if True:
            rng = random.Random(sid * 100003 + seed) if seed is not None else None
            ctr = iter(range(10 ** 9))
            P, bridges = bridge_holes(outer, holes, ctr, sid, rng)
            tris = triangulate(P, strat, ctr, sid, rng)
            ps = PieceSet(ctx)
            for t in tris:
                ps.add(list(t), {"sectors": {sid}, "a2": Counter({sid: area2([p for p, _ in t])})})
            labs = sorted({l for t in tris for _, l in t if l[0] == "d"}, key=lambda l: l[2])
            if hm == "longues":
                def L_of(l):
                    for t in tris:
                        for k, (p, ll) in enumerate(t):
                            if ll == l:
                                q = t[(k + 1) % 3][0]
                                return (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2
                    return 0
                labs = sorted(labs, key=lambda l: (-L_of(l), l[2]))
            hm_and_greedy(ps, lambda a, b, l: l[0] == "d", hm_order=labs)
            pieces = [ps.P[k] for k in sorted(ps.P)]
            mina = min(area2([p for p, _ in pc]) for pc in pieces)
            key = (len(pieces), -mina)
            name = f"{strat[0]}{strat[1] if strat[0] in ('first', 'rand') else ''}/{hm}"
            if seed is None:
                results[name] = len(pieces)
            else:
                results["rand_min"] = min(results.get("rand_min", 10 ** 9), len(pieces))
                results["rand_essais"] = results.get("rand_essais", 0) + 1
            if best is None or key < best[0]:
                best = (key, name, pieces, len(bridges), len(tris))
    (_, name, pieces, nb, nt) = best
    return pieces, {"strategie": name, "essais": results, "ponts": nb, "triangles": nt}


# ------------------------------------------------------------------------------------------
# identite de secteurs (fusion inter-secteurs)
# ------------------------------------------------------------------------------------------
def plane_equal(s, t, pre):
    """Plans sol (pre='floor') ou plafond egaux en exact (formule getzsofslope sans troncature)."""
    zs, zt = s[pre + "z"], t[pre + "z"]
    hs = s[pre + "heinum"] if s[pre + "stat"] & 2 else 0
    ht = t[pre + "heinum"] if t[pre + "stat"] & 2 else 0
    if hs == 0 and ht == 0:
        return zs == zt
    if hs == 0 or ht == 0 or abs(hs) != abs(ht):
        return False
    x1, y1, x1b, y1b = s["slope_ref"]; x2, y2, x2b, y2b = t["slope_ref"]
    d1 = (x1b - x1, y1b - y1); d2 = (x2b - x2, y2b - y2)
    if d1[0] * d2[1] - d1[1] * d2[0] != 0:
        return False
    if hs * ht * (d1[0] * d2[0] + d1[1] * d2[1]) <= 0:
        return False
    # z_s(x2,y2) == zt  <=>  (zt - zs)*256*len1 == hs*(dx1*(y2-y1) - dy1*(x2-x1))
    K = (zt - zs) * 256
    Lc = hs * (d1[0] * (y2 - y1) - d1[1] * (x2 - x1))
    if K == 0:
        return Lc == 0
    return (K > 0) == (Lc > 0) and K * K * (d1[0] ** 2 + d1[1] ** 2) == Lc * Lc


SURF_KEYS = ("stat", "picnum", "shade", "pal", "xpanning", "ypanning")


def sectors_identical(s, t):
    if s["lotag"] != t["lotag"] or s["hitag"] != t["hitag"]:
        return False
    for pre in ("floor", "ceiling"):
        for k in SURF_KEYS:
            if s[pre + k] != t[pre + k]:
                return False
        if not plane_equal(s, t, pre):
            return False
    return True


def wall_mergeable(W, wid):
    w = W[wid]
    nw = w["nextwall"]
    if nw < 0:
        return False
    for x in (w, W[nw]):
        if x["cstat"] & NOMERGE_WALL_CSTAT or x["lotag"] or x["hitag"]:
            return False
    return True


# ------------------------------------------------------------------------------------------
# jonctions en T
# ------------------------------------------------------------------------------------------
def scan_overlaps(ctx, pieces):
    """Recouvrements colineaires de longueur > 0 entre aretes de morceaux differents.
    Retourne (liste des recouvrements opposes non exacts, compteurs)."""
    buckets = defaultdict(list)
    for pid, C in pieces.items():
        n = len(C)
        for k in range(n):
            a, b = C[k][0], C[(k + 1) % n][0]
            A, B, _ = key = canon_line(a, b)
            ta, tb = -B * a[0] + A * a[1], -B * b[0] + A * b[1]
            buckets[key].append((min(ta, tb), max(ta, tb), 1 if tb > ta else -1, pid, k, a, b, C[k][1]))
    bad = []
    cnt = Counter()
    for key, es in buckets.items():
        if len(es) < 2:
            continue
        es.sort()
        for i in range(len(es)):
            lo1, hi1, s1, p1, k1, a1, b1, l1 = es[i]
            for j in range(i + 1, len(es)):
                lo2, hi2, s2, p2, k2, a2, b2, l2 = es[j]
                if lo2 >= hi1:
                    break
                if p1 == p2 or max(lo1, lo2) >= min(hi1, hi2):
                    continue
                if s1 == s2:
                    cnt["meme_sens_superposes"] += 1
                    continue
                exact = a1 == b2 and b1 == a2
                twin = ctx.twin_label(l1) == l2
                i1, i2 = ctx.is_internal(l1), ctx.is_internal(l2)
                if exact and twin:
                    cnt["jumelles_exactes"] += 1
                elif exact:
                    cnt["contact_exact_non_apparie_" + ("plein_plein" if not (i1 or i2) else "avec_interne")] += 1
                else:
                    kind = "plein_plein" if not (i1 or i2) else "avec_interne"
                    cnt["T_" + kind] += 1
                    bad.append((kind, (p1, k1, a1, b1, l1), (p2, k2, a2, b2, l2)))
    return bad, cnt


def split_edges(pieces, cuts):
    """cuts : {(pid, k): set(points)} -> insere les points (colineaires) dans l'arete k du morceau."""
    for pid in sorted({p for p, _ in cuts}):
        C = pieces[pid]
        out = []
        for k, (pt, lab) in enumerate(C):
            out.append((pt, lab))
            pts = cuts.get((pid, k))
            if pts:
                b = C[(k + 1) % len(C)][0]
                for q in sorted(pts, key=lambda q: (q[0] - pt[0]) ** 2 + (q[1] - pt[1]) ** 2):
                    out.append((q, lab))
        pieces[pid] = out


def fix_t_junctions(ctx, pieces, split_solid):
    total = 0
    for _ in range(20):
        bad, cnt = scan_overlaps(ctx, pieces)
        cuts = defaultdict(set)
        for kind, e1, e2 in bad:
            if kind == "plein_plein" and not split_solid:
                continue
            for (p, k, a, b, l), (_, _, c, d, _) in ((e1, e2), (e2, e1)):
                for q in (c, d):
                    if strictly_inside_seg(a, b, q):
                        cuts[(p, k)].add(q)
        if not cuts:
            return total, bad, cnt
        total += sum(len(v) for v in cuts.values())
        split_edges(pieces, cuts)
    bad, cnt = scan_overlaps(ctx, pieces)
    return total, bad, cnt


# ------------------------------------------------------------------------------------------
# comparaison HOLYWOOD (conteneurs gen2 dans l'emprise des secteurs gardes)
# ------------------------------------------------------------------------------------------
def holywood_containers():
    src = open(GEN2, encoding="utf-8").read().split("AGG = {}")[0]
    ns = {"__name__": "gen2_lev"}
    saved = sys.stdout
    try:
        exec(compile(src, GEN2, "exec"), ns)
    finally:
        sys.stdout = saved
    b, h, L, _ = ns["parse"](HOLYWOOD, "duke")
    S2 = ns["S2"]
    out = []
    for i in range(h["cont"]):
        o = L["cont"] + 28 * i
        out.append((S2(b, o + 4), S2(b, o + 8), S2(b, o + 10)))
    return out


def floor_y_range(sec, W):
    ys = []
    for L in sec["loops"]:
        for w in L:
            ys.append(-slope_z_exact(sec, "floor", W[w]["x"], W[w]["y"]) / 128)
    return min(ys), max(ys)


def slope_z_exact(sec, pre, x, y):
    z = sec[pre + "z"]
    if not sec[pre + "stat"] & 2:
        return z
    x0, y0, x1, y1 = sec["slope_ref"]
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    if L == 0:
        return z
    return z + sec[pre + "heinum"] * (dx * (y - y0) - dy * (x - x0)) / (256 * L)


# ------------------------------------------------------------------------------------------
# ecriture
# ------------------------------------------------------------------------------------------
def atomic_write(path, data: bytes):
    d = os.path.dirname(os.path.abspath(path))
    os.makedirs(d, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=d, prefix=".tmp_", suffix=os.path.basename(path))
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


def dump_json(obj, rows=("pieces", "secteurs")):
    parts = ["{"]
    keys = list(obj.keys())
    for k_i, k in enumerate(keys):
        v = obj[k]
        comma = "," if k_i < len(keys) - 1 else ""
        if k in rows and isinstance(v, list):
            parts.append(f' {json.dumps(k)}: [')
            for r_i, r in enumerate(v):
                parts.append("  " + json.dumps(r, ensure_ascii=False, separators=(",", ":")) +
                             ("," if r_i < len(v) - 1 else ""))
            parts.append(" ]" + comma)
        else:
            body = json.dumps(v, ensure_ascii=False, indent=1).replace("\n", "\n ")
            parts.append(f" {json.dumps(k)}: {body}{comma}")
    parts.append("}")
    return ("\n".join(parts) + "\n").encode("utf-8")


def draw_png(path, pieces_out, start_xy):
    from PIL import Image, ImageDraw
    xs = [v[0] for p in pieces_out for v in p["verts"]]
    ys = [v[1] for p in pieces_out for v in p["verts"]]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    pad = 256
    sc = 2000 / max(x1 - x0 + 2 * pad, y1 - y0 + 2 * pad)
    Wp, Hp = int((x1 - x0 + 2 * pad) * sc) + 1, int((y1 - y0 + 2 * pad) * sc) + 1
    img = Image.new("RGB", (Wp, Hp), (255, 255, 255))
    dr = ImageDraw.Draw(img)

    def P(v):
        return ((v[0] - x0 + pad) * sc, (v[1] - y0 + pad) * sc)
    for p in pieces_out:
        h = int(hashlib.md5(str(p["id"]).encode()).hexdigest()[:6], 16)
        col = (170 + (h & 63), 170 + ((h >> 6) & 63), 170 + ((h >> 12) & 63))
        if len(p["sectors_build"]) > 1:
            col = (255, 215, 120)
        dr.polygon([P(v) for v in p["verts"]], fill=col)
    order = {"diagonale": 0, "portail": 1, "mur": 2}
    colr = {"diagonale": (150, 150, 150), "portail": (30, 90, 230), "mur": (0, 0, 0)}
    segs = sorted(((order[e["type"]], p["verts"][k], p["verts"][(k + 1) % len(p["verts"])], e["type"])
                   for p in pieces_out for k, e in enumerate(p["edges"])), key=lambda s: s[0])
    for _, a, b, t in segs:
        dr.line([P(a), P(b)], fill=colr[t], width=1)
    sx, sy = P(start_xy)
    dr.ellipse([sx - 6, sy - 6, sx + 6, sy + 6], outline=(220, 0, 0), width=3)
    dr.text((10, 10), "E2 e1l1_convex : morceaux (couleurs), mur noir, portail bleu, diagonale gris, "
                      "orange = fusion inter-secteurs, cercle rouge = depart M1", fill=(0, 0, 0))
    buf = __import__("io").BytesIO()
    img.save(buf, "PNG")
    atomic_write(path, buf.getvalue())


# ------------------------------------------------------------------------------------------
# auto-test synthetique
# ------------------------------------------------------------------------------------------
def selftest():
    """Cas synthetiques : carre troue, peigne, colineaires, deux secteurs identiques, T-jonction."""
    ok = True

    def mk_walls(loops_per_sector):
        S, W = [], []
        for sid, loops in enumerate(loops_per_sector):
            ids = []
            for L in loops:
                base = len(W)
                for k, p in enumerate(L):
                    W.append({"id": base + k, "sector": sid, "x": p[0], "y": p[1], "point2": base + (k + 1) % len(L),
                              "nextwall": -1, "nextsector": -1, "cstat": 0, "lotag": 0, "hitag": 0})
                ids.append(list(range(base, base + len(L))))
            sec = {"id": sid, "loops": ids, "lotag": 0, "hitag": 0, "slope_ref": [0, 0, 1, 0]}
            for pre in ("floor", "ceiling"):
                sec.update({pre + "z": 0 if pre == "floor" else -8192, pre + "stat": 0, pre + "heinum": 0,
                            pre + "picnum": 1, pre + "shade": 0, pre + "pal": 0, pre + "xpanning": 0,
                            pre + "ypanning": 0})
            S.append(sec)
        # jumelles par coordonnees
        idx = {}
        for w in W:
            idx[(w["x"], w["y"], W[w["point2"]]["x"], W[w["point2"]]["y"])] = w["id"]
        for w in W:
            k = (W[w["point2"]]["x"], W[w["point2"]]["y"], w["x"], w["y"])
            if k in idx:
                w["nextwall"] = idx[k]; w["nextsector"] = W[idx[k]]["sector"]
        return S, W

    cases = {
        # exterieure positive (Build), trou negatif
        "carre_troue": [[[(0, 0), (0, 100), (100, 100), (100, 0)][::-1][::-1],
                         [(40, 40), (60, 40), (60, 60), (40, 60)]]],
        "peigne": [[[(0, 0), (0, 10), (10, 10), (10, 2), (20, 2), (20, 10), (30, 10), (30, 2), (40, 2),
                     (40, 10), (50, 10), (50, 0)]]],
        "colineaires": [[[(0, 0), (0, 10), (0, 20), (10, 20), (20, 20), (20, 10), (10, 10), (10, 0), (5, 0)]]],
    }
    for name, loops in cases.items():
        # orienter : exterieure > 0, trous < 0
        fixed = []
        for i, L in enumerate(loops[0]):
            a = area2(L)
            if (i == 0) != (a > 0):
                L = L[::-1]
            fixed.append(L)
        S, W = mk_walls([fixed])
        ctx = Ctx(S, W)
        pcs, info = decompose_sector(ctx, S[0], Counter())
        a_tot = sum(area2([p for p, _ in c]) for c in pcs)
        a_ref = sum(area2(L) for L in fixed)
        good = all(is_convex([p for p, _ in c]) for c in pcs) and a_tot == a_ref
        print(f"  selftest {name:12s} : {len(pcs)} morceaux, aire {a_tot}/{a_ref}, convexes "
              f"{'OK' if good else 'ECHEC'} ({info['strategie']})")
        ok &= good
    # deux secteurs identiques cote a cote -> fusion en 1 ; T-jonction : un 3e morceau voisin d'un
    # mur coupe differemment
    A = [(0, 0), (0, 10), (10, 10), (10, 0)]
    A = A if area2(A) > 0 else A[::-1]
    B = [(10, 0), (10, 10), (20, 10), (20, 0)]
    B = B if area2(B) > 0 else B[::-1]
    S, W = mk_walls([[A], [B]])
    ctx = Ctx(S, W)
    ps = PieceSet(ctx)
    for s in S:
        o, _ = sector_loops_corners(s, W, Counter())
        ps.add(o, {"sectors": {s["id"]}, "a2": Counter({s["id"]: area2([p for p, _ in o])})})
    hm_and_greedy(ps, lambda a, b, l: l[0] == "w" and wall_mergeable(W, l[1]) and
                  sectors_identical(S[W[l[1]]["sector"]], S[W[W[l[1]]["nextwall"]]["sector"]]))
    good = len(ps.P) == 1 and is_convex([p for p, _ in next(iter(ps.P.values()))])
    print(f"  selftest fusion_2sect  : {len(ps.P)} morceau(x) {'OK' if good else 'ECHEC'}")
    ok &= good
    # T : morceau X = [0,10]x[0,10] ; Y et Z de l'autre cote du mur x=10 coupe en y=4 ; aretes internes
    X = [((0, 0), ("d", 0, 1)), ((0, 10), ("d", 0, 2)), ((10, 10), ("d", 0, 3)), ((10, 0), ("d", 0, 4))]
    if area2([p for p, _ in X]) < 0:
        X = [(X[(k + 1) % 4][0], X[k][1]) for k in range(4)][::-1]
    pieces = {0: X,
              1: [((10, 0), ("d", 9, 1)), ((10, 4), ("d", 9, 2)), ((20, 4), ("d", 9, 3)), ((20, 0), ("d", 9, 4))],
              2: [((10, 4), ("d", 9, 5)), ((10, 10), ("d", 9, 6)), ((20, 10), ("d", 9, 7)), ((20, 4), ("d", 9, 2))]}
    for k in (1, 2):
        if area2([p for p, _ in pieces[k]]) < 0:
            C = pieces[k]
            pieces[k] = [(C[(j + 1) % 4][0], C[j][1]) for j in range(4)][::-1]
    n0, bad0, _ = fix_t_junctions(ctx, pieces, False)
    good = n0 >= 1 and not bad0 and len(pieces[0]) == 5 and is_convex([p for p, _ in pieces[0]])
    print(f"  selftest T_jonction    : {n0} coupe(s), restantes {len(bad0)} {'OK' if good else 'ECHEC'}")
    ok &= good
    return ok


# ------------------------------------------------------------------------------------------
# principal
# ------------------------------------------------------------------------------------------
def main(argv=None):
    global TOL_B, N_RANDOM
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--in", dest="inp", default=IN_DEFAULT)
    ap.add_argument("--out", default=OUT_DEFAULT)
    ap.add_argument("--png", default=PNG_DEFAULT)
    ap.add_argument("--no-png", action="store_true")
    ap.add_argument("--no-fusion-secteurs", action="store_true",
                    help="ne pas fusionner les morceaux de secteurs Build voisins identiques")
    ap.add_argument("--split-solid-t", action="store_true",
                    help="scinder aussi les recouvrements en T entre murs PLEINS dos a dos")
    ap.add_argument("--no-holywood", action="store_true")
    ap.add_argument("--tol-u", type=float, default=0.0,
                    help="tolerance de convexite des FUSIONS en u Saturn (defaut 0 = exact ; max 0.5 = critere)")
    ap.add_argument("--random", type=int, default=N_RANDOM, help="essais aleatoires par secteur (defaut 64)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    TOL_B = int(round(args.tol_u * BU))
    if TOL_B > CONVEX_TOL_U * BU:
        ap.error("--tol-u au-dela du critere de 0.5 u")
    N_RANDOM = args.random
    sys.stdout.reconfigure(encoding="utf-8")
    t0 = time.time()
    if args.selftest:
        return 0 if selftest() else 1

    raw = open(args.inp, "rb").read()
    d = json.loads(raw)
    S, W = d["sectors"], d["walls"]
    ctx = Ctx(S, W)
    stats = Counter()
    print(f"E2 decoupe convexe : entree {os.path.relpath(args.inp, ROOT)} ({d['format']}, sha1 "
          f"{hashlib.sha1(raw).hexdigest()[:12]}) : {len(S)} secteurs, {len(W)} murs")

    # ---- 1-4 : decoupe par secteur
    ps = PieceSet(ctx)
    sinfo = {}
    for sec in S:
        pcs, info = decompose_sector(ctx, sec, stats)
        info["reflexes"] = reflex_count(sec, W)
        info["trous"] = len(sec["loops"]) - 1
        info["morceaux_intra"] = len(pcs)
        sinfo[sec["id"]] = info
        for c in pcs:
            ps.add(c, {"sectors": {sec["id"]}, "a2": Counter({sec["id"]: area2([p for p, _ in c])})})
    n_intra = len(ps.P)
    lb = sum(max(1, -(-i["reflexes"] // 2) + 1 - i["trous"]) for i in sinfo.values())
    ub = sum(max(1, i["reflexes"] + 1 - i["trous"]) for i in sinfo.values())
    strat_use = Counter(i["strategie"] for i in sinfo.values())
    print(f"  intra-secteur : {n_intra} morceaux ({time.time() - t0:.1f} s) ; bornes sur les {len(S)} secteurs "
          f"gardes : ceil(r/2)+1-h = {lb}, r+1-h = {ub} ; strategies retenues {dict(strat_use.most_common())}")

    # ---- 5 : fusion inter-secteurs
    fstats = Counter()
    ident_cache = {}

    def ident(a, b):
        k = (min(a, b), max(a, b))
        if k not in ident_cache:
            ident_cache[k] = sectors_identical(S[a], S[b])
        return ident_cache[k]

    def ok_pair_global(pa, pb, lab):
        if lab[0] == "d":
            return True
        if not wall_mergeable(W, lab[1]):
            return False
        sa = min(ps.meta[pa]["sectors"]); sb = min(ps.meta[pb]["sectors"])
        return ident(sa, sb)

    if not args.no_fusion_secteurs:
        hm_and_greedy(ps, ok_pair_global, stats=fstats)
    n_after = len(ps.P)
    n_multi = sum(1 for m in ps.meta.values() if len(m["sectors"]) > 1)
    # paires de secteurs voisins identiques (information)
    ident_pairs = set()
    for w in W:
        if w["nextwall"] >= 0 and w["sector"] < w["nextsector"] and wall_mergeable(W, w["id"]) and \
                ident(w["sector"], w["nextsector"]):
            ident_pairs.add((w["sector"], w["nextsector"]))
    print(f"  fusion inter-secteurs {'OFF' if args.no_fusion_secteurs else 'ON'} : {n_intra} -> {n_after} morceaux "
          f"({n_multi} morceaux multi-secteurs ; {len(ident_pairs)} paires de secteurs voisins identiques ; "
          f"fusions {dict(fstats)})")

    # ---- 6 : jonctions en T
    pieces = dict(ps.P)
    n_cuts, bad_t, ov_cnt = fix_t_junctions(ctx, pieces, args.split_solid_t)
    t_internal = [b for b in bad_t if b[0] == "avec_interne"]
    t_solid = [b for b in bad_t if b[0] == "plein_plein"]

    # ---- sortie : ordre deterministe, rotation au plus petit sommet
    order = sorted(pieces, key=lambda p: (min(ps.meta[p]["sectors"]), min(c[0] for c in pieces[p])))
    newid = {p: i for i, p in enumerate(order)}
    out_pieces = []
    edge_idx = {}
    for p in order:
        C = pieces[p]
        r = min(range(len(C)), key=lambda k: C[k][0])
        C = C[r:] + C[:r]
        pieces[p] = C
        n = len(C)
        for k in range(n):
            edge_idx[(C[k][1], C[k][0], C[(k + 1) % n][0])] = (newid[p], k)
    twin_fail = []
    n_int_edges = 0
    etypes = Counter()
    for p in order:
        C = pieces[p]
        n = len(C)
        edges = []
        for k in range(n):
            a, b, lab = C[k][0], C[(k + 1) % n][0], C[k][1]
            tl = ctx.twin_label(lab)
            if lab[0] == "d":
                typ = "diagonale"
            elif tl is None:
                typ = "mur"
            else:
                typ = "portail"
            etypes[typ] += 1
            e = {"type": typ}
            if lab[0] == "w":
                w = W[lab[1]]
                e["wall"] = lab[1]
                e["build_wall"] = w["build_id"]
                if typ == "portail":
                    e["nextsector"] = w["nextsector"]
            else:
                e["diag"] = lab[2]
                e["sector"] = lab[1]
            if tl is not None:
                n_int_edges += 1
                tw = edge_idx.get((tl, b, a))
                if tw is None:
                    twin_fail.append((newid[p], k, lab))
                e["twin"] = list(tw) if tw else None
            else:
                e["twin"] = None
            edges.append(e)
        pts = [c[0] for c in C]
        m = ps.meta[p]
        a2 = area2(pts)
        out_pieces.append({
            "id": newid[p],
            "sectors": sorted(m["sectors"]),
            "sectors_build": sorted(S[s]["build_id"] for s in m["sectors"]),
            "area2_build": a2,
            "area2_par_secteur": {str(k): v for k, v in sorted(m["a2"].items())},
            "aire_u2": round(a2 / 2 / BU / BU, 3),
            "verts": [list(v) for v in pts],
            "edges": edges,
        })
    # symetrie des jumelles
    asym = 0
    for pc in out_pieces:
        for k, e in enumerate(pc["edges"]):
            if e["twin"]:
                q, j = e["twin"]
                if out_pieces[q]["edges"][j]["twin"] != [pc["id"], k]:
                    asym += 1

    # ---- criteres
    viol = max(convex_violation_u([tuple(v) for v in pc["verts"]]) for pc in out_pieces)
    not_convex = [pc["id"] for pc in out_pieces
                  if not is_convex([tuple(v) for v in pc["verts"]], tol=int(CONVEX_TOL_U * BU))]
    strict_convex = sum(1 for pc in out_pieces if is_convex([tuple(v) for v in pc["verts"]], tol=0))
    area_sec = Counter()
    for pc in out_pieces:
        for k, v in pc["area2_par_secteur"].items():
            area_sec[int(k)] += v
    area_bad = []
    for s in S:
        ref = s["area2_build"]
        if ref <= 0 or abs(area_sec[s["id"]] - ref) > AREA_TOL * ref:
            area_bad.append((s["id"], area_sec[s["id"]], ref))
    tot_ref = sum(s["area2_build"] for s in S)
    tot_out = sum(pc["area2_build"] for pc in out_pieces)
    area_exact = sum(1 for s in S if area_sec[s["id"]] == s["area2_build"])
    min_area = min(pc["aire_u2"] for pc in out_pieces)
    small = [pc["id"] for pc in out_pieces if pc["area2_build"] / 2 / BU / BU < MIN_AREA_U2]
    # couverture des murs : chaque mur Build garde couvert exactement une fois (longueurs)
    wall_len = defaultdict(float)
    wall_cnt = Counter()
    for pc in out_pieces:
        V = pc["verts"]
        for k, e in enumerate(pc["edges"]):
            if "wall" in e:
                a, b = V[k], V[(k + 1) % len(V)]
                wall_len[e["wall"]] += math.hypot(b[0] - a[0], b[1] - a[1])
                wall_cnt[e["wall"]] += 1
    wall_bad = []
    wall_fused = []
    piece_secs = [set(pc["sectors"]) for pc in out_pieces]
    for w in W:
        b = W[w["point2"]]
        L = math.hypot(b["x"] - w["x"], b["y"] - w["y"])
        if abs(wall_len[w["id"]] - L) <= 1e-6:
            continue
        nw = w["nextwall"]
        if wall_cnt[w["id"]] == 0 and nw >= 0 and wall_cnt[nw] == 0 and \
                any(w["sector"] in ss and w["nextsector"] in ss for ss in piece_secs):
            wall_fused.append(w["id"])          # portail interne a un morceau multi-secteurs (fusion)
        else:
            wall_bad.append(w["id"])
    n = len(out_pieces)

    # ---- depart et sprites
    dep = d["depart_m1"]
    sxy = tuple(dep["build_xy"])
    dep_pieces = [pc["id"] for pc in out_pieces if dep["sector"] in pc["sectors"] and
                  in_convex(sxy, [tuple(v) for v in pc["verts"]])]
    by_sector = defaultdict(list)
    for pc in out_pieces:
        for s in pc["sectors"]:
            by_sector[s].append(pc["id"])
    spr_piece = []
    spr_out = 0
    for i, sp in enumerate(d["sprites"]):
        if sp["sector"] is None:
            spr_piece.append(None)
            continue
        cand = by_sector[sp["sector"]]
        hit = [q for q in cand if in_convex((sp["x"], sp["y"]), [tuple(v) for v in out_pieces[q]["verts"]])]
        if hit:
            spr_piece.append(hit[0])
        else:
            spr_out += 1
            best = min(cand, key=lambda q: min(dist_point_seg((sp["x"], sp["y"]), out_pieces[q]["verts"][k],
                                                              out_pieces[q]["verts"][(k + 1) % len(out_pieces[q]["verts"])])
                                               for k in range(len(out_pieces[q]["verts"]))))
            spr_piece.append(best)

    # ---- secteurs les plus decoupes
    sec_rows = []
    for s in S:
        i = sinfo[s["id"]]
        sec_rows.append({"sector": s["id"], "build_id": s["build_id"], "murs": s["wallnum"],
                         "reflexes": i["reflexes"], "trous": i["trous"],
                         "borne_basse": max(1, -(-i["reflexes"] // 2) + 1 - i["trous"]),
                         "morceaux_intra": i["morceaux_intra"], "morceaux": sorted(by_sector[s["id"]]),
                         "strategie": i["strategie"], "essais": i["essais"]})
    most = sorted(sec_rows, key=lambda r: (-r["morceaux_intra"], r["build_id"]))[:15]

    # ---- HOLYWOOD
    hw = None
    if not args.no_holywood and os.path.exists(HOLYWOOD) and os.path.exists(GEN2):
        try:
            conts = holywood_containers()
            loops_pts = {s["id"]: [[(W[w]["x"], W[w]["y"]) for w in L] for L in s["loops"]] for s in S}
            fyr = {s["id"]: floor_y_range(s, W) for s in S}
            in_fp = 0
            matched = Counter()
            for cx, cz, alt in conts:
                X, Y = cx * 8, -cz * 8
                hits = []
                for s in S:
                    Ls = loops_pts[s["id"]]
                    if point_in_loop((X, Y), Ls[0] if area2(Ls[0]) > 0 else
                                     next(L for L in Ls if area2(L) > 0)) == 0:
                        continue
                    if any(point_in_loop((X, Y), L) == 1 for L in Ls if area2(L) < 0):
                        continue
                    hits.append(s["id"])
                if hits:
                    in_fp += 1
                    ok_alt = [h for h in hits if fyr[h][0] - 8 <= alt <= fyr[h][1] + 8]
                    if ok_alt:
                        matched[min(ok_alt, key=lambda h: abs(alt - (fyr[h][0] + fyr[h][1]) / 2))] += 1
            n_match = sum(matched.values())
            ours_matched = sum(len(by_sector[s]) for s in matched)
            diff = sorted(((len(by_sector[s["id"]]) - matched[s["id"]], s["build_id"], len(by_sector[s["id"]]),
                           matched[s["id"]]) for s in S if matched[s["id"]]), reverse=True)[:10]
            hw = {"conteneurs": len(conts), "centre_dans_emprise_gardee": in_fp,
                  "apparies_avec_altitude_8u": n_match, "secteurs_apparies": len(matched),
                  "nos_morceaux_sur_ces_secteurs": ours_matched,
                  "plus_grands_ecarts_nous_moins_holywood": [
                      {"build_id": b, "nous": o, "holywood": h, "ecart": e} for e, b, o, h in diff],
                  "note": "centre du conteneur (+4,+8 ; X = cx*8, y = -cz*8 Build) teste dans les 276 secteurs "
                          "gardes ; altitude (+10) comparee au Y du sol a 8 u pres (loi alt = -floorz/128)"}
        except Exception as ex:  # information seulement
            hw = {"erreur": repr(ex)}

    crit = {
        "tous_convexes_0_5u": {"ok": viol <= CONVEX_TOL_U and not not_convex,
                               "depassement_max_u": round(max(viol, 0.0), 6), "non_convexes": not_convex,
                               "strictement_convexes_exact": f"{strict_convex}/{len(out_pieces)}",
                               "tolerance_des_fusions_u": TOL_B / BU},
        "aire_par_secteur_0_1pc": {"ok": not area_bad, "hors_tolerance": area_bad,
                                   "secteurs_egalite_exacte": f"{area_exact}/{len(S)}"},
        "aire_totale_0_1pc": {"ok": abs(tot_out - tot_ref) <= AREA_TOL * tot_ref, "sortie_area2": tot_out,
                              "build_area2": tot_ref, "ecart": tot_out - tot_ref,
                              "aire_u2": round(tot_out / 2 / BU / BU, 3)},
        "une_jumelle_par_arete_interne": {"ok": not twin_fail and asym == 0, "aretes_internes": n_int_edges,
                                          "sans_jumelle": [list(map(str, t)) for t in twin_fail][:20],
                                          "asymetriques": asym},
        "zero_jonction_en_T": {"ok": not t_internal, "restantes_avec_arete_interne": len(t_internal),
                               "coupes_faites": n_cuts,
                               "info_plein_plein_non_scindees": len(t_solid),
                               "info_recouvrements": dict(sorted(ov_cnt.items()))},
        "morceaux_le_520": {"ok": n <= TARGET, "valeur": n},
        "morceaux_le_600_dur": {"ok": n <= MAXNMSECTORS, "valeur": n},
        "aucun_morceau_lt_1u2": {"ok": not small, "min_u2": min_area, "petits": small},
        "murs_couverts_une_fois": {"ok": not wall_bad, "murs": len(W), "defauts": wall_bad[:20],
                                   "portails_fondus_par_fusion": len(wall_fused)},
        "depart_dans_un_morceau": {"ok": len(dep_pieces) >= 1, "morceaux": dep_pieces},
    }
    all_ok = all(v["ok"] for v in crit.values())

    out = {
        "format": "duke2ps/e2-convex v1",
        "source": {"import": os.path.relpath(args.inp, ROOT).replace("\\", "/"),
                   "import_sha1": hashlib.sha1(raw).hexdigest(), "import_format": d["format"]},
        "options": {"fusion_secteurs": not args.no_fusion_secteurs, "split_solid_t": args.split_solid_t,
                    "tol_u": TOL_B / BU, "essais_aleatoires": N_RANDOM,
                    "strategies": [f"{a}{b}" for a, b in STRATEGIES], "hm_ordres": ["creation", "longues"]},
        "conventions": {
            "coords": "coordonnees Build entieres (x, y) ; Saturn X = x/8, Z = -y/8",
            "orientation": "verts dans le sens des boucles exterieures Build : somme de lacets "
                           "sum(x_i*y_{i+1}-x_{i+1}*y_i) > 0 (clockdir()==0) ; en Saturn sum(X_i*Z_{i+1}-X_{i+1}*Z_i) < 0 ; "
                           "le morceau est a GAUCHE de chaque arete dans le plan (x,y) cartesien",
            "edges": "edges[k] va de verts[k] a verts[k+1] ; type mur (plein, wall = id E1), portail (wall = id E1, "
                     "nextsector), diagonale (interne a un secteur Build, diag = n, sector = id E1) ; twin = [morceau, k] "
                     "de l'arete jumelle (sens oppose, memes extremites) ; un mur Build peut etre scinde (jonction en T) : "
                     "plusieurs aretes portent alors le meme wall",
            "sectors": "ids E1 des secteurs Build fusionnes dans le morceau (tous identiques : hauteurs, plans, "
                       "picnums, shade, pal, panning, stat, lotag, hitag) ; area2_par_secteur = part de chacun",
            "convexite": "sommets colineaires admis (necessaires aux jumelles exactes), aucun pic",
        },
        "bornes": {"plan_317_secteurs": [PLAN_LB, PLAN_UB], "holywood": HOLYWOOD_N,
                   "recalcul_276_secteurs": {"ceil_r_2_plus_1_moins_h": lb, "r_plus_1_moins_h": ub},
                   "morceaux_intra_secteur": n_intra, "morceaux_apres_fusion": n},
        "fusion_inter_secteurs": {"paires_voisines_identiques": len(ident_pairs), "morceaux_multi_secteurs": n_multi,
                                  "fusions": dict(fstats)},
        "aretes": dict(etypes),
        "sommets_distincts": len({tuple(v) for pc in out_pieces for v in pc["verts"]}),
        "holywood": hw,
        "depart_m1": {"sector": dep["sector"], "build_xy": list(sxy), "morceaux": dep_pieces},
        "sprites_morceau": {"table": spr_piece, "hors_de_leur_secteur_rattaches_au_plus_proche": spr_out,
                            "note": "index = rang du sprite dans e1l1_import.json ; null = sprite sans secteur"},
        "plus_decoupes": [{k: r[k] for k in ("build_id", "sector", "murs", "reflexes", "trous", "borne_basse",
                                              "morceaux_intra", "strategie")} for r in most],
        "stats": dict(stats),
        "criteres": crit,
        "secteurs": sec_rows,
        "pieces": out_pieces,
    }
    blob = dump_json(out)
    atomic_write(args.out, blob)
    if not args.no_png:
        draw_png(args.png, out_pieces, sxy)

    # ---- impression
    print(f"  aretes : {dict(etypes)} ; {out['sommets_distincts']} sommets distincts ; murs de longueur nulle "
          f"ignores {stats['murs_longueur_nulle']}")
    print(f"  jonctions en T : {n_cuts} coupe(s) ; recouvrements {dict(sorted(ov_cnt.items()))}")
    print(f"  depart M1 (secteur {dep['sector']}) dans le(s) morceau(x) {dep_pieces} ; sprites hors de leur "
          f"secteur rattaches au plus proche : {spr_out}")
    print("  secteurs Build les plus decoupes (build_id : morceaux intra / borne basse ; reflexes, trous) :")
    for r in most[:10]:
        print(f"    {r['build_id']:4d} : {r['morceaux_intra']:3d} / {r['borne_basse']:3d} ; r={r['reflexes']:2d} "
              f"h={r['trous']} murs={r['murs']} ({r['strategie']})")
    print(f"  COMPARAISON : {n} morceaux ; bornes du plan {PLAN_LB}-{PLAN_UB} (317 secteurs) ; bornes recalculees "
          f"sur 276 secteurs {lb}-{ub} ; HOLYWOOD {HOLYWOOD_N} conteneurs")
    if hw and "erreur" not in hw:
        print(f"    HOLYWOOD : {hw['centre_dans_emprise_gardee']}/{hw['conteneurs']} centres dans l'emprise gardee, "
              f"{hw['apparies_avec_altitude_8u']} apparies (altitude 8 u) sur {hw['secteurs_apparies']} secteurs ; "
              f"nos morceaux sur ces secteurs : {hw['nos_morceaux_sur_ces_secteurs']}")
    elif hw:
        print(f"    HOLYWOOD : {hw['erreur']}")
    print("CRITERES :")
    for k, v in crit.items():
        extra = {kk: vv for kk, vv in v.items() if kk != "ok" and not (isinstance(vv, list) and len(vv) > 6)}
        print(f"  [{'OK' if v['ok'] else 'ECHEC'}] {k} : {json.dumps(extra, ensure_ascii=False)}")
    print(f"sortie {os.path.relpath(args.out, ROOT)} ({len(blob)} o, sha1 {hashlib.sha1(blob).hexdigest()[:12]}) ; "
          f"{time.time() - t0:.1f} s ; {'TOUS LES CRITERES OK' if all_ok else 'CRITERE(S) EN ECHEC'}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""gridparts.py -- decoupe des secteurs Doom sur la grille de 64 (« carres exacts »).

Le moteur pose la tuile ENTIERE sur chaque face de sol ou de plafond : une face n'est donc exacte
que si elle est un carre de 64 aligne sur la grille des flats de Doom. Les feuilles du BSP de Doom
coupent les pieces a des endroits arbitraires, souvent en diagonale : un carre entier au milieu
d'une piece part alors en deux ou trois morceaux, chacun avec la tuile entiere etiree (les
triangles du plafond de la grande salle d'E1M1). MESURE 15-09 : sols 53,6 % d'aire en carres
exacts, 20,2 % en carres complets coupes par des cordes ; plafonds 49,7 / 27,5 %.

Ici on ne garde du BSP que la GEOMETRIE (ses feuilles convexes, etiquetees, cf. adjacency.py) :
1. chaque feuille est coupee par TOUTES les lignes de grille qui la traversent -> morceaux convexes
   inclus dans une cellule, les nouvelles aretes etiquetees par leur ligne de grille ;
2. les morceaux d'un MEME secteur Doom sont fusionnes tant que leur union reste convexe, jamais a
   travers un linedef. Une fusion ne fait qu'unir : un carre plein n'est plus jamais coupe, et
   add_flat (grille) le rend en UNE face exacte.
Le resultat remplace les feuilles : memes anneaux etiquetes, donc adjacency.build s'applique tel
quel (reciprocite par construction)."""
from __future__ import annotations

import math
from collections import defaultdict

import adjacency as A

EPS = 1e-6


def area2(ring):
    s = 0.0
    n = len(ring)
    for i in range(n):
        x1, y1 = ring[i][0], ring[i][1]
        x2, y2 = ring[(i + 1) % n][0], ring[(i + 1) % n][1]
        s += x1 * y2 - x2 * y1
    return s


def vkey(x, y):
    return (round(x, 5), round(y, 5))


def clean(ring):
    """Retire les sommets confondus (l'arete qui en part est nulle)."""
    out = list(ring)
    changed = True
    while changed and len(out) >= 3:
        changed = False
        for i in range(len(out)):
            a, b = out[i], out[(i + 1) % len(out)]
            if abs(a[0] - b[0]) < EPS and abs(a[1] - b[1]) < EPS:
                del out[i]
                changed = True
                break
    return out


def split_grid(poly, g):
    """Coupe un polygone convexe etiquete par toutes les lignes de grille qui le traversent."""
    parts = [poly]
    for axis in (0, 1):
        vals = [p[axis] for p in poly]
        for k in range(math.floor(min(vals) / g) + 1, math.ceil(max(vals) / g)):
            c = k * g
            nxt = []
            for p in parts:
                pv = [q[axis] for q in p]
                if min(pv) < c - EPS and max(pv) > c + EPS:
                    for keep in (True, False):
                        if axis == 0:
                            s = A.clip_tagged(p, c, 0, 0, 1, keep_front=keep)
                        else:
                            s = A.clip_tagged(p, 0, c, 1, 0, keep_front=keep)
                        s = clean(s)
                        if len(s) >= 3 and abs(area2(s)) > 1e-4:
                            nxt.append(s)
                else:
                    nxt.append(p)
            parts = nxt
    return parts


def split_tjunctions(rings):
    """Ajoute a chaque arete les sommets des AUTRES anneaux poses dessus (meme etiquette). Deux
    morceaux voisins ont alors exactement les memes aretes, en sens inverse -- c'est ce que la
    fusion apparie."""
    pts = {}
    for r in rings:
        for x, y, _ in r:
            pts[vkey(x, y)] = (x, y)
    grid = defaultdict(list)                   # hachage spatial grossier
    for k, (x, y) in pts.items():
        grid[(int(x // 32), int(y // 32))].append((x, y))
    out = []
    for r in rings:
        nr = []
        n = len(r)
        for i in range(n):
            ax, ay, tag = r[i]
            bx, by, _ = r[(i + 1) % n]
            nr.append((ax, ay, tag))
            ex, ey = bx - ax, by - ay
            L2 = ex * ex + ey * ey
            if L2 <= EPS:
                continue
            L = math.sqrt(L2)
            cand = []
            for gx in range(int(min(ax, bx) // 32) - 1, int(max(ax, bx) // 32) + 2):
                for gy in range(int(min(ay, by) // 32) - 1, int(max(ay, by) // 32) + 2):
                    for x, y in grid.get((gx, gy), ()):
                        t = ((x - ax) * ex + (y - ay) * ey) / L2
                        if EPS / L < t < 1 - EPS / L and abs((x - ax) * ey - (y - ay) * ex) / L < 1e-4:
                            cand.append((t, x, y))
            for t, x, y in sorted(set(cand)):
                nr.append((x, y, tag))
        out.append(clean(nr))
    return out


def edges(ring):
    n = len(ring)
    return [(vkey(ring[i][0], ring[i][1]), vkey(ring[(i + 1) % n][0], ring[(i + 1) % n][1]), i)
            for i in range(n)]


def union(ra, rb, orient):
    """Anneau de l'union de deux anneaux qui partagent des aretes (en sens inverse), s'il est
    CONVEXE et d'un seul tenant ; sinon None. Etiquettes conservees arete par arete."""
    ea, eb = edges(ra), edges(rb)
    sa = {(u, v) for u, v, _ in ea}
    sb = {(u, v) for u, v, _ in eb}
    shared = {(u, v) for (u, v) in sa if (v, u) in sb}
    if not shared:
        return None
    nxt = {}
    for ring, es, other in ((ra, ea, sb), (rb, eb, sa)):
        for u, v, i in es:
            if (v, u) in other:
                continue
            if u in nxt:
                return None                        # sommet de passage double : pas un anneau
            nxt[u] = (v, ring[i])
    start = next(iter(nxt))
    out, u, guard = [], start, 0
    while True:
        v, p = nxt[u]
        out.append(p)
        u = v
        guard += 1
        if u == start:
            break
        if u not in nxt or guard > len(nxt):
            return None
    if len(out) != len(nxt):
        return None
    # convexite (les sommets colineaires sont permis)
    n = len(out)
    for i in range(n):
        a, b, c = out[i - 1], out[i], out[(i + 1) % n]
        cr = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
        if cr * orient < -1e-6 * (1 + abs(b[0]) + abs(b[1])):
            return None
    if abs(area2(out)) < 1e-4:
        return None
    return out


def simplify(ring):
    """Retire un sommet quand ses deux aretes portent la MEME etiquette (meme droite) : les
    sommets poses par split_tjunctions ne servaient qu'a apparier la fusion. Garde, chaque petit
    bout devenait un mur -- MESURE E1M1 : 3 947 murs au lieu de 1 887. adjacency.build recoupe de
    toute facon la ou un voisin l'exige (union des points de coupe par droite)."""
    out = list(ring)
    changed = True
    while changed and len(out) > 3:
        changed = False
        for i in range(len(out)):
            if out[i - 1][2] == out[i][2]:
                del out[i]
                changed = True
                break
    return out


def on_linedef(u, v, lds, tol=1.5):
    """L'arete (u, v) est-elle posee sur un linedef ? (a `tol` pres : les droites de noeud arrondies)"""
    (ux, uy), (vx, vy) = u, v
    for ax, ay, bx, by in lds:
        if max(ux, vx) < min(ax, bx) - tol or min(ux, vx) > max(ax, bx) + tol:
            continue
        if max(uy, vy) < min(ay, by) - tol or min(uy, vy) > max(ay, by) + tol:
            continue
        ex, ey = bx - ax, by - ay
        L = math.hypot(ex, ey)
        if L < 1e-9:
            continue
        if all(abs((x - ax) * ey - (y - ay) * ex) / L <= tol for x, y in (u, v)):
            ts = [((x - ax) * ex + (y - ay) * ey) / (L * L) for x, y in (u, v)]
            if max(ts) > 0 and min(ts) < 1:
                return True
    return False


def partition(M, leaf_rings, leaf_sector, keep, g=64):
    """-> [(anneau etiquete, secteur Doom, [feuilles d'origine])], ordre deterministe.

    `leaf_rings` = adjacency.leaf_polygons(M) ; `keep(ring)` ecarte les feuilles hors carte."""
    DV = M["vertices"]
    lds = [(DV[l.v1][0], DV[l.v1][1], DV[l.v2][0], DV[l.v2][1]) for l in M["linedefs"]]
    pieces = []                                # dict(ring, sector, members)
    for li, ring in enumerate(leaf_rings):
        if not ring or len(ring) < 3 or not keep(ring):
            continue
        for p in split_grid(clean(ring), g):
            pieces.append(dict(ring=p, sector=leaf_sector[li], members={li}))
    if not pieces:
        return []
    orient = 1.0 if area2(pieces[0]["ring"]) > 0 else -1.0
    # T-jonctions par secteur, puis fusion gloutonne : la plus grande union d'abord
    bysec = defaultdict(list)
    for p in pieces:
        bysec[p["sector"]].append(p)
    result = []
    for sec in sorted(bysec):
        grp = bysec[sec]
        rings = split_tjunctions([p["ring"] for p in grp])
        cur = {k: dict(ring=r, members=set(p["members"]), area=abs(area2(r)))
               for k, (r, p) in enumerate(zip(rings, grp))}
        nid = len(cur)
        blocked = {}
        for p in cur.values():
            r = p["ring"]
            p["cell"] = (math.floor(sum(q[0] for q in r) / len(r) / g),
                         math.floor(sum(q[1] for q in r) / len(r) / g))
        # 1re phase : on ne fusionne que DANS une cellule, pour reconstituer d'abord chaque carre ;
        # 2e phase : entre cellules. Sans cela la plus grande union passait d'abord, une moitie de
        # carre etait absorbee par un grand voisin et l'autre moitie ne pouvait plus la rejoindre
        # (union non convexe) -- MESURE : 13,4 % des sols et 20,9 % des plafonds restaient coupes.
        for phase in (0, 1):
          while True:
            owner = {}
            for pid, p in cur.items():
                for u, v, _ in edges(p["ring"]):
                    owner[(u, v)] = pid
            pairs = defaultdict(float)
            bad = set()
            for (u, v), pid in owner.items():
                q = owner.get((v, u))
                if q is None or q == pid:
                    continue
                if phase == 0 and cur[pid]["cell"] != cur[q]["cell"]:
                    continue
                a_, b_ = (pid, q) if pid < q else (q, pid)
                key = (u, v) if u < v else (v, u)
                if key not in blocked:
                    blocked[key] = on_linedef(u, v, lds)
                if blocked[key]:
                    bad.add((a_, b_))
                pairs[(a_, b_)] += math.hypot(u[0] - v[0], u[1] - v[1])
            done, merged = set(), False
            for (a_, b_), _L in sorted(pairs.items(),
                                       key=lambda kv: (-(cur[kv[0][0]]["area"] + cur[kv[0][1]]["area"]),
                                                       kv[0])):
                if (a_, b_) in bad or a_ in done or b_ in done:
                    continue
                r = union(cur[a_]["ring"], cur[b_]["ring"], orient)
                if r is None:
                    continue
                cur[nid] = dict(ring=r, members=cur[a_]["members"] | cur[b_]["members"],
                                area=abs(area2(r)), cell=cur[a_]["cell"])
                done |= {a_, b_}
                del cur[a_], cur[b_]
                nid += 1
                merged = True
            if not merged:
                break
        for p in cur.values():
            result.append((simplify(p["ring"]), sec, sorted(p["members"])))
    result.sort(key=lambda e: (e[2][0], min(q[1] for q in e[0]), min(q[0] for q in e[0])))
    return result

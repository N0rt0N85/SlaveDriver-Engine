#!/usr/bin/env python3
"""adjacency.py -- polygones de feuille BSP et adjacence EXACTE entre feuilles.

Remplace l'echantillonnage. La premiere version de doom3d.py cherchait le voisin d'une arete en
tirant un point a 1,5 u en dehors et en redescendant le BSP, puis subdivisait l'arete tant que les
deux moities ne repondaient pas pareil. Resultat MESURE sur E1M1 : **58 portails a sens unique sur
752, dont 31 sans aucun mur en face** -- des trous. Deux feuilles voisines derivaient leur frontiere
commune chacune de son cote, donc leurs points de coupe ne tombaient pas aux memes endroits.

Ici la reciprocite est vraie PAR CONSTRUCTION :

1. On clippe la bbox de la carte par les lignes de partition des ancetres puis par les segs de la
   feuille, en **etiquetant chaque arete par la LIGNE qui l'a creee**. L'etiquette est la forme
   canonique entiere `(a, b, c)` de `a*x + b*y + c = 0`, reduite par pgcd et de signe normalise :
   elle est donc IDENTIQUE que la ligne vienne d'un noeud du BSP ou d'un linedef, ce qui est le cas
   courant (les partitions de Doom suivent des linedefs).
2. Toutes les aretes portant la meme etiquette sont colineaires. On les parametre par
   `t = (-b, a) . p` le long de cette ligne commune, on prend l'UNION de tous les points de coupe,
   et on redecoupe toutes les aretes dessus. Deux feuilles voisines ont alors exactement les memes
   bornes.
3. Un intervalle `[t0, t1]` d'une etiquette est couvert par une ou deux feuilles. Deux -> portail
   reciproque. Une -> frontiere pleine.
4. Les sommets sont RECONSTRUITS depuis (ligne, t) par la formule exacte, donc les deux feuilles
   arrondissent le meme reel et tombent sur le meme entier.
"""
from __future__ import annotations

import math
from collections import defaultdict

EPS = 1e-7


def line_key(px, py, dx, dy):
    """Forme canonique entiere de la droite passant par (px,py) de direction (dx,dy)."""
    a, b = dy, -dx
    c = -(a * px + b * py)
    g = math.gcd(math.gcd(abs(int(a)), abs(int(b))), abs(int(c))) or 1
    a, b, c = int(a) // g, int(b) // g, int(c) // g
    if a < 0 or (a == 0 and b < 0):
        a, b, c = -a, -b, -c
    return (a, b, c)


def param(key, x, y):
    """Abscisse le long de la droite : t = (-b, a) . p."""
    a, b, _ = key
    return -b * x + a * y


def point_at(key, t):
    """Point exact de la droite d'abscisse t (inverse de `param`)."""
    a, b, c = key
    n = float(a * a + b * b)
    return ((-a * c - b * t) / n, (-b * c + a * t) / n)


def point_side(nx, ny, ndx, ndy, px, py):
    """< 0 = cote AVANT (children[0]) ; > 0 = cote ARRIERE. Forme de R_PointOnSide."""
    return ndx * (py - ny) - ndy * (px - nx)


def clip_tagged(poly, nx, ny, ndx, ndy, keep_front):
    """Sutherland-Hodgman en portant une etiquette par arete.

    `poly` = [(x, y, tag)] ou tag etiquette l'arete qui PART de ce sommet."""
    if not poly:
        return []
    tag = line_key(nx, ny, ndx, ndy)
    sgn = -1.0 if keep_front else 1.0
    out = []
    n = len(poly)
    for i in range(n):
        ax, ay, ta = poly[i]
        bx, by, _tb = poly[(i + 1) % n]
        fa = sgn * point_side(nx, ny, ndx, ndy, ax, ay)
        fb = sgn * point_side(nx, ny, ndx, ndy, bx, by)
        if fa >= -EPS:
            out.append((ax, ay, ta))
        if (fa > EPS and fb < -EPS) or (fa < -EPS and fb > EPS):
            t = fa / (fa - fb)
            x, y = ax + (bx - ax) * t, ay + (by - ay) * t
            # en SORTANT, l'arete qui commence au croisement longe la ligne de coupe ;
            # en ENTRANT, le croisement ouvre le reste de l'arete courante.
            out.append((x, y, tag if fa > 0 else ta))

    # RECALAGE DES ETIQUETTES. L'etiquette est portee par le SOMMET et vaut pour l'arete qui en
    # part -- mais quand un sommet tombe EXACTEMENT sur la ligne de coupe, Sutherland-Hodgman ne
    # produit aucun croisement : le sommet est garde tel quel, avec son ANCIENNE etiquette, alors
    # que l'arete qui en part longe desormais la ligne de coupe. Elle est alors comptee sur la
    # mauvaise droite, et surtout `build()` reconstruit ses extremites par `point_at(tag, t)`,
    # donc **elle est projetee sur une droite qui n'est pas la sienne** : le mur change de place.
    # MESURE sur E1M1 avant correctif : 25 aretes sur 932 (2,7 %) dont une deplacee de 416 unites.
    # L'invariant qui repare : si les DEUX extremites d'une arete sont sur la ligne de coupe, son
    # arete EST cette ligne, donc son etiquette est `tag` -- une autre forme canonique de la meme
    # droite est impossible.
    nrm = math.hypot(ndx, ndy) or 1.0
    m = len(out)
    for i in range(m):
        ax, ay, ta = out[i]
        bx, by, _ = out[(i + 1) % m]
        if (abs(point_side(nx, ny, ndx, ndy, ax, ay)) / nrm <= 1e-6
                and abs(point_side(nx, ny, ndx, ndy, bx, by)) / nrm <= 1e-6):
            out[i] = (ax, ay, tag)
    return out


def leaf_polygons(M, margin=16):
    """Polygones convexes etiquetes des feuilles du BSP, en flottants."""
    xs = [v[0] for v in M["vertices"]]
    ys = [v[1] for v in M["vertices"]]
    x0, x1 = min(xs) - margin, max(xs) + margin
    y0, y1 = min(ys) - margin, max(ys) + margin
    corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    box = []
    for i, (cx, cy) in enumerate(corners):
        nx_, ny_ = corners[(i + 1) % 4]
        box.append((cx, cy, line_key(cx, cy, nx_ - cx, ny_ - cy)))

    polys = [None] * len(M["subsectors"])
    nodes = M["nodes"]
    stack = [(len(nodes) - 1, box)]
    while stack:
        idx, poly = stack.pop()
        if idx & 0x8000:
            polys[idx & 0x7FFF] = poly
            continue
        nd = nodes[idx]
        for k, child in enumerate(nd.children):
            sub = clip_tagged(poly, nd.x, nd.y, nd.dx, nd.dy, keep_front=(k == 0))
            if len(sub) >= 3:
                stack.append((child, sub))

    # On decoupe par la droite du LINEDEF PARENT, pas par celle du seg. Un seg est un morceau de
    # linedef dont le nodebuilder a ARRONDI A L'ENTIER les sommets qu'il a crees en coupant :
    # MESURE sur E1M1, 37 segs sur 732 (5,1 %) ne sont pas exactement sur la droite de leur
    # linedef. Sur une diagonale, la feuille d'un cote se trouvait alors bornee par la droite du
    # seg et celle d'en face par la partition du noeud -- qui, elle, suit le linedef exact. Deux
    # etiquettes differentes pour la meme frontiere, donc aucun appariement, donc un mur plein de
    # chaque cote : 17 murs fantomes dans la cour diagonale d'E1M1.
    # Le linedef est la verite ; l'etendue du seg n'entre pas en jeu puisqu'on coupe par la droite
    # INFINIE. Un seg de cote 1 parcourt le linedef a l'envers, d'ou l'inversion de direction.
    V = M["vertices"]
    for si, ss in enumerate(M["subsectors"]):
        p = polys[si]
        if not p:
            continue
        for k in range(ss.first, ss.first + ss.count):
            sg = M["segs"][k]
            ld = M["linedefs"][sg.line]
            ax, ay = V[ld.v1]
            bx, by = V[ld.v2]
            if (ax, ay) == (bx, by):
                ax, ay = V[sg.v1]
                bx, by = V[sg.v2]
                if (ax, ay) == (bx, by):
                    continue
            if sg.side:
                ax, ay, bx, by = bx, by, ax, ay
            p = clip_tagged(p, ax, ay, bx - ax, by - ay, keep_front=True)
            if len(p) < 3:
                break
        polys[si] = p
    return polys


def build(M, margin=16, tol=1e-4):
    """-> (polys, boundary) ; boundary[leaf] = [(P, Q, tag, t0, t1, voisin_ou_-1), ...] en ENTIERS,
    dans l'ordre du polygone, avec la reciprocite garantie."""
    polys = leaf_polygons(M, margin)

    # GARDE-FOU. Tout le reste repose sur l'etiquette : elle sert de cle de regroupement, et
    # `point_at(tag, t)` RECONSTRUIT les sommets depuis elle. Une arete dont l'etiquette ne decrit
    # pas la droite se retrouve donc projetee ailleurs -- un mur qui change de place. C'est arrive
    # (25 aretes sur 932, jusqu'a 416 unites de deplacement), alors on le verifie au lieu de
    # l'esperer.
    for li, p in enumerate(polys):
        if not p or len(p) < 3:
            continue
        n = len(p)
        for i in range(n):
            ax, ay, tag = p[i]
            bx, by, _ = p[(i + 1) % n]
            if abs(ax - bx) < tol and abs(ay - by) < tol:
                continue
            a, b, c = tag
            nrm = math.hypot(a, b) or 1.0
            ecart = max(abs(a * ax + b * ay + c), abs(a * bx + b * by + c)) / nrm
            assert ecart < 0.01, (
                f"feuille {li} : l'arete ({ax:.1f},{ay:.1f})-({bx:.1f},{by:.1f}) n'est pas sur sa "
                f"droite {tag} (ecart {ecart:.1f})")

    # 1. toutes les aretes, groupees par etiquette de ligne
    raw = []            # (leaf, ordre, tag, t0, t1)
    bytag = defaultdict(list)
    for li, p in enumerate(polys):
        if not p or len(p) < 3:
            continue
        n = len(p)
        for i in range(n):
            ax, ay, tag = p[i]
            bx, by, _ = p[(i + 1) % n]
            if abs(ax - bx) < tol and abs(ay - by) < tol:
                continue
            t0, t1 = param(tag, ax, ay), param(tag, bx, by)
            k = len(raw)
            raw.append([li, i, tag, t0, t1])
            bytag[tag].append(k)

    # 2. union des points de coupe par ligne, puis redecoupe.
    #    Les extremites des LINEDEFS entrent dans l'union au meme titre que celles des aretes de
    #    feuille : un mur de Doom est souvent fait de plusieurs linedefs bout a bout (une bande
    #    SUPPORT2 entre deux panneaux STARGR1), et sans ces points une seule arete les recouvre
    #    tous et n'en prend qu'un -- donc une texture pour l'autre, et le mauvais voisin si les
    #    linedefs ne s'accordent pas. MESURE sur E1M1 : 23 aretes concernees.
    DV = M["vertices"]
    ld_cuts = defaultdict(set)
    for ld in M["linedefs"]:
        ax, ay = DV[ld.v1]
        bx, by = DV[ld.v2]
        if (ax, ay) == (bx, by):
            continue
        k = line_key(ax, ay, bx - ax, by - ay)
        ld_cuts[k].add(round(param(k, ax, ay), 4))
        ld_cuts[k].add(round(param(k, bx, by), 4))

    # NE PAS fusionner les points de coupe a l'echelle de la grille de sortie : essaye, MESURE,
    # regression 17 -> 35 murs fantomes. Les aretes brutes sont bornees par les sommets des
    # polygones ; supprimer un point de coupe qui EST un tel sommet desaccorde les deux cotes --
    # la feuille qui a le sommet produit deux morceaux, l'autre un seul, et plus rien ne s'apparie.
    # Tout sommet de polygone doit rester dans l'union.
    cuts = {}
    seuils = {}
    for tag, ks in bytag.items():
        seuil = tol
        seuils[tag] = seuil
        bornes = {round(v, 4) for k in ks for v in (raw[k][3], raw[k][4])}
        lo_t = min(bornes)
        hi_t = max(bornes)
        bornes |= {v for v in ld_cuts.get(tag, ()) if lo_t < v < hi_t}
        vals = sorted(bornes)
        merged = []
        for v in vals:
            if not merged or v - merged[-1] > seuil:
                merged.append(v)
        cuts[tag] = merged

    pieces = defaultdict(list)          # leaf -> [(ordre, tag, lo, hi)]
    span = defaultdict(list)            # (tag, lo, hi) -> [leaf]
    for li, order, tag, t0, t1 in raw:
        lo, hi = (t0, t1) if t0 < t1 else (t1, t0)
        seuil = seuils[tag]
        inside = [v for v in cuts[tag] if lo + seuil < v < hi - seuil]
        bounds = [lo] + inside + [hi]
        for a_, b_ in zip(bounds, bounds[1:]):
            if b_ - a_ <= seuil:
                continue
            key = (tag, round(a_, 4), round(b_, 4))
            pieces[li].append((order, tag, a_, b_, t0 > t1))
            span[key].append(li)

    # 3. voisin = l'autre feuille qui couvre le meme intervalle.
    #    La decision se prend UNE FOIS par intervalle, sur l'ENSEMBLE des feuilles qui le
    #    couvrent : exactement deux -> portail reciproque, tout le reste -> frontiere pleine des
    #    deux cotes. La prendre par feuille laissait passer les cas ou une feuille couvre un
    #    intervalle DEUX fois (arete degeneree) : elle voyait 2 voisins et refusait, pendant que
    #    l'autre n'en voyait qu'un et acceptait -- 7 portails a sens unique sur E1M1.
    voisin = {}
    for key, leaves in span.items():
        u = set(leaves)
        if len(u) == 2 and len(leaves) == 2:
            a_, b_ = sorted(u)
            voisin[(key, a_)] = b_
            voisin[(key, b_)] = a_
    boundary = {}
    for li, lst in pieces.items():
        lst.sort(key=lambda e: (e[0], e[2] if not e[4] else -e[2]))
        out = []
        for order, tag, a_, b_, rev in lst:
            key = (tag, round(a_, 4), round(b_, 4))
            nb = voisin.get((key, li), -1)
            others = [x for x in span[key] if x != li]
            ta, tb = (b_, a_) if rev else (a_, b_)
            px, py = point_at(tag, ta)
            qx, qy = point_at(tag, tb)
            P = (int(round(px)), int(round(py)))
            Q = (int(round(qx)), int(round(qy)))
            if P == Q:
                continue
            out.append((P, Q, tag, ta, tb, nb, len(others)))
        boundary[li] = out
    return polys, boundary

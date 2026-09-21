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
    """Polygones convexes etiquetes des feuilles du BSP, en flottants. Les lignes de noeud qui
    laissent un TROU entre deux feuilles sont recalees sur leur linedef (ligne_de_noeud), et
    seulement elles : recaler partout deplace des frontieres qui n'en avaient pas besoin (E1M1 :
    3 noeuds, une aretes de 1,4 u dont le plan infini sortait un monstre de son secteur)."""
    polys = _feuilles(M, margin, set())
    recaler = _noeuds_troues(M, polys)
    return _feuilles(M, margin, recaler) if recaler else polys


def _aire(p):
    n = len(p)
    return abs(sum(p[i][0] * p[(i + 1) % n][1] - p[(i + 1) % n][0] * p[i][1] for i in range(n))) / 2.0


SEUIL_DEBORD = 8.0     # u2 ; voir reattribuer_debords


def reattribuer_debords(M, rings, secteurs, tol=1.0, seuil=SEUIL_DEBORD):
    """Rend a son VRAI secteur ce qu'une feuille du BSP deborde.

    La region d'une feuille est fermee par les lignes de noeud ET par ses segs. Quand elle n'a pas
    assez de segs pour se fermer, la region de noeud deborde : Doom ne la dessine jamais (il ne
    trace que des segs, les sols se remplissent entre eux), mais notre polygone, lui, la couvre.
    MESURE 2026-09-18 : sur E1M6, la feuille 227 du secteur 104 (une bande de 16 u sous un linteau,
    plafond 48) n'a qu'un seg et s'etend 128 u dans la salle du secteur 20 (plafond 224) -- une dalle
    de plafond flottante et trois portails pleine hauteur ; sur E1M7, la feuille 207 passe derriere le
    mur a une face 895 de son propre secteur.
    Test EXACT d'un debord : un linedef de SON secteur coupe le polygone, et la corde tient dans
    l'etendue du linedef (a `tol` pres) -- une feuille saine ne traverse jamais un linedef de son
    secteur, ses segs sont sur son BORD. La partie du mauvais cote passe au secteur d'en face, ou
    disparait si le linedef n'a qu'une face (c'est le vide).
    `seuil` : sous 8 u2 le debord est un eclat d'arrondi que l'appariement a 1,5 u (seg_near) rattrape
    deja -- MESURE sur l'episode : 2 a 6 u2 pour les eclats (E1M1 : 3 et 6, sans aucun defaut), 11,6
    a 1 280 u2 pour les debords qui en font un (E1M5, E1M6, E1M7). E1M1 n'est donc pas touche.
    -> (anneaux, secteurs, membres, n coupes) ; `membres` = feuilles BSP d'origine de chaque anneau."""
    V, SD = M["vertices"], M["sidedefs"]
    bords = defaultdict(list)
    for ld in M["linedefs"]:
        r = SD[ld.right].sector if ld.right >= 0 else None
        g = SD[ld.left].sector if ld.left >= 0 else None
        if r == g:
            continue
        (ax, ay), (bx, by) = V[ld.v1], V[ld.v2]
        if (ax, ay) == (bx, by):
            continue
        for s in (r, g):
            if s is not None:
                bords[s].append((ax, ay, bx, by, r, g))
    out_r, out_s, out_m = [], [], []
    n = 0
    todo = [(ring, secteurs[si], [si]) for si, ring in enumerate(rings)]
    todo.reverse()
    while todo:
        ring, sec, mem = todo.pop()
        coupe = None
        if ring and len(ring) >= 3:
            for (ax, ay, bx, by, r, g) in bords.get(sec, ()):
                avant = clip_tagged(ring, ax, ay, bx - ax, by - ay, keep_front=True)
                if len(avant) < 3 or _aire(avant) <= 1.0:
                    continue
                arriere = clip_tagged(ring, ax, ay, bx - ax, by - ay, keep_front=False)
                if len(arriere) < 3 or _aire(arriere) <= 1.0:
                    continue
                ex, ey = bx - ax, by - ay
                L2 = float(ex * ex + ey * ey)
                L = math.sqrt(L2)
                corde = [((x - ax) * ex + (y - ay) * ey) / L2 for x, y, _ in avant
                         if abs((x - ax) * ey - (y - ay) * ex) / L <= 1e-6]
                if not corde or min(corde) < -tol / L or max(corde) > 1 + tol / L:
                    continue
                garde, autre = (avant, arriere) if r == sec else (arriere, avant)
                if _aire(autre) < seuil:
                    continue
                coupe = (garde, autre, g if r == sec else r)
                break
        if coupe is None:
            out_r.append(ring)
            out_s.append(sec)
            out_m.append(mem)
            continue
        n += 1
        todo.append((coupe[0], sec, mem))
        if coupe[2] is not None:
            todo.append((coupe[1], coupe[2], mem))
    return out_r, out_s, out_m, n


def _dans_la_carte(M, x, y):
    """Parite des croisements d'un rayon +x avec les linedefs a UNE face : ce sont eux, et eux
    seuls, qui separent le monde du vide (meme test que doom3d._dans_la_carte)."""
    V = M["vertices"]
    c = False
    for ld in M["linedefs"]:
        if ld.left >= 0 and ld.right >= 0:
            continue
        (ax, ay), (bx, by) = V[ld.v1], V[ld.v2]
        if (ay > y) != (by > y) and x < ax + (y - ay) / float(by - ay) * (bx - ax):
            c = not c
    return c


def _noeuds_troues(M, polys, tol=1.0):
    """Les noeuds dont une arete de feuille n'a PERSONNE en face : la portion de l'arete qu'aucune
    arete de meme etiquette et de sens oppose (la feuille voisine) ne recouvre depasse `tol` u, et
    le point juste au-dela est DANS la carte (derriere un mur a une face, le vide n'est pas un trou).
    C'est la signature du coin que laissent entre elles deux droites voisines, le noeud et le
    linedef -- quelle que soit sa largeur : sur E1M2 il fait moins d'une unite (noeud tire d'un seg
    arrondi du linedef 917, secteur 141), assez pour que les deux cotes ne s'apparient plus.

    PLUSIEURS noeuds peuvent porter la meme droite (E1M2 : 321 et 330 sur le seg arrondi de 917).
    Le dictionnaire d'origine n'en gardait que le DERNIER : 330, qui ne borde que la moitie est ;
    321, le parent de la feuille trouee (ss 322, la nappe 141 face a la passerelle 142), restait
    decale -- ni portail ni mur cote passerelle, le ciel a travers sur toute la hauteur (console,
    2026-09-19/21).  Le noeud a recaler est celui qui a TRACE l'arete : un ANCETRE de la feuille
    trouee.  Le dernier noeud reste recale comme avant, pour ne rien deplacer d'autre."""
    nodes = M["nodes"]
    cle = {line_key(nd.x, nd.y, nd.dx, nd.dy): k for k, nd in enumerate(nodes)}
    sur_la_droite = defaultdict(list)
    for k, nd in enumerate(nodes):
        sur_la_droite[line_key(nd.x, nd.y, nd.dx, nd.dy)].append(k)
    parent = {}
    for k, nd in enumerate(nodes):
        for c in nd.children:
            parent[c] = k

    def ancetres(ss):
        out, c = set(), ss | 0x8000
        while c in parent:
            c = parent[c]
            out.add(c)
        return out
    par_tag = defaultdict(list)
    for pi, p in enumerate(polys):
        if not p or len(p) < 3:
            continue
        n = len(p)
        o = 1.0 if sum(p[i][0] * p[(i + 1) % n][1] - p[(i + 1) % n][0] * p[i][1]
                       for i in range(n)) > 0 else -1.0
        for i in range(n):
            ax, ay, tag = p[i]
            if tag not in cle:
                continue
            bx, by = p[(i + 1) % n][0], p[(i + 1) % n][1]
            t0, t1 = param(tag, ax, ay), param(tag, bx, by)
            par_tag[tag].append((min(t0, t1), max(t0, t1), t1 > t0, pi, o, (ax, ay, bx, by)))
    out = set()
    for tag, lst in par_tag.items():
        k = cle[tag]
        echelle = math.hypot(tag[0], tag[1]) or 1.0
        for (a, b, sens, pi, o, (ax, ay, bx, by)) in lst:
            if (b - a) / echelle <= tol:
                continue
            en_face = sorted((a2, b2) for (a2, b2, s2, pj, _o, _s) in lst if pj != pi and s2 != sens)
            trous, pos = [], a
            for a2, b2 in en_face:
                if b2 <= pos:
                    continue
                if a2 > pos:
                    trous.append((pos, min(a2, b)))
                pos = max(pos, b2)
                if pos >= b:
                    break
            if pos < b:
                trous.append((pos, b))
            for u, v in trous:
                if (v - u) / echelle <= tol:
                    continue
                x, y = point_at(tag, (u + v) / 2.0)
                ex, ey = bx - ax, by - ay
                L = math.hypot(ex, ey) or 1.0
                if _dans_la_carte(M, x + o * ey / L * 0.5, y - o * ex / L * 0.5):
                    out.add(k)
                    out |= ancetres(pi) & set(sur_la_droite[tag])
                    break
    return out


def _feuilles(M, margin, recaler):
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
        nx_, ny_, ndx_, ndy_ = (ligne_de_noeud(M, nd) if idx in recaler
                                else (nd.x, nd.y, nd.dx, nd.dy))
        for k, child in enumerate(nd.children):
            sub = clip_tagged(poly, nx_, ny_, ndx_, ndy_, keep_front=(k == 0))
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


def ligne_de_noeud(M, nd, tol=1.5):
    """La droite par laquelle couper au noeud `nd` : celle du LINEDEF dont le noeud est tire quand
    il ne s'en ecarte que d'un arrondi, sinon celle du noeud.

    Le nodebuilder tire sa partition d'un SEG, et un seg ne de la coupe d'un linedef a ses sommets
    arrondis a l'entier (5 % des segs d'E1M1, voir leaf_polygons). Or on coupe les feuilles par la
    droite du LINEDEF (meme raison) : quand la partition vient d'un seg arrondi, la feuille qui porte
    le seg est bornee par le linedef et celle d'en face par le noeud, et les deux droites s'ecartent
    AU-DELA du linedef. MESURE 2026-09-18, E1M8 : le noeud 39 suit le seg (-143, 2624)-(-351, 1952)
    du linedef 284, qui finit en (-352, 1952) ; 1 u d'ecart a son bout, 3 u a 1 200 u plus loin, un
    coin de 1 200 u2 de la cour qu'aucune feuille ne couvrait -- deux murs pleins invisibles de part
    et d'autre, et le joueur mure au depart (1 secteur sur 200 atteignable). Recaler le noeud sur son
    linedef donne la MEME droite aux deux cotes, partout. On garde l'orientation du noeud (quel cote
    est l'avant) ; un noeud qui ne tombe sur aucun linedef a `tol` pres reste tel quel."""
    p0 = (nd.x, nd.y)
    p1 = (nd.x + nd.dx, nd.y + nd.dy)
    V = M["vertices"]
    for ld in M["linedefs"]:
        (ax, ay), (bx, by) = V[ld.v1], V[ld.v2]
        ex, ey = bx - ax, by - ay
        L2 = float(ex * ex + ey * ey)
        if L2 <= 0:
            continue
        L = math.sqrt(L2)
        ok = True
        for x, y in (p0, p1):
            if abs((x - ax) * ey - (y - ay) * ex) / L > tol:
                ok = False
                break
            t = ((x - ax) * ex + (y - ay) * ey) / L2
            if t < -tol / L or t > 1 + tol / L:
                ok = False
                break
        if not ok:
            continue
        if line_key(ax, ay, ex, ey) == line_key(nd.x, nd.y, nd.dx, nd.dy):
            return nd.x, nd.y, nd.dx, nd.dy
        if ex * nd.dx + ey * nd.dy < 0:
            ax, ay, ex, ey = bx, by, -ex, -ey
        return ax, ay, ex, ey
    return nd.x, nd.y, nd.dx, nd.dy


def build(M, margin=16, tol=1e-4, polys=None):
    """-> (polys, boundary) ; boundary[leaf] = [(P, Q, tag, t0, t1, voisin_ou_-1), ...] en ENTIERS,
    dans l'ordre du polygone, avec la reciprocite garantie. `polys` : anneaux etiquetes deja faits
    (gridparts.partition) ; par defaut les feuilles du BSP."""
    if polys is None:
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

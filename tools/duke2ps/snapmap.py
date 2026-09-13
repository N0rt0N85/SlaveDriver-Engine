#!/usr/bin/env python3
"""snapmap.py -- etape E2.5b : snap sur la grille du .LEV AVEC preservation des features.

Probleme (MESURE, E1L1) : 27 groupes de coordonnees distinctes tombent sur le meme point de grille
(etendue 1 a 5 unites Build, soit 0,125 a 0,625 u). La plupart sont des doublons inoffensifs, mais
certains bornent une VRAIE feature :
  - le secteur 23 est un mur-secteur d'UNE unite Build d'epaisseur (x = 21951 contre 21952) qui separe
    les secteurs 1/19/21 des secteurs 2/20/22 ; l'arrondi le fait disparaitre et colle les deux moities ;
  - 8 murs des secteurs 118/119/122 deviennent de longueur nulle.

Regle : on arrondit normalement, SAUF quand la collision cree une degenerescence (arete de longueur
nulle, sommet repete dans une boucle). Dans ce cas les valeurs en cause sont ecartees d'un cran de
grille, en gardant fixe celle qui etait deja exactement sur la grille. Le mur-secteur passe donc de
0,125 u a 1 u d'epaisseur : la topologie est preservee, au prix d'une derive bornee.
"""
from __future__ import annotations
from collections import defaultdict

G = 8               # unites Build par unite Saturn


def _snap(v, m):
    return m[v]


def _area2(p):
    n = len(p)
    return sum(p[k][0] * p[(k + 1) % n][1] - p[(k + 1) % n][0] * p[k][1] for k in range(n))


def find_constraints(S, W, mx, my):
    """Paires de valeurs (par axe) qui ne doivent PAS tomber sur le meme point de grille.
    Derivees des degenerescences que la carte snappee presenterait."""
    cx, cy = set(), set()

    def need(P, Q):
        dx, dy = abs(P[0] - Q[0]), abs(P[1] - Q[1])
        if dx >= dy and dx > 0:
            cx.add((min(P[0], Q[0]), max(P[0], Q[0])))
        elif dy > 0:
            cy.add((min(P[1], Q[1]), max(P[1], Q[1])))

    for s in S:
        # sommets des AUTRES boucles du meme secteur : une boucle interieure (trou) qui vient
        # toucher la boucle exterieure casse la triangulation sans qu'aucune boucle, prise seule,
        # ne soit degeneree (MESURE : d13/E2L2 secteur 80, 19/19 sommets distincts et pourtant
        # « plus d'oreille »).
        loops_o = [[(W[w]['x'], W[w]['y']) for w in L] for L in s['loops']]
        loops_s = [[(mx[a], my[b]) for a, b in o] for o in loops_o]
        for i1 in range(len(loops_s)):
            for i2 in range(i1 + 1, len(loops_s)):
                if len(loops_s[i1]) * len(loops_s[i2]) > 20000:
                    continue
                common = set(loops_s[i1]) & set(loops_s[i2])
                for pt in common:
                    a = loops_o[i1][loops_s[i1].index(pt)]
                    b = loops_o[i2][loops_s[i2].index(pt)]
                    if a != b:
                        need(a, b)

        for L in s['loops']:
            orig = [(W[w]['x'], W[w]['y']) for w in L]
            snap = [(mx[a], my[b]) for a, b in orig]
            n = len(L)
            # effondrement de la boucle : l'aire tombe a zero ou change de signe alors que les
            # sommets restent distincts (MESURE : d13/E2L5 secteur 55, boucle de 0,5 u de large,
            # 15,97 u2 -> 0,00). On contraint alors les extremes de l'axe qui s'est ecrase.
            ao, as_ = _area2(orig), _area2(snap)
            if ao != 0 and (as_ == 0 or (ao > 0) != (as_ > 0)):
                xs = sorted({p[0] for p in orig})
                ys = sorted({p[1] for p in orig})
                if len({p[0] for p in snap}) < len(xs):
                    cx.add((xs[0], xs[-1]))
                if len({p[1] for p in snap}) < len(ys):
                    cy.add((ys[0], ys[-1]))
            for i in range(n):                       # aretes de longueur nulle
                j = (i + 1) % n
                if snap[i] == snap[j] and orig[i] != orig[j]:
                    need(orig[i], orig[j])
            seen = defaultdict(list)                 # sommets repetes (auto-tangence)
            for i, p in enumerate(snap):
                seen[p].append(i)
            for p, idx in seen.items():
                if len(idx) > 1:
                    for a in range(len(idx)):
                        for b in range(a + 1, len(idx)):
                            i, j = idx[a], idx[b]
                            if orig[i] != orig[j]:
                                need(orig[i], orig[j])
            # sommet tombe A L'INTERIEUR d'une arete non adjacente (jonction en T) : la triangulation
            # par oreilles echoue dessus. Cause : une coordonnee s'est alignee sur la droite de l'arete.
            if n <= 256:
                for i in range(n):
                    P = snap[i]
                    for j in range(n):
                        if j == i or (j + 1) % n == i:
                            continue
                        A, B = snap[j], snap[(j + 1) % n]
                        if A == B or P == A or P == B:
                            continue
                        if (B[0] - A[0]) * (P[1] - A[1]) - (B[1] - A[1]) * (P[0] - A[0]) != 0:
                            continue
                        if not (min(A[0], B[0]) <= P[0] <= max(A[0], B[0])
                                and min(A[1], B[1]) <= P[1] <= max(A[1], B[1])):
                            continue
                        oi, oj, ok = orig[i], orig[j], orig[(j + 1) % n]
                        if A[0] == B[0] == P[0] and oi[0] not in (oj[0], ok[0]):
                            cx.add((min(oi[0], oj[0]), max(oi[0], oj[0])))
                        elif A[1] == B[1] == P[1] and oi[1] not in (oj[1], ok[1]):
                            cy.add((min(oi[1], oj[1]), max(oi[1], oj[1])))
                        else:
                            need(oi, oj if oj != oi else ok)
    return cx, cy


def _sweep(sv, m, apart, step):
    """Une passe monotone : on parcourt les valeurs dans l'ordre (croissant si step > 0) et on
    n'attribue jamais un creneau qui casserait l'ordre. Un creneau deja pris par une valeur
    CONTRAINTE de la meme serie force le cran suivant. L'ordre est donc garanti par construction,
    et non repare apres coup."""
    out = {}
    cur = None
    run = set()
    for v in sv:
        t = m[v]
        if cur is not None:
            t = max(t, cur) if step > 0 else min(t, cur)
        if t == cur and (apart[v] & run):
            t = cur + step
        if t != cur:
            cur = t
            run = set()
        run.add(v)
        out[v] = t
    return out


def spread(vals, cons, m):
    """Ecarte les valeurs liees par une contrainte, en garantissant TROIS proprietes :
      1. l'ordre est preserve (inverser deux coordonnees retourne un polygone mince) ;
      2. les paires non contraintes peuvent partager un creneau (la derive reste faible) ;
      3. une paire contrainte n'est jamais reunie, meme par une iteration ulterieure.

    Deux passes monotones, l'une vers le haut l'autre vers le bas, sont calculees ; sur chaque plage
    ou elles different on garde celle qui deplace le moins. Les deux passes coincident aux bornes de
    ces plages, donc le melange reste monotone. Un simple garde-fou « creneau occupe » ne suffisait
    pas : il enjambait des valeurs et inversait l'ordre (MESURE : d13/E2L5 secteur 55, aire
    +15,97 -> -31,50 u2, donc boucle retournee)."""
    apart = defaultdict(set)
    for (a, b) in cons:
        apart[a].add(b)
        apart[b].add(a)
    sv = sorted(vals)
    up = _sweep(sv, m, apart, G)
    dn = _sweep(sv[::-1], m, apart, -G)

    out = {}
    i = 0
    moved = 0
    while i < len(sv):
        if up[sv[i]] == dn[sv[i]]:
            out[sv[i]] = up[sv[i]]
            i += 1
            continue
        j = i
        while j < len(sv) and up[sv[j]] != dn[sv[j]]:
            j += 1
        seg = sv[i:j]
        cu = sum(abs(v - up[v]) for v in seg)
        cd = sum(abs(v - dn[v]) for v in seg)
        pick = up if cu <= cd else dn
        for v in seg:
            out[v] = pick[v]
        i = j
    for v in sv:
        if out[v] != m[v]:
            moved += 1
        m[v] = out[v]
    return m, moved


def build_maps(S, W, max_iter: int = 8):
    """Retourne (mx, my, rapport). mx/my : {valeur Build -> valeur Build sur la grille}."""
    xs = sorted({w['x'] for w in W})
    ys = sorted({w['y'] for w in W})
    mx = {v: round(v / G) * G for v in xs}
    my = {v: round(v / G) * G for v in ys}
    rep = {'iterations': 0, 'ecartees_x': 0, 'ecartees_y': 0, 'contraintes_x': [], 'contraintes_y': []}
    CX, CY = set(), set()          # ACCUMULEES : une paire separee ne doit jamais etre reunie
    for it in range(max_iter):
        cx, cy = find_constraints(S, W, mx, my)
        if not cx and not cy:
            break
        CX |= cx
        CY |= cy
        rep['iterations'] = it + 1
        rep['contraintes_x'] = sorted(CX)
        rep['contraintes_y'] = sorted(CY)
        mx, nx = spread(xs, CX, mx)
        my, ny = spread(ys, CY, my)
        rep['ecartees_x'] += nx
        rep['ecartees_y'] += ny
        if nx == 0 and ny == 0:
            rep['bloque'] = True          # contrainte non satisfiable : signalee par l'appelant
            break
    rep['derive_max_u'] = round(max(max((abs(v - mx[v]) for v in xs), default=0),
                                    max((abs(v - my[v]) for v in ys), default=0)) / G, 3)
    rep['paires_preservees'] = len(rep['contraintes_x']) + len(rep['contraintes_y'])
    return mx, my, rep


def keep_apart_pairs(mx, my, cx, cy):
    """Couples de valeurs SNAPPEES a ne jamais souder (sinon la feature preservee retombe)."""
    kx = {(mx[a], mx[b]) for (a, b) in cx if mx[a] != mx[b]}
    ky = {(my[a], my[b]) for (a, b) in cy if my[a] != my[b]}
    kx |= {(b, a) for (a, b) in kx}
    ky |= {(b, a) for (a, b) in ky}
    return kx, ky

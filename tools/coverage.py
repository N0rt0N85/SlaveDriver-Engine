#!/usr/bin/env python3
"""coverage.py -- le critere « aucun trou dans un sol ou un plafond », commun aux deux
convertisseurs.

Les autres criteres verifient ce qui est EMIS ; celui-ci verifie ce qui MANQUE, et rien d'autre ne
le voit -- compter les faces, comparer deux builds ou croiser sol et plafond laisse passer le cas ou
les DEUX manquent au meme endroit (on voit alors a travers le niveau).

Le juge est l'EMPREINTE de la feuille : une feuille est CONVEXE (BSP pour Doom, decomposition
convexe de convex.py pour Duke), donc son contour au sol est l'enveloppe convexe des extremites de
ses murs verticaux. On echantillonne tous les `pas` u ; un point compte comme peint s'il tombe dans
une face du MEME plan de N'IMPORTE QUELLE feuille, parce que le debord fait precisement peindre un
carre par le voisin. Un plan PARALLAX (ciel) n'emet aucune face : sa feuille est exclue pour ce
plan-la.

MESURE 16-09 (E1M1) : la conversion laisse 560 u2 au sol et 464 u2 au plafond, en eclats de 16 a
128 u2 par feuille -- des bords diagonaux que la grille d'echantillonnage rate, pas des trous. Un
VRAI trou est d'un tout autre ordre : l'election de proprietaire essayee le 16-09 en a creuse un de
3 760 u2 (9,5 % de la feuille 189, sol ET plafond a 0 %). Le seuil est donc a 256 u2 par feuille :
le double du pire eclat, le quinzieme du trou.

GENERIQUE : ne lit que (S, W, V, F) -- les quatre tableaux du bloc niveau. Le JSON de geom3d.py et
le .LEV relu par lev.py portent les MEMES noms de champs (`normal` en 16.16, `flags` 0x40 =
parallax, `v` indexant `vertices`), donc la meme fonction juge les deux convertisseurs.
"""
from __future__ import annotations

import math

SEUIL_TROU = 256.0   # u2 par feuille et par plan : au-dela, c'est un trou, pas un eclat de bord
PAS = 4.0            # u : maille d'echantillonnage
TUILE = 64.0         # u : cote de la cellule, sert a indexer les faces


def enveloppe(pts):
    """Enveloppe convexe (Andrew monotone chain) d'une liste de (x, z)."""
    p = sorted(set(pts))
    if len(p) < 3:
        return p

    def demi(ps):
        h = []
        for q in ps:
            while len(h) >= 2 and ((h[-1][0] - h[-2][0]) * (q[1] - h[-2][1])
                                   - (h[-1][1] - h[-2][1]) * (q[0] - h[-2][0])) <= 0:
                h.pop()
            h.append(q)
        return h
    return demi(p)[:-1] + demi(p[::-1])[:-1]


def dans(poly, x, z):
    """Lancer de rayon : le point (x, z) est-il dans le polygone ?"""
    d = False
    for k in range(len(poly)):
        ax, az = poly[k]
        bx, bz = poly[(k + 1) % len(poly)]
        if (az > z) != (bz > z) and x < ax + (z - az) / float(bz - az) * (bx - ax):
            d = not d
    return d


def trous_de_flats(S, W, V, F, seuil=SEUIL_TROU, pas=PAS):
    """-> (trous, total) ; trous = [(feuille, "sol"|"plafond", aire_u2)], total = [sol, plafond].

    Une feuille apparait dans `trous` des que son plan laisse plus de `seuil` u2 non peints."""
    grille = ({}, {})                       # [sol][carre de 64] -> faces, puis [plafond]
    ciel = (set(), set())                   # feuilles dont CE plan est parallax
    for si, s_ in enumerate(S):
        for wi in range(s_["firstWall"], s_["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] == 0:
                continue
            p = 0 if w["normal"][1] > 0 else 1
            if w["flags"] & 0x40:
                ciel[p].add(si)
                continue
            b = w["firstVertex"]
            if w["firstFace"] < 0 or b >= len(V):
                continue
            for fi in range(w["firstFace"], w["lastFace"] + 1):
                q = [(V[b + i]["x"], V[b + i]["z"]) for i in F[fi]["v"]]
                r = [v for k, v in enumerate(q) if v != q[k - 1]]
                if len(r) < 3:
                    continue
                xs = [v[0] for v in r]
                zs = [v[1] for v in r]
                for gx in range(int(math.floor(min(xs) / TUILE)), int(math.ceil(max(xs) / TUILE))):
                    for gz in range(int(math.floor(min(zs) / TUILE)),
                                    int(math.ceil(max(zs) / TUILE))):
                        grille[p].setdefault((gx, gz), []).append(r)
    trous, total = [], [0.0, 0.0]
    for si, s_ in enumerate(S):
        h = enveloppe([(V[i]["x"], V[i]["z"]) for wi in range(s_["firstWall"], s_["lastWall"] + 1)
                       for i in W[wi]["v"] if W[wi]["normal"][1] == 0 and i < len(V)])
        if len(h) < 3:
            continue
        xs = [v[0] for v in h]
        zs = [v[1] for v in h]
        for p in (0, 1):
            if si in ciel[p]:
                continue
            vide = 0
            x = min(xs) + pas / 2.0
            while x < max(xs):
                z = min(zs) + pas / 2.0
                while z < max(zs):
                    if dans(h, x, z):
                        g = (int(math.floor(x / TUILE)), int(math.floor(z / TUILE)))
                        if not any(dans(q, x, z) for q in grille[p].get(g, ())):
                            vide += 1
                    z += pas
                x += pas
            a = vide * pas * pas
            total[p] += a
            if a > seuil:
                trous.append((si, "sol" if p == 0 else "plafond", int(a)))
    return trous, total


def resume(trous, total, seuil=SEUIL_TROU):
    """La ligne de detail, identique pour les deux verificateurs."""
    if trous:
        return f"{len(trous)} feuilles, ex. {sorted(trous, key=lambda t: -t[2])[:4]}"
    return f"residu {total[0]:.0f} u2 au sol, {total[1]:.0f} u2 au plafond"

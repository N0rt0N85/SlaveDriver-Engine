#!/usr/bin/env python3
"""tuiles.py -- faire tenir un niveau dans son budget de tuiles en fusionnant les plus semblables.

LE MUR. Un niveau PowerSlave indexe ses tuiles de geometrie sur UN OCTET, apres les tuiles d'armes
(`level_texture[i] += tileBase`, `level_face[i].tile += tileBase`, LEVEL.C) : G + tileBase <= 255.
Et le chargeur les garde TOUTES en memoire, 4 096 o chacune (PIC.C, COMPRESS16BPP), dans le meme
pool que le niveau, les sprites et les sequences. MESURE sur les 9 cartes du shareware (2026-09-18,
geometrie des convertisseurs a ce jour) : la RAM est le mur, pas l'octet -- de 83 tuiles (E1M6,
niveau de 572 Ko et 317 Ko de sprites) a 157 (E1M9), pour 132 a 470 tuiles produites.

POURQUOI IL Y EN A TROP. Une tuile est fabriquee POUR SA CELLULE : la meme texture posee sur un mur
de 72 u et sur un de 128 u donne deux tuiles, et une par calage vertical, et une par fenetre d'un
linedef plus etroit que sa texture (doom3d.wall_tex). C'est ce qui rend chaque mur exact ; c'est
aussi ce qui multiplie les tuiles d'une texture -- 128 pour STONE sur E1M5.

LE REMEDE, et ce qu'il ne fait PAS. On ne remplace jamais une texture par une autre : la fusion ne
se fait qu'a l'interieur d'un GROUPE (une texture), donc chaque texture garde au moins une tuile et
aucun contenu ne disparait. A l'interieur d'une texture, on fusionne d'abord les variantes qui se
ressemblent le plus A L'IMAGE -- une texture uniforme en hauteur rend ses calages interchangeables a
cout nul -- ponderees par la surface qu'elles couvrent. Le representant d'un paquet est une de ses
tuiles (medoide), donc au moins un mur reste exact et aucune tuile n'est une moyenne floue.

Cout d'une fusion = somme, sur les tuiles du paquet, de (aire monde couverte) x (ecart moyen au
carre entre l'image de la tuile et celle du representant). Les images sont comparees en vignettes
(moyenne de blocs 4 x 4) : un decalage d'un ou deux texels coute peu, un changement d'echelle
beaucoup -- c'est ce que l'oeil voit aussi.

Les tuiles FIGEES ne sont ni fusionnees ni prises pour representant : interrupteurs (la tuile OFF
doit rester unique dans son secteur, AI2.C constructSwitch) et images de flats animes (indexees par
leurs sequences).

Generique : ne connait ni Doom ni Build. L'appelant fournit les images, les poids, les groupes et
les figees ; `aires` sait lire les poids dans les tableaux .LEV, communs aux deux convertisseurs.
"""
from __future__ import annotations

import heapq
import math

import numpy as np

VIGNETTE = 4        # cote des blocs moyennes avant comparaison (64 -> 16 x 16)


def vignette(rgb, f=VIGNETTE):
    """(64, 64, 3) -> (64/f, 64/f, 3) float32, moyenne de blocs f x f."""
    a = np.asarray(rgb, dtype=np.float32)
    h, w, c = a.shape
    return a.reshape(h // f, f, w // f, f, c).mean(axis=(1, 3))


def aires(walls, vertices, faces, texture, n_tuiles):
    """Aire monde (u2) couverte par chaque tuile de geometrie, lue dans les tableaux .LEV :
    cellules des murs PARALLELOGRAMME (textures, 2 octets par cellule, WALLS.C drawRectWall) et
    faces des autres murs. Les tuiles sans cellule (tuile ON d'un interrupteur, images d'un flat
    anime) valent 0."""
    A = np.zeros(n_tuiles, dtype=np.float64)

    def pt(i):
        v = vertices[i]
        return np.array((v["x"], v["y"], v["z"]), dtype=np.float64)

    for w in walls:
        if w["flags"] & 0x02:                              # INVISIBLE : ni cellule ni face
            continue
        if w["flags"] & 0x01:                              # PARALLELOGRAM
            v0, v1, v2 = pt(w["v"][0]), pt(w["v"][1]), pt(w["v"][2])
            tl, th = w["tileLength"], w["tileHeight"]
            a = np.linalg.norm(np.cross(v1 - v0, v2 - v1)) / max(1, tl * th)
            for c in range(tl * th):
                A[texture[w["textures"] + 2 * c + 1]] += a
        elif w["firstFace"] >= 0:
            base = w["firstVertex"]
            for f in faces[w["firstFace"]:w["lastFace"] + 1]:
                p = [pt(base + k) for k in f["v"]]
                a = 0.5 * (np.linalg.norm(np.cross(p[1] - p[0], p[2] - p[0]))
                           + np.linalg.norm(np.cross(p[2] - p[0], p[3] - p[0])))
                A[f["tile"]] += a
    return A


class _Paquet:
    __slots__ = ("membres", "rep", "cout", "vivant")

    def __init__(self, membres, rep, cout):
        self.membres, self.rep, self.cout, self.vivant = membres, rep, cout, True


def reduire(images, poids, groupes, figees, budget, trace=None):
    """Ramene le nombre de tuiles a `budget` en fusionnant a l'interieur des groupes.

    images  : (N, h, w, 3), deja en vignettes (voir `vignette`)
    poids   : (N,) aire couverte (voir `aires`) ; une tuile de poids nul se fond gratuitement
    groupes : N cles hashables ; on ne fusionne que deux tuiles de meme cle
    figees  : indices jamais fusionnes ni representants
    -> (repr, info). repr[i] = tuile qui remplace i (repr[i] == i : gardee). info : n, garde,
       budget, atteint (bool), plancher (le minimum possible : une tuile par groupe + figees),
       cout (sum poids x ecart), ecart_rms (moyenne ponderee, en niveaux RVB 0..255), et `pires` :
       les 8 fusions les plus cheres [(cout, groupe, tuile, representant)]."""
    N = len(images)
    X = np.asarray(images, dtype=np.float64).reshape(N, -1)
    w = np.asarray(poids, dtype=np.float64)
    figees = set(int(i) for i in figees)
    repr_ = list(range(N))
    par_groupe = {}
    for i in range(N):
        if i not in figees:
            par_groupe.setdefault(groupes[i], []).append(i)
    plancher = len(figees) + len(par_groupe)
    info = dict(n=N, budget=int(budget), plancher=plancher, garde=N, atteint=N <= budget,
                cout=0.0, ecart_rms=0.0, pires=[], fusions=0)
    if N <= budget:
        return repr_, info

    # ecarts moyens au carre, par groupe (tout le reste est a l'infini : jamais fusionne)
    D, locaux = {}, {}
    for g, idx in par_groupe.items():
        Y = X[idx]
        sq = (Y * Y).sum(axis=1)
        d = (sq[:, None] + sq[None, :] - 2.0 * (Y @ Y.T)) / X.shape[1]
        D[g] = np.maximum(d, 0.0)
        locaux[g] = {t: k for k, t in enumerate(idx)}

    def evaluer(g, membres):
        """(rep, cout) du paquet `membres` (indices locaux au groupe) : medoide pondere."""
        m = np.asarray(membres)
        s = w[np.asarray(par_groupe[g])[m]] @ D[g][np.ix_(m, m)]
        k = int(np.argmin(s))
        return membres[k], float(s[k])

    paquets = {g: [_Paquet([k], k, 0.0) for k in range(len(idx))] for g, idx in par_groupe.items()}
    tas = []
    compteur = 0

    def pousser(g, a, b):
        nonlocal compteur
        pa, pb = paquets[g][a], paquets[g][b]
        rep, c = evaluer(g, pa.membres + pb.membres)
        heapq.heappush(tas, (c - pa.cout - pb.cout, compteur, g, a, b, rep, c))
        compteur += 1

    for g, lst in paquets.items():
        for a in range(len(lst)):
            for b in range(a + 1, len(lst)):
                pousser(g, a, b)

    n = N
    while n > budget and tas:
        delta, _, g, a, b, rep, c = heapq.heappop(tas)
        lst = paquets[g]
        if not (lst[a].vivant and lst[b].vivant):
            continue
        lst[a].vivant = lst[b].vivant = False
        nouveau = _Paquet(lst[a].membres + lst[b].membres, rep, c)
        lst.append(nouveau)
        k = len(lst) - 1
        for j, p in enumerate(lst[:-1]):
            if p.vivant:
                pousser(g, j, k)
        n -= 1
        info["fusions"] += 1
    info["atteint"] = n <= budget
    info["garde"] = n

    total, aire = 0.0, 0.0
    detail = []
    for g, lst in paquets.items():
        idx = par_groupe[g]
        for p in lst:
            if not p.vivant:
                continue
            r = idx[p.rep]
            for k in p.membres:
                t = idx[k]
                repr_[t] = r
                e = float(D[g][k, p.rep])
                total += w[t] * e
                aire += w[t]
                if t != r:
                    detail.append((w[t] * e, g, t, r))
    info["cout"] = total
    info["ecart_rms"] = math.sqrt(total / aire) if aire > 0 else 0.0
    info["pires"] = sorted(detail, key=lambda x: -x[0])[:8]
    if trace:
        trace("  tuiles : %d -> %d (budget %d, plancher %d)%s ; ecart RVB moyen pondere par l'aire "
              "%.1f" % (N, n, budget, plancher, "" if info["atteint"] else " -- BUDGET NON ATTEINT",
                        info["ecart_rms"]))
    return repr_, info


def compacter(repr_):
    """repr -> (garder, remap) : les tuiles gardees dans leur ordre d'origine, et pour chaque
    ancienne tuile son NOUVEL index (celui de son representant)."""
    garder = [i for i, r in enumerate(repr_) if r == i]
    nouvel = {t: k for k, t in enumerate(garder)}
    return garder, [nouvel[r] for r in repr_]

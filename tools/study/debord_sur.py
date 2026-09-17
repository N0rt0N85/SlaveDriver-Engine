#!/usr/bin/env python3
"""debord_sur.py -- les 321 faces que `debord.py` dit retirables le sont-elles VRAIMENT ?

CE QUE `debord.py` A PROUVE : en retirant ces 321 faces, l'union geometrique de tout ce qui est
peint ne bouge pas d'un u2. C'est vrai, c'est verifie, et CA NE SUFFIT PAS -- parce que le moteur
ne peint jamais tout le niveau a la fois. Il peint les faces des secteurs VISIBLES. Une face
retiree parce qu'un voisin la recouvre ouvre un trou a chaque point de vue ou ce voisin n'est pas
dessine, et le controle d'union, qui prend toutes les faces ensemble, ne peut pas le voir.

C'est exactement comme ca que l'election d'un proprietaire du 16-09 a creuse 3 760 u2 dans la
feuille 189, sol ET plafond a 0 %. La lecon est ecrite dans le docstring de `dissoudre_penombres` :
« chacun fait RENONCER un morceau a un carre que personne ne repeint ensuite en entier ».

CE QUE CETTE SONDE AJOUTE : le point de vue. Pour chaque face retirable, on prend les autres faces
qui la recouvrent, on note LEURS SECTEURS, et on demande a `ordre.visibilite` s'il existe une
position debout -- et un cap -- ou le secteur de la face est visible alors que les recouvrantes ne
le sont pas assez pour la remplacer. Une seule suffit a condamner la face.

⚠ L'inondation d'`ordre` ne modelise pas l'occlusion : elle voit PLUS que le moteur. Un trou
qu'elle trouve quand meme est donc certain ; une face qu'elle declare sure ne l'est que dans ce
modele. On restreint au moins la vue a un CONE (comme tools/cout.py), sans quoi le test serait
franchement optimiste.

Lancer depuis la racine du fork : python tools\\study\\debord_sur.py [--pas 8] [--duke]
"""
import argparse
import math
import os
import sys

import numpy as np

RACINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RACINE, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cout                                                 # noqa: E402
import debord                                               # noqa: E402
import ordre                                                # noqa: E402

NIVEAUX = (("E1M1", os.path.join(RACINE, "cd_doom", "E1M1.LEV")),
           ("TOMB", os.path.join(RACINE, "cd_duke", "TOMB.LEV")))
MAX_COUVRANTS = 10          # au-dela on ne cherche pas : la face est comptee NON ANALYSEE


def retirables(morceaux, pas):
    """Rejoue le glouton de debord.py -> [(face, plan, groupe)] reellement retirables ensemble."""
    sortie = []
    for plan, liste in enumerate(morceaux):
        if not liste:
            continue
        x0 = min(p[0] for m in liste for p in m["poly"])
        z0 = min(p[1] for m in liste for p in m["poly"])
        masques = [debord.masque(m["poly"], x0, z0, pas) for m in liste]
        groupes = {}
        for i, m in enumerate(liste):
            groupes.setdefault(m["plan"], []).append(i)
        aires = {i: liste[i]["aire"] for i in range(len(liste))}
        for g in groupes.values():
            ii0, jj0, cnt = debord.cumul(g, masques)
            tranches = {}
            for f in g:
                i0, j0, m = masques[f]
                if not m.any():
                    continue
                tranches[f] = ((slice(i0 - ii0, i0 - ii0 + m.shape[0]),
                                slice(j0 - jj0, j0 - jj0 + m.shape[1])), m)
            for f in sorted(tranches, key=lambda k: aires[k]):
                sl, mm = tranches[f]
                if (cnt[sl][mm] >= 2).all():
                    cnt[sl] -= mm
                    sortie.append((f, plan, g, masques, liste))
    return sortie


def couvrants(f, groupe, masques, liste):
    """-> [(secteur, masque des cellules de f qu'il couvre)] pour les AUTRES faces du groupe."""
    i0, j0, mf = masques[f]
    out = []
    for g in groupe:
        if g == f:
            continue
        ig, jg, mg = masques[g]
        di, dj = ig - i0, jg - j0
        inter = np.zeros_like(mf)
        a0, a1 = max(0, di), min(mf.shape[0], di + mg.shape[0])
        b0, b1 = max(0, dj), min(mf.shape[1], dj + mg.shape[1])
        if a0 >= a1 or b0 >= b1:
            continue
        inter[a0:a1, b0:b1] = mg[a0 - di:a1 - di, b0 - dj:b1 - dj]
        inter &= mf
        if inter.any():
            out.append((liste[g]["secteur"], inter))
    return out


def sous_ensembles_couvrants(mf, cvr):
    """-> l'ensemble des masques de bits de `cvr` dont l'union couvre TOUT mf.

    On enumere, parce que le nombre de recouvrantes est petit et qu'une reponse exacte vaut mieux
    qu'une heuristique sur une question de trou."""
    n = len(cvr)
    bons = set()
    for bits in range(1 << n):
        u = np.zeros_like(mf)
        for k in range(n):
            if bits & (1 << k):
                u |= cvr[k][1]
        if u[mf].all():                     # toute cellule de f est prise par l'union
            bons.add(bits)
    return bons


def mesurer(nom, chemin, pas, fov, caps, trace):
    L = debord.charger(chemin)
    S, W, V = L["sectors"], L["walls"], L["vertices"]
    morceaux, compte = debord.recolter(L)
    trace("")
    trace("=== %s : %d faces, %d retenues (comptabilite bouclee : %s)"
          % (nom, compte["faces_fichier"], compte["retenues"], compte["boucle"]))

    R = retirables(morceaux, pas)
    trace("  %d faces retirables au sens de debord.py (pas %d u)" % (len(R), pas))

    # Pour chaque retirable : ses recouvrantes, par secteur, et les sous-ensembles qui suffisent.
    dossiers = []
    trop = 0
    for f, plan, groupe, masques, liste in R:
        mf = masques[f][2]
        cvr = couvrants(f, groupe, masques, liste)
        if not cvr or len(cvr) > MAX_COUVRANTS:
            trop += 1
            continue
        dossiers.append(dict(secteur=liste[f]["secteur"], aire=liste[f]["aire"],
                             secteurs=[c[0] for c in cvr],
                             bons=sous_ensembles_couvrants(mf, cvr)))
    trace("  %d analysees, %d ecartees (aucune recouvrante, ou plus de %d)"
          % (len(dossiers), trop, MAX_COUVRANTS))

    propre = sum(1 for d in dossiers if d["secteur"] not in d["secteurs"])
    trace("  dont %d dont AUCUNE recouvrante n'est dans leur propre secteur -- c'est-a-dire qui "
          "dependent entierement d'un voisin" % propre)

    geo, vues = ordre.visibilite(S, W, V)
    pts = geo[0]
    demi = math.radians(fov) / 2.0
    condamnees = set()
    positions_trouees = 0
    aire_pire = 0.0
    for ex, ez, _s0, vis, _d in vues:
        arcs = {s: ordre._arc(pts, ex, ez, s) for s in vis}
        troue_ici = False
        for k in range(caps):
            h = 2 * math.pi * k / caps
            vue = {s for s in vis
                   if arcs[s] is not None and ordre._croise((h - demi, h + demi), arcs[s])}
            for i, d in enumerate(dossiers):
                if d["secteur"] not in vue:
                    continue
                bits = 0
                for kk, s in enumerate(d["secteurs"]):
                    if s in vue:
                        bits |= (1 << kk)
                if bits not in d["bons"]:
                    condamnees.add(i)
                    troue_ici = True
                    aire_pire = max(aire_pire, d["aire"])
        if troue_ici:
            positions_trouees += 1

    sures = len(dossiers) - len(condamnees)
    trace("  VERDICT : %d faces sur %d restent sures a toutes les positions et tous les caps ; "
          "%d ouvriraient un trou" % (sures, len(dossiers), len(condamnees)))
    trace("            %d positions sur %d verraient au moins un trou ; pire face condamnee "
          "%.0f u2" % (positions_trouees, len(vues), aire_pire))
    cel = sum(1 for _ in range(sures))
    trace("            gain si on ne retire QUE les sures : %d cellules sur le niveau, "
          "%.1f a %.1f ms a 12-18 us" % (cel, cel * 12 / 1000.0, cel * 18 / 1000.0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pas", type=int, default=8)
    ap.add_argument("--fov", type=float, default=cout.FOV)
    ap.add_argument("--caps", type=int, default=cout.CAPS)
    ap.add_argument("--duke", action="store_true")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    for nom, chemin in (NIVEAUX if a.duke else NIVEAUX[:1]):
        if os.path.exists(chemin):
            mesurer(nom, chemin, a.pas, a.fov, a.caps, print)


if __name__ == "__main__":
    main()

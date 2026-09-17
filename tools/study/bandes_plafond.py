#!/usr/bin/env python3
"""bandes.py -- les 46 % de bandes sont-ils une propriete de l'ORDRE D'EMISSION ou de la
TESSELLATION ?

Le moteur soude les faces noires consecutives d'un mur de maillage (WALLS.C weldFaceStrip,
commit 3f8477a) quand elles forment une BANDE : l'arete (v3,v2) de l'une est l'arete (v0,v1) de
la suivante, ou le contraire.  Mesure du commit : 819 + 585 = 1404 jointures sur 3019 faces, dit
« 46 % ».  Le commit suivant (20f185d) pose la question : est-ce deja tout ce que la tessellation
du convertisseur permet, ou bien un tiers des jointures est-il hors de portee (755 ne partagent
qu'un sommet, 304 aucun) ?

La sonde tranche en separant deux choses que le chiffre unique melange :
  - ce que les faces SONT (graphe d'adjacence par arete : la tessellation),
  - dans quel ORDRE le convertisseur les ecrit (la suite firstFace..lastFace).
Une bande = un CHEMIN simple dans ce graphe.  Le plafond d'un reordonnancement est donc la
meilleure couverture du graphe par des chemins ; ce qu'aucun ordre ne rattrapera est ce que le
graphe n'a pas : les composantes, et en particulier les faces isolees.

Trois plafonds sont mesures, du plus contraint au plus libre :
  1. ordre actuel, test moteur actuel                  -> le chiffre de reference
  2. reordonnancement seul, test moteur INCHANGE       -> graphe ORIENTE par le motif (v3,v2)
  3. reordonnancement + adjacence generale             -> graphe NON ORIENTE par arete
Le 2 est gratuit cote moteur ; l'ecart 2 -> 3 est le prix d'un test d'adjacence plus general
(« un eventail, un virage, le debut d'une autre rangee », 20f185d).

MESURE 18-09 (E1M1.LEV) -- les compteurs du commit se recoupent au chiffre pres (565/851 murs
de grille, 437/3019 murs de maillage, 819 + 585 bandes, 421 triangles, 755 et 304).  Le « 46 % »
prend les FACES pour denominateur ; sur les 2582 vraies jointures c'est 54,4 %.  Reponse : ORDRE.
Sur les 1178 jointures ratees, 1144 (97 %) mettent en jeu deux faces qui ont chacune une voisine
par arete AILLEURS dans le mur.  Plafonds : 2094 jointures (81,1 %) en reordonnant seulement, test
moteur inchange ; 2471 (95,7 %) si le test accepte aussi les virages ; borne du graphe 2531
(98,0 %).  Hors de portee : 51 jointures (2,0 %), soit 488 composantes pour 437 murs, dont 100
faces isolees (72 ne partagent meme pas un sommet).  La tessellation n'est donc PAS le mur : le
mur est l'ordre d'ecriture, puis la traversee (36 % des faces a >= 2 voisines ne les ont pas par
des aretes opposees, donc le motif actuel ne peut pas les enchainer).

Chemins d'entree EN DUR, lancer depuis la racine du fork :
    python tools\\study\\bandes.py
"""
from __future__ import annotations

import os
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LEV = os.path.join(ROOT, "cd_doom", "E1M1.LEV")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import lev                                               # noqa: E402

FL_PARALLELOGRAM = 0x01

# --- le test du moteur, a la ligne pres (WALLS.C:weldFaceStrip) -------------------------------
#   dir==1 : a[3]==c[0] && a[2]==c[1]      la bande avance dans le sens v0 -> v3
#   dir==2 : a[0]==c[1] && a[3]==c[2]      ... dans l'autre sens
# `a` est la face qui precede dans le tableau, `c` celle qui suit.  Les indices de F[]['v'] sont
# LOCAUX a wall['firstVertex'] (drawWall transforme level_vertex+firstVertex dans vCalc), donc
# les comparer entre faces d'UN MEME mur est licite tel quel.


def motif(a, c):
    """1 ou 2 si la face `c` peut suivre la face `a` dans une bande, 0 sinon."""
    if a[3] == c[0] and a[2] == c[1]:
        return 1
    if a[0] == c[1] and a[3] == c[2]:
        return 2
    return 0


def aretes(a):
    """Les aretes d'une face, en paires non orientees.  Une face degeneree (triangle ecrit en
    quad, 421 sur 3019) a une arete de longueur nulle : on la jette, il reste le triangle."""
    e = set()
    for i in range(4):
        x, y = a[i], a[(i + 1) % 4]
        if x != y:
            e.add((x, y) if x < y else (y, x))
    return e


def slots(a, ens):
    """Les FENTES (0 = v0v1, 1 = v1v2, 2 = v2v3, 3 = v3v0) de la face `a` dont l'arete est dans
    `ens`."""
    out = set()
    for i in range(4):
        x, y = a[i], a[(i + 1) % 4]
        if x != y and ((x, y) if x < y else (y, x)) in ens:
            out.add(i)
    return out


def dans(poly, x, z):
    """Lancer de rayon, comme coverage.dans : le point est-il dans le polygone (x, z) ?"""
    d = False
    for k in range(len(poly)):
        ax, az = poly[k]
        bx, bz = poly[(k + 1) % len(poly)]
        if (az > z) != (bz > z) and x < ax + (z - az) / float(bz - az) * (bx - ax):
            d = not d
    return d


def composantes(n, adj):
    vu = [False] * n
    out = []
    for s in range(n):
        if vu[s]:
            continue
        pile, c = [s], []
        vu[s] = True
        while pile:
            u = pile.pop()
            c.append(u)
            for v in adj[u]:
                if not vu[v]:
                    vu[v] = True
                    pile.append(v)
        out.append(c)
    return out


# --- couverture par chemins, graphe NON ORIENTE -----------------------------------------------
# Glouton « degre restant minimal », multi-depart : on essaie CHAQUE sommet de la composante
# comme depart et on garde le meilleur.  Le maximum exact est NP-dur (chemin hamiltonien), mais la
# borne haute est triviale et se calcule a cote : une composante de n faces ne peut pas donner
# plus de n-1 jointures.  Quand le glouton colle a la borne, la question est close.

def glouton_chemins(sommets, adj):
    """-> nombre de jointures d'une couverture par chemins de la composante `sommets`."""
    best = -1
    for depart in sommets:
        reste = {u: set(adj[u]) for u in sommets}
        libre = set(sommets)
        joints = 0
        u = depart
        while True:
            libre.discard(u)
            for v in reste[u]:
                reste[v].discard(u)
            while True:
                cand = [v for v in reste[u] if v in libre]
                if not cand:
                    break
                v = min(cand, key=lambda x: (len(reste[x]), x))
                joints += 1
                libre.discard(v)
                for w in reste[v]:
                    reste[w].discard(v)
                u = v
            if not libre:
                break
            u = min(libre, key=lambda x: (len(reste[x]), x))
        if joints > best:
            best = joints
        if best == len(sommets) - 1:
            break                       # la borne est atteinte, inutile de chercher plus loin
    return best


# --- couverture par chemins, graphe ORIENTE (le test moteur actuel) ---------------------------
# Un chemin oriente = une suite de faces consecutives que weldFaceStrip accepte telle quelle.
# Couplage biparti maximal (Kuhn) : chaque face a une place de « precedent » et une de
# « suivant », le couplage donne le nombre maximal de jointures ; les cycles qu'il peut fabriquer
# coutent une jointure chacun (il faut bien couper la boucle pour en faire une suite).

def couplage(n, arcs):
    """-> (taille du couplage, successeur[]) ; arcs[i] = liste des j tels que i -> j."""
    suiv = [-1] * n
    prec = [-1] * n

    def augmente(u, vu):
        for v in arcs[u]:
            if vu[v]:
                continue
            vu[v] = True
            if prec[v] < 0 or augmente(prec[v], vu):
                prec[v] = u
                suiv[u] = v
                return True
        return False

    taille = 0
    for u in range(n):
        if arcs[u] and augmente(u, [False] * n):
            taille += 1
    return taille, suiv


def cycles(n, suiv):
    """Nombre de cycles dans le graphe fonctionnel `suiv` (chaque sommet a <= 1 successeur)."""
    etat = [0] * n                      # 0 neuf, 1 en cours, 2 fini
    nb = 0
    for s in range(n):
        if etat[s]:
            continue
        chemin = []
        u = s
        while u >= 0 and etat[u] == 0:
            etat[u] = 1
            chemin.append(u)
            u = suiv[u]
        if u >= 0 and etat[u] == 1:
            nb += 1
        for x in chemin:
            etat[x] = 2
    return nb


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    sys.setrecursionlimit(10000)
    r = lev.Reader(LEV)
    lev.parse_sky(r)
    L = lev.parse_level_block(r)
    W, V, F = L["walls"], L["vertices"], L["faces"]
    print("bandes : %s -- %d murs, %d sommets, %d faces"
          % (os.path.basename(LEV), len(W), len(V), len(F)))

    # 0. CONTROLE DES COMPTEURS contre le commit 3f8477a : « 565 grid walls carry 851 cells,
    #    437 mesh walls carry 3019 faces ».  Grille = WALLFLAG_PARALLELOGRAM (drawRectWall,
    #    WALLS.C:2106), maillage = le reste avec des faces (drawWall, :2109).
    grille = [w for w in W if w["flags"] & FL_PARALLELOGRAM]
    maille = [w for w in W if not (w["flags"] & FL_PARALLELOGRAM) and w["firstFace"] >= 0]
    cellules = sum(w["tileLength"] * w["tileHeight"] for w in grille)
    nf = sum(w["lastFace"] - w["firstFace"] + 1 for w in maille)
    horiz = sum(1 for w in maille if w["normal"][1] != 0)
    print("  grille  : %d murs, %d cellules      (commit : 565 / 851  %s)"
          % (len(grille), cellules, "OK" if (len(grille), cellules) == (565, 851) else "ECART"))
    print("  maillage: %d murs, %d faces         (commit : 437 / 3019 %s)"
          % (len(maille), nf, "OK" if (len(maille), nf) == (437, 3019) else "ECART"))
    print("  dont horizontaux (sol/plafond) : %d / %d murs" % (horiz, len(maille)))

    # UN SEUL PARCOURS : l'ordre actuel (1), le graphe oriente (2), le graphe par arete (3, 4).
    d1 = d2 = njoint = 0
    triangles = 0
    cat = Counter()
    rate_mais_voisines = 0
    borne = glout = 0
    dir_couple = dir_cycles = 0
    isolees = un_sommet = zero_sommet = 0
    tailles = Counter()
    paires = paires_motif = 0
    recouvre = 0
    traversables = virages = 0
    borne_coord = 0
    for w in maille:
        f0, f1 = w["firstFace"], w["lastFace"]
        n = f1 - f0 + 1
        E = [aretes(F[f0 + i]["v"]) for i in range(n)]
        Sv = [set(F[f0 + i]["v"]) for i in range(n)]
        # Reordonner n'est gratuit que si les faces d'un meme mur ne se RECOUVRENT pas : elles
        # sont coplanaires (les 437 murs de maillage sont horizontaux), donc si deux d'entre
        # elles se chevauchaient, l'ordre de peinture se verrait.  Test : le barycentre d'une
        # face tombe-t-il dans une autre ?
        b = w["firstVertex"]
        qs = [[(V[b + k]["x"], V[b + k]["z"]) for k in F[f0 + i]["v"]] for i in range(n)]
        for i in range(n):
            cx = sum(p[0] for p in qs[i]) / 4.0
            cz = sum(p[1] for p in qs[i]) / 4.0
            for j in range(n):
                if i != j and dans(qs[j], cx, cz):
                    recouvre += 1
                    break
        # Variante COORDONNEES : et si le convertisseur avait simplement ecrit deux fois le meme
        # point, separant par des indices des faces geometriquement collees ?  On refait le meme
        # graphe en identifiant les sommets par leur position ; si la borne ne bouge pas, cette
        # hypothese-la est morte.
        Ec = [set() for _ in range(n)]
        for i in range(n):
            a = F[f0 + i]["v"]
            for k in range(4):
                p = (V[b + a[k]]["x"], V[b + a[k]]["y"], V[b + a[k]]["z"])
                q = (V[b + a[(k + 1) % 4]]["x"], V[b + a[(k + 1) % 4]]["y"],
                     V[b + a[(k + 1) % 4]]["z"])
                if p != q:
                    Ec[i].add((p, q) if p < q else (q, p))
        adjc = [set() for _ in range(n)]
        for i in range(n):
            for j in range(i + 1, n):
                if Ec[i] & Ec[j]:
                    adjc[i].add(j)
                    adjc[j].add(i)
        for c in composantes(n, adjc):
            borne_coord += len(c) - 1

        adj = [set() for _ in range(n)]
        arcs = [[] for _ in range(n)]
        fentes = [set() for _ in range(n)]
        for i in range(n):
            if len(Sv[i]) < 4:
                triangles += 1
            for j in range(n):
                if i != j and motif(F[f0 + i]["v"], F[f0 + j]["v"]):
                    arcs[i].append(j)
            for j in range(i + 1, n):
                if E[i] & E[j]:
                    adj[i].add(j)
                    adj[j].add(i)
                    paires += 1
                    if motif(F[f0 + i]["v"], F[f0 + j]["v"]) or motif(F[f0 + j]["v"], F[f0 + i]["v"]):
                        paires_motif += 1
                    # Par quelle FENTE (v0v1, v1v2, v2v3, v3v0) chaque face voit-elle l'autre ?
                    # Le motif ne sait TRAVERSER une face que par deux aretes OPPOSEES (entrer
                    # par v0v1 et sortir par v2v3, ou entrer par v1v2 et sortir par v3v0) : un
                    # virage, lui, entre et sort par deux aretes VOISINES.
                    fentes[i] |= slots(F[f0 + i]["v"], E[i] & E[j])
                    fentes[j] |= slots(F[f0 + j]["v"], E[i] & E[j])
        # 1. l'ordre que le convertisseur a ecrit
        for i in range(n - 1):
            a, c = F[f0 + i]["v"], F[f0 + i + 1]["v"]
            njoint += 1
            m = motif(a, c)
            if m == 1:
                d1 += 1
            elif m == 2:
                d2 += 1
            if m:
                cat["bande (motif moteur)"] += 1
                continue
            if E[i] & E[i + 1]:
                cat["arete complete, hors motif"] += 1
            else:
                p = len(Sv[i] & Sv[i + 1])
                cat["1 sommet" if p == 1 else ("0 sommet" if p == 0 else "2 sommets sans arete")] += 1
            # la jointure est ratee : les DEUX faces ont-elles malgre tout une voisine par
            # arete ailleurs dans le mur ?  Si oui, c'est l'ordre qui les a separees.
            if adj[i] and adj[i + 1]:
                rate_mais_voisines += 1
        # 3 et 4. ce que le graphe par arete permet
        for c in composantes(n, adj):
            tailles[len(c)] += 1
            borne += len(c) - 1
            glout += glouton_chemins(c, adj)
        # 2. ce que le test moteur actuel permet, en reordonnant seulement
        t, suiv = couplage(n, arcs)
        dir_couple += t
        dir_cycles += cycles(n, suiv)
        for i in range(n):
            if not adj[i]:
                isolees += 1
                if any(Sv[i] & Sv[j] for j in range(n) if j != i):
                    un_sommet += 1
                else:
                    zero_sommet += 1
            elif len(adj[i]) >= 2:
                traversables += 1
                if not ({0, 2} <= fentes[i] or {1, 3} <= fentes[i]):
                    virages += 1
    bande = d1 + d2
    dirige = dir_couple - dir_cycles

    print("\n1. ORDRE D'EMISSION ACTUEL, test moteur actuel")
    print("   %d jointures consecutives (faces - murs = %d - %d)" % (njoint, nf, len(maille)))
    print("   bandes : %d (sens v0->v3) + %d (inverse) = %d   (commit : 819 + 585 %s)"
          % (d1, d2, bande, "OK" if (d1, d2) == (819, 585) else "ECART"))
    print("   soit %.1f %% des JOINTURES, ou %.1f %% des FACES (le « 46 %% » du commit prend les"
          " faces pour denominateur)" % (100.0 * bande / njoint, 100.0 * bande / nf))
    print("   triangles ecrits en quad : %d (commit : 421 %s)"
          % (triangles, "OK" if triangles == 421 else "ECART"))
    for k, v in cat.most_common():
        print("      %-28s %4d" % (k, v))
    print("   sur les %d jointures ratees, %d (%.1f %%) mettent en jeu DEUX faces qui ont chacune"
          " une voisine par arete ailleurs dans le mur : ce n'est pas la tessellation qui manque,"
          " c'est l'ordre" % (njoint - bande, rate_mais_voisines,
                              100.0 * rate_mais_voisines / (njoint - bande)))

    print("\n2. REORDONNANCEMENT SEUL, test moteur INCHANGE (graphe oriente par le motif)")
    print("   %d jointures  = %.1f %% des jointures (%.1f %% des faces)   [couplage %d - %d cycles]"
          % (dirige, 100.0 * dirige / njoint, 100.0 * dirige / nf, dir_couple, dir_cycles))
    print("\n3. REORDONNANCEMENT + ADJACENCE GENERALE (graphe non oriente par arete)")
    print("   glouton multi-depart : %d jointures = %.1f %% des jointures (%.1f %% des faces)"
          % (glout, 100.0 * glout / njoint, 100.0 * glout / nf))
    print("   borne haute (faces - composantes) : %d = %.1f %% des jointures"
          % (borne, 100.0 * borne / njoint))
    print("   meme borne en identifiant les sommets par leur POSITION : %d (%s -- %s)"
          % (borne_coord, "identique" if borne_coord == borne else "DIFFERENTE",
             "aucun sommet double ne separe deux faces collees" if borne_coord == borne
             else "des sommets doubles separent des faces collees"))
    print("   paires de faces adjacentes par arete : %d, dont %d (%.1f %%) que le motif moteur"
          " sait deja exprimer dans un sens ou l'autre"
          % (paires, paires_motif, 100.0 * paires_motif / paires))
    print("   la contrainte qui reste n'est donc pas la PAIRE mais la TRAVERSEE : sur %d faces a"
          " >= 2 voisines, %d (%.1f %%) n'ont pas deux voisines par des aretes OPPOSEES -- le"
          " motif ne peut pas passer au travers, il faudrait un virage"
          % (traversables, virages, 100.0 * virages / traversables))

    print("\n4. HORS DE PORTEE DE TOUT REORDONNANCEMENT")
    print("   %d composantes pour %d faces sur %d murs -> %d jointures perdues d'office"
          " (composantes - murs), soit %.1f %% des jointures"
          % (sum(tailles.values()), nf, len(maille), sum(tailles.values()) - len(maille),
             100.0 * (sum(tailles.values()) - len(maille)) / njoint))
    print("   faces isolees (composante de taille 1) : %d, dont %d partagent un SOMMET avec une"
          " autre face du mur et %d rien du tout" % (isolees, un_sommet, zero_sommet))
    print("   tailles de composantes : " + ", ".join(
        "%dx%d" % (v, k) for k, v in sorted(tailles.items())))
    print("   faces dont le barycentre tombe dans une autre face du MEME mur : %d"
          " (0 = reordonner ne change rien a l'image, les faces d'un mur pavent leur plan)"
          % recouvre)

    print("\n5. RECAPITULATIF, en jointures sur %d" % njoint)
    for nom, val in (("ordre actuel", bande), ("reordonne, test actuel", dirige),
                     ("reordonne, adjacence generale", glout), ("borne haute du graphe", borne)):
        print("   %-32s %5d  %5.1f %%   (+%d vs ordre actuel)"
              % (nom, val, 100.0 * val / njoint, val - bande))


if __name__ == "__main__":
    main()

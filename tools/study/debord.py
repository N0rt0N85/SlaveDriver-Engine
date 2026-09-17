#!/usr/bin/env python3
"""debord.py -- ce que coute le DEBORD des sols et plafonds.

La regle en place (docstring de `dissoudre_penombres`, doom3d.py:1533-1537) est que CHAQUE morceau
qui touche un carre de 64 repeint le carre ENTIER. C'est ce qui garantit l'absence de trou -- les
trois elections d'un proprietaire essayees le 16-09 ont creusees jusqu'a 3 760 u2, sol ET plafond a
0 %. Cette sonde ne discute pas la regle : elle CHIFFRE ce qu'elle coute, pour que l'arbitrage se
fasse sur un nombre et pas sur une impression.

Quatre mesures, separement pour le plan SOL et le plan PLAFOND :
  1. aire peinte (avec multiplicite) contre aire couverte (union) -> facteur de redondance ;
  2. distribution du recouvrement : par combien de faces distinctes chaque point est peint ;
  3. faces ENTIEREMENT couvertes par l'union des AUTRES, en deux bornes -- geometrique (memes
     normale et d, donc meme plan) et stricte (meme plan ET meme tuile) ;
  4. par carre de 64 : combien de morceaux distincts le repeignent, et la queue de la distribution.

DEUX UNIONS, et elles ne disent pas la meme chose. Le debord fait deborder un morceau de sol sur le
carre du voisin, qui est A UNE AUTRE HAUTEUR : les deux faces se recouvrent en projection XZ sans
etre redondantes (chacune est la seule a peindre SON cote). Seule la redondance DANS UN MEME PLAN
est recuperable ; l'union globale XZ est donnee a cote pour montrer l'ecart, pas pour en conclure.

Conventions reprises telles quelles de `coverage.py` (ne pas les reinventer) : sol = normal[1] > 0,
plafond = normal[1] < 0, ciel = flags & 0x40 (n'emet aucune face), faces de firstFace a lastFace,
indices de sommet LOCAUX a ajouter a firstVertex, doublons consecutifs retires cycliquement, test de
point interieur = lancer de rayon pair-impair `x < ax + (z-az)/(bz-az)*(bx-ax)`.

Le plan d'une face est celui de son mur : (normal, d), avec n.p + d = 0 (VERIFIE : pour un sol a
y=32, normal=[0,65536,0] et d=-2097152 = -65536*32). C'est exact pour un sol plat comme pour une
pente Duke, la ou la seule hauteur Y ne l'est pas.

Ce qui est MESURE : les aires, le recouvrement, le nombre de faces couvertes. Ce qui est EXTRAPOLE :
la conversion en millisecondes, qui suppose 24 us par face retiree (marge mesuree de la loi de cout)
ET que la face serait visible a l'image -- c'est donc un MAJORANT du gain par image, pas un gain.

Usage : python tools/study/debord.py     (chemins en dur, lancer depuis la racine du fork)
"""
from __future__ import annotations

import math
import os
import sys
from collections import Counter, defaultdict

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import lev                                                # noqa: E402

NIVEAUX = [("E1M1  (doom2ps)", os.path.join(ROOT, "cd_doom", "E1M1.LEV")),
           ("TOMB  (duke2ps)", os.path.join(ROOT, "cd_duke", "TOMB.LEV"))]
PAS_LISTE = (4, 2)          # u : maille d'echantillonnage, puis la moitie (controle de stabilite)
TUILE = 64                  # u : cote du carre du debord
COUT_FACE_US = 24.0         # us : cout MARGINAL d'une cellule dessinee (loi de cout SlaveDriver)
PLANS = ("sol", "plafond")


# --------------------------------------------------------------------------- lecture

def charger(chemin):
    r = lev.Reader(chemin)
    lev.parse_sky(r)
    return lev.parse_level_block(r)


def contour(V, base, face):
    """Contour XZ d'une face : sommets LOCAUX + firstVertex, doublons consecutifs retires.

    Le retrait est CYCLIQUE (`q[k - 1]` avec k=0 designe le dernier), exactement comme
    coverage.py:83 : les triangles sont encodes en quads soit [A,B,C,C] soit [A,B,C,A], et les deux
    formes se reduisent au meme triangle."""
    q = [(V[base + i]["x"], V[base + i]["z"]) for i in face["v"]]
    return [p for k, p in enumerate(q) if p != q[k - 1]]


def aire_polygone(poly):
    """Aire du contour XZ (lacet de Gauss). Projetee : pour une pente, elle sous-estime la surface
    reelle, ce qui est dit dans le rapport."""
    a = 0.0
    for k in range(len(poly)):
        x1, z1 = poly[k]
        x2, z2 = poly[(k + 1) % len(poly)]
        a += x1 * z2 - x2 * z1
    return abs(a) / 2.0


def recolter(L):
    """-> (morceaux, compte) : morceaux[plan] = liste de faces retenues, compte = la comptabilite.

    `compte` doit boucler : retenues + ciel + verticales + degenerees + hors_portee == nmFaces."""
    S, W, V, F = L["sectors"], L["walls"], L["vertices"], L["faces"]
    morceaux = ([], [])
    c = Counter()
    murs_vus = Counter()
    faces_vues = Counter()

    for si, s in enumerate(S):
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            murs_vus[wi] += 1
            w = W[wi]
            nf = 0 if w["firstFace"] < 0 else w["lastFace"] - w["firstFace"] + 1
            for fi in range(w["firstFace"], w["firstFace"] + nf):
                faces_vues[fi] += 1
            if w["normal"][1] == 0:
                c["verticales"] += nf
                continue
            if w["flags"] & 0x40:
                c["ciel"] += nf              # un plan PARALLAX n'emet aucune face
                continue
            base = w["firstVertex"]
            if w["firstFace"] < 0 or base >= len(V):
                c["hors_portee"] += nf
                continue
            plan = 0 if w["normal"][1] > 0 else 1
            cle = (tuple(w["normal"]), w["d"])
            for fi in range(w["firstFace"], w["lastFace"] + 1):
                poly = contour(V, base, F[fi])
                if len(poly) < 3 or aire_polygone(poly) <= 0.0:
                    c["degenerees"] += 1
                    continue
                morceaux[plan].append(dict(mur=wi, secteur=si, face=fi, poly=poly, plan=cle,
                                           tuile=F[fi]["tile"], aire=aire_polygone(poly)))
                c["retenues"] += 1
    c["faces_fichier"] = len(F)
    c["murs_du_fichier"] = len(W)
    c["murs_atteints"] = len(murs_vus)
    c["murs_en_double"] = sum(1 for v in murs_vus.values() if v > 1)
    c["faces_atteintes"] = len(faces_vues)
    c["faces_en_double"] = sum(1 for v in faces_vues.values() if v > 1)
    c["boucle"] = (c["retenues"] + c["ciel"] + c["verticales"] + c["degenerees"]
                   + c["hors_portee"] == len(F))
    return morceaux, c


# --------------------------------------------------------------------------- echantillonnage

def masque(poly, x0, z0, pas):
    """-> (i0, j0, masque) : les centres de cellule DANS le polygone, sur la maille `pas`.

    Meme juge que coverage.dans (lancer de rayon pair-impair), vectorise. Les aretes horizontales
    sont sautees a la main : dans coverage.dans le `and` court-circuite deja le quotient, ici il
    faudrait diviser par zero."""
    xs = [p[0] for p in poly]
    zs = [p[1] for p in poly]
    i0 = int(math.floor((min(xs) - x0) / pas))
    i1 = int(math.ceil((max(xs) - x0) / pas))
    j0 = int(math.floor((min(zs) - z0) / pas))
    j1 = int(math.ceil((max(zs) - z0) / pas))
    if i1 <= i0 or j1 <= j0:
        return i0, j0, np.zeros((0, 0), dtype=bool)
    X = (x0 + (np.arange(i0, i1) + 0.5) * pas)[:, None]
    Z = (z0 + (np.arange(j0, j1) + 0.5) * pas)[None, :]
    d = np.zeros((i1 - i0, j1 - j0), dtype=bool)
    n = len(poly)
    for k in range(n):
        ax, az = poly[k]
        bx, bz = poly[(k + 1) % n]
        if az == bz:
            continue
        d ^= ((az > Z) != (bz > Z)) & (X < ax + (Z - az) / float(bz - az) * (bx - ax))
    return i0, j0, d


def cumul(indices, masques):
    """Somme des masques d'un groupe de faces dans une boite locale -> (i0, j0, compteur)."""
    ii0 = min(masques[f][0] for f in indices)
    jj0 = min(masques[f][1] for f in indices)
    ii1 = max(masques[f][0] + masques[f][2].shape[0] for f in indices)
    jj1 = max(masques[f][1] + masques[f][2].shape[1] for f in indices)
    cnt = np.zeros((ii1 - ii0, jj1 - jj0), dtype=np.int32)
    for f in indices:
        i0, j0, m = masques[f]
        if m.size:
            cnt[i0 - ii0:i0 - ii0 + m.shape[0], j0 - jj0:j0 - jj0 + m.shape[1]] += m
    return ii0, jj0, cnt


def couvertes(masques, groupes, aires):
    """-> (indep, glouton_pt, glouton_gd, vide) : les faces couvertes par l'union des AUTRES.

    `groupes` = liste de listes d'indices ; une face est couverte si TOUTES ses cellules
    echantillonnees portent un compteur >= 2 dans SON groupe (elle-meme compte pour 1, donc >= 2
    veut dire qu'au moins une autre face du groupe la recouvre la aussi).

    TROIS comptes, et ils ne disent pas la meme chose :
      `indep`  = le critere pose face par face, chacune contre TOUTES les autres. C'est un MAJORANT
                 qui SE DOUBLE-COMPTE : deux faces identiques se couvrent mutuellement, donc les
                 DEUX sont declarees couvertes alors qu'on ne peut en retirer qu'une. Controle
                 immediat : l'aire de `indep` peut depasser l'exces peint-moins-union, ce qui est
                 geometriquement impossible pour un retrait reel.
      `glouton_pt` / `glouton_gd` = un ensemble REELLEMENT retirable en meme temps, construit en
                 retirant les faces l'une apres l'autre (compteur decremente a chaque retrait), dans
                 l'ordre des aires croissantes puis decroissantes. Les compteurs ne font que baisser,
                 donc une seule passe suffit : l'ensemble obtenu est MAXIMAL. L'ordre change le
                 resultat, d'ou les deux, qui encadrent ce que vaut l'heuristique.

    Une face sans aucune cellule echantillonnee (plus fine que le pas) n'est PAS declaree couverte :
    un echantillon vide ne prouve rien, et la compter donnerait un gisement invente."""
    indep, vide = [], 0
    glouton = ([], [])
    for g in groupes:
        if not g:
            continue
        ii0, jj0, cnt0 = cumul(g, masques)
        tranches = {}
        for f in g:
            i0, j0, m = masques[f]
            if not m.any():
                vide += 1
                continue
            tranches[f] = ((slice(i0 - ii0, i0 - ii0 + m.shape[0]),
                            slice(j0 - jj0, j0 - jj0 + m.shape[1])), m)
            sl, mm = tranches[f]
            if (cnt0[sl][mm] >= 2).all():
                indep.append(f)
        for sens in (0, 1):
            cnt = cnt0.copy()
            for f in sorted(tranches, key=lambda k: aires[k], reverse=bool(sens)):
                sl, mm = tranches[f]
                if (cnt[sl][mm] >= 2).all():
                    cnt[sl] -= mm
                    glouton[sens].append(f)
    return indep, glouton[0], glouton[1], vide


# --------------------------------------------------------------------------- mesure

def mesurer(chemin, pas):
    L = charger(chemin)
    morceaux, compte = recolter(L)
    res = dict(compte=compte, pas=pas, plans={})
    tous = morceaux[0] + morceaux[1]
    if not tous:
        return res
    x0 = int(math.floor(min(p[0] for m in tous for p in m["poly"]) / TUILE)) * TUILE
    z0 = int(math.floor(min(p[1] for m in tous for p in m["poly"]) / TUILE)) * TUILE
    x1 = int(math.ceil(max(p[0] for m in tous for p in m["poly"]) / TUILE)) * TUILE
    z1 = int(math.ceil(max(p[1] for m in tous for p in m["poly"]) / TUILE)) * TUILE
    res["boite"] = (x0, x1, z0, z1)

    for plan in (0, 1):
        M = morceaux[plan]
        r = dict(faces=len(M), aire_exacte=sum(m["aire"] for m in M))
        if not M:
            res["plans"][PLANS[plan]] = r
            continue
        masques = [masque(m["poly"], x0, z0, pas) for m in M]

        # -- 1 et 2 : aire peinte, union par plan, union globale, histogramme du recouvrement
        par_plan = defaultdict(list)
        par_plan_tuile = defaultdict(list)
        for i, m in enumerate(M):
            par_plan[m["plan"]].append(i)
            par_plan_tuile[(m["plan"], m["tuile"])].append(i)
        hist = Counter()
        cell_union_plan = 0
        cell_peintes = 0
        for cle, idx in par_plan.items():
            _, _, cnt = cumul(idx, masques)
            v, n = np.unique(cnt[cnt > 0], return_counts=True)
            for a, b in zip(v.tolist(), n.tolist()):
                hist[a] += b
            cell_union_plan += int((cnt > 0).sum())
            cell_peintes += int(cnt.sum())
        glob = np.zeros(((x1 - x0) // pas, (z1 - z0) // pas), dtype=np.int32)
        for i0, j0, m in masques:
            if m.size:
                glob[i0:i0 + m.shape[0], j0:j0 + m.shape[1]] += m
        hist_glob = Counter()
        v, n = np.unique(glob[glob > 0], return_counts=True)
        for a, b in zip(v.tolist(), n.tolist()):
            hist_glob[a] += b
        cell_union_glob = int((glob > 0).sum())
        a = float(pas * pas)
        # CONTROLE : la grille stricte doit vraiment etre plus fine que la geometrique, sinon
        # "borne stricte == borne geometrique" ne voudrait rien dire.
        multi = sum(1 for cle, idx in par_plan.items()
                    if len(set(M[i]["tuile"] for i in idx)) > 1)
        r.update(aire_peinte=cell_peintes * a, aire_union_plan=cell_union_plan * a,
                 aire_union_globale=cell_union_glob * a, hist=hist, hist_glob=hist_glob,
                 plans_distincts=len(par_plan), couples_plan_tuile=len(par_plan_tuile),
                 plans_multi_tuiles=multi)

        # -- 3 : faces entierement couvertes par les autres
        aires = [m["aire"] for m in M]
        par_mur = defaultdict(list)
        for i, m in enumerate(M):
            par_mur[(m["plan"], m["tuile"], m["mur"])].append(i)
        retire = None
        for nom, gr in (("geo", par_plan), ("strict", par_plan_tuile), ("temoin", par_mur)):
            ind, gp, gg, vide = couvertes(masques, list(gr.values()), aires)
            r["couv_" + nom] = len(ind)
            r["aire_" + nom] = sum(aires[i] for i in ind)
            r["glouton_" + nom] = (len(gp), len(gg))
            r["aire_glouton_" + nom] = (sum(aires[i] for i in gp), sum(aires[i] for i in gg))
            if nom == "geo":
                r["sans_echantillon"] = vide
            if nom == "strict":
                retire = set(gp)

        # CONTROLE de bout en bout : on REFAIT l'union sans les faces que le glouton a retirees.
        # Si le retrait est bien neutre pour l'image, l'union ne doit pas bouger d'une cellule --
        # c'est la seule verification qui ne repose pas sur le raisonnement qui a produit la liste.
        reste = 0
        for cle, idx in par_plan.items():
            garde = [i for i in idx if i not in retire]
            if not garde:
                continue
            _, _, cnt = cumul(garde, masques)
            reste += int((cnt > 0).sum())
        r["union_apres"] = reste * a
        r["union_intacte"] = (reste * a == r["aire_union_plan"])

        # -- 4 : par carre de 64, tous plans confondus puis PLAN PAR PLAN (le debord n'est accorde
        #        qu'a un carre tenant dans un seul secteur, donc a un seul plan : c'est la deuxieme
        #        colonne qui chiffre la regle elle-meme)
        q = TUILE // pas
        carre_faces = defaultdict(int)
        carre_murs = defaultdict(set)
        carre_murs_plan = defaultdict(set)
        for i, (i0, j0, m) in enumerate(masques):
            if not m.any():
                continue
            ii, jj = np.nonzero(m)
            gi = (i0 + ii) // q
            gj = (j0 + jj) // q
            for k in np.unique(gi.astype(np.int64) * 100000 + gj).tolist():
                carre_faces[k] += 1
                carre_murs[k].add(M[i]["mur"])
                carre_murs_plan[(M[i]["plan"], k)].add(M[i]["mur"])
        r["hist_carre_faces"] = Counter(carre_faces.values())
        r["hist_carre_murs"] = Counter(len(s) for s in carre_murs.values())
        r["hist_carre_murs_plan"] = Counter(len(s) for s in carre_murs_plan.values())
        r["carres"] = len(carre_faces)
        pire = max(carre_murs, key=lambda k: len(carre_murs[k]))
        r["pire_carre"] = (int(pire // 100000) * TUILE + x0, int(pire % 100000) * TUILE + z0,
                           len(carre_murs[pire]), carre_faces[pire])
        res["plans"][PLANS[plan]] = r
    return res


# --------------------------------------------------------------------------- rapport

def ligne_hist(h, maxi=8):
    tot = sum(h.values())
    bouts = []
    for k in sorted(h):
        if k > maxi:
            break
        bouts.append("%dx:%.1f%%" % (k, 100.0 * h[k] / tot))
    queue = sum(v for k, v in h.items() if k > maxi)
    if queue:
        bouts.append(">%dx:%.2f%%" % (maxi, 100.0 * queue / tot))
    return "  ".join(bouts) + "   (max %d)" % max(h)


def identite(chemin):
    """Le .LEV est REGENERE a chaque build du disque : sans son empreinte, un chiffre de cette
    sonde ne se rattache a rien (constate le 18-09, le fichier a change EN COURS DE MESURE)."""
    import hashlib
    import time
    h = hashlib.sha1(open(chemin, "rb").read()).hexdigest()[:12]
    st = os.stat(chemin)
    return "%d o, %s, sha1 %s" % (st.st_size,
                                  time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(st.st_mtime)), h)


def rapport(nom, chemin):
    print("=" * 96)
    print("%s -- %s" % (nom, chemin))
    print("  ENTREE : %s" % identite(chemin))
    resultats = {}
    for pas in PAS_LISTE:
        resultats[pas] = mesurer(chemin, pas)
    c = resultats[PAS_LISTE[0]]["compte"]
    print("  CONTROLE : %d faces au fichier = %d retenues + %d ciel + %d verticales + %d degenerees"
          " + %d hors portee  -> %s"
          % (c["faces_fichier"], c["retenues"], c["ciel"], c["verticales"], c["degenerees"],
             c["hors_portee"], "BOUCLE" if c["boucle"] else "*** NE BOUCLE PAS ***"))
    print("  CONTROLE : %d murs au fichier, %d atteints par un secteur (%d en double) ; "
          "%d faces atteintes (%d en double)"
          % (c["murs_du_fichier"], c["murs_atteints"], c["murs_en_double"], c["faces_atteintes"],
             c["faces_en_double"]))
    b = resultats[PAS_LISTE[0]]["boite"]
    print("  boite XZ [%d..%d] x [%d..%d] = %d u2" % (b[0], b[1], b[2], b[3],
                                                      (b[1] - b[0]) * (b[3] - b[2])))

    for plan in PLANS:
        print("  --- plan %s" % plan.upper())
        for pas in PAS_LISTE:
            r = resultats[pas]["plans"][plan]
            if not r.get("faces"):
                print("    pas %d u : aucune face" % pas)
                continue
            print("    pas %d u : %d faces sur %d plans distincts ; aire exacte (lacet) %.0f u2, "
                  "aire peinte echantillonnee %.0f u2 (ecart %+.2f %%)"
                  % (pas, r["faces"], r["plans_distincts"], r["aire_exacte"], r["aire_peinte"],
                     100.0 * (r["aire_peinte"] - r["aire_exacte"]) / r["aire_exacte"]))
            print("      [1] union PAR PLAN %.0f u2 -> redondance x%.3f   |   union GLOBALE XZ "
                  "%.0f u2 -> x%.3f"
                  % (r["aire_union_plan"], r["aire_peinte"] / r["aire_union_plan"],
                     r["aire_union_globale"], r["aire_peinte"] / r["aire_union_globale"]))
            print("      [2] recouvrement dans le plan : %s" % ligne_hist(r["hist"]))
            print("          recouvrement tous plans   : %s" % ligne_hist(r["hist_glob"]))
            exces = r["aire_peinte"] - r["aire_union_plan"]
            print("      [3] couvertes par les autres, critere pose FACE PAR FACE (majorant qui se "
                  "double-compte) : geometrique %d (%.1f %%, %.0f u2), stricte meme tuile %d "
                  "(%.1f %%, %.0f u2) ; exces peint-union = %.0f u2"
                  % (r["couv_geo"], 100.0 * r["couv_geo"] / r["faces"], r["aire_geo"],
                     r["couv_strict"], 100.0 * r["couv_strict"] / r["faces"], r["aire_strict"],
                     exces))
            for nom, etiq in (("geo", "geometrique"), ("strict", "stricte    ")):
                gp, gg = r["glouton_" + nom]
                ap, ag = r["aire_glouton_" + nom]
                print("          retirables SIMULTANEMENT, borne %s : %d..%d faces (%.1f..%.1f %%),"
                      " %.0f..%.0f u2 -> %.2f..%.2f ms a %.0f us/face"
                      % (etiq, min(gp, gg), max(gp, gg), 100.0 * min(gp, gg) / r["faces"],
                         100.0 * max(gp, gg) / r["faces"], min(ap, ag), max(ap, ag),
                         min(gp, gg) * COUT_FACE_US / 1000.0, max(gp, gg) * COUT_FACE_US / 1000.0,
                         COUT_FACE_US))
            print("          CONTROLE de bout en bout : union apres retrait du glouton strict "
                  "%.0f u2 contre %.0f u2 avant -> %s"
                  % (r["union_apres"], r["aire_union_plan"],
                     "INTACTE" if r["union_intacte"] else "*** L'UNION A BOUGE ***"))
            print("          %d faces sans echantillon (non comptees) ; CONTROLE grille stricte : "
                  "%d plans -> %d couples (plan, tuile), %d plans a plusieurs tuiles ; TEMOIN "
                  "(groupe = un seul mur, doit tomber a ~0) : %d face(s)"
                  % (r["sans_echantillon"], r["plans_distincts"], r["couples_plan_tuile"],
                     r["plans_multi_tuiles"], r["couv_temoin"]))
            print("      [4] %d carres de 64 peints ; morceaux (murs) par carre, tous plans : %s"
                  % (r["carres"], ligne_hist(r["hist_carre_murs"])))
            print("          morceaux par carre DANS UN MEME PLAN : %s"
                  % ligne_hist(r["hist_carre_murs_plan"]))
            print("          faces par carre : %s ; pire carre x=%d z=%d : %d morceaux, %d faces"
                  % (ligne_hist(r["hist_carre_faces"]), r["pire_carre"][0], r["pire_carre"][1],
                     r["pire_carre"][2], r["pire_carre"][3]))
        # stabilite : ce qui doit peu bouger d'un pas a l'autre
        a, b2 = (resultats[p]["plans"][plan] for p in PAS_LISTE[:2])
        if a.get("faces"):
            print("      STABILITE pas %d -> %d : redondance par plan %.3f -> %.3f (%+.2f %%) ; "
                  "couvertes stricte %d -> %d (%+d) ; retirables stricte %s -> %s"
                  % (PAS_LISTE[0], PAS_LISTE[1],
                     a["aire_peinte"] / a["aire_union_plan"], b2["aire_peinte"] / b2["aire_union_plan"],
                     100.0 * ((b2["aire_peinte"] / b2["aire_union_plan"])
                              / (a["aire_peinte"] / a["aire_union_plan"]) - 1.0),
                     a["couv_strict"], b2["couv_strict"], b2["couv_strict"] - a["couv_strict"],
                     a["glouton_strict"], b2["glouton_strict"]))


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    for nom, chemin in NIVEAUX:
        if not os.path.exists(chemin):
            print("absent :", chemin)
            continue
        rapport(nom, chemin)
    return 0


if __name__ == "__main__":
    sys.exit(main())

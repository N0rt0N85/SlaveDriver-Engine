#!/usr/bin/env python3
"""soldom_secteur.py -- le SOL DOMINANT VDP2 elu par SECTEUR, pas par quad.

Ce que `soldom_vue.py` mesurait deja, et ce qu'il ne mesurait pas.

  DEJA. L'election porte sur le couple (hauteur, tuile) du sol, jamais sur une cellule : tous les
  quads qui portent ce couple disparaissent, dans TOUS les secteurs visibles qui le partagent. Et
  le sol d'un secteur ne porte JAMAIS qu'un seul couple -- colonne `sect>1` ci-dessous, 0 sur les
  neuf cartes. Un secteur de Doom a une hauteur et un flat, le decoupage en feuilles BSP ne le
  divise pas, et la cle de tuile d'un plat vaut (pic,0,0,1,1) sans decalage ni fenetre
  (doomtiles.tile_of_key), donc un flat = une tuile. Elire le couple, c'est deja elire des
  SECTEURS ENTIERS, et tous ceux de meme hauteur et meme texture avec eux.

  PAS ENCORE, et c'est ce que ce fichier ajoute.
  1. LA LUMIERE. `sector.light` vaut 128 partout : le convertisseur ne s'en sert pas. La lumiere
     reelle est par SOMMET (`vertexLight` ; WALLS.C:1635 fait level_vertexLight[..] - fogTable[z],
     borne a 32 pour indexer worldGrey). Un plan VDP2 n'a pas de Gouraud : il porte UN niveau.
     Reunir sans regarder la lumiere, c'est promettre un depot qu'on ne pourra pas tenir sans
     marche visible. La tolerance dit combien de crans worldGrey on accepte de se tromper.
  2. LE CRITERE. `soldom_vue` elisait le plan SOUS LES PIEDS. Ce n'est pas le plan dominant :
     mesure ci-dessous, il rapporte moins que le plus grand plan de l'image dans la majorite des
     positions. Les deux sont mesures cote a cote -- le pied est stable et gratuit a designer,
     le plus grand demande un balayage des secteurs visibles et change plus souvent.
  3. L'HYSTERESIS. Elire le meilleur plan a chaque image le fait changer en marchant, et changer
     de plan change la texture ET la hauteur de la couche VDP2 : ca se voit. On mesure le taux de
     changement par pas de 64 u, puis ce qu'une marge le ramene a, et ce que la marge coute.

Lancer depuis la racine du fork : python tools\\study\\soldom_secteur.py [cartes...]
"""
import os
import sys
from collections import Counter, defaultdict

RACINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RACINE, "tools"))
import cout                                                 # noqa: E402
import lev                                                  # noqa: E402
import ordre                                                # noqa: E402

CARTES = ["E1M%d" % i for i in range(1, 10)]
TOLERANCES = (0, 1, 2, 4, 8, 16, 32)      # crans worldGrey (la lumiere utile est bornee a 32)
MARGES = (1.00, 1.15, 1.30, 1.60, 2.00)   # hysteresis : ce que le candidat doit valoir en plus
TOL_RETENUE = 8                           # la tolerance ou le depot sature (voir la table)


def plans(S, W, V, F, tex):
    """-> (sol, cel) ; sol[si] = None ou dict(y, tuile, n, plans, lum, lmin, lmax).

    `cel` est le cout du secteur en cellules REELLEMENT dessinees par le VDP1. Les cellules de
    ciel (0x40) en sont retirees par principe, mais le convertisseur Doom n'en emet AUCUNE : un
    plafond a ciel ouvert n'y donne pas un mur marque ciel, il ne donne pas de mur du tout. La
    soustraction est donc nulle sur les neuf cartes, et le denominateur etait deja juste."""
    sol = [None] * len(S)
    cel = [0] * len(S)
    for si, s in enumerate(S):
        acc = Counter()
        lum = Counter()
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            grille = bool(w["flags"] & 0x01)
            n = (w["tileLength"] * w["tileHeight"] if grille
                 else (w["lastFace"] - w["firstFace"] + 1 if w["firstFace"] >= 0 else 0))
            if w["flags"] & 0x40:                            # ciel : le VDP1 n'en dessine rien
                continue
            cel[si] += n
            if w["normal"][1] <= 0:                          # mur vertical ou plafond
                continue
            b = w["firstVertex"]
            if grille:
                y = V[w["v"][0]]["y"]
                for i in range(n):
                    acc[(y, tex[w["textures"] + 2 * i + 1])] += 1
                for i in range((w["tileHeight"] + 1) * (w["tileLength"] + 1)):
                    lum[min(32, V[b + i]["light"])] += 1
            elif w["firstFace"] >= 0:
                for fi in range(w["firstFace"], w["lastFace"] + 1):
                    f = F[fi]
                    acc[(V[b + f["v"][0]]["y"], f["tile"])] += 1
                    for k in range(4):
                        lum[min(32, V[b + f["v"][k]]["light"])] += 1
        if acc:
            (y, t), _n = acc.most_common(1)[0]
            tot = sum(lum.values())
            sol[si] = dict(y=y, tuile=t, n=sum(acc.values()), plans=len(acc),
                           lum=(sum(k * v for k, v in lum.items()) / float(tot)) if tot else 0.0,
                           lmin=min(lum) if lum else 0, lmax=max(lum) if lum else 0)
    return sol, cel


def q(v, p):
    if not v:
        return 0
    t = sorted(v)
    return t[min(len(t) - 1, int(p * len(t)))]


def mesurer(nom, chemin, trace):
    r = lev.Reader(chemin)
    lev.parse_sky(r)
    L = lev.parse_level_block(r)
    S, W, V, F, tex = L["sectors"], L["walls"], L["vertices"], L["faces"], L["texture"]
    sol, cel = plans(S, W, V, F, tex)
    brut = cout.cellules_par_secteur(S, W)
    multi = sum(1 for d in sol if d and d["plans"] > 1)

    # -- 1. la famille d'un plan, et l'ecart de lumiere qu'elle demande d'avaler ---------------
    fam = defaultdict(list)
    for si, d in enumerate(sol):
        if d:
            fam[(d["y"], d["tuile"])].append(si)
    ec = [max(sol[s]["lum"] for s in m) - min(sol[s]["lum"] for s in m)
          for m in fam.values() if len(m) > 1]

    geo, vues = ordre.visibilite(S, W, V)
    pos = [(ex, ez, s0, vis) for ex, ez, s0, vis, _d in vues if sum(cel[s] for s in vis) > 0]

    def famille(vis, k, lref, tol):
        """Les cellules de sol visibles que le plan `k` retire : TOUS les quads de TOUS les
        secteurs visibles qui portent la meme hauteur et la meme tuile, et dont la lumiere
        moyenne est a `tol` crans de la reference."""
        return sum(sol[s]["n"] for s in vis
                   if sol[s] and (sol[s]["y"], sol[s]["tuile"]) == k
                   and abs(sol[s]["lum"] - lref) <= tol)

    def candidats(vis, tol):
        """Tout ce qui peut etre elu depuis cette position -> {plan: (cellules, lumiere de ref)}.
        La reference d'un plan est la lumiere du PLUS GRAND secteur visible qui le porte : c'est
        lui qui donne son ton au plan VDP2, les autres s'y rangent ou en sont exclus."""
        gros = {}
        for s in vis:
            d = sol[s]
            if d is None:
                continue
            k = (d["y"], d["tuile"])
            if k not in gros or d["n"] > sol[gros[k]]["n"]:
                gros[k] = s
        return {k: (famille(vis, k, sol[g]["lum"], tol), sol[g]["lum"])
                for k, g in gros.items()}

    def depot(tol, critere):
        """Par position : part des cellules VDP1 de l'image retiree, cellules, plan elu.
        critere `pied` = le plan du secteur ou se tient le joueur ; `max` = le plus grand plan
        de l'image."""
        parts, cells, elus, tous = [], [], [], []
        for _ex, _ez, s0, vis in pos:
            total = sum(cel[s] for s in vis)
            cand = candidats(vis, tol)
            tous.append(cand)
            if critere == "pied":
                d0 = sol[s0]
                k = (d0["y"], d0["tuile"]) if d0 else None
            else:
                k = max(cand, key=lambda x: cand[x][0]) if cand else None
            n = cand.get(k, (0, 0))[0] if k is not None else 0
            parts.append(100.0 * n / total)
            cells.append(n)
            elus.append(k)
        return parts, cells, elus, tous

    trace("")
    trace("=== %s : %d secteurs, %d cellules VDP1 (%d avec le ciel, soit %d cellules de ciel)"
          % (nom, len(S), sum(cel), sum(brut), sum(brut) - sum(cel)))
    trace("    secteurs dont le sol porte PLUS D'UN plan : %d   <- l'election est deja un "
          "secteur entier" % multi)
    trace("    %d plans distincts, %d partages par >= 2 secteurs ; ecart de lumiere DANS une "
          "famille : mediane %.1f, p90 %.1f, max %.1f crans sur 32"
          % (len(fam), len(ec), q(ec, 0.5), q(ec, 0.9), max(ec) if ec else 0))
    trace("    %d positions debout" % len(pos))
    trace("            |       plan SOUS LES PIEDS       |     PLUS GRAND plan de l'image")
    trace("    tol.lum | median    p90  cel.med cel.moy | median    p90  cel.med cel.moy")
    garde = {}
    for tol in TOLERANCES:
        ligne = [tol]
        for critere in ("pied", "max"):
            parts, cells, elus, _t = depot(tol, critere)
            garde[(tol, critere)] = (parts, cells, elus)
            ligne += [q(parts, 0.5), q(parts, 0.9), q(cells, 0.5),
                      sum(cells) / float(len(cells))]
        trace("    %7d | %5.1f %% %5.1f %7d %7.1f | %5.1f %% %5.1f %7d %7.1f" % tuple(ligne))
    for critere, mot in (("pied", "sous les pieds"), ("max", "plus grand de l'image")):
        cm = q(garde[(32, critere)][1], 0.5)
        trace("    -> %-22s sans contrainte de lumiere : %d cellules medianes = %.1f ms au "
              "tarif marginal de %.0f us"
              % (mot, cm, cm * cout.US_MARGINAL / 1000.0, cout.US_MARGINAL))

    # -- 3. hysteresis : combien de fois le plan change en marchant ----------------------------
    # Un pas est une arete entre deux positions debout voisines de la maille. Le taux donne est
    # donc la probabilite qu'un plan elu sans memoire change en franchissant 64 u -- a la course
    # de Doom (~17 u/tic), c'est un pas toutes les 4 images.
    pas = int(ordre.PAS)
    idx = {(x, z): i for i, (x, z, _s, _v) in enumerate(pos)}
    aretes = [(i, idx[(x + dx, z + dz)]) for i, (x, z, _s, _v) in enumerate(pos)
              for dx, dz in ((pas, 0), (0, pas)) if (x + dx, z + dz) in idx]
    for critere, mot in (("pied", "sous les pieds"), ("max", "plus grand de l'image")):
        _p, _c, elus = garde[(TOL_RETENUE, critere)]
        cand = [candidats(vis, TOL_RETENUE) for _x, _z, _s0, vis in pos]
        trace("    hysteresis (%s, tolerance %d), %d pas de %d u :"
              % (mot, TOL_RETENUE, len(aretes), pas))
        for m in MARGES:
            chg, cede, tenus = 0, 0, 0
            for a, b in aretes:
                ca, cb = elus[a], elus[b]
                if ca is None or cb is None or ca == cb:
                    continue
                neuf = cand[b].get(cb, (0, 0))[0]
                vieux = cand[b].get(ca, (0, 0))[0]
                if neuf > m * max(1, vieux):
                    chg += 1
                else:
                    tenus += 1
                    cede += neuf - vieux
            trace("      marge x%.2f : %5.2f %% des pas changent, %4d pas gardent l'ancien plan "
                  "pour %+d cellules" % (m, 100.0 * chg / max(1, len(aretes)), tenus, cede))


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    for nm in (sys.argv[1:] or CARTES):
        p = os.path.join(RACINE, "cd_doom", nm + ".LEV")
        if os.path.exists(p):
            mesurer(nm, p, print)
        else:
            print("absent : %s" % p)


if __name__ == "__main__":
    main()

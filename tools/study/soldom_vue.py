#!/usr/bin/env python3
"""soldom_vue.py -- ce que le SOL DOMINANT VDP2 vaut sur NOTRE niveau converti, par point de vue.

docs/VDP2_DOMINANT_FLOOR.md a chiffre le gisement sur les 24 `.LEV` RETAIL : le couple (hauteur,
tuile) dominant ne couvre que 13,8 % des cellules de sol d'un niveau entier, mais 70 % a un saut
de portail (`tools/study/neigh.py`), d'ou un verdict de 7-10 % des commandes, soit 2,0-2,7 ms.

Deux raisons de refaire la mesure ici :
  1. le contenu n'est pas le meme. Une piece de Doom a UN flat par secteur, la ou PowerSlave pose
     une mosaique cellule par cellule ; la part dominante devrait etre bien plus haute ;
  2. le saut de portail n'etait qu'un SUBSTITUT du champ de vision. `ordre.visibilite` donne
     maintenant ce qui est reellement visible depuis chaque position debout, donc la question se
     pose dans la bonne unite : combien de cellules le plan sous les pieds du joueur retire-t-il
     de CETTE image.

Deux plans sont mesures : celui elu sous les pieds (la regle proposee par l'etude, realisable) et
le MEILLEUR plan de l'image (borne haute, non realisable puisque le joueur ne le designe pas).
Le resultat est donne en CELLULES, jamais en pourcentage seul : la loi de cout se juge contre les
paliers 470 / 896 / 1321 (tools/cout.py).

⚠ CE FICHIER ELIT LE PLAN SOUS LES PIEDS, et la mesure du 26-09 dit que ce n'est pas le plan
dominant : le plus grand plan de l'image rend 3 a 5 points de plus sur huit cartes sur neuf. Il
ignore aussi la lumiere, qu'un plan VDP2 ne peut pas nuancer. Pour la regle a implementer, voir
`soldom_secteur.py` et VDP2_DOMINANT_FLOOR.md §7 ; celui-ci reste comme temoin.

Lancer depuis la racine du fork : python tools\\study\\soldom_vue.py [--duke]
"""
import argparse
import os
import sys
from collections import Counter, defaultdict

RACINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RACINE, "tools"))
import cout                                                 # noqa: E402
import lev                                                  # noqa: E402
import ordre                                                # noqa: E402

NIVEAUX = (("E1M1", os.path.join(RACINE, "cd_doom", "E1M1.LEV")),
           ("TOMB", os.path.join(RACINE, "cd_duke", "TOMB.LEV")))


def plans_de_secteur(S, W, V, F, tex):
    """-> (murs, sols, plafonds) ou chaque entree est [par secteur] un Counter {(y, tuile): n}.

    Une cellule de grille porte sa tuile dans `texture[w.textures + 2*i + 1]` (LEVEL.C:70-71, deux
    octets par cellule [motif, tuile]) ; une face porte la sienne dans `face.tile`. La hauteur
    d'un plat est celle de ses sommets -- toutes egales, c'est ce qui en fait un plat."""
    murs = [0] * len(S)
    sols = [Counter() for _ in S]
    plafs = [Counter() for _ in S]
    for si, s in enumerate(S):
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] == 0:                          # mur vertical
                if w["flags"] & 0x01:
                    murs[si] += w["tileLength"] * w["tileHeight"]
                if w["firstFace"] >= 0:
                    murs[si] += w["lastFace"] - w["firstFace"] + 1
                continue
            cible = sols[si] if w["normal"][1] > 0 else plafs[si]
            if w["flags"] & 0x40:                            # ciel : n'emet rien
                continue
            if w["flags"] & 0x01:
                y = V[w["v"][0]]["y"]
                for i in range(w["tileLength"] * w["tileHeight"]):
                    cible[(y, tex[w["textures"] + 2 * i + 1])] += 1
            elif w["firstFace"] >= 0:
                b = w["firstVertex"]
                for fi in range(w["firstFace"], w["lastFace"] + 1):
                    y = V[b + F[fi]["v"][0]]["y"]
                    cible[(y, F[fi]["tile"])] += 1
    return murs, sols, plafs


def quantiles(v):
    if not v:
        return (0, 0, 0, 0)
    t = sorted(v)
    n = len(t)
    return (t[0], t[n // 2], t[min(n - 1, int(0.9 * n))], t[-1])


def mesurer(nom, chemin, trace):
    r = lev.Reader(chemin)
    lev.parse_sky(r)
    L = lev.parse_level_block(r)
    S, W, V, F, tex = L["sectors"], L["walls"], L["vertices"], L["faces"], L["texture"]
    murs, sols, plafs = plans_de_secteur(S, W, V, F, tex)
    nm_m, nm_s, nm_p = sum(murs), sum(sum(c.values()) for c in sols), \
        sum(sum(c.values()) for c in plafs)
    tot = nm_m + nm_s + nm_p
    trace("")
    trace("=== %s : %d cellules -- murs %d (%.1f %%), sols %d (%.1f %%), plafonds %d (%.1f %%)"
          % (nom, tot, nm_m, 100.0 * nm_m / tot, nm_s, 100.0 * nm_s / tot,
             nm_p, 100.0 * nm_p / tot))
    trace("    retail pour memoire : 66,8 / 19,6 / 13,5 %% (24 .LEV, floorshare.py)")
    glob = Counter()
    for c in sols:
        glob += c
    if glob:
        trace("    plan de sol dominant du niveau entier : %.1f %% des cellules de sol "
              "(retail : 13,8 %%), %d plans distincts"
              % (100.0 * glob.most_common(1)[0][1] / max(1, nm_s), len(glob)))

    cel = cout.cellules_par_secteur(S, W)
    geo, vues = ordre.visibilite(S, W, V)
    part_elu, part_max, gain_elu, gain_max, vues_vues = [], [], [], [], 0
    for _ex, _ez, s0, vis, _d in vues:
        total = sum(cel[s] for s in vis)
        if total <= 0:
            continue
        vues_vues += 1
        vu_sol = Counter()
        for s in vis:
            vu_sol += sols[s]
        if not vu_sol:
            part_elu.append(0.0)
            part_max.append(0.0)
            gain_elu.append(0)
            gain_max.append(0)
            continue
        pied = sols[s0].most_common(1)[0][0] if sols[s0] else None
        n_elu = vu_sol.get(pied, 0) if pied is not None else 0
        n_max = vu_sol.most_common(1)[0][1]
        gain_elu.append(n_elu)
        gain_max.append(n_max)
        part_elu.append(100.0 * n_elu / total)
        part_max.append(100.0 * n_max / total)
    trace("    %d positions debout" % vues_vues)
    trace("    plan sous les pieds : %d / %d / %d / %d cellules retirees "
          "(min / mediane / p90 / max), soit %.1f %% de l'image en mediane"
          % (quantiles(gain_elu) + (quantiles(part_elu)[1],)))
    trace("    meilleur plan de l'image (borne haute) : %d / %d / %d / %d cellules, %.1f %% "
          "en mediane" % (quantiles(gain_max) + (quantiles(part_max)[1],)))
    med = quantiles(gain_elu)[1]
    trace("    -> %d cellules medianes = %.1f ms au tarif marginal de %.0f us "
          "(paliers %s)" % (med, med * cout.US_MARGINAL / 1000.0, cout.US_MARGINAL,
                            ", ".join(str(p) for p, _f in cout.PALIERS)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duke", action="store_true")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    for nom, chemin in (NIVEAUX if a.duke else NIVEAUX[:1]):
        if os.path.exists(chemin):
            mesurer(nom, chemin, print)
        else:
            print("absent : %s" % chemin)


if __name__ == "__main__":
    main()

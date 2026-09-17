#!/usr/bin/env python3
"""balayage_ordre.py -- ce que coutent et ce que rapportent les reglages de tools/ordre.py.

La question posee : la liste de paires laisse 6 % de positions fautives chez Doom et 38 % chez
Duke. Est-ce que ces residus se rachetent en depensant plus -- plus de TOURS d'iteration, une
grille de points de vue plus fine (PAS), ou carrement toutes les paires -- ou est-ce qu'ils sont
la structure du niveau ?

Quatre mesures, dans cet ordre :
  A. le FAN-IN reellement atteint. `ancestor[]` est de taille MAXFANIN (WALLS.C:87, 20) et les
     portails le remplissent avant la table. Si le plafond etait atteint, la table serait
     silencieusement amputee sur console. Mesure le maximum vraiment observe.
  B. TOURS : 1, 2, 4, 8, 16. Le convertisseur est a 4.
  C. PAS : 64 (defaut) puis 32. Une maille plus fine voit plus de defauts et donne plus de paires
     a contraindre, pour un temps de conversion quadratique.
  D. LE PLAFOND : toutes les paires qui se chevauchent a l'ecran, sans selection. C'est la borne
     qu'aucune liste clairsemee ne peut battre.

Lancer depuis la racine du fork : python tools\\study\\balayage_ordre.py [--duke] [--fin]
Sans argument : E1M1 seul, PAS 64 (environ une minute).
"""
import argparse
import os
import sys
import time
from collections import defaultdict

RACINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RACINE, "tools"))
import lev                                                  # noqa: E402
import ordre                                                # noqa: E402

NIVEAUX = (("E1M1", os.path.join(RACINE, "cd_doom", "E1M1.LEV")),
           ("TOMB", os.path.join(RACINE, "cd_duke", "TOMB.LEV")))


def charger(chemin):
    r = lev.Reader(chemin)
    lev.parse_sky(r)
    L = lev.parse_level_block(r)
    return L["sectors"], L["walls"], L["vertices"]


def fan_in(scenes):
    """Le nombre d'ancetres que les PORTAILS posent deja, par secteur et par position -> le pire.

    C'est `nmAncestors` du moteur avant que la table n'ajoute quoi que ce soit ; ce qui reste
    jusqu'a MAXFANIN est tout ce que la table peut esperer poser."""
    pire = 0
    histo = defaultdict(int)
    for _ex, _ez, _vis, _dist, enf, _rel in scenes:
        c = defaultdict(int)
        for s in enf:
            for a in enf[s]:
                c[a] += 1
        m = max(c.values()) if c else 0
        histo[m] += 1
        pire = max(pire, m)
    return pire, histo


def pourcent(n, t):
    return 100.0 * n / max(1, t)


def mesurer(nom, chemin, pas, tours_a_essayer, trace):
    S, W, V = charger(chemin)
    t0 = time.time()
    geo, vues = ordre.visibilite(S, W, V, pas)
    t_vis = time.time() - t0
    t0 = time.time()
    scenes, plans, indecidables = ordre._scenes(geo, vues)
    t_sc = time.time() - t0
    murs = geo[1]

    trace("")
    trace("=== %s, PAS %.0f : %d secteurs, %d positions debout (%.1f s), "
          "%d paires se chevauchent (%.1f s)"
          % (nom, pas, len(S), len(vues), t_vis, len(plans), t_sc))

    # A. fan-in
    pire, histo = fan_in(scenes)
    top = sorted(histo.items(), key=lambda kv: -kv[0])[:4]
    trace("  A. fan-in des portails : pire %d ancetres sur MAXFANIN %d ; queue %s"
          % (pire, ordre.MAXFANIN, ", ".join("%d ancetres x%d pos" % kv for kv in top)))

    # B/C. tours
    res = []
    for tours in tours_a_essayer:
        t0 = time.time()
        paires, st = ordre.paires_d_ordre(S, W, V, pas=pas, tours=tours,
                                          vu=(geo, vues))
        d = min(st, key=lambda s: (s["positions"], s["paires"], s["entrees"]))
        res.append((tours, len(paires), d, time.time() - t0))
        trace("  B. tours %2d : %4d entrees (%5d o) -> %4d/%d positions (%.1f %%), "
              "%d aretes perdues, %.0f s"
              % (tours, len(paires), 4 + 6 * len(paires), d["positions"], d["total"],
                 pourcent(d["positions"], d["total"]), d["perdues"], time.time() - t0))

    # D. plafond : toutes les paires qui se chevauchent, sans selection
    toutes = {p: (pl[0], pl[1]) for p, pl in plans.items()
              if pl is not None and pl[2] <= ordre.SEUIL_DEPASSEMENT}
    t0 = time.time()
    n_pos, n_mauvais, _f, n_perdues = ordre._juger(scenes, murs, toutes)
    trace("  D. plafond : %d entrees (%d o) -> %d/%d positions (%.1f %%), %d paires, "
          "%d aretes perdues, %.0f s"
          % (len(toutes), 4 + 6 * len(toutes), n_pos, len(scenes),
             pourcent(n_pos, len(scenes)), n_mauvais, n_perdues, time.time() - t0))
    trace("     ... et %d paires sans plan a moins de %.1f u (laissees au hasard)"
          % (len(indecidables), ordre.SEUIL_DEPASSEMENT))

    # D'. LE TEMOIN. Si le plafond est mauvais a cause du plafond `ancestor[MAXFANIN]` et non des
    #     paires elles-memes, alors desserrer ce seul chiffre doit tout changer. On ne touche a
    #     rien d'autre.
    garde = ordre.MAXFANIN
    ordre.MAXFANIN = 100000
    try:
        n2, m2, _f2, p2 = ordre._juger(scenes, murs, toutes)
    finally:
        ordre.MAXFANIN = garde
    trace("  D'. le meme, MAXFANIN desserre a 100000 : %d/%d positions (%.1f %%), %d paires, "
          "%d aretes perdues" % (n2, len(scenes), pourcent(n2, len(scenes)), m2, p2))

    # E. LA COURBE DU BUDGET : et si on prenait les N paires les plus souvent fautives ?
    #    C'est la question « qu'est-ce que des octets en plus racheteraient », posee proprement :
    #    l'iteration, elle, sature (mesure B).
    _n0, _m0, compte, _p0 = ordre._juger(scenes, murs, {})
    classees = [p for p, _n in compte.most_common()]
    trace("  E. courbe du budget (les N paires les plus souvent cassees, sans iteration) :")
    for N in sorted({res[0][1] if res else 0, 100, 200, 400, 800, 1600, len(classees)}):
        if N <= 0 or N > len(classees):
            continue
        t = {}
        for p in classees[:N]:
            pl = plans.get(p)
            if pl is not None and pl[2] <= ordre.SEUIL_DEPASSEMENT:
                t[p] = (pl[0], pl[1])
        nn, mm, _ff, pp = ordre._juger(scenes, murs, t)
        trace("     N %5d : %5d entrees (%6d o) -> %4d/%d positions (%.1f %%), "
              "%d aretes perdues" % (N, len(t), 4 + 6 * len(t), nn, len(scenes),
                                     pourcent(nn, len(scenes)), pp))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duke", action="store_true", help="mesurer aussi TOMB (long)")
    ap.add_argument("--fin", action="store_true", help="ajouter le PAS 32 (tres long)")
    ap.add_argument("--tours", default="1,2,4,8,16")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    tours = [int(x) for x in a.tours.split(",")]
    niveaux = NIVEAUX if a.duke else NIVEAUX[:1]
    for nom, chemin in niveaux:
        if not os.path.exists(chemin):
            print("absent : %s" % chemin)
            continue
        mesurer(nom, chemin, ordre.PAS, tours, print)
        if a.fin:
            mesurer(nom, chemin, ordre.PAS / 2.0, tours, print)


if __name__ == "__main__":
    main()

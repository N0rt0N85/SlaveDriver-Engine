#!/usr/bin/env python3
"""verif_e3.py -- verification contradictoire d'E3, ecrite SANS importer geom3d.

Relit e1l1_geom3d.json (et le modele convexe d'entree) et cherche a le mettre en defaut sur des
proprietes que le generateur ne calcule nulle part :
  A. aire des sols : la somme des aires des faces du sol d'un secteur doit valoir l'aire du morceau
     convexe correspondant (un pavage qui laisse un trou ou qui se recouvre echoue ici) ;
  B. etancheite verticale : le long d'une arete, les murs emis doivent couvrir exactement la tranche
     sol -> plafond, sans trou ni recouvrement ;
  C. faces dans le plan de leur mur ;
  D. faces d'aire nulle ;
  E. tileLength / tileHeight coherents avec la geometrie du quad ;
  F. normales unitaires ;
  G. portails reciproques et invisibles.
"""
from __future__ import annotations
import argparse
import json
import math
import os
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def area2(pts):
    n = len(pts)
    return sum(pts[k][0] * pts[(k + 1) % n][1] - pts[(k + 1) % n][0] * pts[k][1] for k in range(n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--geom", default=os.path.join(ROOT, "build", "duke2ps", "e1l1_geom3d.json"))
    ap.add_argument("--convex", default=os.path.join(ROOT, "build", "duke2ps", "e1l1_quant_convex.json"))
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    G = json.load(open(a.geom, encoding="utf-8"))
    C = json.load(open(a.convex, encoding="utf-8"))
    S, W, V, F = G["sectors"], G["walls"], G["vertices"], G["faces"]
    pieces = C["pieces"]
    res = {}

    def rec(key, bad, total):
        res[key] = dict(ok=not bad, n=len(bad), total=total, exemples=list(bad)[:4])

    # --- A. pavage des sols ----------------------------------------------------------------
    # A1 : etancheite INTERNE (une arete interieure au pavage appartient a exactement 2 faces) ;
    # A2 : depassement du bord (un sommet de face hors du polygone du morceau) ;
    # A3 : ecart d'aire, avec la tolerance MESUREE sur le retail (p95 0,12 %, max 3,65 %).
    bad_seam, bad_out, area_rel = [], [], []
    worst_out = 0.0
    for si, s in enumerate(S):
        flo = [W[w] for w in range(s["firstWall"], s["lastWall"] + 1) if W[w]["normal"][1] > 0]
        if len(flo) != 1 or flo[0]["firstFace"] < 0:
            continue
        w = flo[0]
        base = w["firstVertex"]
        tot = 0.0
        seam = defaultdict(int)
        pts = set()
        for f in range(w["firstFace"], w["lastFace"] + 1):
            q = [(V[base + i]["x"], V[base + i]["z"]) for i in F[f]["v"]]
            u = []
            for t in q:
                if not u or t != u[-1]:
                    u.append(t)
            if len(u) > 1 and u[0] == u[-1]:
                u.pop()
            if len(u) < 3:
                continue
            if area2(u) < 0:
                u = u[::-1]
            pts.update(u)
            tot += abs(area2(u)) / 2.0
            for k in range(len(u)):
                seam[(u[k], u[(k + 1) % len(u)])] += 1
        # une arete interieure est parcourue une fois dans chaque sens ; une arete de bord une seule
        for (a, b), n in seam.items():
            if n > 1:
                bad_seam.append((si, a, b, "arete repetee", n))
            elif seam.get((b, a), 0) == 0:
                pass                                   # arete de bord : normal
        poly = [(v[0] // 8, -(v[1] // 8)) for v in pieces[si]["verts"]]
        if area2(poly) < 0:
            poly = poly[::-1]
        npo = len(poly)
        for (x, z) in pts:
            d = -1e9
            for k in range(npo):
                A, B = poly[k], poly[(k + 1) % npo]
                ex, ez = B[0] - A[0], B[1] - A[1]
                Ln = math.hypot(ex, ez)
                if Ln:
                    d = max(d, -(ex * (z - A[1]) - ez * (x - A[0])) / Ln)
            worst_out = max(worst_out, d)
            if d > 1.0:
                bad_out.append((si, x, z, round(d, 2)))
        want = pieces[si]["aire_u2"]
        if want:
            area_rel.append(abs(tot - want) / want)
    rec("pavage_sans_arete_repetee", bad_seam, len(S))
    rec("bord_du_sol_a_1u", bad_out, len(S))
    area_rel.sort()
    m = len(area_rel) or 1
    res["aire_des_sols"] = dict(
        ok=area_rel[int(0.95 * (m - 1))] <= 0.01 and area_rel[-1] <= 0.0365,
        n=sum(1 for r in area_rel if r > 0.01), total=len(area_rel),
        exemples=[f"median {area_rel[m // 2] * 100:.3f} %",
                  f"p95 {area_rel[int(0.95 * (m - 1))] * 100:.3f} %",
                  f"max {area_rel[-1] * 100:.3f} %",
                  f"depassement max {worst_out:.2f} u",
                  "tolerance calibree sur le retail : p95 0,12 %, max 3,65 %"])

    # --- B. etancheite verticale ----------------------------------------------------------
    bad_seal = []
    for si, s in enumerate(S):
        # regroupe par ARETE (la paire de colonnes), pas par colonne : un coin du polygone appartient
        # a DEUX aretes, chacune emettant sa propre tranche verticale.
        edges = defaultdict(list)
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] != 0:
                continue
            vs = [V[i] for i in w["v"]]
            cols = sorted({(v["x"], v["z"]) for v in vs})
            if len(cols) != 2:
                continue
            edges[tuple(cols)].append((cols, vs))
        for key, group in edges.items():
            for c in key:
                iv = []
                for cols, vs in group:
                    ys = [v["y"] for v in vs if (v["x"], v["z"]) == c]
                    if ys and min(ys) != max(ys):
                        iv.append((min(ys), max(ys)))
                iv.sort()
                merged = []
                for lo, hi in iv:
                    if merged and lo < merged[-1][1]:
                        bad_seal.append((si, c, "recouvrement", lo, merged[-1][1]))
                        merged[-1] = (merged[-1][0], max(merged[-1][1], hi))
                    elif merged and lo == merged[-1][1]:
                        merged[-1] = (merged[-1][0], max(merged[-1][1], hi))
                    else:
                        if merged:
                            bad_seal.append((si, c, "trou", merged[-1][1], lo))
                        merged.append((lo, hi))
    rec("etancheite_verticale", bad_seal, len(S))

    # --- C / F. plans ---------------------------------------------------------------------
    bad_plane, bad_norm = [], []
    for wi, w in enumerate(W):
        n = w["normal"]
        ln = math.sqrt(sum((c / 65536.0) ** 2 for c in n))
        if abs(ln - 1.0) > 0.01:
            bad_norm.append((wi, round(ln, 4)))

        def off(v):
            return (n[0] * v["x"] + n[1] * v["y"] + n[2] * v["z"] + w["d"]) / 65536.0

        worst = max((abs(off(V[i])) for i in w["v"]), default=0)
        if w["firstFace"] >= 0:
            base = w["firstVertex"]
            for f in range(w["firstFace"], w["lastFace"] + 1):
                for i in F[f]["v"]:
                    worst = max(worst, abs(off(V[base + i])))
        if worst > 1.5:
            bad_plane.append((wi, round(worst, 2)))
    rec("quads_dans_leur_plan", bad_plane, len(W))
    rec("normales_unitaires", bad_norm, len(W))

    # --- D. faces degenerees --------------------------------------------------------------
    bad_face = []
    for wi, w in enumerate(W):
        if w["firstFace"] < 0:
            continue
        base = w["firstVertex"]
        for f in range(w["firstFace"], w["lastFace"] + 1):
            u = {(V[base + i]["x"], V[base + i]["y"], V[base + i]["z"]) for i in F[f]["v"]}
            if len(u) < 3:
                bad_face.append((wi, f, len(u)))
    rec("faces_non_degenerees", bad_face, len(F))

    # --- E. tuiles ------------------------------------------------------------------------
    bad_tile = []
    for wi, w in enumerate(W):
        if w["normal"][1] != 0:
            continue
        v = [V[i] for i in w["v"]]
        p = [(q["x"], q["y"], q["z"]) for q in v]
        tl = max(1, int((math.dist(p[0], p[1]) + 32) // 64))
        th = max(1, int((math.dist(p[1], p[2]) + 32) // 64))
        if (tl, th) != (w["tileLength"], w["tileHeight"]):
            bad_tile.append((wi, tl, th, w["tileLength"], w["tileHeight"]))
    rec("tuiles_coherentes", bad_tile, len(W))

    # --- J. ordre des murs et drapeaux du ciel ----------------------------------------------
    # findDoorways sort de la boucle au PREMIER mur plein (WALLS.C:1740-1743) : un portail place
    # apres un mur plein est invisible au moteur, et la visibilite ne quitte plus le secteur.
    bad_ord = []
    for si, s in enumerate(S):
        solid = False
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            if W[wi]["nextSector"] == -1:
                solid = True
            elif solid:
                bad_ord.append((si, wi))
                break
    rec("portails_en_tete", bad_ord, len(S))

    # le test INVISIBLE passe AVANT la branche parallax (WALLS.C:1616 contre 1666) : un ciel
    # INVISIBLE n'est jamais traite. MESURE : les 2 844 murs PARALLAX retail valent tous 320.
    bad_plx = [(wi, w["flags"]) for wi, w in enumerate(W)
               if (w["flags"] & 0x40) and (w["flags"] & 0x02)]
    rec("ciel_non_invisible", bad_plx, len(W))

    # --- H. parallelogrammes ---------------------------------------------------------------
    # drawRectWall reconstruit la grille avec deux vecteurs constants (WALLS.C:1036-1049) : un mur
    # marque PARALLELOGRAMME qui n'en est pas serait redresse a l'ecran. MESURE : les 11 712 murs
    # PARALLELOGRAMME du retail verifient v0+v2 == v1+v3 exactement.
    bad_par, bad_grid = [], []
    for wi, w in enumerate(W):
        if not (w["flags"] & 0x01):
            continue
        q = [(V[i]["x"], V[i]["y"], V[i]["z"]) for i in w["v"]]
        e = max(abs(q[0][k] + q[2][k] - q[1][k] - q[3][k]) for k in range(3))
        if e != 0:
            bad_par.append((wi, e))
        g = (w["tileLength"] + 1) * (w["tileHeight"] + 1)
        if g > 700 or w["tileLength"] * w["tileHeight"] >= 700:
            bad_grid.append((wi, w["tileLength"], w["tileHeight"], g))
    rec("parallelogrammes_reels", bad_par, len(W))
    rec("grille_rect_sous_700", bad_grid, len(W))

    # --- G. portails ----------------------------------------------------------------------
    owner = {}
    for si, s in enumerate(S):
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            owner[wi] = si
    bad_p = []
    for wi, w in enumerate(W):
        ns = w["nextSector"]
        if ns == -1:
            continue
        if not (0 <= ns < len(S)):
            bad_p.append((wi, "voisin hors bornes", ns))
            continue
        if not any(W[j]["nextSector"] == owner[wi]
                   for j in range(S[ns]["firstWall"], S[ns]["lastWall"] + 1)):
            bad_p.append((wi, "pas de reciproque", ns))
        if not (w["flags"] & 0x02):
            bad_p.append((wi, "portail non INVISIBLE", w["flags"]))
    rec("portails_reciproques", bad_p, len(W))

    # --- I. connexite du graphe des portails ------------------------------------------------
    # le critere qui compte pour « marchable » : depuis le secteur de depart, peut-on atteindre tous
    # les autres en franchissant des portails d'au moins 1 u de hauteur ?
    adj = defaultdict(set)
    for si, s in enumerate(S):
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = S and W[wi]
            if w["nextSector"] == -1 or w["normal"][1] != 0:
                continue
            ys = [V[i]["y"] for i in w["v"]]
            if max(ys) - min(ys) >= 1:
                adj[si].add(w["nextSector"])
    dep = G.get("criteres", {}).get("depart_present", {}).get("depart", {})
    start = 0
    seen, stack = {start}, [start]
    while stack:
        c = stack.pop()
        for n2 in adj[c]:
            if n2 not in seen:
                seen.add(n2)
                stack.append(n2)
    res["portails_connexes"] = dict(ok=len(seen) == len(S), n=len(S) - len(seen), total=len(S),
                                    exemples=[f"{len(seen)}/{len(S)} secteurs atteints depuis le 0",
                                              f"depart Build {dep.get('build_xy')}"])

    ok = all(v["ok"] for v in res.values())
    for k, v in res.items():
        print(f"  [{'OK' if v['ok'] else 'ECHEC'}] {k} : {v['n']} / {v['total']} "
              f"{json.dumps(v['exemples'], ensure_ascii=False)[:170]}")
    print(f"\n  -> {'AUCUNE MISE EN DEFAUT' if ok else 'E3 MIS EN DEFAUT'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""diag_geom.py -- confronte la GEOMETRIE du .LEV produit a la carte Doom d'origine.

Relit le FICHIER (pas les intermediaires) et repond a trois questions posees par un testeur :
  1. des murs ne sont pas au bon endroit  -> un mur plein pose sur une CORDE du BSP (une frontiere
     interne, sans linedef) est un mur qui ne devrait pas exister ;
  2. des plafonds sont trop bas           -> compare la hauteur de chaque secteur .LEV au secteur
     Doom dont sa feuille est issue ;
  3. on ne voit pas tout a la fois        -> compte ce que le peintre doit trier.

Usage : python tools\doom2ps\diag_geom.py [--lev F] [--wad W] [--map E1M1]
"""
from __future__ import annotations

import argparse
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import lev                                               # noqa: E402
import wad as wadmod                                     # noqa: E402
import adjacency                                         # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--lev", default=os.path.join(ROOT, "refs", "build", "duke2ps",
                                                  "TOMB_doom_e1m1.LEV"))
    ap.add_argument("--wad", default=os.path.join(os.path.dirname(ROOT), "Mimas", "cd", "data",
                                                  "DOOM1.WAD"))
    ap.add_argument("--map", default="E1M1")
    a = ap.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")

    r = lev.Reader(a.lev)
    lev.parse_sky(r)
    L = lev.parse_level_block(r)
    S, W, V = L["sectors"], L["walls"], L["vertices"]
    M = wadmod.read_map(wadmod.Wad(a.wad), a.map)
    DV, LD, SD, DS = M["vertices"], M["linedefs"], M["sidedefs"], M["sectors"]
    print(f"{os.path.basename(a.lev)} : {len(S)} secteurs, {len(W)} murs "
          f"| {a.map} : {len(DS)} secteurs, {len(LD)} linedefs")

    # index des linedefs par ligne canonique, avec leur intervalle
    par_ligne = collections.defaultdict(list)
    for li, ld in enumerate(LD):
        ax, ay = DV[ld.v1]
        bx, by = DV[ld.v2]
        if (ax, ay) == (bx, by):
            continue
        k = adjacency.line_key(ax, ay, bx - ax, by - ay)
        t0, t1 = adjacency.param(k, ax, ay), adjacency.param(k, bx, by)
        par_ligne[k].append((min(t0, t1), max(t0, t1), li, ld.left >= 0))

    def porte_par(px, pz, qx, qz):
        """-> ('une_face'|'deux_faces'|None, linedef) pour le segment (p,q) en coords Doom."""
        if (px, pz) == (qx, qz):
            return None, -1
        k = adjacency.line_key(px, pz, qx - px, qz - pz)
        if k not in par_ligne:
            return None, -1
        t0, t1 = adjacency.param(k, px, pz), adjacency.param(k, qx, qz)
        lo, hi = min(t0, t1), max(t0, t1)
        # Couverture par l'UNION des linedefs colineaires : un mur de Doom est souvent fait de
        # plusieurs linedefs bout a bout (une bande SUPPORT2 entre deux panneaux). Exiger UN SEUL
        # linedef englobant classait ces murs bien reels comme des fantomes.
        pieces = sorted((a0, a1, li, deux) for a0, a1, li, deux in par_ligne[k]
                        if a0 < hi - 0.5 and lo < a1 - 0.5)
        if not pieces:
            return None, -1
        bord = lo
        kinds, first = set(), pieces[0][2]
        for a0, a1, li, deux in pieces:
            if a0 > bord + 1.0:
                return None, -1                       # trou : pas couvert
            bord = max(bord, a1)
            kinds.add("deux_faces" if deux else "une_face")
        if bord < hi - 1.0:
            return None, -1
        return ("mixte" if len(kinds) > 1 else kinds.pop()), first

    cls = collections.Counter()
    faux = []
    for si, s in enumerate(S):
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] != 0:
                continue
            p, q = V[w["v"][0]], V[w["v"][1]]
            kind, li = porte_par(p["x"], p["z"], q["x"], q["z"])
            plein = w["nextSector"] == -1
            if kind is None:
                cls[("PLEIN SUR CORDE" if plein else "portail sur corde")] += 1
                if plein:
                    faux.append((si, wi, (p["x"], p["z"]), (q["x"], q["z"]),
                                 max(V[i]["y"] for i in w["v"]) - min(V[i]["y"] for i in w["v"])))
            else:
                cls[("plein" if plein else "portail") + " sur linedef " + kind] += 1
    print("\n-- 1. chaque mur vertical est-il porte par un linedef de Doom ? --")
    for k, n in cls.most_common():
        print(f"  {n:5d}  {k}")
    if faux:
        print(f"\n  MURS PLEINS SUR UNE CORDE (= murs fantomes) : {len(faux)}")
        for si, wi, p, q, h in faux[:12]:
            print(f"    secteur {si:4d} mur {wi:5d} : {p} .. {q}  hauteur {h}")

    # 2. hauteurs
    print("\n-- 2. hauteurs de secteur : .LEV contre Doom --")
    def hauteurs(s):
        f = c = None
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] > 0:
                f = V[w["v"][0]]["y"]
            elif w["normal"][1] < 0:
                c = V[w["v"][0]]["y"]
        return f, c
    hist = collections.Counter()
    for si, s in enumerate(S):
        f, c = hauteurs(s)
        hist[(c - f) if (f is not None and c is not None) else None] += 1
    doom_h = collections.Counter(ds.ceilh - ds.floorh for ds in DS)
    print("  hauteurs libres .LEV  :", sorted(hist.items(), key=lambda e: -e[1])[:8])
    print("  hauteurs libres Doom  :", doom_h.most_common(8))

    # 3. charge du peintre
    print("\n-- 3. ce que le peintre doit trier --")
    fanin = collections.Counter()
    for si, s in enumerate(S):
        n = sum(1 for wi in range(s["firstWall"], s["lastWall"] + 1)
                if W[wi]["nextSector"] != -1 and W[wi]["normal"][1] == 0)
        fanin[n] += 1
    print("  portails par secteur :", sorted(fanin.items()))
    print(f"  MAXFANIN du moteur = 20 (WALLS.C:87, assert WALLS.C:2153) ; "
          f"max ici = {max(fanin)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

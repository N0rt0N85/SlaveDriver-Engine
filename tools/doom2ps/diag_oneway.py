#!/usr/bin/env python3
"""diag_oneway.py -- montre la frontiere exacte des portails a sens unique restants."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import lev  # noqa: E402


def main():
    path = os.path.join(ROOT, "cd", "TOMB.LEV")
    r = lev.Reader(path)
    lev.parse_sky(r)
    L = lev.parse_level_block(r)
    S, W, V = L["sectors"], L["walls"], L["vertices"]

    back = {}
    for si, s in enumerate(S):
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            n = W[wi]["nextSector"]
            if n >= 0:
                back.setdefault(si, set()).add(n)
    one = [(a, b) for a, bs in back.items() for b in bs if a not in back.get(b, set())]
    print("couples a sens unique :", one)

    def seg(w):
        p0, p1 = V[w["v"][0]], V[w["v"][1]]
        ys = [V[i]["y"] for i in w["v"]]
        return ((p0["x"], p0["z"]), (p1["x"], p1["z"]), min(ys), max(ys))

    for a, b in one[:4]:
        print(f"\n=== {a} -> {b} (mais {b} ne revient pas vers {a}) ===")
        sa = S[a]
        for wi in range(sa["firstWall"], sa["lastWall"] + 1):
            w = W[wi]
            if w["nextSector"] == b:
                p, q, y0, y1 = seg(w)
                print(f"  {a} mur {wi} -> {b} : {p} .. {q}  y {y0}..{y1}  flags {w['flags']:#x}")
                ref = (p, q)
        sb = S[b]
        print(f"  murs de {b} (sol {sb['floorLevel']}) :")
        for wi in range(sb["firstWall"], sb["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] != 0:
                continue
            p, q, y0, y1 = seg(w)
            # distance du milieu au milieu de l'arete de reference
            mx = (ref[0][0] + ref[1][0]) / 2.0
            mz = (ref[0][1] + ref[1][1]) / 2.0
            qx = (p[0] + q[0]) / 2.0
            qz = (p[1] + q[1]) / 2.0
            d = math.hypot(qx - mx, qz - mz)
            if d < 120:
                print(f"    mur {wi} next {w['nextSector']:4d} : {p} .. {q}  y {y0}..{y1} "
                      f" flags {w['flags']:#x}  d={d:.0f}")
        print(f"  sol/plafond de {a} : ", end="")
        for wi in range(sa["firstWall"], sa["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] != 0:
                ys = [V[i]["y"] for i in w["v"]]
                print(f"{'sol' if w['normal'][1] > 0 else 'plaf'} y={ys[0]} ", end="")
        print()
        print(f"  sol/plafond de {b} : ", end="")
        for wi in range(sb["firstWall"], sb["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] != 0:
                ys = [V[i]["y"] for i in w["v"]]
                print(f"{'sol' if w['normal'][1] > 0 else 'plaf'} y={ys[0]} ", end="")
        print()


if __name__ == "__main__":
    main()

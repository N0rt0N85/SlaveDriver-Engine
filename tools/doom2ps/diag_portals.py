#!/usr/bin/env python3
"""diag_portals.py -- diagnostic des portails a sens unique du .LEV produit par doom2ps."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import lev  # noqa: E402


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "cd", "TOMB.LEV")
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

    def wlen(w):
        p0, p1 = V[w["v"][0]], V[w["v"][1]]
        return math.hypot(p0["x"] - p1["x"], p0["z"] - p1["z"])

    lens = []
    for a, b in one:
        s = S[a]
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            if W[wi]["nextSector"] == b:
                lens.append(wlen(W[wi]))
    allp = [wlen(w) for w in W if w["nextSector"] >= 0]
    lens.sort()
    allp.sort()
    tot = sum(len(v) for v in back.values())
    print("portails a sens unique : %d couples sur %d" % (len(one), tot))
    print("  longueur XZ : min %.1f  mediane %.1f  p90 %.1f  max %.1f"
          % (lens[0], lens[len(lens) // 2], lens[int(len(lens) * 0.9)], lens[-1]))
    print("  sous 8 u : %d / %d   sous 16 u : %d / %d"
          % (sum(1 for x in lens if x <= 8), len(lens),
             sum(1 for x in lens if x <= 16), len(lens)))
    print("TOUS les portails : mediane %.1f  p10 %.1f  min %.1f"
          % (allp[len(allp) // 2], allp[len(allp) // 10], allp[0]))

    # Le secteur vise a-t-il quand meme un mur sur cette frontiere ? (sinon = trou)
    trous = 0
    for a, b in one:
        s = S[a]
        seg = None
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            if W[wi]["nextSector"] == b:
                seg = W[wi]
                break
        if seg is None:
            continue
        mx = sum(V[i]["x"] for i in seg["v"]) / 4.0
        mz = sum(V[i]["z"] for i in seg["v"]) / 4.0
        near = False
        sb = S[b]
        for wi in range(sb["firstWall"], sb["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] != 0:
                continue
            qx = sum(V[i]["x"] for i in w["v"]) / 4.0
            qz = sum(V[i]["z"] for i in w["v"]) / 4.0
            if math.hypot(qx - mx, qz - mz) < 40:
                near = True
                break
        if not near:
            trous += 1
    print("  frontieres sans aucun mur en face (trous potentiels) : %d / %d" % (trous, len(one)))


if __name__ == "__main__":
    main()

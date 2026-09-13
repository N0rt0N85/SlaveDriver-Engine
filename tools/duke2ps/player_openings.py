"""player_openings.py -- releve des passages (murs rouges) et des largeurs de secteurs des cartes Duke PC,
pour dimensionner le joueur du fork SlaveDriver (boule de collision de rayon R).

Usage :
  python tools/duke2ps/player_openings.py                  # tous les corpus -> build/duke2ps/player/
  python tools/duke2ps/player_openings.py --corpus d13 --map E1L1 --verbose
Sorties : passages.csv (1 ligne par paire de murs rouges), sectors.csv, survey_meta.json.
Puis : python tools/duke2ps/player_report.py  -> openings_report.md

Definitions (toutes les longueurs en u : xy/8, z/128) :
  * hauteur libre h d'un passage = min sur ses 2 extremites de [ min(sols) - max(plafonds) ] (sens Y vers le
    haut), pentes comprises (formule de getzsofslope, jfbuild engine.c:10567-10584 ; racine exacte au lieu de
    nsqrtasm).  Plafond parallaxe (ceilingstat&1) = ciel = pas de plafond (clipmove l'ignore aussi :
    engine.c:9211) ; si les deux cotes sont en ciel, h = inf.
  * marche = max sur les 2 extremites de |sol A - sol B|.
  * longueur = longueur du mur / 8.
  * largeur utile w_eff = 2 x max, le long du segment du passage, de la distance aux murs BLOQUANTS pour la
    posture du passage (murs blancs, murs rouges cstat&1, murs rouges de hauteur libre < seuil de la posture).
    C'est le diametre du plus grand disque centre sur le passage qui ne touche rien. Plafonne a 2*CAP.
  * largeur praticable d'un secteur = 2 x max, sur les points du secteur, de la distance aux murs bloquants
    (grille + montee locale) = diametre du plus grand cercle inscrit dans l'espace libre, centre dans le
    secteur.
  * portes : secteurs lotag 20/21/22 fermes (floorz-ceilingz <= 0) OUVERTS comme sector.c:786-868
    (20 : plafond -> nextsectorneighborz(plafond,-1,-1) ; 21 : sol -> nextsectorneighborz(plafond,1,1) ;
    22 : les deux).  Autres secteurs fermes = volumes pleins (h <= 0).
  * cloison = memes sol ET plafond des deux cotes (a 0,5 u pres) : ce n'est pas une ouverture, mais le joueur
    la traverse (elle compte pour la couverture en largeur).
  * bloque = cstat&1 sur l'un des 2 murs (clipmove : wal->cstat & dawalclipmask, engine.c:9194).
Classement Duke (player_dims.py) : debout h>=88 ; accroupi 44<=h<88 ; retreci 24<=h<44 ; sinon
infranchissable ; et largeur Duke : distance de Chebyshev entre jambages >= 2*164 (carres axes de clipmove,
engine.c:9219-9228), approchee par w_eff * max(|ux|,|uy|) >= 41 u.
Limites (HYPOTHESE) : les sprites bloquants (grilles, meubles, cstat&1) sont ignores ; les ascenseurs et
plateformes restent dans leur etat initial ; portes coulissantes/battantes telles que dans la carte.
"""
from __future__ import annotations
import argparse
import csv
import glob
import json
import math
import os
import sys
import time
from collections import defaultdict

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import buildmap  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MAPS = os.path.join(ROOT, "refs", "extract", "KILLATON", "maps")
OUT = os.path.join(ROOT, "build", "duke2ps", "player")
CORPORA = ["d13", "atomic", "dukedc", "dz2", "xtreme_sp"]

XY = 8.0
ZU = 128.0
H_STAND, H_CROUCH, H_SHRUNK = 88.0, 44.0, 24.0         # player_dims.py
DUKE_W = 2 * 164 / XY                                   # 41 u, jambages dans l'axe
CAP = 1024.0                                            # build : clairance max mesuree (128 u -> w 256 u)
CELL = 1024
DOOR_LOTAGS = {9, 20, 21, 22, 23, 25, 26}
# vantaux / panneaux mobiles (SE11 battant 23, SE15 coulissant 25, portes etoile 9/26) : consideres OUVERTS,
# leurs murs ne bloquent pas (HYPOTHESE d'etat ouvert, comme les regles R-23/R-25 de l'import E1).
MOBILE_LOTAGS = {9, 23, 25, 26}
POSTURES = {"debout": H_STAND, "accroupi": H_CROUCH, "retreci": H_SHRUNK}


# ----------------------------------------------------------------------------------------------------------
def nextsectorneighborz(m, si, thez, topbottom, direction):
    """jfbuild engine.c:8534 -- sur les valeurs d'ORIGINE (toutes les portes fermees)."""
    s = m.sectors[si]
    nextz = None
    use = -1
    for w in range(s.wallptr, s.wallptr + s.wallnum):
        ns = m.walls[w].nextsector
        if ns < 0 or ns >= len(m.sectors):
            continue
        tz = m.sectors[ns].floorz if topbottom == 1 else m.sectors[ns].ceilingz
        if direction == 1:
            if tz > thez and (nextz is None or tz < nextz):
                nextz, use = tz, ns
        else:
            if tz < thez and (nextz is None or tz > nextz):
                nextz, use = tz, ns
    return use


def open_doors(m):
    cz = [s.ceilingz for s in m.sectors]
    fz = [s.floorz for s in m.sectors]
    opened = {}
    for si, s in enumerate(m.sectors):
        lt = s.lotag & 0x7fff
        if lt not in (20, 21, 22) or s.floorz - s.ceilingz > 0:
            continue
        if lt in (20, 22):
            j = nextsectorneighborz(m, si, s.ceilingz, -1, -1)
            if j >= 0:
                cz[si] = m.sectors[j].ceilingz
        if lt == 21:
            j = nextsectorneighborz(m, si, s.ceilingz, 1, 1)
            if j >= 0:
                fz[si] = m.sectors[j].floorz
        if lt == 22:
            j = nextsectorneighborz(m, si, s.floorz, 1, 1)
            if j >= 0:
                fz[si] = m.sectors[j].floorz
        opened[si] = (lt, (fz[si] - cz[si]) / ZU)
    return cz, fz, opened


def zs(m, cz0, fz0, si, x, y):
    """getzsofslope (engine.c:10567) sur les z de base eventuellement ouverts."""
    s = m.sectors[si]
    c, f = float(cz0[si]), float(fz0[si])
    if (s.ceilingstat | s.floorstat) & 2:
        w = m.walls[s.wallptr]
        w2 = m.walls[w.point2]
        dx, dy = w2.x - w.x, w2.y - w.y
        i = math.isqrt(dx * dx + dy * dy) << 5
        if i:
            j = (dx * (y - w.y) - dy * (x - w.x)) / 8.0
            if s.ceilingstat & 2:
                c += s.ceilingheinum * j / i
            if s.floorstat & 2:
                f += s.floorheinum * j / i
    return c, f


# ----------------------------------------------------------------------------------------------------------
class SegIndex:
    def __init__(self, segs: np.ndarray):
        self.s = segs
        b = defaultdict(list)
        for k in range(len(segs)):
            x1, y1, x2, y2 = segs[k]
            for cx in range(int(min(x1, x2) // CELL), int(max(x1, x2) // CELL) + 1):
                for cy in range(int(min(y1, y2) // CELL), int(max(y1, y2) // CELL) + 1):
                    b[(cx, cy)].append(k)
        self.b = {k: np.array(v, dtype=np.int64) for k, v in b.items()}

    def query(self, xmin, ymin, xmax, ymax):
        parts = []
        for cx in range(int(xmin // CELL), int(xmax // CELL) + 1):
            for cy in range(int(ymin // CELL), int(ymax // CELL) + 1):
                a = self.b.get((cx, cy))
                if a is not None:
                    parts.append(a)
        if not parts:
            return np.zeros((0, 4))
        return self.s[np.unique(np.concatenate(parts))]


def mindist(P: np.ndarray, S: np.ndarray) -> np.ndarray:
    """distance mini de chaque point P (N,2) a l'ensemble de segments S (M,4)."""
    if len(S) == 0:
        return np.full(len(P), CAP)
    ax, ay, bx, by = S[:, 0], S[:, 1], S[:, 2], S[:, 3]
    dx, dy = bx - ax, by - ay
    L2 = dx * dx + dy * dy
    L2 = np.where(L2 == 0, 1e-9, L2)
    out = np.empty(len(P))
    step = max(1, 200000 // max(1, len(S)))
    for a in range(0, len(P), step):
        px = P[a:a + step, 0:1]
        py = P[a:a + step, 1:2]
        t = np.clip(((px - ax) * dx + (py - ay) * dy) / L2, 0.0, 1.0)
        qx = ax + t * dx - px
        qy = ay + t * dy - py
        out[a:a + step] = np.sqrt((qx * qx + qy * qy).min(axis=1))
    return np.minimum(out, CAP)


def passage_clearance(idx: SegIndex, x1, y1, x2, y2):
    L = math.hypot(x2 - x1, y2 - y1)
    if L == 0:
        return 0.0
    S = idx.query(min(x1, x2) - CAP, min(y1, y2) - CAP, max(x1, x2) + CAP, max(y1, y2) + CAP)
    K = int(min(64, max(8, L / 16)))
    ts = (np.arange(K) + 0.5) / K
    P = np.stack([x1 + ts * (x2 - x1), y1 + ts * (y2 - y1)], axis=1)
    d = mindist(P, S)
    k = int(np.argmax(d))
    best = float(d[k])
    lo, hi = max(0.0, ts[k] - 1.0 / K), min(1.0, ts[k] + 1.0 / K)
    ts2 = np.linspace(lo, hi, 17)
    P2 = np.stack([x1 + ts2 * (x2 - x1), y1 + ts2 * (y2 - y1)], axis=1)
    best = max(best, float(mindist(P2, S).max()))
    return min(best, CAP)


def inside(P: np.ndarray, E: np.ndarray) -> np.ndarray:
    x = P[:, 0:1]
    y = P[:, 1:2]
    x1, y1, x2, y2 = E[:, 0], E[:, 1], E[:, 2], E[:, 3]
    cond = (y1 > y) != (y2 > y)
    dy = np.where(y2 - y1 == 0, 1e-12, y2 - y1)
    xi = x1 + (y - y1) * (x2 - x1) / dy
    return ((cond & (x < xi)).sum(axis=1) % 2) == 1


def sector_clearance(idx: SegIndex, E: np.ndarray):
    xmin, ymin = E[:, [0, 2]].min(), E[:, [1, 3]].min()
    xmax, ymax = E[:, [0, 2]].max(), E[:, [1, 3]].max()
    bw, bh = xmax - xmin, ymax - ymin
    step = max(8.0, min(bw, bh) / 12.0)
    while (bw / step + 1) * (bh / step + 1) > 900:
        step *= 1.4
    gx = np.arange(xmin + step / 2, xmax, step)
    gy = np.arange(ymin + step / 2, ymax, step)
    if len(gx) == 0 or len(gy) == 0:
        return float("nan")
    G = np.array(np.meshgrid(gx, gy)).reshape(2, -1).T
    G = G[inside(G, E)]
    if len(G) == 0:
        mids = np.stack([(E[:, 0] + E[:, 2]) / 2, (E[:, 1] + E[:, 3]) / 2], axis=1)
        G = mids[inside(mids, E)] if len(mids) else G
        if len(G) == 0:
            return float("nan")
    S = idx.query(xmin - CAP, ymin - CAP, xmax + CAP, ymax + CAP)
    d = mindist(G, S)
    k = int(np.argmax(d))
    best, p = float(d[k]), G[k].copy()
    h = step / 2
    nb = np.array([[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]], dtype=float)
    while h >= 2.0:
        improved = True
        while improved:
            improved = False
            C = p + nb * h
            C = C[inside(C, E)]
            if len(C) == 0:
                break
            dc = mindist(C, S)
            j = int(np.argmax(dc))
            if dc[j] > best + 1e-6:
                best, p, improved = float(dc[j]), C[j].copy(), True
        h /= 2
    return min(best, CAP)


# ----------------------------------------------------------------------------------------------------------
PASS_FIELDS = ["corpus", "map", "wall", "nextwall", "secA", "secB", "lotagA", "lotagB", "x1", "y1", "x2", "y2",
               "len_u", "axis_f", "h_u", "h1_u", "h2_u", "step_u", "ceil_diff_u", "skyA", "skyB",
               "blocked", "partition", "door_adj", "w_eff_u", "duke_w_req_u", "cls"]
SEC_FIELDS = ["corpus", "map", "sector", "lotag", "hmin_u", "hmax_u", "area_u2", "cls", "w_sec_u", "opened",
              "n_pass", "mobile"]


def classify(h, blocked, w_eff, w_req, mobile=False):
    if blocked:
        return "bloque"
    if mobile and h <= 0:
        return "porte_mobile"
    if h < H_SHRUNK:
        return "infr_h"
    if w_eff < w_req - 1e-6:
        return "infr_l"
    if h >= H_STAND:
        return "debout"
    if h >= H_CROUCH:
        return "accroupi"
    return "retreci"


def posture_of_height(h):
    if h >= H_STAND:
        return "debout"
    if h >= H_CROUCH:
        return "accroupi"
    if h >= H_SHRUNK:
        return "retreci"
    return None


def analyse_map(corpus, name, path, verbose=False):
    m = buildmap.load(path)
    ns = len(m.sectors)
    nwall = len(m.walls)
    wsec = np.full(nwall, -1, dtype=np.int64)
    for si, s in enumerate(m.sectors):
        wsec[s.wallptr:s.wallptr + s.wallnum] = si
    cz0, fz0, opened = open_doors(m)
    sky = [bool(s.ceilingstat & 1) for s in m.sectors]

    # --- passages --------------------------------------------------------------------------------------
    rows = []
    hpair = {}
    for w, wl in enumerate(m.walls):
        nw = wl.nextwall
        B = wl.nextsector
        if nw < 0 or B < 0 or B >= ns or nw >= nwall:
            continue
        if nw < w:
            continue
        A = int(wsec[w])
        if A < 0:
            continue
        p2 = m.walls[wl.point2]
        x1, y1, x2, y2 = wl.x, wl.y, p2.x, p2.y
        hs, steps, cdiffs = [], [], []
        for (x, y) in ((x1, y1), (x2, y2)):
            cA, fA = zs(m, cz0, fz0, A, x, y)
            cB, fB = zs(m, cz0, fz0, B, x, y)
            lo_floor = min(fA, fB)
            if sky[A] and sky[B]:
                hs.append(math.inf)
            else:
                top = cB if sky[A] else (cA if sky[B] else max(cA, cB))
                hs.append((lo_floor - top) / ZU)
            steps.append(abs(fA - fB) / ZU)
            if not sky[A] and not sky[B]:
                cdiffs.append(abs(cA - cB) / ZU)
        h = min(hs)
        step = max(steps)
        cdiff = max(cdiffs) if cdiffs else 0.0
        L = math.hypot(x2 - x1, y2 - y1)
        axis_f = max(abs(x2 - x1), abs(y2 - y1)) / L if L else 1.0
        blocked = bool((wl.cstat & 1) or (m.walls[nw].cstat & 1))
        partition = step < 0.5 and cdiff < 0.5 and (sky[A] == sky[B])
        la, lb = m.sectors[A].lotag & 0x7fff, m.sectors[B].lotag & 0x7fff
        door_adj = (la in DOOR_LOTAGS) or (lb in DOOR_LOTAGS)
        hpair[w] = hpair[nw] = (h, blocked)
        rows.append(dict(corpus=corpus, map=name, wall=w, nextwall=nw, secA=A, secB=B, lotagA=la, lotagB=lb,
                         x1=x1, y1=y1, x2=x2, y2=y2, len_u=L / XY, axis_f=axis_f, h_u=h, h1_u=hs[0], h2_u=hs[1],
                         step_u=step, ceil_diff_u=cdiff, skyA=int(sky[A]), skyB=int(sky[B]),
                         blocked=int(blocked), partition=int(partition), door_adj=int(door_adj)))

    # --- murs bloquants par posture --------------------------------------------------------------------
    mobile_sec = {si for si, s in enumerate(m.sectors) if (s.lotag & 0x7fff) in MOBILE_LOTAGS}
    idx = {}
    for post, hmin in POSTURES.items():
        segs = []
        for w, wl in enumerate(m.walls):
            p2 = m.walls[wl.point2] if 0 <= wl.point2 < nwall else None
            if p2 is None:
                continue
            if int(wsec[w]) in mobile_sec or (wl.nextsector >= 0 and wl.nextsector in mobile_sec):
                continue                                   # vantail/panneau ouvert
            if wl.nextwall < 0 or wl.nextsector < 0:
                block = True
            else:
                hb = hpair.get(w)
                block = hb is None or hb[1] or hb[0] < hmin
            if block:
                segs.append((wl.x, wl.y, p2.x, p2.y))
        idx[post] = SegIndex(np.array(segs, dtype=float) if segs else np.zeros((0, 4)))

    for r in rows:
        post = posture_of_height(r["h_u"]) or "retreci"
        c = passage_clearance(idx[post], r["x1"], r["y1"], r["x2"], r["y2"])
        r["w_eff_u"] = 2 * c / XY
        r["duke_w_req_u"] = DUKE_W / r["axis_f"]
        r["cls"] = classify(r["h_u"], r["blocked"], r["w_eff_u"], r["duke_w_req_u"],
                            mobile=(r["secA"] in mobile_sec or r["secB"] in mobile_sec))
    npass = defaultdict(int)
    for r in rows:
        if r["cls"] in POSTURES:
            npass[r["secA"]] += 1
            npass[r["secB"]] += 1

    # --- secteurs ----------------------------------------------------------------------------------------
    srows = []
    for si, s in enumerate(m.sectors):
        if s.wallnum < 3:
            continue
        E = []
        hv = []
        area2 = 0.0
        for w in range(s.wallptr, s.wallptr + s.wallnum):
            wl = m.walls[w]
            if not (0 <= wl.point2 < nwall):
                continue
            p2 = m.walls[wl.point2]
            E.append((wl.x, wl.y, p2.x, p2.y))
            area2 += wl.x * p2.y - p2.x * wl.y
            c, f = zs(m, cz0, fz0, si, wl.x, wl.y)
            hv.append(math.inf if sky[si] else (f - c) / ZU)
        if not E:
            continue
        E = np.array(E, dtype=float)
        hmin, hmax = min(hv), max(hv)
        post = posture_of_height(hmax)
        wsec_u = float("nan")
        if post is not None:
            wsec_u = 2 * sector_clearance(idx[post], E) / XY
        srows.append(dict(corpus=corpus, map=name, sector=si, lotag=s.lotag & 0x7fff, hmin_u=hmin, hmax_u=hmax,
                          area_u2=abs(area2) / 2 / 64.0, cls=post or "plein", w_sec_u=wsec_u,
                          opened=int(si in opened), n_pass=npass.get(si, 0),
                          mobile=int(si in mobile_sec)))
    if verbose:
        print(f"  {corpus}/{name}: {ns} secteurs, {nwall} murs, {len(rows)} passages, portes ouvertes {len(opened)}")
    return rows, srows, dict(sectors=ns, walls=nwall, passages=len(rows), doors_opened=len(opened))


def fmt(v):
    if isinstance(v, float):
        if math.isinf(v):
            return "inf"
        if math.isnan(v):
            return "nan"
        return f"{v:.3f}"
    return v


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", action="append")
    ap.add_argument("--map")
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args(argv)
    os.makedirs(a.out, exist_ok=True)
    corpora = a.corpus or CORPORA
    t0 = time.time()
    all_rows, all_srows, meta = [], [], {}
    for c in corpora:
        files = sorted(glob.glob(os.path.join(MAPS, c, "*.MAP")) + glob.glob(os.path.join(MAPS, c, "*.map")))
        seen = set()
        for p in files:
            name = os.path.splitext(os.path.basename(p))[0].upper()
            if name in seen or (a.map and name != a.map.upper()):
                continue
            seen.add(name)
            try:
                rows, srows, info = analyse_map(c, name, p, a.verbose)
            except Exception as e:  # carte illisible : on la note et on continue
                meta[f"{c}/{name}"] = {"erreur": repr(e)}
                print(f"  ERREUR {c}/{name}: {e!r}")
                continue
            all_rows += rows
            all_srows += srows
            meta[f"{c}/{name}"] = info
        print(f"{c}: {len([k for k in meta if k.startswith(c + '/')])} cartes, "
              f"{sum(1 for r in all_rows if r['corpus'] == c)} passages ({time.time() - t0:.0f} s)")
    suffix = "" if not a.map else f"_{a.map.upper()}"
    for fname, fields, data in (("passages", PASS_FIELDS, all_rows), ("sectors", SEC_FIELDS, all_srows)):
        path = os.path.join(a.out, f"{fname}{suffix}.csv")
        with open(path + ".tmp", "w", newline="", encoding="utf-8") as f:
            wr = csv.writer(f)
            wr.writerow(fields)
            for r in data:
                wr.writerow([fmt(r[k]) for k in fields])
        os.replace(path + ".tmp", path)
    mp = os.path.join(a.out, f"survey_meta{suffix}.json")
    with open(mp + ".tmp", "w", encoding="utf-8") as f:
        json.dump({"maps": meta, "corpora": corpora, "cap_u": 2 * CAP / XY,
                   "seuils_u": {"debout": H_STAND, "accroupi": H_CROUCH, "retreci": H_SHRUNK, "duke_w": DUKE_W}},
                  f, indent=1)
    os.replace(mp + ".tmp", mp)
    print(f"-> {a.out} ({time.time() - t0:.0f} s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

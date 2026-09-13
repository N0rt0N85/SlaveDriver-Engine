#!/usr/bin/env python3
"""quantize.py -- etape E2.5 du convertisseur Duke Nukem 3D PC -> .LEV PowerSlave (fork gen1).

Garde-fou d'arrondi demande par le proprietaire : la conversion vers le .LEV (qui ne stocke que des
\`short\` entiers, SLEVEL.H:115-118) doit donner le MEME resultat des deux cotes d'une jonction, et deux
valeurs distantes de 2 u ou moins doivent tomber sur la meme valeur.

Strategie : quantifier TOT dans l'espace Build, sur la liste de sommets PARTAGEE, AVANT la decoupe
convexe. Ainsi la conversion aval (X = x/8, Z = -y/8, Y = -z/128) devient exacte, sans aucun arrondi
tardif, et les invariants deja verifies (jumelles exactes, aires, convexite) sont recontroles apres.

  - grille : x,y Build -> multiple de 8 (1 u) ; z Build -> multiple de 128 (1 u) ;
  - soudure des sommets distants de <= T (2 u par defaut), par union-find deterministe, avec coupure des
    chaines qui s'etalent sur plus de 2*T ;
  - soudure des hauteurs de sol/plafond de part et d'autre d'un portail, quand les deux cotes sont PLATS
    et distants de <= T (les jonctions de pente sont re-ancragees puis verifiees, pas soudees) ;
  - re-ancrage des pentes : slope_ref re-quantifie sur la grille ;
  - aires recalees ; degenerescences (mur de longueur nulle, secteur de hauteur <= 0) detectees.

Usage (depuis la racine) :
    python tools\\duke2ps\\quantize.py [--in J] [--out J] [--tol-u U] [--no-convex]
Code retour 0 si tous les criteres passent, 1 sinon (le JSON est ecrit dans les deux cas).
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import subprocess
import sys
import time
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import snapmap

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
IN_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_import.json")
OUT_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_quant.json")
CONVEX_PY = os.path.join(HERE, "convex.py")

GRID_XY = 8      # x,y Build -> multiple de 8 (1 u Saturn)
GRID_Z = 128     # z Build -> multiple de 128 (1 u Saturn)
T_U = 2.0        # tolerance de soudure (unites Saturn)

sys.setrecursionlimit(10000)


# ------------------------------------------------------------------------------------------
# geometrie
# ------------------------------------------------------------------------------------------
def slope_z(sec, pre, x, y):
    z = sec[pre + "z"]
    if not sec[pre + "stat"] & 2:
        return z
    x0, y0, x1, y1 = sec["slope_ref"]
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    if L == 0:
        return z
    return z + sec[pre + "heinum"] * (dx * (y - y0) - dy * (x - x0)) / (256.0 * L)


def area2(pts):
    n = len(pts)
    return sum(pts[k][0] * pts[(k + 1) % n][1] - pts[(k + 1) % n][0] * pts[k][1] for k in range(n))


class UnionFind:
    def __init__(self, n):
        self.p = list(range(n))

    def find(self, i):
        while self.p[i] != i:
            self.p[i] = self.p[self.p[i]]
            i = self.p[i]
        return i

    def union(self, i, j):
        ri, rj = self.find(i), self.find(j)
        if ri != rj:
            self.p[rj] = ri


def weld_scalars(values, pairs, T):
    """values : liste de scalaires ; pairs : couples d'indices a souder si |diff| <= T.
    Union-find, puis chaque composante (dont l'etendue max-min <= T) est ramenee a sa mediane.
    Retourne {index: valeur_finale}."""
    n = len(values)
    uf = UnionFind(n)
    for i, j in pairs:
        if abs(values[i] - values[j]) <= T:
            uf.union(i, j)
    comps = defaultdict(list)
    for i in range(n):
        comps[uf.find(i)].append(i)
    out = dict(values) if isinstance(values, dict) else list(values)
    for root, members in comps.items():
        vals = sorted(values[i] for i in members)
        if vals[-1] - vals[0] > T:          # chaine qui s'etale : on ne soude pas
            continue
        med = vals[len(vals) // 2]          # mediane (toujours sur la grille, valeurs entieres)
        for i in members:
            out[i] = med
    return out


# ------------------------------------------------------------------------------------------
# quantification XZ
# ------------------------------------------------------------------------------------------
def quantize_xz(W, T_xy, S=None):
    """Arrondit chaque extremite de mur (x,y) sur la grille, puis soude les sommets distants de <= T_xy.
    Retourne (W, rapport). Les coordonnees sont partagees via point2 : arrondir le (x,y) propre de chaque
    mur arrondit chaque sommet une seule fois ; les murs rouges deja symetriques restent symetriques.
    E2.5b : si S est fourni, l'arrondi preserve les features sub-resolution (snapmap.build_maps) et la
    soudure a interdiction de refusionner les paires ainsi ecartees."""
    report = Counter()
    orig = {w["id"]: (w["x"], w["y"]) for w in W}
    keep_x, keep_y = set(), set()
    # 1. arrondir (avec preservation des features si S est connu)
    if S is not None:
        mx, my, srep = snapmap.build_maps(S, W)
        cx = {tuple(p) for p in srep["contraintes_x"]}
        cy = {tuple(p) for p in srep["contraintes_y"]}
        keep_x, keep_y = snapmap.keep_apart_pairs(mx, my, cx, cy)
        for w in W:
            w["x"], w["y"] = mx[w["x"]], my[w["y"]]
        report["features_preservees"] = srep["paires_preservees"]
        report["valeurs_ecartees"] = srep["ecartees_x"] + srep["ecartees_y"]
        report["snap_iterations"] = srep["iterations"]
        report["snap_derive_max_u"] = srep["derive_max_u"]
        if srep.get("bloque"):
            report["snap_bloque"] = 1
    else:
        for w in W:
            w["x"] = round(w["x"] / GRID_XY) * GRID_XY
            w["y"] = round(w["y"] / GRID_XY) * GRID_XY
    # 2. soudure des sommets distincts distants de <= T_xy
    pts = sorted({(w["x"], w["y"]) for w in W})
    idx = {p: i for i, p in enumerate(pts)}
    n = len(pts)
    uf = UnionFind(n)
    for i in range(n):
        x, y = pts[i]
        for j in range(i + 1, n):
            x2, y2 = pts[j]
            if x2 - x > T_xy:
                break                       # tri sur x : on peut sortir tot
            if (x, x2) in keep_x or (y, y2) in keep_y:
                continue                    # feature preservee par E2.5b : ne jamais refusionner
            if abs(y2 - y) <= T_xy and (x2 - x) ** 2 + (y2 - y) ** 2 <= T_xy * T_xy:
                uf.union(i, j)
    comps = defaultdict(list)
    for i in range(n):
        comps[uf.find(i)].append(i)
    newpt = {}
    for root, members in comps.items():
        ms = sorted(members, key=lambda i: pts[i])
        med = pts[ms[len(ms) // 2]]
        q = (round(med[0] / GRID_XY) * GRID_XY, round(med[1] / GRID_XY) * GRID_XY)
        if len(ms) > 1:
            report["sommets_soudes"] += len(ms) - 1
        for i in ms:
            newpt[pts[i]] = q
    # 3. appliquer
    maxmove = 0.0
    for w in W:
        ox, oy = orig[w["id"]]
        w["x"], w["y"] = newpt[(w["x"], w["y"])]
        maxmove = max(maxmove, math.hypot(w["x"] - ox, w["y"] - oy) / 8.0)
    report["sommets_deplaces_max_u"] = round(maxmove, 3)
    report["sommets_distincts_avant"] = n
    report["sommets_distincts_apres"] = len({(w["x"], w["y"]) for w in W})
    return W, report


# ------------------------------------------------------------------------------------------
# quantification des hauteurs (sols/plafonds) + soudure aux portails plats
# ------------------------------------------------------------------------------------------
def quantize_heights(S, W, T_z):
    """Hauteurs deja sur la grille en general : arrondit puis soude les paires de secteurs PLATS de part
    et d'autre d'un portail quand la hauteur differe de <= T_z. Les jonctions de pente sont ignorees ici
    (verifiees apres re-ancrage). Retourne (S, rapport)."""
    report = Counter()
    for s in S:
        s["floorz"] = round(s["floorz"] / GRID_Z) * GRID_Z
        s["ceilingz"] = round(s["ceilingz"] / GRID_Z) * GRID_Z
    # paires de secteurs adjacents par un portail, tous deux PLATS
    for pre, key in (("floor", "floorz"), ("ceiling", "ceilingz")):
        vals = [s[key] for s in S]
        pairs = []
        seen = set()
        for w in W:
            if w["nextwall"] < 0:
                continue
            a, b = w["sector"], w["nextsector"]
            if a == b:
                continue
            k = (min(a, b), max(a, b))
            if k in seen:
                continue
            seen.add(k)
            if S[a][pre + "stat"] & 2 or S[b][pre + "stat"] & 2:
                report[f"portails_pente_ignores_{pre}"] += 1
                continue
            pairs.append((a, b))
        welded = weld_scalars(vals, pairs, T_z)
        for s in S:
            s[key] = welded[s["id"]]
        report[f"soudes_{pre}"] = sum(1 for i, v in enumerate(vals) if v != welded[i])
    return S, report


# ------------------------------------------------------------------------------------------
# re-ancrage des pentes
# ------------------------------------------------------------------------------------------
def reanchor_slopes(S, W):
    """slope_ref est un couple d'extremites de mur ; on le re-quantifie sur la grille (les murs ont deja
    ete quantifies). Retourne (S, rapport)."""
    report = Counter()
    for s in S:
        if s["floorstat"] & 2 or s["ceilingstat"] & 2:
            x0, y0, x1, y1 = s["slope_ref"]
            qx0 = round(x0 / GRID_XY) * GRID_XY
            qy0 = round(y0 / GRID_XY) * GRID_XY
            qx1 = round(x1 / GRID_XY) * GRID_XY
            qy1 = round(y1 / GRID_XY) * GRID_XY
            if (qx0, qy0, qx1, qy1) != (x0, y0, x1, y1):
                report["pentes_reancreees"] += 1
            s["slope_ref"] = [qx0, qy0, qx1, qy1]
    return S, report


# ------------------------------------------------------------------------------------------
# recalcul des aires par secteur (apres quantification)
# ------------------------------------------------------------------------------------------
def recompute_areas(S, W):
    for s in S:
        tot = 0
        for L in s["loops"]:
            pts = [(W[w]["x"], W[w]["y"]) for w in L]
            # elimine les murs de longueur nulle (consecutifs identiques)
            pts2 = [p for i, p in enumerate(pts) if p != pts[(i + 1) % len(pts)]]
            if len(pts2) >= 3:
                tot += area2(pts2)
        s["area2_build"] = tot
    return S


# ------------------------------------------------------------------------------------------
# degenerescences
# ------------------------------------------------------------------------------------------
def detect_degenerate(S, W):
    zero_len = [w["id"] for w in W
                if (w["x"], w["y"]) == (W[w["point2"]]["x"], W[w["point2"]]["y"])]
    # Build : sol en dessous du plafond => hauteur = floorz - ceilingz > 0
    neg = [s["id"] for s in S
           if s["floorz"] - s["ceilingz"] <= 0]
    return zero_len, neg


def orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def loop_degenerate(pts):
    """pts : liste de points d'une boucle (apres suppression des doublons consecutifs).
    Retourne (self_touch, self_intersect) : paires de sommets non adjacents egaux, et croisements."""
    n = len(pts)
    if n < 3:
        return True, []
    # doublons non adjacents (auto-tangence)
    idx = defaultdict(list)
    for i, p in enumerate(pts):
        idx[p].append(i)
    touch = [i for p, l in idx.items() if len(l) > 1 for i in l]
    # croisements
    cross = []
    for k in range(n):
        a, b = pts[k], pts[(k + 1) % n]
        for j in range(k + 1, n):
            if j == k or (j + 1) % n == k or j == (k + 1) % n:
                continue
            u, w = pts[j], pts[(j + 1) % n]
            if a == u or a == w or b == u or b == w:
                continue
            d1, d2 = orient(a, b, u), orient(a, b, w)
            d3, d4 = orient(u, w, a), orient(u, w, b)
            if d1 * d2 < 0 and d3 * d4 < 0:
                cross.append((k, j))
    return len(touch) > 0, cross


def degenerate_loops(S, W):
    """Compte les boucles degenerees apres quantification (auto-tangence, croisement, doublon consecutif)."""
    out = {"auto_tangentes": 0, "croisements": 0, "secteurs": []}
    for s in S:
        bad = False
        for L in s["loops"]:
            pts = [(W[w]["x"], W[w]["y"]) for w in L]
            pts2 = [p for i, p in enumerate(pts) if p != pts[(i + 1) % len(pts)]]
            if len(pts2) < 3:
                bad = True
                continue
            touch, cross = loop_degenerate(pts2)
            if touch:
                out["auto_tangentes"] += 1
                bad = True
            if cross:
                out["croisements"] += 1
                bad = True
        if bad:
            out["secteurs"].append(s["id"])
    return out


# ------------------------------------------------------------------------------------------
# criteres
# ------------------------------------------------------------------------------------------
def verify(S, W, S0, W0, meta, T_u):
    crit = {}
    defer = {}
    # entiers sur la grille et dans un short
    off = [w["id"] for w in W if w["x"] % GRID_XY or w["y"] % GRID_XY]
    crit["coords_sur_grille"] = {"ok": not off, "hors_grille": len(off)}
    zoff = [s["id"] for s in S for k in ("floorz", "ceilingz") if s[k] % GRID_Z]
    crit["hauteurs_sur_grille"] = {"ok": not zoff, "hors_grille": len(zoff)}
    xmax = max(max(abs(w["x"]) for w in W), 1)
    zmax = max(max(abs(s["floorz"]), abs(s["ceilingz"])) for s in S)
    crit["tient_dans_short"] = {"ok": xmax // GRID_XY <= 16383 and zmax // GRID_Z <= 16383,
                                "note": "|v| <= 16383 u"}
    # symetrie des murs rouges
    asym = 0
    for w in W:
        nw = w["nextwall"]
        if nw >= 0:
            a, b = w, W[nw]
            if not (a["x"] == W[b["point2"]]["x"] and a["y"] == W[b["point2"]]["y"]
                    and b["x"] == W[a["point2"]]["x"] and b["y"] == W[a["point2"]]["y"]):
                asym += 1
    crit["symetrie_rouge"] = {"ok": asym == 0, "asymetriques": asym}
    # composantes connexes
    adj = defaultdict(set)
    for w in W:
        if w["nextwall"] >= 0:
            adj[w["sector"]].add(w["nextsector"])
            adj[w["nextsector"]].add(w["sector"])
    seen = set()
    comps = 0
    for s in S:
        if s["id"] in seen:
            continue
        comps += 1
        st = [s["id"]]
        seen.add(s["id"])
        while st:
            for nb in adj[st.pop()]:
                if nb not in seen:
                    seen.add(nb)
                    st.append(nb)
    crit["une_composante"] = {"ok": comps == 1, "composantes": comps}
    # hauteurs > 0
    zlen, neg = detect_degenerate(S, W)
    crit["aucun_secteur_hauteur_le_0"] = {"ok": not neg, "secteurs": neg[:20]}
    # egalite conservee : deux hauteurs PLATES egales avant restent egales apres (portails plats)
    eq_split = 0
    for w in W:
        if w["nextwall"] < 0:
            continue
        a, b = w["sector"], w["nextsector"]
        for pre, key in (("floor", "floorz"), ("ceiling", "ceilingz")):
            if S[a][pre + "stat"] & 2 or S[b][pre + "stat"] & 2:
                continue
            if S0[a][key] == S0[b][key] and S[a][key] != S[b][key]:
                eq_split += 1
    crit["egalite_conservee_jonctions_plates"] = {"ok": eq_split == 0, "scindees_par_arrondi": eq_split}
    # depart dans son secteur (sans objet sur une carte sans appariement SE7)
    dep = meta.get("depart_m1") or {}
    if not dep.get("build_xy"):
        crit["depart_dans_secteur"] = {"ok": True, "note": "aucun depart SE7 sur cette carte"}
    else:
        qx = round(dep["build_xy"][0] / GRID_XY) * GRID_XY
        qy = round(dep["build_xy"][1] / GRID_XY) * GRID_XY
        dep_ok = False
        for s in S:
            if s["id"] != dep["sector"]:
                continue
            for L in s["loops"]:
                pts = [(W[w]["x"], W[w]["y"]) for w in L]
                pts2 = [p for i, p in enumerate(pts) if p != pts[(i + 1) % len(pts)]]
                if len(pts2) >= 3 and _point_in_loop((qx, qy), pts2) == 1:
                    dep_ok = True
        crit["depart_dans_secteur"] = {"ok": dep_ok,
                                       "note": "test pair-impair sur le secteur du depart"}

    # ---- points A DEFERER (hors garde-fou d'arrondi, relevent d'E3 ou E2.5b) ----
    # petits ecarts de hauteur aux jonctions de PENTE (plan incline continu vs hauteur plate entiere)
    gaps = []
    seen_p = set()
    for w in W:
        if w["nextwall"] < 0:
            continue
        a, b = w["sector"], w["nextsector"]
        k = (min(a, b), max(a, b))
        if k in seen_p:
            continue
        seen_p.add(k)
        x, y = w["x"], w["y"]
        for pre in ("floor", "ceiling"):
            if not (S[a][pre + "stat"] & 2 or S[b][pre + "stat"] & 2):
                continue
            za = slope_z(S[a], pre, x, y) / 128.0
            zb = slope_z(S[b], pre, x, y) / 128.0
            if 0 < abs(za - zb) <= T_u:
                gaps.append((a, b, pre, round(za - zb, 3)))
    defer["petits_ecarts_pente"] = {"n": len(gaps), "ecarts": gaps,
                                    "note": "plan incline continu vs hauteur entiere ; releve d'E3"}
    # boucles degenerees (features sub-resolution qui se replient)
    dg = degenerate_loops(S, W)
    defer["boucles_degenerees"] = {"auto_tangentes": dg["auto_tangentes"], "croisements": dg["croisements"],
                                   "secteurs": dg["secteurs"], "n_secteurs": len(dg["secteurs"]),
                                   "note": "features sub-resolution (encoches/eclats < 1 u) ; E2.5b"}
    defer["murs_longueur_nulle"] = {"murs": zlen, "n": len(zlen),
                                    "note": "convex.py les ignore deja ; extremites d'eclats"}
    return crit, defer


def _point_in_loop(p, pts):
    px, py = p
    c = False
    n = len(pts)
    for k in range(n):
        a, b = pts[k], pts[(k + 1) % n]
        if (a[1] > py) != (b[1] > py):
            lhs = (b[0] - a[0]) * (py - a[1]); rhs = (px - a[0]) * (b[1] - a[1])
            if (lhs > rhs) if b[1] > a[1] else (lhs < rhs):
                c = not c
    return 1 if c else 0


# ------------------------------------------------------------------------------------------
# principal
# ------------------------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--in", dest="inp", default=IN_DEFAULT)
    ap.add_argument("--out", default=OUT_DEFAULT)
    ap.add_argument("--tol-u", type=float, default=T_U, help="tolerance de soudure (u, defaut 2)")
    ap.add_argument("--no-convex", action="store_true", help="ne pas relancer convex.py")
    args = ap.parse_args(argv)

    T_xy = int(round(args.tol_u * GRID_XY))     # 2 u -> 16 Build
    T_z = int(round(args.tol_u * GRID_Z))       # 2 u -> 256 Build
    sys.stdout.reconfigure(encoding="utf-8")
    t0 = time.time()

    raw = open(args.inp, "rb").read()
    d = json.loads(raw)
    S, W, SP = d["sectors"], d["walls"], d["sprites"]
    S0 = copy.deepcopy(S)      # original, pour les controles relatifs (avant/apres)
    W0 = copy.deepcopy(W)
    print(f"E2.5 quantification : entree {os.path.relpath(args.inp, ROOT)} ({d['format']}, sha1 "
          f"{hashlib.sha1(raw).hexdigest()[:12]}) : {len(S)} secteurs, {len(W)} murs, T = {args.tol_u} u")

    # depart : quantifier build_xy. Il est absent sur toute carte sans appariement SE7 (la logique
    # de depart d'E1 est taillee pour E1L1) : ce n'est pas une anomalie de quantification.
    dep = d.get("depart_m1") or {}
    if dep.get("build_xy"):
        dep["build_xy"] = [round(dep["build_xy"][0] / GRID_XY) * GRID_XY,
                           round(dep["build_xy"][1] / GRID_XY) * GRID_XY]
        if dep.get("saturn"):
            dep["saturn"]["X"] = dep["build_xy"][0] / 8.0
            dep["saturn"]["Z"] = -dep["build_xy"][1] / 8.0

    rep = {}
    W, rep["xz"] = quantize_xz(W, T_xy, S)
    S, rep["hauteurs"] = quantize_heights(S, W, T_z)
    S, rep["pentes"] = reanchor_slopes(S, W)
    S = recompute_areas(S, W)
    print(f"  XZ : {rep['xz']['sommets_distincts_avant']} -> {rep['xz']['sommets_distincts_apres']} sommets "
          f"distincts, {rep['xz']['sommets_soudes']} soudures, deplacement max {rep['xz']['sommets_deplaces_max_u']} u")
    print(f"  hauteurs : sols soudees {rep['hauteurs']['soudes_floor']}, plafonds soudees "
          f"{rep['hauteurs']['soudes_ceiling']} ; portails de pente ignores "
          f"{rep['hauteurs']['portails_pente_ignores_floor']} ; pentes re-ancreees {rep['pentes']['pentes_reancreees']}")

    crit, defer = verify(S, W, S0, W0, d, args.tol_u)
    all_ok = all(v["ok"] for v in crit.values())

    out = dict(d)
    out["format"] = "duke2ps/e2.5-quant v1"
    out["quantization"] = {
        "source": os.path.relpath(args.inp, ROOT).replace("\\", "/"),
        "source_sha1": hashlib.sha1(raw).hexdigest(),
        "grille_xy": GRID_XY, "grille_z": GRID_Z, "tol_u": args.tol_u,
        "rapport": {k: dict(v) for k, v in rep.items()},
        "criteres": crit,
        "a_deferer": defer,
    }
    blob = json.dumps(out, ensure_ascii=False, indent=1).encode("utf-8")
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    tmp = args.out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(blob)
    os.replace(tmp, args.out)

    print("CRITERES :")
    for k, v in crit.items():
        extra = {kk: vv for kk, vv in v.items() if kk != "ok"}
        print(f"  [{'OK' if v['ok'] else 'ECHEC'}] {k} : {json.dumps(extra, ensure_ascii=False)}")
    print("A DEFERER (hors garde-fou) :")
    for k, v in defer.items():
        print(f"  {k} : {json.dumps(v, ensure_ascii=False)}")
    print(f"sortie {os.path.relpath(args.out, ROOT)} ({len(blob)} o, sha1 {hashlib.sha1(blob).hexdigest()[:12]}) ; "
          f"{time.time() - t0:.1f} s")

    # relance de la decoupe convexe sur le modele quantifie
    convex_ok = None
    if not args.no_convex:
        cout = args.out.replace("_quant.json", "_quant_convex.json")
        cpng = os.path.join(os.path.dirname(args.out), "e1l1_quant_convex.png")
        print("\nrelance convex.py sur le modele quantifie ...")
        r = subprocess.run([sys.executable, CONVEX_PY, "--in", args.out, "--out", cout, "--png", cpng],
                           capture_output=True, text=True, cwd=ROOT)
        print(r.stdout.strip()[-3000:])
        if r.returncode != 0:
            print("convex.py : ECHEC (code %d)" % r.returncode)
            convex_ok = False
        else:
            print("convex.py : OK")
            convex_ok = True

    return 0 if (all_ok and (convex_ok is None or convex_ok)) else 1


if __name__ == "__main__":
    sys.exit(main())

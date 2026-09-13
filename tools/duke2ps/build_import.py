#!/usr/bin/env python3
"""build_import.py -- etape E1 du convertisseur Duke Nukem 3D PC -> .LEV PowerSlave (fork gen1).

Import d'une carte Build v7 (tools/buildmap.py) et nettoyage pour le jalon M1 :
  1. composantes connexes du graphe des secteurs (nextsector) : on garde la plus grande,
     on retire le toit (composante de cursectnum) et les ilots inaccessibles ;
  2. portes/rideaux/vantaux mis dans un etat STATIQUE OUVERT, secteurs fermes sans role = volumes
     pleins (regles R-* ci-dessous, toutes HYPOTHESE a valider sur console) ;
  3. plafonds de ciel (ceilingstat & 1) plafonnes a Y <= 1760 u Saturn (Y = -z/128) ;
  4. depart M1 = point d'arrivee du SE7 du toit dans la composante gardee ;
  5. nettoyage topologique exact (pics de largeur nulle, boucles pincees) puis sortie JSON.

Repere Duke Saturn (decision owner 2026-09-10, mesure HOLYWOOD) : X = x/8, Z = -y/8, Y = -z/128.

Usage (depuis la racine du depot) :
    python tools\\duke2ps\\build_import.py [--map M] [--out J] [--png P | --no-png] [--cap-all-ceilings]
Code retour 0 si tous les criteres d'acceptation passent, 1 sinon (le JSON est ecrit dans les deux cas).

REGLES (HYPOTHESE = etat statique choisi ; la dynamique citee est SOURCE)
  R-20  secteur lotag 20 ferme : ceilingz := ceilingz de nextsectorneighborz(sn, ceilingz, -1, -1)
        (= ce que fait l'ouverture Duke, sector.c:805-819) ; sol inchange.
  R-23  tout secteur lotag 23 (vantail de porte battante, SE11) : vantail SUPPRIME. Si c'est une ile
        (1 boucle, tous ses murs rouges vers un meme conteneur) : on supprime le vantail ET, dans le
        conteneur, soit la boucle-trou jumelle, soit la sous-chaine pincee jumelle (le vantail touche le
        chambranle en un sommet). Sinon : "air" (hauteurs des voisins, voir R-27b).
  R-27  tout secteur lotag 27 (dans E1L1 : bandes-rideaux de 64 u Build tirees par un SE20, pas des
        ponts) : a) ile -> supprimee avec son trou ; b) sinon "air" : plafond = le plus BAS des voisins
        ouverts, sol = le plus HAUT, attributs de surface du voisin donneur, surfaces plates.
  R-25  secteur lotag 25 (panneau de porte coulissante SE15) dont la boucle n'est pas simple (panneau
        replie en pics de largeur nulle) : ouvert = enveloppe convexe de ses sommets ; ses portails sont
        gardes (ils doivent tomber sur l'enveloppe), les trous de l'enveloppe deviennent des murs pleins
        neufs (texture du plus long mur plein retire).
  R-0   secteur ferme de lotag 0 (ou lotag non gere) : volume plein -> secteur supprime ; les murs rouges
        des voisins vers lui deviennent pleins (rendu Build identique : haut + bas se rejoignent).
  T-pic pic de largeur nulle (aretes consecutives colineaires et opposees) : replie exactement ; si le
        mur replie etait un portail, son jumeau est coupe au point de jonction en T ; les deux cotes
        exterieurs deviennent jumeaux (ou plein, texture du mur plein qui etait vu).
  T-pin boucle qui repasse par un meme sommet : scindee en deux boucles (aucune coordonnee ne bouge).

SOURCES (chemins relatifs a refs/build/) :
  jfbuild/src/build.c:5732-5766   clockdir() : 0 = CW, 1 = CCW (repere Build, y vers le bas)
  jfbuild/src/build.c:4401,4428,4470  ile/secteur neuf force CW ; boucle ajoutee DANS un secteur forcee
                                  CCW et inseree a sector.wallptr (l'exterieure n'est pas forcement 1re)
  jfbuild/src/engine.c:8534-      nextsectorneighborz()
  jfbuild/src/engine.c:10567-10584 getzsofslope() (reference de pente = 1er mur du secteur + son point2)
  jfduke3d/src/sector.c:786-822   porte de plafond (lotag 20)
  jfduke3d/src/sector.c:870-909 + actors.c:5753-5810  porte battante (lotag 23, SE11 fait tourner le vantail)
  jfduke3d/src/sector.c:943-960 + actors.c:6297-6361 + game.c:4755-4798  lotag 27 / SE20 : tire les 2
                                  sommets les plus proches du SE le long de son angle
  jfduke3d/src/premap.c:771-773 + game.c:4604  GPSPEED : sector.extra = lotag ; SE.yvel = sector.extra
  jfduke3d/src/actors.c:5944-5974 + game.c:4900-4940  SE15 porte coulissante (ms() deplace le secteur)
  jfduke3d/src/game.c:4613-4628   appariement SE7 : 1er sprite SE (lotag 7 ou 23), j != i, meme hitag ;
                                  T5 (onfloorz) = sector.floorz == sprite.z
  jfduke3d/src/actors.c:2728-2748 transport SE7 onfloorz == 0 : xy += OW - SE, z = OW.z + 6144, angle inchange
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import os
import sys
import tempfile
from collections import Counter, defaultdict
from dataclasses import asdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import buildmap  # noqa: E402

SKY_CAP_Y = 1760                  # u Saturn (plafond du ciel mesure sur HOLYWOOD, DUKE_PC_TO_SATURN.md §2.2)
SKY_CAP_Z = -SKY_CAP_Y * 128      # z Build correspondant (Y = -z/128) = -225280
SECTOREFFECTOR = 1                # jfduke3d/src/names.h
PICNAMES = {1: "SECTOREFFECTOR", 2: "ACTIVATOR", 3: "TOUCHPLATE", 4: "ACTIVATORLOCKED",
            5: "MUSICANDSFX", 6: "LOCATORS", 7: "CYCLER", 8: "MASTERSWITCH", 9: "RESPAWN",
            10: "GPSPEED"}
MAXSTATUS = 1024                  # jfbuild build.h
EXPECT_MAIN, EXPECT_ROOF = 293, 18
WALL_ATTRS = ("cstat", "picnum", "overpicnum", "shade", "pal", "xrepeat", "yrepeat", "xpanning",
              "ypanning", "lotag", "hitag", "extra")


# ------------------------------------------------------------------------------------------
# geometrie entiere exacte
# ------------------------------------------------------------------------------------------
def orient(ax, ay, bx, by, cx, cy):
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)


def on_seg(ax, ay, bx, by, px, py):
    return min(ax, bx) <= px <= max(ax, bx) and min(ay, by) <= py <= max(ay, by)


def seg_inter(a, b, c, d):
    """None, 'cross' (croisement propre) ou 'touch' (contact/recouvrement). Entier exact."""
    d1 = orient(*c, *d, *a); d2 = orient(*c, *d, *b)
    d3 = orient(*a, *b, *c); d4 = orient(*a, *b, *d)
    if ((d1 > 0 > d2) or (d1 < 0 < d2)) and ((d3 > 0 > d4) or (d3 < 0 < d4)):
        return "cross"
    if (d1 == 0 and on_seg(*c, *d, *a)) or (d2 == 0 and on_seg(*c, *d, *b)) or \
       (d3 == 0 and on_seg(*a, *b, *c)) or (d4 == 0 and on_seg(*a, *b, *d)):
        return "touch"
    return None


def area2(pts):
    """Somme de lacets sum(x_i*y_{i+1} - x_{i+1}*y_i), coordonnees Build brutes (y vers le bas)."""
    n = len(pts)
    return sum(pts[k][0] * pts[(k + 1) % n][1] - pts[(k + 1) % n][0] * pts[k][1] for k in range(n))


def clockdir(pts):
    """Port exact de jfbuild build.c:5732-5766 sur une boucle (pts[k] = debut du mur k). 0 = CW, 1 = CCW."""
    n = len(pts)
    minx, themin = 0x7FFFFFFF, -1
    for k in range(n):
        ex = pts[(k + 1) % n][0]
        if ex < minx:
            minx, themin = ex, k
    x0, y0 = pts[themin]
    x1, y1 = pts[(themin + 1) % n]
    x2, y2 = pts[(themin + 2) % n]
    if y1 >= y2 and y1 <= y0:
        return 0
    if y1 >= y0 and y1 <= y2:
        return 1
    t = (x0 - x1) * (y2 - y1) - (x2 - x1) * (y0 - y1)
    return 0 if t < 0 else 1


def point_in_loop(px, py, pts):
    """1 dedans, 0 dehors, 2 sur le bord (entier exact, pair-impair)."""
    c = False
    n = len(pts)
    for k in range(n):
        ax, ay = pts[k]; bx, by = pts[(k + 1) % n]
        if orient(ax, ay, bx, by, px, py) == 0 and on_seg(ax, ay, bx, by, px, py):
            return 2
        if (ay > py) != (by > py):
            lhs = (bx - ax) * (py - ay); rhs = (px - ax) * (by - ay)
            if (lhs > rhs) if by > ay else (lhs < rhs):
                c = not c
    return 1 if c else 0


def loop_self_defects(pts):
    """Defauts d'une boucle : aretes nulles, pics (aller-retour colineaire), croisements ('cross') et
    contacts ('touch') entre aretes non adjacentes. Test entier exact."""
    n = len(pts)
    if n < 3:
        return [("moins_de_3_murs", -1, -1)]
    out = []
    E = [(pts[k], pts[(k + 1) % n]) for k in range(n)]
    for k, (a, b) in enumerate(E):
        if a == b:
            out.append(("arete_nulle", k, k))
    for k in range(n):
        a, b = E[k]; c, d = E[(k + 1) % n]
        if orient(*a, *b, *d) == 0 and ((b[0] - a[0]) * (d[0] - c[0]) + (b[1] - a[1]) * (d[1] - c[1])) < 0:
            out.append(("pic", k, (k + 1) % n))
    bb = [(min(a[0], b[0]), max(a[0], b[0]), min(a[1], b[1]), max(a[1], b[1])) for a, b in E]
    for i in range(n):
        for j in range(i + 2, n):
            if i == 0 and j == n - 1:
                continue
            if bb[i][1] < bb[j][0] or bb[j][1] < bb[i][0] or bb[i][3] < bb[j][2] or bb[j][3] < bb[i][2]:
                continue
            r = seg_inter(*E[i], *E[j])
            if r:
                out.append((r, i, j))
    return out


def convex_hull(points):
    """Chaine monotone, entier exact, sans sommets colineaires ; sens = somme de lacets > 0
    (= exterieure Build, CW en y vers le bas)."""
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts

    def half(seq):
        h = []
        for p in seq:
            while len(h) >= 2 and orient(*h[-2], *h[-1], *p) <= 0:
                h.pop()
            h.append(p)
        return h
    lo, up = half(pts), half(list(reversed(pts)))
    return lo[:-1] + up[:-1]


def cdiv(a, b):
    """Division entiere C (troncature vers 0)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def slope_z(base, heinum, stat, ref, x, y):
    """getzsofslope (engine.c:10567-10584) ; isqrt a la place de nsqrtasm (table approchee)."""
    if not stat & 2:
        return base
    x0, y0, x1, y1 = ref
    dx, dy = x1 - x0, y1 - y0
    i = math.isqrt(dx * dx + dy * dy) << 5
    if i == 0:
        return base
    j = (dx * (y - y0) - dy * (x - x0)) >> 3       # dmulscale3
    return base + cdiv(heinum * j, i)               # scale()


def dist_point_seg(px, py, a, b):
    ax, ay = a; bx, by = b
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


# ------------------------------------------------------------------------------------------
# lecture Build
# ------------------------------------------------------------------------------------------
def sector_loops(W, s):
    """Boucles d'un secteur en suivant point2 depuis wallptr (anomalies = point2 != w+1 hors fermeture)."""
    loops, anomalies = [], 0
    w, end = s.wallptr, s.wallptr + s.wallnum
    while w < end:
        start, loop = w, []
        while True:
            loop.append(w)
            p = W[w].point2
            if p == start:
                break
            if p != w + 1:
                anomalies += 1
            w = p
            if len(loop) > s.wallnum:
                raise ValueError("boucle non fermee")
        loops.append(loop)
        w = max(loop) + 1
    return loops, anomalies


def components(n, edges):
    par = list(range(n))

    def f(a):
        while par[a] != a:
            par[a] = par[par[a]]
            a = par[a]
        return a
    for a, b in edges:
        ra, rb = f(a), f(b)
        if ra != rb:
            par[ra] = rb
    comp = defaultdict(list)
    for i in range(n):
        comp[f(i)].append(i)
    return sorted(comp.values(), key=lambda c: (-len(c), c[0]))


def se_owner(SP, i):
    """game.c:4613-4628 : OW d'un SE7 = 1er sprite SE lotag 7|23, j != i, meme hitag ; SE23 -> lui-meme."""
    s = SP[i]
    if s.lotag == 23:
        return i
    for j, t in enumerate(SP):
        if t.statnum < MAXSTATUS and t.picnum == SECTOREFFECTOR and t.lotag in (7, 23) and j != i \
                and t.hitag == s.hitag:
            return j
    return None


# ------------------------------------------------------------------------------------------
# graphe de murs mutable
# ------------------------------------------------------------------------------------------
class MW:
    """Mur mutable : debut (x, y) ; la fin est le debut du mur suivant de sa boucle."""
    __slots__ = ("x", "y", "sec", "twin", "a", "bid", "prov", "tex_from", "orig_seg", "solid_from", "loop")

    def __init__(self, x, y, sec, a, bid):
        self.x, self.y, self.sec, self.a, self.bid = x, y, sec, a, bid
        self.twin = None
        self.prov = []
        self.tex_from = None
        self.orig_seg = None
        self.solid_from = None
        self.loop = None


def nxt(w):
    L = w.loop
    return L[(L.index(w) + 1) % len(L)]


def pts_of(L):
    return [(w.x, w.y) for w in L]


# ------------------------------------------------------------------------------------------
# ecriture
# ------------------------------------------------------------------------------------------
def atomic_write(path, data: bytes):
    d = os.path.dirname(os.path.abspath(path))
    os.makedirs(d, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=d, prefix=".tmp_", suffix=os.path.basename(path))
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


def dump_json(obj):
    """JSON lisible : une ligne par enregistrement pour sectors/walls/sprites."""
    parts = ["{"]
    keys = list(obj.keys())
    for k_i, k in enumerate(keys):
        v = obj[k]
        comma = "," if k_i < len(keys) - 1 else ""
        if k in ("sectors", "walls", "sprites") and isinstance(v, list):
            parts.append(f' {json.dumps(k)}: [')
            for r_i, r in enumerate(v):
                parts.append("  " + json.dumps(r, ensure_ascii=False, separators=(",", ":")) +
                             ("," if r_i < len(v) - 1 else ""))
            parts.append(" ]" + comma)
        else:
            body = json.dumps(v, ensure_ascii=False, indent=1).replace("\n", "\n ")
            parts.append(f" {json.dumps(k)}: {body}{comma}")
    parts.append("}")
    return ("\n".join(parts) + "\n").encode("utf-8")


# ------------------------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description="E1 : import Build + nettoyage (Duke PC -> PowerSlave)")
    ap.add_argument("--map", default=os.path.join(ROOT, "refs", "build", "duke13", "maps", "E1L1.MAP"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "duke2ps", "e1l1_import.json"))
    ap.add_argument("--png", default=os.path.join(ROOT, "build", "duke2ps", "e1l1_import.png"))
    ap.add_argument("--no-png", action="store_true")
    ap.add_argument("--cap-all-ceilings", action="store_true",
                    help="plafonner aussi les plafonds NON ciel a Y <= 1760 (defaut : ciel seulement)")
    args = ap.parse_args(argv)

    raw = open(args.map, "rb").read()
    sha1 = hashlib.sha1(raw).hexdigest()
    m = buildmap.parse(raw)
    S, W, SP = m.sectors, m.walls, m.sprites
    NS = len(S)
    print(f"[E1] {os.path.relpath(args.map, ROOT)} sha1 {sha1} : v{m.version}, {NS} secteurs, {len(W)} murs, "
          f"{len(SP)} sprites ; depart ({m.posx},{m.posy},{m.posz}) ang {m.ang} secteur {m.cursectnum}")

    wsec = [0] * len(W)
    loops = {}
    loop_anom = 0
    for si, s in enumerate(S):
        for w in range(s.wallptr, s.wallptr + s.wallnum):
            wsec[w] = si
        loops[si], a = sector_loops(W, s)
        loop_anom += a
    spr_in = defaultdict(list)
    for i, p in enumerate(SP):
        spr_in[p.sectnum].append(i)

    def verts(si):
        return [(W[w].x, W[w].y) for L in loops[si] for w in L]

    def seg0(bid):
        return None if bid is None else [W[bid].x, W[bid].y, W[W[bid].point2].x, W[W[bid].point2].y]

    slope_ref = {}
    for si, s in enumerate(S):
        a = W[s.wallptr]; b = W[a.point2]
        slope_ref[si] = (a.x, a.y, b.x, b.y)

    orig_defects = {(si, li): loop_self_defects([(W[w].x, W[w].y) for w in L])
                    for si in range(NS) for li, L in enumerate(loops[si])}

    # ---------------------------------------------------------------- 1. composantes
    comps = components(NS, [(wsec[i], w.nextsector) for i, w in enumerate(W) if w.nextsector >= 0])
    comp_of = {si: ci for ci, c in enumerate(comps) for si in c}
    main_ci, roof_ci = 0, comp_of[m.cursectnum]
    print(f"\n[1] composantes (portails nextsector) : {len(comps)} de tailles {[len(c) for c in comps]}")
    removed = []
    for ci, c in enumerate(comps):
        if ci == main_ci:
            continue
        xs = [x for si in c for x, _ in verts(si)]
        ys = [y for si in c for _, y in verts(si)]
        sprs = [j for si in c for j in spr_in[si]]
        links = []
        for j in sprs:
            p = SP[j]
            if p.picnum == SECTOREFFECTOR and p.lotag in (7, 17, 23):
                partners = [k for k, q in enumerate(SP) if k != j and q.picnum == SECTOREFFECTOR and
                            q.lotag in (7, 17, 23) and q.hitag == p.hitag]
                links.append({"sprite": j, "lotag": p.lotag, "hitag": p.hitag,
                              "partenaires": [(k, SP[k].sectnum, comp_of[SP[k].sectnum]) for k in partners]})
        if ci == roof_ci:
            reason = f"TOIT (decision owner, jalon M1b) : contient cursectnum {m.cursectnum}, relie au reste par SE7"
        else:
            reason = "ilot inaccessible : aucun portail vers la composante principale"
            reason += (" MAIS lien SE vers la principale (a verifier)"
                       if any(pp[2] == main_ci for L in links for pp in L["partenaires"]) else " ni lien SE7/SE17/SE23")
        removed.append({"composante": ci, "secteurs": c, "n": len(c), "raison": reason,
                        "bbox_build": [min(xs), min(ys), max(xs), max(ys)], "sprites": len(sprs),
                        "picnums_sprites": dict(Counter(SP[j].picnum for j in sprs)), "liens_SE": links})
        print(f"    retire comp {ci} ({len(c)} secteurs {c}) : {reason} ; bbox x {min(xs)}..{max(xs)} "
              f"y {min(ys)}..{max(ys)} ; {len(sprs)} sprites ; liens SE "
              f"{[(L['sprite'], L['lotag'], L['hitag'], L['partenaires']) for L in links]}")
    main = sorted(comps[main_ci])
    main_set = set(main)
    print(f"    garde comp {main_ci} : {len(main)} secteurs (attendu {EXPECT_MAIN}) ; toit {len(comps[roof_ci])} "
          f"(attendu {EXPECT_ROOF})")

    sec = {si: asdict(S[si]) for si in main}
    for d in sec.values():
        d.pop("raw_ceilingheinum", None)
        d.pop("raw_floorheinum", None)
    treatments = defaultdict(list)
    cat = {}   # categorie pour la vue PNG

    def cz_at(si, x, y):
        d = sec[si]; return slope_z(d["ceilingz"], d["ceilingheinum"], d["ceilingstat"], slope_ref[si], x, y)

    def fz_at(si, x, y):
        d = sec[si]; return slope_z(d["floorz"], d["floorheinum"], d["floorstat"], slope_ref[si], x, y)

    # ---------------------------------------------------------------- 3. ciel (avant les portes : les
    # portes lisent les plafonds voisins dans leur etat final)
    sky_rows = []
    for si in main:
        d = sec[si]
        is_sky = bool(d["ceilingstat"] & 1)
        if not is_sky and not args.cap_all_ceilings:
            continue
        ymax = max(-cz_at(si, x, y) / 128 for x, y in verts(si))
        if ymax > SKY_CAP_Y:
            fymax = max(-fz_at(si, x, y) / 128 for x, y in verts(si))
            if fymax >= SKY_CAP_Y:
                # le plafonnement fermerait le secteur (sol deja a Y >= 1760) : refuse, jamais de suppression
                treatments[si].append(f"plafonnement refuse (sol Y max {fymax})")
                sky_rows.append({"build_sector": si, "ciel": is_sky, "ceilingz_avant": d["ceilingz"],
                                 "Y_max_avant": round(ymax, 3), "refuse": f"sol Y max {fymax} >= {SKY_CAP_Y}"})
                print(f"    plafonnement REFUSE pour {si} (ciel={is_sky}) : sol Y max {fymax} >= {SKY_CAP_Y}")
                continue
            before = d["ceilingz"]
            flattened = bool(d["ceilingstat"] & 2)
            d["ceilingz"] = SKY_CAP_Z
            if flattened:
                d["ceilingstat"] &= ~2
                d["ceilingheinum"] = 0
            treatments[si].append(("ciel" if is_sky else "plafond") + "_plafonne" + ("_aplati" if flattened else ""))
            cat[si] = "ciel"
            sky_rows.append({"build_sector": si, "ciel": is_sky, "ceilingz_avant": before, "Y_max_avant": round(ymax, 3),
                             "ceilingz_apres": SKY_CAP_Z, "pente_aplatie": flattened})
    n_sky = sum(1 for si in main if sec[si]["ceilingstat"] & 1)
    capped = [r for r in sky_rows if "refuse" not in r]
    print(f"\n[3] plafonds de ciel (ceilingstat&1) dans la composante gardee : {n_sky} ; plafonnes a z={SKY_CAP_Z} "
          f"(Y={SKY_CAP_Y}) : {sum(r['ciel'] for r in capped)} (non ciel : {sum(not r['ciel'] for r in capped)} ; "
          f"pentes aplaties : {sum(r.get('pente_aplatie', False) for r in capped)} ; refuses : "
          f"{[r['build_sector'] for r in sky_rows if 'refuse' in r]}) ; Y avant "
          f"{min((r['Y_max_avant'] for r in capped), default=0)}..{max((r['Y_max_avant'] for r in capped), default=0)}")

    # ---------------------------------------------------------------- 2. portes, rideaux, volumes pleins
    closed = [si for si in main if sec[si]["floorz"] - sec[si]["ceilingz"] <= 0]
    print(f"\n[2] secteurs fermes (floorz - ceilingz <= 0) dans la composante gardee : {len(closed)} ; lotags "
          f"{sorted(Counter(sec[si]['lotag'] for si in closed).items())}")
    deleted = {}                          # build sector -> raison
    removed_loops = set()                 # (conteneur, index de boucle) : trou supprime en entier
    removed_chains = defaultdict(set)     # (conteneur, index de boucle) -> murs de la sous-chaine pincee retires
    rows = []

    def nb_walls(si):
        return [w for L in loops[si] for w in L if W[w].nextsector >= 0]

    def island_container(si):
        """(c, li, chaine) : chaine None = trou entier de c ; sinon sous-chaine contigue de la boucle li de c
        qui part et revient au meme sommet (boucle pincee)."""
        ws = [w for L in loops[si] for w in L]
        ns = {W[w].nextsector for w in ws}
        if len(loops[si]) != 1 or len(ns) != 1 or -1 in ns:
            return None
        c = next(iter(ns))
        twins = {W[w].nextwall for w in ws}
        for li, L in enumerate(loops[c]):
            if set(L) == twins:
                return c, li, None
            if twins <= set(L):
                n = len(L)
                pos = sorted(L.index(t) for t in twins)
                for r in range(n):
                    idx = [(p - r) % n for p in pos]
                    if max(idx) - min(idx) == len(idx) - 1:
                        chain = [L[(min(idx) + r + k) % n] for k in range(len(idx))]
                        a = W[chain[0]]; z = W[W[chain[-1]].point2]
                        if (a.x, a.y) == (z.x, z.y):
                            return c, li, chain
                        break
        return None

    def se_summary(si):
        return [(j, PICNAMES.get(SP[j].picnum, SP[j].picnum), SP[j].lotag, SP[j].hitag, SP[j].ang)
                for j in spr_in[si]]

    def base_row(si, cls):
        d = sec[si]
        return {"build_sector": si, "lotag": d["lotag"], "classe": cls,
                "ferme": d["floorz"] - d["ceilingz"] <= 0, "avant": [d["floorz"], d["ceilingz"]],
                "sprites": se_summary(si)}

    def air_fill(si):
        cands = [n for n in dict.fromkeys(W[w].nextsector for w in nb_walls(si))
                 if n not in deleted and sec[n]["floorz"] - sec[n]["ceilingz"] > 0]
        if not cands:
            return None
        shared = defaultdict(float)
        for w in nb_walls(si):
            a = W[w]; b = W[a.point2]
            shared[a.nextsector] += math.hypot(b.x - a.x, b.y - a.y)
        cg = min(cands, key=lambda n: (-sec[n]["ceilingz"], -shared[n], n))   # plafond le plus bas
        fg = min(cands, key=lambda n: (sec[n]["floorz"], -shared[n], n))      # sol le plus haut
        d, cd, fd = sec[si], sec[cg], sec[fg]
        if fd["floorz"] - cd["ceilingz"] <= 0:
            return None
        sloped = bool(cd["ceilingstat"] & 2) or bool(fd["floorstat"] & 2)
        for k in ("ceilingz", "ceilingpicnum", "ceilingshade", "ceilingpal", "ceilingxpanning", "ceilingypanning"):
            d[k] = cd[k]
        d["ceilingstat"] = cd["ceilingstat"] & ~2; d["ceilingheinum"] = 0
        for k in ("floorz", "floorpicnum", "floorshade", "floorpal", "floorxpanning", "floorypanning"):
            d[k] = fd[k]
        d["floorstat"] = fd["floorstat"] & ~2; d["floorheinum"] = 0
        return cg, fg, sloped

    # R-20
    for si in [s for s in closed if sec[s]["lotag"] == 20]:
        d = sec[si]
        row = base_row(si, "porte_plafond")
        best, bestz = None, -0x80000000
        for w in nb_walls(si):                      # nextsectorneighborz(sn, cz, -1, -1), ordre des murs
            tz = sec[W[w].nextsector]["ceilingz"]
            if tz < d["ceilingz"] and tz > bestz:
                bestz, best = tz, W[w].nextsector
        if best is None:
            deleted[si] = "porte 20 sans voisin plus haut -> volume plein"
            row.update(regle="R-20 repli : volume plein", apres="supprime")
            cat[si] = "volume_plein"
        else:
            d["ceilingz"] = bestz
            treatments[si].append(f"R-20 porte ouverte (plafond de {best})")
            row.update(regle=f"R-20 : ceilingz := ceilingz du voisin {best} (nextsectorneighborz, haut le plus proche)",
                       voisin=best, apres=[d["floorz"], d["ceilingz"]])
            cat[si] = "porte20"
        rows.append(row)

    # R-23 et R-27 (tous les secteurs de ces lotags, fermes ou non)
    for si in [s for s in main if sec[s]["lotag"] in (23, 27)]:
        lo = sec[si]["lotag"]
        cls = "vantail_porte_battante" if lo == 23 else "rideau_SE20"
        row = base_row(si, cls)
        isl = island_container(si)
        if isl is not None:
            c, li, chain = isl
            deleted[si] = f"R-{lo}a {cls} supprime (conteneur {c})"
            if chain is None:
                removed_loops.add((c, li))
                how = f"ile ET boucle-trou jumelle de {c} supprimees"
            else:
                removed_chains[(c, li)].update(chain)
                how = f"ile ET sous-chaine pincee jumelle de {c} ({len(chain)} murs) supprimees"
            treatments[c].append(f"R-{lo}a vantail/rideau {si} retire")
            row.update(regle=f"R-{lo}a : {how}", conteneur=c, apres="supprime")
            cat[si] = "ile_supprimee"
        else:
            g = air_fill(si)
            if g is None:
                deleted[si] = f"{cls} sans voisin ouvert -> volume plein"
                row.update(regle=f"R-{lo} repli : volume plein", apres="supprime")
                cat[si] = "volume_plein"
            else:
                cg, fg, sloped = g
                treatments[si].append(f"R-{lo}b air (plafond de {cg}, sol de {fg})")
                row.update(regle=f"R-{lo}b : air : plafond = le plus bas des voisins ouverts ({cg}), sol = le plus "
                                 f"haut ({fg}), attributs de surface copies, plats",
                           plafond_de=cg, sol_de=fg, apres=[sec[si]["floorz"], sec[si]["ceilingz"]],
                           donneur_en_pente=sloped)
                cat[si] = "air"
        rows.append(row)

    # R-0 (et repli pour tout autre lotag ferme)
    for si in [s for s in closed if s not in deleted and sec[s]["lotag"] not in (20, 23, 27)]:
        lo = sec[si]["lotag"]
        deleted[si] = "R-0 lotag 0 ferme sans role -> volume plein" if lo == 0 else f"lotag {lo} ferme non gere -> volume plein"
        row = base_row(si, "volume_plein" if lo == 0 else "inconnu")
        row.update(regle="R-0 : secteur supprime ; les murs rouges des voisins vers lui deviennent pleins", apres="supprime")
        rows.append(row)
        cat[si] = "volume_plein"
    for r in rows:
        print(f"    {r['build_sector']:4d} lotag {r['lotag']:3d} {'ferme' if r['ferme'] else 'ouvert':6s} "
              f"{r['classe']:24s} fz/cz {r['avant']} -> {r['apres']} | {r['regle']}")
        if r["sprites"]:
            print(f"         sprites {r['sprites']}")
    solid = [si for si, why in deleted.items() if "volume plein" in why]
    sg = components(len(solid), [(i, solid.index(W[w].nextsector)) for i, si in enumerate(solid)
                                 for w in nb_walls(si) if W[w].nextsector in solid])
    solid_groups = [sorted(solid[i] for i in g) for g in sg]
    print(f"    volumes pleins supprimes : {len(solid)} en {len(solid_groups)} grappes {solid_groups}")

    # ---------------------------------------------------------------- graphe mutable des secteurs vivants
    live = [si for si in main if si not in deleted]
    liveset = set(live)
    obj, G = {}, {}
    for si in live:
        Ls = []
        for li, L in enumerate(loops[si]):
            if (si, li) in removed_loops:
                continue
            drop = removed_chains.get((si, li), set())
            LL = []
            for w in L:
                if w in drop:
                    continue
                o = MW(W[w].x, W[w].y, si, {k: getattr(W[w], k) for k in WALL_ATTRS}, w)
                obj[w] = o
                LL.append(o)
            for o in LL:
                o.loop = LL
            Ls.append(LL)
        G[si] = Ls
    dangling = 0
    for w, o in obj.items():
        a = W[w]
        if a.nextsector < 0:
            continue
        if a.nextsector in liveset and a.nextwall in obj:
            o.twin = obj[a.nextwall]
        else:
            dangling += a.nextsector in liveset
            o.solid_from = a.nextsector
            o.prov.append("rouge->plein (" + deleted.get(a.nextsector, "?").split(" ")[0] + ")")

    def split(w, P):
        """Coupe w (debut -> fin) en P : w = debut -> P, retourne le nouveau mur P -> fin."""
        L = w.loop; k = L.index(w)
        n = MW(P[0], P[1], w.sec, dict(w.a), w.bid)
        if w.orig_seg is None:
            w.orig_seg = seg0(w.bid)
        n.orig_seg, n.tex_from, n.solid_from = w.orig_seg, w.tex_from, w.solid_from
        n.prov = list(w.prov) + ["coupe_T"]
        w.prov.append("coupe_T")
        n.loop = L
        L.insert(k + 1, n)
        return n

    def solid_seen(w, src):
        """w devient plein ; il montre la texture du mur plein src qui etait vu juste derriere."""
        w.twin = None
        w.prov.append("pic->plein")
        if src is not None:
            for k in ("picnum", "shade", "pal", "xrepeat", "yrepeat", "xpanning", "ypanning"):
                w.a[k] = src.a[k]
            w.a["cstat"] = (w.a["cstat"] & ~(4 | 8 | 256)) | (src.a["cstat"] & (4 | 8 | 256))
            w.tex_from = src.tex_from if src.tex_from is not None else src.bid

    # R-25 : panneaux de portes coulissantes replies -> enveloppe convexe
    r25_rows = []
    for si in [s for s in live if sec[s]["lotag"] == 25]:
        row = base_row(si, "porte_coulissante_SE15")
        Ls = G[si]
        why = None
        if len(Ls) != 1:
            why = "plusieurs boucles"
        elif not loop_self_defects(pts_of(Ls[0])):
            why = "boucle deja simple : laissee"
        if why is None:
            L = Ls[0]
            H = convex_hull(pts_of(L))
            portals = [w for w in L if w.twin is not None]
            solids = [w for w in L if w.twin is None]
            src = max(solids, key=lambda w: (w.x - nxt(w).x) ** 2 + (w.y - nxt(w).y) ** 2) if solids else None
            ends = {w: (nxt(w).x, nxt(w).y) for w in L}
            newL, placed = [], set()
            for a in range(len(H)):
                Ha, Hb = H[a], H[(a + 1) % len(H)]
                dx, dy = Hb[0] - Ha[0], Hb[1] - Ha[1]
                on = []
                for w in portals:
                    S0, E0 = (w.x, w.y), ends[w]
                    if orient(*Ha, *Hb, *S0) == 0 and orient(*Ha, *Hb, *E0) == 0 and \
                            on_seg(*Ha, *Hb, *S0) and on_seg(*Ha, *Hb, *E0):
                        if (E0[0] - S0[0]) * dx + (E0[1] - S0[1]) * dy <= 0:
                            why = "portail a contresens sur l'enveloppe"
                        on.append(((S0[0] - Ha[0]) * dx + (S0[1] - Ha[1]) * dy, w))
                on.sort(key=lambda t: t[0])
                cur = Ha
                for _, w in on:
                    if (w.x - Ha[0]) * dx + (w.y - Ha[1]) * dy < (cur[0] - Ha[0]) * dx + (cur[1] - Ha[1]) * dy:
                        why = "portails qui se recouvrent"
                    if (w.x, w.y) != cur:
                        nw_ = MW(cur[0], cur[1], si, dict(src.a) if src else dict(w.a), None)
                        nw_.prov.append("R-25 bord neuf"); nw_.tex_from = src.bid if src else None
                        newL.append(nw_)
                    newL.append(w); placed.add(w); cur = ends[w]
                if cur != Hb:
                    nw_ = MW(cur[0], cur[1], si, dict(src.a) if src else {k: 0 for k in WALL_ATTRS}, None)
                    nw_.prov.append("R-25 bord neuf"); nw_.tex_from = src.bid if src else None
                    newL.append(nw_)
            if why is None and len(placed) != len(portals):
                why = "portail hors de l'enveloppe"
            if why is None:
                for sj, Ls2 in G.items():
                    if sj == si or why:
                        continue
                    for L2 in Ls2:
                        for w in L2:
                            if point_in_loop(w.x, w.y, H) == 1:
                                why = f"sommet de {sj} dans l'enveloppe"; break
                            e = nxt(w)
                            if any(seg_inter(H[a], H[(a + 1) % len(H)], (w.x, w.y), (e.x, e.y)) == "cross"
                                   for a in range(len(H))):
                                why = f"mur de {sj} traverse l'enveloppe"; break
                        if why:
                            break
            if why is None:
                for w in newL:
                    w.loop = newL
                G[si] = [newL]
                treatments[si].append(f"R-25 ouverte (enveloppe {len(H)} sommets, {len(solids)} murs pleins retires, "
                                      f"{sum(1 for w in newL if w.bid is None)} murs neufs)")
                cat[si] = "coulissante"
                row.update(regle="R-25 : panneau replie -> enveloppe convexe (portails gardes, bords neufs pleins)",
                           enveloppe=H, apres=[sec[si]["floorz"], sec[si]["ceilingz"]])
        if why is not None:
            row.update(regle=f"R-25 non applique : {why}", apres=row["avant"])
        r25_rows.append(row)
        rows.append(row)
        print(f"    {si:4d} lotag  25 {'ouvert':6s} porte_coulissante_SE15   | {row['regle']}")

    # T-pic : pics de largeur nulle
    def collapse(L, k):
        n = len(L)
        e1, e2, e3 = L[k], L[(k + 1) % n], L[(k + 2) % n]
        P0, P1, P2 = (e1.x, e1.y), (e2.x, e2.y), (e3.x, e3.y)
        l1 = (P1[0] - P0[0]) ** 2 + (P1[1] - P0[1]) ** 2
        l2 = (P2[0] - P1[0]) ** 2 + (P2[1] - P1[1]) ** 2
        t1, t2 = e1.twin, e2.twin
        if t1 is e2 or t2 is e1:          # repli d'un mur sur son propre jumeau
            L.remove(e1); L.remove(e2)
            return
        if l2 < l1:                        # P2 strictement dans [P0, P1] : e1 -> P0..P2, e2 disparait
            if t1 is not None:
                t1b = split(t1, P2)        # t1 : P1 -> P2 ; t1b : P2 -> P0
                t1b.twin, e1.twin = e1, t1b
                if t2 is not None:
                    t1.twin, t2.twin = t2, t1
                else:
                    solid_seen(t1, e2)
            elif t2 is not None:
                solid_seen(t2, e1)
            if e1.orig_seg is None:
                e1.orig_seg = seg0(e1.bid)
            e1.prov.append("raccourci_pic")
            L.remove(e2)
        elif l2 == l1:                     # P2 == P0 : les deux disparaissent
            if t1 is not None and t2 is not None:
                t1.twin, t2.twin = t2, t1
            elif t1 is not None:
                solid_seen(t1, e2)
            elif t2 is not None:
                solid_seen(t2, e1)
            L.remove(e1); L.remove(e2)
        else:                              # P0 strictement dans [P1, P2] : e1 disparait, e2 -> P0..P2
            if t2 is not None:
                t2b = split(t2, P0)        # t2 : P2 -> P0 ; t2b : P0 -> P1
                if t1 is not None:
                    t2b.twin, t1.twin = t1, t2b
                else:
                    solid_seen(t2b, e1)
            elif t1 is not None:
                solid_seen(t1, e2)
            if e2.orig_seg is None:
                e2.orig_seg = seg0(e2.bid)
            e2.x, e2.y = P0
            e2.prov.append("raccourci_pic")
            L.remove(e1)

    spikes = Counter()
    dropped_loops = []
    again = True
    while again:
        again = False
        for si in list(G):
            for L in G[si]:
                n = len(L)
                if n < 3:
                    continue
                for k in range(n):
                    a, b, c = L[k], L[(k + 1) % n], L[(k + 2) % n]
                    if orient(a.x, a.y, b.x, b.y, c.x, c.y) == 0 and \
                            (b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y) < 0:
                        collapse(L, k)
                        spikes[si] += 1
                        again = True
                        break
                if again:
                    break
            if again:
                break
        for si in list(G):
            for L in list(G[si]):
                if len(L) < 3 or area2(pts_of(L)) == 0:
                    for w in L:
                        if w.twin is not None:
                            solid_seen(w.twin, None)
                    G[si].remove(L)
                    dropped_loops.append(si)
                    again = True
    for si, n in spikes.items():
        treatments[si].append(f"T-pic {n} pic(s) replie(s)")

    # T-pin : boucles pincees
    pinches = Counter()
    again = True
    while again:
        again = False
        for si in G:
            for L in G[si]:
                seen = {}
                for i, w in enumerate(L):
                    p = (w.x, w.y)
                    if p in seen:
                        j = seen[p]
                        A, B = L[j:i], L[i:] + L[:j]
                        G[si].remove(L)
                        for part in (A, B):
                            for o in part:
                                o.loop = part
                            G[si].append(part)
                        pinches[si] += 1
                        again = True
                        break
                    seen[p] = i
                if again:
                    break
            if again:
                break
    for si, n in pinches.items():
        treatments[si].append(f"T-pin boucle scindee {n} fois")
    print(f"\n    nettoyage topologique : pics replies {sum(spikes.values())} dans {len(spikes)} secteurs "
          f"{dict(spikes)} ; boucles degenerees retirees {len(dropped_loops)} ; boucles pincees scindees "
          f"{sum(pinches.values())} dans {dict(pinches)} ; murs rouges sans jumeau vivant (anomalie) {dangling}")

    # composantes apres traitement
    idx = {si: k for k, si in enumerate(live)}
    post = components(len(live), [(idx[si], idx[w.twin.sec]) for si in live for L in G[si] for w in L
                                  if w.twin is not None])
    post_removed = []
    for c in post[1:]:
        for k in c:
            post_removed.append(live[k])
            deleted[live[k]] = "isole apres traitement"
            cat[live[k]] = "isole"
    live = [si for si in live if si not in deleted]
    print(f"    composantes apres traitement : {len(post)} (secteurs isoles retires : {post_removed})")

    # ---------------------------------------------------------------- serialisation
    newsec = {si: k for k, si in enumerate(live)}
    order, sec_loops = [], {}
    for si in live:
        sl = []
        for L in G[si]:
            ids = []
            for w in L:
                ids.append(len(order)); order.append(w)
            sl.append(ids)
        sec_loops[si] = sl
    wid = {id(w): k for k, w in enumerate(order)}
    walls_out = []
    for k, w in enumerate(order):
        e = nxt(w)
        rec = {"id": k, "build_id": w.bid, "sector": newsec[w.sec], "x": w.x, "y": w.y, "point2": wid[id(e)],
               "nextwall": wid[id(w.twin)] if w.twin is not None else -1,
               "nextsector": newsec[w.twin.sec] if w.twin is not None else -1}
        rec.update(w.a)
        rec["solidified_from"] = w.solid_from
        if w.solid_from is not None:
            v = S[w.solid_from]      # le volume supprime, tel que dans la carte : Duke ne dessine ce mur que
            rec["solidified_info"] = {"floorz": v.floorz, "ceilingz": v.ceilingz,   # jusqu'a v.floorz si le
                                      "floorstat": v.floorstat, "ceilingstat": v.ceilingstat,  # volume a un
                                      "ciel_au_dessus": bool(v.ceilingstat & 1)}   # plafond de ciel
        rec["prov"] = w.prov
        rec["tex_from"] = w.tex_from
        rec["orig_seg"] = w.orig_seg
        walls_out.append(rec)
    n_solidified = Counter(p.split(" ")[0] for w in order for p in w.prov if "->plein" in p)

    sectors_out = []
    for si in live:
        d = sec[si]
        ids = [w for L in sec_loops[si] for w in L]
        rec = {"id": newsec[si], "build_id": si, "wallptr": min(ids), "wallnum": len(ids), "loops": sec_loops[si]}
        rec.update({k: v for k, v in d.items() if k not in ("wallptr", "wallnum")})
        f0 = walls_out[min(ids)]; f1 = walls_out[f0["point2"]]
        rec["slope_ref"] = list(slope_ref[si])
        rec["slope_ref_is_first_wall"] = (f0["x"], f0["y"], f1["x"], f1["y"]) == slope_ref[si]
        rec["area2_build"] = sum(area2([(walls_out[w]["x"], walls_out[w]["y"]) for w in L]) for L in sec_loops[si])
        rec["treatment"] = treatments.get(si, [])
        rec["saturn"] = {"floorY": -d["floorz"] / 128, "ceilY": -d["ceilingz"] / 128}
        sectors_out.append(rec)
    by_bid = {s["build_id"]: s for s in sectors_out}

    sprites_out = []
    for j, p in enumerate(SP):
        if p.sectnum in main_set:
            r = {"build_id": j, "sector": newsec.get(p.sectnum), "build_sector": p.sectnum}
            r.update(asdict(p))
            r.pop("sectnum")
            sprites_out.append(r)
    print(f"    sortie : {len(sectors_out)} secteurs, {len(walls_out)} murs, {len(sprites_out)} sprites de la "
          f"composante gardee (dont {sum(1 for r in sprites_out if r['sector'] is None)} dans des secteurs supprimes, "
          f"sector=null) ; murs devenus pleins {dict(n_solidified)} ; murs neufs R-25 "
          f"{sum(1 for w in order if w.bid is None)} ; murs coupes (T) {sum(1 for w in order if 'coupe_T' in w.prov)}")

    # ---------------------------------------------------------------- 4. depart
    print("\n[4] depart M1 (teleporteur SE7 du toit)")
    se7_rows, start = [], None
    for j in [j for j, p in enumerate(SP) if p.picnum == SECTOREFFECTOR and p.lotag == 7]:
        p = SP[j]; ow = se_owner(SP, j)
        onfloorz = S[p.sectnum].floorz == p.z
        se7_rows.append({"sprite": j, "sector": p.sectnum, "composante": comp_of[p.sectnum], "xyz": [p.x, p.y, p.z],
                         "ang": p.ang, "hitag": p.hitag, "OW": ow, "onfloorz": onfloorz})
        print(f"    SE7 spr {j} secteur {p.sectnum} comp {comp_of[p.sectnum]} ({p.x},{p.y},{p.z}) ang {p.ang} "
              f"hitag {p.hitag} -> OW spr {ow} ; onfloorz {onfloorz} (sector.floorz {S[p.sectnum].floorz})")
    for r in [r for r in se7_rows if r["composante"] == roof_ci]:
        ow = r["OW"]
        if ow is None or SP[ow].sectnum not in by_bid:
            continue
        q = SP[ow]; si = q.sectnum; srec = by_bid[si]
        inl = [point_in_loop(q.x, q.y, [(walls_out[w]["x"], walls_out[w]["y"]) for w in L]) for L in srec["loops"]]
        fz = fz_at(si, q.x, q.y); cz = cz_at(si, q.x, q.y)
        segs = [((walls_out[w]["x"], walls_out[w]["y"]),
                 (walls_out[walls_out[w]["point2"]]["x"], walls_out[walls_out[w]["point2"]]["y"]),
                 walls_out[w]["nextsector"]) for L in srec["loops"] for w in L]
        dmin = min(dist_point_seg(q.x, q.y, a, b) for a, b, _ in segs)
        dsol = min((dist_point_seg(q.x, q.y, a, b) for a, b, n in segs if n < 0), default=None)
        start = {"se7_source": r["sprite"], "se7_arrivee": ow, "build_sector": si, "sector": srec["id"],
                 "build_xy": [q.x, q.y], "se7_z": q.z, "arrivee_oeil_z_duke": q.z + 6144,
                 "floorz_au_point": fz, "ceilingz_au_point": cz,
                 "saturn": {"X": q.x / 8, "Z": -q.y / 8, "Y_sol": -fz / 128, "Y_joueur_PS_hyp": -fz / 128 + 48},
                 "ang_build": m.ang, "ang_se7_arrivee": q.ang,
                 "direction_saturn_XZ": [round(math.cos(m.ang * 2 * math.pi / 2048), 6),
                                         round(-math.sin(m.ang * 2 * math.pi / 2048), 6)],
                 "point_dans_boucles": inl,
                 "distance_mur_le_plus_proche_u": round(dmin / 8, 2),
                 "distance_mur_plein_le_plus_proche_u": None if dsol is None else round(dsol / 8, 2),
                 "chute_u": round((fz - (q.z + 6144)) / 128, 2)}
        print(f"    depart = arrivee du SE7 {r['sprite']} (toit) -> SE7 {ow} : secteur Build {si} -> id {srec['id']} ; "
              f"Build ({q.x},{q.y}) sol z {fz} (plafond {cz}) ; Saturn X {q.x / 8} Z {-q.y / 8} Y_sol {-fz / 128} "
              f"(joueur PS hyp. sol+48 = {-fz / 128 + 48}) ; ang Build {m.ang} (= {m.ang * 360 / 2048:.2f} deg ; "
              f"SE7 d'arrivee ang {q.ang}, non lu par Duke) ; point dans boucles {inl} ; mur le plus proche "
              f"{dmin / 8:.1f} u, mur plein le plus proche {'-' if dsol is None else f'{dsol / 8:.1f}'} u ; Duke fait "
              f"arriver l'oeil a z {q.z + 6144} puis chute de {(fz - (q.z + 6144)) / 128:.1f} u")

    # ---------------------------------------------------------------- criteres
    print("\n[criteres d'acceptation]")
    crit = {}
    P = lambda w: (walls_out[w]["x"], walls_out[w]["y"])  # noqa: E731
    fin = components(len(sectors_out), [(w["sector"], w["nextsector"]) for w in walls_out if w["nextsector"] >= 0])
    crit["une_seule_composante"] = {"ok": len(fin) == 1, "valeur": len(fin)}
    bad_flat = [s["build_id"] for s in sectors_out if s["floorz"] - s["ceilingz"] <= 0]
    bad_vert = []
    for s in sectors_out:
        si = s["build_id"]
        hmin = min(fz_at(si, *P(w)) - cz_at(si, *P(w)) for L in s["loops"] for w in L)
        if hmin <= 0:
            bad_vert.append((si, hmin))
    crit["zero_hauteur_le_0"] = {"ok": not bad_flat and not bad_vert, "valeur": len(bad_flat) + len(bad_vert),
                                 "secteurs": bad_flat + [b[0] for b in bad_vert],
                                 "note": "hauteur au point de reference ET a chaque sommet (pentes getzsofslope)"}
    defects = []
    for s in sectors_out:
        for li, L in enumerate(s["loops"]):
            dd = loop_self_defects([P(w) for w in L])
            if dd:
                defects.append({"build_sector": s["build_id"], "boucle": li, "n": len(dd),
                                "types": dict(Counter(t for t, _, _ in dd))})
    crit["zero_boucle_auto_intersectee"] = {"ok": not defects, "valeur": len(defects), "detail": defects,
                                            "avant_nettoyage_dans_la_carte": sum(1 for v in orig_defects.values() if v),
                                            "note": "croisement, contact entre aretes non adjacentes, pic, arete nulle"}
    inter_loop = []
    for s in sectors_out:
        Ls = s["loops"]
        if len(Ls) < 2:
            continue
        segs = [[(P(w), P(walls_out[w]["point2"])) for w in L] for L in Ls]
        for a in range(len(Ls)):
            for b in range(a + 1, len(Ls)):
                hits = Counter()
                for e1 in segs[a]:
                    for e2 in segs[b]:
                        r = seg_inter(*e1, *e2)
                        if r:
                            hits[r] += 1
                if hits:
                    inter_loop.append({"build_sector": s["build_id"], "boucles": [a, b], **dict(hits)})
    crit["info_contacts_entre_boucles_meme_secteur"] = {
        "valeur": len(inter_loop), "detail": inter_loop,
        "croisements": sum(x.get("cross", 0) for x in inter_loop)}
    orient_bad, cd_disagree, outer_not_first, multi_outer = [], 0, 0, []
    n_loops = n_outer = n_inner = 0
    for s in sectors_out:
        info = []
        for L in s["loops"]:
            Pl = [P(w) for w in L]
            a2 = area2(Pl); cd = clockdir(Pl)
            cd_disagree += (cd == 0) != (a2 > 0)
            info.append((a2, Pl))
        n_loops += len(info)
        outers = [k for k, (a2, _) in enumerate(info) if a2 > 0]
        n_outer += len(outers); n_inner += len(info) - len(outers)
        if len(outers) != 1:
            multi_outer.append((s["build_id"], len(outers)))
            orient_bad.append(s["build_id"])
            continue
        outer_not_first += outers[0] != 0
        Po = info[outers[0]][1]
        if any(point_in_loop(x, y, Po) == 0 for k, (_, Pl) in enumerate(info) if k != outers[0] for x, y in Pl):
            orient_bad.append(s["build_id"])
    conv = ("Build (y vers le bas) : boucle exterieure CW au sens clockdir()==0 <=> somme de lacets "
            "sum(x_i*y_{i+1}-x_{i+1}*y_i) > 0 ; trous CCW (< 0), sommets dans l'exterieure ; en repere Saturn "
            "(X=x/8, Z=-y/8) le signe s'inverse : exterieure sum(X_i*Z_{i+1}-X_{i+1}*Z_i) < 0")
    crit["orientation_coherente"] = {
        "ok": not orient_bad and cd_disagree == 0, "valeur": len(orient_bad), "convention": conv,
        "boucles": n_loops, "exterieures": n_outer, "trous": n_inner,
        "desaccords_clockdir_vs_lacets": cd_disagree, "secteurs_sans_exactement_1_exterieure": multi_outer,
        "trous_hors_exterieure": [b for b in orient_bad if b not in [x[0] for x in multi_outer]],
        "info_exterieure_pas_en_premier": outer_not_first}
    asym = n_red = 0
    for w in walls_out:
        if w["nextwall"] < 0:
            continue
        n_red += 1
        t = walls_out[w["nextwall"]]
        asym += not (t["nextwall"] == w["id"] and t["nextsector"] == w["sector"] and w["nextsector"] == t["sector"]
                     and P(t["id"]) == P(w["point2"]) and P(t["point2"]) == P(w["id"]))
    crit["symetrie_murs_rouges"] = {"ok": asym == 0, "valeur": f"{n_red - asym}/{n_red}",
                                    "pourcent": 100.0 * (n_red - asym) / n_red if n_red else 100.0}
    crit["secteurs_vivants"] = {"ok": True, "valeur": len(sectors_out)}
    crit["composante_gardee_293"] = {"ok": len(main) == EXPECT_MAIN, "valeur": len(main)}
    crit["toit_18_retire"] = {"ok": len(comps[roof_ci]) == EXPECT_ROOF and roof_ci != main_ci,
                              "valeur": len(comps[roof_ci])}
    crit["depart_dans_secteur_vivant"] = {
        "ok": start is not None and start["point_dans_boucles"].count(1) == 1 and 2 not in start["point_dans_boucles"],
        "valeur": None if start is None else start["point_dans_boucles"]}

    def ymax_ceil(s):
        return max(-cz_at(s["build_id"], *P(w)) / 128 for L in s["loops"] for w in L)
    sky_left = [s["build_id"] for s in sectors_out if s["ceilingstat"] & 1 and ymax_ceil(s) > SKY_CAP_Y]
    crit["ciel_Y_le_1760"] = {"ok": not sky_left, "valeur": len(sky_left), "secteurs": sky_left}
    over = [(s["build_id"], round(ymax_ceil(s), 2)) for s in sectors_out
            if not s["ceilingstat"] & 1 and ymax_ceil(s) > SKY_CAP_Y]
    crit["info_plafonds_non_ciel_Y_gt_1760"] = {"valeur": len(over), "secteurs": over}
    slope_moved = [s["build_id"] for s in sectors_out if (s["ceilingstat"] | s["floorstat"]) & 2
                   and not s["slope_ref_is_first_wall"]]
    crit["info_pente_reference_deplacee"] = {"valeur": len(slope_moved), "secteurs": slope_moved,
                                             "note": "utiliser slope_ref (coordonnees Build) et non le 1er mur"}
    allY = [(-fz_at(s["build_id"], *P(w)) / 128, -cz_at(s["build_id"], *P(w)) / 128)
            for s in sectors_out for L in s["loops"] for w in L]
    bbox = {"X": [min(w["x"] for w in walls_out) / 8, max(w["x"] for w in walls_out) / 8],
            "Z": [-max(w["y"] for w in walls_out) / 8, -min(w["y"] for w in walls_out) / 8],
            "Y": [min(min(a, b) for a, b in allY), max(max(a, b) for a, b in allY)]}
    for k, v in crit.items():
        tag = ("OK   " if v["ok"] else "ECHEC") if "ok" in v else "info "
        extra = ""
        if k == "orientation_coherente":
            extra = (f" ({v['boucles']} boucles = {v['exterieures']} ext + {v['trous']} trous ; desaccords clockdir/lacets "
                     f"{v['desaccords_clockdir_vs_lacets']} ; ext. pas en 1er {v['info_exterieure_pas_en_premier']} ; "
                     f"secteurs sans 1 ext. {v['secteurs_sans_exactement_1_exterieure']})")
        elif k == "zero_boucle_auto_intersectee":
            extra = f" (dans la carte avant nettoyage : {v['avant_nettoyage_dans_la_carte']}) {v['detail'][:8]}"
        elif "secteurs" in v and v["valeur"]:
            extra = f" {v['secteurs']}"
        elif "detail" in v and v["valeur"]:
            extra = f" {v['detail'][:6]}"
        print(f"    {tag} {k:45s} : {v['valeur']}{extra}")
    print(f"    info  bbox Saturn : X {bbox['X']} Z {bbox['Z']} Y {bbox['Y']} ; anomalies point2 dans la carte : "
          f"{loop_anom}")

    special = []
    for s in sectors_out:
        if s["lotag"] != 0 and not s["treatment"]:
            special.append({"build_sector": s["build_id"], "lotag": s["lotag"], "hitag": s["hitag"],
                            "hauteur_u": (s["floorz"] - s["ceilingz"]) / 128, "sprites": se_summary(s["build_id"])})
    print(f"\n[info] secteurs a lotag != 0 laisses dans l'etat initial : {len(special)} ; lotags "
          f"{sorted(Counter(x['lotag'] for x in special).items())}")
    for x in special:
        print(f"    {x['build_sector']:4d} lotag {x['lotag']:5d} hauteur {x['hauteur_u']:7.1f} u sprites {x['sprites']}")

    out = {
        "format": "duke2ps/e1-import v2",
        "source": {"map": os.path.relpath(args.map, ROOT).replace("\\", "/"), "sha1": sha1,
                   "version": m.version, "secteurs": NS, "murs": len(W), "sprites": len(SP),
                   "depart_build": {"xyz": [m.posx, m.posy, m.posz], "ang": m.ang, "sector": m.cursectnum}},
        "options": {"cap_all_ceilings": args.cap_all_ceilings},
        "repere": {"X": "x/8", "Z": "-y/8", "Y": "-z/128", "decalage": 0, "sky_cap_Y": SKY_CAP_Y, "sky_cap_z": SKY_CAP_Z},
        "conventions": {
            "ids": "sectors[].id / walls[].id = indices apres nettoyage (contigus, murs groupes par secteur et par "
                   "boucle) ; build_id = indice dans la carte d'origine (null = mur neuf R-25)",
            "loops": "sectors[].loops = listes d'ids de murs ; mur k va de (x,y) a (x,y) de son point2",
            "orientation": conv,
            "slope_ref": "pente = getzsofslope avec le 1er mur du secteur D'ORIGINE (x0,y0,x1,y1 Build)",
            "area2_build": "somme des lacets de toutes les boucles (exterieure > 0, trous < 0) = 2 x aire Build",
            "solidified_from": "mur rouge dont le voisin a ete supprime : build_id du voisin ; cstat/picnum d'origine",
            "prov": "historique : coupe_T (coupe a une jonction en T), raccourci_pic, pic->plein, rouge->plein, "
                    "R-25 bord neuf",
            "tex_from": "build_id du mur dont la texture est montree (mur plein vu derriere un pic replie)",
            "orig_seg": "segment Build d'origine (x0,y0,x1,y1) d'un mur coupe ou raccourci : reference pour le "
                        "placage (xrepeat/xpanning portent sur le mur d'origine entier)",
            "sprites": "tous les sprites de la composante gardee ; sector = null si leur secteur a ete supprime"},
        "retrait_composantes": removed,
        "regles_appliquees": rows,
        "volumes_pleins_grappes": solid_groups,
        "secteurs_supprimes": {str(k): v for k, v in sorted(deleted.items())},
        "isoles_apres_traitement": post_removed,
        "nettoyage_topologique": {"pics": dict(spikes), "boucles_degenerees_retirees": dropped_loops,
                                  "pincements": dict(pinches)},
        "ciel_plafonne": sky_rows,
        "se7": se7_rows,
        "depart_m1": start,
        "speciaux_non_traites": special,
        "bbox_saturn": bbox,
        "criteres": crit,
        "sectors": sectors_out,
        "walls": walls_out,
        "sprites": sprites_out,
    }
    atomic_write(args.out, dump_json(out))
    print(f"\n[E1] ecrit {os.path.relpath(args.out, ROOT)} ({os.path.getsize(args.out)} o)")
    if not args.no_png:
        for ci, c in enumerate(comps):
            for si in c:
                if ci == roof_ci:
                    cat[si] = "toit"
                elif ci != main_ci:
                    cat[si] = "ilot"
        try:
            draw_png(args.png, m, loops, cat, start, se7_rows, main_set)
            print(f"[E1] ecrit {os.path.relpath(args.png, ROOT)}")
        except ImportError as e:
            print(f"[E1] PNG saute ({e})")
    fails = [k for k, v in crit.items() if "ok" in v and not v["ok"]]
    print(f"\n[E1] {'TOUS LES CRITERES OK' if not fails else 'ECHEC : ' + ', '.join(fails)} ; secteurs vivants "
          f"{len(sectors_out)}")
    return 0 if not fails else 1


# ------------------------------------------------------------------------------------------
COLORS = {"garde": (215, 215, 215), "ciel": (185, 215, 245), "porte20": (40, 90, 230), "air": (40, 200, 200),
          "ile_supprimee": (210, 40, 210), "volume_plein": (0, 0, 0), "coulissante": (255, 140, 0),
          "toit": (240, 150, 150), "ilot": (250, 200, 90), "isole": (120, 60, 0)}
LEGEND = [("garde", "garde"), ("ciel", "garde, ciel plafonne"), ("porte20", "R-20 porte ouverte"),
          ("air", "R-23b/R-27b -> air"), ("ile_supprimee", "R-23a/R-27a vantail/rideau supprime"),
          ("volume_plein", "R-0 volume plein supprime"), ("coulissante", "R-25 porte coulissante ouverte"),
          ("toit", "toit retire (M1b)"), ("ilot", "ilot inaccessible retire")]


def draw_png(path, m, loops, cat, start, se7_rows, view):
    """Vue de dessus cadree sur la composante gardee (le toit et les ilots retires sont hors cadre)."""
    from PIL import Image, ImageDraw
    S, W = m.sectors, m.walls
    xs = [W[w].x for si in view for L in loops[si] for w in L]
    ys = [W[w].y for si in view for L in loops[si] for w in L]
    x0, x1, y0, y1 = min(xs) - 256, max(xs) + 256, min(ys) - 256, max(ys) + 256
    Wpx = 2000
    sc = (Wpx - 40) / (x1 - x0)
    Hpx = int((y1 - y0) * sc) + 140
    img = Image.new("RGB", (Wpx, Hpx), (255, 255, 255))
    dr = ImageDraw.Draw(img)

    def P(x, y):
        return (20 + (x - x0) * sc, 20 + (y - y0) * sc)

    def area(si):
        return max(abs(area2([(W[w].x, W[w].y) for w in L])) for L in loops[si])
    for si in sorted(range(len(S)), key=lambda s: -area(s)):
        outer = max(loops[si], key=lambda L: abs(area2([(W[w].x, W[w].y) for w in L])))
        dr.polygon([P(W[w].x, W[w].y) for w in outer], fill=COLORS[cat.get(si, "garde")])
    for w in W:
        b = W[w.point2]
        dr.line([P(w.x, w.y), P(b.x, b.y)], fill=(0, 0, 0) if w.nextsector < 0 else (150, 150, 150), width=1)
    for r in se7_rows:
        x, y = P(*r["xyz"][:2])
        dr.ellipse([x - 6, y - 6, x + 6, y + 6], outline=(200, 0, 0), width=2)
    if start:
        x, y = P(*start["build_xy"])
        dr.line([x - 12, y, x + 12, y], fill=(0, 150, 0), width=3)
        dr.line([x, y - 12, x, y + 12], fill=(0, 150, 0), width=3)
        a = start["ang_build"] * 2 * math.pi / 2048
        dr.line([x, y, x + 30 * math.cos(a), y + 30 * math.sin(a)], fill=(0, 150, 0), width=3)
    yy = Hpx - 110
    for k, (c, t) in enumerate(LEGEND + [(None, "cercle rouge = SE7 ; croix verte = depart M1 (trait = ang)")]):
        xx = 20 + (k % 4) * 490
        yk = yy + (k // 4) * 32
        if c:
            dr.rectangle([xx, yk, xx + 22, yk + 22], fill=COLORS[c], outline=(0, 0, 0))
        dr.text((xx + 30, yk + 5), t, fill=(0, 0, 0))
    dr.text((20, 2), f"E1L1 import E1 - vue Build (y vers le bas = -Z Saturn), {sc * 8:.3f} px par u Saturn",
            fill=(0, 0, 0))
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    atomic_write(path, buf.getvalue())


if __name__ == "__main__":
    sys.exit(main())

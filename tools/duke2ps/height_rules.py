"""height_rules.py -- regles generales de hauteur (ciels, plafonds hauts, pieces perchees) pour le convertisseur
Duke Nukem 3D PC (Build .MAP v7) -> SlaveDriver (PowerSlave Saturn).

Fonctions PURES : un `Model` (carte en repere Saturn) entre, un dict {secteur: Decision} sort, avec la raison de
chaque changement.  Rien n'est lu ni ecrit ici, sauf par les deux adaptateurs (`model_from_build`,
`model_from_import`) qui ne font que traduire des objets deja charges.  Branchement prevu : build_import.py
appellera `apply_rules(model_from_import(json))` a la place de son plafonnement fixe a Y 1760.

Regles (principes, detail et calibration dans tools/duke2ps/NOTES_HEIGHTS.md) :
  H1 ciel      -- un plafond de ciel n'est pas de la geometrie : il fixe la hauteur des facades qui bordent le
                  ciel.  Une facade doit depasser le haut de l'ecran depuis tout point ou le joueur se tient et
                  la voit : oeil + d * tan(demi-champ haut), arrondi a 128.  Une region de ciel (secteurs ciel
                  relies entre eux) recoit UNE hauteur.
  H2 air mort  -- l'espace qu'aucun joueur ne peut ni atteindre ni voir est retire : un plafond jamais a l'ecran
                  (a tangage nul) descend jusqu'a la hauteur ou il y entrerait ; une piece perchee dont l'ouverture
                  serait coupee descend avec son puits (translation rigide : hauteurs et ouvertures gardees).
  H3 ne jamais couper -- aucun sol, aucune ouverture, aucun sprite au-dessus d'un plafond abaisse ; sinon la
                  regle recule (on remonte) et le signale.

Repere Duke PC -> Saturn : X = x/8, Z = -y/8, Y = -z/128, sans decalage (docs/RETAIL_DISCS.md s6).
"""
from __future__ import annotations

import math
from collections import defaultdict, deque
from dataclasses import dataclass, field

import numpy as np

# ----------------------------------------------------------------------------------------------------------
# Constantes du moteur SlaveDriver (SOURCE = fichier:ligne du depot)
# ----------------------------------------------------------------------------------------------------------
CELL = 128                    # pavage des faces en cellules de 128 u (decision du proprietaire)
RADIUS = 47                   # AI.C:46 constructPlayer -> newSprite(sector, F(47), ...)
CAM_LIFT = 8                  # SPRITE.C:643-644 : pour la camera, floorDistance += F(8)
EYE = RADIUS + CAM_LIFT       # 55 : SPRITE.C:642-655 pose la camera a sol + rayon + 8 ; la vue est la camera
                              # (SRUINS.C:1998 camera = player->sprite ; SRUINS.C:2097-2100 MoveMatrix(-pos.y +
                              # playerHeightOffset), offset nul au repos SRUINS.C:229-233)
FOCAL = 160                   # wallasm_gnu.s:40-45 project_point : dividende 160<<32 / z -> x*160/z ; WALLS.H:4
Y_TOP = 110                   # WALLS.C:129 YMIN -110 (fenetre de clip ; centre ecran y 120 : SRUINS.C:2109)
Y_BOT = 90                    # WALLS.C:131 YMAX 90
X_HALF = 160                  # WALLS.C:128/130 XMIN/XMAX -+160
TAN_UP = Y_TOP / FOCAL        # 0,6875 -> demi-champ vertical haut 34,5 deg (bas 29,4 deg, horizontal 45 deg)
TAN_DOWN = Y_BOT / FOCAL
TAN_SIDE = X_HALF / FOCAL
JUMP_VEL = 39 << 13           # SRUINS.C:101 NORMALJUMPVEL (sandales SRUINS.C:100 : objet PowerSlave, exclu)
GRAVITY = 6 << 12             # AICOMMON.H:4 ; joueur cree avec GRAVITY (AI.C:46), SRUINS.C:448
JUMP_HOLD = 3 << 12           # SRUINS.C:611 : +3<<12 par image tant que le bouton est tenu
MAXVPERWALL = 700             # WALLS.C:975 ; asserts WALLS.C:1020 (w*h), 1207/1216 (sommets d'un mur)
MAXNMSECTORS, MAXNMWALLS = 600, 5500     # UTIL.H:21-22
LEV_MAX_BYTES = 900000        # LEVEL.C:41 assert(size < 900000)
FRAME_POLY_BUDGET = 1300      # budget du slave (proprietaire), polygones par image


def jump_apex(hold: bool = True) -> float:
    """Apogee du saut, en u, simule image par image dans l'ordre du moteur : a l'appui vel.y = JUMP_VEL
    (SRUINS.C:612-617, qui ecrase le +HOLD de la meme image), puis moveCamera -> internal_moveSprite : vel -= g ;
    pos += vel (SPRITE.C:459-467, appele une fois par image d'entree SRUINS.C:866-1048) ; bouton tenu : +HOLD."""
    v, y, top = JUMP_VEL, 0, 0
    while True:
        v -= GRAVITY
        y += v
        top = max(top, y)
        if v <= 0:
            break
        if hold:
            v += JUMP_HOLD
    return top / 65536.0


JUMP = jump_apex(True)                 # 56,25 u (bouton tenu) ; 29,25 u sur un appui bref
REACH_STEP = CAM_LIFT + JUMP           # 64 : bas de la boule a l'apogee = marche franchissable en sautant
REACH_TOP = EYE + JUMP + RADIUS        # 158 : haut de la boule a l'apogee = plus haut point atteint
MIN_PASS = 32                          # ouverture minimale franchissable retenue (HYPOTHESE : M1 accepte des
                                       # passages < 94 u = 2 x rayon, DUKE_PC_TO_SATURN.md s5 decision 4)

# Duke : lotags de secteurs qui bougent (portes, ascenseurs, plates-formes) -- valeurs des cartes, lues dans les
# scripts d'import (build_import.py R-20/23/25/27) ; laisses tels quels par H2.
MOVING_LOTAGS = set(range(9, 33))
ELEVATOR_LOTAGS = {15, 16, 17, 18, 19}
# SE (sprite picnum 1) dont le lotag fait bouger le secteur -- SOURCE refs/build/jfduke3d/src/actors.c:4985 (1
# pivot), 5753 (11 porte battante), 6205 (19 boucliers), 6297 (20 pont), 6363 (21 cascade), 6527 (25 pistons),
# 6744 (31 sol), 6881 (32 plafond) ; game.c:4891-4903 (6, 11, 14, 15, 26, 30).  0, 13, 29 : HYPOTHESE (rotation,
# C-9, vague).  SE7 (teleporteur, game.c:4613) et SE17 (ascenseur de transport) ne deforment pas le secteur.
MOVING_SE = {0, 1, 6, 11, 13, 14, 15, 19, 20, 21, 25, 26, 29, 30, 31, 32}
MARKER_PICS = set(range(1, 11))        # SECTOREFFECTOR..GPSPEED : marqueurs invisibles


# ----------------------------------------------------------------------------------------------------------
# Modele
# ----------------------------------------------------------------------------------------------------------
@dataclass
class Wall:
    id: int
    sector: int
    ax: float
    az: float
    bx: float
    bz: float
    next: int = -1            # secteur voisin (-1 = mur plein)
    twin: int = -1            # mur jumeau dans le voisin
    cstat: int = 0

    @property
    def length(self) -> float:
        return math.hypot(self.bx - self.ax, self.bz - self.az)


@dataclass
class Sector:
    id: int
    walls: list
    loops: list               # [[(X, Z), ...], ...]
    floor_lo: float
    floor_hi: float
    ceil_lo: float
    ceil_hi: float
    sky: bool = False
    floor_sky: bool = False
    lotag: int = 0
    hitag: int = 0
    build_id: int | None = None
    fslope: tuple | None = None   # (z, heinum, stat, x0, y0, x1, y1) Build, pour le sol au point
    moving: bool = False          # porte / ascenseur / secteur pilote par un SE

    def bbox(self):
        xs = [p[0] for L in self.loops for p in L]
        zs = [p[1] for L in self.loops for p in L]
        return min(xs), min(zs), max(xs), max(zs)

    def contains(self, X: float, Z: float) -> bool:
        c = False
        for L in self.loops:
            n = len(L)
            for i in range(n):
                x0, z0 = L[i]
                x1, z1 = L[(i + 1) % n]
                if (z0 > Z) != (z1 > Z) and X < x0 + (Z - z0) * (x1 - x0) / (z1 - z0):
                    c = not c
        return c

    def floor_at(self, X: float, Z: float) -> float:
        if not self.fslope:
            return self.floor_lo
        z, hei, stat, x0, y0, x1, y1 = self.fslope
        return -_slope_z(z, hei, stat, x0, y0, x1, y1, X * 8.0, -Z * 8.0) / 128.0

    @property
    def area(self) -> float:
        tot = 0.0
        for L in self.loops:
            a = 0.0
            for i in range(len(L)):
                x0, z0 = L[i]
                x1, z1 = L[(i + 1) % len(L)]
                a += x0 * z1 - x1 * z0
            tot += a / 2.0
        return abs(tot)


@dataclass
class Sprite:
    X: float
    Z: float
    Ybot: float
    Ytop: float
    sector: int
    picnum: int = 0
    lotag: int = 0
    hitag: int = 0
    cstat: int = 0
    marker: bool = False


@dataclass
class Model:
    sectors: list
    walls: list
    sprites: list
    start: tuple | None = None       # (X, Z, sector)
    name: str = ''


@dataclass
class Decision:
    ceil_old: float
    ceil_new: float
    floor_old: float
    floor_new: float
    dy: float = 0.0                  # translation verticale (piece perchee)
    reasons: list = field(default_factory=list)

    @property
    def changed(self) -> bool:
        return abs(self.ceil_new - self.ceil_old) > 1e-6 or abs(self.dy) > 1e-6


# ----------------------------------------------------------------------------------------------------------
# Adaptateurs (seules fonctions qui connaissent les formats d'entree)
# ----------------------------------------------------------------------------------------------------------
def _cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def _slope_z(base, heinum, stat, x0, y0, x1, y1, x, y):
    """Hauteur Build d'un plan incline au point (x, y) ; formule de getzsofslope (refs/build/jfbuild engine.c,
    lue comme reference, reecrite)."""
    if not stat & 2:
        return base
    dx, dy = x1 - x0, y1 - y0
    i = math.isqrt(int(dx * dx + dy * dy)) << 5
    if i == 0:
        return base
    j = int(dx * (y - y0) - dy * (x - x0)) >> 3
    return base + _cdiv(int(heinum) * j, i)


def _sprite_from(x, y, z, cstat, picnum, yrepeat, sector, lotag, hitag, tiles):
    marker = picnum in MARKER_PICS or bool(cstat & 32768)
    ybot = -z / 128.0
    t = tiles.get(picnum) if tiles else None
    if t is None:
        h = 64.0                                   # HYPOTHESE sans ART : 64 u
    elif (cstat & 48) == 32:
        h = 0.0                                    # sprite a plat
    else:
        h = t[1] * yrepeat * 4 / 128.0             # Build : hauteur z = tilesizy * yrepeat << 2
    if cstat & 128 and h:                          # centre
        ybot -= h / 2
    return Sprite(x / 8.0, -y / 8.0, ybot, ybot + h, sector, picnum, lotag, hitag, cstat, marker)


def model_from_build(m, tiles=None, name='') -> Model:
    """m = tools/buildmap.py BuildMap.  tiles = {picnum: (sizx, sizy, ...)} (facultatif)."""
    S, W = m.sectors, m.walls
    se_sectors = {sp.sectnum for sp in m.sprites if sp.picnum == 1 and sp.lotag in MOVING_SE}
    walls = []
    for i, w in enumerate(W):
        b = W[w.point2]
        walls.append(Wall(i, -1, w.x / 8.0, -w.y / 8.0, b.x / 8.0, -b.y / 8.0, w.nextsector, w.nextwall, w.cstat))
    secs = []
    for si, s in enumerate(S):
        ids = list(range(s.wallptr, s.wallptr + s.wallnum))
        for k in ids:
            walls[k].sector = si
        seen, loops = set(), []
        for w in ids:
            if w in seen:
                continue
            L, k, g = [], w, 0
            while k not in seen and g < 100000:
                seen.add(k)
                L.append(k)
                k = W[k].point2
                g += 1
            loops.append([(W[q].x / 8.0, -W[q].y / 8.0) for q in L])
        w0, w1 = W[s.wallptr], W[W[s.wallptr].point2]
        fy = [-_slope_z(s.floorz, s.floorheinum, s.floorstat, w0.x, w0.y, w1.x, w1.y, W[k].x, W[k].y) / 128 for k in ids]
        cy = [-_slope_z(s.ceilingz, s.ceilingheinum, s.ceilingstat, w0.x, w0.y, w1.x, w1.y, W[k].x, W[k].y) / 128
              for k in ids]
        fsl = (s.floorz, s.floorheinum, s.floorstat, w0.x, w0.y, w1.x, w1.y) if s.floorstat & 2 else None
        secs.append(Sector(si, ids, loops, min(fy), max(fy), min(cy), max(cy), bool(s.ceilingstat & 1),
                           bool(s.floorstat & 1), s.lotag, s.hitag, si, fsl,
                           moving=(s.lotag in MOVING_LOTAGS) or si in se_sectors))
    sprites = [_sprite_from(sp.x, sp.y, sp.z, sp.cstat, sp.picnum, sp.yrepeat, sp.sectnum, sp.lotag, sp.hitag, tiles)
               for sp in m.sprites if 0 <= sp.sectnum < len(S)]
    start = (m.posx / 8.0, -m.posy / 8.0, m.cursectnum)
    return Model(secs, walls, sprites, start, name)


def model_from_import(j, tiles=None, name='e1l1_import') -> Model:
    """j = JSON de build_import.py ('duke2ps/e1-import v2') : sectors[].loops = ids de murs, walls[] Build."""
    J = {w['id']: w for w in j['walls']}
    walls = []
    for w in j['walls']:
        b = J[w['point2']]
        walls.append(Wall(w['id'], w['sector'], w['x'] / 8.0, -w['y'] / 8.0, b['x'] / 8.0, -b['y'] / 8.0,
                          w['nextsector'], w['nextwall'], w['cstat']))
    se_sectors = {sp['sector'] for sp in j['sprites']
                  if sp['picnum'] == 1 and sp['sector'] is not None and sp['lotag'] in MOVING_SE}
    secs = []
    for s in j['sectors']:
        ids = [k for L in s['loops'] for k in L]
        loops = [[(J[k]['x'] / 8.0, -J[k]['y'] / 8.0) for k in L] for L in s['loops']]
        x0, y0, x1, y1 = s['slope_ref']
        fy = [-_slope_z(s['floorz'], s['floorheinum'], s['floorstat'], x0, y0, x1, y1, J[k]['x'], J[k]['y']) / 128
              for k in ids]
        cy = [-_slope_z(s['ceilingz'], s['ceilingheinum'], s['ceilingstat'], x0, y0, x1, y1, J[k]['x'], J[k]['y'])
              / 128 for k in ids]
        fsl = (s['floorz'], s['floorheinum'], s['floorstat'], x0, y0, x1, y1) if s['floorstat'] & 2 else None
        secs.append(Sector(s['id'], ids, loops, min(fy), max(fy), min(cy), max(cy), bool(s['ceilingstat'] & 1),
                           bool(s['floorstat'] & 1), s['lotag'], s['hitag'], s['build_id'], fsl,
                           moving=(s['lotag'] in MOVING_LOTAGS) or s['id'] in se_sectors))
    sprites = [_sprite_from(sp['x'], sp['y'], sp['z'], sp['cstat'], sp['picnum'], sp.get('yrepeat', 64), sp['sector'],
                            sp['lotag'], sp['hitag'], tiles)
               for sp in j['sprites'] if sp['sector'] is not None]
    start = None
    d = j.get('depart_m1') or {}
    sat = d.get('saturn') if isinstance(d, dict) else None
    if sat:
        for s in secs:
            if s.contains(sat['X'], sat['Z']):
                start = (sat['X'], sat['Z'], s.id)
                break
    return Model(secs, walls, sprites, start, name)


# ----------------------------------------------------------------------------------------------------------
# Atteignable
# ----------------------------------------------------------------------------------------------------------
def reachable(model: Model) -> set:
    """Secteurs ou le joueur PowerSlave peut se tenir, depuis le depart : marche, saut (marche <= REACH_STEP),
    chute libre, ouverture >= MIN_PASS ; teleporteurs SE7 et ascenseurs SE17 apparies par hitag ; secteurs
    ascenseurs (lotag 15-19) : tous leurs voisins.  Pas de jetpack (absent de PowerSlave)."""
    S, W = model.sectors, model.walls
    links = defaultdict(set)
    by = defaultdict(list)
    for sp in model.sprites:
        if sp.picnum == 1 and sp.lotag in (7, 17):
            by[(sp.lotag, sp.hitag)].append(sp.sector)
    for secs in by.values():
        for a in secs:
            for b in secs:
                if a != b:
                    links[a].add(b)
    if model.start is not None:
        st = model.start[2]
    else:
        st = max(range(len(S)), key=lambda i: S[i].area)
    seen, q = {st}, deque([st])
    while q:
        a = q.popleft()
        sa = S[a]
        nxt = set(links[a])
        for wi in sa.walls:
            w = W[wi]
            b = w.next
            if b < 0 or b >= len(S):
                continue
            sb = S[b]
            lo = max(sa.floor_lo, sb.floor_lo)
            hi = min(sa.ceil_lo, sb.ceil_lo)
            elev = sa.lotag in ELEVATOR_LOTAGS or sb.lotag in ELEVATOR_LOTAGS
            if elev or (hi - lo >= MIN_PASS and sb.floor_lo <= sa.floor_hi + REACH_STEP):
                nxt.add(b)
        for b in nxt:
            if b not in seen:
                seen.add(b)
                q.append(b)
    return seen


# ----------------------------------------------------------------------------------------------------------
# Echantillonnage et visibilite 2,5D
# ----------------------------------------------------------------------------------------------------------
def sample_points(sec: Sector, walls, step: float = 64.0, inset: float = RADIUS):
    """Points ou le centre du joueur peut se tenir : grille `step` dans le polygone, a >= `inset` des murs
    PLEINS (le joueur est une boule de rayon 47).  Secteur trop etroit : le point le plus eloigne des murs."""
    x0, z0, x1, z1 = sec.bbox()
    xs = np.arange(x0 + step / 2, x1, step) if x1 - x0 > step else np.array([(x0 + x1) / 2])
    zs = np.arange(z0 + step / 2, z1, step) if z1 - z0 > step else np.array([(z0 + z1) / 2])
    G = np.array([(x, z) for x in xs for z in zs if sec.contains(x, z)], dtype=np.float64)
    extra = [((x0 + x1) / 2, (z0 + z1) / 2)]
    for L in sec.loops[:1]:
        cx = sum(p[0] for p in L) / len(L)
        cz = sum(p[1] for p in L) / len(L)
        extra.append((cx, cz))
    E = np.array([p for p in extra if sec.contains(*p)], dtype=np.float64).reshape(-1, 2)
    P = np.vstack([G.reshape(-1, 2), E]) if len(E) else G.reshape(-1, 2)
    if not len(P):
        return P
    solid = [walls[w] for w in sec.walls if walls[w].next < 0]
    if not solid:
        return P
    A = np.array([(w.ax, w.az) for w in solid])
    B = np.array([(w.bx, w.bz) for w in solid])
    d = _pt_seg_dist(P, A, B).min(axis=1)
    keep = P[d >= inset]
    if len(keep):
        return keep
    return P[[int(np.argmax(d))]]


def _pt_seg_dist(P, A, B):
    AB = B - A
    L = (AB ** 2).sum(1)
    L[L == 0] = 1e-12
    t = ((P[:, None, :] - A[None]) * AB[None]).sum(2) / L[None]
    t = np.clip(t, 0, 1)
    C = A[None] + t[..., None] * AB[None]
    return np.sqrt(((P[:, None, :] - C) ** 2).sum(2))


def wall_targets(walls, wall_ids, step: float = 64.0):
    """Points cibles le long des murs (extremites + tous les `step` u) : (X, Z, id du mur)."""
    out = []
    for wi in wall_ids:
        w = walls[wi]
        n = max(1, int(math.ceil(w.length / step)))
        for k in range(n + 1):
            t = k / n
            out.append((w.ax + t * (w.bx - w.ax), w.az + t * (w.bz - w.az), wi))
    return out


def visible_heights(model: Model, ceil: dict, floor: dict, view_sids: set, targets, views, sky_ids: set,
                    chunk: int = 64, dist_cap: float | None = None, eye_below=None):
    """Pour chaque cible q (point d'un mur) : hauteur maximale visible au-dessus de q, a tangage nul, depuis les
    points de vue `views` = [(X, Z, Yoeil, secteur)] : oeil + D * min(TAN_UP, min des (haut d'ouverture - oeil)/a)
    sur les ouvertures traversees a la distance a ; un mur plein (ou une sortie de `view_sids`) bloque.
    Les ouvertures ciel <-> ciel ne limitent rien (le parallax continue, WALLS.C:1667).
    Retourne (hauteurs[M], indice du point de vue[M]) ; -inf si la cible n'est vue de nulle part."""
    S, W = model.sectors, model.walls
    wl = [wi for s in view_sids for wi in S[s].walls]
    if not targets or not views or not wl:
        return np.full(len(targets), -np.inf), np.full(len(targets), -1)
    A = np.array([(W[i].ax, W[i].az) for i in wl])
    B = np.array([(W[i].bx, W[i].bz) for i in wl])
    block = np.zeros(len(wl), bool)
    top = np.full(len(wl), np.inf)
    for k, wi in enumerate(wl):
        w = W[wi]
        s, n = w.sector, w.next
        if n < 0 or n not in view_sids:
            block[k] = True
            continue
        lo = max(floor[s], floor[n])
        if s in sky_ids and n in sky_ids:
            continue
        hi = min(ceil[s], ceil[n])
        if hi - lo <= 1:
            block[k] = True
        top[k] = hi
    wid = np.array(wl)
    twin = np.array([W[i].twin for i in wl])
    Q = np.array([(t[0], t[1]) for t in targets])
    qw = np.array([t[2] for t in targets])
    qtw = np.array([W[t[2]].twin for t in targets])
    V = np.array([(v[0], v[1]) for v in views])
    E = np.array([v[2] for v in views])
    best = np.full(len(Q), -np.inf)
    arg = np.full(len(Q), -1)
    same = (wid[None, :] == qw[:, None]) | (wid[None, :] == qtw[:, None]) | (twin[None, :] == qw[:, None])
    r = B - A                                              # N x 2
    for k0 in range(0, len(V), chunk):
        for k in range(k0, min(len(V), k0 + chunk)):
            p = V[k]
            e = E[k]
            d = Q - p                                          # M x 2
            D = np.sqrt((d ** 2).sum(1))
            den = d[:, None, 0] * r[None, :, 1] - d[:, None, 1] * r[None, :, 0]      # M x N
            ap = A[None, :, :] - p[None, None, :]
            with np.errstate(divide='ignore', invalid='ignore'):
                t = (ap[..., 0] * r[None, :, 1] - ap[..., 1] * r[None, :, 0]) / den      # le long de p->q
                u = (ap[..., 0] * d[:, None, 1] - ap[..., 1] * d[:, None, 0]) / den      # le long du mur
            hit = (np.abs(den) > 1e-9) & (t > 1e-6) & (t < 1 - 1e-6) & (u >= -1e-9) & (u <= 1 + 1e-9) & ~same
            blocked = (hit & block[None, :]).any(1)
            with np.errstate(divide='ignore', invalid='ignore', over='ignore'):
                a = t * D[:, None]
                lim = np.where(hit & ~block[None, :] & np.isfinite(top[None, :]), (top[None, :] - e) / a, np.inf)
            slope = np.minimum(TAN_UP, lim.min(1))
            vis = np.where(blocked, -np.inf, e + D * slope)
            if dist_cap is not None:
                vis = np.where(D > dist_cap, -np.inf, vis)
            if eye_below is not None:
                # un spectateur deja au-dessus du ciel du secteur de la facade la voit d'en haut : pas de contrainte
                vis = np.where(e < eye_below, vis, -np.inf)
            better = vis > best
            best = np.where(better, vis, best)
            arg = np.where(better, k, arg)
    return best, arg


def round_up(y: float, floor_ref: float, mode: str = 'rel128') -> float:
    if mode == 'abs128':
        return math.ceil(y / CELL - 1e-9) * CELL
    if mode == 'rel128':
        return floor_ref + math.ceil((y - floor_ref) / CELL - 1e-9) * CELL
    if mode == 'rel64':
        return floor_ref + math.ceil((y - floor_ref) / 64 - 1e-9) * 64
    return y


# ----------------------------------------------------------------------------------------------------------
# Regions de ciel, vues
# ----------------------------------------------------------------------------------------------------------
def sky_regions(model: Model) -> list:
    S, W = model.sectors, model.walls
    seen, out = set(), []
    for s in S:
        if not s.sky or s.id in seen:
            continue
        comp, q = [], deque([s.id])
        seen.add(s.id)
        while q:
            a = q.popleft()
            comp.append(a)
            for wi in S[a].walls:
                b = W[wi].next
                if 0 <= b < len(S) and S[b].sky and b not in seen:
                    seen.add(b)
                    q.append(b)
        out.append(sorted(comp))
    return out


def neighbours(model: Model, sid: int) -> set:
    return {model.walls[w].next for w in model.sectors[sid].walls if model.walls[w].next >= 0}


def _views(model, sids, reach, step, cache):
    """Points de vue : grille des sols atteignables + arrivees de teleporteur SE7 (le joueur y apparait a
    l'altitude du SE partenaire : refs/build/jfduke3d/src/actors.c:2712 posz = sprite[OW].z - PHEIGHT, puis
    tombe -- ex. le puits de depart d'E1L1)."""
    out = []
    for s in sids:
        if s not in reach:
            continue
        if s not in cache:
            sec = model.sectors[s]
            cache[s] = [(float(x), float(z), sec.floor_at(float(x), float(z)) + EYE, s)
                        for x, z in sample_points(sec, model.walls, step)]
        out += cache[s]
    if ('se7', None) not in cache:
        cache[('se7', None)] = [(sp.X, sp.Z, sp.Ybot + EYE, sp.sector) for sp in model.sprites
                                if sp.picnum == 1 and sp.lotag == 7 and 0 <= sp.sector < len(model.sectors)
                                and sp.Ybot > model.sectors[sp.sector].floor_hi + REACH_TOP]
    out += [v for v in cache[('se7', None)] if v[3] in sids and v[3] in reach]
    return out


# ----------------------------------------------------------------------------------------------------------
# Regles
# ----------------------------------------------------------------------------------------------------------
DEFAULTS = dict(
    sky='fov',            # 'fov' (H1) | 'global' | 'neigh' | 'build'
    sky_global=960.0,     # pour sky='global'
    sky_neigh_margin=128.0,
    rounding='rel128',    # 'rel128' | 'abs128' | 'rel64' | 'none'
    sky_margin=0.0,       # ajoute a la hauteur H1 avant arrondi
    unify=True,           # une region de ciel = une hauteur (les ciels bas remontent)
    h2=True,              # plafonds jamais vus
    perched=True,         # pieces perchees
    sample=64.0,          # pas des points de vue / cibles
    ceiling_sprite_tol=16.0,   # sprite colle au plafond (<= tol sous le plafond Build) : suit le plafond
    sky_cap=None,         # plafond de ciel impose (budget) : H = min(H, sky_cap), puis H3
    view_cap=None,        # distance au-dela de laquelle un haut de facade / plafond peut apparaitre (None = oo)
    perched_max_sectors=4,   # H2b : une PIECE (<= 4 secteurs), pas une zone -- seuil pose apres les faux positifs
    perched_min_drop=128.0,  # de SECRET1/RAWMEAT/HOTELHEL (perched_eval.log) : NON calibre sur E1L1 seul
)


def _floor_ref(model, sids):
    return min(model.sectors[s].floor_lo for s in sids)


def rule_h2_ceilings(model: Model, reach: set, p: dict, cache: dict) -> dict:
    """H2 : plafond non ciel jamais a l'ecran (tangage nul) -> descendu a la hauteur ou il y entrerait."""
    S = model.sectors
    ceil = {s.id: s.ceil_lo for s in S}
    floor = {s.id: s.floor_hi for s in S}
    out = {}
    for s in S:
        if s.sky or s.moving:
            continue
        nb = neighbours(model, s.id)
        vs = {s.id} | nb
        x0, z0, x1, z1 = s.bbox()
        for n in nb:
            a, b, c, d = S[n].bbox()
            x0, z0, x1, z1 = min(x0, a), min(z0, b), max(x1, c), max(z1, d)
        diag = math.hypot(x1 - x0, z1 - z0)
        if s.ceil_hi - s.floor_lo <= EYE + diag * TAN_UP + CELL:
            continue                                   # visible a coup sur : inutile de calculer
        views = _views(model, vs, reach, p['sample'], cache)
        if not views:
            out[s.id] = (s.ceil_hi, 'H2: aucun point de vue atteignable -- inchange')
            continue
        tg = wall_targets(model.walls, s.walls, p['sample'])
        vis, arg = visible_heights(model, ceil, floor, vs, tg, views, set(), dist_cap=p.get('view_cap'))
        if not np.isfinite(vis).any():
            continue
        k = int(np.argmax(vis))
        req = float(vis[k])
        new = round_up(req, s.floor_lo, p['rounding'])
        if new < s.ceil_lo - 1e-6:                     # tout le plafond (pente comprise) est hors de l'ecran
            v = views[int(arg[k])]
            pente = ' ; pente aplatie' if s.ceil_lo != s.ceil_hi else ''
            out[s.id] = (new, f'H2: plafond jamais vu au-dessus de Y {req:.0f} (vu depuis le secteur {v[3]}, '
                              f'd {math.hypot(tg[k][0] - v[0], tg[k][1] - v[1]):.0f} u) -> {new:.0f}{pente}')
    return out


def rule_h1_sky(model: Model, reach: set, ceil_now: dict, p: dict, cache: dict) -> dict:
    """H1 : une hauteur par region de ciel.  Variantes : fov (principe), global, neigh, build."""
    S, W = model.sectors, model.walls
    floor = {s.id: s.floor_hi for s in S}
    out = {}
    for R in sky_regions(model):
        Rs = set(R)
        bmax = max(S[s].ceil_hi for s in R)
        fref = _floor_ref(model, R)
        if p['sky'] == 'build':
            for s in R:
                out[s] = (S[s].ceil_hi, 'H1 build: inchange')
            continue
        if p['sky'] == 'global':
            H = p['sky_global']
            why = f'H1 global: {H:.0f}'
        elif p['sky'] == 'neigh':
            tops = []
            for s in R:
                tops.append(S[s].floor_hi)
                for n in neighbours(model, s):
                    if n in Rs:
                        continue
                    tops.append(S[n].floor_hi)
                    if not S[n].sky:
                        tops.append(ceil_now[n])
            H = round_up(max(tops) + p['sky_neigh_margin'], fref, p['rounding'])
            why = f'H1 voisins: max(sols, plafonds voisins) {max(tops):.0f} + {p["sky_neigh_margin"]:.0f} -> {H:.0f}'
        else:
            ring = set()
            for s in R:
                ring |= {n for n in neighbours(model, s) if n not in Rs and not S[n].sky}
            vs = Rs | ring
            views = _views(model, vs, reach, p['sample'], cache)
            fac = [wi for s in R for wi in S[s].walls if W[wi].next < 0 or W[wi].next not in Rs]
            if not views or not fac:
                for s in R:                               # chaque secteur garde SON ciel Build
                    out[s] = (S[s].ceil_hi, 'H1 fov: aucun point de vue ou aucune facade -- Build garde')
                continue
            else:
                ceil = dict(ceil_now)
                for s in R:
                    ceil[s] = 1e9
                tg = wall_targets(W, fac, p['sample'])
                below = np.array([S[W[t[2]].sector].ceil_hi for t in tg])
                vis, arg = visible_heights(model, ceil, floor, vs, tg, views, Rs, dist_cap=p.get('view_cap'),
                                           eye_below=below)
                k = int(np.argmax(vis))
                if not np.isfinite(vis[k]):
                    for s in R:                           # chaque secteur garde SON ciel Build
                        out[s] = (S[s].ceil_hi, 'H1 fov: aucune facade vue -- Build garde')
                    continue
                req = float(vis[k]) + p['sky_margin']
                H = round_up(req, fref, p['rounding'])
                v = views[int(arg[k])]
                why = (f'H1 fov: facade vue jusqu a Y {vis[k]:.0f} (depuis le secteur {v[3]}, d '
                       f'{math.hypot(tg[k][0] - v[0], tg[k][1] - v[1]):.0f} u, oeil {v[2]:.0f}) -> {H:.0f}')
                if p['unify'] is not True:
                    # besoin propre de chaque secteur : max de la hauteur vue sur SES facades
                    per = {}
                    tsec = np.array([W[t[2]].sector for t in tg])
                    for s in R:
                        msk = tsec == s
                        if msk.any() and np.isfinite(vis[msk]).any():
                            kk = int(np.argmax(np.where(msk, vis, -np.inf)))
                            hs = round_up(float(vis[kk]) + p['sky_margin'], S[s].floor_lo, p['rounding'])
                            vv = views[int(arg[kk])]
                            per[s] = (min(hs, bmax), f'H1 fov (secteur): facade vue jusqu a Y {vis[kk]:.0f} (depuis '
                                                     f'{vv[3]}, d {math.hypot(tg[kk][0] - vv[0], tg[kk][1] - vv[1]):.0f}'
                                                     f' u) -> {min(hs, bmax):.0f}')
                    Hr = min(H, bmax)
                    for s in R:
                        if p['unify'] == 'lower':
                            # un ciel haut descend a la hauteur de la region ; un ciel bas ne monte que jusqu'a
                            # son propre besoin (jamais au-dela de la region) -- pas de facade geante inutile
                            if S[s].ceil_hi >= Hr:
                                h_s, w_s = Hr, why + ' (region)'
                            else:
                                own, w_own = per.get(s, (S[s].ceil_hi, 'aucune facade vue'))
                                h_s = min(Hr, max(S[s].ceil_hi, own))
                                w_s = (f'H1 fov (ciel bas): garde {S[s].ceil_hi:.0f}' if h_s <= S[s].ceil_hi + 1e-6
                                       else w_own.replace(f'-> {own:.0f}', f'-> {h_s:.0f}') + ' (monte au besoin propre)')
                        else:
                            h_s, w_s = per.get(s, (Hr, why + ' (secteur sans facade : region)'))
                        if p.get('sky_cap') is not None and h_s > p['sky_cap']:
                            h_s, w_s = p['sky_cap'], w_s + f' ; plafonne au budget {p["sky_cap"]:.0f}'
                        out[s] = (h_s, w_s)
                    continue
        if H > bmax:
            H, why = bmax, why + f' ; borne au ciel Build {bmax:.0f}'
        if p.get('sky_cap') is not None and H > p['sky_cap']:
            H, why = p['sky_cap'], why + f' ; plafonne au budget {p["sky_cap"]:.0f}'
        for s in R:
            out[s] = (H, why)
    return out


def _opening(model, s, n, ceil, floor, sky_ok=True):
    S = model.sectors
    lo = max(floor[s], floor[n])
    if sky_ok and S[s].sky and S[n].sky:
        return lo, math.inf
    return lo, min(ceil[s], ceil[n])


def _xz_overlap(a: Sector, b: Sector) -> bool:
    ax0, az0, ax1, az1 = a.bbox()
    bx0, bz0, bx1, bz1 = b.bbox()
    if ax1 <= bx0 or bx1 <= ax0 or az1 <= bz0 or bz1 <= az0:
        return False
    for L in a.loops:
        for x, z in L:
            if b.contains(x, z):
                return True
    for L in b.loops:
        for x, z in L:
            if a.contains(x, z):
                return True
    cx, cz = (max(ax0, bx0) + min(ax1, bx1)) / 2, (max(az0, bz0) + min(az1, bz1)) / 2
    return a.contains(cx, cz) and b.contains(cx, cz)


def apply_rules(model: Model, params: dict | None = None, memo: dict | None = None) -> tuple:
    """Applique H2 (plafonds), H2b (pieces perchees), H1 (ciels), puis H3 jusqu'au point fixe.
    `memo` (dict fourni par l'appelant, facultatif) garde les calculs de visibilite H2/H1, qui ne dependent pas de
    `sky_cap` : un balayage de plafonds de ciel ne refait alors que H2b et H3.
    Retourne ({secteur: Decision}, rapport = liste de lignes)."""
    p = dict(DEFAULTS)
    p.update(params or {})
    S, W = model.sectors, model.walls
    mkey = repr(sorted((k, v) for k, v in p.items() if k != 'sky_cap'))
    if memo is not None and memo.get('key') != mkey:
        memo.clear()
        memo['key'] = mkey
    reach = memo['reach'] if memo and 'reach' in memo else reachable(model)
    if memo is not None:
        memo['reach'] = reach
    cache = memo.setdefault('views', {}) if memo is not None else {}
    report = [f'atteignables {len(reach)}/{len(S)} secteurs ; oeil {EYE} u, saut {JUMP:.2f} u, '
              f'tan demi-champ haut {TAN_UP:.4f}']
    dec = {s.id: Decision(s.ceil_hi, s.ceil_hi, s.floor_lo, s.floor_lo) for s in S}
    ceil = {s.id: s.ceil_lo for s in S}           # plafond courant (bas de pente)
    floor = {s.id: s.floor_hi for s in S}
    orig_open = {}
    for w in W:
        if w.next >= 0 and w.sector >= 0:
            lo, hi = _opening(model, w.sector, w.next, ceil, floor)
            if hi - lo > 1:
                orig_open[w.id] = (lo, hi)
    # ---- H2 : plafonds jamais vus
    if p['h2']:
        if memo is not None and 'h2' in memo:
            h2 = memo['h2']
        else:
            h2 = rule_h2_ceilings(model, reach, p, cache)
            if memo is not None:
                memo['h2'] = h2
        for sid, (new, why) in h2.items():
            if new < dec[sid].ceil_new:
                dec[sid].ceil_new = new
                ceil[sid] = new
                dec[sid].reasons.append(why)
    # ---- H2b : pieces perchees (ouverture coupee par un plafond abaisse -> la piece descend avec son puits)
    moved = set()
    if p['perched']:
        for wid, (lo, hi) in sorted(orig_open.items()):
            w = W[wid]
            s, n = w.sector, w.next
            if s in moved or n in moved:
                continue
            need = lo + min(hi - lo, REACH_TOP)
            if min(ceil[s], ceil[n]) >= need - 1e-6:
                continue
            shaft, up = (s, n) if floor[n] > floor[s] else (n, s)
            if dec[shaft].ceil_new >= dec[shaft].ceil_old:
                continue                                  # le puits n'a pas ete abaisse : H3 s'en chargera
            G, q = {up}, deque([up])
            while q:
                a = q.popleft()
                for wi in S[a].walls:
                    b = W[wi].next
                    if b < 0 or b == shaft or b in G:
                        continue
                    if wi in orig_open or W[wi].twin in orig_open:
                        G.add(b)
                        q.append(b)
            st = model.start[2] if model.start else -1
            delta = need - ceil[shaft]
            if st in G or len(G) > p['perched_max_sectors'] or delta < p['perched_min_drop']:
                continue                                  # une zone (pas une piece) ou un gain < 1 cellule : H3

            safe = delta
            for g in G:
                for o in S:
                    if o.id in G or o.id == shaft:
                        continue
                    if o.ceil_hi <= S[g].floor_lo + 1e-6 and _xz_overlap(S[g], o):
                        safe = min(safe, S[g].floor_lo - o.ceil_hi)
            safe = max(0.0, safe)
            if safe <= 0:
                continue
            for g in G:
                dec[g].dy -= safe
                dec[g].floor_new -= safe
                dec[g].ceil_new -= safe
                ceil[g] -= safe
                floor[g] -= safe
                moved.add(g)
                dec[g].reasons.append(f'H2b: piece perchee au-dessus du puits {shaft} descendue de {safe:.0f} u '
                                      f'(groupe {sorted(G)})')
            reach_note = 'inaccessible sans jetpack' if (lo - safe) - floor[shaft] > REACH_STEP else 'accessible'
            dec[shaft].reasons.append(f'H2b: ouverture vers {up} (Y {lo:.0f}) redescendue a {lo - safe:.0f} ; '
                                      f'{reach_note}' + ('' if safe >= delta else f' ; recul collision {delta - safe:.0f} u'))
            report.append(f'perchee: groupe {sorted(G)} au-dessus de {shaft} descend de {safe:.0f} u ({reach_note})')
    # ---- H1 : ciels (calcul sans plafond de budget, mis en memo ; le plafond est applique ensuite)
    if memo is not None and 'h1' in memo:
        h1 = memo['h1']
    else:
        h1 = rule_h1_sky(model, reach, ceil, dict(p, sky_cap=None), cache)
        if memo is not None:
            memo['h1'] = h1
    if p.get('sky_cap') is not None:
        cap = p['sky_cap']
        h1 = {s: ((cap, w + f' ; plafonne au budget {cap:.0f}') if h > cap else (h, w)) for s, (h, w) in h1.items()}
    for sid, (new, why) in h1.items():
        dec[sid].ceil_new = new
        ceil[sid] = new
        dec[sid].reasons.append(why)
    # ---- H3 : jamais couper (point fixe, on ne fait que remonter)
    spr = defaultdict(list)
    for sp in model.sprites:
        if not sp.marker and 0 <= sp.sector < len(S):
            spr[sp.sector].append(sp)
    regions = {s: tuple(R) for R in sky_regions(model) for s in R}
    for it in range(20):
        raise_to = {}

        def want(sid, y, why):
            if y > ceil[sid] + 1e-6 and y > raise_to.get(sid, (-math.inf, ''))[0]:
                raise_to[sid] = (y, why)

        for s in S:
            d = dec[s.id]
            if d.ceil_new >= d.ceil_old - 1e-6 and not s.sky:
                continue
            h0 = s.ceil_lo - s.floor_hi
            want(s.id, floor[s.id] + min(h0, REACH_TOP), f'H3 sol: garder min(hauteur {h0:.0f}, {REACH_TOP:.0f})')
            for sp in spr[s.id]:
                if sp.Ytop >= s.ceil_lo - p['ceiling_sprite_tol']:
                    continue                                 # accroche au plafond (haut au plafond) : le suit
                top = sp.Ytop + d.dy
                if top > ceil[s.id] + 1e-6:
                    want(s.id, top, f'H3 sprite: pic {sp.picnum} haut Y {top:.0f}')
        for wid, (lo, hi) in orig_open.items():
            w = W[wid]
            s, n = w.sector, w.next
            dlo = max(floor[s], floor[n]) if (s in moved or n in moved) else lo
            if S[s].sky and S[n].sky:
                continue
            need = dlo + min(hi - lo, REACH_TOP)
            for x in (s, n):
                if ceil[x] < need - 1e-6 and dec[x].ceil_new < dec[x].ceil_old + (dec[x].dy) - 1e-6:
                    want(x, need, f'H3 ouverture: mur {wid} vers {n if x == s else s} (Y {dlo:.0f}..{hi:.0f})')
        if not raise_to:
            break
        for sid, (y, why) in raise_to.items():
            grp = regions.get(sid, (sid,)) if (S[sid].sky and p['unify'] is True) else (sid,)
            for g in grp:
                if y > ceil[g] + 1e-6:
                    ceil[g] = y
                    dec[g].ceil_new = max(dec[g].ceil_new, y)
                    dec[g].reasons.append('recul ' + why + (f' (region {sid})' if g != sid else ''))
            report.append(f'H3 recul secteur {sid} -> {y:.0f} : {why}')
    return dec, report


# ----------------------------------------------------------------------------------------------------------
# Budget
# ----------------------------------------------------------------------------------------------------------
def faces(model: Model, dec: dict):
    """Faces de murs a paver : (mur, secteur, longueur, y_bas, y_haut, parallax).
    Mur plein : sol -> plafond.  Mur rouge vers N : marche basse [f_S, f_N] si f_N > f_S ; haut [c_N, c_S] si
    c_S > c_N ; ciel <-> ciel : le haut est parallax (pas de cellules, CONVERT.C:1865-1867/1894-1896)."""
    S, W = model.sectors, model.walls
    out = []
    for w in W:
        s = w.sector
        if s < 0:
            continue
        L = w.length
        if L <= 0:
            continue
        fs, cs = dec[s].floor_new, dec[s].ceil_new
        if w.next < 0:
            out.append((w.id, s, L, fs, cs, False))
            continue
        n = w.next
        fn, cn = dec[n].floor_new, dec[n].ceil_new
        if fn > fs + 1e-6:
            out.append((w.id, s, L, fs, min(fn, cs), False))
        if cs > cn + 1e-6:
            out.append((w.id, s, L, max(cn, fs), cs, S[s].sky and S[n].sky))
    return out


def static_cost(model: Model, dec: dict, cell: int = CELL):
    """Cout statique : cellules, sommets de grille, faces qui depassent MAXVPERWALL, octets estimes."""
    cells = verts = over = 0
    worst = 0
    for wid, s, L, y0, y1, par in faces(model, dec):
        if par or y1 - y0 <= 0:
            continue
        c, r = math.ceil(L / cell), math.ceil((y1 - y0) / cell)
        cells += c * r
        v = (c + 1) * (r + 1)
        verts += v
        worst = max(worst, v)
        if v >= MAXVPERWALL:
            over += 1
    return dict(cells=cells, verts=verts, over_maxv=over, worst_face_verts=worst,
                bytes_est=9 * verts + 2 * cells)     # sommet 8 o (SLEVEL.H:115-118) + 1 o de lumiere ; 2 o/cellule


def frame_cost(model: Model, dec: dict, views, yaws: int = 8, cell: int = CELL, max_visits: int = 3000):
    """Estimation par image : depuis chaque point de vue et `yaws` caps, parcours 2D des portails en fenetre
    ecran (comme sectorDraw xmin/xmax, WALLS.C:1587-1818).  Compte les cellules de faces visibles dans le
    tronc vertical (dessin : les cellules hors ecran sont rejetees une a une, WALLS.C:1071-1109) et les sommets
    de grille des faces visibles (transformes pour la face entiere, WALLS.C:1014-1065).
    Sur-estime (pas d'occultation a l'interieur d'un secteur non convexe).  Retourne une liste de dicts."""
    S, W = model.sectors, model.walls
    fl = defaultdict(list)
    for f in faces(model, dec):
        fl[f[0]].append(f)
    res = []
    for (X, Z, eye, sid) in views:
        best = None
        for k in range(yaws):
            yaw = 2 * math.pi * k / yaws
            cy, sy = math.cos(yaw), math.sin(yaw)
            tverts = floorc = 0
            seen_f = set()
            face_best = {}
            done = defaultdict(list)
            stack = [(sid, -TAN_SIDE, TAN_SIDE, -1)]
            visits = 0
            seen_sec = set()
            while stack and visits < max_visits:
                s, lo, hi, came = stack.pop()
                if any(a <= lo + 1e-9 and hi <= b + 1e-9 for a, b in done[s]):
                    continue                              # fenetre deja couverte pour ce secteur
                done[s].append((lo, hi))
                visits += 1
                if s not in seen_sec:
                    seen_sec.add(s)
                    flat = math.ceil(S[s].area / (cell * cell))
                    floorc += flat if S[s].sky else 2 * flat      # sol + plafond ; plafond ciel = parallax
                for wi in S[s].walls:
                    if wi == came:
                        continue
                    w = W[wi]
                    ax, az = w.ax - X, w.az - Z
                    bx, bz = w.bx - X, w.bz - Z
                    za, xa = ax * cy + az * sy, -ax * sy + az * cy
                    zb, xb = bx * cy + bz * sy, -bx * sy + bz * cy
                    near = 1.0
                    if za < near and zb < near:
                        continue
                    if za < near:
                        t = (near - za) / (zb - za)
                        xa, za = xa + t * (xb - xa), near
                    elif zb < near:
                        t = (near - zb) / (za - zb)
                        xb, zb = xb + t * (xa - xb), near
                    sa, sb = xa / za, xb / zb
                    l, h = min(sa, sb), max(sa, sb)
                    L2, H2 = max(l, lo), min(h, hi)
                    if L2 >= H2:
                        continue
                    frac = (H2 - L2) / (h - l) if h > l else 1.0
                    zmax = max(za, zb)
                    for f in fl.get(wi, ()):
                        _, _, Lf, y0, y1, par = f
                        if par or y1 <= y0:
                            continue
                        c, r = math.ceil(Lf / cell), math.ceil((y1 - y0) / cell)
                        key = (wi, y0)
                        if key not in seen_f:
                            seen_f.add(key)
                            tverts += (c + 1) * (r + 1)
                        vlo, vhi = eye - zmax * TAN_DOWN, eye + zmax * TAN_UP
                        rows = sum(1 for i in range(r) if y0 + i * cell < vhi and y0 + (i + 1) * cell > vlo)
                        face_best[key] = max(face_best.get(key, 0), math.ceil(c * frac) * rows)
                    n = w.next
                    if n >= 0:
                        lo_o = max(dec[s].floor_new, dec[n].floor_new)
                        hi_o = min(dec[s].ceil_new, dec[n].ceil_new)
                        if (S[s].sky and S[n].sky) or hi_o - lo_o > 1:
                            stack.append((n, L2, H2, w.twin))
            drawn = sum(face_best.values())
            tot = drawn + floorc
            if best is None or tot > best['total']:
                best = dict(total=tot, wall_cells=drawn, flat_cells=floorc, verts=tverts, yaw=k, sector=sid)
        res.append(best)
    return res


def frame_views(model: Model, step: float = 128.0, max_n: int = 200, seed: int = 1):
    reach = reachable(model)
    cache = {}
    v = _views(model, sorted(reach), reach, step, cache)
    if len(v) > max_n:
        rng = np.random.default_rng(seed)
        v = [v[i] for i in sorted(rng.choice(len(v), max_n, replace=False))]
    return v

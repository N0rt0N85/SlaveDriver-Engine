#!/usr/bin/env python3
"""doom3d.py -- WAD Doom -> geometrie .LEV PowerSlave (generation 1), jumeau Doom de geom3d.py.

L'emetteur de `tools/duke2ps/geom3d.py` (classe Emitter) est GENERIQUE : il prend des quads et des
polygones deja en repere Saturn et sait en faire des murs, des grilles de cellules, des faces, des
tuiles et des lumieres, en respectant les conventions MESUREES sur les 24 .LEV retail. Seul le
*Converter* de ce fichier-la est specifique a Build. Ici on reecrit le Converter pour Doom et on
reutilise l'Emitter tel quel.

Repere : X = x_doom, Y = z_doom (vertical), Z = y_doom -- **1:1, sans facteur d'echelle**.
C'est ce qui rend la conversion des textures exacte : une unite Doom = un texel (mur de 128 de haut
= texture de 128 de haut), et une tuile SlaveDriver fait 64 texels pour 64 unites (TILESIZE 64) --
donc un FLAT Doom 64x64 tombe pile sur une cellule, alignement de la grille monde compris.
Doom y croit vers le nord et Build vers le sud, donc Z = +y_doom garde la main (pas de miroir).

Morceaux convexes : les FEUILLES DU BSP du WAD. Elles sont convexes par construction et gratuites
(pas de decomposition a calculer). Le polygone d'une feuille n'est pas dans le WAD : on le
reconstruit en clippant la bbox de la carte par les lignes de partition des ancetres, puis par les
segs de la feuille. Le decoupage des aretes par les VOISINS reels -- une arete de partition peut
border plusieurs feuilles -- est delegue a `adjacency.py`, qui le fait EXACTEMENT, par etiquette de
ligne. Ne jamais revenir a l'echantillonnage : voir l'en-tete de ce module-la pour la mesure.

Usage : python tools\\doom2ps\\doom3d.py --wad DOOM1.WAD --map E1M1 --out build\\doom2ps\\e1m1_geom3d.json
"""
from __future__ import annotations

import argparse
import json
import math
import os
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "duke2ps"))

import wad as wadmod                                   # noqa: E402
import adjacency                                       # noqa: E402
import doom_specials as sp                             # noqa: E402
from geom3d import (Emitter, TILESIZE, CAP_CELLS, plane_of, area2,     # noqa: E402
                    CELL_MIN_U, CELL_MAX_U, CELL_MIN_V, CELL_MAX_V, SECTOR_LIGHT,
                    CELL_HARD_MAX)

SKYFLAT = "F_SKY1"
EPS = 1e-6
MAXNMSECTORS = 600          # UTIL.H:21
MAXNMWALLS = 5500           # UTIL.H:22
WORLD_LIMIT = 16000         # assert(camera->pos.x < F(16000)) WALLS.C:1605-1606

# WALLFLAG_SHORTOPENING : ouverture trop basse pour que le JOUEUR passe. Ce n'est pas une
# decoration -- `constructPlayer` donne SPRITEFLAG_BSHORT au joueur EN DUR (AI.C:47), et
# `bumpSectorBoundries` (SPRITE.C:411) refuse la traversee des que
# `(mur->flags & sprite->flags) & WALLFLAG_BLOCKBITS`. Un portail marque est donc un MUR pour le
# joueur, invisible ou non.
#
# Le convertisseur retail compare a 90 (UTIL/CONVERT.C:2571). Cette constante n'est pas
# universelle : c'est la taille du joueur de PowerSlave, une sphere de PLAYER_RADIUS = 47, soit 94
# (gameparams.cfg:19). La recopier pour Doom fut le bug : la salle de depart d'E1M1 fait 72 de haut
# -- la hauteur canonique de Doom -- donc ses cinq portails etaient bloques et le joueur scellé
# dans une seule feuille du BSP. MESURE : 219 portails sur 781 bloques (28 %).
#
# La bonne valeur est celle de Doom lui-meme : P_LineOpening / PIT_CheckLine bloquent le joueur
# quand `opening < hauteur de MT_PLAYER`, soit 56. Elle est aussi tres au-dessus des 2 x 16 = 32
# de la sphere de `params/doom.cfg`, donc ce qui est autorise passe physiquement.
PLAYER_FIT_HEIGHT = 56.0
# WALLFLAG_BLOCKED (0x100, que seul le joueur porte : SPRITEFLAG_BBLOCKED, AI.C:47) sur un portail
# dont le bas est a plus de 24 u (MAXSTEPMOVE, params/doom.cfg PLAYER_STEP) au-dessus du sol de SA
# feuille. Le test de marche du moteur (bumpFloor, SPRITE.C:369) ne suffit pas : la sphere du joueur
# est centree sur l'oeil (41 u) et ROULE par-dessus toute arete plus basse que son centre
# (bumpWall, SPRITE.C:200-267), donc un rebord de fenetre de 25 a 40 u se franchissait. Les
# portails d'un ascenseur sont recalcules en marche par doom_pbBlockBits (DOOM_GAME.C).
PLAYER_STEP_HEIGHT = 24.0
WALLFLAG_BLOCKED = 0x100

# ----------------------------------------------------------------------------------------
# geometrie MOBILE (--mobile) : portes fermees + course, push blocks (SPEC_CONVERTER 5.1)
# ----------------------------------------------------------------------------------------
# Fente d'une ouverture fermee : plafond de porte = sol + 1, portail d'ascenseur ferme = 1 u sous
# le sol. `Emitter.add_wall` rejette un quad sans plan (geom3d.py:400-404, `quads_degeneres`), donc
# une ouverture de hauteur 0 n'aurait AUCUN mur a faire bouger. 1 u < 56 : SHORTOPENING bloque le
# joueur tant que c'est ferme (E8), et `setDoorBlockBits` recalcule a chaque pas de porte (AI.C:4294).
DOOR_SLIT = sp.DOOR_SLIT
WALLFLAG_DOORWALL = 0x20
WALLFLAG_SHORTOPENING = 0x1000
PBVERT_MAX = 255            # sPBVertex.vNm est un unsigned char (SLEVEL.H:109-113)


class MobileTag:
    """Un secteur Doom qui bouge : la liste ORDONNEE des murs .LEV qui le composent (PBWall) et
    l'ensemble des sommets a la hauteur mobile (PBVert). Les sommets sont notes PAR ROLE au moment
    de l'emission (haut d'un mur de porte, bas d'un mur d'ascenseur, sol/plafond entier), jamais
    en cherchant une coordonnee apres coup : le plafond ferme d'une porte et son sol sont a 1 u.
    Seul `dy` est applique par le moteur (SPRITE.C:852-890)."""

    def __init__(self, sector, kind, lower, upper):
        self.sector = sector            # secteur Doom
        self.kind = kind                # 'door' | 'lift' | 'floor_*'
        self.lower, self.upper = lower, upper
        # OBJETS mur (dicts de l'Emitter), pas des index : `emit_leaf` reordonne les murs d'une
        # feuille apres emission (portails en tete), donc un index note pendant l'emission est
        # perime. Les index sont resolus par `wall_indices` une fois le niveau construit.
        self.walls = []                 # dicts de murs, dans l'ordre d'emission, sans doublon
        self._wset = set()
        self.verts = set()              # index de sommets .LEV (jamais reordonnes)
        self.leaves = []                # secteurs .LEV (feuilles) du secteur Doom
        self.leaf_area = {}             # secteur .LEV -> aire (choix de enclosingSector/floorSector)
        self.doorwalls = 0

    @property
    def throw(self):
        return self.upper - self.lower

    def add_wall(self, w):
        if id(w) not in self._wset:
            self._wset.add(id(w))
            self.walls.append(w)

    def wall_indices(self, em):
        pos = {id(w): i for i, w in enumerate(em.walls)}
        return [pos[id(w)] for w in self.walls]


def close_doors(M, specials, slit=DOOR_SLIT):
    """Ferme les portes (plafond = sol + fente) : l'etat FICHIER d'une porte est ferme, le moteur
    la monte de `doorHeight` a l'execution. Une porte deja ouverte dans le WAD est laissee."""
    sects = M["sectors"]
    n = 0
    for d in specials.doors:
        s = sects[d["sector"]]
        if s.ceilh <= s.floorh:
            sects[d["sector"]] = s._replace(ceilh=s.floorh + slit)
            n += 1
    return n


# ----------------------------------------------------------------------------------------
# BSP : polygones de feuille
# ----------------------------------------------------------------------------------------
def point_side(nx, ny, ndx, ndy, px, py):
    """< 0 = cote AVANT (enfant droit, children[0]) ; > 0 = cote ARRIERE.
    Meme forme que R_PointOnSide (`right < left` -> avant)."""
    return ndx * (py - ny) - ndy * (px - nx)


def clip_half(poly, nx, ny, ndx, ndy, keep_front):
    """Sutherland-Hodgman contre un demi-plan defini par une ligne de partition."""
    if not poly:
        return []
    sgn = -1.0 if keep_front else 1.0

    def f(p):
        return sgn * point_side(nx, ny, ndx, ndy, p[0], p[1])

    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        fa, fb = f(a), f(b)
        if fa >= -EPS:
            out.append(a)
        if (fa > EPS and fb < -EPS) or (fa < -EPS and fb > EPS):
            t = fa / (fa - fb)
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return out


def leaf_polygons(M, margin=16.0):
    xs = [v[0] for v in M["vertices"]]
    ys = [v[1] for v in M["vertices"]]
    x0, x1 = min(xs) - margin, max(xs) + margin
    y0, y1 = min(ys) - margin, max(ys) + margin
    box = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    polys = [None] * len(M["subsectors"])
    nodes = M["nodes"]
    stack = [(len(nodes) - 1, box)]
    while stack:
        idx, poly = stack.pop()
        if idx & 0x8000:
            polys[idx & 0x7FFF] = poly
            continue
        nd = nodes[idx]
        for k, child in enumerate(nd.children):
            sub = clip_half(poly, nd.x, nd.y, nd.dx, nd.dy, keep_front=(k == 0))
            if len(sub) >= 3:
                stack.append((child, sub))
    # ... puis les segs de la feuille : ils bornent la partie "interieure" du convexe.
    V = M["vertices"]
    for si, ss in enumerate(M["subsectors"]):
        p = polys[si]
        if not p:
            continue
        for k in range(ss.first, ss.first + ss.count):
            sg = M["segs"][k]
            ax, ay = V[sg.v1]
            bx, by = V[sg.v2]
            if ax == bx and ay == by:
                continue
            p = clip_half(p, ax, ay, bx - ax, by - ay, keep_front=True)
            if len(p) < 3:
                break
        polys[si] = p
    return polys


def snap_poly(poly, quant=1.0):
    """Arrondit sur la grille entiere et supprime les sommets colineaires ou confondus."""
    pts = []
    for x, y in poly:
        q = (int(round(x / quant)) * quant, int(round(y / quant)) * quant)
        if not pts or (abs(q[0] - pts[-1][0]) > 1e-9 or abs(q[1] - pts[-1][1]) > 1e-9):
            pts.append(q)
    while len(pts) > 1 and pts[0] == pts[-1]:
        pts.pop()
    out = []
    n = len(pts)
    for i in range(n):
        a, b, c = pts[(i - 1) % n], pts[i], pts[(i + 1) % n]
        cr = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
        if abs(cr) > 0.5:
            out.append(b)
    return out if len(out) >= 3 else []


class Bsp:
    def __init__(self, M):
        self.nodes = M["nodes"]

    def leaf_at(self, x, y):
        idx = len(self.nodes) - 1
        while not (idx & 0x8000):
            nd = self.nodes[idx]
            idx = nd.children[0] if point_side(nd.x, nd.y, nd.dx, nd.dy, x, y) < 0 else nd.children[1]
        return idx & 0x7FFF


# ----------------------------------------------------------------------------------------
# convertisseur
# ----------------------------------------------------------------------------------------
# Distance de reference pour convertir la lumiere, en pas de `scalelight` (0..47, plus grand =
# plus pres). SlaveDriver n'a AUCUNE attenuation par la distance : la lumiere d'un sommet est
# peinte telle quelle. Doom, lui, RE-ECLAIRE ce qui est proche -- `level = startmap - j/2` borne a
# 0 (R_ExecuteSetViewSize) -- si bien qu'une piece a lightlevel 160 est plein feu quand on est
# dedans. Une rampe lineaire 0..255 -> 0..16 fige donc cette piece a 10/16, d'ou « la luminosite
# est plus faible que prevu ». On cuit a la place la valeur que Doom donnerait A UNE DISTANCE
# REPRESENTATIVE : celle ou l'on se tient dans la piece qu'on regarde.
LIGHT_REF_J = 32


def light_of(level):
    """lightlevel Doom 0..255 -> lumiere PowerSlave 0..16 (16 = le plus clair).

    Reprend la table de Doom : `lightnum = L >> 4`, `startmap = (15 - lightnum) * 4` (soit
    `((LIGHTLEVELS-1-i)*2)*NUMCOLORMAPS/LIGHTLEVELS` avec LIGHTLEVELS 16 et NUMCOLORMAPS 32),
    puis `level = startmap - j/2` borne a [0, 31]. La clarte vaut (32 - level)/32.
    Effet a LIGHT_REF_J=32 : 255->16, 192->16, 160->14, 128->10, 96->6, 64->2 ; l'ancienne rampe
    donnait 16, 12, 10, 8, 6, 4 -- plus sombre partout ou Doom est clair."""
    i = max(0, min(15, int(level) >> 4))
    lvl = max(0, min(31, (15 - i) * 4 - LIGHT_REF_J // 2))
    return max(0, min(16, int(round((32 - lvl) * 16.0 / 32.0))))


class DoomConverter:
    def __init__(self, M, sizes, cap_cells=CAP_CELLS, mobile=None, switch_lines=None):
        """`mobile` : {secteur Doom: MobileTag} (--mobile) ; `switch_lines` : {linedef: dict(channel,
        special)} des interrupteurs S dont il faut isoler la tuile (doom_specials.specials_of)."""
        self.M = M
        self.bsp = Bsp(M)
        self.em = Emitter()
        self.cap = cap_cells
        self.sizes = sizes                    # {nom de texture: (w, h)}
        self.pic = {}                         # ("tex"|"flat", nom) -> picnum
        self.picnames = []
        self.stats = Counter()
        self.mobile = mobile or {}
        self.switch_lines = switch_lines or {}
        # murs DOORWALL portes par une ligne ML_SECRET (0x20) : les DICTS, pas les index -- la
        # feuille est reordonnee apres emission (portails en tete, :523), cf. MobileTag (:94)
        self.secret_walls = []
        self._secret_ids = set()
        self.switch_hits = defaultdict(list)  # linedef -> [dict(leaf, walls, name, bot, top, P, Q)]
        self.switches = []                    # finalize_mobile : objets OT_SW1..4 a emettre
        # Polygones ETIQUETES + frontieres exactes : voir tools/doom2ps/adjacency.py. La
        # reciprocite des portails y est vraie par construction, ce qui supprime l'echantillonnage
        # de voisinage de la 1re version (58 portails a sens unique, 31 trous, MESURE sur E1M1).
        self.fpolys, self.boundary = adjacency.build(M)
        self.polys = {}
        for li, edges in self.boundary.items():
            ring = [P for (P, Q, tag, ta, tb, nb, nother) in edges]
            if len(ring) >= 3:
                self.polys[li] = ring
        self.leaf_sector = []
        for si, ss in enumerate(M["subsectors"]):
            sg = M["segs"][ss.first]
            ld = M["linedefs"][sg.line]
            sd = ld.right if sg.side == 0 else ld.left
            self.leaf_sector.append(M["sidedefs"][sd].sector if sd >= 0 else 0)
        # index des segs par feuille et par ligne canonique : dit EXACTEMENT si une sous-arete
        # est portee par un linedef (donc texturee) ou si c'est une corde de partition.
        V = M["vertices"]
        self.segidx = {}
        for si, ss in enumerate(M["subsectors"]):
            d = {}
            for k in range(ss.first, ss.first + ss.count):
                sg = M["segs"][k]
                ax, ay = V[sg.v1]
                bx, by = V[sg.v2]
                if (ax, ay) == (bx, by):
                    continue
                key = adjacency.line_key(ax, ay, bx - ax, by - ay)
                t0 = adjacency.param(key, ax, ay)
                t1 = adjacency.param(key, bx, by)
                d.setdefault(key, []).append((sg, min(t0, t1), max(t0, t1)))
            self.segidx[si] = d
        # feuille -> index de secteur .LEV (identite : une feuille = un secteur gen 1)
        # Les feuilles HORS CARTE ne sont pas converties. Le BSP de Doom partitionne tout le plan,
        # donc une feuille peut deborder dans le vide au-dela des murs a une face ; elle y coute un
        # secteur, des murs et des cellules pour rien, et ses frontieres, que rien ne borde,
        # ressortent en murs pleins que la carte n'a pas. MESURE sur E1M1 : 1 feuille sur 237.
        # Test : parite des croisements d'un rayon +x avec les linedefs a UNE SEULE face -- ce sont
        # eux, et eux seuls, qui separent le monde du vide.
        self.keep = [i for i in sorted(self.polys) if self._dans_la_carte(i)]
        self.remap = {li: k for k, li in enumerate(self.keep)}

    # -- textures ------------------------------------------------------------------------
    def picnum(self, kind, name):
        key = (kind, name)
        if key not in self.pic:
            self.pic[key] = len(self.picnames)
            self.picnames.append([kind, name])
        return self.pic[key]

    def tex_h(self, name):
        return self.sizes.get(name, (64, 128))[1] or 128

    def wall_tex(self, name, hauteur=None, voff=0):
        """Descripteur de placage E4.1c : la cellule porte la HAUTEUR REELLE du mur.

        E4.1b posait cv = hauteur de la texture, en comptant sur `tile_counts` pour tomber juste.
        Il ne tombe juste que si la hauteur du mur est un multiple de celle de la texture. Sinon
        la cellule posee mesure `H / tileHeight` pendant que la tuile porte `cv` unites de
        texture, et tout l'ecart part en etirement ou en repetition. MESURE sur E1M1 : hauteurs de
        cellule de 2 a 336 u, et 332 murs sous 64 u ou la texture entiere s'ecrase -- les
        contremarches surtout.

        Regle : `tileHeight = H / h` quand h divise H (chaque cellule porte alors UNE repetition
        entiere, a pleine resolution), sinon 1 -- une seule cellule qui porte toute la hauteur du
        mur, ce qui est EXACTEMENT ce que Doom dessine, en clippant la texture. Le cout est borne
        par CELL_HARD_MAX. MESURE : 112 tuiles de mur, 1 par secteur en mediane et 6 au maximum,
        pour un cache VDP1 de 28 -- toujours plus frugal que le retail (mediane 4, max 25).

        `voff` est la ligne de texture qui tombe en HAUT de la section, d'apres les regles de
        calage de Doom (r_segs.c) plus le `rowoffset` du sidedef."""
        p = self.picnum("tex", name)
        w, h = self.sizes.get(name, (64, 128))
        h = h or 128
        cu = max(CELL_MIN_U, min(CELL_MAX_U, int(w)))
        H = int(hauteur) if hauteur else 0
        if H > 0:
            th = (H // h) if (H >= h and H % h == 0) else 1
            th = max(th, int(math.ceil(H / float(CELL_HARD_MAX))))
            cv = max(1.0, H / float(th))
        else:
            cv = float(max(CELL_MIN_V, min(CELL_MAX_V, int(h))))
        return dict(mode="wall", pic=p, cu=cu, cv=cv, ncx=1, ncy=1,
                    ox=0, oz=0, dx=1, dz=0, yref=0, pu=0, pv=0, flipx=False, flipy=False,
                    keyed_cell=True, voff=int(voff) % h), p

    def flat_tex(self, name):
        p = self.picnum("flat", name)
        return dict(mode="flat", pic=p, cu=TILESIZE, cv=TILESIZE, ncx=1, ncy=1,
                    swap=False, sgnu=1, sgnv=1, flipx=False, flipy=False, doom_flat=True), p

    # -- aretes --------------------------------------------------------------------------
    def seg_on_edge(self, leaf, tag, ta, tb):
        """Le seg de `leaf` qui porte la sous-arete [ta, tb] de la ligne `tag`, ou None.
        Test EXACT : meme ligne canonique, et intervalle contenu dans celui du seg."""
        lo, hi = (ta, tb) if ta < tb else (tb, ta)
        for (sg, s0, s1) in self.segidx.get(leaf, {}).get(tag, ()):
            if s0 - 0.01 <= lo and hi <= s1 + 0.01:
                return sg
        return None

    # -- emission ------------------------------------------------------------------------
    def sky(self, sec):
        return sec.ceilpic == SKYFLAT

    def emit_wall(self, P, Q, bot, top, *, next_sector, tex, picnum, light, invisible,
                  centre=None, mob=None, open_height=None):
        """Emet un mur vertical. `mob` = [(MobileTag, 'top'|'bottom')] : les sommets de cette
        arete rejoignent le push block ; `open_height` : la tuile des cellules est refaite pour
        cette hauteur (etat OUVERT d'un mur qui grandit : rails de porte, contremarche d'ascenseur).
        Retourne la liste des index de murs emis (un mur trop grand est decoupe)."""
        if top - bot <= 0:
            return []
        quad = [(P[0], top, P[1]), (Q[0], top, Q[1]),
                (Q[0], bot, Q[1]), (P[0], bot, P[1])]
        pl = plane_of(quad)
        if pl is None:
            self.stats["quads_degeneres"] += 1
            return []
        # NORMALE VERS L'INTERIEUR. drawSector cull un mur quand (camera - v[0]) . normal < 0
        # (WALLS.C:1618-1622) : un quad monte dans le mauvais sens est invisible depuis son
        # propre secteur. Le sens depend du parcours P->Q et de la main du repere, donc on ne
        # le suppose pas -- on le teste contre le centre du morceau, comme orient_inward du
        # convertisseur Duke. (Trouve par tools/doom2ps/verif_doom.py : 1441 murs sur 1442.)
        if centre is not None:
            n = pl[0]
            qx = sum(p[0] for p in quad) / 4.0
            qz = sum(p[2] for p in quad) / 4.0
            if n[0] * (centre[0] - qx) + n[2] * (centre[1] - qz) < 0:
                quad = [quad[1], quad[0], quad[3], quad[2]]
                self.stats["quads_retournes"] += 1
        idx = self.em.add_wall(quad, next_sector=next_sector, picnum=picnum,
                               invisible=invisible, blocked=(next_sector < 0), light=light,
                               cap_cells=self.cap, stats=self.stats, tex=tex)
        if open_height and not invisible:
            self._rekey_height(idx, open_height)
        for (tag, role) in (mob or ()):
            self._tag_walls(idx, tag, role, top if role == "top" else bot)
        return idx

    # -- mobile ----------------------------------------------------------------------------
    def _tag_walls(self, idx, tag, role, yv):
        """Note les murs `idx` dans le push block `tag` et leurs sommets de l'arete `role`
        (y == yv, la hauteur de CETTE arete : haut ou bas du quad emis, jamais une recherche
        globale). `rigid` : TOUS les sommets, le mur glisse d'un bloc comme les faces de porte
        retail (4 coins mobiles sur KILENTRY, KARNAK, CAVERN) -- sa texture suit, rien ne
        s'ecrase. Un portail d'une porte recoit DOORWALL (relu par setDoorBlockBits
        AI.C:4294-4309) et SHORTOPENING (fente 1 u < 56)."""
        V, W = self.em.vertices, self.em.walls
        for wi in idx:
            w = W[wi]
            tag.add_wall(w)
            vs = list(w["v"])
            if w["firstVertex"] != 65535:
                vs += range(w["firstVertex"], w["lastVertex"] + 1)
            if role == "rigid":
                tag.verts.update(vs)
            else:
                tag.verts.update(vi for vi in vs if V[vi]["y"] == yv)
            if tag.kind == "door" and w["nextSector"] >= 0 and not (w["flags"] & WALLFLAG_DOORWALL):
                w["flags"] |= WALLFLAG_DOORWALL | WALLFLAG_SHORTOPENING
                tag.doorwalls += 1
                self.stats["doorwalls"] += 1
        self.stats["murs_mobiles"] += len(idx)

    def _tag_flat(self, idx, tag):
        """Sol ou plafond entier d'un secteur mobile : tous ses sommets bougent."""
        V, W = self.em.vertices, self.em.walls
        for wi in idx:
            w = W[wi]
            tag.add_wall(w)
            tag.verts.update(w["v"])
            if w["firstVertex"] != 65535:
                tag.verts.update(range(w["firstVertex"], w["lastVertex"] + 1))
        self.stats["plats_mobiles"] += len(idx)

    def _rekey_height(self, idx, open_h):
        """Refait la cle de tuile des cellules pour la hauteur `open_h` (E4.1c : la tuile est
        faite POUR la cellule ; ici la cellule du fichier fait 1 u et celle qu'on voit fait
        `open_h`). Les cles orphelines sont retirees par `compact_tiles`."""
        em = self.em
        for wi in idx:
            w = em.walls[wi]
            if not (w["flags"] & 0x01):
                continue
            n = w["tileLength"] * w["tileHeight"]
            for c in range(n):
                t = em.texture[w["textures"] + 2 * c + 1]
                key = tuple(em.tiles[t])
                if len(key) >= 7:
                    key = key[:5] + (round(float(open_h), 2), key[6])
                    em.texture[w["textures"] + 2 * c + 1] = em.tile(key)
        self.stats["cellules_rekeyees_ouvert"] += 1

    def _note_switch(self, sg, leaf, idx, name, bot, top, P, Q):
        if sg.line in self.switch_lines and sg.side == 0 and idx:
            self.switch_hits[sg.line].append(dict(leaf=leaf, walls=[self.em.walls[i] for i in idx],
                                                  name=name, bot=bot, top=top, P=P, Q=Q))

    def _dans_la_carte(self, li):
        ring = self.polys[li]
        cx = sum(p[0] for p in ring) / float(len(ring))
        cy = sum(p[1] for p in ring) / float(len(ring))
        DV, LD = self.M["vertices"], self.M["linedefs"]
        n = 0
        for ld in LD:
            ax, ay = DV[ld.v1]
            bx, by = DV[ld.v2]
            if ld.left >= 0 or (ay > cy) == (by > cy):
                continue
            if ax + (cy - ay) / float(by - ay) * (bx - ax) > cx:
                n += 1
        if n % 2 == 0:
            self.stats["feuilles_hors_carte"] += 1
            return False
        return True

    def emit_leaf(self, leaf):
        M = self.M
        poly = self.polys[leaf]
        sec = M["sectors"][self.leaf_sector[leaf]]
        fh, ch = sec.floorh, sec.ceilh
        light = light_of(sec.light)
        first = len(self.em.walls)

        cen = (sum(p[0] for p in poly) / len(poly), sum(p[1] for p in poly) / len(poly))
        for (P, Q, tag, ta, tb, nb, nother) in self.boundary[leaf]:
            if nother > 1:
                self.stats["frontieres_ambigues"] += 1
            self.emit_edge(leaf, P, Q, tag, ta, tb, nb, sec, fh, ch, light, cen)

        pxz = list(poly)
        if area2(pxz) < 0:
            pxz = pxz[::-1]
        ms = self.mobile.get(self.leaf_sector[leaf])
        ftex, fpic = self.flat_tex(sec.floorpic)
        fidx = self.em.add_flat(pxz, lambda x, z: fh, is_floor=True, picnum=fpic,
                                parallax=False, light=light, stats=self.stats, tex=ftex)
        if self.sky(sec):
            cidx = self.em.add_flat(pxz, lambda x, z: ch, is_floor=False, picnum=0,
                                    parallax=True, light=light, stats=self.stats, tex=None)
        else:
            ctex, cpic = self.flat_tex(sec.ceilpic)
            cidx = self.em.add_flat(pxz, lambda x, z: ch, is_floor=False, picnum=cpic,
                                    parallax=False, light=light, stats=self.stats, tex=ctex)
        if ms is not None:
            # porte : le plafond monte ; ascenseur / sol : le sol descend
            self._tag_flat(cidx if ms.kind == "door" else fidx, ms)
            ms.leaves.append(self.remap[leaf])
            ms.leaf_area[self.remap[leaf]] = abs(area2(pxz)) / 2.0

        # PORTAILS EN TETE : findDoorways fait `if (nextSector == -1) return;` (WALLS.C:1740-1743).
        # 0 des 8 211 secteurs retail ne viole la regle ; 361 de nos 440 la violaient au 2e disque
        # Duke, et la visibilite ne quittait jamais le secteur de depart.
        seg = self.em.walls[first:]
        self.em.walls[first:] = ([w for w in seg if w["nextSector"] >= 0]
                                 + [w for w in seg if w["nextSector"] == -1])
        last = len(self.em.walls) - 1

        acc = [0, 0, 0]
        cnt = lvl = nl = 0
        for wi in range(first, last + 1):
            w = self.em.walls[wi]
            for vi in w["v"]:
                v = self.em.vertices[vi]
                acc[0] += v["x"]
                acc[1] += v["y"]
                acc[2] += v["z"]
                cnt += 1
                if w["normal"][1] > 0:
                    lvl += v["y"]
                    nl += 1
        if cnt == 0:
            self.stats["secteurs_vides"] += 1
            cnt = 1
        self.em.sectors.append(dict(
            object=0, center=[acc[0] // cnt, acc[1] // cnt, acc[2] // cnt],
            floorLevel=(lvl // nl) if nl else fh,
            firstWall=first, lastWall=last, light=SECTOR_LIGHT, flags=0,
            cutIndex=0, cutChannel=0, pad=0))

    def emit_edge(self, leaf, P, Q, tag, ta, tb, nb, sec, fh, ch, light, cen):
        M = self.M
        sg = self.seg_on_edge(leaf, tag, ta, tb)
        nbi = self.remap.get(nb, -1) if nb is not None and nb >= 0 else -1

        # ROLES MOBILES (--mobile). `ms` = ce secteur bouge, `mn` = le voisin bouge. Porte : tout
        # ce dont le HAUT est le plafond de la porte monte (murs de la porte, portails, et le bas
        # des linteaux du voisin) ; ascenseur / sol : tout ce dont le BAS est le sol de
        # l'ascenseur descend (murs de l'ascenseur, portails, et le haut des contremarches du
        # voisin). Les linteaux ne suivent JAMAIS un sol : le linteau [plafond voisin, plafond]
        # est fixe dans Doom quelle que soit la position de la plate-forme.
        ms = self.mobile.get(self.leaf_sector[leaf])
        mn = (self.mobile.get(self.leaf_sector[nb])
              if (nb is not None and nb >= 0 and nb in self.remap) else None)
        if mn is ms:
            mn = None
        # deux feuilles du MEME secteur Doom : rien ne les separe quand il bouge
        same = (nb is not None and nb >= 0 and nb in self.remap
                and self.leaf_sector[nb] == self.leaf_sector[leaf])

        def own(bot_, top_):
            if ms is None:
                return []
            if ms.kind == "door":
                return [(ms, "top")] if top_ == ch else []
            return [(ms, "bottom")] if bot_ == fh else []

        if sg is None:
            # arete de partition pure : portail plein, invisible
            if nbi < 0:
                self.stats["bords_de_carte"] += 1
                self.emit_wall(P, Q, fh, ch, next_sector=-1, tex=None,
                               picnum=self.picnum("tex", "-"), light=light, invisible=True,
                               centre=cen, mob=own(fh, ch))
            else:
                self.emit_wall(P, Q, fh, ch, next_sector=nbi, tex=None, picnum=0,
                               light=light, invisible=True, centre=cen, mob=own(fh, ch))
                self.stats["portails_chord"] += 1
            return

        ld = M["linedefs"][sg.line]
        sd_i = ld.right if sg.side == 0 else ld.left
        op_i = ld.left if sg.side == 0 else ld.right
        side = M["sidedefs"][sd_i] if sd_i >= 0 else None
        other = M["sidedefs"][op_i] if op_i >= 0 else None

        # CALAGE VERTICAL DE DOOM (r_segs.c). `*texturemid` est la hauteur monde de la ligne 0
        # de la texture ; la ligne qui tombe en haut d'une section vaut donc `mid - sommet`, plus
        # le `rowoffset` du sidedef. Sans cela une texture sur deux glisse : 143 linedefs sur 475
        # d'E1M1 portent un drapeau de calage.
        pegtop = bool(ld.flags & 0x0008)               # ML_DONTPEGTOP
        pegbot = bool(ld.flags & 0x0010)               # ML_DONTPEGBOTTOM
        yoff = side.yoff if side else 0

        if other is None or op_i < 0:
            name = side.middle if side else "-"
            name = name if name != "-" else "BROWN1"
            h = self.tex_h(name)
            # Un mur mobile ne grandit JAMAIS : ses tuiles sont faites pour sa hauteur et un mur
            # qui change de taille les ecrase. Sa texture suit le bord que Doom lui donne pour
            # ancre : ancre au bord FIXE -> mur fixe a sa pleine course, le surplus cache par le
            # sol ou le plafond de sa feuille (dessines apres les murs, WALLS.C:1610 + ordre
            # d'emission) ; ancre au bord MOBILE -> dalle rigide qui glisse avec lui.
            bot_, top_, mob = fh, ch, own(fh, ch)
            if ms is not None and ms.kind == "door":
                # rail de porte : DONTPEGBOTTOM (le cas DOORTRAK) = fixe a la hauteur ouverte ;
                # sinon ancre au plafond mobile = dalle qui part sous le sol
                if pegbot:
                    bot_, top_, mob = fh, fh + ms.throw, None
                else:
                    bot_, top_, mob = ch - ms.throw, ch, [(ms, "rigid")]
                self.stats["rails_porte"] += 1
            elif ms is not None:
                # mur d'ascenseur / de sol qui descend de ms.throw : ancre au plafond (defaut) =
                # fixe jusqu'au bas de course ; DONTPEGBOTTOM = dalle qui descend avec le sol
                if pegbot:
                    bot_, top_, mob = fh, ch + ms.throw, [(ms, "rigid")]
                else:
                    bot_, top_, mob = fh - ms.throw, ch, None
                self.stats["murs_cage_ascenseur"] += 1
            # une face : `mid` = plafond (defaut) ou sol + h (DONTPEGBOTTOM) ; v = ligne au sommet
            mid = (fh + h) if pegbot else ch
            v = ((mid - top_) % h) + yoff
            tex, pic = self.wall_tex(name, top_ - bot_, v)
            idx = self.emit_wall(P, Q, bot_, top_, next_sector=-1, tex=tex, picnum=pic,
                                 light=light, invisible=False, centre=cen, mob=mob)
            self._note_switch(sg, leaf, idx, name, fh, ch, P, Q)
            self.stats["murs_pleins"] += 1
            return

        # Les hauteurs du voisin viennent de la FEUILLE voisine reelle, pas du sidedef oppose
        # du linedef. Les deux ne coincident pas toujours : la frontiere geometrique exacte peut
        # separer deux feuilles dont l'une n'a pas de seg pour ce linedef. Prendre le sidedef
        # rendait l'ouverture ASYMETRIQUE -- un cote la voyait fermee (mur plein) et l'autre
        # ouverte (portail), soit 7 portails a sens unique sur E1M1. La feuille voisine, elle,
        # donne le meme couple de hauteurs des deux cotes par construction.
        nsec = M["sectors"][self.leaf_sector[nb]] if (nb is not None and nb >= 0) else             M["sectors"][other.sector]
        nfh, nch = nsec.floorh, nsec.ceilh
        bot, top = max(fh, nfh), min(ch, nch)

        mn_lift = mn is not None and mn.kind != "door"
        mn_door = mn is not None and mn.kind == "door"

        lift_self = ms is not None and ms.kind != "door" and not same
        if lift_self and nfh > ms.lower:
            # paroi de la cage vue DE l'ascenseur une fois descendu : fixe du bas de course au sol
            # du voisin. Doom ancre cette texture `lower` au sol du voisin (defaut) ou a notre
            # plafond (DONTPEGBOTTOM), deux bords fixes ; notre sol cache ce qui est dessous.
            # Remplace la contremarche dont le bas suivait le sol (etiree en descendant) et la
            # fente de 1 u du cote voisin (tournee vers le voisin, jamais vue de la cage).
            name = side.lower if side and side.lower != "-" else "BROWN1"
            h = self.tex_h(name)
            hb = min(nfh, ch)
            v = ((ch - hb) % h if pegbot else 0) + yoff
            tex, pic = self.wall_tex(name, hb - ms.lower, v)
            idx = self.emit_wall(P, Q, ms.lower, hb, next_sector=-1, tex=tex, picnum=pic,
                                 light=light, invisible=False, centre=cen, mob=None)
            self._note_switch(sg, leaf, idx, name, ms.lower, hb, P, Q)
            self.stats["parois_cage"] += 1
        elif nfh > fh:                                 # marche montante : contremarche
            name = side.lower if side and side.lower != "-" else "BROWN1"
            h = self.tex_h(name)
            hb = min(nfh, ch)
            # bas : `mid` = sol du voisin (defaut) ou plafond de devant (DONTPEGBOTTOM)
            v = ((ch - hb) % h if pegbot else 0) + yoff
            tex, pic = self.wall_tex(name, hb - fh, v)
            mob = own(fh, hb)
            if mn_lift and hb == nfh:
                # plate-forme voisine plus haute que nous : Doom ancre la texture a SON sol, qui
                # descend -> dalle rigide, ce qui passe sous notre sol est cache par lui
                mob.append((mn, "top") if ms is not None else (mn, "rigid"))
            idx = self.emit_wall(P, Q, fh, hb, next_sector=-1, tex=tex, picnum=pic,
                                 light=light, invisible=False, centre=cen, mob=mob)
            self._note_switch(sg, leaf, idx, name, fh, hb, P, Q)
            self.stats["contremarches"] += 1
        if nch < ch:                                   # linteau
            if self.sky(sec) and self.sky(nsec):
                pass                                   # deux ciels : rien (Doom ne dessine rien)
            else:
                name = side.upper if side and side.upper != "-" else "BROWN1"
                h = self.tex_h(name)
                hb = max(nch, fh)
                top_ = ch
                if mn_door and hb == nch and ms is None and not self.sky(sec):
                    # FACE DE PORTE : la recette retail, une dalle rigide qui monte avec la porte
                    # (4 coins mobiles). Doom ancre ce `upper` au plafond de la porte (defaut), il
                    # glisse avec elle : la dalle est allongee de la course pour que son haut, cache
                    # par notre plafond, ne descende jamais sous lui une fois la porte ouverte.
                    top_ = ch + mn.throw
                    mob = [(mn, "rigid")]
                    self.stats["faces_porte_rigides"] += 1
                else:
                    mob = ([(ms, "top")] if (ms is not None and ms.kind == "door") else []) + (
                        [(mn, "bottom")] if (mn_door and hb == nch) else [])
                    if mn_door and hb == nch:
                        # sous un plafond de ciel rien ne cache le haut d'une dalle : ancien mode
                        self.stats["faces_porte_non_rigides"] += 1
                # haut : `mid` = plafond de devant (DONTPEGTOP) ou plafond du voisin + h (defaut)
                mid = ch if pegtop else hb + h
                v = ((mid - top_) % h) + yoff
                tex, pic = self.wall_tex(name, top_ - hb, v)
                idx = self.emit_wall(P, Q, hb, top_, next_sector=-1, tex=tex, picnum=pic,
                                     light=light, invisible=False, centre=cen, mob=mob)
                self._note_switch(sg, leaf, idx, name, hb, top_, P, Q)
                self.stats["linteaux"] += 1

        if top > bot and nbi >= 0:
            mob = own(bot, top)
            if lift_self and nfh >= fh:
                # voisin au niveau de la plate-forme (ou plus haut) : le bas de l'ouverture est
                # son sol, fixe ; la paroi de cage ci-dessus bouche ce qui passe dessous
                mob = [m for m in mob if m[0] is not ms]
            if mn_door and top == nch:
                mob.append((mn, "top"))
            if mn_lift and bot == nfh and nfh > fh:
                mob.append((mn, "bottom"))
            idx = self.emit_wall(P, Q, bot, top, next_sector=nbi, tex=None, picnum=0,
                                 light=light, invisible=True, centre=cen, mob=mob)
            if ld.flags & 0x0020:                      # ML_SECRET : un monstre ne l'ouvre pas
                for wi in idx:
                    w = self.em.walls[wi]
                    if (w["flags"] & WALLFLAG_DOORWALL) and id(w) not in self._secret_ids:
                        self._secret_ids.add(id(w))
                        self.secret_walls.append(w)
            self.stats["portails_ligne"] += 1
        elif top <= bot and nbi >= 0 and (
                (ms is not None and ms.kind != "door" and bot == fh)
                or (mn_lift and bot == nfh)):
            # ouverture fermee par le SOL de l'ascenseur (plafond du voisin bas = sol de la
            # plate-forme, ex. E1M1 58|70) : portail-fente 1 u sous le sol, dont le bas descend
            # avec l'ascenseur. Le linteau au-dessus est deja emis et reste fixe.
            mob = ([(ms, "bottom")] if (ms is not None and ms.kind != "door" and bot == fh)
                   else []) + ([(mn, "bottom")] if (mn_lift and bot == nfh) else [])
            self.emit_wall(P, Q, bot - DOOR_SLIT, bot, next_sector=nbi, tex=None, picnum=0,
                           light=light, invisible=True, centre=cen, mob=mob)
            self.stats["portails_fente"] += 1
        elif top <= bot:
            # ouverture fermee (porte baissee, mur plein a deux faces) : bouchon plein
            name = side.middle if side and side.middle != "-" else (
                side.lower if side and side.lower != "-" else "BROWN1")
            h = self.tex_h(name)
            v = ((h - (ch - fh)) % h if pegbot else 0) + yoff
            tex, pic = self.wall_tex(name, ch - fh, v)
            idx = self.emit_wall(P, Q, fh, ch, next_sector=-1, tex=tex, picnum=pic,
                                 light=light, invisible=False, centre=cen, mob=own(fh, ch))
            self._note_switch(sg, leaf, idx, name, fh, ch, P, Q)
            self.stats["ouvertures_fermees"] += 1

    def post_flags(self):
        W, S, V = self.em.walls, self.em.sectors, self.em.vertices
        for s in S:
            for wi in range(s["firstWall"], s["lastWall"] + 1):
                w = W[wi]
                if w["nextSector"] == -1 or w["normal"][1] != 0:
                    continue
                ys = [V[i]["y"] for i in w["v"]]
                h = max(ys) - min(ys)
                if 1.0 < h < PLAYER_FIT_HEIGHT:
                    w["flags"] |= 0x1000                # SHORTOPENING
                    self.stats["shortopening"] += 1
                if min(ys) - s["floorLevel"] > PLAYER_STEP_HEIGHT:
                    w["flags"] |= WALLFLAG_BLOCKED      # marche > 24 : voir PLAYER_STEP_HEIGHT
                    self.stats["marche_haute"] += 1
                if s["floorLevel"] - S[w["nextSector"]]["floorLevel"] > 320:
                    w["flags"] |= 0x800                 # CLIFFBNDRY
                    self.stats["cliffbndry"] += 1

    def build(self):
        for li in self.keep:
            self.emit_leaf(li)
        self.post_flags()
        if self.mobile or self.switch_lines:
            self.finalize_mobile()
        return self.em

    # -- mobile : interrupteurs et compaction des tuiles -----------------------------------
    def finalize_mobile(self):
        """Interrupteurs S : la tuile OFF doit etre UNIQUE dans la feuille (`constructSwitch`
        cherche `level_texture[t] == ourTile` dans les murs de `sectorNm`, AI2.C:606-632) et la
        tuile ON existe pour l'animation. On donne au mur de l'interrupteur une cle de tuile
        DEDIEE (cle E4.1c + marqueur 'sw') et on fabrique la cle ON (SW2xxx) de la meme cellule ;
        doomtiles.py lit `key[0], key[5], key[6]` et ignore le reste. Un type OT_SW1..4 par paire
        (OFF, ON) : `level_sequenceMap[type]` est la seule sequence d'un type (AI2.C:598)."""
        em = self.em
        pos = {id(w): i for i, w in enumerate(em.walls)}
        pairs = {}
        for line, hits in sorted(self.switch_hits.items()):
            info = self.switch_lines[line]
            cand = [h for h in hits if h["name"].startswith(("SW1", "SW2"))] or hits
            done_leaf = set()
            for h in cand:
                if h["leaf"] in done_leaf:
                    continue
                done_leaf.add(h["leaf"])
                w = h["walls"][0]
                wi = pos[id(w)]
                if not (w["flags"] & 0x01):
                    self.stats["interrupteur_sur_mur_a_faces"] += 1
                    continue
                n = w["tileLength"] * w["tileHeight"]
                if n > 1 or len(h["walls"]) > 1:
                    self.stats["interrupteur_multi_cellules"] += 1
                t = em.texture[w["textures"] + 1]
                key = tuple(em.tiles[t])
                off_name = self.picnames[key[0]][1]
                on_name = ("SW2" + off_name[3:]) if off_name.startswith("SW1") else (
                    ("SW1" + off_name[3:]) if off_name.startswith("SW2") else off_name)
                p_on = self.picnum("tex", on_name)
                off_key = key + ("sw",)
                on_key = (p_on,) + key[1:] + ("sw",)
                t_off = em.tile(off_key)
                t_on = em.tile(on_key)
                for c in range(n):
                    em.texture[w["textures"] + 2 * c + 1] = t_off
                pair = (t_off, t_on)
                if pair not in pairs:
                    if len(pairs) >= 4:
                        raise SystemExit("plus de 4 paires de tuiles d'interrupteur : "
                                         "OT_SW1..OT_SW4 seulement (SLEVEL.H)")
                    pairs[pair] = sp.OT_SW1 + len(pairs)
                # orifice : milieu du mur, a hauteur d'oeil (le press mesure < 40 u depuis le
                # POINT D'IMPACT du rayon, SRUINS.C:843-853 ; l'oeil est a sol + 41)
                P, Q = h["P"], h["Q"]
                oy = h["bot"] + sp.PLAYER_EYE
                oy = max(h["bot"], min(h["top"], oy))
                self.switches.append(dict(
                    line=line, channel=info["channel"], special=info["special"],
                    leaf=h["leaf"], leaf_sector=self.remap[h["leaf"]], wall=wi,
                    tile_off=t_off, tile_on=t_on, type=pairs[pair],
                    texture_off=off_name, texture_on=on_name,
                    orifice=[int(round((P[0] + Q[0]) / 2.0)), int(oy),
                             int(round((P[1] + Q[1]) / 2.0))]))
                self.stats["interrupteurs"] += 1
        for line in self.switch_lines:
            if line not in self.switch_hits:
                self.stats["interrupteur_sans_mur"] += 1
        # la tuile ON n'est referencee que par les chunks de la sequence d'interrupteur : a garder
        remap = self.compact_tiles(extra={s["tile_on"] for s in self.switches}
                                   | {s["tile_off"] for s in self.switches})
        for s in self.switches:
            s["tile_off"] = remap[s["tile_off"]]
            s["tile_on"] = remap[s["tile_on"]]

    def compact_tiles(self, extra=()):
        """Retire les cles de tuile qu'aucune cellule ni face ne reference plus (rekey des murs
        mobiles, interrupteurs) : la geometrie doit tenir en <= 255 - tileBase tuiles u8.
        `extra` = index a garder quoi qu'il arrive (tuile ON des interrupteurs)."""
        em = self.em
        used = {em.texture[i] for i in range(1, len(em.texture), 2)}
        used |= {f["tile"] for f in em.faces}
        used |= set(extra)
        keep = [t for t in range(len(em.tiles)) if t in used]
        remap = {t: k for k, t in enumerate(keep)}
        if len(keep) != len(em.tiles):
            self.stats["tuiles_orphelines_retirees"] += len(em.tiles) - len(keep)
            em.tiles = [em.tiles[t] for t in keep]
            em._tile_index = {tuple(k): i for i, k in enumerate(em.tiles)}
            for i in range(1, len(em.texture), 2):
                em.texture[i] = remap[em.texture[i]]
            for f in em.faces:
                f["tile"] = remap[f["tile"]]
        return remap


def push_blocks(conv, tags):
    """sPBType / sPBVertex / PBWall (SLEVEL.H:97-113) pour chaque secteur mobile, dans l'ordre
    des `tags`. -> (pushBlocks, PBVert, PBWall, pb_index {secteur Doom: pb}).
    `enclosingSector` = la plus grande feuille (position du son, OBJECT.C:550-561) ;
    `floorSector` = cette feuille pour un ascenseur / sol (les sprites qui s'y tiennent suivent,
    SPRITE.C:873-887 -- une seule feuille par push block), -1 pour une porte ;
    `dx = dy = dz = 0` ; les plages start..end sont INCLUSIVES."""
    pbs, pbv, pbw, index = [], [], [], {}
    for tag in tags:
        if not tag.walls:
            conv.stats["push_block_vide"] += 1
            continue
        w0 = len(pbw)
        pbw.extend(tag.wall_indices(conv.em))
        v0 = len(pbv)
        run = []
        for vi in sorted(tag.verts) + [None]:
            if run and (vi is None or vi != run[-1] + 1 or len(run) >= PBVERT_MAX):
                pbv.append(dict(vStart=run[0], vNm=len(run), flags=0))
                run = []
            if vi is not None:
                run.append(vi)
        if len(pbv) == v0:
            conv.stats["push_block_sans_sommet"] += 1
            continue
        big = max(tag.leaves, key=lambda s: tag.leaf_area.get(s, 0)) if tag.leaves else 0
        index[tag.sector] = len(pbs)
        pbs.append(dict(enclosingSector=big, startWall=w0, endWall=len(pbw) - 1,
                        startVertex=v0, endVertex=len(pbv) - 1,
                        floorSector=(-1 if tag.kind == "door" else big), dx=0, dy=0, dz=0))
    return pbs, pbv, pbw, index


# ----------------------------------------------------------------------------------------
# criteres
# ----------------------------------------------------------------------------------------
def check(em, conv):
    crit = {}

    def put(k, ok, **x):
        crit[k] = dict(ok=bool(ok), **x)

    put("secteurs_sous_limite", len(em.sectors) <= MAXNMSECTORS,
        n=len(em.sectors), limite=MAXNMSECTORS)
    put("murs_sous_limite", len(em.walls) <= MAXNMWALLS, n=len(em.walls), limite=MAXNMWALLS)

    bad = []
    for si, s in enumerate(em.sectors):
        seen_solid = False
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            if em.walls[wi]["nextSector"] == -1:
                seen_solid = True
            elif seen_solid:
                bad.append(si)
                break
    put("portails_en_tete", not bad, secteurs_fautifs=bad[:5], n=len(bad))

    nf = [sum(1 for wi in range(s["firstWall"], s["lastWall"] + 1)
              if em.walls[wi]["normal"][1] > 0) for s in em.sectors]
    nc = [sum(1 for wi in range(s["firstWall"], s["lastWall"] + 1)
              if em.walls[wi]["normal"][1] < 0) for s in em.sectors]
    put("un_sol_un_plafond", all(x == 1 for x in nf) and all(x == 1 for x in nc),
        sols_non_1=sum(1 for x in nf if x != 1), plafonds_non_1=sum(1 for x in nc if x != 1))

    px = [abs(v["x"]) for v in em.vertices] + [abs(v["z"]) for v in em.vertices]
    put("dans_les_bornes_monde", max(px) < WORLD_LIMIT, max_xz=max(px), limite=WORLD_LIMIT)

    put("motifs_sous_8", all(em.texture[i] < 8 for i in range(0, len(em.texture), 2)),
        n=len(em.texture) // 2)

    par = [w for w in em.walls if w["flags"] & 0x40]
    put("parallax_sans_invisible", all((w["flags"] & 0x02) == 0 for w in par), n=len(par))

    cells = [w["tileLength"] * w["tileHeight"] for w in em.walls if w["flags"] & 0x01]
    faces = [w["lastFace"] - w["firstFace"] + 1 for w in em.walls if w["firstFace"] >= 0]
    put("budget_slave", (max(cells or [0]) <= CAP_CELLS and max(faces or [0]) <= CAP_CELLS),
        max_cellules=max(cells or [0]), max_faces=max(faces or [0]), cap=CAP_CELLS)

    put("sommets_par_mur", all((w["lastVertex"] - w["firstVertex"]) < 700
                               for w in em.walls if w["firstVertex"] != 65535),
        limite=700)

    put("voisins_reciproques",
        all(0 <= w["nextSector"] < len(em.sectors) for w in em.walls if w["nextSector"] >= 0),
        n=sum(1 for w in em.walls if w["nextSector"] >= 0))
    return crit


# Specials de Doom qui font MONTER une porte. La pre-passe ci-dessous ne s'applique qu'aux
# secteurs deja fermes (plafond == sol), ou ces specials ne peuvent designer qu'une porte.
DOOR_SPECIALS = {1, 2, 3, 4, 16, 26, 27, 28, 29, 31, 32, 33, 34, 46, 61, 63, 75, 76, 86, 90,
                 99, 103, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118,
                 133, 135, 137}


def open_doors(M):
    """Ouvre les portes de Doom, qui sont FERMEES dans la geometrie statique du WAD.

    Une porte de Doom est un secteur dont le plafond est colle au sol ; elle ne s'ecarte qu'a
    l'execution. Converti tel quel, ce secteur devient un bouchon plein et scelle tout ce qu'il y a
    derriere. MESURE sur E1M1 : 105 secteurs atteignables sur 236 (44 % de l'aire) portes fermees.

    On applique la regle de Doom elle-meme -- `EV_VerticalDoor` pose
    `topheight = P_FindLowestCeilingSurrounding(sec) - 4` -- donc la geometrie produite est
    exactement celle de la porte grande ouverte, pas une approximation.
    """
    sects, lines, sides = M["sectors"], M["linedefs"], M["sidedefs"]
    ferme = {i for i, s in enumerate(sects) if s.ceilh == s.floorh}
    if not ferme:
        return 0
    voisins = defaultdict(set)
    porte = set()
    for ld in lines:
        ss = [sides[sd].sector for sd in (ld.right, ld.left) if 0 <= sd < len(sides)]
        for sn in ss:
            if sn not in ferme:
                continue
            if ld.special in DOOR_SPECIALS:
                porte.add(sn)
            voisins[sn].update(o for o in ss if o != sn)
    n = 0
    for sn in sorted(porte):
        haut = min((sects[o].ceilh for o in voisins[sn]), default=None)
        if haut is None or haut - 4 <= sects[sn].floorh:
            continue
        sects[sn] = sects[sn]._replace(ceilh=haut - 4)
        n += 1
    return n


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=os.path.join(os.path.dirname(ROOT), "Mimas", "cd", "data",
                                                  "DOOM1.WAD"))
    ap.add_argument("--map", default="E1M1")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "doom2ps", "e1m1_geom3d.json"))
    ap.add_argument("--cap-cells", type=int, default=CAP_CELLS)
    ap.add_argument("--mobile", action="store_true",
                    help="portes FERMEES + course, push blocks, interrupteurs (SPEC_CONVERTER 5.1) ; "
                         "la sortie gagne une cle `mobile`")
    ap.add_argument("--static-doors", action="store_true",
                    help="portes ouvertes en dur (open_doors, controle visuel) -- le defaut sans --mobile")
    a = ap.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")

    W = wadmod.Wad(a.wad)
    M = wadmod.read_map(W, a.map)
    sizes = {nm: (t["width"], t["height"]) for nm, t in W.textures().items()}
    print(f"doom2ps E3 : {a.map} de {os.path.basename(a.wad)} -- "
          f"{len(M['sectors'])} secteurs Doom, {len(M['subsectors'])} feuilles BSP, "
          f"{len(M['segs'])} segs")
    specials = tags = None
    if a.mobile and not a.static_doors:
        specials = sp.specials_of(M)
        nportes = close_doors(M, specials)
        tags = {}
        for d in specials.doors + specials.lifts + specials.floors:
            tags[d["sector"]] = MobileTag(d["sector"], d["kind"], d["lower"], d["upper"])
        print(f"  mobile : {len(specials.doors)} portes fermees (fente {DOOR_SLIT} u, {nportes} "
              f"plafonds abaisses), {len(specials.lifts)} ascenseurs, {len(specials.floors)} sols, "
              f"{len(specials.wswitch)} lignes W, {len(specials.sswitch)} lignes S, "
              f"{len(specials.exits)} sorties, {len(specials.damage)} secteurs a degats"
              + (f" ; ignores : {specials.ignored}" if specials.ignored else ""))
        for d in specials.doors + specials.lifts + specials.floors:
            print(f"    secteur {d['sector']:3d} {d['kind']:13s} course {d['lower']} .. {d['upper']}"
                  f" ({d['upper'] - d['lower']} u)")
        switch_lines = {s["line"]: s for s in specials.sswitch}
        conv = DoomConverter(M, sizes, a.cap_cells, mobile=tags, switch_lines=switch_lines)
    else:
        nportes = open_doors(M)
        if nportes:
            print(f"  {nportes} portes ouvertes (plafond = plus bas plafond voisin - 4, "
                  f"regle de EV_VerticalDoor)")
        conv = DoomConverter(M, sizes, a.cap_cells)
    vides = len(M["subsectors"]) - len(conv.keep)
    if vides:
        print(f"  {vides} feuilles degenerees ecartees")
    em = conv.build()
    print(f"  {len(em.sectors)} secteurs, {len(em.walls)} murs, {len(em.vertices)} sommets, "
          f"{len(em.faces)} faces, {len(em.texture)} octets de texture, "
          f"{len(em.vertexLight)} lumieres, {len(em.tiles)} tuiles distinctes")
    for k in sorted(conv.stats):
        print(f"    {k} : {conv.stats[k]}")

    # depart du joueur (THING type 1)
    st = next((t for t in M["things"] if t.type == 1), None)
    if st is None:
        raise SystemExit("pas de depart joueur (thing type 1)")
    lf = conv.bsp.leaf_at(st.x, st.y)
    sect = conv.remap.get(lf, 0)
    # assemble.py recalcule px, pz depuis build_xy : px = bx // 8, pz = -(by // 8)
    build_xy = [int(st.x) * 8, -int(st.y) * 8]
    # Angle. `suckSpriteParams` fait `angle = normalizeAngle(short * 5760)` (OBJECT.C:194) et
    # F(90) = 5898240 = 1024 * 5760, donc **l'unite du .LEV est 360/4096 de degre**. Et l'init du
    # joueur retire 90 degres (`angle - F(90)`, AI.C:51), qu'il faut donc ajouter ici.
    # Doom : 0 = est, sens trigonometrique ; nous : vel.x = cos(angle), vel.z = sin(angle)
    # (AI.C:528-529) avec Z = +y_doom = nord -> meme sens, pas de correction de repere.
    angle = int(round(((st.angle + 90) % 360) * 4096.0 / 360.0))

    crit = check(em, conv)
    crit["depart_present"] = dict(ok=True, depart=dict(sector_gen1=sect, build_xy=build_xy,
                                                       angle_doom=st.angle, angle_lev=angle,
                                                       feuille=lf))
    out = dict(format="doom2ps/e3-geom3d v1",
               source=dict(wad=os.path.basename(a.wad), map=a.map),
               conventions=dict(repere="X = x_doom, Y = z_doom, Z = y_doom (1:1)",
                                morceaux="feuilles du BSP du WAD",
                                tuile=f"TILESIZE {TILESIZE}",
                                placage="E4.1b : cellule = une repetition de la texture"),
               stats=dict(conv.stats), criteres=crit, tiles=em.tiles,
               picnames=conv.picnames,
               sectors=em.sectors, walls=em.walls, vertices=em.vertices, faces=em.faces,
               texture=em.texture, vertexLight=em.vertexLight)
    if tags is not None:
        order = [tags[s] for s in sorted(tags)]
        pbs, pbv, pbw, pb_index = push_blocks(conv, order)
        crit["push_blocks_complets"] = dict(
            ok=len(pbs) == len(order), n=len(pbs), attendus=len(order),
            sans_mur=[t.sector for t in order if not t.walls])
        crit["interrupteurs_trouves"] = dict(
            ok=conv.stats.get("interrupteur_sans_mur", 0) == 0,
            n=len(conv.switches), lignes=[s["line"] for s in conv.switches])
        out["mobile"] = dict(
            door_slit=DOOR_SLIT,
            specials={k: v for k, v in specials._asdict().items()},
            tags=[dict(sector=t.sector, kind=t.kind, lower=t.lower, upper=t.upper,
                       throw=t.throw, pb=pb_index.get(t.sector), leaves=t.leaves,
                       walls=t.wall_indices(em), nverts=len(t.verts), doorwalls=t.doorwalls)
                  for t in order],
            pushBlocks=pbs, PBVert=pbv, PBWall=pbw,
            pb_index={str(k): v for k, v in pb_index.items()},
            switches=conv.switches,
            secret_walls=sorted(i for i, w in enumerate(em.walls) if id(w) in conv._secret_ids),
            damage_leaves=[dict(sector=d["sector"], hp=d["hp"],
                                leaves=sp.leaves_of_sector(conv, d["sector"]))
                           for d in specials.damage],
            wswitch_leaves=[dict(line=w["line"], channel=w["channel"],
                                 leaves=[conv.remap[l_] for l_ in sp.leaves_on_line(conv, w["line"])])
                            for w in specials.wswitch])
        print(f"  mobile : {len(pbs)} push blocks, {len(pbv)} runs de sommets "
              f"({sum(t['vNm'] for t in pbv)} sommets), {len(pbw)} murs ; "
              f"{len(conv.switches)} interrupteurs ; {len(em.tiles)} tuiles apres compaction")
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    tmp = a.out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    os.replace(tmp, a.out)

    print("\n  criteres :")
    allok = True
    for k, v in crit.items():
        allok &= v["ok"]
        x = {i: j for i, j in v.items() if i != "ok"}
        print(f"    [{'OK' if v['ok'] else 'ECHEC'}] {k} : {json.dumps(x, ensure_ascii=False)[:150]}")
    print(f"\n  -> {a.out}  ({'TOUS LES CRITERES OK' if allok else 'ECHEC'})")
    return 0 if allok else 1


if __name__ == "__main__":
    sys.exit(main())

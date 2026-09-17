#!/usr/bin/env python3
"""geom3d.py -- etape E3 du convertisseur Duke Nukem 3D PC -> .LEV PowerSlave (generation 1).

Entree  : build\duke2ps\e1l1_quant.json      (E2.5b : modele Build quantifie sur la grille)
          build\duke2ps\e1l1_quant_convex.json (E2 : 440 morceaux convexes exacts)
Sortie  : build\duke2ps\e1l1_geom3d.json     (geometrie generation 1 : secteurs/murs/sommets/faces)

Le format est celui du moteur du fork (SLEVEL.H) ; les conventions ci-dessous sont MESUREES sur les
24 .LEV retail et relues dans l'emetteur de Lobotomy (UTIL\CONVERT.C), jamais devinees :

  repere      X = x_build/8, Z = -y_build/8, Y = -z_build/128 ; Y vers le HAUT (SLEVEL.H:115-118).
  mur         4 sommets v[0..3] : v0,v1 = arete HAUTE, v2,v3 = arete BASSE ; v0->v1 donne la longueur,
              v1->v2 la hauteur (CONVERT.C:1793-1810, verifie sur KILENTRY W0).
  plan        normale de Newell sur v[0..3], normalisee, *65536 ; d = -(centroide . n) * 65536
              (CONVERT.C:1992-2001). La normale pointe VERS L'INTERIEUR du secteur.
  tuiles      TILESIZE = 64 (SLEVEL.H:126). tileLength = (|v0v1| + 32) / 64, minimum 1 ; idem
              tileHeight sur |v1v2| (CONVERT.C:1802-1812). MESURE : sur les 66 881 murs retail, le
              rapport longueur/tileLength vaut 64 pour 34 293 murs (idem hauteur).
  parallelo.  WALLFLAG_PARALLELOGRAM : pas de faces ; `textures` indexe level_texture (unsigned char)
              a raison de DEUX octets par cellule, en lignes : [motif, tuile]
              (WALLS.C:1083 `textures+(rr*tileLength+cc)*2`, WALLS.C:1141-1143 `pattern[...]` puis
              `mapPic(...)`). `firstLight` indexe level_vertexLight, (tileLength+1)*(tileHeight+1)
              octets (verifie : KILENTRY W5 3x3 -> firstLight 0, W6 -> 16 ; W6 20x3 -> W8 a 100).
  faces       sinon firstFace..lastFace dans level_face et firstVertex..lastVertex dans level_vertex ;
              les indices de sommet d'une face sont LOCAUX au mur (WALLS.C:1223 `assert(v<maxV)`).
              Contrainte MAXVPERWALL : lastVertex-firstVertex < 700 (WALLS.C:1207).
  sol/plafond ce sont des MURS du secteur, normale +Y (sol) ou -Y (plafond). MESURE : exactement UN
              sol et UN plafond dans chacun des 8 408 secteurs retail. Paves sur une grille de 64 u
              decoupee par le polygone (MESURE : 11 207 faces de 64x64, le reste sur les bords).
  secteur     center = moyenne des v[0..3] de TOUS ses murs ; floorLevel = moyenne des y des murs de
              normale[1] > 0 (CONVERT.C:2003-2056).
  drapeaux    INVISIBLE si pas de face ; BLOCKED si nextSector == -1 ; SHORTOPENING si le portail
              (normal[1]==0) a 1 < hauteur < 90 ; CLIFFBNDRY si floorLevel - floorLevel_voisin > 320
              (CONVERT.C:2541-2586).
  budget      le slave reserve tileHeight*tileLength cellules pour un parallelogramme (WALLS.C:1374)
              et lastFace-firstFace+1 faces pour un mur a faces (WALLS.C:1555), AVANT tout rejet
              ecran, sur MAXNMSLAVEPOLYS 1300 moins 50 de marge. MESURE retail : aucun mur ne depasse
              748 cellules sur les 24 niveaux -> on decoupe a CAP_CELLS.

Usage : python tools\duke2ps\geom3d.py [--in-quant J] [--in-convex J] [--out J] [--cap-cells N]
Code retour 0 si tous les criteres passent, 1 sinon.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bandes                                           # noqa: E402
import ordre                                            # noqa: E402
import cout                                             # noqa: E402

Q_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_quant.json")
C_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_quant_convex.json")
OUT_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_geom3d.json")

GX = 8            # unites Build par unite Saturn en X/Z
GY = 128          # unites Build par unite Saturn en Y
TILESIZE = 64     # SLEVEL.H:126
EPS_AVALE = 8.0   # u : residu en deca duquel une coupe ne se fait pas (voir _grid_cells)

MAXVPERWALL = 700 # WALLS.C:1207 (strict : lastVertex-firstVertex < 700)
MAX_SLAVE = 1300  # MAXNMSLAVEPOLYS (WALLS.C:1336)
SLAVE_MARGIN = 50 # WALLS.C:1374
# Taille d'une cellule, en unites Saturn. Une cellule = UN sprite distordu VDP1
# (WALLS.C:1136 EZ_specialDistSpr2) qui porte UNE tuile entiere : si sa taille monde egale la
# REPETITION de la texture Build, l'echelle est juste sans decouper la texture.
# Les deux axes n'ont pas la meme contrainte : la deformation affine d'un quad VDP1 croit avec
# l'ecart de PROFONDEUR entre ses coins, donc une cellule LONGUE se deforme et une cellule HAUTE
# non (les deux bords horizontaux sont a la meme distance). MESURE retail sur 68 890 murs :
# longueur mediane 64 u, p99 90,5 ; hauteur mediane 62,9 u mais jusqu'a 544.
# LOI VDP1 (mesuree 2026-09-13). Un sommet de commande VDP1 tient sur 11 bits signes,
# -1024..1023 ; au-dela "operation cannot be guaranteed" (HW_VDP1.md:184). Colle a un mur, la
# collision garantit z >= PLAYER_RADIUS (SPRITE.C:141) et le mur entier est a cette distance,
# donc la cellule qui chevauche le bord de l'ecran projette a 160*(R+c)/R. D'ou :
#
#     c <= R * 863 / 160  ~=  5,4 * R
#
# PowerSlave : R=47 -> 253 u, et leurs cellules sont dessous : ils n'ont jamais rencontre le
# defaut. Duke avec R=30 (params/duke.cfg, le plus gros rayon qui passe encore dans un conduit
# de 64 u) -> 162 u. On prend 160.
CELL_HARD_MAX = 256

CELL_MIN_U, CELL_MAX_U = 64, CELL_HARD_MAX   # le long du mur
CELL_MIN_V, CELL_MAX_V = 64, CELL_HARD_MAX   # en hauteur

CAP_CELLS = 256   # borne de decoupe. MESURE retail : le plus gros mur PARALLELOGRAMME fait 242
                  # cellules et le plus gros mur A FACES 376 faces. Les bornes DURES sont ailleurs :
                  # drawRectWall assert(tileLength*tileHeight < MAXVPERWALL) (WALLS.C:1017) et
                  # rectTransform remplit (tileLength+1)*(tileHeight+1) entrees dans vCalc[700].


# ------------------------------------------------------------------------------------------
# optimisations de niveau
# ------------------------------------------------------------------------------------------
# Refusables une par une (--optim). Une carte editee a la main doit pouvoir etre convertie TELLE
# QUELLE : celui qui place ses secteurs au pixel pres a le droit de refuser qu'on les deplace, meme
# pour gagner des faces. Le convertisseur Doom a sa propre liste (doom3d.OPTIMS) et se sert du meme
# lecteur.
#   avalement : une coupe de cellule qui ne laisserait qu'une echarde ne se fait pas, la voisine
#               s'etend sur le residu (_grid_cells) -- etirement borne a x1,25
#   ordre     : liste de paires de secteurs que le moteur ordonnerait au hasard, faute de portail
#               entre eux (tools/ordre.py) -- CORRIGE un defaut, elle n'ajoute aucun risque
#   bandes    : les faces d'un mur sont rangees dans l'ordre ou weldFaceStrip sait les souder
#               (tools/bandes.py) -- PERMUTATION pure, aucun sommet ne bouge
OPTIMS = ("avalement", "ordre", "bandes")
OPTIM_ACTIFS = set(OPTIMS)


def lire_optims(s, connues, defaut=None):
    """`defaut`, `all`, `none`, ou une liste de noms pris dans `connues`. Un nom inconnu est une
    ERREUR : une faute de frappe qui couperait silencieusement une regle serait pire que pas
    d'option du tout.

    `defaut` != `all` : une optimisation dont un defaut a ete CONSTATE mais pas encore explique
    reste disponible sans etre active. On ne la retire pas du code -- on la retire du defaut."""
    s = (s or "defaut").strip().lower()
    if s in ("defaut", "default"):
        return set(connues if defaut is None else defaut)
    if s in ("all", "toutes"):
        return set(connues)
    if s in ("none", "aucune"):
        return set()
    noms = {x.strip() for x in s.split(",") if x.strip()}
    inconnus = noms - set(connues)
    if inconnus:
        raise SystemExit("--optim : %s inconnu(s) ; connus : %s"
                         % (", ".join(sorted(inconnus)), ", ".join(connues)))
    return noms


# ------------------------------------------------------------------------------------------
# repere
# ------------------------------------------------------------------------------------------
def sx(x_build):
    assert x_build % GX == 0, f"x {x_build} hors grille (E2.5 aurait du le quantifier)"
    return x_build // GX


def sz(y_build):
    assert y_build % GX == 0, f"y {y_build} hors grille"
    return -(y_build // GX)


def sy(z_build):
    """Y Saturn depuis le z Build. Les pentes donnent des z non multiples de 128 : on arrondit ici,
    et le critere `pentes_arrondies` compte les cas."""
    return -int(round(z_build / GY))


def slope_z(sec, pre, x, y):
    """z Build du plan `pre` ('floor'/'ceiling') au point Build (x, y). Identique a quantize.slope_z
    (meme formule que getzsofslope : heinum en 1/256 de z par unite le long de la normale)."""
    z = sec[pre + "z"]
    if not sec[pre + "stat"] & 2:
        return z
    x0, y0, x1, y1 = sec["slope_ref"]
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    if L == 0:
        return z
    return z + sec[pre + "heinum"] * (dx * (y - y0) - dy * (x - x0)) / (256.0 * L)


# ------------------------------------------------------------------------------------------
# polygones 2D (X, Z)
# ------------------------------------------------------------------------------------------
def area2(pts):
    n = len(pts)
    return sum(pts[k][0] * pts[(k + 1) % n][1] - pts[(k + 1) % n][0] * pts[k][1] for k in range(n))


def split_convex(poly, axis, c, src_edges=None):
    """Coupe un polygone convexe par la droite axis == c. Retourne (cote <= c, cote >= c).

    Chaque sommet est un triplet (x, z, src) ou `src` designe l'arete du polygone D'ORIGINE que porte
    l'arete partant de ce sommet (None pour une arete nee d'une coupe). Le point de croisement est
    calcule sur l'arete d'origine, pas sur la sous-arete courante : sinon une coupe interpole sur un
    bord deja arrondi par la coupe precedente et l'erreur se compose (MESURE : 1,12 u de depassement
    avant ce traitement, 0,5 u apres). Le meme croisement donne ainsi toujours le meme point entier,
    ce qui rend le pavage etanche."""
    n = len(poly)
    lo, hi = [], []
    for k in range(n):
        P, Q = poly[k], poly[(k + 1) % n]
        s = P[2]
        sp, sq = P[axis] - c, Q[axis] - c
        # P SUR la coupe : dans le morceau que l'arete d'origine quitte, l'arete sortante de P est
        # la coupe elle-meme (src None). Garder `s` y faisait calculer le croisement suivant sur la
        # mauvaise droite (MESURE E1M1 : 112 coupes refusees, faces de 128-181 u -- tuile etiree).
        if sp <= 0:
            lo.append(P if (sp < 0 or sq <= 0) else (P[0], P[1], None))
        if sp >= 0:
            hi.append(P if (sp > 0 or sq >= 0) else (P[0], P[1], None))
        if (sp < 0 < sq) or (sq < 0 < sp):
            if src_edges is not None and s is not None:
                A, B = src_edges[s]                    # arete d'origine : croisement exact
            else:
                A, B = P, Q
            da, db = A[axis] - c, B[axis] - c
            t = da / (da - db) if da != db else 0.0
            X = [0, 0]
            X[axis] = c
            X[1 - axis] = int(round(A[1 - axis] + (B[1 - axis] - A[1 - axis]) * t))
            if sp < 0:                                 # on sort : X termine l'arete d'origine
                lo.append((X[0], X[1], None))
                hi.append((X[0], X[1], s))
            else:                                      # on entre : X poursuit l'arete d'origine
                lo.append((X[0], X[1], s))
                hi.append((X[0], X[1], None))

    def clean(p):
        out = []
        for q in p:
            if not out or (q[0], q[1]) != (out[-1][0], out[-1][1]):
                out.append(q)
            elif q[2] is not None:
                out[-1] = q
        if len(out) > 1 and (out[0][0], out[0][1]) == (out[-1][0], out[-1][1]):
            out.pop()
        return out

    return clean(lo), clean(hi)


def clip_convex(subject, clipper):
    """Sutherland-Hodgman : subject (convexe) coupe par clipper (convexe). Les deux doivent tourner
    dans le meme sens ; on les normalise en sens trigonometrique. Coordonnees rationnelles ramenees
    a l'entier le plus proche -- la grille etant a 64 u et les sommets entiers, les intersections
    tombent presque toujours juste."""
    def ccw(p):
        return p if area2(p) > 0 else p[::-1]

    out = ccw(list(subject))
    cl = ccw(list(clipper))
    n = len(cl)
    for i in range(n):
        A, B = cl[i], cl[(i + 1) % n]
        if not out:
            return []
        ex, ez = B[0] - A[0], B[1] - A[1]

        def side(P):
            return ex * (P[1] - A[1]) - ez * (P[0] - A[0])

        new = []
        m = len(out)
        for k in range(m):
            P, Q = out[k], out[(k + 1) % m]
            sp, sq = side(P), side(Q)
            if sp >= 0:
                new.append(P)
            if (sp > 0 and sq < 0) or (sp < 0 and sq > 0):
                t = sp / (sp - sq)
                new.append((int(round(P[0] + (Q[0] - P[0]) * t)),
                            int(round(P[1] + (Q[1] - P[1]) * t))))
        out = []
        for p in new:                                # supprime les doublons consecutifs
            if not out or p != out[-1]:
                out.append(p)
        if len(out) > 1 and out[0] == out[-1]:
            out.pop()
    return out


# ------------------------------------------------------------------------------------------
# plan d'un quad (CONVERT.C:1974-2001)
# ------------------------------------------------------------------------------------------
def newell(v):
    """Normale de Newell (non normalisee) d'un quad donne dans l'ordre v0..v3."""
    nx = ny = nz = 0.0
    for i in range(4):
        a, b = v[i], v[(i + 1) % 4]
        nx += (a[1] - b[1]) * (a[2] + b[2])
        ny += (a[2] - b[2]) * (a[0] + b[0])
        nz += (a[0] - b[0]) * (a[1] + b[1])
    return nx, ny, nz


def plane_of(v):
    """(normal[3] en 16.16, d en 16.16) pour le quad v, ou None si degenere."""
    nx, ny, nz = newell(v)
    f = math.sqrt(nx * nx + ny * ny + nz * nz)
    if f <= 1e-6:
        return None
    nx, ny, nz = nx / f, ny / f, nz / f
    px = sum(p[0] for p in v) / 4.0
    py = sum(p[1] for p in v) / 4.0
    pz = sum(p[2] for p in v) / 4.0
    d = -(px * nx + py * ny + pz * nz)
    return [int(round(nx * 65536)), int(round(ny * 65536)), int(round(nz * 65536))], int(round(d * 65536))


def tile_counts(v, cu=TILESIZE, cv=TILESIZE):
    """(tileLength, tileHeight) : CONVERT.C:1802-1812, arrondi au plus proche, minimum 1.

    `cu`/`cv` sont la taille VOULUE d'une cellule. Le retail se contente de 64 u partout ; nous
    la prenons egale a la repetition de la texture Build, ce qui remet l'echelle d'aplomb sans
    depenser une seule tuile de plus (E4.1b)."""
    def dist(a, b):
        return math.dist(a, b)
    lu, lv = dist(v[0], v[1]), dist(v[1], v[2])
    tl = int((lu + cu / 2) // cu) or 1
    th = int((lv + cv / 2) // cv) or 1
    # L'arrondi AU PLUS PROCHE est ce qui tient l'echelle (un mur de 80 u pour une repetition de
    # 64 u vaut mieux en 1 cellule de 80 qu'en 2 de 40). Mais il peut rendre une cellule plus
    # grande que la cible -- mesure sur E1L1 : jusqu'a 376 u pour une cible de 256. La loi VDP1
    # s'impose donc comme PLANCHER SUR LE NOMBRE de cellules, pas comme changement d'arrondi.
    tl = max(tl, int(math.ceil(lu / CELL_HARD_MAX)))
    th = max(th, int(math.ceil(lv / CELL_HARD_MAX)))
    return max(tl, 1), max(th, 1)


def cell_size(tex):
    """Taille de cellule portee par un descripteur de placage, 64 u a defaut."""
    if tex is None or tex.get("mode") != "wall":
        return TILESIZE, TILESIZE
    return tex["cu"], tex["cv"]


def canon(quad):
    """Orientation canonique d'un quad : celle ou v0 <= v1 lexicographiquement. Les deux faces d'un
    portail decrivent le meme quad en sens inverse et retombent donc sur la MEME orientation
    canonique -- indispensable pour que la decoupe soit identique des deux cotes."""
    return [quad[1], quad[0], quad[3], quad[2]] if tuple(quad[0]) > tuple(quad[1]) else list(quad)


def is_parallelogram(v):
    """v0 + v2 == v1 + v3 sur les trois axes. MESURE : vrai pour les 11 712 murs PARALLELOGRAMME
    des 24 .LEV retail, avec un ecart exactement nul."""
    return all(v[0][k] + v[2][k] == v[1][k] + v[3][k] for k in range(3))


def pixel_length(v):
    """Longueur projetee en XZ (CONVERT.C:1798-1800)."""
    return int(round(math.hypot(v[0][0] - v[1][0], v[0][2] - v[1][2])))


# ------------------------------------------------------------------------------------------
# placage de texture (E4.1)
# ------------------------------------------------------------------------------------------
# Permutations de sommets qui reproduisent, sur une FACE, les motifs de pattern[]
# (WALLS.C:982-992), une face n'ayant pas d'octet motif a elle.
FACE_PERM = {0: (0, 1, 2, 3), 2: (2, 3, 0, 1), 5: (1, 0, 3, 2), 7: (3, 2, 1, 0)}


def quad_point(quad, s, t):
    """Point du quad par interpolation bilineaire ; s le long de v0->v1, t de v0->v3."""
    v0, v1, v2, v3 = quad
    a = [v0[i] + (v1[i] - v0[i]) * s for i in range(3)]
    b = [v3[i] + (v2[i] - v3[i]) * s for i in range(3)]
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


# ------------------------------------------------------------------------------------------
# emetteur
# ------------------------------------------------------------------------------------------
class Emitter:
    """Accumule sommets, faces, textures et lumieres dans les tableaux du .LEV."""

    def __init__(self):
        # Avalement des echardes (voir _grid_cells). Reglable par l'appelant : les deux
        # convertisseurs le mettent a 0 quand l'auteur refuse cette optimisation (--optim none).
        self.eps_avale = EPS_AVALE
        # Rangement des faces en bandes soudables (tools/bandes.py). Meme regle que ci-dessus :
        # les deux convertisseurs le coupent quand l'auteur refuse l'optimisation.
        self.bandes = True
        self.vertices = []      # {x,y,z,light,pad}
        self.faces = []         # {v[4] LOCAUX au mur, tile, pad}
        self.texture = []       # unsigned char, 2 par cellule : [motif, tuile]
        self.vertexLight = []   # char, 1 par sommet de grille
        self.walls = []
        self.sectors = []
        self.tiles = []         # cles de sous-tuile [pic, sx, sy, nx, ny] ; E4 fait les pixels
        self._tile_index = {}

    def tile(self, key):
        """Index de tuile (0..255) pour une cle de SOUS-TUILE (pic, sx, sy, nx, ny) : la case
        (sx, sy) d'un decoupage nx x ny de la texture Build `pic`. nx = ny = 1 donne la texture
        entiere, c'est-a-dire le comportement d'avant E4.1."""
        if key not in self._tile_index:
            self._tile_index[key] = len(self.tiles)
            self.tiles.append(list(key))
        return self._tile_index[key]

    def cell_tile(self, tex, picnum, pt, cell=None):
        """(index de tuile, motif) pour la cellule dont le CENTRE monde est `pt`.

        Le choix de la sous-tuile se fait sur la POSITION MONDE, jamais sur un decalage propage :
        une cellule garde donc la meme sous-tuile quelle que soit la facon dont le mur a ete
        decoupe (_split_wall) ou retourne (les deux faces d'un portail decrivent le meme quad en
        sens inverse). C'est ce qui rend le placage insensible a toute la machinerie geometrique.

        Le MOTIF encode les miroirs, gratuitement : le premier octet de chaque cellule indexe
        pattern[8][4] (WALLS.C:982-992), une permutation des 4 sommets -- 5 = miroir horizontal,
        7 = miroir vertical, 2 = rotation 180 = les deux."""
        if tex is None:
            return self.tile((picnum, 0, 0, 1, 1)), 0
        ncx, ncy = tex["ncx"], tex["ncy"]
        if tex["mode"] == "flat":
            a, b = pt[0], pt[2]                       # sol/plafond : les deux axes sont en XZ
            if tex["swap"]:                           # floorstat bit 2 (engine.c:1439-1444)
                a, b = -b, a
            u, v = a * tex["sgnu"], b * tex["sgnv"]   # bits 4 et 5 : miroirs
        else:
            u = (pt[0] - tex["ox"]) * tex["dx"] + (pt[2] - tex["oz"]) * tex["dz"]
            v = tex["yref"] - pt[1]
        cx = cy = 0
        if ncx > 1:
            cx = int(math.floor(u / tex["cu"] + tex["pu"])) % ncx
        if ncy > 1:
            cy = int(math.floor(v / tex["cv"] + tex["pv"])) % ncy
        if tex["flipx"]:
            cx = ncx - 1 - cx
        if tex["flipy"]:
            cy = ncy - 1 - cy
        pat = (2 if tex["flipy"] else 5) if tex["flipx"] else (7 if tex["flipy"] else 0)
        key = (tex["pic"], cx, cy, ncx, ncy)
        if tex.get("keyed_cell") and cell is not None:
            # La tuile est fabriquee POUR CETTE CELLULE. Sans cela elle porte `tex["cv"]` unites
            # monde de texture alors que la cellule posee en mesure `lv / tileHeight` : tout ce
            # qui n'est pas un multiple exact est etire ou repete. MESURE sur E1M1 : la hauteur de
            # cellule allait de 2 a 336 u pour une tuile toujours calee sur la hauteur de la
            # texture -- 332 murs plus courts que 64 u y ecrasaient la texture entiere, ce qui se
            # voit surtout sur les contremarches.
            # Seule la HAUTEUR entre dans la cle : horizontalement la tuile reste echantillonnee
            # sur une repetition de la texture, comme en E4.1b. Mettre aussi la largeur faisait
            # passer E1M1 de 112 a 295 tuiles (1,2 Mo) sans rien corriger de plus.
            key = key + (round(cell[1], 2), int(tex.get("voff", 0)))
            if tex.get("uwin"):
                # Doom, mur plus ETROIT que sa texture : Doom n'en montre que les colonnes
                # [u, u + L), on fabrique la tuile pour elles au lieu d'ecraser la texture entiere
                # dans la cellule (E1M1 : EXITDOOR, 128 de large, sur la face de 64 de la porte de
                # sortie). u = colonne au bord v0 de la cellule, texture en boucle.
                u0 = (u - cell[0] / 2.0 + tex["uoff"]) % tex["uw"]
                key = key + (round(cell[0], 2), round(u0, 2))
        return self.tile(key), pat

    def push_vertices(self, pts, light=0):
        """Ajoute un bloc CONTIGU de sommets et retourne (premier, dernier).
        Aucun partage entre murs : MESURE sur KILENTRY, les v[0..3] sont 0-3, 4-7, 8-11... donc
        Lobotomy n'en partage pas non plus, et un mur a faces EXIGE un bloc contigu
        (firstVertex..lastVertex, indices de face locaux a ce bloc)."""
        first = len(self.vertices)
        for p in pts:
            self.vertices.append(dict(x=int(p[0]), y=int(p[1]), z=int(p[2]),
                                      light=int(p[3]) if len(p) > 3 else light, pad=0))
        return first, len(self.vertices) - 1

    # -- murs ----------------------------------------------------------------------------
    def add_wall(self, quad, *, next_sector, picnum, invisible, blocked, parallax=False,
                 light=0, cap_cells=CAP_CELLS, stats=None, tex=None):
        """Emet UN mur (quad = [v0,v1,v2,v3], v0/v1 en haut). Retourne la liste des index emis.
        Un mur trop gros pour la reservation du slave est decoupe en longueur puis en hauteur."""
        pl = plane_of(quad)
        if pl is None:
            if stats is not None:
                stats["quads_degeneres"] += 1
            return []
        cu, cv = cell_size(tex)
        tl, th = tile_counts(quad, cu, cv)
        # La DECISION de couper se prend sur l'orientation canonique : tileHeight se mesure sur
        # l'arete v1->v2, donc au bout B pour une face du portail et au bout A pour l'autre. Sur un
        # portail en pente les deux hauteurs different, et decider sur `th` local desynchronise les
        # deux faces (MESURE : 8 portails asymetriques entre les secteurs 188/189/190 d'E1L1).
        ctl, cth = tile_counts(canon(quad), cu, cv)
        if ctl * cth > cap_cells or (ctl + 1) * (cth + 1) > MAXVPERWALL:
            return self._split_wall(quad, ctl, cth, next_sector=next_sector, picnum=picnum,
                                    invisible=invisible, blocked=blocked, parallax=parallax,
                                    light=light, cap_cells=cap_cells, stats=stats, tex=tex)
        normal, d = pl
        flags = 0
        if invisible or parallax:
            flags |= 0x02                        # WALLFLAG_INVISIBLE
        if blocked:
            flags |= 0x100                       # WALLFLAG_BLOCKED
        if parallax:
            flags |= 0x40                        # WALLFLAG_PARALLAX
        w = dict(normal=normal, d=d, object=0, flags=flags, textures=0,
                 firstFace=-1, lastFace=-1, firstVertex=65535, lastVertex=65535,
                 v=[], nextSector=next_sector, firstLight=0,
                 pixelLength=pixel_length(quad), tileLength=tl, tileHeight=th)
        fv, lv = self.push_vertices(quad, light)
        w["v"] = [fv, fv + 1, fv + 2, fv + 3]

        if not (invisible or parallax):
            # drawRectWall construit sa grille avec DEUX vecteurs constants,
            # vWidth = (v1-v0)/tileLength et vHeight = (v2-v1)/tileHeight (WALLS.C:1036-1049) :
            # c'est une hypothese de PARALLELOGRAMME STRICT. MESURE : les 11 712 murs
            # PARALLELOGRAMME du retail verifient v0+v2 == v1+v3 a l'unite pres sur les 3 axes.
            # Un mur trapezoidal (deux secteurs en pente de part et d'autre) doit donc partir en
            # liste de faces, sinon le moteur le redresse en parallelogramme.
            if is_parallelogram(quad):
                flags |= 0x01                    # WALLFLAG_PARALLELOGRAM
                w["flags"] = flags
                w["textures"] = len(self.texture)
                cell = (math.dist(quad[0], quad[1]) / tl, math.dist(quad[1], quad[2]) / th)
                for rr in range(th):
                    for cc in range(tl):
                        t, pat = self.cell_tile(
                            tex, picnum, quad_point(quad, (cc + 0.5) / tl, (rr + 0.5) / th),
                            cell=cell)
                        self.texture.append(pat)
                        self.texture.append(t)
                w["firstLight"] = len(self.vertexLight)
                self.vertexLight.extend([light] * ((tl + 1) * (th + 1)))
            else:
                self._faces_from_quad(w, quad, tl, th, picnum, light, tex, stats)
                if stats is not None:
                    stats["murs_trapezes"] += 1
        self.walls.append(w)
        if stats is not None:
            stats["cellules"] += tl * th
            stats["cellules_max"] = max(stats["cellules_max"], tl * th)
        return [len(self.walls) - 1]

    def push_faces(self, w, faces, stats=None):
        """Pose les faces d'un mur, apres les avoir rangees dans l'ordre ou le moteur sait les
        SOUDER (tools/bandes.py : weldFaceStrip n'enchaine que des faces consecutives liees par
        des aretes opposees). C'est une PERMUTATION -- aucun sommet ne bouge, aucune face ne
        change, et l'ordre de v[0..3], qui porte l'orientation de la texture, est intact."""
        avant = bandes.jointures([f["v"] for f in faces])
        apres, casses = avant, 0
        if self.bandes:
            faces, avant, apres, casses = bandes.ranger(faces)
        if stats is not None:
            stats["bandes_possibles"] += max(0, len(faces) - 1)
            stats["bandes_avant"] += avant
            stats["bandes_apres"] += apres
            stats["bandes_cycles"] += casses
        w["firstFace"] = len(self.faces)
        self.faces.extend(faces)
        w["lastFace"] = len(self.faces) - 1

    def _faces_from_quad(self, w, quad, tl, th, picnum, light, tex=None, stats=None):
        """Mur non parallelogramme : grille (tl x th) par interpolation bilineaire sur le quad,
        emise en faces explicites. L'ordre des coins suit drawRectWall (WALLS.C:1105-1120) :
        (h,w), (h,w+1), (h+1,w+1), (h+1,w)."""
        v0, v1, v2, v3 = quad

        def lerp(p, q, t):
            return tuple(p[i] + (q[i] - p[i]) * t for i in range(3))

        pts = []
        for h in range(th + 1):
            for c in range(tl + 1):
                a = lerp(v0, v1, c / tl)
                b = lerp(v3, v2, c / tl)
                pts.append(tuple(int(round(x)) for x in lerp(a, b, h / th)) + (light,))
        fv, lv = self.push_vertices(pts, light)
        w["firstVertex"], w["lastVertex"] = fv, lv
        # Une FACE n'a pas d'octet motif (sFaceType = v[4] + tile + pad, SLEVEL.H:120-124, et
        # WALLS.C:1223-1231 mappe poly[i]=vCalc[v[i]] sans permutation) : son orientation EST
        # l'ordre de v[0..3]. On applique donc le miroir en permutant les sommets.
        faces = []
        for h in range(th):
            r1, r2 = h * (tl + 1), (h + 1) * (tl + 1)
            for c in range(tl):
                t, pat = self.cell_tile(
                    tex, picnum, quad_point(quad, (c + 0.5) / tl, (h + 0.5) / th),
                    cell=(math.dist(quad[0], quad[1]) / tl, math.dist(quad[1], quad[2]) / th))
                q = [r1 + c, r1 + c + 1, r2 + c + 1, r2 + c]
                if pat:
                    q = [q[i] for i in FACE_PERM[pat]]
                faces.append(dict(v=q, tile=t, pad=0))
        self.push_faces(w, faces, stats)

    def _split_wall(self, quad, tl, th, *, cap_cells, stats=None, **kw):
        """Decoupe un mur dont tileLength*tileHeight depasse la reservation du slave (WALLS.C:1374).
        Coupe binaire sur la plus grande dimension, sur une frontiere de tuile pour que la grille de
        textures reste alignee. Termine toujours : tl et th valent au moins 1, et 1*1 <= cap_cells."""
        if stats is not None:
            stats["murs_decoupes"] += 1

        # Les deux faces d'un portail decrivent le MEME quad en sens inverse. Une coupe a `tl//2`
        # depuis v0 ne tombe pas au meme endroit vue de l'autre cote quand tl est impair (2/5 contre
        # 3/5), ce qui casserait la symetrie des portails. On coupe donc toujours dans l'orientation
        # canonique (v0 <= v1 lexicographiquement), puis on remet chaque morceau dans le sens d'entree.
        def mirror(q):
            return [q[1], q[0], q[3], q[2]]

        flipped = tuple(quad[0]) > tuple(quad[1])
        v0, v1, v2, v3 = mirror(quad) if flipped else list(quad)

        def lerp(p, q, t):
            return tuple(int(round(p[i] + (q[i] - p[i]) * t)) for i in range(3))

        if tl >= th:                                   # coupe en LONGUEUR
            t = (tl // 2) / tl
            m01, m32 = lerp(v0, v1, t), lerp(v3, v2, t)
            parts = [[v0, m01, m32, v3], [m01, v1, v2, m32]]
        else:                                          # coupe en HAUTEUR
            t = (th // 2) / th
            m03, m12 = lerp(v0, v3, t), lerp(v1, v2, t)
            parts = [[v0, v1, m12, m03], [m03, m12, v2, v3]]
        if flipped:
            parts = [mirror(p) for p in reversed(parts)]
        out = []
        for sub in parts:
            out += self.add_wall(sub, cap_cells=cap_cells, stats=stats, **kw)
        return out

    # -- sols et plafonds ----------------------------------------------------------------
    def add_flat(self, poly_xz, height_at, *, is_floor, picnum, parallax, light=0, stats=None,
                 tex=None, pleins=None):
        """Emet LE sol (is_floor=True) ou LE plafond du secteur. MESURE : chacun des 8 408 secteurs
        retail a exactement UN mur de normale +Y et UN de normale -Y.
        `height_at(x, z) -> y` donne la hauteur du plan (constante, ou pente).
        `pleins` (flat Doom) : cellules (gx, gy) de la grille ou l'on pose le CARRE ENTIER meme si
        le polygone n'en couvre qu'une partie -- voir DoomConverter.plafonds_pleins."""
        xs = [p[0] for p in poly_xz]
        zs = [p[1] for p in poly_xz]
        x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
        # v[0..3] : le quad ENGLOBANT, pose sur le plan (MESURE : KILENTRY W8, sol 192x1280 -> tl 3,
        # th 20, alors que le polygone du secteur y est deja rectangulaire).
        quad = [(x0, height_at(x0, z0), z0), (x1, height_at(x1, z0), z0),
                (x1, height_at(x1, z1), z1), (x0, height_at(x0, z1), z1)]
        pl = plane_of(quad)
        if pl is None:
            if stats is not None:
                stats["plats_degeneres"] += 1
            return []
        if (pl[0][1] > 0) != bool(is_floor):        # normale vers l'interieur : +Y sol, -Y plafond
            quad = [quad[0], quad[3], quad[2], quad[1]]
            pl = plane_of(quad)
        normal, d = pl
        tl = max(1, int(((x1 - x0) + TILESIZE / 2) // TILESIZE))
        th = max(1, int(((z1 - z0) + TILESIZE / 2) // TILESIZE))
        w = dict(normal=normal, d=d, object=0, flags=0x100, textures=0,
                 firstFace=-1, lastFace=-1, firstVertex=65535, lastVertex=65535,
                 v=[], nextSector=-1, firstLight=0,
                 pixelLength=x1 - x0, tileLength=tl, tileHeight=th)
        fv, _ = self.push_vertices(quad, light)
        w["v"] = [fv, fv + 1, fv + 2, fv + 3]

        if parallax:                                 # ciel : ni face ni cellule (CONVERT.C:1962-1968)
            # PAS d'INVISIBLE : le test `flags & WALLFLAG_INVISIBLE -> continue` (WALLS.C:1616) passe
            # AVANT la branche parallax (WALLS.C:1666), donc un ciel INVISIBLE n'est jamais vu du tout.
            # MESURE : les 2 844 murs PARALLAX des 23 niveaux retail valent tous exactement 320
            # (BLOCKED | PARALLAX), sans face et sans INVISIBLE.
            w["flags"] |= 0x40                       # PARALLAX
            self.walls.append(w)
            return [len(self.walls) - 1]

        cells = self._grid_cells(poly_xz, stats,
                                 grille=bool(tex is not None and tex.get("doom_flat")))
        pts, faces, index = [], [], {}
        faits = set()
        for cell in cells:
            cxm = sum(p[0] for p in cell) / float(len(cell))
            czm = sum(p[1] for p in cell) / float(len(cell))
            t, _ = self.cell_tile(tex, picnum,
                                  (cxm, height_at(int(round(cxm)), int(round(czm))), czm))
            def idx(x, z):
                k = (x, z)
                if k not in index:
                    index[k] = len(pts)
                    pts.append((x, height_at(x, z), z, light))
                return index[k]

            if pleins:
                g = (int(math.floor(cxm / TILESIZE)), int(math.floor(czm / TILESIZE)))
                if g in pleins:
                    if g not in faits:
                        faits.add(g)
                        x0c, z0c = g[0] * TILESIZE, g[1] * TILESIZE
                        sq = [(x0c, z0c), (x0c + TILESIZE, z0c), (x0c + TILESIZE, z0c + TILESIZE),
                              (x0c, z0c + TILESIZE)]
                        faces.append(([idx(x, z) for (x, z) in oriente_face_doom(sq)], t))
                        if stats is not None and abs(area2(cell)) < 2 * TILESIZE * TILESIZE - 1:
                            stats["plats_carres_debordants"] += 1
                    continue
            if tex is not None and tex.get("doom_flat"):
                # Flat Doom : sommets colineaires retires (sinon un carre plein part en un quad
                # plus un triangle, chacun portant la tuile ENTIERE), puis chaque face orientee
                # NO, NE, SE, SO -- voir oriente_face_doom.
                cp = sans_colineaires(list(cell))
                if len(cp) < 3:
                    continue
                morceaux = [cp] if len(cp) <= 4 else [
                    [cp[0]] + cp[a:a + 3] if a + 3 <= len(cp) else [cp[0]] + cp[a:]
                    for a in range(1, len(cp) - 1, 2)]
                for m in morceaux:
                    m = sans_colineaires(m) if len(m) > 3 else m
                    if len(m) < 3:
                        continue
                    faces.append(([idx(x, z) for (x, z) in oriente_face_doom(m)], t))
                continue

            loc = [idx(x, z) for (x, z) in cell]
            # un polygone de coupe a 3 a 6 sommets : eventail de quads, triangle = sommet repete
            for a in range(1, len(loc) - 1, 2):
                q = loc[0:1] + loc[a:a + 3]
                while len(q) < 4:
                    q.append(q[-1])
                faces.append((q[:4], t))
        if not faces:
            if stats is not None:
                stats["plats_vides"] += 1
            return []
        fvp, lvp = self.push_vertices(pts, light)
        w["firstVertex"], w["lastVertex"] = fvp, lvp
        self.push_faces(w, [dict(v=list(q), tile=tile, pad=0) for q, tile in faces], stats)
        self.walls.append(w)
        if stats is not None:
            stats["faces_plats"] += len(faces)
            stats["cellules"] += len(faces)
            stats["cellules_max"] = max(stats["cellules_max"], len(faces))
            if lvp - fvp >= MAXVPERWALL:
                stats["plats_trop_de_sommets"] += 1
            if len(faces) > CAP_CELLS:
                stats["plats_hors_budget_slave"] += 1
        return [len(self.walls) - 1]

    def _grid_cells(self, poly_xz, stats=None, grille=False):
        """Decoupe le polygone convexe pour que chaque face tienne dans une tuile de 64 u.

        On ne pave PAS avec une grille globale : une coupe deplace le point de croisement sur le
        reseau entier, et ce deplacement (au plus 0,5 u perpendiculairement a l'arete) coute une aire
        proportionnelle a la LONGUEUR de l'arete coupee. Sur un secteur mince -- le 143 d'E1L1 fait
        0,66 u d'epaisseur -- les deux croisements d'une coupe tombent sur le MEME entier et la moitie
        du sol disparait (MESURE : 16 u2 paves contre 32).

        Regle (la meme qu'en E2.5b) : on coupe sur la ligne de grille, SAUF quand la coupe n'est pas
        representable. Le test est exact : les aires sont entieres (area2), donc on compare
        area2(gauche) + area2(droite) a area2(entier) et on refuse la coupe si l'ecart depasse le
        seuil. On essaie alors l'autre axe, puis on garde le polygone entier.

        `grille` (flat Doom) : on coupe sur TOUTE ligne de la grille de 64 qui traverse le
        polygone, pas seulement tant qu'il depasse 64 u. Un flat Doom est cale sur cette grille
        (R_DrawSpan, `x & 63`) : une face de 60 u a cheval sur x = 64 portait une tuile entiere
        decalee sur deux cellules (MESURE E1M1 : 150 faces sur 3 039). Chaque face tient alors
        dans UNE cellule : les cellules pleines sont exactes, seule la cellule du bord s'ecrase."""
        src_edges = [(poly_xz[k], poly_xz[(k + 1) % len(poly_xz)]) for k in range(len(poly_xz))]
        # AVALEMENT : une coupe qui ne laisserait qu'une ECHARDE d'un cote -- moins de EPS_AVALE --
        # ne se fait pas, et la cellule voisine s'etend sur le residu. Une echarde porte la tuile
        # ENTIERE ecrasee au moins 8 fois, donc sous ce que le moteur sait representer ; l'avaler
        # coute un etirement borne par (64 + 2*eps)/64 = x1,25 sur sa voisine (le residu peut tomber
        # des DEUX cotes), contre x8 et plus si on la garde. MESURE E1M1 : 178 echardes -> 42, et les
        # faces baissent avec elles.
        # Les DEUX branches sont relachees ensemble, sinon la boucle tourne : `_cut_line` refuserait
        # une coupe que `fini` reclame encore, et on sortirait sur la garde des 20 000. Une cellule
        # peut donc aller jusqu'a 64 + 2*eps -- c'est exactement l'etirement x1,25 ci-dessus. La
        # terminaison tient parce qu'au-dela de 64 + 2*eps l'intervalle (lo+eps, hi-eps) est plus
        # long que 64 : il contient toujours un multiple de 64, donc la coupe est une vraie ligne de
        # grille et le nombre de lignes restantes decroit strictement.
        # A eps = 0 les expressions redonnent EXACTEMENT les anciennes : --optim none reproduit le
        # convertisseur d'avant a l'octet pres.
        eps = self.eps_avale

        def traverse(lo, hi):
            return (int((lo + eps) // TILESIZE) + 1) * TILESIZE < hi - eps
        out = []
        todo = [[(p[0], p[1], k) for k, p in enumerate(poly_xz)]]
        guard = 0
        while todo:
            guard += 1
            if guard > 20000:
                if stats is not None:
                    stats["pavage_garde_atteinte"] += 1
                out.extend(todo)
                break
            poly = todo.pop()
            xs = [p[0] for p in poly]
            zs = [p[1] for p in poly]
            dx, dz = max(xs) - min(xs), max(zs) - min(zs)
            if grille:
                fini = not (traverse(min(xs), max(xs)) or traverse(min(zs), max(zs)))
            else:
                fini = dx <= TILESIZE + 2 * eps and dz <= TILESIZE + 2 * eps
            if fini:
                out.append(poly)
                continue
            axes = [0, 1] if dx >= dz else [1, 0]
            cut = None
            for ax in axes:
                lo, hi = (min(xs), max(xs)) if ax == 0 else (min(zs), max(zs))
                if (not traverse(lo, hi)) if grille else (hi - lo <= TILESIZE + 2 * eps):
                    continue
                c = self._cut_line(lo, hi, eps)
                a, b = split_convex(poly, ax, c, src_edges)
                if len(a) < 3 or len(b) < 3:
                    continue
                err = abs(area2(a) + area2(b) - area2(poly)) / 2.0
                # grille : une bande de 12 u sur 128 le long d'un mur refusait sa coupe (tuile
                # etiree x2) pour un ecart d'arrondi invisible -- on tolere 1/4 u sur l'etendue.
                tol = 0.25 * max(dx, dz) if grille else 0.0
                if err > max(0.5, 0.01 * abs(area2(poly)) / 2.0, tol):
                    if stats is not None:
                        stats["coupes_refusees"] += 1
                    continue
                cut = (a, b)
                break
            if cut is None:
                if stats is not None:
                    stats["polygones_non_coupables"] += 1
                out.append(poly)
            else:
                todo.extend(cut)
        return [[(q[0], q[1]) for q in p] for p in out
                if len(p) >= 3 and abs(area2(p)) > 0]

    @staticmethod
    def _cut_line(lo, hi, eps=0.0):
        """Ligne de grille de 64 u la plus proche du milieu de [lo, hi] (strictement interieure).

        `eps` (avalement, voir _grid_cells) ecarte les lignes qui ne laisseraient qu'une echarde."""
        mid = (lo + hi) / 2.0
        c = int(round(mid / TILESIZE)) * TILESIZE
        if c <= lo + eps or c >= hi - eps:
            c = (int(lo + eps) // TILESIZE + 1) * TILESIZE
        if c <= lo + eps or c >= hi - eps:
            c = int((lo + hi) // 2)
        return c


# ------------------------------------------------------------------------------------------
# construction
# ------------------------------------------------------------------------------------------
def sans_colineaires(pts, eps=1e-6):
    """Retire les sommets colineaires (et les doublons) d'un polygone de (x, z)."""
    out = []
    n = len(pts)
    for i in range(n):
        a, b, c = pts[i - 1], pts[i], pts[(i + 1) % n]
        if abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])) > eps:
            out.append(b)
    return out


def oriente_face_doom(pts):
    """Ordonne les sommets d'une face de flat Doom pour le VDP1.

    Le VDP1 pose le coin (0,0) de la tuile sur poly[0] puis parcourt B, C, D, et le moteur ne
    permute rien (WALLS.C:1223-1231, WALLASM.H:50-61) : l'ordre des sommets EST l'orientation.
    Doom echantillonne un flat en `u = x & 63`, `v = -y & 63` (R_DrawSpan), donc la ligne 0 est
    au NORD et la colonne 0 a l'OUEST ; avec Z = +y_doom il faut A,B,C,D = NO, NE, SE, SO, soit le
    sens HORAIRE vu de dessus (x a droite, z en haut) en partant du coin nord-ouest.
    MESURE avant correctif sur E1M1 : 0 des ~1 100 carres alignes dans cet ordre, repartis sur
    les quatre rotations ET en sens inverse (miroir). Un triangle garde un sommet double, place
    au coin manquant."""
    xs = [q[0] for q in pts]
    zs = [q[1] for q in pts]
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    coins = [(x0, z1), (x1, z1), (x1, z0), (x0, z0)]          # NO, NE, SE, SO
    cx = sum(xs) / float(len(pts))
    cz = sum(zs) / float(len(pts))
    o = sorted(pts, key=lambda q: -math.atan2(q[1] - cz, q[0] - cx))
    # Candidats = toutes les rotations de l'ordre HORAIRE (jamais de miroir), et pour un triangle
    # toutes les facons de doubler un sommet en gardant les trois dans l'ordre cyclique -- la 1re
    # version associait chaque coin au sommet le plus proche et un triangle fin pouvait PERDRE un
    # sommet : face ecrasee en ligne, trou ou l'on voit le ciel VDP2 (20 faces degenerees sur le
    # disque du 14-09, dont la 898/909 du secteur 84 capturee par le testeur).
    # On garde le candidat le plus proche des quatre coins NO, NE, SE, SO de la cellule. Un carre
    # entier donne 0 et ne change pas ; une face PARTIELLE ne se cale plus sur « le sommet le plus
    # proche du NO », qui la faisait partir en biais le long d'un mur diagonal (face 889 du secteur
    # 84 : ecart 9 177 contre 3 673 pour la bonne rotation).
    n = len(o)
    rots = [o[k:] + o[:k] for k in range(n)]
    if n == 3:
        cands = [c for r in rots for c in ([r[0], r[1], r[2], r[2]], [r[0], r[1], r[1], r[2]],
                                           [r[0], r[0], r[1], r[2]])]
    elif n == 4:
        cands = rots
    else:
        return rots[min(range(n), key=lambda k: (o[k][0] - x0) ** 2 + (o[k][1] - z1) ** 2)]
    return list(min(cands, key=lambda c: sum((c[i][0] - coins[i][0]) ** 2
                                             + (c[i][1] - coins[i][1]) ** 2 for i in range(4))))


def shade_to_light(shade):
    """Build shade (0 = plein jour, plus haut = plus sombre) -> lumiere PowerSlave.
    MESURE : level_vertexLight et vertex.light valent 0..16 sur les niveaux retail (16 = le plus
    clair), et sSectorType.light vaut 128 dans les 2 869 secteurs regardes."""
    return max(0, min(16, int(round(16 - shade / 2.0))))


SECTOR_LIGHT = 128          # MESURE : constante sur tout le retail



# ------------------------------------------------------------------------------------------
# E4.1b : mesurer la repetition de chaque texture Build
def rep_wall_u(tsx, tsy, L_u, xrepeat, yrepeat):
    """Taille en u Saturn d'UNE repetition de la texture sur un mur Build.
    Horizontal : 8*xrepeat texels sont plaques sur toute la longueur (jfbuild engine.c:1027,
    1044-1045 ; confirme independamment par l'editeur, build.c:1726) -> une repetition couvre
    tsx*L/(8*xrepeat). Vertical : yrepeat/16 texels par u (engine.c:2512-2524) -> 16*tsy/yrepeat.
    Aucune ligne de jfbuild n'est recopiee : seules les formules sont reproduites."""
    return (tsx * L_u / (8.0 * max(1, xrepeat)),
            16.0 * tsy / max(1, yrepeat))


def rep_flat_u(tsx, tsy, stat):
    """Idem pour un sol/plafond : 16 unites Build par texel, soit 2 u Saturn par texel, divise par
    deux si le bit 3 « double smooshiness » est pose (engine.c:1437). MESURE E1L1 : 58 % des sols
    le portent, c'est la regle et non l'exception."""
    k = 1.0 if (stat & 8) else 0.5
    return (tsx / k, tsy / k)


class TexPlanner:
    """Mesure, par picnum, la REPETITION de la texture Build et ce que la cellule pourra en rendre.

    E4.1 decoupait les textures en sous-tuiles. C'etait un contresens sur ce moteur : le cache de
    caracteres VDP1 ne tient que 28 tuiles 64x64 16 bpp (initPicSystem(i,{28,31,1,10,12}),
    SRUINS.C:1903, classe TILE16BPP), et `map()` evince a la frame courante -- oldTime part de
    frameCount+1, donc une tuile DEJA utilisee dans la frame est eligible (PIC.C:271-275). Passer
    de 64 a 100 tuiles, avec une tuile differente par cellule, faisait deborder ce cache : chaque
    miss coute une expansion palette de 4 096 iterations (PIC.C:352) plus un DMA de 8 192 octets
    (PIC.C:397), et la tuile evincee laisse sa commande VDP1 pointer sur des pixels devenus ceux
    d'une autre. D'ou les deux symptomes a la fois : effondrement du framerate ET texture fausse.
    MESURE retail : 4 tuiles distinctes par secteur en mediane, 12 au p99, 25 au maximum.

    E4.1b corrige l'echelle autrement : la CELLULE prend la taille d'une repetition. Une seule
    tuile par picnum, donc exactement la pression de cache d'E4 -- celle qui tournait."""

    def __init__(self, quant, sizes, budget):
        self.sizes = sizes or {}
        self.grid = {}                 # plus aucune sous-tuile : cell_tile retombe sur (pic,0,0,1,1)
        self.rapport = []
        self.budget, self.depense, self.decoupes = budget, 0, 0
        self.aire_ok = 0.0
        if not self.sizes:
            return
        S = {s["id"]: s for s in quant["sectors"]}
        W = {w["id"]: w for w in quant["walls"]}
        tot = ok = 0.0
        par_pic = {}
        for w in quant["walls"]:
            pic = w["picnum"]
            p2, s = W.get(w["point2"]), S.get(w["sector"])
            if pic not in self.sizes or p2 is None or s is None:
                continue
            L = math.hypot(p2["x"] - w["x"], p2["y"] - w["y"]) / float(GX)
            if L <= 0:
                continue
            H = max(1.0, (s["floorz"] - s["ceilingz"]) / float(GY))
            tsx, tsy = self.sizes[pic]
            rx, ry = rep_wall_u(tsx, tsy, L, w["xrepeat"], w["yrepeat"])
            cu = min(CELL_MAX_U, max(CELL_MIN_U, rx))
            cv = min(CELL_MAX_V, max(CELL_MIN_V, ry))
            tl = max(1, int(round(L / cu)))
            th = max(1, int(round(H / cv)))
            # echelle reellement obtenue : la cellule mesure L/tl x H/th et porte UNE texture
            e = max((L / tl) / rx, rx / (L / tl)) * max((H / th) / ry, ry / (H / th))
            a = L * H
            tot += a
            if e <= 1.3:
                ok += a
            d = par_pic.setdefault(pic, dict(pic=pic, aire=0.0, aire_ok=0.0,
                                             rep=[round(rx), round(ry)]))
            d["aire"] += a
            if e <= 1.3:
                d["aire_ok"] += a
        self.aire_ok = ok / max(1e-9, tot)
        self.depense = len(par_pic)
        for d in par_pic.values():
            d["aire"] = round(d["aire"], 1)
            d["aire_ok"] = round(d["aire_ok"], 1)
        self.rapport = sorted(par_pic.values(), key=lambda r: -r["aire"])


class Builder:
    def __init__(self, quant, convex, cap_cells=CAP_CELLS, plan=None):
        self.S = {s["id"]: s for s in quant["sectors"]}
        self.W = {w["id"]: w for w in quant["walls"]}
        self.plan = plan
        self.pieces = convex["pieces"]
        self.cap = cap_cells
        self.em = Emitter()
        if "avalement" not in OPTIM_ACTIFS:
            self.em.eps_avale = 0.0           # --optim : l'auteur refuse l'avalement des echardes
        self.em.bandes = "bandes" in OPTIM_ACTIFS
        self.stats = Counter()
        self.stats["cellules_max"] = 0
        # morceau -> secteur gen1 (meme indice : un morceau = un secteur)
        self.piece_sector = {p["id"]: i for i, p in enumerate(self.pieces)}

    # -- hauteurs --------------------------------------------------------------------------
    def sec_of(self, piece):
        return self.S[piece["sectors"][0]]

    def y_at(self, piece, pre, xy):
        """Y Saturn du plan `pre` du morceau au point Build xy."""
        z = slope_z(self.sec_of(piece), pre, xy[0], xy[1])
        y = -z / GY
        r = int(round(y))
        if abs(y - r) > 1e-6:
            self.stats["hauteurs_arrondies"] += 1
        return r


    # -- placage (E4.1) --------------------------------------------------------------------
    def wall_tex(self, wall, sec, nsec, part):
        """Descripteur de placage d'un mur Build. `part` vaut "plein", "bas" ou "haut".

        Il ne porte plus de sous-tuile (ncx = ncy = 1) : il porte la TAILLE DE CELLULE, c'est-a-dire
        une repetition de la texture, bornee par CELL_*. Ce qu'il reste de gratuit, ce sont les
        miroirs, encodes dans le MOTIF de la cellule (WALLS.C:982-992) et non dans une tuile.

        L'ancre verticale suit le bit 2 de cstat (engine.c:2521-2523, 2627-2629, 2723-2724) :
        sans le bit on s'accroche au bord MOBILE (le voisin), avec le bit au secteur courant.
        MESURE E1L1 : 41 % des murs portent ce bit."""
        if wall is None or self.plan is None or not self.plan.sizes:
            return None
        pic = wall["picnum"]
        if pic not in self.plan.sizes:
            return None
        p2 = self.W.get(wall["point2"])
        if p2 is None:
            return None
        ox, oz = float(sx(wall["x"])), float(sz(wall["y"]))
        ex, ez = float(sx(p2["x"])), float(sz(p2["y"]))
        L = math.hypot(ex - ox, ez - oz)
        if L <= 0:
            return None
        tsx, tsy = self.plan.sizes[pic]
        rx, ry = rep_wall_u(tsx, tsy, L, wall["xrepeat"], wall["yrepeat"])
        cst = wall["cstat"]
        bas = bool(cst & 4)
        if part == "plein":
            zref = sec["floorz"] if bas else sec["ceilingz"]
        elif part == "bas":
            zref = sec["ceilingz"] if bas else (nsec or sec)["floorz"]
        else:
            zref = sec["ceilingz"] if bas else (nsec or sec)["ceilingz"]
        return dict(mode="wall", pic=pic, ncx=1, ncy=1,
                    ox=ox, oz=oz, dx=(ex - ox) / L, dz=(ez - oz) / L,
                    cu=min(CELL_MAX_U, max(CELL_MIN_U, rx)),
                    cv=min(CELL_MAX_V, max(CELL_MIN_V, ry)),
                    pu=0.0, pv=0.0,
                    yref=-zref / float(GY),
                    flipx=bool(cst & 8), flipy=bool(cst & 256))

    def flat_tex(self, sec, is_floor):
        """Descripteur de placage d'un sol ou d'un plafond. L'alignement RELATIF (bit 6) n'est pas
        traite : ces secteurs retombent sur la texture entiere en une tuile, ce qui est le
        comportement d'avant E4.1 -- un decoupage mal oriente serait pire que pas de decoupage."""
        if self.plan is None:
            return None
        pre = "floor" if is_floor else "ceiling"
        pic = sec[pre + "picnum"]
        g = self.plan.grid.get(pic)
        if not g or g == (1, 1):
            return None
        stat = sec[pre + "stat"]
        if stat & 64:
            return None
        tsx, tsy = self.plan.sizes[pic]
        ncx, ncy = g
        rx, ry = rep_flat_u(tsx, tsy, stat)
        return dict(mode="flat", pic=pic, ncx=ncx, ncy=ncy,
                    swap=bool(stat & 4), sgnu=-1.0 if (stat & 16) else 1.0,
                    sgnv=-1.0 if (stat & 32) else 1.0,
                    cu=rx / ncx, cv=ry / ncy,
                    pu=sec[pre + "xpanning"] / 256.0 * ncx,
                    pv=sec[pre + "ypanning"] / 256.0 * ncy,
                    flipx=False, flipy=False)

    # -- murs ------------------------------------------------------------------------------
    def emit_edge(self, piece, k):
        """Emet les murs du morceau `piece` le long de son arete k."""
        vs = piece["verts"]
        n = len(vs)
        A, B = vs[k], vs[(k + 1) % n]
        e = piece["edges"][k]
        P = (sx(A[0]), sz(A[1]))
        Q = (sx(B[0]), sz(B[1]))
        pfA, pfB = self.y_at(piece, "floor", A), self.y_at(piece, "floor", B)
        pcA, pcB = self.y_at(piece, "ceiling", A), self.y_at(piece, "ceiling", B)

        wall = self.W.get(e.get("wall"))
        picnum = wall["picnum"] if wall else self.sec_of(piece)["floorpicnum"]
        light = shade_to_light(wall["shade"] if wall else self.sec_of(piece)["floorshade"])

        twin = e.get("twin")
        nb = None
        if twin is not None and e["type"] in ("portail", "diagonale"):
            nb = self.pieces[twin[0]]

        if nb is None:                                # mur plein
            self.add_seg(piece, P, Q, (pfA, pfB), (pcA, pcB), next_sector=-1,
                         picnum=picnum, light=light, invisible=False, blocked=True,
                         tex=self.wall_tex(wall, self.sec_of(piece), None, "plein"))
            return

        qfA, qfB = self.y_at(nb, "floor", A), self.y_at(nb, "floor", B)
        qcA, qcB = self.y_at(nb, "ceiling", A), self.y_at(nb, "ceiling", B)
        ns = self.piece_sector[nb["id"]]

        # Bornes de l'ouverture. lo et hi sont des fonctions SYMETRIQUES des deux secteurs, donc les
        # deux faces du portail decrivent le meme quad -- indispensable a la symetrie des portails.
        loA, loB = max(pfA, qfA), max(pfB, qfB)
        hiA, hiB = min(pcA, qcA), min(pcB, qcB)

        if hiA <= loA and hiB <= loB:                  # ouverture refermee partout : mur plein
            self.stats["portails_refermes"] += 1
            self.add_seg(piece, P, Q, (pfA, pfB), (pcA, pcB), next_sector=-1,
                         picnum=picnum, light=light, invisible=False, blocked=True,
                         tex=self.wall_tex(wall, self.sec_of(piece), None, "plein"))
            return

        # Une ouverture peut se refermer EN COURS d'arete quand les deux secteurs sont en pente
        # (MESURE : 200 <-> 342 sur E1L1). Le portail s'effile alors jusqu'a une arete de hauteur
        # nulle a ce bout ; sans ce traitement le quad se croise en noeud papillon et chevauche la
        # marche basse.
        botA, topA = (loA, hiA) if hiA > loA else (loA, loA)
        botB, topB = (loB, hiB) if hiB > loB else (loB, loB)
        if (hiA <= loA) != (hiB <= loB):
            self.stats["portails_effiles"] += 1

        # Les parties VISIBLES restent bornees par le sol et le plafond du secteur ; seul le quad du
        # portail, invisible, peut depasser (cas ou le sol du voisin est au-dessus de notre plafond).
        lsA, lsB = min(botA, pcA), min(botB, pcB)
        if lsA > pfA or lsB > pfB:                     # marche basse
            self.add_seg(piece, P, Q, (pfA, pfB), (lsA, lsB), next_sector=-1,
                         picnum=picnum, light=light, invisible=False, blocked=True,
                         tex=self.wall_tex(wall, self.sec_of(piece),
                                           self.sec_of(nb), "bas"))
            self.stats["marches_basses"] += 1
        liA, liB = max(topA, pfA), max(topB, pfB)
        if pcA > liA or pcB > liB:                     # linteau
            self.add_seg(piece, P, Q, (liA, liB), (pcA, pcB), next_sector=-1,
                         picnum=picnum, light=light, invisible=False, blocked=True,
                         tex=self.wall_tex(wall, self.sec_of(piece),
                                           self.sec_of(nb), "haut"))
            self.stats["linteaux"] += 1
        self.add_seg(piece, P, Q, (botA, botB), (topA, topB), next_sector=ns,
                     picnum=picnum, light=light, invisible=True,
                     blocked=bool(wall and (wall["cstat"] & 1)))
        self.stats["portails"] += 1

    def add_seg(self, piece, P, Q, bot, top, *, next_sector, picnum, light, invisible, blocked,
                tex=None):
        """Un segment vertical entre les hauteurs bot=(bA,bB) et top=(tA,tB)."""
        if top[0] <= bot[0] and top[1] <= bot[1]:
            return []                                  # nul aux deux bouts : rien a emettre
        quad = [(P[0], top[0], P[1]), (Q[0], top[1], Q[1]),
                (Q[0], bot[1], Q[1]), (P[0], bot[0], P[1])]
        quad = self.orient_inward(piece, quad)
        return self.em.add_wall(quad, next_sector=next_sector, picnum=picnum,
                                invisible=invisible, blocked=blocked, light=light,
                                cap_cells=self.cap, stats=self.stats, tex=tex)

    def orient_inward(self, piece, quad):
        """Retourne le quad si sa normale de Newell ne pointe pas vers l'interieur du morceau."""
        pl = plane_of(quad)
        if pl is None:
            return quad
        n = pl[0]
        cx = sum(sx(v[0]) for v in piece["verts"]) / len(piece["verts"])
        cz = sum(sz(v[1]) for v in piece["verts"]) / len(piece["verts"])
        qx = sum(p[0] for p in quad) / 4.0
        qz = sum(p[2] for p in quad) / 4.0
        if n[0] * (cx - qx) + n[2] * (cz - qz) < 0:
            return [quad[1], quad[0], quad[3], quad[2]]
        return quad

    # -- secteur ---------------------------------------------------------------------------
    def emit_sector(self, piece):
        first = len(self.em.walls)
        for k in range(len(piece["verts"])):
            self.emit_edge(piece, k)

        sec = self.sec_of(piece)
        poly = [(sx(v[0]), sz(v[1])) for v in piece["verts"]]
        # le pavage attend un polygone en sens trigonometrique dans (X, Z)
        if area2(poly) < 0:
            poly = poly[::-1]

        def hf(x, z):
            return -slope_z(sec, "floor", x * GX, -z * GX) / GY

        def hc(x, z):
            return -slope_z(sec, "ceiling", x * GX, -z * GX) / GY

        self.em.add_flat(poly, lambda x, z: int(round(hf(x, z))), is_floor=True,
                         picnum=sec["floorpicnum"], parallax=bool(sec["floorstat"] & 1),
                         light=shade_to_light(sec["floorshade"]), stats=self.stats,
                         tex=self.flat_tex(sec, True))
        self.em.add_flat(poly, lambda x, z: int(round(hc(x, z))), is_floor=False,
                         picnum=sec["ceilingpicnum"], parallax=bool(sec["ceilingstat"] & 1),
                         light=shade_to_light(sec["ceilingshade"]), stats=self.stats,
                         tex=self.flat_tex(sec, False))
        # Les portails doivent etre EN TETE : findDoorways parcourt firstWall..lastWall et fait
        # `if (theWall->nextSector == -1) return;` (WALLS.C:1740-1743, commentaire « doorways are
        # sorted to be first in the list »). Un seul mur plein en tete coupe toute la propagation de
        # visibilite : on ne voit plus que le secteur ou l'on se tient.
        # MESURE : 0 des 8 211 secteurs retail ne viole cette regle.
        seg = self.em.walls[first:]
        self.em.walls[first:] = ([w for w in seg if w["nextSector"] >= 0]
                                 + [w for w in seg if w["nextSector"] == -1])
        last = len(self.em.walls) - 1

        # center = moyenne des v[0..3] de TOUS les murs ; floorLevel = moyenne des y des sols
        # (CONVERT.C:2003-2056)
        acc = [0, 0, 0]
        n = 0
        lvl = 0
        nl = 0
        for wi in range(first, last + 1):
            w = self.em.walls[wi]
            for vi in w["v"]:
                v = self.em.vertices[vi]
                acc[0] += v["x"]
                acc[1] += v["y"]
                acc[2] += v["z"]
                n += 1
                if w["normal"][1] > 0:
                    lvl += v["y"]
                    nl += 1
        self.em.sectors.append(dict(
            object=0, center=[acc[0] // n, acc[1] // n, acc[2] // n],
            floorLevel=(lvl // nl) if nl else 0,
            firstWall=first, lastWall=last, light=SECTOR_LIGHT, flags=0,
            cutIndex=0, cutChannel=0, pad=0))
        if nl == 0:
            self.stats["secteurs_sans_sol"] += 1

    # -- passes finales --------------------------------------------------------------------
    def post_flags(self):
        """SHORTOPENING et CLIFFBNDRY (CONVERT.C:2541-2586)."""
        W, S, V = self.em.walls, self.em.sectors, self.em.vertices
        for si, s in enumerate(S):
            for wi in range(s["firstWall"], s["lastWall"] + 1):
                w = W[wi]
                if w["nextSector"] == -1 or w["normal"][1] != 0:
                    continue
                ys = [V[i]["y"] for i in w["v"]]
                h = max(ys) - min(ys)
                if 1.0 < h < 90.0:
                    w["flags"] |= 0x1000                 # WALLFLAG_SHORTOPENING
                    self.stats["shortopening"] += 1
                if s["floorLevel"] - S[w["nextSector"]]["floorLevel"] > 320:
                    w["flags"] |= 0x800                  # WALLFLAG_CLIFFBNDRY
                    self.stats["cliffbndry"] += 1

    def build(self):
        for p in self.pieces:
            self.emit_sector(p)
        self.post_flags()
        return self.em


# ------------------------------------------------------------------------------------------
# criteres (verification contradictoire : relit la sortie, n'utilise pas les intermediaires)
# ------------------------------------------------------------------------------------------
def check(em, quant, cap_cells):
    S, W, V, F = em.sectors, em.walls, em.vertices, em.faces
    crit = {}

    def put(key, ok, **extra):
        crit[key] = dict(ok=bool(ok), **extra)

    put("secteurs_max", len(S) <= 600, n=len(S), limite=600)
    put("murs_max", len(W) <= 5500, n=len(W), limite=5500)

    hors = [i for i, v in enumerate(V) if max(abs(v["x"]), abs(v["y"]), abs(v["z"])) > 16383]
    put("coords_dans_short", not hors, n=len(hors))

    bad = []
    for si, s in enumerate(S):
        f = sum(1 for wi in range(s["firstWall"], s["lastWall"] + 1) if W[wi]["normal"][1] > 0)
        c = sum(1 for wi in range(s["firstWall"], s["lastWall"] + 1) if W[wi]["normal"][1] < 0)
        if f != 1 or c != 1:
            bad.append((si, f, c))
    put("un_sol_un_plafond", not bad, n=len(bad), exemples=bad[:5])

    # la normale doit pointer vers l'interieur : le centre du secteur du bon cote de chaque plan
    out = []
    for si, s in enumerate(S):
        cx, cy, cz = s["center"]
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            val = (w["normal"][0] * cx + w["normal"][1] * cy + w["normal"][2] * cz) + w["d"]
            if val < -2 * 65536:
                out.append((si, wi, round(val / 65536.0, 1)))
    put("normales_vers_interieur", not out, n=len(out), exemples=out[:5])

    # symetrie des portails : meme quadruplet de sommets des deux cotes
    def quad_key(w):
        return tuple(sorted((V[i]["x"], V[i]["y"], V[i]["z"]) for i in w["v"]))

    owner = {}
    for si, s in enumerate(S):
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            owner[wi] = si
    asym = []
    for wi, w in enumerate(W):
        ns = w["nextSector"]
        if ns == -1:
            continue
        me = owner[wi]
        k = quad_key(w)
        twin = [j for j in range(S[ns]["firstWall"], S[ns]["lastWall"] + 1)
                if W[j]["nextSector"] == me and quad_key(W[j]) == k]
        if len(twin) != 1:
            asym.append((wi, me, ns, len(twin)))
    put("symetrie_portails", not asym, n=len(asym), exemples=asym[:5])

    # indices de face locaux au mur, et MAXVPERWALL
    bad_f, over_v = [], []
    for wi, w in enumerate(W):
        if w["firstFace"] < 0:
            continue
        nv = w["lastVertex"] - w["firstVertex"] + 1
        if nv >= MAXVPERWALL:
            over_v.append((wi, nv))
        for f in range(w["firstFace"], w["lastFace"] + 1):
            if any(not (0 <= i < nv) for i in F[f]["v"]):
                bad_f.append((wi, f))
                break
    put("indices_faces_locaux", not bad_f, n=len(bad_f), exemples=bad_f[:5])
    put("sommets_par_mur", not over_v, n=len(over_v), limite=MAXVPERWALL, exemples=over_v[:5])

    # budget du slave : WALLS.C:1374 (parallelogramme) et WALLS.C:1555 (faces)
    over_s = []
    for wi, w in enumerate(W):
        cells = (w["tileLength"] * w["tileHeight"] if w["flags"] & 0x01
                 else (w["lastFace"] - w["firstFace"] + 1 if w["firstFace"] >= 0 else 0))
        if cells + SLAVE_MARGIN > MAX_SLAVE:
            over_s.append((wi, cells))
    put("budget_slave", not over_s, n=len(over_s), limite=MAX_SLAVE - SLAVE_MARGIN,
        exemples=over_s[:5])

    # parallelogrammes : les blocs de textures et de lumieres doivent PAVER exactement leurs
    # tableaux, sans trou ni chevauchement. On ne peut plus exiger l'ordre des murs : les portails
    # sont remontes en tete de chaque secteur, donc les blocs ne sont plus monotones.
    spans_t, spans_l = [], []
    for wi, w in enumerate(W):
        if not (w["flags"] & 0x01):
            continue
        spans_t.append((w["textures"], w["textures"] + 2 * w["tileLength"] * w["tileHeight"], wi))
        spans_l.append((w["firstLight"],
                        w["firstLight"] + (w["tileLength"] + 1) * (w["tileHeight"] + 1), wi))
    bad_p = []
    for spans, total, nom in ((spans_t, len(em.texture), "textures"),
                              (spans_l, len(em.vertexLight), "lumieres")):
        pos = 0
        for a, b, wi in sorted(spans):
            if a != pos:
                bad_p.append((nom, wi, a, pos))
                break
            pos = b
        if pos != total:
            bad_p.append((nom, "fin", pos, total))
    put("blocs_parallelogrammes", not bad_p, n=len(bad_p), textures=len(em.texture),
        lumieres=len(em.vertexLight), exemples=bad_p[:3])

    # les portails doivent etre en tete de chaque secteur (WALLS.C:1740-1743)
    bad_o = []
    for si, s in enumerate(S):
        solid = False
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            if W[wi]["nextSector"] == -1:
                solid = True
            elif solid:
                bad_o.append(si)
                break
    put("portails_en_tete", not bad_o, n=len(bad_o), exemples=bad_o[:5])

    # budget d'octets du bloc niveau (LEVEL.C:40-41 : assert(size < 900000))
    # tailles MESUREES par lev.py : secteur 24, mur 48, sommet 8, face 10, objet 4
    nb = (len(S) * 24 + len(W) * 48 + len(V) * 8 + len(F) * 10
          + len(em.texture) + len(em.vertexLight) + 56)
    put("budget_octets", nb < 900000, octets=nb, limite=900000,
        detail=dict(secteurs=len(S) * 24, murs=len(W) * 48, sommets=len(V) * 8,
                    faces=len(F) * 10, textures=len(em.texture), lumieres=len(em.vertexLight)))

    # depart du joueur dans un secteur
    dep = quant.get("depart_m1") or {}
    put("depart_present", bool(dep), depart=dep)
    return crit



def tile_sizes(grp_path):
    """{picnum: (sizx, sizy)} lu dans les en-tetes des TILES0xx.ART du GRP. Seuls les en-tetes
    sont lus : int32 version/numtiles/first/last, puis int16 sizx[n], int16 sizy[n]."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import grp as grpmod
    g = grpmod.GrpFile(grp_path)
    out = {}
    for name in sorted(n for n in g.names()
                       if n.upper().startswith("TILES") and n.upper().endswith(".ART")):
        d = g.read(name)
        _, _, first, last = struct.unpack("<4i", d[:16])
        n = last - first + 1
        sxs = struct.unpack("<%dh" % n, d[16:16 + 2 * n])
        sys_ = struct.unpack("<%dh" % n, d[16 + 2 * n:16 + 4 * n])
        for i in range(n):
            if sxs[i] * sys_[i]:
                out[first + i] = (sxs[i], sys_[i])
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--in-quant", dest="q", default=Q_DEFAULT)
    ap.add_argument("--in-convex", dest="c", default=C_DEFAULT)
    ap.add_argument("--out", default=OUT_DEFAULT)
    ap.add_argument("--cap-cells", type=int, default=CAP_CELLS)
    ap.add_argument("--grp", default=os.path.join(ROOT, "refs", "build", "duke13", "DUKE3D.GRP"),
                    help="GRP de Duke : sert a lire les TAILLES des textures (E4.1)")
    ap.add_argument("--budget-tuiles", type=int, default=0,
                    help="conserve pour la ligne de commande ; sans effet depuis E4.1b, ou une "
                         "texture ne coute plus qu'une seule tuile")
    ap.add_argument("--optim", default="defaut",
                    help="optimisations de niveau : `defaut`, `all`, `none`, ou une liste parmi %s. "
                         "`none` reproduit a l'octet pres la geometrie de la carte dessinee."
                         % ", ".join(OPTIMS))
    args = ap.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    global OPTIM_ACTIFS
    OPTIM_ACTIFS = lire_optims(args.optim, OPTIMS)

    quant = json.load(open(args.q, encoding="utf-8"))
    convex = json.load(open(args.c, encoding="utf-8"))
    print(f"E3 : {len(convex['pieces'])} morceaux -> geometrie generation 1 "
          f"(plafond slave {args.cap_cells} cellules)")

    sizes = {}
    if args.grp and os.path.exists(args.grp):
        sizes = tile_sizes(args.grp)
    else:
        print(f"  (pas de GRP en {args.grp} : cellule fixee a {TILESIZE} u comme le retail)")
    plan = TexPlanner(quant, sizes, args.budget_tuiles)
    if sizes:
        print(f"  E4.1b : cellule = 1 repetition, bornee a [{CELL_MIN_U},{CELL_MAX_U}] u en "
              f"longueur et [{CELL_MIN_V},{CELL_MAX_V}] u en hauteur ; {plan.depense} picnums de "
              f"mur, une tuile chacun, {plan.aire_ok * 100:.1f} % de aire a la bonne echelle")

    b = Builder(quant, convex, args.cap_cells, plan)
    em = b.build()
    print(f"  {len(em.sectors)} secteurs, {len(em.walls)} murs, {len(em.vertices)} sommets, "
          f"{len(em.faces)} faces, {len(em.texture)} octets de texture, "
          f"{len(em.vertexLight)} lumieres, {len(em.tiles)} tuiles distinctes")
    for k in sorted(b.stats):
        print(f"    {k} : {b.stats[k]}")

    # Comme chez Doom : le flot de visibilite sert deux fois, aux paires d'ordre et a la carte de
    # cout, donc il est calcule une fois et passe aux deux. Il ne change rien a ce qui est emis.
    vu = ordre.visibilite(em.sectors, em.walls, em.vertices)
    paires = []
    if "ordre" in OPTIM_ACTIFS:
        # Meme defaut que chez Doom : deux secteurs sans portail commun (les deux cotes d'un
        # pilier) ne recoivent aucune contrainte de buildTree. Le calcul ne lit que (S, W, V).
        paires, st_ordre = ordre.paires_d_ordre(em.sectors, em.walls, em.vertices, vu=vu,
                                                trace=print)
        d = min(st_ordre, key=lambda s: (s["positions"], s["paires"], s["entrees"]))
        print(f"  paires d'ordre : {len(paires)} entrees ({4 + 6 * len(paires)} o), "
              f"{d['positions']}/{d['total']} positions encore fautives "
              f"({100.0 * d['positions'] / max(1, d['total']):.0f} %, "
              f"{100.0 * st_ordre[0]['positions'] / max(1, d['total']):.0f} % sans la table), "
              f"{d['indecidables']} paires sans plan separateur")
    _positions, st_cout = cout.carte(em.sectors, em.walls, vu[0], vu[1])
    print(f"  cout : {cout.resume(st_cout)}")

    crit = check(em, quant, args.cap_cells)
    out = dict(format="duke2ps/e3-geom3d v1",
               source=dict(quant=os.path.relpath(args.q, ROOT).replace("\\", "/"),
                           convex=os.path.relpath(args.c, ROOT).replace("\\", "/"),
                           optim=sorted(OPTIM_ACTIFS)),
               conventions=dict(
                   repere="X = x_build/8, Z = -y_build/8, Y = -z_build/128 ; Y vers le haut",
                   mur="v0,v1 arete haute ; v2,v3 arete basse ; v0->v1 longueur, v1->v2 hauteur",
                   tuile=f"TILESIZE {TILESIZE} (SLEVEL.H:126)",
                   textures="2 octets par cellule : [motif, tuile] (WALLS.C:1083)",
                   faces="indices de sommet LOCAUX au mur (WALLS.C:1223)",
                   tiles="`tiles` = cles [pic, 0, 0, 1, 1] : la texture Build `pic` entiere. "
                         "L'echelle passe par la TAILLE de la cellule, pas par un decoupage."),
               orderPairs=[list(p) for p in paires],
               stats=dict(b.stats), criteres=crit, tiles=em.tiles,
               placage=dict(cellule_u=[CELL_MIN_U, CELL_MAX_U],
                            cellule_v=[CELL_MIN_V, CELL_MAX_V],
                            picnums_mur=getattr(plan, "depense", 0),
                            aire_correcte=round(getattr(plan, "aire_ok", 0.0), 4),
                            par_picnum=plan.rapport),
               sectors=em.sectors, walls=em.walls, vertices=em.vertices, faces=em.faces,
               texture=em.texture, vertexLight=em.vertexLight)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    tmp = args.out + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    os.replace(tmp, args.out)

    print("\n  criteres :")
    all_ok = True
    for k, v in crit.items():
        extra = {a: b_ for a, b_ in v.items() if a != "ok"}
        all_ok &= v["ok"]
        print(f"    [{'OK' if v['ok'] else 'ECHEC'}] {k} : {json.dumps(extra, ensure_ascii=False)[:160]}")
    print(f"\n  -> {args.out}  ({'TOUS LES CRITERES OK' if all_ok else 'ECHEC'})")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())

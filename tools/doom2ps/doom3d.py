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
sys.path.insert(0, os.path.join(ROOT, "tools"))

import wad as wadmod                                   # noqa: E402
import adjacency                                       # noqa: E402
import gridparts                                       # noqa: E402
import ordre                                           # noqa: E402
import cout                                            # noqa: E402
import doom_specials as sp                             # noqa: E402
from geom3d import (Emitter, TILESIZE, CAP_CELLS, plane_of, area2, quad_point,     # noqa: E402
                    CELL_MIN_U, CELL_MAX_U, CELL_MIN_V, CELL_MAX_V, SECTOR_LIGHT,
                    CELL_HARD_MAX, tile_count_1d, lire_optims,
                    GROS_BLOC as geom3d_GROS_BLOC)

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
# La marche et la chute de Doom (MAXSTEPMOVE 24, params/doom.cfg PLAYER_STEP), posees sur les
# portails. Le test de marche du moteur (bumpFloor, SPRITE.C:369) ne suffit pas : la sphere du joueur
# est centree sur l'oeil (41 u) et ROULE par-dessus toute arete plus basse que son centre
# (bumpWall, SPRITE.C:200-267), donc un rebord de fenetre de 25 a 40 u se franchissait.
#   monter > 24 : SHORTOPENING -- le joueur (BSHORT, AI.C:47) et les monstres (DOOM_ACTOR.C) le
#                 portent, pas les projectiles. PAS WALLFLAG_BLOCKED : newSprite le donne a TOUS les
#                 sprites (SPRITE.C:92) et une boule de feu ne passait plus par une fenetre.
#   chuter > 24 : CLIFFBNDRY -- seuls les monstres portent BCLIFF : P_TryMove refuse a un monstre
#                 un `floorz - dropoffz > 24`, le joueur saute ou il veut.
# Les portails d'un ascenseur sont recalcules en marche par doom_pbBlockBits (DOOM_GAME.C).
PLAYER_STEP_HEIGHT = 24.0

# Mur plus court que ca : son PLAN vient de la droite exacte qui le porte (l'etiquette d'adjacency),
# pas de ses sommets. Les sommets sont arrondis a l'entier, et sur un mur d'une unite l'arrondi
# choisit la direction : MESURE 2026-09-18, E1M2, le mur (-137, 759)-(-137, 760) d'un bord en
# diagonale sortait VERTICAL, et le prolongement de son plan coupait le secteur 43 a 39 u d'un
# imp -- pointInSectorP (SPRITE.C:940-960, qui teste TOUS les plans du secteur) le jugeait dehors.
# A 8 u l'arrondi tourne encore le plan de 7 degres ; au-dela, le plan des sommets est le bon.
MUR_COURT = 8.0

# FENETRES HORIZONTALES (wall_tex, choisir_fenetres). Une cellule porte `cu` colonnes de texture
# sur `L / tileLength` unites de monde : l'image est fausse du rapport des deux. Au-dela de cet
# ecart la face de linedef devient CANDIDATE a une tuile fabriquee pour elle.
# MESURE 2026-09-20 sur l'episode 1 : 31 % des murs textures (2 754 sur 8 917) depassent 25 %
# d'ecart. L'ancienne porte d'entree -- « linedef plus etroit que sa texture » -- n'en voyait
# qu'une partie : elle ne peut JAMAIS etre vraie pour une texture de 8 u (DOORTRAK, DOORSTOP,
# DOORBLU/RED/YEL : 381 murs de l'episode montrent 8 motifs la ou Doom en montre 1 ou 2, parce
# que CELL_MIN_U remonte cu a 64), ni pour les contremarches STEP* de 32 u (430 murs).
ECART_FENETRE = 0.02

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

# Les deux drapeaux de LIGNE de Doom qui bloquent le passage sans rien fermer (p_map.c
# PIT_CheckLine). Aucun n'etait lu : MESURE sur l'episode, 165 lignes a deux faces ML_BLOCKING et
# 108 ML_BLOCKMONSTERS. La plupart des ML_BLOCKING sont deja bloquees par la marche (> 24) ou
# portent une grille ; il en reste 31 qu'on franchissait pour de bon -- les rebords des deux
# batiments de la cour d'E1M2, la corniche d'E1M5, et les quatre cotes de la dalle de teleportation
# d'E1M8. Les ML_BLOCKMONSTERS, elles, ceinturent les nappes de nukage (E1M1 10 sur 13,
# E1M3 11 sur 19) : sans elles les monstres marchent dans l'acide.
#
# Le moteur n'a pas de bit libre : ses cinq bits de blocage (WALLFLAG_BLOCKBITS 0x1f00) sont pris,
# et le test est un ET entre le mur et le sprite. On reprend donc deux bits dont le SENS est deja
# le bon, et que Doom n'utilise pas autrement (ce jeu n'a pas d'eau) :
#   ML_BLOCKING      -> SHORTOPENING : porte par le joueur (AI.C:47) ET par les monstres
#                       (DOOM_ACTOR.C:313), pas par les projectiles. Ne touche ni la ligne de vue
#                       ni les balles (HITSCAN.C ne regarde que WALLFLAG_BLOCKED), ce qui compte :
#                       ces ouvertures font jusqu'a 176 u de haut, on tire au travers.
#                       Divergence assumee : Doom y arrete aussi les projectiles.
#   ML_BLOCKMONSTERS -> WATERBNDRY : seuls les monstres portent BWATERBNDRY. Exactement Doom.
# ATTENTION : doom_pbBlockBits (DOOM_GAME.C) recalcule SHORTOPENING sur les portails d'un push
# block en marche et effacerait un ML_BLOCKING pose la. Aucune des 31 lignes n'est dans ce cas
# (verifie sur l'episode) ; WATERBNDRY, lui, n'est jamais recalcule.
WALLFLAG_WATERBNDRY = 0x400
ML_BLOCKING = 0x0001
ML_BLOCKMONSTERS = 0x0002
PBVERT_MAX = 255            # sPBVertex.vNm est un unsigned char (SLEVEL.H:109-113)


class MobileTag:
    """Un secteur Doom qui bouge : la liste ORDONNEE des murs .LEV qui le composent (PBWall) et
    l'ensemble des sommets a la hauteur mobile (PBVert). Les sommets sont notes PAR ROLE au moment
    de l'emission (haut d'un mur de porte, bas d'un mur d'ascenseur, sol/plafond entier), jamais
    en cherchant une coordonnee apres coup : le plafond ferme d'une porte et son sol sont a 1 u.
    Seul `dy` est applique par le moteur (SPRITE.C:852-890)."""

    def __init__(self, sector, kind, lower, upper):
        self.sector = sector            # secteur Doom
        self.kind = kind                # 'door' | 'lift' | 'floor_*' | 'raise_*' (raise_floors)
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

    @property
    def monte(self):
        """Emis EN HAUT, charge EN BAS : l'objet (OT_DOOM_FLOOR) descend le push block de la course
        a la construction, donc l'etat du niveau a son chargement est `lower` (raise_floors)."""
        return self.kind.startswith("raise_")

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


def raise_floors(M, specials):
    """Monte les sols qui montent (doom_specials.RAISE) a leur DESTINATION dans la carte a emettre.

    Un sol qui monte est emis comme un ASCENSEUR EN HAUT, qui descendrait de sa course : c'est la
    geometrie que les ascenseurs d'E1M1 ont validee sur console -- parois de cage fixes que le sol
    cache, dalles rigides chez les voisins plus bas, fente de portail sous un plafond voisin. Emis EN
    BAS, les contremarches qui APPARAISSENT en montant n'existeraient pas (un quad de hauteur nulle
    est rejete). L'objet OT_DOOM_FLOOR descend le push block de la course au chargement : le niveau
    demarre dans l'etat du WAD. -> nombre de sols montes."""
    sects = M["sectors"]
    for r in specials.raises:
        sects[r["sector"]] = sects[r["sector"]]._replace(floorh=r["upper"])
    return len(specials.raises)


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


# FAUX CONTRASTE (r_bsp.c R_StoreWallRange) : Doom eclaire un mur selon la DIRECTION qu'il suit,
# pas seulement selon son secteur -- `if (v1->y == v2->y) lightnum--; else if (v1->x == v2->x)
# lightnum++`, soit un cran de LIGHTSEGSHIFT, exactement un cran de notre echelle 0..16. Sans lui
# tous les murs d'une piece ont la meme valeur et le relief disparait. Les flats n'en ont pas.
# Le moteur refait le meme calcul depuis la normale du mur a chaque changement de lumiere de
# secteur (DOOM_GAME.C doom_leafLight), sinon une piece animee le perdrait a son 1er clignotement.
def fake_contrast(light, P, Q):
    """lumiere 0..16 du mur P->Q (coordonnees Doom x, y), le cran de Doom applique."""
    if P[1] == Q[1]:
        light -= 1
    elif P[0] == Q[0]:
        light += 1
    return max(0, min(16, light))


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


# Decoupe en morceaux convexes, "grid" ou "bsp" (DoomConverter, gridparts.py). Global de module :
# make_e1m1 recree un DoomConverter pour placer les objets, il doit decouper exactement pareil.
PARTITION = "bsp"

# OPTIMISATIONS DE NIVEAU, debrayables une par une par --optim. Chacune MODIFIE la geometrie au-dela
# d'une simple conversion : qui edite ses cartes a la main doit pouvoir les couper une par une pour
# retrouver exactement ce qu'il a dessine. La liste des actives est recopiee dans le JSON de sortie
# (`source.optim`), donc un build dit toujours ce qu'il s'est autorise.
#   penombres : rend a sa piece toute bande de moins de 8 u qui n'en differe que par la lumiere
#               (dissoudre_penombres) -- la bande perd sa lumiere propre
#   avalement : une coupe de cellule qui ne laisserait qu'une echarde ne se fait pas, la voisine
#               s'etend sur le residu (geom3d._grid_cells) -- etirement borne a x1,25
#   fusion    : recolle deux feuilles du BSP d'un meme secteur Doom quand leur union est convexe
#               (gridparts.partition, g=None) -- elle DETRUIT des cordes du BSP, voir ci-dessous
#   ordre     : liste de paires de secteurs que le moteur ordonnerait au hasard, faute de portail
#               entre eux (tools/ordre.py) -- CORRIGE un defaut, elle n'ajoute aucun risque
#   bandes    : les faces d'un mur sont rangees dans l'ordre ou weldFaceStrip sait les souder
#               (tools/bandes.py) -- PERMUTATION pure, aucun sommet ne bouge, aucune face ne change
#   sommets   : deux murs dont un coin tombe au meme endroit partagent ce sommet (Emitter.partager_
#               coins) -- aucune geometrie ne bouge ; -40 Ko sur E1M1, -94 Ko sur E1M6 ; les
#               sommets des push blocks et des grilles de faces restent a eux
#   tuiles    : un niveau qui produit plus de tuiles que l'octet et la RAM ne lui en accordent en
#               fond les variantes d'une meme texture, les plus semblables d'abord (tools/tuiles.py,
#               applique par make_e1m1.budget_tuiles) -- un mur peut montrer le calage ou la hauteur
#               d'un voisin, jamais une autre texture ; SANS EFFET sous le budget (E1M1)
#
# `fusion` EST dans le defaut depuis le 18-09, apres deux verdicts contraires qu'il faut garder
# ecrits tous les deux. Elle enleve de l'information au moteur : les cordes du BSP ne portent
# aucune texture, mais elles portent l'ORDRE DU PEINTRE -- `buildTree` (WALLS.C:2128) refait a
# chaque image un graphe « s apres adjoin » a partir des portails tournes vers l'oeil, et une corde
# supprimee est une contrainte en moins. Quand ce graphe BOUCLE il se casse au hasard, son propre
# commentaire disant « this doesn't happen normally » puis « NOTE: this breaks the tree structure
# somewhat » (WALLS.C:2368-2385). L'A/B console du 16-09 (E1M1, salle tech) l'a donc accusee : la
# 1re version peignait les bandes haute et basse du mur du fond PAR-DESSUS le U qui les cache,
# selon le point de vue, et `--optim penombres,avalement` etait propre.
#
# C'ETAIT FAUX. Le defaut EXISTE sans elle -- le tonneau a travers la colonne de la 1re salle, le
# cadavre a travers un caisson, meme disque, meme jour -- et la fusion ne faisait que le DEPLACER.
# Deux secteurs sans portail commun n'ont jamais eu de contrainte, fusion ou pas (tools/ordre.py).
# Une fois la liste de paires en place, la fusion AIDE meme : moins de secteurs, donc moins de
# paires que la geometrie ne decide pas (182 -> 155), et 6 % de positions fautives contre 13 %
# sans elle. Elle vaut par ailleurs -7,2 % de secteurs et -4,8 % de murs (219/1796 contre
# 236/1886) -- mais PAS de cellules : 3 835 contre 3 870, soit -0,9 % (tools/cout.py). Recoller
# deux feuilles ne retire aucun sol a peindre, seulement des secteurs a traverser (42 visites
# medianes par position contre 45). Son gain est l'ORDRE, pas le remplissage.
#
# Elle reste DESTRUCTRICE, donc bornee et coupable. Deux gardes dans gridparts.partition : `troue()`
# refuse une piece percee, `--cap-canal` refuse une piece qui demanderait un canal de plans de coupe
# plus gros que le plus gros que Lobotomy ait livre. Et `--optim penombres,avalement,ordre` la
# coupe : qui edite ses cartes a la main retrouve le decoupage exact du BSP.
#   grossiers : DEGRADANTE, hors du defaut, demandee carte par carte (make_e1m1.OPTIM_CARTE) : les
#               carres pleins d'un flat se peignent par blocs de GROS_BLOC x GROS_BLOC puis 2 x 2,
#               une face par bloc, tuile etiree -- texel de 2 a 4 u, motif 2 a 4 fois plus grand
#               (geom3d.Emitter._gros_carres). Pour la carte que ses sols empechent de tourner.
OPTIMS = ("penombres", "avalement", "fusion", "ordre", "bandes", "sommets", "tuiles",
          "grossiers", "lumieres")
OPTIM_DEFAUT = ("penombres", "avalement", "fusion", "ordre", "bandes", "sommets", "tuiles",
                "lumieres")
OPTIM_ACTIFS = set(OPTIM_DEFAUT)
GROS_BLOC = geom3d_GROS_BLOC               # plus gros bloc de `grossiers`, en carres de TILESIZE

# Plafond de secteurs par canal de plans de coupe, quand `fusion` est active (gridparts.CAP_CANAL
# documente le chiffre). Global de module, comme PARTITION : make_e1m1 recree un DoomConverter pour
# placer les objets, il doit decouper exactement pareil.
CAP_CANAL = gridparts.CAP_CANAL


def lire_optim(s):
    """`defaut`, `all`, `none`, ou une liste d'OPTIMS (lecteur commun avec le convertisseur Duke)."""
    return lire_optims(s, OPTIMS, OPTIM_DEFAUT)


MAXCUTSECTORS = 128        # SLEVEL.H:172 ; plafond DUR, pour tout le niveau
SECFLAG_CUTSORT = 2        # SLEVEL.H:175


def plans_de_coupe(conv):
    """Rend au peintre l'ordre que la fusion lui a pris, avec le mecanisme PREVU PAR LE MOTEUR.

    `sortLeafList` (WALLS.C:2178-2226) corrige son tri par distance paire par paire : pour deux
    secteurs du MEME `cutChannel`, `level_cutPlane[cutIndex_1][cutIndex_2]` designe un MUR (de l'un
    ou de l'autre, bit 0x80) et le cote de son plan ou se trouve la camera decide qui passe devant.
    C'est un plan separateur par paire -- l'equivalent d'un BSP, tabule au lieu d'etre un arbre.
    MESURE sur les 24 .LEV retail : 129 secteurs marques sur 8 408 (1,5 %), dans 13 niveaux ;
    THOTH en a 82 sur 37 canaux ; la matrice est DENSE (entrees definies = n^2 exactement).

    Sans ca, recoller des feuilles casse l'ordre : un obstacle au milieu d'une piece entoure l'oeil
    d'un ANNEAU, aucun ordre lineaire n'existe, et le moteur casse la boucle au hasard (WALLS.C:2368
    « this doesn't happen normally »).

    Ce qu'on marque : toutes les feuilles des secteurs Doom ou une fusion a eu lieu, un canal par
    secteur Doom. Deux polygones convexes disjoints admettent TOUJOURS une droite separatrice portee
    par une arete de l'un des deux (axe separateur, en 2D), donc la table est toujours remplissable.
    -> (matrice [n][MAXCUTSECTORS], paires sans plan, pire depassement en u)."""
    em = conv.em
    fusionnes = {conv.leaf_sector[li] for li, m in enumerate(conv.members)
                 if len(m) > 1 and li in conv.remap}
    if not fusionnes:
        return [], 0, 0.0
    canal = {sec: i for i, sec in enumerate(sorted(fusionnes))}
    membres = defaultdict(list)                 # secteur Doom -> [secteurs .LEV]
    for li, sec in enumerate(conv.leaf_sector):
        if sec in canal and li in conv.remap:
            membres[sec].append(conv.remap[li])
    ordre = [s for sec in sorted(canal) for s in sorted(membres[sec])]
    if len(ordre) > MAXCUTSECTORS:
        raise SystemExit("plans de coupe : %d secteurs a trier, plafond %d (SLEVEL.H:172)"
                         % (len(ordre), MAXCUTSECTORS))
    idx = {s: i for i, s in enumerate(ordre)}
    for sec, liste in membres.items():
        for s in liste:
            em.sectors[s]["flags"] |= SECFLAG_CUTSORT
            em.sectors[s]["cutIndex"] = idx[s]
            em.sectors[s]["cutChannel"] = canal[sec]

    V = em.vertices

    def murs(s):
        """(offset depuis firstWall, normale XZ, point du plan) des murs VERTICAUX du secteur."""
        f = em.sectors[s]["firstWall"]
        out = []
        for wi in range(f, em.sectors[s]["lastWall"] + 1):
            w = em.walls[wi]
            if w["normal"][1] != 0 or wi - f >= 0x80:
                continue
            p = V[w["v"][0]]
            out.append((wi - f, w["normal"][0] / 65536.0, w["normal"][2] / 65536.0,
                        p["x"], p["z"]))
        return out

    def sommets(s):
        return [(V[i]["x"], V[i]["z"])
                for wi in range(em.sectors[s]["firstWall"], em.sectors[s]["lastWall"] + 1)
                if em.walls[wi]["normal"][1] == 0 for i in em.walls[wi]["v"]]

    mur_de = {s: murs(s) for s in ordre}
    pts_de = {s: sommets(s) for s in ordre}

    def separateur(a, b):
        """Le mur de `a` dont le plan laisse le MIEUX tout `b` du cote exterieur -> (offset, pire
        depassement en u). La normale du .LEV pointe vers l'interieur du secteur (critere
        normales_vers_interieur).

        On minimise le depassement au lieu d'exiger zero : deux morceaux qui se touchent par une
        arete courte sont presque TANGENTS au plan de cette arete, et l'arrondi des normales en
        16.16 fait basculer le signe (MESURE : secteurs 148/153 d'E1M1, depassement 0,8 u sur une
        arete de 8 u). Ca reste juste : le moteur ne teste que la CAMERA contre ce plan, jamais les
        sommets -- le plan doit dire de quel cote on est, pas separer au sens strict."""
        best = None
        for off, nx, nz, px, pz in mur_de[a]:
            d = max(nx * (x - px) + nz * (z - pz) for x, z in pts_de[b])
            if best is None or d < best[1]:
                best = (off, d)
        return best

    M = [[99] * MAXCUTSECTORS for _ in ordre]
    pire = 0.0
    for sec, liste in membres.items():
        ls = sorted(liste)
        for k, a in enumerate(ls):
            for b in ls[k + 1:]:
                i, j = idx[a], idx[b]
                oa, da = separateur(a, b) or (None, 1e18)
                ob, db = separateur(b, a) or (None, 1e18)
                if da <= db and oa is not None:     # le mur est dans a
                    M[i][j] = oa                    # ligne i : a est s1, le mur est dans s1
                    M[j][i] = 0x80 | oa             # ligne j : a est s2, d'ou le bit 0x80
                    pire = max(pire, da)
                elif ob is not None:                # le mur est dans b
                    M[i][j] = 0x80 | ob
                    M[j][i] = ob
                    pire = max(pire, db)
    # une entree 99 lue ferait sauter `assert(plane!=99)` (WALLS.C:2197) : on verifie qu'il n'en
    # reste aucune dans une paire du MEME canal, seules paires que le moteur lit.
    sans = sum(1 for sec, liste in membres.items() for a in liste for b in liste
               if a != b and M[idx[a]][idx[b]] == 99)
    return M, sans, pire


# Nom de texture RESERVE : aucun WAD n'en contient, et doomtiles lui fabrique un damier criard.
DIAG_TEX = "ZZFUSION"


def peindre_fusions(conv):
    """--diag-fusion : repeint en damier tous les murs VERTICAUX des feuilles nees d'une fusion.

    Instrument, pas optimisation. Il repond a UNE question, celle qu'aucune sonde statique n'a su
    trancher : le mur fautif appartient-il a une feuille fusionnee, ou le voit-on A TRAVERS une
    feuille fusionnee ? Une capture y repond d'un coup d'oeil.
    On laisse les sols et les plafonds intacts : repeindre un sol masquerait le mur qu'on cherche."""
    em = conv.em
    t = em.tile((conv.picnum("tex", DIAG_TEX), 0, 0, 1, 1))
    n = 0
    for li, membres in enumerate(conv.members):
        if len(membres) < 2 or li not in conv.remap:
            continue
        s = em.sectors[conv.remap[li]]
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = em.walls[wi]
            nc = w["tileLength"] * w["tileHeight"]
            if w["normal"][1] != 0 or nc <= 0:
                continue
            # On AJOUTE un bloc de cellules et on y pointe le mur, au lieu d'ecraser le sien : deux
            # murs qui portent la meme texture PARTAGENT leur bloc, et ecraser en place repeignait
            # des murs qu'on n'avait pas choisis (MESURE : 533 murs touches pour 265 demandes).
            o = len(em.texture)
            for _ in range(nc):
                em.texture.append(0)               # motif 0 : aucun miroir
                em.texture.append(t)
            w["textures"] = o
            n += 1
    return n


# Flats animes de Doom, dans l'ordre des images (p_spec.c animdefs ; SWATER, RROCK et SLIME
# n'existent qu'a partir de Doom II : une famille absente du WAD n'est jamais utilisee).
ANIM_FLATS = [["NUKAGE1", "NUKAGE2", "NUKAGE3"], ["FWATER1", "FWATER2", "FWATER3", "FWATER4"],
              ["SWATER1", "SWATER2", "SWATER3", "SWATER4"], ["LAVA1", "LAVA2", "LAVA3", "LAVA4"],
              ["BLOOD1", "BLOOD2", "BLOOD3"], ["RROCK05", "RROCK06", "RROCK07", "RROCK08"],
              ["SLIME01", "SLIME02", "SLIME03", "SLIME04"], ["SLIME05", "SLIME06", "SLIME07", "SLIME08"],
              ["SLIME09", "SLIME10", "SLIME11", "SLIME12"]]


class DoomConverter:
    def __init__(self, M, sizes, cap_cells=CAP_CELLS, mobile=None, switch_lines=None, uwin=None,
                 partition=None):
        """`mobile` : {secteur Doom: MobileTag} (--mobile) ; `switch_lines` : {linedef: dict(channel,
        special)} des interrupteurs S dont il faut isoler la tuile (doom_specials.specials_of)."""
        self.M = M
        self.bsp = Bsp(M)
        self.em = Emitter()
        if "avalement" not in OPTIM_ACTIFS:
            self.em.eps_avale = 0.0           # --optim : l'auteur refuse l'avalement des echardes
        self.em.bandes = "bandes" in OPTIM_ACTIFS
        self.em.gros_carres = GROS_BLOC if "grossiers" in OPTIM_ACTIFS else 0
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
        # fenetres horizontales (wall_tex) : `uwin` = {(linedef, cote)} autorises, None = tous,
        # en notant pour chacun [gain, {cles de tuile AVEC fenetre}, nom de texture,
        # {cle SANS fenetre: nombre de cellules}] -- le 1er passage de main, voir choisir_fenetres
        self.uwin = uwin
        self.uwin_use = defaultdict(lambda: [0.0, set(), None, Counter()])
        self.uwin_wall = {}                   # id(mur) -> face de linedef (compter_fenetres)
        self.uwin_base = Counter()            # cle SANS fenetre -> cellules qui s'en servent
        self.door_faces = set()               # FACES DE PORTE (linedef, cote) : linteau mobile
        self.anim_keys = []                   # [[cle de tuile de chaque image]] par famille animee
        self.anims = []                       # idem en index de tuile, apres compaction
        # Polygones ETIQUETES + frontieres exactes : voir tools/doom2ps/adjacency.py. La
        # reciprocite des portails y est vraie par construction, ce qui supprime l'echantillonnage
        # de voisinage de la 1re version (58 portails a sens unique, 31 trous, MESURE sur E1M1).
        sub_sector = []
        for si, ss in enumerate(M["subsectors"]):
            sg = M["segs"][ss.first]
            ld = M["linedefs"][sg.line]
            sd = ld.right if sg.side == 0 else ld.left
            sub_sector.append(M["sidedefs"][sd].sector if sd >= 0 else 0)
        # « Feuille » = un morceau convexe du .LEV. PARTITION "grid" (defaut) : les feuilles du BSP
        # recoupees sur la grille de 64 puis refusionnees par secteur (gridparts.py) -- un carre
        # plein n'est jamais coupe. "bsp" : les feuilles du BSP telles quelles. `members` = les
        # feuilles BSP d'origine de chaque morceau (leurs segs, et leaf_at).
        self.partition = partition or PARTITION
        # `fusion` (--optim) : recoller deux feuilles voisines d'un MEME secteur Doom quand leur
        # union reste convexe. Chaque morceau que le BSP laisse coute un secteur, ses murs et une
        # visite du renderer -- mais ses cordes ne sont PAS libres pour autant : elles portent
        # l'ordre de dessin du peintre, et c'est `troue()` + `cap` qui rendent la fusion sure
        # (gridparts.partition, et l'entete OPTIMS ci-dessus pour toute l'histoire).
        fusionner = self.partition == "grid" or "fusion" in OPTIM_ACTIFS
        rings = adjacency.leaf_polygons(M)
        # ce qu'une feuille du BSP deborde hors de son secteur retourne a son vrai secteur
        # (adjacency.reattribuer_debords) -- avant la fusion, qui recollerait le debord
        rings, secs, membres, debords = adjacency.reattribuer_debords(M, rings, sub_sector)
        if debords:
            self.stats["feuilles_debordantes_coupees"] += debords
        if fusionner:
            parts = gridparts.partition(M, rings, secs, self._ring_in_map,
                                        g=64 if self.partition == "grid" else None, cap=CAP_CANAL,
                                        membres=membres)
        else:
            parts = list(zip(rings, secs, membres))
        # Un morceau trop grand pour UN sol (gridparts.CAP_PLAT : slave et vCalc[700]) part en
        # plusieurs morceaux. Ce n'est pas une optimisation : sans cela le moteur ecrit hors de
        # vCalc. adjacency.build(M, polys=leaf_polygons(M)) est adjacency.build(M) a l'octet pres.
        parts, coupes = gridparts.decouper_gros(parts)
        if coupes:
            self.stats["morceaux_trop_gros_coupes"] += coupes
        self.fpolys, self.boundary = adjacency.build(M, polys=[r for r, _, _ in parts])
        self.leaf_sector = [s for _, s, _ in parts]
        self.members = [m for _, _, m in parts]
        self._pieces_of_leaf = defaultdict(list)
        for li, m in enumerate(self.members):
            for si in m:
                self._pieces_of_leaf[si].append(li)
        self.polys = {}
        for li, edges in self.boundary.items():
            ring = [P for (P, Q, tag, ta, tb, nb, nother) in edges]
            if len(ring) >= 3:
                self.polys[li] = ring
        # index des segs par feuille et par ligne canonique : dit EXACTEMENT si une sous-arete
        # est portee par un linedef (donc texturee) ou si c'est une corde de partition.
        V = M["vertices"]
        sub_segs = {}
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
            sub_segs[si] = d
        self.segidx = {}
        for li, m in enumerate(self.members):
            d = {}
            for si in m:
                for key, lst in sub_segs[si].items():
                    d.setdefault(key, []).extend(lst)
            self.segidx[li] = d
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

    def wall_tex(self, name, hauteur=None, voff=0, cadre=None, masked=False):
        """Descripteur de placage E4.1c : la cellule porte SES PROPRES mesures, hauteur et largeur.

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
        d = dict(mode="wall", pic=p, cu=cu, cv=cv, ncx=1, ncy=1,
                 ox=0, oz=0, dx=1, dz=0, yref=0, pu=0, pv=0, flipx=False, flipy=False,
                 keyed_cell=True, voff=int(voff) % h)
        if masked:
            d["mask"] = True                    # grille : la tuile garde ses trous
        if cadre is not None:
            # FENETRE HORIZONTALE : la tuile porte les colonnes que Doom montre VRAIMENT sur cette
            # cellule (Emitter.cell_tile, cle `uwin`), au lieu des `cu` colonnes de la texture.
            # C'est le jumeau horizontal de la regle de hauteur ci-dessus -- et la cellule reste
            # celle qu'on aurait posee sans fenetre, donc le NOMBRE de cellules ne bouge pas :
            # une fenetre coute une tuile, jamais une milliseconde.
            # La candidature se decide sur l'ECART D'ECHELLE REEL, pas sur « linedef plus etroit
            # que sa texture » (l'ancienne regle, qui ne pouvait pas etre vraie pour DOORTRAK ou
            # les contremarches, voir ECART_FENETRE).
            sx, sy, dx, dz, xoff, L, ll, ident = cadre
            if w < CELL_MIN_U and L <= CELL_MIN_U + 0.5:
                # TEXTURE ETROITE SUR MUR COURT : UNE repetition par cellule, et la fenetre toujours,
                # hors selection et hors budget. Sinon CELL_MIN_U fait porter 64 colonnes a la
                # tuile (8 motifs de DOORRED dans 16 u), et une fenetre par largeur ne suffit pas :
                # MESURE 2026-09-21 sur le disque, E1M2, 22 murs de cadre sur 60 avaient leur fenetre
                # FONDUE par doomtiles.reduire avec une variante voisine (DOORRED demande a 16
                # colonnes, montre a 32 : le double des bandes rouges). Le reducteur pese une fusion
                # par l'aire du mur, et un cadre de porte est le plus petit mur du niveau -- donc
                # toujours le premier sacrifie. Une repetition par cellule donne a TOUTES les bandes
                # d'une texture la meme tuile (a hauteur et calage egaux) : plus rien a fondre.
                # Ces textures ne sont JAMAIS posees sur plus de 64 u dans l'episode 1 (DOORTRAK,
                # DOORSTOP, DOORRED/BLU/YEL, LITE*, BRNBIG*) ; les contremarches STEP* le sont et
                # gardent la regle generale. Prix : des cellules (16 u -> 2, 32 u -> 4), pas de tuile.
                cu = int(w)
                d.update(cu=cu, ox=sx, oz=sy, dx=dx, dz=dz, uwin=True, uoff=xoff, uw=int(w))
                return d, p
            cell = L / tile_count_1d(L, cu)
            ecart = max(cell, cu) / min(cell, cu)
            if ecart - 1.0 > ECART_FENETRE and (self.uwin is None or ident in self.uwin):
                d.update(ox=sx, oz=sy, dx=dx, dz=dz, uwin=True, uoff=xoff, uw=int(w),
                         uwin_id=ident, squash=ecart)
        return d, p

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

    def seg_near(self, leaf, P, Q, tol=1.5, other=None):
        """Seg de `leaf` dont le LINEDEF porte l'arete PQ a `tol` pres, pour une arete sans voisin
        ni seg exact. seg_on_edge exige la MEME droite canonique ; or une feuille peut etre bornee
        par la droite d'un NOEUD tiree d'un seg que le nodebuilder a arrondi (5 % des segs ne sont
        pas sur leur linedef) : l'arete longe alors un linedef a une face a < 1 u sans le
        reconnaitre, et sortait en bord de carte INVISIBLE. MESURE E1M1 : 23 murs pleins
        invisibles, tous a <= 0,8 u d'un linedef a une face (dont le flanc de l'ascenseur 269)."""
        M = self.M
        V = M["vertices"]
        cands = [M["segs"][k] for si in self.members[leaf]
                 for k in range(M["subsectors"][si].first,
                                M["subsectors"][si].first + M["subsectors"][si].count)]
        # puis les linedefs a UNE face du meme secteur Doom : le seg peut appartenir a une feuille
        # voisine ecartee hors carte (E1M1 : 2 bords de 1 et 9 u contre l'eclat 161)
        sec = self.leaf_sector[leaf]
        for i, ld in enumerate(M["linedefs"]):
            if M["sidedefs"][ld.right].sector == sec:
                cands.append(M["segs"][0]._replace(line=i, side=0))
            elif ld.left >= 0 and M["sidedefs"][ld.left].sector == sec:
                cands.append(M["segs"][0]._replace(line=i, side=1))
        for sg in cands:
            ld = M["linedefs"][sg.line]
            if other is not None:
                # voisine d'un autre secteur : le linedef doit SEPARER les deux secteurs, sinon on
                # prend un linedef presque colineaire d'a cote (mur fantome de 44 u, feuille 159)
                so = ld.left if sg.side == 0 else ld.right
                if so < 0 or M["sidedefs"][so].sector != other:
                    continue
            (ax, ay), (bx, by) = V[ld.v1], V[ld.v2]
            L = math.hypot(bx - ax, by - ay)
            if L < 1:
                continue
            ux, uy = (bx - ax) / L, (by - ay) / L
            if all(abs((X[0] - ax) * uy - (X[1] - ay) * ux) <= tol for X in (P, Q)):
                t = sorted((X[0] - ax) * ux + (X[1] - ay) * uy for X in (P, Q))
                if t[0] >= -tol and t[1] <= L + tol:
                    return sg
        return None

    # -- emission ------------------------------------------------------------------------
    def sky(self, sec):
        return sec.ceilpic == SKYFLAT

    def emit_wall(self, P, Q, bot, top, *, next_sector, tex, picnum, light, invisible,
                  centre=None, mob=None, open_height=None, droite=None, force_blocked=None):
        """Emet un mur vertical. `mob` = [(MobileTag, 'top'|'bottom')] : les sommets de cette
        arete rejoignent le push block ; `open_height` : la tuile des cellules est refaite pour
        cette hauteur (etat OUVERT d'un mur qui grandit : rails de porte, contremarche d'ascenseur).
        `droite` : l'etiquette (a, b, c) de la droite qui porte l'arete (voir MUR_COURT).
        Retourne la liste des index de murs emis (un mur trop grand est decoupe)."""
        if top - bot <= 0:
            return []
        light = fake_contrast(light, P, Q)   # le cran de Doom selon la direction du mur
        normale = None
        if droite is not None and centre is not None and math.dist(P, Q) < MUR_COURT:
            # orientee vers le centre du morceau (convexe, donc du bon cote de SA droite), et non
            # d'apres le quad arrondi, qui ment justement sur ces murs
            h = math.hypot(droite[0], droite[1]) or 1.0
            nx, nz = droite[0] / h, droite[1] / h
            if nx * (centre[0] - (P[0] + Q[0]) / 2.0) + nz * (centre[1] - (P[1] + Q[1]) / 2.0) < 0:
                nx, nz = -nx, -nz
            normale = (nx, nz)
            self.stats["murs_courts_plan_exact"] += 1
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
                               cap_cells=self.cap, stats=self.stats, tex=tex, normale=normale,
                               force_blocked=force_blocked)
        if tex is not None and tex.get("uwin_id") is not None:
            # GAIN de la fenetre : l'aire fausse, ponderee par l'ecart d'echelle. Les TUILES,
            # elles, sont comptees a la fin (compter_fenetres) : ici les cellules d'un mur a faces
            # ne sont pas encore posees, et surtout il faut compter ce qu'une fenetre LIBERE.
            rec = self.uwin_use[tex["uwin_id"]]
            rec[0] += (top - bot) * math.dist(P, Q) * (tex["squash"] - 1.0)
            rec[2] = self.picnames[tex["pic"]][1]
            for wi in idx:
                # par IDENTITE : emit_leaf remet les portails en tete, les index bougent encore
                self.uwin_wall[id(self.em.walls[wi])] = tex["uwin_id"]
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
                    key = key[:5] + (round(float(open_h), 2),) + key[6:]
                    em.texture[w["textures"] + 2 * c + 1] = em.tile(key)
        self.stats["cellules_rekeyees_ouvert"] += 1

    def _note_switch(self, sg, leaf, idx, name, bot, top, P, Q):
        if sg.line in self.switch_lines and sg.side == 0 and idx:
            self.switch_hits[sg.line].append(dict(leaf=leaf, walls=[self.em.walls[i] for i in idx],
                                                  name=name, bot=bot, top=top, P=P, Q=Q))

    def leaf_at(self, x, y):
        """Le morceau qui contient (x, y) : la feuille du BSP, puis celui de ses morceaux dont
        l'anneau (convexe) contient le point."""
        lf = self.bsp.leaf_at(x, y)
        cands = self._pieces_of_leaf.get(lf, [])

        def ecart(li):                    # pire depassement hors du convexe, 0 = dedans
            r = self.fpolys[li]
            o = 1.0 if gridparts.area2(r) > 0 else -1.0
            n = len(r)
            pire = 0.0
            for i in range(n):
                ex, ey = r[(i + 1) % n][0] - r[i][0], r[(i + 1) % n][1] - r[i][1]
                L = math.hypot(ex, ey) or 1.0
                cr = (ex * (y - r[i][1]) - ey * (x - r[i][0])) * o / L
                pire = max(pire, -cr)
            return pire
        for li in cands:
            if ecart(li) <= 1e-6:
                return li
        # la feuille du BSP et nos anneaux different d'un arrondi : on prend le morceau (de tout
        # le niveau, en gardant la carte) qui contient le point, sinon le plus proche
        tous = [li for li in range(len(self.fpolys)) if li in self.polys]
        return min(cands + tous, key=ecart) if (cands or tous) else -1

    def plafonds_pleins(self):
        return self._cellules_pleines(True)

    def sols_pleins(self):
        """Idem au SOL, mais un carre ne passe que s'il est dans UN SEUL secteur Doom.

        Un plafond continu couvre des sols de hauteurs differentes (les marches) ; un SOL qui
        deborde chez un voisin de meme hauteur, lui, passe DERRIERE un monstre debout dessus quand
        le voisin est peint apres (c'est le bug « monstres enfonces », WALLS.C doom_spriteLeaves).
        On se limite donc aux cordes INTERNES d'un secteur Doom -- la ou doom_spriteLeaves sait
        deja rendre un sprite a la feuille peinte en dernier -- et la portee de ce rattrapage est
        portee au debord (64 u)."""
        return self._cellules_pleines(False)

    def _cellules_pleines(self, up):
        """Cellules de 64 dont chaque morceau qui y touche peint le CARRE ENTIER de son plafond.

        Un morceau est convexe ; une piece de Doom ne l'est pas, et ses marches la coupent en
        secteurs de sols differents sous UN MEME plafond. Chaque morceau recevait sa part de
        cellule, tuile entiere etiree dedans (« triangles » du testeur, 16-09). MESURE E1M1 : 7,6 %
        de l'aire des plafonds dans des carres coupes par un secteur de MEME plafond, 2 % par une
        corde du meme secteur -- et 51 % pour la salle aux marches (secteur Doom 60).
        Condition : le carre est couvert en entier par des secteurs de meme (hauteur, flat,
        lumiere), ni ciel ni mobile, et aucun linedef a une face, a texture du milieu ou entre
        deux plafonds differents ne le traverse. Le debord est alors COPLANAIRE a un vrai plafond de
        Doom : un rayon parti d'un oeil sous le plafond qui atteint ce plan y rencontre ce plafond,
        rien sous le plan ne peut etre derriere, et RECTCLIP borne chaque copie a la fenetre de son
        morceau -- chacun peint le meme carre, visible des qu'un des deux l'est."""
        cache = getattr(self, "_pleins", None)
        if cache is None:
            cache = self._pleins = {}
        if up in cache:
            return cache[up]
        M, T = self.M, TILESIZE
        S, SD, DV = M["sectors"], M["sidedefs"], M["vertices"]

        def cle(s):
            sec = S[s]
            if s in self.mobile or (up and self.sky(sec)):
                return None
            return (sec.ceilh, sec.ceilpic, sec.light) if up else s

        def rect(ring, x0, x1, y0, y1):
            p = [(q[0], q[1]) for q in ring]
            for ax, c, sg in ((0, x0, 1), (0, x1, -1), (1, y0, 1), (1, y1, -1)):
                out = []
                for i in range(len(p)):
                    a, b = p[i - 1], p[i]
                    ia, ib = (a[ax] - c) * sg >= 0, (b[ax] - c) * sg >= 0
                    if ia != ib:
                        t = (c - a[ax]) / float(b[ax] - a[ax])
                        out.append((a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])))
                    if ib:
                        out.append(b)
                p = out
                if len(p) < 3:
                    return []
            return p

        aire = defaultdict(lambda: defaultdict(float))
        for li in self.keep:
            ring, k = self.polys[li], cle(self.leaf_sector[li])
            xs, ys = [q[0] for q in ring], [q[1] for q in ring]
            for gx in range(int(math.floor(min(xs) / T)), int(math.ceil(max(xs) / T))):
                for gy in range(int(math.floor(min(ys) / T)), int(math.ceil(max(ys) / T))):
                    c = rect(ring, gx * T, gx * T + T, gy * T, gy * T + T)
                    if c:
                        aire[(gx, gy)][k] += abs(area2(c)) / 2.0
        pleins = set()
        for g, d in aire.items():
            k, a = max(d.items(), key=lambda kv: kv[1])
            if k is not None and a >= T * T - 1 and sum(d.values()) - a < 0.5:
                pleins.add(g)
        for ld in M["linedefs"]:
            ss = [SD[i] for i in (ld.right, ld.left) if i >= 0]
            if (len(ss) == 2 and cle(ss[0].sector) is not None
                    and cle(ss[0].sector) == cle(ss[1].sector)
                    and all(sd.middle in ("-", "") for sd in ss)):
                continue
            (ax, ay), (bx, by) = DV[ld.v1], DV[ld.v2]
            for gx in range(int(math.floor(min(ax, bx) / T)), int(math.floor(max(ax, bx) / T)) + 1):
                for gy in range(int(math.floor(min(ay, by) / T)), int(math.floor(max(ay, by) / T)) + 1):
                    if (gx, gy) not in pleins:
                        continue
                    # Liang-Barsky : le milieu du morceau de segment dans le carre est STRICTEMENT
                    # interieur ssi le linedef traverse le carre (et ne le longe pas sur un bord)
                    t0, t1 = 0.0, 1.0
                    for p_, q_ in ((-(bx - ax), ax - gx * T), (bx - ax, gx * T + T - ax),
                                   (-(by - ay), ay - gy * T), (by - ay, gy * T + T - ay)):
                        if p_ == 0:
                            if q_ < 0:
                                t0, t1 = 1.0, 0.0
                        elif p_ < 0:
                            t0 = max(t0, q_ / float(p_))
                        else:
                            t1 = min(t1, q_ / float(p_))
                    if t1 <= t0:
                        continue
                    tm = (t0 + t1) / 2.0
                    mx, my = ax + tm * (bx - ax), ay + tm * (by - ay)
                    if gx * T + 1e-6 < mx < gx * T + T - 1e-6 and gy * T + 1e-6 < my < gy * T + T - 1e-6:
                        pleins.discard((gx, gy))
        self.stats["cellules_%s_pleines" % ("plafond" if up else "sol")] = len(pleins)
        cache[up] = pleins
        return pleins

    def _dans_la_carte(self, li):
        return self._ring_in_map(self.polys[li], count=True)

    def _ring_in_map(self, ring, count=False):
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
            if count:
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
                                parallax=False, light=light, stats=self.stats, tex=ftex,
                                pleins=self.sols_pleins())
        if self.sky(sec):
            cidx = self.em.add_flat(pxz, lambda x, z: ch, is_floor=False, picnum=0,
                                    parallax=True, light=light, stats=self.stats, tex=None)
        else:
            ctex, cpic = self.flat_tex(sec.ceilpic)
            cidx = self.em.add_flat(pxz, lambda x, z: ch, is_floor=False, picnum=cpic,
                                    parallax=False, light=light, stats=self.stats, tex=ctex,
                                    pleins=self.plafonds_pleins())
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
        # floorLevel est l'etat CHARGE : les things s'y posent (things2objects) et post_flags s'y
        # mesure. Un sol qui monte est emis en haut mais charge en bas (MobileTag.monte).
        niveau = (lvl // nl) if nl else fh
        if ms is not None and ms.monte:
            niveau = ms.lower
        self.em.sectors.append(dict(
            object=0, center=[acc[0] // cnt, acc[1] // cnt, acc[2] // cnt],
            floorLevel=niveau,
            firstWall=first, lastWall=last, light=SECTOR_LIGHT, flags=0,
            cutIndex=0, cutChannel=0, rejectClass=0))

    def emit_edge(self, leaf, P, Q, tag, ta, tb, nb, sec, fh, ch, light, cen):
        M = self.M

        def emit_wall(*args, **kw):          # tous les murs de l'arete portent SA droite (MUR_COURT)
            return self.emit_wall(*args, droite=tag, **kw)
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
        # Sans seg exact, l'arete n'est une corde que si la voisine est du MEME secteur Doom. Sinon
        # c'est un linedef que seg_on_edge a rate (droite de noeud arrondie) : bord de carte ->
        # mur invisible (23 sur E1M1), secteur voisin -> portail pleine hauteur SANS contremarche
        # ni linteau (8 sur E1M1 : les tranches du chemin au-dessus du nukage, flanc d'ascenseur).
        if sg is None and not same:
            sg = self.seg_near(leaf, P, Q, other=self.leaf_sector[nb] if nbi >= 0 else None)
            if sg is None and math.dist(P, Q) < MUR_COURT:
                # Arete COURTE : le chanfrein qu'une ligne de noeud taille en passant a moins de 3 u
                # d'un coin. Ses deux bouts ne sont pas a 1,5 u d'un MEME linedef, et elle sortait
                # en mur plein INVISIBLE -- MESURE 2026-09-18, E1M7, coin (-64, -1792) du secteur
                # Doom 129 : 4,5 u d'ou l'on voyait le vide. A 3 u elle prend le mur du coin.
                sg = self.seg_near(leaf, P, Q, tol=3.0,
                                   other=self.leaf_sector[nb] if nbi >= 0 else None)
            if sg is not None:
                self.stats["bords_rattrapes_par_linedef"] += 1
            elif nbi >= 0:
                self.stats["corde_entre_secteurs_sans_linedef"] += 1

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
                emit_wall(P, Q, fh, ch, next_sector=-1, tex=None,
                               picnum=self.picnum("tex", "-"), light=light, invisible=True,
                               centre=cen, mob=own(fh, ch))
            else:
                emit_wall(P, Q, fh, ch, next_sector=nbi, tex=None, picnum=0,
                               light=light, invisible=True, centre=cen, mob=own(fh, ch))
                self.stats["portails_chord"] += 1
            return

        ld = M["linedefs"][sg.line]
        sd_i = ld.right if sg.side == 0 else ld.left
        op_i = ld.left if sg.side == 0 else ld.right
        side = M["sidedefs"][sd_i] if sd_i >= 0 else None
        other = M["sidedefs"][op_i] if op_i >= 0 else None
        # CALAGE HORIZONTAL (r_segs.c) : colonne = xoff du sidedef + distance le long du linedef
        # depuis son debut VU DE CE COTE (v1 pour la face droite, v2 pour la gauche).
        a_, b_ = (ld.v1, ld.v2) if sg.side == 0 else (ld.v2, ld.v1)
        (sx_, sy_), (ex_, ey_) = M["vertices"][a_], M["vertices"][b_]
        ll_ = math.hypot(ex_ - sx_, ey_ - sy_) or 1.0
        cadre = (sx_, sy_, (ex_ - sx_) / ll_, (ey_ - sy_) / ll_, side.xoff if side else 0,
                 math.dist(P, Q), ll_, (sg.line, sg.side))

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
            tex, pic = self.wall_tex(name, top_ - bot_, v, cadre)
            idx = emit_wall(P, Q, bot_, top_, next_sector=-1, tex=tex, picnum=pic,
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
        if lift_self and nfh > ms.lower and not mn_lift:
            # paroi de la cage vue DE l'ascenseur une fois descendu : fixe du bas de course au sol
            # du voisin. Doom ancre cette texture `lower` au sol du voisin (defaut) ou a notre
            # plafond (DONTPEGBOTTOM), deux bords fixes ; notre sol cache ce qui est dessous.
            # Remplace la contremarche dont le bas suivait le sol (etiree en descendant) et la
            # fente de 1 u du cote voisin (tournee vers le voisin, jamais vue de la cage).
            # Contre un voisin qui BOUGE aussi, le haut de cette paroi n'est plus fixe : c'est la
            # contremarche ci-dessous, bas avec nous, haut avec lui. MESURE 2026-09-18 : sans ce
            # garde, deux sols qui montent ensemble (E1M3, secteurs 48 et 49) et chaque marche
            # d'un escalier se dressaient au chargement un mur de toute leur course (120 u).
            name = side.lower if side and side.lower != "-" else "BROWN1"
            h = self.tex_h(name)
            hb = min(nfh, ch)
            v = ((ch - hb) % h if pegbot else 0) + yoff
            tex, pic = self.wall_tex(name, hb - ms.lower, v, cadre)
            idx = emit_wall(P, Q, ms.lower, hb, next_sector=-1, tex=tex, picnum=pic,
                                 light=light, invisible=False, centre=cen, mob=None)
            self._note_switch(sg, leaf, idx, name, ms.lower, hb, P, Q)
            self.stats["parois_cage"] += 1
        elif nfh > fh:                                 # marche montante : contremarche
            name = side.lower if side and side.lower != "-" else "BROWN1"
            h = self.tex_h(name)
            # GCC14: LE MUR QUI S'ETEND. Une PORTE est emise fermee, plafond a 1 u de son sol, et
            # ce plafond est temporaire. Clamper la contremarche dessus la faisait finir sur CE
            # plafond, donc own() lui donnait le role `top` et elle montait AVEC la porte : un mur
            # plein de toute la course, dresse exactement la ou l'ouverture devait apparaitre. Vu
            # sur console 2026-09-24 (« j'entends l'action, mais un mur s'etend la ou je devrais
            # voir un portail »), E1M3 secteur 134 et E1M5 secteurs 50, 52, 64, 82, 122. Le plafond
            # qui borne une contremarche est celui de la porte OUVERTE : la marche a sa vraie
            # hauteur, fixe, et si le voisin est une plate-forme elle la suit (cas mn_lift ci-
            # dessous, qui ne pouvait pas se declencher tant que hb etait coince a 1 u).
            plafond = ms.upper if (ms is not None and ms.kind == "door") else ch
            hb = min(nfh, plafond)
            # bas : `mid` = sol du voisin (defaut) ou plafond de devant (DONTPEGBOTTOM)
            v = ((ch - hb) % h if pegbot else 0) + yoff
            tex, pic = self.wall_tex(name, hb - fh, v, cadre)
            mob = own(fh, hb)
            if mn_lift and hb == nfh:
                # plate-forme voisine plus haute que nous : Doom ancre la texture a SON sol, qui
                # descend -> dalle rigide, ce qui passe sous notre sol est cache par lui
                mob.append((mn, "top") if ms is not None else (mn, "rigid"))
            elif mn_lift and ms is None and nfh > ch and mn.lower < ch:
                # plate-forme dont le HAUT depasse notre plafond (E1M8, couloir 26 sous
                # l'ascenseur 28 : plafond 0, plate-forme 8, bas de course -96). La contremarche
                # coupee a notre plafond ne touchait pas la plate-forme : mur FIXE de -96 a 0, qui
                # bouchait le passage une fois l'ascenseur descendu (vu sur console 2026-09-19,
                # « le joueur reste coince dans le couloir », l'interrupteur de ce mur marchait).
                # Dalle rigide jusqu'a SON sol : les 8 u au-dessus de notre plafond sont caches
                # par lui, et en bas de course la dalle entiere passe sous notre sol.
                hb = nfh
                tex, pic = self.wall_tex(name, hb - fh, v, cadre)
                mob = own(fh, hb) + [(mn, "rigid")]
                self.stats["contremarches_sous_plafond"] = self.stats.get("contremarches_sous_plafond", 0) + 1
            idx = emit_wall(P, Q, fh, hb, next_sector=-1, tex=tex, picnum=pic,
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
                if mn_door and hb == nch:
                    # CETTE face de linedef est une face de porte -- pas toutes celles qui portent
                    # la meme texture (choisir_fenetres les laissait toutes passer hors budget :
                    # MESURE 2026-09-20, STARTAN2 sert UNE fois de linteau en E1M2 et gagnait 58
                    # fenetres partout dans la carte ; 446 tuiles demandees au lieu de 285).
                    self.door_faces.add((sg.line, sg.side))
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
                tex, pic = self.wall_tex(name, top_ - hb, v, cadre)
                idx = emit_wall(P, Q, hb, top_, next_sector=-1, tex=tex, picnum=pic,
                                     light=light, invisible=False, centre=cen, mob=mob)
                self._note_switch(sg, leaf, idx, name, hb, top_, P, Q)
                self.stats["linteaux"] += 1

        if top > bot and side and side.middle not in ("-", ""):
            # TEXTURE DU MILIEU (grille, barreaux, grillage) : Doom la peint DANS l'ouverture,
            # sans la repeter verticalement -- DONTPEGBOTTOM ancre son bas au bas de l'ouverture,
            # sinon son haut au haut (r_segs.c). Elle n'etait pas emise du tout : `side.middle`
            # ne servait qu'aux murs a une face et aux ouvertures fermees, et les 46 lignes de
            # l'episode qui en portent une montraient un trou. Mur PLEIN (nextSector = -1) pour
            # ne pas doubler la traversee du portail deja emis juste au-dessous, place apres lui
            # par le tri « portails en tete », et qui ne BOUCHE que si Doom le dit (ML_BLOCKING).
            # La tuile garde ses texels a zero (doomtiles.coverage), et les cellules de mur sont
            # tirees sans SPD_DISABLE (SPR.C:287) : le moteur laisse donc voir a travers.
            hm = self.tex_h(side.middle)
            mtop = (bot + hm if pegbot else top) + yoff
            t_ = min(top, mtop)
            b_ = max(bot, mtop - hm)
            if t_ - b_ > 0:
                tex, pic = self.wall_tex(side.middle, t_ - b_, mtop - t_, cadre, masked=True)
                emit_wall(P, Q, b_, t_, next_sector=-1, tex=tex, picnum=pic, light=light,
                          invisible=False, centre=cen, mob=own(b_, t_),
                          force_blocked=bool(ld.flags & 0x0001))
                self.stats["grilles"] = self.stats.get("grilles", 0) + 1

        if top > bot and nbi >= 0:
            mob = own(bot, top)
            if lift_self and nfh >= fh and not mn_lift:
                # voisin FIXE au niveau de la plate-forme (ou plus haut) : le bas de l'ouverture est
                # son sol, fixe ; la paroi de cage ci-dessus bouche ce qui passe dessous. Un voisin
                # qui bouge avec nous (meme sol) : le bas suit notre sol.
                mob = [m for m in mob if m[0] is not ms]
            if mn_door and top == nch:
                mob.append((mn, "top"))
            if mn_lift and bot == nfh and nfh > fh:
                mob.append((mn, "bottom"))
            idx = emit_wall(P, Q, bot, top, next_sector=nbi, tex=None, picnum=0,
                                 light=light, invisible=True, centre=cen, mob=mob)
            bloc = ((WALLFLAG_SHORTOPENING if ld.flags & ML_BLOCKING else 0) |
                    (WALLFLAG_WATERBNDRY if ld.flags & ML_BLOCKMONSTERS else 0))
            if bloc:                                   # voir les deux drapeaux en tete de fichier
                for wi in idx:
                    self.em.walls[wi]["flags"] |= bloc
                self.stats["lignes_bloquantes"] = self.stats.get("lignes_bloquantes", 0) + 1
            if ld.flags & 0x0020:                      # ML_SECRET : un monstre ne l'ouvre pas
                for wi in idx:
                    w = self.em.walls[wi]
                    if (w["flags"] & WALLFLAG_DOORWALL) and id(w) not in self._secret_ids:
                        self._secret_ids.add(id(w))
                        self.secret_walls.append(w)
            self.stats["portails_ligne"] += 1
        elif top <= bot and nbi >= 0 and (
                (ms is not None and ms.kind == "door" and top == ch and nfh < ms.upper)
                or (mn_door and top == nch and fh < mn.upper)):
            # OUVERTURE FERMEE PAR LE PLAFOND D'UNE PORTE, et non par un sol. Le voisin d'une porte
            # n'est pas toujours plus bas qu'elle : quand SON sol est au-dessus du plafond ferme de
            # la porte, les deux volumes ne se touchent pas et il n'y avait ici ni portail (mur
            # plein) ni portail mobile (fente posee au sol du voisin, qui ne suit que LUI). La porte
            # s'ouvrait donc d'un seul cote : on l'entend, on voit l'ouverture depuis la salle, et
            # de l'autre on fait face a un mur -- E1M3 secteur 134 (la porte secrete en haut de
            # l'escalier, voisin a 176 contre un plafond ferme a 121), E1M5 secteurs 50, 52, 64, 82
            # et la porte 122 (captures console 2026-09-24).
            # La fente va donc AU PLAFOND DE LA PORTE (top - 1 .. top), pas au sol du voisin, et son
            # haut entre dans le push block de cette porte : il monte avec elle, et l'ouverture est
            # exactement celle de Doom -- min(plafond ouvert, plafond voisin). _tag_walls en fait un
            # DOORWALL, donc ce cote se presse aussi. Quand le sol du voisin bouge lui aussi (E1M3 :
            # l'ascenseur 133), c'est la PORTE qui gagne : le bas cale sur le sol de la porte donne
            # les quatre etats justes (ferme/ouvert x haut/bas), le bas cale sur l'ascenseur en
            # donnait deux faux.
            # le garde `sol du voisin < plafond OUVERT` compte : sans lui, une porte dont le
            # voisin reste au-dessus d'elle meme ouverte gagnerait un passage que Doom n'a pas
            # (aucun cas dans l'episode 1 -- mesure du 24-09 -- mais un autre WAD en aura).
            mob = ([(ms, "top")] if (ms is not None and ms.kind == "door" and top == ch) else [])                 + ([(mn, "top")] if (mn_door and top == nch) else [])
            emit_wall(P, Q, top - DOOR_SLIT, top, next_sector=nbi, tex=None, picnum=0,
                           light=light, invisible=True, centre=cen, mob=mob)
            self.stats["portails_fente_porte"] = self.stats.get("portails_fente_porte", 0) + 1
        elif top <= bot and nbi >= 0 and (
                (ms is not None and ms.kind != "door" and bot == fh)
                or (mn_lift and bot == nfh)):
            # ouverture fermee par le SOL de l'ascenseur (plafond du voisin bas = sol de la
            # plate-forme, ex. E1M1 58|70) : portail-fente 1 u sous le sol, dont le bas descend
            # avec l'ascenseur. Le linteau au-dessus est deja emis et reste fixe.
            mob = ([(ms, "bottom")] if (ms is not None and ms.kind != "door" and bot == fh)
                   else []) + ([(mn, "bottom")] if (mn_lift and bot == nfh) else [])
            emit_wall(P, Q, bot - DOOR_SLIT, bot, next_sector=nbi, tex=None, picnum=0,
                           light=light, invisible=True, centre=cen, mob=mob)
            self.stats["portails_fente"] += 1
        elif top <= bot:
            # ouverture fermee (porte baissee, mur plein a deux faces) : bouchon plein
            name = side.middle if side and side.middle != "-" else (
                side.lower if side and side.lower != "-" else "BROWN1")
            h = self.tex_h(name)
            v = ((h - (ch - fh)) % h if pegbot else 0) + yoff
            tex, pic = self.wall_tex(name, ch - fh, v, cadre)
            idx = emit_wall(P, Q, fh, ch, next_sector=-1, tex=tex, picnum=pic,
                                 light=light, invisible=False, centre=cen, mob=own(fh, ch))
            self._note_switch(sg, leaf, idx, name, fh, ch, P, Q)
            self.stats["ouvertures_fermees"] += 1

    def post_flags(self):
        W, S, V = self.em.walls, self.em.sectors, self.em.vertices
        # drapeaux de l'etat CHARGE : les sommets d'un sol qui monte sont emis `throw` plus haut
        # (raise_floors) ; doom_pbBlockBits (DOOM_GAME.C) les recalcule ensuite a chaque pas
        charge = {}
        for t in self.mobile.values():
            if t.monte:
                for vi in t.verts:
                    charge[vi] = -t.throw
        for s in S:
            for wi in range(s["firstWall"], s["lastWall"] + 1):
                w = W[wi]
                if w["nextSector"] == -1 or w["normal"][1] != 0:
                    continue
                ys = [V[i]["y"] + charge.get(i, 0) for i in w["v"]]
                h = max(ys) - min(ys)
                if 1.0 < h < PLAYER_FIT_HEIGHT:
                    w["flags"] |= 0x1000                # SHORTOPENING
                    self.stats["shortopening"] += 1
                if min(ys) - s["floorLevel"] > PLAYER_STEP_HEIGHT:
                    w["flags"] |= 0x1000                # marche > 24 : voir PLAYER_STEP_HEIGHT
                    self.stats["marche_haute"] += 1
                if s["floorLevel"] - S[w["nextSector"]]["floorLevel"] > PLAYER_STEP_HEIGHT:
                    w["flags"] |= 0x800                 # CLIFFBNDRY : chute > 24 (monstres)
                    self.stats["cliffbndry"] += 1
        # Un sol mobile FERME au chargement (sol = plafond dans le WAD : le mur que la mort des
        # Barons abaisse sur E1M8, tag 666) ne s'ouvre qu'une fente de 1 u, par ou le moteur
        # traversait tout ce qui est derriere (WALLS.C:2138). MESURE 18-09 sur E1M8 : l'ordre du
        # peintre y voyait 118 secteurs de plus -- 81 % de positions fautives contre 0 %, et la
        # conversion manquait de memoire. BLOCKSSIGHT (0x08, le drapeau des murs explosables de
        # PowerSlave, AICOMMON.C:486) arrete le rendu, l'ordre (tools/ordre.py) et la ligne de vue
        # (HITSCAN.C:289) ; DOOM_GAME.C l'enleve des portails du push block quand le sol part.
        Ms = self.M["sectors"]
        for t in self.mobile.values():
            if t.kind.startswith("floor_") and Ms[t.sector].floorh >= Ms[t.sector].ceilh:
                for w in t.walls:
                    if w["nextSector"] >= 0 and w["normal"][1] == 0:
                        w["flags"] |= 0x08
                        self.stats["portails_fermes_a_la_vue"] += 1

    @staticmethod
    def cle_sans_fenetre(k):
        """La cle qu'aurait la MEME cellule sans fenetre horizontale. Le decoupage est celui de
        doomtiles.tile_of_key : 5 elements de sous-tuile, (cv, voff), un marqueur 'mask'
        facultatif, puis la fenetre (largeur de cellule, colonne de depart)."""
        n = 7 + (1 if "mask" in k else 0)
        return tuple(k[:n])

    def compter_fenetres(self):
        """Ce que chaque face de linedef candidate COUTE et ce qu'elle LIBERE, en cles de tuile.

        Appele a la fin du 1er passage, ou toutes les fenetres sont posees : la cle sans fenetre
        d'une cellule fenetree est celle qu'elle aurait sans, donc `uwin_base` compte exactement
        les clients de chaque tuile dans le monde SANS fenetre."""
        em = self.em

        def cellules(w):
            if w["flags"] & 0x01:
                return [em.texture[w["textures"] + 2 * c + 1]
                        for c in range(w["tileLength"] * w["tileHeight"])]
            if w["firstFace"] >= 0:
                return [em.faces[i]["tile"] for i in range(w["firstFace"], w["lastFace"] + 1)]
            return []

        for w in em.walls:
            ident = self.uwin_wall.get(id(w))
            rec = self.uwin_use[ident] if ident is not None else None
            for t in cellules(w):
                k = tuple(em.tiles[t])
                base = self.cle_sans_fenetre(k)
                if base != k and rec is None:
                    continue                       # fenetre OBLIGATOIRE (texture etroite, wall_tex) :
                                                   # jamais cliente de la tuile d'origine
                self.uwin_base[base] += 1
                if rec is not None and base != k:
                    rec[1].add(k)                  # la tuile que la fenetre demande
                    rec[3][base] += 1              # la cellule qu'elle retire a la tuile d'origine

    def choisir_fenetres(self, budget):
        """Les faces de linedef a fenetrer, par gain (aire x ecart d'echelle) par tuile NETTE,
        tant que la depense nette tient dans `budget`.

        NETTE : une fenetre demande une tuile, mais elle en RETIRE une quand elle etait la seule
        cliente de la tuile d'origine -- les 4 grandes faces des caissons a planete d'E1M1
        (PLANET1 256 colonnes ecrasees dans 192 u) sont dans ce cas, et l'ancien compte, qui ne
        regardait que les tuiles demandees, les classait derriere tout le monde alors qu'elles ne
        coutent RIEN. Le cout net ne peut que baisser quand d'autres faces sont prises (elles
        vident les memes tuiles d'origine), donc tout ce qui est gratuit se prend d'abord, jusqu'au
        point fixe, et le budget n'arbitre que le reste.
        Le budget reste necessaire : tout fenetrer MESURE 161 -> 528 tuiles sur E1M1, pour un
        plafond de 159 (l'index d'une tuile de geometrie est un octet, + tileBase <= 255) -- et
        autant de plus dans le cache VDP1 de la vue.
        Les FACES DE PORTE passent d'abord, hors budget : Doom les dessine au texel pres pour leur
        cadre, et c'est la que l'ecrasement se voit (le testeur : la porte de sortie d'E1M1,
        EXITDOOR 128 sur 64 u) -- alors que leur petite aire les classe loin derriere de grands
        murs a peine ecrases (x 1,33)."""
        pris, tuiles, vides, depense = set(), set(), Counter(), 0

        def cout_net(faces):
            """Tuiles que ces faces AJOUTENT moins celles qu'elles VIDENT, vu l'etat courant."""
            neuf, ajout = set(), Counter()
            for k in faces:
                rec = self.uwin_use[k]
                neuf |= rec[1]
                for base, n in rec[3].items():
                    ajout[base] += n
            rendu = sum(1 for base, n in ajout.items()
                        if vides[base] < self.uwin_base[base] <= vides[base] + n)
            return len(neuf - tuiles) - rendu

        def prendre(faces):
            nonlocal depense
            depense += cout_net(faces)
            for k in faces:
                rec = self.uwin_use[k]
                pris.add(k)
                tuiles.update(rec[1])
                for base, n in rec[3].items():
                    vides[base] += n

        # UNITE DE DECISION : une face seule, ou l'ensemble des faces qui se partagent une meme
        # tuile d'origine -- c'est seulement ENSEMBLE qu'elles la vident, donc seulement ensemble
        # qu'elles peuvent etre gratuites (les 4 grandes faces des caissons a planete d'E1M1).
        groupes = defaultdict(set)
        for k, rec in self.uwin_use.items():
            for base in rec[3]:
                groupes[base].add(k)
        unites = [frozenset((k,)) for k in sorted(self.uwin_use)]
        unites += [frozenset(g) for base, g in sorted(groupes.items(), key=lambda kv: str(kv[0]))
                   if len(g) > 1]

        for k in sorted(self.uwin_use):                # les faces de porte, hors budget
            if k in self.door_faces:
                prendre((k,))
        while True:
            change = True
            while change:                              # tout ce qui est gratuit, au point fixe
                change = False
                for u in unites:
                    r = u - pris
                    if r and cout_net(r) <= 0:
                        prendre(r)
                        change = True
            meilleure, mieux = None, 0.0               # puis UNE unite payante, la plus rentable
            for u in unites:
                r = u - pris
                if not r:
                    continue
                c = cout_net(r)
                if c <= 0 or depense + c > budget:
                    continue
                gain = sum(self.uwin_use[k][0] for k in r) / c
                if gain > mieux:
                    meilleure, mieux = r, gain
            if meilleure is None:
                return pris
            prendre(meilleure)

    def anim_flat_keys(self):
        """Flats ANIMES de Doom (p_spec.c animdefs, 8 tics par image) : des qu'une face porte une
        image d'une famille, on fabrique les tuiles de TOUTES ses images. Le moteur les fait
        tourner via markAnimTiles (PIC.C:129) : une sequence OT_ANM* dont les chunks sont ces
        tuiles, mapPic redirige chacune vers l'image courante (make_e1m1.anim_sequences)."""
        used = {self.picnames[k[0]][1] for k in self.em.tiles
                if self.picnames[k[0]][0] == "flat"}
        for fam in ANIM_FLATS:
            if used & set(fam):
                keys = [(self.picnum("flat", nm), 0, 0, 1, 1) for nm in fam]
                for k in keys:
                    self.em.tile(k)
                self.anim_keys.append(keys)
                self.stats["familles_animees"] += 1

    def build(self):
        for li in self.keep:
            self.emit_leaf(li)
        self.post_flags()
        self.compter_fenetres()               # AVANT les tuiles qui ne sont pas des cellules
        self.anim_flat_keys()
        if self.mobile or self.switch_lines:
            self.finalize_mobile()
        self.anims = [[self.em._tile_index[k] for k in fam] for fam in self.anim_keys]
        marquer_nukage(self.em, self.M, self, self.stats)
        cuire_lumieres(self.em, self.M, self, self.stats)
        if "sommets" in OPTIM_ACTIFS:
            # coins partages (Emitter.partager_coins) ; les sommets des push blocks restent a eux
            mobiles = set()
            for t in self.mobile.values():
                mobiles |= t.verts
            remap, n = self.em.partager_coins(mobiles)
            for t in self.mobile.values():
                t.verts = {remap[i] for i in t.verts}
            self.stats["sommets_partages"] += n
        cuire_vert_nukage(self.em, self.M, self, self.stats)
        return self.em

    # -- mobile : interrupteurs et compaction des tuiles -----------------------------------
    def finalize_mobile(self):
        """Interrupteurs S : la tuile OFF doit etre UNIQUE dans la feuille (`constructSwitch`
        cherche `level_texture[t] == ourTile` dans les murs de `sectorNm`, AI2.C:606-632) et la
        tuile ON existe pour l'animation. On donne au mur de l'interrupteur une cle de tuile
        DEDIEE (cle E4.1c + marqueur 'sw') et on fabrique la cle ON (SW2xxx) de la meme cellule ;
        doomtiles.py lit `key[0], key[5], key[6]` et ignore le reste. Un type par paire (OFF, ON) :
        `level_sequenceMap[type]` est la seule sequence d'un type (AI2.C:598)."""
        em = self.em
        pos = {id(w): i for i, w in enumerate(em.walls)}
        # (tuile de la cellule, texture ON) -> type. C'est ce qui distingue une paire, et on le
        # connait AVANT d'allouer quoi que ce soit : une paire refusee ne laisse ni tuile ni picnum.
        types = {}
        # Chaque type est UNE apparence -- l'objet cherche sa tuile OFF sur les murs de son secteur
        # et s'y accroche (AI2.C:606-634, `assert(this->tilePos)`), donc meme un interrupteur qui ne
        # change pas de texture consomme un type. Le moteur en a quatre, le jeu Doom 23 de plus
        # (doom_specials.OT_SWITCH_TYPES). Au-dela on REFUSE l'interrupteur au lieu d'arreter la
        # conversion : le critere `interrupteurs_types_disponibles` echoue alors, le .LEV n'est pas
        # ecrit, mais tous les autres criteres ont parle.
        self.interrupteurs_refuses = []
        V = em.vertices
        pris = set()                          # (feuille, apparence, k) deja accroches
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
                # Deux interrupteurs de MEME apparence dans la meme feuille trouveraient la meme
                # cellule (constructSwitch prend la premiere) : le second recoit une tuile, donc un
                # type, a lui. MESURE 2026-09-18 : E1M3 en a trois dans une feuille.
                k = 0
                while (h["leaf"], key, on_name, k) in pris:
                    k += 1
                cle = (key, on_name, k)
                if cle not in types and len(types) >= len(sp.OT_SWITCH_TYPES):
                    self.stats["interrupteurs_sans_type"] += 1
                    self.interrupteurs_refuses.append("ligne %d %s %s" % (line, off_name, key[1:]))
                    continue
                pris.add((h["leaf"], key, on_name, k))
                marque = ("sw",) if k == 0 else ("sw", k)
                p_on = self.picnum("tex", on_name)
                off_key = key + marque
                on_key = (p_on,) + key[1:] + marque
                t_off = em.tile(off_key)
                t_on = em.tile(on_key)
                # orifice : milieu du mur, a hauteur d'oeil -- la source du son ; le press vaut
                # sur tout le mur (CFG_SWITCH_AIM 0, SPRITE.H), pas a < 40 u de ce point
                P, Q = h["P"], h["Q"]
                oy = h["bot"] + sp.PLAYER_EYE
                oy = max(h["bot"], min(h["top"], oy))
                orifice = [int(round((P[0] + Q[0]) / 2.0)), int(oy), int(round((P[1] + Q[1]) / 2.0))]
                # UNE cellule porte la tuile OFF : constructSwitch s'accroche a la premiere qu'il
                # trouve et n'anime qu'elle (tilePos, AI2.C:613-627). La poser sur toutes les
                # cellules d'un mur long la rendait non unique dans la feuille (critere 16 de
                # verif_doom : 2 et 3 occurrences sur E1M2 et E1M3). On la pose sur la cellule la
                # plus proche de l'orifice ; les autres gardent leur tuile, la meme image, qui ne
                # bascule pas.
                tl, th = w["tileLength"], w["tileHeight"]
                quad = [(V[i]["x"], V[i]["y"], V[i]["z"]) for i in w["v"]]
                c = min(range(n), key=lambda c_: math.dist(
                    quad_point(quad, (c_ % tl + 0.5) / tl, (c_ // tl + 0.5) / th), orifice))
                em.texture[w["textures"] + 2 * c + 1] = t_off
                if cle not in types:
                    types[cle] = sp.OT_SWITCH_TYPES[len(types)]
                self.switches.append(dict(
                    line=line, channel=info["channel"], special=info["special"],
                    leaf=h["leaf"], leaf_sector=self.remap[h["leaf"]], wall=wi,
                    tile_off=t_off, tile_on=t_on, type=types[cle],
                    texture_off=off_name, texture_on=on_name, orifice=orifice))
                self.stats["interrupteurs"] += 1
        for line in self.switch_lines:
            if line not in self.switch_hits:
                self.stats["interrupteur_sans_mur"] += 1
        # la tuile ON n'est referencee que par les chunks de la sequence d'interrupteur : a garder
        remap = self.compact_tiles(extra={s["tile_on"] for s in self.switches}
                                   | {s["tile_off"] for s in self.switches}
                                   | {self.em._tile_index[k] for fam in self.anim_keys for k in fam})
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

# ---------------------------------------------------------------------------------------------
# LUMIERES CUITES (optim `lumieres`). Doom n'a aucune lumiere dynamique et le moteur n'en a que 15
# slots, qui coutent cher : une lampe met sur le chemin par sommet TOUT mur dont le plan passe dans
# son rayon. Ce qui ne bouge pas est donc cuit ici, dans la lumiere des sommets -- cout nul a
# l'execution, et rien a tenir en RAM.
#   halos       les dalles de plafond lumineuses (TLITE6_*) : `dissoudre_penombres` fond leur
#               anneau de penombre dans la piece (choix assume, voir plus haut), donc le halo
#               clair autour d'elles avait disparu. Il est repeint ici, en rond, depuis la dalle.
#   decor       les objets qui portent une flamme (colonne lumineuse, torches, chandelier, bougie,
#               bidon) n'eclairaient rien : 51 colonnes rien que dans l'episode 1.
#   debordement un passage entre un secteur clair et un secteur sombre coupe net (Doom le coupe
#               aussi), mais le sol du cote sombre recoit un peu de la lumiere du seuil.
# Le modele est le meme pour les trois : une source a une CIBLE (le niveau qu'elle voudrait
# donner) et un rayon ; a la distance d elle propose `l + (cible - l) * (1 - d/rayon)`, et un
# sommet prend le meilleur de ce qui le vise -- jamais moins que ce qu'il avait. Une source
# n'eclaircit donc jamais au-dela de sa cible, et deux sources ne s'additionnent pas.
# Les dalles de plafond qui ECLAIRENT. Doom et Doom 2 partagent les quatre TLITE6_ ; c'est ici
# qu'on ajoute ce qu'apportent TNT et Plutonia. E1 : 97 dalles.
LIGHT_FLATS = ("TLITE6_1", "TLITE6_4", "TLITE6_5", "TLITE6_6")
HALO_MARGE = 128            # rayon du halo = rayon de la dalle + ca
HALO_CIBLE = 16
# type Doom -> (hauteur de la flamme au-dessus du sol, rayon, cible)
DECOR_LAMPES = {34: (14, 96, 14),      # CAND  bougie
                35: (42, 128, 15),     # CBRA  chandelier
                44: (68, 160, 16),     # TBLU  torche bleue haute
                45: (68, 160, 16),     # TGRN  torche verte haute
                46: (68, 160, 16),     # TRED  torche rouge haute
                55: (40, 128, 15),     # SMBT  torche bleue courte
                56: (40, 128, 15),     # SMGT  torche verte courte
                57: (40, 128, 15),     # SMRT  torche rouge courte
                70: (40, 160, 16),     # FCAN  bidon en feu
                2028: (48, 176, 16)}   # COLU  colonne lumineuse
DEBORD_MIN = 4              # ecart de lumiere (0..16) a partir duquel un seuil deborde
DEBORD_RAYON = 80


def _centre_rayon(M, si):
    """(cx, cy, rayon) du secteur Doom si : le centre de ses sommets, et le rayon du disque de
    meme aire -- de quoi poser une source ronde sur une dalle qui ne l'est pas."""
    V, L, SD = M["vertices"], M["linedefs"], M["sidedefs"]
    pts = []
    for ld in L:
        for sd in (ld.right, ld.left):
            if sd >= 0 and SD[sd].sector == si:
                pts.append(V[ld.v1])
                pts.append(V[ld.v2])
                break
    if not pts:
        return None
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    _per, aire = _perimetre_aire(M, si)
    return cx, cy, math.sqrt(max(1.0, abs(aire)) / math.pi)


class _Sources:
    """Les sources, rangees dans une grille de 256 u en (X, Z) : un sommet ne teste que les
    sources de sa case et des huit voisines."""
    PAS = 256

    def __init__(self):
        self.cases = defaultdict(list)
        self.n = 0

    def point(self, x, y, z, rayon, cible):
        self._poser(("p", x, y, z, rayon, cible), x, z, rayon)

    def segment(self, p, q, rayon, cible):
        self._poser(("s", p, q, rayon, cible), (p[0] + q[0]) / 2.0, (p[2] + q[2]) / 2.0,
                    rayon + math.dist((p[0], p[2]), (q[0], q[2])) / 2.0)

    def _poser(self, src, cx, cz, port):
        self.n += 1
        for gx in range(int((cx - port) // self.PAS), int((cx + port) // self.PAS) + 1):
            for gz in range(int((cz - port) // self.PAS), int((cz + port) // self.PAS) + 1):
                self.cases[(gx, gz)].append(src)

    def niveau(self, l, x, y, z):
        """le meilleur niveau que les sources proposent au point (x, y, z), jamais sous `l`"""
        best = l
        for src in self.cases.get((int(x // self.PAS), int(z // self.PAS)), ()):
            if src[0] == "p":
                _k, sx, sy, sz, rayon, cible = src
                d2 = (x - sx) ** 2 + (y - sy) ** 2 + (z - sz) ** 2
            else:
                _k, p, q, rayon, cible = src
                d2 = _dist2_segment(x, y, z, p, q)
            if d2 >= rayon * rayon or cible <= l:
                continue
            d = math.sqrt(d2)
            v = l + (cible - l) * (1.0 - d / rayon)
            if v > best:
                best = v
        return best


def _dist2_segment(x, y, z, p, q):
    dx, dy, dz = q[0] - p[0], q[1] - p[1], q[2] - p[2]
    n = dx * dx + dy * dy + dz * dz
    t = 0.0 if n <= 0 else ((x - p[0]) * dx + (y - p[1]) * dy + (z - p[2]) * dz) / n
    t = max(0.0, min(1.0, t))
    ax, ay, az = p[0] + t * dx, p[1] + t * dy, p[2] + t * dz
    return (x - ax) ** 2 + (y - ay) ** 2 + (z - az) ** 2


SECFLAG_NUKAGE = 0x40       # SLEVEL.H : la feuille appartient a une salle a nukage
VERT_NM = 4                 # UTIL.H WORLDGREEN_NM : bandes de vert de la rampe du monde
VERT_SH = 5                 # UTIL.H WORLDGREEN_SH : leur place dans l'octet de lumiere
# UN CRAN DE VERT PAR TUILE. La rampe etait une fonction continue de la distance, arrondie sur
# quatre bandes : ses paliers tombaient ou ils voulaient, et deux sommets d'une meme tuile avaient
# souvent la meme valeur -- donc la tuile etait d'un vert plat et la marche se voyait sur son
# arete. Un pas d'exactement une tuile met une bande de chaque cote de chaque tuile : le gouraud
# fait alors le degrade A L'INTERIEUR de chaque tuile, de la flaque jusqu'au zero, et les points
# proches d'une tuile valent les points lointains de la precedente (demande a l'ecran, 2026-09-23).
# Une tuile de Doom fait 64 u -- un texel une unite -- et il n'y a que VERT_NM-1 bandes non nulles,
# donc le degrade s'etend sur trois tuiles et pas plus.
VERT_TUILE = 64             # u : le pas, une tuile de Doom
VERT_COEUR = 64             # u depuis le BORD du nukage : plein vert dans la premiere tuile
VERT_PORTEE = VERT_COEUR + (VERT_NM - 2) * VERT_TUILE   # 192 : au-dela, plus de vert du tout
SECFLAG_NUKAGE_CORE = 0x80  # SLEVEL.H : la feuille est dans le coeur vert (things teintes)


def marquer_nukage(em, M, conv, stats):
    """Pose SECFLAG_NUKAGE sur les feuilles des SALLES a nukage.

    C'est tout ce que le vert coute : le moteur fait lire a leurs murs et a leurs plafonds -- pas
    a leurs sols -- la rampe VERTE du monde au lieu de la neutre (WALLS.C getLight, UTIL.C
    worldGreen). Rien n'est calcule a l'execution, et la lumiere des sommets ne change pas : c'est
    la rampe qu'elle traverse qui change. Hors du drapeau d'optimisation `lumieres` : la couleur
    d'une salle n'est pas une lumiere cuite, c'est une propriete du niveau."""
    import doom_specials                           # tardif : doom_specials importe ce module
    salles = doom_specials.salles_nukage(M, conv)
    if not salles:
        return
    for i, li in enumerate(conv.keep):
        if conv.leaf_sector[li] in salles:
            em.sectors[i]["flags"] |= SECFLAG_NUKAGE
            stats["nukage_feuilles"] += 1
    stats["nukage_salles"] = len(salles)


def _aretes_nukage(M, flaques):
    """Les segments du BORD du nukage : toute arete d'un secteur de liquide, en (x, hauteur, z).

    C'est la bonne reference et pas le centre : une flaque n'est pas un disque, et deux murs a la
    meme distance du centre n'etaient pas au meme niveau de vert si elle s'etirait vers l'un
    d'eux (vu a l'ecran, 2026-09-23). La hauteur est celle du liquide, pas celle du sommet :
    un plafond haut prend moins de vert qu'un mur bas, ce qui est le bon sens."""
    V, L, SD, S = M["vertices"], M["linedefs"], M["sidedefs"], M["sectors"]
    liquide = set()
    for fl in flaques:
        liquide |= fl["liquide"]
    out = []
    for ld in L:
        secs = {SD[sd].sector for sd in (ld.right, ld.left) if sd >= 0}
        if not (secs & liquide):
            continue
        h = min(S[si].floorh for si in (secs & liquide))
        a, b = V[ld.v1], V[ld.v2]
        out.append(((a[0], h, a[1]), (b[0], h, b[1])))
    return out


def cuire_vert_nukage(em, M, conv, stats):
    """Ecrit le NIVEAU DE VERT de chaque sommet dans les bits 5-6 de son octet de lumiere.

    Un sommet d'un mur ou d'un PLAFOND d'une salle a nukage prend VERT_NM-1 jusqu'a VERT_COEUR
    unites du BORD du liquide, puis s'eteint a VERT_PORTEE ; les SOLS n'en prennent pas
    (normale +Y, geom3d.py) -- la passerelle qui traverse une salle verte reste grise, et c'est
    ce qui dit qu'elle est seche. Le moteur ne teste rien : sa rampe a une bande par niveau, et
    l'octet la designe tout seul.
    La feuille dont un sommet atteint le plein vert prend SECFLAG_NUKAGE_CORE : c'est la que le
    moteur teinte aussi les THINGS (WALLS.C drawSprites)."""
    import doom_specials                           # tardif : doom_specials importe ce module
    flaques = doom_specials.bassins_nukage(M, conv)
    if not flaques:
        return
    salles = doom_specials.salles_nukage(M, conv)
    segs = _aretes_nukage(M, flaques)
    if not segs:
        return
    V = em.vertices

    def niveau(x, y, z, nx=0.0, nz=0.0):
        """Le niveau de vert en (x, y, z). nx, nz : la normale XZ de la FACE, quand elle en a une.

        Seuls les bords du liquide places DEVANT elle comptent. Une face tourne le dos au
        liquide : ce qu'on en voit n'est pas eclaire par lui, et le mur du fond d'une salle a
        nukage ressortait vert vu de l'autre cote (vu a l'ecran, E1M1 dehors, 2026-09-23). La
        normale du moteur pointe VERS L'INTERIEUR de la feuille (SPRITE.C bumpWall : planeDist
        est positif pour un sprite dedans), donc "devant" est bien le cote qu'on regarde.
        Les PLAFONDS gardent leur vert : leur normale est (0,-1,0), il n'y a pas de cote."""
        best = None
        for p, q in segs:
            if nx or nz:
                if ((p[0] + q[0]) * 0.5 - x) * nx + ((p[2] + q[2]) * 0.5 - z) * nz <= 0.0:
                    continue
            d2 = _dist2_segment(x, y, z, p, q)
            if best is None or d2 < best:
                best = d2
        if best is None or best >= VERT_PORTEE * VERT_PORTEE:
            return 0
        d = math.sqrt(best)
        if d <= VERT_COEUR:
            return VERT_NM - 1
        k = (VERT_NM - 1) - int(math.ceil((d - VERT_COEUR) / float(VERT_TUILE)))
        return k if k > 0 else 0

    n = 0
    for i, sec in enumerate(em.sectors):
        if conv.leaf_sector[conv.keep[i]] not in salles:
            continue
        for wi in range(sec["firstWall"], sec["lastWall"] + 1):
            w = em.walls[wi]
            if w["normal"][1] > 0:                 # un sol : jamais de vert
                continue
            nx = nz = 0.0
            if w["normal"][1] == 0:                # un vrai mur : il a un devant et un derriere
                nx, nz = w["normal"][0] / 65536.0, w["normal"][2] / 65536.0
            fort = 0
            if w["flags"] & 0x01:                  # parallelogramme : sommets de GRILLE
                tl, th, base = w["tileLength"], w["tileHeight"], w["firstLight"]
                v0, v1, v2, v3 = (V[k] for k in w["v"])
                for rr in range(th + 1):
                    fr = rr / th
                    for cc in range(tl + 1):
                        fc = cc / tl
                        x = (v0["x"] + (v1["x"] - v0["x"]) * fc) * (1 - fr) + \
                            (v3["x"] + (v2["x"] - v3["x"]) * fc) * fr
                        y = (v0["y"] + (v1["y"] - v0["y"]) * fc) * (1 - fr) + \
                            (v3["y"] + (v2["y"] - v3["y"]) * fc) * fr
                        z = (v0["z"] + (v1["z"] - v0["z"]) * fc) * (1 - fr) + \
                            (v3["z"] + (v2["z"] - v3["z"]) * fc) * fr
                        k = niveau(x, y, z, nx, nz)
                        if k:
                            j = base + rr * (tl + 1) + cc
                            em.vertexLight[j] |= k << VERT_SH
                            fort = max(fort, k)
                            n += 1
            elif w["firstVertex"] != 65535:        # mur a faces : ses sommets propres
                for vi in range(w["firstVertex"], w["lastVertex"] + 1):
                    v = V[vi]
                    k = niveau(v["x"], v["y"], v["z"], nx, nz)
                    if k:
                        v["light"] |= k << VERT_SH
                        fort = max(fort, k)
                        n += 1
            else:
                # Un PORTAIL INVISIBLE (ni parallelogramme, ni faces : firstVertex 0xffff). Il ne
                # montre rien, et ses quatre coins sont des sommets PARTAGES du niveau, que les
                # murs a faces d'a cote lisent : le teinter ne peignait que les autres, et c'est
                # comme ca que le vert passait de l'autre cote d'une salle a nukage (vu a
                # l'ecran, E1M1 dehors, 2026-09-23). Les deux branches au-dessus ecrivent chacune
                # dans SA propre lumiere -- grille du mur, ou plage de sommets de la face.
                continue
            if fort >= VERT_NM - 1:
                sec["flags"] |= SECFLAG_NUKAGE_CORE
    stats["nukage_sommets_verts"] = n
    stats["nukage_feuilles_coeur"] = sum(1 for x in em.sectors if x["flags"] & SECFLAG_NUKAGE_CORE)


def _sources_nukage(src, M, conv, stats):
    """Une source au CENTRE de chaque flaque : le relief du vert, cuit dans la lumiere des sommets.
    Le vert est plat sans elle -- la rampe teinte ce que la lumiere du sommet dit, et le WAD dit la
    meme chose sur tout un secteur."""
    import doom_specials
    for fl in doom_specials.bassins_nukage(M, conv):
        sec = M["sectors"][fl["si"]]
        cible = min(16, light_of(sec.light) + doom_specials.NUKAGE_CIBLE)
        src.point(fl["x"], sec.floorh + 16, fl["y"], fl["rayon"] + 64, cible)
        stats["lumiere_flaque"] += 1


def cuire_lumieres(em, M, conv, stats):
    """Pose les sources, puis reecrit la lumiere de chaque sommet et de chaque sommet de grille.

    Les sommets de GRILLE (murs parallelogrammes) ne portent pas leur position : elle se refait
    par interpolation bilineaire sur le quad, exactement comme drawRectWall construit la sienne
    (vWidth = (v1-v0)/tileLength, vHeight = (v2-v1)/tileHeight, WALLS.C:1036-1049), donc l'indice
    de (rr, cc) est firstLight + rr*(tileLength+1) + cc."""
    if "lumieres" not in OPTIM_ACTIFS:
        return
    S, V = M["sectors"], em.vertices
    src = _Sources()
    # halos : une source sous chaque dalle de plafond lumineuse
    for si, sec in enumerate(S):
        if sec.ceilpic not in LIGHT_FLATS:
            continue
        cr = _centre_rayon(M, si)
        if cr is None:
            continue
        cx, cy, r = cr
        src.point(cx, sec.ceilh - 16, cy, r + HALO_MARGE, HALO_CIBLE)
        stats["lumiere_halo"] += 1
    # decor : les objets qui portent une flamme
    for t in M["things"]:
        d = DECOR_LAMPES.get(t.type)
        if d is None:
            continue
        lf = conv.leaf_at(t.x, t.y)
        si = conv.leaf_sector.get(lf) if hasattr(conv.leaf_sector, "get") else (
            conv.leaf_sector[lf] if lf in conv.remap else None)
        if si is None:
            continue
        h, rayon, cible = d
        src.point(t.x, S[si].floorh + h, t.y, rayon, cible)
        stats["lumiere_decor"] += 1
    # debordements : le seuil d'un secteur clair deborde sur le sol du voisin sombre
    niveaux = [light_of(S[conv.leaf_sector[li]].light) for li in conv.keep]
    for i, s in enumerate(em.sectors):
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = em.walls[wi]
            t = w["nextSector"]
            if t < 0 or t >= len(niveaux) or niveaux[i] - niveaux[t] < DEBORD_MIN:
                continue
            a, b = V[w["v"][3]], V[w["v"][2]]      # l'arete BASSE du portail : le seuil
            src.segment((a["x"], a["y"], a["z"]), (b["x"], b["y"], b["z"]),
                        DEBORD_RAYON, niveaux[i])
            stats["lumiere_debord"] += 1
    _sources_nukage(src, M, conv, stats)
    if not src.n:
        return
    # les sommets propres (murs a faces, sols et plafonds : ils portent leur position)
    touches = 0
    for v in V:
        l = src.niveau(v["light"], v["x"], v["y"], v["z"])
        n = int(round(l))
        if n != v["light"]:
            v["light"] = min(16, n)
            touches += 1
    # les sommets de grille des murs parallelogrammes
    for w in em.walls:
        if not (w["flags"] & 0x01):
            continue
        tl, th, base = w["tileLength"], w["tileHeight"], w["firstLight"]
        v0, v1, v2, v3 = (V[i] for i in w["v"])
        for rr in range(th + 1):
            fr = rr / th
            for cc in range(tl + 1):
                fc = cc / tl
                x = (v0["x"] + (v1["x"] - v0["x"]) * fc) * (1 - fr) + \
                    (v3["x"] + (v2["x"] - v3["x"]) * fc) * fr
                y = (v0["y"] + (v1["y"] - v0["y"]) * fc) * (1 - fr) + \
                    (v3["y"] + (v2["y"] - v3["y"]) * fc) * fr
                z = (v0["z"] + (v1["z"] - v0["z"]) * fc) * (1 - fr) + \
                    (v3["z"] + (v2["z"] - v3["z"]) * fc) * fr
                k = base + rr * (tl + 1) + cc
                l = src.niveau(em.vertexLight[k], x, y, z)
                n = min(16, int(round(l)))
                if n != em.vertexLight[k]:
                    em.vertexLight[k] = n
                    touches += 1
    stats["lumiere_sommets_eclaircis"] = touches

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

    # Une apparence d'interrupteur par type, et doom_specials.OT_SWITCH_TYPES en offre 27 (voir
    # `finalize_mobile`). Un interrupteur refuse est un mur qu'on ne peut plus presser.
    refus = getattr(conv, "interrupteurs_refuses", [])
    put("interrupteurs_types_disponibles", not refus, n=len(refus), types=len(sp.OT_SWITCH_TYPES),
        exemples=refus[:6])
    return crit


# Specials de Doom qui font MONTER une porte. La pre-passe ci-dessous ne s'applique qu'aux
# secteurs deja fermes (plafond == sol), ou ces specials ne peuvent designer qu'une porte.
DOOR_SPECIALS = {1, 2, 3, 4, 16, 26, 27, 28, 29, 31, 32, 33, 34, 46, 61, 63, 75, 76, 86, 90,
                 99, 103, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118,
                 133, 135, 137}


EPS_PENOMBRE = 8.0            # u : au-dela, une bande n'est plus une penombre mais une piece


def _perimetre_aire(M, si):
    """(perimetre, aire) du secteur si, reconstitues depuis ses linedefs. Un ANNEAU (contour
    exterieur et contour interieur, d'orientations opposees) rend bien l'aire de la seule bande."""
    V, L, SD = M["vertices"], M["linedefs"], M["sidedefs"]
    per = deux_a = 0.0
    for ld in L:
        a = ld.right >= 0 and SD[ld.right].sector == si
        b = ld.left >= 0 and SD[ld.left].sector == si
        if not (a or b):
            continue
        (x1, y1), (x2, y2) = V[ld.v1], V[ld.v2]
        per += math.hypot(x2 - x1, y2 - y1)
        deux_a += ((1 if a else 0) - (1 if b else 0)) * (x1 * y2 - x2 * y1)
    return per, abs(deux_a) / 2.0


def classes_de_rejet(W, mapname, n_doom, doom_sector, fondus=()):
    """La table de rejet du .LEV (SLEVEL.H) : le REJECT de la carte, que P_CheckSight lit AVANT de
    tracer une ligne -- et le moteur aussi (HITSCAN.C canSee).

    Un secteur .LEV prend le secteur Doom de sa feuille : la fusion ne recolle que des feuilles d'un
    meme secteur Doom, et reattribuer_debords rend a son vrai secteur ce qu'une feuille deborde.
    Seule exception, les penombres fondues dans leur piece (dissoudre_penombres) : un secteur de la
    piece peut etre fait de la bande, donc la piece ne rejette que ce que la piece ET chacune de ses
    bandes rejettent -- jamais aveugle la ou Doom verrait. Une paire n'est gardee que si elle est
    rejetee DANS LES DEUX SENS : la table est symetrique (celles d'E1 le sont deja toutes), le
    moteur n'en stocke que le triangle. Les secteurs de meme ligne partagent une classe : MESURE
    E1, 74-250 secteurs Doom -> 19-119 classes, 24-893 octets au lieu des 685-7 813 du lump.
    -> (classe par secteur .LEV, dict(classes=n, table=[octets], triangle a <= b, bit
    a*n-a*(a-1)/2+b-a poids faible d'abord comme le REJECT)), ou (None, None) : pas de lump, ou
    rien a rejeter."""
    rej = bytes(W.map_lumps(mapname).get("REJECT") or b"")
    rej += bytes(max(0, (n_doom * n_doom + 7) // 8 - len(rej)))   # court : des zeros, comme Chocolate Doom

    def bit(a, b):
        p = a * n_doom + b
        return (rej[p >> 3] >> (p & 7)) & 1

    recoit = {f: r for f, r, _ in fondus}

    def piece(s):
        while s in recoit:
            s = recoit[s]
        return s

    membres = defaultdict(list)
    for s in range(n_doom):
        membres[piece(s)].append(s)
    used = sorted(set(doom_sector))
    ligne = {a: tuple(int(all(bit(x, y) and bit(y, x) for x in membres[a] for y in membres[b]))
                      for b in used)
             for a in used}
    if not any(any(r) for r in ligne.values()):
        return None, None
    rang = {a: i for i, a in enumerate(used)}
    cle, classe, rep = {}, {}, []
    for a in used:
        if ligne[a] not in cle:
            cle[ligne[a]] = len(rep)
            rep.append(a)
        classe[a] = cle[ligne[a]]
    n = len(rep)
    table = bytearray((n * (n + 1) // 2 + 7) // 8)
    for ca in range(n):
        for cb in range(ca, n):
            if ligne[rep[ca]][rang[rep[cb]]]:
                p = ca * n - ca * (ca - 1) // 2 + cb - ca
                table[p >> 3] |= 1 << (p & 7)
    return [classe[d] for d in doom_sector], dict(classes=n, table=list(table))


def dissoudre_penombres(M, eps=EPS_PENOMBRE):
    """Rend a sa piece toute BANDE MINCE qui n'en differe QUE par la lumiere.

    LA CAUSE EXACTE de la « bordure a texture ecrasee » de la salle tech. Les dalles eclairees sont
    un LUMINAIRE (secteurs Doom 10 et 12, plafond 96, TLITE6_5, deja cale sur la grille) entoure
    d'une PENOMBRE de 8 u (secteurs 9 et 11) identique a la piece (secteur 7) sur TOUT sauf la
    lumiere. Le carre de 64 qui contient cette bande contient DEUX secteurs Doom -- or `sols_pleins`
    n'accorde le debord qu'a un carre tenant dans UN SEUL secteur. Debord refuse, la bande peint donc
    sa cellule de 8 u en y ECRASANT la tuile entiere 8 fois. Rendre la bande au secteur 7 remet le
    carre dans un seul secteur : le debord DEJA EN PLACE le peint entier et exact, sans regle
    nouvelle et sans tuile de plus.

    On ne DEPLACE aucun sommet et on ne SUPPRIME aucun linedef -- seul le secteur des sidedefs
    change. Le BSP du WAD reste donc exact et la partition ne bouge pas. (Caler les sommets sur la
    grille a ete essaye : les segs bougent, les plans de noeud non, MESURE 10 murs pleins INVISIBLES.)

    Le discriminant est l'EPAISSEUR, pas la ressemblance. MESURE E1M1 : 5 paires de secteurs
    adjacents ne different que par la lumiere, mais 3 sont de vraies pieces (21 504, 62 920 et
    59 821 u2, la derniere portant un SPECIAL) -- les fondre aplatirait l'eclairage du niveau. Un
    anneau de largeur w a une aire de w x perimetre / 2, donc `2*aire/perimetre` vaut exactement 8,0
    pour les deux cadres et bien plus pour une piece. MESURE DU CORPUS (200 cartes Doom, 37 683
    secteurs ; 6 cartes Duke) : eps 8 -> 346 secteurs (0,014 % de l'aire de sol), eps 16 -> 795
    (0,064 %), eps 32 -> 1 182 (0,192 %). Aucune rupture naturelle dans la distribution (1,8 a 1 263,
    mediane 29,7) : le seuil est une DECISION, prise a 8 parce que sous 8 u la bande porte sa tuile
    ecrasee >= 8 fois, donc sous ce que le moteur sait representer.

    ⚠ NE PAS plafonner par l'ecart de lumiere : essaye a 64, ce plafond ecartait les DEUX cadres
    d'E1M1 (255 contre 128 = 127), c'est-a-dire le cas qui motive la regle. La luminosite n'a rien a
    voir avec la REPRESENTABILITE.
    ⚠ COUT ASSUME : l'anneau de 8 u prend la lumiere de la piece (vertexLight 16 -> 10), donc le halo
    clair autour des dalles disparait ; les dalles elles-memes restent a 255. C'est le prix de
    l'exactitude, et il est VISIBLE -- le testeur l'a remarque de lui-meme.
    ⚠ NE RIEN AJOUTER D'AUTRE ICI. Avalement des cellules minces, « un carre un proprietaire » et
    election d'un proprietaire ont tous les trois ete essayes le 16-09 : chacun fait RENONCER un
    morceau a un carre que personne ne repeint ensuite en entier -> TROUS (jusqu'a 3 760 u2, sol et
    plafond a 0 %, on voit a travers). Le debord de cette base peint le carre depuis CHAQUE morceau
    qui le touche : c'est redondant, mais c'est ce qui le rend sans trou.
    Retourne [(fondu, receveur, epaisseur)]."""
    if "penombres" not in OPTIM_ACTIFS:
        return []
    S, L, SD = M["sectors"], M["linedefs"], M["sidedefs"]

    def meme(a, b):
        return (a.floorh == b.floorh and a.ceilh == b.ceilh and a.floorpic == b.floorpic
                and a.ceilpic == b.ceilpic and a.special == b.special and a.tag == b.tag)

    declencheur = set()                   # un secteur touche par un linedef a effet ne se fond pas
    for ld in L:
        if ld.special or ld.tag:
            for sd in (ld.right, ld.left):
                if sd >= 0:
                    declencheur.add(SD[sd].sector)
    cand = defaultdict(set)
    for ld in L:
        if ld.right < 0 or ld.left < 0:
            continue
        x, y = SD[ld.right].sector, SD[ld.left].sector
        if x == y or S[x].light == S[y].light or not meme(S[x], S[y]):
            continue
        cand[x].add(y)
        cand[y].add(x)
    fondus, pris = [], set()
    for si in sorted(cand):
        if si in declencheur or S[si].special or S[si].tag or si in pris:
            continue
        per, aire = _perimetre_aire(M, si)
        if per <= 0 or 2.0 * aire / per > eps:
            continue                      # une vraie piece, pas une bande
        # Tous les receveurs possibles sont DEJA equivalents en texture (`meme` impose le meme flat
        # et le meme calage, un flat Doom etant ancre sur la grille du MONDE). C'est en amont que le
        # LUMINAIRE est ecarte : son plafond differe, il n'est donc jamais candidat. Reste a
        # departager par la taille, le plus grand voisin completant le plus de carres.
        rec = max(cand[si], key=lambda k: _perimetre_aire(M, k)[1])
        if rec in pris:
            continue
        for k, sd in enumerate(SD):
            if sd.sector == si:
                SD[k] = sd._replace(sector=rec)
        pris.add(si)
        fondus.append((si, rec, round(2.0 * aire / per, 1)))
    return fondus


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
    global PARTITION, OPTIM_ACTIFS, CAP_CANAL, GROS_BLOC   # make_e1m1 recree un DoomConverter
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--wad", default=os.path.join(os.path.dirname(ROOT), "Mimas", "cd", "data",
                                                  "DOOM1.WAD"))
    ap.add_argument("--map", default="E1M1")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "doom2ps", "e1m1_geom3d.json"))
    ap.add_argument("--cap-cells", type=int, default=CAP_CELLS)
    ap.add_argument("--mobile", action="store_true",
                    help="portes FERMEES + course, push blocks, interrupteurs (SPEC_CONVERTER 5.1) ; "
                         "la sortie gagne une cle `mobile`")
    ap.add_argument("--partition", choices=("grid", "bsp"), default=None,
                    help="morceaux convexes : bsp = feuilles du BSP telles quelles (defaut ; MESURE "
                         "console 16-09 : jusqu'a 15 ms de moins que grid, et le debord de carres "
                         "entiers lui rend les memes sols exacts) ; grid = BSP recoupe sur la grille de 64")
    ap.add_argument("--uwin-budget", type=int, default=120,
                    help="tuiles NETTES accordees aux fenetres horizontales (choisir_fenetres). "
                         "MESURE 2026-09-21, qualite APRES le budget de tuiles de la carte "
                         "(doomtiles.reduire) : monter de 12 a 120 fait passer les murs a l'echelle "
                         "juste de 282 a 342 sur E1M1, 209 a 307 sur E1M3, 229 a 361 sur E1M5, "
                         "370 a 573 sur E1M9 -- contre un ecart de fusion (rms) qui monte de 0,3 a "
                         "2,5 sur E1M1 et de 0,0 a 1,3 sur E1M9. Au-dela de 120 le gain plafonne "
                         "et l'ecart continue de monter")
    ap.add_argument("--static-doors", action="store_true",
                    help="portes ouvertes en dur (open_doors, controle visuel) -- le defaut sans --mobile")
    ap.add_argument("--diag-fusion", action="store_true",
                    help="DIAGNOSTIC : repeint en damier les murs des feuilles nees d'une fusion "
                         "(se combine avec --optim all). Ne jamais livrer un disque avec ca.")
    ap.add_argument("--optim", default="defaut",
                    help="optimisations de NIVEAU : `defaut` (= %s), `all` (= %s), `none`, ou une "
                         "liste. Elles modifient la geometrie ; `none` rend exactement la carte "
                         "dessinee." % (", ".join(OPTIM_DEFAUT), ", ".join(OPTIMS)))
    ap.add_argument("--cap-canal", type=int, default=CAP_CANAL,
                    help="avec `--optim fusion` : secteurs qu'on s'autorise dans un canal de plans "
                         "de coupe (defaut %d = le maximum de tout le jeu retail ; le plafond DUR "
                         "du moteur est MAXCUTSECTORS=%d pour le niveau entier). Plus haut = plus "
                         "de fusions, mais le tri du moteur est une insertion en une passe."
                         % (CAP_CANAL, MAXCUTSECTORS))
    ap.add_argument("--gros-bloc", type=int, default=GROS_BLOC,
                    help="avec `--optim ...,grossiers` : plus gros bloc, en carres de %d u (defaut %d)"
                         % (TILESIZE, GROS_BLOC))
    a = ap.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if a.partition:
        PARTITION = a.partition          # module : build_objects (make_e1m1) doit decouper pareil
    OPTIM_ACTIFS = lire_optim(a.optim)   # idem : make_e1m1 recree un DoomConverter
    CAP_CANAL = a.cap_canal              # idem
    GROS_BLOC = a.gros_bloc              # idem

    W = wadmod.Wad(a.wad)
    M = wadmod.read_map(W, a.map)
    fondus = dissoudre_penombres(M)       # make_e1m1.build_objects fond LES MEMES
    sizes = {nm: (t["width"], t["height"]) for nm, t in W.textures().items()}
    print(f"doom2ps E3 : {a.map} de {os.path.basename(a.wad)} -- "
          f"{len(M['sectors'])} secteurs Doom, {len(M['subsectors'])} feuilles BSP, "
          f"{len(M['segs'])} segs")
    specials = tags = None
    if a.mobile and not a.static_doors:
        specials = sp.specials_of(M)
        nportes = close_doors(M, specials)
        raise_floors(M, specials)
        mobiles = specials.doors + specials.lifts + specials.floors + specials.raises
        tags = {}
        for d in mobiles:
            tags[d["sector"]] = MobileTag(d["sector"], d["kind"], d["lower"], d["upper"])
        print(f"  mobile : {len(specials.doors)} portes fermees (fente {DOOR_SLIT} u, {nportes} "
              f"plafonds abaisses), {len(specials.lifts)} ascenseurs, {len(specials.floors)} sols, "
              f"{len(specials.raises)} sols qui montent (emis en haut), "
              f"{len(specials.teleports)} lignes de teleporteur, "
              f"{len(specials.wswitch)} lignes W, {len(specials.sswitch)} lignes S, "
              f"{len(specials.exits)} sorties, {len(specials.damage)} secteurs a degats"
              + (f" ; ignores : {specials.ignored}" if specials.ignored else ""))
        for d in mobiles:
            print(f"    secteur {d['sector']:3d} {d['kind']:13s} course {d['lower']} .. {d['upper']}"
                  f" ({d['upper'] - d['lower']} u)" + (" -- arrete a la fente sous son plafond"
                                                        if d.get("fente") else ""))
        switch_lines = {s["line"]: s for s in specials.sswitch}
        def mk(u=None):
            nonlocal tags                       # un MobileTag accumule les murs de SON passage
            tags = {d["sector"]: MobileTag(d["sector"], d["kind"], d["lower"], d["upper"])
                    for d in mobiles}
            return DoomConverter(M, sizes, a.cap_cells, mobile=tags, switch_lines=switch_lines,
                                 uwin=u)
    else:
        nportes = open_doors(M)
        if nportes:
            print(f"  {nportes} portes ouvertes (plafond = plus bas plafond voisin - 4, "
                  f"regle de EV_VerticalDoor)")
        def mk(u=None):
            return DoomConverter(M, sizes, a.cap_cells, uwin=u)
    conv = mk()
    print(f"  decoupe {conv.partition} : {len(conv.keep)} morceaux convexes pour "
          f"{len(M['subsectors'])} feuilles BSP")
    em = conv.build()
    if conv.uwin_use:
        # 2e passage : seulement les fenetres que le budget de tuiles accorde
        avant, n_cand = len(em.tiles), len(conv.uwin_use)
        pris = conv.choisir_fenetres(a.uwin_budget)
        portes = len(pris & conv.door_faces)
        conv = mk(pris)
        em = conv.build()
        print(f"  fenetres horizontales : {len(pris)}/{n_cand} faces de linedef candidates "
              f"({portes} faces de porte, hors budget), budget {a.uwin_budget} tuiles NETTES ; "
              f"{len(em.tiles)} tuiles, contre {avant} en fenetrant tout")
    cutplane, sans_plan, pire_plan = plans_de_coupe(conv)
    if cutplane:
        canaux = Counter(s["cutChannel"] for s in em.sectors if s["flags"] & SECFLAG_CUTSORT)
        print(f"  plans de coupe : {len(cutplane)} secteurs tries par plan "
              f"(plafond {MAXCUTSECTORS}), {len(canaux)} canaux, le plus gros de "
              f"{max(canaux.values())} (--cap-canal {CAP_CANAL}), "
              f"pire depassement {pire_plan:.2f} u"
              + (f" -- {sans_plan} PAIRE(S) SANS PLAN, LE MOTEUR LIRAIT 99" if sans_plan else ""))
    # Les points de vue du niveau et ce qu'on y voit : LE calcul cher, partage par les paires
    # d'ordre et la carte de cout. Il ne change rien a ce qui est emis, donc il a lieu meme quand
    # `ordre` est coupe -- un diagnostic ne se desactive pas avec une optimisation.
    vu = ordre.visibilite(em.sectors, em.walls, em.vertices)
    paires = []
    if "ordre" in OPTIM_ACTIFS:
        # Deux secteurs sans portail commun ne recoivent AUCUNE contrainte de buildTree : seul le
        # scalaire `distance` les separe, et il se trompe de part et d'autre d'un obstacle. Voir
        # tools/ordre.py pour la mesure et pour ce que coutent les autres remedes.
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
    if a.diag_fusion:
        print(f"  DIAGNOSTIC : {peindre_fusions(conv)} murs repeints en damier "
              f"(feuilles nees d'une fusion)")
    print(f"  {len(em.sectors)} secteurs, {len(em.walls)} murs, {len(em.vertices)} sommets, "
          f"{len(em.faces)} faces, {len(em.texture)} octets de texture, "
          f"{len(em.vertexLight)} lumieres, {len(em.tiles)} tuiles distinctes")
    for k in sorted(conv.stats):
        print(f"    {k} : {conv.stats[k]}")

    # depart du joueur (THING type 1)
    st = next((t for t in M["things"] if t.type == 1), None)
    if st is None:
        raise SystemExit("pas de depart joueur (thing type 1)")
    lf = conv.leaf_at(st.x, st.y)
    sect = conv.remap.get(lf, 0)
    # assemble.py recalcule px, pz depuis build_xy : px = bx // 8, pz = -(by // 8)
    build_xy = [int(st.x) * 8, -int(st.y) * 8]
    # Angle. `suckSpriteParams` fait `angle = normalizeAngle(short * 5760)` (OBJECT.C:194) et
    # F(90) = 5898240 = 1024 * 5760, donc **l'unite du .LEV est 360/4096 de degre**.
    # Doom : 0 = est, sens trigonometrique ; un sprite : vel.x = cos(angle), vel.z = sin(angle)
    # (AI.C:528-529) avec Z = +y_doom = nord -> meme sens, l'angle Doom s'ecrit tel quel. L'init du
    # joueur retire 90 degres (`angle - F(90)`, AI.C:51) pour passer a la convention de la CAMERA
    # (regard (-sin yaw, cos yaw), SRUINS.C:389) : il ne faut PAS les ajouter ici -- le +90 d'avant
    # faisait arriver le joueur tourne de 90 degres a gauche (console, 2026-09-18).
    angle = int(round((st.angle % 360) * 4096.0 / 360.0))

    crit = check(em, conv)
    crit["depart_present"] = dict(ok=True, depart=dict(sector_gen1=sect, build_xy=build_xy,
                                                       angle_doom=st.angle, angle_lev=angle,
                                                       feuille=lf))
    dsec = [conv.leaf_sector[li] for li in conv.keep]
    rclasse, rejet = classes_de_rejet(W, a.map, len(M["sectors"]), dsec, fondus)
    if rclasse:
        for s, c in zip(em.sectors, rclasse):
            s["rejectClass"] = c
        print(f"  rejet : {len(M['sectors'])} secteurs Doom -> {rejet['classes']} classes, "
              f"{len(rejet['table'])} o (lump {(len(M['sectors']) ** 2 + 7) // 8} o)")
    out = dict(format="doom2ps/e3-geom3d v1",
               source=dict(wad=os.path.basename(a.wad), map=a.map, optim=sorted(OPTIM_ACTIFS),
                           **(dict(gros_bloc=GROS_BLOC) if "grossiers" in OPTIM_ACTIFS else {})),
               conventions=dict(repere="X = x_doom, Y = z_doom, Z = y_doom (1:1)",
                                morceaux="feuilles du BSP du WAD",
                                tuile=f"TILESIZE {TILESIZE}",
                                placage="E4.1b : cellule = une repetition de la texture"),
               stats=dict(conv.stats), criteres=crit, tiles=em.tiles,
               picnames=conv.picnames,
               sectors=em.sectors, walls=em.walls, vertices=em.vertices, faces=em.faces,
               cutPlane=cutplane, orderPairs=[list(p) for p in paires],
               texture=em.texture, vertexLight=em.vertexLight, anims=conv.anims,
               doom_sector=dsec, reject=rejet)
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

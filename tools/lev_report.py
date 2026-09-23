#!/usr/bin/env python3
"""lev_report.py -- ce qu'un .LEV coute et ce qui le tuerait, lu depuis le FICHIER seul.

CE QU'IL AJOUTE. Les verificateurs des convertisseurs (`doom2ps/verif_doom.py`, `duke2ps/verif_e3.py`)
jugent un niveau QUE LEUR CONVERTISSEUR VIENT DE PRODUIRE : ils ont besoin des intermediaires
(`*_geom3d.json`, le WAD, la carte Build). Celui-ci ne lit que le `.LEV`, donc il juge AUSSI les
24 niveaux retail -- et c'est la tout l'interet, parce que le retail est le seul etalon de ce qui
tourne vraiment sur la machine. C'est l'outil qu'un auteur de niveau lance apres sa conversion, et
la source des chiffres qu'on renvoie vers son editeur (`--json`, voir docs/LEVEL_EDITING_PLAN.md).

TROIS FAMILLES, ET LA DEUXIEME EST LA RAISON D'ETRE DU FICHIER.

1. CE QUI FAIT PLANTER LE CHARGEUR. Delegue a `duke2ps/lev_write.engine_problems`, qui rejoue les
   `assert` de LEVEL.C / PLAX.C / SOUND.C / PIC.C / SEQUENCE.C. Rien a reecrire ici.

2. CE QUI NE PLANTE PAS ET CASSE QUAND MEME -- les defauts SILENCIEUX. Aucun `assert` ne les
   couvre, et `assert` est de toute facon un no-op sur un disque NDEBUG (UTIL.H:124 contre :129) :
   sur la console ils ne se manifestent que par une image fausse ou une salle qui n'apparait pas.
   Le plus couteux est `portails_en_tete` : `findDoorways` (WALLS.C:2833-2835) sort de la boucle
   AU PREMIER mur sans `nextSector`, donc un portail range derriere un mur plein n'est jamais
   franchi et toute la geometrie derriere lui est invisible -- sans message, sans ralentissement,
   sans trace. C'est le controle qui justifie a lui seul de lancer cet outil.

3. CE QUE CA COUTE. La carte de `tools/cout.py` (loi mesuree sur console, 12-09) plus la residence
   RAM et la pression sur le cache de tuiles du VDP1.

⚠ LA LOI MAJORE, ET IL FAUT LE DIRE A L'AUTEUR. `cout.py` documente deja que les paliers
470 / 896 / 1321 cellules viennent d'un build ASSERT et sur-estiment la pente d'environ 1,5x en
NDEBUG. La MESURE DU 23-09 faite par cet outil le confirme par l'autre bout : passes a la meme
moulinette, les 24 niveaux retail donnent une mediane de cone allant de 452 (TOMBEND) a 1441
(SETARENA), mediane des medianes 928 -- autrement dit PowerSlave commercial serait declare
majoritairement sous les 30 images par seconde par ces paliers. Ils ne sont donc pas un seuil de
recette ; ils situent. Le vrai etalon est la DISTRIBUTION RETAIL, et c'est pour ca que `--etalon`
existe : la question utile n'est pas « suis-je au-dessus de 470 » mais « suis-je plus cher que le
pire niveau qui a ete presse sur un CD ».

Usage : python tools\\lev_report.py [FICHIER.LEV ...] [--map MAIN.map] [--tile-base N] [--slots N]
                                    [--secteurs] [--json OUT.json] [--etalon] [--vite]
        python tools\\lev_report.py --ecrire-etalon      (recalcule tools/lev_etalon.json)
Sans fichier : tous les refs/extract/PS/*.LEV. Code de retour 1 si un defaut FATAL est trouve.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "duke2ps"))
import cout                                             # noqa: E402
import lev                                              # noqa: E402
import lev_io                                           # noqa: E402
import lev_write                                        # noqa: E402
import ordre                                            # noqa: E402

ETALON = os.path.join(HERE, "lev_etalon.json")

# --- plafonds du moteur -------------------------------------------------------------------
MAXNMSECTORS = 600          # UTIL.H:21
MAXNMWALLS = 5500           # UTIL.H:22
MAXCUTSECTORS = 128         # SLEVEL.H:191
MAXVPERWALL = 700           # WALLS.C:1146, :1783, :2018 -- sommets ET cellules d'un mur
MAXNMSLAVEPOLYS = 1300      # walls.h:101 ; au-dela l'esclave LACHE le mur (WALLS.C:2304, :2516)
MAXFANIN = 20               # WALLS.C:3444 -- ancestor[] est de taille fixe
TAILLE_NIVEAU_MAX = 900000  # LEVEL.C:65-66

# --- ce qui n'est pas dans le .LEV et doit donc etre passe --------------------------------
# tileBase = le nombre de tuiles d'ARME lues dans STATIC.DAT avant le niveau (SRUINS.C:2376,
# :2386) ; le chargeur l'ajoute aux octets IMPAIRS de texture[] et a chaque face.tile
# (LEVEL.C:122-125), qui sont des OCTETS. MESURE 23-09 en depliant les deux STATIC.DAT :
# 40 pour PowerSlave retail, 96 pour le build Doom du fork.
TILEBASE = dict(ps=40, doom=96)
# Slots du cache de tuiles du VDP1. MESURE : gameparams.cfg:60 en donne 28 (PowerSlave),
# params/doom.cfg:130 en donne 32 (Doom). Coder 28 en dur sous-estimerait Doom de 4 slots.
SLOTS = dict(ps=28, doom=32)

# --- classes de tuiles (PIC.C:98-106, SLEVEL.H:216) ---------------------------------------
T_16_64, T_VDP2, T_8RLE_64, T_8RLE_32, T_16_32, T_16RLE_64 = 0x32, 0x01, 0x6A, 0x6C, 0x34, 0x72
# Le moteur exige la classe TILE16BPP sous une cellule de geometrie (WALLS.C:1981 pour la grille,
# :2049 pour le maillage). Seuls 0x32 et 0x72 la portent.
GEOMETRIE_OK = (T_16_64, T_16RLE_64)
# Ce qu'une tuile occupe dans le POOL une fois chargee. 0x32 garde ses 4 096 octets d'index
# (PIC.C:932, COMPRESS16BPP) ; une RLE garde son flux compresse (PIC.C:993) ; une VDP2 n'a pas
# de pixels du tout (PIC.C:1180).
def octets_tuile(t):
    f = t["flags"]
    if f == T_16_64:
        return 4096
    if f == T_16_32:
        return 1024
    if f in (T_8RLE_64, T_8RLE_32, T_16RLE_64):
        return len(t["rle"])
    return 0


def align4(n):
    return (n + 3) & ~3


# ============================================================ 1. lecture
def charger(chemin):
    """-> (modele lev_io, layout lev.py). Les deux : le modele porte le CONTENU, le layout les
    OFFSETS (dont un applicateur de retouches a besoin, cf. docs/LEVEL_EDITING_PLAN.md)."""
    with open(chemin, "rb") as f:
        data = f.read()
    modele, lay = lev_io.model_from_bytes(data, os.path.basename(chemin))
    return modele, lay, len(data)


# ============================================================ 2. defauts silencieux
def _cellules_du_mur(w):
    """Nombre de cellules qu'un mur demande au peintre, par la MEME regle que cout.py."""
    if w["flags"] & 0x01:                       # WALLFLAG_PARALLELOGRAM : grille
        return w["tileLength"] * w["tileHeight"]
    if w["firstFace"] >= 0:                     # maillage : liste libre de quads
        return w["lastFace"] - w["firstFace"] + 1
    return 0                                    # portail sans geometrie


def defauts_silencieux(modele, tile_base):
    """-> [(gravite, code, texte)] avec gravite dans {'FATAL', 'ALERTE'}.

    FATAL = le niveau est faux sur la console meme si le chargeur ne bronche pas.
    ALERTE = le retail le fait deja quelque part, donc ce n'est pas un refus : c'est un chiffre a
    connaitre. La frontiere entre les deux a ete posee en passant TOUS les criteres sur les 24
    niveaux retail (23-09) : tout critere que le retail enfreint est une ALERTE, par construction.
    """
    P = []
    Lv = modele["level"]
    S, W, V, F = Lv["sectors"], Lv["walls"], Lv["vertices"], Lv["faces"]
    TEX, VL = Lv["texture"], Lv["vertexLight"]
    tuiles = modele["tiles"]

    # --- 2.1 les tableaux fixes du moteur, ecrits AVANT que l'assert ne parle ---------------
    # MAXNMSECTORS / MAXNMWALLS ne sont verifies qu'a SRUINS.C:2432-2433, c'est-a-dire APRES
    # loadLevel, loadTiles, loadSequences et initWallRenderer. Sur un disque NDEBUG un niveau
    # trop gros a deja ecrit par-dessus sectorDraw[600] et doorwayCache[5500] : ce n'est pas un
    # avertissement, c'est de la corruption.
    if len(S) > MAXNMSECTORS:
        P.append(("FATAL", "secteurs", "%d secteurs > %d (UTIL.H:21) -- les tableaux fixes du "
                  "moteur sont deja deborde pendant le chargement" % (len(S), MAXNMSECTORS)))
    if len(W) > MAXNMWALLS:
        P.append(("FATAL", "murs", "%d murs > %d (UTIL.H:22) -- idem, doorwayCache est deborde"
                  % (len(W), MAXNMWALLS)))

    # --- 2.2 LE controle : les portails d'abord ---------------------------------------------
    # findDoorways (WALLS.C:2833-2835) parcourt firstWall..lastWall et fait `return` au PREMIER
    # mur dont nextSector vaut -1. Un portail place apres un mur plein n'est donc jamais franchi,
    # et tout ce qu'il ouvre disparait de l'image. Aucun assert nulle part.
    mal_ranges = []
    for si, s in enumerate(S):
        plein_vu = False
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            if W[wi]["nextSector"] < 0:
                plein_vu = True
            elif plein_vu:
                mal_ranges.append((si, wi))
                break
    if mal_ranges:
        ex = ", ".join("s%d/m%d" % t for t in mal_ranges[:6])
        P.append(("FATAL", "portails_en_tete",
                  "%d secteurs ont un portail range APRES un mur plein (%s%s) -- findDoorways "
                  "s'arrete au premier mur plein (WALLS.C:2833), ces portails ne sont jamais "
                  "franchis et les salles derriere ne sont jamais dessinees"
                  % (len(mal_ranges), ex, "..." if len(mal_ranges) > 6 else "")))

    # --- 2.3 l'octet de tuile, apres tileBase ------------------------------------------------
    # face.tile et les octets IMPAIRS de texture[] sont des `unsigned char` (SLEVEL.H:141, :177)
    # auxquels loadLevel ajoute tileBase (LEVEL.C:122-125). Le depassement est un WRAP silencieux
    # qui pose une tuile d'arme sur un mur.
    imax_face = max((f["tile"] for f in F), default=-1)
    imax_tex = max((TEX[i] for i in range(1, len(TEX), 2)), default=-1)
    imax = max(imax_face, imax_tex)
    if imax + tile_base > 255:
        P.append(("FATAL", "octet_tuile",
                  "index de tuile de geometrie max %d + tileBase %d = %d > 255 (LEVEL.C:122-125) "
                  "-- l'octet enroule et la cellule affiche une tuile d'arme"
                  % (imax, tile_base, imax + tile_base)))

    # --- 2.4 la classe de la tuile sous une cellule ------------------------------------------
    # WALLS.C:1981 et :2049 exigent TILE16BPP. MESURE 23-09 sur les 24 retail + le build Doom :
    # 279 399 references de face.tile et 89 746 de texture[impair] tombent TOUTES sur du 0x32.
    mauvaises = sorted({i for i in set([f["tile"] for f in F] + [TEX[i] for i in range(1, len(TEX), 2)])
                        if i < len(tuiles) and tuiles[i]["flags"] not in GEOMETRIE_OK})
    if mauvaises:
        P.append(("FATAL", "classe_tuile",
                  "%d tuiles de geometrie ne sont pas TILE16BPP (0x32/0x72) : %s (WALLS.C:1981, :2049)"
                  % (len(mauvaises), ", ".join("#%d=0x%02x" % (i, tuiles[i]["flags"]) for i in mauvaises[:8]))))
    hors = sorted({i for i in set([f["tile"] for f in F]) if i >= len(tuiles)})
    if hors:
        P.append(("FATAL", "tuile_absente", "face.tile pointe hors du jeu de %d tuiles : %s"
                  % (len(tuiles), ", ".join(str(i) for i in hors[:8]))))

    # --- 2.5 le motif de cellule --------------------------------------------------------------
    # L'octet PAIR de chaque cellule indexe pattern[8][4] (WALLS.C:1153) et WALLS.C:1951 l'assert
    # < 8. Au-dela on lit hors du tableau, donc des sommets au hasard.
    motifs = [TEX[i] for i in range(0, len(TEX), 2)]
    if motifs and max(motifs) > 7:
        P.append(("FATAL", "motif", "octet de motif max %d > 7 (WALLS.C:1951, pattern[8][4] :1153)"
                  % max(motifs)))

    # --- 2.6 l'indexation RELATIVE des sommets de face ---------------------------------------
    # face.v[] est relatif a wall.firstVertex (WALLS.C:2021-2040, :922). Un convertisseur qui
    # ecrit des index globaux produit un fichier qui se LIT parfaitement et se dessine faux :
    # les valeurs restent des index globaux valides. lev.py:validate ne voit pas ce defaut.
    hors_plage = 0
    for w in W:
        if w["firstFace"] < 0:
            continue
        span = w["lastVertex"] - w["firstVertex"] + 1
        for fi in range(w["firstFace"], w["lastFace"] + 1):
            if any(not (0 <= v < span) for v in F[fi]["v"]):
                hors_plage += 1
    if hors_plage:
        P.append(("FATAL", "face_relative",
                  "%d faces ont un sommet hors de [0, lastVertex-firstVertex] -- face.v[] est "
                  "RELATIF a wall.firstVertex (WALLS.C:2021-2040)" % hors_plage))

    # --- 2.7 un sol et un plafond par secteur -------------------------------------------------
    # Le sol et le plafond sont des murs ORDINAIRES de firstWall..lastWall, reconnus au seul signe
    # de normal[1] (UTIL.C:41 findFloorDistance, :67 findCeilDistance). MESURE 23-09 : exactement
    # un de chaque dans les 8 408 secteurs retail. findFloorDistance rend n'importe quoi sinon.
    bancals = []
    for si, s in enumerate(S):
        sols = plafs = 0
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            n1 = W[wi]["normal"][1]
            if n1 > 0:
                sols += 1
            elif n1 < 0:
                plafs += 1
        if (sols, plafs) != (1, 1):
            bancals.append((si, sols, plafs))
    if bancals:
        P.append(("FATAL", "un_sol_un_plafond",
                  "%d secteurs n'ont pas exactement 1 sol et 1 plafond (%s) -- findFloorDistance "
                  "prend le premier normal[1] > 0 (UTIL.C:41)"
                  % (len(bancals), ", ".join("s%d:%d/%d" % t for t in bancals[:6]))))

    # --- 2.8 les plafonds par mur --------------------------------------------------------------
    trop_larges = [wi for wi, w in enumerate(W) if _cellules_du_mur(w) >= MAXVPERWALL]
    if trop_larges:
        P.append(("FATAL", "mur_trop_large",
                  "%d murs demandent >= %d cellules (WALLS.C:1146, :1783, :2018) : %s"
                  % (len(trop_larges), MAXVPERWALL, ", ".join("m%d=%d" % (wi, _cellules_du_mur(W[wi]))
                                                              for wi in trop_larges[:6]))))
    lache = [wi for wi, w in enumerate(W) if _cellules_du_mur(w) > MAXNMSLAVEPOLYS - 50]
    if lache:
        P.append(("ALERTE", "mur_lache_par_esclave",
                  "%d murs approchent MAXNMSLAVEPOLYS %d -- au-dela l'esclave abandonne le mur "
                  "sans rien dire (WALLS.C:2304, :2516)" % (len(lache), MAXNMSLAVEPOLYS)))

    # --- 2.9 le tri : cutIndex et le degre entrant ---------------------------------------------
    ncut = len(Lv["cutPlane"])
    mauvais_cut = [si for si, s in enumerate(S) if s["cutIndex"] and not (0 <= s["cutIndex"] < ncut)]
    if mauvais_cut:
        P.append(("FATAL", "cutIndex",
                  "%d secteurs ont un cutIndex hors des %d lignes de cutPlane (WALLS.C:3518)"
                  % (len(mauvais_cut), ncut)))
    if ncut > MAXCUTSECTORS:
        P.append(("FATAL", "cutSectors", "%d secteurs de coupe > %d (SLEVEL.H:191)" % (ncut, MAXCUTSECTORS)))

    # Degre entrant : buildTree remplit ancestor[MAXFANIN] (WALLS.C:3444). Le compte STATIQUE
    # majore le compte par image, et le retail le depasse deja (CAVERN : 36) -- d'ou ALERTE.
    entrant = Counter()
    for w in W:
        if w["nextSector"] >= 0:
            entrant[w["nextSector"]] += 1
    pires = [(s, n) for s, n in entrant.items() if n > MAXFANIN]
    if pires:
        pires.sort(key=lambda t: -t[1])
        P.append(("ALERTE", "fanin",
                  "%d secteurs recoivent > %d portails (pire s%d avec %d) -- borne haute de "
                  "ancestor[MAXFANIN] (WALLS.C:3444) ; le retail monte a 36, donc a surveiller, "
                  "pas a refuser" % (len(pires), MAXFANIN, pires[0][0], pires[0][1])))

    # --- 2.10 le curseur des parametres d'objet -------------------------------------------------
    # OBJECT.C:251 assert(level_object[o].firstParam == objectPPos) : les blocs de parametres
    # doivent se suivre exactement, sans trou ni recouvrement.
    objets = Lv["objects"]
    if objets:
        for i in range(1, len(objets)):
            if objets[i]["firstParam"] < objets[i - 1]["firstParam"]:
                P.append(("FATAL", "firstParam",
                          "objet %d a un firstParam %d inferieur au precedent %d -- OBJECT.C:251 "
                          "exige un curseur strictement croissant"
                          % (i, objets[i]["firstParam"], objets[i - 1]["firstParam"])))
                break
        if objets[0]["firstParam"] != 0:
            P.append(("FATAL", "firstParam0", "le premier objet commence a %d et non a 0 (OBJECT.C:251)"
                      % objets[0]["firstParam"]))

    # --- 2.11 la grille de lumiere ---------------------------------------------------------------
    # La grille de lumiere d'un mur en parallelogramme compte (tileHeight+1) x (tileLength+1)
    # entrees a partir de firstLight, PAS tileHeight x tileLength.
    for wi, w in enumerate(W):
        if not (w["flags"] & 0x01):
            continue
        fin = w["firstLight"] + (w["tileHeight"] + 1) * (w["tileLength"] + 1)
        if fin > len(VL):
            P.append(("FATAL", "grille_lumiere",
                      "mur %d lit la lumiere jusqu'a %d pour un tableau de %d (grille "
                      "(tileHeight+1)x(tileLength+1))" % (wi, fin, len(VL))))
            break
    return P


# ============================================================ 3. budget
def residence(modele):
    """Octets qui restent dans le pool mem_malloc apres le chargement.

    ⚠ CE N'EST PAS LA TAILLE DU FICHIER, et l'ecart atteint ~400 Ko : les PCM sont recopies dans
    la RAM du SCSP puis leur tampon est LIBERE (SOUND.C:255), et le ciel part directement en VRAM
    VDP2 (PLAX.C). Ne restent que le bloc niveau, les palettes, les tuiles et les sequences."""
    Lv = modele["level"]
    sq = modele["sequences"]
    niveau = 56 + sum(len(p) for _, p in lev_write.level_parts(Lv))
    palettes = 2 + 512 * len(modele["palettes"]["palettes"])
    tuiles = sum(align4(octets_tuile(t)) for t in modele["tiles"])
    seqs = 12 + 2 * len(sq["sequence"]) + 8 * len(sq["frames"]) + 8 * len(sq["chunks"]) \
        + 2 * len(sq["sequenceMap"])
    return dict(niveau=align4(niveau), palettes=align4(palettes), tuiles=tuiles,
                sequences=align4(seqs),
                total=align4(niveau) + align4(palettes) + tuiles + align4(seqs))


def pool_depuis_map(chemin_map):
    """Le pool mem_malloc = 1 Mo de LWRAM + (0x06100000 - _end) de HWRAM (UTIL.C:429-438).

    _end n'est PAS dans le .LEV : il sort du .map de l'edition de liens DU BUILD VISE. C'est
    pourquoi le budget de tuiles est une option et non un resultat par defaut."""
    with open(chemin_map, encoding="utf-8", errors="replace") as f:
        txt = f.read()
    vu = False
    for ligne in txt.splitlines():
        if "Linker script and memory map" in ligne:
            vu = True
        if vu and "_end" in ligne:
            for mot in ligne.split():
                if mot.startswith("0x"):
                    fin = int(mot, 16)
                    if 0x06000000 < fin < 0x06100000:
                        return 1024 * 1024 + (0x06100000 - fin), fin
    return None, None


# ============================================================ 4. cout
def cout_du_niveau(modele, vite=False):
    Lv = modele["level"]
    S, W, V = Lv["sectors"], Lv["walls"], Lv["vertices"]
    cel = cout.cellules_par_secteur(S, W)
    if vite:
        return None, cel, None
    geo, vues = ordre.visibilite(S, W, V)
    positions, st = cout.carte(S, W, geo, vues)
    return st, cel, positions


def tuiles_par_secteur(modele):
    """Tuiles de geometrie DISTINCTES visibles depuis un secteur, sans bouger : la pression que
    la piece met sur le cache de tuiles du VDP1 (28 slots PowerSlave, 32 Doom). Un secteur qui
    demande plus de tuiles distinctes qu'il n'y a de slots fait tourner le cache DANS une seule
    image -- c'est `vswaps` dans l'overlay STATUSTEXT."""
    Lv = modele["level"]
    S, W, F, TEX = Lv["sectors"], Lv["walls"], Lv["faces"], Lv["texture"]
    out = []
    for s in S:
        vu = set()
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            if w["flags"] & 0x01:
                base, n = w["textures"], w["tileLength"] * w["tileHeight"]
                for k in range(n):
                    j = base + 2 * k + 1
                    if j < len(TEX):
                        vu.add(TEX[j])
            elif w["firstFace"] >= 0:
                for fi in range(w["firstFace"], w["lastFace"] + 1):
                    vu.add(F[fi]["tile"])
        out.append(len(vu))
    return out


# ============================================================ 5. etalon
def charger_etalon():
    if not os.path.exists(ETALON):
        return None
    with open(ETALON, encoding="utf-8") as f:
        return json.load(f)


def rang(valeur, serie):
    """Position d'une valeur dans une serie triee, en pourcentage (0 = moins cher que tout)."""
    if not serie:
        return None
    t = sorted(serie)
    n = sum(1 for v in t if v < valeur)
    return 100.0 * n / len(t)


# ============================================================ 6. rapport
def analyser(chemin, args):
    modele, lay, fsize = charger(chemin)
    Lv = modele["level"]
    base = args.tile_base if args.tile_base is not None else TILEBASE[args.jeu]
    slots = args.slots if args.slots is not None else SLOTS[args.jeu]

    dur = lev_write.engine_problems(modele)
    doux = defauts_silencieux(modele, base)
    res = residence(modele)
    st, cel, _pos = cout_du_niveau(modele, vite=args.vite)
    tps = tuiles_par_secteur(modele)

    geo_tuiles = sorted({f["tile"] for f in Lv["faces"]}
                        | {Lv["texture"][i] for i in range(1, len(Lv["texture"]), 2)})
    return dict(
        fichier=os.path.basename(chemin), taille_fichier=fsize,
        secteurs=len(Lv["sectors"]), murs=len(Lv["walls"]), sommets=len(Lv["vertices"]),
        faces=len(Lv["faces"]), objets=len(Lv["objects"]),
        tuiles=len(modele["tiles"]), tuiles_geometrie=len(geo_tuiles),
        palettes=len(modele["palettes"]["palettes"]),
        taille_niveau=56 + sum(len(p) for _, p in lev_write.level_parts(Lv)),
        tile_base=base, index_tuile_max=max(geo_tuiles) if geo_tuiles else -1,
        residence=res, cellules=sum(cel), cellules_secteur=cel, tuiles_secteur=tps,
        slots=slots, cache_sature=sum(1 for n in tps if n > slots),
        cout=st, asserts=dur, silencieux=doux,
    )


def imprimer(r, etalon=None, secteurs=False):
    print("=" * 78)
    print("%s -- %d secteurs, %d murs, %d sommets, %d faces, %d objets"
          % (r["fichier"], r["secteurs"], r["murs"], r["sommets"], r["faces"], r["objets"]))
    print("   %d tuiles dont %d de geometrie (index max %d + tileBase %d = %d / 255), %d palettes"
          % (r["tuiles"], r["tuiles_geometrie"], r["index_tuile_max"], r["tile_base"],
             r["index_tuile_max"] + r["tile_base"], r["palettes"]))

    print("\n-- chargeur (asserts LEVEL.C / PLAX.C / SOUND.C / PIC.C / SEQUENCE.C)")
    if r["asserts"]:
        for p in r["asserts"]:
            print("   REFUS  %s" % p)
    else:
        print("   ok     bloc niveau %d / %d o, aucun assert du chargeur n'est enfreint"
              % (r["taille_niveau"], TAILLE_NIVEAU_MAX))

    print("\n-- defauts silencieux (aucun assert ne les couvre ; NDEBUG n'en a aucun de toute facon)")
    if r["silencieux"]:
        for grav, code, txt in r["silencieux"]:
            print("   %-6s %-20s %s" % (grav, code, txt))
    else:
        print("   ok     portails en tete, octet de tuile, classe de tuile, motif, faces "
              "relatives, un sol / un plafond, curseur d'objets")

    res = r["residence"]
    print("\n-- RAM residente (pool mem_malloc ; SANS les PCM ni le ciel, liberes au chargement)")
    print("   %7d o  niveau %d + palettes %d + tuiles %d + sequences %d   (fichier : %d o)"
          % (res["total"], res["niveau"], res["palettes"], res["tuiles"], res["sequences"],
             r["taille_fichier"]))
    if r.get("pool"):
        libre = r["pool"] - res["total"]
        print("   pool %d o (_end 0x%08x) -> %s%d o libres = %d tuiles de 4 Ko"
              % (r["pool"], r["end"], "+" if libre >= 0 else "", libre, libre // 4096))

    st = r["cout"]
    if st:
        _mn, med, p90, mx = st["cone"]
        print("\n-- cout (loi de cout.py ; MAJORANT, voir l'avertissement du module)")
        print("   %d cellules au total, %d positions debout ; cone %.0f deg : mediane %d, p90 %d, "
              "max %d (~%.1f ms)" % (st["cellules_niveau"], st["positions"], cout.FOV, med, p90,
                                     mx, cout.millisecondes(mx)))
        sur = ", ".join("%d positions > %d (%d fps)" % (st["au_dessus"][s], s, f)
                        for s, f in cout.PALIERS if st["au_dessus"][s])
        if sur:
            print("   %s" % sur)
        p = st["pire"]
        if p:
            print("   pire vue en (%d, %d), secteur %d, %d secteurs visibles, %d cellules"
                  % (p["x"], p["z"], p["secteur"], p["visibles"], p["cellules"]))
        if etalon:
            serie_med = [v["cone"][1] for v in etalon.values()]
            serie_max = [v["cone"][3] for v in etalon.values()]
            print("   ETALON RETAIL (%d niveaux) : mediane %d = %.0f%% du retail ; max %d = %.0f%%"
                  % (len(etalon), med, rang(med, serie_med), mx, rang(mx, serie_max)))
            print("   retail : mediane de %d a %d, max de %d a %d"
                  % (min(serie_med), max(serie_med), min(serie_max), max(serie_max)))

    print("\n-- cache de tuiles du VDP1 (%d slots)" % r["slots"])
    tps = r["tuiles_secteur"]
    if tps:
        pire = max(range(len(tps)), key=lambda i: tps[i])
        print("   tuiles distinctes par secteur : max %d (secteur %d), %d secteurs au-dessus de %d"
              % (tps[pire], pire, r["cache_sature"], r["slots"]))

    if secteurs:
        print("\n-- par secteur (les %d plus chers)" % min(15, len(r["cellules_secteur"])))
        rows = sorted(enumerate(zip(r["cellules_secteur"], r["tuiles_secteur"])),
                      key=lambda t: -t[1][0])[:15]
        print("   %-6s %8s %8s" % ("sect", "cellules", "tuiles"))
        for si, (c, t) in rows:
            print("   %-6d %8d %8d" % (si, c, t))


# ============================================================ 7. CLI
def ecrire_etalon(args):
    """Recalcule tools/lev_etalon.json depuis les .LEV retail.

    Le fichier est du CHIFFRE DERIVE, pas du contenu : refs/ est gitignore mais cette table-la se
    committe, c'est elle qui rend `--etalon` utilisable sans les disques."""
    fichiers = sorted(glob.glob(os.path.join(ROOT, "refs", "extract", "PS", "*.LEV")))
    if not fichiers:
        print("aucun .LEV retail dans refs/extract/PS/", file=sys.stderr)
        return 1
    out = {}
    for c in fichiers:
        modele, _lay, fsize = charger(c)
        Lv = modele["level"]
        st, cel, _ = cout_du_niveau(modele)
        out[os.path.basename(c)[:-4]] = dict(
            secteurs=len(Lv["sectors"]), murs=len(Lv["walls"]), faces=len(Lv["faces"]),
            tuiles=len(modele["tiles"]), cellules=st["cellules_niveau"], positions=st["positions"],
            cone=list(st["cone"]), tour=list(st["tour"]),
            secteur_max=max(cel) if cel else 0,
            residence=residence(modele)["total"], taille_fichier=fsize)
        print("  %s" % os.path.basename(c), flush=True)
    tmp = ETALON + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1, sort_keys=True)
    os.replace(tmp, ETALON)
    print("ecrit %s (%d niveaux)" % (ETALON, len(out)))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("fichiers", nargs="*", help="les .LEV a juger (defaut : refs/extract/PS/*.LEV)")
    ap.add_argument("--jeu", choices=("ps", "doom"), default="ps",
                    help="jeu vise : fixe tileBase (40 / 96) et les slots du cache (28 / 32)")
    ap.add_argument("--tile-base", type=int, default=None, help="force tileBase")
    ap.add_argument("--slots", type=int, default=None, help="force les slots du cache VDP1")
    ap.add_argument("--map", default=None, help="le MAIN.map du build vise, pour le budget RAM")
    ap.add_argument("--secteurs", action="store_true", help="table des secteurs les plus chers")
    ap.add_argument("--etalon", action="store_true", help="situer le niveau dans le retail")
    ap.add_argument("--vite", action="store_true", help="sauter la carte de cout (~2 s par niveau)")
    ap.add_argument("--json", default=None, help="ecrire le rapport complet en JSON")
    ap.add_argument("--ecrire-etalon", action="store_true", help="recalculer tools/lev_etalon.json")
    a = ap.parse_args(argv)

    if a.ecrire_etalon:
        return ecrire_etalon(a)

    fichiers = a.fichiers or sorted(glob.glob(os.path.join(ROOT, "refs", "extract", "PS", "*.LEV")))
    if not fichiers:
        ap.error("aucun fichier ; donner un .LEV ou peupler refs/extract/PS/")
    etalon = charger_etalon() if a.etalon else None
    pool = fin = None
    if a.map:
        pool, fin = pool_depuis_map(a.map)
        if pool is None:
            print("%s : _end introuvable (lire APRES « Linker script and memory map »)" % a.map,
                  file=sys.stderr)

    rapports, fatal = [], 0
    for c in fichiers:
        try:
            r = analyser(c, a)
        except Exception as e:                      # un .LEV illisible est un resultat, pas un crash
            print("=" * 78)
            print("%s -- ILLISIBLE : %s" % (os.path.basename(c), e))
            fatal += 1
            continue
        if pool:
            r["pool"], r["end"] = pool, fin
        imprimer(r, etalon, a.secteurs)
        fatal += len(r["asserts"]) + sum(1 for g, _, _ in r["silencieux"] if g == "FATAL")
        rapports.append(r)

    if a.json:
        tmp = a.json + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(rapports, f, indent=1)
        os.replace(tmp, a.json)
        print("\nJSON ecrit : %s" % a.json)
    return 1 if fatal else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""verif_scene.py -- le constructeur de scene de l'extension, juge SANS Blender.

Toute la logique qui peut etre fausse est dans `io_lev/scene.py` : generation de la grille d'un mur
en parallelogramme, permutation de motif, indexation RELATIVE des sommets de face, pas propre a la
grille de lumiere. Rien de cela n'a besoin de Blender, donc rien n'excuse de ne pas le verifier.

LE CONTROLE QUI VAUT LES AUTRES : le nombre de quads produits par secteur doit valoir exactement
ce que `tools/cout.py:cellules_par_secteur` compte de son cote. Les deux comptent la meme chose --
ce que le peintre doit tracer -- par deux chemins qui ne partagent pas une ligne : cout.py lit les
champs tileLength/tileHeight/firstFace/lastFace, scene.py FABRIQUE les quads un par un. Un ecart
denonce l'un des deux.

Les autres controles : tout quad a 4 index valides et distincts, 4 couples d'UV et 4 lumieres ;
toute tuile citee existe et est de classe geometrie (0x32/0x72, WALLS.C:1981/:2049) ; aucun sommet
hors de la boite du niveau ; les objets poses tombent dans cette boite.

Usage : python tools\\blender\\verif_scene.py [FICHIER.LEV ...]
"""
from __future__ import annotations

import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.join(HERE, "io_lev"))
import cout                                             # noqa: E402
import lev                                              # noqa: E402
import levdata                                          # noqa: E402
import scene as scn                                     # noqa: E402


def verifier(chemin):
    ecarts = []
    donnees = levdata.lire(chemin)
    N = donnees["level"]
    sc = scn.construire(donnees)

    # --- 1. le compte de quads, contre tools/cout.py --------------------------------------
    ref = cout.cellules_par_secteur(N["sectors"], N["walls"])
    mine = scn.cellules_par_secteur(sc, len(N["sectors"]))
    if sum(ref) != sum(mine):
        differents = [(i, a, b) for i, (a, b) in enumerate(zip(ref, mine)) if a != b]
        ecarts.append("quads : %d contre %d pour cout.py ; %d secteurs en ecart, p.ex. %s"
                      % (sum(mine), sum(ref), len(differents),
                         ", ".join("s%d %d/%d" % t for t in differents[:5])))

    # --- 2. integrite des quads ------------------------------------------------------------
    ns = len(sc.sommets)
    mauvais = degen = 0
    for q in sc.quads:
        if len(q) != 4 or any(not (0 <= i < ns) for i in q):
            mauvais += 1
        elif len(set(q)) < 3:
            degen += 1                       # < 3 sommets distincts : pas une surface
    if mauvais:
        ecarts.append("%d quads ont un index de sommet invalide" % mauvais)
    if len(sc.uv) != len(sc.quads) or any(len(u) != 4 for u in sc.uv):
        ecarts.append("les UV ne suivent pas les quads")
    if len(sc.lum) != len(sc.quads) or any(len(l) != 4 for l in sc.lum):
        ecarts.append("les lumieres ne suivent pas les quads")

    # --- 3. les tuiles citees existent et sont de classe geometrie -------------------------
    T = donnees["tiles"]
    hors = sorted({t for t in sc.tuiles if not (0 <= t < len(T))})
    if hors:
        ecarts.append("%d index de tuile hors du jeu de %d : %s"
                      % (len(hors), len(T), hors[:6]))
    else:
        mauvaise_classe = sorted({t for t in set(sc.tuiles)
                                  if T[t]["flags"] not in levdata.GEOMETRIE})
        if mauvaise_classe:
            ecarts.append("%d tuiles de geometrie ne sont pas 0x32/0x72 : %s"
                          % (len(mauvaise_classe),
                             ["#%d=0x%02x" % (t, T[t]["flags"]) for t in mauvaise_classe[:6]]))

    # --- 4. la boite : rien ne doit partir a l'infini ---------------------------------------
    V = N["vertices"]
    if V and sc.sommets:
        bx = (min(v["x"] for v in V), max(v["x"] for v in V))
        by = (min(v["y"] for v in V), max(v["y"] for v in V))
        bz = (min(v["z"] for v in V), max(v["z"] for v in V))
        marge = 2.0                      # la grille pose des coins INTERPOLES, jamais au-dela
        fuyards = sum(1 for p in sc.sommets
                      if not (bx[0] - marge <= p[0] <= bx[1] + marge
                              and by[0] - marge <= p[1] <= by[1] + marge
                              and bz[0] - marge <= p[2] <= bz[1] + marge))
        if fuyards:
            ecarts.append("%d sommets hors de la boite du niveau" % fuyards)

    # --- 5. les lumieres indexent worldGrey sans en sortir -------------------------------------
    # L'assembleur du peintre indexe worldGrey[WORLDGREEN_NM*32] avec l'octet TEL QUEL
    # (wallasm_gnu.s .Lrt_grey, UTIL.C:151) : la seule borne vraie est 0..127. Borner a 31, comme
    # le suggere la mesure retail, refuserait les niveaux Doom du fork, qui utilisent les bandes
    # de brouillard (mesure 23-09 : jusqu'a 112, soit bande 3 niveau 16).
    bandes = set()
    if sc.lum:
        lo = min(min(l) for l in sc.lum)
        hi = max(max(l) for l in sc.lum)
        if lo < 0 or hi > scn.LUM_MAX:
            ecarts.append("octet de lumiere hors de [0, %d] : %d..%d -- worldGrey deborde "
                          "(UTIL.C:151)" % (scn.LUM_MAX, lo, hi))
        bandes = {scn.niveau_et_bande(v)[1] for l in sc.lum for v in l}
    # --- 6. les faces a montrer : triangles, cellules plates, et l'INVARIANT DES COINS -----
    # Ce controle remplace un defaut reel : la couche Blender ecartait toute cellule dont un indice
    # de sommet se repete, en les croyant toutes d'aire nulle. Mesure du 24-09 : sur les 33 fichiers
    # du banc, 28 482 cellules sont dans ce cas et seules 411 sont vraiment plates -- le reste sont
    # des TRIANGLES, et les jeter perdait 11 % des surfaces d'E1M1. On verifie donc ici les deux
    # choses qui peuvent casser, et qui autrement ne se manifesteraient qu'a l'import :
    #   * chaque face gardee a 3 ou 4 coins, tous distincts, et ses coins designent des sommets
    #     distincts -- sinon Blender la retire dans `validate()` et desaligne TOUS les tableaux ;
    #   * le total des coins est ce que les tableaux par coin (UV, lumiere) devront compter.
    for plafonds in (True, False):
        faces, garde, coins, plates, triangles = scn.faces_a_montrer(sc, plafonds)
        if not (len(faces) == len(garde) == len(coins)):
            ecarts.append("faces_a_montrer (plafonds=%s) rend %d faces, %d gardes, %d coins"
                          % (plafonds, len(faces), len(garde), len(coins)))
            break
        if len(faces) + plates + (0 if plafonds else sum(1 for g in sc.genre if g == 2)) \
                != len(sc.quads):
            ecarts.append("faces_a_montrer (plafonds=%s) : %d faces + %d plates ne rendent pas "
                          "les %d cellules" % (plafonds, len(faces), plates, len(sc.quads)))
            break
        mauvaises = 0
        for f, c in zip(faces, coins):
            if len(f) != len(c) or len(f) not in (3, 4) or len(set(f)) != len(f):
                mauvaises += 1
        if mauvaises:
            ecarts.append("%d faces dont les coins ne sont pas 3 ou 4 sommets distincts"
                          % mauvaises)
            break
        if plafonds:
            n_tri, n_plat = triangles, plates

    return ecarts, dict(quads=len(sc.quads), sommets=ns, degen=degen,
                        triangles=n_tri, plates=n_plat,
                        portails=len(sc.portails), objets=len(sc.objets),
                        muets=sc.objets_muets, tuiles=len(set(sc.tuiles)),
                        bandes=sorted(bandes))


def main(argv=None):
    args = list(argv if argv is not None else sys.argv[1:])
    if args:
        fichiers = args
    else:
        fichiers = []
        for motif in ("refs/extract/PS/*.LEV", "cd_doom/*.LEV", "build/doom2ps/*.LEV"):
            fichiers += sorted(glob.glob(os.path.join(ROOT, motif)))
    if not fichiers:
        print("aucun .LEV a verifier", file=sys.stderr)
        return 1
    faux = 0
    for c in fichiers:
        try:
            ecarts, st = verifier(c)
        except Exception as e:
            print("  FAUX  %-22s exception : %s" % (os.path.basename(c), e))
            faux += 1
            continue
        if ecarts:
            faux += 1
            print("  FAUX  %-22s %d ecart(s)" % (os.path.basename(c), len(ecarts)))
            for e in ecarts[:6]:
                print("        %s" % e)
        else:
            # Les triangles et les cellules plates sont IMPRIMES, pas seulement comptes : c'est en
            # les croyant tous plats que le premier jet de la couche Blender les a jetes.
            print("  ok    %-22s %6d quads (%d triangles, %d plates), %6d sommets, %3d tuiles, "
                  "%4d portails, %3d objets (%d muets), bandes %s%s"
                  % (os.path.basename(c), st["quads"], st["triangles"], st["plates"],
                     st["sommets"], st["tuiles"], st["portails"], st["objets"], st["muets"],
                     "".join(str(b) for b in st["bandes"]) or "-",
                     "" if not st["degen"] else ", %d quads degeneres" % st["degen"]))
    print("\n%d fichier(s), %d en defaut" % (len(fichiers), faux))
    return 1 if faux else 0


if __name__ == "__main__":
    sys.exit(main())

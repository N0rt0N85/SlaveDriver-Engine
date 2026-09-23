#!/usr/bin/env python3
"""verif_io_lev.py -- le garde-fou de la deuxieme lecture du .LEV.

L'extension Blender embarque son propre lecteur (`io_lev/levdata.py`) parce qu'elle doit
s'installer seule, sans le depot. Une deuxieme lecture non gardee derive : celle-ci est confrontee
a `tools/lev.py`, qui fait foi, sur TOUS les .LEV qu'on trouve -- champ par champ.

Ce qui est compare : les 14 comptes d'en-tete, chaque champ de chaque secteur, mur, sommet, face
et objet, les trois tableaux d'octets (objectParams, texture, vertexLight), le nombre et les
drapeaux des tuiles, le nombre de palettes et le numero de palette objet. Plus un controle que
`lev.py` ne fait pas et qui est le point faible de tout lecteur de tuiles : le flux RLE de CHAQUE
tuile est deroule, et on exige qu'il rende exactement w x h pixels en consommant exactement ses
octets.

⚠ UNE DIFFERENCE EST VOULUE et le verificateur la connait : `levdata` force a 0xffff l'entree 255
de la palette OBJET, parce que le chargeur le fait en RAM avant la copie en CRAM (PIC.C:1147) et
que le disque ne la porte presque jamais ainsi. C'est une correction, pas un ecart.

Usage : python tools\\blender\\verif_io_lev.py [FICHIER.LEV ...]
Sans argument : refs/extract/PS/*.LEV + cd_doom/*.LEV + build/doom2ps/*.LEV. Retour 1 au moindre
ecart.
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
import lev                                              # noqa: E402  la reference
import levdata                                          # noqa: E402  la lecture de l'extension

CHAMPS_SECTEUR = ("object", "center", "floorLevel", "firstWall", "lastWall", "light", "flags",
                  "cutIndex", "cutChannel", "rejectClass")
CHAMPS_MUR = ("normal", "d", "object", "flags", "textures", "firstFace", "lastFace", "firstVertex",
              "lastVertex", "v", "nextSector", "firstLight", "pixelLength", "tileLength",
              "tileHeight")
CHAMPS_SOMMET = ("x", "y", "z", "light")
CHAMPS_FACE = ("v", "tile")
CHAMPS_OBJET = ("type", "firstParam")


def _comparer_listes(nom, a, b, champs, ecarts, cap=3):
    if len(a) != len(b):
        ecarts.append("%s : %d contre %d" % (nom, len(a), len(b)))
        return
    vus = 0
    for i, (x, y) in enumerate(zip(a, b)):
        for c in champs:
            if x[c] != y[c]:
                ecarts.append("%s[%d].%s : %r contre %r" % (nom, i, c, x[c], y[c]))
                vus += 1
                if vus >= cap:
                    return


def verifier(chemin):
    ecarts = []
    ref = lev.parse_lev(chemin)
    mine = levdata.lire(chemin)
    R, M = ref["level"], mine["level"]

    if R["size"] != M["size"]:
        ecarts.append("taille du bloc niveau : %d contre %d" % (R["size"], M["size"]))
    _comparer_listes("secteur", R["sectors"], M["sectors"], CHAMPS_SECTEUR, ecarts)
    _comparer_listes("mur", R["walls"], M["walls"], CHAMPS_MUR, ecarts)
    _comparer_listes("sommet", R["vertices"], M["vertices"], CHAMPS_SOMMET, ecarts)
    _comparer_listes("face", R["faces"], M["faces"], CHAMPS_FACE, ecarts)
    _comparer_listes("objet", R["objects"], M["objects"], CHAMPS_OBJET, ecarts)
    for nom in ("objectParams", "texture", "vertexLight"):
        if bytes(R[nom]) != bytes(M[nom]):
            ecarts.append("%s : %d octets contre %d, contenu different"
                          % (nom, len(R[nom]), len(M[nom])))

    # --- tuiles : meme nombre, memes drapeaux, dans le meme ordre -------------------------
    kinds = {"16bpp64": levdata.TF_16_64, "vdp2": levdata.TF_VDP2, "8rle64": levdata.TF_8RLE_64,
             "8rle32": levdata.TF_8RLE_32, "16bpp32": levdata.TF_16_32,
             "16rle64": levdata.TF_16RLE_64}
    tref = ref["tiles"]["tiles"]
    if len(tref) != len(mine["tiles"]):
        ecarts.append("tuiles : %d contre %d" % (len(tref), len(mine["tiles"])))
    else:
        for i, ((_off, k), t) in enumerate(zip(tref, mine["tiles"])):
            if kinds[k] != t["flags"]:
                ecarts.append("tuile[%d] : %s contre 0x%02x" % (i, k, t["flags"]))
                break
    npal_ref = (ref["tiles"]["palette_size"] - 2) // 512
    if npal_ref != len(mine["palettes"]):
        ecarts.append("palettes : %d contre %d" % (npal_ref, len(mine["palettes"])))

    # --- le flux de pixels de CHAQUE tuile se deroule et rend la bonne taille --------------
    for i, t in enumerate(mine["tiles"]):
        if t["flags"] == levdata.TF_VDP2:
            continue
        try:
            px = levdata.indices(t)
        except Exception as e:
            ecarts.append("tuile[%d] 0x%02x indechiffrable : %s" % (i, t["flags"], e))
            break
        if len(px) != t["w"] * t["h"]:
            ecarts.append("tuile[%d] rend %d pixels au lieu de %d"
                          % (i, len(px), t["w"] * t["h"]))
            break
        # ⚠ `max(px) >= 256` ETAIT ICI, ET NE POUVAIT PAS TIRER : `px` est un `bytes`, donc chacun
        # de ses elements vaut 0..255 par construction du type. Un controle qui ne peut pas se
        # declencher a exactement la meme sortie qu'un controle qui passe, et fait croire que la
        # palette a ete verifiee. (Trouve le 24-09 par relecture contradictoire.)
        # Ce qui suit, LUI, peut tirer : le flux RLE doit etre consomme entierement, au bourrage de
        # mot pres. Un flux tronque rendrait moins de pixels (deja vu au-dessus) mais un flux TROP
        # LONG -- un convertisseur qui ecrit un bloc de trop -- passait jusqu'ici inapercu.
        if "rle" in t:
            lu = _consomme_rle(t["rle"], t["w"] * t["h"])
            if lu is None:
                ecarts.append("tuile[%d] : le flux RLE se termine avant les %d pixels"
                              % (i, t["w"] * t["h"]))
                break
            if not (0 <= len(t["rle"]) - lu <= 3):
                ecarts.append("tuile[%d] : %d octets de flux RLE pour %d consommes"
                              % (i, len(t["rle"]), lu))
                break
    return ecarts, dict(secteurs=len(M["sectors"]), murs=len(M["walls"]),
                        faces=len(M["faces"]), tuiles=len(mine["tiles"]),
                        palettes=len(mine["palettes"]))


def _consomme_rle(data, nm_pixels):
    """Combien d'octets le decodeur RLE consomme pour rendre nm_pixels, ou None s'il en manque.

    Deuxieme ecriture de levdata.unrle, volontairement independante, et qui rend EN PLUS le
    curseur -- ce que l'originale ne fait pas. Sans ce curseur on ne peut dire que « le flux rend
    assez de pixels », jamais « le flux fait la bonne longueur »."""
    out = i = 0
    n = len(data)
    while out < nm_pixels:
        if i + 1 >= n:
            return None
        z, lit = data[i], data[i + 1]
        i += 2
        if i + lit > n:
            return None
        out += z + lit
        i += lit
    return i


def main(argv=None):
    args = list(argv if argv is not None else sys.argv[1:])
    if args:
        fichiers = args
    else:
        fichiers = []
        for motif in ("refs/extract/PS/*.LEV", "cd_doom/*.LEV", "build/doom2ps/*.LEV",
                      "build/ndebug/stext/doom/iso/*.LEV"):
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
            print("  ok    %-22s %d secteurs, %d murs, %d faces, %d tuiles, %d palettes"
                  % (os.path.basename(c), st["secteurs"], st["murs"], st["faces"],
                     st["tuiles"], st["palettes"]))
    print("\n%d fichier(s), %d en ecart avec tools/lev.py" % (len(fichiers), faux))
    return 1 if faux else 0


if __name__ == "__main__":
    sys.exit(main())

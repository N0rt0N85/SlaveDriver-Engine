#!/usr/bin/env python3
"""assemble.py -- etape E5 (version « boite grise ») du convertisseur Duke PC -> .LEV PowerSlave.

But : rendre le niveau TESTABLE sur console AVANT d'avoir fait E4 (extraction des tuiles Duke).
On prend un niveau retail comme DONNEUR et on ne remplace que son bloc geometrie par le notre ;
ses tuiles, sa palette, son ciel, ses sons et ses sequences sont gardes tels quels. Nos 64 picnums
Build sont simplement repartis sur les tuiles 64x64 16 bpp du donneur. Le resultat n'a pas les
textures de Duke, mais il a la GEOMETRIE de Duke, ce que le jalon M1 demande de juger.

Faits utilises (relus dans le moteur, pas devines) :
  - `level_texture[i] += tileBase` pour i IMPAIR et `level_face[i].tile += tileBase` (LEVEL.C:70-72) :
    les indices de tuile sont locaux au niveau, donc reutiliser ceux du donneur est legitime, et la
    disposition [motif, tuile] par cellule est confirmee par ce `i+=2`.
  - un objet `OT_PLAYER` (type 13, SLEVEL.H:22-27) consomme 5 shorts gros-boutistes :
    secteur, x, y, z, angle (OBJECT.C:201-204 puis suckSpriteParams, OBJECT.C:183-194).
  - `assert(level_object[o].firstParam == objectPPos)` (OBJECT.C:211) : les `firstParam` doivent etre
    les offsets cumules dans objectParams.
  - les faces doivent pointer une tuile 16 bpp (`assert(getPicClass(...)==TILE16BPP)`, WALLS.C:1235).
  - taille du bloc niveau < 900 000 (LEVEL.C:40-41).

Usage : python tools\\duke2ps\\assemble.py [--geom J] [--donneur F] [--out F]
"""
from __future__ import annotations

import argparse
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import lev_io
import lev_write

GEOM_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_geom3d.json")
TILES_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e1l1_tiles.json")
# Le donneur fournit les TUILES, pas le creneau : le fichier s'appelle TOMB.LEV quoi qu'il arrive.
# KILENTRY est le donneur le plus leger du jeu (306 251 o de tuiles, demande totale 497 290) ; TOMB en
# a 826 447 et sa demande passait a 1 437 656 avec notre geometrie, soit 93 883 o de PLUS que SHRINE,
# le niveau retail le plus serre -> mem_malloc rendait NULL (assert UTIL.C:392, ecran noir dore).
DONOR_DEFAULT = os.path.join(ROOT, "cd", "KILENTRY.LEV")
DEMANDE_MAX = 1250000      # cible du plan ; plafond dur mesure 1 343 769 (SHRINE)
OUT_DEFAULT = os.path.join(ROOT, "build", "duke2ps", "e5", "TOMB.LEV")

OT_PLAYER = 13                 # SLEVEL.H:22-27, 14e entree de l'enum
TILEFLAG_64x64 = 0x02
TILEFLAG_16BPP = 0x10
TILEFLAG_PALLETE = 0x20
TILE_MUR = TILEFLAG_64x64 | TILEFLAG_16BPP | TILEFLAG_PALLETE   # 0x32 : la seule classe utilisee
                                                               # par la geometrie des 23 niveaux


# La liste EXACTE de PIC.C:130-136 (markAnimTiles).
ANIM_OBJETS = """OT_ANIM_CHAOS1 OT_ANIM_CHAOS2 OT_ANIM_CHAOS3 OT_ANIM_LAVA1 OT_ANIM_LAVA2
OT_ANIM_LAVA3 OT_ANIM_LAVAFALL OT_ANIM_LAVAPO1 OT_ANIM_LAVAPO2 OT_ANIM_TELEP1 OT_ANIM_TELEP2
OT_ANIM_TELEP3 OT_ANIM_TELEP4 OT_ANIM_TELEP5 OT_ANIM_LAVAHEAD OT_ANIM_FORCEFIELD OT_ANIM_WSAND
OT_ANIM_WBRICK OT_ANIM_SWAMP OT_ANM1 OT_ANM2 OT_ANM3 OT_ANM4 OT_ANM5 OT_ANM6 OT_ANM7 OT_ANM8
OT_ANM9 OT_ANM10 OT_ANM11 OT_ANM12""".split()


def types_objets():
    """L'enum OT_* de SLEVEL.H, lu a la source : {nom: valeur}."""
    import re
    with open(os.path.join(ROOT, "SLEVEL.H"), encoding="latin-1") as f:
        src = f.read()
    i = src.index("OT_ANIM_CHAOS1")
    blk = src[src.rindex("enum", 0, i):src.index("}", i)].split("{", 1)[1]
    noms = [n.strip() for n in re.split("[,;" + chr(10) + "]", blk)]
    noms = [n for n in noms if re.fullmatch(r"OT_[A-Z0-9_]+", n)]
    return {n: k for k, n in enumerate(noms)}


def neutralise_animations(model):
    """Coupe les animations de tuiles heritees du donneur. INDISPENSABLE.

    markAnimTiles (PIC.C:129-160) parcourt les sequences des objets OT_ANIM_* et estampille
    PICFLAG_ANIM sur `pics[tile]`, ou `tile` est l'index de tuile BRUT du chunk. Ensuite mapPic
    (PIC.C:440-442) DETOURNE toute tuile ainsi marquee vers level_chunk[animTileChunk[...]].tile,
    qui avance toutes les deux frames (advanceWallAnimations).

    Le piege : seuls level_texture et level_face[].tile recoivent `+= tileBase` (LEVEL.C:69-72),
    PAS les chunks. Chez le donneur KILENTRY l'estampille tombe sur les pics 6..33, sous
    tileBase = 40, donc inoffensive. En mettant nos 64 tuiles Duke EN TETE, les chunks du donneur
    glissent de 6..33 vers 64..75 -- exactement dans notre plage de geometrie (pics 40..103).
    MESURE : 12 de nos textures de mur etaient marquees ANIM et permutaient entre elles toutes les
    deux frames, ce qui garantit un defaut de cache permanent (le cache ne tient que 28 tuiles) et
    donc un appel a map() -- 8 244 octets de pile -- au fond d'une chaine de rendu qui en consomme
    deja 15,4 Ko sur les 20,4 Ko de mystack[] (UTIL.C:28).

    On ne perd rien : assemble.py remplace la liste d'objets par un unique OT_PLAYER, donc aucun
    objet OT_ANIM_* n'existe dans le niveau et aucune de ces sequences n'est jouee."""
    OT = types_objets()
    smap = model["sequences"]["sequenceMap"]
    coupes = []
    for nom in ANIM_OBJETS:
        i = OT.get(nom)
        if i is not None and i < len(smap) and smap[i] >= 0:
            smap[i] = -1
            coupes.append(nom)
    return coupes


def tile_size(t):
    """Octets qu'une tuile occupe dans le fichier : palNm + pixels, ou palNm + taille + RLE."""
    return 2 + (len(t["pixels"]) if "pixels" in t else 2 + len(t["rle"]))


def donor_wall_tiles(model):
    """Indices des tuiles du donneur utilisables sur une face.

    MESURE sur les 23 niveaux retail : la geometrie n'utilise QUE des tuiles de flags exactement
    0x32 (64x64 | 16BPP | PALLETE), et toujours un prefixe contigu a partir de 0. Filtrer seulement
    sur « 64x64 et 16 bpp » laisse passer les 0x72, les MEMES en RLE -- ce sont des sprites
    d'objets, et les peindre sur un mur donne des colonnes de figures repetees (vu en console)."""
    out = [i for i, t in enumerate(model["tiles"]) if t["flags"] == TILE_MUR]
    if not out:
        raise SystemExit("le donneur n'a aucune tuile 64x64 16 bpp")
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--geom", default=GEOM_DEFAULT)
    ap.add_argument("--donneur", default=DONOR_DEFAULT)
    ap.add_argument("--out", default=OUT_DEFAULT)
    ap.add_argument("--angle", type=int, default=0, help="angle de depart (unites de 5760)")
    ap.add_argument("--tuiles", default=TILES_DEFAULT,
                    help="tuiles Duke (E4) ; --tuiles \"\" pour la boite grise du donneur")
    a = ap.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")

    G = json.load(open(a.geom, encoding="utf-8"))
    model, _ = lev_io.read_model(a.donneur)
    print(f"E5 boite grise : geometrie {os.path.basename(a.geom)} dans le donneur "
          f"{os.path.basename(a.donneur)}")

    # -- tuiles ------------------------------------------------------------------------------
    if a.tuiles and os.path.exists(a.tuiles):
        # E4 : les VRAIES textures de Duke. Nos tuiles passent en TETE (indices 0..N-1, comme la
        # geometrie retail qui n'utilise qu'un prefixe a partir de 0) et les tuiles du donneur sont
        # decalees de +N. Il faut alors corriger les indices de tuile des SEQUENCES du donneur
        # (arme, HUD, effets), seuls autres consommateurs du tableau.
        T = json.load(open(a.tuiles, encoding="utf-8"))
        duke = [bytes.fromhex(h) for h in T["tiles"]]
        N = len(duke)

        # Elagage : la geometrie du donneur a disparu, remplacee par la notre, donc ses tuiles de
        # MUR ne servent plus a personne. On ne garde que celles qu'une sequence reference encore.
        # MESURE sur KILENTRY : 22 tuiles 0x32 orphelines, 90 156 octets -- de quoi financer
        # 22 sous-tuiles de decoupage E4.1, qui valent nettement mieux.
        vivantes = {c["tile"] for c in model["sequences"]["chunks"]}
        garde = [i for i in range(len(model["tiles"])) if i in vivantes]
        gagne = sum(tile_size(model["tiles"][i])
                    for i in range(len(model["tiles"])) if i not in vivantes)
        donneur = {old: k for k, old in enumerate(garde)}
        model["tiles"] = [model["tiles"][i] for i in garde]

        pal_index = len(model["palettes"]["palettes"])
        model["palettes"]["palettes"].append(list(T["palette"]))
        model["tiles"] = ([dict(flags=TILE_MUR, palNm=pal_index, pixels=px) for px in duke]
                          + model["tiles"])
        for c in model["sequences"]["chunks"]:
            c["tile"] = donneur[c["tile"]] + N
        coupes = neutralise_animations(model)
        remap = {i: i for i in range(len(G["tiles"]))}
        if coupes:
            print(f"  E4 : {len(coupes)} animations du donneur neutralisees ({', '.join(coupes)}) "
                  f"-- sinon markAnimTiles detourne nos textures de mur (PIC.C:129-160)")
        print(f"  E4 : {N} textures Duke en tete, palette Duke en position {pal_index}, "
              f"{len(model['sequences']['chunks'])} morceaux de sequence recales ; "
              f"{len(garde)} tuiles du donneur gardees, "
              f"{gagne:,} o recuperes sur des tuiles orphelines")
    else:
        pool = donor_wall_tiles(model)
        remap = {i: pool[i % len(pool)] for i in range(len(G["tiles"]))}
        print(f"  boite grise : {len(G['tiles'])} picnums Build -> {len(pool)} tuiles du donneur")

    faces = [dict(v=list(f["v"]), tile=remap[f["tile"]], pad=0) for f in G["faces"]]
    texture = list(G["texture"])
    for i in range(1, len(texture), 2):                 # LEVEL.C:70 : un octet sur deux est la tuile
        texture[i] = remap[texture[i]]

    # -- depart du joueur ---------------------------------------------------------------------
    dep = G["criteres"]["depart_present"]["depart"]
    sect = dep.get("sector_gen1")
    if sect is None:
        # le morceau qui contient le point de depart : on le retrouve par le secteur du .LEV dont le
        # centre est le plus proche, faute de champ dedie dans le modele d'entree.
        bx, by = dep["build_xy"]
        px, pz = bx // 8, -(by // 8)
        sect = min(range(len(G["sectors"])),
                   key=lambda i: (G["sectors"][i]["center"][0] - px) ** 2
                   + (G["sectors"][i]["center"][2] - pz) ** 2)
    s = G["sectors"][sect]
    # MESURE : dans les 23 niveaux retail, le y de l'objet OT_PLAYER vaut EXACTEMENT le floorLevel
    # de son secteur (ecart 0 partout). Le « sol + 48 » du plan M1 est faux ; c'est le moteur qui
    # releve la camera (elle flotte a R+8 au-dessus du contact, SPRITE.C:642-655).
    py = s["floorLevel"]
    bx, by = dep["build_xy"]
    px, pz = bx // 8, -(by // 8)
    params = struct.pack(">5h", sect, px, py, pz, a.angle)
    print(f"  depart : secteur {sect}, X {px}, Y {py}, Z {pz} (sol {s['floorLevel']})")

    model["level"] = dict(
        sectors=[dict(s) for s in G["sectors"]],
        walls=[dict(w) for w in G["walls"]],
        vertices=[dict(v) for v in G["vertices"]],
        faces=faces,
        objects=[dict(type=OT_PLAYER, firstParam=0)],
        objectParams=list(params),
        pushBlocks=[], PBVert=[], waveVert=[], waveFace=[], PBWall=[],
        texture=texture, vertexLight=list(G["vertexLight"]),
        # Plans de coupe : le convertisseur Doom s'en sert pour rendre au peintre l'ordre que la
        # fusion des feuilles lui prend (doom3d.plans_de_coupe). Duke n'en emet pas.
        cutPlane=G.get("cutPlane") or [],
    )

    data = lev_write.write_lev(model, strict=False)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    lev_write.atomic_write(a.out, data)
    # le bloc ciel precede le bloc niveau : la taille ne se lit pas en tete de fichier
    lvl = lev_write.serialize_level(model["level"])
    size = struct.unpack(">i", lvl[:4])[0]
    print(f"  -> {a.out}  fichier {len(data):,} o, bloc niveau {size:,} o "
          f"(limite 900 000, LEVEL.C:41), sha1 {lev_write.sha1(data)[:12]}")
    if not (0 < size < 900000):
        print("  ECHEC : le bloc niveau ne tient pas dans la limite du chargeur")

    # DEMANDE memoire = bloc niveau + palettes + tuiles + sequences. C'est CE budget qui a manque au
    # premier disque, pas la taille du bloc : le chargeur alloue part par part (LEVEL.C, LOADPART) et
    # `mem_malloc` fait `assert(r)` quand il ne reste plus rien (UTIL.C:392).
    lay = lev_io.layout(data, os.path.basename(a.out))
    pal = lay["tiles"]["palette_size"]
    til = lay["tiles"]["end"] - lay["tiles"]["tileset_off"]
    seq = lay["sequences"]["size"]
    dem = size + pal + til + seq
    ok = dem <= DEMANDE_MAX
    print(f"  demande memoire : niveau {size:,} + palettes {pal:,} + tuiles {til:,} + sequences "
          f"{seq:,} = {dem:,}  ({'OK' if ok else 'ECHEC'}, cible {DEMANDE_MAX:,}, "
          f"plafond retail mesure 1 343 769 = SHRINE)")
    if not ok:
        print("  ECHEC : prendre un donneur plus leger (KILENTRY) ou alleger la geometrie")

    # relecture : le fichier doit se re-analyser et redonner exactement le meme modele
    back, lay = lev_io.model_from_bytes(data, os.path.basename(a.out))
    d = lev_io.diff(model, back)
    print(f"  relecture : {'identique' if not d else str(len(d)) + ' differences'} "
          f"{'; '.join(d[:3])}")

    probs = lev_write.engine_problems(model)
    if probs:
        print("  PROBLEMES signales par lev_write :")
        for p in probs:
            print("   ", p)
    else:
        print("  lev_write : aucun probleme signale")
    return 1 if probs else 0


if __name__ == "__main__":
    sys.exit(main())

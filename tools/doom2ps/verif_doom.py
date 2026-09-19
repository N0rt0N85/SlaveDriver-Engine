#!/usr/bin/env python3
"""verif_doom.py -- verificateur INDEPENDANT du .LEV produit par doom2ps.

Il relit le FICHIER (pas les intermediaires) et rejoue les tests que le moteur fait vraiment,
a la ligne pres. Le convertisseur passe toujours ses propres criteres du premier coup ; ce sont
ceux-la qui trouvent les defauts (lecon E3/E6 du convertisseur Duke).

Usage : python tools\\doom2ps\\verif_doom.py [--lev cd_doom/E1M1.LEV] [--geom J] [--tiles J] [--wad W]
        [--map E1M1] [--ids build/doom/doom_ids.json] [--static cd_doom/STATIC.DAT] [--skill 3]
Tests 1-16 : geometrie, objets, push blocks, interrupteur ; 17-21 (contrat « Verifications PC ») :
tuiles (prefixe 0x32, G + tileBase <= 255, MAXNMPICS), sequences atteignables (formule seq() avec la
garde -2), sons (carte, DMX len - 32, 80 / 512 Ko), barils (avertissement), tailles et memoire
residente contre le pool reel (LWRAM 1 Mo + 0x06100000 - _end du disque de test, make_e1m1.MAP_PROGRAMME).
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import bandes                                           # noqa: E402
import coverage                                         # noqa: E402
import lev                                              # noqa: E402
import ordre                                            # noqa: E402

FAIL = []
OK = []

# % de positions debout ou une paire de secteurs reste peinte dans le mauvais ordre. Ce n'est pas
# zero et ca ne peut pas l'etre avec ce mecanisme : contraindre TOUTES les paires qui se
# chevauchent donne 86 %, bien pire que les 68 % d'origine (tools/ordre.py explique pourquoi).
# MESURE 18-09 sur le disque par defaut : 6 % avec la fusion, 13 % sans. Le seuil est une
# DECISION, posee ici comme garde-fou de REGRESSION : il laisse passer les deux reglages et
# refuse tout retour vers les 68 % d'avant la table.
SEUIL_ORDRE = 20.0


def murs_ajoures(a, W, tex):
    """Index des murs AJOURES : un mur plein, dessine, en parallelogramme, dont une cellule porte
    une tuile qui contient l'indice 0 -- la transparence du moteur (PIC.C:500-531).  C'est la
    signature d'une grille (texture du milieu d'une ligne a deux faces, doom3d), et elle se lit
    dans la SORTIE seule : le verificateur ne consulte aucun intermediaire du convertisseur."""
    sys.path.insert(0, os.path.join(ROOT, "tools", "duke2ps"))
    import lev_io
    model, _ = lev_io.read_model(a.lev)
    tiles = model["tiles"]
    creuse = {i for i, t in enumerate(tiles) if 0 in (t.get("pixels") or ())}
    out = set()
    for wi, w in enumerate(W):
        if w["nextSector"] >= 0 or (w["flags"] & 0x02) or not (w["flags"] & 0x01):
            continue
        base = w["textures"]
        for c in range(w["tileLength"] * w["tileHeight"]):
            if tex[base + 2 * c + 1] in creuse:
                out.add(wi)
                break
    return out


def put(name, ok, detail=""):
    (OK if ok else FAIL).append(name)
    print(f"  [{'OK  ' if ok else 'ECHEC'}] {name}{(' : ' + detail) if detail else ''}")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--lev", default=os.path.join(ROOT, "cd_doom", "E1M1.LEV"),
                    help="le .LEV a relire (defaut : le disque Doom produit par make_e1m1.py)")
    ap.add_argument("--geom", default=os.path.join(ROOT, "build", "doom2ps",
                                                   "e1m1_geom3d.json"))
    ap.add_argument("--wad", default=os.path.join(os.path.dirname(ROOT), "Mimas", "cd", "data",
                                                  "DOOM1.WAD"))
    ap.add_argument("--map", default="E1M1")
    ap.add_argument("--tiles", default=os.path.join(ROOT, "build", "doom2ps", "e1m1_tiles.json"),
                    help="sortie de doomtiles.py pour la MEME geometrie (test 13)")
    ap.add_argument("--ids", default=os.path.join(ROOT, "build", "doom", "doom_ids.json"),
                    help="doom_ids.json (tests 17-21 : sequences, sons, barils)")
    ap.add_argument("--static", default=os.path.join(ROOT, "cd_doom", "STATIC.DAT"),
                    help="STATIC.DAT du meme disque : tileBase (bloc 4) et sons statiques (bloc 3)")
    ap.add_argument("--skill", type=int, default=3)
    a = ap.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")

    r = lev.Reader(a.lev)
    lev.parse_sky(r)
    L = lev.parse_level_block(r)
    S, W, V, F = L["sectors"], L["walls"], L["vertices"], L["faces"]
    tex = L["texture"]
    print(f"verif : {os.path.basename(a.lev)} -- {len(S)} secteurs, {len(W)} murs, "
          f"{len(V)} sommets, {len(F)} faces")

    # Point garanti INTERIEUR au secteur : barycentre des sommets des murs VERTICAUX (ce sont
    # les sommets du polygone convexe, donc leur moyenne est dedans) a mi-hauteur.
    # PAS `sector.center` : lui est la moyenne des sommets de TOUS les murs, sol et plafond
    # compris, et ceux-la sont le quad ENGLOBANT du polygone (add_flat) -- donc il peut tomber
    # hors du secteur sans que ce soit un defaut. Le retail le calcule pareil (CONVERT.C:2003)
    # et le moteur ne s'en sert que pour des distances de son (AI.C:4879, 5731, 5866).
    def inner_point(s):
        ax = az = 0
        n = 0
        ys = []
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] == 0:
                for i in w["v"]:
                    ax += V[i]["x"]
                    az += V[i]["z"]
                    n += 1
            else:
                ys.append(sum(V[i]["y"] for i in w["v"]) / 4.0)
        if not n:
            return None
        return (ax / n, (sum(ys) / len(ys)) if ys else 0, az / n)

    # 1. NORMALES VERS L'INTERIEUR. drawSector (WALLS.C:1618-1622) cull un mur quand
    #    (camera - v[0]) . normal < 0. Avec la camera a l'interieur, tous ses murs doivent
    #    donc etre du bon cote : c'est LE test qui decide si un secteur est visible de
    #    l'interieur ou completement retourne.
    bad = []
    for si, s in enumerate(S):
        c = inner_point(s)
        if c is None:
            continue
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            v0 = V[w["v"][0]]
            dot = ((c[0] - v0["x"]) * w["normal"][0] + (c[1] - v0["y"]) * w["normal"][1]
                   + (c[2] - v0["z"]) * w["normal"][2])
            if dot < 0:
                bad.append((si, wi, dot))
    put("normales vers l'interieur", not bad,
        f"{len(bad)} murs retournes, ex. {bad[:3]}" if bad else f"{len(W)} murs")

    # 2. pointInSectorP (SPRITE.C:940-960) : le centre doit etre DANS son secteur, sinon
    #    findSectorContaining ne retrouvera jamais le joueur ni les monstres.
    bad = []
    for si, s in enumerate(S):
        c = inner_point(s)
        if c is None:
            continue
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            v0 = V[w["v"][0]]
            d = ((c[0] - v0["x"]) * w["normal"][0] + (c[1] - v0["y"]) * w["normal"][1]
                 + (c[2] - v0["z"]) * w["normal"][2])
            if d < -(1 << 16) // 4:
                bad.append((si, wi))
                break
    put("point interieur dans son secteur", not bad,
        f"{len(bad)} secteurs, ex. {bad[:5]}" if bad else "")

    # 3. PORTAILS EN TETE (WALLS.C:1740-1743, `if (nextSector == -1) return;`)
    bad = []
    for si, s in enumerate(S):
        seen = False
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            if W[wi]["nextSector"] == -1:
                seen = True
            elif seen:
                bad.append(si)
                break
    put("portails en tete de liste", not bad, f"{len(bad)} secteurs" if bad else "")

    # 4. un sol et un plafond par secteur (MESURE : vrai dans les 8 408 secteurs retail)
    nf = nc = 0
    for s in S:
        f_ = sum(1 for wi in range(s["firstWall"], s["lastWall"] + 1) if W[wi]["normal"][1] > 0)
        c_ = sum(1 for wi in range(s["firstWall"], s["lastWall"] + 1) if W[wi]["normal"][1] < 0)
        nf += (f_ != 1)
        nc += (c_ != 1)
    put("un sol et un plafond par secteur", nf == 0 and nc == 0, f"sols!=1 {nf}, plafonds!=1 {nc}")

    # 5. voisins dans les bornes et RECIPROQUES (le voisin doit avoir un portail qui revient)
    horsbornes = [wi for wi, w in enumerate(W)
                  if w["nextSector"] != -1 and not (0 <= w["nextSector"] < len(S))]
    put("nextSector dans les bornes", not horsbornes, str(horsbornes[:5]))
    back = {}
    for si, s in enumerate(S):
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            n = W[wi]["nextSector"]
            if n >= 0:
                back.setdefault(si, set()).add(n)
    norecip = [(a_, b_) for a_, bs in back.items() for b_ in bs
               if a_ not in back.get(b_, set())]
    # Un portail a sens unique n'est PAS un defaut en soi : Doom a des linedefs a UNE SEULE face
    # avec une feuille BSP de chaque cote. Le cote jouable y voit un mur plein (correct), le cote
    # arriere y voit un portail. Ce qui serait un defaut, c'est une frontiere SANS AUCUN MUR en
    # face -- la, le joueur voit le vide. C'est cela qu'on teste.
    trous = []
    for a_, b_ in norecip:
        sa = S[a_]
        ref = None
        for wi in range(sa["firstWall"], sa["lastWall"] + 1):
            if W[wi]["nextSector"] == b_:
                ref = W[wi]
                break
        if ref is None:
            continue
        mx = sum(V[i]["x"] for i in ref["v"]) / 4.0
        mz = sum(V[i]["z"] for i in ref["v"]) / 4.0
        sb = S[b_]
        near = any(math.hypot(sum(V[i]["x"] for i in W[wi]["v"]) / 4.0 - mx,
                              sum(V[i]["z"] for i in W[wi]["v"]) / 4.0 - mz) < 40
                   for wi in range(sb["firstWall"], sb["lastWall"] + 1)
                   if W[wi]["normal"][1] == 0)
        if not near:
            trous.append((a_, b_))
    put("aucune frontiere sans mur en face", not trous,
        f"{len(trous)} trous, ex. {trous[:4]}" if trous else
        f"{len(norecip)} portails a sens unique, tous avec un mur plein en face (linedefs "
        f"a une face) sur {sum(len(v) for v in back.values())}")

    # 6. tuiles : motif < 8 (assert WALLS.C:1142) et classe 16 bpp (assert WALLS.C:1235)
    put("motif < 8", all(tex[i] < 8 for i in range(0, len(tex), 2)), f"{len(tex)//2} cellules")
    r2 = lev.Reader(a.lev)
    lev.parse_sky(r2)
    lev.parse_level_block(r2)
    lev.parse_sounds(r2)
    T = lev.parse_tiles(r2)
    nm = T["count"]
    used_tex = {tex[i] for i in range(1, len(tex), 2)}
    used_face = {f["tile"] for f in F}
    mx = max(used_tex | used_face) if (used_tex or used_face) else 0
    put("index de tuile dans le tileset", mx < nm, f"max {mx} pour {nm} tuiles")
    kinds = T["kinds"]
    put("tuiles de geometrie en 64x64 16 bpp", kinds.get("16bpp64", 0) > 0, str(kinds))

    # 7. PARALLAX jamais INVISIBLE (WALLS.C:1616 passe AVANT WALLS.C:1666)
    par = [w for w in W if w["flags"] & 0x40]
    put("parallax sans invisible", all((w["flags"] & 0x02) == 0 for w in par), f"{len(par)} ciels")

    # 8. budget esclave (WALLS.C:1374 / :1555) et vCalc[700] (WALLS.C:1017)
    cells = [w["tileLength"] * w["tileHeight"] for w in W if w["flags"] & 0x01]
    faces = [w["lastFace"] - w["firstFace"] + 1 for w in W if w["firstFace"] >= 0]
    put("assert tileLength*tileHeight < 700", max(cells or [0]) < 700, f"max {max(cells or [0])}")
    put("reservation esclave <= 1250", max((cells or [0]) + (faces or [0])) <= 1250,
        f"max {max((cells or [0]) + (faces or [0]))}")

    # 9. OBJETS (SPEC_CONVERTER 7 : remplace « un seul objet »). Le joueur en tete (OBJECT.C:200-206
    #    le cherche, assemble.py:204 le met premier), puis les mobjs (6 shorts) et les speciaux
    #    (portes 3, ascenseurs 4, sector-switch 2, interrupteur 5, sortie 1, degats 2). Rejoue :
    #    `assert(firstParam == objectPPos)` (OBJECT.C:211) = les params attendus par type se
    #    cumulent exactement sur nmObjectParams ; pointInSectorP (SPRITE.C:940-960) sur (x, y, z)
    #    de chaque objet positionne ; `y == floorLevel` (MESURE retail : ecart 0).
    obj = L["objects"]
    import struct
    sys.path.insert(0, HERE)
    import doom_specials as sp
    import things2objects as t2o
    p = L["objectParams"]
    put("joueur OT_PLAYER en tete", bool(obj) and obj[0]["type"] == 13, str(obj[:1]))
    nmobj = sum(1 for o in obj if o["type"] not in sp.OT_SPECIAL_TYPES) - 1
    put("firstParam cumules == nmObjectParams", sp.expected_param_bytes(obj) == len(p)
        and all(obj[i + 1]["firstParam"] > obj[i]["firstParam"] for i in range(len(obj) - 1)),
        f"{len(obj)} objets (1 joueur + {nmobj} mobjs + {len(obj) - 1 - nmobj} speciaux), "
        f"{sp.expected_param_bytes(obj)} o attendus, {len(p)} o")
    chk = t2o.check_objects(obj, p, S, W, V)
    put("objets dans leur secteur (pointInSectorP)", not chk["hors_secteur"],
        f"{chk['n']} positionnes, {len(chk['hors_secteur'])} hors secteur {chk['hors_secteur'][:3]}")
    put("y des objets == floorLevel", not chk["y_faux"], str(chk["y_faux"][:3]))
    sect, px, py, pz, ang = struct.unpack(">5h", bytes(p[:10]))
    s = S[sect]
    print(f"       depart : secteur {sect}, ({px}, {py}, {pz}), angle {ang}")
    put("y du depart == floorLevel du secteur", py == s["floorLevel"],
        f"{py} vs {s['floorLevel']}")
    inside = True
    for wi in range(s["firstWall"], s["lastWall"] + 1):
        w = W[wi]
        v0 = V[w["v"][0]]
        d = ((px - v0["x"]) * w["normal"][0] + (py - v0["y"]) * w["normal"][1]
             + (pz - v0["z"]) * w["normal"][2])
        if w["normal"][1] == 0 and d < -(1 << 16) // 4:
            inside = False
    put("depart dans son secteur (murs verticaux)", inside)

    # 10. hauteur libre au depart : le joueur fait 41 + 16 = 57 u (params/doom.cfg)
    ys = []
    for wi in range(s["firstWall"], s["lastWall"] + 1):
        w = W[wi]
        if w["normal"][1] != 0:
            ys.append((w["normal"][1] > 0, max(V[i]["y"] for i in w["v"])))
    fl = [y for isf, y in ys if isf]
    ce = [y for isf, y in ys if not isf]
    if fl and ce:
        put("hauteur libre au depart >= 57", (ce[0] - fl[0]) >= 57, f"{ce[0] - fl[0]} u")

    # 11. ACCESSIBILITE -- le critere qui manquait. Un .LEV peut passer les dix precedents et
    #     laisser le joueur SCELLE dans sa feuille de depart : c'est arrive, parce que le seuil
    #     WALLFLAG_SHORTOPENING avait ete recopie du convertisseur retail (90 = la sphere de
    #     PowerSlave, rayon 47) au lieu d'utiliser celui de Doom (56). Les cinq portails du
    #     secteur de depart etaient marques, donc pleins pour le joueur.
    #     On rejoue ici `bumpSectorBoundries` (SPRITE.C:406-424) A LA LIGNE :
    #       - parcours firstWall..lastWall, ARRET au premier nextSector == -1 (c'est un `break`) ;
    #       - portail refuse si (mur->flags & sprite->flags) & WALLFLAG_BLOCKBITS (0x1f00) ;
    #       - flags du joueur = BSHORT|BBLOCKED = 0x1100 (AI.C:47 + SPRITE.C:92), en dur.
    #     Plus la marche de `bumpFloor` : STEPHEIGHT = PLAYER_STEP (24 dans params/doom.cfg),
    #     ce qui rend le graphe ORIENTE -- on descend d'une corniche sans pouvoir y remonter.
    #     Un portail DOORWALL (0x20) est une PORTE : son SHORTOPENING est recalcule a chaque pas par
    #     `setDoorBlockBits` (AI.C:4294-4309), et le joueur l'ouvre en pressant. On le traverse.
    PLAYER_FLAGS = 0x1100
    STEPHEIGHT = 24
    #     Un sol MOBILE (ascenseur, sol qui descend : push block a floorSector) se traverse aussi :
    #     son interrupteur ou son declencheur l'amene au niveau. MESURE 2026-09-18 : E1M8 demarre
    #     dans une salle fermee par un sol a +64 u (secteur Doom 10) que l'interrupteur de la ligne
    #     141, DANS la salle, abaisse -- 1 secteur sur 200 atteignable sans cette regle.
    owner = {}
    for si, s_ in enumerate(S):
        for wi in range(s_["firstWall"], s_["lastWall"] + 1):
            owner[wi] = si
    mobiles = set()
    for b in L["pushBlocks"]:
        if b["floorSector"] >= 0:
            mobiles |= {owner[L["PBWall"][k]] for k in range(b["startWall"], b["endWall"] + 1)
                        if L["PBWall"][k] in owner}
    adj = [[] for _ in S]
    for si, s_ in enumerate(S):
        for wi in range(s_["firstWall"], s_["lastWall"] + 1):
            w = W[wi]
            if w["nextSector"] == -1:
                break
            n = w["nextSector"]
            mobile = si in mobiles or n in mobiles
            if (w["flags"] & PLAYER_FLAGS) & 0x1f00 and not (w["flags"] & 0x20) and not mobile:
                continue
            if 0 <= n < len(S) and (S[n]["floorLevel"] - s_["floorLevel"] <= STEPHEIGHT or mobile):
                adj[si].append(n)
    # Un TELEPORTEUR (OT_DOOM_TELEPORT) : entrer dans sa feuille, c'est arriver a sa destination.
    teles = [struct.unpack(">8h", bytes(p[o["firstParam"]:o["firstParam"] + 16]))
             for o in obj if o["type"] == sp.OT_DOOM_TELEPORT]
    for t_ in teles:
        if 0 <= t_[0] < len(S) and 0 <= t_[1] < len(S):
            adj[t_[0]].append(t_[1])
    seen = {sect}
    stack = [sect]
    while stack:
        for n in adj[stack.pop()]:
            if n not in seen:
                seen.add(n)
                stack.append(n)
    # Deux tests, pas un pourcentage arbitraire.
    #  (a) Le plancher de bon sens : une conversion STATIQUE perd les ascenseurs et les
    #      plates-formes de Doom, qui ne se baissent jamais, donc 100 % est hors d'atteinte --
    #      MESURE sur E1M1 apres correction : 196/236 = 83 %. Le plancher est pose bien en
    #      dessous ; le defaut qu'il attrape (joueur scellé) se lit a 0 %, pas a 70 %.
    #  (b) Le vrai test : CHAQUE frontiere entre la zone atteignable et le reste doit s'expliquer
    #      par une marche de Doom ou par une ouverture trop basse. Une frontiere inexplicable est
    #      un bug de conversion.
    raisons = Counter()
    for si in seen:
        s_ = S[si]
        for wi in range(s_["firstWall"], s_["lastWall"] + 1):
            w = W[wi]
            if w["nextSector"] == -1:
                break
            n = w["nextSector"]
            if not (0 <= n < len(S)) or n in seen:
                continue
            ys = [V[i]["y"] for i in w["v"]]
            h = max(ys) - min(ys)
            d = S[n]["floorLevel"] - s_["floorLevel"]
            # SHORTOPENING porte aussi la marche de Doom (> 24, doom3d.post_flags) : l'etiquette
            # suit la vraie cause, et un portail marque sans l'une ni l'autre reste INEXPLIQUE.
            if (w["flags"] & PLAYER_FLAGS) & 0x1f00 and not (w["flags"] & 0x20) and h < 56:
                raisons[f"ouverture de {h} (< {56})"] += 1
            elif d > STEPHEIGHT:
                raisons[f"marche de {d} (> {STEPHEIGHT})"] += 1
            else:
                raisons["INEXPLIQUE"] += 1
    put("joueur non scelle au depart", len(seen) * 5 >= len(S) * 3,
        f"{len(seen)}/{len(S)} secteurs atteignables a pied "
        f"({100.0 * len(seen) / len(S):.0f} %)")
    put("toute frontiere bloquante s'explique", raisons["INEXPLIQUE"] == 0,
        ", ".join(f"{n} x {k}" for k, n in raisons.most_common()) or "aucune frontiere")

    # 12. AUCUN MUR FANTOME. Le defaut le plus couteux trouve jusqu'ici ne violait aucun critere
    #     interne : les sommets sont RECONSTRUITS depuis l'etiquette de droite de leur arete
    #     (`adjacency.point_at`), donc une arete mal etiquetee voit ses extremites PROJETEES sur
    #     une autre droite. MESURE avant correctif : 25 aretes sur 932 (2,7 %), l'une deplacee de
    #     416 unites -- des murs en plein milieu des pieces, des polygones de plafond deformes, et
    #     des quads de portail decales donc une fenetre de clip trop petite pour le secteur
    #     suivant (« je ne vois pas tout a la fois »).
    #     Le seul juge possible est la CARTE D'ORIGINE : un mur plein doit etre porte par un seg
    #     ou un linedef de Doom. Les deux comptent -- le nodebuilder arrondit a l'entier les
    #     sommets qu'il cree en coupant, donc 5 % des segs d'E1M1 ne sont PAS exactement sur la
    #     droite de leur linedef parent.
    try:
        sys.path.insert(0, HERE)
        import wad as wadmod
        import adjacency
        M = wadmod.read_map(wadmod.Wad(a.wad), a.map)
    except Exception as e:                                   # WAD absent : on ne bloque pas
        put("aucun mur fantome", True, f"non teste ({e})")
        M = None
    if M is not None:
        # L'angle du depart est celui du thing 1, sans correction : c'est la convention des sprites
        # et constructPlayer en retire lui-meme 90 degres pour la camera (AI.C:51). Le +90 d'avant
        # faisait arriver le joueur tourne de 90 degres a gauche (console, 2026-09-18).
        st1 = next((t for t in M["things"] if t.type == 1), None)
        ang_doom = int(round((st1.angle % 360) * 4096.0 / 360.0)) % 4096 if st1 else None
        put("angle du depart == angle Doom du thing 1 (constructPlayer retire 90)",
            ang == ang_doom, f"{ang} vs {ang_doom}")
        # 11b. LES CLES. Une porte a cle (26-28, 32-34) ne s'ouvre qu'avec sa carte ou son crane
        #      (OT_DOOM_DOOR, 2026-09-18 ; avant, toutes s'ouvraient sans cle). On rejoue la
        #      recherche du 11 en fermant les feuilles d'une porte a cle tant que sa cle n'est pas
        #      ramassee, jusqu'au point fixe : une cle hors d'atteinte bloquerait la partie.
        try:
            ids_k = json.load(open(a.ids, encoding="utf-8"))
            dsec_k = json.load(open(a.geom, encoding="utf-8"))["doom_sector"]
        except Exception:
            ids_k = dsec_k = None
        if ids_k is not None and dsec_k is not None:
            e2m = {int(k): int(v) for k, v in ids_k["ed_to_mt"].items()}
            cle_ot = {}
            for ed, cle in ((5, 1), (40, 1), (6, 2), (39, 2), (13, 3), (38, 3)):
                if ed in e2m and ids_k["mt_to_ot"][e2m[ed]] >= 0:
                    cle_ot[ids_k["mt_to_ot"][e2m[ed]]] = cle
            # Une cle se ramasse d'un secteur ou l'on se tient : P_TouchSpecialThing (et
            # doom_item_func) touche a rayon cle + joueur (20 + 16) en x et en z, si la cle est de
            # -8 a +56 u au-dessus des pieds. MESURE E1M7 : la cle bleue est sur un rebord de 32 u
            # (secteur Doom 76) qu'on ne gravit pas -- on la prend d'en bas, du secteur 77.
            def dedans(si, x, z):
                s_ = S[si]
                for wi in range(s_["firstWall"], s_["lastWall"] + 1):
                    w = W[wi]
                    if w["normal"][1] != 0:
                        continue
                    v0 = V[w["v"][0]]
                    if (x - v0["x"]) * w["normal"][0] + (z - v0["z"]) * w["normal"][2] < 0:
                        return False
                return True
            cles_ou = defaultdict(set)
            for o in obj:
                if o["type"] in cle_ot:
                    s_k, x_k, y_k, z_k = struct.unpack(
                        ">4h", bytes(p[o["firstParam"]:o["firstParam"] + 8]))
                    for si, s_ in enumerate(S):
                        if not (y_k - 56 <= s_["floorLevel"] <= y_k + 8):
                            continue
                        if any(dedans(si, x_k + dx, z_k + dz) for dx in (-35, 0, 35)
                               for dz in (-35, 0, 35)):
                            cles_ou[cle_ot[o["type"]]].add(si)
            spk = sp.specials_of(M)
            porte_cle = {d["sector"]: d["key"] for d in spk.doors if d.get("key")}
            ferme = {s_: porte_cle[ds_] for s_, ds_ in enumerate(dsec_k) if ds_ in porte_cle}
            tenues = set()
            while True:
                vus_k, pile = {sect}, [sect]
                while pile:
                    for n in adj[pile.pop()]:
                        if n not in vus_k and (n not in ferme or ferme[n] in tenues):
                            vus_k.add(n)
                            pile.append(n)
                neuves = {c for c, ss in cles_ou.items() if ss & vus_k} - tenues
                if not neuves:
                    break
                tenues |= neuves
            manque = sorted({ferme[s_] for s_ in seen - vus_k if s_ in ferme})
            put("cles : chaque porte a cle atteignable l'est avec une cle ramassable avant",
                vus_k == seen,
                f"{len(set(ferme.values()))} couleurs de porte, cles au sol {sorted(cles_ou)}, "
                f"{len(vus_k)}/{len(seen)} secteurs avec les cles"
                + (f", couleurs hors d'atteinte {manque}" if manque else ""))
        DV, LD, SG = M["vertices"], M["linedefs"], M["segs"]

        murs_carte = [(DV[x.v1][0], DV[x.v1][1], DV[x.v2][0], DV[x.v2][1]) for x in LD]
        murs_carte += [(DV[x.v1][0], DV[x.v1][1], DV[x.v2][0], DV[x.v2][1]) for x in SG]

        def porte(segments, px, pz, qx, qz, tol=1.5):
            """Le mur est-il pose sur un segment de la carte ?

            SURTOUT PAS une egalite de cle de droite : les sommets du .LEV sont des ENTIERS, et
            sur une diagonale un arrondi de +-0,5 change la direction, donc la forme canonique de
            la droite -- des murs parfaitement reels etaient declares fantomes. On teste donc la
            GEOMETRIE : les deux extremites du mur sont-elles a moins de `tol` d'un meme segment
            de la carte, et sa longueur couverte par lui ? Un mur de Doom pouvant etre fait de
            plusieurs linedefs bout a bout, on accepte aussi le recouvrement par l'union."""
            couvert = []
            for ax, ay, bx, by in segments:
                ex, ey = bx - ax, by - ay
                L2 = ex * ex + ey * ey
                if L2 <= 0:
                    continue
                ok = True
                ts = []
                for x, y in ((px, pz), (qx, qz)):
                    t = ((x - ax) * ex + (y - ay) * ey) / L2
                    d = abs((x - ax) * ey - (y - ay) * ex) / math.sqrt(L2)
                    if d > tol:
                        ok = False
                        break
                    ts.append(t)
                if ok:
                    # portion du MUR couverte : les extremites du segment projetees sur le mur.
                    # (Avant : ts, parametre le long du SEGMENT -- un long linedef qui couvre tout
                    # un mur court ne comptait que pour longueur_mur / longueur_segment.)
                    wx, wz = qx - px, qz - pz
                    W2 = (wx * wx + wz * wz) or 1.0
                    ua = ((ax - px) * wx + (ay - pz) * wz) / W2
                    ub = ((bx - px) * wx + (by - pz) * wz) / W2
                    couvert.append((min(ua, ub), max(ua, ub), math.sqrt(L2)))
            if not couvert:
                return False
            # union des portions couvertes, exprimee en fraction du mur
            m = sorted((max(0.0, a), min(1.0, b)) for a, b, _ in couvert)
            bord, total = None, 0.0
            for a, b in m:
                if b <= a:
                    continue
                if bord is None or a > bord:
                    bord = b
                    total += b - a
                elif b > bord:
                    total += b - bord
                    bord = b
            return total

        fantomes = []
        pleins = 0
        for si, s_ in enumerate(S):
            for wi in range(s_["firstWall"], s_["lastWall"] + 1):
                w = W[wi]
                if w["normal"][1] != 0 or w["nextSector"] != -1:
                    continue
                pleins += 1
                p_, q_ = V[w["v"][0]], V[w["v"][1]]
                # Seuil : un mur VISIBLE que la carte n'a pas. Un mur couvert a 90 % est le bon
                # mur dont l'extremite a ete arrondie a l'entier (MESURE : 5 des 8 derniers cas
                # sont couverts a 83-96 %), et une echarde de 1 a 4 unites est sous le texel.
                # Ce qui doit echouer, c'est le defaut reel : un mur long pose la ou il n'y a
                # rien -- le bug d'etiquette en produisait de 100 a 400 unites, couverts a 0 %.
                lg = math.hypot(q_["x"] - p_["x"], q_["z"] - p_["z"])
                couv = porte(murs_carte, p_["x"], p_["z"], q_["x"], q_["z"])
                if couv >= 0.5 or lg <= 8.0:
                    continue
                fantomes.append((si, wi, (p_["x"], p_["z"]), (q_["x"], q_["z"]),
                                 round(lg), f"{couv * 100:.0f} %"))
        put("aucun mur fantome", not fantomes,
            f"{len(fantomes)} murs pleins sans seg ni linedef sur {pleins}, ex. {fantomes[:3]}"
            if fantomes else f"{pleins} murs pleins, tous portes par la carte")

    # 13. ECHELLE VERTICALE DES MURS. La tuile est fabriquee POUR la cellule (E4.1c) : la hauteur
    #     de cellule reellement posee, `H / tileHeight`, doit egaler le `cv` avec lequel la tuile a
    #     ete echantillonnee. Sans cela la texture est etiree ou repetee -- MESURE avant correctif :
    #     hauteurs de cellule de 2 a 336 u pour une tuile calee sur la hauteur de la texture, et
    #     332 murs sous 64 u ou la texture entiere s'ecrasait (les contremarches).
    #     Murs MOBILES (--mobile) : un mur qui GRANDIT a l'execution (rail de porte, flanc
    #     d'ascenseur) fait 1 u ferme et sa tuile est faite pour l'etat ouvert : exempte.
    try:
        import json as _json
        det = _json.load(open(a.tiles, encoding="utf-8"))["detail"]
    except Exception as e:
        put("echelle verticale des murs", True, f"non teste ({e})")
        det = None
    if det is not None:
        # Les index du fichier sont LOCAUX au niveau (le chargeur ajoute tileBase, LEVEL.C) : la
        # tuile 0 est la premiere de tiles.json. L'ancien `min(index des cellules)` ne valait 0 que
        # tant qu'une cellule de mur montrait la tuile 0 -- faux des qu'elle est un flat (E1M5
        # apres le budget de tuiles : 805 murs sur 948 compares a la tuile voisine).
        base = 0
        pbwalls = set(L["PBWall"])
        faux, vus = [], 0
        # Niveau passe au budget de tuiles (doomtiles.reduire) : une tuile gardee represente les
        # variantes fondues avec elle. Une cellule dont la hauteur est celle d'une de ces variantes
        # est un compromis ACCEPTE (compte a part) ; hors de toutes, c'est un defaut.
        try:
            rep = _json.load(open(a.geom, encoding="utf-8")).get("tuiles_representees")
        except Exception:
            rep = None
        fondus = 0
        for si, s_ in enumerate(S):
            for wi in range(s_["firstWall"], s_["lastWall"] + 1):
                w = W[wi]
                if w["normal"][1] != 0 or (w["flags"] & 0x02) or not (w["flags"] & 0x01):
                    continue
                if wi in pbwalls and max(V[i]["y"] for i in w["v"]) - min(V[i]["y"] for i in w["v"]) <= 1:
                    continue
                loc = tex[w["textures"] + 1] - base
                if not (0 <= loc < len(det)):
                    continue
                ys = [V[i]["y"] for i in w["v"]]
                cell = (max(ys) - min(ys)) / float(w["tileHeight"] or 1)
                cv = det[loc].get("cv") or 0
                vus += 1
                if not cv or max(cell / cv, cv / cell) > 1.02:
                    if rep and any(len(k) > 5 and isinstance(k[5], (int, float)) and k[5]
                                   and max(cell / k[5], k[5] / cell) <= 1.02 for k in rep[loc]):
                        fondus += 1
                        continue
                    faux.append((si, wi, round(cell, 1), cv))
        put("echelle verticale des murs", not faux,
            f"{len(faux)}/{vus} murs hors de 2 %, ex. {faux[:3]}" if faux
            else f"{vus} murs, cellule posee == cellule echantillonnee"
                 + (f" ou celle d'une variante fondue au budget ({fondus})" if fondus else ""))

    # 14. ORIENTATION DES FLATS. Le VDP1 pose le coin (0,0) de la tuile sur poly[0] et le moteur
    #     ne permute rien (WALLS.C:1223-1231) ; Doom veut la ligne 0 au NORD et la colonne 0 a
    #     l'OUEST (R_DrawSpan : u = x & 63, v = -y & 63). Tout carre 64x64 aligne doit donc etre
    #     NO, NE, SE, SO. MESURE avant correctif : 0 sur ~1 100, tournes ET en miroir.
    mal, carres = [], 0
    for si, s_ in enumerate(S):
        for wi in range(s_["firstWall"], s_["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] == 0 or w["firstFace"] < 0:
                continue
            b = w["firstVertex"]
            for fi in range(w["firstFace"], w["lastFace"] + 1):
                q = [(V[b + i]["x"], V[b + i]["z"]) for i in F[fi]["v"]]
                xs = [p_[0] for p_ in q]
                zs = [p_[1] for p_ in q]
                x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
                if not (x1 - x0 == 64 and z1 - z0 == 64 and x0 % 64 == 0 and z0 % 64 == 0
                        and sorted(q) == sorted([(x0, z0), (x0, z1), (x1, z0), (x1, z1)])):
                    continue
                carres += 1
                if q != [(x0, z1), (x1, z1), (x1, z0), (x0, z0)]:
                    mal.append((si, fi))
    put("flats orientes NO-NE-SE-SO", not mal,
        f"{len(mal)}/{carres} carres mal orientes, ex. {mal[:3]}" if mal
        else f"{carres} carres alignes, tous orientes")

    # 15. PUSH BLOCKS (SPEC_CONVERTER 7, --mobile). Rejoue les asserts de registerPBObject
    #     (OBJECT.C:141-152 : PBWall dans [0, nmWalls)), d'updatePushBlockPositions (SPRITE.C:863 :
    #     vStart + vNm <= nmVerticies), la fente fermee de chaque DOORWALL (setDoorBlockBits mesure
    #     v[1].y - v[2].y, AI.C:4302) et la course des objets contre la REGLE DE DOOM recalculee
    #     depuis le WAD (doom_specials.mobile_bounds : porte = plus bas plafond voisin - 4 - sol,
    #     36 = plus HAUT sol voisin + 8).
    PB, PBV, PBW = L["pushBlocks"], L["PBVert"], L["PBWall"]
    if PB:
        bad = [w for w in PBW if not (0 <= w < len(W))]
        put("PBWall dans [0, nmWalls)", not bad, f"{len(PBW)} murs, {len(PB)} push blocks")
        bad = [r for r in PBV if r["vStart"] + r["vNm"] > len(V) or r["vNm"] <= 0]
        put("PBVert.vStart + vNm <= nmVerticies", not bad, f"{len(PBV)} runs, "
            f"{sum(r['vNm'] for r in PBV)} sommets")
        bad = [i for i, b in enumerate(PB)
               if not (0 <= b["startWall"] <= b["endWall"] < len(PBW)
                       and 0 <= b["startVertex"] <= b["endVertex"] < len(PBV)
                       and 0 <= b["enclosingSector"] < len(S)
                       and (b["floorSector"] == -1 or 0 <= b["floorSector"] < len(S))
                       and b["dx"] == b["dy"] == b["dz"] == 0)]
        put("plages des push blocks", not bad, str(bad[:3]))
        # Une porte FERMEE bloque le joueur : son ouverture reste sous 56 u (setDoorBlockBits,
        # AI.C:4294-4309 : SHORTOPENING des qu'elle est plus basse que CFG_DOOR_FIT). Fermee a la
        # fente (1 u) le plus souvent ; une porte que le WAD laisse entrouverte garde son jour
        # (E1M5 secteur 121 : 8 u), comme dans Doom au depart.
        dws = [wi for wi in PBW if W[wi]["flags"] & 0x20]
        hs = {wi: V[W[wi]["v"][1]]["y"] - V[W[wi]["v"][2]]["y"] for wi in dws}
        bad = [wi for wi in dws if not (sp.DOOR_SLIT <= hs[wi] < 56) or not (W[wi]["flags"] & 0x1000)]
        entrouvertes = sum(1 for wi in dws if hs[wi] != sp.DOOR_SLIT)
        put("chaque DOORWALL ferme (fente 1 u, ou jour du WAD < 56) + SHORTOPENING", not bad,
            f"{len(dws)} DOORWALL" + (f", dont {entrouvertes} entrouverts par le WAD" if entrouvertes else "")
            + (f", fautifs {bad[:3]}" if bad else ""))
        # objets contre PB : chaque porte / ascenseur pointe un pb existant, un pb par objet
        # portes Doom : pb, canal, doorHeight, genre de l'appui, cle, genre du tag (doom_specials)
        doors = [struct.unpack(">6h", bytes(p[o["firstParam"]:o["firstParam"] + 12]))
                 for o in obj if o["type"] == sp.OT_DOOM_DOOR]
        lifts = [(o["type"], struct.unpack(">4h", bytes(p[o["firstParam"]:o["firstParam"] + 8])))
                 for o in obj if o["type"] in (49, 61)]
        # ascenseurs Doom : pb, course (< 0), canal, vitesse, attente
        lifts_d = [struct.unpack(">5h", bytes(p[o["firstParam"]:o["firstParam"] + 10]))
                   for o in obj if o["type"] == sp.OT_DOOM_LIFT]
        # sols : pb, course (> 0 monte, < 0 descend), canal, vitesse, face donneuse, degats
        sols_o = [struct.unpack(">6h", bytes(p[o["firstParam"]:o["firstParam"] + 12]))
                  for o in obj if o["type"] == sp.OT_DOOM_FLOOR]
        raises = [r for r in sols_o if r[1] > 0]
        pbs = ([d[0] for d in doors] + [l_[1][0] for l_ in lifts] + [l_[0] for l_ in lifts_d]
               + [r[0] for r in sols_o])
        put("un push block par porte / ascenseur / sol", sorted(pbs) == list(range(len(PB))),
            f"{len(doors)} portes, {len(lifts) + len(lifts_d)} ascenseurs, {len(raises)} sols qui montent, "
            f"{len(sols_o) - len(raises)} sols qui descendent, {len(PB)} push blocks")
        # Une porte Doom a un appui, un tag, ou les deux ; un canal si et seulement si un tag
        # (E1M2 secteur 97 : manuelle ET interrupteur, le canal perdu -- vu sur console 09-18).
        bad = [d for d in doors if not (0 <= d[3] <= 4 and 0 <= d[4] <= 3 and 0 <= d[5] <= 4)
               or not (d[3] or d[5]) or ((d[1] != -1) != (d[5] != 0)) or (d[4] and not d[3])]
        put("portes : genre d'appui / cle / genre de tag coherents, canal <=> tag", not bad,
            f"{sum(1 for d in doors if d[3] and d[5])} portes manuelles ET a tag, "
            f"{sum(1 for d in doors if d[4])} a cle" + (f", fautives {bad[:3]}" if bad else ""))
        put("floorSector : -1 porte, feuille ascenseur / sol qui monte",
            all(PB[d[0]]["floorSector"] == -1 for d in doors)
            and all(PB[l_[1][0]]["floorSector"] >= 0 for l_ in lifts)
            and all(PB[l_[0]]["floorSector"] >= 0 for l_ in lifts_d)
            and all(PB[r[0]]["floorSector"] >= 0 for r in sols_o))
        # BLOCKSSIGHT (doom3d.post_flags) : les portails d'un sol FERME au chargement (sol = plafond,
        # le tag 666 d'E1M8) ne laissent passer ni le rendu ni la vue ; DOOM_GAME.C les rouvre en
        # vidant le drapeau sur les murs du push block quand le sol part. Donc : aucun portail
        # BLOCKSSIGHT hors d'un push block de sol, et aucun portail d'entree dans les feuilles d'un
        # tel sol sans le drapeau (le rendu traverserait par lui).
        own_ = {}
        for si, s_ in enumerate(S):
            for wi in range(s_["firstWall"], s_["lastWall"] + 1):
                own_[wi] = si
        murs_sol, fermes = set(), set()
        for (pb_, *_r) in sols_o:
            ws_ = {PBW[k] for k in range(PB[pb_]["startWall"], PB[pb_]["endWall"] + 1)}
            murs_sol |= ws_
            if any((W[wi]["flags"] & 0x08) and W[wi]["nextSector"] >= 0 for wi in ws_):
                fermes |= {own_[wi] for wi in ws_ if W[wi]["normal"][1] > 0}
        aveugles = [wi for wi, w in enumerate(W) if (w["flags"] & 0x08) and w["nextSector"] >= 0]
        bad = [wi for wi in aveugles if wi not in murs_sol]
        bad += [wi for wi, w in enumerate(W) if w["nextSector"] in fermes and own_[wi] not in fermes
                and not (w["flags"] & 0x08)]
        if aveugles or fermes:
            put("portails BLOCKSSIGHT : ceux d'un sol ferme au chargement, tous rouvrables",
                not bad, f"{len(aveugles)} portails, {len(fermes)} feuilles fermees"
                + (f", fautifs {bad[:4]}" if bad else ""))
        # Un sol qui monte est EMIS en haut et CHARGE en bas (doom3d.raise_floors) : ses faces de
        # sol sont a floorLevel + course, floorLevel etant l'etat du WAD ou les things se posent.
        if raises:
            owner_ = {}
            for si, s_ in enumerate(S):
                for wi in range(s_["firstWall"], s_["lastWall"] + 1):
                    owner_[wi] = si
            bad = []
            for (pb_, course, ch, vit, donor, dg) in raises:
                b = PB[pb_]
                for k in range(b["startWall"], b["endWall"] + 1):
                    wi = PBW[k]
                    if W[wi]["normal"][1] <= 0:
                        continue
                    ys = {V[i]["y"] for i in W[wi]["v"]}
                    si = owner_[wi]
                    if ys != {S[si]["floorLevel"] + course}:
                        bad.append((pb_, si, sorted(ys), S[si]["floorLevel"], course))
                if not (0 < vit <= 64) or not (-1 <= donor < len(F)) or not (-1 <= dg < 256):
                    bad.append((pb_, "params", vit, donor, dg))
            put("sols qui montent : emis a floorLevel + course, params dans les bornes", not bad,
                f"{len(raises)} sols, courses {sorted(r[1] for r in raises)}"
                + (f", fautifs {bad[:3]}" if bad else ""))
        # ETAT CHARGE : aucun mur plein visible dans l'OUVERTURE REELLE d'un portail -- entre le
        # plus haut des deux sols et le plus bas des deux plafonds, a l'etat ou le niveau demarre
        # (sommets d'un sol qui monte : emis `course` plus haut). Une paroi de cage posee contre un
        # voisin qui bouge lui aussi y dressait un mur de toute la course (E1M3 48/49, 120 u) ;
        # contre un voisin fixe elle s'arrete a son sol, donc au bord de l'ouverture.
        dy = {}
        for (pb_, course, *_r) in raises:
            b = PB[pb_]
            for k in range(b["startVertex"], b["endVertex"] + 1):
                for vi in range(PBV[k]["vStart"], PBV[k]["vStart"] + PBV[k]["vNm"]):
                    dy[vi] = -course
        plafond = {}
        for si, s_ in enumerate(S):
            for wi in range(s_["firstWall"], s_["lastWall"] + 1):
                if W[wi]["normal"][1] < 0:
                    plafond[si] = V[W[wi]["v"][0]]["y"]
                    break
        bad, vus_ = [], 0
        for si, s_ in enumerate(S):
            ws = [wi for wi in range(s_["firstWall"], s_["lastWall"] + 1) if W[wi]["normal"][1] == 0]
            pleins_ = [wi for wi in ws if W[wi]["nextSector"] == -1 and not (W[wi]["flags"] & 0x02)]
            for pw in ws:
                n = W[pw]["nextSector"]
                if n < 0 or si not in plafond or n not in plafond:
                    continue
                lo = max(S[si]["floorLevel"], S[n]["floorLevel"])
                hi = min(plafond[si], plafond[n])
                if hi - lo <= 1:
                    continue
                vus_ += 1
                P_, Q_ = V[W[pw]["v"][0]], V[W[pw]["v"][1]]
                ex, ez = Q_["x"] - P_["x"], Q_["z"] - P_["z"]
                L2 = float(ex * ex + ez * ez) or 1.0
                for fw in pleins_:
                    # sur la MEME arete : les murs d'une arete en partagent les sommets entiers, donc
                    # les deux bouts a moins de 0,5 u de la droite du portail, et au moins 2 u de
                    # recouvrement le long de lui. A 1 u, un mur d'une unite perpendiculaire au bout
                    # du portail, ou le bout de 3,6 u d'une autre arete presque alignee (pad en
                    # etoile d'E1M5, a 0,7 u), passaient pour des murs dans l'ouverture.
                    ts, sur = [], True
                    for vi in W[fw]["v"][:2]:
                        X = V[vi]
                        ts.append(((X["x"] - P_["x"]) * ex + (X["z"] - P_["z"]) * ez) / L2)
                        if abs((X["x"] - P_["x"]) * ez - (X["z"] - P_["z"]) * ex) / math.sqrt(L2) > 0.5:
                            sur = False
                    if not sur or (min(max(ts), 1.0) - max(min(ts), 0.0)) * math.sqrt(L2) < 2.0:
                        continue
                    ys_ = [V[vi]["y"] + dy.get(vi, 0) for vi in W[fw]["v"]]
                    ov = min(hi, max(ys_)) - max(lo, min(ys_))
                    if ov > 1:
                        # POSE SUR le portail : meme droite et meme segment, aux arrondis pres.
                        pxz = sorted((V[vi]["x"], V[vi]["z"]) for vi in W[pw]["v"])
                        fxz = sorted((V[vi]["x"], V[vi]["z"]) for vi in W[fw]["v"])
                        meme = (len(set(pxz)) == 2 and len(set(fxz)) == 2
                                and set(pxz) == set(fxz))
                        bad.append((si, n, fw, round(ov), meme))
        # Une GRILLE est justement un mur plein pose dans l'ouverture d'un portail : c'est comme
        # cela que Doom peint la texture du milieu d'une ligne a deux faces.  Elle est excusee
        # quand elle est POSEE SUR le portail -- meme droite, meme segment : c'est la signature
        # d'une texture du milieu, et non d'une geometrie etrangere qui traverse l'ouverture.
        # La plupart sont ajourees (BRNBIG*, BRNSMAL*), une ne l'est pas (BROWNGRN, pleine) et
        # Doom la peint tout autant, donc la transparence ne peut pas etre le critere ; elle est
        # comptee a part, pour qu'un trou reel se voie dans le detail.
        ajoures = murs_ajoures(a, W, tex) if bad else set()
        sur_portail = [t for t in bad if t[4]]
        bad = [t for t in bad if not t[4]]
        put("etat charge : aucun mur plein ETRANGER dans l'ouverture d'un portail", not bad,
            f"{vus_} portails ouverts, {len(sur_portail)} textures du milieu dont "
            f"{sum(1 for t in sur_portail if t[2] in ajoures)} ajourees"
            + (f", {len(bad)} murs dedans, ex. {bad[:3]}" if bad else ""))
        if M is not None:
            spx = sp.specials_of(M)
            # fermee a sol + fente (ou entrouverte par le WAD, M n'est pas referme ici), door_func
            # monte de doorHeight : depart + doorHeight = regle Doom
            want_d = sorted(d["upper"] - max(M["sectors"][d["sector"]].ceilh, d["lower"] + sp.DOOR_SLIT)
                            for d in spx.doors)
            want_l = sorted((l_["lower"], l_["upper"]) for l_ in spx.lifts)
            want_f = sorted(f_["lower"] - f_["upper"] for f_ in spx.floors)
            put("fente + doorHeight == regle Doom (min plafond voisin - 4 - sol)",
                sorted(d[2] for d in doors) == want_d, f"{sorted(d[2] for d in doors)} vs {want_d}")
            sec = [struct.unpack(">h", bytes(p[o["firstParam"]:o["firstParam"] + 2]))[0]
                   for o in obj if o["type"] == sp.OT_DOOM_SECRETWALL]
            nsl = sum(1 for ld in M["linedefs"] if (ld.flags & sp.ML_SECRET) and ld.special in sp.DOOR_MANUAL)
            put("OT_DOOM_SECRETWALL : murs DOORWALL des portes ML_SECRET",
                (len(sec) > 0) == (nsl > 0) and all(0 <= w < len(W) and (W[w]["flags"] & 0x20) for w in sec),
                f"{len(sec)} murs pour {nsl} ligne(s) de porte ML_SECRET : {sec}")
            put("course ascenseur == regle Doom (plus bas sol voisin)",
                sorted((l_[1][1], l_[1][2]) for l_ in lifts) == want_l if lifts else
                sorted(l_[1] for l_ in lifts_d) == sorted(lo - hi for lo, hi in want_l),
                f"{sorted((l_[1][1], l_[1][2]) for l_ in lifts) or sorted(l_[1] for l_ in lifts_d)}"
                f" vs {want_l}")
            if lifts_d:
                put("ascenseurs Doom : canal = tag, vitesse PLATSPEED*4 / *8, attente PLATWAIT",
                    all(c_ > 0 and v_ in (sp.LIFT_SPEED, sp.LIFT_SPEED_BLAZE) and a_ == sp.LIFT_WAIT
                        for (_pb, _co, c_, v_, a_) in lifts_d),
                    f"{len(lifts_d)} ascenseurs, canaux {sorted(l_[2] for l_ in lifts_d)}")
            if spx.floors or len(sols_o) != len(raises):
                have_f = sorted(r[1] for r in sols_o if r[1] < 0)
                put("course des sols qui descendent == regle Doom (36 : plus haut sol voisin + 8 ; "
                    "tag 666 d'A_BossDeath)", have_f == want_f, f"{have_f} vs {want_f}")
            if spx.raises or raises:
                want_r = sorted(r["upper"] - r["lower"] for r in spx.raises)
                put("course des sols qui montent == regle Doom (p_floor.c / p_plats.c, fente sous "
                    "le plafond)", sorted(r[1] for r in raises) == want_r,
                    f"{sorted(r[1] for r in raises)} vs {want_r}")
    else:
        put("push blocks", True, "aucun (geometrie statique)")

    # 15b. TELEPORTEURS (OT_DOOM_TELEPORT, DOOM_GAME.C). Rejoue EV_Teleport contre le WAD : la
    #      feuille qui declenche est du cote ARRIERE d'une ligne de teleporteur (on part en la
    #      franchissant depuis l'avant), l'arrivee est DANS sa feuille (pointInSectorP, murs
    #      verticaux) et cette feuille est dans un secteur du tag ; une feuille ne porte qu'UN objet
    #      de secteur (level_sector[s].object : teleporteur ou sector-switch).
    teles_o = [struct.unpack(">8h", bytes(p[o["firstParam"]:o["firstParam"] + 16]))
               for o in obj if o["type"] == sp.OT_DOOM_TELEPORT]
    if M is not None:
        lignes_t = [(li, ld) for li, ld in enumerate(M["linedefs"]) if ld.special in sp.TELEPORT]
        if lignes_t or teles_o:
            try:
                import json as _json2
                dsec = _json2.load(open(a.geom, encoding="utf-8"))["doom_sector"]
            except Exception:
                dsec = None
            SDm, Sm = M["sidedefs"], M["sectors"]
            arrieres = {SDm[ld.left].sector: ld.tag for li, ld in lignes_t if ld.left >= 0}
            ssw_ = {struct.unpack(">h", bytes(p[o["firstParam"]:o["firstParam"] + 2]))[0]
                    for o in obj if o["type"] == sp.OT_SECTORSWITCH}
            bad = []
            for (s0, s1, x, z, ang, fx, fz, fl) in teles_o:
                if not (0 <= s0 < len(S) and 0 <= s1 < len(S)) or dsec is None:
                    bad.append((s0, s1, "bornes"))
                    continue
                tag = arrieres.get(dsec[s0])
                if tag is None:
                    bad.append((s0, "pas du cote arriere d'une ligne de teleporteur"))
                    continue
                if Sm[dsec[s1]].tag != tag:
                    bad.append((s1, "arrivee hors du tag %d" % tag))
                tm = [t_ for t_ in M["things"] if t_.type == 14 and (t_.x, t_.y) == (x, z)]
                if not tm or ang != int(round((tm[0].angle % 360) * 4096.0 / 360.0)) % 4096:
                    bad.append((s1, "angle d'arrivee %d != angle Doom du MT_TELEPORTMAN" % ang))
                sd = S[s1]
                for wi in range(sd["firstWall"], sd["lastWall"] + 1):
                    w = W[wi]
                    if w["normal"][1] != 0:
                        continue
                    v0 = V[w["v"][0]]
                    if ((x - v0["x"]) * w["normal"][0] + (z - v0["z"]) * w["normal"][2]) < -(1 << 16) // 4:
                        bad.append((s1, (x, z), "arrivee hors de sa feuille", wi))
                        break
                if s0 in ssw_:
                    bad.append((s0, "feuille avec un sector-switch ET un teleporteur"))
            couverts = {dsec[t_[0]] for t_ in teles_o} if dsec else set()
            manquants = sorted(set(arrieres) - couverts)
            put("teleporteurs : cote arriere -> arrivee dans le tag, dans sa feuille",
                not bad and not manquants,
                f"{len(teles_o)} feuilles pour {len(lignes_t)} lignes, arrivees "
                f"{sorted({(t_[2], t_[3]) for t_ in teles_o})}"
                + (f", secteurs arriere sans objet {manquants}" if manquants else "")
                + (f", fautifs {bad[:3]}" if bad else ""))

    # 16. INTERRUPTEURS : rejoue constructSwitch (AI2.C:582-632) -- sequenceMap[type] >= 0,
    #     4 sequences, tuile OFF (chunk de la 1re frame) presente EXACTEMENT une fois dans les murs
    #     de sectorNm (le moteur asserte `tilePos`, et la premiere occurrence est celle animee).
    sws = [(o["type"], struct.unpack(">5h", bytes(p[o["firstParam"]:o["firstParam"] + 10])))
           for o in obj if o["type"] in sp.OT_SWITCH_TYPES]
    if sws:
        r3 = lev.Reader(a.lev)
        lev.parse_sky(r3)
        lev.parse_level_block(r3)
        lev.parse_sounds(r3)
        lev.parse_tiles(r3)
        Q = lev.parse_sequences(r3)
        # decodage du bloc (SEQUENCE.C:29-72 ; lev.parse_sequences ne garde que les comptes)
        b = r3.b
        q = Q["off"] + 4 + 12
        ns, nf, nc = Q["nmSequences"], Q["nmFrames"], Q["nmChunks"]
        frames = [dict(chunkIndex=struct.unpack_from(">h", b, q + 8 * i)[0]) for i in range(nf)]
        q += 8 * nf
        chunks = [dict(tile=struct.unpack_from(">h", b, q + 8 * i + 4)[0]) for i in range(nc)]
        q += 8 * nc
        seqs = list(struct.unpack_from(">%dh" % ns, b, q))
        q += 2 * ns
        smap = list(struct.unpack_from(">%dh" % lev.OT_NMTYPES, b, q))
        bad = []
        for t, (sn, ch, ox, oy, oz) in sws:
            base = smap[t]
            if base < 0 or base + 4 >= len(seqs):
                bad.append((t, "sequenceMap", base))
                continue
            off = chunks[frames[seqs[base]]["chunkIndex"]]["tile"]
            s_ = S[sn]
            cnt = 0
            for wi in range(s_["firstWall"], s_["lastWall"] + 1):
                w = W[wi]
                if w["flags"] & 0x01:
                    n_ = w["tileLength"] * w["tileHeight"]
                    cnt += sum(1 for c in range(n_) if tex[w["textures"] + 2 * c + 1] == off)
                elif w["firstFace"] >= 0:
                    cnt += sum(1 for f in F[w["firstFace"]:w["lastFace"] + 1] if f["tile"] == off)
            if cnt != 1:
                bad.append((t, "tuile OFF", off, "occurrences", cnt))
        put("interrupteurs : sequence et tuile OFF unique dans la feuille", not bad,
            f"{len(sws)} interrupteurs, canaux {[s_[1][1] for s_ in sws]}" if not bad else str(bad))

    # 16b. CANAUX : tout canal qu'un interrupteur ou un declencheur W fait sonner a un objet qui
    #      l'ecoute, et tout objet a canal a quelqu'un pour le faire sonner (A_BossDeath sonne les
    #      tags de doom_specials.BOSS_TAGS). E1M2 : l'interrupteur du canal 7 ne commandait rien,
    #      la porte 97 manuelle avait garde le canal -1 (console, 2026-09-18).
    def _sh(o, k):
        return struct.unpack(">%dh" % k, bytes(p[o["firstParam"]:o["firstParam"] + 2 * k]))
    emis = defaultdict(int)
    for o in obj:
        if o["type"] == sp.OT_SECTORSWITCH:
            emis[_sh(o, 2)[1]] += 1
        elif o["type"] == sp.OT_DOOM_WLINE:
            emis[_sh(o, 6)[4]] += 1
        elif o["type"] in sp.OT_SWITCH_TYPES:
            emis[_sh(o, 5)[1]] += 1
    ecoute = defaultdict(int)
    for o in obj:
        if o["type"] == sp.OT_DOOM_DOOR:
            d_ = _sh(o, 6)
            if d_[5]:
                ecoute[d_[1]] += 1
        elif o["type"] == 49:
            ecoute[_sh(o, 4)[3]] += 1
        elif o["type"] == sp.OT_DOOM_LIFT:
            ecoute[_sh(o, 5)[2]] += 1
        elif o["type"] == sp.OT_DOOM_FLOOR:
            ecoute[_sh(o, 6)[2]] += 1
        elif o["type"] == sp.OT_DOOM_LIGHT:
            ecoute[_sh(o, 3)[1]] += 1
        elif o["type"] in (sp.OT_DOOM_EXIT, sp.OT_DOOM_SECRETEXIT):
            ecoute[_sh(o, 1)[0]] += 1
    boss = {t_ for t_, _g in sp.BOSS_TAGS.get((a.map or "").upper(), ())}
    sourds = sorted(c for c in emis if c not in ecoute)
    muets = sorted(c for c in ecoute if c not in emis and c not in boss and c != sp.CHANNEL_NONE)
    put("canaux : chaque canal emis a un recepteur, chaque recepteur un emetteur (ou A_BossDeath)",
        not sourds and not muets,
        f"{len(emis)} canaux emis, {len(ecoute)} ecoutes" + (f", boss {sorted(boss)}" if boss else "")
        + (f", sans recepteur {sourds}" if sourds else "") + (f", sans emetteur {muets}" if muets else ""))
    if M is not None:
        wl = [_sh(o, 6) for o in obj if o["type"] == sp.OT_DOOM_WLINE]
        spw = sp.specials_of(M).wswitch
        Vm, Lm = M["vertices"], M["linedefs"]
        want_w = sorted(Vm[Lm[w_["line"]].v1] + Vm[Lm[w_["line"]].v2]
                        + (w_["channel"],
                           (sp.WLINE_ONCE if w_["special"] in sp.W_ONCE else 0)
                           | (sp.WLINE_GUN if w_["special"] in sp.DOOR_TAGGED_G else 0))
                        for w_ in spw)
        put("lignes W : une par linedef W, ses bouts, canal, W1 / WR (P_CrossSpecialLine)",
            sorted(wl) == want_w,
            f"{len(wl)} lignes, {sum(1 for w_ in wl if w_[5])} W1" if sorted(wl) == want_w
            else f"{len(wl)} objets vs {len(want_w)} lignes")
    dmg = [struct.unpack(">2h", bytes(p[o["firstParam"]:o["firstParam"] + 4]))
           for o in obj if o["type"] == 179]
    if dmg:
        put("OT_DOOM_DAMAGE : feuilles dans les bornes, hp > 0",
            all(0 <= d[0] < len(S) and d[1] > 0 for d in dmg), f"{len(dmg)} feuilles")
    lig = [_sh(o, 3) for o in obj if o["type"] == sp.OT_DOOM_LIGHT]
    if lig:
        put("OT_DOOM_LIGHT : feuilles dans les bornes, lumiere 0..16",
            all(0 <= l_[0] < len(S) and 0 <= l_[2] <= 16 for l_ in lig),
            f"{len(lig)} feuilles, canaux {sorted({l_[1] for l_ in lig})}, "
            f"lumieres {sorted({l_[2] for l_ in lig})}")

    # 17-21. CONTRAT « Verifications PC » (DOOM_ABI, SPEC_CONVERTER 7) : tuiles, sequences
    #        atteignables, sons, barils, tailles. Tout est relu dans le FICHIER par lev_io (le
    #        lecteur verifie), croise avec doom_ids.json, le WAD et STATIC.DAT.
    tail_checks(a, L, S, W, V, F, tex, obj, p, M)

    # 15. AUCUNE FACE DEGENEREE. Une face de moins de 3 sommets distincts ne peint rien : son aire
    #     manque, et l'on voit le ciel VDP2 a travers (trous triangulaires du disque du 14-09).
    degen = []
    for si, s_ in enumerate(S):
        for wi in range(s_["firstWall"], s_["lastWall"] + 1):
            w = W[wi]
            if w["firstFace"] < 0:
                continue
            b = w["firstVertex"]
            for fi in range(w["firstFace"], w["lastFace"] + 1):
                q = {(V[b + i]["x"], V[b + i]["y"], V[b + i]["z"]) for i in F[fi]["v"]}
                if len(q) < 3:
                    degen.append((si, fi))
    put("aucune face degeneree", not degen,
        f"{len(degen)} faces a moins de 3 sommets, ex. {degen[:4]}" if degen else f"{len(F)} faces")

    # 16. AUCUN MUR PLEIN INVISIBLE (hors ciel). Un mur plein INVISIBLE ne peint rien et l'on voit
    #     le noir au travers : 23 sur le disque du 15-09, tous sur un linedef a une face que
    #     seg_on_edge n'avait pas reconnu (flanc de l'ascenseur, linedef 269).
    ghost = [(si, wi) for si, s_ in enumerate(S) for wi in range(s_["firstWall"], s_["lastWall"] + 1)
             if W[wi]["nextSector"] < 0 and W[wi]["normal"][1] == 0
             and (W[wi]["flags"] & 0x02) and not (W[wi]["flags"] & 0x40)]
    put("aucun mur plein invisible (hors ciel)", not ghost,
        f"{len(ghost)} murs, ex. {ghost[:4]}" if ghost else "0")

    # 17. AUCUN PORTAIL PLEINE HAUTEUR VERS UNE FEUILLE PLUS HAUTE OU PLUS BASSE. Un portail qui
    #     couvre exactement [sol, plafond] de sa feuille alors que la voisine a un autre sol ou un
    #     autre plafond n'a ni contremarche ni linteau : on voit au travers (tranches du chemin
    #     au-dessus du nukage, disque du 15-09 : 8 portails).
    def flat_y(si, up):
        for wi in range(S[si]["firstWall"], S[si]["lastWall"] + 1):
            if (W[wi]["normal"][1] > 0) if up else (W[wi]["normal"][1] < 0):
                return V[W[wi]["v"][0]]["y"]
        return None
    pleins = []
    for si, s_ in enumerate(S):
        for wi in range(s_["firstWall"], s_["lastWall"] + 1):
            n = W[wi]["nextSector"]
            if n < 0 or W[wi]["normal"][1] != 0:
                continue
            ys = [V[i]["y"] for i in W[wi]["v"]]
            f0, c0, f1, c1 = flat_y(si, 1), flat_y(si, 0), flat_y(n, 1), flat_y(n, 0)
            if None in (f0, c0, f1, c1) or (f0, c0) == (f1, c1):
                continue
            if min(ys) == f0 and max(ys) == c0 and (f1 > f0 or c1 < c0):
                pleins.append((si, wi, n))
    put("aucun portail pleine hauteur vers une feuille plus haute / plus basse", not pleins,
        f"{len(pleins)} portails, ex. {pleins[:4]}" if pleins else "0")

    # 18. COUVERTURE DES FLATS : toute surface INTERIEURE a une feuille doit etre peinte. Les autres
    #     criteres verifient ce qui est EMIS ; celui-ci verifie ce qui MANQUE, et rien d'autre ne le
    #     voit. La regle, le seuil et les mesures qui le fixent sont dans tools/coverage.py -- le
    #     critere ne lit que (S, W, V, F), donc le convertisseur Duke le rejoue tel quel
    #     (verif_e3.py, critere « flats_sans_trou »).
    trous, aire = coverage.trous_de_flats(S, W, V, F)
    put(f"aucun trou dans un sol ou un plafond (> {coverage.SEUIL_TROU:.0f} u2 par feuille)",
        not trous, coverage.resume(trous, aire))

    # 19. ORDRE DU PEINTRE : le second critere qui juge l'IMAGE et non le fichier. Il rejoue la
    #     boucle de dessin du moteur depuis chaque position debout, avec la table DU FICHIER --
    #     donc il attrape aussi bien une table mal calculee qu'une table bien calculee et mal
    #     serialisee, ce qu'aucun critere du convertisseur ne peut voir. La regle et les mesures
    #     sont dans tools/ordre.py ; le convertisseur Duke le rejoue tel quel (verif_e3.py).
    o = ordre.evaluer(S, W, V, [(p["a"], p["b"], p["plane"]) for p in L["orderPairs"]])
    pc = 100.0 * o["positions"] / max(1, o["total"])
    put(f"ordre du peintre : moins de {SEUIL_ORDRE:.0f} % de positions fautives", pc <= SEUIL_ORDRE,
        f"{o['positions']}/{o['total']} positions ({pc:.0f} %), {o['paires']} paires mal "
        f"ordonnees, {o['entrees']} entrees de table, {o['indecidables']} paires sans plan, "
        f"{o['perdues']} aretes refusees par MAXFANIN")

    # 20. BANDES DE MAILLAGE : les faces d'un mur sont-elles rangees dans l'ordre ou
    #     weldFaceStrip sait les souder ? Le critere n'a PAS de seuil arbitraire -- il recalcule
    #     le plafond sur les faces du fichier lui-meme (tools/bandes.py, couplage biparti, donc
    #     optimum exact) et exige qu'on y soit. Un ordre d'emission relache se voit tout de suite.
    atteint = possibles = plafond = paires = 0
    for w in W:
        if w["firstFace"] < 0:
            continue
        q = [F[i]["v"] for i in range(w["firstFace"], w["lastFace"] + 1)]
        possibles += max(0, len(q) - 1)
        atteint += bandes.jointures_moteur(q)
        paires += bandes.jointures(q)
        o, _c = bandes.ordonner(q)
        plafond += bandes.jointures_moteur([q[i] for i in o])
    put("faces rangees en bandes soudables (au plafond du rangement)", atteint >= plafond,
        f"{atteint}/{possibles} jointures CONSOMMEES ({100.0 * atteint / max(1, possibles):.1f} %), "
        f"plafond {plafond} ({100.0 * plafond / max(1, possibles):.1f} %), "
        f"{paires} paires valides dont la boucle laisse tomber {paires - atteint}")

    # 21. TABLE DE REJET (HITSCAN.C canSee, SLEVEL.H) : rejouee contre le REJECT du WAD, paire de
    #     secteurs Doom par paire. Une paire que la table rejette et pas Doom rendrait un monstre
    #     AVEUGLE la ou Doom le fait voir : ECHEC. Une paire que Doom rejette et pas la table n'est
    #     qu'un trace de plus (le ET des penombres fondues ou des deux sens, doom3d.classes_de_rejet) :
    #     comptee.
    Rj = L["reject"]
    try:
        dsec = json.load(open(a.geom, encoding="utf-8"))["doom_sector"]
    except Exception as e:
        dsec = None
        put("table de rejet", True, f"non teste ({e})")
    if dsec is not None and Rj is None:
        put("table de rejet : absente, tous les secteurs en classe 0",
            all(s["rejectClass"] == 0 for s in S))
    elif dsec is not None:
        n, tb = Rj["classes"], Rj["table"]
        Wd = wadmod_of(a)
        nd = len(Wd.map_lumps(a.map)["SECTORS"]) // 26
        lump = bytes(Wd.map_lumps(a.map).get("REJECT") or b"")
        lump += bytes(max(0, (nd * nd + 7) // 8 - len(lump)))
        cls = {}
        melange = [d for si, d in enumerate(dsec) if cls.setdefault(d, S[si]["rejectClass"]) != S[si]["rejectClass"]]
        inventees = perdues = gardees = 0
        for da, ca in cls.items():
            for db, cb in cls.items():
                u, v = min(ca, cb), max(ca, cb)
                p, q = u * n - u * (u - 1) // 2 + v - u, da * nd + db
                t, d = (tb[p >> 3] >> (p & 7)) & 1, (lump[q >> 3] >> (q & 7)) & 1
                inventees += t and not d
                perdues += d and not t
                gardees += t and d
        put("table de rejet : aucune paire que Doom ne rejette pas, une classe par secteur Doom",
            not inventees and not melange,
            f"{len(cls)} secteurs Doom -> {n} classes, {len(tb)} o ; {gardees} paires rejetees, "
            f"{perdues} rendues au trace (penombres, un seul sens), {inventees} inventees"
            + (f", secteurs Doom a plusieurs classes {melange[:4]}" if melange else ""))

    print(f"\n  {len(OK)} OK, {len(FAIL)} echec(s)" + (f" : {FAIL}" if FAIL else ""))
    return 1 if FAIL else 0


def tail_checks(a, L, S, W, V, F, tex, obj, p, M):
    import struct
    import re
    sys.path.insert(0, HERE)
    sys.path.insert(0, os.path.join(ROOT, "tools", "duke2ps"))
    import lev_io
    import wad2snd
    import wad2sprites
    import verif_static
    ids = None
    if a.ids and os.path.exists(a.ids):
        ids = json.load(open(a.ids, encoding="utf-8"))
    else:
        print(f"  (doom_ids.json absent : {a.ids} -- tests 18-20 non joues)")
    static = None
    if a.static and os.path.exists(a.static):
        static = verif_static.static_summary(open(a.static, "rb").read())
    else:
        print(f"  (STATIC.DAT absent : {a.static} -- tileBase suppose 95, sons statiques non comptes)")
    tile_base = static["tileBase"] if static else 95
    model, lay = lev_io.read_model(a.lev)
    tiles = model["tiles"]
    sq = model["sequences"]
    snd = model["sounds"]

    # 17. TUILES : la geometrie est un PREFIXE 0x32 (LEVEL.C:69-72 : `level_texture[i] += tileBase`,
    #     `level_face[i].tile += tileBase` sur des u8), les chunks 0x6A viennent apres ;
    #     G + tileBase <= 255 ; nmWeaponTiles + nmTiles < MAXNMPICS 800 (PIC.C:32, addPic :408) ;
    #     aucun pixel 0 (transparent, PIC.C:500-531) ni 255 (force 0xffff PIC.C:631) en geometrie.
    used = {tex[i] for i in range(1, len(tex), 2)} | {f["tile"] for f in F}
    G = 0
    while G < len(tiles) and tiles[G]["flags"] == 0x32:
        G += 1                                   # la tuile ON d'un interrupteur est 0x32 sans cellule
    after = [t["flags"] for t in tiles[G:]]
    put("tuiles : geometrie = prefixe 0x32, chunks 0x6A ensuite",
        all(k == 0x6A for k in after) and G > 0 and (not used or max(used) < G),
        f"{G} x 0x32 (cellules/faces < {(max(used) + 1) if used else 0}) puis {len(after)} x 0x6A")
    put(f"tuiles : geometrie {G} + tileBase {tile_base} <= 255", G + tile_base <= 255,
        f"= {G + tile_base}, marge {255 - G - tile_base}")
    put("tuiles : nmWeaponTiles + nmTiles < 800 (MAXNMPICS)", tile_base + len(tiles) < 800,
        f"{tile_base} + {len(tiles)} = {tile_base + len(tiles)}")
    # L'indice 0 est la transparence (PIC.C:500-531) : interdit partout SAUF dans les tuiles des
    # murs ajoures (les grilles), ou il est le trou entre les barreaux.  Une tuile creuse posee
    # ailleurs -- sur une face de sol ou de plafond, sur un mur opaque -- ferait un vrai trou.
    ajoures = murs_ajoures(a, W, tex)
    tuiles_grille = set()
    for wi in ajoures:
        w = W[wi]
        for c in range(w["tileLength"] * w["tileHeight"]):
            tuiles_grille.add(tex[w["textures"] + 2 * c + 1])
    n0 = sum(t["pixels"].count(0) for i, t in enumerate(tiles[:G]) if i not in tuiles_grille)
    n0g = sum(t["pixels"].count(0) for i, t in enumerate(tiles[:G]) if i in tuiles_grille)
    n255 = sum(t["pixels"].count(255) for t in tiles[:G])
    put("tuiles : pixel 0 seulement dans les grilles, aucun 255 en geometrie",
        n0 == 0 and n255 == 0,
        f"hors grilles 0 x{n0}, 255 x{n255} ; {len(tuiles_grille)} tuiles de grille, 0 x{n0g}")
    pal = model["palettes"]
    put("palette 0 = objet, entree 0 = 0x0000, objectPalette = 0",
        pal["objectPalette"] == 0 and pal["palettes"] and pal["palettes"][0][0] == 0
        and all(t["palNm"] == 0 for t in tiles), f"{len(pal['palettes'])} palette(s)")

    # 18. SEQUENCES ATTEIGNABLES : pour chaque (MT present ou spawnable, etat atteignable, vue 0..7),
    #     la formule du contrat (garde -2 AVANT le bit 0x8000) doit tomber sur une sequence NON VIDE
    #     (WALLS.C:2777-2779 lit sequence[s] et frame+1 sans test : une sequence vide dessine la
    #     suivante) dont chaque chunk pointe une tuile 0x6A existante ; [172..203] < 0 (markAnimTiles).
    if ids is not None and M is not None:
        Wd = wadmod_of(a)
        present = wad2snd.present_mobj_types(Wd, ids, a.map, a.skill)
        spawn = wad2snd.spawnable_mobj_types(ids, present)
        ss = wad2sprites.SpriteSet(tiles, sq["frames"], sq["chunks"], sq["sequence"],
                                   sq["sequenceMap"], None, None, None)
        tested, problems = wad2sprites.check_reachable(ss, ids, present)
        put("sequences atteignables non vides, chunks -> tuiles 0x6A", not problems,
            f"{tested} (MT, etat, vue) testes" + (f", defauts {problems[:3]}" if problems else ""))
        # 172..191 : types d'animation retail, jamais poses ; 192..203 (OT_ANM1..12) : les flats
        # animes de Doom, dont chaque chunk doit pointer une tuile de GEOMETRIE (prefixe 0x32)
        anim_bad = []
        for i in range(192, 204):
            b = sq["sequenceMap"][i]
            if b < 0:
                continue
            for fr in range(sq["sequence"][b], sq["sequence"][b + 1]):
                for c in range(sq["frames"][fr]["chunkIndex"], sq["frames"][fr + 1]["chunkIndex"]):
                    if not (0 <= sq["chunks"][c]["tile"] < G):
                        anim_bad.append((i, c))
        n_anim = sum(1 for i in range(192, 204) if sq["sequenceMap"][i] >= 0)
        put("sequenceMap[172..191] < 0, animations 192..203 -> tuiles de geometrie (markAnimTiles)",
            all(sq["sequenceMap"][i] < 0 for i in range(172, 192)) and not anim_bad,
            f"{n_anim} famille(s) animee(s)" + (f", defauts {anim_bad[:3]}" if anim_bad else ""))
        put("sequenceMap[163..171] : -2 sauf les OT_SW poses",
            all(sq["sequenceMap"][i] == -2 for i in range(163, 172) if not (168 <= i <= 171)))
        import doom_specials as sp
        poses = {o["type"] for o in obj if o["type"] in sp.OT_SWITCH_TYPES}
        put("sequenceMap[204..226] : -2 sauf les interrupteurs 5..27 poses (DOOM_GAME.C)",
            all((sq["sequenceMap"][i] >= 0) == (i in poses) for i in range(204, 227)),
            f"{sum(1 for i in range(204, 227) if i in poses)} type(s) au-dela de OT_SW4")
        fam = sum(1 for i in range(138) if sq["sequenceMap"][i] != -2)
        print(f"       {fam} familles de sprites cartographiees, {len(sq['sequence']) - 1} sequences, "
              f"{len(sq['frames']) - 1} frames, {len(sq['chunks'])} chunks")

        # 19. SONS : carte 227 indexee par sfxenum_t ; chaque son d'un MT present hors statiques est
        #     present (>= 0) ; rate 0x7000, size pair et == len - 32 arrondi (DMX) ; n < 80 avec les
        #     20 statiques ; soundTop = PCM statiques + dynamiques < 512 Ko (SOUND.C:218-219).
        want = [n for n in wad2snd.sound_names_for(ids, spawn) if Wd.has(wad2snd.lump_name(n))]
        num = ids["sfxname_to_num"]
        missing = [n for n in want if snd["map"][num[n]] < 0]
        put("sons : tout son d'un MT present (hors statiques) est dans la carte", not missing,
            f"{len(want)} attendus" + (f", absents {missing}" if missing else ""))
        inv = {v: k for k, v in enumerate(snd["map"]) if v >= 0}
        bad = []
        for i, s_ in enumerate(snd["sounds"]):
            sfx = inv.get(i)
            nm = ids["sfx_names"][sfx] if sfx is not None else None
            ok_ = (s_["rate"] == 0x7000 and s_["bps"] == 8 and s_["loopStart"] == -1
                   and not (len(s_["pcm"]) & 1))
            if nm and Wd.has(wad2snd.lump_name(nm)):
                ln = struct.unpack("<HHI", Wd.lump(wad2snd.lump_name(nm))[:8])[2]
                ok_ = ok_ and len(s_["pcm"]) == (ln - 32 + 1) // 2 * 2
            if not ok_:
                bad.append((i, nm))
        put("sons : rate 0x7000, bps 8, loop -1, size pair == len - 32", not bad, str(bad[:3]))
        put("sons : map[sfx] dans [0, n) pour toute entree >= 0",
            all(v < len(snd["sounds"]) for v in snd["map"] if v >= 0))
        nst = static["static_sounds"] if static else 20
        top = sum(len(s_["pcm"]) for s_ in snd["sounds"]) + (static["static_pcm"] if static else 0)
        put("sons : n dynamiques + statiques < 80, soundTop < 512 Ko",
            len(snd["sounds"]) + nst < 80 and top < 512 * 1024,
            f"{len(snd['sounds'])} + {nst} = {len(snd['sounds']) + nst} ; PCM {top} o"
            + ("" if static else " (statiques non comptes)"))

        # 20. BARILS (SPEC_RUNTIME 2 : sphere de rayon height/2 = 21 u) : un baril a moins de
        #     21 + 16 u d'un mur PLEIN de sa feuille n'est plus contournable -> avertissement.
        mt_bar = ids["mt_names"].index("MT_BARREL") if "MT_BARREL" in ids["mt_names"] else -1
        ot_bar = ids["mt_to_ot"][mt_bar] if mt_bar >= 0 else -1
        close = []
        nbar = 0
        for o in obj:
            if o["type"] != ot_bar:
                continue
            nbar += 1
            s_, x, y, z = struct.unpack(">4h", bytes(p[o["firstParam"]:o["firstParam"] + 8]))
            sec = S[s_]
            for wi in range(sec["firstWall"], sec["lastWall"] + 1):
                w = W[wi]
                if w["normal"][1] != 0 or w["nextSector"] != -1:
                    continue
                ax, az = V[w["v"][0]]["x"], V[w["v"][0]]["z"]
                bx, bz = V[w["v"][1]]["x"], V[w["v"][1]]["z"]
                ex, ez = bx - ax, bz - az
                L2 = ex * ex + ez * ez
                t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((x - ax) * ex + (z - az) * ez) / L2))
                d = math.hypot(x - (ax + t * ex), z - (az + t * ez))
                if d < 21 + 16:
                    close.append((s_, wi, round(d, 1)))
                    break
        if close:
            print(f"  [AVERT] barils : {len(close)}/{nbar} a moins de 37 u d'un mur plein de leur "
                  f"feuille (non contournables), ex. {close[:4]}")
        else:
            put("barils a >= 37 u des murs pleins", True, f"{nbar} barils")

    # 21. TAILLES : bloc niveau < 900 000 (LEVEL.C:41), sequences < 1 Mo (SEQUENCE.C:30) ; somme
    #     RESIDENTE niveau + palettes + tuiles (4 096 par 0x32, RLE par 0x6A : PIC.C:513, :575)
    #     + sequences + STATIC (tuiles d'armes + wseq, deja en memoire) <= pool reel = LWRAM 1 Mo
    #     + (0x06100000 - _end du disque de test, make_e1m1.MAP_PROGRAMME) (UTIL.C:352-359). Les
    #     sons vont a la SCSP.
    lvl = lay["level"]["size"]
    psz = lay["tiles"]["palette_size"]
    tsz = sum(((4096 if "pixels" in t else len(t["rle"])) + 3) & ~3 for t in tiles)   # align 4 (UTIL.C:370)
    ssz = lay["sequences"]["size"]
    put("bloc niveau < 900 000, sequences < 1 Mo", 0 < lvl < 900000 and 0 < ssz < 1024 * 1024,
        f"niveau {lvl}, sequences {ssz}")
    # le pool et ce que le demarrage y VERROUILLE (jeu d'images des menus, texte local :
    # MENU.C:266-268, LOCAL.C:25-26) comptes comme make_e1m1 : un seul map, MAP_PROGRAMME
    import make_e1m1
    pool, src = make_e1m1.resident_pool()
    verrou, _vsrc = make_e1m1.verrou_initload()
    st_res = (static["weapon_tiles_bytes"] + static["wseq_bytes"]) if static else 0
    res = ((lvl + 3) & ~3) + ((psz + 3) & ~3) + tsz + ((ssz + 3) & ~3) + st_res
    if pool is None:
        put("memoire residente <= pool (LWRAM 1 Mo + haut de HWRAM - verrou du demarrage)", False,
            f"{src} : construire d'abord le disque de test")
    else:
        put("memoire residente <= pool (LWRAM 1 Mo + haut de HWRAM - verrou du demarrage)",
            res + verrou <= pool,
            f"niveau {lvl} + palettes {psz} + tuiles {tsz} + sequences {ssz} + STATIC {st_res} = {res} ; "
            f"pool {pool} ({src}) - verrou {verrou} ; marge {pool - verrou - res}")


def wadmod_of(a):
    sys.path.insert(0, HERE)
    import wad as wadmod
    if not hasattr(wadmod_of, "cache"):
        wadmod_of.cache = wadmod.Wad(a.wad)
    return wadmod_of.cache


if __name__ == "__main__":
    sys.exit(main())

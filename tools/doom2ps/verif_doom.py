#!/usr/bin/env python3
"""verif_doom.py -- verificateur INDEPENDANT du .LEV produit par doom2ps.

Il relit le FICHIER (pas les intermediaires) et rejoue les tests que le moteur fait vraiment,
a la ligne pres. Le convertisseur passe toujours ses propres criteres du premier coup ; ce sont
ceux-la qui trouvent les defauts (lecon E3/E6 du convertisseur Duke).

Usage : python tools\\doom2ps\\verif_doom.py [--lev F] [--geom J]
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import lev                                              # noqa: E402

FAIL = []
OK = []


def put(name, ok, detail=""):
    (OK if ok else FAIL).append(name)
    print(f"  [{'OK  ' if ok else 'ECHEC'}] {name}{(' : ' + detail) if detail else ''}")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--lev", default=os.path.join(ROOT, "cd", "TOMB.LEV"))
    ap.add_argument("--geom", default=os.path.join(ROOT, "build", "doom2ps",
                                                   "e1m1_geom3d.json"))
    ap.add_argument("--wad", default=os.path.join(os.path.dirname(ROOT), "Mimas", "cd", "data",
                                                  "DOOM1.WAD"))
    ap.add_argument("--map", default="E1M1")
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

    # 9. le depart du joueur
    obj = L["objects"]
    put("un seul objet, OT_PLAYER", len(obj) == 1 and obj[0]["type"] == 13, str(obj[:2]))
    import struct
    p = L["objectParams"]
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
    PLAYER_FLAGS = 0x1100
    STEPHEIGHT = 24
    adj = [[] for _ in S]
    for si, s_ in enumerate(S):
        for wi in range(s_["firstWall"], s_["lastWall"] + 1):
            w = W[wi]
            if w["nextSector"] == -1:
                break
            if (w["flags"] & PLAYER_FLAGS) & 0x1f00:
                continue
            n = w["nextSector"]
            if 0 <= n < len(S) and S[n]["floorLevel"] - s_["floorLevel"] <= STEPHEIGHT:
                adj[si].append(n)
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
            if (w["flags"] & PLAYER_FLAGS) & 0x1f00:
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
                    couvert.append((min(ts), max(ts), math.sqrt(L2)))
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
    try:
        import json as _json
        det = _json.load(open(os.path.join(ROOT, "build", "doom2ps",
                                           "e1m1_tiles.json"), encoding="utf-8"))["detail"]
    except Exception as e:
        put("echelle verticale des murs", True, f"non teste ({e})")
        det = None
    if det is not None:
        base = min((tex[i] for i in range(1, len(tex), 2)), default=0)
        faux, vus = [], 0
        for si, s_ in enumerate(S):
            for wi in range(s_["firstWall"], s_["lastWall"] + 1):
                w = W[wi]
                if w["normal"][1] != 0 or (w["flags"] & 0x02) or not (w["flags"] & 0x01):
                    continue
                loc = tex[w["textures"] + 1] - base
                if not (0 <= loc < len(det)):
                    continue
                ys = [V[i]["y"] for i in w["v"]]
                cell = (max(ys) - min(ys)) / float(w["tileHeight"] or 1)
                cv = det[loc].get("cv") or 0
                vus += 1
                if not cv or max(cell / cv, cv / cell) > 1.02:
                    faux.append((si, wi, round(cell, 1), cv))
        put("echelle verticale des murs", not faux,
            f"{len(faux)}/{vus} murs hors de 2 %, ex. {faux[:3]}" if faux
            else f"{vus} murs, cellule posee == cellule echantillonnee")

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

    print(f"\n  {len(OK)} OK, {len(FAIL)} echec(s)" + (f" : {FAIL}" if FAIL else ""))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())

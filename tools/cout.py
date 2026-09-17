#!/usr/bin/env python3
"""cout.py -- la CARTE DE COUT du niveau, commune aux deux convertisseurs.

CE QU'ELLE AJOUTE. Les criteres de conversion disent si le niveau est LEGAL ; aucun ne dit ce
qu'il COUTERA. Or le calcul cher est deja fait : `ordre.visibilite` enumere les positions debout
et ce qu'on y voit, pour les paires d'ordre. Multiplier ce qui est visible par la loi mesuree sur
console donne, sans console et sans emulateur, une carte du pire cas par position -- donc l'endroit
ou depenser, au lieu de le deviner.

LA LOI (docs/STEXT_BASELINE_2026-09-12.md, 5 captures STATUSTEXT sur console, build ASSERT) :
    calc ~= 14,9 ms + 39,2 us x cellules ; paliers 470 = 30 fps, 896 = 20 fps, 1321 = 15 fps
`cellules` est `nmPolys + nmSlavePolys` : les cellules de murs et les faces de sols et plafonds,
SANS les sprites. C'est exactement ce qui est compte ici.

⚠ LA PENTE MOYENNE N'EST PAS LA MARGE. 39,2 us situe une position sur l'echelle ; elle ne dit pas
ce que rend une cellule RETIREE, qui vaut 24 us, et 12-18 pour une face de plat (une face ne paie
ni la preparation de son mur ni ses 4 sommets : `normTransform` transforme la plage une fois par
MUR, WALLS.C:1210-1212). Chiffrer un gain au tarif moyen le surestime du double.

⚠ CE QUE LA CARTE MAJORE. L'inondation d'`ordre` est a 360 degres et ne modelise pas l'occlusion
par la geometrie : elle voit tout ce qui est atteignable en tournant sur soi-meme. D'ou deux
colonnes : `tour`, majorant vrai et sans hypothese, et `cone`, le pire cap parmi CAPS a FOV
degres. Le FOV est un PARAMETRE de l'estimation, pas une constante du moteur re-derivee ici : le
moteur clippe les polygones projetes (`clip_visible`, WALLS.C:177), il n'a aucun angle en dur.

GENERIQUE : ne lit que (S, W) et la sortie de `ordre.visibilite`, donc les deux convertisseurs
l'utilisent tel quel.
"""
import math

import ordre

BASE_MS = 14.9          # ms : le fixe de la loi (traversee, tic, sprites, arme, overlay)
US_CELLULE = 39.2       # us : la pente MOYENNE, pour situer une position
US_MARGINAL = 24.0      # us : ce que rend UNE cellule retiree (12-18 pour une face de plat)
PALIERS = ((470, 30), (896, 20), (1321, 15))
FOV = 53.0              # degres : l'angle suppose du cone de vue
CAPS = 8                # caps echantillonnes par position ; on garde le PIRE


def cellules_par_secteur(S, W):
    """Ce qu'un secteur coute au peintre : ses cellules de mur (grille, flag 0x01) plus ses faces
    de sol et de plafond (maillage). Les deux familles sont DISJOINTES (verifie sur E1M1 : 565
    murs grille, 437 murs maillage, 0 des deux), donc la somme ne double compte rien."""
    n = [0] * len(S)
    for si, s in enumerate(S):
        t = 0
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            if w["flags"] & 0x01:
                t += w["tileLength"] * w["tileHeight"]
            if w["firstFace"] >= 0:
                t += w["lastFace"] - w["firstFace"] + 1
        n[si] = t
    return n


def carte(S, W, geo, vues, fov=FOV, caps=CAPS):
    """-> (positions, stats).

    positions = [(ex, ez, s0, nm_secteurs, cel_tour, cel_cone)], une par position debout.
    `cel_cone` est le PIRE cap de la position : c'est la vue qu'il faut tenir, pas la moyenne."""
    pts = geo[0]
    cel = cellules_par_secteur(S, W)
    demi = math.radians(fov) / 2.0
    positions = []
    for ex, ez, s0, vis, _dist in vues:
        tour = sum(cel[s] for s in vis)
        arcs = [(cel[s], ordre._arc(pts, ex, ez, s)) for s in vis]
        pire = 0
        for k in range(caps):
            h = 2 * math.pi * k / caps
            c = sum(n for n, a in arcs if a is not None and ordre._croise((h - demi, h + demi), a))
            if c > pire:
                pire = c
        positions.append((ex, ez, s0, len(vis), tour, pire))
    return positions, _stats(positions, len(S), sum(cel))


def _quantiles(v):
    if not v:
        return (0, 0, 0, 0)
    t = sorted(v)
    n = len(t)
    return (t[0], t[n // 2], t[min(n - 1, int(0.9 * n))], t[-1])


def _stats(positions, nm_secteurs, cel_total):
    tour = [p[4] for p in positions]
    cone = [p[5] for p in positions]
    pire = max(positions, key=lambda p: p[5]) if positions else None
    return dict(
        positions=len(positions),
        secteurs=nm_secteurs,
        cellules_niveau=cel_total,
        visites=_quantiles([p[3] for p in positions]),
        tour=_quantiles(tour),
        cone=_quantiles(cone),
        au_dessus={seuil: sum(1 for c in cone if c > seuil) for seuil, _fps in PALIERS},
        pire=(None if pire is None else dict(x=pire[0], z=pire[1], secteur=pire[2],
                                             visibles=pire[3], cellules=pire[5])),
    )


def millisecondes(cellules):
    """La loi, pour situer -- jamais pour chiffrer un gain (voir l'avertissement du module)."""
    return BASE_MS + US_CELLULE * cellules / 1000.0


def resume(st):
    """La ligne que les deux convertisseurs impriment."""
    if not st["positions"]:
        return "aucune position debout"
    _mn, med, p90, mx = st["cone"]
    sur = ", ".join("%d > %d (%d fps)" % (st["au_dessus"][s], s, f) for s, f in PALIERS
                    if st["au_dessus"][s])
    p = st["pire"]
    return ("%d positions, %d cellules au total ; par position, cone %.0f deg : mediane %d, "
            "p90 %d, max %d (~%.1f ms)%s ; pire vue en (%d, %d) secteur %d, %d secteurs visibles"
            % (st["positions"], st["cellules_niveau"], FOV, med, p90, mx, millisecondes(mx),
               ("" if not sur else " -- " + sur), p["x"], p["z"], p["secteur"], p["visibles"]))

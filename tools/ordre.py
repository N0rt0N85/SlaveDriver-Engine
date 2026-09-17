#!/usr/bin/env python3
"""ordre.py -- les PAIRES D'ORDRE du peintre, communes aux deux convertisseurs.

LE DEFAUT. `buildTree` (WALLS.C:2600-2616) ne tire une contrainte d'ordre que des PORTAILS tournes
vers l'oeil. Deux secteurs qui n'en partagent aucun -- les deux cotes d'une colonne, d'un caisson,
d'un pilier -- n'en recoivent AUCUNE : seul le scalaire `distance` les separe, et ce scalaire est la
distance au portail d'entree le plus proche (WALLS.C:2334), pas la profondeur. Autour d'un obstacle
les deux secteurs sont atteints par des portes equidistantes, donc l'ordre est un pile ou face.
MESURE E1M1 (16-09, console) : le sol et le tonneau visibles A TRAVERS le mur d'une colonne ; un
cadavre a travers un caisson ; le fond de la salle tech par-dessus le U qui le cache.

LE REMEDE. Une liste clairsemee de paires (a, b, mur separateur). Le moteur la parcourt apres la
boucle des portails et ajoute une arete dans LE MEME graphe, donc son tri topologique la compose
avec tout le reste -- contrairement aux `cutPlane` du moteur, qui deplacent un secteur APRES COUP
(WALLS.C:2636-2685) et defont plus qu'ils ne reparent.

CHIFFRES (E1M1, 1 089 positions debout, simulation fidele de la boucle de dessin) :
    etat actuel ............................... 68 % des positions ont une paire mal ordonnee
    liste clairsemee, sans `--optim fusion` ... 193 entrees (1 162 o), 13 %
    ... la meme, fusion active (le defaut) .... 179 entrees (1 078 o),  6 %
    TOUTES les paires qui se chevauchent ...... 7 239 entrees (43 Ko), 86 %  <-- PIRE QUE RIEN
MESURE Duke (TOMB, meme calcul) : 79 % -> 38 % pour 552 entrees (3 316 o) ; toutes les paires
(14 109 entrees, 85 Ko) rendent 70 %, la aussi bien pire que la liste courte.

⚠ CLAIRSEME N'EST PAS UN COMPROMIS, C'EST LA BONNE REPONSE. Une version anterieure de ce fichier
annoncait un « plafond theorique a 3 % pour 8 354 entrees » : il ne se reproduit pas, et la mesure
du 18-09 dit l'inverse. La cause n'est PAS le plafond `ancestor[MAXFANIN]` -- temoin joue
(tools/study/balayage_ordre.py) : desserre a 100 000, la table complete rend 86 % au lieu de
85,7 %, donc les 522 862 aretes refusees n'y sont pour rien. C'est que la table porte UN plan par
paire pour TOUS les points de vue : quelques paires bien choisies sont rarement actives ensemble,
des milliers le sont toujours, elles bouclent, et le moteur casse les boucles a la distance.
Contraindre plus, c'est contraindre faux.

⚠ ET LES TROIS REGLAGES SONT AU PLAFOND, mesures un par un le 18-09 :
  TOURS  : 1 -> 9,4 %, 2 -> 8,1 %, 4 -> 5,8 %, 8 et 16 -> 5,8 % ; sur TOMB, strictement rien
           apres le 1er tour. Le 4 actuel est deja le palier.
  BUDGET : sur E1M1, 142 paires SEULEMENT sont jamais fautives a vide ; les contraindre toutes
           donne 9,4 %, moins bien que les 179 que l'iteration trouve. Sur TOMB la courbe
           100 / 200 / 400 / 552 entrees donne 56 / 52 / 41 / 38 % puis s'arrete faute de
           candidates. Des octets en plus n'achetent rien.
  PAS    : 32 u au lieu de 64 donne une table PLUS GROSSE et MOINS BONNE sur les deux grilles
           (voir la constante). La grille fine ne sert qu'a dire la verite : 8 %, pas 6 %.
Le residu se rachete donc sur la GEOMETRIE, pas sur la table -- c'est exactement ce que fait la
fusion (13 % -> 6 %) en retirant des secteurs, donc des paires que rien ne decide (182 -> 155).

PREREQUIS : la rupture de cycle par la DISTANCE (WALLS.C:2826). Les aretes ajoutees creent des
cycles ; les casser par le nombre d'enfants, comme le faisait le moteur, fait DIVERGER la table
(193 -> 2 515 entrees et 13 % -> 31 %). Les deux changements ne valent que pris ensemble.

GENERIQUE : ne lit que (S, W, V) -- secteurs, murs, sommets du bloc niveau. Le JSON de geom3d.py et
le .LEV portent les memes noms de champs, donc le convertisseur Duke rejoue ceci tel quel.
"""
import math
from collections import Counter, defaultdict

PAS = 64.0          # grille des points de vue, en unites monde. NE PAS L'AFFINER : a 32 u, la
                    # table calculee est plus grosse (260 entrees contre 179) et MOINS BONNE sur
                    # les DEUX grilles -- 11,9 % contre 8,1 % sur la grille fine, 11,6 % contre
                    # 5,8 % sur la grille large -- pour 5 fois le temps de conversion. Le seul
                    # service que rend la grille fine est de DIRE la verite : la table livree
                    # laisse 8 % des positions fautives, pas 6 %.
DECALAGE = 13.7     # ... decalee d'un irrationnel : les murs sont sur des coordonnees rondes, et un
                    # oeil DANS le plan d'un mur fait basculer le signe du test de cote
TOURS = 4           # ajouter une contrainte change l'ordre, donc peut reveler d'autres paires
MARGE = 1.0         # u : en deca, l'oeil est trop pres du plan pour que son cote soit sur
MAXFANIN = 20       # WALLS.C:87 -- ancestor[] est de taille fixe, on ne le fait pas deborder

# u : de combien le mur separateur le mieux place laisse encore l'autre secteur DU MAUVAIS COTE.
# Une paire au-dela n'a pas de plan qui la decide, et lui en donner un quand meme pose une arete
# FAUSSE pour une partie des points de vue -- or les aretes fausses font des cycles, et les cycles
# sont casses arbitrairement. Mieux vaut ne rien dire que dire faux : la paire est comptee
# `indecidables` et laissee au hasard, qui est ce qu'elle avait deja.
#
# MESURE 18-09, et elle CONTREDIT ce pour quoi le garde avait ete ecrit : le plan separateur est
# presque toujours excellent. Sur les 8 238 paires d'E1M1 qui se chevauchent a l'ecran, le
# depassement median vaut -520 u (largement du bon cote), le p90 -56 u, le MAXIMUM 0,00 u -- aucune
# paire ne franchit 1 u. Sur les 14 127 de TOMB, 18 paires (0,1 %) depassent 1 u, au pire de 121 u.
# Le garde reste, parce qu'une paire sans plan doit etre DITE et non silencieusement rangee parmi
# les bonnes, mais il n'explique RIEN du residu : ni les 6 % de Doom, ni les 38 % de Duke.
SEUIL_DEPASSEMENT = 1.0


def _polygones(S, W, V):
    """-> (pts, murs, portes, anneaux) par secteur.

    ATTENTION : les murs d'un secteur sont ranges PORTAILS EN TETE (le `break` de buildTree en
    depend), donc PAS dans l'ordre geometrique. L'anneau doit etre rechaine par ses segments,
    sinon le polygone zigzague et tout test d'appartenance echoue (MESURE : 603 points localises
    sur 3 109)."""
    pts, murs, portes, anneaux = {}, {}, {}, {}
    for si, s in enumerate(S):
        P, M, T, segs = [], [], [], []
        f = s["firstWall"]
        for wi in range(f, s["lastWall"] + 1):
            w = W[wi]
            if w["normal"][1] != 0:
                continue                        # sol ou plafond
            a, b = V[w["v"][0]], V[w["v"][1]]
            pa, pb = (a["x"], a["z"]), (b["x"], b["z"])
            P.append(pa)
            P.append(pb)
            segs.append((pa, pb))
            nx, nz = w["normal"][0] / 65536.0, w["normal"][2] / 65536.0
            if wi - f < 0x80:
                M.append((wi - f, nx, nz, a["x"], a["z"]))
            T.append((w["nextSector"], w["flags"], nx, nz,
                      a["x"], a["z"], b["x"], b["z"]))
        suite = {}
        for pa, pb in segs:
            suite.setdefault(pa, pb)
        anneau = []
        if segs:
            depart = segs[0][0]
            u = depart
            for _ in range(len(segs) + 1):
                anneau.append(u)
                u = suite.get(u)
                if u is None or u == depart:
                    break
            if u != depart or len(anneau) < 3:
                anneau = []                     # anneau non ferme : on ne localise pas ici
        pts[si], murs[si], portes[si], anneaux[si] = P, M, T, anneau
    return pts, murs, portes, anneaux


def _visible(portes, ex, ez, s0, maxpop=20000):
    """findVisible : inondation par les portails en RETRECISSANT l'intervalle angulaire, comme le
    doorwayCache du moteur. Le `break` sur un mur solide reproduit `if (adjoin==-1) break`."""
    vis = {s0}
    dist = {s0: 0.0}
    vus = defaultdict(list)
    pile = [(s0, None)]
    pops = 0
    while pile and pops < maxpop:
        s, iv = pile.pop()
        pops += 1
        for adj, fl, nx, nz, ax, az, bx, bz in portes[s]:
            if adj < 0:
                break
            if fl & 0x08:                        # WALLFLAG_BLOCKSSIGHT
                continue
            if nx * (ex - ax) + nz * (ez - az) <= 0:
                continue                         # normale opposee a l'oeil
            a0 = math.atan2(az - ez, ax - ex)
            a1 = math.atan2(bz - ez, bx - ex)
            d = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
            lo, hi = (a0, a0 + d) if d > 0 else (a0 + d, a0)
            if iv is not None:
                lo2 = max(lo, iv[0] - 2 * math.pi * round((iv[0] - lo) / (2 * math.pi)))
                hi2 = min(hi, iv[1] - 2 * math.pi * round((iv[1] - hi) / (2 * math.pi)))
                if hi2 - lo2 <= 1e-6:
                    continue
                lo, hi = lo2, hi2
            dx, dz = bx - ax, bz - az
            L = dx * dx + dz * dz
            t = 0.0 if L < 1e-9 else max(0.0, min(1.0, ((ex - ax) * dx + (ez - az) * dz) / L))
            dist[adj] = min(dist.get(adj, 1e18),
                            math.hypot(ex - (ax + t * dx), ez - (az + t * dz)))
            if any(x0 - 1e-9 <= lo and hi <= x1 + 1e-9 for x0, x1 in vus[adj]):
                continue
            vus[adj].append((lo, hi))
            vis.add(adj)
            pile.append((adj, (lo, hi)))
    return vis, dist


def _arc(pts, ex, ez, si):
    """Arc minimal contenant le secteur, vu de l'oeil -> (lo, hi)."""
    A = sorted(math.atan2(z - ez, x - ex) for x, z in pts[si])
    n = len(A)
    if n < 2:
        return None
    trous = [(A[(i + 1) % n] - A[i]) % (2 * math.pi) for i in range(n)]
    g = max(range(n), key=lambda i: trous[i])
    lo = A[(g + 1) % n]
    return (lo, lo + 2 * math.pi - trous[g])


def _croise(i1, i2):
    if i1 is None or i2 is None:
        return False
    for k in (-2, -1, 0, 1, 2):
        if max(i1[0], i2[0] + 2 * math.pi * k) < min(i1[1], i2[1] + 2 * math.pi * k):
            return True
    return False


def _separateur(pts, murs, a, b):
    """Le mur de `a` qui laisse le MIEUX tout `b` du cote exterieur -> (offset, depassement).

    Les normales pointent VERS L'INTERIEUR du secteur, donc `depassement` est le plus grand
    n . (p - p0) sur les sommets de `b` : negatif, tout `b` est dehors et le plan separe vraiment ;
    positif, une part de `b` deborde du mauvais cote et le plan ne decide plus.

    On minimise le depassement au lieu d'exiger zero : deux morceaux qui se touchent par une arete
    courte sont presque tangents a son plan, et l'arrondi des normales en 16.16 fait basculer le
    signe. Ca reste juste tant que le depassement reste sous SEUIL_DEPASSEMENT ; au-dela l'appelant
    refuse la paire au lieu de lui inventer un plan."""
    best = None
    for off, nx, nz, px, pz in murs[a]:
        d = max(nx * (x - px) + nz * (z - pz) for x, z in pts[b])
        if best is None or d < best[1]:
            best = (off, d)
    return best


def _plan(pts, murs, a, b):
    """-> (proprietaire, offset, depassement) du mur separateur de la paire (a < b), ou None."""
    oa = _separateur(pts, murs, a, b)
    ob = _separateur(pts, murs, b, a)
    if oa and ob and oa[1] <= ob[1]:
        return (a, oa[0], oa[1])
    if ob:
        return (b, ob[0], ob[1])
    return (a, oa[0], oa[1]) if oa else None


def _ordre_moteur(vis, enfants, dist):
    """La boucle de dessin du moteur (WALLS.C:2810-2862) : feuilles d'abord, tri par distance
    decroissante, rupture de cycle par la PLUS GRANDE distance."""
    reste = {s: len(enfants.get(s, ())) for s in vis}
    peres = defaultdict(list)
    for s in vis:
        for a in enfants.get(s, ()):
            peres[a].append(s)
    feuilles = [s for s in vis if reste[s] == 0]
    dessin, vus = [], set()
    while len(dessin) < len(vis):
        if not feuilles:
            c = [s for s in vis if s not in vus]
            if not c:
                break
            b = max(c, key=lambda s: (dist.get(s, 0.0), -s))
            reste[b] = 0
            feuilles.append(b)
        feuilles.sort(key=lambda s: -dist.get(s, 0.0))
        x = feuilles.pop(0)
        dessin.append(x)
        vus.add(x)
        for p in peres[x]:
            reste[p] -= 1
            if reste[p] == 0 and p not in vus:
                feuilles.append(p)
    return dessin


def visibilite(S, W, V, pas=PAS):
    """Les points de vue du niveau et ce qu'on y voit -> (geo, vues).

    C'EST LE CALCUL CHER, et tout ce qui juge l'image le partage : les paires d'ordre, leur
    verification, et la carte de cout (tools/cout.py). Le calculer une fois et le passer en
    parametre, plutot que de le refaire.

    geo = (pts, murs, portes, anneaux) ; vues = [(ex, ez, s0, vis, dist)].
    Un point de vue est une position DEBOUT : un point de la maille de `pas` u qui tombe dans
    l'ANNEAU d'un secteur. Passer par les feuilles du BSP a la place rend des positions DANS LE
    VIDE (mesure E1M1 : 3 109 au lieu de 1 089) et fausse tous les pourcentages."""
    geo = _polygones(S, W, V)
    pts, murs, portes, anneaux = geo

    def secteur_de(x, z):
        for si, R in anneaux.items():
            if not R:
                continue
            d = False
            n = len(R)
            for k in range(n):
                ax, az = R[k]
                bx, bz = R[(k + 1) % n]
                if (az > z) != (bz > z) and x < ax + (z - az) / float(bz - az) * (bx - ax):
                    d = not d
            if d:
                return si
        return None

    xs = [p[0] for P in pts.values() for p in P]
    zs = [p[1] for P in pts.values() for p in P]
    vues = []
    x = min(xs) + DECALAGE
    while x <= max(xs):
        z = min(zs) + DECALAGE
        while z <= max(zs):
            s0 = secteur_de(x, z)
            if s0 is not None:
                vis, dist = _visible(portes, x, z, s0)
                vues.append((x, z, s0, vis, dist))
            z += pas
        x += pas
    return geo, vues


def _scenes(geo, vues, seuil=SEUIL_DEPASSEMENT):
    """Ce qu'il faut savoir d'un point de vue pour juger son ordre, calcule une fois pour toutes :
    les contraintes que les portails donnent DEJA, et les paires qui se CHEVAUCHENT a l'ecran --
    les seules dont l'ordre se voie.

    -> (scenes, plans, indecidables). `indecidables` = les paires qui se chevauchent mais dont
    aucun mur ne decide a moins de `seuil` : elles ne sont ni contraintes ni comptees fautives,
    donc elles doivent etre DITES, sinon le pourcentage s'ameliore en cachant des defauts."""
    pts, murs, portes, anneaux = geo
    scenes, plans, indecidables = [], {}, set()
    for ex, ez, s0, vis, dist in vues:
        enf = defaultdict(set)
        for s in vis:
            for adj, fl, nx, nz, ax, az, bx, bz in portes[s]:
                if adj < 0:
                    break
                if (fl & 0x08) or adj not in vis:
                    continue
                if nx * (ex - ax) + nz * (ez - az) > 0:
                    enf[s].add(adj)              # adj est derriere : dessine AVANT s
        arcs = {s: _arc(pts, ex, ez, s) for s in vis}
        rel = []
        for a in vis:
            for b in vis:
                if a >= b or not _croise(arcs[a], arcs[b]):
                    continue
                if (a, b) not in plans:
                    plans[(a, b)] = _plan(pts, murs, a, b)
                pl = plans[(a, b)]
                if pl is None or pl[2] > seuil:
                    indecidables.add((a, b))
                    continue
                src, off, _dep = pl
                for o, nx, nz, px, pz in murs[src]:
                    if o == off:
                        d = nx * (ex - px) + nz * (ez - pz)
                        if abs(d) >= MARGE:
                            rel.append((src if d > 0 else (b if src == a else a),
                                        (b if src == a else a) if d > 0 else src))
                        break
        scenes.append((ex, ez, vis, dist, enf, rel))
    return scenes, plans, indecidables


def _juger(scenes, murs, table):
    """Rejoue la boucle de dessin sur chaque scene, `table` en contraintes supplementaires.
    -> (positions fautives, paires fautives, paires a corriger, aretes perdues).

    `ancestor[]` est de taille MAXFANIN dans le moteur et les PORTAILS le remplissent deja
    (WALLS.C:2611) : la table n'a droit qu'a ce qu'ils laissent. Compter le fan-in a partir de
    zero, comme le faisait cette simulation, lui accordait 20 aretes DE PLUS que le moteur n'en
    accepte -- donc elle jugeait une table que la console ne lit pas.

    `fautives` est un COMPTEUR et non un ensemble : combien de positions cassent chaque paire.
    L'iteration n'en lit que les cles, mais le classement sert a savoir ce qu'une table plus
    grosse racheterait (tools/study/balayage_ordre.py)."""
    fautives = Counter()
    n_mauvais = n_pos = n_perdues = 0
    for ex, ez, vis, dist, enf, rel in scenes:
        e2 = defaultdict(set)
        fanin = defaultdict(int)
        for s in enf:
            e2[s] |= enf[s]
            for a in enf[s]:
                fanin[a] += 1                # a gagne un ancetre : c'est `nmAncestors` du moteur
        for (a, b), (src, off) in table.items():
            if a not in vis or b not in vis:
                continue
            for o, nx, nz, px, pz in murs[src]:
                if o == off:
                    d = nx * (ex - px) + nz * (ez - pz)
                    dv = src if d > 0 else (b if src == a else a)
                    dr = (b if src == a else a) if d > 0 else src
                    if fanin[dr] < MAXFANIN:
                        fanin[dr] += 1
                        e2[dv].add(dr)
                    else:
                        n_perdues += 1       # le moteur laisse tomber l'arete, on fait pareil
                    break
        rang = {s: i for i, s in enumerate(_ordre_moteur(vis, e2, dist))}
        n = 0
        for dv, dr in rel:
            if rang[dr] > rang[dv]:
                n += 1
                fautives[(min(dv, dr), max(dv, dr))] += 1
        n_mauvais += n
        if n:
            n_pos += 1
    return n_pos, n_mauvais, fautives, n_perdues


def _decoder(paires):
    """La table telle qu'elle est ECRITE dans le .LEV -> la table interne."""
    return {(a, b): ((b if (pl & 0x80) else a), pl & 0x7f) for a, b, pl in paires}


def paires_d_ordre(S, W, V, pas=PAS, tours=TOURS, trace=None, vu=None,
                   seuil=SEUIL_DEPASSEMENT):
    """-> ([(a, b, plane)], stats). `plane` suit la convention de level_cutPlane : offset du mur
    depuis firstWall de `a`, ou de `b` si le bit 0x80 est mis. `vu` = la sortie de visibilite(),
    quand l'appelant l'a deja calculee."""
    geo, vues = vu if vu is not None else visibilite(S, W, V, pas)
    pts, murs = geo[0], geo[1]
    scenes, plans, indecidables = _scenes(geo, vues, seuil)

    # On part des paires que le tri actuel casse, puis on itere : contraindre une paire change
    # l'ordre, donc peut en reveler d'autres. L'iteration n'est PAS monotone -- sur E1L1 (Duke)
    # elle passe par 38 % au 1er tour puis remonte a 40 % -- donc on GARDE LA MEILLEURE table
    # rencontree, jamais la derniere.
    table = {}
    stats = []
    meilleure = (None, None)            # (score, table)
    for tour in range(tours + 1):
        n_pos, n_mauvais, fautives, n_perdues = _juger(scenes, murs, table)
        stats.append(dict(tour=tour, entrees=len(table), paires=n_mauvais,
                          positions=n_pos, total=len(scenes),
                          indecidables=len(indecidables), perdues=n_perdues))
        if trace:
            trace("    tour %d : %4d entrees -> %5d paires mal ordonnees, %4d/%d positions (%.0f %%)"
                  % (tour, len(table), n_mauvais, n_pos, len(scenes),
                     100.0 * n_pos / max(1, len(scenes))))
        score = (n_pos, n_mauvais, len(table))
        if meilleure[0] is None or score < meilleure[0]:
            meilleure = (score, dict(table))
        neuves = {p for p in fautives if p not in table}
        if not neuves or tour == tours:
            break
        for p in neuves:
            pl = plans.get(p)
            if pl is None:
                pl = _plan(pts, murs, p[0], p[1])
                plans[p] = pl
            if pl is not None and pl[2] <= seuil:
                table[p] = (pl[0], pl[1])

    table = meilleure[1] or {}
    if trace:
        dep = sorted(plans[p][2] for p in table)
        trace("    depassement des %d plans retenus : min %.2f, mediane %.2f, max %.2f u ; "
              "%d paires laissees au hasard faute de plan a moins de %.1f u"
              % (len(dep), dep[0] if dep else 0.0, dep[len(dep) // 2] if dep else 0.0,
                 dep[-1] if dep else 0.0, len(indecidables), seuil))
    sortie = []
    for (a, b), (src, off) in sorted(table.items()):
        sortie.append((a, b, off | (0x80 if src == b else 0)))
    return sortie, stats


def evaluer(S, W, V, paires, pas=PAS, vu=None, seuil=SEUIL_DEPASSEMENT):
    """Le critere des verificateurs : rejoue la boucle de dessin avec la table DU FICHIER.

    Il juge ce qui est LIVRE, pas ce que le convertisseur croit avoir calcule -- une table bien
    calculee et mal serialisee ressort ici, et nulle part ailleurs.
    -> dict(positions, total, paires, indecidables, entrees)."""
    geo, vues = vu if vu is not None else visibilite(S, W, V, pas)
    scenes, plans, indecidables = _scenes(geo, vues, seuil)
    n_pos, n_mauvais, _f, n_perdues = _juger(scenes, geo[1], _decoder(paires))
    return dict(positions=n_pos, total=len(scenes), paires=n_mauvais,
                indecidables=len(indecidables), entrees=len(paires), perdues=n_perdues)

#!/usr/bin/env python3
"""bandes.py -- ranger les faces d'un mur dans l'ordre ou le moteur sait les SOUDER.

LE DEFAUT. `weldFaceStrip` (WALLS.C:1186-1212) replie une suite de faces noircies par la brume en
un seul quadrilatere, mais seulement si elles sont CONSECUTIVES dans `level_face` et enchainees
par des aretes OPPOSEES : l'arete (v3,v2) de l'une est l'arete (v0,v1) de la suivante (`dir` 1),
ou l'inverse (`dir` 2). Le convertisseur, lui, emettait ses faces dans l'ordre ou il les
fabriquait -- rangee par rangee pour une grille, cellule par cellule pour un plat -- sans savoir
que cet ordre decide de ce que le moteur pourra souder.

MESURE 18-09 (tools/study/bandes_plafond.py, E1M1) : les jointures consecutives sont
`faces - murs` = 2 582, et 1 404 seulement forment une bande, soit 54,4 %. Le meme jeu de faces,
range par une couverture par CHEMINS du graphe d'adjacence, en donne 2 094, soit 81,1 % -- et
c'est un OPTIMUM EXACT, obtenu par couplage biparti, pas un glouton. Ce qui reste au-dessus
demanderait de generaliser le test du moteur au VIRAGE (entrer et sortir par deux aretes
voisines au lieu d'opposees) ; la tessellation, elle, ne coute que 51 jointures sur 2 582, soit
2,0 %. Autrement dit le « tiers hors de portee » qu'on soupconnait n'existe pas : 97 % des
jointures ratees sont des faces voisines que l'ordre d'ecriture avait separees.

CE QUE CA NE CHANGE PAS. Aucun sommet ne bouge, aucune face n'est modifiee : c'est une
PERMUTATION. Les sommets d'une face sont locaux au mur et l'ordre de v[0..3] EST l'orientation
de la texture (WALLS.C:1223-1231), donc on ne le touche pas -- c'est d'ailleurs pourquoi le
plafond est 81 % et non 96 %. Rien d'autre dans le moteur n'indexe une face : seuls
`wall->firstFace/lastFace` les delimitent, `waveFace` (qui, lui, relie des faces) est vide dans
les deux convertisseurs, et la recherche par tuile d'AI2.C:624 est indifferente a l'ordre.

LA PREMISSE, VERIFIEE. Permuter ne se voit que si deux faces d'un MEME mur se recouvrent : elles
sont coplanaires, donc la derniere peinte gagne. MESURE sur E1M1 : 2 barycentres sur 2 985
tombent dans une autre face du meme mur, et ce sont deux fois la MEME chose -- un eclat de
0,1 u2 (trois sommets a 0,22 u d'etre alignes) laisse par l'eventail de la decomposition, au sol
et au plafond du meme endroit, et il porte LA MEME TUILE que le quad qui le contient. L'image est
donc identique quel que soit l'ordre. Chez Duke, l'etancheite du pavage est deja un critere du
verificateur (verif_e3, A1 : une arete interieure appartient a exactement deux faces).

GENERIQUE : ne lit que les quatre indices de sommet de chaque face.
"""
from collections import defaultdict


def direction(a, b):
    """Le `dir` que weldFaceStrip donnerait aux faces CONSECUTIVES a puis b (WALLS.C:1192-1196),
    ou 0 si elles ne forment pas de bande."""
    if a[3] == b[0] and a[2] == b[1]:
        return 1
    if a[0] == b[1] and a[3] == b[2]:
        return 2
    return 0


def jointures(quads):
    """Combien de jointures consecutives forment une bande, dans l'ordre donne."""
    return sum(1 for i in range(len(quads) - 1) if direction(quads[i], quads[i + 1]))


def _successeurs(quads):
    """Les arcs i -> j : « j peut suivre i ». Indexes par l'arete cherchee, donc lineaires en
    nombre de faces -- une arete est partagee par deux faces, pas par toutes."""
    par01 = defaultdict(list)
    par12 = defaultdict(list)
    for j, q in enumerate(quads):
        par01[(q[0], q[1])].append(j)
        par12[(q[1], q[2])].append(j)
    succ = []
    for i, q in enumerate(quads):
        s = set(par01.get((q[3], q[2]), ()))          # dir 1
        s |= set(par12.get((q[0], q[3]), ()))         # dir 2 : b[1] == a[0], b[2] == a[3]
        s.discard(i)
        succ.append(sorted(s))
    return succ


def _couplage(succ):
    """Couverture par chemins de cardinal MAXIMUM : un couplage biparti (Kuhn) entre « i a un
    successeur » et « j a un predecesseur ». Iteratif, parce qu'un chemin augmentant peut etre
    long comme le mur et que la recursion de Python plafonne a 1 000.
    -> (apres, avant), -1 quand il n'y en a pas."""
    n = len(succ)
    apres = [-1] * n
    avant = [-1] * n
    for depart in range(n):
        if not succ[depart]:
            continue
        vus = set()
        pile = [(depart, iter(succ[depart]))]
        chemin = []                                   # le chemin alternant en cours d'essai
        while pile:
            i, it = pile[-1]
            j = next((k for k in it if k not in vus), None)
            if j is None:
                pile.pop()
                if chemin:
                    chemin.pop()                      # l'arete qui menait ici a echoue
                continue
            vus.add(j)
            chemin.append((i, j))
            if avant[j] < 0:
                for a, b in chemin:                   # on augmente tout le chemin d'un coup
                    apres[a] = b
                    avant[b] = a
                break
            pile.append((avant[j], iter(succ[avant[j]])))
    return apres, avant


def _casser_cycles(apres, avant):
    """Un couplage maximum peut contenir des CYCLES, qui ne sont pas des chemins. On en coupe une
    arete au sommet de plus petit indice, pour que le resultat ne depende pas de l'ordre de
    parcours. MESURE E1M1 : aucun cycle, mais rien ne le garantit sur une autre carte."""
    n = len(apres)
    vus = [False] * n
    for i in range(n):                                # d'abord tout ce qui pend a un debut
        if avant[i] < 0:
            k = i
            while k >= 0 and not vus[k]:
                vus[k] = True
                k = apres[k]
    casses = 0
    for i in range(n):
        if vus[i]:
            continue
        k = i                                         # i est dans un cycle : on l'ouvre ici
        while not vus[k]:
            vus[k] = True
            k = apres[k]
        p = avant[i]
        if p >= 0:
            apres[p] = -1
            avant[i] = -1
            casses += 1
    return casses


def ordonner(quads):
    """-> la permutation des faces qui maximise les jointures soudables, et le nombre de cycles
    casses. Deterministe : a graphe egal, meme sortie."""
    if len(quads) < 3:
        return list(range(len(quads))), 0
    apres, avant = _couplage(_successeurs(quads))
    casses = _casser_cycles(apres, avant)
    ordre = []
    for i in range(len(quads)):
        if avant[i] < 0:
            k = i
            while k >= 0:
                ordre.append(k)
                k = apres[k]
    assert len(ordre) == len(quads), (len(ordre), len(quads))
    return ordre, casses


def ranger(faces, cle=lambda f: f["v"]):
    """Applique `ordonner` a une liste de faces, quelle que soit leur representation.
    -> (faces rangees, jointures avant, jointures apres, cycles casses)."""
    quads = [cle(f) for f in faces]
    avant = jointures(quads)
    ordre, casses = ordonner(quads)
    rangees = [faces[i] for i in ordre]
    return rangees, avant, jointures([cle(f) for f in rangees]), casses

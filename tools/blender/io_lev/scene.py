#!/usr/bin/env python3
"""scene.py -- du .LEV a une description de scene, SANS bpy.

POURQUOI CETTE COUPURE. Tout ce qui peut etre faux est ici : la generation de la grille d'un mur
en parallelogramme, la permutation de motif qui donne les coins de texture, l'indexation RELATIVE
des sommets de face, le pas particulier de la grille de lumiere. Rien de cela n'a besoin de
Blender, donc rien de cela n'a besoin de Blender pour etre VERIFIE -- `tools/blender/verif_scene.py`
rejoue ce module sur tous les .LEV disponibles et confronte, secteur par secteur, le nombre de
quads produits a celui que `tools/cout.py` compte de son cote par un tout autre chemin.
`__init__.py` ne fait plus que recopier des tableaux dans Blender.

CE QUE LE MOTEUR SAIT FAIRE, ET QU'ON RETROUVE ICI : un mur est un quad a 4 sommets portant sa
propre equation de plan, donc les sols et les plafonds PENCHENT (1 548 murs inclines mesures sur les
24 niveaux retail) et ce sont des murs comme les autres, reconnus au seul signe de normal[1]
(UTIL.C:41 et :67). Il n'y a PAS d'UV libre : une cellule prend sa tuile entiere, et le seul
placage reglable est le miroir encode dans l'octet de motif.
"""
import levdata

# Ce qu'un quad porte dans la scene.
#   quad   : 4 index dans `sommets`
#   tuile  : index de tuile de geometrie (0-base dans le jeu de tuiles DU FICHIER -- le moteur y
#            ajoute tileBase au chargement, LEVEL.C:122-125, et un importeur ne doit PAS le faire)
#   uv     : 4 couples (u, v), v vers le bas
#   lum    : 4 octets de lumiere BRUTS -- voir niveau_et_bande()
#   secteur, mur : d'ou il vient, pour pouvoir selectionner une piece

# L'OCTET DE LUMIERE EST EMPAQUETE (UTIL.H:74-84, mesure 23-09). `worldGrey` fait
# WORLDGREEN_NM * 32 = 128 entrees et l'assembleur du peintre l'indexe avec l'octet TEL QUEL
# (wallasm_gnu.s .Lrt_grey) : bits 0-4 = le NIVEAU 0..31, bits 5-6 = la BANDE de couleur du
# brouillard 0..3. La bande 0 est l'ancienne rampe grise octet pour octet.
# ⚠ 16 est la luminosite NEUTRE, pas 31 : la rampe va de la couleur du brouillard a 0 jusqu'au
# neutre a 16, et les entrees 17..31 d'une bande sont son PLANCHER (l'assembleur soustrait le
# brouillard avant d'indexer, donc un sommet avale par le brouillard retombe en haut de la bande
# du dessous). Les 24 niveaux retail n'utilisent que la bande 0 et ne depassent pas 16 ; les
# niveaux Doom du fork montent a 112, c'est-a-dire bande 3 niveau 16, et c'est parfaitement licite.
WORLDGREEN_NM = 4
LUM_MAX = WORLDGREEN_NM * 32 - 1        # 127
LUM_NEUTRE = 16


def niveau_et_bande(octet):
    """-> (niveau 0..31, bande 0..3) d'un octet de lumiere."""
    return octet & 31, (octet >> 5) & (WORLDGREEN_NM - 1)


def clarte(octet):
    """-> 0..1 pour peindre : le niveau borne au neutre. Au-dela de 16 on est dans le plancher de
    brouillard de la bande, qui n'est pas « plus clair »."""
    n, _b = niveau_et_bande(octet)
    return min(n, LUM_NEUTRE) / float(LUM_NEUTRE)


class Scene:
    def __init__(self):
        self.sommets = []        # [(x, y, z)] en unites monde du .LEV
        self.quads = []          # [[i0, i1, i2, i3]]
        self.tuiles = []         # [int] par quad
        self.uv = []             # [[(u, v) x4]] par quad
        self.lum = []            # [[l x4]] par quad
        self.secteur = []        # [int] par quad
        self.mur = []            # [int] par quad
        self.genre = []          # [int] par quad : 0 mur, 1 sol, 2 plafond -- voir genre_du_mur()
        self.portails = []       # [[i0..i3]] quads sans geometrie (portails, trous de ciel)
        self.objets = []         # [dict(type, nom, x, y, z, angle)]
        self.objets_muets = 0    # enregistrements qu'on n'a pas su lire -- comptes, jamais caches
        self._cle = {}

    def _pt(self, p, wi):
        """Index d'un sommet, fusionne PAR MUR.

        Fusionner a l'interieur d'un mur est necessaire : la grille recree chaque coin interieur
        quatre fois. Fusionner ENTRE murs serait tentant, mais deux surfaces coincidentes de deux
        murs differents -- le retail en a -- deviendraient alors le MEME quad a quatre index pres,
        et Blender supprime les faces en double dans `validate()` (mesure 23-09 : 37 faces perdues
        sur KILENTRY, 365 avec les cellules d'aire nulle). Souder par mur les garde distinctes et
        laisse le compte de faces coller exactement a celui de `tools/cout.py`."""
        k = (wi, round(p[0], 3), round(p[1], 3), round(p[2], 3))
        i = self._cle.get(k)
        if i is None:
            i = len(self.sommets)
            self._cle[k] = i
            self.sommets.append((k[1], k[2], k[3]))
        return i

    def _ajouter(self, pts, tuile, uv, lum, si, wi, genre):
        self.quads.append([self._pt(p, wi) for p in pts])
        self.tuiles.append(tuile)
        self.uv.append(list(uv))
        self.lum.append(list(lum))
        self.secteur.append(si)
        self.mur.append(wi)
        self.genre.append(genre)


def genre_du_mur(w):
    """0 mur, 1 sol, 2 plafond.

    Un sol et un plafond sont des murs ORDINAIRES de firstWall..lastWall : rien ne les distingue
    qu'un SIGNE, celui de normal[1] -- findFloorDistance prend le mur dont normal[1] > 0 et
    findCeilDistance celui dont normal[1] < 0 (UTIL.C:41 et :67). Ils PENCHENT souvent (1 548 murs
    inclines sur les 24 niveaux retail), donc ce n'est pas une hauteur, c'est une orientation."""
    n1 = w["normal"][1]
    return 1 if n1 > 0 else (2 if n1 < 0 else 0)


def construire(donnees, portails=True, objets=True):
    """-> Scene. `donnees` est la sortie de levdata.lire()."""
    N = donnees["level"]
    S, W, V, F = N["sectors"], N["walls"], N["vertices"], N["faces"]
    sc = Scene()

    for si, s in enumerate(S):
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            if not (0 <= wi < len(W)):
                continue
            w = W[wi]
            genre = genre_du_mur(w)
            if w["flags"] & 0x01:
                # --- mur en grille : les sommets n'existent pas dans le fichier, on les pose ---
                tl, th = w["tileLength"], w["tileHeight"]
                for h in range(th):
                    for wcol in range(tl):
                        j = w["textures"] + 2 * (h * tl + wcol)
                        if j + 1 >= len(N["texture"]):
                            continue
                        motif = N["texture"][j]
                        tuile = N["texture"][j + 1]
                        if motif > 7:
                            motif = 0
                        pts = _coins_de_cellule(w, V, wcol, h)
                        if pts is None:
                            continue
                        uv = [levdata.COIN_UV[levdata.PATTERN[motif][k]] for k in range(4)]
                        lum = [levdata.lumiere_de_grille(w, N, h + dy, wcol + dx)
                               for dx, dy in ((0, 0), (1, 0), (1, 1), (0, 1))]
                        sc._ajouter(pts, tuile, uv, lum, si, wi, genre)
            elif w["firstFace"] >= 0:
                # --- mur en maillage : des quads libres, index RELATIFS a firstVertex ----------
                base = w["firstVertex"]
                span = w["lastVertex"] - w["firstVertex"] + 1
                for fi in range(w["firstFace"], w["lastFace"] + 1):
                    if not (0 <= fi < len(F)):
                        continue
                    f = F[fi]
                    if any(not (0 <= v < span) for v in f["v"]):
                        continue                    # fichier faux : signale par lev_report.py
                    idx = [base + v for v in f["v"]]
                    if any(not (0 <= i < len(V)) for i in idx):
                        continue
                    pts = [(V[i]["x"], V[i]["y"], V[i]["z"]) for i in idx]
                    lum = [V[i]["light"] for i in idx]
                    sc._ajouter(pts, f["tile"], levdata.COIN_UV, lum, si, wi, genre)
            elif portails:
                # --- aucune geometrie : un portail ou un trou de ciel. Il garde un quad valide et
                # il porte la collision, donc on le montre a part plutot que de le perdre.
                idx = w["v"]
                if all(0 <= i < len(V) for i in idx):
                    sc.portails.append([sc._pt((V[i]["x"], V[i]["y"], V[i]["z"]), wi)
                                        for i in idx])

    if objets:
        _objets(N, sc)
    return sc


def _coins_de_cellule(w, V, cw, ch):
    """Les 4 coins monde de la cellule (ch, cw) d'un mur en grille.

    Pas de largeur = (v1 - v0) / tileLength, pas de hauteur = (v2 - v1) / tileHeight -- c'est bien
    (v2 - v1) que le moteur prend (WALLS.C:1832-1837), pas (v3 - v0) ; les deux coincident parce
    qu'un mur de ce type est un parallelogramme exact."""
    tl, th = w["tileLength"], w["tileHeight"]
    if tl <= 0 or th <= 0 or any(not (0 <= i < len(V)) for i in w["v"]):
        return None
    p = [(V[i]["x"], V[i]["y"], V[i]["z"]) for i in w["v"]]
    dW = [(p[1][k] - p[0][k]) / float(tl) for k in range(3)]
    dH = [(p[2][k] - p[1][k]) / float(th) for k in range(3)]

    def c(a, b):
        return tuple(p[0][k] + a * dW[k] + b * dH[k] for k in range(3))

    return [c(cw, ch), c(cw + 1, ch), c(cw + 1, ch + 1), c(cw, ch + 1)]


def _objets(N, sc):
    """Pose ce qu'on sait lire et COMPTE le reste.

    Il n'y a AUCUNE table de tailles dans le moteur : chaque type consomme ce que son `construct*`
    aspire (placeObjects, OBJECT.C:236-418). Les tailles se deduisent en revanche exactement des
    ecarts entre `firstParam` consecutifs -- les blocs se suivent bout a bout, sans trou, et
    OBJECT.C:251 l'assert. Le patron le plus repandu est celui de `suckSpriteParams`
    (OBJECT.C:222-234) : secteur, x, y, z, angle, en shorts gros-boutistes, memes unites monde que
    les sommets, angle en 360/4096 de degre.

    On ne DEVINE pas : un enregistrement n'est pose que s'il fait au moins 5 shorts, que son
    secteur existe et que son point tombe dans la boite des sommets. Tout le reste est compte dans
    `objets_muets` et dit a l'utilisateur, plutot que place au hasard."""
    import struct
    OP = N["objectParams"]
    objs = N["objects"]
    V = N["vertices"]
    if not objs or not V:
        return
    xs = [v["x"] for v in V]
    ys = [v["y"] for v in V]
    zs = [v["z"] for v in V]
    boite = (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))
    try:
        from ot_names import nom as ot_nom
    except ImportError:
        def ot_nom(t):
            return "OT_%d" % t

    for i, o in enumerate(objs):
        debut = o["firstParam"]
        fin = objs[i + 1]["firstParam"] if i + 1 < len(objs) else len(OP)
        if fin - debut < 10 or debut < 0 or fin > len(OP):
            sc.objets_muets += 1
            continue
        sect, x, y, z, ang = struct.unpack_from(">5h", OP, debut)
        if not (0 <= sect < len(N["sectors"])) or not (
                boite[0] <= x <= boite[1] and boite[2] <= y <= boite[3]
                and boite[4] <= z <= boite[5]):
            sc.objets_muets += 1
            continue
        sc.objets.append(dict(type=o["type"], nom=ot_nom(o["type"]), secteur=sect,
                              x=x, y=y, z=z, angle=(ang % 4096) * 360.0 / 4096.0))


def cellules_par_secteur(sc, n):
    """Quads produits par secteur -- la grandeur que `tools/cout.py` compte par un autre chemin."""
    out = [0] * n
    for si in sc.secteur:
        if 0 <= si < n:
            out[si] += 1
    return out


def couleur_de_cout(valeur, pire):
    """Vert -> jaune -> rouge, pour peindre un secteur par ce qu'il coute. `pire` cale l'echelle."""
    if pire <= 0:
        return (0.0, 0.8, 0.2, 1.0)
    t = min(1.0, max(0.0, valeur / float(pire)))
    if t < 0.5:
        return (2.0 * t, 0.8, 0.1, 1.0)
    return (1.0, 0.8 * (1.0 - 2.0 * (t - 0.5)), 0.1, 1.0)


def coins_utiles(q):
    """Les indices de COIN (0..3) a garder dans une cellule, les repetitions d'indice de sommet
    supprimees en gardant le premier de chaque.

    UN INDICE REPETE N'EST PAS UNE CELLULE VIDE : C'EST LE PLUS SOUVENT UN TRIANGLE. Le format
    range un triangle en quad dont un sommet se repete, exactement comme le VDP1 le dessine.
    MESURE DU 24-09, sur les 24 retail plus les 9 cartes Doom converties : 28 482 cellules ont un
    indice repete, et seules 411 sont vraiment plates -- toutes les autres ont trois coins distincts
    et une surface bien visible, jusqu'a 2 408 unites carrees. Sur E1M1, les 439 cellules concernees
    etaient 439 triangles et zero cellule plate, soit 11 % des surfaces du niveau. Les jeter, comme
    le faisait le premier jet, perdait donc des surfaces que la console dessine.

    -> quatre coins pour un quad, trois pour un triangle, moins de trois pour ce qui n'est pas une
    surface et n'a rien a montrer."""
    vus, out = set(), []
    for c, v in enumerate(q):
        if v not in vus:
            vus.add(v)
            out.append(c)
    return out


def faces_a_montrer(sc, plafonds=True):
    """-> (faces, garde, coins, plates, triangles) -- tout ce qu'il faut pour batir un maillage.

    EST ICI, ET PAS DANS LA COUCHE `bpy`, parce que c'est exactement ce qui peut etre faux : le
    nombre de coins d'une face doit valoir le nombre d'entrees par coin qu'on lui donnera ensuite
    (UV, lumiere), sinon l'import echoue sur un desalignement de tableau. Mis ici, `verif_scene.py`
    le juge sans Blender ; laisse dans `__init__.py`, il n'etait jugeable que dans Blender."""
    faces, garde, coins, plates = [], [], [], 0
    for i, q in enumerate(sc.quads):
        if not plafonds and sc.genre[i] == 2:
            continue
        c = coins_utiles(q)
        if len(c) < 3:
            plates += 1
            continue
        faces.append([q[k] for k in c])
        garde.append(i)
        coins.append(c)
    return faces, garde, coins, plates, sum(1 for c in coins if len(c) == 3)

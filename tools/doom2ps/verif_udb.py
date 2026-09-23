#!/usr/bin/env python3
"""verif_udb.py -- verification contradictoire du calque de budget, ecrite SANS importer
`udb_budget`.

CE QU'ELLE JUGE, ET CE QU'ELLE NE JUGE PAS. Elle juge la PLOMBERIE : que le calque soit bien une
copie de la carte source, que la mesure ecrite dans les trois canaux soit la meme et soit la bonne,
et qu'aucun secteur ne soit peint « pas cher » alors qu'il n'a pas ete mesure. Elle ne juge PAS la
loi de cout elle-meme -- c'est le travail de `cout.py` et de l'etalon retail. La distinction
compte : une erreur de plomberie (mauvais secteur, mauvais boutisme, valeur ecretee en silence)
peindrait une carte fausse sans que rien ne proteste, et c'est exactement la classe de defaut que
l'auteur ne peut pas voir -- il croirait sa salle bon marche.

LES CINQ FAMILLES :
  A. LE CALQUE EST UNE COPIE. Tout lump de carte autre que SECTORS et THINGS est identique OCTET
     POUR OCTET a celui du WAD source. C'est la garantie que l'auteur reconnait sa carte et que le
     calque ne peut pas lui faire croire a une geometrie qu'il n'a pas dessinee. S'y ajoute la
     conformite du fichier au format, et le fait que les six aplats soient ENTRE `F_START` et
     `F_END` -- verifier qu'ils existent quelque part ne prouvait rien, un aplat range hors de la
     plage n'est pas un flat pour l'editeur et tous les sols s'afficheraient comme manquants.
  B. SEULS TROIS CHAMPS BOUGENT. Dans SECTORS : hauteurs de sol et de plafond et type de secteur
     sont intacts, et le plafond aussi tant que `--plafonds` n'est pas demande. Un F_SKY1 n'est
     JAMAIS repeint, meme avec `--plafonds`.
  C. LES CANAUX DISENT TOUS LA MEME CHOSE. Depuis le seul `tag`, l'aplat ET la luminosite sont
     RE-DERIVES ici (deuxieme ecriture de `bornes` et de la rampe 48..255) et compares. Il y a DEUX
     canaux d'aplat, pas un : le sol, et le PLAFOND des que `--plafonds` est demande -- les deux
     sont re-derives, et un tag a -1 doit s'accompagner d'un aplat NA ou XX et d'une luminosite
     nulle sur chacun. S'y ajoutent deux controles de forme, intervalles de tag SEPARANTS et
     luminosite croissante avec le tag, qui sont aujourd'hui redondants avec la re-derivation et ne
     sont gardes que parce qu'ils resteraient valables si l'echelle changeait.
     ⚠ DEUX TROUS REELS ONT ETE BOUCHES ICI, ET AUCUN N'AURAIT ETE TROUVE PAR RELECTURE :
     relire les frontieres dans le calque au lieu de les re-deriver ne donne qu'un controle
     ORDINAL, et sur E1M9 -- pire cone 830, donc tout le niveau sous la premiere borne -- un
     relabelage coherent passe inapercu ; et le canal PLAFOND n'etait re-derive par personne, si
     bien que repeindre les 78 plafonds non-ciel d'E1M1 laissait imprimer « aucun defaut ».
  D. LA MESURE EST LA BONNE. Les cellules par secteur sont RE-DERIVEES ici depuis le `.LEV`, par
     une deuxieme ecriture de la regle (grille des murs plats + faces du maillage, WALLS.C:1832-1837
     et cout.py), puis re-agregees par `doom_sector`. Pour `--metrique propre` la comparaison est
     exacte, secteur par secteur, plus l'invariant de somme. Pour `--metrique vue`,
     `ordre.visibilite` est rejoue et RE-AGREGE ici par une deuxieme ecriture de la regle du
     maximum -- ce qui ne prouve rien sur la loi de cout (meme code des deux cotes) mais isole
     entierement la plomberie : position -> secteur .LEV -> secteur Doom, agregation, ecriture.
     S'y ajoutent deux invariants reellement independants : tout tag est <= au total des cellules
     du niveau, et le MAXIMUM des tags est exactement le pire cone du niveau.
  E. LES MARQUEURS SONT DANS LA CARTE. Les objets ajoutes sont tous du type marqueur, en nombre
     annonce, et leurs coordonnees tombent dans la boite englobante des sommets du WAD -- ce qui
     verifie du meme coup le repere 1:1 du convertisseur (doom3d.py:10).

`--mutations` PROUVE QUE CHAQUE FAMILLE PEUT TIRER. Une verification qui ne se trompe jamais parce
qu'elle ne regarde rien a exactement la meme sortie qu'une bonne. Le banc abime le calque d'une
quinzaine de facons -- un octet de LINEDEFS, un aplat tronque ou bariole, une hauteur deplacee, un
ciel repeint, une luminosite hors bande puis decalee d'un seul cran, une couleur qui ment sur son
tag, un tag decale d'une unite, des bornes declarees qui ne sont plus celles de l'etalon, un
marqueur hors carte -- et exige que la BONNE famille proteste a chaque fois.
TROIS REGLES QUE LE BANC S'IMPOSE, apprises en le faisant tourner :
  * une mutation doit CHANGER quelque chose. Repeindre en vert un secteur deja vert n'est pas une
    faute, et se lisait comme un echec de la verification (vu sur E1M9, dont le pire cone tient
    sous la premiere borne, donc dont tous les secteurs sont verts). Celle qui repeint le pire
    secteur choisit maintenant un aplat different du sien.
  * la famille attendue depend du MODE. Toucher un sol est une incoherence entre canaux (C) en
    temps normal, mais une atteinte a la copie (B) sous `--garder-sols`.
  * AUCUNE COUPE SILENCIEUSE. Une mutation sans objet -- les deux mutations de marqueur sur un
    calque qui n'en porte aucun, le ciel repeint sur une carte sans F_SKY1 -- est ECARTEE et
    NOMMEE, et le rapport imprime les familles reellement exercees puis avertit de celles qui ne
    le sont pas. Le banc imprimait auparavant un score plein en n'exercant pas la famille E, ce
    qui se lit comme « les cinq familles sont prouvees » : une coupe muette dans un banc de preuve
    est pire que pas de banc.
L'aller-retour sans mutation doit d'abord repasser, sinon le banc mesurerait son propre ecrivain.

Usage : python tools\\doom2ps\\verif_udb.py --calque build\\udb\\E1M1_BUDGET.wad
                                            --wad DOOM1.WAD --map E1M1 --lev cd_doom\\E1M1.LEV
                                            --geom build\\doom2ps\\e1m1_geom3d.json
                                            [--metrique vue|propre] [--bandes retail|paliers]
                                            [--fov 53] [--pire 8] [--plafonds] [--garder-sols]
                                            [--mutations]
Code de retour 1 si un defaut est trouve, ou si une mutation passe inapercue.
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

import lev                                                             # noqa: E402

MAP_LUMPS = ("THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
             "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP")
NOMS = ("BUDGET0", "BUDGET1", "BUDGET2", "BUDGET3", "BUDGETNA", "BUDGETXX")
LUM_MIN, LUM_MAX = 48, 255
TAG_NON_MESURE = -1
MARQUEUR_TYPE = 32000


# -- un lecteur de WAD ecrit ici, pour ne rien partager avec l'outil juge ------------------------
def lire_wad(path):
    b = open(path, "rb").read()
    magic, n, off = struct.unpack("<4sii", b[:12])
    if magic not in (b"IWAD", b"PWAD"):
        raise SystemExit("%s n'est pas un WAD" % path)
    d = []
    for i in range(n):
        fo, sz, nm = struct.unpack("<ii8s", b[off + 16 * i:off + 16 * i + 16])
        d.append((nm.rstrip(b"\0").decode("latin-1").upper(), b[fo:fo + sz]))
    return b, d


def conformite(path):
    """-> [defauts] : le fichier est-il un WAD qu'un AUTRE programme accepterait d'ouvrir ?

    Ce controle est la parce que les quatre autres familles comparent le calque a la carte source :
    elles ne verraient pas un repertoire qui deborde du fichier, des lumps qui se chevauchent, ou un
    nom de lump qu'un port refuse. C'est exactement la classe de defaut qui se manifeste par « UDB
    n'ouvre pas le fichier » sans rien dire de plus."""
    import re
    d = []
    b = open(path, "rb").read()
    if len(b) < 12:
        return ["fichier de %d octets" % len(b)]
    magic, n, off = struct.unpack("<4sii", b[:12])
    if magic != b"PWAD":
        d.append("en-tete %r au lieu de PWAD" % magic)
    if n < 0 or off < 12 or off + 16 * n > len(b):
        return d + ["repertoire de %d entrees a %d hors d'un fichier de %d o" % (n, off, len(b))]
    if off + 16 * n != len(b):
        d.append("%d octets apres le repertoire" % (len(b) - off - 16 * n))
    bon = re.compile(rb"^[A-Z0-9_\[\]\\-]{1,8}\x00*$")
    fin = 12
    for i in range(n):
        fo, sz, nm = struct.unpack("<ii8s", b[off + 16 * i:off + 16 * i + 16])
        if not bon.match(nm):
            d.append("lump %d : nom %r hors de l'alphabet des noms de lump" % (i, nm))
        if sz < 0 or fo < 12 or fo + sz > off:
            d.append("lump %d (%s) : %d o a %d, hors de la zone de donnees" % (i, _nom8(nm), sz, fo))
        elif fo < fin:
            d.append("lump %d (%s) chevauche le precedent" % (i, _nom8(nm)))
        else:
            fin = fo + sz
    return d


def declaration(dc):
    """-> dict des reglages declares par le calque dans son lump BUDGET, ou {}.

    Le calque est AUTO-DESCRIPTIF, et c'est voulu : si la verification dependait de drapeaux
    repasses a la main, un drapeau oublie la ferait accuser un calque correct -- le pire des
    defauts pour un outil de controle, celui qui apprend a l'auteur a l'ignorer."""
    for nm, data in dc:
        if nm != "BUDGET":
            continue
        for ligne in data.decode("latin-1", "replace").splitlines():
            if not ligne.startswith("PARAMS "):
                continue
            out = {}
            for mot in ligne[7:].split():
                cle, _, val = mot.partition("=")
                out[cle] = val
            return out
    return {}


def carte_de(d, mapname):
    """Les lumps de la carte, dans l'ordre, a partir du marqueur."""
    noms = [nm for nm, _ in d]
    i = noms.index(mapname.upper())
    out = []
    for j in range(i + 1, min(i + 12, len(d))):
        nm, data = d[j]
        if nm in MAP_LUMPS:
            out.append((nm, data))
        elif out:
            break
    return out


def _nom8(b):
    return b.rstrip(b"\0").decode("latin-1").upper()


def bornes(metrique, bandes):
    """Les trois frontieres de couleur, RE-DERIVEES ici -- deuxieme ecriture de `udb_budget.bornes`.

    Elles ne sont pas relues dans le calque : le faire ne donnerait qu'un controle ORDINAL, qui
    laisse passer un relabelage coherent. Mesure du 23-09 : sur E1M9, dont le pire cone est 830,
    donc entierement sous la premiere borne, repeindre le secteur le plus cher dans une autre bande
    ne casse aucun ordre et un controle ordinal ne le voit pas. La source (`tools/lev_etalon.json`)
    est une DONNEE, pas du code de l'outil juge : la re-deriver est une verification, pas une copie."""
    if bandes == "paliers":
        import cout                                                     # noqa: E402
        b = [s for s, _fps in cout.PALIERS]
        return b[0], b[1], b[2]
    e = json.load(open(os.path.join(ROOT, "tools", "lev_etalon.json"), encoding="utf-8"))
    med = lambda v: sorted(v)[len(v) // 2]                              # noqa: E731
    if metrique == "vue":
        return (med([x["cone"][1] for x in e.values()]),
                med([x["cone"][2] for x in e.values()]),
                med([x["cone"][3] for x in e.values()]))
    v = sorted(x["secteur_max"] for x in e.values())
    return v[0], med(v), v[-1]


def bande_de(v, b):
    return 0 if v <= b[0] else 1 if v <= b[1] else 2 if v <= b[2] else 3


def lum_de(v, plein):
    x = min(1.0, max(0.0, float(v) / float(plein)))
    return int(round(LUM_MIN + (LUM_MAX - LUM_MIN) * x))


def secteurs(brut):
    return [struct.unpack_from("<hh8s8shhh", brut, 26 * k) for k in range(len(brut) // 26)]


# -- la regle du peintre, RE-ECRITE ICI ---------------------------------------------------------
def cellules_par_secteur(S, W):
    """Deuxieme ecriture de la regle de `cout.cellules_par_secteur`, volontairement independante.

    Un mur plat (WALLFLAG_PARALLELOGRAM, 0x01) coute sa grille `tileLength x tileHeight`, dont les
    sommets n'existent meme pas dans le fichier (WALLS.C:1832-1837) ; un mur a faces coute ses
    faces. Les deux familles sont disjointes."""
    n = []
    for s in S:
        t = 0
        for wi in range(s["firstWall"], s["lastWall"] + 1):
            w = W[wi]
            if w["flags"] & 0x01:
                t += w["tileLength"] * w["tileHeight"]
            if w["firstFace"] >= 0:
                t += w["lastFace"] - w["firstFace"] + 1
        n.append(t)
    return n


_VUE = {}


def mesure_vue(chemin, S, W, V, fov):
    """-> (pire cone par secteur .LEV, pire cone du niveau). Memorise : le banc rappelle la
    verification une quinzaine de fois sur le MEME .LEV, et `ordre.visibilite` est le calcul cher.

    ⚠ CE QUE CETTE FONCTION PROUVE, ET CE QU'ELLE NE PROUVE PAS. Elle rappelle `ordre` et `cout`,
    donc elle ne prouve RIEN sur la loi de cout -- si la loi est fausse, les deux cotes le sont
    ensemble. Ce qu'elle isole en revanche entierement, c'est la PLOMBERIE du calque : la relation
    position -> secteur .LEV -> secteur Doom, la regle d'agregation (maximum et non somme, parce
    qu'on ne se tient qu'a un endroit a la fois), et l'ecriture dans les trois canaux. C'est la que
    vivent les defauts silencieux, et c'est ce qu'aucun autre controle ne couvre."""
    cle = (chemin, fov)
    if cle not in _VUE:
        import cout                                                     # noqa: E402
        import ordre                                                    # noqa: E402
        geo, vues = ordre.visibilite(S, W, V)
        positions, st = cout.carte(S, W, geo, vues, fov=fov)
        par = [None] * len(S)
        for _ex, _ez, s0, _nv, _tour, cone in positions:
            if par[s0] is None or cone > par[s0]:
                par[s0] = cone
        _VUE[cle] = (par, st["cone"][3])
    return _VUE[cle]


def verifier(a, entrees=None):
    """-> (defauts, info). `entrees` permet au banc de juger un calque tenu en memoire."""
    defauts = []

    def ko(fam, txt):
        defauts.append("%s %s" % (fam, txt))

    _bs, ds = lire_wad(a.wad)
    dc = entrees if entrees is not None else lire_wad(a.calque)[1]
    src_l = carte_de(ds, a.map)
    src = dict(src_l)
    cal_l = carte_de(dc, a.map)
    cal = dict(cal_l)
    noms_cal = [nm for nm, _ in dc]

    # -- A. le calque est une copie -------------------------------------------------------------
    if entrees is None:
        for x in conformite(a.calque):
            ko("A", x)
    if [nm for nm, _ in cal_l] != [nm for nm, _ in src_l]:
        ko("A", "l'ordre des lumps de carte differe de celui du WAD source")
    for nm, data in src.items():
        if nm not in cal:
            ko("A", "lump %s absent du calque" % nm)
        elif nm not in ("SECTORS", "THINGS") and cal[nm] != data:
            ko("A", "lump %s modifie (%d o contre %d)" % (nm, len(cal[nm]), len(data)))
    for m in ("F_START", "F_END", "BUDGET"):
        if m not in noms_cal:
            ko("A", "lump %s absent" % m)
    # ET ILS DOIVENT ETRE ENTRE LES DEUX MARQUEURS. Verifier que F_START, F_END et les six aplats
    # existent quelque part dans le repertoire ne prouve RIEN : un aplat range hors de la plage
    # n'est pas un flat pour l'editeur, il est ignore, et tous les sols du calque s'affichent comme
    # textures manquantes. C'est le seul critere qui compte, et c'est celui qui manquait.
    # (Defaut trouve le 24-09 par relecture contradictoire.)
    if "F_START" in noms_cal and "F_END" in noms_cal:
        d0, d1 = noms_cal.index("F_START"), noms_cal.index("F_END")
        if d1 < d0:
            ko("A", "F_END precede F_START : la plage de flats est vide")
        for nm in NOMS:
            if nm in noms_cal and not (d0 < noms_cal.index(nm) < d1):
                ko("A", "aplat %s hors de la plage F_START..F_END : l'editeur ne le verra pas" % nm)
    plats = dict(dc)
    for nm in NOMS:
        if nm not in plats:
            ko("A", "aplat %s absent" % nm)
        elif len(plats[nm]) != 4096:
            ko("A", "aplat %s fait %d o au lieu de 4096" % (nm, len(plats[nm])))
        elif len(set(plats[nm])) != 1:
            ko("A", "aplat %s n'est pas un aplat (%d indices)" % (nm, len(set(plats[nm]))))
    idx = [plats[n][0] for n in NOMS if n in plats and len(plats[n]) == 4096]
    if len(idx) == len(NOMS) and len(set(idx)) != len(NOMS):
        ko("A", "deux aplats partagent le meme index de palette : les bandes seraient confondues")

    # -- la mesure, re-derivee ------------------------------------------------------------------
    m = lev.parse_lev(a.lev)["level"]
    S, W = m["sectors"], m["walls"]
    cel = cellules_par_secteur(S, W)
    dsec = json.load(open(a.geom, encoding="utf-8"))["doom_sector"]
    if len(dsec) != len(S):
        # ET ON S'ARRETE LA. Continuer indexait `dsec[i]` sur `range(len(S))` et levait une
        # IndexError AVANT que le defaut qu'on vient d'enregistrer ne soit imprime : la trace
        # remplacait le diagnostic, et `udb_budget.agreger` nommait mieux le probleme que son
        # propre verificateur. (Defaut trouve le 24-09 par relecture contradictoire.)
        ko("D", "le geom3d annonce %d secteurs, le .LEV en a %d : ils ne viennent pas de la meme "
                "conversion" % (len(dsec), len(S)))
        return defauts, {"secteurs": len(cal.get("SECTORS", b"")) // 26, "lumps": len(cal_l),
                         "marqueurs": 0, "absents": 0, "abandon": True}

    so = secteurs(src["SECTORS"])
    co = secteurs(cal.get("SECTORS", b""))
    if len(so) != len(co):
        ko("B", "%d secteurs dans le calque contre %d dans la source" % (len(co), len(so)))
    n = min(len(so), len(co))
    vus = set(dsec)
    absents = [k for k in range(len(so)) if k not in vus]

    propre = [None] * len(so)
    for i, c in enumerate(cel):
        d = dsec[i]
        if 0 <= d < len(propre):
            propre[d] = c if propre[d] is None else propre[d] + c

    # -- B. seuls trois champs bougent ----------------------------------------------------------
    for k in range(n):
        fh, ch, fp, cp, _lt, sp, _tg = so[k]
        fh2, ch2, fp2, cp2, _lt2, sp2, _tg2 = co[k]
        if (fh, ch, sp) != (fh2, ch2, sp2):
            ko("B", "secteur %d : hauteur ou type modifie" % k)
        if a.garder_sols:
            if fp != fp2:
                ko("B", "secteur %d : sol modifie malgre --garder-sols" % k)
        elif _nom8(fp2) not in NOMS:
            ko("B", "secteur %d : sol %r n'est pas un aplat de budget" % (k, _nom8(fp2)))
        if _nom8(cp) == "F_SKY1" and cp != cp2:
            ko("B", "secteur %d : un plafond de ciel a ete repeint" % k)
        elif _nom8(cp) != "F_SKY1" and not a.plafonds and cp != cp2:
            ko("B", "secteur %d : plafond modifie sans --plafonds" % k)

    # -- C. les trois canaux disent la meme chose -----------------------------------------------
    B = bornes(a.metrique, a.bandes)
    dit = declaration(dc).get("bornes")
    if dit and dit != "%d/%d/%d" % B:
        ko("C", "le calque declare les bornes %s, re-derivees %d/%d/%d : il a ete fabrique contre "
                "un autre etalon" % (dit, B[0], B[1], B[2]))
    par_bande = {}
    for k in range(n):
        lt, tg = co[k][4], co[k][6]
        # LES CANAUX D'APLAT DU SECTEUR, ET IL Y EN A DEUX. Le sol, sauf sous `--garder-sols` ou il
        # n'en porte plus ; et LE PLAFOND des que `--plafonds` est demande et que la source n'est
        # pas un ciel -- `udb_budget.construire_secteurs` y ecrit alors le MEME aplat que le sol.
        # ⚠ Ce second canal n'etait re-derive par personne : la famille B cesse de regarder le
        # plafond des que `--plafonds` est pose, et la famille C ne lisait que le sol. Mesure du
        # 24-09 : repeindre les 78 plafonds non-ciel d'E1M1 en BUDGET0 laissait la verification
        # imprimer « aucun defaut » et sortir avec 0, alors que l'auteur parcourant la vue 3D
        # lisait sa piece la plus chere comme bon marche. Trou trouve par relecture contradictoire.
        canaux = []
        if not a.garder_sols:
            canaux.append(("sol", _nom8(co[k][2])))
        if a.plafonds and k < len(so) and _nom8(so[k][3]) != "F_SKY1":
            canaux.append(("plafond", _nom8(co[k][3])))
        nm = canaux[0][1] if canaux else None
        if tg == TAG_NON_MESURE:
            for quoi, c in canaux:
                if c not in ("BUDGETNA", "BUDGETXX"):
                    ko("C", "secteur %d : tag -1 mais %s peint %s" % (k, quoi, c))
                if k not in absents and c == "BUDGETXX":
                    ko("C", "secteur %d : %s peint XX alors qu'il est present dans le .LEV"
                       % (k, quoi))
                if k in absents and c != "BUDGETXX":
                    ko("C", "secteur %d : absent du .LEV mais %s peint %s" % (k, quoi, c))
            if lt != 0:
                ko("C", "secteur %d : non mesure mais luminosite %d" % (k, lt))
            continue
        if k in absents:
            ko("C", "secteur %d : absent du .LEV mais porte un tag de %d" % (k, tg))
        if not (LUM_MIN <= lt <= LUM_MAX):
            ko("C", "secteur %d : luminosite %d hors de %d..%d" % (k, lt, LUM_MIN, LUM_MAX))
        elif lt != lum_de(tg, B[2]):
            ko("C", "secteur %d : luminosite %d, re-derivee %d pour un tag de %d"
               % (k, lt, lum_de(tg, B[2]), tg))
        attendu = NOMS[bande_de(tg, B)]
        for quoi, c in canaux:
            if c in ("BUDGETNA", "BUDGETXX"):
                ko("C", "secteur %d : mesure (%d) mais %s peint %s" % (k, tg, quoi, c))
            elif c != attendu:
                ko("C", "secteur %d : tag %d, %s peint %s, re-derive %s (bornes %d/%d/%d)"
                   % (k, tg, quoi, c, attendu, B[0], B[1], B[2]))
        if nm is not None and nm not in ("BUDGETNA", "BUDGETXX"):
            par_bande.setdefault(nm, []).append(tg)
    ordonnees = [nm for nm in NOMS[:4] if nm in par_bande]
    for i in range(len(ordonnees) - 1):
        if max(par_bande[ordonnees[i]]) >= min(par_bande[ordonnees[i + 1]]):
            ko("C", "les bandes %s et %s se chevauchent (%d >= %d)"
               % (ordonnees[i], ordonnees[i + 1], max(par_bande[ordonnees[i]]),
                  min(par_bande[ordonnees[i + 1]])))
    mes = sorted((co[k][6], co[k][4]) for k in range(n) if co[k][6] != TAG_NON_MESURE)
    for i in range(len(mes) - 1):
        if mes[i][1] > mes[i + 1][1]:
            ko("C", "luminosite non monotone : tag %d -> %d mais tag %d -> %d"
               % (mes[i][0], mes[i][1], mes[i + 1][0], mes[i + 1][1]))
            break

    # -- D. la mesure est la bonne --------------------------------------------------------------
    info = {}
    if a.metrique == "propre":
        for k in range(n):
            if k in absents:
                continue
            tg, att = co[k][6], propre[k]
            if att is None:
                if tg != TAG_NON_MESURE:
                    ko("D", "secteur %d : aucun secteur .LEV, mais tag %d" % (k, tg))
            elif tg != att:
                ko("D", "secteur %d : tag %d, re-derive %d" % (k, tg, att))
        somme = sum(co[k][6] for k in range(n) if co[k][6] != TAG_NON_MESURE)
        if somme != sum(cel):
            ko("D", "la somme des tags (%d) n'est pas le total des cellules du .LEV (%d)"
               % (somme, sum(cel)))
        info["total"] = sum(cel)
    else:
        total = sum(cel)
        tags = [co[k][6] for k in range(n) if co[k][6] != TAG_NON_MESURE]
        if tags and max(tags) > total:
            ko("D", "un tag (%d) depasse le total des cellules du niveau (%d)" % (max(tags), total))
        par_lev, pc = mesure_vue(a.lev, S, W, m["vertices"], a.fov)
        # re-agregation INDEPENDANTE : maximum et non somme, et par le mapping relu du geom3d
        att = [None] * len(so)
        for i, v in enumerate(par_lev):
            d = dsec[i]
            if v is not None and 0 <= d < len(att):
                att[d] = v if att[d] is None else max(att[d], v)
        for k in range(n):
            if k in absents:
                continue
            tg = co[k][6]
            if att[k] is None:
                if tg != TAG_NON_MESURE:
                    ko("D", "secteur %d : aucune position debout, mais tag %d" % (k, tg))
            elif tg != att[k]:
                ko("D", "secteur %d : tag %d, re-derive %d" % (k, tg, att[k]))
        if tags and max(tags) != pc:
            ko("D", "le plus gros tag est %d, le pire cone du niveau est %d" % (max(tags), pc))
        info["cone"] = pc
        info["sans_position"] = sum(1 for k in range(n)
                                    if co[k][6] == TAG_NON_MESURE and k not in absents)
    info["absents"] = len(absents)

    # -- E. les marqueurs sont dans la carte ----------------------------------------------------
    th_s, th_c = src.get("THINGS", b""), cal.get("THINGS", b"")
    if not th_c.startswith(th_s):
        ko("E", "les objets d'origine ont ete modifies")
    extra = th_c[len(th_s):]
    if len(extra) % 10:
        ko("E", "%d octets d'objets ajoutes, ce n'est pas un multiple de 10" % len(extra))
    marq = [struct.unpack_from("<5h", extra, 10 * k) for k in range(len(extra) // 10)]
    if len(marq) != a.pire:
        ko("E", "%d marqueurs ajoutes, %d attendus" % (len(marq), a.pire))
    vx = src.get("VERTEXES", b"")
    pts = [struct.unpack_from("<hh", vx, 4 * k) for k in range(len(vx) // 4)]
    if pts:
        x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
        y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
        for x, y, _ang, ty, _fl in marq:
            if ty != MARQUEUR_TYPE:
                ko("E", "objet ajoute de type %d au lieu de %d" % (ty, MARQUEUR_TYPE))
            if not (x0 <= x <= x1 and y0 <= y <= y1):
                ko("E", "marqueur (%d, %d) hors de la boite [%d..%d] x [%d..%d] : le repere du "
                        "convertisseur n'est pas 1:1" % (x, y, x0, x1, y0, y1))
    info["secteurs"] = len(co)
    info["lumps"] = len(cal_l)
    info["marqueurs"] = len(marq)
    return defauts, info


# -- le banc de mutations -----------------------------------------------------------------------
def _sect(entrees, k, champ, valeur):
    """Ecrit un champ du secteur `k` (0=fh 1=ch 2=fp 3=cp 4=light 5=special 6=tag)."""
    out = []
    for nm, d in entrees:
        if nm == "SECTORS":
            b = bytearray(d)
            r = list(struct.unpack_from("<hh8s8shhh", b, 26 * k))
            r[champ] = valeur
            struct.pack_into("<hh8s8shhh", b, 26 * k, *r)
            d = bytes(b)
        out.append((nm, d))
    return out


def _lump(entrees, nom, f):
    return [(nm, f(d) if nm == nom else d) for nm, d in entrees]


def mutations(entrees, a):
    """-> [(famille, nom, entrees mutees)]. Chaque mutation est un defaut REEL qu'un calque pourrait
    avoir ; si la verification ne le voit pas, elle ne protege de rien."""
    src = dict(carte_de(entrees, a.map))
    co = secteurs(src["SECTORS"])
    mesures = [k for k in range(len(co)) if co[k][6] != TAG_NON_MESURE]
    kmax = max(mesures, key=lambda k: co[k][6])
    kmin = min(mesures, key=lambda k: co[k][6])
    nonmes = [k for k in range(len(co)) if co[k][6] == TAG_NON_MESURE]
    ciels = [k for k in range(len(co)) if _nom8(co[k][3]) == "F_SKY1"]
    E = list(entrees)
    # Repeindre un sol n'est pas le meme defaut selon le mode : avec `--garder-sols` le sol n'est
    # plus un canal de mesure, donc y toucher est une atteinte a la COPIE (famille B) et non une
    # incoherence entre canaux (famille C). Le banc doit annoncer la famille que le defaut a
    # reellement, sinon il compte comme un rate ce qui est une bonne prise.
    fsol = "B" if a.garder_sols else "C"
    out = [
        ("A", "un octet de LINEDEFS change",
         _lump(E, "LINEDEFS", lambda d: bytes([d[0] ^ 0xff]) + d[1:])),
        ("A", "un aplat tronque d'un octet",
         _lump(E, "BUDGET2", lambda d: d[:-1])),
        ("A", "un aplat qui n'est plus uni",
         _lump(E, "BUDGET1", lambda d: bytes([d[0] ^ 1]) + d[1:])),
        ("A", "le lump BUDGET retire",
         [(nm, d) for nm, d in E if nm != "BUDGET"]),
        ("B", "une hauteur de sol deplacee",
         _sect(E, kmax, 0, co[kmax][0] + 8)),
        ("C", "la luminosite d'un secteur mesure mise a 1",
         _sect(E, kmax, 4, 1)),
        (fsol, "le moins cher peint NA alors qu'il porte un tag",
         _sect(E, kmin, 2, b"BUDGETNA")),
        ("C", "la luminosite d'un secteur decalee d'un cran",
         _sect(E, kmin, 4, co[kmin][4] + 1)),
        (fsol, "le pire secteur repeint dans la bande du dessus",
         _sect(E, kmax, 2, b"BUDGET3\0" if _nom8(co[kmax][2]) != "BUDGET3" else b"BUDGET0\0")),
        ("D", "un tag decale d'une unite",
         _sect(E, kmin, 6, co[kmin][6] + 1)),
        ("C", "des bornes declarees qui ne sont plus celles de l'etalon",
         _lump(E, "BUDGET", lambda d: d.replace(b"bornes=", b"bornes=9"))),
    ]
    plafonds = [k for k in range(len(co)) if _nom8(co[k][3]).startswith("BUDGET")]
    ecartees = []
    if ciels:
        out.append(("B", "un plafond de ciel repeint", _sect(E, ciels[0], 3, b"BUDGET3\0")))
    else:
        ecartees.append(("B", "un plafond de ciel repeint : la carte n'a aucun F_SKY1"))
    if nonmes:
        out.append(("C", "un secteur non mesure qui s'allume", _sect(E, nonmes[0], 4, 120)))
    else:
        ecartees.append(("C", "un secteur non mesure qui s'allume : tous sont mesures"))
    if a.plafonds and plafonds:
        out.append(("C", "un plafond peint qui ment sur son tag",
                    _sect(E, plafonds[0], 3, b"BUDGET0\0"
                          if _nom8(co[plafonds[0]][3]) != "BUDGET0" else b"BUDGET3\0")))
    elif a.plafonds:
        ecartees.append(("C", "un plafond peint : aucun plafond ne porte d'aplat"))
    if a.pire:
        out.append(("E", "un marqueur hors de la carte",
                    _lump(E, "THINGS", lambda d: d[:-10] + struct.pack(
                        "<5h", 30000, 30000, 0, MARQUEUR_TYPE, 7))))
        out.append(("E", "un marqueur d'un autre type",
                    _lump(E, "THINGS", lambda d: d[:-4] + struct.pack("<2h", 7, 7))))
    else:
        # ⚠ IL FAUT LE DIRE. Sans marqueur il n'y a rien a abimer, donc la famille E n'est PAS
        # exercee -- et le banc imprimait tout de meme un score plein, ce qui se lit comme « les
        # cinq familles sont prouvees ». Une coupe silencieuse dans un banc de preuve est pire que
        # pas de banc. (Defaut trouve le 24-09 par relecture contradictoire.)
        ecartees.append(("E", "les deux mutations de marqueur : le calque n'en porte aucun"))
    return out, ecartees


def main(argv=None):
    ap = argparse.ArgumentParser(description="verification du calque de budget UDB")
    ap.add_argument("--calque", required=True)
    ap.add_argument("--wad", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--lev", required=True)
    ap.add_argument("--geom", required=True)
    ap.add_argument("--metrique", choices=("vue", "propre"), default="vue")
    ap.add_argument("--bandes", choices=("retail", "paliers"), default="retail",
                    help="doit etre celui passe a udb_budget.py")
    ap.add_argument("--pire", type=int, default=8)
    ap.add_argument("--fov", type=float, default=53.0,
                    help="doit etre celui passe a udb_budget.py")
    ap.add_argument("--plafonds", action="store_true")
    ap.add_argument("--garder-sols", action="store_true")
    ap.add_argument("--mutations", action="store_true",
                    help="prouver que chaque famille peut TIRER, en abimant le calque")
    a = ap.parse_args(argv)

    # LE CALQUE FAIT FOI sur la facon dont il a ete fabrique ; les drapeaux ne servent que pour un
    # calque sans declaration. Sinon la verification jugerait un fichier avec les reglages d'un
    # autre, et son verdict ne voudrait rien dire.
    p = declaration(lire_wad(a.calque)[1])
    if p:
        a.metrique = p.get("metrique", a.metrique)
        a.bandes = p.get("bandes", a.bandes)
        a.fov = float(p.get("fov", a.fov))
        a.pire = int(p.get("pire", a.pire))
        a.garder_sols = bool(int(p.get("garder_sols", int(a.garder_sols))))
        a.plafonds = bool(int(p.get("plafonds", int(a.plafonds))))
        print("reglages relus dans le calque : metrique=%s bandes=%s fov=%g pire=%d "
              "garder_sols=%d plafonds=%d"
              % (a.metrique, a.bandes, a.fov, a.pire, a.garder_sols, a.plafonds))
    else:
        print("le calque ne declare rien : reglages pris sur la ligne de commande")

    defauts, info = verifier(a)
    print("%s contre %s / %s : %d secteurs, %d lumps de carte, %d marqueurs"
          % (os.path.basename(a.calque), os.path.basename(a.wad), a.map.upper(),
             info["secteurs"], info["lumps"], info["marqueurs"]))
    if info.get("abandon"):
        print("   verification ABANDONNEE : les entrees ne vont pas ensemble, rien d'autre n'a"
              " pu etre juge")
    elif "cone" in info:
        print("   vue : %d secteurs Doom sans position debout, %d absents du .LEV, pire cone %d"
              % (info["sans_position"], info["absents"], info["cone"]))
    else:
        print("   propre : %d cellules au total, %d secteurs absents du .LEV"
              % (info["total"], info["absents"]))
    if defauts:
        for d in defauts:
            print("  DEFAUT %s" % d)
        print("%d defaut(s)" % len(defauts))
        return 1
    print("  aucun defaut sur les 5 familles (copie, champs, canaux, mesure, marqueurs)")

    if a.mutations:
        entrees = lire_wad(a.calque)[1]
        d0, _ = verifier(a, entrees)
        if d0:
            print("  BANC INVALIDE : le calque relu sans mutation tire (%s)" % d0[0])
            return 1
        print("banc de mutations :")
        rate = 0
        muts, ecartees = mutations(entrees, a)
        for fam, nom, e in muts:
            d, _ = verifier(a, e)
            vu = [x for x in d if x.startswith(fam + " ")]
            if vu:
                print("  %s OK   %-45s -> %s" % (fam, nom, vu[0][2:][:70]))
            else:
                rate += 1
                print("  %s RATE %-45s -> %s" % (fam, nom, d[0] if d else "rien"))
        for fam, pourquoi in ecartees:
            print("  %s --   %s" % (fam, pourquoi))
        exercees = sorted({f for f, _n, _e in muts})
        print("%d/%d mutations rattrapees par la bonne famille ; familles exercees : %s"
              % (len(muts) - rate, len(muts), " ".join(exercees) or "aucune"))
        manquantes = [f for f in "ABCDE" if f not in exercees]
        if manquantes:
            # Le score plein ne vaut QUE pour les familles reellement mises a l'epreuve. Le dire
            # ici, et non en note de bas de page, est la seule facon que le lecteur ne prenne pas
            # « 13/13 » pour « les cinq familles sont prouvees sur ce calque ».
            print("  ATTENTION : famille(s) %s NON exercee(s) sur ce calque : le score ci-dessus ne les "
                  "couvre pas" % ", ".join(manquantes))
        if rate:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

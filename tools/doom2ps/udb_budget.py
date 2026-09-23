#!/usr/bin/env python3
"""udb_budget.py -- le CALQUE DE BUDGET : un PWAD jetable qui montre dans Ultimate Doom Builder
ce que chaque salle coute au peintre de la Saturn.

POURQUOI UN CALQUE A PART, ET PAS LA CARTE DE L'AUTEUR. Le seul canal qu'UDB sait colorer sans une
ligne de greffon est la LUMINOSITE des secteurs (mode *Brightness* de la vue 2D) -- or la
luminosite est deja une donnee de jeu que l'auteur regle a la main, et que `doom3d.py` convertit en
lumiere par sommet. Y ecrire le cout ecraserait son travail d'eclairage. D'ou le parti pris de ce
fichier : on ne touche JAMAIS a la carte de l'auteur, on emet A COTE une copie jetable dont la
luminosite, la texture de sol et le tag ne servent plus qu'a porter la mesure. L'auteur garde les
deux fichiers ouverts, edite l'un, relit l'autre.

CE QUE LE CALQUE PORTE -- TROIS CANAUX, LA MEME MESURE :
  * `light`    -> degrade continu 48..255 : la vue 2D en mode *Brightness* devient une carte de
                  chaleur de tout le niveau d'un coup d'oeil (docs/LEVEL_EDITING_PLAN.md §4).
  * `floorpic` -> un aplat BUDGET0..3 (vert / jaune / orange / rouge) livre DANS le PWAD entre
                  F_START et F_END : le mode visuel 3D d'UDB devient une carte de chaleur qu'on
                  PARCOURT A PIED, en format Doom binaire, sans UDMF et sans configuration.
                  (C'est plus simple que la voie « T1 - la couleur » du plan, qui demandait une
                  configuration UDMF pour teinter par `lightcolor` : un aplat suffit, parce que le
                  calque a le droit de perdre ses vraies textures. Le plan est corrige en ce sens.)
  * `tag`      -> le NOMBRE EXACT de cellules. L'auteur clique un secteur et lit la valeur, au lieu
                  de deviner une nuance de gris. -1 = pas de mesure (voir plus bas).
Les autres champs (hauteurs, plafond, type) et tous les autres lumps sont copies OCTET POUR OCTET,
et `verif_udb.py` le verifie. UNE SEULE EXCEPTION, et il faut la nommer : avec `--pire N` (8 par
defaut) le lump THINGS recoit N objets EN PLUS a la fin, aux pires positions debout ; il n'est donc
pas identique, il est prefixe par l'original. C'est ce que verifie la famille E, pas la famille A.

LES DEUX MESURES, ET POURQUOI LA VUE EST CELLE PAR DEFAUT.
  * `--metrique vue` (defaut) : le PIRE cone de `cout.carte` parmi les positions debout du secteur,
    c'est-a-dire ce que le peintre doit tenir quand on se tient LA. C'est la seule qui reponde a
    « est-ce que ca garde 30 images ? », parce que le cout d'une image est celui de tout ce qu'on
    VOIT, pas de la piece ou l'on est. Elle passe par `ordre.visibilite`, le calcul cher.
  * `--metrique propre` : les cellules que le secteur POSSEDE (`cout.cellules_par_secteur`). Elle
    ne dit pas le cout d'une image, mais elle est la seule ACTIONNABLE : elle designe la piece a
    degraisser. Les deux sont complementaires -- la premiere dit ou ca casse, la seconde dit quoi
    couper.

LES BORNES DE COULEUR SONT CELLES DU RETAIL, PAS CELLES DE LA LOI. `cout.PALIERS`
(470 / 896 / 1321 cellules) vient d'un build ASSERT et sur-estime la pente d'environ 1,5x en
NDEBUG ; passes a la meme moulinette, les 24 niveaux de PowerSlave commercial seraient declares
majoritairement sous les 30 images (mesure 23-09, `tools/lev_etalon.json`). Peindre une carte en
rouge a 1321 mentirait donc a l'auteur. Les bornes par defaut sont DERIVEES A L'EXECUTION de
`lev_etalon.json`, de sorte qu'elles suivent l'etalon si on le recalcule. Valeurs du 23-09, la
mediane etant ici `t[n//2]`, donc la superieure des deux valeurs centrales des 24 niveaux :
    cone mediane : min  452  mediane  920  max 1441
    cone p90     : min  785  mediane 1474  max 2156
    cone max     : min 1335  mediane 2157  max 3636
    cellules du plus gros secteur : min 166  mediane 306  max 1147
d'ou, pour `vue`, vert <= 920 < jaune <= 1474 < orange <= 2157 < rouge, et pour `propre`
166 / 306 / 1147. Le rouge veut dire « au-dela de ce que le niveau retail median montre a son pire
endroit », ce qui est une phrase qu'on peut defendre devant un auteur. `--bandes paliers` rend les
seuils de la loi a qui les veut.

CE QUI N'EST PAS MESURE EST PEINT COMME TEL -- il n'y a pas de silence :
  * BUDGETNA (bleu) : aucune position debout dans le secteur. Les positions sont sur une maille de
    64 u (`ordre.PAS`, a ne pas affiner), donc un secteur etroit peut n'en contenir aucune. Ce
    n'est PAS « pas cher ».
  * BUDGETXX (magenta) : le secteur Doom n'existe pas dans le `.LEV` -- le convertisseur l'a
    supprime (fusion, secteur vide, hors budget). Mesure sur E1M1 : 2 secteurs Doom sur 85, les
    219 secteurs .LEV retombant sur les 83 autres.

⚠ LE CALQUE EST JETABLE ET NE SE JOUE PAS. Son `tag` porte un nombre de cellules, pas un numero de
tag : le tester dans un port lui ferait faire n'importe quoi. Le lump texte `BUDGET` du PWAD le
redit a qui l'ouvrirait plus tard. L'outil refuse d'ecrire sur le WAD source.

CE QUE L'APPEL ECRIT -- QUATRE FICHIERS, POUR QU'IL N'Y AIT AUCUNE ETAPE DE CONFIGURATION :
le `.wad` (le calque), le `.csv` (secteur par secteur, auditable), le `.dbs` (les reglages qu'UDB
range a cote d'un WAD : configuration de jeu et WAD source en ressource) et un `.bat` d'un clic.
Le nom par defaut porte la METRIQUE, pour que le second calque n'ecrase pas le premier.
Le `.bat` existe EN PLUS du `.dbs` pour une raison mesuree (journal `UDBuilder.log`, 23-09) : UDB
relit bien le `gameconfig` du `.dbs`, mais sur le chemin d'ouverture par LIGNE DE COMMANDE il
n'applique pas sa liste de ressources.

Usage :
  python tools\\doom2ps\\udb_budget.py --wad DOOM1.WAD --map E1M1 --lev cd_doom\\E1M1.LEV
                                       --geom build\\doom2ps\\e1m1_geom3d.json
                                       [--out build\\udb\\E1M1_VUE.wad] [--csv F.csv]
                                       [--metrique vue|propre] [--bandes retail|paliers]
                                       [--fov 53] [--pire 8] [--garder-sols] [--plafonds]
                                       [--config Doom_DoomDoom.cfg] [--sans-dbs] [--udb Builder.exe]
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

import cout                                                            # noqa: E402
import lev                                                             # noqa: E402
import ordre                                                           # noqa: E402
import wad as wadmod                                                   # noqa: E402

ETALON = os.path.join(ROOT, "tools", "lev_etalon.json")

LUM_MIN = 48        # un secteur pas cher doit rester VISIBLE dans la vue 3D, pas noir
LUM_MAX = 255       # l'octet de luminosite Doom
TAG_NON_MESURE = -1

MARQUEUR_TYPE = 32000   # type d'objet inconnu : UDB le dessine ET le signale dans son verificateur
MARQUEUR_FLAGS = 7      # les trois niveaux de difficulte, pour qu'aucun filtre ne le cache

# Les aplats livres dans le PWAD : (nom, rvb vise). L'index de palette retenu est le plus proche
# dans le PLAYPAL du WAD source -- la palette Doom n'a ni bleu franc ni magenta, donc le rapport
# imprime l'index reellement choisi plutot que de laisser croire a la couleur visee.
APLATS = (
    ("BUDGET0", (0, 168, 0)),
    ("BUDGET1", (216, 200, 0)),
    ("BUDGET2", (232, 120, 0)),
    ("BUDGET3", (216, 0, 0)),
    ("BUDGETNA", (48, 48, 216)),
    ("BUDGETXX", (200, 0, 200)),
)
APL_NA = 4
APL_XX = 5

LEGENDE_NA = "bleu    : aucune position debout (maille de 64 u)"
LEGENDE_XX = "magenta : secteur absent du .LEV (supprime a la conversion)"


def legendes(metrique, bandes, b):
    """Ce que chaque couleur VEUT DIRE, construit depuis les bornes reellement employees.

    ⚠ CE N'ETAIT PAS UNE CONSTANTE, ET LA CROIRE CONSTANTE ETAIT UN MENSONGE (defaut trouve le
    24-09 par relecture contradictoire). Les quatre legendes etaient ecrites une fois pour toutes
    dans le style « entre mediane et p90 retail », qui decrit les quantiles du CONE. Or ces
    quantiles ne servent qu'en `--metrique vue` : `propre` tire ses bornes du plus gros SECTEUR
    retail (min / mediane / max) et `--bandes paliers` de la loi de cout. Le rapport imprimait donc,
    juste sous une ligne annoncant « <=166 / <=306 / <=1147 ... plus gros secteur », une legende
    parlant de p90 -- deux quantiles differents de deux populations differentes, colles l'un sous
    l'autre. L'auteur lisait « au-dessus de la mediane retail » pour des pieces qui etaient
    dessous. La legende se derive maintenant des memes trois nombres que la peinture."""
    if bandes == "paliers":
        f = [fps for _s, fps in cout.PALIERS]
        return ("vert    : <= %d cellules, le palier des %d images" % (b[0], f[0]),
                "jaune   : %d a %d, entre les paliers %d et %d images" % (b[0] + 1, b[1], f[0], f[1]),
                "orange  : %d a %d, entre les paliers %d et %d images" % (b[1] + 1, b[2], f[1], f[2]),
                "rouge   : au-dela de %d, sous le palier des %d images" % (b[2], f[2]))
    if metrique == "vue":
        return ("vert    : <= %d, sous la mediane des medianes de cone retail" % b[0],
                "jaune   : %d a %d, jusqu'a la mediane des p90 retail" % (b[0] + 1, b[1]),
                "orange  : %d a %d, jusqu'a la mediane des max retail" % (b[1] + 1, b[2]),
                "rouge   : au-dela de %d, au-dela du pire endroit du retail median" % b[2])
    return ("vert    : <= %d, sous le plus petit des plus gros secteurs retail" % b[0],
            "jaune   : %d a %d, jusqu'a la mediane des plus gros secteurs" % (b[0] + 1, b[1]),
            "orange  : %d a %d, jusqu'au plus gros secteur retail connu" % (b[1] + 1, b[2]),
            "rouge   : au-dela de %d, plus gros qu'aucun secteur retail" % b[2])

TEXTE = """CALQUE DE BUDGET -- FICHIER JETABLE, NE PAS JOUER, NE PAS EDITER.

Copie de la carte ou trois champs de secteur ont ete detournes pour porter une mesure :
  light    = degre de cout (mode Brightness de la vue 2D d'UDB)
  floorpic = BUDGET0..3 / BUDGETNA / BUDGETXX, aplats livres dans ce PWAD (vue 3D)
  tag      = le nombre exact de cellules du peintre SlaveDriver, -1 si non mesure

Tout le reste est copie octet pour octet depuis le WAD source. Editez VOTRE carte, pas celle-ci.
Produit par tools/doom2ps/udb_budget.py (depot SlaveDriver-Engine / Aguzzino).
"""


def declaration(a, b, marqueurs):
    """La ligne que `verif_udb.py` relit pour savoir COMMENT le calque a ete fait.

    Sans elle, la verification dependrait de drapeaux repasses a la main, et un drapeau oublie la
    ferait accuser un calque correct -- le pire des defauts pour un outil de controle, celui qui
    apprend a l'auteur a ignorer ce qu'il dit. Les bornes y figurent aussi : le verificateur les
    re-derive et les CONFRONTE, ce qui detecte un calque fabrique contre un autre etalon.

    ⚠ `pire` est le nombre de marqueurs REELLEMENT poses, pas celui demande. Les deux different en
    `--metrique propre`, qui n'enumere aucune position debout : declarer l'intention au lieu du
    fait rendait le calque coupable a sa propre verification (defaut trouve le 23-09, precisement
    parce que la declaration existait)."""
    return ("PARAMS metrique=%s bandes=%s fov=%g pire=%d garder_sols=%d plafonds=%d bornes=%d/%d/%d"
            % (a.metrique, a.bandes, a.fov, marqueurs, int(a.garder_sols), int(a.plafonds),
               b[0], b[1], b[2]))


# -- l'etalon retail ---------------------------------------------------------------------------
def _mediane(v):
    t = sorted(v)
    return t[len(t) // 2]


def bornes(metrique, bandes):
    """-> (b0, b1, b2, origine) : les trois frontieres de couleur, et d'ou elles viennent.

    DERIVEES A L'EXECUTION : si `lev_report.py --ecrire-etalon` recalcule l'etalon, le calque
    suit. `paliers` rend les seuils de la loi de cout, qui ne chiffrent QU'UNE vue."""
    if bandes == "paliers":
        if metrique != "vue":
            raise SystemExit("--bandes paliers n'a de sens que pour --metrique vue : "
                             "cout.PALIERS chiffre une VUE, pas les cellules d'un secteur.")
        b = [s for s, _fps in cout.PALIERS]
        return b[0], b[1], b[2], "loi de cout (build ASSERT, majore ~1,5x en NDEBUG)"
    e = json.load(open(ETALON, encoding="utf-8"))
    if metrique == "vue":
        return (_mediane([x["cone"][1] for x in e.values()]),
                _mediane([x["cone"][2] for x in e.values()]),
                _mediane([x["cone"][3] for x in e.values()]),
                "etalon retail : mediane des medianes / p90 / max de cone sur %d niveaux" % len(e))
    v = sorted(x["secteur_max"] for x in e.values())
    return v[0], _mediane(v), v[-1], \
        "etalon retail : plus gros secteur, min / mediane / max sur %d niveaux" % len(e)


def bande(v, b):
    if v is None:
        return None
    return 0 if v <= b[0] else 1 if v <= b[1] else 2 if v <= b[2] else 3


# -- la mesure ---------------------------------------------------------------------------------
def mesurer(chemin_lev, metrique, fov):
    """-> (par_secteur_lev, positions, (nm_secteurs, nm_murs)).

    `par_secteur_lev[i]` vaut None quand rien n'a pu etre mesure, jamais 0 : un secteur sans
    position debout n'est pas un secteur gratuit."""
    m = lev.parse_lev(chemin_lev)["level"]
    S, W, V = m["sectors"], m["walls"], m["vertices"]
    if metrique == "propre":
        return [float(c) for c in cout.cellules_par_secteur(S, W)], [], (len(S), len(W))
    geo, vues = ordre.visibilite(S, W, V)
    positions, _st = cout.carte(S, W, geo, vues, fov=fov)
    par = [None] * len(S)
    for _ex, _ez, s0, _nv, _tour, cone in positions:
        if par[s0] is None or cone > par[s0]:
            par[s0] = cone
    return par, positions, (len(S), len(W))


def agreger(par_lev, doom_sector, n_doom, metrique):
    """Ramene la mesure des secteurs .LEV sur les secteurs DOOM que l'auteur edite.

    `doom_sector[i]` est le secteur Doom d'ou vient le secteur .LEV `i` (doom3d.py:2588). La
    relation est un a plusieurs (E1M1 : 219 -> 83), d'ou deux regles distinctes : les cellules
    POSSEDEES s'additionnent -- c'est bien la meme piece qui les paie -- tandis que le pire cone
    se prend au MAXIMUM, parce qu'on ne se tient qu'a un endroit a la fois."""
    if len(doom_sector) != len(par_lev):
        raise SystemExit("le geom3d annonce %d secteurs, le .LEV en a %d : ils ne viennent pas de "
                         "la meme conversion" % (len(doom_sector), len(par_lev)))
    out = [None] * n_doom
    for i, v in enumerate(par_lev):
        d = doom_sector[i]
        if not (0 <= d < n_doom):
            raise SystemExit("doom_sector[%d] = %d hors des %d secteurs du WAD" % (i, d, n_doom))
        if v is None:
            continue
        out[d] = v if out[d] is None else (out[d] + v if metrique == "propre" else max(out[d], v))
    vus = set(doom_sector)
    return out, [d for d in range(n_doom) if d not in vus]


# -- le PWAD -----------------------------------------------------------------------------------
def _nom(b):
    return b.rstrip(b"\0").decode("latin-1").upper()


def lumps_de_carte(w, mapname):
    """Les lumps de la carte DANS L'ORDRE du repertoire, bruts. `Wad.map_lumps` rend un dict, donc
    perd l'ordre -- or on reecrit un WAD, et l'ordre des lumps d'une carte fait partie du format."""
    i = w.index[mapname.upper()]
    out = []
    for j in range(i + 1, min(i + 12, len(w.dir))):
        nm, fo, sz = w.dir[j]
        if nm in wadmod.MAP_LUMPS:
            out.append((nm, w.b[fo:fo + sz]))
        elif out:
            break
    return out


def index_palette(pal, rgb):
    """L'index le plus proche dans le PLAYPAL. La palette Doom n'a pas toutes les couleurs : le
    bleu et le magenta des deux aplats « non mesure » tombent sur ce qu'elle a de plus proche."""
    r, g, b = rgb
    best, bd = 0, 1 << 30
    for i, (pr, pg, pb) in enumerate(pal):
        d = (pr - r) ** 2 + (pg - g) ** 2 + (pb - b) ** 2
        if d < bd:
            best, bd = i, d
    return best


def luminosite(v, plein):
    if v is None:
        return 0
    x = min(1.0, max(0.0, float(v) / float(plein)))
    return int(round(LUM_MIN + (LUM_MAX - LUM_MIN) * x))


def construire_secteurs(brut, valeurs, absents, b, plein, garder_sols, plafonds, noms):
    """Reecrit le lump SECTORS. Seuls light / tag / floorpic (et ceilpic si `--plafonds`) changent :
    hauteurs, type et plafond de ciel sont recopies tels quels, pour que l'auteur reconnaisse sa
    carte au premier coup d'oeil."""
    n = len(brut) // 26
    out = bytearray()
    for k in range(n):
        fh, ch, fp, cp, _lt, sp, _tg = struct.unpack_from("<hh8s8shhh", brut, 26 * k)
        if k in absents:
            v, idx = None, APL_XX
        else:
            v = valeurs[k] if k < len(valeurs) else None
            d = bande(v, b)
            idx = APL_NA if d is None else d
        nom = noms[idx].encode("latin-1").ljust(8, b"\0")
        nfp = fp if garder_sols else nom
        # jamais le ciel : un plafond de ciel repeint transforme la salle en boite fermee, et
        # l'auteur ne reconnait plus sa carte dans la vue 3D.
        ncp = nom if (plafonds and _nom(cp) != "F_SKY1") else cp
        tag = TAG_NON_MESURE if v is None else max(-32768, min(32767, int(round(v))))
        out += struct.pack("<hh8s8shhh", fh, ch, nfp, ncp, luminosite(v, plein), sp, tag)
    return bytes(out), n


def construire_things(brut, positions, combien):
    """Ajoute `combien` marqueurs aux pires positions debout. Le repere du convertisseur est 1:1
    (doom3d.py:10 -- X = x_doom, Z = y_doom, sans facteur d'echelle), verifie sur E1M1 : les deux
    fichiers couvrent exactement -768..3808 en X et -4864..-2048 en Z."""
    if combien <= 0 or not positions:
        return brut, []
    pires = sorted(positions, key=lambda p: -p[5])[:combien]
    out = bytearray(brut)
    for ex, ez, _s0, _nv, _tour, _cone in pires:
        out += struct.pack("<5h", int(round(ex)), int(round(ez)), 0, MARQUEUR_TYPE, MARQUEUR_FLAGS)
    return bytes(out), pires


def ecrire_pwad(chemin, mapname, lumps, aplats, texte):
    """PWAD : en-tete de 12 octets, les donnees a la suite, le repertoire a la fin.

    Ecriture ATOMIQUE (temp + os.replace) : un calque a moitie ecrit serait ouvert par UDB comme un
    WAD corrompu, et l'auteur chercherait le bogue dans sa carte."""
    entrees = [(mapname.upper(), b"")] + list(lumps)
    entrees.append(("F_START", b""))
    entrees += [(nm, d) for nm, d in aplats]
    entrees.append(("F_END", b""))
    entrees.append(("BUDGET", texte.encode("latin-1", "replace")))
    corps = bytearray()
    rep = []
    off = 12
    for nm, d in entrees:
        rep.append((off, len(d), nm))
        corps += d
        off += len(d)
    buf = bytearray(struct.pack("<4sii", b"PWAD", len(entrees), 12 + len(corps)))
    buf += corps
    for fo, sz, nm in rep:
        buf += struct.pack("<ii8s", fo, sz, nm.encode("latin-1").ljust(8, b"\0"))
    d = os.path.dirname(os.path.abspath(chemin))
    if d:
        os.makedirs(d, exist_ok=True)
    tmp = chemin + ".tmp"
    with open(tmp, "wb") as f:
        f.write(buf)
    os.replace(tmp, chemin)
    return len(buf), len(entrees)


def ecrire_dbs(chemin_wad, mapname, wad_source, gameconfig):
    """Ecrit le `.dbs` -- le fichier de reglages qu'UDB range A COTE du WAD.

    SANS LUI, l'auteur doit declarer a la main la configuration de jeu et le WAD de ressources a
    chaque ouverture, faute de quoi le calque s'affiche sans aucune texture. Avec lui, il double-
    clique et tout est en place. Le format et les cles viennent de la SOURCE d'UDB (GPL-3), pas
    d'une supposition :
      * le chemin est `<wad sans .wad>.dbs`            -- MapManager.cs:1021
      * `type` / `gameconfig` / `strictpatches` a la racine, puis `maps.<CARTE>` -- MapOptions.cs:342-430
      * `resources.resourceN.{type,location,option1,option2,notfortesting}`      -- DataLocationList.cs:94-115
      * `type = 0` designe un WAD (1 = repertoire, 2 = PK3)                      -- DataLocation.cs:30-32
    La syntaxe est celle des `Configuration` de CodeImp, la meme que les `Configurations/*.cfg`
    livres avec l'editeur : `cle = valeur;` et des blocs en accolades, l'antislash double dans les
    chaines."""
    p = os.path.splitext(chemin_wad)[0] + ".dbs"
    src = os.path.abspath(wad_source).replace("\\", "\\\\")
    txt = (
        'type = "Doom Builder Map Settings Configuration";\n'
        'gameconfig = "%s";\n'
        'strictpatches = 0;\n\n'
        'maps\n{\n\t%s\n\t{\n\t\tresources\n\t\t{\n\t\t\tresource0\n\t\t\t{\n'
        '\t\t\t\ttype = 0;\n\t\t\t\tlocation = "%s";\n'
        '\t\t\t\toption1 = 0;\n\t\t\t\toption2 = 0;\n\t\t\t\tnotfortesting = 0;\n'
        '\t\t\t}\n\t\t}\n\t}\n}\n' % (gameconfig, mapname.upper(), src))
    with open(p + ".tmp", "w", encoding="latin-1", newline="\r\n") as f:
        f.write(txt)
    os.replace(p + ".tmp", p)
    return p


def trouver_udb(explicite):
    """Le chemin de `Builder.exe`, sans jamais coder en dur celui d'une machine.

    Ordre : l'argument, puis la variable d'environnement UDB, puis les emplacements
    conventionnels, exprimes a partir de %USERPROFILE% et de %LOCALAPPDATA%."""
    if explicite:
        return explicite if os.path.isfile(explicite) else None
    cand = [os.environ.get("UDB")]
    for base in (os.environ.get("USERPROFILE", ""), os.environ.get("LOCALAPPDATA", ""),
                 r"C:\Program Files", r"C:\Program Files (x86)"):
        if base:
            cand += [os.path.join(base, "Tools", "UltimateDoomBuilder", "Builder.exe"),
                     os.path.join(base, "UltimateDoomBuilder", "Builder.exe"),
                     os.path.join(base, "Programs", "UltimateDoomBuilder", "Builder.exe")]
    for c in cand:
        if c and os.path.isfile(c):
            return c
    return None


def ecrire_bat(chemin_wad, mapname, wad_source, gameconfig, builder):
    """Un lanceur d'un clic.

    POURQUOI EN PLUS DU `.dbs`. Mesure du 23-09, journal `UDBuilder.log` a l'appui : UDB relit bien
    le `gameconfig` du `.dbs` -- en y ecrivant `Boom_DoomDoom.cfg` l'editeur ouvre en Boom -- mais
    sur le chemin d'ouverture PAR LIGNE DE COMMANDE il n'applique PAS la liste de ressources : le
    journal ne montre alors que le calque lui-meme, et « None of the loaded resources define a color
    palette ». Le `.bat` passe donc `-RESOURCE WAD` explicitement. Syntaxe et jetons pris dans la
    source d'UDB (General.cs:845-950) : -CFG, -MAP, -RESOURCE <WAD|DIR|PK3> <chemin>."""
    p = os.path.splitext(chemin_wad)[0] + ".bat"
    # La variable UDB ne l'emporte que si elle DESIGNE QUELQUE CHOSE. Preferer aveuglement %UDB%
    # au chemin qu'on vient de valider fait echouer le lanceur sur une variable perimee, avec en
    # prime un message qui conseille de poser UDB... alors qu'elle est posee. Le chemin valide a
    # la generation est donc le repli, et il est toujours essaye.
    txt = (
        "@echo off\r\n"
        "rem Ouvre le calque de budget dans Ultimate Doom Builder, deja configure.\r\n"
        "rem Calque JETABLE : son tag porte des cellules, pas un numero de tag.\r\n"
        'set BUILDER=%s\r\n'
        'if not "%%UDB%%"=="" if exist "%%UDB%%" set BUILDER=%%UDB%%\r\n'
        'if not exist "%%BUILDER%%" (echo Builder.exe introuvable en "%%BUILDER%%" : '
        'posez la variable UDB sur un Builder.exe existant. & pause & exit /b 1)\r\n'
        'start "" "%%BUILDER%%" "%s" -MAP %s -CFG %s -RESOURCE WAD "%s"\r\n'
        % (builder, os.path.abspath(chemin_wad), mapname.upper(), gameconfig,
           os.path.abspath(wad_source)))
    with open(p + ".tmp", "w", encoding="latin-1", newline="") as f:
        f.write(txt)
    os.replace(p + ".tmp", p)
    return p


def ecrire_csv(chemin, n_doom, valeurs, absents, doom_sector, b, plein):
    cpt = {}
    for i, d in enumerate(doom_sector):
        cpt.setdefault(d, []).append(i)
    with open(chemin + ".tmp", "w", encoding="utf-8", newline="") as f:
        f.write("secteur_doom;cellules;bande;luminosite;secteurs_lev\n")
        for k in range(n_doom):
            v = valeurs[k]
            d = bande(v, b)
            f.write("%d;%s;%s;%d;%s\n" % (
                k, "" if v is None else "%d" % round(v),
                "XX" if k in absents else ("NA" if d is None else str(d)),
                0 if k in absents else luminosite(v, plein),
                " ".join(str(x) for x in cpt.get(k, []))))
    os.replace(chemin + ".tmp", chemin)


# -- rendu -------------------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description="calque de budget pour Ultimate Doom Builder")
    ap.add_argument("--wad", required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--lev", required=True)
    ap.add_argument("--geom", required=True, help="le *_geom3d.json de la MEME conversion")
    ap.add_argument("--out", default=None)
    ap.add_argument("--csv", default=None)
    ap.add_argument("--metrique", choices=("vue", "propre"), default="vue")
    ap.add_argument("--bandes", choices=("retail", "paliers"), default="retail")
    ap.add_argument("--fov", type=float, default=cout.FOV)
    ap.add_argument("--pire", type=int, default=8,
                    help="marqueurs aux N pires positions debout (0 = aucun)")
    ap.add_argument("--garder-sols", action="store_true",
                    help="ne pas remplacer les textures de sol : la luminosite porte seule la mesure")
    ap.add_argument("--plafonds", action="store_true",
                    help="repeindre aussi les plafonds (jamais F_SKY1)")
    ap.add_argument("--config", default="Doom_DoomDoom.cfg",
                    help="configuration de jeu UDB ecrite dans le .dbs")
    ap.add_argument("--sans-dbs", action="store_true",
                    help="ne pas ecrire le .dbs : l'auteur declarera lui-meme config et ressources")
    ap.add_argument("--udb", default=None,
                    help="chemin de Builder.exe ; sinon la variable UDB, sinon les emplacements usuels")
    a = ap.parse_args(argv)

    # le nom porte la METRIQUE : les deux calques d'une meme carte sont complementaires et l'auteur
    # les ouvre l'un apres l'autre, donc un nom commun ferait silencieusement ecraser le premier.
    out = a.out or os.path.join(ROOT, "build", "udb",
                                "%s_%s.wad" % (a.map.upper(), a.metrique.upper()))
    # `normcase` et non `abspath` seul : sous Windows `doom1.wad` et `DOOM1.WAD` sont le MEME
    # fichier, et une garde sensible a la casse sur un systeme qui ne l'est pas ne garde rien --
    # elle laisserait ecraser le WAD source de l'auteur, qui est justement le seul fichier
    # irremplacable de la chaine. (Defaut trouve le 24-09 par relecture contradictoire.)
    if os.path.normcase(os.path.abspath(out)) == os.path.normcase(os.path.abspath(a.wad)):
        raise SystemExit("refus d'ecrire sur le WAD source : le calque est un fichier A PART.")

    w = wadmod.Wad(a.wad)
    if a.map.upper() not in w.index:
        raise SystemExit("%s n'a pas de carte %s" % (a.wad, a.map))
    lumps = lumps_de_carte(w, a.map)
    par_nom = dict(lumps)
    if "SECTORS" not in par_nom:
        raise SystemExit("carte sans lump SECTORS")
    n_doom = len(par_nom["SECTORS"]) // 26

    g = json.load(open(a.geom, encoding="utf-8"))
    if "doom_sector" not in g:
        raise SystemExit("%s ne porte pas doom_sector : ce n'est pas un geom3d de doom2ps" % a.geom)

    par_lev, positions, (n_lev, n_murs) = mesurer(a.lev, a.metrique, a.fov)
    valeurs, absents = agreger(par_lev, g["doom_sector"], n_doom, a.metrique)
    b = bornes(a.metrique, a.bandes)
    plein = b[2]
    abs_set = set(absents)

    pal = w.playpal(0)
    noms = [nm for nm, _rgb in APLATS]
    idxs = [index_palette(pal, rgb) for _nm, rgb in APLATS]
    aplats = [(nm, bytes([ix]) * 4096) for nm, ix in zip(noms, idxs)]

    sect, _n = construire_secteurs(par_nom["SECTORS"], valeurs, abs_set, b, plein,
                                   a.garder_sols, a.plafonds, noms)
    things, pires = construire_things(par_nom.get("THINGS", b""), positions, a.pire)
    finaux = [(nm, sect if nm == "SECTORS" else things if nm == "THINGS" else d)
              for nm, d in lumps]

    taille, n_lumps = ecrire_pwad(out, a.map, finaux, aplats,
                                  TEXTE + "\n" + declaration(a, b, len(pires)) + "\n")
    csv = a.csv or os.path.splitext(out)[0] + ".csv"
    ecrire_csv(csv, n_doom, valeurs, abs_set, g["doom_sector"], b, plein)
    dbs = None if a.sans_dbs else ecrire_dbs(out, a.map, a.wad, a.config)
    builder = trouver_udb(a.udb)
    bat = ecrire_bat(out, a.map, a.wad, a.config, builder) if builder else None

    # -- le rapport ----------------------------------------------------------------------------
    print("calque   : %s (%d o, %d lumps)" % (out, taille, n_lumps))
    print("source   : %s / %s -- %d secteurs Doom ; .LEV %d secteurs, %d murs"
          % (os.path.basename(a.wad), a.map.upper(), n_doom, n_lev, n_murs))
    print("metrique : %s -- %s" % (
        a.metrique,
        "pire cone a %.0f deg par position debout (cout.carte)" % a.fov if a.metrique == "vue"
        else "cellules possedees par le secteur (cout.cellules_par_secteur)"))
    print("bandes   : <=%d / <=%d / <=%d ; %s" % (b[0], b[1], b[2], b[3]))
    tot = [0, 0, 0, 0]
    na = 0
    for k in range(n_doom):
        if k in abs_set:
            continue
        d = bande(valeurs[k], b)
        if d is None:
            na += 1
        else:
            tot[d] += 1
    leg = legendes(a.metrique, a.bandes, b)
    for i in range(4):
        print("  %-9s idx %3d  %-62s %3d secteurs" % (noms[i], idxs[i], leg[i], tot[i]))
    print("  %-9s idx %3d  %-62s %3d secteurs" % (noms[APL_NA], idxs[APL_NA], LEGENDE_NA, na))
    print("  %-9s idx %3d  %-62s %3d secteurs" % (noms[APL_XX], idxs[APL_XX], LEGENDE_XX,
                                                  len(absents)))
    if a.metrique == "vue" and na:
        # A DIRE A HAUTE VOIX. La maille de 64 u d'`ordre.PAS` rate les secteurs etroits -- couloirs,
        # marches, seuils de porte -- et la part n'a rien de marginale : mesure 23-09 sur l'episode 1
        # converti, de 18 secteurs non mesures sur 147 (E1M9, 12 %) a 57 sur 170 (E1M7, 34 %), en
        # passant par 17 sur 83 (E1M1) et 83 sur 250 (E1M6). Ces secteurs-la ne sont pas peints faute
        # de mesure, et c'est `--metrique propre` qui les couvre, elle qui n'a pas de trou. Taire ce
        # chiffre laisserait croire a une carte complete.
        print("           dont %d non mesures (%.0f %% des secteurs convertis) : la maille de %.0f u "
              "rate les secteurs\n           etroits -- pour ceux-la, relancer avec --metrique propre"
              % (na, 100.0 * na / max(1, n_doom - len(absents)), ordre.PAS))
    mes = sorted(((k, valeurs[k]) for k in range(n_doom) if valeurs[k] is not None),
                 key=lambda t: -t[1])
    if mes:
        print("pires    : " + ", ".join("s%d=%d" % (k, round(v)) for k, v in mes[:8]))
    if pires:
        print("marqueurs: %d objets de type %d ; la 1re position a (%d, %d) = %d cellules"
              % (len(pires), MARQUEUR_TYPE, round(pires[0][0]), round(pires[0][1]), pires[0][5]))
    elif a.pire:
        print("marqueurs: aucun -- `--metrique propre` n'enumere pas de position debout, il n'y a "
              "donc rien a classer")
    print("csv      : %s" % csv)
    if dbs:
        print("reglages : %s -- %s + %s en ressource ; le calque s'ouvre deja configure"
              % (os.path.basename(dbs), a.config, os.path.basename(a.wad)))
    if bat:
        print("lanceur  : %s -- un clic, vers %s" % (os.path.basename(bat), builder))
    else:
        print("lanceur  : aucun -- Builder.exe introuvable ; poser la variable UDB ou passer --udb")
    print("ATTENTION : calque JETABLE. Son tag porte des cellules, pas un numero de tag --"
          " ne pas le jouer, ne pas l'editer.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

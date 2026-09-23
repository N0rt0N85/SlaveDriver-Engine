#!/usr/bin/env python3
"""levdata.py -- lecture d'un .LEV SANS AUCUNE DEPENDANCE, pour l'extension Blender.

POURQUOI UNE DEUXIEME LECTURE. `tools/lev.py` est le lecteur de reference et le reste : c'est lui
qui fait foi sur le format. Mais une extension Blender s'installe seule, chez un graphiste qui n'a
pas forcement le depot ; elle ne peut donc pas `import lev`. Ce module relit le fichier de son
cote, avec la seule bibliotheque standard.

CE N'EST PAS UNE COPIE NON GARDEE. `tools/blender/verif_io_lev.py` confronte les deux lectures sur
tous les .LEV disponibles et refuse le moindre ecart : champ par champ sur les secteurs, murs,
sommets, faces et objets, plus le decoupage des tuiles. Le jour ou le format bouge, le verificateur
tombe. C'est la meme discipline que `verif_doom.py`, qui relit le FICHIER plutot que de croire les
intermediaires du convertisseur.

CE QU'IL LIT, dans l'ordre ou le moteur lit (runLevel, SRUINS.C:2382-2390) : le ciel (saute), le
bloc niveau, les sons (sautes), les palettes, les tuiles, les sequences (sautees). L'importeur
n'a besoin que du bloc niveau, des palettes et des tuiles -- mais il faut traverser les sons
exactement pour arriver aux palettes, donc tout le chemin est parcouru.

REPERE DU MONDE (verifie 23-09) : x, y, z du .LEV sont des UNITES MONDE ENTIERES rangees en shorts ;
le moteur les elargit en 16.16 a l'usage (`F(a)=(a)<<16`, UTIL.H:134, getVertex UTIL.C:33-35). X et
Z sont horizontaux, +Y est le HAUT (project_point nie y, wallasm_gnu.s:61 ; findFloorDistance prend
le mur dont normal[1] > 0, UTIL.C:41).

COULEUR : un mot de palette vaut 0x8000 | (b << 10) | (g << 5) | r -- du BGR555, PAS du RGB555
(UTIL.H:204, decompose dans l'autre sens a PIC.C:1055-1057). Le bit 15 est le bit de mode RGB du
VDP1, jamais de l'alpha. La transparence est l'INDICE 0.
"""
import struct

OT_NMTYPES = 227            # SLEVEL.H:41-100
MAXCUTSECTORS = 128         # SLEVEL.H:191

SECTOR_FMT = '>i3hhhhhhbbh'
WALL_FMT = '>iiiiihHhhHHHHHHhHhBB'
VERTEX_FMT = '>hhhbb'
FACE_FMT = '>HHHHBb'
OBJECT_FMT = '>hh'
PB_FMT = '>hhhhhhhhh'
PBVERT_FMT = '>HBb'
WAVEVERT_FMT = '>iihhhh'
WAVEFACE_FMT = '>hhhh'

# Drapeaux de tuile (SLEVEL.H:216) recombines comme loadTileSet les teste (PIC.C:1171-1200).
TF_16_64, TF_VDP2, TF_8RLE_64, TF_8RLE_32, TF_16_32, TF_16RLE_64 = 0x32, 0x01, 0x6A, 0x6C, 0x34, 0x72
# Les seules classes que le moteur accepte sous une cellule de geometrie (WALLS.C:1981, :2049).
GEOMETRIE = (TF_16_64, TF_16RLE_64)

# WALLS.C:1153 -- permutation des 4 coins d'une cellule vers les 4 sommets du quad VDP1.
# Les quatre dernieres lignes sont les miroirs ; c'est le seul « placage libre » du moteur.
PATTERN = ((0, 1, 2, 3), (1, 2, 3, 0), (2, 3, 0, 1), (3, 0, 1, 2),
           (0, 3, 2, 1), (1, 0, 3, 2), (2, 1, 0, 3), (3, 2, 1, 0))
# Coin de texture de chaque emplacement du quad, en (u, v) avec v vers le BAS.
COIN_UV = ((0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0))


class _R:
    def __init__(self, b):
        self.b, self.p, self.n = b, 0, len(b)

    def read(self, n):
        if n < 0 or self.p + n > self.n:
            raise EOFError('lecture de %d octets a 0x%x au-dela de %d' % (n, self.p, self.n))
        d = self.b[self.p:self.p + n]
        self.p += n
        return d

    def i32(self):
        return struct.unpack('>i', self.read(4))[0]

    def i16(self):
        return struct.unpack('>h', self.read(2))[0]

    def skip(self, n):
        st = self.p
        self.read(n)
        return st

    def recs(self, fmt, count):
        sz = struct.calcsize(fmt)
        out = [struct.unpack_from(fmt, self.b, self.p + i * sz) for i in range(count)]
        self.skip(count * sz)
        return out


def _sky(r):
    """initPlax PLAX.C:84-115 : 256 shorts de palette, 512, 256, le bitmap, 320 ints."""
    r.skip(512)
    w, h = r.i32(), r.i32()
    if (w, h) != (512, 256):
        raise ValueError('ciel %dx%d au lieu de 512x256 (PLAX.C:91-94)' % (w, h))
    r.skip(512 * 256)
    r.skip(320 * 4)


def _niveau(r):
    """loadLevel LEVEL.C:37-68 : int size, 14 ints d'en-tete, puis les LOADPART dans l'ordre."""
    size = r.i32()
    if not (0 < size < 900000):
        raise ValueError('taille du bloc niveau %d hors de ]0, 900000[ (LEVEL.C:65-66)' % size)
    debut = r.p
    h = struct.unpack('>14i', r.read(56))
    (nS, nW, nV, nF, nT, nL, nO, nOP, nPB, nPBW, nPBV, nWV, nWF, nCut) = h

    secs = r.recs(SECTOR_FMT, nS)
    walls = r.recs(WALL_FMT, nW)
    verts = r.recs(VERTEX_FMT, nV)
    faces = r.recs(FACE_FMT, nF)
    objs = r.recs(OBJECT_FMT, nO)
    r.recs(PB_FMT, nPB)
    r.recs(PBVERT_FMT, nPBV)
    r.recs(WAVEVERT_FMT, nWV)
    r.recs(WAVEFACE_FMT, nWF)
    r.recs('>h', nPBW)
    op_off = r.skip(nOP)
    objparams = r.b[op_off:op_off + nOP]
    t_off = r.skip(nT)
    texture = r.b[t_off:t_off + nT]
    l_off = r.skip(nL)
    vlight = r.b[l_off:l_off + nL]
    r.skip(nCut * MAXCUTSECTORS)
    # Blocs optionnels ajoutes par notre convertisseur apres cutPlane (paires d'ordre, puis table
    # de rejet) : l'en-tete n'a plus de champ libre, leur presence se lit a la taille restante
    # (tools/ordre.py). L'importeur ne s'en sert pas -- il suffit de sauter a la fin du bloc, qui
    # vaut `size` octets a partir de l'en-tete (size == 56 + somme des LOADPART, LEVEL.C:65-66).
    r.p = debut + size
    return dict(
        size=size,
        sectors=[dict(object=s[0], center=list(s[1:4]), floorLevel=s[4], firstWall=s[5],
                      lastWall=s[6], light=s[7], flags=s[8], cutIndex=s[9], cutChannel=s[10],
                      rejectClass=s[11]) for s in secs],
        walls=[dict(normal=list(w[0:3]), d=w[3], object=w[4], flags=w[5], textures=w[6],
                    firstFace=w[7], lastFace=w[8], firstVertex=w[9], lastVertex=w[10],
                    v=list(w[11:15]), nextSector=w[15], firstLight=w[16], pixelLength=w[17],
                    tileLength=w[18], tileHeight=w[19]) for w in walls],
        vertices=[dict(x=v[0], y=v[1], z=v[2], light=v[3]) for v in verts],
        faces=[dict(v=list(f[0:4]), tile=f[4]) for f in faces],
        objects=[dict(type=o[0], firstParam=o[1]) for o in objs],
        objectParams=objparams, texture=texture, vertexLight=vlight)


def _sons(r):
    """loadSoundSet SOUND.C:231-241 puis loadSound :199-228."""
    n = r.i32()
    if n != OT_NMTYPES:
        raise ValueError('carte des sons %d != OT_NMTYPES (SOUND.C:233)' % n)
    r.skip(2 * n)
    for _ in range(r.i32()):
        size = r.i32()
        r.i32(), r.i32(), r.i32()
        r.skip(size)


def _tuiles(r):
    """loadPalletes PIC.C:1130-1141 puis loadTileSet :1171-1200.

    La palette p commence au mot d'indice 1 + 256*p : le +1 saute l'objectPaletteNumber en tete
    (PIC.C:936, :1146). npalettes = (taille - 2) / 512 -- ne JAMAIS le deduire du plus grand palNm.
    """
    psz = r.i32()
    if not (0 < psz < 1024 * 1024):
        raise ValueError('bloc de palettes %d hors de ]0, 1 Mo[ (PIC.C:1131)' % psz)
    bloc = r.read(psz)
    obj_pal = struct.unpack_from('>H', bloc, 0)[0]
    npal = (psz - 2) // 512
    palettes = [list(struct.unpack_from('>256H', bloc, 2 + 512 * p)) for p in range(npal)]
    # PIC.C:1147 : l'entree 255 de la palette OBJET est forcee a 0xffff en RAM avant la copie en
    # CRAM. Le disque ne la porte presque jamais ainsi (1 palette sur 538 mesuree) ; il faut donc
    # la forcer ici, sinon la derniere couleur des sprites est fausse.
    if 0 <= obj_pal < npal:
        palettes[obj_pal][255] = 0xFFFF

    tuiles = []
    for _ in range(r.i32()):
        f = r.i16()
        if f == TF_16_64:
            pal = r.i16()
            tuiles.append(dict(flags=f, palNm=pal, pixels=r.read(4096), w=64, h=64))
        elif f == TF_16_32:
            pal = r.i16()
            tuiles.append(dict(flags=f, palNm=pal, pixels=r.read(1024), w=32, h=32))
        elif f == TF_VDP2:
            r.skip(8)
            tuiles.append(dict(flags=f, palNm=-1, pixels=b'', w=0, h=0))
        elif f in (TF_8RLE_64, TF_8RLE_32, TF_16RLE_64):
            pal = r.i16()
            n = r.i16()
            wh = 32 if f == TF_8RLE_32 else 64          # PIC.C:405-408 : par CLASSE, pas par bit
            tuiles.append(dict(flags=f, palNm=pal, rle=r.read(n), w=wh, h=wh))
        else:
            raise ValueError('drapeau de tuile 0x%x inconnu (PIC.C:1197)' % f)
    return dict(objectPalette=obj_pal, palettes=palettes, tiles=tuiles)


def lire(chemin):
    """-> dict(level=..., palettes=..., tiles=..., objectPalette=...)."""
    with open(chemin, 'rb') as f:
        r = _R(f.read())
    _sky(r)
    niveau = _niveau(r)
    _sons(r)
    t = _tuiles(r)
    return dict(level=niveau, palettes=t['palettes'], tiles=t['tiles'],
                objectPalette=t['objectPalette'])


# ------------------------------------------------------------------ pixels
def unrle(data, nm_pixels):
    """Decodeur RLE, copie mot pour mot de PIC.C:409-421 : des blocs
    [n0 zeros][n1 litteraux][n1 octets] repetes TANT QUE outSize < nmPixels -- pas de bloc final."""
    out = bytearray()
    i = 0
    while len(out) < nm_pixels:
        z = data[i]
        lit = data[i + 1]
        i += 2
        out += b'\0' * z
        out += data[i:i + lit]
        i += lit
    return bytes(out[:nm_pixels])


def indices(tuile):
    """Les octets d'INDICE de palette d'une tuile. « 16BPP » nomme le mode couleur du VDP1, pas le
    stockage : tous les formats rangent 8 bits par pixel (PIC.C:937, :542)."""
    if 'pixels' in tuile:
        return tuile['pixels']
    return unrle(tuile['rle'], tuile['w'] * tuile['h'])


def palette_de(tuile, donnees):
    """La palette que le MOTEUR utilise reellement pour cette tuile.

    PIEGE. Les tuiles 0x6a et 0x6c portent un palNm que le chargeur LIT ET JETTE : addPic recoit
    NULL (PIC.C:996, :1010) et le VDP1 les tire en mode banque de couleurs CRAM, banque 0, qui est
    la palette OBJET (WALLS.C:4525, :4356, PIC.C:1149). Honorer leur palNm recolorie faux la
    plupart des sprites retail -- sur TOMB.LEV, 237 des 414 tuiles 0x6a annoncent la palette 13,
    qui differe de la palette 0 sur ses 255 entrees. Les 0x72, elles, gardent bien la leur
    (PIC.C:1024-1025)."""
    pals = donnees['palettes']
    if tuile['flags'] in (TF_8RLE_64, TF_8RLE_32):
        return pals[donnees['objectPalette']]
    p = tuile['palNm']
    return pals[p] if 0 <= p < len(pals) else pals[0]


def rgba(tuile, donnees, retourner=True):
    """-> (largeur, hauteur, liste plate de flottants RGBA en 0..1).

    `retourner` empile les lignes DU BAS VERS LE HAUT, ce que `Image.pixels` de Blender attend,
    alors que la tuile est rangee en lignes du haut vers le bas (PIC.C:556-565, :828 : row-major,
    x le plus rapide, pas de 64)."""
    if tuile['flags'] == TF_VDP2:
        return 0, 0, []
    px = indices(tuile)
    pal = palette_de(tuile, donnees)
    w, h = tuile['w'], tuile['h']
    # Table des 256 couleurs decodee une fois : BGR555, indice 0 transparent.
    table = []
    for i, v in enumerate(pal):
        if i == 0:
            table.append((0.0, 0.0, 0.0, 0.0))
        else:
            table.append(((v & 0x1F) / 31.0, ((v >> 5) & 0x1F) / 31.0,
                          ((v >> 10) & 0x1F) / 31.0, 1.0))
    out = []
    lignes = range(h - 1, -1, -1) if retourner else range(h)
    for y in lignes:
        base = y * w
        for x in range(w):
            out.extend(table[px[base + x]])
    return w, h, out


# ------------------------------------------------------------------ geometrie
def cellules_du_mur(mur, niveau):
    """-> [(quad de 4 index de sommet GLOBAUX, index de tuile, uv des 4 coins)].

    LES DEUX SEULES FORMES QU'UN MUR PEUT PRENDRE (SLEVEL.H:150-190, WALLS.C:1783-1900 / 2018-2076) :
      * WALLFLAG_PARALLELOGRAM (0x01) : une grille tileLength x tileHeight posee sur le quad
        v[0..3]. Les sommets ne sont PAS dans le fichier, ils se calculent ; la cellule (h, w) lit
        sa paire [motif, tuile] a texture[wall.textures + 2*(h*tileLength + w)] (WALLS.C:1893).
      * liste de faces (firstFace >= 0) : des quads libres dont les v[] sont RELATIFS a
        wall.firstVertex (WALLS.C:2021-2040, :922) et qui portent leur tuile directement.
      * firstFace == -1 et pas de drapeau : aucune geometrie -- un portail ou un trou de ciel.

    Le mode grille rend des positions et non des index : l'appelant cree les sommets.
    """
    if mur['flags'] & 0x01:
        return None                      # la grille se construit par positions, voir grille()
    if mur['firstFace'] < 0:
        return []
    base = mur['firstVertex']
    out = []
    for fi in range(mur['firstFace'], mur['lastFace'] + 1):
        f = niveau['faces'][fi]
        out.append(([base + v for v in f['v']], f['tile'], COIN_UV))
    return out


def grille(mur, niveau):
    """-> [(4 positions (x, y, z), index de tuile, uv des 4 coins)] pour un mur en parallelogramme.

    Le pas est (v1 - v0) / tileLength en largeur et (v2 - v1) / tileHeight en hauteur -- c'est
    (v2 - v1) et NON (v3 - v0) que le moteur prend (WALLS.C:1832-1837) ; les deux coincident parce
    qu'un mur de ce type est un parallelogramme exact (mesure : aucun ecart sur 2 638 murs).
    ⚠ Le pas n'est PAS de 64 unites : tileLength et tileHeight sont des comptes ARRONDIS et la
    tuile est etiree sur la cellule (largeur par tuile mesuree de 8 a 90,5 sur le retail, de 3,6 a
    256 sur le build Doom). Ne jamais re-deriver ces comptes d'une longueur."""
    V = niveau['vertices']
    TEX = niveau['texture']
    p = [(V[i]['x'], V[i]['y'], V[i]['z']) for i in mur['v']]
    tl, th = mur['tileLength'], mur['tileHeight']
    if tl <= 0 or th <= 0:
        return []
    dW = tuple((p[1][k] - p[0][k]) / float(tl) for k in range(3))
    dH = tuple((p[2][k] - p[1][k]) / float(th) for k in range(3))

    def coin(cw, ch):
        return tuple(p[0][k] + cw * dW[k] + ch * dH[k] for k in range(3))

    out = []
    for h in range(th):
        for w in range(tl):
            j = mur['textures'] + 2 * (h * tl + w)
            if j + 1 >= len(TEX):
                continue
            motif, tuile = TEX[j], TEX[j + 1]
            if motif > 7:
                motif = 0
            # Les 4 coins de grille, dans l'ordre 0..3 du moteur ; le motif dit dans quel
            # EMPLACEMENT du quad chaque coin atterrit, donc quel coin de texture il porte.
            coins = [coin(w, h), coin(w + 1, h), coin(w + 1, h + 1), coin(w, h + 1)]
            uv = [COIN_UV[PATTERN[motif][k]] for k in range(4)]
            out.append((coins, tuile, uv))
    return out


def lumiere_de_grille(mur, niveau, h, w):
    """La lumiere d'un coin de grille. ⚠ Le pas de la grille de lumiere n'est PAS celui des
    cellules : (tileHeight + 1) x (tileLength + 1) entrees a partir de firstLight."""
    VL = niveau['vertexLight']
    j = mur['firstLight'] + h * (mur['tileLength'] + 1) + w
    return VL[j] if 0 <= j < len(VL) else 0

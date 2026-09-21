#!/usr/bin/env python3
"""mkdat.py -- le corps des autres joueurs (PowerSlave, multi) : manifeste de planche.py -> PSPLAYER.DAT.

planche.py decoupe les figures et ecrit build/psplayer/manifeste.json (sequences dans l'ordre du
moteur, pieces, miroirs, ancres aux pieds).  Ce script en fait ce que le moteur dessine : des tuiles
64x64 8 bpp en RLE (le format 0x6A des sprites, PIC.C unRle), des trames et des morceaux au format
des .LEV (SLEVEL.H sFrameType / sChunkType), et une table de sequences.  PSMULTI.C les charge au
debut d'un niveau joue a plusieurs, apres tout le reste (ps_loadBody) ; le solo n'en lit pas un
octet.

Deux resolutions dans le meme fichier, la DEMI d'abord (le fichier ne se lit que vers l'avant,
FILE.H n'a pas de fs_seek : on peut lire la demie et l'ecraser par la pleine, pas l'inverse).
MESURE 2026-09-21 (sortie de ce script) : pleine ~80 Ko, demie ~22 Ko.  Un niveau lourd de
PowerSlave n'a que quelques dizaines de Ko une fois ses jeux de parcours du split alloues
(memoire powerslave-memory-budget : 11-59 Ko) : la pleine resolution ne tient pas partout, la
demie presque partout, et le moteur prend la meilleure qui tient (sinon le monstre du niveau,
comme avant).  La demie est dessinee a l'echelle 2 (sprite->scale double) : la meme taille a
l'ecran, deux fois moins de detail -- dans une vue de 160 px un joueur a 256 u fait ~45 px de haut,
la ou la demie en porte 42.

Palette : la sienne, 255 couleurs (0 = transparent), quantifiee une fois ici.  Le moteur la
ramene au chargement sur la palette OBJET du niveau (couleur la plus proche, PSMULTI.C) : la
palette objet de PowerSlave change a chaque niveau, et c'est a travers elle que les teintes des
joueurs sont construites (PIC.C buildTintedBank, a partir de la banque 0) -- un corps dans sa
propre banque CRAM n'aurait pas eu de teinte, et la CRAM est pleine en split.

Format (grand-boutien, tout aligne sur 4) :
  "PSPL", version 1, nmVariants 2
  par variante : scale (Fixed32 de sprite->scale), taille du bloc
  bloc : palette 256 x u16 (RGB du VDP, bit 15 mis ; l'entree 0 inutilisee)
         nmTiles, nmFrames, nmChunks, nmSequences -- des ENREGISTREMENTS ecrits : trames et
         sequences portent chacune leur borne de fin (SLEVEL.H : « there is always an extra entry »)
         tuiles : nmTiles x (u16 taille, u16 0, RLE, bourrage a 4)
         trames : nmFrames x sFrameType (chunkIndex, flags 0, sound -1, pad), la derniere = borne
         morceaux : nmChunks x sChunkType (chunkx, chunky, tile relative, flags bit 0 = miroir)
         sequences : nmSequences shorts (premiere trame de chacune, la derniere = borne), bourrage
Ordre des sequences (PSMULTI.C PSB_*) : repos, course, visee, tir, douleur (8 vues chacune, vue =
getFacingAngle), puis mort (une seule, non orientee) = 41 sequences.

Usage : python tools/psplayer/mkdat.py [--manifeste build/psplayer/manifeste.json] [--sortie cd/PSPLAYER.DAT]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

ICI = os.path.dirname(os.path.abspath(__file__))
RACINE = os.path.dirname(os.path.dirname(ICI))
sys.path.insert(0, os.path.join(RACINE, 'tools', 'doom2ps'))
import rle8                                                    # noqa: E402  (RLE 0x6A, PIC.C unRle)

CELL = 64
SCALE_PLEINE = 48000            # SPRITE.C newSprite : 0,732 u par pixel, l'echelle de planche.py
ANIMS = ('repos', 'course', 'visee', 'tir', 'douleur')        # 8 vues chacune
MORT = 'mort'                                                 # une sequence, non orientee


def rgb555(r, g, b):
    """RGB() du moteur (UTIL.H) : bit 15 | b << 10 | g << 5 | r, 5 bits par canal."""
    return 0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def charger(man, dossier, facteur):
    """-> {nom: (rgba ndarray, x, y, bourrage)} a l'echelle 1/facteur.  La demie : moyenne 2x2 des pixels
    OPAQUES (la transparence ne teinte pas le bord), opaque si au moins 2 des 4 le sont."""
    out = {}
    for nom, inf in man['images'].items():
        im = np.asarray(Image.open(os.path.join(dossier, inf['png'])).convert('RGBA')).astype(np.float32)
        x, y = inf['x'], inf['y']
        py = 0
        if facteur == 2:
            h, w = im.shape[:2]
            # l'origine reste aux pieds : on aligne la grille 2x2 sur x et y PAIRS du monde
            px, py = x & 1, y & 1
            im = np.pad(im, ((py, (h + py) & 1), (px, (w + px) & 1), (0, 0)))
            x, y = (x - px) // 2, (y - py) // 2
            h, w = im.shape[:2]
            b = im.reshape(h // 2, 2, w // 2, 2, 4)
            a = (b[..., 3] > 127).astype(np.float32)
            n = a.sum(axis=(1, 3))
            rgb = (b[..., :3] * a[..., None]).sum(axis=(1, 3)) / np.maximum(n, 1)[..., None]
            im = np.concatenate([rgb, np.where(n >= 2, 255.0, 0.0)[..., None]], axis=-1)
        out[nom] = (im.astype(np.uint8), x, y, py)    # py : lignes de bourrage en tete (les `rangs`)
    return out


def quantifier(images):
    """255 couleurs pour toutes les figures (celles des deux resolutions ensemble : une seule
    palette, le moteur ne la remappe qu'une fois).  -> (palette [(r,g,b)] * 256, indexeur)."""
    px = np.concatenate([im[im[..., 3] > 127][:, :3] for ims in images for im, _, _, _ in ims.values()])
    tas = Image.fromarray(px.reshape(1, -1, 3))
    q = tas.quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    pal = np.array(q.getpalette()[:255 * 3], dtype=np.int32).reshape(-1, 3)
    palette = [(0, 0, 0)] + [tuple(int(v) for v in c) for c in pal]
    palette += [(0, 0, 0)] * (256 - len(palette))

    def indexer(im):
        rgb = im[..., :3].reshape(-1, 3).astype(np.int32)
        d = ((rgb[:, None, :] - pal[None, :, :]) ** 2).sum(-1)
        idx = d.argmin(1).astype(np.uint8) + 1
        idx[im[..., 3].reshape(-1) <= 127] = 0
        return idx.reshape(im.shape[:2])
    return palette, indexer


def bloc(man, images, indexer, palette, facteur):
    """Une variante : tuiles, trames, morceaux, sequences, dans le format du moteur."""
    idx = {nom: (indexer(im), x, y, py) for nom, (im, x, y, py) in images.items()}
    tuiles, par_sha = [], {}

    def tuile(px):
        rle = rle8.rle8(bytes(px.reshape(-1)))
        k = hashlib.sha1(rle).digest()
        if k not in par_sha:
            par_sha[k] = len(tuiles)
            tuiles.append(rle)
        return par_sha[k]

    def morceaux(piece):
        """Les morceaux d'une piece, dans l'ordre de dessin : la grille 64 de l'image posee en
        (x, y) + (dx, dy), lignes hors `rangs` transparentes, miroir autour de x = 0 par le
        drapeau du morceau (WALLS.C drawSprites : flags & 1 -> DIR_LRREV) et chunkx = -chunkx - 64
        (rle8.mirror_chunkx_origin, STATIC.C:512-515)."""
        a, x, y, py = idx[piece['img']]
        a = a.copy()
        if 'rangs' in piece:
            r0, r1 = ((v + py) // facteur for v in piece['rangs'])
            a[:r0] = 0
            a[r1:] = 0
        x += piece.get('dx', 0) // facteur
        y += piece.get('dy', 0) // facteur
        h, w = a.shape
        out = []
        for r in range(0, h, CELL):
            for c in range(0, w, CELL):
                cell = np.zeros((CELL, CELL), np.uint8)
                bout = a[r:r + CELL, c:c + CELL]
                cell[:bout.shape[0], :bout.shape[1]] = bout
                if not cell.any():
                    continue                                   # un morceau vide ne coute rien
                cx, cy = x + c, y + r
                if piece['miroir']:
                    out.append((rle8.mirror_chunkx_origin(cx), cy, tuile(cell), 1))
                else:
                    out.append((cx, cy, tuile(cell), 0))
        return out

    trames, chunks, seqs = [], [], []
    anims = [man['animations'][a] for a in ANIMS]
    for anim in anims:
        assert len(anim) == 8, 'une animation orientee a 8 vues'
        for vue in anim:
            seqs.append(len(trames))
            for image in vue:
                trames.append(len(chunks))
                for p in image:
                    chunks += morceaux(p)
    seqs.append(len(trames))                                   # la mort : ses images a la suite
    for image in man['animations'][MORT]:
        trames.append(len(chunks))
        for p in image:
            chunks += morceaux(p)
    seqs.append(len(trames))                                   # la fin (SLEVEL.H : une de plus)
    trames.append(len(chunks))                                 # borne de la derniere trame
    n_trames = len(trames) - 1

    b = bytearray()
    b += struct.pack('>256H', *([0] + [rgb555(*c) for c in palette[1:]]))
    b += struct.pack('>4i', len(tuiles), n_trames + 1, len(chunks), len(seqs))
    for rle in tuiles:
        b += struct.pack('>HH', len(rle), 0) + rle + b'\0' * (-len(rle) % 4)
    for i in range(n_trames + 1):                              # + la borne (chunkIndex de fin)
        b += struct.pack('>hhh2x', trames[i], 0, -1)
    for cx, cy, t, fl in chunks:
        b += struct.pack('>hhhbx', cx, cy, t, fl)
    b += struct.pack('>%dh' % len(seqs), *seqs)
    b += b'\0' * (-len(b) % 4)
    return bytes(b), dict(tuiles=len(tuiles), rle=sum(len(t) for t in tuiles), trames=n_trames,
                          morceaux=len(chunks), sequences=len(seqs) - 1)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--manifeste', default=os.path.join(RACINE, 'build', 'psplayer', 'manifeste.json'))
    ap.add_argument('--sortie', default=os.path.join(RACINE, 'cd', 'PSPLAYER.DAT'))
    a = ap.parse_args(argv)
    man = json.load(open(a.manifeste, encoding='utf-8'))
    dossier = os.path.dirname(a.manifeste)
    variantes = [(2, SCALE_PLEINE * 2), (1, SCALE_PLEINE)]      # la DEMI d'abord (voir l'en-tete)
    images = {f: charger(man, dossier, f) for f, _ in variantes}
    palette, indexer = quantifier(list(images.values()))
    blocs = []
    for f, sc in variantes:
        data, info = bloc(man, images[f], indexer, palette, f)
        blocs.append((sc, data))
        print('%s : %d tuiles (%d o de RLE), %d trames, %d morceaux, %d sequences -> bloc %d o'
              % ('demie' if f == 2 else 'pleine', info['tuiles'], info['rle'], info['trames'],
                 info['morceaux'], info['sequences'], len(data)))
    out = bytearray(b'PSPL') + struct.pack('>ii', 1, len(blocs))
    for sc, data in blocs:
        out += struct.pack('>ii', sc, len(data))
    for _, data in blocs:
        out += data
    tmp = a.sortie + '.tmp'
    with open(tmp, 'wb') as fh:
        fh.write(out)
    os.replace(tmp, a.sortie)
    print('ecrit %s (%d o)' % (a.sortie, len(out)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
